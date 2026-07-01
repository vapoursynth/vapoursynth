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
 * AVX-512 masked gather for Lut2, dispatched only when the LUT spills L2
 * (integer output, >= ~512 KB) on CPUs with AVX-512 (a safe fast-gather gate).
 * Mask registers handle the width remainder in-lane, so there is no scalar
 * tail. uint16/uint8 tables are gathered as dwords (reading entry[i] plus a
 * few overread bytes), so lutCreateHelper pads the table by 64 bytes.
 */

#include <cstdint>
#include <type_traits>
#include <immintrin.h>

namespace {

template<typename T>
inline __m512i load_widen(const T *p, __mmask16 k) {
    if constexpr (sizeof(T) == 1)
        return _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(k, p));
    else
        return _mm512_cvtepu16_epi32(_mm256_maskz_loadu_epi16(k, p));
}

template<typename V>
inline void gather_store(V *d, __mmask16 k, const V *lut, __m512i idx) {
    if constexpr (sizeof(V) == 2) {
        __m512i g = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), k, idx, (const int *)lut, 2);
        _mm256_mask_storeu_epi16(d, k, _mm512_cvtepi32_epi16(g));
    } else {
        __m512i g = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), k, idx, (const int *)lut, 1);
        _mm_mask_storeu_epi8(d, k, _mm512_cvtepi32_epi8(g));
    }
}

template<typename T, typename U, typename V>
void lut2_gather(const T *sx, const U *sy, V *d, int w, const V *lut, int bitsx, unsigned mx, unsigned my) {
    __m512i vmx = _mm512_set1_epi32(mx), vmy = _mm512_set1_epi32(my);
    __m128i sh = _mm_cvtsi32_si128(bitsx);
    for (int x = 0; x < w; x += 16) {
        __mmask16 k = (x + 16 <= w) ? (__mmask16)0xFFFF : (__mmask16)((1u << (w - x)) - 1);
        __m512i ix = _mm512_min_epu32(load_widen<T>(sx + x, k), vmx);
        __m512i iy = _mm512_min_epu32(load_widen<U>(sy + x, k), vmy);
        __m512i idx = _mm512_add_epi32(_mm512_sll_epi32(iy, sh), ix);
        gather_store<V>(d + x, k, lut, idx);
    }
}

} // namespace

extern "C" {
void vs_lut2_gather_ww_w_avx512(const uint16_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_ww_b_avx512(const uint16_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_w_avx512(const uint16_t *sx, const uint8_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_b_avx512(const uint16_t *sx, const uint8_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_w_avx512(const uint8_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_b_avx512(const uint8_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
} // extern "C"
