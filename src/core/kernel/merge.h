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

void vs_mask_merge_byte_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_word_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_float_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);

void vs_mask_merge_premul_byte_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_premul_word_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_premul_float_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);

void vs_merge_byte_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_word_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
void vs_merge_float_sse2(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);

void vs_mask_merge_byte_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_word_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_float_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);

void vs_mask_merge_premul_byte_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_premul_word_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
void vs_mask_merge_premul_float_sse2(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);

#ifdef VS_MERGE_IMPL
// Magic divisors from: https://www.hackersdelight.org/magic.htm
// Signed coefficients used for up to 15-bit, unsigned for 16-bit.
// Only 16-bit data can occupy all 32 intermediate bits.
static const uint32_t div_table[8] = { 0x80402011, 0x80200803, 0x80100201, 0x80080081, 0x80040021, 0x80020009, 0x80010003, 0x80008001 };
static const uint8_t shift_table[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };
#endif

#ifdef __cplusplus
}
#endif

#endif // MERGE_H