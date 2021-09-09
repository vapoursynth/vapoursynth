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
#include <immintrin.h>
#include "../planestats.h"

static const uint8_t ascend8[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
static const uint16_t ascend16[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const uint32_t ascend32[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

static unsigned hmax_epu8(__m256i x)
{
    __m128i tmp = _mm_max_epu8(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
    tmp = _mm_max_epu8(tmp, _mm_srli_si128(tmp, 8));
    tmp = _mm_max_epu8(tmp, _mm_srli_si128(tmp, 4));
    tmp = _mm_max_epu8(tmp, _mm_srli_si128(tmp, 2));
    tmp = _mm_max_epu8(tmp, _mm_srli_si128(tmp, 1));
    return _mm_cvtsi128_si32(tmp) & 0xFF;
}

static unsigned hmin_epu8(__m256i x)
{
    __m128i tmp = _mm_min_epu8(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
    tmp = _mm_min_epu8(tmp, _mm_srli_si128(tmp, 8));
    tmp = _mm_min_epu8(tmp, _mm_srli_si128(tmp, 4));
    tmp = _mm_min_epu8(tmp, _mm_srli_si128(tmp, 2));
    tmp = _mm_min_epu8(tmp, _mm_srli_si128(tmp, 1));
    return _mm_cvtsi128_si32(tmp) & 0xFF;
}

static unsigned hmax_epu16(__m256i x)
{
    __m128i tmp = _mm_max_epu16(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
    tmp = _mm_max_epu16(tmp, _mm_srli_si128(tmp, 8));
    tmp = _mm_max_epu16(tmp, _mm_srli_si128(tmp, 4));
    tmp = _mm_max_epu16(tmp, _mm_srli_si128(tmp, 2));
    return (uint16_t)_mm_extract_epi16(tmp, 0);
}

static unsigned hmin_epu16(__m256i x)
{
    __m128i tmp = _mm_min_epu16(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
    tmp = _mm_min_epu16(tmp, _mm_srli_si128(tmp, 8));
    tmp = _mm_min_epu16(tmp, _mm_srli_si128(tmp, 4));
    tmp = _mm_min_epu16(tmp, _mm_srli_si128(tmp, 2));
    return (uint16_t)_mm_extract_epi16(tmp, 0);
}

static float hmax_ps(__m256 x)
{
    __m128 tmp = _mm_max_ps(_mm256_castps256_ps128(x), _mm256_extractf128_ps(x, 1));
    tmp = _mm_max_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(1, 0, 3, 2)));
    tmp = _mm_max_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtss_f32(tmp);
}

static float hmin_ps(__m256 x)
{
    __m128 tmp = _mm_min_ps(_mm256_castps256_ps128(x), _mm256_extractf128_ps(x, 1));
    tmp = _mm_min_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(1, 0, 3, 2)));
    tmp = _mm_min_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtss_f32(tmp);
}

static __m128i hadd_epi64(__m256i x)
{
    __m128i tmp = _mm_add_epi64(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
    tmp = _mm_add_epi64(tmp, _mm_srli_si128(tmp, 8));
    return tmp;
}

static double hadd_pd(__m256d x)
{
    __m128d tmp = _mm_add_pd(_mm256_castpd256_pd128(x), _mm256_extractf128_pd(x, 1));
    return _mm_cvtsd_f64(_mm_add_sd(tmp, _mm_unpackhi_pd(tmp, tmp)));
}


void vs_plane_stats_1_byte_avx2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~31;
    unsigned x, y;

    __m256i mmin = _mm256_set1_epi8(UINT8_MAX);
    __m256i mmax = _mm256_setzero_si256();
    __m256i macc = _mm256_setzero_si256();
    __m256i mask = _mm256_cmpgt_epi8(_mm256_set1_epi8(width % 32), _mm256_loadu_si256((const __m256i *)ascend8));
    __m256i onesmask = _mm256_andnot_si256(mask, _mm256_set1_epi8(UINT8_MAX));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 32) {
            __m256i v = _mm256_load_si256((const __m256i *)(srcp + x));
            mmin = _mm256_min_epu8(mmin, v);
            mmax = _mm256_max_epu8(mmax, v);
            macc = _mm256_add_epi64(macc, _mm256_sad_epu8(v, _mm256_setzero_si256()));
        }
        if (width != tail) {
            __m256i v = _mm256_and_si256(_mm256_load_si256((const __m256i *)(srcp + tail)), mask);
            mmin = _mm256_min_epu8(mmin, _mm256_or_si256(v, onesmask));
            mmax = _mm256_max_epu8(mmax, v);
            macc = _mm256_add_epi64(macc, _mm256_sad_epu8(v, _mm256_setzero_si256()));
        }
        srcp += stride;
    }

    stats->i.min = hmin_epu8(mmin);
    stats->i.max = hmax_epu8(mmax);
    _mm_storel_epi64((__m128i *)&stats->i.acc, hadd_epi64(macc));
}

void vs_plane_stats_1_word_avx2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~15;
    unsigned x, y;

    __m256i mmin = _mm256_set1_epi16(UINT16_MAX);
    __m256i mmax = _mm256_setzero_si256();
    __m256i macc_lo = _mm256_setzero_si256();
    __m256i macc_hi = _mm256_setzero_si256();
    __m256i mask = _mm256_cmpgt_epi16(_mm256_set1_epi16(width % 16), _mm256_loadu_si256((const __m256i *)ascend16));
    __m256i onesmask = _mm256_andnot_si256(mask, _mm256_set1_epi16(UINT16_MAX));
    __m256i low8mask = _mm256_set1_epi16(0xFF);
    __m256i tmp;

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 16) {
            __m256i v = _mm256_load_si256((const __m256i *)((const uint16_t *)srcp + x));
            mmin = _mm256_min_epu16(mmin, v);
            mmax = _mm256_max_epu16(mmax, v);

            macc_lo = _mm256_add_epi64(macc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, v), _mm256_setzero_si256()));
            macc_hi = _mm256_add_epi64(macc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, v), _mm256_setzero_si256()));
        }
        if (width != tail) {
            __m256i v = _mm256_and_si256(_mm256_load_si256((const __m256i *)((const uint16_t *)srcp + tail)), mask);
            mmin = _mm256_min_epu16(mmin, _mm256_or_si256(v, onesmask));
            mmax = _mm256_max_epu16(mmax, v);

            macc_lo = _mm256_add_epi64(macc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, v), _mm256_setzero_si256()));
            macc_hi = _mm256_add_epi64(macc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, v), _mm256_setzero_si256()));
        }
        srcp += stride;
    }

    stats->i.min = hmin_epu16(mmin);
    stats->i.max = hmax_epu16(mmax);

    tmp = _mm256_add_epi64(_mm256_unpacklo_epi64(macc_lo, macc_hi), _mm256_unpackhi_epi64(macc_lo, macc_hi));
    tmp = _mm256_add_epi64(tmp, _mm256_slli_epi64(_mm256_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.acc, _mm_add_epi64(_mm256_castsi256_si128(tmp), _mm256_extractf128_si256(tmp, 1)));
}

