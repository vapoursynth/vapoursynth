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

// AVX2 primitive layer for convolution_impl.h.
//   256-bit vectors: 8x int32 accumulator lanes, 16 integer pixels / iter,
//   8 float lanes / iter. AVX2 has native unsigned 16-bit saturating pack/min,
//   so the int16 sign-bias dance the SSE2 tier needs is dropped. The float
//   paths use FMA (single rounding) -- not bit-exact with SSE2/C by design.
struct ISA_AVX2 {
    typedef __m256i ivec;
    typedef __m256 fvec;

    static constexpr unsigned LANES = 8;    // int32 lanes per accumulator
    static constexpr unsigned IPELS = 16;   // integer pixels per iteration
    static constexpr unsigned FLANES = 8;   // float lanes per iteration
    static constexpr size_t ALIGN = 32;
    static constexpr unsigned TMP_PAD = 16;
    static constexpr unsigned HBLK = 16;    // horizontal left-block / store granularity
    static constexpr unsigned PADDED = 64;  // mirror scratch buffer size

    static ivec zero_i() { return _mm256_setzero_si256(); }
    static ivec set1_i32(uint32_t x) { return _mm256_set1_epi32(static_cast<int>(x)); }
    static ivec load_acc(const int32_t *p) { return _mm256_load_si256((const __m256i *)p); }
    static void store_acc(int32_t *p, ivec v) { _mm256_store_si256((__m256i *)p, v); }

    static ivec unpacklo16(ivec a, ivec b) { return _mm256_unpacklo_epi16(a, b); }
    static ivec unpackhi16(ivec a, ivec b) { return _mm256_unpackhi_epi16(a, b); }
    static ivec madd(ivec a, ivec b) { return _mm256_madd_epi16(a, b); }
    static ivec add32(ivec a, ivec b) { return _mm256_add_epi32(a, b); }
    static ivec sub32(ivec a, ivec b) { return _mm256_sub_epi32(a, b); }

