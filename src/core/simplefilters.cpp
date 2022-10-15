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

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "kernel/cpulevel.h"
#include "kernel/planestats.h"
#include "kernel/transpose.h"
#include "VapourSynth3.h" // only used for old colorfamily constant conversion in ShufflePlanes

using namespace vsh;

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
// Cache compatibility filter, does nothing

static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clip", 0, nullptr), maAppend);
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

static const VSFrame *VS_CC cropGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CropData *d = reinterpret_cast<CropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        char msg[150];
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);

        if (cropVerify(d->x, d->y, d->width, d->height, width, height, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return nullptr;
        }

        VSFrame *dst = vsapi->newVideoFrame(fi, d->width, d->height, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            ptrdiff_t srcstride = vsapi->getStride(src, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            srcdata += srcstride * (d->y >> (plane ? fi->subSamplingH : 0));
            srcdata += (d->x >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            bitblt(dstdata, dststride, srcdata, srcstride, (d->width >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample, vsapi->getFrameHeight(dst, plane));
        }

        vsapi->freeFrame(src);

        if (d->y & 1) {
            VSMap *props = vsapi->getFramePropertiesRW(dst);
            int error;
            int64_t fb = vsapi->mapGetInt(props, "_FieldBased", 0, &error);
            if (fb == VSC_FIELD_BOTTOM || fb == VSC_FIELD_TOP)
                vsapi->mapSetInt(props, "_FieldBased", (fb == VSC_FIELD_BOTTOM) ? VSC_FIELD_TOP : VSC_FIELD_BOTTOM, maReplace);
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC cropAbsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CropData> d(new CropData(vsapi));
    char msg[150];
    int err;

    d->x = vsapi->mapGetIntSaturated(in, "left", 0, &err);
    if (err)
        d->x = vsapi->mapGetIntSaturated(in, "x", 0, &err);
    d->y = vsapi->mapGetIntSaturated(in, "top", 0, &err);
    if (err)
        d->y = vsapi->mapGetIntSaturated(in, "y", 0, &err);

    d->height = vsapi->mapGetIntSaturated(in, "height", 0, 0);
    d->width = vsapi->mapGetIntSaturated(in, "width", 0, 0);
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    d->vi = vsapi->getVideoInfo(d->node);

    if (cropVerify(d->x, d->y, d->width, d->height, d->vi->width, d->vi->height, &d->vi->format, msg, sizeof(msg)))
        RETERROR(msg);

    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "Crop", &vi, cropGetframe, filterFree<CropData>, fmParallel, deps, 1, d.release(), core);
}

static void VS_CC cropRelCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CropData> d(new CropData(vsapi));
    char msg[150];
    int err;

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    if (!isConstantVideoFormat(d->vi))
        RETERROR("Crop: constant format and dimensions needed");

    d->x = vsapi->mapGetIntSaturated(in, "left", 0, &err);
    d->y = vsapi->mapGetIntSaturated(in, "top", 0, &err);

    d->height = d->vi->height - d->y - vsapi->mapGetIntSaturated(in, "bottom", 0, &err);
    d->width = d->vi->width - d->x - vsapi->mapGetIntSaturated(in, "right", 0, &err);

    // passthrough for the no cropping case
    if (d->x == 0 && d->y == 0 && d->width == d->vi->width && d->height == d->vi->height) {
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    if (cropVerify(d->x, d->y, d->width, d->height, d->vi->width, d->vi->height, &d->vi->format, msg, sizeof(msg)))
        RETERROR(msg);

    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "Crop", &vi, cropGetframe, filterFree<CropData>, fmParallel, deps, 1, d.release(), core);
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

static const VSFrame *VS_CC addBordersGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = reinterpret_cast<AddBordersData *>(instanceData);
    char msg[150];

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrame *dst;

        if (addBordersVerify(d->left, d->right, d->top, d->bottom, fi, msg, sizeof(msg))) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(msg, frameCtx);
            return nullptr;
        }

        dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0) + d->left + d->right, vsapi->getFrameHeight(src, 0) + d->top + d->bottom, src, core);

        int bytesPerSample = fi->bytesPerSample;

        // now that argument validation is over we can spend the next few lines actually adding borders
        for (int plane = 0; plane < fi->numPlanes; plane++) {
            int rowsize = vsapi->getFrameWidth(src, plane) * bytesPerSample;
            ptrdiff_t srcstride = vsapi->getStride(src, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            int srcheight = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            int padt = d->top >> (plane ? fi->subSamplingH : 0);
            int padb = d->bottom >> (plane ? fi->subSamplingH : 0);
            int padl = (d->left >> (plane ? fi->subSamplingW : 0)) * bytesPerSample;
            int padr = (d->right >> (plane ? fi->subSamplingW : 0)) * bytesPerSample;
            uint32_t color = d->color[plane];

            switch (bytesPerSample) {
            case 1:
                vs_memset<uint8_t>(dstdata, color, padt * dststride);
                break;
            case 2:
                vs_memset<uint16_t>(dstdata, color, padt * dststride / 2);
                break;
            case 4:
                vs_memset<uint32_t>(dstdata, color, padt * dststride / 4);
                break;
            }
            dstdata += padt * dststride;

            for (int hloop = 0; hloop < srcheight; hloop++) {
                switch (bytesPerSample) {
                case 1:
                    vs_memset<uint8_t>(dstdata, color, padl);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset<uint8_t>(dstdata + padl + rowsize, color, padr);
                    break;
                case 2:
                    vs_memset<uint16_t>(dstdata, color, padl / 2);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset<uint16_t>(dstdata + padl + rowsize, color, padr / 2);
                    break;
                case 4:
                    vs_memset<uint32_t>(dstdata, color, padl / 4);
                    memcpy(dstdata + padl, srcdata, rowsize);
                    vs_memset<uint32_t>(dstdata + padl + rowsize, color, padr / 4);
                    break;
                }

                dstdata += dststride;
                srcdata += srcstride;
            }

            switch (bytesPerSample) {
            case 1:
                vs_memset<uint8_t>(dstdata, color, padb * dststride);
                break;
            case 2:
                vs_memset<uint16_t>(dstdata, color, padb * dststride / 2);
                break;
            case 4:
                vs_memset<uint32_t>(dstdata, color, padb * dststride / 4);
                break;
            }
        }

        vsapi->freeFrame(src);

        if (d->top & 1) {
            VSMap *props = vsapi->getFramePropertiesRW(dst);
            int error;
            int64_t fb = vsapi->mapGetInt(props, "_FieldBased", 0, &error);
            if (fb == VSC_FIELD_BOTTOM || fb == VSC_FIELD_TOP)
                vsapi->mapSetInt(props, "_FieldBased", (fb == VSC_FIELD_BOTTOM) ? VSC_FIELD_TOP : VSC_FIELD_BOTTOM, maReplace);
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC addBordersCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AddBordersData> d(new AddBordersData(vsapi));
    char msg[150];
    int err;

    d->left = vsapi->mapGetIntSaturated(in, "left", 0, &err);
    d->right = vsapi->mapGetIntSaturated(in, "right", 0, &err);
    d->top = vsapi->mapGetIntSaturated(in, "top", 0, &err);
    d->bottom = vsapi->mapGetIntSaturated(in, "bottom", 0, &err);
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    // pass through if nothing to be done
    if (d->left == 0 && d->right == 0 && d->top == 0 && d->bottom == 0) {
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    if (d->left < 0 || d->right < 0 || d->top < 0 || d->bottom < 0)
        RETERROR("AddBorders: border size to add must not be negative");

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    if (vi.format.colorFamily == cfUndefined)
        RETERROR("AddBorders: input needs to be constant format");

    if (addBordersVerify(d->left, d->right, d->top, d->bottom, &vi.format, msg, sizeof(msg)))
        RETERROR(msg);

    int numcomponents = vi.format.numPlanes;
    int ncolors = vsapi->mapNumElements(in, "color");

    setBlack(d->color, &vi.format);

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->mapGetFloat(in, "color", i, 0);
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

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "AddBorders", &vi, addBordersGetframe, filterFree<AddBordersData>, fmParallel, deps, 1, d.release(), core);
}

//////////////////////////////////////////
// ShufflePlanes

typedef struct {
    VSVideoInfo vi;
    int plane[3];
    int format;
} ShufflePlanesDataExtra;

typedef VariableNodeData<ShufflePlanesDataExtra> ShufflePlanesData;

static const VSFrame *VS_CC shufflePlanesGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = reinterpret_cast<ShufflePlanesData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);

        if (d->nodes[1] && d->nodes[1] != d->nodes[0])
            vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);

        if (d->nodes[2] && d->nodes[2] != d->nodes[0] && d->nodes[2] != d->nodes[1])
            vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        if (d->vi.format.colorFamily != cfGray) {
            const VSFrame *src[3];
            VSFrame *dst;

            for (int i = 0; i < 3; i++)
                src[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);

            dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height, src, d->plane, src[0], core);

            for (int i = 0; i < 3; i++)
                vsapi->freeFrame(src[i]);

            return dst;
        } else {
            VSFrame *dst;
            const VSFrame *src = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
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
    int nclips = vsapi->mapNumElements(in, "clips");
    int nplanes = vsapi->mapNumElements(in, "planes");
    int err;

    d->nodes.resize(3);
    assert(d->plane[0] == 0);

    d->format = vsapi->mapGetIntSaturated(in, "colorfamily", 0, 0);
    
    if (d->format == vs3::cmGray)
        d->format = cfGray;
    else if (d->format == vs3::cmYUV || d->format == vs3::cmYCoCg)
        d->format = cfYUV;
    else if (d->format == vs3::cmRGB)
        d->format = cfRGB;

    if (d->format != cfRGB && d->format != cfYUV && d->format != cfGray)
        RETERROR("ShufflePlanes: invalid output colorfamily");

    int outplanes = (d->format == cfGray ? 1 : 3);

    // please don't make this assumption if you ever write a plugin, it's only accepted in the core where all existing color families may be known
    if (nclips > outplanes)
        RETERROR("ShufflePlanes: 1-3 clips need to be specified");

    if (nplanes > outplanes)
        RETERROR("ShufflePlanes: too many planes specified");

    for (int i = 0; i < nplanes; i++)
        d->plane[i] = vsapi->mapGetIntSaturated(in, "planes", i, 0);

    for (int i = 0; i < 3; i++)
        d->nodes[i] = vsapi->mapGetNode(in, "clips", i, &err);

    for (int i = 0; i < 3; i++) {
        if (d->nodes[i] && !isConstantVideoFormat(vsapi->getVideoInfo(d->nodes[i])))
            RETERROR("ShufflePlanes: only clips with constant format and dimensions supported");
    }

    if (d->format != cfGray && nclips == 1) {
        d->nodes[1] = vsapi->addNodeRef(d->nodes[0]);
        d->nodes[2] = vsapi->addNodeRef(d->nodes[0]);
    } else if (d->format != cfGray && nclips == 2) {
        d->nodes[2] = vsapi->addNodeRef(d->nodes[1]);
    }

    for (int i = 0; i < outplanes; i++) {
        if (d->plane[i] < 0 || (vsapi->getVideoInfo(d->nodes[i])->format.colorFamily != cfUndefined && d->plane[i] >= vsapi->getVideoInfo(d->nodes[i])->format.numPlanes))
            RETERROR("ShufflePlanes: invalid plane specified");
    }

    d->vi = *vsapi->getVideoInfo(d->nodes[0]);

    // compatible format checks
    if (d->format == cfGray) {
        // gray is always compatible and special, it can work with variable input size clips
        if (d->vi.format.colorFamily != cfUndefined)
             vsapi->queryVideoFormat(&d->vi.format, cfGray, d->vi.format.sampleType, d->vi.format.bitsPerSample, 0, 0, core);
        d->vi.width = planeWidth(vsapi->getVideoInfo(d->nodes[0]), d->plane[0]);
        d->vi.height = planeHeight(vsapi->getVideoInfo(d->nodes[0]), d->plane[0]);
    } else {
        // no variable size video with more than one plane, it's just crazy
        int c0height = planeHeight(vsapi->getVideoInfo(d->nodes[0]), d->plane[0]);
        int c0width = planeWidth(vsapi->getVideoInfo(d->nodes[0]), d->plane[0]);
        int c1height = planeHeight(vsapi->getVideoInfo(d->nodes[1]), d->plane[1]);
        int c1width = planeWidth(vsapi->getVideoInfo(d->nodes[1]), d->plane[1]);
        int c2height = planeHeight(vsapi->getVideoInfo(d->nodes[2]), d->plane[2]);
        int c2width = planeWidth(vsapi->getVideoInfo(d->nodes[2]), d->plane[2]);

        d->vi.width = c0width;
        d->vi.height = c0height;

        if (c1width != c2width || c1height != c2height)
            RETERROR("ShufflePlanes: plane 1 and 2 do not have the same size");

        int ssH = findSubSampling(c0height, c1height);
        int ssW = findSubSampling(c0width, c1width);

        if (ssH < 0 || ssW < 0)
            RETERROR("ShufflePlanes: plane 1 and 2 are not subsampled multiples of first plane");

        for (int i = 1; i < 3; i++) {
            const VSVideoInfo *pvi = vsapi->getVideoInfo(d->nodes[i]);

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

    if (d->format == cfGray) {
        VSFilterDependency deps1[] = {{ d->nodes[0], rpStrictSpatial }};
        vsapi->createVideoFilter(out, "ShufflePlanes", &d->vi, shufflePlanesGetframe, filterFree<ShufflePlanesData>, fmParallel, deps1, 1, d.get(), core);
    } else {
        VSFilterDependency deps3[] = {{ d->nodes[0], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[0])->numFrames) ? rpStrictSpatial : rpGeneral }, { d->nodes[1], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpGeneral }, { d->nodes[2], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpGeneral }};
        vsapi->createVideoFilter(out, "ShufflePlanes", &d->vi, shufflePlanesGetframe, filterFree<ShufflePlanesData>, fmParallel, deps3, 3, d.get(), core);
    }

    d.release();
}

