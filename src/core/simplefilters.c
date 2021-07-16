/*
* Copyright (c) 2012-2016 Fredrik Mellbin
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

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "VSHelper.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "kernel/cpulevel.h"
#include "kernel/planestats.h"
#include "kernel/transpose.h"

static inline uint32_t doubleToUInt32S(double v) {
    if (v < 0)
        return 0;
    if (v > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)(v + 0.5);
}

static inline uint32_t bit_cast_uint32(float v) {
    uint32_t ret;
    memcpy(&ret, &v, sizeof(ret));
    return ret;
}

static inline float bit_cast_float(uint32_t v) {
    float ret;
    memcpy(&ret, &v, sizeof(ret));
    return ret;
}

static inline uint16_t floatToHalf(float x) {
    float magic = bit_cast_float((uint32_t)15 << 23);
    uint32_t inf = 255UL << 23;
    uint32_t f16inf = 31UL << 23;
    uint32_t sign_mask = 0x80000000UL;
    uint32_t round_mask = ~0x0FFFU;
    uint16_t ret;
    uint32_t f = bit_cast_uint32(x);
    uint32_t sign = f & sign_mask;
    f ^= sign;

    if (f >= inf) {
        ret = f > inf ? 0x7E00 : 0x7C00;
    } else {
        f &= round_mask;
        f = bit_cast_uint32(bit_cast_float(f)* magic);
        f -= round_mask;

        if (f > f16inf)
            f = f16inf;

        ret = (uint16_t)(f >> 13);
    }

    ret |= (uint16_t)(sign >> 16);
    return ret;
}

static inline int isInfHalf(uint16_t v) {
    return (v & 0x7C00) == 0x7C00;
}

static inline uint32_t doubleToIntPixelValue(double v, int bits, int *err) {
    *err = 0;

    if (!isfinite(v) || v < 0) {
        *err = 1;
        return 0;
    }

    uint32_t i = doubleToUInt32S(v);
    if (i >= ((uint64_t)1 << bits)) {
        *err = 1;
        return 0;
    }

    return i;
}

static inline uint32_t doubleToFloatPixelValue(double v, int *err) {
    *err = 0;

    float f = (float)v;
    if (!isfinite(f)) {
        *err = 1;
        return 0;
    }

    return bit_cast_uint32(f);
}

static inline uint16_t doubleToHalfPixelValue(double v, int *err) {
    *err = 0;

    float f = (float)v;
    if (!isfinite(f)) {
        *err = 1;
        return 0;
    }

    uint16_t f16 = floatToHalf(f);
    if (isInfHalf(f16)) {
        *err = 1;
        return 0;
    }

    return f16;
}

//////////////////////////////////////////
// Crop

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int x;
    int y;
    int width;
    int height;
} CropData;

static void VS_CC cropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    CropData *d = (CropData *) * instanceData;
    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;
    vsapi->setVideoInfo(&vi, 1, node);
}

static int cropVerify(int x, int y, int width, int height, int srcwidth, int srcheight, const VSFormat *fi, char *msg, size_t len) {
    msg[0] = 0;

    if (y < 0 || x < 0)
        snprintf(msg, len, "Crop: negative corner coordinates not allowed");

    if (width <= 0 || height <= 0)
        snprintf(msg, len, "Crop: negative/zero cropping dimensions not allowed");

    if (srcheight > 0 && srcwidth > 0)
        if (srcheight < height + y || srcwidth < width + x)
            snprintf(msg, len, "Crop: cropped area extends beyond frame dimensions");

    if (fi) {
        if (width % (1 << fi->subSamplingW))
            snprintf(msg, len, "Crop: cropped area needs to have mod %d width", 1 << fi->subSamplingW);

        if (height % (1 << fi->subSamplingH))
            snprintf(msg, len, "Crop: cropped area needs to have mod %d height", 1 << fi->subSamplingH);

        if (x % (1 << fi->subSamplingW))
            snprintf(msg, len, "Crop: cropped area needs to have mod %d width offset", 1 << fi->subSamplingW);

        if (y % (1 << fi->subSamplingH))
            snprintf(msg, len, "Crop: cropped area needs to have mod %d height offset", 1 << fi->subSamplingH);
    }

    return !!msg[0];
}

static const VSFrameRef *VS_CC cropGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CropData *d = (CropData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        char msg[150];
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);
        int y = (fi->id == pfCompatBGR32) ? (height - d->height - d->y) : d->y;

        if (cropVerify(d->x, y, d->width, d->height, width, height, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return NULL;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(fi, d->width, d->height, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            int srcstride = vsapi->getStride(src, plane);
            int dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            srcdata += srcstride * (y >> (plane ? fi->subSamplingH : 0));
            srcdata += (d->x >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            vs_bitblt(dstdata, dststride, srcdata, srcstride, (d->width >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample, vsapi->getFrameHeight(dst, plane));
        }

        vsapi->freeFrame(src);

        if (d->y & 1) {
            VSMap *props = vsapi->getFramePropsRW(dst);
            int error;
            int64_t fb = vsapi->propGetInt(props, "_FieldBased", 0, &error);
            if (fb == 1 || fb == 2)
                vsapi->propSetInt(props, "_FieldBased", (fb == 1) ? 2 : 1, paReplace);
        }

        return dst;
    }

    return 0;
}

static void VS_CC cropAbsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    CropData d;
    CropData *data;
    int err;

    d.x = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d.x = int64ToIntS(vsapi->propGetInt(in, "x", 0, &err));
    d.y = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d.y = int64ToIntS(vsapi->propGetInt(in, "y", 0, &err));

    d.height = int64ToIntS(vsapi->propGetInt(in, "height", 0, 0));
    d.width = int64ToIntS(vsapi->propGetInt(in, "width", 0, 0));
    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    d.vi = vsapi->getVideoInfo(d.node);

    if (cropVerify(d.x, d.y, d.width, d.height, d.vi->width, d.vi->height, d.vi->format, msg, sizeof(msg))) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Crop", cropInit, cropGetframe, singleClipFree, fmParallel, 0, data, core);
}

static void VS_CC cropRelCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    CropData d;
    CropData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("Crop: constant format and dimensions needed");
    }

    d.x = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d.y = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));

    d.height = d.vi->height - d.y - int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    d.width = d.vi->width - d.x - int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));

    // passthrough for the no cropping case
    if (d.x == 0 && d.y == 0 && d.width == d.vi->width && d.height == d.vi->height) {
        vsapi->propSetNode(out, "clip", d.node, paReplace);
        vsapi->freeNode(d.node);
        return;
    }

    if (cropVerify(d.x, d.y, d.width, d.height, d.vi->width, d.vi->height, d.vi->format, msg, sizeof(msg))) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Crop", cropInit, cropGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// AddBorders

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int left;
    int right;
    int top;
    int bottom;
    uint32_t color[3];
} AddBordersData;

static void VS_CC addBordersInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = (AddBordersData *) * instanceData;
    VSVideoInfo vi = *d->vi;
    vi.height += vi.height ? d->top + d->bottom : 0;
    vi.width += vi.width ? d->left + d->right : 0;
    vsapi->setVideoInfo(&vi, 1, node);
}

static int addBordersVerify(int left, int right, int top, int bottom, const VSFormat *fi, char *msg, size_t len) {
    msg[0] = 0;

    if (fi) {
        if (left % (1 << fi->subSamplingW))
            snprintf(msg, len, "AddBorders: added area needs to have mod %d width", 1 << fi->subSamplingW);

        if (right % (1 << fi->subSamplingW))
            snprintf(msg, len, "AddBorders: added area needs to have mod %d width", 1 << fi->subSamplingW);

        if (top % (1 << fi->subSamplingH))
            snprintf(msg, len, "AddBorders: added area needs to have mod %d height", 1 << fi->subSamplingH);

        if (bottom % (1 << fi->subSamplingH))
            snprintf(msg, len, "AddBorders: added area needs to have mod %d height", 1 << fi->subSamplingH);
    }

    return !!msg[0];
}

static const VSFrameRef *VS_CC addBordersGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = (AddBordersData *) * instanceData;
    char msg[150];

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst;

        if (addBordersVerify(d->left, d->right, d->top, d->bottom, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return 0;
        }

        dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0) + d->left + d->right, vsapi->getFrameHeight(src, 0) + d->top + d->bottom, src, core);

        // now that argument validation is over we can spend the next few lines actually adding borders
        for (int plane = 0; plane < fi->numPlanes; plane++) {
            int rowsize = vsapi->getFrameWidth(src, plane) * fi->bytesPerSample;
            int srcstride = vsapi->getStride(src, plane);
            int dststride = vsapi->getStride(dst, plane);
            int srcheight = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            int padt = d->top >> (plane ? fi->subSamplingH : 0);
            int padb = d->bottom >> (plane ? fi->subSamplingH : 0);
            int padl = (d->left >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            int padr = (d->right >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            uint32_t color = d->color[plane];

            switch (d->vi->format->bytesPerSample) {
            case 1:
                vs_memset8(dstdata, color, padt * dststride);
                break;
            case 2:
                vs_memset16(dstdata, color, padt * dststride / 2);
                break;
            case 4:
                vs_memset32(dstdata, color, padt * dststride / 4);
                break;
            }
            dstdata += padt * dststride;

            for (int hloop = 0; hloop < srcheight; hloop++) {
                switch (d->vi->format->bytesPerSample) {
                case 1:
                    vs_memset8(dstdata, color, padl);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset8(dstdata + padl + rowsize, color, padr);
                    break;
                case 2:
                    vs_memset16(dstdata, color, padl / 2);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset16(dstdata + padl + rowsize, color, padr / 2);
                    break;
                case 4:
                    vs_memset32(dstdata, color, padl / 4);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset32(dstdata + padl + rowsize, color, padr / 4);
                    break;
                }

                dstdata += dststride;
                srcdata += srcstride;
            }

            switch (d->vi->format->bytesPerSample) {
            case 1:
                vs_memset8(dstdata, color, padb * dststride);
                break;
            case 2:
                vs_memset16(dstdata, color, padb * dststride / 2);
                break;
            case 4:
                vs_memset32(dstdata, color, padb * dststride / 4);
                break;
            }
        }

        vsapi->freeFrame(src);

        if (d->top & 1) {
            VSMap *props = vsapi->getFramePropsRW(dst);
            int error;
            int64_t fb = vsapi->propGetInt(props, "_FieldBased", 0, &error);
            if (fb == 1 || fb == 2)
                vsapi->propSetInt(props, "_FieldBased", (fb == 1) ? 2 : 1, paReplace);
        }

        return dst;
    }

    return 0;
}

static void VS_CC addBordersCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    AddBordersData d;
    AddBordersData *data;
    int err;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    // pass through if nothing to be done
    if (d.left == 0 && d.right == 0 && d.top == 0 && d.bottom == 0) {
        vsapi->propSetNode(out, "clip", d.node, paReplace);
        vsapi->freeNode(d.node);
        return;
    }

    if (d.left < 0 || d.right < 0 || d.top < 0 || d.bottom < 0) {
        vsapi->freeNode(d.node);
        RETERROR("AddBorders: border size to add must not be negative");
    }

    d.vi = vsapi->getVideoInfo(d.node);

    if (isCompatFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("AddBorders: compat formats not supported");
    }

    if (!d.vi->format) {
        vsapi->freeNode(d.node);
        RETERROR("AddBorders: input needs to be constant format");
    }

    if (addBordersVerify(d.left, d.right, d.top, d.bottom, d.vi->format, msg, sizeof(msg))) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    int numcomponents = (d.vi->format->colorFamily == cmCompat) ? 3 : d.vi->format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");

    setBlack(d.color, d.vi->format);

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->propGetFloat(in, "color", i, 0);
            if (d.vi->format->sampleType == stInteger) {
                d.color[i] = doubleToIntPixelValue(color, d.vi->format->bitsPerSample, &err);
            } else {
                if (d.vi->format->bitsPerSample == 16)
                    d.color[i] = doubleToHalfPixelValue(color, &err);
                else
                    d.color[i] = doubleToFloatPixelValue(color, &err);
            }
            if (err)
                RETERROR("AddBorders: color value out of range");
        }
    } else if (ncolors > 0) {
        RETERROR("AddBorders: invalid number of color values specified");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "AddBorders", addBordersInit, addBordersGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// ShufflePlanes

typedef struct {
    VSNodeRef *node[3];
    VSVideoInfo vi;
    int plane[3];
    int format;
} ShufflePlanesData;

static void VS_CC shufflePlanesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC shufflePlanesGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node[0], frameCtx);

        if (d->node[1] && d->node[1] != d->node[0])
            vsapi->requestFrameFilter(n, d->node[1], frameCtx);

        if (d->node[2] && d->node[2] != d->node[0] && d->node[2] != d->node[1])
            vsapi->requestFrameFilter(n, d->node[2], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        if (d->vi.format->colorFamily != cmGray) {
            const VSFrameRef *src[3];
            VSFrameRef *dst;

            for (int i = 0; i < 3; i++)
                src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);

            dst = vsapi->newVideoFrame2(d->vi.format, d->vi.width, d->vi.height, src, d->plane, src[0], core);

            for (int i = 0; i < 3; i++)
                vsapi->freeFrame(src[i]);

            return dst;
        } else {
            VSFrameRef *dst;
            const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
            const VSFormat *fi = vsapi->getFrameFormat(src);

            if (d->plane[0] >= fi->numPlanes) {
                vsapi->freeFrame(src);
                vsapi->setFilterError("ShufflePlanes: invalid plane specified", frameCtx);
                return 0;
            }

            dst = vsapi->newVideoFrame2(d->vi.format, vsapi->getFrameWidth(src, d->plane[0]), vsapi->getFrameHeight(src, d->plane[0]), &src, d->plane, src, core);

            vsapi->freeFrame(src);
            return dst;
        }
    }

    return 0;
}

static void VS_CC shufflePlanesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *)instanceData;
    for (int i = 0; i < 3; i++)
        vsapi->freeNode(d->node[i]);
    free(d);
}

#define SHUFFLEERROR(x) do { for (int j = 0; j < 3; j++) vsapi->freeNode(d.node[j]); RETERROR(x); } while (0)

static int findSubSampling(int s1, int s2) {
    for (int i = 0; i < 6; i++)
        if (s1 - (s2 << i) == 0)
            return i;
    return -1;
}

static void VS_CC shufflePlanesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData d;
    ShufflePlanesData *data;
    int nclips = vsapi->propNumElements(in, "clips");
    int nplanes = vsapi->propNumElements(in, "planes");
    int err;

    for (int i = 0; i < 3; i++) {
        d.plane[i] = 0;
        d.node[i] = 0;
    }

    d.format = int64ToIntS(vsapi->propGetInt(in, "colorfamily", 0, 0));

    if (d.format != cmRGB && d.format != cmYUV && d.format != cmYCoCg && d.format != cmGray)
        RETERROR("ShufflePlanes: invalid output colorfamily");

    int outplanes = (d.format == cmGray ? 1 : 3);

    // please don't make this assumption if you ever write a plugin, it's only accepted in the core where all existing color families may be known
    if (nclips > outplanes)
        RETERROR("ShufflePlanes: 1-3 clips need to be specified");

    if (nplanes > outplanes)
        RETERROR("ShufflePlanes: too many planes specified");

    for (int i = 0; i < nplanes; i++)
        d.plane[i] = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

    for (int i = 0; i < 3; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, &err);

    for (int i = 0; i < 3; i++) {
        if (d.node[i] && isCompatFormat(vsapi->getVideoInfo(d.node[i])))
            SHUFFLEERROR("ShufflePlanes: compat formats not supported");
        if (d.node[i] && !isConstantFormat(vsapi->getVideoInfo(d.node[i])))
            SHUFFLEERROR("ShufflePlanes: only clips with constant format and dimensions supported");
    }

    if (d.format != cmGray && nclips == 1) {
        d.node[1] = vsapi->cloneNodeRef(d.node[0]);
        d.node[2] = vsapi->cloneNodeRef(d.node[0]);
    } else if (d.format != cmGray && nclips == 2) {
        d.node[2] = vsapi->cloneNodeRef(d.node[1]);
    }

    for (int i = 0; i < outplanes; i++) {
        if (d.plane[i] < 0 || (vsapi->getVideoInfo(d.node[i])->format && d.plane[i] >= vsapi->getVideoInfo(d.node[i])->format->numPlanes))
            SHUFFLEERROR("ShufflePlanes: invalid plane specified");
    }

    d.vi = *vsapi->getVideoInfo(d.node[0]);

    // compatible format checks
    if (d.format == cmGray) {
        // gray is always compatible and special, it can work with variable input size clips
        if (d.vi.format)
            d.vi.format = vsapi->registerFormat(cmGray, d.vi.format->sampleType, d.vi.format->bitsPerSample, 0, 0, core);
        d.vi.width = planeWidth(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        d.vi.height = planeHeight(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
    } else {
        // no variable size video with more than one plane, it's just crazy
        int c0height = planeHeight(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        int c0width = planeWidth(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        int c1height = planeHeight(vsapi->getVideoInfo(d.node[1]), d.plane[1]);
        int c1width = planeWidth(vsapi->getVideoInfo(d.node[1]), d.plane[1]);
        int c2height = planeHeight(vsapi->getVideoInfo(d.node[2]), d.plane[2]);
        int c2width = planeWidth(vsapi->getVideoInfo(d.node[2]), d.plane[2]);

        d.vi.width = c0width;
        d.vi.height = c0height;

        if (c1width != c2width || c1height != c2height)
            SHUFFLEERROR("ShufflePlanes: plane 1 and 2 do not have the same size");

        int ssH = findSubSampling(c0height, c1height);
        int ssW = findSubSampling(c0width, c1width);

        if (ssH < 0 || ssW < 0)
            SHUFFLEERROR("ShufflePlanes: Plane 1 and 2 are not subsampled multiples of first plane");

        for (int i = 1; i < 3; i++) {
            const VSVideoInfo *pvi = vsapi->getVideoInfo(d.node[i]);

            if (d.vi.numFrames < pvi->numFrames)
                d.vi.numFrames = pvi->numFrames;

            // simple binary compatibility
            if (d.vi.format->bitsPerSample != pvi->format->bitsPerSample ||
                d.vi.format->sampleType != pvi->format->sampleType)
                SHUFFLEERROR("ShufflePlanes: plane 1 and 2 do not have binary compatible storage");
        }

        if (d.format == cmRGB && (ssH != 0 || ssW != 0))
            SHUFFLEERROR("ShufflePlanes: subsampled RGB not allowed");

        d.vi.format = vsapi->registerFormat(d.format, d.vi.format->sampleType, d.vi.format->bitsPerSample, ssW, ssH, core);
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ShufflePlanes", shufflePlanesInit, shufflePlanesGetframe, shufflePlanesFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// SeparateFields

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
    int modifyDuration;
} SeparateFieldsData;

static void VS_CC separateFieldsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC separateFieldsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / 2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n / 2, d->node, frameCtx);
        const VSMap *props = vsapi->getFramePropsRO(src);
        int err = 0;
        int fieldBased = int64ToIntS(vsapi->propGetInt(props, "_FieldBased", 0, &err));
        int effectiveTFF = d->tff;
        if (fieldBased == 1)
            effectiveTFF = 0;
        else if (fieldBased == 2)
            effectiveTFF = 1;
        if (effectiveTFF == -1) {
            vsapi->setFilterError("SeparateFields: no field order provided", frameCtx);
            vsapi->freeFrame(src);
            return NULL;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        const VSFormat *fi = vsapi->getFrameFormat(dst);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);

            if (!((n & 1) ^ effectiveTFF))
                srcp += src_stride;
            src_stride *= 2;

            vs_bitblt(dstp, dst_stride, srcp, src_stride, vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample, vsapi->getFrameHeight(dst, plane));
        }

        vsapi->freeFrame(src);

        VSMap *dst_props = vsapi->getFramePropsRW(dst);
        vsapi->propSetInt(dst_props, "_Field", ((n & 1) ^ effectiveTFF), paReplace);
        vsapi->propDeleteKey(dst_props, "_FieldBased");

        if (d->modifyDuration) {
            int errNum, errDen;
            int64_t durationNum = vsapi->propGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->propGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                muldivRational(&durationNum, &durationDen, 1, 2); // Divide duration by 2
                vsapi->propSetInt(dst_props, "_DurationNum", durationNum, paReplace);
                vsapi->propSetInt(dst_props, "_DurationDen", durationDen, paReplace);
            }
        }

        return dst;
    }

    return 0;
}

static void VS_CC separateFieldsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData d;
    SeparateFieldsData *data;

    int err;
    d.tff = !!vsapi->propGetInt(in, "tff", 0, &err);
    if (err)
        d.tff = -1;
    d.modifyDuration = !!vsapi->propGetInt(in, "modify_duration", 0, &err);
    if (err)
        d.modifyDuration = 1;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("SeparateFields: clip must have constant format and dimensions");
    }

    if (d.vi.height % (1 << (d.vi.format->subSamplingH + 1))) {
        vsapi->freeNode(d.node);
        RETERROR("SeparateFields: clip height must be mod 2 in the smallest subsampled plane");
    }

    if (d.vi.numFrames > INT_MAX / 2) {
        vsapi->freeNode(d.node);
        RETERROR("SeparateFields: resulting clip is too long");
    }
    d.vi.numFrames *= 2;
    d.vi.height /= 2;

    if (d.modifyDuration)
        muldivRational(&d.vi.fpsNum, &d.vi.fpsDen, 2, 1);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "SeparateFields", separateFieldsInit, separateFieldsGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// DoubleWeave

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
} DoubleWeaveData;

static void VS_CC doubleWeaveInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = (DoubleWeaveData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC doubleWeaveGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = (DoubleWeaveData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n + 1, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n + 1, d->node, frameCtx);

        int err;
        int64_t src1_field = vsapi->propGetInt(vsapi->getFramePropsRO(src1), "_Field", 0, &err);
        if (err)
            src1_field = -1;
        int64_t src2_field = vsapi->propGetInt(vsapi->getFramePropsRO(src2), "_Field", 0, &err);
        if (err)
            src2_field = -1;

        const VSFrameRef *srctop = NULL;
        const VSFrameRef *srcbtn = NULL;

        if (src1_field == 0 && src2_field == 1) {
            srcbtn = src1;
            srctop = src2;
        } else if (src1_field == 1 && src2_field == 0) {
            srctop = src1;
            srcbtn = src2;
        } else if (d->tff != -1) {
            int par = (n & 1) ^ d->tff;
            if (par) {
                srctop = src1;
                srcbtn = src2;
            } else {
                srctop = src2;
                srcbtn = src1;
            }
        } else {
            vsapi->setFilterError("DoubleWeave: field order could not be determined from frame properties", frameCtx);
            vsapi->freeFrame(src1);
            vsapi->freeFrame(src2);
            return NULL;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src1, core);
        const VSFormat *fi = vsapi->getFrameFormat(dst);
        VSMap *dstprops = vsapi->getFramePropsRW(dst);
        vsapi->propDeleteKey(dstprops, "_Field");
        vsapi->propSetInt(dstprops, "_FieldBased", 1 + (srctop == src1), paReplace);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcptop = vsapi->getReadPtr(srctop, plane);
            const uint8_t *srcpbtn = vsapi->getReadPtr(srcbtn, plane);
            int src_stride = vsapi->getStride(srcbtn, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(srctop, plane);
            size_t row_size = vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample;

            for (int hl = 0; hl < h; hl++) {
                memcpy(dstp, srcptop, row_size);
                dstp += dst_stride;
                memcpy(dstp, srcpbtn, row_size);
                srcpbtn += src_stride;
                srcptop += src_stride;
                dstp += dst_stride;
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return 0;
}

static void VS_CC doubleWeaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData d;
    DoubleWeaveData *data;

    int err;
    d.tff = !!vsapi->propGetInt(in, "tff", 0, &err);
    if (err)
        d.tff = -1;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    d.vi.height *= 2;

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("DoubleWeave: clip must have constant format and dimensions");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "DoubleWeave", doubleWeaveInit, doubleWeaveGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// FlipVertical

static const VSFrameRef *VS_CC flipVerticalGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int height = vsapi->getFrameHeight(src, plane);
            dstp += dst_stride * (height - 1);
            vs_bitblt(dstp, -dst_stride, srcp, src_stride, vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample, height);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC flipVerticalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData d;
    SingleClipData *data;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "FlipVertical", singleClipInit, flipVerticalGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// FlipHorizontal

typedef struct {
    VSNodeRef *node;
    int flip;
} FlipHorizontalData;

static const VSFrameRef *VS_CC flipHorizontalGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    // optimize, pshufb, pshufw, palignr could make flipping a lot faster
    FlipHorizontalData *d = (FlipHorizontalData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t * VS_RESTRICT srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);
            int hl;
            int w = vsapi->getFrameWidth(src, plane) - 1;
            int x;

            if (d->flip) {
                dstp += dst_stride * (h - 1);
                dst_stride = -dst_stride;
            }

            switch (fi->bytesPerSample) {
            case 1:
                for (hl = 0; hl < h; hl++) {
                    for (x = 0; x <= w; x++)
                        dstp[w - x] = srcp[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            case 2:
                for (hl = 0; hl < h; hl++) {
                    const int16_t * VS_RESTRICT srcp16 = (const int16_t *)srcp;
                    int16_t * VS_RESTRICT dstp16 = (int16_t *)dstp;

                    for (x = 0; x <= w; x++)
                        dstp16[w - x] = srcp16[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            case 4:
                for (hl = 0; hl < h; hl++) {
                    const int32_t * VS_RESTRICT srcp32 = (const int32_t *)srcp;
                    int32_t * VS_RESTRICT dstp32 = (int32_t *)dstp;

                    for (x = 0; x <= w; x++)
                        dstp32[w - x] = srcp32[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            default:
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                vsapi->setFilterError("FlipHorizontal: Unsupported sample size", frameCtx);
                return 0;
            }

        }

        vsapi->freeFrame(src);
        return dst;

    }

    return 0;
}

static void VS_CC flipHorizontalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FlipHorizontalData d;
    FlipHorizontalData *data;

    d.flip = int64ToIntS((intptr_t)userData);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, d.flip ? "Turn180" : "FlipHorizontal", singleClipInit, flipHorizontalGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Stack

typedef struct {
    VSNodeRef **node;
    VSVideoInfo vi;
    int numclips;
    int vertical;
} StackData;

static void VS_CC stackInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC stackGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *) * instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < d->numclips; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        vsapi->freeFrame(src);

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);

            for (int i = 0; i < d->numclips; i++) {
                src = vsapi->getFrameFilter(n, d->node[i], frameCtx);

                if (d->vertical) {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int size = dst_stride * vsapi->getFrameHeight(src, plane);
                    memcpy(dstp, srcp, size);
                    dstp += size;
                } else {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int src_stride = vsapi->getStride(src, plane);
                    size_t rowsize = vsapi->getFrameWidth(src, plane) * d->vi.format->bytesPerSample;
                    vs_bitblt(dstp, dst_stride,
                        srcp, src_stride,
                        rowsize,
                        vsapi->getFrameHeight(src, plane));
                    dstp += rowsize;
                }

                vsapi->freeFrame(src);
            }
        }

        return dst;
    }

    return 0;
}

static void VS_CC stackFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *)instanceData;
    for (int i = 0; i < d->numclips; i++)
        vsapi->freeNode(d->node[i]);

    free(d->node);
    free(d);
}

static void VS_CC stackCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    StackData d;
    StackData *data;

    d.vertical = int64ToIntS((intptr_t)userData);
    d.numclips = vsapi->propNumElements(in, "clips");

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        VSNodeRef *node = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", node, paReplace);
        vsapi->freeNode(node);
    } else {
        d.node = malloc(sizeof(d.node[0]) * d.numclips);

        for (int i = 0; i < d.numclips; i++)
            d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

        d.vi = *vsapi->getVideoInfo(d.node[0]);
        if (isConstantFormat(&d.vi) && isCompatFormat(&d.vi) && d.vertical) {
            for (int j = 0; j < d.numclips; j++)
                vsapi->freeNode(d.node[j]);
            free(d.node);

            RETERROR("StackVertical: compat formats aren't supported");
        }

        for (int i = 1; i < d.numclips; i++) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(d.node[i]);

            if (d.vi.numFrames < vi->numFrames)
                d.vi.numFrames = vi->numFrames;

            if (!isConstantFormat(vi) || vi->format != d.vi.format || (d.vertical && vi->width != d.vi.width) || (!d.vertical && vi->height != d.vi.height)) {
                for (int j = 0; j < d.numclips; j++)
                    vsapi->freeNode(d.node[j]);
                free(d.node);

                if (d.vertical) {
                    RETERROR("StackVertical: clip format and width must match");
                } else {
                    RETERROR("StackHorizontal: clip format and height must match");
                }
            }

            if (d.vertical)
                d.vi.height += vi->height;
            else
                d.vi.width += vi->width;
        }

        data = malloc(sizeof(d));
        *data = d;

        vsapi->createFilter(in, out, d.vertical ? "StackVertical" : "StackHorizontal", stackInit, stackGetframe, stackFree, fmParallel, 0, data, core);
    }
}

//////////////////////////////////////////
// BlankClip

typedef struct {
    VSFrameRef *f;
    VSVideoInfo vi;
    int keep;
    uint32_t color[3];
} BlankClipData;

static void VS_CC blankClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC blankClipGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *) * instanceData;

    if (activationReason == arInitial) {
        VSFrameRef *frame = NULL;
        if (!d->f) {
            frame = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, 0, core);
            int bytesPerSample = (d->vi.format->id == pfCompatYUY2) ? 4 : d->vi.format->bytesPerSample;

            for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
                switch (bytesPerSample) {
                case 1:
                    vs_memset8(vsapi->getWritePtr(frame, plane), d->color[plane], vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane));
                    break;
                case 2:
                    vs_memset16(vsapi->getWritePtr(frame, plane), d->color[plane], (vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane)) / 2);
                    break;
                case 4:
                    vs_memset32(vsapi->getWritePtr(frame, plane), d->color[plane], (vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane)) / 4);
                    break;
                }
            }

            if (d->vi.fpsNum > 0) {
                VSMap *frameProps = vsapi->getFramePropsRW(frame);
                vsapi->propSetInt(frameProps, "_DurationNum", d->vi.fpsDen, paReplace);
                vsapi->propSetInt(frameProps, "_DurationDen", d->vi.fpsNum, paReplace);
            }
        }

        if (d->keep) {
            if (frame)
                d->f = frame;
            return vsapi->cloneFrameRef(d->f);
        } else {
            return frame;
        }
    }

    return 0;
}

static void VS_CC blankClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *)instanceData;
    vsapi->freeFrame(d->f);
    free(d);
}

static void VS_CC blankClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    BlankClipData d = {0};
    BlankClipData *data;
    int hasvi = 0;
    int64_t temp;
    int err;

    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, &err);

    if (!err) {
        d.vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
        hasvi = 1;
    }

    temp = vsapi->propGetInt(in, "width", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.width = 640;
    } else {
        d.vi.width = int64ToIntS(temp);
    }

    temp = vsapi->propGetInt(in, "height", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.height = 480;
    } else {
        d.vi.height = int64ToIntS(temp);
    }

    temp = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.fpsNum = 24;
    } else {
        d.vi.fpsNum = temp;
    }

    temp = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.fpsDen = 1;
    } else
        d.vi.fpsDen = temp;

    if (d.vi.fpsDen < 0 || d.vi.fpsNum < 0)
        RETERROR("BlankClip: invalid framerate specified");

    if (d.vi.fpsDen == 0 || d.vi.fpsNum == 0) {
        d.vi.fpsNum = 0;
        d.vi.fpsDen = 0;
    }

    vs_normalizeRational(&d.vi.fpsNum, &d.vi.fpsDen);

    int format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));

    if (err) {
        if (!hasvi)
            d.vi.format = vsapi->getFormatPreset(pfRGB24, core);
    } else {
        d.vi.format = vsapi->getFormatPreset(format, core);
    }

    if (!d.vi.format)
        RETERROR("BlankClip: invalid format");

    if (isCompatFormat(&d.vi))
        RETERROR("BlankClip: compat formats not supported");

    temp = vsapi->propGetInt(in, "length", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.numFrames = int64ToIntS((d.vi.fpsNum * 10) / d.vi.fpsDen);
    } else {
        d.vi.numFrames = int64ToIntS(temp);
    }

    if (d.vi.width <= 0 || d.vi.width % (1 << d.vi.format->subSamplingW))
        RETERROR("BlankClip: invalid width");

    if (d.vi.height <= 0 || d.vi.height % (1 << d.vi.format->subSamplingH))
        RETERROR("BlankClip: invalid height");

    if (d.vi.numFrames <= 0)
        RETERROR("BlankClip: invalid length");

    setBlack(d.color, d.vi.format);

    int numcomponents = (d.vi.format->colorFamily == cmCompat) ? 3 : d.vi.format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->propGetFloat(in, "color", i, 0);
            if (d.vi.format->sampleType == stInteger) {
                d.color[i] = doubleToIntPixelValue(color, d.vi.format->bitsPerSample, &err);
            } else {
                if (d.vi.format->bitsPerSample == 16)
                    d.color[i] = doubleToHalfPixelValue(color, &err);
                else
                    d.color[i] = doubleToFloatPixelValue(color, &err);
            }
            if (err)
                RETERROR("BlankClip: color value out of range");
        }
    } else if (ncolors > 0) {
        RETERROR("BlankClip: invalid number of color values specified");
    }

    d.keep = !!vsapi->propGetInt(in, "keep", 0, &err);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "BlankClip", blankClipInit, blankClipGetframe, blankClipFree, d.keep ? fmUnordered : fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// AssumeFPS

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
} AssumeFPSData;

static void VS_CC assumeFPSInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = (AssumeFPSData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC assumeFPSGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = (AssumeFPSData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        VSMap *m = vsapi->getFramePropsRW(dst);
        vsapi->freeFrame(src);
        vsapi->propSetInt(m, "_DurationNum", d->vi.fpsDen, paReplace);
        vsapi->propSetInt(m, "_DurationDen", d->vi.fpsNum, paReplace);
        return dst;
    }

    return 0;
}

static void VS_CC assumeFPSCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData d;
    AssumeFPSData *data;
    int hasfps = 0;
    int hassrc = 0;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    d.vi.fpsNum = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (!err)
        hasfps = 1;

    d.vi.fpsDen = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err)
        d.vi.fpsDen = 1;

    VSNodeRef *src = vsapi->propGetNode(in, "src", 0, &err);

    if (!err) {
        const VSVideoInfo *vi = vsapi->getVideoInfo(src);
        d.vi.fpsNum = vi->fpsNum;
        d.vi.fpsDen = vi->fpsDen;
        vsapi->freeNode(src);
        hassrc = 1;
    }

    if (hasfps == hassrc) {
        vsapi->freeNode(d.node);
        RETERROR("AssumeFPS: need to specify source clip or fps");
    }

    if (d.vi.fpsDen < 1 || d.vi.fpsNum < 1) {
        vsapi->freeNode(d.node);
        RETERROR("AssumeFPS: invalid framerate specified");
    }

    vs_normalizeRational(&d.vi.fpsNum, &d.vi.fpsDen);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "AssumeFPS", assumeFPSInit, assumeFPSGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// FrameEval

typedef struct {
    VSVideoInfo vi;
    VSFuncRef *func;
    VSNodeRef **propsrc;
    int numpropsrc;
    VSMap *in;
    VSMap *out;
} FrameEvalData;

static void VS_CC frameEvalInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = (FrameEvalData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC frameEvalGetFrameWithProps(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = (FrameEvalData *) * instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < d->numpropsrc; i++)
            vsapi->requestFrameFilter(n, d->propsrc[i], frameCtx);
    } else if (activationReason == arAllFramesReady && !*frameData) {
        int err;
        vsapi->propSetInt(d->in, "n", n, paAppend);
        for (int i = 0; i < d->numpropsrc; i++) {
            const VSFrameRef *f = vsapi->getFrameFilter(n, d->propsrc[i], frameCtx);
            vsapi->propSetFrame(d->in, "f", f, paAppend);
            vsapi->freeFrame(f);
        }
        vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
        vsapi->clearMap(d->in);
        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return 0;
        }

        VSNodeRef *node = vsapi->propGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return 0;
        }

        *frameData = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *frame;
        VSNodeRef *node = (VSNodeRef *)*frameData;
        frame = vsapi->getFrameFilter(n, node, frameCtx);
        vsapi->freeNode(node);

        if (d->vi.width || d->vi.height) {
            if (d->vi.width != vsapi->getFrameWidth(frame, 0) || d->vi.height != vsapi->getFrameHeight(frame, 0)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong dimensions", frameCtx);
                return 0;
            }
        }

        if (d->vi.format) {
            if (d->vi.format != vsapi->getFrameFormat(frame)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong format", frameCtx);
                return 0;
            }
        }
        return frame;
    } else if (activationReason == arError) {
        vsapi->freeNode(*frameData);
    }

    return 0;
}

static const VSFrameRef *VS_CC frameEvalGetFrameNoProps(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = (FrameEvalData *) * instanceData;

    if (activationReason == arInitial) {

        int err;
        vsapi->propSetInt(d->in, "n", n, paAppend);
        vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
        vsapi->clearMap(d->in);
        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return 0;
        }

        VSNodeRef *node = vsapi->propGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return 0;
        }

        *frameData = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSNodeRef *node = (VSNodeRef *)*frameData;
        const VSFrameRef *frame = vsapi->getFrameFilter(n, node, frameCtx);
        vsapi->freeNode(node);

        if (d->vi.width || d->vi.height) {
            if (d->vi.width != vsapi->getFrameWidth(frame, 0) || d->vi.height != vsapi->getFrameHeight(frame, 0)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong dimensions", frameCtx);
                return 0;
            }
        }

        if (d->vi.format) {
            if (d->vi.format != vsapi->getFrameFormat(frame)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong format", frameCtx);
                return 0;
            }
        }
        return frame;
    } else if (activationReason == arError) {
        vsapi->freeNode(*frameData);
    }

    return 0;
}

static void VS_CC frameEvalFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = (FrameEvalData *)instanceData;
    for (int i = 0; i < d->numpropsrc; i++)
        vsapi->freeNode(d->propsrc[i]);
    free(d->propsrc);
    vsapi->freeFunc(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    free(d);
}

static void VS_CC frameEvalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData d;
    FrameEvalData *data;
    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, 0);
    d.propsrc = 0;
    d.vi = *vsapi->getVideoInfo(node);
    vsapi->freeNode(node);
    d.func = vsapi->propGetFunc(in, "eval", 0, 0);
    d.numpropsrc = vsapi->propNumElements(in, "prop_src");
    if (d.numpropsrc < 0)
        d.numpropsrc = 0;
    if (d.numpropsrc > 0) {
        d.propsrc = malloc(sizeof(VSNodeRef *)*d.numpropsrc);
        for (int i = 0; i < d.numpropsrc; i++)
            d.propsrc[i] = vsapi->propGetNode(in, "prop_src", i, 0);
    }

    d.in = vsapi->createMap();
    d.out = vsapi->createMap();

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "FrameEval", frameEvalInit, d.numpropsrc ? frameEvalGetFrameWithProps : frameEvalGetFrameNoProps, frameEvalFree, d.numpropsrc ? fmParallelRequests : fmUnordered, 0, data, core);
}

//////////////////////////////////////////
// ModifyFrame

typedef struct {
    VSNodeRef **node;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    VSMap *in;
    VSMap *out;
    int numnode;
} ModifyFrameData;

static void VS_CC modifyFrameInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC modifyFrameGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *) * instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < d->numnode; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;

        vsapi->propSetInt(d->in, "n", n, paAppend);

        for (int i = 0; i < d->numnode; i++) {
            const VSFrameRef *f = vsapi->getFrameFilter(n, d->node[i], frameCtx);
            vsapi->propSetFrame(d->in, "f", f, paAppend);
            vsapi->freeFrame(f);
        }

        vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
        vsapi->clearMap(d->in);

        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return 0;
        }

        const VSFrameRef *f = vsapi->propGetFrame(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);
        if (err) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned value not a frame", frameCtx);
            return 0;
        }

        if (d->vi->format && d->vi->format != vsapi->getFrameFormat(f)) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong format", frameCtx);
            return 0;
        }

        if ((d->vi->width || d->vi->height) && (d->vi->width != vsapi->getFrameWidth(f, 0) || d->vi->height != vsapi->getFrameHeight(f, 0))) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong dimensions", frameCtx);
            return 0;
        }

        return f;
    }

    return 0;
}

static void VS_CC modifyFrameFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *)instanceData;
    for (int i = 0; i < d->numnode; i++)
        vsapi->freeNode(d->node[i]);
    free(d->node);
    vsapi->freeFunc(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    free(d);
}

static void VS_CC modifyFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData d;
    ModifyFrameData *data;
    VSNodeRef *formatnode = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(formatnode);
    vsapi->freeNode(formatnode);

    d.numnode = vsapi->propNumElements(in, "clips");
    d.node = malloc(d.numnode * sizeof(d.node[0]));
    for (int i = 0; i < d.numnode; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

    d.func = vsapi->propGetFunc(in, "selector", 0, 0);
    d.in = vsapi->createMap();
    d.out = vsapi->createMap();

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ModifyFrame", modifyFrameInit, modifyFrameGetFrame, modifyFrameFree, fmParallelRequests, 0, data, core);
}

//////////////////////////////////////////
// Transpose

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int cpulevel;
} TransposeData;

static void VS_CC transposeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC transposeGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        int width;
        int height;
        const uint8_t * VS_RESTRICT srcp;
        int src_stride;
        uint8_t * VS_RESTRICT dstp;
        int dst_stride;

        void (*func)(const void *, ptrdiff_t, void *, ptrdiff_t, unsigned, unsigned) = NULL;

#ifdef VS_TARGET_CPU_X86
        if (d->cpulevel >= VS_CPU_LEVEL_SSE2) {
            switch (d->vi.format->bytesPerSample) {
            case 1: func = vs_transpose_plane_byte_sse2; break;
            case 2: func = vs_transpose_plane_word_sse2; break;
            case 4: func = vs_transpose_plane_dword_sse2; break;
            }
        }
#endif
        if (!func) {
            switch (d->vi.format->bytesPerSample) {
            case 1: func = vs_transpose_plane_byte_c; break;
            case 2: func = vs_transpose_plane_word_c; break;
            case 4: func = vs_transpose_plane_dword_c; break;
            }
        }

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            width = vsapi->getFrameWidth(src, plane);
            height = vsapi->getFrameHeight(src, plane);
            srcp = vsapi->getReadPtr(src, plane);
            src_stride = vsapi->getStride(src, plane);
            dstp = vsapi->getWritePtr(dst, plane);
            dst_stride = vsapi->getStride(dst, plane);

            if (func)
                func(srcp, src_stride, dstp, dst_stride, width, height);
        }

        vsapi->freeFrame(src);
        return dst;

    }

    return 0;
}

static void VS_CC transposeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC transposeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    TransposeData d;
    TransposeData *data;
    int temp;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    temp = d.vi.width;
    d.vi.width = d.vi.height;
    d.vi.height = temp;

    if (!isConstantFormat(&d.vi) || d.vi.format->id == pfCompatYUY2) {
        vsapi->freeNode(d.node);
        RETERROR("Transpose: clip must have constant format and dimensions and must not be CompatYUY2");
    }

    d.vi.format = vsapi->registerFormat(d.vi.format->colorFamily, d.vi.format->sampleType, d.vi.format->bitsPerSample, d.vi.format->subSamplingH, d.vi.format->subSamplingW, core);
    d.cpulevel = vs_get_cpulevel(core);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Transpose", transposeInit, transposeGetFrame, transposeFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PEM(Level)Verifier

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int upper[3];
    int lower[3];
    float upperf[3];
    float lowerf[3];
} PEMVerifierData;

static void VS_CC pemVerifierInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC pemVerifierGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        char strbuf[512];

        for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
            int width = vsapi->getFrameWidth(src, plane);
            int height = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            float f;
            uint16_t v;

            switch (d->vi->format->bytesPerSample) {
            case 1:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++)
                        if (srcp[x] < d->lower[plane] || srcp[x] > d->upper[plane]) {
                            snprintf(strbuf, sizeof(strbuf), "PEMVerifier: Illegal sample value (%d) at: plane: %d Y: %d, X: %d, Frame: %d", (int)srcp[x], plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return 0;
                        }
                    srcp += src_stride;
                }
                break;
            case 2:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        v = ((const uint16_t *)srcp)[x];
                        if (v < d->lower[plane] || v > d->upper[plane]) {
                            snprintf(strbuf, sizeof(strbuf), "PEMVerifier: Illegal sample value (%d) at: plane: %d Y: %d, X: %d, Frame: %d", (int)v, plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return 0;
                        }
                    }
                    srcp += src_stride;
                }
                break;
            case 4:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        f = ((const float *)srcp)[x];
                        if (f < d->lowerf[plane] || f > d->upperf[plane] || !isfinite(f)) {
                            snprintf(strbuf, sizeof(strbuf), "PEMVerifier: Illegal sample value (%f) at: plane: %d Y: %d, X: %d, Frame: %d", f, plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return 0;
                        }
                    }
                    srcp += src_stride;
                }
                break;
            }
        }
        return src;
    }
    return 0;
}

static void VS_CC pemVerifierFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC pemVerifierCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData d;
    PEMVerifierData *data;
    int numupper = vsapi->propNumElements(in, "upper");
    int numlower = vsapi->propNumElements(in, "lower");

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->format || isCompatFormat(d.vi) || (d.vi->format->sampleType == stInteger && (d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2))
        || (d.vi->format->sampleType == stFloat && d.vi->format->bytesPerSample != 4)) {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: clip must be constant format and of integer 8-16 bit type or 32 bit float");
    }

    if (numlower < 0) {
        for (int i = 0; i < d.vi->format->numPlanes; i++) {
            d.lower[i] = 0;
            d.lowerf[i] = ((d.vi->format->colorFamily == cmYUV || d.vi->format->colorFamily == cmYCoCg) && i) ? -0.5f : 0.0f;
        }
    } else if (numlower == d.vi->format->numPlanes) {
        for (int i = 0; i < d.vi->format->numPlanes; i++) {
            d.lowerf[i] = (float)vsapi->propGetFloat(in, "lower", i, 0);
            d.lower[i] = floatToIntS(d.lowerf[i]);
            if (d.vi->format->sampleType == stInteger && (d.lower[i] < 0 || d.lower[i] >= (1 << d.vi->format->bitsPerSample))) {
                vsapi->freeNode(d.node);
                RETERROR("PEMVerifier: Invalid lower bound given");
            }
        }
    } else {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: number of lower plane limits does not match the number of planes");
    }

    if (numupper < 0) {
        for (int i = 0; i < d.vi->format->numPlanes; i++) {
            d.upper[i] = (1 << d.vi->format->bitsPerSample) - 1;
            d.upperf[i] = ((d.vi->format->colorFamily == cmYUV || d.vi->format->colorFamily == cmYCoCg) && i) ? 0.5f : 1.0f;
        }
    } else if (numupper == d.vi->format->numPlanes) {
        for (int i = 0; i < d.vi->format->numPlanes; i++) {
            d.upperf[i] = (float)vsapi->propGetFloat(in, "upper", i, 0);
            d.upper[i] = floatToIntS(d.upperf[i]);
            if (d.vi->format->sampleType == stInteger && (d.upper[i] < d.lower[i] || d.upper[i] >= (1 << d.vi->format->bitsPerSample))) {
                vsapi->freeNode(d.node);
                RETERROR("PEMVerifier: Invalid upper bound given");
            } else if (d.vi->format->sampleType == stFloat && (d.upperf[i] < d.lowerf[i])) {
                vsapi->freeNode(d.node);
                RETERROR("PEMVerifier: Invalid upper bound given");
            }
        }
    } else {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: number of upper plane limits does not match the number of planes");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PEMVerifier", pemVerifierInit, pemVerifierGetFrame, pemVerifierFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PlaneStats

typedef struct {
    VSNodeRef *node1;
    VSNodeRef *node2;
    const VSVideoInfo *vi;
    char *propAverage;
    char *propMin;
    char *propMax;
    char *propDiff;
    int plane;
    int cpulevel;
} PlaneStatsData;

static void VS_CC planeStatsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData *d = (PlaneStatsData *)* instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC planeStatsGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData *d = (PlaneStatsData *)* instanceData;
    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        if (d->node2)
            vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = d->node2 ? vsapi->getFrameFilter(n, d->node2, frameCtx) : NULL;
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        const VSFormat *fi = vsapi->getFrameFormat(dst);
        int width = vsapi->getFrameWidth(src1, d->plane);
        int height = vsapi->getFrameHeight(src1, d->plane);
        const uint8_t *srcp = vsapi->getReadPtr(src1, d->plane);
        int src_stride = vsapi->getStride(src1, d->plane);
        union vs_plane_stats stats = { 0 };

        if (src2) {
            const void *srcp2 = vsapi->getReadPtr(src2, d->plane);
            ptrdiff_t src2_stride = vsapi->getStride(src2, d->plane);
            void (*func)(union vs_plane_stats *, const void *, ptrdiff_t, const void *, ptrdiff_t, unsigned, unsigned) = NULL;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_avx2; break;
                case 2: func = vs_plane_stats_2_word_avx2; break;
                case 4: func = vs_plane_stats_2_float_avx2; break;
                }
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_sse2; break;
                case 2: func = vs_plane_stats_2_word_sse2; break;
                case 4: func = vs_plane_stats_2_float_sse2; break;
                }
            }
#endif
            if (!func) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_c; break;
                case 2: func = vs_plane_stats_2_word_c; break;
                case 4: func = vs_plane_stats_2_float_c; break;
                }
            }

            if (func)
                func(&stats, srcp, src_stride, srcp2, src2_stride, width, height);
        } else {
            void (*func)(union vs_plane_stats *, const void *, ptrdiff_t, unsigned, unsigned) = NULL;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_1_byte_avx2; break;
                case 2: func = vs_plane_stats_1_word_avx2; break;
                case 4: func = vs_plane_stats_1_float_avx2; break;
                }
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_1_byte_sse2; break;
                case 2: func = vs_plane_stats_1_word_sse2; break;
                case 4: func = vs_plane_stats_1_float_sse2; break;
                }
            }
#endif
            if (!func) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_1_byte_c; break;
                case 2: func = vs_plane_stats_1_word_c; break;
                case 4: func = vs_plane_stats_1_float_c; break;
                }
            }

            if (func)
                func(&stats, srcp, src_stride, width, height);
        }

        VSMap *dstProps = vsapi->getFramePropsRW(dst);

        if (fi->sampleType == stInteger) {
            vsapi->propSetInt(dstProps, d->propMin, stats.i.min, paReplace);
            vsapi->propSetInt(dstProps, d->propMax, stats.i.max, paReplace);
        } else {
            vsapi->propSetFloat(dstProps, d->propMin, stats.f.min, paReplace);
            vsapi->propSetFloat(dstProps, d->propMax, stats.f.max, paReplace);
        }

        double avg = 0.0;
        double diff = 0.0;
        if (fi->sampleType == stInteger) {
            avg = stats.i.acc / (double)(width * height * (((int64_t)1 << fi->bitsPerSample) - 1));
            if (d->node2)
                diff = stats.i.diffacc / (double)(width * height * (((int64_t)1 << fi->bitsPerSample) - 1));
        } else {
            avg = stats.f.acc / (double)((int64_t)width * height);
            if (d->node2)
                diff = stats.f.diffacc / (double)((int64_t)width * height);
        }

        vsapi->propSetFloat(dstProps, d->propAverage, avg, paReplace);
        if (d->node2)
            vsapi->propSetFloat(dstProps, d->propDiff, diff, paReplace);

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }
    return 0;
}

static void VS_CC planeStatsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData *d = (PlaneStatsData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    free(d->propAverage);
    free(d->propMin);
    free(d->propMax);
    free(d->propDiff);
    free(d);
}

static void VS_CC planeStatsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData d;
    PlaneStatsData *data;
    int err;

    d.node1 = vsapi->propGetNode(in, "clipa", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node1);

    if (!d.vi->format || isCompatFormat(d.vi) || (d.vi->format->sampleType == stInteger && (d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2))
        || (d.vi->format->sampleType == stFloat && d.vi->format->bytesPerSample != 4)) {
        vsapi->freeNode(d.node1);
        RETERROR("PlaneStats: clip must be constant format and of integer 8-16 bit type or 32 bit float");
    }

    d.plane = int64ToIntS(vsapi->propGetInt(in, "plane", 0, &err));
    if (d.plane < 0 || d.plane >= d.vi->format->numPlanes) {
        vsapi->freeNode(d.node1);
        RETERROR("PlaneStats: invalid plane specified");
    }

    d.node2 = vsapi->propGetNode(in, "clipb", 0, &err);
    if (d.node2) {
        if (!isSameFormat(d.vi, vsapi->getVideoInfo(d.node2)) || !isConstantFormat(vsapi->getVideoInfo(d.node2))) {
            vsapi->freeNode(d.node1);
            vsapi->freeNode(d.node2);
            RETERROR("PlaneStats: both input clips must have the same format when clipb is used");
        }
    }

    const char *tempprop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        tempprop = "PlaneStats";
    size_t l = strlen(tempprop);
    d.propMin = malloc(l + 3 + 1);
    d.propMax = malloc(l + 3 + 1);
    d.propAverage = malloc(l + 7 + 1);
    d.propDiff = malloc(l + 4 + 1);
    strcpy(d.propMin, tempprop);
    strcpy(d.propMax, tempprop);
    strcpy(d.propAverage, tempprop);
    strcpy(d.propDiff, tempprop);
    strcpy(d.propMin + l, "Min");
    strcpy(d.propMax + l, "Max");
    strcpy(d.propAverage + l, "Average");
    strcpy(d.propDiff + l, "Diff");
    d.cpulevel = vs_get_cpulevel(core);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PlaneStats", planeStatsInit, planeStatsGetFrame, planeStatsFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// ClipToProp

typedef struct {
    VSNodeRef *node1;
    VSNodeRef *node2;
    VSVideoInfo vi;
    char *prop;
} ClipToPropData;

static void VS_CC clipToPropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC clipToPropGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        vsapi->propSetFrame(vsapi->getFramePropsRW(dst), d->prop, src2, paReplace);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return 0;
}

static void VS_CC clipToPropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    free(d->prop);
    free(d);
}

static void VS_CC clipToPropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData d;
    ClipToPropData *data;
    int err;

    d.node1 = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node1);
    d.node2 = vsapi->propGetNode(in, "mclip", 0, 0);
    d.vi.numFrames = vsapi->getVideoInfo(d.node2)->numFrames;

    if (!isConstantFormat(&d.vi) || !isConstantFormat(vsapi->getVideoInfo(d.node2))) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("ClipToProp: clips must have constant format and dimensions");
    }

    const char *tempprop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        tempprop = "_Alpha";
    d.prop = malloc(strlen(tempprop) + 1);
    strcpy(d.prop, tempprop);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ClipToProp", clipToPropInit, clipToPropGetFrame, clipToPropFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PropToClip

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    char *prop;
} PropToClipData;

static void VS_CC propToClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC propToClipGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *dst = vsapi->propGetFrame(vsapi->getFramePropsRO(src), d->prop, 0, &err);
        vsapi->freeFrame(src);

        if (dst) {
            if (d->vi.format != vsapi->getFrameFormat(dst) || d->vi.height != vsapi->getFrameHeight(dst, 0) || d->vi.width != vsapi->getFrameWidth(dst, 0)) {
                vsapi->setFilterError("PropToClip: retrieved frame doesn't match output format or dimensions", frameCtx);
                return 0;
            }

            return dst;
        } else {
            vsapi->setFilterError("PropToClip: failed to extract frame from specified property", frameCtx);
            return 0;
        }
    }

    return 0;
}

static void VS_CC propToClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *)instanceData;
    vsapi->freeNode(d->node);
    free(d->prop);
    free(d);
}

static void VS_CC propToClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PropToClipData d;
    PropToClipData *data;
    int err;
    char errmsg[512];

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("PropToClip: clip must have constant format and dimensions");
    }

    const char *tempprop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        tempprop = "_Alpha";


    const VSFrameRef *src = vsapi->getFrame(0, d.node, errmsg, sizeof(errmsg));
    if (!src) {
        char errmsg2[1024];
        vsapi->freeNode(d.node);
        snprintf(errmsg2, sizeof(errmsg2), "PropToClip: upstream error: %s", errmsg);
        RETERROR(errmsg2);
    }

    const VSFrameRef *msrc = vsapi->propGetFrame(vsapi->getFramePropsRO(src), tempprop, 0, &err);
    if (err) {
        vsapi->freeNode(d.node);
        vsapi->freeFrame(src);
        RETERROR("PropToClip: no frame stored in property");
    }

    d.prop = malloc(strlen(tempprop) + 1);
    strcpy(d.prop, tempprop);

    d.vi.format = vsapi->getFrameFormat(msrc);
    d.vi.width = vsapi->getFrameWidth(msrc, 0);
    d.vi.height = vsapi->getFrameHeight(msrc, 0);
    vsapi->freeFrame(msrc);
    vsapi->freeFrame(src);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PropToClip", propToClipInit, propToClipGetFrame, propToClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// SetFrameProp

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    char *prop;
    int delete;
    int64_t *ints;
    double *floats;
    char **strings;
    int num_ints;
    int num_floats;
    int num_strings;
} SetFramePropData;

static void VS_CC setFramePropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData *d = (SetFramePropData *) * instanceData;

    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC setFramePropGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData *d = (SetFramePropData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropsRW(dst);

        if (d->delete)
            vsapi->propDeleteKey(props, d->prop);
        else {
            if (d->ints)
                vsapi->propSetIntArray(props, d->prop, d->ints, d->num_ints);
            else if (d->floats)
                vsapi->propSetFloatArray(props, d->prop, d->floats, d->num_floats);
            else if (d->strings) {
                vsapi->propSetData(props, d->prop, d->strings[0], -1, paReplace);
                for (int i = 1; i < d->num_strings; i++)
                    vsapi->propSetData(props, d->prop, d->strings[i], -1, paAppend);
            }
        }

        return dst;
    }

    return 0;
}

static void VS_CC setFramePropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData *d = (SetFramePropData *)instanceData;

    vsapi->freeNode(d->node);
    free(d->prop);
    if (d->ints)
        free(d->ints);
    if (d->floats)
        free(d->floats);
    if (d->strings) {
        for (int i = 0; i < d->num_strings; i++)
            free(d->strings[i]);
        free(d->strings);
    }

    free(d);
}

static void VS_CC setFramePropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData d;
    SetFramePropData *data;
    int err;

    d.delete = !!vsapi->propGetInt(in, "delete", 0, &err);

    d.num_ints = vsapi->propNumElements(in, "intval");
    d.num_floats = vsapi->propNumElements(in, "floatval");
    d.num_strings = vsapi->propNumElements(in, "data");

    if ((d.num_ints > -1) + (d.num_floats > -1) + (d.num_strings > -1) > 1)
        RETERROR("SetFrameProp: only one of 'intval', 'floatval', and 'data' can be passed.");

    if (d.delete && (d.num_ints + d.num_floats + d.num_strings > -3))
        RETERROR("SetFrameProp: 'delete' can't be True when passing one of 'intval', 'floatval', or 'data'.");

    if (!d.delete && (d.num_ints + d.num_floats + d.num_strings == -3))
        RETERROR("SetFrameProp: one of 'intval', 'floatval', or 'data' must be passed.");

    int prop_len = vsapi->propGetDataSize(in, "prop", 0, NULL);

    if (prop_len == 0)
        RETERROR("SetFrameProp: 'prop' can't be an empty string.");

    d.prop = malloc(prop_len + 1);
    memcpy(d.prop, vsapi->propGetData(in, "prop", 0, NULL), prop_len + 1); // It's NULL-terminated.

    d.node = vsapi->propGetNode(in, "clip", 0, NULL);
    d.vi = vsapi->getVideoInfo(d.node);

    d.ints = NULL;
    d.floats = NULL;
    d.strings = NULL;

    if (d.num_ints > -1) {
        d.ints = malloc(d.num_ints * sizeof(int64_t));
        memcpy(d.ints, vsapi->propGetIntArray(in, "intval", NULL), d.num_ints * sizeof(int64_t));
    }

    if (d.num_floats > -1) {
        d.floats = malloc(d.num_floats * sizeof(double));
        memcpy(d.floats, vsapi->propGetFloatArray(in, "floatval", NULL), d.num_floats * sizeof(double));
    }

    if (d.num_strings > -1) {
        d.strings = malloc(d.num_strings * sizeof(char *));
        for (int i = 0; i < d.num_strings; i++) {
            int string_len = vsapi->propGetDataSize(in, "data", i, NULL);
            d.strings[i] = malloc(string_len + 1);
            memcpy(d.strings[i], vsapi->propGetData(in, "data", i, NULL), string_len + 1);
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "SetFrameProp", setFramePropInit, setFramePropGetFrame, setFramePropFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// SetFieldBased

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int64_t fieldbased;
} SetFieldBasedData;

static const VSFrameRef *VS_CC setFieldBasedGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFieldBasedData *d = (SetFieldBasedData *)* instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropsRW(dst);
        vsapi->propDeleteKey(props, "_Field");
        vsapi->propSetInt(props, "_FieldBased", d->fieldbased, paReplace);

        return dst;
    }

    return 0;
}

static void VS_CC setFieldBasedCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SetFieldBasedData d;
    SetFieldBasedData *data;

    d.fieldbased = vsapi->propGetInt(in, "value", 0, NULL);
    if (d.fieldbased < 0 || d.fieldbased > 2)
        RETERROR("SetFieldBased: value must be 0, 1 or 2");
    d.node = vsapi->propGetNode(in, "clip", 0, NULL);
    d.vi = vsapi->getVideoInfo(d.node);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "SetFieldBased", singleClipInit, setFieldBasedGetFrame, singleClipFree, fmParallel, nfNoCache, data, core);
}

static void VS_CC setMaxCpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *str = vsapi->propGetData(in, "cpu", 0, NULL);
    int level = vs_cpulevel_from_str(str);
    level = vs_set_cpulevel(core, level);
    str = vs_cpulevel_to_str(level);
    vsapi->propSetData(out, "cpu", str, (int)strlen(str), paReplace);
}

//////////////////////////////////////////
// Init

void VS_CC stdlibInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("CropAbs", "clip:clip;width:int;height:int;left:int:opt;top:int:opt;x:int:opt;y:int:opt;", cropAbsCreate, 0, plugin);
    registerFunc("CropRel", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", cropRelCreate, 0, plugin);
    registerFunc("Crop", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", cropRelCreate, 0, plugin);
    registerFunc("AddBorders", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;color:float[]:opt;", addBordersCreate, 0, plugin);
    registerFunc("ShufflePlanes", "clips:clip[];planes:int[];colorfamily:int;", shufflePlanesCreate, 0, plugin);
    registerFunc("SeparateFields", "clip:clip;tff:int:opt;modify_duration:int:opt;", separateFieldsCreate, 0, plugin);
    registerFunc("DoubleWeave", "clip:clip;tff:int:opt;", doubleWeaveCreate, 0, plugin);
    registerFunc("FlipVertical", "clip:clip;", flipVerticalCreate, 0, plugin);
    registerFunc("FlipHorizontal", "clip:clip;", flipHorizontalCreate, 0, plugin);
    registerFunc("Turn180", "clip:clip;", flipHorizontalCreate, (void *)1, plugin);
    registerFunc("StackVertical", "clips:clip[];", stackCreate, (void *)1, plugin);
    registerFunc("StackHorizontal", "clips:clip[];", stackCreate, 0, plugin);
    registerFunc("BlankClip", "clip:clip:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:float[]:opt;keep:int:opt;", blankClipCreate, 0, plugin);
    registerFunc("AssumeFPS", "clip:clip;src:clip:opt;fpsnum:int:opt;fpsden:int:opt;", assumeFPSCreate, 0, plugin);
    registerFunc("FrameEval", "clip:clip;eval:func;prop_src:clip[]:opt;", frameEvalCreate, 0, plugin);
    registerFunc("ModifyFrame", "clip:clip;clips:clip[];selector:func;", modifyFrameCreate, 0, plugin);
    registerFunc("Transpose", "clip:clip;", transposeCreate, 0, plugin);
    registerFunc("PEMVerifier", "clip:clip;upper:float[]:opt;lower:float[]:opt;", pemVerifierCreate, 0, plugin);
    registerFunc("PlaneStats", "clipa:clip;clipb:clip:opt;plane:int:opt;prop:data:opt;", planeStatsCreate, 0, plugin);
    registerFunc("ClipToProp", "clip:clip;mclip:clip;prop:data:opt;", clipToPropCreate, 0, plugin);
    registerFunc("PropToClip", "clip:clip;prop:data:opt;", propToClipCreate, 0, plugin);
    registerFunc("SetFrameProp", "clip:clip;prop:data;delete:int:opt;intval:int[]:opt;floatval:float[]:opt;data:data[]:opt;", setFramePropCreate, 0, plugin);
    registerFunc("SetFieldBased", "clip:clip;value:int;", setFieldBasedCreate, 0, plugin);
    registerFunc("SetMaxCPU", "cpu:data;", setMaxCpu, 0, plugin);
}
