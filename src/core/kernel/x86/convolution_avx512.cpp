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
#include <immintrin.h>

namespace {

// AVX-512 primitive layer for convolution_impl.h (x86-64-v4: F+BW+DQ+VL+CD).
//   512-bit vectors: 16x int32 accumulator lanes, 32 integer pixels / iter,
//   16 float lanes / iter. Frames are 64-byte aligned/strided whenever this
//   tier runs (VSFrame::alignment == 64 on AVX-512 hardware), so aligned row
//   loads/stores are safe.
//
// Lane bookkeeping: unpacklo/hi_epi16, packs/packus and min operate within each
// 128-bit lane (4 lanes here). After the tap loop accum_lo holds output pixels
// {L*8+0..3} and accum_hi {L*8+4..7} for lanes L=0..3 -- i.e. a 4-way lane
// interleave. packus_epi32(lo,hi) re-linearises this for the 16-bit store; the
// 8-bit store additionally needs a vpermq to gather the per-lane low halves.
struct ISA_AVX512 {
    typedef __m512i ivec;
    typedef __m512 fvec;

    static constexpr unsigned LANES = 16;   // int32 lanes per accumulator
    static constexpr unsigned IPELS = 32;   // integer pixels per iteration
    static constexpr unsigned FLANES = 16;  // float lanes per iteration
    static constexpr size_t ALIGN = 64;
    static constexpr unsigned TMP_PAD = 32;
    static constexpr unsigned HBLK = 32;    // horizontal left-block / store granularity
    static constexpr unsigned PADDED = 96;  // mirror scratch buffer size

    static ivec zero_i() { return _mm512_setzero_si512(); }
    static ivec set1_i32(uint32_t x) { return _mm512_set1_epi32(static_cast<int>(x)); }
    static ivec load_acc(const int32_t *p) { return _mm512_load_si512((const void *)p); }
    static void store_acc(int32_t *p, ivec v) { _mm512_store_si512((void *)p, v); }

    static ivec unpacklo16(ivec a, ivec b) { return _mm512_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm512_unpackhi_epi16(a, b); }
    static ivec madd(ivec a, ivec b) { return _mm512_madd_epi16(a, b); }
    static ivec add32(ivec a, ivec b) { return _mm512_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm512_sub_epi32(a, b); }

