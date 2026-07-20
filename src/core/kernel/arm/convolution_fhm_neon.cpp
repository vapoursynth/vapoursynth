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
* FEAT_FHM half (binary16) convolution: 3x3, NxN squares, and the 1D
* horizontal/vertical/separable shapes -- fmlal/fmlal2 do the f16->f32
* widening MAC in one instruction, replacing the fcvtl+fmla pairs of the
* baseline half path (half_tap16: 2 loads + 4 converts + 4 FMAs per 16 px per
* tap becomes 2 loads + 4 fmlal). The baseline 3x3 half kernel only ties the
* autovectorised C tier (1.02-1.07x), and it is conversion-bound, which is
* exactly what fmlal removes.
*
* fmlal takes BOTH operands in f16, so the coefficient must survive f32->f16
* narrowing exactly (the dispatcher's conv_f16 gate).
* Under that gate results are identical to the baseline half kernel: the f16
* product is exact in f32 (11x11-bit significands), and fmlal accumulates it
* into f32 with a single rounding, the same as vfmaq_f32 on the converted
* values.
*
* Compiled in its own TU with -march=...+fp16+fp16fml (see meson.build); the
* NEON baseline sources must not be built with +fp16fml.
*/

#include <cstdint>
#include <cstring>
#include "VSHelper4.h"
#include "../generic.h"
#include "conv_scalar.h"
#include "neon_common.h"

namespace {

// Scalar replicate-edge pixel; mirrors conv_plane_3x3_neon's scalar_px for
// half exactly (same accumulation order, same store expression).
inline uint16_t h3x3_scalar_px(const uint16_t *const rows[3], const float *coeffs,
                               unsigned a, unsigned b, unsigned c,
                               float div, float bias, bool saturate)
{
    float accum = 0.0f;
    for (unsigned r = 0; r < 3; ++r) {
        accum += coeffs[r * 3 + 0] * nc_half_to_float(rows[r][a]);
        accum += coeffs[r * 3 + 1] * nc_half_to_float(rows[r][b]);
        accum += coeffs[r * 3 + 2] * nc_half_to_float(rows[r][c]);
    }
    float tmp = accum * div + bias;
    tmp = saturate ? tmp : std::fabs(tmp);
    return nc_float_to_half(tmp);
}

void conv_plane_3x3_half_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                             const vs_generic_params &p, unsigned width, unsigned height)
{
    const float *coeffs = p.matrixf;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);

    // Both fmlal operands are f16; the dispatch gate (conv_f16) guarantees the
    // narrowing is exact. Broadcast once per plane, as the float 3x3 does.
    float16x8_t cf[9];
    for (unsigned i = 0; i < 9; ++i)
        cf[i] = vdupq_n_f16(static_cast<float16_t>(coeffs[i]));

    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? 0 : i - 1;
        unsigned below_idx = i == height - 1 ? height - 1 : i + 1;
        const uint16_t *rows[3] = {
            reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(above_idx) * src_stride),
            reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(i) * src_stride),
            reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(below_idx) * src_stride),
        };
        uint16_t *dstp = reinterpret_cast<uint16_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);

        dstp[0] = h3x3_scalar_px(rows, coeffs, 0, 0, width > 1 ? 1 : 0, p.div, p.bias, p.saturate);

        unsigned end = width > 1 ? width - 1 : 0;
        unsigned j = 1;
        auto block = [&](unsigned jj) {
            float32x4_t a0 = vdupq_n_f32(0), a1 = vdupq_n_f32(0), a2 = vdupq_n_f32(0), a3 = vdupq_n_f32(0);
            for (unsigned r = 0; r < 3; ++r) {
                for (unsigned k = 0; k < 3; ++k) {
                    const uint16_t *q = rows[r] + jj - 1 + k;
                    float16x8_t w = cf[r * 3 + k];
                    float16x8_t x0 = vreinterpretq_f16_u16(vld1q_u16(q));
                    float16x8_t x1 = vreinterpretq_f16_u16(vld1q_u16(q + 8));
                    a0 = vfmlalq_low_f16(a0, x0, w);
                    a1 = vfmlalq_high_f16(a1, x0, w);
                    a2 = vfmlalq_low_f16(a2, x1, w);
                    a3 = vfmlalq_high_f16(a3, x1, w);
                }
            }
            nc_store_h16x8(dstp + jj, a0, a1, sc, bi, sm);
            nc_store_h16x8(dstp + jj + 8, a2, a3, sc, bi, sm);
        };
        if (end > 1) {
            for (; j + 16 <= end; j += 16) block(j);
            if (j < end && end >= 1 + 16) { block(end - 16); j = end; }
            for (; j < end; ++j)
                dstp[j] = h3x3_scalar_px(rows, coeffs, j - 1, j, j + 1, p.div, p.bias, p.saturate);
        }

        if (width > 1)
            dstp[width - 1] = h3x3_scalar_px(rows, coeffs, width - 2, width - 1, width - 1, p.div, p.bias, p.saturate);
    }
}

