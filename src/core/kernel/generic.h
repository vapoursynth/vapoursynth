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

struct vs_generic_params {
	uint16_t maxval;

	/* Prewitt, Sobel. */
	float scale;

	/* Minimum, Maximum, Deflate, Inflate. */
	uint16_t threshold;
	float thresholdf;

	/* Minimum, Maximum. */
	uint8_t stencil;

	/* Convolution. Square mode allows up to 11x11 = 121 coefficients. */
	unsigned matrixsize;
	int16_t matrix[121];
	float matrixf[121];
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

/* float16 (half) scalar fallback: 3x3 neighbourhood family + square 3x3
   convolution. Portable (bit-manipulation conversions), no ISA requirement. */
DECL_3x3(prewitt, half, c)
DECL_3x3(sobel, half, c)
DECL_3x3(min, half, c)
DECL_3x3(max, half, c)
DECL_3x3(median, half, c)
DECL_3x3(deflate, half, c)
DECL_3x3(inflate, half, c)
DECL_3x3(conv, half, c)
DECL(5x5_conv, half, c)
DECL(7x7_conv, half, c)
DECL(9x9_conv, half, c)
DECL(11x11_conv, half, c)
DECL(1d_conv_h, half, c)
DECL(1d_conv_v, half, c)
DECL(2d_conv_sep, half, c)

DECL(5x5_conv, byte, c)
DECL(5x5_conv, word, c)
DECL(5x5_conv, float, c)

DECL(7x7_conv, byte, c)
DECL(7x7_conv, word, c)
DECL(7x7_conv, float, c)

DECL(9x9_conv, byte, c)
DECL(9x9_conv, word, c)
DECL(9x9_conv, float, c)

DECL(11x11_conv, byte, c)
DECL(11x11_conv, word, c)
DECL(11x11_conv, float, c)

DECL(1d_conv_h, byte, c)
DECL(1d_conv_h, word, c)
DECL(1d_conv_h, float, c)

DECL(1d_conv_v, byte, c)
DECL(1d_conv_v, word, c)
DECL(1d_conv_v, float, c)

DECL(2d_conv_sep, byte, c)
DECL(2d_conv_sep, word, c)
DECL(2d_conv_sep, float, c)

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

DECL(1d_conv_h, byte, sse2)
DECL(1d_conv_h, word, sse2)
DECL(1d_conv_h, float, sse2)

DECL(1d_conv_v, byte, sse2)
DECL(1d_conv_v, word, sse2)
DECL(1d_conv_v, float, sse2)

DECL(2d_conv_sep, byte, sse2)
DECL(2d_conv_sep, word, sse2)
DECL(2d_conv_sep, float, sse2)

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

DECL(1d_conv_h, byte, avx2)
DECL(1d_conv_h, word, avx2)
DECL(1d_conv_h, float, avx2)

DECL(1d_conv_v, byte, avx2)
DECL(1d_conv_v, word, avx2)
DECL(1d_conv_v, float, avx2)

DECL(2d_conv_sep, byte, avx2)
DECL(2d_conv_sep, word, avx2)
DECL(2d_conv_sep, float, avx2)

DECL_3x3(prewitt, byte, avx512)
DECL_3x3(prewitt, word, avx512)
DECL_3x3(prewitt, float, avx512)

DECL_3x3(sobel, byte, avx512)
DECL_3x3(sobel, word, avx512)
DECL_3x3(sobel, float, avx512)

DECL_3x3(min, byte, avx512)
DECL_3x3(min, word, avx512)
DECL_3x3(min, float, avx512)

DECL_3x3(max, byte, avx512)
DECL_3x3(max, word, avx512)
DECL_3x3(max, float, avx512)

DECL_3x3(median, byte, avx512)
DECL_3x3(median, word, avx512)
DECL_3x3(median, float, avx512)

DECL_3x3(deflate, byte, avx512)
DECL_3x3(deflate, word, avx512)
DECL_3x3(deflate, float, avx512)

DECL_3x3(inflate, byte, avx512)
DECL_3x3(inflate, word, avx512)
DECL_3x3(inflate, float, avx512)

DECL_3x3(conv, byte, avx512)
DECL_3x3(conv, word, avx512)
DECL_3x3(conv, float, avx512)

DECL(1d_conv_h, byte, avx512)
DECL(1d_conv_h, word, avx512)
DECL(1d_conv_h, float, avx512)

DECL(1d_conv_v, byte, avx512)
DECL(1d_conv_v, word, avx512)
DECL(1d_conv_v, float, avx512)

DECL(2d_conv_sep, byte, avx512)
DECL(2d_conv_sep, word, avx512)
DECL(2d_conv_sep, float, avx512)

/* float16 (half): 3x3 neighbourhood family + square 3x3 convolution only.
   F16C tiers (avx2/avx512); arithmetic runs in float32. */
DECL_3x3(prewitt, half, avx2)
DECL_3x3(sobel, half, avx2)
DECL_3x3(min, half, avx2)
DECL_3x3(max, half, avx2)
DECL_3x3(median, half, avx2)
DECL_3x3(deflate, half, avx2)
DECL_3x3(inflate, half, avx2)
DECL_3x3(conv, half, avx2)

DECL_3x3(prewitt, half, avx512)
DECL_3x3(sobel, half, avx512)
DECL_3x3(min, half, avx512)
DECL_3x3(max, half, avx512)
DECL_3x3(median, half, avx512)
DECL_3x3(deflate, half, avx512)
DECL_3x3(inflate, half, avx512)
DECL_3x3(conv, half, avx512)

/* Square (mode 's') NxN convolution SIMD, byte/word/float for 5x5..11x11. */
DECL(5x5_conv, byte, avx2)
DECL(7x7_conv, byte, avx2)
DECL(9x9_conv, byte, avx2)
DECL(11x11_conv, byte, avx2)
DECL(5x5_conv, float, avx2)
DECL(7x7_conv, float, avx2)
DECL(9x9_conv, float, avx2)
DECL(11x11_conv, float, avx2)
DECL(5x5_conv, word, avx2)
DECL(7x7_conv, word, avx2)
DECL(9x9_conv, word, avx2)
DECL(11x11_conv, word, avx2)

DECL(5x5_conv, byte, avx512)
DECL(7x7_conv, byte, avx512)
DECL(9x9_conv, byte, avx512)
DECL(11x11_conv, byte, avx512)
DECL(5x5_conv, float, avx512)
DECL(7x7_conv, float, avx512)
DECL(9x9_conv, float, avx512)
DECL(11x11_conv, float, avx512)
DECL(5x5_conv, word, avx512)
DECL(7x7_conv, word, avx512)
DECL(9x9_conv, word, avx512)
DECL(11x11_conv, word, avx512)

/* AVX-512-VNNI byte square convolution (5x5..11x11); used when the CPU reports VNNI+VBMI
   and every coefficient fits int8. Bit-exact with the plain avx512 byte kernels. */
DECL(5x5_conv, byte, avx512vnni)
DECL(7x7_conv, byte, avx512vnni)
DECL(9x9_conv, byte, avx512vnni)
DECL(11x11_conv, byte, avx512vnni)

#endif /* VS_TARGET_CPU_X86 */

#ifdef VS_TARGET_CPU_ARM64
/* NEON baseline kernels: the whole convolution family (square 3x3..11x11,
   1D horizontal/vertical, separable) for byte/word/float/half. Integer paths
   are bit-exact with the C reference; float/half use FMA. */
DECL_3x3(conv, byte, neon)
DECL_3x3(conv, word, neon)
/* 3x3 float: no NEON kernel -- loses to its autovectorised C tier at a full
   thread pool on M4, so it is dispatched to C. */
DECL_3x3(conv, half, neon)

DECL(5x5_conv, byte, neon)
DECL(7x7_conv, byte, neon)
DECL(9x9_conv, byte, neon)
DECL(11x11_conv, byte, neon)
DECL(5x5_conv, word, neon)
DECL(7x7_conv, word, neon)
DECL(9x9_conv, word, neon)
DECL(11x11_conv, word, neon)
DECL(5x5_conv, float, neon)
DECL(7x7_conv, float, neon)
DECL(9x9_conv, float, neon)
DECL(11x11_conv, float, neon)
DECL(5x5_conv, half, neon)
DECL(7x7_conv, half, neon)
DECL(9x9_conv, half, neon)
DECL(11x11_conv, half, neon)

DECL(1d_conv_h, byte, neon)
DECL(1d_conv_h, word, neon)
DECL(1d_conv_h, float, neon)
DECL(1d_conv_h, half, neon)

DECL(1d_conv_v, byte, neon)
DECL(1d_conv_v, word, neon)
DECL(1d_conv_v, float, neon)
DECL(1d_conv_v, half, neon)

DECL(2d_conv_sep, byte, neon)
DECL(2d_conv_sep, word, neon)
DECL(2d_conv_sep, float, neon)
DECL(2d_conv_sep, half, neon)

#ifdef VS_TARGET_ARM_I8MM
/* usdot byte square conv; valid only when every coefficient fits int8. */
DECL_3x3(conv, byte, neon_dot)
DECL(5x5_conv, byte, neon_dot)
DECL(7x7_conv, byte, neon_dot)
DECL(9x9_conv, byte, neon_dot)
DECL(11x11_conv, byte, neon_dot)
DECL(1d_conv_h, byte, neon_dot)
/* Separable byte: vmlal vertical pass, usdot horizontal pass. Same int8
   coefficient requirement as the rest of this block. */
DECL(2d_conv_sep, byte, neon_dot)
/* One horizontal scanline of the above, exported from the i8mm TU so the
   separable driver (built without i8mm) can call it per row. */
void vs_generic_1d_conv_h_byte_scanline_neon_dot(const void *srcp, void *dstp, const struct vs_generic_params *params, unsigned width);
#endif /* VS_TARGET_ARM_I8MM */

#ifdef VS_TARGET_ARM_FHM
/* fmlal widening f16 MAC; valid only when coefficients are f16-exact (conv_f16). */
DECL_3x3(conv, half, neon_fhm)
DECL(5x5_conv, half, neon_fhm)
DECL(7x7_conv, half, neon_fhm)
DECL(9x9_conv, half, neon_fhm)
DECL(11x11_conv, half, neon_fhm)
DECL(1d_conv_h, half, neon_fhm)
DECL(1d_conv_v, half, neon_fhm)
/* Separable: both halves are half-format, so this drives its own row loop and
   runs the vertical and horizontal passes on fmlal. */
DECL(2d_conv_sep, half, neon_fhm)
#endif /* VS_TARGET_ARM_FHM */

#ifdef VS_TARGET_ARM_SVE
/* Non-streaming SVE (VLA) kernels; never built or dispatched on Apple Silicon. */
unsigned vs_sve_vector_length(void);

DECL_3x3(conv, byte, sve)
DECL_3x3(conv, word, sve)

/* Plain-SVE byte squares: only 5x5/7x7 (wide coefficients) still beat NEON on
   Graviton3; 9x9/11x11 lost to the round-2 NEON lane-coefficient squares and are
   pruned. The plain word squares, the float squares, and the byte separable all
   lost to NEON too and are gone. */
DECL(5x5_conv, byte, sve)
DECL(7x7_conv, byte, sve)

DECL(1d_conv_h, byte, sve)

/* svdot_s64 word squares: the only integer SVE form that beats NEON at a
   128-bit VL. 9x9/11x11 win at every pool size; 7x7 became a wash after the
   round-2 NEON word squares and is pruned. */
DECL(9x9_conv, word, sve_dot)
DECL(11x11_conv, word, sve_dot)

#ifdef VS_TARGET_ARM_SVE_I8MM
/* SVE usdot byte square conv; needs SVE + I8MM and pays only above 128-bit VL.
   Only 7x7/9x9 win at every pool size; 5x5/11x11 were thread-gated and pruned
   (int8 5x5/11x11 fall back to the NEON usdot squares). */
DECL(7x7_conv, byte, sve_dot)
DECL(9x9_conv, byte, sve_dot)
#endif /* VS_TARGET_ARM_SVE_I8MM */

#endif /* VS_TARGET_ARM_SVE */

#ifdef VS_TARGET_ARM_SME
/* SME2 streaming-mode kernels: only the shapes that beat NEON even fully
   contended survive -- the >= 7x7 square outer products and vertical 1D. Byte
   via 2-way int16 SMOPA into ZA32 (WIDE coefficients only; int8 goes to NEON
   usdot), float via FMOPA. The smaller/thread-contested squares, all word
   squares, and word vertical were pruned and fall back to NEON. */
DECL(7x7_conv, byte, sme)
DECL(9x9_conv, byte, sme)
DECL(11x11_conv, byte, sme)
DECL(7x7_conv, float, sme)
DECL(9x9_conv, float, sme)
DECL(11x11_conv, float, sme)

DECL(1d_conv_v, byte, sme)
DECL(1d_conv_v, float, sme)

/* half: samples widen to f32 and reuse the ZA32 FMOPA path. */
DECL(7x7_conv, half, sme)
DECL(9x9_conv, half, sme)
DECL(11x11_conv, half, sme)
DECL(1d_conv_v, half, sme)

#endif /* VS_TARGET_ARM_SME */

#endif /* VS_TARGET_CPU_ARM64 */

#undef DECL_3x3
#undef DECL

#endif
