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
* NEON square (non-separable) NxN convolution, N in {3,5,7,9,11}.
*
* Same structure as the x86 square_impl.h kernels: scalar mirror-edge columns,
* SIMD interior, and an overlapping final vector to cover the sub-vector
* interior remainder (re-storing already-written columns is safe because the
* SIMD interior is bit-exact with the scalar reference on integer formats).
*
* byte:  u8 widens to s16 (non-negative), per-tap vmlal_s16 into int32.
*        Worst case 121 * 1023 * 255 < INT32_MAX -- never overflows.
* word:  u16 biased by INT16_MIN into s16, per-tap vmlal_s16 into int32.
*        N <= 7: 49 * 1023 * 32768 < INT32_MAX; the coefficient*bias sum is
*        subtracted afterwards (exact), so results match the C reference.
*        N >= 9: per-row int32 sums (11 * 1023 * 32768 fits) spill into int64
*        accumulators; int64 -> float via double is exact, bit-identical to
*        the int64 C reference.
* float: per-tap FMA; like the x86 AVX2+ tiers this is not bit-exact with the
*        non-FMA scalar reference (allowed for float).
* half:  samples widen to float32, float math, narrow on store.
*
* The 3x3 square entry (vs_generic_3x3_conv_*) uses replicate edges and a
* separate driver (see convolution_neon.cpp); this file is 5x5 and up.
*/

#include <cstdint>
#include "../generic.h"
#include "neon_common.h"

namespace {

// ---- per-row lane-form coefficients (integer squares) ----
//
// The obvious way to feed a coefficient to vmlal_s16 is vdup_n_s16(m[...]),
// but the compiler cannot hoist that broadcast out of the block: p.matrix is
// caller memory and the block stores through dst, which may alias it, so every
// tap of every 16-px block re-emits an address `sub` plus an `ld1r.8h`
// (verified in the emitted asm). That is 2 of the ~10 ops per tap, one of them
// a load.
//
// vmlal_laneq_s16 instead reads the coefficient from a lane of a register, so
// the matrix can be loaded once per scanline. Coefficients are staged per ROW,
// not per matrix: RQ = ceil(N/8) vectors hold one row's taps (tap k -> vector
// k/8, lane k%8), and the row loop stays rolled, so only one row's coefficients
// and loads are in flight at a time.
//
// Staging the WHOLE matrix and unrolling all N*N taps was measurably worse:
// it lets the scheduler hoist every tap's loads to the top of the block, which
// needs more registers than gcc 13.3 has. gcc emits the lane forms correctly
// and then spills around them (-35..-47% on Neoverse V1/V2/V3); clang gained
// less than the per-row form does. Per-row wins on both compilers, so unlike
// the whole-matrix float hoist this replaced, it needs no __clang__ gate.
//
// Bit-exact: vmlal_laneq_s16 with lane L computes the same widening MLA as
// vmlal_s16 against a dup of the same coefficient, and the tap order is
// unchanged, so results are identical to the plain path.

template <unsigned N>
constexpr unsigned sq_row_q() { return (N + 7) / 8; }

template <unsigned N>
inline void sq_load_row_coeffs(int16x8_t *cq, const int16_t *m)
{
    constexpr unsigned RQ = sq_row_q<N>();
    int16_t cpad[N * RQ * 8] = {};
    for (unsigned r = 0; r < N; ++r)
        for (unsigned k = 0; k < N; ++k)
            cpad[(r * RQ + k / 8) * 8 + k % 8] = m[r * N + k];
    for (unsigned i = 0; i < N * RQ; ++i)
        cq[i] = vld1q_s16(cpad + 8 * i);
}

// One row's N taps over 16 byte pixels.
template <unsigned N, unsigned K>
inline void sq_tap_byte_row(int32x4_t (&a)[4], const uint8_t *p, const int16x8_t *crow)
{
    nc_byte16 x = nc_load_byte16(p + K);
    a[0] = vmlal_laneq_s16(a[0], vget_low_s16(x.lo), crow[K / 8], K % 8);
    a[1] = vmlal_laneq_s16(a[1], vget_high_s16(x.lo), crow[K / 8], K % 8);
    a[2] = vmlal_laneq_s16(a[2], vget_low_s16(x.hi), crow[K / 8], K % 8);
    a[3] = vmlal_laneq_s16(a[3], vget_high_s16(x.hi), crow[K / 8], K % 8);
    if constexpr (K + 1 < N)
        sq_tap_byte_row<N, K + 1>(a, p, crow);
}

// One row's N taps over 16 word pixels.
template <unsigned N, unsigned K>
inline void sq_tap_word_row(int32x4_t (&a)[4], const uint16_t *p, const int16x8_t *crow)
{
    int16x8_t x0 = nc_load_word_biased(p + K);
    int16x8_t x1 = nc_load_word_biased(p + K + 8);
    a[0] = vmlal_laneq_s16(a[0], vget_low_s16(x0), crow[K / 8], K % 8);
    a[1] = vmlal_laneq_s16(a[1], vget_high_s16(x0), crow[K / 8], K % 8);
    a[2] = vmlal_laneq_s16(a[2], vget_low_s16(x1), crow[K / 8], K % 8);
    a[3] = vmlal_laneq_s16(a[3], vget_high_s16(x1), crow[K / 8], K % 8);
    if constexpr (K + 1 < N)
        sq_tap_word_row<N, K + 1>(a, p, crow);
}

// One row's N taps over 8 word pixels, into the int32 per-row sums that the
// int64 path widens from.
template <unsigned N, unsigned K>
inline void sq_tap_word8_row(int32x4_t &r0, int32x4_t &r1, const uint16_t *p, const int16x8_t *crow)
{
    int16x8_t x = nc_load_word_biased(p + K);
    r0 = vmlal_laneq_s16(r0, vget_low_s16(x), crow[K / 8], K % 8);
    r1 = vmlal_laneq_s16(r1, vget_high_s16(x), crow[K / 8], K % 8);
    if constexpr (K + 1 < N)
        sq_tap_word8_row<N, K + 1>(r0, r1, p, crow);
}

// ---- byte ----

template <unsigned N>
unsigned sq_interior_byte(const uint8_t *const *rows, uint8_t *dst, unsigned S, unsigned W,
                          const int16_t *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    constexpr unsigned RQ = sq_row_q<N>();
    int16x8_t cq[N * RQ];
    sq_load_row_coeffs<N>(cq, m);
    const uint8_t *lrows[N];
    for (unsigned r = 0; r < N; ++r)
        lrows[r] = rows[r];

    auto block = [&](unsigned jj) {
        int32x4_t a[4] = { vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0) };
        for (unsigned r = 0; r < N; ++r)
            sq_tap_byte_row<N, 0>(a, lrows[r] + (jj - S), cq + r * RQ);
        nc_store_u8x8(dst + jj, a[0], a[1], sc, bi, sm);
        nc_store_u8x8(dst + jj + 8, a[2], a[3], sc, bi, sm);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 16 <= end; j += 16) block(j);
    if (j < end && end >= S + 16) { block(end - 16); j = end; }
    return j;
}

