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

#include "sse2neon.h"
#define VS_MERGE_IMPL
#include "../merge.h"
#include "VSHelper4.h"

#define MERGESHIFT 15
#define ROUND (1U << (MERGESHIFT - 1))

void vs_merge_byte_neon(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    __m128i w = _mm_set1_epi16(weight.u);

    for (i = 0; i < n; i += 16) {
        __m128i v1b = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2b = _mm_load_si128((const __m128i *)(srcp2 + i));

        __m128i v1w_lo = _mm_unpacklo_epi8(v1b, _mm_setzero_si128());
        __m128i v2w_lo = _mm_unpacklo_epi8(v2b, _mm_setzero_si128());
        __m128i v1w_hi = _mm_unpackhi_epi8(v1b, _mm_setzero_si128());
        __m128i v2w_hi = _mm_unpackhi_epi8(v2b, _mm_setzero_si128());

        // tmp1 = (v2 - v1) * 2
        __m128i tmp1lo = _mm_slli_epi16(_mm_sub_epi16(v2w_lo, v1w_lo), 1);
        __m128i tmp1hi = _mm_slli_epi16(_mm_sub_epi16(v2w_hi, v1w_hi), 1);

        // tmp2 = ((tmp1 * w) >> 16) + (((tmp1 * w) >> 15) & 1)
        __m128i tmp2lo = _mm_add_epi16(_mm_add_epi16(_mm_mulhi_epi16(tmp1lo, w), _mm_srli_epi16(_mm_mullo_epi16(tmp1lo, w), 15)), v1w_lo);
        __m128i tmp2hi = _mm_add_epi16(_mm_add_epi16(_mm_mulhi_epi16(tmp1hi, w), _mm_srli_epi16(_mm_mullo_epi16(tmp1hi, w), 15)), v1w_hi);

        // result = tmp2 >> 8
        __m128i result = _mm_packus_epi16(tmp2lo, tmp2hi);
        _mm_store_si128((__m128i *)(dstp + i), result);
    }
}

void vs_merge_word_neon(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    unsigned w2 = VSMIN(VSMAX(weight.u, 1U), (1U << MERGESHIFT) - 1);
    unsigned w1 = (1U << MERGESHIFT) - w2;
    __m128i w = _mm_set1_epi32((w2 << 16) | w1);

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i tmplo, tmphi, result;

        v1 = _mm_add_epi16(v1, _mm_set1_epi16(INT16_MIN));
        v2 = _mm_add_epi16(v2, _mm_set1_epi16(INT16_MIN));
        tmplo = _mm_unpacklo_epi16(v1, v2);
        tmphi = _mm_unpackhi_epi16(v1, v2);

        // w1 * v1 + w2 * v2
        tmplo = _mm_madd_epi16(w, tmplo);
        tmplo = _mm_add_epi32(tmplo, _mm_set1_epi32(ROUND));
        tmplo = _mm_srai_epi32(tmplo, MERGESHIFT);

        tmphi = _mm_madd_epi16(w, tmphi);
        tmphi = _mm_add_epi32(tmphi, _mm_set1_epi32(ROUND));
        tmphi = _mm_srai_epi32(tmphi, MERGESHIFT);

        result = _mm_packs_epi32(tmplo, tmphi);
        result = _mm_sub_epi16(result, _mm_set1_epi16(INT16_MIN));
        _mm_store_si128((__m128i *)(dstp + i), result);
    }
}

void vs_merge_float_neon(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    __m128 w2 = _mm_set_ps1(weight.f);
    __m128 w1 = _mm_set_ps1(1.0f - weight.f);

    for (i = 0; i < n; i += 4) {
        __m128 v1 = _mm_load_ps(srcp1 + i);
        __m128 v2 = _mm_load_ps(srcp2 + i);
        v1 = _mm_mul_ps(w1, v1);
        v2 = _mm_mul_ps(w2, v2);
        _mm_store_ps(dstp + i, _mm_add_ps(v1, v2));
    }
}


