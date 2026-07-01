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
#include <emmintrin.h>

namespace {

inline __m128i mm_blendv_epi8(__m128i a, __m128i b, __m128i mask)
{
    return _mm_or_si128(_mm_andnot_si128(mask, a), _mm_and_si128(mask, b));
}

// SSE2 primitive layer for generic_impl.h. 128-bit: 16 byte / 8 word / 4 float
// lanes. SSE2 lacks unsigned 16-bit min/max, so the word domain uses the
// INT16_MIN sign-bias trick (wsign/wunsign + signed epi16 min/max).
struct Backend_SSE2 {
    typedef __m128i ivec;
    typedef __m128 fvec;
    static constexpr unsigned BYTE_LEN = 16;
    static constexpr unsigned WORD_LEN = 8;
    static constexpr unsigned FLOAT_LEN = 4;

    static ivec zero_i() { return _mm_setzero_si128(); }
    static ivec set1_i8(int x) { return _mm_set1_epi8(static_cast<char>(x)); }
    static ivec set1_i16(int x) { return _mm_set1_epi16(static_cast<short>(x)); }
    static ivec set1_i32(int x) { return _mm_set1_epi32(x); }

    static ivec add16(ivec a, ivec b) { return _mm_add_epi16(a, b); }
    static ivec sub16(ivec a, ivec b) { return _mm_sub_epi16(a, b); }
    static ivec slli16(ivec a) { return _mm_slli_epi16(a, 1); }
    static ivec add32(ivec a, ivec b) { return _mm_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm_sub_epi32(a, b); }
    static ivec slli32(ivec a) { return _mm_slli_epi32(a, 1); }
    static ivec srli16_3(ivec a) { return _mm_srli_epi16(a, 3); }
    static ivec srli32_3(ivec a) { return _mm_srli_epi32(a, 3); }
    static ivec madd16(ivec a, ivec b) { return _mm_madd_epi16(a, b); }
    static ivec unpacklo8(ivec a, ivec b) { return _mm_unpacklo_epi8(a, b); }
    static ivec unpackhi8(ivec a, ivec b) { return _mm_unpackhi_epi8(a, b); }
    static ivec unpacklo16(ivec a, ivec b) { return _mm_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm_unpackhi_epi16(a, b); }
    static ivec min_u8(ivec a, ivec b) { return _mm_min_epu8(a, b); }
    static ivec max_u8(ivec a, ivec b) { return _mm_max_epu8(a, b); }
    static ivec adds_u8(ivec a, ivec b) { return _mm_adds_epu8(a, b); }
    static ivec subs_u8(ivec a, ivec b) { return _mm_subs_epu8(a, b); }
    static ivec adds_u16(ivec a, ivec b) { return _mm_adds_epu16(a, b); }
    static ivec subs_u16(ivec a, ivec b) { return _mm_subs_epu16(a, b); }
    static ivec and_i(ivec a, ivec b) { return _mm_and_si128(a, b); }
    static ivec or_i(ivec a, ivec b) { return _mm_or_si128(a, b); }
    static ivec packs32(ivec a, ivec b) { return _mm_packs_epi32(a, b); }
    static ivec packus16(ivec a, ivec b) { return _mm_packus_epi16(a, b); }

    // Word domain (signed-biased).
    static ivec pack_word(ivec a, ivec b) { return _mm_packs_epi32(a, b); }
    static ivec wsign(ivec x) { return _mm_add_epi16(x, _mm_set1_epi16(INT16_MIN)); }
    static ivec wunsign(ivec x) { return _mm_sub_epi16(x, _mm_set1_epi16(INT16_MIN)); }
    static ivec wsign32(ivec x) { return _mm_add_epi32(x, _mm_set1_epi32(INT16_MIN)); }
    static ivec wmin(ivec a, ivec b) { return _mm_min_epi16(a, b); }
    static ivec wmax(ivec a, ivec b) { return _mm_max_epi16(a, b); }
    static ivec wmaxval(uint16_t mv) { return _mm_set1_epi16(static_cast<short>(static_cast<int32_t>(mv) + INT16_MIN)); }