// ---- half NxN squares (mirror edges, sq_plane semantics), N in {5,7,9,11} ----
// Same interior as sq_interior_half with the fcvtl+fmla pairs replaced by
// fmlal; the per-tap coefficient stays a per-tap ld1r dup (hoisting 25+
// broadcasts is the exact gcc 13.3 spill trap square_neon.cpp documents).

template <unsigned N>
unsigned sq_interior_half_fhm(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                              const float *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    auto block = [&](unsigned jj) {
        float32x4_t a0 = vdupq_n_f32(0), a1 = vdupq_n_f32(0), a2 = vdupq_n_f32(0), a3 = vdupq_n_f32(0);
        for (unsigned r = 0; r < N; ++r) {
            const uint16_t *row = rows[r] + (jj - S);
            for (unsigned k = 0; k < N; ++k) {
                float16x8_t x0 = vreinterpretq_f16_u16(vld1q_u16(row + k));
                float16x8_t x1 = vreinterpretq_f16_u16(vld1q_u16(row + k + 8));
                float16x8_t w = vdupq_n_f16(static_cast<float16_t>(m[r * N + k]));
                a0 = vfmlalq_low_f16(a0, x0, w);
                a1 = vfmlalq_high_f16(a1, x0, w);
                a2 = vfmlalq_low_f16(a2, x1, w);
                a3 = vfmlalq_high_f16(a3, x1, w);
            }
        }
        nc_store_h16x8(dst + jj, a0, a1, sc, bi, sm);
        nc_store_h16x8(dst + jj + 8, a2, a3, sc, bi, sm);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 16 <= end; j += 16) block(j);
    if (j < end && end >= S + 16) { block(end - 16); j = end; }
    return j;
}

template <unsigned N>
void sq_plane_half_fhm(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                       const vs_generic_params &p, unsigned W, unsigned H)
{
    constexpr unsigned S = N / 2;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);

    for (unsigned i = 0; i < H; ++i) {
        const uint16_t *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(src) +
                static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * ss);
        uint16_t *d = reinterpret_cast<uint16_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * ds);

        unsigned aend = sq_interior_half_fhm<N>(rows, d, S, W, p.matrixf, sc, bi, sm);

        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = nc_sq_scalar_px_half<N>(rows, j, S, W, p.matrixf, p.div, p.bias, p.saturate);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = nc_sq_scalar_px_half<N>(rows, j, S, W, p.matrixf, p.div, p.bias, p.saturate);
    }
}

// ---- half 1D horizontal / vertical / separable on fmlal ---------------------
//
// The fmlal counterpart of convolution_neon.cpp's half h/v/x scanlines, and it
// mirrors their structure exactly: templated on a compile-time tap count FW
// (odd 3..25, FW == 0 the runtime fallback), coefficients staged once per
// scanline in lane form, and taps emitted in GROUPS OF 4 with the group loop
// left ROLLED. The group size is the load-bearing part -- widening the group
// past one coefficient vector is what cost the baseline scanlines -10..-20%
// under gcc 13.3 (see convolution_neon.cpp's cv_lanes comment); fmlal changes
// the MAC, not the scheduling budget it has to fit in.
//
// Edge columns/rows go through the same scalar float code as the baseline, so
// only the interior changes -- and there the values are identical too, since
// the conv_f16 gate makes the f16 coefficient equal to the f32 one.

struct fhm_acc16 {
    float32x4_t a0, a1, a2, a3;
    void clear() { a0 = a1 = a2 = a3 = vdupq_n_f32(0); }
};

// fmlal has no widening-multiply form, so unlike the other formats' scanlines
// there is no first-tap vmul init to skip the zero fill with.
inline void fhm_tap16(fhm_acc16 &a, const uint16_t *p, float16x8_t w)
{
    float16x8_t x0 = vreinterpretq_f16_u16(vld1q_u16(p));
    float16x8_t x1 = vreinterpretq_f16_u16(vld1q_u16(p + 8));
    a.a0 = vfmlalq_low_f16(a.a0, x0, w);
    a.a1 = vfmlalq_high_f16(a.a1, x0, w);
    a.a2 = vfmlalq_low_f16(a.a2, x1, w);
    a.a3 = vfmlalq_high_f16(a.a3, x1, w);
}

