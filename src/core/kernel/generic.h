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

	/* Tiled processing, honoured by every vs_generic_3x3_* kernel and by the horizontal, vertical
	   and separable convolutions (vs_generic_1d_conv_h_*, vs_generic_1d_conv_v_*,
	   vs_generic_2d_conv_sep_*), and by nothing else: the larger square convolutions always
	   process whole planes. With row_end non-zero they write just rows [row_start, row_end) of the
	   plane, height still being the whole plane's, and address row r of src at
	   src + (r & src_mask) * src_stride, of dst likewise; see VSTileBuffer. Zero (the default)
	   processes the whole plane. */
	unsigned row_start;
	unsigned row_end;
	unsigned src_mask;
	unsigned dst_mask;

	/* Scratch rows for the horizontal, vertical and separable convolutions: at least
	   vs_generic_conv_scratch_bytes(width) bytes, 64 byte aligned, or NULL to have each call
	   allocate its own. A tiled filter calls a kernel once per band of rows, which made allocating
	   on every call cost a heap round trip per band. */
	void *scratch;
};

#ifdef __cplusplus
#include <algorithm>
#include "VSHelper4.h"

/* The rows a tiled kernel writes and how it finds them, decoded from the window in
   vs_generic_params. EVERY vs_generic_3x3_* kernel and every horizontal, vertical and separable
   convolution, whatever the ISA and however hand-written, must take its rows from this:
   genericfilters.cpp runs all of them tiled and hands whichever kernel the selectors picked ring
   buffers of a few rows, so a kernel walking the whole plane instead would read and write far
   outside them. */
struct vs_generic_rows {
	unsigned start;
	unsigned end;
	unsigned src_mask;
	unsigned dst_mask;

	vs_generic_rows(const vs_generic_params &params, unsigned height) :
		start(params.row_end ? params.row_start : 0),
		end(params.row_end ? params.row_end : height),
		src_mask(params.row_end ? params.src_mask : ~0u),
		dst_mask(params.row_end ? params.dst_mask : ~0u) {
	}
};

/* The bytes of each of the two rows vs_generic_params::scratch provides, the second starting
   right after the first, both 64 byte aligned: width + 64 samples of up to four bytes, which
   holds any convolution kernel's intermediate row or accumulator row. */
static inline size_t vs_generic_conv_scratch_row_bytes(unsigned width)
{
	return ((static_cast<size_t>(width) + 64) * 4 + 63) & ~static_cast<size_t>(63);
}

/* What vs_generic_params::scratch has to point at for a plane of this width. */
static inline size_t vs_generic_conv_scratch_bytes(unsigned width)
{
	return 2 * vs_generic_conv_scratch_row_bytes(width);
}

/* The source rows of a vertical convolution pass over row i: fwidth taps, at most 25, mirrored
   at the edges of a plane of height rows, addressed through src_mask (see vs_generic_rows).
   Every mirrored row lies within fwidth / 2 rows of i, so a row window with that much halo holds
   them, and tiled and whole plane results only agree while every ISA mirrors the same way, which
   is why all of them take their rows from here. Internal linkage here and below: each ISA's
   translation unit keeps its own copy built with its own flags, which the linker could
   otherwise fold into one built for a CPU this one is not. */
static inline void vs_generic_gather_v_rows(const void *src, ptrdiff_t src_stride, unsigned src_mask, unsigned i, unsigned height,
	unsigned fwidth, const void *srcp[])
{
	unsigned support = fwidth / 2;
	unsigned dist_from_bottom = height - 1 - i;

	for (unsigned k = 0; k < support; ++k) {
		unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
		srcp[k] = static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(row & src_mask) * src_stride;
	}
	for (unsigned k = support; k < fwidth; ++k) {
		unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
		srcp[k] = static_cast<const unsigned char *>(src) + static_cast<ptrdiff_t>(row & src_mask) * src_stride;
	}
}

namespace {

/* A convolution kernel's scratch rows: those vs_generic_params::scratch points at when the caller
   gave some, else allocated on first use and freed with this. row(0) and row(1) are each
   vs_generic_conv_scratch_row_bytes(width) bytes, 64 byte aligned, or null when allocating
   failed. */
class vs_generic_conv_scratch {
	unsigned char *base;
	void *owned = nullptr;
	size_t row_bytes;
public:
	vs_generic_conv_scratch(const vs_generic_params &params, unsigned width) :
		base(static_cast<unsigned char *>(params.scratch)), row_bytes(vs_generic_conv_scratch_row_bytes(width)) {
	}

	vs_generic_conv_scratch(const vs_generic_conv_scratch &) = delete;
	vs_generic_conv_scratch &operator=(const vs_generic_conv_scratch &) = delete;

	~vs_generic_conv_scratch() {
		vsh::vsh_aligned_free(owned);
	}

	void *row(unsigned index) {
		if (!base)
			base = static_cast<unsigned char *>(owned = vsh::vsh_aligned_malloc(2 * row_bytes, 64));
		return base ? base + index * row_bytes : nullptr;
	}
};

} // namespace
#endif

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
/* NEON 3x3 neighborhood family (generic_neon.cpp, a Backend instantiation of
   generic_impl.h). Integer paths are bit-exact with the C reference; the half
   tier converts f16<->f32 at the loads/stores (baseline AArch64, no feature
   gate) and keeps arithmetic in float32 like the x86 F16C tiers.
   Formats with no declaration here (deflate/inflate word+float, prewitt
   float, sobel float+half) lose or tie against their autovectorised C tier
   under streaming sources on M4 and are not built, like 3x3 conv float. */
DECL_3x3(prewitt, byte, neon)
DECL_3x3(prewitt, word, neon)
DECL_3x3(prewitt, half, neon)

DECL_3x3(sobel, byte, neon)
DECL_3x3(sobel, word, neon)

DECL_3x3(min, byte, neon)
DECL_3x3(min, word, neon)
DECL_3x3(min, float, neon)
DECL_3x3(min, half, neon)

DECL_3x3(max, byte, neon)
DECL_3x3(max, word, neon)
DECL_3x3(max, float, neon)
DECL_3x3(max, half, neon)

DECL_3x3(median, byte, neon)
DECL_3x3(median, word, neon)
DECL_3x3(median, float, neon)
DECL_3x3(median, half, neon)

DECL_3x3(deflate, byte, neon)
DECL_3x3(deflate, half, neon)

DECL_3x3(inflate, byte, neon)
DECL_3x3(inflate, half, neon)

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
/* Native-f16 half Min/Max/Median (8 lanes, no f32 conversion; comparisons are
   exact in any precision). Needs only FEAT_FP16; lives in the FHM TU and is
   gated on the fhm flag, a strict superset. */
DECL_3x3(min, half, neon_fp16)
DECL_3x3(max, half, neon_fp16)
DECL_3x3(median, half, neon_fp16)
#endif /* VS_TARGET_ARM_FHM */

#endif /* VS_TARGET_CPU_ARM64 */

#undef DECL_3x3
#undef DECL

#endif