    // Float.
    static fvec cvt_f(ivec x) { return _mm_cvtepi32_ps(x); }
    static ivec cvt_i(fvec x) { return _mm_cvtps_epi32(x); }
    static fvec set1_f(float x) { return _mm_set_ps1(x); }
    static fvec fadd(fvec a, fvec b) { return _mm_add_ps(a, b); }
    static fvec fsub(fvec a, fvec b) { return _mm_sub_ps(a, b); }
    static fvec fmul(fvec a, fvec b) { return _mm_mul_ps(a, b); }
    static fvec fmin(fvec a, fvec b) { return _mm_min_ps(a, b); }
    static fvec fmax(fvec a, fvec b) { return _mm_max_ps(a, b); }
    static fvec fsqrt(fvec a) { return _mm_sqrt_ps(a); }
    static fvec fand(fvec a, fvec b) { return _mm_and_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm_add_ps(_mm_mul_ps(a, b), c); }
    static fvec satmask(uint8_t saturate) { return _mm_castsi128_ps(_mm_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF)); }

    // Memory.
    static ivec byte_load(const uint8_t *p) { return _mm_load_si128((const __m128i *)p); }
    static ivec byte_loadu(const uint8_t *p) { return _mm_loadu_si128((const __m128i *)p); }
    static void byte_store(uint8_t *p, ivec x) { _mm_store_si128((__m128i *)p, x); }
    static ivec word_load(const uint16_t *p) { return _mm_load_si128((const __m128i *)p); }
    static ivec word_loadu(const uint16_t *p) { return _mm_loadu_si128((const __m128i *)p); }
    static void word_store(uint16_t *p, ivec x) { _mm_store_si128((__m128i *)p, x); }
    static fvec float_load(const float *p) { return _mm_load_ps(p); }
    static fvec float_loadu(const float *p) { return _mm_loadu_ps(p); }
    static void float_store(float *p, fvec x) { _mm_store_ps(p, x); }

    // Edge inserts.
    static ivec byte_shl_insert_lo(ivec x, uint8_t y)
    {
        return _mm_or_si128(_mm_cvtsi32_si128(y), _mm_slli_si128(x, 1));
    }
    static ivec byte_shr_insert(ivec x, uint8_t y, unsigned idx)
    {
        __m128i mask = _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm_cmpeq_epi8(mask, _mm_set1_epi8(static_cast<char>(idx)));
        return mm_blendv_epi8(_mm_srli_si128(x, 1), _mm_set1_epi8(static_cast<char>(y)), mask);
    }
    static ivec word_shl_insert_lo(ivec x, uint16_t y)
    {
        return _mm_or_si128(_mm_cvtsi32_si128(y), _mm_slli_si128(x, 2));
    }
    static ivec word_shr_insert(ivec x, uint16_t y, unsigned idx)
    {
        __m128i mask = _mm_set_epi16(7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm_cmpeq_epi16(mask, _mm_set1_epi16(static_cast<short>(idx)));
        return mm_blendv_epi8(_mm_srli_si128(x, 2), _mm_set1_epi16(static_cast<short>(y)), mask);
    }
    static fvec float_shl_insert_lo(fvec x, float y)
    {
        return _mm_or_ps(_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 4)), _mm_load_ss(&y));
    }
    static fvec float_shr_insert(fvec x, float y, unsigned idx)
    {
        __m128i mask = _mm_set_epi32(3, 2, 1, 0);
        mask = _mm_cmpeq_epi32(mask, _mm_set1_epi32(idx));
        return _mm_castsi128_ps(mm_blendv_epi8(_mm_srli_si128(_mm_castps_si128(x), 4), _mm_castps_si128(_mm_set_ps1(y)), mask));
    }
};

} // namespace

#define BACKEND Backend_SSE2
#include "generic_impl.h"

VS_GENERIC_ENTRYPOINTS(sse2)
