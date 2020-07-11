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

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include "VSHelper4.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "filtersharedcpp.h"
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
    const VSVideoInfo *vi;
    int x;
    int y;
    int width;
    int height;
} CropDataExtra;

typedef SingleNodeData<CropDataExtra> CropData;

static int cropVerify(int x, int y, int width, int height, int srcwidth, int srcheight, const VSVideoFormat *fi, char *msg, size_t len) {
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

static const VSFrameRef *VS_CC cropGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CropData *d = reinterpret_cast<CropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        char msg[150];
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);
        int y = (fi->colorFamily == cfCompatBGR32) ? (height - d->height - d->y) : d->y;

        if (cropVerify(d->x, y, d->width, d->height, width, height, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return nullptr;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(fi, d->width, d->height, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            ptrdiff_t srcstride = vsapi->getStride(src, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
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

    return nullptr;
}

static void VS_CC cropAbsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CropData> d(new CropData(vsapi));
    char msg[150];
    int err;

    d->x = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d->x = int64ToIntS(vsapi->propGetInt(in, "x", 0, &err));
    d->y = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d->y = int64ToIntS(vsapi->propGetInt(in, "y", 0, &err));

    d->height = int64ToIntS(vsapi->propGetInt(in, "height", 0, 0));
    d->width = int64ToIntS(vsapi->propGetInt(in, "width", 0, 0));
    d->node = vsapi->propGetNode(in, "clip", 0, 0);

    d->vi = vsapi->getVideoInfo(d->node);

    if (cropVerify(d->x, d->y, d->width, d->height, d->vi->width, d->vi->height, &d->vi->format, msg, sizeof(msg)))
        RETERROR(msg);

    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;

    vsapi->createVideoFilter(out, "Crop", &vi, 1, cropGetframe, filterFree<CropData>, fmParallel, 0, d.release(), core);
}

static void VS_CC cropRelCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CropData> d(new CropData(vsapi));
    char msg[150];
    int err;

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    if (!isConstantVideoFormat(d->vi))
        RETERROR("Crop: constant format and dimensions needed");

    d->x = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d->y = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));

    d->height = d->vi->height - d->y - int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    d->width = d->vi->width - d->x - int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));

    // passthrough for the no cropping case
    if (d->x == 0 && d->y == 0 && d->width == d->vi->width && d->height == d->vi->height) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        return;
    }

    if (cropVerify(d->x, d->y, d->width, d->height, d->vi->width, d->vi->height, &d->vi->format, msg, sizeof(msg)))
        RETERROR(msg);

    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;

    vsapi->createVideoFilter(out, "Crop", &vi, 1, cropGetframe, filterFree<CropData>, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// AddBorders

typedef struct {
    int left;
    int right;
    int top;
    int bottom;
    uint32_t color[3];
} AddBordersDataExtra;

typedef SingleNodeData<AddBordersDataExtra> AddBordersData;

static int addBordersVerify(int left, int right, int top, int bottom, const VSVideoFormat *fi, char *msg, size_t len) {
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

static const VSFrameRef *VS_CC addBordersGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = reinterpret_cast<AddBordersData *>(instanceData);
    char msg[150];

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrameRef *dst;

        if (addBordersVerify(d->left, d->right, d->top, d->bottom, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return nullptr;
        }

        dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0) + d->left + d->right, vsapi->getFrameHeight(src, 0) + d->top + d->bottom, src, core);

        // now that argument validation is over we can spend the next few lines actually adding borders
        for (int plane = 0; plane < fi->numPlanes; plane++) {
            int rowsize = vsapi->getFrameWidth(src, plane) * fi->bytesPerSample;
            ptrdiff_t srcstride = vsapi->getStride(src, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            int srcheight = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            int padt = d->top >> (plane ? fi->subSamplingH : 0);
            int padb = d->bottom >> (plane ? fi->subSamplingH : 0);
            int padl = (d->left >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            int padr = (d->right >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            uint32_t color = d->color[plane];

            switch (fi->bytesPerSample) {
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
                switch (fi->bytesPerSample) {
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

            switch (fi->bytesPerSample) {
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

    return nullptr;
}

static void VS_CC addBordersCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AddBordersData> d(new AddBordersData(vsapi));
    char msg[150];
    int err;

    d->left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d->right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    d->top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    d->bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    d->node = vsapi->propGetNode(in, "clip", 0, 0);

    // pass through if nothing to be done
    if (d->left == 0 && d->right == 0 && d->top == 0 && d->bottom == 0) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        return;
    }

    if (d->left < 0 || d->right < 0 || d->top < 0 || d->bottom < 0)
        RETERROR("AddBorders: border size to add must not be negative");

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    if (isCompatFormat(&vi.format))
        RETERROR("AddBorders: compat formats not supported");

    if (vi.format.colorFamily == cfUndefined)
        RETERROR("AddBorders: input needs to be constant format");

    if (addBordersVerify(d->left, d->right, d->top, d->bottom, &vi.format, msg, sizeof(msg)))
        RETERROR(msg);

    int numcomponents = isCompatFormat(&vi.format) ? 3 : vi.format.numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");

    setBlack(d->color, &vi.format);

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->propGetFloat(in, "color", i, 0);
            if (vi.format.sampleType == stInteger) {
                d->color[i] = doubleToIntPixelValue(color, vi.format.bitsPerSample, &err);
            } else {
                if (vi.format.bitsPerSample == 16)
                    d->color[i] = doubleToHalfPixelValue(color, &err);
                else
                    d->color[i] = doubleToFloatPixelValue(color, &err);
            }
            if (err)
                RETERROR("AddBorders: color value out of range");
        }
    } else if (ncolors > 0) {
        RETERROR("AddBorders: invalid number of color values specified");
    }

    vi.height += vi.height ? (d->top + d->bottom) : 0;
    vi.width += vi.width ? (d->left + d->right) : 0;

    vsapi->createVideoFilter(out, "AddBorders", &vi, 1, addBordersGetframe, filterFree<AddBordersData>, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// ShufflePlanes

typedef struct {
    VSVideoInfo vi;
    int plane[3];
    int format;
} ShufflePlanesDataExtra;

typedef VariableNodeData<ShufflePlanesDataExtra> ShufflePlanesData;

static const VSFrameRef *VS_CC shufflePlanesGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = reinterpret_cast<ShufflePlanesData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node[0], frameCtx);

        if (d->node[1] && d->node[1] != d->node[0])
            vsapi->requestFrameFilter(n, d->node[1], frameCtx);

        if (d->node[2] && d->node[2] != d->node[0] && d->node[2] != d->node[1])
            vsapi->requestFrameFilter(n, d->node[2], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        if (d->vi.format.colorFamily != cfGray) {
            const VSFrameRef *src[3];
            VSFrameRef *dst;

            for (int i = 0; i < 3; i++)
                src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);

            dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height, src, d->plane, src[0], core);

            for (int i = 0; i < 3; i++)
                vsapi->freeFrame(src[i]);

            return dst;
        } else {
            VSFrameRef *dst;
            const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
            const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);

            if (d->plane[0] >= fi->numPlanes) {
                vsapi->freeFrame(src);
                vsapi->setFilterError("ShufflePlanes: invalid plane specified", frameCtx);
                return nullptr;
            }

            dst = vsapi->newVideoFrame2(&d->vi.format, vsapi->getFrameWidth(src, d->plane[0]), vsapi->getFrameHeight(src, d->plane[0]), &src, d->plane, src, core);

            vsapi->freeFrame(src);
            return dst;
        }
    }

    return nullptr;
}

