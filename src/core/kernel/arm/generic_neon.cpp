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

#include <cstdint>
#include <arm_neon.h>

namespace {

// NEON primitive layer for generic_impl.h. 128-bit: 16 byte / 8 word / 4 float
// lanes. The word domain is native unsigned like the AVX2 backend
// (wsign/wunsign are identity, wmin/wmax map to vmin/vmaxq_u16, pack_word to a
// signed->unsigned saturating narrow). ivec is uint8x16_t with reinterprets at
// the op boundaries (NEON's typed vectors vs the untyped __m128i the impl was
// written against). cvt_i uses FCVTNS (nearest-even), matching the x86 tiers'
// default MXCSR rounding and the C reference's lrint, and fsqrt is the
// IEEE-rounded FSQRT, so the integer Prewitt/Sobel paths stay bit-exact.
//
// The half tier needs no feature gate: FCVTL/FCVTN (f16<->f32 conversion) are
// baseline AArch64; FEAT_FP16 is only required for f16 *arithmetic*, which
// this backend does not use (arithmetic stays in float32, like F16C on x86).
struct Backend_NEON {
    typedef uint8x16_t ivec;
    typedef float32x4_t fvec;
    static constexpr unsigned BYTE_LEN = 16;
    static constexpr unsigned WORD_LEN = 8;
    static constexpr unsigned FLOAT_LEN = 4;

    static uint16x8_t as_u16(ivec x) { return vreinterpretq_u16_u8(x); }
    static int16x8_t as_s16(ivec x) { return vreinterpretq_s16_u8(x); }
    static uint32x4_t as_u32(ivec x) { return vreinterpretq_u32_u8(x); }
    static int32x4_t as_s32(ivec x) { return vreinterpretq_s32_u8(x); }
    static ivec from_u16(uint16x8_t x) { return vreinterpretq_u8_u16(x); }
    static ivec from_s16(int16x8_t x) { return vreinterpretq_u8_s16(x); }
    static ivec from_u32(uint32x4_t x) { return vreinterpretq_u8_u32(x); }
    static ivec from_s32(int32x4_t x) { return vreinterpretq_u8_s32(x); }

    static ivec zero_i() { return vdupq_n_u8(0); }
    static ivec set1_i8(int x) { return vdupq_n_u8(static_cast<uint8_t>(x)); }
    static ivec set1_i16(int x) { return from_u16(vdupq_n_u16(static_cast<uint16_t>(x))); }
    static ivec set1_i32(int x) { return from_u32(vdupq_n_u32(static_cast<uint32_t>(x))); }

    static ivec add16(ivec a, ivec b) { return from_u16(vaddq_u16(as_u16(a), as_u16(b))); }
    static ivec sub16(ivec a, ivec b) { return from_u16(vsubq_u16(as_u16(a), as_u16(b))); }
    static ivec slli16(ivec a) { return from_u16(vshlq_n_u16(as_u16(a), 1)); }
    static ivec add32(ivec a, ivec b) { return from_u32(vaddq_u32(as_u32(a), as_u32(b))); }
    static ivec sub32(ivec a, ivec b) { return from_u32(vsubq_u32(as_u32(a), as_u32(b))); }
    static ivec slli32(ivec a) { return from_u32(vshlq_n_u32(as_u32(a), 1)); }
    static ivec srli16_3(ivec a) { return from_u16(vshrq_n_u16(as_u16(a), 3)); }
    static ivec srli32_3(ivec a) { return from_u32(vshrq_n_u32(as_u32(a), 3)); }
    // pmaddwd: widening multiply + pairwise add reproduces it exactly (the
    // impl's operands never reach the wrap/saturate corner cases).
    static ivec madd16(ivec a, ivec b)
    {
        int32x4_t lo = vmull_s16(vget_low_s16(as_s16(a)), vget_low_s16(as_s16(b)));
        int32x4_t hi = vmull_s16(vget_high_s16(as_s16(a)), vget_high_s16(as_s16(b)));
        return from_s32(vpaddq_s32(lo, hi));
    }
    static ivec unpacklo8(ivec a, ivec b) { return vzip1q_u8(a, b); }
    static ivec unpackhi8(ivec a, ivec b) { return vzip2q_u8(a, b); }
    static ivec unpacklo16(ivec a, ivec b) { return from_u16(vzip1q_u16(as_u16(a), as_u16(b))); }
    static ivec unpackhi16(ivec a, ivec b) { return from_u16(vzip2q_u16(as_u16(a), as_u16(b))); }
    static ivec min_u8(ivec a, ivec b) { return vminq_u8(a, b); }
    static ivec max_u8(ivec a, ivec b) { return vmaxq_u8(a, b); }
    static ivec adds_u8(ivec a, ivec b) { return vqaddq_u8(a, b); }
    static ivec subs_u8(ivec a, ivec b) { return vqsubq_u8(a, b); }
    static ivec adds_u16(ivec a, ivec b) { return from_u16(vqaddq_u16(as_u16(a), as_u16(b))); }
    static ivec subs_u16(ivec a, ivec b) { return from_u16(vqsubq_u16(as_u16(a), as_u16(b))); }
    static ivec and_i(ivec a, ivec b) { return vandq_u8(a, b); }
    static ivec or_i(ivec a, ivec b) { return vorrq_u8(a, b); }
    static ivec packs32(ivec a, ivec b) { return from_s16(vcombine_s16(vqmovn_s32(as_s32(a)), vqmovn_s32(as_s32(b)))); }
    static ivec packus16(ivec a, ivec b) { return vcombine_u8(vqmovun_s16(as_s16(a)), vqmovun_s16(as_s16(b))); }

