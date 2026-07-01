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

/*
* Shared, ISA-neutral implementation of the separable / 1-D / 3x3 convolution
* kernels. The per-ISA translation units (convolution_sse2/avx2/avx512.cpp)
* define an `ISA` traits struct supplying the SIMD primitives, then include this
* file to instantiate the full kernel set. This keeps the blocking / edge-mirror
* / dispatch scaffolding -- which is identical across vector widths -- in one
* place, and reduces each ISA tier to just its irreducible primitive layer.
*
* Note on determinism: the integer paths are bit-exact with the C reference
* (kernel/generic.cpp) on every tier. The float paths are NOT bit-exact across
* tiers -- the reduction order differs with vector width and the AVX2/AVX-512
* tiers use FMA (single rounding) where SSE2 and the C reference use separate
* multiply + add. This matches long-standing behaviour and is expected.
*/

#ifndef CONVOLUTION_IMPL_H
#define CONVOLUTION_IMPL_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <VSHelper4.h>
#include "../generic.h"

namespace {

template <class T>
T *line_ptr(T *ptr, unsigned i, ptrdiff_t stride)
{
    return (T *)(((unsigned char *)ptr) + static_cast<ptrdiff_t>(i) * stride);
}

template <class T>
void flip_left(T *ptr, unsigned n)
{
    for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
        ptr[-1 - i] = ptr[i];
    }
}

template <class T>
void flip_right(T *ptr, unsigned n)
{
    for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
        ptr[i] = ptr[-1 - i];
    }
}

// Fill srcp[0..fwidth-1] with the clamped/mirrored source row pointers for a
// vertical pass centered on row i. Shared by conv_plane_v and conv_plane_x.
inline void gather_v_rows(const void *src, ptrdiff_t src_stride, unsigned i, unsigned height, unsigned fwidth, const void *srcp[])
{
    unsigned support = fwidth / 2;
    unsigned dist_from_bottom = height - 1 - i;

    for (unsigned k = 0; k < support; ++k) {
        unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
        srcp[k] = line_ptr(src, row, src_stride);
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
        srcp[k] = line_ptr(src, row, src_stride);
    }
}