//////////////////////////////////////////
// SplitPlanes

static void VS_CC splitPlanesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    if (vi->format.colorFamily == cfUndefined) {
        vsapi->mapSetError(out, "SplitPlanes: only constant format clips supported");
        return;
    }

    int numPlanes = vi->format.numPlanes;

    // Pass through when nothing to do
    if (numPlanes == 1) {
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
        return;
    }

    VSMap *map = vsapi->createMap();
    vsapi->mapConsumeNode(map, "clips", node, maAppend);
    vsapi->mapSetInt(map, "colorfamily", cfGray, maAppend);

    for (int i = 0; i < numPlanes; i++) {
        vsapi->mapSetInt(map, "planes", i, maReplace);
        VSMap *tmp = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShufflePlanes", map);
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(tmp, "clip", 0, nullptr), maAppend);
        vsapi->freeMap(tmp);
    }

    vsapi->freeMap(map);
}

//////////////////////////////////////////
// SeparateFields

typedef struct {
    VSVideoInfo vi;
    int tff;
    bool modifyDuration;
} SeparateFieldsDataExtra;

typedef SingleNodeData<SeparateFieldsDataExtra> SeparateFieldsData;

static const VSFrame *VS_CC separateFieldsGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = reinterpret_cast<SeparateFieldsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / 2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n / 2, d->node, frameCtx);
        const VSMap *props = vsapi->getFramePropertiesRO(src);
        int err = 0;
        int fieldBased = vsapi->mapGetIntSaturated(props, "_FieldBased", 0, &err);
        int effectiveTFF = d->tff;
        if (fieldBased == VSC_FIELD_BOTTOM)
            effectiveTFF = 0;
        else if (fieldBased == VSC_FIELD_TOP)
            effectiveTFF = 1;
        if (effectiveTFF == -1) {
            vsapi->setFilterError("SeparateFields: no field order provided", frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);

            if (!((n & 1) ^ effectiveTFF))
                srcp += src_stride;
            src_stride *= 2;

            bitblt(dstp, dst_stride, srcp, src_stride, vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample, vsapi->getFrameHeight(dst, plane));
        }

        vsapi->freeFrame(src);

        VSMap *dst_props = vsapi->getFramePropertiesRW(dst);
        vsapi->mapSetInt(dst_props, "_Field", ((n & 1) ^ effectiveTFF), maReplace);
        vsapi->mapDeleteKey(dst_props, "_FieldBased");

        if (d->modifyDuration) {
            int errNum, errDen;
            int64_t durationNum = vsapi->mapGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->mapGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                muldivRational(&durationNum, &durationDen, 1, 2); // Divide duration by 2
                vsapi->mapSetInt(dst_props, "_DurationNum", durationNum, maReplace);
                vsapi->mapSetInt(dst_props, "_DurationDen", durationDen, maReplace);
            }
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC separateFieldsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SeparateFieldsData> d(new SeparateFieldsData(vsapi));

    int err;
    d->tff = !!vsapi->mapGetInt(in, "tff", 0, &err);
    if (err)
        d->tff = -1;
    d->modifyDuration = !!vsapi->mapGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = 1;
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
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
        muldivRational(&d->vi.fpsNum, &d->vi.fpsDen, 2, 1);

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "SeparateFields", &d->vi, separateFieldsGetframe, filterFree<SeparateFieldsData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// DoubleWeave

