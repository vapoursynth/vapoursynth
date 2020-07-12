/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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

#ifndef FILTERSHARED_H
#define FILTERSHARED_H

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include <cstring>

// FIXME, merge everything of value into filtersharedcpp.h and remove this header

#define RETERROR(x) do { vsapi->setError(out, (x)); return; } while (0)

// to detect compat formats
static inline bool isCompatFormat(const VSVideoFormat *format) {
    return format->colorFamily == cfCompatBGR32 || format->colorFamily == cfCompatYUY2;
}

// to detect undefined format
static inline bool isUndefinedFormat(const VSVideoFormat *format) {
    return format->colorFamily == cfUndefined;
}

// to get the width/height of a plane easily when not having a frame around
static inline int planeWidth(const VSVideoInfo *vi, int plane) {
    return vi->width >> (plane ? vi->format.subSamplingW : 0);
}

static inline int planeHeight(const VSVideoInfo *vi, int plane) {
    return vi->height >> (plane ? vi->format.subSamplingH : 0);
}

// get the triplet representing black for any colorspace (works for union with float too since it's always 0)
static inline void setBlack(uint32_t color[3], const VSVideoFormat *format) {
    for (int i = 0; i < 3; i++)
        color[i] = 0;
    if (format->sampleType == stInteger && format->colorFamily == cfYUV)
        color[1] = color[2] = (1 << (format->bitsPerSample - 1));
    else if (format->colorFamily == cfCompatYUY2)
        color[1] = color[2] = 128;
}

static inline int64_t floatToInt64S(float f) {
    if (f > INT64_MAX)
        return INT64_MAX;
    else if (f < INT64_MIN)
        return INT64_MIN;
    else
        return (int64_t)llround(f);
}

static inline int floatToIntS(float f) {
    if (f > INT_MAX)
        return INT_MAX;
    else if (f < INT_MIN)
        return INT_MIN;
    else
        return (int)lround(f);
}

#endif // FILTERSHARED_H