/* ---- horizontal pass kernels ------------------------------------------- */

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_byte_pass(const uint8_t *src, uint8_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n)
{
    typedef typename ISA::ivec ivec;
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    ivec w0w1 = ISA::set1_i32((weight(1) << 16) | weight(0));
    ivec w2w3 = ISA::set1_i32((weight(3) << 16) | weight(2));
    ivec w4w5 = ISA::set1_i32((weight(5) << 16) | weight(4));
    ivec w6w7 = ISA::set1_i32((weight(7) << 16) | weight(6));
    ivec w8w9 = ISA::set1_i32((weight(9) << 16) | weight(8));
    ivec w10w11 = ISA::set1_i32((weight(11) << 16) | weight(10));
    ivec w12 = ISA::set1_i32(weight(12));

    typename ISA::fvec scale = ISA::set1_ps(params.div);
    typename ISA::fvec bias = ISA::set1_ps(params.bias);
    typename ISA::fvec satmask = ISA::satmask(params.saturate);

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::IPELS) {
        ivec accum_lo = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 0 * ISA::LANES);
        ivec accum_hi = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 1 * ISA::LANES);
        ivec x0, x1;

        if (N >= 1) {
            x0 = ISA::widen_byte(src + j + 0); x1 = ISA::widen_byte(src + j + 1);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w0w1));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w0w1));
        }
        if (N >= 3) {
            x0 = ISA::widen_byte(src + j + 2); x1 = ISA::widen_byte(src + j + 3);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w2w3));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w2w3));
        }
        if (N >= 5) {
            x0 = ISA::widen_byte(src + j + 4); x1 = ISA::widen_byte(src + j + 5);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w4w5));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w4w5));
        }
        if (N >= 7) {
            x0 = ISA::widen_byte(src + j + 6); x1 = ISA::widen_byte(src + j + 7);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w6w7));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w6w7));
        }
        if (N >= 9) {
            x0 = ISA::widen_byte(src + j + 8); x1 = ISA::widen_byte(src + j + 9);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w8w9));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w8w9));
        }
        if (N >= 11) {
            x0 = ISA::widen_byte(src + j + 10); x1 = ISA::widen_byte(src + j + 11);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w10w11));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w10w11));
        }
        if (N >= 13) {
            x0 = ISA::widen_byte(src + j + 12);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x0), w12));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x0), w12));
        }

        if (Last)
            ISA::store_u8(dst + j, accum_lo, accum_hi, scale, bias, satmask);
        else {
            ISA::store_acc(tmp + j + 0 * ISA::LANES, accum_lo);
            ISA::store_acc(tmp + j + 1 * ISA::LANES, accum_hi);
        }
    }
}

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_word_pass(const uint16_t *src, uint16_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n, int32_t i32bias)
{
    typedef typename ISA::ivec ivec;
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    ivec w0w1 = ISA::set1_i32((weight(1) << 16) | weight(0));
    ivec w2w3 = ISA::set1_i32((weight(3) << 16) | weight(2));
    ivec w4w5 = ISA::set1_i32((weight(5) << 16) | weight(4));
    ivec w6w7 = ISA::set1_i32((weight(7) << 16) | weight(6));
    ivec w8w9 = ISA::set1_i32((weight(9) << 16) | weight(8));
    ivec w10w11 = ISA::set1_i32((weight(11) << 16) | weight(10));
    ivec w12 = ISA::set1_i32(weight(12));
    ivec w_bias = ISA::set1_i32(i32bias);
    ivec bias16 = ISA::word_bias();

    typename ISA::fvec scale = ISA::set1_ps(params.div);
    typename ISA::fvec bias = ISA::set1_ps(params.bias);
    typename ISA::fvec satmask = ISA::satmask(params.saturate);
    ivec maxval = ISA::word_maxval(params.maxval);

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::IPELS) {
        ivec accum_lo = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 0 * ISA::LANES);
        ivec accum_hi = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 1 * ISA::LANES);
        ivec x0, x1;

        if (N >= 1) {
            x0 = ISA::load_word_biased(src + j + 0, bias16); x1 = ISA::load_word_biased(src + j + 1, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w0w1));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w0w1));
        }
        if (N >= 3) {
            x0 = ISA::load_word_biased(src + j + 2, bias16); x1 = ISA::load_word_biased(src + j + 3, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w2w3));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w2w3));
        }
        if (N >= 5) {
            x0 = ISA::load_word_biased(src + j + 4, bias16); x1 = ISA::load_word_biased(src + j + 5, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w4w5));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w4w5));
        }
        if (N >= 7) {
            x0 = ISA::load_word_biased(src + j + 6, bias16); x1 = ISA::load_word_biased(src + j + 7, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w6w7));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w6w7));
        }
        if (N >= 9) {
            x0 = ISA::load_word_biased(src + j + 8, bias16); x1 = ISA::load_word_biased(src + j + 9, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w8w9));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w8w9));
        }
        if (N >= 11) {
            x0 = ISA::load_word_biased(src + j + 10, bias16); x1 = ISA::load_word_biased(src + j + 11, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w10w11));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w10w11));
        }
        if (N >= 13) {
            x0 = ISA::load_word_biased(src + j + 12, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x0), w12));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x0), w12));
        }

        if (Last) {
            accum_lo = ISA::sub32(accum_lo, w_bias);
            accum_hi = ISA::sub32(accum_hi, w_bias);
            ISA::store_u16(dst + j, accum_lo, accum_hi, scale, bias, satmask, maxval);
        } else {
            ISA::store_acc(tmp + j + 0 * ISA::LANES, accum_lo);
            ISA::store_acc(tmp + j + 1 * ISA::LANES, accum_hi);
        }
    }
}

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_float_pass(const float *src, float *dst, const vs_generic_params &params, unsigned n)
{
    typedef typename ISA::fvec fvec;
    auto weight = [=](unsigned k) -> float { return k < N ? params.matrixf[K + k] : 0; };

    fvec w0 = ISA::set1_ps(weight(0)), w1 = ISA::set1_ps(weight(1)), w2 = ISA::set1_ps(weight(2));
    fvec w3 = ISA::set1_ps(weight(3)), w4 = ISA::set1_ps(weight(4)), w5 = ISA::set1_ps(weight(5));
    fvec w6 = ISA::set1_ps(weight(6)), w7 = ISA::set1_ps(weight(7)), w8 = ISA::set1_ps(weight(8));
    fvec w9 = ISA::set1_ps(weight(9));

    fvec scale = ISA::set1_ps(params.div);
    fvec bias = ISA::set1_ps(params.bias);
    fvec satmask = ISA::satmask(params.saturate);

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::FLANES) {
        fvec accum0 = First ? ISA::fzero() : ISA::load_ps(dst + j);
        fvec accum1 = ISA::fzero();

        if (N >= 1) accum0 = ISA::fmadd(ISA::loadu_ps(src + j + 0), w0, accum0);
        if (N >= 2) accum1 = ISA::fmadd(ISA::loadu_ps(src + j + 1), w1, accum1);
        if (N >= 3) accum0 = ISA::fmadd(ISA::loadu_ps(src + j + 2), w2, accum0);
        if (N >= 4) accum1 = ISA::fmadd(ISA::loadu_ps(src + j + 3), w3, accum1);
        if (N >= 5) accum0 = ISA::fmadd(ISA::loadu_ps(src + j + 4), w4, accum0);
        if (N >= 6) accum1 = ISA::fmadd(ISA::loadu_ps(src + j + 5), w5, accum1);
        if (N >= 7) accum0 = ISA::fmadd(ISA::loadu_ps(src + j + 6), w6, accum0);
        if (N >= 8) accum1 = ISA::fmadd(ISA::loadu_ps(src + j + 7), w7, accum1);
        if (N >= 9) accum0 = ISA::fmadd(ISA::loadu_ps(src + j + 8), w8, accum0);
        if (N >= 10) accum1 = ISA::fmadd(ISA::loadu_ps(src + j + 9), w9, accum1);

        accum0 = ISA::add_ps(accum0, accum1);
        if (Last) accum0 = ISA::scale_bias_sat(accum0, scale, bias, satmask);
        ISA::store_ps(dst + j, accum0);
    }
}


