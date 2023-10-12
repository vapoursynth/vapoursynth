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

#include <immintrin.h>
#define VS_MERGE_IMPL
#include "../merge.h"
#include "VSHelper4.h"

#define MERGESHIFT 15
#define ROUND (1U << (MERGESHIFT - 1))

void vs_merge_byte_avx2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    __m256i w = _mm256_set1_epi16(weight.u);

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp1 + i)));
        __m256i v2 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp2 + i)));

        // tmp1 = (v2 - v1) * 2
        __m256i tmp1 = _mm256_slli_epi16(_mm256_sub_epi16(v2, v1), 1);
        // tmp2 = ((tmp1 * w) >> 16) + (((tmp1 * w) >> 15) & 1)
        __m256i tmp2 = _mm256_add_epi16(_mm256_add_epi16(_mm256_mulhi_epi16(tmp1, w), _mm256_srli_epi16(_mm256_mullo_epi16(tmp1, w), 15)), v1);
        // result = tmp2 >> 8
        __m256i result = _mm256_packus_epi16(tmp2, tmp2);
        result = _mm256_permute4x64_epi64(result, _MM_SHUFFLE(3, 1, 2, 0));

        _mm_store_si128((__m128i *)(dstp + i), _mm256_castsi256_si128(result));
    }
}

void vs_merge_word_avx2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    unsigned w2 = VSMIN(VSMAX(weight.u, 1U), (1U << MERGESHIFT) - 1);
    unsigned w1 = (1U << MERGESHIFT) - w2;
    __m256i w = _mm256_set1_epi32((w2 << 16) | w1);

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i tmplo, tmphi, result;

        v1 = _mm256_add_epi16(v1, _mm256_set1_epi16(INT16_MIN));
        v2 = _mm256_add_epi16(v2, _mm256_set1_epi16(INT16_MIN));
        tmplo = _mm256_unpacklo_epi16(v1, v2);
        tmphi = _mm256_unpackhi_epi16(v1, v2);

        // w1 * v1 + w2 * v2
        tmplo = _mm256_madd_epi16(w, tmplo);
        tmplo = _mm256_add_epi32(tmplo, _mm256_set1_epi32(ROUND));
        tmplo = _mm256_srai_epi32(tmplo, MERGESHIFT);

        tmphi = _mm256_madd_epi16(w, tmphi);
        tmphi = _mm256_add_epi32(tmphi, _mm256_set1_epi32(ROUND));
        tmphi = _mm256_srai_epi32(tmphi, MERGESHIFT);

        result = _mm256_packs_epi32(tmplo, tmphi);
        result = _mm256_sub_epi16(result, _mm256_set1_epi16(INT16_MIN));
        _mm256_store_si256((__m256i *)(dstp + i), result);
    }
}

void vs_merge_float_avx2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    __m256 w2 = _mm256_set1_ps(weight.f);
    __m256 w1 = _mm256_set1_ps(1.0f - weight.f);

    for (i = 0; i < n; i += 8) {
        __m256 v1 = _mm256_load_ps(srcp1 + i);
        __m256 v2 = _mm256_load_ps(srcp2 + i);
        _mm256_store_ps(dstp + i, _mm256_fmadd_ps(w1, v1, _mm256_mul_ps(w2, v2)));
    }
}


static __m256i div255_epu16(__m256i x)
{
    x = _mm256_mulhi_epu16(x, _mm256_set1_epi16(0x8081));
    x = _mm256_srli_epi16(x, 7);
    return x;
}

static __m256i divX_epu32(__m256i x, unsigned depth)
{
    __m256i lo = _mm256_unpacklo_epi32(x, x);
    __m256i hi = _mm256_unpackhi_epi32(x, x);
    __m256i div = _mm256_set1_epi32(div_table[depth - 9]);
    lo = _mm256_mul_epu32(lo, div);
    hi = _mm256_mul_epu32(hi, div);
    x = _mm256_castps_si256(_mm256_shuffle_ps(_mm256_castsi256_ps(lo), _mm256_castsi256_ps(hi), _MM_SHUFFLE(3, 1, 3, 1)));
    x = _mm256_srl_epi32(x, _mm_cvtsi32_si128(shift_table[depth - 9]));
    return x;
}

void vs_mask_merge_byte_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp1 + i)));
        __m256i v2 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp2 + i)));
        __m256i w2 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(maskp + i)));
        __m256i w1 = _mm256_sub_epi16(_mm256_set1_epi16(UINT8_MAX), w2);
        __m256i tmp1 = _mm256_mullo_epi16(v1, w1);
        __m256i tmp2 = _mm256_mullo_epi16(v2, w2);
        __m256i tmp;

        tmp = _mm256_add_epi16(_mm256_add_epi16(tmp1, tmp2), _mm256_set1_epi16(UINT8_MAX / 2));
        tmp = div255_epu16(tmp);

        tmp = _mm256_packus_epi16(tmp, tmp);
        tmp = _mm256_permute4x64_epi64(tmp, _MM_SHUFFLE(3, 1, 2, 0));
        _mm_store_si128((__m128i *)(dstp + i), _mm256_castsi256_si128(tmp));
    }
}

