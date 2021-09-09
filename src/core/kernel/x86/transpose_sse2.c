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

#include <stddef.h>
#include <stdint.h>
#include <emmintrin.h>

#define VS_TRANSPOSE_IMPL
#define BLOCK_WIDTH_BYTE 16
#define BLOCK_HEIGHT_BYTE 8
#define BLOCK_WIDTH_WORD 8
#define BLOCK_HEIGHT_WORD 8
#define BLOCK_WIDTH_DWORD 4
#define BLOCK_HEIGHT_DWORD 4
#include "../transpose.h"

static void transpose_block_byte(const uint8_t * VS_RESTRICT src, ptrdiff_t src_stride, uint8_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    __m128i row0 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 0 * src_stride));
    __m128i row1 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 1 * src_stride));
    __m128i row2 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 2 * src_stride));
    __m128i row3 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 3 * src_stride));
    __m128i row4 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 4 * src_stride));
    __m128i row5 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 5 * src_stride));
    __m128i row6 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 6 * src_stride));
    __m128i row7 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 7 * src_stride));

    __m128i t0, t1, t2, t3, t4, t5, t6, t7;
    __m128i tt0, tt1, tt2, tt3, tt4, tt5, tt6, tt7;

    row0 = _mm_shuffle_epi32(row0, _MM_SHUFFLE(3, 1, 2, 0));
    row1 = _mm_shuffle_epi32(row1, _MM_SHUFFLE(3, 1, 2, 0));
    row2 = _mm_shuffle_epi32(row2, _MM_SHUFFLE(3, 1, 2, 0));
    row3 = _mm_shuffle_epi32(row3, _MM_SHUFFLE(3, 1, 2, 0));
    row4 = _mm_shuffle_epi32(row4, _MM_SHUFFLE(3, 1, 2, 0));
    row5 = _mm_shuffle_epi32(row5, _MM_SHUFFLE(3, 1, 2, 0));
    row6 = _mm_shuffle_epi32(row6, _MM_SHUFFLE(3, 1, 2, 0));
    row7 = _mm_shuffle_epi32(row7, _MM_SHUFFLE(3, 1, 2, 0));

    t0 = _mm_unpacklo_epi8(row0, row1);
    t1 = _mm_unpacklo_epi8(row2, row3);
    t2 = _mm_unpacklo_epi8(row4, row5);
    t3 = _mm_unpacklo_epi8(row6, row7);
    t4 = _mm_unpackhi_epi8(row0, row1);
    t5 = _mm_unpackhi_epi8(row2, row3);
    t6 = _mm_unpackhi_epi8(row4, row5);
    t7 = _mm_unpackhi_epi8(row6, row7);

    tt0 = _mm_unpacklo_epi16(t0, t1);
    tt1 = _mm_unpackhi_epi16(t0, t1);
    tt2 = _mm_unpacklo_epi16(t2, t3);
    tt3 = _mm_unpackhi_epi16(t2, t3);
    tt4 = _mm_unpacklo_epi16(t4, t5);
    tt5 = _mm_unpackhi_epi16(t4, t5);
    tt6 = _mm_unpacklo_epi16(t6, t7);
    tt7 = _mm_unpackhi_epi16(t6, t7);

    row0 = _mm_unpacklo_epi32(tt0, tt2);
    row1 = _mm_unpackhi_epi32(tt0, tt2);
    row2 = _mm_unpacklo_epi32(tt1, tt3);
    row3 = _mm_unpackhi_epi32(tt1, tt3);
    row4 = _mm_unpacklo_epi32(tt4, tt6);
    row5 = _mm_unpackhi_epi32(tt4, tt6);
    row6 = _mm_unpacklo_epi32(tt5, tt7);
    row7 = _mm_unpackhi_epi32(tt5, tt7);

    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 0 * dst_stride), row0);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 2 * dst_stride), row1);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 8 * dst_stride), row2);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 10 * dst_stride), row3);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 4 * dst_stride), row4);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 6 * dst_stride), row5);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 12 * dst_stride), row6);
    _mm_storel_epi64((__m128i *)ADD_OFFSET(dst, 14 * dst_stride), row7);

    _mm_storeh_pd((double *)ADD_OFFSET(dst, 1 * dst_stride), _mm_castsi128_pd(row0));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 3 * dst_stride), _mm_castsi128_pd(row1));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 9 * dst_stride), _mm_castsi128_pd(row2));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 11 * dst_stride), _mm_castsi128_pd(row3));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 5 * dst_stride), _mm_castsi128_pd(row4));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 7 * dst_stride), _mm_castsi128_pd(row5));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 13 * dst_stride), _mm_castsi128_pd(row6));
    _mm_storeh_pd((double *)ADD_OFFSET(dst, 15 * dst_stride), _mm_castsi128_pd(row7));
}