static int findSubSampling(int s1, int s2) {
    for (int i = 0; i < 6; i++)
        if (s1 - (s2 << i) == 0)
            return i;
    return -1;
}

static void VS_CC shufflePlanesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ShufflePlanesData> d(new ShufflePlanesData(vsapi));
    int nclips = vsapi->propNumElements(in, "clips");
    int nplanes = vsapi->propNumElements(in, "planes");
    int err;

    d->node.resize(3);
    assert(d->plane[0] == 0);

    d->format = int64ToIntS(vsapi->propGetInt(in, "colorfamily", 0, 0));

    // FIXME, translate from old colorfamily constants too!

    if (d->format != cfRGB && d->format != cfYUV && d->format != cfGray)
        RETERROR("ShufflePlanes: invalid output colorfamily");

    int outplanes = (d->format == cfGray ? 1 : 3);

    // please don't make this assumption if you ever write a plugin, it's only accepted in the core where all existing color families may be known
    if (nclips > outplanes)
        RETERROR("ShufflePlanes: 1-3 clips need to be specified");

    if (nplanes > outplanes)
        RETERROR("ShufflePlanes: too many planes specified");

    for (int i = 0; i < nplanes; i++)
        d->plane[i] = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

    for (int i = 0; i < 3; i++)
        d->node[i] = vsapi->propGetNode(in, "clips", i, &err);

    for (int i = 0; i < 3; i++) {
        if (d->node[i] && isCompatFormat(&vsapi->getVideoInfo(d->node[i])->format))
            RETERROR("ShufflePlanes: compat formats not supported");
        if (d->node[i] && !isConstantVideoFormat(vsapi->getVideoInfo(d->node[i])))
            RETERROR("ShufflePlanes: only clips with constant format and dimensions supported");
    }

    if (d->format != cfGray && nclips == 1) {
        d->node[1] = vsapi->cloneNodeRef(d->node[0]);
        d->node[2] = vsapi->cloneNodeRef(d->node[0]);
    } else if (d->format != cfGray && nclips == 2) {
        d->node[2] = vsapi->cloneNodeRef(d->node[1]);
    }

    for (int i = 0; i < outplanes; i++) {
        if (d->plane[i] < 0 || (vsapi->getVideoInfo(d->node[i])->format.colorFamily != cfUndefined && d->plane[i] >= vsapi->getVideoInfo(d->node[i])->format.numPlanes))
            RETERROR("ShufflePlanes: invalid plane specified");
    }

    d->vi = *vsapi->getVideoInfo(d->node[0]);

    // compatible format checks
    if (d->format == cfGray) {
        // gray is always compatible and special, it can work with variable input size clips
        if (d->vi.format.colorFamily != cfUndefined)
             vsapi->queryVideoFormat(&d->vi.format, cfGray, d->vi.format.sampleType, d->vi.format.bitsPerSample, 0, 0, core);
        d->vi.width = planeWidth(vsapi->getVideoInfo(d->node[0]), d->plane[0]);
        d->vi.height = planeHeight(vsapi->getVideoInfo(d->node[0]), d->plane[0]);
    } else {
        // no variable size video with more than one plane, it's just crazy
        int c0height = planeHeight(vsapi->getVideoInfo(d->node[0]), d->plane[0]);
        int c0width = planeWidth(vsapi->getVideoInfo(d->node[0]), d->plane[0]);
        int c1height = planeHeight(vsapi->getVideoInfo(d->node[1]), d->plane[1]);
        int c1width = planeWidth(vsapi->getVideoInfo(d->node[1]), d->plane[1]);
        int c2height = planeHeight(vsapi->getVideoInfo(d->node[2]), d->plane[2]);
        int c2width = planeWidth(vsapi->getVideoInfo(d->node[2]), d->plane[2]);

        d->vi.width = c0width;
        d->vi.height = c0height;

        if (c1width != c2width || c1height != c2height)
            RETERROR("ShufflePlanes: plane 1 and 2 do not have the same size");

        int ssH = findSubSampling(c0height, c1height);
        int ssW = findSubSampling(c0width, c1width);

        if (ssH < 0 || ssW < 0)
            RETERROR("ShufflePlanes: Plane 1 and 2 are not subsampled multiples of first plane");

        for (int i = 1; i < 3; i++) {
            const VSVideoInfo *pvi = vsapi->getVideoInfo(d->node[i]);

            if (d->vi.numFrames < pvi->numFrames)
                d->vi.numFrames = pvi->numFrames;

            // simple binary compatibility
            if (d->vi.format.bitsPerSample != pvi->format.bitsPerSample ||
                d->vi.format.sampleType != pvi->format.sampleType)
                RETERROR("ShufflePlanes: plane 1 and 2 do not have binary compatible storage");
        }

        if (d->format == cfRGB && (ssH != 0 || ssW != 0))
            RETERROR("ShufflePlanes: subsampled RGB not allowed");

        vsapi->queryVideoFormat(&d->vi.format, d->format, d->vi.format.sampleType, d->vi.format.bitsPerSample, ssW, ssH, core);
    }

    vsapi->createVideoFilter(out, "ShufflePlanes", &d->vi, 1, shufflePlanesGetframe, filterFree<ShufflePlanesData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SeparateFields

typedef struct {
    VSVideoInfo vi;
    int tff;
    bool modifyDuration;
} SeparateFieldsDataExtra;

typedef SingleNodeData<SeparateFieldsDataExtra> SeparateFieldsData;

static const VSFrameRef *VS_CC separateFieldsGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = reinterpret_cast<SeparateFieldsData *>(instanceData);

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
            return nullptr;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);

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
                vs_muldivRational(&durationNum, &durationDen, 1, 2); // Divide duration by 2
                vsapi->propSetInt(dst_props, "_DurationNum", durationNum, paReplace);
                vsapi->propSetInt(dst_props, "_DurationDen", durationDen, paReplace);
            }
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC separateFieldsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SeparateFieldsData> d(new SeparateFieldsData(vsapi));

    int err;
    d->tff = !!vsapi->propGetInt(in, "tff", 0, &err);
    if (err)
        d->tff = -1;
    d->modifyDuration = !!vsapi->propGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = 1;
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("SeparateFields: clip must have constant format and dimensions");

    if (d->vi.height % (1 << (d->vi.format.subSamplingH + 1)))
        RETERROR("SeparateFields: clip height must be mod 2 in the smallest subsampled plane");

    if (d->vi.numFrames > INT_MAX / 2)
        RETERROR("SeparateFields: resulting clip is too long");

    d->vi.numFrames *= 2;
    d->vi.height /= 2;

    if (d->modifyDuration)
        vs_muldivRational(&d->vi.fpsNum, &d->vi.fpsDen, 2, 1);

    vsapi->createVideoFilter(out, "SeparateFields", &d->vi, 1, separateFieldsGetframe, filterFree<SeparateFieldsData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// DoubleWeave

typedef struct {
    VSVideoInfo vi;
    int tff;
} DoubleWeaveDataExtra;

typedef SingleNodeData<DoubleWeaveDataExtra> DoubleWeaveData;

static const VSFrameRef *VS_CC doubleWeaveGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = reinterpret_cast<DoubleWeaveData *>(instanceData);

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

        const VSFrameRef *srctop = nullptr;
        const VSFrameRef *srcbtn = nullptr;

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
            return nullptr;
        }

        VSFrameRef *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src1, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);
        VSMap *dstprops = vsapi->getFramePropsRW(dst);
        vsapi->propDeleteKey(dstprops, "_Field");
        vsapi->propSetInt(dstprops, "_FieldBased", 1 + (srctop == src1), paReplace);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcptop = vsapi->getReadPtr(srctop, plane);
            const uint8_t *srcpbtn = vsapi->getReadPtr(srcbtn, plane);
            ptrdiff_t src_stride = vsapi->getStride(srcbtn, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
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

    return nullptr;
}

