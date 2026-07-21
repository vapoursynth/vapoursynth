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
* NEON 1D (horizontal/vertical/separable) and 3x3 square convolution.
*
* Semantics track kernel/generic.cpp exactly:
*  - 1D edges use the half-sample mirror formulas of conv_scanline_h /
*    conv_plane_v (replicated verbatim in the scalar edge paths).
*  - 3x3 uses replicate edges (filter_plane_3x3 row/column selection).
*  - The separable path quantises the vertical pass into a plane-typed tmp
*    scanline before the horizontal pass, like conv_plane_x.
*
* Integer accumulation:
*  - byte: u8 widens to s16, per-tap vmlal_s16 into int32. 25 * 1023 * 255
*    never overflows. Bit-exact with the int32 C reference.
*  - word: u16 biased by INT16_MIN into s16; biased worst case
*    25 * 1023 * 32768 fits int32, and the coefficient*bias sum is subtracted
*    afterwards. int32 wraparound arithmetic makes this exact, so results are
*    bit-exact with the (unbiased) int32 C reference.
*  - float/half: FMA, not bit-exact with C (allowed for float formats).
*/

#include <cstdint>
#include <cstring>
#include "VSHelper4.h"
#include "../generic.h"
#include "neon_common.h"

namespace {

enum class CvType { Byte, Word, Float, Half };

template <CvType TY>
struct cv_traits;

template <> struct cv_traits<CvType::Byte>  { typedef uint8_t T; typedef int32_t Acc; typedef int16_t Weight; };
template <> struct cv_traits<CvType::Word>  { typedef uint16_t T; typedef int32_t Acc; typedef int16_t Weight; };
template <> struct cv_traits<CvType::Float> { typedef float T; typedef float Acc; typedef float Weight; };
template <> struct cv_traits<CvType::Half>  { typedef uint16_t T; typedef float Acc; typedef float Weight; };

template <CvType TY>
inline const typename cv_traits<TY>::Weight *cv_coeffs(const vs_generic_params &p)
{
    if constexpr (TY == CvType::Byte || TY == CvType::Word)
        return p.matrix;
    else
        return p.matrixf;
}

template <CvType TY>
inline typename cv_traits<TY>::Acc cv_sample(typename cv_traits<TY>::T x)
{
    if constexpr (TY == CvType::Half)
        return nc_half_to_float(x);
    else
        return static_cast<typename cv_traits<TY>::Acc>(x);
}

// Final scalar store; matches limit(xrint<T>(tmp), maxval) in the C reference.
template <CvType TY>
inline typename cv_traits<TY>::T cv_scalar_store(float accum, float div, float bias, bool saturate, uint16_t maxval)
{
    float tmp = accum * div + bias;
    tmp = saturate ? tmp : std::fabs(tmp);
    if constexpr (TY == CvType::Byte) {
        float c = std::min(std::max(tmp, 0.0f), 255.0f);
        long v = std::lrint(c);
        return static_cast<uint8_t>(std::min<long>(v, maxval));
    } else if constexpr (TY == CvType::Word) {
        float c = std::min(std::max(tmp, 0.0f), 65535.0f);
        long v = std::lrint(c);
        return static_cast<uint16_t>(std::min<long>(v, maxval));
    } else if constexpr (TY == CvType::Float) {
        return tmp;
    } else {
        return nc_float_to_half(tmp);
    }
}

// ---- vectorised tap accumulation over one 16-pixel block --------------------
// Each *_tap16 accumulates coefficient k of a row pointer into 4 chains.

struct acc16_i32 {
    int32x4_t a0, a1, a2, a3;
    void clear() { a0 = a1 = a2 = a3 = vdupq_n_s32(0); }
};

struct acc16_f32 {
    float32x4_t a0, a1, a2, a3;
    void clear() { a0 = a1 = a2 = a3 = vdupq_n_f32(0); }
};

inline void byte_tap16(acc16_i32 &a, const uint8_t *p, int16_t w)
{
    nc_byte16 x = nc_load_byte16(p);
    int16x4_t wv = vdup_n_s16(w);
    a.a0 = vmlal_s16(a.a0, vget_low_s16(x.lo), wv);
    a.a1 = vmlal_s16(a.a1, vget_high_s16(x.lo), wv);
    a.a2 = vmlal_s16(a.a2, vget_low_s16(x.hi), wv);
    a.a3 = vmlal_s16(a.a3, vget_high_s16(x.hi), wv);
}

inline void word_tap16(acc16_i32 &a, const uint16_t *p, int16_t w)
{
    int16x8_t x0 = nc_load_word_biased(p);
    int16x8_t x1 = nc_load_word_biased(p + 8);
    int16x4_t wv = vdup_n_s16(w);
    a.a0 = vmlal_s16(a.a0, vget_low_s16(x0), wv);
    a.a1 = vmlal_s16(a.a1, vget_high_s16(x0), wv);
    a.a2 = vmlal_s16(a.a2, vget_low_s16(x1), wv);
    a.a3 = vmlal_s16(a.a3, vget_high_s16(x1), wv);
}

inline void float_tap16(acc16_f32 &a, const float *p, float w)
{
    float32x4_t wv = vdupq_n_f32(w);
    a.a0 = vfmaq_f32(a.a0, vld1q_f32(p), wv);
    a.a1 = vfmaq_f32(a.a1, vld1q_f32(p + 4), wv);
    a.a2 = vfmaq_f32(a.a2, vld1q_f32(p + 8), wv);
    a.a3 = vfmaq_f32(a.a3, vld1q_f32(p + 12), wv);
}

inline void half_tap16(acc16_f32 &a, const uint16_t *p, float w)
{
    nc_half8 x0 = nc_load_half8(p);
    nc_half8 x1 = nc_load_half8(p + 8);
    float32x4_t wv = vdupq_n_f32(w);
    a.a0 = vfmaq_f32(a.a0, x0.lo, wv);
    a.a1 = vfmaq_f32(a.a1, x0.hi, wv);
    a.a2 = vfmaq_f32(a.a2, x1.lo, wv);
    a.a3 = vfmaq_f32(a.a3, x1.hi, wv);
}

// First tap of a block: plain multiply instead of clear() + accumulate. The
// autovectorised C tier starts its chain with fmul, so a zero-init mov per
// accumulator is density we pay and it does not. Bit-exact for the integer
// forms (0 + x*w == x*w) and value-identical for float/half.

inline void byte_tap16_init(acc16_i32 &a, const uint8_t *p, int16_t w)
{
    nc_byte16 x = nc_load_byte16(p);
    int16x4_t wv = vdup_n_s16(w);
    a.a0 = vmull_s16(vget_low_s16(x.lo), wv);
    a.a1 = vmull_s16(vget_high_s16(x.lo), wv);
    a.a2 = vmull_s16(vget_low_s16(x.hi), wv);
    a.a3 = vmull_s16(vget_high_s16(x.hi), wv);
}

inline void word_tap16_init(acc16_i32 &a, const uint16_t *p, int16_t w)
{
    int16x8_t x0 = nc_load_word_biased(p);
    int16x8_t x1 = nc_load_word_biased(p + 8);
    int16x4_t wv = vdup_n_s16(w);
    a.a0 = vmull_s16(vget_low_s16(x0), wv);
    a.a1 = vmull_s16(vget_high_s16(x0), wv);
    a.a2 = vmull_s16(vget_low_s16(x1), wv);
    a.a3 = vmull_s16(vget_high_s16(x1), wv);
}

inline void float_tap16_init(acc16_f32 &a, const float *p, float32x4_t wv)
{
    a.a0 = vmulq_f32(vld1q_f32(p), wv);
    a.a1 = vmulq_f32(vld1q_f32(p + 4), wv);
    a.a2 = vmulq_f32(vld1q_f32(p + 8), wv);
    a.a3 = vmulq_f32(vld1q_f32(p + 12), wv);
}

inline void half_tap16_init(acc16_f32 &a, const uint16_t *p, float w)
{
    nc_half8 x0 = nc_load_half8(p);
    nc_half8 x1 = nc_load_half8(p + 8);
    float32x4_t wv = vdupq_n_f32(w);
    a.a0 = vmulq_f32(x0.lo, wv);
    a.a1 = vmulq_f32(x0.hi, wv);
    a.a2 = vmulq_f32(x1.lo, wv);
    a.a3 = vmulq_f32(x1.hi, wv);
}

template <CvType TY>
inline void store16(typename cv_traits<TY>::T *dst,
                    typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type &a,
                    int32_t wb, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
    if constexpr (TY == CvType::Byte) {
        nc_store_u8x8(dst, a.a0, a.a1, sc, bi, sm);
        nc_store_u8x8(dst + 8, a.a2, a.a3, sc, bi, sm);
    } else if constexpr (TY == CvType::Word) {
        int32x4_t wbv = vdupq_n_s32(wb);
        nc_store_u16x8(dst, vsubq_s32(a.a0, wbv), vsubq_s32(a.a1, wbv), sc, bi, sm, mv);
        nc_store_u16x8(dst + 8, vsubq_s32(a.a2, wbv), vsubq_s32(a.a3, wbv), sc, bi, sm, mv);
    } else if constexpr (TY == CvType::Float) {
        nc_store_f32x4(dst, a.a0, sc, bi, sm);
        nc_store_f32x4(dst + 4, a.a1, sc, bi, sm);
        nc_store_f32x4(dst + 8, a.a2, sc, bi, sm);
        nc_store_f32x4(dst + 12, a.a3, sc, bi, sm);
    } else {
        nc_store_h16x8(dst, a.a0, a.a1, sc, bi, sm);
        nc_store_h16x8(dst + 8, a.a2, a.a3, sc, bi, sm);
    }
}

template <CvType TY>
inline void tap16(typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type &a,
                  const typename cv_traits<TY>::T *p, typename cv_traits<TY>::Weight w)
{
    if constexpr (TY == CvType::Byte)
        byte_tap16(a, p, w);
    else if constexpr (TY == CvType::Word)
        word_tap16(a, p, w);
    else if constexpr (TY == CvType::Float)
        float_tap16(a, p, w);
    else
        half_tap16(a, p, w);
}

template <CvType TY>
inline void tap16_init(typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type &a,
                       const typename cv_traits<TY>::T *p, typename cv_traits<TY>::Weight w)
{
    if constexpr (TY == CvType::Byte)
        byte_tap16_init(a, p, w);
    else if constexpr (TY == CvType::Word)
        word_tap16_init(a, p, w);
    else if constexpr (TY == CvType::Float)
        float_tap16_init(a, p, vdupq_n_f32(w));
    else
        half_tap16_init(a, p, w);
}

// ---- compile-time tap counts + lane-form coefficients -----------------------
//
// With a runtime fwidth the tap loop reloads and re-broadcasts coeffs[k] every
// tap (an `ld1r` per tap; the compiler cannot hoist it, since p.matrix is
// caller memory the dstp stores may alias) and pays loop overhead per tap on
// top. Templating fwidth makes the count compile-time, which both unrolls the
// loop and lets coefficients live in register lanes instead.
//
// Taps are emitted in GROUPS of one coefficient vector (8 int16 lanes / 4 f32
// lanes) with the group loop left ROLLED. That cap is deliberate: the square
// kernels measured -35..-47% under gcc 13.3 when all N*N taps were unrolled
// against a fully staged matrix, because the scheduler hoists every tap's
// loads to the top of the block and spills (see square_neon.cpp). x86 caps the
// same way, at 13 taps per pass (VS_CONV_SELECT / conv_scanline_*_pass).
// For fwidth <= 8 (int) or <= 4 (float) there is exactly one group, so small
// filters still unroll completely.
//
// Bit-exact: the lane forms compute the same MLA/FMA as a dup'd scalar, tap
// order is unchanged, and tap 0 keeps the vmull/vmul first-tap init.

// 4 taps per group for every type. Integer coefficients could hold 8 lanes in
// an int16x8_t, but an 8-tap group measured WORSE under gcc 13.3 on exactly
// the horizontal integer shapes (word h25 -10..-20%, separable byte/word
// hv9 -4..-18% on V1/V2/V3) while float/half -- which only ever had 4 lanes --
// won everywhere. Same unroll-cap lesson as the squares, one level finer.
template <CvType TY>
constexpr unsigned cv_lanes() { return 4; }

template <CvType TY>
using cv_cvec = typename std::conditional<TY == CvType::Byte || TY == CvType::Word, int16x4_t, float32x4_t>::type;

template <CvType TY>
using cv_acc = typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type;

template <unsigned L>
inline void byte_tap16_lane(acc16_i32 &a, const uint8_t *p, int16x4_t c)
{
    nc_byte16 x = nc_load_byte16(p);
    a.a0 = vmlal_lane_s16(a.a0, vget_low_s16(x.lo), c, L);
    a.a1 = vmlal_lane_s16(a.a1, vget_high_s16(x.lo), c, L);
    a.a2 = vmlal_lane_s16(a.a2, vget_low_s16(x.hi), c, L);
    a.a3 = vmlal_lane_s16(a.a3, vget_high_s16(x.hi), c, L);
}

template <unsigned L>
inline void word_tap16_lane(acc16_i32 &a, const uint16_t *p, int16x4_t c)
{
    int16x8_t x0 = nc_load_word_biased(p);
    int16x8_t x1 = nc_load_word_biased(p + 8);
    a.a0 = vmlal_lane_s16(a.a0, vget_low_s16(x0), c, L);
    a.a1 = vmlal_lane_s16(a.a1, vget_high_s16(x0), c, L);
    a.a2 = vmlal_lane_s16(a.a2, vget_low_s16(x1), c, L);
    a.a3 = vmlal_lane_s16(a.a3, vget_high_s16(x1), c, L);
}

template <unsigned L>
inline void float_tap16_lane(acc16_f32 &a, const float *p, float32x4_t c)
{
    a.a0 = vfmaq_laneq_f32(a.a0, vld1q_f32(p), c, L);
    a.a1 = vfmaq_laneq_f32(a.a1, vld1q_f32(p + 4), c, L);
    a.a2 = vfmaq_laneq_f32(a.a2, vld1q_f32(p + 8), c, L);
    a.a3 = vfmaq_laneq_f32(a.a3, vld1q_f32(p + 12), c, L);
}

template <unsigned L>
inline void half_tap16_lane(acc16_f32 &a, const uint16_t *p, float32x4_t c)
{
    nc_half8 x0 = nc_load_half8(p);
    nc_half8 x1 = nc_load_half8(p + 8);
    a.a0 = vfmaq_laneq_f32(a.a0, x0.lo, c, L);
    a.a1 = vfmaq_laneq_f32(a.a1, x0.hi, c, L);
    a.a2 = vfmaq_laneq_f32(a.a2, x1.lo, c, L);
    a.a3 = vfmaq_laneq_f32(a.a3, x1.hi, c, L);
}

template <unsigned L>
inline void byte_tap16_init_lane(acc16_i32 &a, const uint8_t *p, int16x4_t c)
{
    nc_byte16 x = nc_load_byte16(p);
    a.a0 = vmull_lane_s16(vget_low_s16(x.lo), c, L);
    a.a1 = vmull_lane_s16(vget_high_s16(x.lo), c, L);
    a.a2 = vmull_lane_s16(vget_low_s16(x.hi), c, L);
    a.a3 = vmull_lane_s16(vget_high_s16(x.hi), c, L);
}

template <unsigned L>
inline void word_tap16_init_lane(acc16_i32 &a, const uint16_t *p, int16x4_t c)
{
    int16x8_t x0 = nc_load_word_biased(p);
    int16x8_t x1 = nc_load_word_biased(p + 8);
    a.a0 = vmull_lane_s16(vget_low_s16(x0), c, L);
    a.a1 = vmull_lane_s16(vget_high_s16(x0), c, L);
    a.a2 = vmull_lane_s16(vget_low_s16(x1), c, L);
    a.a3 = vmull_lane_s16(vget_high_s16(x1), c, L);
}

template <unsigned L>
inline void float_tap16_init_lane(acc16_f32 &a, const float *p, float32x4_t c)
{
    a.a0 = vmulq_laneq_f32(vld1q_f32(p), c, L);
    a.a1 = vmulq_laneq_f32(vld1q_f32(p + 4), c, L);
    a.a2 = vmulq_laneq_f32(vld1q_f32(p + 8), c, L);
    a.a3 = vmulq_laneq_f32(vld1q_f32(p + 12), c, L);
}

template <unsigned L>
inline void half_tap16_init_lane(acc16_f32 &a, const uint16_t *p, float32x4_t c)
{
    nc_half8 x0 = nc_load_half8(p);
    nc_half8 x1 = nc_load_half8(p + 8);
    a.a0 = vmulq_laneq_f32(x0.lo, c, L);
    a.a1 = vmulq_laneq_f32(x0.hi, c, L);
    a.a2 = vmulq_laneq_f32(x1.lo, c, L);
    a.a3 = vmulq_laneq_f32(x1.hi, c, L);
}

template <CvType TY, unsigned L>
inline void tap16_lane(cv_acc<TY> &a, const typename cv_traits<TY>::T *p, cv_cvec<TY> c)
{
    if constexpr (TY == CvType::Byte)
        byte_tap16_lane<L>(a, p, c);
    else if constexpr (TY == CvType::Word)
        word_tap16_lane<L>(a, p, c);
    else if constexpr (TY == CvType::Float)
        float_tap16_lane<L>(a, p, c);
    else
        half_tap16_lane<L>(a, p, c);
}

template <CvType TY, unsigned L>
inline void tap16_init_lane(cv_acc<TY> &a, const typename cv_traits<TY>::T *p, cv_cvec<TY> c)
{
    if constexpr (TY == CvType::Byte)
        byte_tap16_init_lane<L>(a, p, c);
    else if constexpr (TY == CvType::Word)
        word_tap16_init_lane<L>(a, p, c);
    else if constexpr (TY == CvType::Float)
        float_tap16_init_lane<L>(a, p, c);
    else
        half_tap16_init_lane<L>(a, p, c);
}

// Number of coefficient vectors for FW taps (>= 1 so the FW == 0 runtime path
// can still declare the array).
template <CvType TY, unsigned FW>
constexpr unsigned cv_nq() { return FW ? (FW + cv_lanes<TY>() - 1) / cv_lanes<TY>() : 1; }

template <CvType TY, unsigned FW>
inline void stage_coeffs(cv_cvec<TY> *cq, const typename cv_traits<TY>::Weight *m)
{
    constexpr unsigned G = cv_lanes<TY>();
    constexpr unsigned NQ = cv_nq<TY, FW>();
    typename cv_traits<TY>::Weight pad[NQ * G] = {};
    for (unsigned i = 0; i < FW; ++i)
        pad[i] = m[i];
    for (unsigned i = 0; i < NQ; ++i) {
        if constexpr (TY == CvType::Byte || TY == CvType::Word)
            cq[i] = vld1_s16(pad + 4 * i);
        else
            cq[i] = vld1q_f32(pad + 4 * i);
    }
}

// CNT taps from one coefficient vector, starting at lane L. Contiguous source
// (horizontal / separable): tap i reads p + i.
template <CvType TY, unsigned CNT, unsigned L>
inline void tap_group_h(cv_acc<TY> &a, const typename cv_traits<TY>::T *p, cv_cvec<TY> c)
{
    tap16_lane<TY, L>(a, p + L, c);
    if constexpr (L + 1 < CNT)
        tap_group_h<TY, CNT, L + 1>(a, p, c);
}

// Same, but tap i reads row pointer srcs[i] (vertical).
template <CvType TY, unsigned CNT, unsigned L>
inline void tap_group_v(cv_acc<TY> &a, const void *const *srcs, unsigned jj, cv_cvec<TY> c)
{
    tap16_lane<TY, L>(a, static_cast<const typename cv_traits<TY>::T *>(srcs[L]) + jj, c);
    if constexpr (L + 1 < CNT)
        tap_group_v<TY, CNT, L + 1>(a, srcs, jj, c);
}

// All FW taps: group 0 (init + rest), rolled middle groups, compile-time tail.
template <CvType TY, unsigned FW>
inline void conv_taps_h(cv_acc<TY> &a, const typename cv_traits<TY>::T *base, const cv_cvec<TY> *cq)
{
    constexpr unsigned G = cv_lanes<TY>();
    constexpr unsigned NG = FW / G;
    constexpr unsigned TAIL = FW - NG * G;
    constexpr unsigned FIRST = FW < G ? FW : G;

    tap16_init_lane<TY, 0>(a, base, cq[0]);
    if constexpr (FIRST > 1)
        tap_group_h<TY, FIRST, 1>(a, base, cq[0]);
    for (unsigned g = 1; g < NG; ++g)
        tap_group_h<TY, G, 0>(a, base + g * G, cq[g]);
    if constexpr (TAIL > 0 && FW > G)
        tap_group_h<TY, TAIL, 0>(a, base + NG * G, cq[NG]);
}

template <CvType TY, unsigned FW>
inline void conv_taps_v(cv_acc<TY> &a, const void *const *srcs, unsigned jj, const cv_cvec<TY> *cq)
{
    constexpr unsigned G = cv_lanes<TY>();
    constexpr unsigned NG = FW / G;
    constexpr unsigned TAIL = FW - NG * G;
    constexpr unsigned FIRST = FW < G ? FW : G;

    tap16_init_lane<TY, 0>(a, static_cast<const typename cv_traits<TY>::T *>(srcs[0]) + jj, cq[0]);
    if constexpr (FIRST > 1)
        tap_group_v<TY, FIRST, 1>(a, srcs, jj, cq[0]);
    for (unsigned g = 1; g < NG; ++g)
        tap_group_v<TY, G, 0>(a, srcs + g * G, jj, cq[g]);
    if constexpr (TAIL > 0 && FW > G)
        tap_group_v<TY, TAIL, 0>(a, srcs + NG * G, jj, cq[NG]);
}

// store16, minus the fabs mask when it is a guaranteed no-op. The C tier
// unswitches saturate into masked/unmasked loop copies; matching it costs one
// predictable branch per block instead of 4 no-op ands. Float only: the mask
// is a smaller fraction of the integer/half store pipelines.
//
// GATED ON !__clang__: measured +2% (3x3) / +3.5% (h25) float on
// Neoverse-V3 gcc 13.3, but -4.5% (3x3) / -11% (5x5, since reverted) on
// Apple M4 clang 17 -- the branch defeats clang's store scheduling, while its
// branchless masked store was already fine. Same one-compiler story as
// VS_SQ_FLOAT_HOIST in square_neon.cpp, opposite direction.
template <CvType TY>
inline void store16_sat(typename cv_traits<TY>::T *dst,
                        typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type &a,
                        [[maybe_unused]] bool saturate, int32_t wb, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv)
{
#if !defined(__clang__)
    if constexpr (TY == CvType::Float) {
        if (saturate) {
            vst1q_f32(dst, vfmaq_f32(bi, a.a0, sc));
            vst1q_f32(dst + 4, vfmaq_f32(bi, a.a1, sc));
            vst1q_f32(dst + 8, vfmaq_f32(bi, a.a2, sc));
            vst1q_f32(dst + 12, vfmaq_f32(bi, a.a3, sc));
            return;
        }
    }
#endif
    store16<TY>(dst, a, wb, sc, bi, sm, mv);
}

// Sum of coeff * INT16_MIN over the taps in use; subtracted from biased word
// accumulators before the store. Zero for the other formats.
template <CvType TY>
inline int32_t word_bias_sum(const typename cv_traits<TY>::Weight *coeffs, unsigned n)
{
    if constexpr (TY == CvType::Word) {
        int32_t wb = 0;
        for (unsigned i = 0; i < n; ++i)
            wb += static_cast<int32_t>(INT16_MIN) * coeffs[i];
        return wb;
    } else {
        (void)coeffs; (void)n;
        return 0;
    }
}

// ---- 1D horizontal ----------------------------------------------------------

// Scalar mirror-edge pixel; replicates the conv_scanline_h formulas verbatim.
template <CvType TY>
inline typename cv_traits<TY>::T conv_h_edge_px(const typename cv_traits<TY>::T *srcp, unsigned j, unsigned width,
                                                const vs_generic_params &p)
{
    typedef typename cv_traits<TY>::Acc Acc;
    const auto *coeffs = cv_coeffs<TY>(p);
    unsigned fwidth = p.matrixsize;
    unsigned support = fwidth / 2;
    unsigned dist_from_right = width - 1 - j;

    Acc accum = 0;
    for (unsigned k = 0; k < support; ++k) {
        unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
        accum += coeffs[k] * cv_sample<TY>(srcp[idx]);
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
        accum += coeffs[k] * cv_sample<TY>(srcp[idx]);
    }
    return cv_scalar_store<TY>(static_cast<float>(accum), p.div, p.bias, p.saturate, p.maxval);
}

// FW == 0 keeps the runtime-fwidth loop (the fallback for tap counts the
// dispatch below does not instantiate); FW > 0 is the templated form.
template <CvType TY, unsigned FW = 0>
void conv_h_scanline(const typename cv_traits<TY>::T *srcp, typename cv_traits<TY>::T *dstp, unsigned width,
                     const vs_generic_params &p, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv, int32_t wb)
{
    const auto *coeffs = cv_coeffs<TY>(p);
    unsigned fwidth = FW ? FW : p.matrixsize;
    unsigned support = fwidth / 2;
    // Local copy: p is caller memory, so reading p.saturate inside the block
    // would reload (and branch on) it every 16 pixels -- the optimizer cannot
    // hoist it past the dstp stores it might alias.
    const bool satur = p.saturate != 0;

    // Same reason: staged once here, not re-read per tap inside the block.
    [[maybe_unused]] cv_cvec<TY> cq[cv_nq<TY, FW>()];
    if constexpr (FW)
        stage_coeffs<TY, FW>(cq, coeffs);

    for (unsigned j = 0; j < std::min(width, support); ++j)
        dstp[j] = conv_h_edge_px<TY>(srcp, j, width, p);

    unsigned end = width - std::min(width, support);
    unsigned j = support;
    auto block = [&](unsigned jj) {
        cv_acc<TY> a;
        if constexpr (FW) {
            conv_taps_h<TY, FW>(a, srcp + jj - support, cq);
        } else {
            tap16_init<TY>(a, srcp + jj - support, coeffs[0]);
            for (unsigned k = 1; k < fwidth; ++k)
                tap16<TY>(a, srcp + jj - support + k, coeffs[k]);
        }
        store16_sat<TY>(dstp + jj, a, satur, wb, sc, bi, sm, mv);
    };
    if (end > support) {
        for (; j + 16 <= end; j += 16) block(j);
        if (j < end && end >= support + 16) { block(end - 16); j = end; }
        for (; j < end; ++j)
            dstp[j] = conv_h_edge_px<TY>(srcp, j, width, p);
    }

    for (unsigned jj = std::max(support, end); jj < width; ++jj)
        dstp[jj] = conv_h_edge_px<TY>(srcp, jj, width, p);
}

// ---- 1D vertical --------------------------------------------------------------

template <CvType TY, unsigned FW = 0>
void conv_v_scanline(const void *const *srcs, typename cv_traits<TY>::T *dstp, unsigned width,
                     const vs_generic_params &p, float32x4_t sc, float32x4_t bi, uint32x4_t sm, uint16x8_t mv, int32_t wb)
{
    typedef typename cv_traits<TY>::T T;
    typedef typename cv_traits<TY>::Acc Acc;
    const auto *coeffs = cv_coeffs<TY>(p);
    unsigned fwidth = FW ? FW : p.matrixsize;
    const bool satur = p.saturate != 0;   // local copy; see conv_h_scanline

    [[maybe_unused]] cv_cvec<TY> cq[cv_nq<TY, FW>()];
    if constexpr (FW)
        stage_coeffs<TY, FW>(cq, coeffs);

    unsigned j = 0;
    auto block = [&](unsigned jj) {
        cv_acc<TY> a;
        if constexpr (FW) {
            conv_taps_v<TY, FW>(a, srcs, jj, cq);
        } else {
            tap16_init<TY>(a, static_cast<const T *>(srcs[0]) + jj, coeffs[0]);
            for (unsigned k = 1; k < fwidth; ++k)
                tap16<TY>(a, static_cast<const T *>(srcs[k]) + jj, coeffs[k]);
        }
        store16_sat<TY>(dstp + jj, a, satur, wb, sc, bi, sm, mv);
    };
    for (; j + 16 <= width; j += 16) block(j);
    if (j < width && width >= 16) { block(width - 16); j = width; }
    for (; j < width; ++j) {
        Acc accum = 0;
        for (unsigned k = 0; k < fwidth; ++k)
            accum += coeffs[k] * cv_sample<TY>(static_cast<const T *>(srcs[k])[j]);
        dstp[j] = cv_scalar_store<TY>(static_cast<float>(accum), p.div, p.bias, p.saturate, p.maxval);
    }
}

// Mirror row-pointer selection of conv_plane_v / conv_plane_x, verbatim.
inline void conv_v_select_rows(const void *src, ptrdiff_t src_stride, const void *srcp[25],
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

// ---- plane drivers ------------------------------------------------------------

template <CvType TY, unsigned FW>
void conv_plane_h_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                       const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename cv_traits<TY>::T T;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);
    uint16x8_t mv = vdupq_n_u16(p.maxval);
    int32_t wb = word_bias_sum<TY>(cv_coeffs<TY>(p), p.matrixsize);

    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = reinterpret_cast<const T *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(i) * src_stride);
        T *dstp = reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        conv_h_scanline<TY, FW>(srcp, dstp, width, p, sc, bi, sm, mv, wb);
    }
}