static void transpose_block_word(const uint16_t * VS_RESTRICT src, ptrdiff_t src_stride, uint16_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    __m128i row0 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 0 * src_stride));
    __m128i row1 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 1 * src_stride));
    __m128i row2 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 2 * src_stride));
    __m128i row3 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 3 * src_stride));
    __m128i row4 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 4 * src_stride));
    __m128i row5 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 5 * src_stride));
    __m128i row6 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 6 * src_stride));
    __m128i row7 = _mm_load_si128((const __m128i *)ADD_OFFSET(src, 7 * src_stride));

    __m128i t0, t1, t2, t3, t4, t5, t6, t7;
    __m128i tt0, tt1, tt2, tt3, tt4, tt5, tt6, tt7;

    t0 = _mm_unpacklo_epi16(row0, row1);
    t1 = _mm_unpacklo_epi16(row2, row3);
    t2 = _mm_unpacklo_epi16(row4, row5);
    t3 = _mm_unpacklo_epi16(row6, row7);
    t4 = _mm_unpackhi_epi16(row0, row1);
    t5 = _mm_unpackhi_epi16(row2, row3);
    t6 = _mm_unpackhi_epi16(row4, row5);
    t7 = _mm_unpackhi_epi16(row6, row7);

    tt0 = _mm_unpacklo_epi32(t0, t1);
    tt1 = _mm_unpackhi_epi32(t0, t1);
    tt2 = _mm_unpacklo_epi32(t2, t3);
    tt3 = _mm_unpackhi_epi32(t2, t3);
    tt4 = _mm_unpacklo_epi32(t4, t5);
    tt5 = _mm_unpackhi_epi32(t4, t5);
    tt6 = _mm_unpacklo_epi32(t6, t7);
    tt7 = _mm_unpackhi_epi32(t6, t7);

    row0 = _mm_unpacklo_epi64(tt0, tt2);
    row1 = _mm_unpackhi_epi64(tt0, tt2);
    row2 = _mm_unpacklo_epi64(tt1, tt3);
    row3 = _mm_unpackhi_epi64(tt1, tt3);
    row4 = _mm_unpacklo_epi64(tt4, tt6);
    row5 = _mm_unpackhi_epi64(tt4, tt6);
    row6 = _mm_unpacklo_epi64(tt5, tt7);
    row7 = _mm_unpackhi_epi64(tt5, tt7);

    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 0 * dst_stride), row0);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 1 * dst_stride), row1);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 2 * dst_stride), row2);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 3 * dst_stride), row3);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 4 * dst_stride), row4);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 5 * dst_stride), row5);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 6 * dst_stride), row6);
    _mm_store_si128((__m128i *)ADD_OFFSET(dst, 7 * dst_stride), row7);
}

static void transpose_block_dword(const uint32_t * VS_RESTRICT src, ptrdiff_t src_stride, uint32_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    __m128 row0 = _mm_load_ps((const float *)ADD_OFFSET(src, 0 * src_stride));
    __m128 row1 = _mm_load_ps((const float *)ADD_OFFSET(src, 1 * src_stride));
    __m128 row2 = _mm_load_ps((const float *)ADD_OFFSET(src, 2 * src_stride));
    __m128 row3 = _mm_load_ps((const float *)ADD_OFFSET(src, 3 * src_stride));

    _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

    _mm_store_ps((float *)ADD_OFFSET(dst, 0 * dst_stride), row0);
    _mm_store_ps((float *)ADD_OFFSET(dst, 1 * dst_stride), row1);
    _mm_store_ps((float *)ADD_OFFSET(dst, 2 * dst_stride), row2);
    _mm_store_ps((float *)ADD_OFFSET(dst, 3 * dst_stride), row3);
}

void vs_transpose_plane_byte_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_byte(src, src_stride, dst, dst_stride, width, height);
}

void vs_transpose_plane_word_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_word(src, src_stride, dst, dst_stride, width, height);
}

void vs_transpose_plane_dword_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_dword(src, src_stride, dst, dst_stride, width, height);
}
