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

#ifndef MERGE_H
#define MERGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

union vs_merge_weight {
	unsigned u;
	float f;
};

void vs_merge_byte_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_word_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_float_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);

void vs_merge_byte_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_word_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_float_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);

#ifdef __cplusplus
}
#endif

#endif // MERGE_H