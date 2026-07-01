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
 * AVX-512 (F+BW+DQ) merge kernels.
 *
 * Only the mask-merge and premultiply kernels are provided here: their
 * per-pixel magic-number division (divX_epu32) is ALU-bound, so 512-bit width
 * gives ~1.25-1.94x over AVX2 (byte 1.25x / 1.76x, word 1.71x / 1.94x).
 * The plain merge, makediff/mergediff and all float kernels are memory-bound
 * (AVX-512 within ~1.1x of AVX2), so those keep using the AVX2 path.
 *
 * Notes vs merge_avx2.c: byte kernels process 64 px/iter and packus interleaves
 * the four 128-bit lanes, so a single vpermq restores linear order before the
 * store; the word kernels are 128-bit-lane-local so need no fixup. The
 * sign-conditional negate in premultiply uses mask registers / the branchless
 * (x ^ s) - s idiom instead of vpblendvb, which AVX-512 lacks.
 */

#include <immintrin.h>
#define VS_MERGE_IMPL
#include "../merge.h"
#include "VSHelper4.h"

/* pack two vectors of 32x uint16 into 64x uint8 in linear order */
static __m512i pack64(__m512i a, __m512i b)
{
    __m512i idx = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
    return _mm512_permutexvar_epi64(idx, _mm512_packus_epi16(a, b));
}

static __m512i div255_epu16(__m512i x)
{
    x = _mm512_mulhi_epu16(x, _mm512_set1_epi16(0x8081));
    x = _mm512_srli_epi16(x, 7);
    return x;
}

static __m512i divX_epu32(__m512i x, unsigned depth)
{
    __m512i lo = _mm512_unpacklo_epi32(x, x);
    __m512i hi = _mm512_unpackhi_epi32(x, x);
    __m512i div = _mm512_set1_epi32(div_table[depth - 9]);
    lo = _mm512_mul_epu32(lo, div);
    hi = _mm512_mul_epu32(hi, div);
    x = _mm512_castps_si512(_mm512_shuffle_ps(_mm512_castsi512_ps(lo), _mm512_castsi512_ps(hi), _MM_SHUFFLE(3, 1, 3, 1)));
    x = _mm512_srl_epi32(x, _mm_cvtsi32_si128(shift_table[depth - 9]));
    return x;
}

void vs_mask_merge_byte_avx512(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 64) {
        __m512i v1a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp1 + i)));
        __m512i v1b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp1 + i + 32)));
        __m512i v2a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp2 + i)));
        __m512i v2b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp2 + i + 32)));
        __m512i w2a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(maskp + i)));
        __m512i w2b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(maskp + i + 32)));
        __m512i w1a = _mm512_sub_epi16(_mm512_set1_epi16(UINT8_MAX), w2a);
        __m512i w1b = _mm512_sub_epi16(_mm512_set1_epi16(UINT8_MAX), w2b);

        __m512i tmpa = _mm512_add_epi16(_mm512_add_epi16(_mm512_mullo_epi16(v1a, w1a), _mm512_mullo_epi16(v2a, w2a)), _mm512_set1_epi16(UINT8_MAX / 2));
        __m512i tmpb = _mm512_add_epi16(_mm512_add_epi16(_mm512_mullo_epi16(v1b, w1b), _mm512_mullo_epi16(v2b, w2b)), _mm512_set1_epi16(UINT8_MAX / 2));
        tmpa = div255_epu16(tmpa);
        tmpb = div255_epu16(tmpb);

        _mm512_store_si512((void *)(dstp + i), pack64(tmpa, tmpb));
    }
}

void vs_mask_merge_word_avx512(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;
    (void)offset;

    for (i = 0; i < n; i += 32) {
        __m512i v1 = _mm512_load_si512((const void *)(srcp1 + i));
        __m512i v2 = _mm512_load_si512((const void *)(srcp2 + i));
        __m512i w2 = _mm512_load_si512((const void *)(maskp + i));
        __m512i w1 = _mm512_sub_epi16(_mm512_set1_epi16(maxval), w2);

        __m512i tmp1lo = _mm512_mullo_epi16(w1, v1);
        __m512i tmp1hi = _mm512_mulhi_epu16(w1, v1);
        __m512i tmp2lo = _mm512_mullo_epi16(w2, v2);
        __m512i tmp2hi = _mm512_mulhi_epu16(w2, v2);

        __m512i tmp1d_lo = _mm512_unpacklo_epi16(tmp1lo, tmp1hi);
        __m512i tmp1d_hi = _mm512_unpackhi_epi16(tmp1lo, tmp1hi);
        __m512i tmp2d_lo = _mm512_unpacklo_epi16(tmp2lo, tmp2hi);
        __m512i tmp2d_hi = _mm512_unpackhi_epi16(tmp2lo, tmp2hi);
        __m512i tmp;

        tmp1d_lo = _mm512_add_epi32(tmp1d_lo, tmp2d_lo);
        tmp1d_lo = _mm512_add_epi32(tmp1d_lo, _mm512_set1_epi32(maxval / 2));
        tmp1d_hi = _mm512_add_epi32(tmp1d_hi, tmp2d_hi);
        tmp1d_hi = _mm512_add_epi32(tmp1d_hi, _mm512_set1_epi32(maxval / 2));

        tmp1d_lo = divX_epu32(tmp1d_lo, depth);
        tmp1d_hi = divX_epu32(tmp1d_hi, depth);
        tmp = _mm512_packus_epi32(tmp1d_lo, tmp1d_hi);
        _mm512_store_si512((void *)(dstp + i), tmp);
    }
}

