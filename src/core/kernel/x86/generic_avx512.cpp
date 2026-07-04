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

// Whole-512-bit shift left by Count bytes (Count < 16), zero-filling the low
// bytes. bslli shifts each 128-bit lane; the cross-lane carry (each lane's low
// Count bytes = previous lane's high Count bytes) is supplied by shifting the
// lanes up by one 128-bit lane (lane0 -> 0) and bringing their high bytes down.
template <unsigned Count>
inline __m512i mm512_bslli_whole(__m512i x)
{
    __m512i lane_up = _mm512_maskz_permutexvar_epi64(0xFC, _mm512_setr_epi64(0, 0, 0, 1, 2, 3, 4, 5), x);
    return _mm512_or_si512(_mm512_bslli_epi128(x, Count), _mm512_bsrli_epi128(lane_up, 16 - Count));
}

// Whole-512-bit shift right by Count bytes, zero-filling the high bytes.
template <unsigned Count>
inline __m512i mm512_bsrli_whole(__m512i x)
{
    __m512i lane_down = _mm512_maskz_permutexvar_epi64(0x3F, _mm512_setr_epi64(2, 3, 4, 5, 6, 7, 0, 0), x);
    return _mm512_or_si512(_mm512_bsrli_epi128(x, Count), _mm512_bslli_epi128(lane_down, 16 - Count));
}

// AVX-512 (x86-64-v4: F+BW+DQ+VL+CD) primitive layer for generic_impl.h.
// 512-bit: 64 byte / 32 word / 16 float lanes. Native unsigned 16-bit min/max
// (word domain is the identity, like AVX2). These ops are column-vertical, so
// the lane partition is preserved end to end and the byte/float kernels match
// the narrower tiers without any permute fixup. Frames are 64-byte aligned
// whenever this tier runs, so aligned loads/stores are safe.
struct Backend_AVX512 {
    typedef __m512i ivec;
    typedef __m512 fvec;
    static constexpr unsigned BYTE_LEN = 64;
    static constexpr unsigned WORD_LEN = 32;
    static constexpr unsigned FLOAT_LEN = 16;

    static ivec zero_i() { return _mm512_setzero_si512(); }
    static ivec set1_i8(int x) { return _mm512_set1_epi8(static_cast<char>(x)); }
    static ivec set1_i16(int x) { return _mm512_set1_epi16(static_cast<short>(x)); }
    static ivec set1_i32(int x) { return _mm512_set1_epi32(x); }

    static ivec add16(ivec a, ivec b) { return _mm512_add_epi16(a, b); }
    static ivec sub16(ivec a, ivec b) { return _mm512_sub_epi16(a, b); }
    static ivec slli16(ivec a) { return _mm512_slli_epi16(a, 1); }
    static ivec add32(ivec a, ivec b) { return _mm512_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm512_sub_epi32(a, b); }
    static ivec slli32(ivec a) { return _mm512_slli_epi32(a, 1); }
    static ivec srli16_3(ivec a) { return _mm512_srli_epi16(a, 3); }
    static ivec srli32_3(ivec a) { return _mm512_srli_epi32(a, 3); }
    static ivec madd16(ivec a, ivec b) { return _mm512_madd_epi16(a, b); }
    static ivec unpacklo8(ivec a, ivec b) { return _mm512_unpacklo_epi8(a, b); }
    static ivec unpackhi8(ivec a, ivec b) { return _mm512_unpackhi_epi8(a, b); }
    static ivec unpacklo16(ivec a, ivec b) { return _mm512_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm512_unpackhi_epi16(a, b); }
    static ivec min_u8(ivec a, ivec b) { return _mm512_min_epu8(a, b); }
    static ivec max_u8(ivec a, ivec b) { return _mm512_max_epu8(a, b); }
    static ivec adds_u8(ivec a, ivec b) { return _mm512_adds_epu8(a, b); }
    static ivec subs_u8(ivec a, ivec b) { return _mm512_subs_epu8(a, b); }
    static ivec adds_u16(ivec a, ivec b) { return _mm512_adds_epu16(a, b); }
    static ivec subs_u16(ivec a, ivec b) { return _mm512_subs_epu16(a, b); }
    static ivec and_i(ivec a, ivec b) { return _mm512_and_si512(a, b); }
    static ivec or_i(ivec a, ivec b) { return _mm512_or_si512(a, b); }
    static ivec packs32(ivec a, ivec b) { return _mm512_packs_epi32(a, b); }
    static ivec packus16(ivec a, ivec b) { return _mm512_packus_epi16(a, b); }

    // Word domain (native unsigned).
    static ivec pack_word(ivec a, ivec b) { return _mm512_packus_epi32(a, b); }
    static ivec wsign(ivec x) { return x; }
    static ivec wunsign(ivec x) { return x; }
    static ivec wsign32(ivec x) { return x; }
    static ivec wmin(ivec a, ivec b) { return _mm512_min_epu16(a, b); }
    static ivec wmax(ivec a, ivec b) { return _mm512_max_epu16(a, b); }
    static ivec wmaxval(uint16_t mv) { return _mm512_set1_epi16(static_cast<short>(mv)); }

