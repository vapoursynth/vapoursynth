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
* Non-streaming SVE (VLA) convolution kernels: square NxN (3x3..11x11),
* 1D horizontal/vertical and separable, for byte/word/float. Never built on
* Apple platforms (no non-streaming SVE there) and requires only base SVE
* (Graviton3 / Neoverse V1 upward; SVE2 not assumed).
*
* Everything runs at int32/float32 lane density with predicated loads and
* stores, so there is no scalar interior tail; only the mirror/replicate edge
* columns use the scalar reference paths.
*
* Integer math: pixels widen to 32-bit lanes on load. byte accumulates
* unbiased (worst case 121 * 1023 * 255 fits int32). word subtracts 32768 on
* load; the biased worst case fits int32 through 7x7 and 1D (25 taps), and
* the coefficient*bias sum is added back exactly, so results are bit-exact
* with the C reference. word 9x9/11x11 sums each row in int32 and spills to
* int64 lanes (unpklo/unpkhi), matching the int64 C reference bit-exactly.
* float uses fused MLA (not bit-exact with C, as on the other SIMD tiers).
*/

#include <arm_sve.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../generic.h"
#include "conv_scalar.h"

namespace {

enum class SvType { Byte, Word, Float };

template <SvType TY>
struct sv_traits;
template <> struct sv_traits<SvType::Byte>  { typedef uint8_t T; typedef int32_t Acc; typedef int16_t Weight; };
template <> struct sv_traits<SvType::Word>  { typedef uint16_t T; typedef int32_t Acc; typedef int16_t Weight; };
template <> struct sv_traits<SvType::Float> { typedef float T; typedef float Acc; typedef float Weight; };

template <SvType TY>
inline const typename sv_traits<TY>::Weight *sv_coeffs(const vs_generic_params &p)
{
    if constexpr (TY == SvType::Float)
        return p.matrixf;
    else
        return p.matrix;
}

// Pixel load widened to 32-bit lanes; word pixels come back biased by -32768.
template <SvType TY>
inline svint32_t sv_load_px(svbool_t pg, const typename sv_traits<TY>::T *p)
{
    if constexpr (TY == SvType::Byte)
        return svreinterpret_s32_u32(svld1ub_u32(pg, p));
    else
        return svsub_n_s32_x(pg, svreinterpret_s32_u32(svld1uh_u32(pg, p)), 32768);
}

// scale/bias/saturate + round + clamp + narrowing store of one int32 vector.
template <SvType TY>
inline void sv_store_int(typename sv_traits<TY>::T *dst, svbool_t pg, svint32_t acc, int32_t weight_bias,
                         float div, float bias, uint32_t satmask, int32_t maxclamp)
{
    svbool_t pt = svptrue_b32();
    acc = svsub_n_s32_x(pt, acc, weight_bias);
    svfloat32_t f = svcvt_f32_s32_x(pt, acc);
    f = svmad_n_f32_x(pt, f, svdup_f32(div), bias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt, svreinterpret_u32_f32(f), satmask));
    f = svrintn_f32_x(pt, f);
    svint32_t r = svcvt_s32_f32_x(pt, f);
    r = svmax_n_s32_x(pt, r, 0);
    r = svmin_n_s32_x(pt, r, maxclamp);
    if constexpr (TY == SvType::Byte)
        svst1b_s32(pg, reinterpret_cast<int8_t *>(dst), r);
    else
        svst1h_s32(pg, reinterpret_cast<int16_t *>(dst), r);
}

inline void sv_store_float(float *dst, svbool_t pg, svfloat32_t acc, float div, float bias, uint32_t satmask)
{
    svbool_t pt = svptrue_b32();
    svfloat32_t f = svmad_n_f32_x(pt, acc, svdup_f32(div), bias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt, svreinterpret_u32_f32(f), satmask));
    svst1_f32(pg, dst, f);
}

