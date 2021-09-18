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

#define DECL_PREMUL(pixel, isa) void vs_premultiply_##pixel##_##isa(const void *src1, const void *src2, void *dst, unsigned depth, unsigned offset, unsigned n);
#define DECL_MERGE(pixel, isa) void vs_merge_##pixel##_##isa(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n);
#define DECL_MASK_MERGE(pixel, isa) void vs_mask_merge_##pixel##_##isa(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
#define DECL_MASK_MERGE_PREMUL(pixel, isa) void vs_mask_merge_premul_##pixel##_##isa(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n);
#define DECL_MAKEDIFF(pixel, isa) void vs_makediff_##pixel##_##isa(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n);
#define DECL_MERGEDIFF(pixel, isa) void vs_mergediff_##pixel##_##isa(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n);

DECL_PREMUL(byte, c)
DECL_PREMUL(word, c)
DECL_PREMUL(float, c)

DECL_MERGE(byte, c)
DECL_MERGE(word, c)
DECL_MERGE(float, c)

DECL_MASK_MERGE(byte, c)
DECL_MASK_MERGE(word, c)
DECL_MASK_MERGE(float, c)

DECL_MASK_MERGE_PREMUL(byte, c)
DECL_MASK_MERGE_PREMUL(word, c)
DECL_MASK_MERGE_PREMUL(float, c)

DECL_MAKEDIFF(byte, c)
DECL_MAKEDIFF(word, c)
DECL_MAKEDIFF(float, c)

DECL_MERGEDIFF(byte, c)
DECL_MERGEDIFF(word, c)
DECL_MERGEDIFF(float, c)

#ifdef VS_TARGET_CPU_X86
DECL_MERGE(byte, sse2);
DECL_MERGE(word, sse2);
DECL_MERGE(float, sse2);

DECL_MASK_MERGE(byte, sse2)
DECL_MASK_MERGE(word, sse2)
DECL_MASK_MERGE(float, sse2)

DECL_MASK_MERGE_PREMUL(byte, sse2)
DECL_MASK_MERGE_PREMUL(word, sse2)
DECL_MASK_MERGE_PREMUL(float, sse2)

DECL_MAKEDIFF(byte, sse2)
DECL_MAKEDIFF(word, sse2)
DECL_MAKEDIFF(float, sse2)

DECL_MERGEDIFF(byte, sse2)
DECL_MERGEDIFF(word, sse2)
DECL_MERGEDIFF(float, sse2)

DECL_MERGE(byte, avx2);
DECL_MERGE(word, avx2);
DECL_MERGE(float, avx2);

DECL_MASK_MERGE(byte, avx2)
DECL_MASK_MERGE(word, avx2)
DECL_MASK_MERGE(float, avx2)

DECL_MASK_MERGE_PREMUL(byte, avx2)
DECL_MASK_MERGE_PREMUL(word, avx2)
DECL_MASK_MERGE_PREMUL(float, avx2)

DECL_MAKEDIFF(byte, avx2)
DECL_MAKEDIFF(word, avx2)
DECL_MAKEDIFF(float, avx2)

DECL_MERGEDIFF(byte, avx2)
DECL_MERGEDIFF(word, avx2)
DECL_MERGEDIFF(float, avx2)
#endif /* VS_TARGET_CPU_X86 */

#undef DECL_MERGEDIFF
#undef DECL_MAKEDIFF
#undef DECL_MASK_MERGE_PREMUL
#undef DECL_MASK_MERGE
#undef DECL_MERGE

#ifdef VS_MERGE_IMPL
/*
* Magic divisors from : https://www.hackersdelight.org/magic.htm
* Signed coefficients used for up to 15-bit, unsigned for 16-bit.
* Only 16-bit data can occupy all 32 intermediate bits.
*/
static const uint32_t div_table[8] = { 0x80402011, 0x80200803, 0x80100201, 0x80080081, 0x80040021, 0x80020009, 0x80010003, 0x80008001 };
static const uint8_t shift_table[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MERGE_H