// ---- word, int32 accumulation (N <= 7) ----

template <unsigned N>
unsigned sq_interior_word_i32(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                              const int16_t *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
    int32_t wb = 0;
    for (unsigned i = 0; i < N * N; ++i) wb += static_cast<int32_t>(INT16_MIN) * m[i];
    int32x4_t wbv = vdupq_n_s32(wb);

    constexpr unsigned RQ = sq_row_q<N>();
    int16x8_t cq[N * RQ];
    sq_load_row_coeffs<N>(cq, m);
    const uint16_t *lrows[N];
    for (unsigned r = 0; r < N; ++r)
        lrows[r] = rows[r];

    auto block = [&](unsigned jj) {
        int32x4_t a[4] = { vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0) };
        for (unsigned r = 0; r < N; ++r)
            sq_tap_word_row<N, 0>(a, lrows[r] + (jj - S), cq + r * RQ);
        nc_store_u16x8(dst + jj, vsubq_s32(a[0], wbv), vsubq_s32(a[1], wbv), sc, bi, sm, mv);
        nc_store_u16x8(dst + jj + 8, vsubq_s32(a[2], wbv), vsubq_s32(a[3], wbv), sc, bi, sm, mv);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 16 <= end; j += 16) block(j);
    if (j < end && end >= S + 16) { block(end - 16); j = end; }
    return j;
}

// ---- word, int64 accumulation (N >= 9) ----