static void VS_CC doubleWeaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DoubleWeaveData> d(new DoubleWeaveData(vsapi));

    int err;
    d->tff = !!vsapi->propGetInt(in, "tff", 0, &err);
    if (err)
        d->tff = -1;
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);
    d->vi.height *= 2;

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("DoubleWeave: clip must have constant format and dimensions");

    vsapi->createVideoFilter(out, "DoubleWeave", &d->vi, 1, doubleWeaveGetframe, filterFree<DoubleWeaveData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FlipVertical

typedef SingleNodeData<NoExtraData> FlipVeritcalData;

static const VSFrameRef *VS_CC flipVerticalGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FlipVeritcalData *d = reinterpret_cast<FlipVeritcalData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int height = vsapi->getFrameHeight(src, plane);
            dstp += dst_stride * (height - 1);
            vs_bitblt(dstp, -dst_stride, srcp, src_stride, vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample, height);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC flipVerticalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FlipVeritcalData> d(new FlipVeritcalData(vsapi));
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    vsapi->createVideoFilter(out, "FlipVertical", vsapi->getVideoInfo(d->node), 1, flipVerticalGetframe, filterFree<FlipVeritcalData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FlipHorizontal

typedef struct {
    bool flip;
} FlipHorizontalDataExtra;

typedef SingleNodeData<FlipHorizontalDataExtra> FlipHorizontalData;

static const VSFrameRef *VS_CC flipHorizontalGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    // optimize, pshufb, pshufw, palignr could make flipping a lot faster
    FlipHorizontalData *d = reinterpret_cast<FlipHorizontalData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t * VS_RESTRICT srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
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
                return nullptr;
            }

        }

        vsapi->freeFrame(src);
        return dst;

    }

    return nullptr;
}

static void VS_CC flipHorizontalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FlipHorizontalData> d(new FlipHorizontalData(vsapi));
    d->flip = !!userData;
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    vsapi->createVideoFilter(out, d->flip ? "Turn180" : "FlipHorizontal", vsapi->getVideoInfo(d->node), 1, flipHorizontalGetframe, filterFree<FlipHorizontalData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Stack

typedef struct {
    VSVideoInfo vi;
    bool vertical;
} StackDataExtra;

typedef VariableNodeData<StackDataExtra> StackData;

static const VSFrameRef *VS_CC stackGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    StackData *d = reinterpret_cast<StackData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter: d->node)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
        vsapi->freeFrame(src);

        for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);

            for (auto iter : d->node) {
                src = vsapi->getFrameFilter(n, iter, frameCtx);

                if (d->vertical) {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    size_t size = dst_stride * vsapi->getFrameHeight(src, plane);
                    memcpy(dstp, srcp, size);
                    dstp += size;
                } else {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    ptrdiff_t src_stride = vsapi->getStride(src, plane);
                    size_t rowsize = vsapi->getFrameWidth(src, plane) * d->vi.format.bytesPerSample;
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

    return nullptr;
}

static void VS_CC stackCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<StackData> d(new StackData(vsapi));

    d->vertical = !!userData;
    int numclips = vsapi->propNumElements(in, "clips");

    if (numclips == 1) { // passthrough for the special case with only one clip
        VSNodeRef *node = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", node, paReplace);
        vsapi->freeNode(node);
    } else {
        d->node.resize(numclips);

        for (int i = 0; i < numclips; i++)
            d->node[i] = vsapi->propGetNode(in, "clips", i, 0);

        d->vi = *vsapi->getVideoInfo(d->node[0]);
        if (isConstantVideoFormat(&d->vi) && isCompatFormat(&d->vi.format) && d->vertical)
            RETERROR("StackVertical: compat formats aren't supported");

        for (int i = 1; i < numclips; i++) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(d->node[i]);

            if (d->vi.numFrames < vi->numFrames)
                d->vi.numFrames = vi->numFrames;

            if (!isConstantVideoFormat(vi) || !isSameVideoFormat(&vi->format, &d->vi.format) || (d->vertical && vi->width != d->vi.width) || (!d->vertical && vi->height != d->vi.height)) {
                if (d->vertical) {
                    RETERROR("StackVertical: clip format and width must match");
                } else {
                    RETERROR("StackHorizontal: clip format and height must match");
                }
            }

            if (d->vertical)
                d->vi.height += vi->height;
            else
                d->vi.width += vi->width;
        }

        vsapi->createVideoFilter(out, d->vertical ? "StackVertical" : "StackHorizontal", &d->vi, 1, stackGetframe, filterFree<StackData>, fmParallel, 0, d.get(), core);
        d.release();
    }
}

