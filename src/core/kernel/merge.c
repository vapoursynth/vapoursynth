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

#include "merge.h"

#define MERGESHIFT 15
#define ROUND (1U << (MERGESHIFT - 1))

void vs_merge_byte_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
	const uint8_t *srcp1 = src1;
	const uint8_t *srcp2 = src2;
	uint8_t *dstp = dst;
	unsigned w = weight.u;
	unsigned i;

	for (i = 0; i < n; i++) {
		unsigned v1 = srcp1[i];
		unsigned v2 = srcp2[i];
		dstp[i] = v1 + (((v2 - v1) * w + ROUND) >> MERGESHIFT);
	}
}

void vs_merge_word_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
	const uint16_t *srcp1 = src1;
	const uint16_t *srcp2 = src2;
	uint16_t *dstp = dst;
	unsigned w = weight.u;
	unsigned i;

	for (i = 0; i < n; i++) {
		unsigned v1 = srcp1[i];
		unsigned v2 = srcp2[i];
		dstp[i] = v1 + (((v2 - v1) * w + ROUND) >> MERGESHIFT);
	}
}

void vs_merge_float_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
	const float *srcp1 = src1;
	const float *srcp2 = src2;
	float *dstp = dst;
	float w = weight.f;
	unsigned i;

	for (i = 0; i < n; i++) {
		float v1 = srcp1[i];
		float v2 = srcp2[i];
		dstp[i] = v1 + (v2 - v1) * w;
	}
}