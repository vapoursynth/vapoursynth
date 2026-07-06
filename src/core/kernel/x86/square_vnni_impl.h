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
* AVX-512-VNNI byte square (non-separable) NxN convolution, N in {5,7,9,11} -- a faster
* interior for sq_plane's byte path, dispatched only when the CPU reports VNNI+VBMI and every
* coefficient fits int8. vpdpbusd reduces 4 uint8*int8 products into an int32 lane in one op
* (no byte->word widening); one vpermb builds the 4-consecutive-pixel sliding window per lane,
* and the accumulator is linear (lane i = output j+i) so the store needs no lane un-scramble.
* The integer accumulation is identical to sq_interior_byte and the scale/bias/round/clamp
* matches ISA_AVX512::store_u8, so this is bit-exact with the existing pmaddwd kernel.
*
* Must be included AFTER square_impl.h (reuses sq_mirror and sq_scalar_px). VNNI/VBMI are not
* part of the TU's x86-64-v4 baseline, so each function is pinned with a target attribute (the
* symbol then exists in every variant; the runtime dispatch only calls it on capable CPUs).
*/

#ifndef SQUARE_VNNI_IMPL_H
#define SQUARE_VNNI_IMPL_H

#include <cstdint>
#include <cstddef>
#include <immintrin.h>

#if defined(__clang__) || defined(__GNUC__)
#define VS_TARGET_SQ_VNNI __attribute__((target("avx512vnni,avx512vbmi,avx512bw,avx512dq,avx512vl,avx512f")))
#else
#define VS_TARGET_SQ_VNNI
#endif

namespace {

// SIMD interior: 4 independent 16-output blocks per iteration (single-block + masked tail for the
// remainder). Returns the first column not covered (the scalar edges handle [0,S) and [returned, W)).
template <unsigned N>
VS_TARGET_SQ_VNNI
static unsigned sq_interior_byte_vnni(const uint8_t *const *rows, uint8_t *dst, unsigned S, unsigned W,
                                      ptrdiff_t ss, const int16_t *m, __m512 sc, __m512 bi, __m512 sm)
{
    constexpr unsigned G = (N + 3) / 4;                       // 4-tap groups per row
    alignas(64) uint8_t ib[64];
    for (unsigned i = 0; i < 16; ++i) for (unsigned d = 0; d < 4; ++d) ib[4 * i + d] = static_cast<uint8_t>(i + d);
    const __m512i widx = _mm512_load_si512(ib);               // lane i -> {V[i],V[i+1],V[i+2],V[i+3]}
    uint32_t pk[N][G];                                        // per (row,group): 4 int8 coeffs packed
    for (unsigned r = 0; r < N; ++r) for (unsigned g = 0; g < G; ++g) {
        uint32_t c = 0;
        for (unsigned d = 0; d < 4; ++d) { unsigned k = g * 4 + d; int8_t cv = k < N ? static_cast<int8_t>(m[r * N + k]) : 0; c |= static_cast<uint32_t>(static_cast<uint8_t>(cv)) << (8 * d); }
        pk[r][g] = c;
    }
    const __m512i zero = _mm512_setzero_si512(), v255 = _mm512_set1_epi32(255);
    auto store16 = [&](unsigned col, __m512i acc) {
        __m512 f = _mm512_and_ps(_mm512_fmadd_ps(_mm512_cvtepi32_ps(acc), sc, bi), sm);
        __m512i o = _mm512_min_epi32(_mm512_max_epi32(_mm512_cvtps_epi32(f), zero), v255);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + col), _mm512_cvtepi32_epi8(o));
    };
    unsigned end = W > S ? W - S : 0, j = S;
    constexpr ptrdiff_t maxoff = static_cast<ptrdiff_t>((G - 1) * 4);
    constexpr unsigned B = 4;   // independent 16-output blocks processed per iteration
    // Fast path: B blocks in parallel. The B window loads (rb + g*4 + 16*a) overlap, one shared
    // coefficient broadcast feeds all B, and the B independent vpdpbusd chains expose enough ILP
    // to hide the ~4-cycle latency a single accumulator can't (~1.3-1.9x over one block). Bounded
    // so even the widest load (block B-1, into stride padding) stays inside the row.
    for (; j + 16 * B <= end && static_cast<ptrdiff_t>(j - S) + maxoff + 16 * (B - 1) + 64 <= ss; j += 16 * B) {
        __m512i acc[B];
        for (unsigned a = 0; a < B; ++a) acc[a] = _mm512_setzero_si512();
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *rb = rows[r] + (j - S);
            for (unsigned g = 0; g < G; ++g) {
                __m512i cf = _mm512_set1_epi32(static_cast<int>(pk[r][g]));   // shared across the B blocks
                for (unsigned a = 0; a < B; ++a)
                    acc[a] = _mm512_dpbusd_epi32(acc[a], _mm512_permutexvar_epi8(widx, _mm512_loadu_si512(rb + g * 4 + 16 * a)), cf);
            }
        }
        for (unsigned a = 0; a < B; ++a) store16(j + 16 * a, acc[a]);
    }
    // Single-block fast path for the < B leftover full blocks (plain 64-byte loads into padding).
    for (; j + 16 <= end && static_cast<ptrdiff_t>(j - S) + maxoff + 64 <= ss; j += 16) {
        __m512i acc = _mm512_setzero_si512();
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *rb = rows[r] + (j - S);
            for (unsigned g = 0; g < G; ++g)
                acc = _mm512_dpbusd_epi32(acc, _mm512_permutexvar_epi8(widx, _mm512_loadu_si512(rb + g * 4)), _mm512_set1_epi32(static_cast<int>(pk[r][g])));
        }
        store16(j, acc);
    }
    // Tail: the remaining interior columns, with the window load masked to the row stride so it
    // never reads past the frame. Masked-out lanes read 0 and only ever feed the zero-padded tap,
    // so the result is identical -- this covers the interior right up to the S-wide scalar edge.
    for (; j + 16 <= end; j += 16) {
        __m512i acc = _mm512_setzero_si512();
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *rb = rows[r] + (j - S);
            for (unsigned g = 0; g < G; ++g) {
                ptrdiff_t off = static_cast<ptrdiff_t>(j - S) + g * 4;
                unsigned valid = off < ss ? static_cast<unsigned>(ss - off) : 0;
                __mmask64 lm = valid >= 64 ? ~static_cast<__mmask64>(0) : ((static_cast<__mmask64>(1) << valid) - 1);
                __m512i V = _mm512_maskz_loadu_epi8(lm, rb + g * 4);
                acc = _mm512_dpbusd_epi32(acc, _mm512_permutexvar_epi8(widx, V), _mm512_set1_epi32(static_cast<int>(pk[r][g])));
            }
        }
        store16(j, acc);
    }
    return j;
}

