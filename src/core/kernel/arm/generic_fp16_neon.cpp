/*
* Copyright (c) 2012-2019 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

/*
* FEAT_FP16 native-f16 Minimum/Maximum/Median for the half format: comparisons
* are exact in ANY precision, so doing them in f16 at 8 lanes/vector (instead
* of converting to f32 at 4 lanes like the baseline half kernels) changes no
* min/max/median RESULT bits. The one non-comparison step, the Min/Max
* threshold clamp, rounds a11+-threshold to f16 where the C reference keeps it
* in f32 until the final store -- identical for the default (unlimited) and
* zero thresholds, and at most one f16 ulp apart for finite ones (eps-class,
* same bucket as the accumulation-order deviations on float).
*
* Deflate/Inflate/Prewitt/Sobel half deliberately stay on the f32 kernels:
* their arithmetic accumulates, and per-op f16 rounding would diverge from the
* C reference beyond the contract.
*
* Lives in the FHM static lib (built with +fp16+fp16fml, LTO off) and is
* dispatch-gated on the fhm cpu flag, which is a strict superset of the actual
* FEAT_FP16 requirement (FHM implies FP16); a dedicated asimdhp probe can
* replace the gate if a chip with FP16-but-not-FHM ever matters.
*
* Earns its dispatch under streaming sources at 1 thread and a full pool on
* both primary targets: 2.2-2.5x over the f32 half kernels on M4/clang and
* 3.6-4.5x on Neoverse-V3/gcc (where the scalar half C tier is slowest).
*/

#include <cstdint>
#include <arm_neon.h>

#include "../generic_impl.h"

namespace {

// Minimal float-domain backend for generic_impl.h's MinMax/Median templates
// with fvec = float16x8_t. Only the members those templates touch exist.
struct Backend_F16 {
    typedef float16x8_t fvec;
    static fvec set1_f(float x) { return vdupq_n_f16(static_cast<float16_t>(x)); }
    static fvec fmin(fvec a, fvec b) { return vminq_f16(a, b); }
    static fvec fmax(fvec a, fvec b) { return vmaxq_f16(a, b); }
    static fvec fadd(fvec a, fvec b) { return vaddq_f16(a, b); }
    static fvec fsub(fvec a, fvec b) { return vsubq_f16(a, b); }
};

// Memory adapter for filter_plane_3x3: the plane is raw binary16 (uint16_t),
// loaded/stored as f16 lanes with no conversion at all.
template <class Op>
struct F16Mem : Op {
    using Op::Op;
    typedef uint16_t T;
    static constexpr unsigned vec_len = 8;
    static float16x8_t load(const uint16_t *p) { return vreinterpretq_f16_u16(vld1q_u16(p)); }
    static float16x8_t loadu(const uint16_t *p) { return load(p); }
    static void store(uint16_t *p, float16x8_t x) { vst1q_u16(p, vreinterpretq_u16_f16(x)); }
    static float16x8_t shl_insert_lo(float16x8_t x, uint16_t y)
    {
        return vextq_f16(vreinterpretq_f16_u16(vdupq_n_u16(y)), x, 7);
    }
    static float16x8_t shr_insert(float16x8_t x, uint16_t y, unsigned idx)
    {
        static const uint16_t iota[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        uint16x8_t shifted = vextq_u16(vreinterpretq_u16_f16(x), vdupq_n_u16(0), 1);
        uint16x8_t mask = vceqq_u16(vld1q_u16(iota), vdupq_n_u16(static_cast<uint16_t>(idx)));
        return vreinterpretq_f16_u16(vbslq_u16(mask, vdupq_n_u16(y), shifted));
    }
};

template <uint8_t Stencil, bool Max>
void f16_minmax_fixed(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                      const vs_generic_params &p, unsigned w, unsigned h)
{
    filter_plane_3x3<F16Mem<MinMaxFixedFloat<Backend_F16, Stencil, Max>>>(src, ss, dst, ds, p, w, h);
}

template <bool Max>
void f16_minmax(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                const vs_generic_params &p, unsigned w, unsigned h)
{
    switch (p.stencil) {
    case STENCIL_H: f16_minmax_fixed<STENCIL_H, Max>(src, ss, dst, ds, p, w, h); break;
    case STENCIL_V: f16_minmax_fixed<STENCIL_V, Max>(src, ss, dst, ds, p, w, h); break;
    case STENCIL_PLUS: f16_minmax_fixed<STENCIL_PLUS, Max>(src, ss, dst, ds, p, w, h); break;
    case STENCIL_ALL: f16_minmax_fixed<STENCIL_ALL, Max>(src, ss, dst, ds, p, w, h); break;
    default: filter_plane_3x3<F16Mem<MinMaxFloat<Backend_F16, Max>>>(src, ss, dst, ds, p, w, h); break;
    }
}

} // namespace

void vs_generic_3x3_min_half_neon_fp16(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{
    f16_minmax<false>(src, ss, dst, ds, *p, w, h);
}

void vs_generic_3x3_max_half_neon_fp16(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{
    f16_minmax<true>(src, ss, dst, ds, *p, w, h);
}

void vs_generic_3x3_median_half_neon_fp16(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{
    filter_plane_3x3<F16Mem<MedianFloat<Backend_F16>>>(src, ss, dst, ds, *p, w, h);
}