template <CvType TY, unsigned FW>
void conv_plane_v_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                       const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename cv_traits<TY>::T T;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);
    uint16x8_t mv = vdupq_n_u16(p.maxval);
    int32_t wb = word_bias_sum<TY>(cv_coeffs<TY>(p), p.matrixsize);

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        conv_v_select_rows(src, src_stride, srcp, i, height, p.matrixsize);
        T *dstp = reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        conv_v_scanline<TY, FW>(srcp, dstp, width, p, sc, bi, sm, mv, wb);
    }
}

// dot_h routes the horizontal half through the i8mm usdot scanline (byte only,
// and only when the caller has checked conv_int8 + i8mm). It is a runtime flag,
// not a template parameter, deliberately: it is read once per row, and
// templating it would double this driver's instantiations for nothing.
template <CvType TY, unsigned FW>
void conv_plane_x_impl(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                       const vs_generic_params &p, unsigned width, unsigned height,
                       [[maybe_unused]] bool dot_h = false)
{
    typedef typename cv_traits<TY>::T T;
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);
    uint16x8_t mv = vdupq_n_u16(p.maxval);
    int32_t wb = word_bias_sum<TY>(cv_coeffs<TY>(p), p.matrixsize);

    T *tmp = static_cast<T *>(vsh::vsh_aligned_malloc(width * sizeof(T), 64));
    if (!tmp) {
        // Returning quietly would leave the destination plane as whatever the
        // frame allocator handed out (possibly other frames' data). Zero is
        // still wrong output, but deterministic and not an information leak.
        for (unsigned i = 0; i < height; ++i)
            std::memset(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride, 0, width * sizeof(T));
        return;
    }

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        conv_v_select_rows(src, src_stride, srcp, i, height, p.matrixsize);
        T *dstp = reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        conv_v_scanline<TY, FW>(srcp, tmp, width, p, sc, bi, sm, mv, wb);
