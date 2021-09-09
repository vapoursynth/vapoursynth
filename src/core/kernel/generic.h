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

#ifndef GENERIC_H
#define GENERIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vs_generic_params {
	uint16_t maxval;

	/* Prewitt, Sobel. */
	float scale;

	/* Minimum, Maximum, Deflate, Inflate. */
	uint16_t threshold;
	float thresholdf;

	/* Minimum, Maximum. */
	uint8_t stencil;

	/* Convolution. */
	unsigned matrixsize;
	int16_t matrix[25];
	float matrixf[25];
	float div;
	float bias;
	uint8_t saturate;
};

#define DECL(kernel, pixel, isa) void vs_generic_##kernel##_##pixel##_##isa(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height);
#define DECL_3x3(kernel, pixel, isa) DECL(3x3_##kernel, pixel, isa)

DECL_3x3(prewitt, byte, c)
DECL_3x3(prewitt, word, c)
DECL_3x3(prewitt, float, c)

DECL_3x3(sobel, byte, c)
DECL_3x3(sobel, word, c)
DECL_3x3(sobel, float, c)

DECL_3x3(min, byte, c)
DECL_3x3(min, word, c)
DECL_3x3(min, float, c)

DECL_3x3(max, byte, c)
DECL_3x3(max, word, c)
DECL_3x3(max, float, c)

DECL_3x3(median, byte, c)
DECL_3x3(median, word, c)
DECL_3x3(median, float, c)

DECL_3x3(deflate, byte, c)
DECL_3x3(deflate, word, c)
DECL_3x3(deflate, float, c)

DECL_3x3(inflate, byte, c)
DECL_3x3(inflate, word, c)
DECL_3x3(inflate, float, c)

DECL_3x3(conv, byte, c)
DECL_3x3(conv, word, c)
DECL_3x3(conv, float, c)

DECL(5x5_conv, byte, c)
DECL(5x5_conv, word, c)
DECL(5x5_conv, float, c)

DECL(1d_conv_h, byte, c)
DECL(1d_conv_h, word, c)
DECL(1d_conv_h, float, c)

DECL(1d_conv_v, byte, c)
DECL(1d_conv_v, word, c)
DECL(1d_conv_v, float, c)

#ifdef VS_TARGET_CPU_X86
DECL_3x3(prewitt, byte, sse2)
DECL_3x3(prewitt, word, sse2)
DECL_3x3(prewitt, float, sse2)

DECL_3x3(sobel, byte, sse2)
DECL_3x3(sobel, word, sse2)
DECL_3x3(sobel, float, sse2)

DECL_3x3(min, byte, sse2)
DECL_3x3(min, word, sse2)
DECL_3x3(min, float, sse2)

DECL_3x3(max, byte, sse2)
DECL_3x3(max, word, sse2)
DECL_3x3(max, float, sse2)

DECL_3x3(median, byte, sse2)
DECL_3x3(median, word, sse2)
DECL_3x3(median, float, sse2)

DECL_3x3(deflate, byte, sse2)
DECL_3x3(deflate, word, sse2)
DECL_3x3(deflate, float, sse2)

DECL_3x3(inflate, byte, sse2)
DECL_3x3(inflate, word, sse2)
DECL_3x3(inflate, float, sse2)

DECL_3x3(conv, byte, sse2)
DECL_3x3(conv, word, sse2)
DECL_3x3(conv, float, sse2)

DECL_3x3(prewitt, byte, avx2)
DECL_3x3(prewitt, word, avx2)
DECL_3x3(prewitt, float, avx2)

DECL_3x3(sobel, byte, avx2)
DECL_3x3(sobel, word, avx2)
DECL_3x3(sobel, float, avx2)

DECL_3x3(min, byte, avx2)
DECL_3x3(min, word, avx2)
DECL_3x3(min, float, avx2)

DECL_3x3(max, byte, avx2)
DECL_3x3(max, word, avx2)
DECL_3x3(max, float, avx2)

DECL_3x3(median, byte, avx2)
DECL_3x3(median, word, avx2)
DECL_3x3(median, float, avx2)

DECL_3x3(deflate, byte, avx2)
DECL_3x3(deflate, word, avx2)
DECL_3x3(deflate, float, avx2)

DECL_3x3(inflate, byte, avx2)
DECL_3x3(inflate, word, avx2)
DECL_3x3(inflate, float, avx2)

DECL_3x3(conv, byte, avx2)
DECL_3x3(conv, word, avx2)
DECL_3x3(conv, float, avx2)
#endif /* VS_TARGET_CPU_X86 */

#undef DECL_3x3
#undef DECL

#ifdef __cplusplus
} // extern "C"
#endif

#endif