template <unsigned N>
unsigned sq_interior_word_i64(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                              const int16_t *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
    int64_t wb = 0;
    for (unsigned i = 0; i < N * N; ++i) wb += static_cast<int64_t>(INT16_MIN) * m[i];
    int64x2_t wbv = vdupq_n_s64(wb);

    constexpr unsigned RQ = sq_row_q<N>();
    int16x8_t cq[N * RQ];
    sq_load_row_coeffs<N>(cq, m);
    const uint16_t *lrows[N];
    for (unsigned r = 0; r < N; ++r)
        lrows[r] = rows[r];

    auto block = [&](unsigned jj) {
        int64x2_t a0 = vdupq_n_s64(0), a1 = vdupq_n_s64(0), a2 = vdupq_n_s64(0), a3 = vdupq_n_s64(0);
        for (unsigned r = 0; r < N; ++r) {
            int32x4_t r0 = vdupq_n_s32(0), r1 = vdupq_n_s32(0);
            sq_tap_word8_row<N, 0>(r0, r1, lrows[r] + (jj - S), cq + r * RQ);
            a0 = vaddw_s32(a0, vget_low_s32(r0));
            a1 = vaddw_s32(a1, vget_high_s32(r0));
            a2 = vaddw_s32(a2, vget_low_s32(r1));
            a3 = vaddw_s32(a3, vget_high_s32(r1));
        }
        nc_store_u16x8_i64(dst + jj, vsubq_s64(a0, wbv), vsubq_s64(a1, wbv),
                           vsubq_s64(a2, wbv), vsubq_s64(a3, wbv), sc, bi, sm, mv);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 8 <= end; j += 8) block(j);
    if (j < end && end >= S + 8) { block(end - 8); j = end; }
    return j;
}

// ---- float ----

// Float coefficients staged per row, exactly like the integer squares above:
// RQ = ceil(N/4) vectors per row, tap k -> vector k/4, lane k%4, row loop left
// rolled. Float taps are 1 load : 1 FMA, so the per-tap coefficient reload the
// compiler cannot hoist (see the integer note) lands right on the FMA critical
// path.
//
// This replaces an earlier 5x5-only variant that staged the whole 25-tap matrix
// and unrolled every tap. That one had to be gated on __clang__: it was +7-10%
// on M4 / +18% on Graviton5-with-clang, but -22% under gcc 13.3, which could
// not keep the coefficients live across the fully unrolled block. The per-row
// form keeps one row's coefficients in flight instead of the whole matrix, wins
// on both toolchains, and so needs no compiler gate and no N == 5 restriction.
//
// Tap order matches the plain r/k loops exactly, and vfmaq_laneq_f32 computes
// the same FMA as vfmaq_f32 with a dup'd scalar, so this is bit-identical to
// the plain path.
//
// 7x7 float is EXCLUDED and keeps the plain loops: it is the one shape that
// measured slower with per-row coefficients under gcc 13.3 (-3.9% G3, -7.9%
// G4b, -6.9% G5, three interleaved runs each, while clang 17 gained +1.4%).
// Every other float size wins on gcc (5x5 +5..15%, 9x9 +41..49%, 11x11
// +78..81%). The cause was not isolated -- gcc emits no spills for it, and
// outlining the block cannot be it either, since 9x9/11x11 are outlined too
// and win big. 7x7 was also the shape the earlier whole-matrix hoist could not
// help (it spilled even under clang), so this size seems to sit right at a
// scheduling cliff. Re-measure before extending per-row to it.
template <unsigned N>
constexpr bool sq_float_lane() { return N != 7; }

template <unsigned N>
constexpr unsigned sq_row_qf() { return (N + 3) / 4; }

template <unsigned N>
inline void sq_load_row_coeffs_f(float32x4_t *cq, const float *m)
{
    constexpr unsigned RQ = sq_row_qf<N>();
    float cpad[N * RQ * 4] = {};
    for (unsigned r = 0; r < N; ++r)
        for (unsigned k = 0; k < N; ++k)
            cpad[(r * RQ + k / 4) * 4 + k % 4] = m[r * N + k];
    for (unsigned i = 0; i < N * RQ; ++i)
        cq[i] = vld1q_f32(cpad + 4 * i);
}

template <unsigned N, unsigned K>
inline void sq_tap_float_row(float32x4_t (&a)[4], const float *p, const float32x4_t *crow)
{
    a[0] = vfmaq_laneq_f32(a[0], vld1q_f32(p + K), crow[K / 4], K % 4);
    a[1] = vfmaq_laneq_f32(a[1], vld1q_f32(p + K + 4), crow[K / 4], K % 4);
    a[2] = vfmaq_laneq_f32(a[2], vld1q_f32(p + K + 8), crow[K / 4], K % 4);
    a[3] = vfmaq_laneq_f32(a[3], vld1q_f32(p + K + 12), crow[K / 4], K % 4);
    if constexpr (K + 1 < N)
        sq_tap_float_row<N, K + 1>(a, p, crow);
}