typedef struct {
    VSVideoInfo vi;
    int tff;
} DoubleWeaveDataExtra;

typedef SingleNodeData<DoubleWeaveDataExtra> DoubleWeaveData;

static const VSFrame *VS_CC doubleWeaveGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = reinterpret_cast<DoubleWeaveData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n + 1, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n + 1, d->node, frameCtx);

        int err;
        int64_t src1_field = vsapi->mapGetInt(vsapi->getFramePropertiesRO(src1), "_Field", 0, &err);
        if (err)
            src1_field = -1;
        int64_t src2_field = vsapi->mapGetInt(vsapi->getFramePropertiesRO(src2), "_Field", 0, &err);
        if (err)
            src2_field = -1;

        const VSFrame *srctop = nullptr;
        const VSFrame *srcbtn = nullptr;

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

        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src1, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);
        VSMap *dstprops = vsapi->getFramePropertiesRW(dst);
        vsapi->mapDeleteKey(dstprops, "_Field");
        vsapi->mapSetInt(dstprops, "_FieldBased", 1 + (srctop == src1), maReplace);

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
    d->tff = !!vsapi->mapGetInt(in, "tff", 0, &err);
    if (err)
        d->tff = -1;
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);
    d->vi.height *= 2;

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("DoubleWeave: clip must have constant format and dimensions");

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "DoubleWeave", &d->vi, doubleWeaveGetframe, filterFree<DoubleWeaveData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FlipVertical

typedef SingleNodeData<NoExtraData> FlipVeritcalData;

static const VSFrame *VS_CC flipVerticalGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FlipVeritcalData *d = reinterpret_cast<FlipVeritcalData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int height = vsapi->getFrameHeight(src, plane);
            dstp += dst_stride * (height - 1);
            bitblt(dstp, -dst_stride, srcp, src_stride, vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample, height);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC flipVerticalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FlipVeritcalData> d(new FlipVeritcalData(vsapi));
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "FlipVertical", vsapi->getVideoInfo(d->node), flipVerticalGetframe, filterFree<FlipVeritcalData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FlipHorizontal

typedef struct {
    bool flip;
} FlipHorizontalDataExtra;

typedef SingleNodeData<FlipHorizontalDataExtra> FlipHorizontalData;

static const VSFrame *VS_CC flipHorizontalGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    // optimize, pshufb, pshufw, palignr could make flipping a lot faster
    FlipHorizontalData *d = reinterpret_cast<FlipHorizontalData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

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
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->flip ? "Turn180" : "FlipHorizontal", vsapi->getVideoInfo(d->node), flipHorizontalGetframe, filterFree<FlipHorizontalData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Stack

typedef struct {
    VSVideoInfo vi;
    bool vertical;
} StackDataExtra;

typedef VariableNodeData<StackDataExtra> StackData;