/* ---- vertical pass kernels --------------------------------------------- */

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_byte_pass(const void * const src[], uint8_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n)
{
    typedef typename ISA::ivec ivec;
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    const uint8_t *srcp0 = static_cast<const uint8_t *>(src[K + 0]);
    const uint8_t *srcp1 = N >= 2 ? static_cast<const uint8_t *>(src[K + 1]) : srcp0;
    const uint8_t *srcp2 = N >= 3 ? static_cast<const uint8_t *>(src[K + 2]) : srcp1;
    const uint8_t *srcp3 = N >= 4 ? static_cast<const uint8_t *>(src[K + 3]) : srcp2;
    const uint8_t *srcp4 = N >= 5 ? static_cast<const uint8_t *>(src[K + 4]) : srcp3;
    const uint8_t *srcp5 = N >= 6 ? static_cast<const uint8_t *>(src[K + 5]) : srcp4;
    const uint8_t *srcp6 = N >= 7 ? static_cast<const uint8_t *>(src[K + 6]) : srcp5;
    const uint8_t *srcp7 = N >= 8 ? static_cast<const uint8_t *>(src[K + 7]) : srcp6;
    const uint8_t *srcp8 = N >= 9 ? static_cast<const uint8_t *>(src[K + 8]) : srcp7;
    const uint8_t *srcp9 = N >= 10 ? static_cast<const uint8_t *>(src[K + 9]) : srcp8;

    ivec w0w1 = ISA::set1_i32((weight(1) << 16) | weight(0));
    ivec w2w3 = ISA::set1_i32((weight(3) << 16) | weight(2));
    ivec w4w5 = ISA::set1_i32((weight(5) << 16) | weight(4));
    ivec w6w7 = ISA::set1_i32((weight(7) << 16) | weight(6));
    ivec w8w9 = ISA::set1_i32((weight(9) << 16) | weight(8));

    typename ISA::fvec scale = ISA::set1_ps(params.div);
    typename ISA::fvec bias = ISA::set1_ps(params.bias);
    typename ISA::fvec satmask = ISA::satmask(params.saturate);

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::IPELS) {
        ivec accum_lo = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 0 * ISA::LANES);
        ivec accum_hi = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 1 * ISA::LANES);
        ivec x0, x1;

        if (N >= 1) {
            x0 = ISA::widen_byte(srcp0 + j); x1 = ISA::widen_byte(srcp1 + j);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w0w1));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w0w1));
        }
        if (N >= 3) {
            x0 = ISA::widen_byte(srcp2 + j); x1 = ISA::widen_byte(srcp3 + j);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w2w3));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w2w3));
        }
        if (N >= 5) {
            x0 = ISA::widen_byte(srcp4 + j); x1 = ISA::widen_byte(srcp5 + j);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w4w5));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w4w5));
        }
        if (N >= 7) {
            x0 = ISA::widen_byte(srcp6 + j); x1 = ISA::widen_byte(srcp7 + j);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w6w7));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w6w7));
        }
        if (N >= 9) {
            x0 = ISA::widen_byte(srcp8 + j); x1 = ISA::widen_byte(srcp9 + j);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w8w9));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w8w9));
        }

        if (Last)
            ISA::store_u8(dst + j, accum_lo, accum_hi, scale, bias, satmask);
        else {
            ISA::store_acc(tmp + j + 0 * ISA::LANES, accum_lo);
            ISA::store_acc(tmp + j + 1 * ISA::LANES, accum_hi);
        }
    }
}

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_word_pass(const void * const src[], uint16_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n, int32_t i32bias)
{
    typedef typename ISA::ivec ivec;
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    const uint16_t *srcp0 = static_cast<const uint16_t *>(src[K + 0]);
    const uint16_t *srcp1 = N >= 2 ? static_cast<const uint16_t *>(src[K + 1]) : srcp0;
    const uint16_t *srcp2 = N >= 3 ? static_cast<const uint16_t *>(src[K + 2]) : srcp1;
    const uint16_t *srcp3 = N >= 4 ? static_cast<const uint16_t *>(src[K + 3]) : srcp2;
    const uint16_t *srcp4 = N >= 5 ? static_cast<const uint16_t *>(src[K + 4]) : srcp3;
    const uint16_t *srcp5 = N >= 6 ? static_cast<const uint16_t *>(src[K + 5]) : srcp4;
    const uint16_t *srcp6 = N >= 7 ? static_cast<const uint16_t *>(src[K + 6]) : srcp5;
    const uint16_t *srcp7 = N >= 8 ? static_cast<const uint16_t *>(src[K + 7]) : srcp6;
    const uint16_t *srcp8 = N >= 9 ? static_cast<const uint16_t *>(src[K + 8]) : srcp7;
    const uint16_t *srcp9 = N >= 10 ? static_cast<const uint16_t *>(src[K + 9]) : srcp8;

    ivec w0w1 = ISA::set1_i32((weight(1) << 16) | weight(0));
    ivec w2w3 = ISA::set1_i32((weight(3) << 16) | weight(2));
    ivec w4w5 = ISA::set1_i32((weight(5) << 16) | weight(4));
    ivec w6w7 = ISA::set1_i32((weight(7) << 16) | weight(6));
    ivec w8w9 = ISA::set1_i32((weight(9) << 16) | weight(8));
    ivec w_bias = ISA::set1_i32(i32bias);
    ivec bias16 = ISA::word_bias();

    typename ISA::fvec scale = ISA::set1_ps(params.div);
    typename ISA::fvec bias = ISA::set1_ps(params.bias);
    typename ISA::fvec satmask = ISA::satmask(params.saturate);
    ivec maxval = ISA::word_maxval(params.maxval);

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::IPELS) {
        ivec accum_lo = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 0 * ISA::LANES);
        ivec accum_hi = First ? ISA::zero_i() : ISA::load_acc(tmp + j + 1 * ISA::LANES);
        ivec x0, x1;

        if (N >= 1) {
            x0 = ISA::load_word_aligned_biased(srcp0 + j, bias16); x1 = ISA::load_word_aligned_biased(srcp1 + j, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w0w1));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w0w1));
        }
        if (N >= 3) {
            x0 = ISA::load_word_aligned_biased(srcp2 + j, bias16); x1 = ISA::load_word_aligned_biased(srcp3 + j, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w2w3));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w2w3));
        }
        if (N >= 5) {
            x0 = ISA::load_word_aligned_biased(srcp4 + j, bias16); x1 = ISA::load_word_aligned_biased(srcp5 + j, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w4w5));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w4w5));
        }
        if (N >= 7) {
            x0 = ISA::load_word_aligned_biased(srcp6 + j, bias16); x1 = ISA::load_word_aligned_biased(srcp7 + j, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w6w7));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w6w7));
        }
        if (N >= 9) {
            x0 = ISA::load_word_aligned_biased(srcp8 + j, bias16); x1 = ISA::load_word_aligned_biased(srcp9 + j, bias16);
            accum_lo = ISA::add32(accum_lo, ISA::madd(ISA::unpacklo16(x0, x1), w8w9));
            accum_hi = ISA::add32(accum_hi, ISA::madd(ISA::unpackhi16(x0, x1), w8w9));
        }

        if (Last) {
            accum_lo = ISA::sub32(accum_lo, w_bias);
            accum_hi = ISA::sub32(accum_hi, w_bias);
            ISA::store_u16(dst + j, accum_lo, accum_hi, scale, bias, satmask, maxval);
        } else {
            ISA::store_acc(tmp + j + 0 * ISA::LANES, accum_lo);
            ISA::store_acc(tmp + j + 1 * ISA::LANES, accum_hi);
        }
    }
}