// Store one int64-accumulated half (svcntd pixels) of a word vector strip.
// int64 -> float32 lands in the even float lane of each doubleword; the f32
// pipeline runs on all lanes (odd lanes garbage) and st1h writes the low 16
// bits of each doubleword.
inline void sv_store_word_i64_half(uint16_t *dst, unsigned valid, svint64_t acc, int64_t weight_bias,
                                   float div, float bias, uint32_t satmask, int32_t maxclamp)
{
    if (!valid)
        return;
    svbool_t pt32 = svptrue_b32();
    svbool_t pt64 = svptrue_b64();
    svbool_t pgd = svwhilelt_b64_u32(0, valid);
    acc = svsub_n_s64_x(pt64, acc, weight_bias);
    svfloat32_t f = svcvt_f32_s64_x(pt64, acc);
    f = svmad_n_f32_x(pt32, f, svdup_f32(div), bias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt32, svreinterpret_u32_f32(f), satmask));
    f = svrintn_f32_x(pt32, f);
    svint32_t r = svcvt_s32_f32_x(pt32, f);
    r = svmax_n_s32_x(pt32, r, 0);
    r = svmin_n_s32_x(pt32, r, maxclamp);
    svst1h_s64(pgd, reinterpret_cast<int16_t *>(dst), svreinterpret_s64_s32(r));
}

template <SvType TY>
inline int32_t sv_word_bias_sum(const typename sv_traits<TY>::Weight *coeffs, unsigned n)
{
    if constexpr (TY == SvType::Word) {
        int32_t weight_bias = 0;
        for (unsigned i = 0; i < n; ++i)
            weight_bias += -32768 * static_cast<int32_t>(coeffs[i]);
        return weight_bias;
    } else {
        (void)coeffs; (void)n;
        return 0;
    }
}

// ---- square NxN ---------------------------------------------------------------

template <SvType TY>
typename sv_traits<TY>::T sv_sq_edge_px(const typename sv_traits<TY>::T *const *rows, unsigned j, unsigned N, unsigned W,
                                        const vs_generic_params &p)
{
    if constexpr (TY == SvType::Byte) {
        return nc_sq_scalar_px_rt<uint8_t, int32_t, int16_t>(rows, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    } else if constexpr (TY == SvType::Word) {
        if (N > 5)
            return nc_sq_scalar_px_rt<uint16_t, int64_t, int16_t>(rows, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
        else
            return nc_sq_scalar_px_rt<uint16_t, int32_t, int16_t>(rows, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    } else {
        return nc_sq_scalar_px_rt<float, float, float>(rows, j, N, W, p.matrixf, p.div, p.bias, p.saturate, p.maxval);
    }
}

template <SvType TY>
void sv_sq_plane(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                 const vs_generic_params &p, unsigned W, unsigned H, unsigned N)
{
    typedef typename sv_traits<TY>::T T;
    const auto *m = sv_coeffs<TY>(p);
    const unsigned S = N / 2;
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    const unsigned lanes32 = static_cast<unsigned>(svcntw());
    const bool word64 = TY == SvType::Word && N > 7;
    const unsigned Wend = W > S ? W - S : 0;

    int32_t weight_bias32 = sv_word_bias_sum<TY>(m, N * N);
    int64_t weight_bias64 = 0;
    if (word64)
        for (unsigned i = 0; i < N * N; ++i) weight_bias64 += -32768 * static_cast<int64_t>(m[i]);

    std::vector<const uint8_t *> rows_all(H + 2 * S);
    for (unsigned t = 0; t < H + 2 * S; ++t)
        rows_all[t] = static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(t) - static_cast<int>(S), static_cast<int>(H))) * src_stride;

    for (unsigned i = 0; i < H; ++i) {
        const T *const *rows = reinterpret_cast<const T *const *>(rows_all.data() + i);
        T *d = reinterpret_cast<T *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);

        for (unsigned j = S; j < Wend; j += lanes32) {
            unsigned valid = std::min(lanes32, Wend - j);
            svbool_t pg = svwhilelt_b32_u32(0, valid);
            if constexpr (TY == SvType::Float) {
                svfloat32_t acc = svdup_f32(0.0f);
                for (unsigned r = 0; r < N; ++r) {
                    const float *row = rows[r] + (j - S);
                    for (unsigned k = 0; k < N; ++k)
                        acc = svmla_n_f32_x(svptrue_b32(), acc, svld1_f32(pg, row + k), m[r * N + k]);
                }
                sv_store_float(d + j, pg, acc, p.div, p.bias, satmask);
            } else if (word64) {
                svint64_t accum_lo = svdup_s64(0), accum_hi = svdup_s64(0);
                for (unsigned r = 0; r < N; ++r) {
                    const T *row = rows[r] + (j - S);
                    svint32_t row_sum = svdup_s32(0);
                    for (unsigned k = 0; k < N; ++k)
                        row_sum = svmla_n_s32_x(svptrue_b32(), row_sum, sv_load_px<TY>(pg, row + k), m[r * N + k]);
                    accum_lo = svadd_s64_x(svptrue_b64(), accum_lo, svunpklo_s64(row_sum));
                    accum_hi = svadd_s64_x(svptrue_b64(), accum_hi, svunpkhi_s64(row_sum));
                }
                unsigned lanes64 = static_cast<unsigned>(svcntd());
                uint16_t *dw = reinterpret_cast<uint16_t *>(d) + j;
                sv_store_word_i64_half(dw, std::min(valid, lanes64), accum_lo, weight_bias64, p.div, p.bias, satmask, p.maxval);
                sv_store_word_i64_half(dw + lanes64, valid > lanes64 ? valid - lanes64 : 0, accum_hi, weight_bias64, p.div, p.bias, satmask, p.maxval);
            } else {
                svint32_t acc = svdup_s32(0);
                for (unsigned r = 0; r < N; ++r) {
                    const T *row = rows[r] + (j - S);
                    for (unsigned k = 0; k < N; ++k)
                        acc = svmla_n_s32_x(svptrue_b32(), acc, sv_load_px<TY>(pg, row + k), m[r * N + k]);
                }
                sv_store_int<TY>(d + j, pg, acc, weight_bias32, p.div, p.bias, satmask, p.maxval);
            }
        }

        // Mirror edge columns via the scalar reference.
        const unsigned edge = std::min(W, S);
        for (unsigned j = 0; j < edge; ++j)
            d[j] = sv_sq_edge_px<TY>(rows, j, N, W, p);
        for (unsigned j = std::max(S, W > edge ? W - edge : 0); j < W; ++j)
            d[j] = sv_sq_edge_px<TY>(rows, j, N, W, p);
    }
}