void vs_plane_stats_1_float_avx2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned tail = width & ~7;
    unsigned x, y;

    __m256 fmmin = _mm256_set1_ps(INFINITY);
    __m256 fmmax = _mm256_set1_ps(-INFINITY);
    __m256d fmacc = _mm256_setzero_pd();
    __m256 mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(width % 8), _mm256_loadu_si256((const __m256i *)ascend32)));
    __m256 posmask = _mm256_andnot_ps(mask, _mm256_set1_ps(INFINITY));
    __m256 negmask = _mm256_andnot_ps(mask, _mm256_set1_ps(-INFINITY));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 8) {
            __m256 v = _mm256_load_ps((const float *)srcp + x);
            fmmin = _mm256_min_ps(fmmin, v);
            fmmax = _mm256_max_ps(fmmax, v);
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_castps256_ps128(v)));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_extractf128_ps(v, 1)));
        }
        if (width != tail) {
            __m256 v = _mm256_and_ps(_mm256_load_ps((const float *)srcp + tail), mask);
            fmmin = _mm256_min_ps(fmmin, _mm256_or_ps(v, posmask));
            fmmax = _mm256_max_ps(fmmax, _mm256_or_ps(v, negmask));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_castps256_ps128(v)));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_extractf128_ps(v, 1)));
        }
        srcp += stride;
    }

    stats->f.min = hmin_ps(fmmin);
    stats->f.max = hmax_ps(fmmax);
    stats->f.acc = hadd_pd(fmacc);
}