void vs_mask_merge_word_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;
    (void)offset;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i w2 = _mm256_load_si256((const __m256i *)(maskp + i));
        __m256i w1 = _mm256_sub_epi16(_mm256_set1_epi16(maxval), w2);

        __m256i tmp1lo = _mm256_mullo_epi16(w1, v1);
        __m256i tmp1hi = _mm256_mulhi_epu16(w1, v1);
        __m256i tmp2lo = _mm256_mullo_epi16(w2, v2);
        __m256i tmp2hi = _mm256_mulhi_epu16(w2, v2);

        __m256i tmp1d_lo = _mm256_unpacklo_epi16(tmp1lo, tmp1hi);
        __m256i tmp1d_hi = _mm256_unpackhi_epi16(tmp1lo, tmp1hi);
        __m256i tmp2d_lo = _mm256_unpacklo_epi16(tmp2lo, tmp2hi);
        __m256i tmp2d_hi = _mm256_unpackhi_epi16(tmp2lo, tmp2hi);
        __m256i tmp;

        tmp1d_lo = _mm256_add_epi32(tmp1d_lo, tmp2d_lo);
        tmp1d_lo = _mm256_add_epi32(tmp1d_lo, _mm256_set1_epi32(maxval / 2));
        tmp1d_hi = _mm256_add_epi32(tmp1d_hi, tmp2d_hi);
        tmp1d_hi = _mm256_add_epi32(tmp1d_hi, _mm256_set1_epi32(maxval / 2));

        tmp1d_lo = divX_epu32(tmp1d_lo, depth);
        tmp1d_hi = divX_epu32(tmp1d_hi, depth);
        tmp = _mm256_packus_epi32(tmp1d_lo, tmp1d_hi);
        _mm256_store_si256((__m256i *)(dstp + i), tmp);
    }
}

void vs_mask_merge_float_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 8) {
        __m256 v1 = _mm256_load_ps(srcp1 + i);
        __m256 v2 = _mm256_load_ps(srcp2 + i);
        __m256 w2 = _mm256_load_ps(maskp + i);
        __m256 diff = _mm256_sub_ps(v2, v1);
        __m256 result = _mm256_fmadd_ps(diff, w2, v1);
        _mm256_store_ps(dstp + i, result);
    }
}

void vs_mask_merge_premul_byte_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp1 + i)));
        __m256i v2 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(srcp2 + i)));
        __m256i w2 = _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i *)(maskp + i)));
        __m256i w1 = _mm256_sub_epi16(_mm256_set1_epi16(UINT8_MAX), w2);
        __m256i neg, sign, tmp;

        // Premultiply v1.
        tmp = _mm256_sub_epi16(v1, _mm256_set1_epi16(offset));
        sign = _mm256_cmpgt_epi16(_mm256_setzero_si256(), tmp);
        neg = _mm256_sub_epi16(_mm256_setzero_si256(), tmp);
        tmp = _mm256_blendv_epi8(tmp, neg, sign);

        tmp = _mm256_add_epi16(_mm256_mullo_epi16(tmp, w1), _mm256_set1_epi16(UINT8_MAX / 2));
        tmp = div255_epu16(tmp);

        neg = _mm256_sub_epi16(_mm256_setzero_si256(), tmp);
        tmp = _mm256_blendv_epi8(tmp, neg, sign);

        // Saturated add v1 (-128...255) to v2 (0...255).
        tmp = _mm256_add_epi16(tmp, v2);
        tmp = _mm256_packus_epi16(tmp, tmp);
        tmp = _mm256_permute4x64_epi64(tmp, _MM_SHUFFLE(3, 1, 2, 0));

        _mm_store_si128((__m128i *)(dstp + i), _mm256_castsi256_si128(tmp));
    }
}

