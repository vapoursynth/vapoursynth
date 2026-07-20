/*
* Copyright (c) 2012-2026 Fredrik Mellbin
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
* Shared helpers for the AArch64 NEON convolution kernels.
*
* Store pipeline semantics match the C reference (and the x86 AVX2+ tiers):
* int32/int64 accumulator -> float, fused multiply-add of (div, bias), optional
* fabs via sign-bit mask, round to nearest-even, saturating narrow, min with
* maxval for word. Integer accumulation orders are associative, so the integer
* kernels are bit-exact with kernel/generic.cpp; float kernels use FMA and a
* different summation order, matching the documented x86 AVX2/AVX-512 float
* behaviour (not bit-exact with C by design).
*
* The scalar edge helpers replicate the C reference expressions exactly; on
* AArch64 the reference C and these helpers compile to the same contracted
* fmadd sequence, so edge columns match the C kernels on integer formats.
*/

#ifndef NEON_COMMON_H
#define NEON_COMMON_H

#include <arm_neon.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <type_traits>
#include "conv_scalar.h"

namespace {

// tmp = accum * div + bias (fused); !saturate clears the sign bit (fabs).
inline float32x4_t nc_scale_bias_sat(float32x4_t x, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    float32x4_t t = vfmaq_f32(bi, x, sc);
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(t), sm));
}

inline uint32x4_t nc_satmask(uint8_t saturate)
{
    return vdupq_n_u32(saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu);
}

// 8 byte pixels from two int32x4 accumulators. Signed saturate to i16 then
// unsigned saturate to u8: same clamp as the scalar [0, 255] + lrint sequence.
inline void nc_store_u8x8(uint8_t *dst, int32x4_t lo, int32x4_t hi, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    int32x4_t rlo = vcvtnq_s32_f32(nc_scale_bias_sat(vcvtq_f32_s32(lo), sc, bi, sm));
    int32x4_t rhi = vcvtnq_s32_f32(nc_scale_bias_sat(vcvtq_f32_s32(hi), sc, bi, sm));
    int16x8_t p = vcombine_s16(vqmovn_s32(rlo), vqmovn_s32(rhi));
    vst1_u8(dst, vqmovun_s16(p));
}

// 8 word pixels from two int32x4 accumulators. Unsigned saturate to u16, then
// clamp to maxval for sub-16-bit depths.
inline void nc_store_u16x8(uint16_t *dst, int32x4_t lo, int32x4_t hi, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
    int32x4_t rlo = vcvtnq_s32_f32(nc_scale_bias_sat(vcvtq_f32_s32(lo), sc, bi, sm));
    int32x4_t rhi = vcvtnq_s32_f32(nc_scale_bias_sat(vcvtq_f32_s32(hi), sc, bi, sm));
    uint16x8_t p = vcombine_u16(vqmovun_s32(rlo), vqmovun_s32(rhi));
    vst1q_u16(dst, vminq_u16(p, mv));
}

// int64 -> float32 for the word 9x9/11x11 accumulators: exact via double
// (|x| < 2^34), single rounding to float32 -- identical to the C reference's
// direct int64 -> float conversion.
inline float32x4_t nc_i64x4_to_f32(int64x2_t a, int64x2_t b)
{
    float32x2_t lo = vcvt_f32_f64(vcvtq_f64_s64(a));
    return vcvt_high_f32_f64(lo, vcvtq_f64_s64(b));
}

inline void nc_store_u16x8_i64(uint16_t *dst, int64x2_t a, int64x2_t b, int64x2_t c, int64x2_t d,
                               float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
    int32x4_t rlo = vcvtnq_s32_f32(nc_scale_bias_sat(nc_i64x4_to_f32(a, b), sc, bi, sm));
    int32x4_t rhi = vcvtnq_s32_f32(nc_scale_bias_sat(nc_i64x4_to_f32(c, d), sc, bi, sm));
    uint16x8_t p = vcombine_u16(vqmovun_s32(rlo), vqmovun_s32(rhi));
    vst1q_u16(dst, vminq_u16(p, mv));
}

// float store: scale/bias/sat only, no clamp (matches C float semantics).
inline void nc_store_f32x4(float *dst, float32x4_t acc, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    vst1q_f32(dst, nc_scale_bias_sat(acc, sc, bi, sm));
}

// half store: f32 -> f16 round-to-nearest-even, matching floatToHalf.
inline void nc_store_h16x8(uint16_t *dst, float32x4_t a0, float32x4_t a1, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    float16x4_t lo = vcvt_f16_f32(nc_scale_bias_sat(a0, sc, bi, sm));
    float16x8_t p = vcvt_high_f16_f32(lo, nc_scale_bias_sat(a1, sc, bi, sm));
    vst1q_u16(dst, vreinterpretq_u16_f16(p));
}

// 16 byte pixels widened to two s16x8 (always non-negative).
struct nc_byte16 {
    int16x8_t lo, hi;
};
inline nc_byte16 nc_load_byte16(const uint8_t *p)
{
    uint8x16_t v = vld1q_u8(p);
    return { vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(v))),
             vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(v))) };
}

// 8 word pixels biased into signed range: u16 + INT16_MIN (xor 0x8000).
inline int16x8_t nc_load_word_biased(const uint16_t *p)
{
    return vreinterpretq_s16_u16(veorq_u16(vld1q_u16(p), vdupq_n_u16(0x8000u)));
}

// 8 half pixels widened to two f32x4.
struct nc_half8 {
    float32x4_t lo, hi;
};
inline nc_half8 nc_load_half8(const uint16_t *p)
{
    float16x8_t v = vreinterpretq_f16_u16(vld1q_u16(p));
    return { vcvt_f32_f16(vget_low_f16(v)), vcvt_high_f32_f16(v) };
}

} // namespace

#endif // NEON_COMMON_H
