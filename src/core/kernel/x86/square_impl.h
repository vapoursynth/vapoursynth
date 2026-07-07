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
* Shared, ISA-neutral implementation of the square (non-separable) NxN convolution
* SIMD kernels, N in {5,7,9,11}. The per-ISA translation units define an `ISA`
* traits struct (the same one convolution_impl.h uses) and include this file to
* instantiate the kernels.
*
* Interior columns run SIMD; only the first/last N/2 columns (plus the sub-vector
* interior remainder) use the scalar reference, so the bit-exactness / mirror
* behaviour matches kernel/generic.cpp conv_plane_square exactly. Integer paths pair
* two taps per pmaddwd (a trick the auto-vectoriser does not apply). byte accumulates in
* int32 (never overflows). word at N<=7 accumulates in int32 (worst case N*N*1023*32768 <
* INT32_MAX at 7x7); word at N>=9 sums each row in int32 (a single row is always safe) and
* spills into int64 accumulators, converting int64 -> float exactly at the end -- all
* bit-exact with the C reference. Float uses FMA -> not bit-exact with the scalar/SSE2
* tiers, matching convolution_impl.h's float behaviour.
*/

#ifndef SQUARE_IMPL_H
#define SQUARE_IMPL_H

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <type_traits>
#include "../generic.h"

namespace {

inline unsigned sq_mirror(int pos, int len)
{
    if (pos < 0) pos = -pos - 1;
    else if (pos >= len) pos = 2 * len - 1 - pos;
    if (pos < 0) pos = 0;
    else if (pos >= len) pos = len - 1;
    return static_cast<unsigned>(pos);
}

// One output pixel, scalar -- bit-exact with kernel/generic.cpp conv_plane_square
// (same (tap-column outer, row inner) order, same mirror, same rounding/clamp).
template <class T, unsigned N>
static T sq_scalar_px(const T *const *rows, unsigned j, unsigned S, unsigned W, const vs_generic_params &p)
{
    typedef typename std::conditional<std::is_integral<T>::value,
        typename std::conditional<(sizeof(T) > 1 && N > 5), int64_t, int32_t>::type, float>::type Acc;
    Acc acc = 0;
    for (unsigned k = 0; k < N; ++k) {
        unsigned col = sq_mirror(static_cast<int>(j) + static_cast<int>(k) - static_cast<int>(S), static_cast<int>(W));
        for (unsigned r = 0; r < N; ++r) {
            Acc c = std::is_integral<T>::value ? static_cast<Acc>(p.matrix[r * N + k]) : static_cast<Acc>(p.matrixf[r * N + k]);
            acc += c * static_cast<Acc>(rows[r][col]);
        }
    }
    float tmp = static_cast<float>(acc) * p.div + p.bias;
    tmp = p.saturate ? tmp : std::fabs(tmp);
    if constexpr (std::is_integral<T>::value) {
        float c = std::min(std::max(tmp, 0.0f), static_cast<float>(sizeof(T) == 1 ? 255 : 65535));
        long v = std::lrint(c);
        return static_cast<T>(std::min<long>(v, p.maxval));
    } else {
        return static_cast<T>(tmp);
    }
}

// ---- SIMD interior kernels (process ISA::IPELS int pixels / 2*FLANES float per iter) ----
template <class ISA, unsigned N>
static unsigned sq_interior_byte(const uint8_t *const *rows, uint8_t *dst, unsigned S, unsigned W,
                                 const int16_t *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm)
{
    typedef typename ISA::ivec ivec;
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + ISA::IPELS <= end; j += ISA::IPELS) {
        ivec lo = ISA::zero_i(), hi = ISA::zero_i();
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *row = rows[r] + (j - S);
            unsigned k = 0;
            for (; k + 1 < N; k += 2) {
                ivec x0 = ISA::widen_byte(row + k), x1 = ISA::widen_byte(row + k + 1);
                ivec w = ISA::set1_i32((static_cast<uint16_t>(m[r * N + k + 1]) << 16) | static_cast<uint16_t>(m[r * N + k]));
                lo = ISA::add32(lo, ISA::madd(ISA::unpacklo16(x0, x1), w));
                hi = ISA::add32(hi, ISA::madd(ISA::unpackhi16(x0, x1), w));
            }
            if constexpr (N & 1) {
                ivec x0 = ISA::widen_byte(row + k);
                ivec w = ISA::set1_i32(static_cast<uint16_t>(m[r * N + k]));
                lo = ISA::add32(lo, ISA::madd(ISA::unpacklo16(x0, ISA::zero_i()), w));
                hi = ISA::add32(hi, ISA::madd(ISA::unpackhi16(x0, ISA::zero_i()), w));
            }
        }
        ISA::storeu_u8(dst + j, lo, hi, sc, bi, sm);
    }
    return j;
}

