/*
* Copyright (c) 2012-2020 Fredrik Mellbin
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
 * AVX-512 gather for Lut2, dispatched only when the LUT spills L2
 * (integer output, >= ~512 KB) on CPUs with AVX-512 (a safe fast-gather gate).
 * Frames are 64-byte aligned with rows padded to a multiple of 64 bytes when this
 * runs (AVX-512 implies avx512_f), and the min-clamp keeps every gather index within
 * the range the in-range lanes already read, so the width tail runs in full vectors
 * with no mask and no scalar tail. uint16/uint8 tables are gathered as dwords (reading
 * entry[i] plus a few overread bytes), so lutCreateHelper pads the table by 64 bytes.
 */

#include <cstdint>
#include <type_traits>
#include <cstddef>
#include <immintrin.h>

// vs_lut1_b_b_avx512vbmi needs AVX-512-VBMI, which is not part of x86-64-v4 (the level this
// TU is normally built at). Pin just that function to VBMI so its symbol exists in every
// variant; the runtime dispatch only calls it on CPUs that actually report VBMI.
#if defined(__clang__) || defined(__GNUC__)
#define VS_TARGET_AVX512VBMI __attribute__((target("avx512vbmi,avx512bw,avx512vl,avx512f")))
#else
#define VS_TARGET_AVX512VBMI
#endif

namespace {

template<typename T>
inline __m512i load_widen(const T *p) {
    if constexpr (sizeof(T) == 1)
        return _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i *)p));
    else
        return _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i *)p));
}

template<typename V>
inline void gather_store(V *d, const V *lut, __m512i idx) {
    if constexpr (sizeof(V) == 2) {
        __m512i g = _mm512_i32gather_epi32(idx, (const int *)lut, 2);
        _mm256_storeu_si256((__m256i *)d, _mm512_cvtepi32_epi16(g));
    } else {
        __m512i g = _mm512_i32gather_epi32(idx, (const int *)lut, 1);
        _mm_storeu_si128((__m128i *)d, _mm512_cvtepi32_epi8(g));
    }
}

template<typename T, typename U, typename V>
void lut2_gather(const T *sx, const U *sy, V *d, int w, const V *lut, int bitsx, unsigned mx, unsigned my) {
    __m512i vmx = _mm512_set1_epi32(mx), vmy = _mm512_set1_epi32(my);
    __m128i sh = _mm_cvtsi32_si128(bitsx);
    for (int x = 0; x < w; x += 16) {
        __m512i ix = _mm512_min_epu32(load_widen<T>(sx + x), vmx);
        __m512i iy = _mm512_min_epu32(load_widen<U>(sy + x), vmy);
        __m512i idx = _mm512_add_epi32(_mm512_sll_epi32(iy, sh), ix);
        gather_store<V>(d + x, lut, idx);
    }
}

} // namespace

void vs_lut2_gather_ww_w_avx512(const uint16_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_ww_b_avx512(const uint16_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_w_avx512(const uint16_t *sx, const uint8_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_b_avx512(const uint16_t *sx, const uint8_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_w_avx512(const uint8_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_b_avx512(const uint8_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }

VS_TARGET_AVX512VBMI
void vs_lut1_b_b_avx512vbmi(const uint8_t *src, uint8_t *dst, int w, const uint8_t *lut) {
    const __m512i lut_00_63   = _mm512_loadu_si512(lut + 0);
    const __m512i lut_64_127  = _mm512_loadu_si512(lut + 64);
    const __m512i lut_128_191 = _mm512_loadu_si512(lut + 128);
    const __m512i lut_192_255 = _mm512_loadu_si512(lut + 192);
    const __m512i zero = _mm512_setzero_si512();

    for (ptrdiff_t i = 0; i < w; i += 64) {
        __m512i x = _mm512_load_si512(src + i);
        __mmask64 lo = _mm512_cmpge_epi8_mask(x, zero);   // bytes 0..127
        __mmask64 hi = _mm512_cmplt_epi8_mask(x, zero);   // bytes 128..255
        __m512i a = _mm512_maskz_permutex2var_epi8(lo, lut_00_63, x, lut_64_127);
        __m512i b = _mm512_maskz_permutex2var_epi8(hi, lut_128_191, x, lut_192_255);
        _mm512_store_si512(dst + i, _mm512_or_si512(a, b));
    }
}