template <unsigned L>
inline void fhm_tap16_lane(fhm_acc16 &a, const uint16_t *p, float16x4_t c)
{
    float16x8_t x0 = vreinterpretq_f16_u16(vld1q_u16(p));
    float16x8_t x1 = vreinterpretq_f16_u16(vld1q_u16(p + 8));
    a.a0 = vfmlalq_lane_low_f16(a.a0, x0, c, L);
    a.a1 = vfmlalq_lane_high_f16(a.a1, x0, c, L);
    a.a2 = vfmlalq_lane_low_f16(a.a2, x1, c, L);
    a.a3 = vfmlalq_lane_high_f16(a.a3, x1, c, L);
}

// 4 f16 coefficients per lane vector; >= 1 vector so the FW == 0 path can still
// declare the array.
template <unsigned FW>
constexpr unsigned fhm_nq() { return FW ? (FW + 3) / 4 : 1; }

template <unsigned FW>
inline void fhm_stage_coeffs(float16x4_t *cq, const float *m)
{
    constexpr unsigned NQ = fhm_nq<FW>();
    float16_t pad[NQ * 4] = {};
    for (unsigned i = 0; i < FW; ++i)
        pad[i] = static_cast<float16_t>(m[i]);
    for (unsigned i = 0; i < NQ; ++i)
        cq[i] = vld1_f16(pad + 4 * i);
}

// CNT taps from one coefficient vector starting at lane L; tap i reads p + i.
template <unsigned CNT, unsigned L>
inline void fhm_tap_group_h(fhm_acc16 &a, const uint16_t *p, float16x4_t c)
{
    fhm_tap16_lane<L>(a, p + L, c);
    if constexpr (L + 1 < CNT)
        fhm_tap_group_h<CNT, L + 1>(a, p, c);
}

// Same, but tap i reads row pointer srcs[i].
template <unsigned CNT, unsigned L>
inline void fhm_tap_group_v(fhm_acc16 &a, const void *const *srcs, unsigned jj, float16x4_t c)
{
    fhm_tap16_lane<L>(a, static_cast<const uint16_t *>(srcs[L]) + jj, c);
    if constexpr (L + 1 < CNT)
        fhm_tap_group_v<CNT, L + 1>(a, srcs, jj, c);
}

template <unsigned FW>
inline void fhm_taps_h(fhm_acc16 &a, const uint16_t *base, const float16x4_t *cq)
{
    constexpr unsigned NG = FW / 4;
    constexpr unsigned TAIL = FW - NG * 4;
    constexpr unsigned FIRST = FW < 4 ? FW : 4;

    a.clear();
    fhm_tap_group_h<FIRST, 0>(a, base, cq[0]);
    for (unsigned g = 1; g < NG; ++g)
        fhm_tap_group_h<4, 0>(a, base + g * 4, cq[g]);
    if constexpr (TAIL > 0 && FW > 4)
        fhm_tap_group_h<TAIL, 0>(a, base + NG * 4, cq[NG]);
}

template <unsigned FW>
inline void fhm_taps_v(fhm_acc16 &a, const void *const *srcs, unsigned jj, const float16x4_t *cq)
{
    constexpr unsigned NG = FW / 4;
    constexpr unsigned TAIL = FW - NG * 4;
    constexpr unsigned FIRST = FW < 4 ? FW : 4;

    a.clear();
    fhm_tap_group_v<FIRST, 0>(a, srcs, jj, cq[0]);
    for (unsigned g = 1; g < NG; ++g)
        fhm_tap_group_v<4, 0>(a, srcs + g * 4, jj, cq[g]);
    if constexpr (TAIL > 0 && FW > 4)
        fhm_tap_group_v<TAIL, 0>(a, srcs + NG * 4, jj, cq[NG]);
}

// Scalar mirror-edge pixel; replicates conv_scanline_h's formulas verbatim, in
// float with the float coefficients, exactly as the baseline half scanline's
// conv_h_edge_px<Half> does.
inline uint16_t fhm_h_edge_px(const uint16_t *srcp, unsigned j, unsigned width, const vs_generic_params &p)
{
    const float *coeffs = p.matrixf;
    unsigned fwidth = p.matrixsize;
    unsigned support = fwidth / 2;
    unsigned dist_from_right = width - 1 - j;

    float accum = 0.0f;
    for (unsigned k = 0; k < support; ++k) {
        unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
        accum += coeffs[k] * nc_half_to_float(srcp[idx]);
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
        accum += coeffs[k] * nc_half_to_float(srcp[idx]);
    }
    float tmp = accum * p.div + p.bias;
    tmp = p.saturate ? tmp : std::fabs(tmp);
    return nc_float_to_half(tmp);
}