// One run of the int32 word interior: BLK independent IPELS-pixel blocks per iteration. BLK>1
// exposes the ILP that the 2-chain (lo/hi) single-block loop leaves on the table -- the b loop is
// innermost so the BLK blocks' madds issue back to back. Bit-identical to BLK=1 (same integer
// math, only reassociated). Returns the first interior column not covered.
template <class ISA, unsigned N, unsigned BLK>
static unsigned sq_word_i32_run(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned end, unsigned j,
                                const int16_t *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm,
                                typename ISA::ivec mv, typename ISA::ivec bias16, typename ISA::ivec wbv)
{
    typedef typename ISA::ivec ivec;
    for (; j + ISA::IPELS * BLK <= end; j += ISA::IPELS * BLK) {
        ivec lo[BLK], hi[BLK];
        for (unsigned b = 0; b < BLK; ++b) { lo[b] = ISA::zero_i(); hi[b] = ISA::zero_i(); }
        for (unsigned r = 0; r < N; ++r) {
            const uint16_t *rb[BLK];
            for (unsigned b = 0; b < BLK; ++b) rb[b] = rows[r] + (j - S + ISA::IPELS * b);
            unsigned k = 0;
            for (; k + 1 < N; k += 2) {
                ivec w = ISA::set1_i32((static_cast<uint16_t>(m[r * N + k + 1]) << 16) | static_cast<uint16_t>(m[r * N + k]));
                for (unsigned b = 0; b < BLK; ++b) {
                    ivec x0 = ISA::load_word_biased(rb[b] + k, bias16), x1 = ISA::load_word_biased(rb[b] + k + 1, bias16);
                    lo[b] = ISA::add32(lo[b], ISA::madd(ISA::unpacklo16(x0, x1), w));
                    hi[b] = ISA::add32(hi[b], ISA::madd(ISA::unpackhi16(x0, x1), w));
                }
            }
            if constexpr (N & 1) {
                ivec w = ISA::set1_i32(static_cast<uint16_t>(m[r * N + k]));
                for (unsigned b = 0; b < BLK; ++b) {
                    ivec x0 = ISA::load_word_biased(rb[b] + k, bias16);
                    lo[b] = ISA::add32(lo[b], ISA::madd(ISA::unpacklo16(x0, ISA::zero_i()), w));
                    hi[b] = ISA::add32(hi[b], ISA::madd(ISA::unpackhi16(x0, ISA::zero_i()), w));
                }
            }
        }
        for (unsigned b = 0; b < BLK; ++b)
            ISA::storeu_u16(dst + j + ISA::IPELS * b, ISA::sub32(lo[b], wbv), ISA::sub32(hi[b], wbv), sc, bi, sm, mv);
    }
    return j;
}

template <class ISA, unsigned N>
static unsigned sq_interior_word(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                                 const int16_t *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm, typename ISA::ivec mv)
{
    typedef typename ISA::ivec ivec;
    ivec bias16 = ISA::word_bias();
    int32_t wb = 0;
    for (unsigned i = 0; i < N * N; ++i) wb += static_cast<int32_t>(INT16_MIN) * m[i];
    ivec wbv = ISA::set1_i32(static_cast<uint32_t>(wb));
    unsigned end = W > S ? W - S : 0, j = S;
    // 512-bit tier (32 regs): 4 blocks -> ~1.4-1.6x over the 2-chain loop; 256-bit tier stays at 1.
    constexpr unsigned B = ISA::IPELS >= 32 ? 4 : 1;
    if constexpr (B > 1)
        j = sq_word_i32_run<ISA, N, B>(rows, dst, S, end, j, m, sc, bi, sm, mv, bias16, wbv);
    return sq_word_i32_run<ISA, N, 1>(rows, dst, S, end, j, m, sc, bi, sm, mv, bias16, wbv);
}

