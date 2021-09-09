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

#include <math.h>
#include <emmintrin.h>
#include "../planestats.h"

static const uint8_t ascend8[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const uint16_t ascend16[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const uint32_t ascend32[4] = { 0, 1, 2, 3 };

static unsigned hmax_epu8(__m128i x)
{
    x = _mm_max_epu8(x, _mm_srli_si128(x, 8));
    x = _mm_max_epu8(x, _mm_srli_si128(x, 4));
    x = _mm_max_epu8(x, _mm_srli_si128(x, 2));
    x = _mm_max_epu8(x, _mm_srli_si128(x, 1));
    return _mm_cvtsi128_si32(x) & 0xFF;
}

static unsigned hmin_epu8(__m128i x)
{
    x = _mm_min_epu8(x, _mm_srli_si128(x, 8));
    x = _mm_min_epu8(x, _mm_srli_si128(x, 4));
    x = _mm_min_epu8(x, _mm_srli_si128(x, 2));
    x = _mm_min_epu8(x, _mm_srli_si128(x, 1));
    return _mm_cvtsi128_si32(x) & 0xFF;
}

static int hmax_epi16(__m128i x)
{
    x = _mm_max_epi16(x, _mm_srli_si128(x, 8));
    x = _mm_max_epi16(x, _mm_srli_si128(x, 4));
    x = _mm_max_epi16(x, _mm_srli_si128(x, 2));
    return (int16_t)_mm_extract_epi16(x, 0);
}

static int hmin_epi16(__m128i x)
{
    x = _mm_min_epi16(x, _mm_srli_si128(x, 8));
    x = _mm_min_epi16(x, _mm_srli_si128(x, 4));
    x = _mm_min_epi16(x, _mm_srli_si128(x, 2));
    return (int16_t)_mm_extract_epi16(x, 0);
}

static float hmax_ps(__m128 x)
{
    x = _mm_max_ps(x, _mm_shuffle_ps(x, x, _MM_SHUFFLE(1, 0, 3, 2)));
    x = _mm_max_ps(x, _mm_shuffle_ps(x, x, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtss_f32(x);
}

static float hmin_ps(__m128 x)
{
    x = _mm_min_ps(x, _mm_shuffle_ps(x, x, _MM_SHUFFLE(1, 0, 3, 2)));
    x = _mm_min_ps(x, _mm_shuffle_ps(x, x, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtss_f32(x);
}

static double hadd_pd(__m128d x)
{
    return _mm_cvtsd_f64(_mm_add_sd(x, _mm_unpackhi_pd(x, x)));
}


void vs_plane_stats_1_byte_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~15;
    unsigned x, y;

    __m128i mmin = _mm_set1_epi8(UINT8_MAX);
    __m128i mmax = _mm_setzero_si128();
    __m128i macc = _mm_setzero_si128();
    __m128i mask = _mm_cmplt_epi8(_mm_loadu_si128((const __m128i *)ascend8), _mm_set1_epi8(width % 16));
    __m128i onesmask = _mm_andnot_si128(mask, _mm_set1_epi8(UINT8_MAX));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 16) {
            __m128i v = _mm_load_si128((const __m128i *)(srcp + x));
            mmin = _mm_min_epu8(mmin, v);
            mmax = _mm_max_epu8(mmax, v);
            macc = _mm_add_epi64(macc, _mm_sad_epu8(v, _mm_setzero_si128()));
        }
        if (width != tail) {
            __m128i v = _mm_and_si128(_mm_load_si128((const __m128i *)(srcp + tail)), mask);
            mmin = _mm_min_epu8(mmin, _mm_or_si128(v, onesmask));
            mmax = _mm_max_epu8(mmax, v);
            macc = _mm_add_epi64(macc, _mm_sad_epu8(v, _mm_setzero_si128()));
        }
        srcp += stride;
    }

    stats->i.min = hmin_epu8(mmin);
    stats->i.max = hmax_epu8(mmax);
    _mm_storel_epi64((__m128i *)&stats->i.acc, _mm_add_epi64(macc, _mm_srli_si128(macc, 8)));
}

void vs_plane_stats_1_word_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~7;
    unsigned x, y;

    __m128i mmin = _mm_set1_epi16(INT16_MAX);
    __m128i mmax = _mm_set1_epi16(INT16_MIN);
    __m128i macc_lo = _mm_setzero_si128();
    __m128i macc_hi = _mm_setzero_si128();
    __m128i mask = _mm_cmplt_epi16(_mm_loadu_si128((const __m128i *)ascend16), _mm_set1_epi16(width % 8));
    __m128i onesmask = _mm_andnot_si128(mask, _mm_set1_epi16(UINT16_MAX));
    __m128i low8mask = _mm_set1_epi16(0xFF);
    __m128i tmp;

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 8) {
            __m128i v = _mm_load_si128((const __m128i *)((const uint16_t *)srcp + x));
            __m128i v_signed = _mm_add_epi16(v, _mm_set1_epi16(INT16_MIN));
            mmin = _mm_min_epi16(mmin, v_signed);
            mmax = _mm_max_epi16(mmax, v_signed);

            macc_lo = _mm_add_epi64(macc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, v), _mm_setzero_si128()));
            macc_hi = _mm_add_epi64(macc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, v), _mm_setzero_si128()));
        }
        if (width != tail) {
            __m128i v = _mm_and_si128(_mm_load_si128((const __m128i *)((const uint16_t *)srcp + tail)), mask);
            mmin = _mm_min_epi16(mmin, _mm_add_epi16(_mm_or_si128(v, onesmask), _mm_set1_epi16(INT16_MIN)));
            mmax = _mm_max_epi16(mmax, _mm_add_epi16(v, _mm_set1_epi16(INT16_MIN)));

            macc_lo = _mm_add_epi64(macc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, v), _mm_setzero_si128()));
            macc_hi = _mm_add_epi64(macc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, v), _mm_setzero_si128()));
        }
        srcp += stride;
    }

    stats->i.min = hmin_epi16(mmin) - INT16_MIN;
    stats->i.max = hmax_epi16(mmax) - INT16_MIN;

    tmp = _mm_add_epi64(_mm_unpacklo_epi64(macc_lo, macc_hi), _mm_unpackhi_epi64(macc_lo, macc_hi));
    tmp = _mm_add_epi64(tmp, _mm_slli_epi64(_mm_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.acc, tmp);
}