    // Word domain (native unsigned).
    static ivec pack_word(ivec a, ivec b) { return from_u16(vcombine_u16(vqmovun_s32(as_s32(a)), vqmovun_s32(as_s32(b)))); }
    static ivec wsign(ivec x) { return x; }
    static ivec wunsign(ivec x) { return x; }
    static ivec wsign32(ivec x) { return x; }
    static ivec wmin(ivec a, ivec b) { return from_u16(vminq_u16(as_u16(a), as_u16(b))); }
    static ivec wmax(ivec a, ivec b) { return from_u16(vmaxq_u16(as_u16(a), as_u16(b))); }
    static ivec wmaxval(uint16_t mv) { return from_u16(vdupq_n_u16(mv)); }

    // Float.
    static fvec cvt_f(ivec x) { return vcvtq_f32_s32(as_s32(x)); }
    static ivec cvt_i(fvec x) { return from_s32(vcvtnq_s32_f32(x)); }
    static fvec set1_f(float x) { return vdupq_n_f32(x); }
    static fvec fadd(fvec a, fvec b) { return vaddq_f32(a, b); }
    static fvec fsub(fvec a, fvec b) { return vsubq_f32(a, b); }
    static fvec fmul(fvec a, fvec b) { return vmulq_f32(a, b); }
    static fvec fmin(fvec a, fvec b) { return vminq_f32(a, b); }
    static fvec fmax(fvec a, fvec b) { return vmaxq_f32(a, b); }
    static fvec fsqrt(fvec a) { return vsqrtq_f32(a); }
    // a*a + b*b as fma(a, a, b*b): clang contracts the C reference's
    // "float(gx)*gx + float(gy)*gy" into exactly this FMLA shape on AArch64,
    // and matching it is what keeps integer Prewitt/Sobel word bit-exact
    // (byte is exact either way: its squares stay under 2^24).
    static fvec fsumsq(fvec a, fvec b) { return vfmaq_f32(vmulq_f32(b, b), a, a); }
    static fvec fand(fvec a, fvec b) { return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b))); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return vfmaq_f32(c, a, b); }
    static fvec satmask(uint8_t saturate) { return vreinterpretq_f32_u32(vdupq_n_u32(saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu)); }

    // Memory. NEON loads carry no alignment contract, so load == loadu.
    static ivec byte_load(const uint8_t *p) { return vld1q_u8(p); }
    static ivec byte_loadu(const uint8_t *p) { return vld1q_u8(p); }
    static void byte_store(uint8_t *p, ivec x) { vst1q_u8(p, x); }
    static ivec word_load(const uint16_t *p) { return from_u16(vld1q_u16(p)); }
    static ivec word_loadu(const uint16_t *p) { return from_u16(vld1q_u16(p)); }
    static void word_store(uint16_t *p, ivec x) { vst1q_u16(p, as_u16(x)); }
    static fvec float_load(const float *p) { return vld1q_f32(p); }
    static fvec float_loadu(const float *p) { return vld1q_f32(p); }
    static void float_store(float *p, fvec x) { vst1q_f32(p, x); }

    // Half (float16): FCVTL/FCVTN on load/store; arithmetic stays float32.
    // FCVTN rounds nearest-even (FPCR default), matching floatToHalf and F16C.
    static fvec half_load(const uint16_t *p) { return vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(p))); }
    static fvec half_loadu(const uint16_t *p) { return half_load(p); }
    static void half_store(uint16_t *p, fvec x) { vst1_u16(p, vreinterpret_u16_f16(vcvt_f16_f32(x))); }

    // Edge inserts: shift the lane block by one with the scalar replicated in,
    // via ext against a dup vector; the runtime-index insert is an iota
    // compare + bsl (the NEON spelling of the SSE2 cmpeq+blend).
    static uint8x16_t iota8()
    {
        static const uint8_t v[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        return vld1q_u8(v);
    }
    static uint16x8_t iota16()
    {
        static const uint16_t v[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        return vld1q_u16(v);
    }
    static uint32x4_t iota32()
    {
        static const uint32_t v[4] = { 0, 1, 2, 3 };
        return vld1q_u32(v);
    }

    static ivec byte_shl_insert_lo(ivec x, uint8_t y)
    {
        return vextq_u8(vdupq_n_u8(y), x, 15);
    }
    static ivec byte_shr_insert(ivec x, uint8_t y, unsigned idx)
    {
        uint8x16_t shifted = vextq_u8(x, vdupq_n_u8(0), 1);
        uint8x16_t mask = vceqq_u8(iota8(), vdupq_n_u8(static_cast<uint8_t>(idx)));
        return vbslq_u8(mask, vdupq_n_u8(y), shifted);
    }
    static ivec word_shl_insert_lo(ivec x, uint16_t y)
    {
        return from_u16(vextq_u16(vdupq_n_u16(y), as_u16(x), 7));
    }
    static ivec word_shr_insert(ivec x, uint16_t y, unsigned idx)
    {
        uint16x8_t shifted = vextq_u16(as_u16(x), vdupq_n_u16(0), 1);
        uint16x8_t mask = vceqq_u16(iota16(), vdupq_n_u16(static_cast<uint16_t>(idx)));
        return from_u16(vbslq_u16(mask, vdupq_n_u16(y), shifted));
    }
    static fvec float_shl_insert_lo(fvec x, float y)
    {
        return vextq_f32(vdupq_n_f32(y), x, 3);
    }
    static fvec float_shr_insert(fvec x, float y, unsigned idx)
    {
        float32x4_t shifted = vextq_f32(x, vdupq_n_f32(0.0f), 1);
        uint32x4_t mask = vceqq_u32(iota32(), vdupq_n_u32(idx));
        return vbslq_f32(mask, vdupq_n_f32(y), shifted);
    }
};

} // namespace