// ---- square NxN, word, svdot_s64 ----------------------------------------------
//
// SDOT with 16-bit operands reduces 4 int16 products into a 64-bit lane in one
// op, so one svdot retires svcntd() outputs x 4 taps = 8 MACs at a 128-bit VL
// where vmlal_s16 (and the svmla u32-lane form above) retire 4. svtbl builds the
// sliding window, exactly as the usdot byte kernels do, one 16-bit element at a
// time: element i of the table result feeds 64-bit lane i/4 at tap i%4, so it
// must come from pixel (i/4 + i%4).
//
// Measured against the NEON vmlal word kernels on a 25-tap row: +60% at 128-bit
// (Graviton4) and +118% at 256-bit (Graviton3). It is the only integer SVE form
// that beats NEON at 128 bits, so unlike the rest of this file it is dispatched
// regardless of vector length.
//
// Bit-exact: pixels are biased by -32768 into int16 exactly as sv_load_px does,
// the int64 sum is exact (121 * 32768 * 1023 fits easily), and the accumulated
// coefficient*bias term is removed by the shared word store. Taps are zero-padded
// to a multiple of 4, so the padding contributes nothing and needs no bias term.

void sv_sq_word_dot_plane(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                          const vs_generic_params &p, unsigned W, unsigned H, unsigned N)
{
    const unsigned S = N / 2;
    const unsigned G = (N + 3) / 4;
    const unsigned lanes64 = static_cast<unsigned>(svcntd());   // outputs per vector
    const unsigned lanes16 = static_cast<unsigned>(svcnth());
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    const int16_t *m = p.matrix;

    std::vector<uint16_t> idxbuf(lanes16);
    for (unsigned i = 0; i < lanes16; ++i)
        idxbuf[i] = static_cast<uint16_t>(i / 4 + i % 4);
    const svuint16_t vidx = svld1_u16(svptrue_b16(), idxbuf.data());
    const svbool_t pgld = svwhilelt_b16_u32(0u, lanes64 + 3u);

    std::vector<int64_t> coeff_groups(static_cast<size_t>(N) * G);
    for (unsigned r = 0; r < N; ++r) {
        for (unsigned g = 0; g < G; ++g) {
            int16_t b[4];
            for (unsigned t = 0; t < 4; ++t) {
                const unsigned k = 4 * g + t;
                b[t] = k < N ? m[r * N + k] : static_cast<int16_t>(0);
            }
            std::memcpy(&coeff_groups[static_cast<size_t>(r) * G + g], b, 8);
        }
    }

    int64_t weight_bias = 0;
    for (unsigned i = 0; i < N * N; ++i)
        weight_bias += -32768 * static_cast<int64_t>(m[i]);

    const unsigned Wend = W > S ? W - S : 0;
    // Furthest element the window touches is row[(j - S) + 4(G-1) + lanes64 + 2];
    // past this the scalar edge takes over rather than read off the row.
    const long maxjj = static_cast<long>(W) + static_cast<long>(S) - 4L * static_cast<long>(G) - static_cast<long>(lanes64);

    std::vector<const uint8_t *> rows_all(H + 2 * S);
    for (unsigned t = 0; t < H + 2 * S; ++t)
        rows_all[t] = static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(t) - static_cast<int>(S), static_cast<int>(H))) * src_stride;

    for (unsigned i = 0; i < H; ++i) {
        const uint16_t *const *rows = reinterpret_cast<const uint16_t *const *>(rows_all.data() + i);
        uint16_t *d = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);

        unsigned j = S;
        for (; j + lanes64 <= Wend && static_cast<long>(j) <= maxjj; j += lanes64) {
            svint64_t acc = svdup_s64(0);
            for (unsigned r = 0; r < N; ++r) {
                const uint16_t *row = rows[r] + (j - S);
                for (unsigned g = 0; g < G; ++g) {
                    svuint16_t pu = svld1_u16(pgld, row + 4 * g);
                    svint16_t x = svreinterpret_s16_u16(svsub_n_u16_x(pgld, pu, 32768));
                    svint16_t t = svtbl_s16(x, vidx);
                    svint16_t w = svreinterpret_s16_s64(svdup_n_s64(coeff_groups[static_cast<size_t>(r) * G + g]));
                    acc = svdot_s64(acc, t, w);
                }
            }
            sv_store_word_i64_half(d + j, lanes64, acc, weight_bias, p.div, p.bias, satmask, p.maxval);
        }

        for (unsigned e = 0; e < S && e < W; ++e)
            d[e] = nc_sq_scalar_px_rt<uint16_t, int64_t, int16_t>(rows, e, N, W, m, p.div, p.bias, p.saturate, p.maxval);
        for (unsigned e = (j > S ? j : S); e < W; ++e)
            d[e] = nc_sq_scalar_px_rt<uint16_t, int64_t, int16_t>(rows, e, N, W, m, p.div, p.bias, p.saturate, p.maxval);
    }
}