#ifdef VS_TARGET_ARM_I8MM
        if constexpr (TY == CvType::Byte) {
            if (dot_h) {
                vs_generic_1d_conv_h_byte_scanline_neon_dot(tmp, dstp, &p, width);
                continue;
            }
        }
#endif
        conv_h_scanline<TY, FW>(tmp, dstp, width, p, sc, bi, sm, mv, wb);
    }

    vsh::vsh_aligned_free(tmp);
}

// Compile-time tap counts for the fwidths the filter actually accepts (odd,
// 3..25), mirroring x86's VS_CONV_SELECT. The switch runs once per plane, so
// the row loop below it is fully specialised. FW = 0 is the runtime fallback
// and stays reachable: matrixsize is validated elsewhere, but a shape this does
// not enumerate must still produce correct output rather than fall off the end.
#define VS_CONV_PLANE_SELECT(dir)                                                                  \
template <CvType TY>                                                                               \
void conv_plane_##dir##_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, \
                             const vs_generic_params &p, unsigned width, unsigned height)          \
{                                                                                                  \
    switch (p.matrixsize) {                                                                        \
    case 3:  return conv_plane_##dir##_impl<TY, 3>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 5:  return conv_plane_##dir##_impl<TY, 5>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 7:  return conv_plane_##dir##_impl<TY, 7>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 9:  return conv_plane_##dir##_impl<TY, 9>(src, src_stride, dst, dst_stride, p, width, height);  \
    case 11: return conv_plane_##dir##_impl<TY, 11>(src, src_stride, dst, dst_stride, p, width, height); \
    case 13: return conv_plane_##dir##_impl<TY, 13>(src, src_stride, dst, dst_stride, p, width, height); \
    case 15: return conv_plane_##dir##_impl<TY, 15>(src, src_stride, dst, dst_stride, p, width, height); \
    case 17: return conv_plane_##dir##_impl<TY, 17>(src, src_stride, dst, dst_stride, p, width, height); \
    case 19: return conv_plane_##dir##_impl<TY, 19>(src, src_stride, dst, dst_stride, p, width, height); \
    case 21: return conv_plane_##dir##_impl<TY, 21>(src, src_stride, dst, dst_stride, p, width, height); \
    case 23: return conv_plane_##dir##_impl<TY, 23>(src, src_stride, dst, dst_stride, p, width, height); \
    case 25: return conv_plane_##dir##_impl<TY, 25>(src, src_stride, dst, dst_stride, p, width, height); \
    default: return conv_plane_##dir##_impl<TY, 0>(src, src_stride, dst, dst_stride, p, width, height);  \
    }                                                                                              \
}