    static ivec widen_byte(const uint8_t *p)
    {
        return _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)p));
    }
    static ivec word_bias() { return _mm256_set1_epi16(INT16_MIN); }
    static ivec load_word_biased(const uint16_t *p, ivec bias16)
    {
        return _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)p), bias16);
    }
    static ivec load_word_aligned_biased(const uint16_t *p, ivec bias16)
    {
        return _mm256_add_epi16(_mm256_load_si256((const __m256i *)p), bias16);
    }
    static ivec word_maxval(uint16_t mv) { return _mm256_set1_epi16(static_cast<int16_t>(mv)); }

    static fvec fzero() { return _mm256_setzero_ps(); }
    static fvec set1_ps(float x) { return _mm256_set1_ps(x); }
    static fvec loadu_ps(const float *p) { return _mm256_loadu_ps(p); }
    static fvec load_ps(const float *p) { return _mm256_load_ps(p); }
    static void store_ps(float *p, fvec v) { _mm256_store_ps(p, v); }
    static fvec add_ps(fvec a, fvec b) { return _mm256_add_ps(a, b); }
    static fvec fmadd(fvec a, fvec b, fvec c) { return _mm256_fmadd_ps(a, b, c); }
    static fvec satmask(uint8_t saturate)
    {
        return _mm256_castsi256_ps(_mm256_set1_epi32(saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    }
    static fvec scale_bias_sat(fvec x, fvec scale, fvec bias, fvec mask)
    {
        return _mm256_and_ps(_mm256_fmadd_ps(x, scale, bias), mask);
    }

    static void store_u8(uint8_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask)
    {
        fvec t;
        t = scale_bias_sat(_mm256_cvtepi32_ps(lo), scale, bias, mask); lo = _mm256_cvtps_epi32(t);
        t = scale_bias_sat(_mm256_cvtepi32_ps(hi), scale, bias, mask); hi = _mm256_cvtps_epi32(t);
        lo = _mm256_packs_epi32(lo, hi);
        lo = _mm256_packus_epi16(lo, lo);
        lo = _mm256_permute4x64_epi64(lo, _MM_SHUFFLE(3, 1, 2, 0));
        _mm_store_si128((__m128i *)dst, _mm256_castsi256_si128(lo));
    }
    static void store_u16(uint16_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec t;
        t = scale_bias_sat(_mm256_cvtepi32_ps(lo), scale, bias, mask); lo = _mm256_cvtps_epi32(t);
        t = scale_bias_sat(_mm256_cvtepi32_ps(hi), scale, bias, mask); hi = _mm256_cvtps_epi32(t);
        lo = _mm256_packus_epi32(lo, hi);
        lo = _mm256_min_epu16(lo, maxval);
        _mm256_store_si256((__m256i *)dst, lo);
    }

    // Unaligned stores for the square-convolution interior (starts at an unaligned column).
    static void storeu_ps(float *p, fvec v) { _mm256_storeu_ps(p, v); }
    static void storeu_u8(uint8_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask)
    {
        fvec t;
        t = scale_bias_sat(_mm256_cvtepi32_ps(lo), scale, bias, mask); lo = _mm256_cvtps_epi32(t);
        t = scale_bias_sat(_mm256_cvtepi32_ps(hi), scale, bias, mask); hi = _mm256_cvtps_epi32(t);
        lo = _mm256_packs_epi32(lo, hi);
        lo = _mm256_packus_epi16(lo, lo);
        lo = _mm256_permute4x64_epi64(lo, _MM_SHUFFLE(3, 1, 2, 0));
        _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(lo));
    }
    static void storeu_u16(uint16_t *dst, ivec lo, ivec hi, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec t;
        t = scale_bias_sat(_mm256_cvtepi32_ps(lo), scale, bias, mask); lo = _mm256_cvtps_epi32(t);
        t = scale_bias_sat(_mm256_cvtepi32_ps(hi), scale, bias, mask); hi = _mm256_cvtps_epi32(t);
        lo = _mm256_packus_epi32(lo, hi);
        lo = _mm256_min_epu16(lo, maxval);
        _mm256_storeu_si256((__m256i *)dst, lo);
    }

    // int64 accumulation for the large-N (9x9/11x11) word path: per-row int32 sums are
    // widened and spilled into int64 so the total (up to N*N*1023*65535) cannot overflow.
    static ivec set1_i64(int64_t x) { return _mm256_set1_epi64x(x); }
    static ivec add64(ivec a, ivec b) { return _mm256_add_epi64(a, b); }
    static ivec sub64(ivec a, ivec b) { return _mm256_sub_epi64(a, b); }
    static ivec widenlo_i64(ivec x) { return _mm256_cvtepi32_epi64(_mm256_castsi256_si128(x)); }
    static ivec widenhi_i64(ivec x) { return _mm256_cvtepi32_epi64(_mm256_extracti128_si256(x, 1)); }
    // int64 -> double, exact for |x| < 2^51 (here |x| < 2^34). AVX2 has no native int64->fp,
    // so use the magic-number trick: bias into [0,2^52), OR into a 2^52-exponent double, subtract back.
    static __m256d i64_to_pd(ivec x)
    {
        ivec u = _mm256_add_epi64(x, _mm256_set1_epi64x(1LL << 51));
        u = _mm256_or_si256(u, _mm256_set1_epi64x(0x4330000000000000LL));
        return _mm256_sub_pd(_mm256_castsi256_pd(u), _mm256_set1_pd(6755399441055744.0));
    }
    // Store 16 pixels held as four int64 accumulators (la/lb = pixel groups of accum_lo, ha/hb of accum_hi).
    // The int64 -> float conversion is exact, so this stays bit-exact with the int64 C reference; packus
    // re-linearises the same lane scramble as store_u16.
    static void storeu_u16_i64(uint16_t *dst, ivec la, ivec lb, ivec ha, ivec hb, fvec scale, fvec bias, fvec mask, ivec maxval)
    {
        fvec flo = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm256_cvtpd_ps(i64_to_pd(la))), _mm256_cvtpd_ps(i64_to_pd(lb)), 1);
        fvec fhi = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm256_cvtpd_ps(i64_to_pd(ha))), _mm256_cvtpd_ps(i64_to_pd(hb)), 1);
        flo = scale_bias_sat(flo, scale, bias, mask); ivec ilo = _mm256_cvtps_epi32(flo);
        fhi = scale_bias_sat(fhi, scale, bias, mask); ivec ihi = _mm256_cvtps_epi32(fhi);
        ivec out = _mm256_packus_epi32(ilo, ihi);
        out = _mm256_min_epu16(out, maxval);
        _mm256_storeu_si256((__m256i *)dst, out);
    }
};

} // namespace

#include "convolution_impl.h"
#include "square_impl.h"

VS_CONV_ENTRYPOINTS(ISA_AVX2, avx2)
VS_SQUARE_ENTRYPOINTS(ISA_AVX2, avx2)