    static ivec widen_byte(const uint8_t *p)
    {
        return _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)p));
    }
    static ivec word_bias() { return _mm512_set1_epi16(INT16_MIN); }
    static ivec load_word_biased(const uint16_t *p, ivec bias16)
    {
        return _mm512_add_epi16(_mm512_loadu_si512((const void *)p), bias16);
    }
    static ivec load_word_aligned_biased(const uint16_t *p, ivec bias16)
    {
        return _mm512_add_epi16(_mm512_load_si512((const void *)p), bias16);
    }
    static ivec word_maxval(uint16_t mv) { return _mm512_set1_epi16(static_cast<int16_t>(mv)); }

    static fvec fzero() { return _mm512_setzero_ps(); }
    static fvec set1_ps(float x) { return _mm512_set1_ps(x); }
    static fvec loadu_ps(const float *p) { return _mm512_loadu_ps(p); }
    static fvec load_ps(const float *p) { return _mm512_load_ps(p); }
    static void store_ps(float *p, fvec v) { _mm512_store_ps(p, v); }
    static fvec add_ps(fvec a, fvec b) { return _mm512_add_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm512_fmadd_ps(a, b, c); }
    static fvec satmask(uint8_t saturate)
    {
        return _mm512_castsi512_ps(_mm512_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    }
    static fvec scale_bias_sat(fvec x, fvec scale, fvec bias, fvec mask)
    {
        return _mm512_and_ps(_mm512_fmadd_ps(x, scale, bias), mask);
    }

    static void store_u8(uint8_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask)
    {
        fvec t;
        t = scale_bias_sat(_mm512_cvtepi32_ps(lo), scale, bias, mask); lo = _mm512_cvtps_epi32(t);
        t = scale_bias_sat(_mm512_cvtepi32_ps(hi), scale, bias, mask); hi = _mm512_cvtps_epi32(t);
        lo = _mm512_packs_epi32(lo, hi);          // per-lane: 32 i16 linear [0..31]
        lo = _mm512_packus_epi16(lo, lo);         // per-lane: valid bytes in low qword
        // Gather the low qword of each 128-bit lane (qwords 0,2,4,6) into the low 256.
        ivec idx = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
        lo = _mm512_permutexvar_epi64(idx, lo);
        _mm256_store_si256((__m256i *)dst, _mm512_castsi512_si256(lo));
    }
    static void store_u16(uint16_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec t;
        t = scale_bias_sat(_mm512_cvtepi32_ps(lo), scale, bias, mask); lo = _mm512_cvtps_epi32(t);
        t = scale_bias_sat(_mm512_cvtepi32_ps(hi), scale, bias, mask); hi = _mm512_cvtps_epi32(t);
        lo = _mm512_packus_epi32(lo, hi);         // per-lane: 32 u16 linear [0..31]
        lo = _mm512_min_epu16(lo, maxval);
        _mm512_store_si512((void *)dst, lo);
    }

    // Unaligned stores for the square-convolution interior (starts at an unaligned column).
    static void storeu_ps(float *p, fvec v) { _mm512_storeu_ps(p, v); }
    static void storeu_u8(uint8_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask)
    {
        fvec t;
        t = scale_bias_sat(_mm512_cvtepi32_ps(lo), scale, bias, mask); lo = _mm512_cvtps_epi32(t);
        t = scale_bias_sat(_mm512_cvtepi32_ps(hi), scale, bias, mask); hi = _mm512_cvtps_epi32(t);
        lo = _mm512_packs_epi32(lo, hi);
        lo = _mm512_packus_epi16(lo, lo);
        ivec idx = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
        lo = _mm512_permutexvar_epi64(idx, lo);
        _mm256_storeu_si256((__m256i *)dst, _mm512_castsi512_si256(lo));
    }
    static void storeu_u16(uint16_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec t;
        t = scale_bias_sat(_mm512_cvtepi32_ps(lo), scale, bias, mask); lo = _mm512_cvtps_epi32(t);
        t = scale_bias_sat(_mm512_cvtepi32_ps(hi), scale, bias, mask); hi = _mm512_cvtps_epi32(t);
        lo = _mm512_packus_epi32(lo, hi);
        lo = _mm512_min_epu16(lo, maxval);
        _mm512_storeu_si512((void *)dst, lo);
    }

    // int64 accumulation for the large-N (9x9/11x11) word path: per-row int32 sums are
    // widened and spilled into int64 so the total (up to N*N*1023*65535) cannot overflow.
    static ivec set1_i64(int64_t x) { return _mm512_set1_epi64(x); }
    static ivec add64(ivec a, ivec b) { return _mm512_add_epi64(a, b); }
    static ivec sub64(ivec a, ivec b) { return _mm512_sub_epi64(a, b); }
    static ivec widenlo_i64(ivec x) { return _mm512_cvtepi32_epi64(_mm512_castsi512_si256(x)); }
    static ivec widenhi_i64(ivec x) { return _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(x, 1)); }
    // Store 32 pixels held as four int64 accumulators. cvtepi64_ps (AVX-512DQ) is exact for our
    // magnitudes, so this is bit-exact with the int64 C reference; packus re-linearises the scramble.
    static void storeu_u16_i64(uint16_t *dst, ivec la, ivec lb, ivec ha, ivec hb, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec flo = _mm512_insertf32x8(_mm512_castps256_ps512(_mm512_cvtepi64_ps(la)), _mm512_cvtepi64_ps(lb), 1);
        fvec fhi = _mm512_insertf32x8(_mm512_castps256_ps512(_mm512_cvtepi64_ps(ha)), _mm512_cvtepi64_ps(hb), 1);
        flo = scale_bias_sat(flo, scale, bias, mask); ivec ilo = _mm512_cvtps_epi32(flo);
        fhi = scale_bias_sat(fhi, scale, bias, mask); ivec ihi = _mm512_cvtps_epi32(fhi);
        ivec out = _mm512_packus_epi32(ilo, ihi);
        out = _mm512_min_epu16(out, maxval);
        _mm512_storeu_si512((void *)dst, out);
    }
};

} // namespace

#include "convolution_impl.h"
#include "square_impl.h"
#include "square_vnni_impl.h"

VS_CONV_ENTRYPOINTS(ISA_AVX512, avx512)
VS_SQUARE_ENTRYPOINTS(ISA_AVX512, avx512)
VS_SQUARE_VNNI_ENTRYPOINTS