VS_CONV_PLANE_SELECT(h)
VS_CONV_PLANE_SELECT(v)
VS_CONV_PLANE_SELECT(x)

#ifdef VS_TARGET_ARM_I8MM
// Separable byte with the horizontal half on usdot. Same tap-count switch as
// above; only the trailing dot_h differs, so it shares every instantiation of
// conv_plane_x_impl<Byte, FW> with the plain separable entry.
void conv_plane_x_byte_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                           const vs_generic_params &p, unsigned width, unsigned height)
{
    constexpr CvType TY = CvType::Byte;
    switch (p.matrixsize) {
    case 3:  return conv_plane_x_impl<TY, 3>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 5:  return conv_plane_x_impl<TY, 5>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 7:  return conv_plane_x_impl<TY, 7>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 9:  return conv_plane_x_impl<TY, 9>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 11: return conv_plane_x_impl<TY, 11>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 13: return conv_plane_x_impl<TY, 13>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 15: return conv_plane_x_impl<TY, 15>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 17: return conv_plane_x_impl<TY, 17>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 19: return conv_plane_x_impl<TY, 19>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 21: return conv_plane_x_impl<TY, 21>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 23: return conv_plane_x_impl<TY, 23>(src, src_stride, dst, dst_stride, p, width, height, true);
    case 25: return conv_plane_x_impl<TY, 25>(src, src_stride, dst, dst_stride, p, width, height, true);
    default: return conv_plane_x_impl<TY, 0>(src, src_stride, dst, dst_stride, p, width, height, true);
    }
}
#endif