//////////////////////////////////////////
// BlankClip

typedef struct {
    VSFrameRef *f;
    VSVideoInfo vi;
    uint32_t color[3];
    bool keep;
} BlankClipData;

static const VSFrameRef *VS_CC blankClipGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = reinterpret_cast<BlankClipData *>(instanceData);

    if (activationReason == arInitial) {
        VSFrameRef *frame = nullptr;
        if (!d->f) {
            frame = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, 0, core);
            int bytesPerSample = (d->vi.format.colorFamily == cfCompatYUY2) ? 4 : d->vi.format.bytesPerSample;

            for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
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

    return nullptr;
}

static void VS_CC blankClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = reinterpret_cast<BlankClipData *>(instanceData);
    vsapi->freeFrame(d->f);
    delete d;
}

static void VS_CC blankClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BlankClipData> d(new BlankClipData());
    bool hasvi = false;
    int64_t temp;
    int err;

    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, &err);

    if (!err) {
        d->vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
        hasvi = true;
    }

    temp = vsapi->propGetInt(in, "width", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.width = 640;
    } else {
        d->vi.width = int64ToIntS(temp);
    }

    temp = vsapi->propGetInt(in, "height", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.height = 480;
    } else {
        d->vi.height = int64ToIntS(temp);
    }

    temp = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.fpsNum = 24;
    } else {
        d->vi.fpsNum = temp;
    }

    temp = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.fpsDen = 1;
    } else
        d->vi.fpsDen = temp;

    if (d->vi.fpsDen < 0 || d->vi.fpsNum < 0)
        RETERROR("BlankClip: invalid framerate specified");

    if (d->vi.fpsDen == 0 || d->vi.fpsNum == 0) {
        d->vi.fpsNum = 0;
        d->vi.fpsDen = 0;
    }

    vs_reduceRational(&d->vi.fpsNum, &d->vi.fpsDen);

    int format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));

    if (err) {
        if (!hasvi)
            vsapi->queryVideoFormat(&d->vi.format, cfRGB, stInteger, 8, 0, 0, core);
    } else {
        vsapi->queryVideoFormatByID(&d->vi.format, format, core);
    }

    if (isUndefinedFormat(&d->vi.format))
        RETERROR("BlankClip: invalid format");

    if (isCompatFormat(&d->vi.format))
        RETERROR("BlankClip: compat formats not supported");

    temp = vsapi->propGetInt(in, "length", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.numFrames = int64ToIntS((d->vi.fpsNum * 10) / d->vi.fpsDen);
    } else {
        d->vi.numFrames = int64ToIntS(temp);
    }

    if (d->vi.width <= 0 || d->vi.width % (1 << d->vi.format.subSamplingW))
        RETERROR("BlankClip: invalid width");

    if (d->vi.height <= 0 || d->vi.height % (1 << d->vi.format.subSamplingH))
        RETERROR("BlankClip: invalid height");

    if (d->vi.numFrames <= 0)
        RETERROR("BlankClip: invalid length");

    setBlack(d->color, &d->vi.format);

    int numcomponents = isCompatFormat(&d->vi.format) ? 3 : d->vi.format.numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->propGetFloat(in, "color", i, 0);
            if (d->vi.format.sampleType == stInteger) {
                d->color[i] = doubleToIntPixelValue(color, d->vi.format.bitsPerSample, &err);
            } else {
                if (d->vi.format.bitsPerSample == 16)
                    d->color[i] = doubleToHalfPixelValue(color, &err);
                else
                    d->color[i] = doubleToFloatPixelValue(color, &err);
            }
            if (err)
                RETERROR("BlankClip: color value out of range");
        }
    } else if (ncolors > 0) {
        RETERROR("BlankClip: invalid number of color values specified");
    }

    d->keep = !!vsapi->propGetInt(in, "keep", 0, &err);

    vsapi->createVideoFilter(out, "BlankClip", &d->vi, 1, blankClipGetframe, blankClipFree, d->keep ? fmUnordered : fmParallel, nfNoCache, d.get(), core);
    d.release();
}


//////////////////////////////////////////
// AssumeFPS

typedef struct {
    VSVideoInfo vi;
} AssumeFPSDataExtra;

typedef SingleNodeData<AssumeFPSDataExtra> AssumeFPSData;

static const VSFrameRef *VS_CC assumeFPSGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = reinterpret_cast<AssumeFPSData *>(instanceData);

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

    return nullptr;
}

static void VS_CC assumeFPSCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AssumeFPSData> d(new AssumeFPSData(vsapi));
    bool hasfps = false;
    bool hassrc = false;
    int err;

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);

    d->vi.fpsNum = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (!err)
        hasfps = true;

    d->vi.fpsDen = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err)
        d->vi.fpsDen = 1;

    VSNodeRef *src = vsapi->propGetNode(in, "src", 0, &err);

    if (!err) {
        const VSVideoInfo *vi = vsapi->getVideoInfo(src);
        d->vi.fpsNum = vi->fpsNum;
        d->vi.fpsDen = vi->fpsDen;
        vsapi->freeNode(src);
        hassrc = true;
    }

    if ((hasfps && hassrc) || (!hasfps && !hassrc))
        RETERROR("AssumeFPS: need to specify source clip or fps");

    if (d->vi.fpsDen < 1 || d->vi.fpsNum < 1)
        RETERROR("AssumeFPS: invalid framerate specified");

    vs_reduceRational(&d->vi.fpsNum, &d->vi.fpsDen);

    vsapi->createVideoFilter(out, "AssumeFPS", &d->vi, 1, assumeFPSGetframe, filterFree<AssumeFPSData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FrameEval

typedef struct {
    VSVideoInfo vi;
    VSFuncRef *func;
    std::vector<VSNodeRef *> propsrc;
    VSMap *in;
    VSMap *out;
} FrameEvalData;

static const VSFrameRef *VS_CC frameEvalGetFrameWithProps(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter : d->propsrc)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady && !*frameData) {
        int err;
        vsapi->propSetInt(d->in, "n", n, paAppend);
        for (auto iter : d->propsrc) {
            const VSFrameRef *f = vsapi->getFrameFilter(n, iter, frameCtx);
            vsapi->propSetFrame(d->in, "f", f, paAppend);
            vsapi->freeFrame(f);
        }
        vsapi->callFunc(d->func, d->in, d->out);
        vsapi->clearMap(d->in);
        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        VSNodeRef *node = vsapi->propGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return nullptr;
        }

        frameData[0] = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *frame;
        VSNodeRef *node = reinterpret_cast<VSNodeRef *>(frameData[0]);
        frame = vsapi->getFrameFilter(n, node, frameCtx);
        vsapi->freeNode(node);

        if (d->vi.width || d->vi.height) {
            if (d->vi.width != vsapi->getFrameWidth(frame, 0) || d->vi.height != vsapi->getFrameHeight(frame, 0)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong dimensions", frameCtx);
                return nullptr;
            }
        }

        if (d->vi.format.colorFamily != cfUndefined) {
            if (!isSameVideoFormat(&d->vi.format, vsapi->getVideoFrameFormat(frame))) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong format", frameCtx);
                return nullptr;
            }
        }
        return frame;
    } else if (activationReason == arError) {
        vsapi->freeNode(reinterpret_cast<VSNodeRef *>(frameData[0]));
    }

    return nullptr;
}