template <class ISA, unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_float_pass(const void * const src[], float *dst, const vs_generic_params &params, unsigned n)
{
    typedef typename ISA::fvec fvec;
    auto weight = [=](unsigned k) -> float { return k < N ? params.matrixf[K + k] : 0; };

    const float *srcp0 = static_cast<const float *>(src[K + 0]);
    const float *srcp1 = N >= 2 ? static_cast<const float *>(src[K + 1]) : srcp0;
    const float *srcp2 = N >= 3 ? static_cast<const float *>(src[K + 2]) : srcp1;
    const float *srcp3 = N >= 4 ? static_cast<const float *>(src[K + 3]) : srcp2;
    const float *srcp4 = N >= 5 ? static_cast<const float *>(src[K + 4]) : srcp3;
    const float *srcp5 = N >= 6 ? static_cast<const float *>(src[K + 5]) : srcp4;
    const float *srcp6 = N >= 7 ? static_cast<const float *>(src[K + 6]) : srcp5;
    const float *srcp7 = N >= 8 ? static_cast<const float *>(src[K + 7]) : srcp6;
    const float *srcp8 = N >= 9 ? static_cast<const float *>(src[K + 8]) : srcp7;
    const float *srcp9 = N >= 10 ? static_cast<const float *>(src[K + 9]) : srcp8;

    fvec w0 = ISA::set1_ps(weight(0)), w1 = ISA::set1_ps(weight(1)), w2 = ISA::set1_ps(weight(2));
    fvec w3 = ISA::set1_ps(weight(3)), w4 = ISA::set1_ps(weight(4)), w5 = ISA::set1_ps(weight(5));
    fvec w6 = ISA::set1_ps(weight(6)), w7 = ISA::set1_ps(weight(7)), w8 = ISA::set1_ps(weight(8));
    fvec w9 = ISA::set1_ps(weight(9));

    fvec scale = ISA::set1_ps(params.div);
    fvec bias = ISA::set1_ps(params.bias);
    fvec satmask = ISA::satmask(params.saturate);

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += ISA::FLANES) {
        fvec accum0 = First ? ISA::fzero() : ISA::load_ps(dst + j);
        fvec accum1 = ISA::fzero();

        if (N >= 1) accum0 = ISA::fmadd(ISA::load_ps(srcp0 + j), w0, accum0);
        if (N >= 2) accum1 = ISA::fmadd(ISA::load_ps(srcp1 + j), w1, accum1);
        if (N >= 3) accum0 = ISA::fmadd(ISA::load_ps(srcp2 + j), w2, accum0);
        if (N >= 4) accum1 = ISA::fmadd(ISA::load_ps(srcp3 + j), w3, accum1);
        if (N >= 5) accum0 = ISA::fmadd(ISA::load_ps(srcp4 + j), w4, accum0);
        if (N >= 6) accum1 = ISA::fmadd(ISA::load_ps(srcp5 + j), w5, accum1);
        if (N >= 7) accum0 = ISA::fmadd(ISA::load_ps(srcp6 + j), w6, accum0);
        if (N >= 8) accum1 = ISA::fmadd(ISA::load_ps(srcp7 + j), w7, accum1);
        if (N >= 9) accum0 = ISA::fmadd(ISA::load_ps(srcp8 + j), w8, accum0);
        if (N >= 10) accum1 = ISA::fmadd(ISA::load_ps(srcp9 + j), w9, accum1);

        accum0 = ISA::add_ps(accum0, accum1);
        if (Last) accum0 = ISA::scale_bias_sat(accum0, scale, bias, satmask);
        ISA::store_ps(dst + j, accum0);
    }
}