    // Float.
    static fvec cvt_f(ivec x) { return _mm512_cvtepi32_ps(x); }
    static ivec cvt_i(fvec x) { return _mm512_cvtps_epi32(x); }
    static fvec set1_f(float x) { return _mm512_set1_ps(x); }
    static fvec fadd(fvec a, fvec b) { return _mm512_add_ps(a, b); }
    static fvec fsub(fvec a, fvec b) { return _mm512_sub_ps(a, b); }
    static fvec fmul(fvec a, fvec b) { return _mm512_mul_ps(a, b); }
    static fvec fmin(fvec a, fvec b) { return _mm512_min_ps(a, b); }
    static fvec fmax(fvec a, fvec b) { return _mm512_max_ps(a, b); }
    static fvec fsqrt(fvec a) { return _mm512_sqrt_ps(a); }
    static fvec fand(fvec a, fvec b) { return _mm512_and_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm512_fmadd_ps(a, b, c); }
    static fvec satmask(uint8_t saturate) { return _mm512_castsi512_ps(_mm512_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF)); }

    // Memory.
    static ivec byte_load(const uint8_t *p) { return _mm512_load_si512((const void *)p); }
    static ivec byte_loadu(const uint8_t *p) { return _mm512_loadu_si512((const void *)p); }
    static void byte_store(uint8_t *p, ivec x) { _mm512_store_si512((void *)p, x); }
    static ivec word_load(const uint16_t *p) { return _mm512_load_si512((const void *)p); }
    static ivec word_loadu(const uint16_t *p) { return _mm512_loadu_si512((const void *)p); }
    static void word_store(uint16_t *p, ivec x) { _mm512_store_si512((void *)p, x); }
    static fvec float_load(const float *p) { return _mm512_load_ps(p); }
    static fvec float_loadu(const float *p) { return _mm512_loadu_ps(p); }
    static void float_store(float *p, fvec x) { _mm512_store_ps(p, x); }

    // Half (float16): F16C convert on load/store; arithmetic stays float32.
    static fvec half_load(const uint16_t *p) { return _mm512_cvtph_ps(_mm256_load_si256((const __m256i *)p)); }
    static fvec half_loadu(const uint16_t *p) { return _mm512_cvtph_ps(_mm256_loadu_si256((const __m256i *)p)); }
    static void half_store(uint16_t *p, fvec x) { _mm256_store_si256((__m256i *)p, _mm512_cvtps_ph(x, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)); }

    // Edge inserts (whole-register shift-by-one with scalar insert).
    static ivec byte_shl_insert_lo(ivec x, uint8_t y)
    {
        return _mm512_or_si512(_mm512_zextsi128_si512(_mm_cvtsi32_si128(y)), mm512_bslli_whole<1>(x));
    }
    static ivec byte_shr_insert(ivec x, uint8_t y, unsigned idx)
    {
        __m512i iota = _mm512_set_epi8(
            63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48,
            47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
            31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
            15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        __mmask64 k = _mm512_cmpeq_epi8_mask(iota, _mm512_set1_epi8(static_cast<char>(idx)));
        return _mm512_mask_blend_epi8(k, mm512_bsrli_whole<1>(x), _mm512_set1_epi8(static_cast<char>(y)));
    }
    static ivec word_shl_insert_lo(ivec x, uint16_t y)
    {
        return _mm512_or_si512(_mm512_zextsi128_si512(_mm_cvtsi32_si128(y)), mm512_bslli_whole<2>(x));
    }
    static ivec word_shr_insert(ivec x, uint16_t y, unsigned idx)
    {
        __m512i iota = _mm512_set_epi16(
            31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
            15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        __mmask32 k = _mm512_cmpeq_epi16_mask(iota, _mm512_set1_epi16(static_cast<short>(idx)));
        return _mm512_mask_blend_epi16(k, mm512_bsrli_whole<2>(x), _mm512_set1_epi16(static_cast<short>(y)));
    }
    static fvec float_shl_insert_lo(fvec x, float y)
    {
        ivec shifted = mm512_bslli_whole<4>(_mm512_castps_si512(x));
        return _mm512_castsi512_ps(_mm512_or_si512(_mm512_zextsi128_si512(_mm_castps_si128(_mm_load_ss(&y))), shifted));
    }
    static fvec float_shr_insert(fvec x, float y, unsigned idx)
    {
        __m512i iota = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        __mmask16 k = _mm512_cmpeq_epi32_mask(iota, _mm512_set1_epi32(static_cast<int>(idx)));
        ivec shifted = mm512_bsrli_whole<4>(_mm512_castps_si512(x));
        return _mm512_castsi512_ps(_mm512_mask_blend_epi32(k, shifted, _mm512_castps_si512(_mm512_set1_ps(y))));
    }
};

} // namespace

#define BACKEND Backend_AVX512
#include "generic_impl.h"

VS_GENERIC_ENTRYPOINTS(avx512)
VS_GENERIC_ENTRYPOINTS_HALF(avx512)