static const VSFrame *VS_CC stackGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    StackData *d = reinterpret_cast<StackData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter: d->nodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
        vsapi->freeFrame(src);

        for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);

            for (auto iter : d->nodes) {
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
                    bitblt(dstp, dst_stride,
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
    int numclips = vsapi->mapNumElements(in, "clips");

    if (numclips == 1) { // passthrough for the special case with only one clip
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clips", 0, 0), maReplace);
    } else {
        d->nodes.resize(numclips);

        for (int i = 0; i < numclips; i++)
            d->nodes[i] = vsapi->mapGetNode(in, "clips", i, 0);

        d->vi = *vsapi->getVideoInfo(d->nodes[0]);

        for (int i = 1; i < numclips; i++) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(d->nodes[i]);

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

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < numclips; i++)
            deps.push_back({d->nodes[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[i])->numFrames) ? rpStrictSpatial : rpGeneral});
        vsapi->createVideoFilter(out, d->vertical ? "StackVertical" : "StackHorizontal", &d->vi, stackGetframe, filterFree<StackData>, fmParallel, deps.data(), numclips, d.get(), core);
        d.release();
    }
}

//////////////////////////////////////////
// BlankClip

typedef struct {
    VSFrame *f;
    VSVideoInfo vi;
    uint32_t color[3];
    bool keep;
} BlankClipData;

static const VSFrame *VS_CC blankClipGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = reinterpret_cast<BlankClipData *>(instanceData);

    if (activationReason == arInitial) {
        VSFrame *frame = nullptr;
        if (!d->f) {
            frame = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, 0, core);
            int bytesPerSample = d->vi.format.bytesPerSample;

            for (int plane = 0; plane < d->vi.format.numPlanes; plane++) {
                switch (bytesPerSample) {
                case 1:
                    vs_memset<uint8_t>(vsapi->getWritePtr(frame, plane), d->color[plane], vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane));
                    break;
                case 2:
                    vs_memset<uint16_t>(vsapi->getWritePtr(frame, plane), d->color[plane], (vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane)) / 2);
                    break;
                case 4:
                    vs_memset<uint32_t>(vsapi->getWritePtr(frame, plane), d->color[plane], (vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane)) / 4);
                    break;
                }
            }

            if (d->vi.fpsNum > 0) {
                VSMap *frameProps = vsapi->getFramePropertiesRW(frame);
                vsapi->mapSetInt(frameProps, "_DurationNum", d->vi.fpsDen, maReplace);
                vsapi->mapSetInt(frameProps, "_DurationDen", d->vi.fpsNum, maReplace);
            }
        }

        if (d->keep) {
            if (frame)
                d->f = frame;
            return vsapi->addFrameRef(d->f);
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
    int tmp1;
    int64_t tmp2;
    int err;

    VSNode *node = vsapi->mapGetNode(in, "clip", 0, &err);

    if (!err) {
        d->vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
        hasvi = true;
    }

    tmp1 = vsapi->mapGetIntSaturated(in, "width", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.width = 640;
    } else {
        d->vi.width = tmp1;
    }

    tmp1 = vsapi->mapGetIntSaturated(in, "height", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.height = 480;
    } else {
        d->vi.height = tmp1;
    }

    tmp2 = vsapi->mapGetInt(in, "fpsnum", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.fpsNum = 24;
    } else {
        d->vi.fpsNum = tmp2;
    }

    tmp2 = vsapi->mapGetInt(in, "fpsden", 0, &err);

    if (err) {
        if (!hasvi)
            d->vi.fpsDen = 1;
    } else
        d->vi.fpsDen = tmp2;

    if (d->vi.fpsDen < 0 || d->vi.fpsNum < 0)
        RETERROR("BlankClip: invalid framerate specified");

    if (d->vi.fpsDen == 0 || d->vi.fpsNum == 0) {
        d->vi.fpsNum = 0;
        d->vi.fpsDen = 0;
    }

    reduceRational(&d->vi.fpsNum, &d->vi.fpsDen);

    int format = vsapi->mapGetIntSaturated(in, "format", 0, &err);

    if (err) {
        if (!hasvi)
            vsapi->queryVideoFormat(&d->vi.format, cfRGB, stInteger, 8, 0, 0, core);
    } else {
        vsapi->getVideoFormatByID(&d->vi.format, format, core);
    }

    if (d->vi.format.colorFamily == cfUndefined)
        RETERROR("BlankClip: invalid format");

    tmp1 = vsapi->mapGetIntSaturated(in, "length", 0, &err);

    if (err) {
        if (!hasvi) {
            if (d->vi.fpsNum > 0 && d->vi.fpsDen > 0)
                d->vi.numFrames = int64ToIntS((d->vi.fpsNum * 10) / d->vi.fpsDen);
            else
                d->vi.numFrames = 300;
        }
    } else {
        d->vi.numFrames = tmp1;
    }

    if (d->vi.width <= 0 || d->vi.width % (1 << d->vi.format.subSamplingW))
        RETERROR("BlankClip: invalid width");

    if (d->vi.height <= 0 || d->vi.height % (1 << d->vi.format.subSamplingH))
        RETERROR("BlankClip: invalid height");

    if (d->vi.numFrames <= 0)
        RETERROR("BlankClip: invalid length");

    setBlack(d->color, &d->vi.format);

    int numcomponents = d->vi.format.numPlanes;
    int ncolors = vsapi->mapNumElements(in, "color");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            double color = vsapi->mapGetFloat(in, "color", i, 0);
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

    d->keep = !!vsapi->mapGetInt(in, "keep", 0, &err);

    VSVideoInfo deliveredInfo = d->vi;

    tmp2 = vsapi->mapGetInt(in, "varsize", 0, &err);
    if (!err && tmp2) {
        deliveredInfo.width = 0;
        deliveredInfo.height = 0;
    }

    tmp2 = vsapi->mapGetInt(in, "varformat", 0, &err);
    if (!err && tmp2) {
        deliveredInfo.format.colorFamily = cfUndefined;
        deliveredInfo.format.bitsPerSample = 0;
        deliveredInfo.format.bytesPerSample = 0;
        deliveredInfo.format.subSamplingW = 0;
        deliveredInfo.format.subSamplingH = 0;
        deliveredInfo.format.numPlanes = 0;
    }

    vsapi->createVideoFilter(out, "BlankClip", &deliveredInfo, blankClipGetframe, blankClipFree, d->keep ? fmUnordered : fmParallel, nullptr, 0, d.get(), core);
    d.release();
}


//////////////////////////////////////////
// AssumeFPS

typedef struct {
    VSVideoInfo vi;
} AssumeFPSDataExtra;

typedef SingleNodeData<AssumeFPSDataExtra> AssumeFPSData;

static const VSFrame *VS_CC assumeFPSGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = reinterpret_cast<AssumeFPSData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src, core);
        VSMap *m = vsapi->getFramePropertiesRW(dst);
        vsapi->freeFrame(src);
        vsapi->mapSetInt(m, "_DurationNum", d->vi.fpsDen, maReplace);
        vsapi->mapSetInt(m, "_DurationDen", d->vi.fpsNum, maReplace);
        return dst;
    }

    return nullptr;
}