/* ---- per-N dispatchers (multi-pass split identical across ISA) ---------- */

template <class ISA, unsigned N>
void conv_scanline_h_byte(const void *src, void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    const uint8_t *srcp = static_cast<const uint8_t *>(src);
    uint8_t *dstp = static_cast<uint8_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);

    if (N > 13) {
        conv_scanline_h_byte_pass<ISA, 12, 0, true, false>(srcp, dstp, tmpp, params, n);
        conv_scanline_h_byte_pass<ISA, N - 12, 12, false, true>(srcp, dstp, tmpp, params, n);
    } else {
        conv_scanline_h_byte_pass<ISA, N, 0, true, true>(srcp, dstp, nullptr, params, n);
    }
}

template <class ISA, unsigned N>
void conv_scanline_h_word(const void *src, void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    const uint16_t *srcp = static_cast<const uint16_t *>(src);
    uint16_t *dstp = static_cast<uint16_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);
    int32_t bias = 0;

    for (unsigned k = 0; k < N; ++k)
        bias += static_cast<int32_t>(INT16_MIN) * params.matrix[k];

    if (N > 13) {
        conv_scanline_h_word_pass<ISA, 12, 0, true, false>(srcp, dstp, tmpp, params, n, bias);
        conv_scanline_h_word_pass<ISA, N - 12, 12, false, true>(srcp, dstp, tmpp, params, n, bias);
    } else {
        conv_scanline_h_word_pass<ISA, N, 0, true, true>(srcp, dstp, nullptr, params, n, bias);
    }
}