template <unsigned FW>
void fhm_h_scanline(const uint16_t *srcp, uint16_t *dstp, unsigned width,
                    const vs_generic_params &p, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    const float *coeffs = p.matrixf;
    unsigned fwidth = FW ? FW : p.matrixsize;
    unsigned support = fwidth / 2;

    [[maybe_unused]] float16x4_t cq[fhm_nq<FW>()];
    if constexpr (FW)
        fhm_stage_coeffs<FW>(cq, coeffs);

    for (unsigned j = 0; j < std::min(width, support); ++j)
        dstp[j] = fhm_h_edge_px(srcp, j, width, p);

    unsigned end = width - std::min(width, support);
    unsigned j = support;
    auto block = [&](unsigned jj) {
        fhm_acc16 a;
        if constexpr (FW) {
            fhm_taps_h<FW>(a, srcp + jj - support, cq);
        } else {
            a.clear();
            for (unsigned k = 0; k < fwidth; ++k)
                fhm_tap16(a, srcp + jj - support + k, vdupq_n_f16(static_cast<float16_t>(coeffs[k])));
        }
        nc_store_h16x8(dstp + jj, a.a0, a.a1, sc, bi, sm);
        nc_store_h16x8(dstp + jj + 8, a.a2, a.a3, sc, bi, sm);
    };
    if (end > support) {
        for (; j + 16 <= end; j += 16) block(j);
        if (j < end && end >= support + 16) { block(end - 16); j = end; }
        for (; j < end; ++j)
            dstp[j] = fhm_h_edge_px(srcp, j, width, p);
    }

    for (unsigned jj = std::max(support, end); jj < width; ++jj)
        dstp[jj] = fhm_h_edge_px(srcp, jj, width, p);
}

template <unsigned FW>
void fhm_v_scanline(const void *const *srcs, uint16_t *dstp, unsigned width,
                    const vs_generic_params &p, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    const float *coeffs = p.matrixf;
    unsigned fwidth = FW ? FW : p.matrixsize;

    [[maybe_unused]] float16x4_t cq[fhm_nq<FW>()];
    if constexpr (FW)
        fhm_stage_coeffs<FW>(cq, coeffs);

    unsigned j = 0;
    auto block = [&](unsigned jj) {
        fhm_acc16 a;
        if constexpr (FW) {
            fhm_taps_v<FW>(a, srcs, jj, cq);
        } else {
            a.clear();
            for (unsigned k = 0; k < fwidth; ++k)
                fhm_tap16(a, static_cast<const uint16_t *>(srcs[k]) + jj, vdupq_n_f16(static_cast<float16_t>(coeffs[k])));
        }
        nc_store_h16x8(dstp + jj, a.a0, a.a1, sc, bi, sm);
        nc_store_h16x8(dstp + jj + 8, a.a2, a.a3, sc, bi, sm);
    };
    for (; j + 16 <= width; j += 16) block(j);
    if (j < width && width >= 16) { block(width - 16); j = width; }
    for (; j < width; ++j) {
        float accum = 0.0f;
        for (unsigned k = 0; k < fwidth; ++k)
            accum += coeffs[k] * nc_half_to_float(static_cast<const uint16_t *>(srcs[k])[j]);
        float tmp = accum * p.div + p.bias;
        tmp = p.saturate ? tmp : std::fabs(tmp);
        dstp[j] = nc_float_to_half(tmp);
    }
}

// Mirror row-pointer selection of conv_plane_v / conv_plane_x, verbatim.
inline void fhm_v_select_rows(const void *src, ptrdiff_t src_stride, const void *srcp[25],
                              unsigned i, unsigned height, unsigned fwidth)
{
    unsigned support = fwidth / 2;
    unsigned dist_from_bottom = height - 1 - i;

    for (unsigned k = 0; k < support; ++k) {
        unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
        srcp[k] = static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(row) * src_stride;
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
        srcp[k] = static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(row) * src_stride;
    }
}

template <unsigned FW>
void fhm_plane_h_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                      const vs_generic_params &p, unsigned width, unsigned height)
{
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);

    for (unsigned i = 0; i < height; ++i) {
        const uint16_t *srcp = reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(i) * src_stride);
        uint16_t *dstp = reinterpret_cast<uint16_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        fhm_h_scanline<FW>(srcp, dstp, width, p, sc, bi, sm);
    }
}