#define BACKEND Backend_NEON
#include "../generic_impl.h"

// The convolution entry points are deliberately NOT emitted: the hand-written
// kernels in convolution_neon.cpp already cover 3x3 conv and beat what this
// port generates (madd16 is a 3-op emulation on NEON).
//
// Formats that do not beat their (auto-vectorised) C tier under a streaming
// source at 1 thread AND a full pool (measured on M4 and Graviton5) are not
// emitted either, the same treatment as 3x3 conv float:
//   deflate/inflate word   - the u32 unpack+add shape loses ~20% to C
//   deflate/inflate float  - pure tie; C already autovectorises optimally
//   prewitt float          - tie at 1 thread, loses at full pool
//   sobel float, half      - loses at both
// Ops with a partial format set get their surviving entry points emitted by
// hand below instead of via the all-format macros.
VS_GENERIC_MINMAX(min, false, neon)
VS_GENERIC_MINMAX(max, true, neon)
VS_GENERIC_3X3(median, Median, neon)

VS_GENERIC_MINMAX_HALF(min, false, neon)
VS_GENERIC_MINMAX_HALF(max, true, neon)
VS_GENERIC_3X3_HALF(median, Median, neon)
VS_GENERIC_3X3_B_HALF(prewitt, PrewittSobel, false, neon)
VS_GENERIC_3X3_B_HALF(deflate, DeflateInflate, false, neon)
VS_GENERIC_3X3_B_HALF(inflate, DeflateInflate, true, neon)

void vs_generic_3x3_prewitt_byte_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<PrewittSobelByte<Backend_NEON, false>>(src, ss, dst, ds, *p, w, h); }
void vs_generic_3x3_prewitt_word_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<PrewittSobelWord<Backend_NEON, false>>(src, ss, dst, ds, *p, w, h); }
void vs_generic_3x3_sobel_byte_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<PrewittSobelByte<Backend_NEON, true>>(src, ss, dst, ds, *p, w, h); }
void vs_generic_3x3_sobel_word_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<PrewittSobelWord<Backend_NEON, true>>(src, ss, dst, ds, *p, w, h); }
void vs_generic_3x3_deflate_byte_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<DeflateInflateByte<Backend_NEON, false>>(src, ss, dst, ds, *p, w, h); }
void vs_generic_3x3_inflate_byte_neon(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h)
{ filter_plane_3x3<DeflateInflateByte<Backend_NEON, true>>(src, ss, dst, ds, *p, w, h); }
