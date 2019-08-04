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

#define VS_MERGE_IMPL
#include "merge.h"
#include "VSHelper.h"

#define MERGESHIFT 15
#define ROUND (1U << (MERGESHIFT - 1))

void vs_merge_byte_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned w = weight.u;
    unsigned i;

    for (i = 0; i < n; i++) {
        unsigned v1 = srcp1[i];
        unsigned v2 = srcp2[i];
        dstp[i] = v1 + (((v2 - v1) * w + ROUND) >> MERGESHIFT);
    }
}

void vs_merge_word_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned w = weight.u;
    unsigned i;

    for (i = 0; i < n; i++) {
        unsigned v1 = srcp1[i];
        unsigned v2 = srcp2[i];
        dstp[i] = v1 + (((v2 - v1) * w + ROUND) >> MERGESHIFT);
    }
}

void vs_merge_float_c(const void *src1, const void *src2, void *dst, union vs_merge_weight weight, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    float w = weight.f;
    unsigned i;

    for (i = 0; i < n; i++) {
        float v1 = srcp1[i];
        float v2 = srcp2[i];
        dstp[i] = v1 + (v2 - v1) * w;
    }
}


void vs_mask_merge_byte_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)offset;
    (void)depth;

    for (i = 0; i < n; i++) {
        uint8_t v1 = srcp1[i];
        uint8_t v2 = srcp2[i];
        uint8_t mask = maskp[i];
        uint8_t invmask = UINT8_MAX - mask;
        uint16_t tmp = invmask * v1 + mask * v2 + UINT8_MAX / 2;
        dstp[i] = tmp / 255;
    }
}

void vs_mask_merge_word_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;
    uint32_t div = div_table[depth - 9];
    uint8_t shift = shift_table[depth - 9];

    (void)offset;

    for (i = 0; i < n; i++) {
        uint16_t v1 = srcp1[i];
        uint16_t v2 = srcp2[i];
        uint16_t mask = maskp[i];
        uint16_t invmask = maxval - mask;
        uint32_t tmp = (uint32_t)invmask * v1 + (uint32_t)mask * v2 + maxval / 2;
        dstp[i] = (uint16_t)(((uint64_t)tmp * div) >> (32 + shift));
    }
}

void vs_mask_merge_float_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i++) {
        float v1 = srcp1[i];
        float v2 = srcp2[i];
        dstp[i] = v1 + (v2 - v1) * maskp[i];
    }
}

void vs_mask_merge_premul_byte_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    const uint8_t *maskp = mask;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i++) {
        uint8_t v1 = srcp1[i];
        uint8_t v2 = srcp2[i];
        uint8_t invmask = UINT8_MAX - maskp[i];

        uint16_t tmp = v1 - offset;
        int sign = (int16_t)tmp < 0;
        tmp = sign ? -tmp : tmp;
        tmp = (tmp * invmask + UINT8_MAX / 2) / 255;
        tmp = sign ? -tmp : tmp;

        dstp[i] = tmp + v2;
    }
}

void vs_mask_merge_premul_word_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    const uint16_t *maskp = mask;
    uint16_t *dstp = dst;
    unsigned i;

    uint16_t maxval = (1U << depth) - 1;
    uint32_t div = div_table[depth - 9];
    uint8_t shift = shift_table[depth - 9];

    for (i = 0; i < n; i++) {
        uint16_t v1 = srcp1[i];
        uint16_t v2 = srcp2[i];
        uint16_t invmask = maxval - maskp[i];
#pragma warning(push)
#pragma warning(disable:4146)
        uint32_t tmp = v1 - offset;
        int sign = (int32_t)tmp < 0;
        tmp = sign ? -tmp : tmp;
        tmp = (((uint64_t)tmp * invmask + maxval / 2) * div) >> (32 + shift);
        tmp = sign ? -tmp : tmp;
#pragma warning(pop)
        dstp[i] = tmp + v2;
    }
}

void vs_mask_merge_premul_float_c(const void *src1, const void *src2, const void *mask, void *dst, unsigned depth, unsigned offset, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    const float *maskp = mask;
    float *dstp = dst;
    unsigned i;

    (void)depth;
    (void)offset;

    for (i = 0; i < n; i++) {
        float v1 = srcp1[i];
        float v2 = srcp2[i];
        dstp[i] = (1.0f - maskp[i]) * v1 + v2;
    }
}

void vs_makediff_byte_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i++) {
        uint8_t v1 = srcp1[i];
        uint8_t v2 = srcp2[i];
        dstp[i] = VSMIN(VSMAX((int)v1 - (int)v2 + 128, 0), 255);
    }
}

void vs_makediff_word_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t half = 1U << (depth - 1);
    int32_t maxval = (1U << depth) - 1;

    for (i = 0; i < n; i++) {
        uint16_t v1 = srcp1[i];
        uint16_t v2 = srcp2[i];
        int32_t tmp = (int32_t)v1 - (int32_t)v2 + half;
        dstp[i] = VSMIN(VSMAX(tmp, 0), maxval);
    }
}

void vs_makediff_float_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i++) {
        dstp[i] = srcp1[i] - srcp2[i];
    }
}

void vs_mergediff_byte_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    uint8_t *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i++) {
        uint8_t v1 = srcp1[i];
        uint8_t v2 = srcp2[i];
        dstp[i] = VSMIN(VSMAX((int)v1 + (int)v2 - 128, 0), 255);
    }
}

void vs_mergediff_word_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const uint16_t *srcp1 = src1;
    const uint16_t *srcp2 = src2;
    uint16_t *dstp = dst;
    unsigned i;

    int32_t half = 1U << (depth - 1);
    int32_t maxval = (1U << depth) - 1;

    for (i = 0; i < n; i++) {
        uint16_t v1 = srcp1[i];
        uint16_t v2 = srcp2[i];
        int32_t tmp = (int32_t)v1 + (int32_t)v2 - half;
        dstp[i] = VSMIN(VSMAX(tmp, 0), maxval);
    }
}

void vs_mergediff_float_c(const void *src1, const void *src2, void *dst, unsigned depth, unsigned n)
{
    const float *srcp1 = src1;
    const float *srcp2 = src2;
    float *dstp = dst;
    unsigned i;

    (void)depth;

    for (i = 0; i < n; i++) {
        dstp[i] = srcp1[i] + srcp2[i];
    }
}