void vs_plane_stats_1_float_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~3;
    unsigned x, y;

    __m128 fmmin = _mm_set_ps1(INFINITY);
    __m128 fmmax = _mm_set_ps1(-INFINITY);
    __m128d fmacc = _mm_setzero_pd();
    __m128 mask = _mm_castsi128_ps(_mm_cmplt_epi32(_mm_loadu_si128((const __m128i *)ascend32), _mm_set1_epi32(width % 4)));
    __m128 posmask = _mm_andnot_ps(mask, _mm_set_ps1(INFINITY));
    __m128 negmask = _mm_andnot_ps(mask, _mm_set_ps1(-INFINITY));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 4) {
            __m128 v = _mm_load_ps((const float *)srcp + x);
            fmmin = _mm_min_ps(fmmin, v);
            fmmax = _mm_max_ps(fmmax, v);
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(v));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(_mm_movehl_ps(v, v)));
        }
        if (width != tail) {
            __m128 v = _mm_and_ps(_mm_load_ps((const float *)srcp + tail), mask);
            fmmin = _mm_min_ps(fmmin, _mm_or_ps(v, posmask));
            fmmax = _mm_max_ps(fmmax, _mm_or_ps(v, negmask));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(v));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(_mm_movehl_ps(v, v)));
        }
        srcp += stride;
    }

    stats->f.min = hmin_ps(fmmin);
    stats->f.max = hmax_ps(fmmax);
    stats->f.acc = hadd_pd(fmacc);
}

