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
#include <immintrin.h>

namespace {

template <unsigned Count>
inline __m256i mm256_slli_ex_si256(__m256i a)
{
    static_assert(Count < 16, "");
    __m256i tmp = _mm256_slli_si256(a, Count);
    __m256i mask = _mm256_srli_si256(_mm256_permute2f128_si256(a, _mm256_setzero_si256(), 0x02), 16 - Count);
    return _mm256_or_si256(tmp, mask);
}

template <unsigned Count>
inline __m256i mm256_srli_ex_si256(__m256i a)
{
    static_assert(Count < 16, "");
    __m256i tmp = _mm256_srli_si256(a, Count);
    __m256i mask = _mm256_slli_si256(_mm256_permute2f128_si256(a, _mm256_setzero_si256(), 0x31), 16 - Count);
    return _mm256_or_si256(tmp, mask);
}

// AVX2 primitive layer for generic_impl.h. 256-bit: 32 byte / 16 word / 8 float
// lanes. Native unsigned 16-bit min/max, so the word domain is the identity
// (wsign/wunsign are no-ops, wmin/wmax map to epu16, pack_word to packus_epi32).
// Float convolution uses FMA.
struct Backend_AVX2 {
    typedef __m256i ivec;
    typedef __m256 fvec;
    static constexpr unsigned BYTE_LEN = 32;
    static constexpr unsigned WORD_LEN = 16;
    static constexpr unsigned FLOAT_LEN = 8;

    static ivec zero_i() { return _mm256_setzero_si256(); }
    static ivec set1_i8(int x) { return _mm256_set1_epi8(static_cast<char>(x)); }
    static ivec set1_i16(int x) { return _mm256_set1_epi16(static_cast<short>(x)); }
    static ivec set1_i32(int x) { return _mm256_set1_epi32(x); }

    static ivec add16(ivec a, ivec b) { return _mm256_add_epi16(a, b); }
    static ivec sub16(ivec a, ivec b) { return _mm256_sub_epi16(a, b); }
    static ivec slli16(ivec a) { return _mm256_slli_epi16(a, 1); }
    static ivec add32(ivec a, ivec b) { return _mm256_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm256_sub_epi32(a, b); }
    static ivec slli32(ivec a) { return _mm256_slli_epi32(a, 1); }
    static ivec srli16_3(ivec a) { return _mm256_srli_epi16(a, 3); }
    static ivec srli32_3(ivec a) { return _mm256_srli_epi32(a, 3); }
    static ivec madd16(ivec a, ivec b) { return _mm256_madd_epi16(a, b); }
    static ivec unpacklo8(ivec a, ivec b) { return _mm256_unpacklo_epi8(a, b); }
    static ivec unpackhi8(ivec a, ivec b) { return _mm256_unpackhi_epi8(a, b); }
    static ivec unpacklo16(ivec a, ivec b) { return _mm256_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm256_unpackhi_epi16(a, b); }
    static ivec min_u8(ivec a, ivec b) { return _mm256_min_epu8(a, b); }
    static ivec max_u8(ivec a, ivec b) { return _mm256_max_epu8(a, b); }
    static ivec adds_u8(ivec a, ivec b) { return _mm256_adds_epu8(a, b); }
    static ivec subs_u8(ivec a, ivec b) { return _mm256_subs_epu8(a, b); }
    static ivec adds_u16(ivec a, ivec b) { return _mm256_adds_epu16(a, b); }
    static ivec subs_u16(ivec a, ivec b) { return _mm256_subs_epu16(a, b); }
    static ivec and_i(ivec a, ivec b) { return _mm256_and_si256(a, b); }
    static ivec or_i(ivec a, ivec b) { return _mm256_or_si256(a, b); }
    static ivec packs32(ivec a, ivec b) { return _mm256_packs_epi32(a, b); }
    static ivec packus16(ivec a, ivec b) { return _mm256_packus_epi16(a, b); }

    // Word domain (native unsigned).
    static ivec pack_word(ivec a, ivec b) { return _mm256_packus_epi32(a, b); }
    static ivec wsign(ivec x) { return x; }
    static ivec wunsign(ivec x) { return x; }
    static ivec wsign32(ivec x) { return x; }
    static ivec wmin(ivec a, ivec b) { return _mm256_min_epu16(a, b); }
    static ivec wmax(ivec a, ivec b) { return _mm256_max_epu16(a, b); }
    static ivec wmaxval(uint16_t mv) { return _mm256_set1_epi16(static_cast<short>(mv)); }