static __m128i div255_epu16(__m128i x)
{
    x = _mm_mulhi_epu16(x, _mm_set1_epi16(0x8081));
    x = _mm_srli_epi16(x, 7);
    return x;
}

static __m128i divX_epu32(__m128i x, unsigned depth)
{
    __m128i lo = _mm_unpacklo_epi32(x, x);
    __m128i hi = _mm_unpackhi_epi32(x, x);
    __m128i div = _mm_set1_epi32(div_table[depth - 9]);
    lo = _mm_mul_epu32(lo, div);
    hi = _mm_mul_epu32(hi, div);
    x = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(lo), _mm_castsi128_ps(hi), _MM_SHUFFLE(3, 1, 3, 1)));
    x = _mm_srl_epi32(x, _mm_cvtsi32_si128(shift_table[depth - 9]));
    return x;
}

void vs_mask_merge_byte_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp1 + i)), _mm_setzero_si128());
        __m128i v2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp2 + i)), _mm_setzero_si128());
        __m128i w2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(maskp + i)), _mm_setzero_si128());
        __m128i w1 = _mm_sub_epi16(_mm_set1_epi16(UINT8_MAX), w2);
        __m128i tmp1 = _mm_mullo_epi16(v1, w1);
        __m128i tmp2 = _mm_mullo_epi16(v2, w2);
        __m128i tmp;

        tmp = _mm_add_epi16(_mm_add_epi16(tmp1, tmp2), _mm_set1_epi16(UINT8_MAX / 2));
        tmp = div255_epu16(tmp);
        _mm_storel_epi64((__m128i *)(dstp + i), _mm_packus_epi16(tmp, tmp));
    }
}

void vs_mask_merge_word_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;
    (void)offset;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i w2 = _mm_load_si128((const __m128i *)(maskp + i));
        __m128i w1 = _mm_sub_epi16(_mm_set1_epi16(maxval), w2);

        __m128i tmp1lo = _mm_mullo_epi16(w1, v1);
        __m128i tmp1hi = _mm_mulhi_epu16(w1, v1);
        __m128i tmp2lo = _mm_mullo_epi16(w2, v2);
        __m128i tmp2hi = _mm_mulhi_epu16(w2, v2);

        __m128i tmp1d_lo = _mm_unpacklo_epi16(tmp1lo, tmp1hi);
        __m128i tmp1d_hi = _mm_unpackhi_epi16(tmp1lo, tmp1hi);
        __m128i tmp2d_lo = _mm_unpacklo_epi16(tmp2lo, tmp2hi);
        __m128i tmp2d_hi = _mm_unpackhi_epi16(tmp2lo, tmp2hi);
        __m128i tmp;

        tmp1d_lo = _mm_add_epi32(tmp1d_lo, tmp2d_lo);
        tmp1d_lo = _mm_add_epi32(tmp1d_lo, _mm_set1_epi32(maxval / 2));
        tmp1d_hi = _mm_add_epi32(tmp1d_hi, tmp2d_hi);
        tmp1d_hi = _mm_add_epi32(tmp1d_hi, _mm_set1_epi32(maxval / 2));

        tmp1d_lo = divX_epu32(tmp1d_lo, depth);
        tmp1d_lo = _mm_add_epi32(tmp1d_lo, _mm_set1_epi32(INT16_MIN));
        tmp1d_hi = divX_epu32(tmp1d_hi, depth);
        tmp1d_hi = _mm_add_epi32(tmp1d_hi, _mm_set1_epi32(INT16_MIN));
        tmp = _mm_packs_epi32(tmp1d_lo, tmp1d_hi);
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));
        _mm_store_si128((__m128i *)(dstp + i), tmp);
    }
}

