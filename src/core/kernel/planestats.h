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

#ifndef PLANESTATS_H
#define PLANESTATS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

union vs_plane_stats {
	struct {
		unsigned min;
		unsigned max;
		uint64_t acc;
		uint64_t diffacc;
	} i;

	struct {
		float min;
		float max;
		double acc;
		double diffacc;
	} f;
};

void vs_plane_stats_1_byte_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);
void vs_plane_stats_1_word_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);
void vs_plane_stats_1_float_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);

void vs_plane_stats_2_byte_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);
void vs_plane_stats_2_word_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);
void vs_plane_stats_2_float_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);

void vs_plane_stats_1_byte_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);
void vs_plane_stats_1_word_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);
void vs_plane_stats_1_float_sse2(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height);

void vs_plane_stats_2_byte_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);
void vs_plane_stats_2_word_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);
void vs_plane_stats_2_float_sse2(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height);

#ifdef __cplusplus
}
#endif

#endif