template <class ISA, unsigned N>
void conv_scanline_h_float(const void *src, void *dst, void *, const vs_generic_params &params, unsigned n)
{
    const float *srcp = static_cast<const float *>(src);
    float *dstp = static_cast<float *>(dst);

    if (N > 19) {
        conv_scanline_h_float_pass<ISA, 10, 0, true, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<ISA, 10, 10, false, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<ISA, N - 20, 20, false, true>(srcp, dstp, params, n);
    } else if (N > 9) {
        conv_scanline_h_float_pass<ISA, 10, 0, true, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<ISA, N - 10, 10, false, true>(srcp, dstp, params, n);
    } else {
        conv_scanline_h_float_pass<ISA, N, 0, true, true>(srcp, dstp, params, n);
    }
}

template <class ISA, unsigned N>
void conv_scanline_v_byte(const void * const src[], void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    uint8_t *dstp = static_cast<uint8_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);

    if (N > 19) {
        conv_scanline_v_byte_pass<ISA, 10, 0, true, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<ISA, 10, 10, false, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<ISA, N - 20, 20, false, true>(src, dstp, tmpp, params, n);
    } else if (N > 9) {
        conv_scanline_v_byte_pass<ISA, 10, 0, true, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<ISA, N - 10, 10, false, true>(src, dstp, tmpp, params, n);
    } else {
        conv_scanline_v_byte_pass<ISA, N, 0, true, true>(src, dstp, tmpp, params, n);
    }
}

template <class ISA, unsigned N>
void conv_scanline_v_word(const void * const src[], void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    uint16_t *dstp = static_cast<uint16_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);
    int32_t bias = 0;

    for (unsigned k = 0; k < N; ++k)
        bias += static_cast<int32_t>(INT16_MIN) * params.matrix[k];

    if (N > 19) {
        conv_scanline_v_word_pass<ISA, 10, 0, true, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<ISA, 10, 10, false, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<ISA, N - 20, 20, false, true>(src, dstp, tmpp, params, n, bias);
    } else if (N > 9) {
        conv_scanline_v_word_pass<ISA, 10, 0, true, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<ISA, N - 10, 10, false, true>(src, dstp, tmpp, params, n, bias);
    } else {
        conv_scanline_v_word_pass<ISA, N, 0, true, true>(src, dstp, tmpp, params, n, bias);
    }
}