void vs_plane_stats_2_byte_avx2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~31;
    unsigned x, y;

    __m256i mmin = _mm256_set1_epi8(UINT8_MAX);
    __m256i mmax = _mm256_setzero_si256();
    __m256i macc = _mm256_setzero_si256();
    __m256i mdiffacc = _mm256_setzero_si256();
    __m256i mask = _mm256_cmpgt_epi8(_mm256_set1_epi8(width % 32), _mm256_loadu_si256((const __m256i *)ascend8));
    __m256i onesmask = _mm256_andnot_si256(mask, _mm256_set1_epi8(UINT8_MAX));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 32) {
            __m256i v1 = _mm256_load_si256((const __m256i *)(srcp1 + x));
            __m256i v2 = _mm256_load_si256((const __m256i *)(srcp2 + x));
            mmin = _mm256_min_epu8(mmin, v1);
            mmax = _mm256_max_epu8(mmax, v1);
            macc = _mm256_add_epi64(macc, _mm256_sad_epu8(v1, _mm256_setzero_si256()));
            mdiffacc = _mm256_add_epi64(mdiffacc, _mm256_sad_epu8(v1, v2));
        }
        if (width != tail) {
            __m256i v1 = _mm256_and_si256(_mm256_load_si256((const __m256i *)(srcp1 + tail)), mask);
            __m256i v2 = _mm256_and_si256(_mm256_load_si256((const __m256i *)(srcp2 + tail)), mask);
            mmin = _mm256_min_epu8(mmin, _mm256_or_si256(v1, onesmask));
            mmax = _mm256_max_epu8(mmax, v1);
            macc = _mm256_add_epi64(macc, _mm256_sad_epu8(v1, _mm256_setzero_si256()));
            mdiffacc = _mm256_add_epi64(mdiffacc, _mm256_sad_epu8(v1, v2));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = hmin_epu8(mmin);
    stats->i.max = hmax_epu8(mmax);
    _mm_storel_epi64((__m128i *)&stats->i.acc, hadd_epi64(macc));
    _mm_storel_epi64((__m128i *)&stats->i.diffacc, hadd_epi64(mdiffacc));
}

void vs_plane_stats_2_word_avx2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~15;
    unsigned x, y;

    __m256i mmin = _mm256_set1_epi16(UINT16_MAX);
    __m256i mmax = _mm256_setzero_si256();
    __m256i macc_lo = _mm256_setzero_si256();
    __m256i macc_hi = _mm256_setzero_si256();
    __m256i mdiffacc_lo = _mm256_setzero_si256();
    __m256i mdiffacc_hi = _mm256_setzero_si256();
    __m256i mask = _mm256_cmpgt_epi16(_mm256_set1_epi16(width % 16), _mm256_loadu_si256((const __m256i *)ascend16));
    __m256i onesmask = _mm256_andnot_si256(mask, _mm256_set1_epi16(UINT16_MAX));
    __m256i low8mask = _mm256_set1_epi16(0xFF);
    __m256i tmp;

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 16) {
            __m256i v1 = _mm256_load_si256((const __m256i *)((const uint16_t *)srcp1 + x));
            __m256i v2 = _mm256_load_si256((const __m256i *)((const uint16_t *)srcp2 + x));
            __m256i udiff = _mm256_or_si256(_mm256_subs_epu16(v1, v2), _mm256_subs_epu16(v2, v1));

            mmin = _mm256_min_epu16(mmin, v1);
            mmax = _mm256_max_epu16(mmax, v1);

            macc_lo = _mm256_add_epi64(macc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, v1), _mm256_setzero_si256()));
            macc_hi = _mm256_add_epi64(macc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, v1), _mm256_setzero_si256()));

            mdiffacc_lo = _mm256_add_epi64(mdiffacc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, udiff), _mm256_setzero_si256()));
            mdiffacc_hi = _mm256_add_epi64(mdiffacc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, udiff), _mm256_setzero_si256()));
        }
        if (width != tail) {
            __m256i v1 = _mm256_and_si256(_mm256_load_si256((const __m256i *)((const uint16_t *)srcp1 + tail)), mask);
            __m256i v2 = _mm256_and_si256(_mm256_load_si256((const __m256i *)((const uint16_t *)srcp2 + tail)), mask);
            __m256i udiff = _mm256_or_si256(_mm256_subs_epu16(v1, v2), _mm256_subs_epu16(v2, v1));

            mmin = _mm256_min_epu16(mmin, _mm256_or_si256(v1, onesmask));
            mmax = _mm256_max_epu16(mmax, v1);

            macc_lo = _mm256_add_epi64(macc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, v1), _mm256_setzero_si256()));
            macc_hi = _mm256_add_epi64(macc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, v1), _mm256_setzero_si256()));

            mdiffacc_lo = _mm256_add_epi64(mdiffacc_lo, _mm256_sad_epu8(_mm256_and_si256(low8mask, udiff), _mm256_setzero_si256()));
            mdiffacc_hi = _mm256_add_epi64(mdiffacc_hi, _mm256_sad_epu8(_mm256_andnot_si256(low8mask, udiff), _mm256_setzero_si256()));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = hmin_epu16(mmin);
    stats->i.max = hmax_epu16(mmax);

    tmp = _mm256_add_epi64(_mm256_unpacklo_epi64(macc_lo, macc_hi), _mm256_unpackhi_epi64(macc_lo, macc_hi));
    tmp = _mm256_add_epi64(tmp, _mm256_slli_epi64(_mm256_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.acc, _mm_add_epi64(_mm256_castsi256_si128(tmp), _mm256_extractf128_si256(tmp, 1)));

    tmp = _mm256_add_epi64(_mm256_unpacklo_epi64(mdiffacc_lo, mdiffacc_hi), _mm256_unpackhi_epi64(mdiffacc_lo, mdiffacc_hi));
    tmp = _mm256_add_epi64(tmp, _mm256_slli_epi64(_mm256_unpackhi_epi64(tmp, tmp), 8));
    _mm_storel_epi64((__m128i *)&stats->i.diffacc, _mm_add_epi64(_mm256_castsi256_si128(tmp), _mm256_extractf128_si256(tmp, 1)));
}