template <unsigned FW>
void fhm_plane_v_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                      const vs_generic_params &p, unsigned width, unsigned height)
{
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        fhm_v_select_rows(src, src_stride, srcp, i, height, p.matrixsize);
        uint16_t *dstp = reinterpret_cast<uint16_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        fhm_v_scanline<FW>(srcp, dstp, width, p, sc, bi, sm);
    }
}

// Separable: both halves are half-format, so unlike the byte separable (whose
// vertical half stays on vmlal and calls an exported usdot scanline for the
// horizontal half) the whole driver lives here and inlines both fmlal
// scanlines. Quantises through a plane-typed tmp scanline, like conv_plane_x.
template <unsigned FW>
void fhm_plane_x_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                      const vs_generic_params &p, unsigned width, unsigned height)
{
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);

    uint16_t *tmp = static_cast<uint16_t *>(vsh::vsh_aligned_malloc(width * sizeof(uint16_t), 64));
    if (!tmp) {
        // Deterministic zeros rather than whatever the frame allocator handed
        // out; see conv_plane_x_impl.
        for (unsigned i = 0; i < height; ++i)
            std::memset(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride, 0, width * sizeof(uint16_t));
        return;
    }

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        fhm_v_select_rows(src, src_stride, srcp, i, height, p.matrixsize);
        uint16_t *dstp = reinterpret_cast<uint16_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        fhm_v_scanline<FW>(srcp, tmp, width, p, sc, bi, sm);
        fhm_h_scanline<FW>(tmp, dstp, width, p, sc, bi, sm);
    }

    vsh::vsh_aligned_free(tmp);
}

// Same tap-count switch as convolution_neon.cpp's VS_CONV_PLANE_SELECT: runs
// once per plane, FW = 0 kept reachable as the correctness fallback for any
// shape not enumerated here.
#define VS_FHM_PLANE_SELECT(dir)                                                                    \
void fhm_plane_##dir(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,        \
                     const vs_generic_params &p, unsigned width, unsigned height)                   \
{                                                                                                   \
    switch (p.matrixsize) {                                                                         \
    case 3:  return fhm_plane_##dir##_impl<3>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 5:  return fhm_plane_##dir##_impl<5>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 7:  return fhm_plane_##dir##_impl<7>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 9:  return fhm_plane_##dir##_impl<9>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 11: return fhm_plane_##dir##_impl<11>(src, src_stride, dst, dst_stride, p, width, height); \
    case 13: return fhm_plane_##dir##_impl<13>(src, src_stride, dst, dst_stride, p, width, height); \
    case 15: return fhm_plane_##dir##_impl<15>(src, src_stride, dst, dst_stride, p, width, height); \
    case 17: return fhm_plane_##dir##_impl<17>(src, src_stride, dst, dst_stride, p, width, height); \
    case 19: return fhm_plane_##dir##_impl<19>(src, src_stride, dst, dst_stride, p, width, height); \
    case 21: return fhm_plane_##dir##_impl<21>(src, src_stride, dst, dst_stride, p, width, height); \
    case 23: return fhm_plane_##dir##_impl<23>(src, src_stride, dst, dst_stride, p, width, height); \
    case 25: return fhm_plane_##dir##_impl<25>(src, src_stride, dst, dst_stride, p, width, height); \
    default: return fhm_plane_##dir##_impl<0>(src, src_stride, dst, dst_stride, p, width, height);  \
    }                                                                                               \
}

VS_FHM_PLANE_SELECT(h)
VS_FHM_PLANE_SELECT(v)
VS_FHM_PLANE_SELECT(x)

} // namespace

void vs_generic_1d_conv_h_half_neon_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{ fhm_plane_h(src, src_stride, dst, dst_stride, *params, width, height); }

void vs_generic_1d_conv_v_half_neon_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{ fhm_plane_v(src, src_stride, dst, dst_stride, *params, width, height); }

void vs_generic_2d_conv_sep_half_neon_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{ fhm_plane_x(src, src_stride, dst, dst_stride, *params, width, height); }

void vs_generic_3x3_conv_half_neon_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_3x3_half_fhm(src, src_stride, dst, dst_stride, *params, width, height);
}

#define VS_SQUARE_FHM_ENTRY(SZ, N) \
    void vs_generic_##SZ##_conv_half_neon_fhm(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane_half_fhm<N>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SQUARE_FHM_ENTRY(5x5, 5)
VS_SQUARE_FHM_ENTRY(7x7, 7)
VS_SQUARE_FHM_ENTRY(9x9, 9)
VS_SQUARE_FHM_ENTRY(11x11, 11)
