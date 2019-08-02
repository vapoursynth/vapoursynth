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

#include <emmintrin.h>
#include "../merge.h"
#include "VSHelper.h"

#define MERGESHIFT 15
#define ROUND (1U << (MERGESHIFT - 1))

void vs_merge_byte_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
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
		__m128i tmp2hi = _mm_add_epi16(_mm_add_epi16(_mm_mulhi_epi16(tmp1hi, w), _mm_srli_epi16(_mm_mullo_epi16(tmp1hi, w), 15)), v1w_lo);

		// result = tmp2 >> 8
		__m128i result = _mm_packus_epi16(tmp2lo, tmp2hi);
		_mm_store_si128((__m128i *)(dstp + i), result);
	}
}

void vs_merge_word_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
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

void vs_merge_float_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
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