static const VSFrameRef *VS_CC frameEvalGetFrameNoProps(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);

    if (activationReason == arInitial) {

        int err;
        vsapi->propSetInt(d->in, "n", n, paAppend);
        vsapi->callFunc(d->func, d->in, d->out);
        vsapi->clearMap(d->in);
        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        VSNodeRef *node = vsapi->propGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return nullptr;
        }

        frameData[0] = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSNodeRef *node = reinterpret_cast<VSNodeRef *>(frameData[0]);
        const VSFrameRef *frame = vsapi->getFrameFilter(n, node, frameCtx);
        vsapi->freeNode(node);

        if (d->vi.width || d->vi.height) {
            if (d->vi.width != vsapi->getFrameWidth(frame, 0) || d->vi.height != vsapi->getFrameHeight(frame, 0)) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong dimensions", frameCtx);
                return nullptr;
            }
        }

        if (d->vi.format.colorFamily != cfUndefined) {
            if (!isSameVideoFormat(&d->vi.format, vsapi->getVideoFrameFormat(frame))) {
                vsapi->freeFrame(frame);
                vsapi->setFilterError("FrameEval: Returned frame has wrong format", frameCtx);
                return nullptr;
            }
        }
        return frame;
    } else if (activationReason == arError) {
        vsapi->freeNode(reinterpret_cast<VSNodeRef *>(frameData[0]));
    }

    return nullptr;
}

static void VS_CC frameEvalFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);
    for (auto iter : d->propsrc)
        vsapi->freeNode(iter);
    vsapi->freeFunc(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    delete d;
}

static void VS_CC frameEvalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FrameEvalData> d(new FrameEvalData());
    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(node);
    vsapi->freeNode(node);
    d->func = vsapi->propGetFunc(in, "eval", 0, 0);
    int numpropsrc = vsapi->propNumElements(in, "prop_src");
    if (numpropsrc > 0) {
        d->propsrc.resize(numpropsrc);
        for (int i = 0; i < numpropsrc; i++)
            d->propsrc[i] = vsapi->propGetNode(in, "prop_src", i, 0);
    }

    d->in = vsapi->createMap();
    d->out = vsapi->createMap();

    vsapi->createVideoFilter(out, "FrameEval", &d->vi, 1, (d->propsrc.size() > 0) ? frameEvalGetFrameWithProps : frameEvalGetFrameNoProps, frameEvalFree, (d->propsrc.size() > 0) ? fmParallelRequests : fmUnordered, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// ModifyFrame

typedef struct {
    std::vector<VSNodeRef *> node;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    VSMap *in;
    VSMap *out;
} ModifyFrameData;

static const VSFrameRef *VS_CC modifyFrameGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = reinterpret_cast<ModifyFrameData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter : d->node)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;

        vsapi->propSetInt(d->in, "n", n, paAppend);

        for (auto iter : d->node) {
            const VSFrameRef *f = vsapi->getFrameFilter(n, iter, frameCtx);
            vsapi->propSetFrame(d->in, "f", f, paAppend);
            vsapi->freeFrame(f);
        }

        vsapi->callFunc(d->func, d->in, d->out);
        vsapi->clearMap(d->in);

        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        const VSFrameRef *f = vsapi->propGetFrame(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);
        if (err) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned value not a frame", frameCtx);
            return nullptr;
        }

        if (d->vi->format.colorFamily != cfUndefined && !isSameVideoFormat(&d->vi->format, vsapi->getVideoFrameFormat(f))) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong format", frameCtx);
            return nullptr;
        }

        if ((d->vi->width || d->vi->height) && (d->vi->width != vsapi->getFrameWidth(f, 0) || d->vi->height != vsapi->getFrameHeight(f, 0))) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong dimensions", frameCtx);
            return nullptr;
        }

        return f;
    }

    return nullptr;
}

static void VS_CC modifyFrameFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = reinterpret_cast<ModifyFrameData *>(instanceData);
    for (auto iter : d->node)
        vsapi->freeNode(iter);
    vsapi->freeFunc(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    delete d;
}

static void VS_CC modifyFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ModifyFrameData> d(new ModifyFrameData());
    VSNodeRef *formatnode = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(formatnode);
    vsapi->freeNode(formatnode);

    int numnode = vsapi->propNumElements(in, "clips");
    d->node.resize(numnode);
    for (int i = 0; i < numnode; i++)
        d->node[i] = vsapi->propGetNode(in, "clips", i, 0);

    d->func = vsapi->propGetFunc(in, "selector", 0, 0);
    d->in = vsapi->createMap();
    d->out = vsapi->createMap();

    vsapi->createVideoFilter(out, "ModifyFrame", d->vi, 1, modifyFrameGetFrame, modifyFrameFree, fmParallelRequests, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Transpose

typedef struct {
    VSVideoInfo vi;
    int cpulevel;
} TransposeDataExtra;

typedef SingleNodeData<TransposeDataExtra> TransposeData;

static const VSFrameRef *VS_CC transposeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = reinterpret_cast<TransposeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
        int width;
        int height;
        const uint8_t * VS_RESTRICT srcp;
        ptrdiff_t src_stride;
        uint8_t * VS_RESTRICT dstp;
        ptrdiff_t dst_stride;

        void (*func)(const void *, ptrdiff_t, void *, ptrdiff_t, unsigned, unsigned) = nullptr;

#ifdef VS_TARGET_CPU_X86
        if (d->cpulevel >= VS_CPU_LEVEL_SSE2) {
            switch (d->vi.format.bytesPerSample) {
            case 1: func = vs_transpose_plane_byte_sse2; break;
            case 2: func = vs_transpose_plane_word_sse2; break;
            case 4: func = vs_transpose_plane_dword_sse2; break;
            }
        }
#endif
        if (!func) {
            switch (d->vi.format.bytesPerSample) {
            case 1: func = vs_transpose_plane_byte_c; break;
            case 2: func = vs_transpose_plane_word_c; break;
            case 4: func = vs_transpose_plane_dword_c; break;
            }
        }

        for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
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

    return nullptr;
}

