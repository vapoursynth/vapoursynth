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

#define VS_TRANSPOSE_IMPL
#define BLOCK_WIDTH_BYTE 1
#define BLOCK_HEIGHT_BYTE 1
#define BLOCK_WIDTH_WORD 1
#define BLOCK_HEIGHT_WORD 1
#define BLOCK_WIDTH_DWORD 1
#define BLOCK_HEIGHT_DWORD 1
#include "transpose.h"

static void transpose_block_byte(const uint8_t * VS_RESTRICT src, ptrdiff_t src_stride, uint8_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    *dst = *src;
}

static void transpose_block_word(const uint16_t * VS_RESTRICT src, ptrdiff_t src_stride, uint16_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    *dst = *src;
}

static void transpose_block_dword(const uint32_t * VS_RESTRICT src, ptrdiff_t src_stride, uint32_t * VS_RESTRICT dst, ptrdiff_t dst_stride)
{
    *dst = *src;
}

void vs_transpose_plane_byte_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_byte(src, src_stride, dst, dst_stride, width, height);
}

void vs_transpose_plane_word_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_word(src, src_stride, dst, dst_stride, width, height);
}

void vs_transpose_plane_dword_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    transpose_plane_dword(src, src_stride, dst, dst_stride, width, height);
}