template <class ISA, unsigned N>
void conv_scanline_v_float(const void * const src[], void *dst, void *, const vs_generic_params &params, unsigned n)
{
    float *dstp = static_cast<float *>(dst);

    if (N > 19) {
        conv_scanline_v_float_pass<ISA, 10, 0, true, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<ISA, 10, 10, false, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<ISA, N - 20, 20, false, true>(src, dstp, params, n);
    } else if (N > 9) {
        conv_scanline_v_float_pass<ISA, 10, 0, true, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<ISA, N - 10, 10, false, true>(src, dstp, params, n);
    } else {
        conv_scanline_v_float_pass<ISA, N, 0, true, true>(src, dstp, params, n);
    }
}


/* ---- fwidth -> kernel selection ---------------------------------------- */

#define VS_CONV_SELECT(dir, type, T) \
template <class ISA> \
decltype(&conv_scanline_##dir##_##type<ISA, 0>) select_conv_scanline_##dir##_##type##_impl(unsigned fwidth) \
{ \
  switch (fwidth) { \
  case 3: return conv_scanline_##dir##_##type<ISA, 3>; \
  case 5: return conv_scanline_##dir##_##type<ISA, 5>; \
  case 7: return conv_scanline_##dir##_##type<ISA, 7>; \
  case 9: return conv_scanline_##dir##_##type<ISA, 9>; \
  case 11: return conv_scanline_##dir##_##type<ISA, 11>; \
  case 13: return conv_scanline_##dir##_##type<ISA, 13>; \
  case 15: return conv_scanline_##dir##_##type<ISA, 15>; \
  case 17: return conv_scanline_##dir##_##type<ISA, 17>; \
  case 19: return conv_scanline_##dir##_##type<ISA, 19>; \
  case 21: return conv_scanline_##dir##_##type<ISA, 21>; \
  case 23: return conv_scanline_##dir##_##type<ISA, 23>; \
  case 25: return conv_scanline_##dir##_##type<ISA, 25>; \
  default: return nullptr; \
  } \
}

VS_CONV_SELECT(h, byte, uint8_t)
VS_CONV_SELECT(h, word, uint16_t)
VS_CONV_SELECT(h, float, float)
VS_CONV_SELECT(v, byte, uint8_t)
VS_CONV_SELECT(v, word, uint16_t)
VS_CONV_SELECT(v, float, float)

#undef VS_CONV_SELECT

template <class ISA> auto select_conv_scanline_h_for(uint8_t)  { return select_conv_scanline_h_byte_impl<ISA>; }
template <class ISA> auto select_conv_scanline_h_for(uint16_t) { return select_conv_scanline_h_word_impl<ISA>; }
template <class ISA> auto select_conv_scanline_h_for(float)    { return select_conv_scanline_h_float_impl<ISA>; }
template <class ISA> auto select_conv_scanline_v_for(uint8_t)  { return select_conv_scanline_v_byte_impl<ISA>; }
template <class ISA> auto select_conv_scanline_v_for(uint16_t) { return select_conv_scanline_v_word_impl<ISA>; }
template <class ISA> auto select_conv_scanline_v_for(float)    { return select_conv_scanline_v_float_impl<ISA>; }


/* ---- plane drivers ----------------------------------------------------- */

template <class ISA, class T>
void conv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    // HBLK = left-edge block width / vector-store granularity for this tier.
    // It must be a multiple of the integer pixels-per-iteration AND match the
    // frame's stride granularity (= frame_alignment / sizeof(word) pixels), so a
    // whole-vector store never crosses the row stride: 16 for the 32-byte aligned
    // SSE2/AVX2 frames, 32 for the 64-byte aligned AVX-512 frames.
    constexpr unsigned HBLK = ISA::HBLK;
    constexpr unsigned CENTER = HBLK + 12;   // copied input pixels for the left block

    unsigned vec_end = std::max((std::max(width, 13U) - 13) & ~(HBLK - 1), HBLK);

    // Max support = 12. Buffering: 12 before + (HBLK window) + lookahead + 12 after.
    alignas(ISA::ALIGN) T padded[ISA::PADDED];

    // Multi-pass threshold = 13.
    auto kernel = select_conv_scanline_h_for<ISA>(T{})(params.matrixsize);
    void *tmp = (params.matrixsize > 13 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + ISA::TMP_PAD) * sizeof(int32_t), ISA::ALIGN) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = static_cast<const T *>(line_ptr(src, i, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        // Pixels 0...HBLK-1.
        {
            // Center.
            std::copy_n(srcp, std::min(width, CENTER), padded + 16);
            flip_left(padded + 16, 12);
            if (width < CENTER)
                flip_right(padded + 16 + width, std::min(CENTER - width, 12U));
            if (width >= HBLK) {
                kernel(padded + 16, dstp, tmp, params, HBLK);
            } else {
                // Narrow row: a whole-vector store of the fixed HBLK-pixel block would
                // overflow the destination stride (e.g. float width <= 8 at 32-byte
                // alignment). Bounce through a scratch buffer and copy only the valid
                // pixels. (width >= HBLK guarantees the HBLK-wide store fits the stride.)
                alignas(ISA::ALIGN) T outbuf[ISA::PADDED];
                kernel(padded + 16, outbuf, tmp, params, HBLK);
                std::copy_n(outbuf, width, dstp);
            }
        }

        // Pixels HBLK...vec_end-1.
        if (vec_end > HBLK) {
            kernel(srcp + HBLK, dstp + HBLK, tmp, params, vec_end - HBLK);
        }

        // Pixels vec_end...N-1
        if (width > HBLK) {
            std::copy_n(srcp + vec_end - 12U, width - vec_end + 12U, padded + 4U);
            flip_right(padded + 16U + (width - vec_end), 12U);
            kernel(padded + 16, dstp + vec_end, tmp, params, width - vec_end);
        }
    }

    vsh::vsh_aligned_free(tmp);
}