static void VS_CC transposeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TransposeData> d(new TransposeData(vsapi));
    int temp;

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);
    temp = d->vi.width;
    d->vi.width = d->vi.height;
    d->vi.height = temp;

    if (!isConstantVideoFormat(&d->vi) || d->vi.format.colorFamily == cfCompatYUY2)
        RETERROR("Transpose: clip must have constant format and dimensions and must not be CompatYUY2");

    vsapi->queryVideoFormat(&d->vi.format, d->vi.format.colorFamily, d->vi.format.sampleType, d->vi.format.bitsPerSample, d->vi.format.subSamplingH, d->vi.format.subSamplingW, core);
    d->cpulevel = vs_get_cpulevel(core);

    vsapi->createVideoFilter(out, "Transpose", &d->vi, 1, transposeGetFrame, filterFree<TransposeData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// PEM(Level)Verifier

typedef struct {
    int upper[3];
    int lower[3];
    float upperf[3];
    float lowerf[3];
} PEMVerifierDataExtra;

typedef SingleNodeData<PEMVerifierDataExtra> PEMVerifierData;

static const VSFrameRef *VS_CC pemVerifierGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = reinterpret_cast<PEMVerifierData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        char strbuf[512];

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            int width = vsapi->getFrameWidth(src, plane);
            int height = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            float f;
            uint16_t v;

            switch (fi->bytesPerSample) {
            case 1:
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++)
                        if (srcp[x] < d->lower[plane] || srcp[x] > d->upper[plane]) {
                            snprintf(strbuf, sizeof(strbuf), "PEMVerifier: Illegal sample value (%d) at: plane: %d Y: %d, X: %d, Frame: %d", (int)srcp[x], plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return nullptr;
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
                            return nullptr;
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
                            return nullptr;
                        }
                    }
                    srcp += src_stride;
                }
                break;
            }
        }
        return src;
    }
    return nullptr;
}

static void VS_CC pemVerifierCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PEMVerifierData> d(new PEMVerifierData(vsapi));
    int numupper = vsapi->propNumElements(in, "upper");
    int numlower = vsapi->propNumElements(in, "lower");

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    if (!is8to16orFloatFormatCheck(vi->format))
        RETERROR("PEMVerifier: clip must be constant format and of integer 8-16 bit type or 32 bit float");

    if (numlower < 0) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->lower[i] = 0;
            d->lowerf[i] = (vi->format.colorFamily == cfYUV && i) ? -0.5f : 0.0f;
        }
    } else if (numlower == vi->format.numPlanes) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->lowerf[i] = (float)vsapi->propGetFloat(in, "lower", i, 0);
            d->lower[i] = floatToIntS(d->lowerf[i]);
            if (vi->format.sampleType == stInteger && (d->lower[i] < 0 || d->lower[i] >= (1 << vi->format.bitsPerSample)))
                RETERROR("PEMVerifier: Invalid lower bound given");
        }
    } else {
        RETERROR("PEMVerifier: number of lower plane limits does not match the number of planes");
    }

    if (numupper < 0) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->upper[i] = (1 << vi->format.bitsPerSample) - 1;
            d->upperf[i] = (vi->format.colorFamily == cfYUV && i) ? 0.5f : 1.0f;
        }
    } else if (numupper == vi->format.numPlanes) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->upperf[i] = (float)vsapi->propGetFloat(in, "upper", i, 0);
            d->upper[i] = floatToIntS(d->upperf[i]);
            if (vi->format.sampleType == stInteger && (d->upper[i] < d->lower[i] || d->upper[i] >= (1 << vi->format.bitsPerSample))) {
                RETERROR("PEMVerifier: Invalid upper bound given");
            } else if (vi->format.sampleType == stFloat && (d->upperf[i] < d->lowerf[i])) {
                RETERROR("PEMVerifier: Invalid upper bound given");
            }
        }
    } else {
        RETERROR("PEMVerifier: number of upper plane limits does not match the number of planes");
    }

    vsapi->createVideoFilter(out, "PEMVerifier", vi, 1, pemVerifierGetFrame, filterFree<PEMVerifierData>, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// PlaneStats

typedef struct {
    std::string propAverage;
    std::string propMin;
    std::string propMax;
    std::string propDiff;
    int plane;
    int cpulevel;
} PlaneStatsDataExtra;

typedef DualNodeData<PlaneStatsDataExtra> PlaneStatsData;

static const VSFrameRef *VS_CC planeStatsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData *d = reinterpret_cast<PlaneStatsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        if (d->node2)
            vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = d->node2 ? vsapi->getFrameFilter(n, d->node2, frameCtx) : nullptr;
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);
        int width = vsapi->getFrameWidth(src1, d->plane);
        int height = vsapi->getFrameHeight(src1, d->plane);
        const uint8_t *srcp = vsapi->getReadPtr(src1, d->plane);
        ptrdiff_t src_stride = vsapi->getStride(src1, d->plane);
        union vs_plane_stats stats = { 0 };

        if (src2) {
            const void *srcp2 = vsapi->getReadPtr(src2, d->plane);
            ptrdiff_t src2_stride = vsapi->getStride(src2, d->plane);
            void (*func)(union vs_plane_stats *, const void *, ptrdiff_t, const void *, ptrdiff_t, unsigned, unsigned) = nullptr;

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
            void (*func)(union vs_plane_stats *, const void *, ptrdiff_t, unsigned, unsigned) = nullptr;

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
            vsapi->propSetInt(dstProps, d->propMin.c_str(), stats.i.min, paReplace);
            vsapi->propSetInt(dstProps, d->propMax.c_str(), stats.i.max, paReplace);
        } else {
            vsapi->propSetFloat(dstProps, d->propMin.c_str(), stats.f.min, paReplace);
            vsapi->propSetFloat(dstProps, d->propMax.c_str(), stats.f.max, paReplace);
        }

        double avg = 0.0;
        double diff = 0.0;
        if (fi->sampleType == stInteger) {
            avg = stats.i.acc / (double)((int64_t)width * height * (((int64_t)1 << fi->bitsPerSample) - 1));
            if (d->node2)
                diff = stats.i.diffacc / (double)((int64_t)width * height * (((int64_t)1 << fi->bitsPerSample) - 1));
        } else {
            avg = stats.f.acc / (double)((int64_t)width * height);
            if (d->node2)
                diff = stats.f.diffacc / (double)((int64_t)width * height);
        }

        vsapi->propSetFloat(dstProps, d->propAverage.c_str(), avg, paReplace);
        if (d->node2)
            vsapi->propSetFloat(dstProps, d->propDiff.c_str(), diff, paReplace);

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }
    return nullptr;
}