template <unsigned N>
VS_TARGET_SQ_VNNI
static void sq_plane_byte_vnni(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const vs_generic_params &p, unsigned W, unsigned H)
{
    constexpr unsigned S = N / 2;
    __m512 sc = _mm512_set1_ps(p.div), bi = _mm512_set1_ps(p.bias);
    __m512 sm = _mm512_castsi512_ps(_mm512_set1_epi32(p.saturate ? static_cast<int>(0xFFFFFFFF) : 0x7FFFFFFF));
    for (unsigned i = 0; i < H; ++i) {
        const uint8_t *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = reinterpret_cast<const uint8_t *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(sq_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * ss);
        uint8_t *d = reinterpret_cast<uint8_t *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * ds);
        unsigned aend = sq_interior_byte_vnni<N>(rows, d, S, W, ss, p.matrix, sc, bi, sm);
        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = sq_scalar_px<uint8_t, N>(rows, j, S, W, p);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = sq_scalar_px<uint8_t, N>(rows, j, S, W, p);
    }
}

} // namespace

/* Emit the VNNI byte square entry points (7x7/9x9/11x11) for the AVX-512 tier. */
#define VS_SQUARE_VNNI_ENTRY(SZ, N) \
    void vs_generic_##SZ##_conv_byte_avx512vnni(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane_byte_vnni<N>(src, src_stride, dst, dst_stride, *params, width, height); }

#define VS_SQUARE_VNNI_ENTRYPOINTS \
    VS_SQUARE_VNNI_ENTRY(5x5,   5) \
    VS_SQUARE_VNNI_ENTRY(7x7,   7) \
    VS_SQUARE_VNNI_ENTRY(9x9,   9) \
    VS_SQUARE_VNNI_ENTRY(11x11, 11)

#endif // SQUARE_VNNI_IMPL_H