    // Float.
    static fvec cvt_f(ivec x) { return _mm256_cvtepi32_ps(x); }
    static ivec cvt_i(fvec x) { return _mm256_cvtps_epi32(x); }
    static fvec set1_f(float x) { return _mm256_set1_ps(x); }
    static fvec fadd(fvec a, fvec b) { return _mm256_add_ps(a, b); }
    static fvec fsub(fvec a, fvec b) { return _mm256_sub_ps(a, b); }
    static fvec fmul(fvec a, fvec b) { return _mm256_mul_ps(a, b); }
    static fvec fmin(fvec a, fvec b) { return _mm256_min_ps(a, b); }
    static fvec fmax(fvec a, fvec b) { return _mm256_max_ps(a, b); }
    static fvec fsqrt(fvec a) { return _mm256_sqrt_ps(a); }
    static fvec fand(fvec a, fvec b) { return _mm256_and_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm256_fmadd_ps(a, b, c); }
    static fvec satmask(uint8_t saturate) { return _mm256_castsi256_ps(_mm256_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF)); }

    // Memory.
    static ivec byte_load(const uint8_t *p) { return _mm256_load_si256((const __m256i *)p); }
    static ivec byte_loadu(const uint8_t *p) { return _mm256_loadu_si256((const __m256i *)p); }
    static void byte_store(uint8_t *p, ivec x) { _mm256_store_si256((__m256i *)p, x); }
    static ivec word_load(const uint16_t *p) { return _mm256_load_si256((const __m256i *)p); }
    static ivec word_loadu(const uint16_t *p) { return _mm256_loadu_si256((const __m256i *)p); }
    static void word_store(uint16_t *p, ivec x) { _mm256_store_si256((__m256i *)p, x); }
    static fvec float_load(const float *p) { return _mm256_load_ps(p); }
    static fvec float_loadu(const float *p) { return _mm256_loadu_ps(p); }
    static void float_store(float *p, fvec x) { _mm256_store_ps(p, x); }

    // Half (float16): F16C convert on load/store; arithmetic stays float32.
    static fvec half_load(const _Float16 *p) { return _mm256_cvtph_ps(_mm_load_si128((const __m128i *)p)); }
    static fvec half_loadu(const _Float16 *p) { return _mm256_cvtph_ps(_mm_loadu_si128((const __m128i *)p)); }
    static void half_store(_Float16 *p, fvec x) { _mm_store_si128((__m128i *)p, _mm256_cvtps_ph(x, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)); }

    // Edge inserts (cross-lane shift-by-one with scalar insert).
    static ivec byte_shl_insert_lo(ivec x, uint8_t y)
    {
        return _mm256_or_si256(_mm256_zextsi128_si256(_mm_cvtsi32_si128(y)), mm256_slli_ex_si256<1>(x));
    }
    static ivec byte_shr_insert(ivec x, uint8_t y, unsigned idx)
    {
        __m256i mask = _mm256_set_epi8(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm256_cmpeq_epi8(mask, _mm256_set1_epi8(static_cast<char>(idx)));
        return _mm256_blendv_epi8(mm256_srli_ex_si256<1>(x), _mm256_set1_epi8(static_cast<char>(y)), mask);
    }
    static ivec word_shl_insert_lo(ivec x, uint16_t y)
    {
        return _mm256_or_si256(_mm256_zextsi128_si256(_mm_cvtsi32_si128(y)), mm256_slli_ex_si256<2>(x));
    }
    static ivec word_shr_insert(ivec x, uint16_t y, unsigned idx)
    {
        __m256i mask = _mm256_set_epi16(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm256_cmpeq_epi16(mask, _mm256_set1_epi16(static_cast<short>(idx)));
        return _mm256_blendv_epi8(mm256_srli_ex_si256<2>(x), _mm256_set1_epi16(static_cast<short>(y)), mask);
    }
    static fvec float_shl_insert_lo(fvec x, float y)
    {
        return _mm256_or_ps(_mm256_castsi256_ps(mm256_slli_ex_si256<4>(_mm256_castps_si256(x))), _mm256_zextps128_ps256(_mm_load_ss(&y)));
    }
    static fvec float_shr_insert(fvec x, float y, unsigned idx)
    {
        __m256i mask = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm256_cmpeq_epi32(mask, _mm256_set1_epi32(idx));
        return _mm256_castsi256_ps(_mm256_blendv_epi8(mm256_srli_ex_si256<4>(_mm256_castps_si256(x)), _mm256_castps_si256(_mm256_set1_ps(y)), mask));
    }
};

} // namespace

#define BACKEND Backend_AVX2
#include "generic_impl.h"

VS_GENERIC_ENTRYPOINTS(avx2)
VS_GENERIC_ENTRYPOINTS_HALF(avx2)