static void VS_CC assumeFPSCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AssumeFPSData> d(new AssumeFPSData(vsapi));
    bool hasfps = false;
    bool hassrc = false;
    int err;

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);

    d->vi.fpsNum = vsapi->mapGetInt(in, "fpsnum", 0, &err);

    if (!err)
        hasfps = true;

    d->vi.fpsDen = vsapi->mapGetInt(in, "fpsden", 0, &err);

    if (err)
        d->vi.fpsDen = 1;

    VSNode *src = vsapi->mapGetNode(in, "src", 0, &err);

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

    reduceRational(&d->vi.fpsNum, &d->vi.fpsDen);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "AssumeFPS", &d->vi, assumeFPSGetframe, filterFree<AssumeFPSData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// FrameEval

typedef struct {
    VSVideoInfo vi;
    VSFunction *func;
    std::vector<VSNode *> propsrc;
    VSMap *in;
    VSMap *out;
} FrameEvalData;

static const VSFrame *VS_CC frameEvalGetFrameWithProps(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter : d->propsrc)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady && !*frameData) {
        int err;
        vsapi->mapSetInt(d->in, "n", n, maAppend);
        for (auto iter : d->propsrc) {
            const VSFrame *f = vsapi->getFrameFilter(n, iter, frameCtx);
            vsapi->mapSetFrame(d->in, "f", f, maAppend);
            vsapi->freeFrame(f);
        }
        vsapi->callFunction(d->func, d->in, d->out);
        vsapi->clearMap(d->in);
        if (vsapi->mapGetError(d->out)) {
            vsapi->setFilterError(vsapi->mapGetError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        VSNode *node = vsapi->mapGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return nullptr;
        }

        frameData[0] = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *frame;
        VSNode *node = reinterpret_cast<VSNode *>(frameData[0]);
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
        vsapi->freeNode(reinterpret_cast<VSNode *>(frameData[0]));
    }

    return nullptr;
}

static const VSFrame *VS_CC frameEvalGetFrameNoProps(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);

    if (activationReason == arInitial) {

        int err;
        vsapi->mapSetInt(d->in, "n", n, maAppend);
        vsapi->callFunction(d->func, d->in, d->out);
        vsapi->clearMap(d->in);
        if (vsapi->mapGetError(d->out)) {
            vsapi->setFilterError(vsapi->mapGetError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        VSNode *node = vsapi->mapGetNode(d->out, "val", 0, &err);
        vsapi->clearMap(d->out);

        if (err) {
            vsapi->setFilterError("FrameEval: Function didn't return a clip", frameCtx);
            return nullptr;
        }

        frameData[0] = node;

        vsapi->requestFrameFilter(n, node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSNode *node = reinterpret_cast<VSNode *>(frameData[0]);
        const VSFrame *frame = vsapi->getFrameFilter(n, node, frameCtx);
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
        vsapi->freeNode(reinterpret_cast<VSNode *>(frameData[0]));
    }

    return nullptr;
}

static void VS_CC frameEvalFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);
    for (auto iter : d->propsrc)
        vsapi->freeNode(iter);
    vsapi->freeFunction(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    delete d;
}

static void VS_CC frameEvalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FrameEvalData> d(new FrameEvalData());
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(node);
    vsapi->freeNode(node);
    d->func = vsapi->mapGetFunction(in, "eval", 0, 0);

    int numpropsrc = vsapi->mapNumElements(in, "prop_src");
    if (numpropsrc > 0) {
        d->propsrc.resize(numpropsrc);
        for (int i = 0; i < numpropsrc; i++)
            d->propsrc[i] = vsapi->mapGetNode(in, "prop_src", i, 0);
    }

    std::vector<VSNode *> clipsrc;
    int numclipsrc = vsapi->mapNumElements(in, "clip_src");
    if (numclipsrc > 0) {
        clipsrc.resize(numclipsrc);
        for (int i = 0; i < numclipsrc; i++)
            clipsrc[i] = vsapi->mapGetNode(in, "clip_src", i, 0);
    }

    d->in = vsapi->createMap();
    d->out = vsapi->createMap();

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < numpropsrc; i++)
        deps.push_back({d->propsrc[i], rpGeneral}); // FIXME, propsrc could be strict spatial
    for (int i = 0; i < numclipsrc; i++)
        deps.push_back({clipsrc[i], rpGeneral});
    vsapi->createVideoFilter(out, "FrameEval", &d->vi, (d->propsrc.size() > 0) ? frameEvalGetFrameWithProps : frameEvalGetFrameNoProps, frameEvalFree, (d->propsrc.size() > 0) ? fmParallelRequests : fmUnordered, deps.data(), deps.size(), d.get(), core);
    d.release();

    for (auto &iter : clipsrc)
        vsapi->freeNode(iter);
}

//////////////////////////////////////////
// ModifyFrame

typedef struct {
    std::vector<VSNode *> node;
    const VSVideoInfo *vi;
    VSFunction *func;
    VSMap *in;
    VSMap *out;
} ModifyFrameData;

static const VSFrame *VS_CC modifyFrameGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = reinterpret_cast<ModifyFrameData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter : d->node)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;

        vsapi->mapSetInt(d->in, "n", n, maAppend);

        for (auto iter : d->node) {
            const VSFrame *f = vsapi->getFrameFilter(n, iter, frameCtx);
            vsapi->mapSetFrame(d->in, "f", f, maAppend);
            vsapi->freeFrame(f);
        }

        vsapi->callFunction(d->func, d->in, d->out);
        vsapi->clearMap(d->in);

        if (vsapi->mapGetError(d->out)) {
            vsapi->setFilterError(vsapi->mapGetError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return nullptr;
        }

        const VSFrame *f = vsapi->mapGetFrame(d->out, "val", 0, &err);
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
    vsapi->freeFunction(d->func);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    delete d;
}

static void VS_CC modifyFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ModifyFrameData> d(new ModifyFrameData());
    VSNode *formatnode = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(formatnode);
    vsapi->freeNode(formatnode);

    int numnode = vsapi->mapNumElements(in, "clips");
    d->node.resize(numnode);
    for (int i = 0; i < numnode; i++)
        d->node[i] = vsapi->mapGetNode(in, "clips", i, 0);

    d->func = vsapi->mapGetFunction(in, "selector", 0, 0);
    d->in = vsapi->createMap();
    d->out = vsapi->createMap();

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < numnode; i++)
        deps.push_back({d->node[i], rpStrictSpatial});
    vsapi->createVideoFilter(out, "ModifyFrame", d->vi, modifyFrameGetFrame, modifyFrameFree, fmParallelRequests, deps.data(), numnode, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Transpose

typedef struct {
    VSVideoInfo vi;
    int cpulevel;
} TransposeDataExtra;

typedef SingleNodeData<TransposeDataExtra> TransposeData;

static const VSFrame *VS_CC transposeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = reinterpret_cast<TransposeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);
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

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);
    temp = d->vi.width;
    d->vi.width = d->vi.height;
    d->vi.height = temp;

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("Transpose: clip must have constant format and dimensions and must not be CompatYUY2");

    vsapi->queryVideoFormat(&d->vi.format, d->vi.format.colorFamily, d->vi.format.sampleType, d->vi.format.bitsPerSample, d->vi.format.subSamplingH, d->vi.format.subSamplingW, core);
    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "Transpose", &d->vi, transposeGetFrame, filterFree<TransposeData>, fmParallel, deps, 1, d.get(), core);
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

