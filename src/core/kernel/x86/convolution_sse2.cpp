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

#include <cstdint>
#include <emmintrin.h>

namespace {

// SSE2 primitive layer for convolution_impl.h.
//   128-bit vectors: 4x int32 accumulator lanes, 8 integer pixels / iter,
//   4 float lanes / iter. Integer paths are bit-exact with the C reference;
//   the int16 signed-bias dance works around the lack of unsigned 16-bit
//   saturating ops in SSE2.
struct ISA_SSE2 {
    typedef __m128i ivec;
    typedef __m128 fvec;

    static constexpr unsigned LANES = 4;   // int32 lanes per accumulator
    static constexpr unsigned IPELS = 8;   // integer pixels per iteration
    static constexpr unsigned FLANES = 4;  // float lanes per iteration
    static constexpr size_t ALIGN = 16;
    static constexpr unsigned TMP_PAD = 8;
    static constexpr unsigned HBLK = 16;   // horizontal left-block / store granularity
    static constexpr unsigned PADDED = 64; // mirror scratch buffer size

    static ivec zero_i() { return _mm_setzero_si128(); }
    static ivec set1_i32(uint32_t x) { return _mm_set1_epi32(static_cast<int>(x)); }
    static ivec load_acc(const int32_t *p) { return _mm_load_si128((const __m128i *)p); }
    static void store_acc(int32_t *p, ivec v) { _mm_store_si128((__m128i *)p, v); }

    static ivec unpacklo16(ivec a, ivec b) { return _mm_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm_unpackhi_epi16(a, b); }
    static ivec madd(ivec a, ivec b) { return _mm_madd_epi16(a, b); }
    static ivec add32(ivec a, ivec b) { return _mm_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm_sub_epi32(a, b); }

    static ivec widen_byte(const uint8_t *p)
    {
        return _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)p), _mm_setzero_si128());
    }
    static ivec word_bias() { return _mm_set1_epi16(INT16_MIN); }
    static ivec load_word_biased(const uint16_t *p, ivec bias16)
    {
        return _mm_add_epi16(_mm_loadu_si128((const __m128i *)p), bias16);
    }
    static ivec load_word_aligned_biased(const uint16_t *p, ivec bias16)
    {
        return _mm_add_epi16(_mm_load_si128((const __m128i *)p), bias16);
    }
    static ivec word_maxval(uint16_t mv)
    {
        return _mm_set1_epi16(static_cast<int16_t>(static_cast<int32_t>(mv) + INT16_MIN));
    }

    static fvec fzero() { return _mm_setzero_ps(); }
    static fvec set1_ps(float x) { return _mm_set_ps1(x); }
    static fvec loadu_ps(const float *p) { return _mm_loadu_ps(p); }
    static fvec load_ps(const float *p) { return _mm_load_ps(p); }
    static void store_ps(float *p, fvec v) { _mm_store_ps(p, v); }
    static fvec add_ps(fvec a, fvec b) { return _mm_add_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm_add_ps(_mm_mul_ps(a, b), c); }
    static fvec satmask(uint8_t saturate)
    {
        return _mm_castsi128_ps(_mm_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    }
    static fvec scale_bias_sat(fvec x, fvec scale, fvec bias, fvec mask)
    {
        return _mm_and_ps(_mm_add_ps(_mm_mul_ps(x, scale), bias), mask);
    }

    static void store_u8(uint8_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask)
    {
        fvec t;
        t = scale_bias_sat(_mm_cvtepi32_ps(lo), scale, bias, mask); lo = _mm_cvtps_epi32(t);
        t = scale_bias_sat(_mm_cvtepi32_ps(hi), scale, bias, mask); hi = _mm_cvtps_epi32(t);
        lo = _mm_packs_epi32(lo, hi);
        lo = _mm_packus_epi16(lo, lo);
        _mm_storel_epi64((__m128i *)dst, lo);
    }
    static void store_u16(uint16_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec t;
        t = scale_bias_sat(_mm_cvtepi32_ps(lo), scale, bias, mask);
        lo = _mm_cvtps_epi32(t); lo = _mm_add_epi32(lo, _mm_set1_epi32(INT16_MIN));
        t = scale_bias_sat(_mm_cvtepi32_ps(hi), scale, bias, mask);
        hi = _mm_cvtps_epi32(t); hi = _mm_add_epi32(hi, _mm_set1_epi32(INT16_MIN));
        lo = _mm_packs_epi32(lo, hi);
        lo = _mm_min_epi16(lo, maxval);
        lo = _mm_sub_epi16(lo, _mm_set1_epi16(INT16_MIN));
        _mm_store_si128((__m128i *)dst, lo);
    }
};

} // namespace

#include "convolution_impl.h"

VS_CONV_ENTRYPOINTS(ISA_SSE2, sse2)