template <unsigned N>
unsigned sq_interior_float(const float *const *rows, float *dst, unsigned S, unsigned W,
                           const float *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    constexpr unsigned RQ = sq_row_qf<N>();
    [[maybe_unused]] float32x4_t cq[N * RQ];
    [[maybe_unused]] const float *lrows[N];
    if constexpr (sq_float_lane<N>()) {
        sq_load_row_coeffs_f<N>(cq, m);
        for (unsigned r = 0; r < N; ++r)
            lrows[r] = rows[r];
    }

    auto block = [&](unsigned jj) {
        if constexpr (sq_float_lane<N>()) {
            float32x4_t a[4] = { vdupq_n_f32(0), vdupq_n_f32(0), vdupq_n_f32(0), vdupq_n_f32(0) };
            for (unsigned r = 0; r < N; ++r)
                sq_tap_float_row<N, 0>(a, lrows[r] + (jj - S), cq + r * RQ);
            nc_store_f32x4(dst + jj, a[0], sc, bi, sm);
            nc_store_f32x4(dst + jj + 4, a[1], sc, bi, sm);
            nc_store_f32x4(dst + jj + 8, a[2], sc, bi, sm);
            nc_store_f32x4(dst + jj + 12, a[3], sc, bi, sm);
            return;
        }
        float32x4_t a0 = vdupq_n_f32(0), a1 = vdupq_n_f32(0), a2 = vdupq_n_f32(0), a3 = vdupq_n_f32(0);
        for (unsigned r = 0; r < N; ++r) {
            const float *row = rows[r] + (jj - S);
            for (unsigned k = 0; k < N; ++k) {
                float32x4_t w = vdupq_n_f32(m[r * N + k]);
                a0 = vfmaq_f32(a0, vld1q_f32(row + k), w);
                a1 = vfmaq_f32(a1, vld1q_f32(row + k + 4), w);
                a2 = vfmaq_f32(a2, vld1q_f32(row + k + 8), w);
                a3 = vfmaq_f32(a3, vld1q_f32(row + k + 12), w);
            }
        }
        nc_store_f32x4(dst + jj, a0, sc, bi, sm);
        nc_store_f32x4(dst + jj + 4, a1, sc, bi, sm);
        nc_store_f32x4(dst + jj + 8, a2, sc, bi, sm);
        nc_store_f32x4(dst + jj + 12, a3, sc, bi, sm);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 16 <= end; j += 16) block(j);
    if (j < end && end >= S + 16) { block(end - 16); j = end; }
    return j;
}

// ---- half ----

// Per-row lane coefficients, as for float above. This path only runs for
// coefficients that do not narrow exactly to f16 (conv_f16) or on CPUs without
// FEAT_FHM; everything else dispatches to the fmlal kernels.
template <unsigned N, unsigned K>
inline void sq_tap_half_row(float32x4_t (&a)[4], const uint16_t *p, const float32x4_t *crow)
{
    nc_half8 x0 = nc_load_half8(p + K);
    nc_half8 x1 = nc_load_half8(p + K + 8);
    a[0] = vfmaq_laneq_f32(a[0], x0.lo, crow[K / 4], K % 4);
    a[1] = vfmaq_laneq_f32(a[1], x0.hi, crow[K / 4], K % 4);
    a[2] = vfmaq_laneq_f32(a[2], x1.lo, crow[K / 4], K % 4);
    a[3] = vfmaq_laneq_f32(a[3], x1.hi, crow[K / 4], K % 4);
    if constexpr (K + 1 < N)
        sq_tap_half_row<N, K + 1>(a, p, crow);
}