void vs_mask_merge_float_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 4) {
        __m128 v1 = _mm_load_ps(srcp1 + i);
        __m128 v2 = _mm_load_ps(srcp2 + i);
        __m128 w2 = _mm_load_ps(maskp + i);
        __m128 diff = _mm_sub_ps(v2, v1);
        __m128 result = _mm_add_ps(v1, _mm_mul_ps(diff, w2));
        _mm_store_ps(dstp + i, result);
    }
}

void vs_mask_merge_premul_byte_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp1 + i)), _mm_setzero_si128());
        __m128i v2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp2 + i)), _mm_setzero_si128());
        __m128i w2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(maskp + i)), _mm_setzero_si128());
        __m128i w1 = _mm_sub_epi16(_mm_set1_epi16(UINT8_MAX), w2);
        __m128i neg, sign, tmp;

        // Premultiply v1.
        tmp = _mm_sub_epi16(v1, _mm_set1_epi16(offset));
        sign = _mm_cmplt_epi16(tmp, _mm_setzero_si128());
        neg = _mm_sub_epi16(_mm_setzero_si128(), tmp);
        tmp = _mm_or_si128(_mm_andnot_si128(sign, tmp), _mm_and_si128(sign, neg));

        tmp = _mm_add_epi16(_mm_mullo_epi16(tmp, w1), _mm_set1_epi16(UINT8_MAX / 2));
        tmp = div255_epu16(tmp);

        neg = _mm_sub_epi16(_mm_setzero_si128(), tmp);
        tmp = _mm_or_si128(_mm_andnot_si128(sign, tmp), _mm_and_si128(sign, neg));

        // Saturated add v1 (-128...255) to v2 (0...255)
        tmp = _mm_add_epi16(tmp, v2);
        tmp = _mm_packus_epi16(tmp, tmp);
        _mm_storel_epi64((__m128i *)(dstp + i), tmp);
    }
}

void vs_mask_merge_premul_word_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i w2 = _mm_load_si128((const __m128i *)(maskp + i));
        __m128i w1 = _mm_sub_epi16(_mm_set1_epi16(maxval), w2);
        __m128i neg, sign, tmp, tmp_lo, tmp_hi, tmpd_lo, tmpd_hi, negd, signd;

        // Premultiply v1.
        tmp = _mm_sub_epi16(v1, _mm_set1_epi16(offset));
        sign = _mm_cmplt_epi16(_mm_add_epi16(v1, _mm_set1_epi16(INT16_MIN)), _mm_set1_epi16(offset + INT16_MIN));
        neg = _mm_sub_epi16(_mm_setzero_si128(), tmp);
        tmp = _mm_or_si128(_mm_andnot_si128(sign, tmp), _mm_and_si128(sign, neg));

        tmp_lo = _mm_mullo_epi16(w1, tmp);
        tmp_hi = _mm_mulhi_epu16(w1, tmp);

        tmpd_lo = _mm_unpacklo_epi16(tmp_lo, tmp_hi);
        tmpd_lo = _mm_add_epi32(tmpd_lo, _mm_set1_epi32(maxval / 2));
        tmpd_lo = divX_epu32(tmpd_lo, depth);
        signd = _mm_unpacklo_epi16(sign, sign);
        negd = _mm_sub_epi32(_mm_setzero_si128(), tmpd_lo);
        tmpd_lo = _mm_or_si128(_mm_andnot_si128(signd, tmpd_lo), _mm_and_si128(signd, negd));

        tmpd_hi = _mm_unpackhi_epi16(tmp_lo, tmp_hi);
        tmpd_hi = _mm_add_epi32(tmpd_hi, _mm_set1_epi32(maxval / 2));
        tmpd_hi = divX_epu32(tmpd_hi, depth);
        signd = _mm_unpackhi_epi16(sign, sign);
        negd = _mm_sub_epi32(_mm_setzero_si128(), tmpd_hi);
        tmpd_hi = _mm_or_si128(_mm_andnot_si128(signd, tmpd_hi), _mm_and_si128(signd, negd));

        // Saturated add v1 (-32768...65535) to v2 (0...65535)
        tmpd_lo = _mm_add_epi32(tmpd_lo, _mm_unpacklo_epi16(v2, _mm_setzero_si128()));
        tmpd_lo = _mm_add_epi32(tmpd_lo, _mm_set1_epi32(INT16_MIN));
        tmpd_hi = _mm_add_epi32(tmpd_hi, _mm_unpackhi_epi16(v2, _mm_setzero_si128()));
        tmpd_hi = _mm_add_epi32(tmpd_hi, _mm_set1_epi32(INT16_MIN));
        tmp = _mm_packs_epi32(tmpd_lo, tmpd_hi);

        tmp = _mm_min_epi16(tmp, _mm_set1_epi16(maxval + INT16_MIN));
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));
        _mm_store_si128((__m128i *)(dstp + i), tmp);
    }
}