// ---- 3x3 square (replicate edges, filter_plane_3x3 semantics) ------------------

template <CvType TY>
void conv_plane_3x3_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                         const vs_generic_params &p, unsigned width, unsigned height)
{
    typedef typename cv_traits<TY>::T T;
    typedef typename cv_traits<TY>::Acc Acc;
    const auto *coeffs = cv_coeffs<TY>(p);
    float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    uint32x4_t sm = nc_satmask(p.saturate);
    uint16x8_t mv = vdupq_n_u16(p.maxval);
    int32_t wb = word_bias_sum<TY>(coeffs, 9);
    const bool satur = p.saturate != 0;   // local copy; see conv_h_scanline

    // Float taps are 1 load : 1 FMA -- the only format with no arithmetic to hide
    // a stray load behind (byte is 1:4, word/half 1:2) -- so a per-block coefficient
    // reload lands on the FMA's critical path. Broadcasting once per plane is +21%
    // on M4 and +2-7% on Graviton3/4/5, and is what puts this kernel back ahead of
    // the C tier, whose autovectoriser was already hoisting. Float only: the same
    // change on half measured -1.2% (it is conversion-bound, not load-starved).
    [[maybe_unused]] float32x4_t cf[9];
    if constexpr (TY == CvType::Float) {
        for (unsigned i = 0; i < 9; ++i)
            cf[i] = vdupq_n_f32(coeffs[i]);
    }

    auto src_row = [&](unsigned r) {
        return reinterpret_cast<const T *>(static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(r) * src_stride);
    };
    auto dst_row = [&](unsigned r) {
        return reinterpret_cast<T *>(static_cast<unsigned char *>(dst) + static_cast<ptrdiff_t>(r) * dst_stride);
    };
    auto scalar_px3 = [&](const T *rt, const T *rm, const T *rb, unsigned a, unsigned b, unsigned c) -> T {
        const T *rows[3] = { rt, rm, rb };
        Acc accum = 0;
        for (unsigned r = 0; r < 3; ++r) {
            accum += coeffs[r * 3 + 0] * cv_sample<TY>(rows[r][a]);
            accum += coeffs[r * 3 + 1] * cv_sample<TY>(rows[r][b]);
            accum += coeffs[r * 3 + 2] * cv_sample<TY>(rows[r][c]);
        }
        return cv_scalar_store<TY>(static_cast<float>(accum), p.div, p.bias, p.saturate, p.maxval);
    };

    auto do_row = [&](unsigned i) {
        unsigned above_idx = i == 0 ? 0 : i - 1;
        unsigned below_idx = i == height - 1 ? height - 1 : i + 1;
        const T *rows[3] = { src_row(above_idx), src_row(i), src_row(below_idx) };
        T *dstp = dst_row(i);

        auto scalar_px = [&](unsigned a, unsigned b, unsigned c) -> T {
            return scalar_px3(rows[0], rows[1], rows[2], a, b, c);
        };

        dstp[0] = scalar_px(0, 0, width > 1 ? 1 : 0);

        unsigned end = width > 1 ? width - 1 : 0;
        unsigned j = 1;
        auto block = [&](unsigned jj) {
            typename std::conditional<TY == CvType::Byte || TY == CvType::Word, acc16_i32, acc16_f32>::type a;
            if constexpr (TY == CvType::Float) {
                float_tap16_init(a, rows[0] + jj - 1, cf[0]);
                for (unsigned r = 0; r < 3; ++r) {
                    for (unsigned k = r == 0 ? 1 : 0; k < 3; ++k) {
                        const float *q = rows[r] + jj - 1 + k;
                        float32x4_t w = cf[r * 3 + k];
                        a.a0 = vfmaq_f32(a.a0, vld1q_f32(q), w);
                        a.a1 = vfmaq_f32(a.a1, vld1q_f32(q + 4), w);
                        a.a2 = vfmaq_f32(a.a2, vld1q_f32(q + 8), w);
                        a.a3 = vfmaq_f32(a.a3, vld1q_f32(q + 12), w);
                    }
                }
            } else {
                tap16_init<TY>(a, rows[0] + jj - 1, coeffs[0]);
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned k = r == 0 ? 1 : 0; k < 3; ++k)
                        tap16<TY>(a, rows[r] + jj - 1 + k, coeffs[r * 3 + k]);
            }
            store16_sat<TY>(dstp + jj, a, satur, wb, sc, bi, sm, mv);
        };
        if (end > 1) {
            for (; j + 16 <= end; j += 16) block(j);
            if (j < end && end >= 1 + 16) { block(end - 16); j = end; }
            for (; j < end; ++j) {
                unsigned a = j - 1, b = j, c = j + 1;
                dstp[j] = scalar_px(a, b, c);
            }
        }

        if (width > 1)
            dstp[width - 1] = scalar_px(width - 2, width - 1, width - 1);
    };

    for (unsigned i = 0; i < height; ++i)
        do_row(i);
}

} // namespace