// Word path for N>=9, where N*N taps overflow int32: each row summed in int32 (always safe),
// spilled into int64 accumulators, exact int64 -> float at the end -- bit-exact with the int64 C
// reference. One run of BLK independent blocks (b loop innermost for ILP). The 64-bit accumulators
// are heavy, so the 512-bit tier uses 2 blocks (4 spills registers past 9x9); 256-bit stays at 1.
template <class ISA, unsigned N, unsigned BLK>
static unsigned sq_word_i64_run(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned end, unsigned j,
                                const int16_t *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm,
                                typename ISA::ivec mv, typename ISA::ivec bias16, typename ISA::ivec wbv)
{
    typedef typename ISA::ivec ivec;
    for (; j + ISA::IPELS * BLK <= end; j += ISA::IPELS * BLK) {
        ivec la[BLK], lb[BLK], ha[BLK], hb[BLK];
        for (unsigned b = 0; b < BLK; ++b) { la[b] = ISA::zero_i(); lb[b] = ISA::zero_i(); ha[b] = ISA::zero_i(); hb[b] = ISA::zero_i(); }
        for (unsigned r = 0; r < N; ++r) {
            const uint16_t *rb[BLK];
            for (unsigned b = 0; b < BLK; ++b) rb[b] = rows[r] + (j - S + ISA::IPELS * b);
            ivec rlo[BLK], rhi[BLK];
            for (unsigned b = 0; b < BLK; ++b) { rlo[b] = ISA::zero_i(); rhi[b] = ISA::zero_i(); }
            unsigned k = 0;
            for (; k + 1 < N; k += 2) {
                ivec w = ISA::set1_i32((static_cast<uint16_t>(m[r * N + k + 1]) << 16) | static_cast<uint16_t>(m[r * N + k]));
                for (unsigned b = 0; b < BLK; ++b) {
                    ivec x0 = ISA::load_word_biased(rb[b] + k, bias16), x1 = ISA::load_word_biased(rb[b] + k + 1, bias16);
                    rlo[b] = ISA::add32(rlo[b], ISA::madd(ISA::unpacklo16(x0, x1), w));
                    rhi[b] = ISA::add32(rhi[b], ISA::madd(ISA::unpackhi16(x0, x1), w));
                }
            }
            if constexpr (N & 1) {
                ivec w = ISA::set1_i32(static_cast<uint16_t>(m[r * N + k]));
                for (unsigned b = 0; b < BLK; ++b) {
                    ivec x0 = ISA::load_word_biased(rb[b] + k, bias16);
                    rlo[b] = ISA::add32(rlo[b], ISA::madd(ISA::unpacklo16(x0, ISA::zero_i()), w));
                    rhi[b] = ISA::add32(rhi[b], ISA::madd(ISA::unpackhi16(x0, ISA::zero_i()), w));
                }
            }
            for (unsigned b = 0; b < BLK; ++b) {
                la[b] = ISA::add64(la[b], ISA::widenlo_i64(rlo[b])); lb[b] = ISA::add64(lb[b], ISA::widenhi_i64(rlo[b]));
                ha[b] = ISA::add64(ha[b], ISA::widenlo_i64(rhi[b])); hb[b] = ISA::add64(hb[b], ISA::widenhi_i64(rhi[b]));
            }
        }
        for (unsigned b = 0; b < BLK; ++b)
            ISA::storeu_u16_i64(dst + j + ISA::IPELS * b, ISA::sub64(la[b], wbv), ISA::sub64(lb[b], wbv),
                                ISA::sub64(ha[b], wbv), ISA::sub64(hb[b], wbv), sc, bi, sm, mv);
    }
    return j;
}

template <class ISA, unsigned N>
static unsigned sq_interior_word_i64(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                                     const int16_t *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm, typename ISA::ivec mv)
{
    typedef typename ISA::ivec ivec;
    ivec bias16 = ISA::word_bias();
    int64_t wb = 0;
    for (unsigned i = 0; i < N * N; ++i) wb += static_cast<int64_t>(INT16_MIN) * m[i];
    ivec wbv = ISA::set1_i64(wb);
    unsigned end = W > S ? W - S : 0, j = S;
    constexpr unsigned B = ISA::IPELS >= 32 ? 2 : 1;
    if constexpr (B > 1)
        j = sq_word_i64_run<ISA, N, B>(rows, dst, S, end, j, m, sc, bi, sm, mv, bias16, wbv);
    return sq_word_i64_run<ISA, N, 1>(rows, dst, S, end, j, m, sc, bi, sm, mv, bias16, wbv);
}