template <unsigned N>
unsigned sq_interior_half(const uint16_t *const *rows, uint16_t *dst, unsigned S, unsigned W,
                          const float *m, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    constexpr unsigned RQ = sq_row_qf<N>();
    float32x4_t cq[N * RQ];
    sq_load_row_coeffs_f<N>(cq, m);
    const uint16_t *lrows[N];
    for (unsigned r = 0; r < N; ++r)
        lrows[r] = rows[r];

    auto block = [&](unsigned jj) {
        float32x4_t a[4] = { vdupq_n_f32(0), vdupq_n_f32(0), vdupq_n_f32(0), vdupq_n_f32(0) };
        for (unsigned r = 0; r < N; ++r)
            sq_tap_half_row<N, 0>(a, lrows[r] + (jj - S), cq + r * RQ);
        nc_store_h16x8(dst + jj, a[0], a[1], sc, bi, sm);
        nc_store_h16x8(dst + jj + 8, a[2], a[3], sc, bi, sm);
    };
    unsigned end = W > S ? W - S : 0, j = S;
    for (; j + 16 <= end; j += 16) block(j);
    if (j < end && end >= S + 16) { block(end - 16); j = end; }
    return j;
}

// ---- plane driver: scalar mirror edges + SIMD interior ----

enum class SqType { Byte, Word, Float, Half };

template <SqType TY, unsigned N>
void sq_plane(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const vs_generic_params &p, unsigned W, unsigned H)
{
    typedef typename std::conditional<TY == SqType::Float, float, typename std::conditional<TY == SqType::Byte, uint8_t, uint16_t>::type>::type T;
    constexpr unsigned S = N / 2;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);
    uint16x8_t mv = vdupq_n_u16(p.maxval);

    for (unsigned i = 0; i < H; ++i) {
        const T *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = reinterpret_cast<const T *>(static_cast<const unsigned char *>(src) +
                static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * ss);
        T *d = reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * ds);

        unsigned aend;
        if constexpr (TY == SqType::Byte)
            aend = sq_interior_byte<N>(rows, d, S, W, p.matrix, sc, bi, sm);
        else if constexpr (TY == SqType::Word) {
            if constexpr (N > 7)
                aend = sq_interior_word_i64<N>(rows, d, S, W, p.matrix, sc, bi, sm, mv);
            else
                aend = sq_interior_word_i32<N>(rows, d, S, W, p.matrix, sc, bi, sm, mv);
        } else if constexpr (TY == SqType::Float)
            aend = sq_interior_float<N>(rows, d, S, W, p.matrixf, sc, bi, sm);
        else
            aend = sq_interior_half<N>(rows, d, S, W, p.matrixf, sc, bi, sm);

        auto scalar_px = [&](unsigned j) -> T {
            if constexpr (TY == SqType::Byte)
                return nc_sq_scalar_px<uint8_t, int32_t, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
            else if constexpr (TY == SqType::Word) {
                typedef typename std::conditional<(N > 5), int64_t, int32_t>::type Acc;
                return nc_sq_scalar_px<uint16_t, Acc, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
            } else if constexpr (TY == SqType::Float)
                return nc_sq_scalar_px<float, float, float, N>(rows, j, S, W, p.matrixf, p.div, p.bias, p.saturate, p.maxval);
            else
                return nc_sq_scalar_px_half<N>(rows, j, S, W, p.matrixf, p.div, p.bias, p.saturate);
        };

        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = scalar_px(j);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = scalar_px(j);
    }
}

} // namespace

#define VS_SQUARE_NEON_ENTRY(SZ, N, TY, TN) \
    void vs_generic_##SZ##_conv_##TN##_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane<SqType::TY, N>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SQUARE_NEON_ENTRY(5x5,   5,  Byte,  byte)
VS_SQUARE_NEON_ENTRY(7x7,   7,  Byte,  byte)
VS_SQUARE_NEON_ENTRY(9x9,   9,  Byte,  byte)
VS_SQUARE_NEON_ENTRY(11x11, 11, Byte,  byte)
VS_SQUARE_NEON_ENTRY(5x5,   5,  Word,  word)
VS_SQUARE_NEON_ENTRY(7x7,   7,  Word,  word)
VS_SQUARE_NEON_ENTRY(9x9,   9,  Word,  word)
VS_SQUARE_NEON_ENTRY(11x11, 11, Word,  word)
VS_SQUARE_NEON_ENTRY(5x5,   5,  Float, float)
VS_SQUARE_NEON_ENTRY(7x7,   7,  Float, float)
VS_SQUARE_NEON_ENTRY(9x9,   9,  Float, float)
VS_SQUARE_NEON_ENTRY(11x11, 11, Float, float)
VS_SQUARE_NEON_ENTRY(5x5,   5,  Half,  half)
VS_SQUARE_NEON_ENTRY(7x7,   7,  Half,  half)
VS_SQUARE_NEON_ENTRY(9x9,   9,  Half,  half)
VS_SQUARE_NEON_ENTRY(11x11, 11, Half,  half)