static void VS_CC planeStatsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PlaneStatsData> d(new PlaneStatsData(vsapi));
    int err;

    d->node1 = vsapi->propGetNode(in, "clipa", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormatCheck(vi->format))
        RETERROR("PlaneStats: clip must be constant format and of integer 8-16 bit type or 32 bit float");

    d->plane = int64ToIntS(vsapi->propGetInt(in, "plane", 0, &err));
    if (d->plane < 0 || d->plane >= vi->format.numPlanes)
        RETERROR("PlaneStats: invalid plane specified");

    d->node2 = vsapi->propGetNode(in, "clipb", 0, &err);
    if (d->node2) {
        if (!isSameVideoInfo(vi, vsapi->getVideoInfo(d->node2)) || !isConstantVideoFormat(vsapi->getVideoInfo(d->node2)))
            RETERROR("PlaneStats: both input clips must have the same format when second clip is used");
    }

    const char *tmpprop = vsapi->propGetData(in, "prop", 0, &err);
    std::string tempprop = tmpprop ? tmpprop : "PlaneStats";
    d->propMin = tempprop + "Min";
    d->propMax = tempprop + "Max";
    d->propAverage = tempprop + "Average";
    d->propDiff = tempprop + "Diff";
    d->cpulevel = vs_get_cpulevel(core);

    vsapi->createVideoFilter(out, "PlaneStats", vi, 1, planeStatsGetFrame, filterFree<PlaneStatsData>, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// ClipToProp

typedef struct {
    std::string prop;
} ClipToPropDataExtra;

typedef DualNodeData<ClipToPropDataExtra> ClipToPropData;

static const VSFrameRef *VS_CC clipToPropGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = reinterpret_cast<ClipToPropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        vsapi->propSetFrame(vsapi->getFramePropsRW(dst), d->prop.c_str(), src2, paReplace);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC clipToPropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ClipToPropData> d(new ClipToPropData(vsapi));
    int err;

    d->node1 = vsapi->propGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);
    d->node2 = vsapi->propGetNode(in, "mclip", 0, 0);

    if (!isConstantVideoFormat(vi) || !isConstantVideoFormat(vsapi->getVideoInfo(d->node2)))
        RETERROR("ClipToProp: clips must have constant format and dimensions");

    const char *tmpprop = vsapi->propGetData(in, "prop", 0, &err);
    d->prop = tmpprop ? tmpprop : "_Alpha";

    vsapi->createVideoFilter(out, "ClipToProp", vi, 1, clipToPropGetFrame, filterFree<ClipToPropData>, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// PropToClip

typedef struct {
    VSVideoInfo vi;
    std::string prop;
} PropToClipDataExtra;

typedef SingleNodeData<PropToClipDataExtra> PropToClipData;

static const VSFrameRef *VS_CC propToClipGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = reinterpret_cast<PropToClipData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *dst = vsapi->propGetFrame(vsapi->getFramePropsRO(src), d->prop.c_str(), 0, &err);
        vsapi->freeFrame(src);

        if (dst) {
            if (!isSameVideoFormat(&d->vi.format, vsapi->getVideoFrameFormat(dst)) || d->vi.height != vsapi->getFrameHeight(dst, 0) || d->vi.width != vsapi->getFrameWidth(dst, 0)) {
                vsapi->setFilterError("PropToClip: retrieved frame doesn't match output format or dimensions", frameCtx);
                return nullptr;
            }

            return dst;
        } else {
            vsapi->setFilterError("PropToClip: failed to extract frame from specified property", frameCtx);
            return nullptr;
        }
    }

    return nullptr;
}

static void VS_CC propToClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PropToClipData> d(new PropToClipData(vsapi));
    int err;
    char errmsg[512];

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("PropToClip: clip must have constant format and dimensions");

    const char *tempprop = vsapi->propGetData(in, "prop", 0, &err);
    d->prop = tempprop ? tempprop : "_Alpha";

    const VSFrameRef *src = vsapi->getFrame(0, d->node, errmsg, sizeof(errmsg));
    if (!src)
        RETERROR(("PropToClip: upstream error: " + std::string(errmsg)).c_str());

    const VSFrameRef *msrc = vsapi->propGetFrame(vsapi->getFramePropsRO(src), tempprop, 0, &err);
    if (err) {
        vsapi->freeFrame(src);
        RETERROR("PropToClip: no frame stored in property");
    }

    d->vi.format = *vsapi->getVideoFrameFormat(msrc);
    d->vi.width = vsapi->getFrameWidth(msrc, 0);
    d->vi.height = vsapi->getFrameHeight(msrc, 0);
    vsapi->freeFrame(msrc);
    vsapi->freeFrame(src);

    vsapi->createVideoFilter(out, "PropToClip", &d->vi, 1, propToClipGetFrame, filterFree<PropToClipData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetFrameProp

typedef struct {
    std::string prop;
    bool del;
    std::vector<int64_t> ints;
    std::vector<double> floats;
    std::vector<std::string> strings;
    std::vector<int> dataType;
} SetFramePropDataExtra;

typedef SingleNodeData<SetFramePropDataExtra> SetFramePropData;

static const VSFrameRef *VS_CC setFramePropGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData *d = reinterpret_cast<SetFramePropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropsRW(dst);

        if (d->del)
            vsapi->propDeleteKey(props, d->prop.c_str());
        else {
            if (!d->ints.empty())
                vsapi->propSetIntArray(props, d->prop.c_str(), d->ints.data(), static_cast<int>(d->ints.size()));
            else if (!d->floats.empty())
                vsapi->propSetFloatArray(props, d->prop.c_str(), d->floats.data(), static_cast<int>(d->floats.size()));
            else if (!d->strings.empty()) {
                for (size_t i = 0; i < d->strings.size(); i++)
                    vsapi->propSetData(props, d->prop.c_str(), d->strings[i].c_str(), static_cast<int>(d->strings[i].length()), d->dataType[i], i > 0 ? paAppend : paReplace);
            }
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC setFramePropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SetFramePropData> d(new SetFramePropData(vsapi));
    int err;

    d->del = !!vsapi->propGetInt(in, "delete", 0, &err);

    int num_ints = vsapi->propNumElements(in, "intval");
    int num_floats = vsapi->propNumElements(in, "floatval");
    int num_strings = vsapi->propNumElements(in, "data");

    if ((num_ints > -1) + (num_floats > -1) + (num_strings > -1) > 1)
        RETERROR("SetFrameProp: only one of 'intval', 'floatval', and 'data' can be passed->");

    if (d->del && (num_ints + num_floats + num_strings > -3))
        RETERROR("SetFrameProp: 'delete' can't be True when passing one of 'intval', 'floatval', or 'data'.");

    if (!d->del && (num_ints + num_floats + num_strings == -3))
        RETERROR("SetFrameProp: one of 'intval', 'floatval', or 'data' must be passed->");

    int prop_len = vsapi->propGetDataSize(in, "prop", 0, nullptr);

    if (prop_len == 0)
        RETERROR("SetFrameProp: 'prop' can't be an empty string.");

    d->prop = vsapi->propGetData(in, "prop", 0, nullptr);

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);

    if (num_ints > -1) {
        d->ints.resize(num_ints);
        memcpy(d->ints.data(), vsapi->propGetIntArray(in, "intval", nullptr), num_ints * sizeof(int64_t));
    }

    if (num_floats > -1) {
        d->floats.resize(num_floats);
        memcpy(d->floats.data(), vsapi->propGetFloatArray(in, "floatval", nullptr), num_floats * sizeof(double));
    }

    if (num_strings > -1) {
        d->strings.resize(num_strings);
        d->dataType.resize(num_strings);
        for (int i = 0; i < num_strings; i++) {
            d->strings[i] = std::string(vsapi->propGetData(in, "data", i, nullptr), vsapi->propGetDataSize(in, "data", i, nullptr));
            d->dataType[i] = vsapi->propGetDataType(in, "data", i, nullptr);
        }
    }

    vsapi->createVideoFilter(out, "SetFrameProp", vsapi->getVideoInfo(d->node), 1, setFramePropGetFrame, filterFree<SetFramePropData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetFieldBased

typedef struct {
    int64_t fieldbased;
} SetFieldBasedDataExtra;

typedef SingleNodeData<SetFieldBasedDataExtra> SetFieldBasedData;

static const VSFrameRef *VS_CC setFieldBasedGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFieldBasedData *d = reinterpret_cast<SetFieldBasedData *>(instanceData);

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

    return nullptr;
}

static void VS_CC setFieldBasedCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SetFieldBasedData> d(new SetFieldBasedData(vsapi));

    d->fieldbased = vsapi->propGetInt(in, "value", 0, nullptr);
    if (d->fieldbased < 0 || d->fieldbased > 2)
        RETERROR("SetFieldBased: value must be 0, 1 or 2");
    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);

    vsapi->createVideoFilter(out, "SetFieldBased", vsapi->getVideoInfo(d->node), 1, setFieldBasedGetFrame, filterFree<SetFieldBasedData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

static void VS_CC setMaxCpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *str = vsapi->propGetData(in, "cpu", 0, nullptr);
    int level = vs_cpulevel_from_str(str);
    level = vs_set_cpulevel(core, level);
    str = vs_cpulevel_to_str(level);
    vsapi->propSetData(out, "cpu", str, -1, dtUtf8, paReplace);
}