template <class ISA, unsigned N>
static unsigned sq_interior_float(const float *const *rows, float *dst, unsigned S, unsigned W,
                                  const float *m, typename ISA::fvec sc, typename ISA::fvec bi, typename ISA::fvec sm)
{
    typedef typename ISA::fvec fvec;
    constexpr unsigned STEP = 2 * ISA::FLANES;
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + STEP <= end; j += STEP) {
        fvec a0 = ISA::fzero(), a1 = ISA::fzero();
        for (unsigned r = 0; r < N; ++r) {
            const float *row = rows[r] + (j - S);
            for (unsigned k = 0; k < N; ++k) {
                fvec w = ISA::set1_ps(m[r * N + k]);
                a0 = ISA::fmadd(ISA::loadu_ps(row + k), w, a0);
                a1 = ISA::fmadd(ISA::loadu_ps(row + k + ISA::FLANES), w, a1);
            }
        }
        ISA::storeu_ps(dst + j, ISA::scale_bias_sat(a0, sc, bi, sm));
        ISA::storeu_ps(dst + j + ISA::FLANES, ISA::scale_bias_sat(a1, sc, bi, sm));
    }
    return j;
}

// ---- plane driver: scalar edges + SIMD interior ----
template <class ISA, class T, unsigned N>
static void sq_plane(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const vs_generic_params &p, unsigned W, unsigned H)
{
    constexpr unsigned S = N / 2;
    typename ISA::fvec sc = ISA::set1_ps(p.div), bi = ISA::set1_ps(p.bias), sm = ISA::satmask(p.saturate);
    for (unsigned i = 0; i < H; ++i) {
        const T *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = reinterpret_cast<const T *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(sq_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * ss);
        T *d = reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * ds);
        unsigned aend;
        if constexpr (std::is_same<T, uint8_t>::value)
            aend = sq_interior_byte<ISA, N>(rows, d, S, W, p.matrix, sc, bi, sm);
        else if constexpr (std::is_same<T, uint16_t>::value) {
            typename ISA::ivec mv = ISA::word_maxval(p.maxval);
            if constexpr (N > 7)
                aend = sq_interior_word_i64<ISA, N>(rows, d, S, W, p.matrix, sc, bi, sm, mv);
            else
                aend = sq_interior_word<ISA, N>(rows, d, S, W, p.matrix, sc, bi, sm, mv);
        } else
            aend = sq_interior_float<ISA, N>(rows, d, S, W, p.matrixf, sc, bi, sm);
        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = sq_scalar_px<T, N>(rows, j, S, W, p);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = sq_scalar_px<T, N>(rows, j, S, W, p);
    }
}

} // namespace

/* Emit the square SIMD entry points for one ISA tier (byte/float 5..11, word 5/7). */
#define VS_SQUARE_ENTRY(ISA, SUF, SZ, N, T, TN) \
    void vs_generic_##SZ##_conv_##TN##_##SUF(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane<ISA, T, N>(src, src_stride, dst, dst_stride, *params, width, height); }

#define VS_SQUARE_ENTRYPOINTS(ISA, SUF) \
    VS_SQUARE_ENTRY(ISA, SUF, 5x5,   5,  uint8_t,  byte) \
    VS_SQUARE_ENTRY(ISA, SUF, 7x7,   7,  uint8_t,  byte) \
    VS_SQUARE_ENTRY(ISA, SUF, 9x9,   9,  uint8_t,  byte) \
    VS_SQUARE_ENTRY(ISA, SUF, 11x11, 11, uint8_t,  byte) \
    VS_SQUARE_ENTRY(ISA, SUF, 5x5,   5,  float,    float) \
    VS_SQUARE_ENTRY(ISA, SUF, 7x7,   7,  float,    float) \
    VS_SQUARE_ENTRY(ISA, SUF, 9x9,   9,  float,    float) \
    VS_SQUARE_ENTRY(ISA, SUF, 11x11, 11, float,    float) \
    VS_SQUARE_ENTRY(ISA, SUF, 5x5,   5,  uint16_t, word) \
    VS_SQUARE_ENTRY(ISA, SUF, 7x7,   7,  uint16_t, word) \
    VS_SQUARE_ENTRY(ISA, SUF, 9x9,   9,  uint16_t, word) \
    VS_SQUARE_ENTRY(ISA, SUF, 11x11, 11, uint16_t, word)

#endif // SQUARE_IMPL_H