static const VSFrame *VS_CC pemVerifierGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = reinterpret_cast<PEMVerifierData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
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
    int numupper = vsapi->mapNumElements(in, "upper");
    int numlower = vsapi->mapNumElements(in, "lower");

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    if (!is8to16orFloatFormat(vi->format))
        RETERROR(("PEMVerifier: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(vi->format, vsapi)).c_str());

    if (numlower < 0) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->lower[i] = 0;
            d->lowerf[i] = (vi->format.colorFamily == cfYUV && i) ? -0.5f : 0.0f;
        }
    } else if (numlower == vi->format.numPlanes) {
        for (int i = 0; i < vi->format.numPlanes; i++) {
            d->lowerf[i] = (float)vsapi->mapGetFloat(in, "lower", i, 0);
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
            d->upperf[i] = (float)vsapi->mapGetFloat(in, "upper", i, 0);
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

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "PEMVerifier", vi, pemVerifierGetFrame, filterFree<PEMVerifierData>, fmParallel, deps, 1, d.release(), core);
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

static const VSFrame *VS_CC planeStatsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PlaneStatsData *d = reinterpret_cast<PlaneStatsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        if (d->node2)
            vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = d->node2 ? vsapi->getFrameFilter(n, d->node2, frameCtx) : nullptr;
        VSFrame *dst = vsapi->copyFrame(src1, core);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);
        int width = vsapi->getFrameWidth(src1, d->plane);
        int height = vsapi->getFrameHeight(src1, d->plane);
        const uint8_t *srcp = vsapi->getReadPtr(src1, d->plane);
        ptrdiff_t src_stride = vsapi->getStride(src1, d->plane);
        union vs_plane_stats stats = {};

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

        VSMap *dstProps = vsapi->getFramePropertiesRW(dst);

        if (fi->sampleType == stInteger) {
            vsapi->mapSetInt(dstProps, d->propMin.c_str(), stats.i.min, maReplace);
            vsapi->mapSetInt(dstProps, d->propMax.c_str(), stats.i.max, maReplace);
        } else {
            vsapi->mapSetFloat(dstProps, d->propMin.c_str(), stats.f.min, maReplace);
            vsapi->mapSetFloat(dstProps, d->propMax.c_str(), stats.f.max, maReplace);
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

        vsapi->mapSetFloat(dstProps, d->propAverage.c_str(), avg, maReplace);
        if (d->node2)
            vsapi->mapSetFloat(dstProps, d->propDiff.c_str(), diff, maReplace);

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }
    return nullptr;
}

static void VS_CC planeStatsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PlaneStatsData> d(new PlaneStatsData(vsapi));
    int err;

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(vi->format))
        RETERROR(("PlaneStats: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(vi->format, vsapi)).c_str());

    d->plane = vsapi->mapGetIntSaturated(in, "plane", 0, &err);
    if (d->plane < 0 || d->plane >= vi->format.numPlanes)
        RETERROR("PlaneStats: invalid plane specified");

    d->node2 = vsapi->mapGetNode(in, "clipb", 0, &err);
    if (d->node2) {
        const VSVideoInfo *vi2 = vsapi->getVideoInfo(d->node2);
        if (!isSameVideoInfo(vi, vi2) || !isConstantVideoFormat(vi2))
            RETERROR(("PlaneStats: both input clips must have the same format when second clip is used, passed " + videoInfoToString(vi, vsapi) + " and " + videoInfoToString(vi2, vsapi)).c_str());
    }

    const char *tmpprop = vsapi->mapGetData(in, "prop", 0, &err);
    std::string tempprop = tmpprop ? tmpprop : "PlaneStats";
    d->propMin = tempprop + "Min";
    d->propMax = tempprop + "Max";
    d->propAverage = tempprop + "Average";
    d->propDiff = tempprop + "Diff";
    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, !d->node2 ? 0 : (vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "PlaneStats", vi, planeStatsGetFrame, filterFree<PlaneStatsData>, fmParallel, deps, d->node2 ? 2 : 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// ClipToProp

typedef struct {
    std::string prop;
} ClipToPropDataExtra;

typedef DualNodeData<ClipToPropDataExtra> ClipToPropData;

static const VSFrame *VS_CC clipToPropGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = reinterpret_cast<ClipToPropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src1, core);
        vsapi->mapSetFrame(vsapi->getFramePropertiesRW(dst), d->prop.c_str(), src2, maReplace);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC clipToPropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ClipToPropData> d(new ClipToPropData(vsapi));
    int err;

    d->node1 = vsapi->mapGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node1);
    d->node2 = vsapi->mapGetNode(in, "mclip", 0, 0);
    const VSVideoInfo *vi2 = vsapi->getVideoInfo(d->node2);

    if (!isConstantVideoFormat(&vi) || !isConstantVideoFormat(vi2))
        RETERROR("ClipToProp: clips must have constant format and dimensions");

    const char *tmpprop = vsapi->mapGetData(in, "prop", 0, &err);
    d->prop = tmpprop ? tmpprop : "_Alpha";


    VSFilterDependency deps[] = {{d->node1, (vi.numFrames >= vi2->numFrames) ? rpStrictSpatial : rpGeneral}, {d->node2, 1}};
    vi.numFrames = vi2->numFrames;
    vsapi->createVideoFilter(out, "ClipToProp", &vi, clipToPropGetFrame, filterFree<ClipToPropData>, fmParallel, deps, 2, d.release(), core);
}

//////////////////////////////////////////
// PropToClip

typedef struct {
    VSVideoInfo vi;
    std::string prop;
} PropToClipDataExtra;

typedef SingleNodeData<PropToClipDataExtra> PropToClipData;