void vs_mask_merge_premul_word_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i w2 = _mm256_load_si256((const __m256i *)(maskp + i));
        __m256i w1 = _mm256_sub_epi16(_mm256_set1_epi16(maxval), w2);
        __m256i neg, sign, tmp, tmp_lo, tmp_hi, tmpd_lo, tmpd_hi, negd, signd;

        // Premultiply v1.
        tmp = _mm256_sub_epi16(v1, _mm256_set1_epi16(offset));
        sign = _mm256_cmpgt_epi16(_mm256_set1_epi16(offset + INT16_MIN), _mm256_add_epi16(v1, _mm256_set1_epi16(INT16_MIN)));
        neg = _mm256_sub_epi16(_mm256_setzero_si256(), tmp);
        tmp = _mm256_blendv_epi8(tmp, neg, sign);

        tmp_lo = _mm256_mullo_epi16(w1, tmp);
        tmp_hi = _mm256_mulhi_epu16(w1, tmp);

        tmpd_lo = _mm256_unpacklo_epi16(tmp_lo, tmp_hi);
        tmpd_lo = _mm256_add_epi32(tmpd_lo, _mm256_set1_epi32(maxval / 2));
        tmpd_lo = divX_epu32(tmpd_lo, depth);
        signd = _mm256_unpacklo_epi16(sign, sign);
        negd = _mm256_sub_epi32(_mm256_setzero_si256(), tmpd_lo);
        tmpd_lo = _mm256_blendv_epi8(tmpd_lo, negd, signd);

        tmpd_hi = _mm256_unpackhi_epi16(tmp_lo, tmp_hi);
        tmpd_hi = _mm256_add_epi32(tmpd_hi, _mm256_set1_epi32(maxval / 2));
        tmpd_hi = divX_epu32(tmpd_hi, depth);
        signd = _mm256_unpackhi_epi16(sign, sign);
        negd = _mm256_sub_epi32(_mm256_setzero_si256(), tmpd_hi);
        tmpd_hi = _mm256_blendv_epi8(tmpd_hi, negd, signd);

        // Saturated add v1 (-32768...65535) to v2 (0...65535)
        tmpd_lo = _mm256_add_epi32(tmpd_lo, _mm256_unpacklo_epi16(v2, _mm256_setzero_si256()));
        tmpd_hi = _mm256_add_epi32(tmpd_hi, _mm256_unpackhi_epi16(v2, _mm256_setzero_si256()));
        tmp = _mm256_packus_epi32(tmpd_lo, tmpd_hi);
        tmp = _mm256_min_epu16(tmp, _mm256_set1_epi16(maxval));
        _mm256_store_si256((__m256i *)(dstp + i), tmp);
    }
}

void vs_mask_merge_premul_float_avx2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 8) {
        __m256 v1 = _mm256_load_ps(srcp1 + i);
        __m256 v2 = _mm256_load_ps(srcp2 + i);
        __m256 w1 = _mm256_sub_ps(_mm256_set1_ps(1.0f), _mm256_load_ps(maskp + i));
        __m256 result = _mm256_fmadd_ps(v1, w1, v2);
        _mm256_store_ps(dstp + i, result);
    }
}

void vs_makediff_byte_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 32) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i diff = _mm256_subs_epi8(_mm256_add_epi8(v1, _mm256_set1_epi8(INT8_MIN)), _mm256_add_epi8(v2, _mm256_set1_epi8(INT8_MIN)));
        diff = _mm256_sub_epi8(diff, _mm256_set1_epi8(INT8_MIN));
        _mm256_store_si256((__m256i *)(dstp + i), diff);
    }
}

void vs_makediff_word_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t maxval = (1 << (depth - 1)) - 1;
    int32_t minval = -maxval - 1;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i diff = _mm256_subs_epi16(_mm256_add_epi16(v1, _mm256_set1_epi16(minval)), _mm256_add_epi16(v2, _mm256_set1_epi16(minval)));
        diff = _mm256_min_epi16(_mm256_max_epi16(diff, _mm256_set1_epi16(minval)), _mm256_set1_epi16(maxval));
        diff = _mm256_sub_epi16(diff, _mm256_set1_epi16(minval));
        _mm256_store_si256((__m256i *)(dstp + i), diff);
    }
}

void vs_makediff_float_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 8) {
        __m256 v1 = _mm256_load_ps(srcp1 + i);
        __m256 v2 = _mm256_load_ps(srcp2 + i);
        _mm256_store_ps(dstp + i, _mm256_sub_ps(v1, v2));
    }
}

void vs_mergediff_byte_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 32) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i tmp = _mm256_adds_epi8(_mm256_add_epi8(v1, _mm256_set1_epi8(INT8_MIN)), _mm256_add_epi8(v2, _mm256_set1_epi8(INT8_MIN)));
        tmp = _mm256_sub_epi8(tmp, _mm256_set1_epi8(INT8_MIN));
        _mm256_store_si256((__m256i *)(dstp + i), tmp);
    }
}

void vs_mergediff_word_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t maxval = (1 << (depth - 1)) - 1;
    int32_t minval = -maxval - 1;

    for (i = 0; i < n; i += 16) {
        __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + i));
        __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + i));
        __m256i tmp = _mm256_adds_epi16(_mm256_add_epi16(v1, _mm256_set1_epi16(minval)), _mm256_add_epi16(v2, _mm256_set1_epi16(minval)));
        tmp = _mm256_min_epi16(_mm256_max_epi16(tmp, _mm256_set1_epi16(minval)), _mm256_set1_epi16(maxval));
        tmp = _mm256_sub_epi16(tmp, _mm256_set1_epi16(minval));
        _mm256_store_si256((__m256i *)(dstp + i), tmp);
    }
}

void vs_mergediff_float_avx2(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 8) {
        __m256 v1 = _mm256_load_ps(srcp1 + i);
        __m256 v2 = _mm256_load_ps(srcp2 + i);
        _mm256_store_ps(dstp + i, _mm256_add_ps(v1, v2));
    }
}