#define VS_CONV_NEON_ENTRY(KERNEL, FN, TY, TN) \
    void vs_generic_##KERNEL##_##TN##_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { FN<CvType::TY>(src, src_stride, dst, dst_stride, *params, width, height); }

// 3x3 float has no NEON entry: it loses to its autovectorised C tier at a full
// thread pool on M4, so it is dispatched to C (see genericSelectNEON).
VS_CONV_NEON_ENTRY(3x3_conv, conv_plane_3x3_neon, Byte, byte)
VS_CONV_NEON_ENTRY(3x3_conv, conv_plane_3x3_neon, Word, word)
VS_CONV_NEON_ENTRY(3x3_conv, conv_plane_3x3_neon, Half, half)

VS_CONV_NEON_ENTRY(1d_conv_h, conv_plane_h_neon, Byte, byte)
VS_CONV_NEON_ENTRY(1d_conv_h, conv_plane_h_neon, Word, word)
VS_CONV_NEON_ENTRY(1d_conv_h, conv_plane_h_neon, Float, float)
VS_CONV_NEON_ENTRY(1d_conv_h, conv_plane_h_neon, Half, half)

VS_CONV_NEON_ENTRY(1d_conv_v, conv_plane_v_neon, Byte, byte)
VS_CONV_NEON_ENTRY(1d_conv_v, conv_plane_v_neon, Word, word)
VS_CONV_NEON_ENTRY(1d_conv_v, conv_plane_v_neon, Float, float)
VS_CONV_NEON_ENTRY(1d_conv_v, conv_plane_v_neon, Half, half)

VS_CONV_NEON_ENTRY(2d_conv_sep, conv_plane_x_neon, Byte, byte)
VS_CONV_NEON_ENTRY(2d_conv_sep, conv_plane_x_neon, Word, word)
VS_CONV_NEON_ENTRY(2d_conv_sep, conv_plane_x_neon, Float, float)
VS_CONV_NEON_ENTRY(2d_conv_sep, conv_plane_x_neon, Half, half)

#ifdef VS_TARGET_ARM_I8MM
void vs_generic_2d_conv_sep_byte_neon_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{ conv_plane_x_byte_dot(src, src_stride, dst, dst_stride, *params, width, height); }
#endif
