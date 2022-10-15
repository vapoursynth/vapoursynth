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

#include <limits.h>
#include "planestats.h"
#include "VSHelper4.h"

void vs_plane_stats_1_byte_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned x, y;
    unsigned imin = UINT_MAX;
    unsigned imax = 0;
    uint64_t acc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t v = srcp[x];
            imin = VSMIN(imin, v);
            imax = VSMAX(imax, v);
            acc += v;
        }
        srcp += stride;
    }

    stats->i.min = imin;
    stats->i.max = imax;
    stats->i.acc = acc;
}

void vs_plane_stats_1_word_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned x, y;
    unsigned imin = UINT_MAX;
    unsigned imax = 0;
    uint64_t acc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint16_t v = ((const uint16_t *)srcp)[x];
            imin = VSMIN(imin, v);
            imax = VSMAX(imax, v);
            acc += v;
        }
        srcp += stride;
    }

    stats->i.min = imin;
    stats->i.max = imax;
    stats->i.acc = acc;
}

void vs_plane_stats_1_float_c(union vs_plane_stats *stats, const void *src, ptrdiff_t stride, unsigned width, unsigned height)
{
    const uint8_t *srcp = src;
    unsigned x, y;
    float fmin = INFINITY;
    float fmax = -INFINITY;
    double facc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            float v = ((const float *)srcp)[x];
            fmin = VSMIN(fmin, v);
            fmax = VSMAX(fmax, v);
            facc += v;
        }
        srcp += stride;
    }

    stats->f.min = fmin;
    stats->f.max = fmax;
    stats->f.acc = facc;
}

void vs_plane_stats_2_byte_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned x, y;
    unsigned imin = UINT_MAX;
    unsigned imax = 0;
    uint64_t acc = 0;
    uint64_t diffacc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t v = srcp1[x];
            uint8_t t = srcp2[x];
            imin = VSMIN(imin, v);
            imax = VSMAX(imax, v);
            acc += v;
            diffacc += abs(v - t);
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = imin;
    stats->i.max = imax;
    stats->i.acc = acc;
    stats->i.diffacc = diffacc;
}

void vs_plane_stats_2_word_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned x, y;
    unsigned imin = UINT_MAX;
    unsigned imax = 0;
    uint64_t acc = 0;
    uint64_t diffacc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint16_t v = ((const uint16_t *)srcp1)[x];
            uint16_t t = ((const uint16_t *)srcp2)[x];
            imin = VSMIN(imin, v);
            imax = VSMAX(imax, v);
            acc += v;
            diffacc += abs(v - t);
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->i.min = imin;
    stats->i.max = imax;
    stats->i.acc = acc;
    stats->i.diffacc = diffacc;
}

void vs_plane_stats_2_float_c(union vs_plane_stats *stats, const void *src1, ptrdiff_t src1_stride, const void *src2, ptrdiff_t src2_stride, unsigned width, unsigned height)
{
    const uint8_t *srcp1 = src1;
    const uint8_t *srcp2 = src2;
    unsigned x, y;
    float fmin = INFINITY;
    float fmax = -INFINITY;
    double facc = 0;
    double fdiffacc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            float v = ((const float *)srcp1)[x];
            float t = ((const float *)srcp2)[x];
            fmin = VSMIN(fmin, v);
            fmax = VSMAX(fmax, v);
            facc += v;
            fdiffacc += fabsf(v - t);
        }
        srcp1 += src1_stride;
        srcp2 += src2_stride;
    }

    stats->f.min = fmin;
    stats->f.max = fmax;
    stats->f.acc = facc;
    stats->f.diffacc = fdiffacc;
}