void vs_plane_stats_2_float_avx2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned tail = width & ~7;
    unsigned x, y;

    __m256 fmmin = _mm256_set1_ps(INFINITY);
    __m256 fmmax = _mm256_set1_ps(-INFINITY);
    __m256d fmacc = _mm256_setzero_pd();
    __m256d fmdiffacc = _mm256_setzero_pd();
    __m256 mask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(width % 8), _mm256_loadu_si256((const __m256i *)ascend32)));
    __m256 posmask = _mm256_andnot_ps(mask, _mm256_set1_ps(INFINITY));
    __m256 negmask = _mm256_andnot_ps(mask, _mm256_set1_ps(-INFINITY));

    for (y = 0; y < height; y++) {
        for (x = 0; x < tail; x += 8) {
            __m256 v1 = _mm256_load_ps((const float *)srcp1 + x);
            __m256 v2 = _mm256_load_ps((const float *)srcp2 + x);
            __m256 tmp;
            fmmin = _mm256_min_ps(fmmin, v1);
            fmmax = _mm256_max_ps(fmmax, v1);
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_castps256_ps128(v1)));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_extractf128_ps(v1, 1)));
            tmp = _mm256_and_ps(_mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF)), _mm256_sub_ps(v1, v2));
            fmdiffacc = _mm256_add_pd(fmdiffacc, _mm256_cvtps_pd(_mm256_castps256_ps128(tmp)));
            fmdiffacc = _mm256_add_pd(fmdiffacc, _mm256_cvtps_pd(_mm256_extractf128_ps(tmp, 1)));
        }
        if (width != tail) {
            __m256 v1 = _mm256_and_ps(_mm256_load_ps((const float *)srcp1 + tail), mask);
            __m256 v2 = _mm256_and_ps(_mm256_load_ps((const float *)srcp2 + tail), mask);
            __m256 tmp;
            fmmin = _mm256_min_ps(fmmin, _mm256_or_ps(v1, posmask));
            fmmax = _mm256_max_ps(fmmax, _mm256_or_ps(v1, negmask));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_castps256_ps128(v1)));
            fmacc = _mm256_add_pd(fmacc, _mm256_cvtps_pd(_mm256_extractf128_ps(v1, 1)));
            tmp = _mm256_and_ps(_mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF)), _mm256_sub_ps(v1, v2));
            fmdiffacc = _mm256_add_pd(fmdiffacc, _mm256_cvtps_pd(_mm256_castps256_ps128(tmp)));
            fmdiffacc = _mm256_add_pd(fmdiffacc, _mm256_cvtps_pd(_mm256_extractf128_ps(tmp, 1)));
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->f.min = hmin_ps(fmmin);
    stats->f.max = hmax_ps(fmmax);
    stats->f.acc = hadd_pd(fmacc);
    stats->f.diffacc = hadd_pd(fmdiffacc);
}