static const VSFrame *VS_CC propToClipGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = reinterpret_cast<PropToClipData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame *dst = vsapi->mapGetFrame(vsapi->getFramePropertiesRO(src), d->prop.c_str(), 0, &err);
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

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);

    if (!isConstantVideoFormat(&d->vi))
        RETERROR("PropToClip: clip must have constant format and dimensions");

    const char *tempprop = vsapi->mapGetData(in, "prop", 0, &err);
    d->prop = tempprop ? tempprop : "_Alpha";

    if (d->prop.empty())
        RETERROR("PropToClip: property name can't be an empty string");

    const VSFrame *src = vsapi->getFrame(0, d->node, errmsg, sizeof(errmsg));
    if (!src)
        RETERROR(("PropToClip: upstream error: " + std::string(errmsg)).c_str());

    const VSFrame *msrc = vsapi->mapGetFrame(vsapi->getFramePropertiesRO(src), d->prop.c_str(), 0, &err);
    if (err) {
        vsapi->freeFrame(src);
        RETERROR(("PropToClip: no frame stored in property: " + d->prop).c_str());
    }

    d->vi.format = *vsapi->getVideoFrameFormat(msrc);
    d->vi.width = vsapi->getFrameWidth(msrc, 0);
    d->vi.height = vsapi->getFrameHeight(msrc, 0);
    vsapi->freeFrame(msrc);
    vsapi->freeFrame(src);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "PropToClip", &d->vi, propToClipGetFrame, filterFree<PropToClipData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetFrameProp

typedef struct {
    std::string prop;
    std::vector<int64_t> ints;
    std::vector<double> floats;
    std::vector<std::string> strings;
    std::vector<int> dataType;
} SetFramePropDataExtra;

typedef SingleNodeData<SetFramePropDataExtra> SetFramePropData;

static const VSFrame *VS_CC setFramePropGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFramePropData *d = reinterpret_cast<SetFramePropData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropertiesRW(dst);

        if (!d->ints.empty())
            vsapi->mapSetIntArray(props, d->prop.c_str(), d->ints.data(), static_cast<int>(d->ints.size()));
        else if (!d->floats.empty())
            vsapi->mapSetFloatArray(props, d->prop.c_str(), d->floats.data(), static_cast<int>(d->floats.size()));
        else if (!d->strings.empty()) {
            for (size_t i = 0; i < d->strings.size(); i++)
                vsapi->mapSetData(props, d->prop.c_str(), d->strings[i].c_str(), static_cast<int>(d->strings[i].length()), d->dataType[i], i > 0 ? maAppend : maReplace);
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC setFramePropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SetFramePropData> d(new SetFramePropData(vsapi));

    int num_ints = vsapi->mapNumElements(in, "intval");
    int num_floats = vsapi->mapNumElements(in, "floatval");
    int num_strings = vsapi->mapNumElements(in, "data");

    if ((num_ints > -1) + (num_floats > -1) + (num_strings > -1) > 1)
        RETERROR("SetFrameProp: only one of 'intval', 'floatval', and 'data' can be passed->");

    if (num_ints + num_floats + num_strings == -3)
        RETERROR("SetFrameProp: one of 'intval', 'floatval', or 'data' must be passed->");

    int prop_len = vsapi->mapGetDataSize(in, "prop", 0, nullptr);

    if (prop_len == 0)
        RETERROR("SetFrameProp: 'prop' can't be an empty string.");

    d->prop = vsapi->mapGetData(in, "prop", 0, nullptr);

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    if (num_ints > -1) {
        d->ints.resize(num_ints);
        memcpy(d->ints.data(), vsapi->mapGetIntArray(in, "intval", nullptr), num_ints * sizeof(int64_t));
    }

    if (num_floats > -1) {
        d->floats.resize(num_floats);
        memcpy(d->floats.data(), vsapi->mapGetFloatArray(in, "floatval", nullptr), num_floats * sizeof(double));
    }

    if (num_strings > -1) {
        d->strings.resize(num_strings);
        d->dataType.resize(num_strings);
        for (int i = 0; i < num_strings; i++) {
            d->strings[i] = std::string(vsapi->mapGetData(in, "data", i, nullptr), vsapi->mapGetDataSize(in, "data", i, nullptr));
            d->dataType[i] = vsapi->mapGetDataTypeHint(in, "data", i, nullptr);
        }
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "SetFrameProp", vsapi->getVideoInfo(d->node), setFramePropGetFrame, filterFree<SetFramePropData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetFrameProps

typedef struct {
    VSMap *props;
} SetFramePropsDataExtra;

typedef SingleNodeData<SetFramePropsDataExtra> SetFramePropsData;

static const VSFrame *VS_CC setFramePropsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFramePropsData *d = reinterpret_cast<SetFramePropsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropertiesRW(dst);

        vsapi->copyMap(d->props, props);

        return dst;
    }

    return nullptr;
}

static void VS_CC setFramePropsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SetFramePropsData *d =  reinterpret_cast<SetFramePropsData *>(instanceData);
    vsapi->freeMap(d->props);
    delete d;
}

static void VS_CC setFramePropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SetFramePropsData> d(new SetFramePropsData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    d->props = vsapi->createMap();
    vsapi->copyMap(in, d->props);
    vsapi->mapDeleteKey(d->props, "clip");

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "SetFrameProps", vsapi->getVideoInfo(d->node), setFramePropsGetFrame, setFramePropsFree, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// RemoveFrameProps

typedef struct {
    std::vector<std::string> props;
    bool all;
} RemoveFramePropsDataExtra;

typedef SingleNodeData<RemoveFramePropsDataExtra> RemoveFramePropsData;

static const VSFrame *VS_CC removeFramePropsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    RemoveFramePropsData *d = reinterpret_cast<RemoveFramePropsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropertiesRW(dst);

        if (d->all) {
            vsapi->clearMap(props);
        } else {
            for (const auto &iter : d->props)
                vsapi->mapDeleteKey(props, iter.c_str());
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC removeFramePropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<RemoveFramePropsData> d(new RemoveFramePropsData(vsapi));

    int num_props = vsapi->mapNumElements(in, "props");
    d->all = (num_props < 0);

    if (!d->all) {
        for (int i = 0; i < num_props; i++)
            d->props.push_back(vsapi->mapGetData(in, "props", i, nullptr));
    }

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "RemoveFrameProps", vsapi->getVideoInfo(d->node), removeFramePropsGetFrame, filterFree<RemoveFramePropsData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetFieldBased

typedef struct {
    int64_t fieldbased;
} SetFieldBasedDataExtra;

typedef SingleNodeData<SetFieldBasedDataExtra> SetFieldBasedData;

static const VSFrame *VS_CC setFieldBasedGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SetFieldBasedData *d = reinterpret_cast<SetFieldBasedData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        VSMap *props = vsapi->getFramePropertiesRW(dst);
        vsapi->mapDeleteKey(props, "_Field");
        vsapi->mapSetInt(props, "_FieldBased", d->fieldbased, maReplace);

        return dst;
    }

    return nullptr;
}

static void VS_CC setFieldBasedCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SetFieldBasedData> d(new SetFieldBasedData(vsapi));

    d->fieldbased = vsapi->mapGetInt(in, "value", 0, nullptr);
    if (d->fieldbased < 0 || d->fieldbased > 2)
        RETERROR("SetFieldBased: value must be 0, 1 or 2");
    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "SetFieldBased", vsapi->getVideoInfo(d->node), setFieldBasedGetFrame, filterFree<SetFieldBasedData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// CopyFrameProps

