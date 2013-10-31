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

#include "VapourSynth.h"
#include "VSHelper.h"
#include <string.h>

#define RETERROR(x) do { vsapi->setError(out, (x)); return; } while (0)
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

static inline void vs_memset8(void *ptr, int value, size_t num) {
    memset(ptr, value, num);
}

static inline void vs_memset16(void *ptr, int value, size_t num) {
    uint16_t *tptr = (uint16_t *)ptr;
    while (num-- > 0)
        *tptr++ = (uint16_t)value;
}

static inline void vs_memset32(void *ptr, int value, size_t num) {
    int32_t *tptr = (int32_t *)ptr;
    while (num-- > 0)
        *tptr++ = (int32_t)value;
}

static inline void vs_memset_float(void *ptr, float value, size_t num) {
    float *tptr = (float *)ptr;
    while (num-- > 0)
        *tptr++ = value;
}

// to detect compat formats
static inline int isCompatFormat(const VSVideoInfo *vi) {
    return vi->format && vi->format->colorFamily == cmCompat;
}

// to get the width/height of a plane easily when not having a frame around
static inline int planeWidth(const VSVideoInfo *vi, int plane) {
    return vi->width >> (plane ? vi->format->subSamplingW : 0);
}

static inline int planeHeight(const VSVideoInfo *vi, int plane) {
    return vi->height >> (plane ? vi->format->subSamplingH : 0);
}

// get the triplet representing black for any colorspace (works for union with float too since it's always 0)
static inline void setBlack(uint32_t color[3], const VSFormat *format) {
    int i;
    for (i = 0; i < 3; i++)
        color[i] = 0;
    if (format->sampleType == stInteger && (format->colorFamily == cmYUV || format->colorFamily == cmYCoCg))
        color[1] = color[2] = (1 << (format->bitsPerSample - 1));
}

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
} SingleClipData;

static void VS_CC singleClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node), 1, node);
}

static void VS_CC singleClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *)instanceData;
    vsapi->freeNode(d->node);
    free(instanceData);
}

#endif // FILTERSHARED_H