void vs_mask_merge_premul_float_neon(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i += 4) {
        __m128 v1 = _mm_load_ps(srcp1 + i);
        __m128 v2 = _mm_load_ps(srcp2 + i);
        __m128 w1 = _mm_sub_ps(_mm_set_ps1(1.0f), _mm_load_ps(maskp + i));
        __m128 result = _mm_add_ps(_mm_mul_ps(w1, v1), v2);
        _mm_store_ps(dstp + i, result);
    }
}

void vs_makediff_byte_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 16) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i diff = _mm_subs_epi8(_mm_add_epi8(v1, _mm_set1_epi8(INT8_MIN)), _mm_add_epi8(v2, _mm_set1_epi8(INT8_MIN)));
        diff = _mm_sub_epi8(diff, _mm_set1_epi8(INT8_MIN));
        _mm_store_si128((__m128i *)(dstp + i), diff);
    }
}

void vs_makediff_word_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t maxval = (1 << (depth - 1)) - 1;
    int32_t minval = -maxval - 1;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i diff = _mm_subs_epi16(_mm_add_epi16(v1, _mm_set1_epi16(minval)), _mm_add_epi16(v2, _mm_set1_epi16(minval)));
        diff = _mm_min_epi16(_mm_max_epi16(diff, _mm_set1_epi16(minval)), _mm_set1_epi16(maxval));
        diff = _mm_sub_epi16(diff, _mm_set1_epi16(minval));
        _mm_store_si128((__m128i *)(dstp + i), diff);
    }
}

void vs_makediff_float_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 4) {
        __m128 v1 = _mm_load_ps(srcp1 + i);
        __m128 v2 = _mm_load_ps(srcp2 + i);
        _mm_store_ps(dstp + i, _mm_sub_ps(v1, v2));
    }
}

void vs_mergediff_byte_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 16) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i tmp = _mm_adds_epi8(_mm_add_epi8(v1, _mm_set1_epi8(INT8_MIN)), _mm_add_epi8(v2, _mm_set1_epi8(INT8_MIN)));
        tmp = _mm_sub_epi8(tmp, _mm_set1_epi8(INT8_MIN));
        _mm_store_si128((__m128i *)(dstp + i), tmp);
    }
}

void vs_mergediff_word_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t maxval = (1 << (depth - 1)) - 1;
    int32_t minval = -maxval - 1;

    for (i = 0; i < n; i += 8) {
        __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + i));
        __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + i));
        __m128i tmp = _mm_adds_epi16(_mm_add_epi16(v1, _mm_set1_epi16(minval)), _mm_add_epi16(v2, _mm_set1_epi16(minval)));
        tmp = _mm_min_epi16(_mm_max_epi16(tmp, _mm_set1_epi16(minval)), _mm_set1_epi16(maxval));
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(minval));
        _mm_store_si128((__m128i *)(dstp + i), tmp);
    }
}

void vs_mergediff_float_neon(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i += 4) {
        __m128 v1 = _mm_load_ps(srcp1 + i);
        __m128 v2 = _mm_load_ps(srcp2 + i);
        _mm_store_ps(dstp + i, _mm_add_ps(v1, v2));
    }
}