// ---- 1D -----------------------------------------------------------------------

// Scalar mirror-edge pixel replicating conv_scanline_h verbatim.
template <SvType TY>
typename sv_traits<TY>::T sv_h_edge_px(const typename sv_traits<TY>::T *srcp, unsigned j, unsigned width,
                                       const vs_generic_params &p)
{
    typedef typename sv_traits<TY>::Acc Acc;
    const auto *coeffs = sv_coeffs<TY>(p);
    unsigned fwidth = p.matrixsize;
    unsigned support = fwidth / 2;
    unsigned dist_from_right = width - 1 - j;

    Acc accum = 0;
    for (unsigned k = 0; k < support; ++k) {
        unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
        accum += coeffs[k] * static_cast<Acc>(srcp[idx]);
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
        accum += coeffs[k] * static_cast<Acc>(srcp[idx]);
    }
    float tmp = static_cast<float>(accum) * p.div + p.bias;
    tmp = p.saturate ? tmp : std::fabs(tmp);
    if constexpr (TY == SvType::Float) {
        return tmp;
    } else {
        float c = std::min(std::max(tmp, 0.0f), static_cast<float>(sizeof(typename sv_traits<TY>::T) == 1 ? 255 : 65535));
        long v = std::lrint(c);
        return static_cast<typename sv_traits<TY>::T>(std::min<long>(v, p.maxval));
    }
}