void vs_plane_stats_2_byte_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~15;
    unsigned x, y;

    __m128i mmin = _mm_set1_epi8(UINT8_MAX);
    __m128i mmax = _mm_setzero_si128();
    __m128i macc = _mm_setzero_si128();
    __m128i mdiffacc = _mm_setzero_si128();
    __m128i mask = _mm_cmplt_epi8(_mm_loadu_si128((const __m128i *)ascend8), _mm_set1_epi8(width % 16));
    __m128i onesmask = _mm_andnot_si128(mask, _mm_set1_epi8(UINT8_MAX));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 16) {
            __m128i v1 = _mm_load_si128((const __m128i *)(srcp1 + x));
            __m128i v2 = _mm_load_si128((const __m128i *)(srcp2 + x));
            mmin = _mm_min_epu8(mmin, v1);
            mmax = _mm_max_epu8(mmax, v1);
            macc = _mm_add_epi64(macc, _mm_sad_epu8(v1, _mm_setzero_si128()));
            mdiffacc = _mm_add_epi64(mdiffacc, _mm_sad_epu8(v1, v2));
        }
        if (width != tail) {
            __m128i v1 = _mm_and_si128(_mm_load_si128((const __m128i *)(srcp1 + tail)), mask);
            __m128i v2 = _mm_and_si128(_mm_load_si128((const __m128i *)(srcp2 + tail)), mask);
            mmin = _mm_min_epu8(mmin, _mm_or_si128(v1, onesmask));
            mmax = _mm_max_epu8(mmax, v1);
            macc = _mm_add_epi64(macc, _mm_sad_epu8(v1, _mm_setzero_si128()));
            mdiffacc = _mm_add_epi64(mdiffacc, _mm_sad_epu8(v1, v2));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = hmin_epu8(mmin);
    stats->i.max = hmax_epu8(mmax);
    _mm_storel_epi64((__m128i *)&stats->i.acc, _mm_add_epi64(macc, _mm_srli_si128(macc, 8)));
    _mm_storel_epi64((__m128i *) & stats->i.diffacc, _mm_add_epi64(mdiffacc, _mm_srli_si128(mdiffacc, 8)));
}