template <class ISA, class T>
void conv_plane_v(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;

    // Multi-pass threshold = 9.
    auto kernel = select_conv_scanline_v_for<ISA>(T{})(params.matrixsize);
    void *tmp = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + ISA::TMP_PAD) * sizeof(int32_t), ISA::ALIGN) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        gather_v_rows(src, src_stride, i, height, fwidth, srcp);
        kernel(srcp, dstp, tmp, params, width);
    }

    vsh::vsh_aligned_free(tmp);
}

template <class ISA, class T>
void conv_plane_x(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;

    // Multi-pass threshold = 9 (v), 13 (h).
    auto kernel_v = select_conv_scanline_v_for<ISA>(T{})(params.matrixsize);
    auto kernel_h = select_conv_scanline_h_for<ISA>(T{})(params.matrixsize);
    T *tmp1 = vsh::vsh_aligned_malloc<T>((width + 64) * sizeof(T), ISA::ALIGN);
    void *tmp2 = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + ISA::TMP_PAD) * sizeof(int32_t), ISA::ALIGN) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        gather_v_rows(src, src_stride, i, height, fwidth, srcp);

        kernel_v(srcp, tmp1 + 32, tmp2, params, width);
        flip_left(tmp1 + 32, 12);
        flip_right(tmp1 + 32 + width, 12);
        kernel_h(tmp1 + 32, dstp, tmp2, params, width);
    }

    vsh::vsh_aligned_free(tmp2);
    vsh::vsh_aligned_free(tmp1);
}

} // namespace

/* Emit the 9 exported entry points for one ISA tier. */
#define VS_CONV_ENTRYPOINTS(ISA, SUFFIX) \
    void vs_generic_1d_conv_h_byte_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_h<ISA, uint8_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_1d_conv_h_word_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_h<ISA, uint16_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_1d_conv_h_float_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_h<ISA, float>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_1d_conv_v_byte_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_v<ISA, uint8_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_1d_conv_v_word_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_v<ISA, uint16_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_1d_conv_v_float_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_v<ISA, float>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_2d_conv_sep_byte_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_x<ISA, uint8_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_2d_conv_sep_word_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_x<ISA, uint16_t>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_2d_conv_sep_float_##SUFFIX(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_x<ISA, float>(src, src_stride, dst, dst_stride, *params, width, height); }

#endif // CONVOLUTION_IMPL_H