typedef DualNodeData<NoExtraData> CopyFramePropsData;

static const VSFrame *VS_CC copyFramePropsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CopyFramePropsData *d = reinterpret_cast<CopyFramePropsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->copyFrame(src1, core);
        VSMap *dstprops = vsapi->getFramePropertiesRW(dst);
        vsapi->clearMap(dstprops);
        vsapi->copyMap(vsapi->getFramePropertiesRO(src2), dstprops);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC copyFramePropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CopyFramePropsData> d(new CopyFramePropsData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->node2 = vsapi->mapGetNode(in, "prop_src", 0, nullptr);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (vsapi->getVideoInfo(d->node1)->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "CopyFrameProps", vsapi->getVideoInfo(d->node1), copyFramePropsGetFrame, filterFree<CopyFramePropsData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SetAudio/VideoCache

static void VS_CC setCache(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    int mode = vsapi->mapGetIntSaturated(in, "mode", 0, &err);
    if (!err)
        vsapi->setCacheMode(node, mode);
    int fixedsize = vsapi->mapGetIntSaturated(in, "fixedsize", 0, &err);
    if (err)
        fixedsize = -1;
    int maxsize = vsapi->mapGetIntSaturated(in, "maxsize", 0, &err);
    if (err)
        maxsize = -1;
    int maxhistory = vsapi->mapGetIntSaturated(in, "maxhistory", 0, &err);
    if (err)
        maxhistory = -1;
    vsapi->setCacheOptions(node, fixedsize, maxsize, maxhistory);
}

//////////////////////////////////////////
// SetMaxCpu

static void VS_CC setMaxCpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *str = vsapi->mapGetData(in, "cpu", 0, nullptr);
    int level = vs_cpulevel_from_str(str);
    level = vs_set_cpulevel(core, level);
    str = vs_cpulevel_to_str(level);
    vsapi->mapSetData(out, "cpu", str, -1, dtUtf8, maReplace);
}

//////////////////////////////////////////
// Init

void stdlibInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Cache", "clip:vnode;size:int:opt;fixed:int:opt;make_linear:int:opt;", "clip:vnode;", createCacheFilter, nullptr, plugin); 
    vspapi->registerFunction("CropAbs", "clip:vnode;width:int;height:int;left:int:opt;top:int:opt;x:int:opt;y:int:opt;", "clip:vnode;", cropAbsCreate, 0, plugin);
    vspapi->registerFunction("CropRel", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("Crop", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("AddBorders", "clip:vnode;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;color:float[]:opt;", "clip:vnode;", addBordersCreate, 0, plugin);
    vspapi->registerFunction("ShufflePlanes", "clips:vnode[];planes:int[];colorfamily:int;", "clip:vnode;", shufflePlanesCreate, 0, plugin);
    vspapi->registerFunction("SplitPlanes", "clip:vnode;", "clip:vnode[];", splitPlanesCreate, 0, plugin);
    vspapi->registerFunction("SeparateFields", "clip:vnode;tff:int:opt;modify_duration:int:opt;", "clip:vnode;", separateFieldsCreate, 0, plugin);
    vspapi->registerFunction("DoubleWeave", "clip:vnode;tff:int:opt;", "clip:vnode;", doubleWeaveCreate, 0, plugin);
    vspapi->registerFunction("FlipVertical", "clip:vnode;", "clip:vnode;", flipVerticalCreate, 0, plugin);
    vspapi->registerFunction("FlipHorizontal", "clip:vnode;", "clip:vnode;", flipHorizontalCreate, 0, plugin);
    vspapi->registerFunction("Turn180", "clip:vnode;", "clip:vnode;", flipHorizontalCreate, (void *)1, plugin);
    vspapi->registerFunction("StackVertical", "clips:vnode[];", "clip:vnode;", stackCreate, (void *)1, plugin);
    vspapi->registerFunction("StackHorizontal", "clips:vnode[];", "clip:vnode;", stackCreate, 0, plugin);
    vspapi->registerFunction("BlankClip", "clip:vnode:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:float[]:opt;keep:int:opt;varsize:int:opt;varformat:int:opt;", "clip:vnode;", blankClipCreate, 0, plugin);
    vspapi->registerFunction("AssumeFPS", "clip:vnode;src:vnode:opt;fpsnum:int:opt;fpsden:int:opt;", "clip:vnode;", assumeFPSCreate, 0, plugin);
    vspapi->registerFunction("FrameEval", "clip:vnode;eval:func;prop_src:vnode[]:opt;clip_src:vnode[]:opt;", "clip:vnode;", frameEvalCreate, 0, plugin);
    vspapi->registerFunction("ModifyFrame", "clip:vnode;clips:vnode[];selector:func;", "clip:vnode;", modifyFrameCreate, 0, plugin);
    vspapi->registerFunction("Transpose", "clip:vnode;", "clip:vnode;", transposeCreate, 0, plugin);
    vspapi->registerFunction("PEMVerifier", "clip:vnode;upper:float[]:opt;lower:float[]:opt;", "clip:vnode;", pemVerifierCreate, 0, plugin);
    vspapi->registerFunction("PlaneStats", "clipa:vnode;clipb:vnode:opt;plane:int:opt;prop:data:opt;", "clip:vnode;", planeStatsCreate, 0, plugin);
    vspapi->registerFunction("ClipToProp", "clip:vnode;mclip:vnode;prop:data:opt;", "clip:vnode;", clipToPropCreate, 0, plugin);
    vspapi->registerFunction("PropToClip", "clip:vnode;prop:data:opt;", "clip:vnode;", propToClipCreate, 0, plugin);
    vspapi->registerFunction("SetFrameProp", "clip:vnode;prop:data;intval:int[]:opt;floatval:float[]:opt;data:data[]:opt;", "clip:vnode;", setFramePropCreate, 0, plugin);
    vspapi->registerFunction("SetFrameProps", "clip:vnode;any", "clip:vnode;", setFramePropsCreate, 0, plugin);
    vspapi->registerFunction("RemoveFrameProps", "clip:vnode;props:data[]:opt;", "clip:vnode;", removeFramePropsCreate, 0, plugin);
    vspapi->registerFunction("SetFieldBased", "clip:vnode;value:int;", "clip:vnode;", setFieldBasedCreate, 0, plugin);
    vspapi->registerFunction("CopyFrameProps", "clip:vnode;prop_src:vnode;", "clip:vnode;", copyFramePropsCreate, 0, plugin);
    vspapi->registerFunction("SetAudioCache", "clip:anode;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetVideoCache", "clip:vnode;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetMaxCPU", "cpu:data;", "cpu:data;", setMaxCpu, 0, plugin);
}