//////////////////////////////////////////
// Init

void VS_CC stdlibInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("CropAbs", "clip:vnode;width:int;height:int;left:int:opt;top:int:opt;x:int:opt;y:int:opt;", "clip:vnode;", cropAbsCreate, 0, plugin);
    vspapi->registerFunction("CropRel", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("Crop", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("AddBorders", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;color:float[]:opt;", "clip:vnode;", addBordersCreate, 0, plugin);
    vspapi->registerFunction("ShufflePlanes", "clips:vnode[];planes:int[];colorfamily:int;", "clip:vnode;", shufflePlanesCreate, 0, plugin);
    vspapi->registerFunction("SeparateFields", "clip:vnode;tff:int:opt;modify_duration:int:opt;", "clip:vnode;", separateFieldsCreate, 0, plugin);
    vspapi->registerFunction("DoubleWeave", "clip:vnode;tff:int:opt;", "clip:vnode;", doubleWeaveCreate, 0, plugin);
    vspapi->registerFunction("FlipVertical", "clip:vnode;", "clip:vnode;", flipVerticalCreate, 0, plugin);
    vspapi->registerFunction("FlipHorizontal", "clip:vnode;", "clip:vnode;", flipHorizontalCreate, 0, plugin);
    vspapi->registerFunction("Turn180", "clip:vnode;", "clip:vnode;", flipHorizontalCreate, (void *)1, plugin);
    vspapi->registerFunction("StackVertical", "clips:vnode[];", "clip:vnode;", stackCreate, (void *)1, plugin);
    vspapi->registerFunction("StackHorizontal", "clips:vnode[];", "clip:vnode;", stackCreate, 0, plugin);
    vspapi->registerFunction("BlankClip", "clip:vnode:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:float[]:opt;keep:int:opt;", "clip:vnode;", blankClipCreate, 0, plugin);
    vspapi->registerFunction("AssumeFPS", "clip:vnode;src:vnode:opt;fpsnum:int:opt;fpsden:int:opt;", "clip:vnode;", assumeFPSCreate, 0, plugin);
    vspapi->registerFunction("FrameEval", "clip:vnode;eval:func;prop_src:vnode[]:opt;", "clip:vnode;", frameEvalCreate, 0, plugin);
    vspapi->registerFunction("ModifyFrame", "clip:vnode;clips:vnode[];selector:func;", "clip:vnode;", modifyFrameCreate, 0, plugin);
    vspapi->registerFunction("Transpose", "clip:vnode;", "clip:vnode;", transposeCreate, 0, plugin);
    vspapi->registerFunction("PEMVerifier", "clip:vnode;upper:float[]:opt;lower:float[]:opt;", "clip:vnode;", pemVerifierCreate, 0, plugin);
    vspapi->registerFunction("PlaneStats", "clipa:vnode;clipb:vnode:opt;plane:int:opt;prop:data:opt;", "clip:vnode;", planeStatsCreate, 0, plugin);
    vspapi->registerFunction("ClipToProp", "clip:vnode;mclip:vnode;prop:data:opt;", "clip:vnode;", clipToPropCreate, 0, plugin);
    vspapi->registerFunction("PropToClip", "clip:vnode;prop:data:opt;", "clip:vnode;", propToClipCreate, 0, plugin);
    vspapi->registerFunction("SetFrameProp", "clip:vnode;prop:data;delete:int:opt;intval:int[]:opt;floatval:float[]:opt;data:data[]:opt;", "clip:vnode;", setFramePropCreate, 0, plugin);
    vspapi->registerFunction("SetFieldBased", "clip:vnode;value:int;", "clip:vnode;", setFieldBasedCreate, 0, plugin);
    vspapi->registerFunction("SetMaxCPU", "cpu:data;", "cpu:data;", setMaxCpu, 0, plugin);
}