static __m512i premul_byte_half(__m512i v1, __m512i v2, __m512i w2, __m512i offs)
{
    __m512i w1 = _mm512_sub_epi16(_mm512_set1_epi16(UINT8_MAX), w2);
    __m512i neg, tmp;
    __mmask32 sign;

    // Premultiply v1.
    tmp = _mm512_sub_epi16(v1, offs);
    sign = _mm512_cmpgt_epi16_mask(_mm512_setzero_si512(), tmp);
    neg = _mm512_sub_epi16(_mm512_setzero_si512(), tmp);
    tmp = _mm512_mask_blend_epi16(sign, tmp, neg);

    tmp = _mm512_add_epi16(_mm512_mullo_epi16(tmp, w1), _mm512_set1_epi16(UINT8_MAX / 2));
    tmp = div255_epu16(tmp);

    neg = _mm512_sub_epi16(_mm512_setzero_si512(), tmp);
    tmp = _mm512_mask_blend_epi16(sign, tmp, neg);

    // Saturated add v1 (-128...255) to v2 (0...255).
    return _mm512_add_epi16(tmp, v2);
}

void vs_mask_merge_premul_byte_avx512(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    __m512i offs = _mm512_set1_epi16(offset);

    (void)depth;

    for (i = 0; i < n; i += 64) {
        __m512i v1a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp1 + i)));
        __m512i v1b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp1 + i + 32)));
        __m512i v2a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp2 + i)));
        __m512i v2b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(srcp2 + i + 32)));
        __m512i w2a = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(maskp + i)));
        __m512i w2b = _mm512_cvtepu8_epi16(_mm256_load_si256((const __m256i *)(maskp + i + 32)));

        __m512i tmpa = premul_byte_half(v1a, v2a, w2a, offs);
        __m512i tmpb = premul_byte_half(v1b, v2b, w2b, offs);

        _mm512_store_si512((void *)(dstp + i), pack64(tmpa, tmpb));
    }
}

void vs_mask_merge_premul_word_avx512(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;

    for (i = 0; i < n; i += 32) {
        __m512i v1 = _mm512_load_si512((const void *)(srcp1 + i));
        __m512i v2 = _mm512_load_si512((const void *)(srcp2 + i));
        __m512i w2 = _mm512_load_si512((const void *)(maskp + i));
        __m512i w1 = _mm512_sub_epi16(_mm512_set1_epi16(maxval), w2);
        __m512i sign, signd, tmp, tmp_lo, tmp_hi, tmpd_lo, tmpd_hi;

        // Premultiply v1. sign = (v1 - offset) < 0, computed unsigned-safely.
        __mmask32 signm = _mm512_cmpgt_epi16_mask(_mm512_set1_epi16(offset + INT16_MIN), _mm512_add_epi16(v1, _mm512_set1_epi16(INT16_MIN)));
        sign = _mm512_movm_epi16(signm);
        tmp = _mm512_sub_epi16(v1, _mm512_set1_epi16(offset));
        tmp = _mm512_sub_epi16(_mm512_xor_si512(tmp, sign), sign); // sign ? -tmp : tmp

        tmp_lo = _mm512_mullo_epi16(w1, tmp);
        tmp_hi = _mm512_mulhi_epu16(w1, tmp);

        tmpd_lo = _mm512_unpacklo_epi16(tmp_lo, tmp_hi);
        tmpd_lo = _mm512_add_epi32(tmpd_lo, _mm512_set1_epi32(maxval / 2));
        tmpd_lo = divX_epu32(tmpd_lo, depth);
        signd = _mm512_unpacklo_epi16(sign, sign);
        tmpd_lo = _mm512_sub_epi32(_mm512_xor_si512(tmpd_lo, signd), signd);

        tmpd_hi = _mm512_unpackhi_epi16(tmp_lo, tmp_hi);
        tmpd_hi = _mm512_add_epi32(tmpd_hi, _mm512_set1_epi32(maxval / 2));
        tmpd_hi = divX_epu32(tmpd_hi, depth);
        signd = _mm512_unpackhi_epi16(sign, sign);
        tmpd_hi = _mm512_sub_epi32(_mm512_xor_si512(tmpd_hi, signd), signd);

        // Saturated add v1 (-32768...65535) to v2 (0...65535)
        tmpd_lo = _mm512_add_epi32(tmpd_lo, _mm512_unpacklo_epi16(v2, _mm512_setzero_si512()));
        tmpd_hi = _mm512_add_epi32(tmpd_hi, _mm512_unpackhi_epi16(v2, _mm512_setzero_si512()));
        tmp = _mm512_packus_epi32(tmpd_lo, tmpd_hi);
        tmp = _mm512_min_epu16(tmp, _mm512_set1_epi16(maxval));
        _mm512_store_si512((void *)(dstp + i), tmp);
    }
}