template <SvType TY>
void sv_h_scanline(const typename sv_traits<TY>::T *srcp, typename sv_traits<TY>::T *dstp, unsigned width,
                   const vs_generic_params &p, uint32_t satmask, int32_t weight_bias)
{
    const auto *coeffs = sv_coeffs<TY>(p);
    unsigned fwidth = p.matrixsize;
    unsigned support = fwidth / 2;
    const unsigned lanes32 = static_cast<unsigned>(svcntw());

    for (unsigned j = 0; j < std::min(width, support); ++j)
        dstp[j] = sv_h_edge_px<TY>(srcp, j, width, p);

    unsigned end = width - std::min(width, support);
    for (unsigned j = support; j < end; j += lanes32) {
        unsigned valid = std::min(lanes32, end - j);
        svbool_t pg = svwhilelt_b32_u32(0, valid);
        if constexpr (TY == SvType::Float) {
            svfloat32_t acc = svdup_f32(0.0f);
            for (unsigned k = 0; k < fwidth; ++k)
                acc = svmla_n_f32_x(svptrue_b32(), acc, svld1_f32(pg, srcp + j - support + k), coeffs[k]);
            sv_store_float(dstp + j, pg, acc, p.div, p.bias, satmask);
        } else {
            svint32_t acc = svdup_s32(0);
            for (unsigned k = 0; k < fwidth; ++k)
                acc = svmla_n_s32_x(svptrue_b32(), acc, sv_load_px<TY>(pg, srcp + j - support + k), coeffs[k]);
            sv_store_int<TY>(dstp + j, pg, acc, weight_bias, p.div, p.bias, satmask, p.maxval);
        }
    }

    for (unsigned j = std::max(support, end); j < width; ++j)
        dstp[j] = sv_h_edge_px<TY>(srcp, j, width, p);
}

template <SvType TY>
void sv_v_scanline(const void *const *srcs, typename sv_traits<TY>::T *dstp, unsigned width,
                   const vs_generic_params &p, uint32_t satmask, int32_t weight_bias)
{
    typedef typename sv_traits<TY>::T T;
    const auto *coeffs = sv_coeffs<TY>(p);
    unsigned fwidth = p.matrixsize;
    const unsigned lanes32 = static_cast<unsigned>(svcntw());

    for (unsigned j = 0; j < width; j += lanes32) {
        unsigned valid = std::min(lanes32, width - j);
        svbool_t pg = svwhilelt_b32_u32(0, valid);
        if constexpr (TY == SvType::Float) {
            svfloat32_t acc = svdup_f32(0.0f);
            for (unsigned k = 0; k < fwidth; ++k)
                acc = svmla_n_f32_x(svptrue_b32(), acc, svld1_f32(pg, static_cast<const float *>(srcs[k]) + j), coeffs[k]);
            sv_store_float(dstp + j, pg, acc, p.div, p.bias, satmask);
        } else {
            svint32_t acc = svdup_s32(0);
            for (unsigned k = 0; k < fwidth; ++k)
                acc = svmla_n_s32_x(svptrue_b32(), acc, sv_load_px<TY>(pg, static_cast<const T *>(srcs[k]) + j), coeffs[k]);
            sv_store_int<TY>(dstp + j, pg, acc, weight_bias, p.div, p.bias, satmask, p.maxval);
        }
    }
}

// Row-pointer selection replicating conv_plane_v / conv_plane_x verbatim.
inline void sv_select_rows(const void *src, ptrdiff_t src_stride, const void *srcp[25],
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

template <SvType TY>
void sv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename sv_traits<TY>::T T;
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    int32_t weight_bias = sv_word_bias_sum<TY>(sv_coeffs<TY>(p), p.matrixsize);
    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = reinterpret_cast<const T *>(static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(i) * src_stride);
        T *dstp = reinterpret_cast<T *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        sv_h_scanline<TY>(srcp, dstp, width, p, satmask, weight_bias);
    }
}

template <SvType TY>
void sv_plane_x(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename sv_traits<TY>::T T;
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    int32_t weight_bias = sv_word_bias_sum<TY>(sv_coeffs<TY>(p), p.matrixsize);
    std::vector<T> tmp(width);
    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        sv_select_rows(src, src_stride, srcp, i, height, p.matrixsize);
        T *dstp = reinterpret_cast<T *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        sv_v_scanline<TY>(srcp, tmp.data(), width, p, satmask, weight_bias);
        sv_h_scanline<TY>(tmp.data(), dstp, width, p, satmask, weight_bias);
    }
}

// ---- 3x3 (replicate edges, filter_plane_3x3 semantics) -------------------------

