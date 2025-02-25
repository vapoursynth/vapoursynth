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

#ifndef TRANSPOSE_H
#define TRANSPOSE_H

#include <stddef.h>
#include <stdint.h>
#include "VSHelper4.h"

#ifdef __cplusplus
extern "C" {
#endif

void vs_transpose_plane_byte_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_word_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_dword_c(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);

#ifdef VS_TARGET_CPU_X86
void vs_transpose_plane_byte_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_word_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_dword_sse2(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
#elif defined(__ARM_NEON__)
void vs_transpose_plane_byte_neon(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_word_neon(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
void vs_transpose_plane_dword_neon(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height);
#endif

/* Implementation details. */
#ifdef VS_TRANSPOSE_IMPL

#define ADD_OFFSET(p, stride) ((p) + (stride) / (sizeof(*(p))))

#define CACHELINE_SIZE 64
#define CACHELINE_SIZE_BYTE (CACHELINE_SIZE / sizeof(uint8_t))
#define CACHELINE_SIZE_WORD (CACHELINE_SIZE / sizeof(uint16_t))
#define CACHELINE_SIZE_DWORD (CACHELINE_SIZE / sizeof(uint32_t))

static void transpose_block_byte(const uint8_t * VS_RESTRICT src, ptrdiff_t src_stride, uint8_t * VS_RESTRICT dst, ptrdiff_t dst_stride);
static void transpose_block_word(const uint16_t * VS_RESTRICT src, ptrdiff_t src_stride, uint16_t * VS_RESTRICT dst, ptrdiff_t dst_stride);
static void transpose_block_dword(const uint32_t * VS_RESTRICT src, ptrdiff_t src_stride, uint32_t * VS_RESTRICT dst, ptrdiff_t dst_stride);

static void transpose_plane_byte(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    const uint8_t *src_p = src;
    uint8_t *dst_p = dst;

    unsigned width_floor = width - width % BLOCK_WIDTH_BYTE;
    unsigned height_floor = height - height % CACHELINE_SIZE_BYTE;
    unsigned height_floor2 = height - height % BLOCK_HEIGHT_BYTE;
    unsigned i, j, ii;

    for (i = 0; i < height_floor; i += CACHELINE_SIZE_BYTE) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_BYTE) {
            /* Prioritize contiguous stores over contiguous loads. */
            for (ii = i; ii < i + CACHELINE_SIZE_BYTE; ii += BLOCK_HEIGHT_BYTE) {
                transpose_block_byte(ADD_OFFSET(src_p, ii * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + ii, dst_stride);
            }
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + CACHELINE_SIZE_BYTE; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor; i < height_floor2; i += BLOCK_HEIGHT_BYTE) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_BYTE) {
            transpose_block_byte(ADD_OFFSET(src_p, i * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + i, dst_stride);
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + BLOCK_HEIGHT_BYTE; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor2; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            *(ADD_OFFSET(dst_p, j * dst_stride) + i) = *(ADD_OFFSET(src_p, i * src_stride) + j);
        }
    }
}

static void transpose_plane_word(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    const uint16_t *src_p = src;
    uint16_t *dst_p = dst;

    unsigned width_floor = width - width % BLOCK_WIDTH_WORD;
    unsigned height_floor = height - height % CACHELINE_SIZE_WORD;
    unsigned height_floor2 = height - height % BLOCK_HEIGHT_WORD;
    unsigned i, j, ii;

    for (i = 0; i < height_floor; i += CACHELINE_SIZE_WORD) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_WORD) {
            /* Prioritize contiguous stores over contiguous loads. */
            for (ii = i; ii < i + CACHELINE_SIZE_WORD; ii += BLOCK_HEIGHT_WORD) {
                transpose_block_word(ADD_OFFSET(src_p, ii * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + ii, dst_stride);
            }
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + CACHELINE_SIZE_WORD; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor; i < height_floor2; i += BLOCK_HEIGHT_WORD) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_WORD) {
            transpose_block_word(ADD_OFFSET(src_p, i * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + i, dst_stride);
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + BLOCK_HEIGHT_WORD; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor2; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            *(ADD_OFFSET(dst_p, j * dst_stride) + i) = *(ADD_OFFSET(src_p, i * src_stride) + j);
        }
    }
}

static void transpose_plane_dword(const void * VS_RESTRICT src, ptrdiff_t src_stride, void * VS_RESTRICT dst, ptrdiff_t dst_stride, unsigned width, unsigned height)
{
    const uint32_t *src_p = src;
    uint32_t *dst_p = dst;

    unsigned width_floor = width - width % BLOCK_WIDTH_BYTE;
    unsigned height_floor = height - height % CACHELINE_SIZE_BYTE;
    unsigned height_floor2 = height - height % BLOCK_HEIGHT_BYTE;
    unsigned i, j, ii;

    for (i = 0; i < height_floor; i += CACHELINE_SIZE_DWORD) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_DWORD) {
            /* Prioritize contiguous stores over contiguous loads. */
            for (ii = i; ii < i + CACHELINE_SIZE_DWORD; ii += BLOCK_HEIGHT_DWORD) {
                transpose_block_dword(ADD_OFFSET(src_p, ii * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + ii, dst_stride);
            }
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + CACHELINE_SIZE_DWORD; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor; i < height_floor2; i += BLOCK_HEIGHT_DWORD) {
        for (j = 0; j < width_floor; j += BLOCK_WIDTH_DWORD) {
            transpose_block_dword(ADD_OFFSET(src_p, i * src_stride) + j, src_stride, ADD_OFFSET(dst_p, j * dst_stride) + i, dst_stride);
        }
        for (j = width_floor; j < width; ++j) {
            for (ii = i; ii < i + BLOCK_HEIGHT_DWORD; ++ii) {
                *(ADD_OFFSET(dst_p, j * dst_stride) + ii) = *(ADD_OFFSET(src_p, ii * src_stride) + j);
            }
        }
    }
    for (i = height_floor2; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            *(ADD_OFFSET(dst_p, j * dst_stride) + i) = *(ADD_OFFSET(src_p, i * src_stride) + j);
        }
    }
}
#endif /* VS_TRANSPOSE_IMPL */

#ifdef __cplusplus
}
#endif

#endif