void vs_plane_stats_2_word_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~7;
    unsigned x, y;

    __m128i mmin = _mm_set1_epi16(INT16_MAX);
    __m128i mmax = _mm_set1_epi16(INT16_MIN);
    __m128i macc_lo = _mm_setzero_si128();
    __m128i macc_hi = _mm_setzero_si128();
    __m128i mdiffacc_lo = _mm_setzero_si128();
    __m128i mdiffacc_hi = _mm_setzero_si128();
    __m128i mask = _mm_cmplt_epi16(_mm_loadu_si128((const __m128i *)ascend16), _mm_set1_epi16(width % 8));
    __m128i onesmask = _mm_andnot_si128(mask, _mm_set1_epi16(UINT16_MAX));
    __m128i low8mask = _mm_set1_epi16(0xFF);
    __m128i tmp;

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 8) {
            __m128i v1 = _mm_load_si128((const __m128i *)((const uint16_t *)srcp1 + x));
            __m128i v2 = _mm_load_si128((const __m128i *)((const uint16_t *)srcp2 + x));
            __m128i v1_signed = _mm_add_epi16(v1, _mm_set1_epi16(INT16_MIN));
            __m128i udiff = _mm_or_si128(_mm_subs_epu16(v1, v2), _mm_subs_epu16(v2, v1));

            mmin = _mm_min_epi16(mmin, v1_signed);
            mmax = _mm_max_epi16(mmax, v1_signed);

            macc_lo = _mm_add_epi64(macc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, v1), _mm_setzero_si128()));
            macc_hi = _mm_add_epi64(macc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, v1), _mm_setzero_si128()));

            mdiffacc_lo = _mm_add_epi64(mdiffacc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, udiff), _mm_setzero_si128()));
            mdiffacc_hi = _mm_add_epi64(mdiffacc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, udiff), _mm_setzero_si128()));
        }
        if (width != tail) {
            __m128i v1 = _mm_and_si128(_mm_load_si128((const __m128i *)((const uint16_t *)srcp1 + tail)), mask);
            __m128i v2 = _mm_and_si128(_mm_load_si128((const __m128i *)((const uint16_t *)srcp2 + tail)), mask);
            __m128i udiff = _mm_or_si128(_mm_subs_epu16(v1, v2), _mm_subs_epu16(v2, v1));

            mmin = _mm_min_epi16(mmin, _mm_add_epi16(_mm_or_si128(v1, onesmask), _mm_set1_epi16(INT16_MIN)));
            mmax = _mm_max_epi16(mmax, _mm_add_epi16(v1, _mm_set1_epi16(INT16_MIN)));

            macc_lo = _mm_add_epi64(macc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, v1), _mm_setzero_si128()));
            macc_hi = _mm_add_epi64(macc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, v1), _mm_setzero_si128()));

            mdiffacc_lo = _mm_add_epi64(mdiffacc_lo, _mm_sad_epu8(_mm_and_si128(low8mask, udiff), _mm_setzero_si128()));
            mdiffacc_hi = _mm_add_epi64(mdiffacc_hi, _mm_sad_epu8(_mm_andnot_si128(low8mask, udiff), _mm_setzero_si128()));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = hmin_epi16(mmin) - INT16_MIN;
    stats->i.max = hmax_epi16(mmax) - INT16_MIN;

    tmp = _mm_add_epi64(_mm_unpacklo_epi64(macc_lo, macc_hi), _mm_unpackhi_epi64(macc_lo, macc_hi));
    tmp = _mm_add_epi64(tmp, _mm_slli_epi64(_mm_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.acc, tmp);

    tmp = _mm_add_epi64(_mm_unpacklo_epi64(mdiffacc_lo, mdiffacc_hi), _mm_unpackhi_epi64(mdiffacc_lo, mdiffacc_hi));
    tmp = _mm_add_epi64(tmp, _mm_slli_epi64(_mm_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.diffacc, tmp);
}

void vs_plane_stats_2_float_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~3;
    unsigned x, y;

    __m128 fmmin = _mm_set_ps1(INFINITY);
    __m128 fmmax = _mm_set_ps1(-INFINITY);
    __m128d fmacc = _mm_setzero_pd();
    __m128d fmdiffacc = _mm_setzero_pd();
    __m128 mask = _mm_castsi128_ps(_mm_cmplt_epi32(_mm_loadu_si128((const __m128i *)ascend32), _mm_set1_epi32(width % 4)));
    __m128 posmask = _mm_andnot_ps(mask, _mm_set_ps1(INFINITY));
    __m128 negmask = _mm_andnot_ps(mask, _mm_set_ps1(-INFINITY));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 4) {
            __m128 v1 = _mm_load_ps((const float *)srcp1 + x);
            __m128 v2 = _mm_load_ps((const float *)srcp2 + x);
            __m128 tmp;
            fmmin = _mm_min_ps(fmmin, v1);
            fmmax = _mm_max_ps(fmmax, v1);
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(v1));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(_mm_movehl_ps(v1, v1)));
            tmp = _mm_and_ps(_mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)), _mm_sub_ps(v1, v2));
            fmdiffacc = _mm_add_pd(fmdiffacc, _mm_cvtps_pd(tmp));
            fmdiffacc = _mm_add_pd(fmdiffacc, _mm_cvtps_pd(_mm_movehl_ps(tmp, tmp)));
        }
        if (width != tail) {
            __m128 v1 = _mm_and_ps(_mm_load_ps((const float *)srcp1 + tail), mask);
            __m128 v2 = _mm_and_ps(_mm_load_ps((const float *)srcp2 + tail), mask);
            __m128 tmp;
            fmmin = _mm_min_ps(fmmin, _mm_or_ps(v1, posmask));
            fmmax = _mm_max_ps(fmmax, _mm_or_ps(v1, negmask));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(v1));
            fmacc = _mm_add_pd(fmacc, _mm_cvtps_pd(_mm_movehl_ps(v1, v1)));
            tmp = _mm_and_ps(_mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)), _mm_sub_ps(v1, v2));
            fmdiffacc = _mm_add_pd(fmdiffacc, _mm_cvtps_pd(tmp));
            fmdiffacc = _mm_add_pd(fmdiffacc, _mm_cvtps_pd(_mm_movehl_ps(tmp, tmp)));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->f.min = hmin_ps(fmmin);
    stats->f.max = hmax_ps(fmmax);
    stats->f.acc = hadd_pd(fmacc);
    stats->f.diffacc = hadd_pd(fmdiffacc);
}