template <SvType TY>
void sv_plane_3x3(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                  const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename sv_traits<TY>::T T;
    typedef typename sv_traits<TY>::Acc Acc;
    const auto *coeffs = sv_coeffs<TY>(p);
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    int32_t weight_bias = sv_word_bias_sum<TY>(coeffs, 9);
    const unsigned lanes32 = static_cast<unsigned>(svcntw());

    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? 0 : i - 1;
        unsigned below_idx = i == height - 1 ? height - 1 : i + 1;
        const T *rows[3] = {
            reinterpret_cast<const T *>(static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(above_idx) * src_stride),
            reinterpret_cast<const T *>(static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(i) * src_stride),
            reinterpret_cast<const T *>(static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(below_idx) * src_stride),
        };
        T *dstp = reinterpret_cast<T *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);

        auto scalar_px = [&](unsigned a, unsigned b, unsigned c) -> T {
            Acc accum = 0;
            for (unsigned r = 0; r < 3; ++r) {
                accum += coeffs[r * 3 + 0] * static_cast<Acc>(rows[r][a]);
                accum += coeffs[r * 3 + 1] * static_cast<Acc>(rows[r][b]);
                accum += coeffs[r * 3 + 2] * static_cast<Acc>(rows[r][c]);
            }
            float tmp = static_cast<float>(accum) * p.div + p.bias;
            tmp = p.saturate ? tmp : std::fabs(tmp);
            if constexpr (TY == SvType::Float) {
                return tmp;
            } else {
                float cl = std::min(std::max(tmp, 0.0f), static_cast<float>(sizeof(T) == 1 ? 255 : 65535));
                long v = std::lrint(cl);
                return static_cast<T>(std::min<long>(v, p.maxval));
            }
        };

        dstp[0] = scalar_px(0, 0, width > 1 ? 1 : 0);

        unsigned end = width > 1 ? width - 1 : 0;
        for (unsigned j = 1; j < end; j += lanes32) {
            unsigned valid = std::min(lanes32, end - j);
            svbool_t pg = svwhilelt_b32_u32(0, valid);
            if constexpr (TY == SvType::Float) {
                svfloat32_t acc = svdup_f32(0.0f);
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned k = 0; k < 3; ++k)
                        acc = svmla_n_f32_x(svptrue_b32(), acc, svld1_f32(pg, rows[r] + j - 1 + k), coeffs[r * 3 + k]);
                sv_store_float(dstp + j, pg, acc, p.div, p.bias, satmask);
            } else {
                svint32_t acc = svdup_s32(0);
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned k = 0; k < 3; ++k)
                        acc = svmla_n_s32_x(svptrue_b32(), acc, sv_load_px<TY>(pg, rows[r] + j - 1 + k), coeffs[r * 3 + k]);
                sv_store_int<TY>(dstp + j, pg, acc, weight_bias, p.div, p.bias, satmask, p.maxval);
            }
        }

        if (width > 1)
            dstp[width - 1] = scalar_px(width - 2, width - 1, width - 1);
    }
}

} // namespace

// Runtime SVE vector length in bytes, for the dispatcher (which is not
// compiled with +sve and so cannot use svcntb itself). Only called after the
// CPU is known to report SVE.
unsigned vs_sve_vector_length(void)
{
    return static_cast<unsigned>(svcntb());
}

#define VS_SVE_SQUARE_ENTRY(SZ, N, TY, TN) \
    void vs_generic_##SZ##_conv_##TN##_sve(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sv_sq_plane<SvType::TY>(src, src_stride, dst, dst_stride, *params, width, height, N); }

// Only the shapes that beat the round-2 NEON tier on Graviton3 survive: byte
// plain squares through 7x7 (9x9/11x11 wide lost to the NEON lane-coefficient
// squares), and word svdot 9x9/11x11 (7x7 became a wash). The plain word
// squares and the float squares (9x9/11x11) all lost to NEON and are gone.
VS_SVE_SQUARE_ENTRY(5x5,   5,  Byte,  byte)
VS_SVE_SQUARE_ENTRY(7x7,   7,  Byte,  byte)
#define VS_SVE_SQUARE_DOT_ENTRY(SZ, N) \
    void vs_generic_##SZ##_conv_word_sve_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sv_sq_word_dot_plane(src, src_stride, dst, dst_stride, *params, width, height, N); }

VS_SVE_SQUARE_DOT_ENTRY(9x9,   9)
VS_SVE_SQUARE_DOT_ENTRY(11x11, 11)

#define VS_SVE_ENTRY(KERNEL, FN, TY, TN) \
    void vs_generic_##KERNEL##_##TN##_sve(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { FN<SvType::TY>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SVE_ENTRY(3x3_conv, sv_plane_3x3, Byte, byte)
VS_SVE_ENTRY(3x3_conv, sv_plane_3x3, Word, word)

VS_SVE_ENTRY(1d_conv_h, sv_plane_h, Byte, byte)
// 2d_conv_sep byte pruned -> the round-2 usdot NEON separable wins on Graviton3.
