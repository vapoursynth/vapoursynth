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

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex>
#include <algorithm>
#include <bit>
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "cpufeatures.h"
#include "internalfilters.h"
#include <array>
#include "filtershared.h"
#include "gpufilter.h"
#include "VSVulkan4.h"
#include "float16_helper.h"
#include "kernel/cpulevel.h"
#include "kernel/planestats.h"
#include "kernel/transpose.h"

using namespace vsh;

static inline uint32_t doubleToUInt32S(double v) {
    if (v < 0)
        return 0;
    if (v > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)(v + 0.5);
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

    return std::bit_cast<uint32_t>(f);
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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "Crop";
        /* The corner is a luma coordinate, so each plane shifts it by its own subsampling.
           fill already runs per plane, which is all this needs. */
        const std::string body = "    STORE(GSRC0(x + int(pc.u[0]), y + int(pc.u[1])));";
        sf.bodyInt = body;
        sf.bodyFloat = body;
        const int cx = d->x, cy = d->y;
        const int subW = d->vi->format.subSamplingW, subH = d->vi->format.subSamplingH;
        sf.fillParams = [cx, cy, subW, subH](int plane, const uint32_t *, float *, uint32_t *u) {
            u[0] = cx >> (plane ? subW : 0);
            u[1] = cy >> (plane ? subH : 0);
        };
        const int cw = d->width, ch = d->height;
        sf.prepareFrame = [cx, cy, cw, ch](int, const VSFrame *const *sources, int,
                const VSAPI *vsapi, uint32_t *, std::string &error) {
            char msg[150];
            if (cropVerify(cx, cy, cw, ch, vsapi->getFrameWidth(sources[0], 0),
                    vsapi->getFrameHeight(sources[0], 0), nullptr, msg, sizeof(msg))) {
                error = msg;
                return false;
            }
            return true;
        };
        /* Cropping an odd number of lines swaps which field the first line belongs to. */
        if (cy & 1) {
            sf.finishFrame = [](int, VSFrame *dst, const VSFrame *const *, int, const uint32_t *,
                    VSCore *, const VSAPI *vsapi) {
                VSMap *props = vsapi->getFramePropertiesRW(dst);
                int error;
                int64_t fb = vsapi->mapGetInt(props, "_FieldBased", 0, &error);
                if (fb == VSC_FIELD_BOTTOM || fb == VSC_FIELD_TOP)
                    vsapi->mapSetInt(props, "_FieldBased", (fb == VSC_FIELD_BOTTOM) ? VSC_FIELD_TOP : VSC_FIELD_BOTTOM, maReplace);
            };
        }
        VSNode *node = d->node;
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, &vi, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("Crop: " + error).c_str());
        return;
    }

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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "Crop";
        /* The corner is a luma coordinate, so each plane shifts it by its own subsampling.
           fill already runs per plane, which is all this needs. */
        const std::string body = "    STORE(GSRC0(x + int(pc.u[0]), y + int(pc.u[1])));";
        sf.bodyInt = body;
        sf.bodyFloat = body;
        const int cx = d->x, cy = d->y;
        const int subW = d->vi->format.subSamplingW, subH = d->vi->format.subSamplingH;
        sf.fillParams = [cx, cy, subW, subH](int plane, const uint32_t *, float *, uint32_t *u) {
            u[0] = cx >> (plane ? subW : 0);
            u[1] = cy >> (plane ? subH : 0);
        };
        /* The create-time check ran against the node's dimensions, which a variable
           dimension clip states as zero; the scalar path re-verifies against every frame,
           so this path has to as well or a rectangle past the edge silently clamps into
           replicated pixels instead of the error. The format checks passed at create and
           the format is constant, so only the dimensions are asked about again. */
        const int cw = d->width, ch = d->height;
        sf.prepareFrame = [cx, cy, cw, ch](int, const VSFrame *const *sources, int,
                const VSAPI *vsapi, uint32_t *, std::string &error) {
            char msg[150];
            if (cropVerify(cx, cy, cw, ch, vsapi->getFrameWidth(sources[0], 0),
                    vsapi->getFrameHeight(sources[0], 0), nullptr, msg, sizeof(msg))) {
                error = msg;
                return false;
            }
            return true;
        };
        /* Cropping an odd number of lines swaps which field the first line belongs to. */
        if (cy & 1) {
            sf.finishFrame = [](int, VSFrame *dst, const VSFrame *const *, int, const uint32_t *,
                    VSCore *, const VSAPI *vsapi) {
                VSMap *props = vsapi->getFramePropertiesRW(dst);
                int error;
                int64_t fb = vsapi->mapGetInt(props, "_FieldBased", 0, &error);
                if (fb == VSC_FIELD_BOTTOM || fb == VSC_FIELD_TOP)
                    vsapi->mapSetInt(props, "_FieldBased", (fb == VSC_FIELD_BOTTOM) ? VSC_FIELD_TOP : VSC_FIELD_BOTTOM, maReplace);
            };
        }
        VSNode *node = d->node;
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, &vi, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("Crop: " + error).c_str());
        return;
    }

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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "AddBorders";
        /* Inside the pad the source is fetched shifted; outside it the border colour is
           written. GSRC0 clamps, but the branch means it is never asked out of range. */
        const std::string body =
            "    int sx = x - int(pc.u[0]);\n"
            "    int sy = y - int(pc.u[1]);\n"
            "    bool inside = sx >= 0 && sy >= 0 && sx < int(pc.srcWidth) && sy < int(pc.srcHeight);\n";
        sf.bodyInt = body + "    STORE(inside ? uint(GSRC0(sx, sy)) : pc.u[2]);";
        sf.bodyFloat = body + "    STORE(inside ? float(GSRC0(sx, sy)) : pc.f[0]);";
        const int left = d->left, top = d->top;
        const int subW = vi.format.subSamplingW, subH = vi.format.subSamplingH;
        /* color holds raw sample bits, and which bits depends on the format: an integer
           value, a float32 pattern, or a half pattern. Decode once here so the kernel only
           ever sees a value. */
        const bool isHalf = vi.format.sampleType == stFloat && vi.format.bytesPerSample == 2;
        std::array<uint32_t, 3> colour;
        std::array<float, 3> colourf;
        for (int p = 0; p < 3; p++) {
            colour[p] = d->color[p];
            if (isHalf) {
                colourf[p] = halfToFloat(static_cast<uint16_t>(d->color[p]));
            } else {
                float asFloat;
                std::memcpy(&asFloat, &d->color[p], sizeof(asFloat));
                colourf[p] = asFloat;
            }
        }
        sf.fillParams = [left, top, subW, subH, colour, colourf](int plane, const uint32_t *, float *f, uint32_t *u) {
            u[0] = left >> (plane ? subW : 0);
            u[1] = top >> (plane ? subH : 0);
            u[2] = colour[plane];
            f[0] = colourf[plane];
        };
        VSNode *node = d->node;
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, &vi, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("AddBorders: " + error).c_str());
        return;
    }

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

        if (d->nodes[3] && d->nodes[3] != d->nodes[0] && d->nodes[3] != d->nodes[1] && d->nodes[3] != d->nodes[2])
            vsapi->requestFrameFilter(n, d->nodes[3], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        if (d->vi.format.colorFamily != cfGray) {
            const VSFrame *src[4];
            VSFrame *dst;

            for (int i = 0; i < 4; i++)
                src[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);

            dst = vsapi->newVideoFrame2(&d->vi.format, d->vi.width, d->vi.height, src, d->plane, src[3], core);

            for (int i = 0; i < 4; i++)
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

            const VSFrame *prop_src = vsapi->getFrameFilter(n, d->nodes[3], frameCtx);

            dst = vsapi->newVideoFrame2(&d->vi.format, vsapi->getFrameWidth(src, d->plane[0]), vsapi->getFrameHeight(src, d->plane[0]), &src, d->plane, prop_src, core);

            vsapi->freeFrame(src);
            vsapi->freeFrame(prop_src);
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

    d->nodes.resize(4);
    assert(d->plane[0] == 0);

    d->format = vsapi->mapGetIntSaturated(in, "colorfamily", 0, 0);

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

    d->nodes[3] = vsapi->mapGetNode(in, "prop_src", 0, &err);
    if (err)
        d->nodes[3] = vsapi->addNodeRef(d->nodes[0]);

    const ClipResidencyResult planeResidency = residencyOfClips(d->nodes.data(), outplanes, vsapi);
    if (planeResidency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("ShufflePlanes", planeResidency.mixedAt).c_str());

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
        VSFilterDependency deps1[] = {{ d->nodes[0], rpStrictSpatial }, { d->nodes[3], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[3])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly }};
        vsapi->createVideoFilterEx(out, "ShufflePlanes", &d->vi, shufflePlanesGetframe, filterFree<ShufflePlanesData>, fmParallel, residencyFlags(d->nodes[0], vsapi), deps1, 2, d.get(), core);
    } else {
        VSFilterDependency deps3[] = {{ d->nodes[0], rpStrictSpatial}, { d->nodes[1], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly }, { d->nodes[2], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly }, { d->nodes[3], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[3])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly } };
        vsapi->createVideoFilterEx(out, "ShufflePlanes", &d->vi, shufflePlanesGetframe, filterFree<ShufflePlanesData>, fmParallel, residencyFlags(d->nodes[0], vsapi), deps3, 4, d.get(), core);
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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "SeparateFields";
        sf.mapFrame = [](int n, int, int) { return n / 2; };
        sf.requestPattern = rpGeneral;
        const int tff = d->tff;
        sf.frameParamCount = 1;
        sf.prepareFrame = [tff](int n, const VSFrame *const *sources, int, const VSAPI *vsapi,
                uint32_t *params, std::string &error) {
            int err = 0;
            const int fieldBased = vsapi->mapGetIntSaturated(vsapi->getFramePropertiesRO(sources[0]), "_FieldBased", 0, &err);
            int effectiveTFF = tff;
            if (fieldBased == VSC_FIELD_BOTTOM)
                effectiveTFF = 0;
            else if (fieldBased == VSC_FIELD_TOP)
                effectiveTFF = 1;
            if (effectiveTFF == -1) {
                error = "SeparateFields: no field order provided";
                return false;
            }
            params[0] = static_cast<uint32_t>((n & 1) ^ effectiveTFF);
            return true;
        };
        const std::string body = "    STORE(GSRC0(x, 2 * y + int(pc.u[0])));";
        sf.bodyInt = body;
        sf.bodyFloat = body;
        /* The scalar path steps down a line when the parity is zero, so the parity is the
           complement of the starting line. */
        sf.fillParams = [](int, const uint32_t *params, float *, uint32_t *u) {
            u[0] = params[0] ? 0u : 1u;
        };
        const bool modifyDuration = d->modifyDuration;
        sf.finishFrame = [modifyDuration](int, VSFrame *dst, const VSFrame *const *, int,
                const uint32_t *params, VSCore *, const VSAPI *vsapi) {
            VSMap *props = vsapi->getFramePropertiesRW(dst);
            /* The same parity the kernel used to pick its starting line. */
            vsapi->mapSetInt(props, "_Field", params[0], maReplace);
            vsapi->mapDeleteKey(props, "_FieldBased");
            if (modifyDuration) {
                int errNum, errDen;
                int64_t durationNum = vsapi->mapGetInt(props, "_DurationNum", 0, &errNum);
                int64_t durationDen = vsapi->mapGetInt(props, "_DurationDen", 0, &errDen);
                if (!errNum && !errDen) {
                    muldivRational(&durationNum, &durationDen, 1, 2);
                    vsapi->mapSetInt(props, "_DurationNum", durationNum, maReplace);
                    vsapi->mapSetInt(props, "_DurationDen", durationDen, maReplace);
                }
            }
        };
        VSNode *node = d->node;
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, &d->vi, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("SeparateFields: " + error).c_str());
        return;
    }

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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "DoubleWeave";
        sf.numInputs = 2;
        sf.mapFrame = [](int n, int clip, int) { return n + clip; };
        sf.requestPattern = rpGeneral;
        const int tff = d->tff;
        sf.frameParamCount = 1;
        sf.prepareFrame = [tff](int n, const VSFrame *const *sources, int, const VSAPI *vsapi,
                uint32_t *params, std::string &error) {
            int err;
            int64_t field1 = vsapi->mapGetInt(vsapi->getFramePropertiesRO(sources[0]), "_Field", 0, &err);
            if (err)
                field1 = -1;
            int64_t field2 = vsapi->mapGetInt(vsapi->getFramePropertiesRO(sources[1]), "_Field", 0, &err);
            if (err)
                field2 = -1;
            const VSFrame *srctop;
            if (field1 == 0 && field2 == 1)
                srctop = sources[1];
            else if (field1 == 1 && field2 == 0)
                srctop = sources[0];
            else if (tff != -1)
                srctop = ((n & 1) ^ tff) ? sources[0] : sources[1];
            else {
                error = "DoubleWeave: field order could not be determined from frame properties";
                return false;
            }
            params[0] = (srctop == sources[0]) ? 1u : 0u;
            return true;
        };
        const std::string body =
            "    int row = y >> 1;\n"
            "    bool wantTop = (y & 1) == 0;\n"
            "    bool useFirst = wantTop == (pc.u[0] != 0u);\n";
        sf.bodyInt = body + "    STORE(useFirst ? uint(GSRC0(x, row)) : uint(GSRC1(x, row)));";
        sf.bodyFloat = body + "    STORE(useFirst ? float(GSRC0(x, row)) : float(GSRC1(x, row)));";
        sf.fillParams = [](int, const uint32_t *params, float *, uint32_t *u) { u[0] = params[0]; };
        sf.finishFrame = [](int, VSFrame *dst, const VSFrame *const *, int, const uint32_t *params,
                VSCore *, const VSAPI *vsapi) {
            VSMap *props = vsapi->getFramePropertiesRW(dst);
            vsapi->mapDeleteKey(props, "_Field");
            vsapi->mapSetInt(props, "_FieldBased", 1 + params[0], maReplace);
        };
        VSNode *nodes[2] = { d->node, vsapi->addNodeRef(d->node) };
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, nodes, 2, &d->vi, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("DoubleWeave: " + error).c_str());
        return;
    }

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

/* The geometry only filters are a single fetch through a remapped coordinate, so one helper
   covers all of them: the caller supplies the expression mapping the output coordinate back
   into the source, and the rest -- both sample type bodies, the node, the error -- is the
   same every time. GSRC0 is the accessor that bounds against the source rather than the
   output, which is what makes a dimension changing map like a transpose legal. */
static void createGeometryGPU(const char *name, const std::string &mapExpr, VSNode *&node,
    const VSVideoInfo *vi, VSMap *out, VSCore *core, const VSAPI *vsapi) {
    vsgpu::SimpleFilter sf;
    sf.name = name;
    const std::string body = "    STORE(GSRC0(" + mapExpr + "));";
    sf.bodyInt = body;
    sf.bodyFloat = body;

    std::string error;
    VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, vi, core, vsapi, error);
    node = nullptr; /* consumed on success and failure alike */
    if (result)
        vsapi->mapConsumeNode(out, "clip", result, maAppend);
    else
        vsapi->mapSetError(out, (std::string(name) + ": " + error).c_str());
}

static void VS_CC flipVerticalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FlipVeritcalData> d(new FlipVeritcalData(vsapi));
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        createGeometryGPU("FlipVertical", "x, int(pc.srcHeight) - 1 - y",
            d->node, vsapi->getVideoInfo(d->node), out, core, vsapi);
        return;
    }

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

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        createGeometryGPU(d->flip ? "Turn180" : "FlipHorizontal",
            d->flip ? "int(pc.srcWidth) - 1 - x, int(pc.srcHeight) - 1 - y"
                    : "int(pc.srcWidth) - 1 - x, y",
            d->node, vsapi->getVideoInfo(d->node), out, core, vsapi);
        return;
    }

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

/* A plain strided copy from one input into an offset rectangle of the output. This is one of
   the few filters that does not fit SimpleFilter -- the input count is unbounded where that
   layer stops at three, and each pass is shaped by its own input rather than by the output --
   so it declares a FilterDesc directly, which is exactly the escape hatch that exists for it. */
static const char stackGlsl[] =
    "\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(std430, set = 0, binding = 0) readonly buffer Src { SAMPLE_T srcData[]; };\n"
    "layout(std430, set = 0, binding = 1) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
    "layout(push_constant) uniform PC {\n"
    "    uint width, height, srcStride, dstStride, dstX, dstY;\n"
    "} pc;\n"
    "\n"
    "void main() {\n"
    "    uint x = gl_GlobalInvocationID.x;\n"
    "    uint y = gl_GlobalInvocationID.y;\n"
    "    if (x >= pc.width || y >= pc.height) return;\n"
    "    dstData[(y + pc.dstY) * pc.dstStride + (x + pc.dstX)] = srcData[y * pc.srcStride + x];\n"
    "}\n";

struct StackPush {
    uint32_t width, height, srcStride, dstStride, dstX, dstY;
};
static_assert(sizeof(StackPush) <= 128, "must fit Vulkan's guaranteed 128 byte push constant minimum");

/* Consumes the nodes either way, like the rest of the GPU creates here. */
static VSNode *createGPUStack(std::vector<VSNode *> &nodes, bool vertical, const VSVideoInfo *vi,
    const VSFilterDependency *deps, int numDeps, VSCore *core, const VSAPI *vsapi, std::string &error) {
    const int numclips = static_cast<int>(nodes.size());

    /* Stacking moves samples without looking at them, so the kernel only needs a type of the
       right width -- uint for 32 bit rather than a float that would need another extension. */
    std::string preamble = "#version 460\n" + vsgpu::glslTypePreamble(false);
    if (vi->format.bytesPerSample == 1)
        preamble += "#define SAMPLE_T uint8_t\n";
    else if (vi->format.bytesPerSample == 2)
        preamble += "#define SAMPLE_T uint16_t\n";
    else
        preamble += "#define SAMPLE_T uint\n";

    vsgpu::FilterDesc desc;
    desc.vi = *vi;
    desc.nodes = nodes;

    vsgpu::Program program;
    program.glsl = preamble + stackGlsl;
    program.storageBufferCount = 2;
    program.pushConstantBytes = sizeof(StackPush);
    desc.programs.push_back(std::move(program));

    /* Where each input lands, per plane, accumulated in plane coordinates so subsampling
       falls out of the per plane sizes rather than needing a shift here. */
    std::vector<std::array<uint32_t, 3>> offset(numclips);
    std::array<uint32_t, 3> running = { 0, 0, 0 };
    for (int i = 0; i < numclips; i++) {
        const VSVideoInfo *cvi = vsapi->getVideoInfo(nodes[i]);
        for (int p = 0; p < 3; p++) {
            offset[i][p] = running[p];
            running[p] += vertical ? static_cast<uint32_t>(planeHeight(cvi, p))
                                   : static_cast<uint32_t>(planeWidth(cvi, p));
        }

        vsgpu::Pass pass;
        pass.bindings.push_back(vsgpu::Operand::source(i));
        pass.bindings.push_back(vsgpu::Operand::output());
        pass.geometryFromBinding = 0; /* this input's plane, not the stacked output */
        pass.independent = true;      /* disjoint destination rectangles */
        desc.passes.push_back(std::move(pass));
    }

    desc.fillPush = [offset, vertical](const vsgpu::PassInfo &info, void *pushData) {
        StackPush push = {};
        push.width = info.width;
        push.height = info.height;
        push.srcStride = info.srcStrideElements();
        push.dstStride = info.dstStrideElements();
        push.dstX = vertical ? 0 : offset[info.pass][info.plane];
        push.dstY = vertical ? offset[info.pass][info.plane] : 0;
        std::memcpy(pushData, &push, sizeof(push));
    };

    VSNode *node = vsgpu::createFilter(vertical ? "StackVertical" : "StackHorizontal", desc,
        deps, numDeps, core, vsapi, error);
    nodes.clear(); /* createFilter took them */
    return node;
}

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
            deps.push_back({d->nodes[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->nodes[i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly});

        const char *stackName = d->vertical ? "StackVertical" : "StackHorizontal";
        const ClipResidencyResult residency = residencyOfClips(d->nodes.data(), numclips, vsapi);
        if (residency.kind == ClipResidency::Mixed)
            RETERROR(residencyMismatchError(stackName, residency.mixedAt).c_str());

        if (residency.kind == ClipResidency::AllGPU) {
            /* One pass per input, each dispatched over its own plane and writing into its
               own slice of the output. The passes never read what another wrote, so they
               are declared independent and the driver leaves out the barriers between
               them, letting the whole stack run as one wide submission. */
            std::string error;
            VSNode *result = createGPUStack(d->nodes, d->vertical, &d->vi, deps.data(), numclips, core, vsapi, error);
            d->nodes.clear(); /* consumed on success and failure alike */
            if (result)
                vsapi->mapConsumeNode(out, "clip", result, maAppend);
            else
                vsapi->mapSetError(out, (std::string(stackName) + ": " + error).c_str());
            return;
        }

        vsapi->createVideoFilter(out, stackName, &d->vi, stackGetframe, filterFree<StackData>, fmParallel, deps.data(), numclips, d.get(), core);
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
    /* Only set on the GPU path; the pool lives and dies with the instance. */
    bool gpu;
    const VSVULKANAPI *vkapi;
    const VSVulkanFunctions *vk;
    VSGPUExecPool *pool;
} BlankClipData;

static bool blankClipFillGPU(VSFrame *frame, const VSVideoInfo &vi, const uint32_t color[3],
    VSGPUExecPool *pool, const VSVULKANAPI *vkapi, const VSVulkanFunctions *vk,
    char *err, int errSize) {
    VSGPUExecContext *ctx = vkapi->gpuExecAcquire(pool, err, errSize);
    if (!ctx)
        return false;

    VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
    for (int plane = 0; plane < vi.format.numPlanes; plane++) {
        VSVulkanPlaneInfo info;
        if (vkapi->getGPUPlane(frame, plane, &info)) {
            vkapi->gpuExecAbandon(ctx);
            snprintf(err, errSize, "BlankClip: output frame is not GPU resident");
            return false;
        }
        uint32_t pattern = color[plane];
        if (vi.format.bytesPerSample == 1)
            pattern *= 0x01010101u;
        else if (vi.format.bytesPerSample == 2)
            pattern *= 0x00010001u;
        /* vkCmdFillBuffer works in whole dwords, and nothing is lost to the rounding: a
           plane is its stride times its height and strides round up to VSFrame::alignment,
           32 or 64, so the size is always a multiple of four. The mask keeps a layout that
           stopped being true from writing past the buffer rather than covering a tail. */
        const VkDeviceSize whole = info.bufferSize & ~VkDeviceSize{ 3 };
        if (whole)
            vk->vkCmdFillBuffer(cmd, info.buffer, 0, whole, pattern);
        vkapi->gpuExecWritesPlane(ctx, frame, plane);
    }

    return vkapi->gpuExecSubmit(ctx, nullptr, err, errSize) == 0;
}

static const VSFrame *VS_CC blankClipGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = reinterpret_cast<BlankClipData *>(instanceData);

    if (activationReason == arInitial) {
        VSFrame *frame = nullptr;
        if (!d->f && d->gpu) {
            frame = d->vkapi->newGPUVideoFrame(&d->vi.format, d->vi.width, d->vi.height, nullptr, core);
            if (!frame) {
                vsapi->setFilterError("BlankClip: failed to allocate the output frame", frameCtx);
                return nullptr;
            }
            char err[512] = { 0 };
            if (!blankClipFillGPU(frame, d->vi, d->color, d->pool, d->vkapi, d->vk, err, sizeof(err))) {
                vsapi->setFilterError(err, frameCtx);
                vsapi->freeFrame(frame);
                return nullptr;
            }
            if (d->vi.fpsNum > 0) {
                VSMap *frameProps = vsapi->getFramePropertiesRW(frame);
                vsapi->mapSetInt(frameProps, "_DurationNum", d->vi.fpsDen, maReplace);
                vsapi->mapSetInt(frameProps, "_DurationDen", d->vi.fpsNum, maReplace);
            }
        } else if (!d->f) {
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
    if (d->pool)
        d->vkapi->freeGPUExecPool(d->pool);
    delete d;
}

static void VS_CC blankClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BlankClipData> d(new BlankClipData());
    bool hasvi = false;
    int tmp1;
    int64_t tmp2;
    int err;

    VSNode *node = vsapi->mapGetNode(in, "clip", 0, &err);
    bool templateOnGPU = false;

    if (!err) {
        d->vi = *vsapi->getVideoInfo(node);
        templateOnGPU = vsapi->getNodeResidency(node) == nrGPU;
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
        deliveredInfo.format = {};
    }

    tmp2 = vsapi->mapGetInt(in, "gpu", 0, &err);
    d->gpu = err ? templateOnGPU : !!tmp2;

    if (d->gpu) {
        if (!deliveredInfo.width || !deliveredInfo.height || !deliveredInfo.format.numPlanes)
            RETERROR("BlankClip: varsize and varformat are not supported on the GPU");

        d->vkapi = vsapi->getVulkanAPI();
        char err2[512] = { 0 };
        d->vk = d->vkapi->getVulkanFunctions(core, err2, sizeof(err2));
        if (!d->vk)
            RETERROR((std::string("BlankClip: ") + err2).c_str());
        /* One frame is filled per instance when keep is set and one per request otherwise,
           so the pool never needs to be deep. */
        d->pool = d->vkapi->createGPUExecPool(core, vqCompute, err2, sizeof(err2));
        if (!d->pool)
            RETERROR((std::string("BlankClip: ") + err2).c_str());
    }

    vsapi->createVideoFilterEx(out, "BlankClip", &deliveredInfo, blankClipGetframe, blankClipFree,
        d->keep ? fmUnordered : fmParallel, d->gpu ? ffGPUOutput : 0, nullptr, 0, d.get(), core);
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
    vsapi->createVideoFilterEx(out, "AssumeFPS", &d->vi, assumeFPSGetframe, filterFree<AssumeFPSData>, fmParallel, residencyFlags(d->node, vsapi), deps, 1, d.get(), core);
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
    bool gpuOutput;
} FrameEvalData;

static const VSFrame *VS_CC frameEvalGetFrameWithProps(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FrameEvalData *d = reinterpret_cast<FrameEvalData *>(instanceData);

    if (activationReason == arInitial) {
        for (auto iter : d->propsrc)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady && !frameData[0]) {
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

        if ((vsapi->getNodeResidency(node) == nrGPU) != d->gpuOutput) {
            vsapi->freeNode(node);
            vsapi->setFilterError("FrameEval: Returned clip has the wrong residency", frameCtx);
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

        if ((vsapi->getNodeResidency(node) == nrGPU) != d->gpuOutput) {
            vsapi->freeNode(node);
            vsapi->setFilterError("FrameEval: Returned clip has the wrong residency", frameCtx);
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
    d->gpuOutput = (vsapi->getNodeResidency(node) == nrGPU);
    int flags = d->gpuOutput ? ffGPUOutput : 0;
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
        deps.push_back({d->propsrc[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->propsrc[i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    for (int i = 0; i < numclipsrc; i++)
        deps.push_back({clipsrc[i], rpGeneral});
    vsapi->createVideoFilterEx(out, "FrameEval", &d->vi, (d->propsrc.size() > 0) ? frameEvalGetFrameWithProps : frameEvalGetFrameNoProps, frameEvalFree, (d->propsrc.size() > 0) ? fmParallelRequests : fmUnordered, flags, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    d.release();

    for (auto &iter : clipsrc)
        vsapi->freeNode(iter);
}

//////////////////////////////////////////
// ModifyFrame

typedef struct {
    std::vector<VSNode *> node;
    VSVideoInfo vi;
    VSFunction *func;
    VSMap *in;
    VSMap *out;
    bool gpuOutput;
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

        if (d->vi.format.colorFamily != cfUndefined && !isSameVideoFormat(&d->vi.format, vsapi->getVideoFrameFormat(f))) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong format", frameCtx);
            return nullptr;
        }

        if ((d->vi.width || d->vi.height) && (d->vi.width != vsapi->getFrameWidth(f, 0) || d->vi.height != vsapi->getFrameHeight(f, 0))) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong dimensions", frameCtx);
            return nullptr;
        }

        if ((vsapi->getFrameResidency(f) == nrGPU) != d->gpuOutput) {
            vsapi->freeFrame(f);
            vsapi->setFilterError("ModifyFrame: Returned frame has the wrong residency", frameCtx);
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
    d->vi = *vsapi->getVideoInfo(formatnode);
    d->gpuOutput = (vsapi->getNodeResidency(formatnode) == nrGPU);
    int flags = d->gpuOutput ? ffGPUOutput : 0;
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
        deps.push_back({d->node[i], (d->vi.numFrames <= vsapi->getVideoInfo(d->node[i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly});
    vsapi->createVideoFilterEx(out, "ModifyFrame", &d->vi, modifyFrameGetFrame, modifyFrameFree, fmParallelRequests, flags, deps.data(), numnode, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Transpose/Turn90/Turn270

enum TransposeMode {
    tmTranspose = 0,
    tmTurn90 = 1,
    tmTurn270 = 2
};

typedef struct {
    VSVideoInfo vi;
    int cpulevel;
    int mode;
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

            if (d->mode == tmTurn90) {
                srcp += src_stride * (height - 1);
                src_stride = -src_stride;
            } else if (d->mode == tmTurn270) {
                dstp += dst_stride * (width - 1);
                dst_stride = -dst_stride;
            }

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

    d->mode = static_cast<int>(reinterpret_cast<intptr_t>(userData));
    const char *name = (d->mode == tmTurn90) ? "Turn90" : ((d->mode == tmTurn270) ? "Turn270" : "Transpose");

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = *vsapi->getVideoInfo(d->node);
    temp = d->vi.width;
    d->vi.width = d->vi.height;
    d->vi.height = temp;

    if (!isConstantVideoFormat(&d->vi))
        RETERROR((std::string(name) + ": clip must have constant format and dimensions").c_str());

    vsapi->queryVideoFormat(&d->vi.format, d->vi.format.colorFamily, d->vi.format.sampleType, d->vi.format.bitsPerSample, d->vi.format.subSamplingH, d->vi.format.subSamplingW, core);
    d->cpulevel = vs_get_cpulevel(core);

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        const char *map = d->mode == tmTurn90  ? "y, int(pc.srcHeight) - 1 - x"
                        : d->mode == tmTurn270 ? "int(pc.srcWidth) - 1 - y, x"
                                               : "y, x";
        createGeometryGPU(name, map, d->node, &d->vi, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, name, &d->vi, transposeGetFrame, filterFree<TransposeData>, fmParallel, deps, 1, d.get(), core);
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
                if (fi->sampleType == stFloat) {
                    // float16: widen and bounds-check like the float32 path
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++) {
                            f = halfToFloat(((const uint16_t *)srcp)[x]);
                            if (f < d->lowerf[plane] || f > d->upperf[plane] || !isfinite(f)) {
                                snprintf(strbuf, sizeof(strbuf), "PEMVerifier: Illegal sample value (%f) at: plane: %d Y: %d, X: %d, Frame: %d", f, plane, y, x, n);
                                vsapi->setFilterError(strbuf, frameCtx);
                                vsapi->freeFrame(src);
                                return nullptr;
                            }
                        }
                        srcp += src_stride;
                    }
                } else {
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
        RETERROR(invalidVideoFormatMessage(vi->format, vsapi, "PEMVerifier").c_str());

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

/* The one filter here that has to read a result back: its answer goes in frame properties, so
   it must wait for the device. One pass reduces a tile per workgroup into the per frame host
   visible buffer, and finishReadback adds the partials in the accumulator width the scalar
   path uses. */

constexpr int reduceGroup = 16; /* 16x16 = 256 threads per workgroup */

struct ReducePush {
    uint32_t width, height, srcStride, src2Stride;
    uint32_t groupsX;
};
static_assert(sizeof(ReducePush) <= 128, "must fit Vulkan's guaranteed 128 byte push constant minimum");

/* Four uints per workgroup: (min, max, sum, diffsum). */
struct ReduceRecord {
    uint32_t a, b, c, d;
};

std::string reduceKernel(const VSVideoFormat &fmt) {
    std::string s = "#version 460\n" + vsgpu::glslTypePreamble(vsgpu::glslUsesFloat16(fmt));
    s += std::string("#define SAMPLE_T ") + vsgpu::glslElementType(fmt) + "\n";
    s += "#extension GL_KHR_shader_subgroup_basic : require\n"
         "#extension GL_KHR_shader_subgroup_arithmetic : require\n"
         /* Baked per instance: the pipeline pins the subgroup size to SGSIZE, so the
            per-subgroup bookkeeping is exact, and the second-clip and float branches fold
            away at pipeline creation. */
         "layout(constant_id = 0) const uint SGSIZE = 32u;\n"
         "layout(constant_id = 1) const uint HAS_SECOND = 0u;\n"
         "layout(constant_id = 2) const uint IS_FLOAT = 0u;\n"
         "\nlayout(local_size_x = 16, local_size_y = 16) in;\n"
         "layout(std430, set = 0, binding = 0) readonly buffer Src { SAMPLE_T s0[]; };\n"
         "layout(std430, set = 0, binding = 1) readonly buffer Src2 { SAMPLE_T s1[]; };\n"
         "layout(std430, set = 0, binding = 2) writeonly buffer Out { uvec4 outRec[]; };\n"
         "layout(push_constant) uniform PC {\n"
         "    uint width, height, srcStride, src2Stride;\n"
         "    uint groupsX;\n"
         "} pc;\n\n";

    /* Integer samples are exact in float32 up to 65535 and their partial sums stay below
       2^24, so one float code path covers both sample types without losing the integer
       exactness. Each subgroup reduces its lanes in registers, leaves one partial in
       shared memory, and the first subgroup folds those after the one barrier; lanes
       outside the plane contribute the identity. The fold walks gl_NumSubgroups slots at
       gl_SubgroupSize stride rather than deriving both from SGSIZE: the pipeline is only
       pinned to SGSIZE when the device lets compute be pinned at all, and an unpinned one
       may run at any size in [minSubgroupSize, maxSubgroupSize], which would make a derived
       count fold uninitialised slots or drop partials. The arrays are sized for the
       workgroup, since a device is free to report a subgroup size down to 1. */
    s += "shared float smn[256]; shared float smx[256];\n"
         "shared float spart[256]; shared float sdpart[256];\n"
         "void main() {\n"
         "    uint x = gl_GlobalInvocationID.x, y = gl_GlobalInvocationID.y;\n"
         "    float v = 0.0, dv = 0.0, lo = 1.0 / 0.0, hi = -1.0 / 0.0;\n"
         "    if (x < pc.width && y < pc.height) {\n"
         "        v = float(s0[y * pc.srcStride + x]);\n"
         "        lo = v; hi = v;\n"
         "        if (HAS_SECOND != 0u) dv = abs(v - float(s1[y * pc.src2Stride + x]));\n"
         "    }\n"
         "    float rlo = subgroupMin(lo), rhi = subgroupMax(hi);\n"
         "    float rv = subgroupAdd(v), rdv = subgroupAdd(dv);\n"
         "    if (gl_SubgroupInvocationID == 0u) {\n"
         "        smn[gl_SubgroupID] = rlo; smx[gl_SubgroupID] = rhi;\n"
         "        spart[gl_SubgroupID] = rv; sdpart[gl_SubgroupID] = rdv;\n"
         "    }\n"
         "    barrier();\n"
         "    if (gl_SubgroupID == 0u) {\n"
         "        const uint nsg = gl_NumSubgroups;\n"
         "        uint lane = gl_SubgroupInvocationID;\n"
         "        float l2 = 1.0 / 0.0, h2 = -1.0 / 0.0, v2 = 0.0, d2 = 0.0;\n"
         "        for (uint i = lane; i < nsg; i += gl_SubgroupSize) {\n"
         "            l2 = min(l2, smn[i]); h2 = max(h2, smx[i]);\n"
         "            v2 += spart[i]; d2 += sdpart[i];\n"
         "        }\n"
         "        l2 = subgroupMin(l2); h2 = subgroupMax(h2);\n"
         "        v2 = subgroupAdd(v2); d2 = subgroupAdd(d2);\n"
         "        if (lane == 0u) {\n"
         "            uint g = gl_WorkGroupID.y * pc.groupsX + gl_WorkGroupID.x;\n"
         "            if (IS_FLOAT != 0u)\n"
         "                outRec[g] = uvec4(floatBitsToUint(l2), floatBitsToUint(h2),\n"
         "                                  floatBitsToUint(v2), floatBitsToUint(d2));\n"
         "            else\n"
         "                outRec[g] = uvec4(uint(l2), uint(h2), uint(v2), uint(d2));\n"
         "        }\n"
         "    }\n"
         "}\n";
    return s;
}

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
        union vs_plane_stats stats = {};

        const uint8_t *srcp = vsapi->getReadPtr(src1, d->plane);
        ptrdiff_t src_stride = vsapi->getStride(src1, d->plane);

        if (src2) {
            const void *srcp2 = vsapi->getReadPtr(src2, d->plane);
            ptrdiff_t src2_stride = vsapi->getStride(src2, d->plane);
            void (*func)(union vs_plane_stats *, const void *, ptrdiff_t, const void *, ptrdiff_t, unsigned, unsigned) = nullptr;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_avx2; break;
                case 2: func = fi->sampleType == stFloat ? vs_plane_stats_2_half_avx2 : vs_plane_stats_2_word_avx2; break;
                case 4: func = vs_plane_stats_2_float_avx2; break;
                }
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_sse2; break;
                case 2: if (fi->sampleType == stInteger) func = vs_plane_stats_2_word_sse2; break;
                case 4: func = vs_plane_stats_2_float_sse2; break;
                }
            }
#endif
            if (!func) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_2_byte_c; break;
                case 2: func = fi->sampleType == stFloat ? vs_plane_stats_2_half_c : vs_plane_stats_2_word_c; break;
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
                case 2: func = fi->sampleType == stFloat ? vs_plane_stats_1_half_avx2 : vs_plane_stats_1_word_avx2; break;
                case 4: func = vs_plane_stats_1_float_avx2; break;
                }
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_1_byte_sse2; break;
                case 2: if (fi->sampleType == stInteger) func = vs_plane_stats_1_word_sse2; break;
                case 4: func = vs_plane_stats_1_float_sse2; break;
                }
            }
#endif
            if (!func) {
                switch (fi->bytesPerSample) {
                case 1: func = vs_plane_stats_1_byte_c; break;
                case 2: func = fi->sampleType == stFloat ? vs_plane_stats_1_half_c : vs_plane_stats_1_word_c; break;
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
        RETERROR(invalidVideoFormatMessage(vi->format, vsapi, "PlaneStats").c_str());

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

    const bool gpu = vsapi->getNodeResidency(d->node1) == nrGPU;
    if (d->node2 && residencyOfClips(d->node1, d->node2, vsapi).kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("PlaneStats", 1).c_str());

    if (gpu) {
        /* Subgroup geometry: the reduction pins the subgroup size so the kernel's partial
           bookkeeping is exact and even the float last bits are stable on a given device
           and driver. Pinning needs the compute stage in requiredSubgroupSizeStages; the
           rare device without it runs at its reported default size unpinned. */
        char verr[512] = { 0 };
        const VSVULKANAPI *vkapi = vsapi->getVulkanAPI();
        VSVulkanCoreHandles handles = {};
        if (vkapi->getVulkanHandles(core, &handles, verr, sizeof(verr)))
            RETERROR(("PlaneStats: " + std::string(verr)).c_str());
        const VSVulkanFunctions *vkf = vkapi->getVulkanFunctions(core, verr, sizeof(verr));
        if (!vkf)
            RETERROR(("PlaneStats: " + std::string(verr)).c_str());
        VkPhysicalDeviceVulkan13Properties props13 = {};
        props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
        VkPhysicalDeviceVulkan11Properties props11 = {};
        props11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
        props11.pNext = &props13;
        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &props11;
        vkf->vkGetPhysicalDeviceProperties2(handles.physicalDevice, &props2);
        const bool canPin = (props13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
        const uint32_t sgSize = canPin ? std::clamp(32u, props13.minSubgroupSize, props13.maxSubgroupSize) : props11.subgroupSize;

        const int pw = vi->width >> (d->plane > 0 ? vi->format.subSamplingW : 0);
        const int ph = vi->height >> (d->plane > 0 ? vi->format.subSamplingH : 0);
        const uint32_t records = static_cast<uint32_t>((pw + reduceGroup - 1) / reduceGroup) *
                                 static_cast<uint32_t>((ph + reduceGroup - 1) / reduceGroup);
        const bool isFloat = vi->format.sampleType == stFloat;
        const bool hasSecond = d->node2 != nullptr;

        struct GPUStatsState {
            std::string propMin, propMax, propAverage, propDiff;
            bool isFloat, hasSecond;
            int bits, pw, ph;
            uint32_t records;
        };
        auto st = std::make_shared<GPUStatsState>();
        st->propMin = d->propMin;
        st->propMax = d->propMax;
        st->propAverage = d->propAverage;
        st->propDiff = d->propDiff;
        st->isFloat = isFloat;
        st->hasSecond = hasSecond;
        st->bits = vi->format.bitsPerSample;
        st->pw = pw;
        st->ph = ph;
        st->records = records;

        vsgpu::FilterDesc desc;
        desc.vi = *vi;
        desc.nodes.push_back(d->node1);
        if (hasSecond)
            desc.nodes.push_back(d->node2);
        for (int i = 0; i < 3; i++)
            desc.process[i] = false;
        desc.sideEffect = true;
        desc.readbackBytes = records * sizeof(ReduceRecord);

        vsgpu::Program program;
        program.glsl = reduceKernel(vi->format);
        program.storageBufferCount = 3;
        program.pushConstantBytes = sizeof(ReducePush);
        struct SpecData { uint32_t sgSize, hasSecond, isFloat; };
        const SpecData spec = { sgSize, hasSecond ? 1u : 0u, isFloat ? 1u : 0u };
        program.specData.resize(sizeof(spec));
        std::memcpy(program.specData.data(), &spec, sizeof(spec));
        program.specEntries.push_back({ 0, offsetof(SpecData, sgSize), sizeof(uint32_t) });
        program.specEntries.push_back({ 1, offsetof(SpecData, hasSecond), sizeof(uint32_t) });
        program.specEntries.push_back({ 2, offsetof(SpecData, isFloat), sizeof(uint32_t) });
        program.requiredSubgroupSize = canPin ? sgSize : 0;
        /* Full subgroups are deliberately not requested: that flag makes the workgroup's X
           dimension have to be a multiple of the subgroup size, and this kernel's X is 16
           against a subgroup of 32 on nearly every device. It would buy nothing anyway --
           256 invocations divide evenly by any subgroup size, so the subgroup count is
           256 / SGSIZE and none of them is partial either way, and every lane reaches the
           subgroup ops active, out of range ones carrying the identity. */
        desc.programs.push_back(std::move(program));

        vsgpu::Pass pass;
        pass.bindings.push_back(vsgpu::Operand::sourcePlane(d->plane, 0));
        pass.bindings.push_back(vsgpu::Operand::sourcePlane(d->plane, hasSecond ? 1 : 0));
        pass.bindings.push_back(vsgpu::Operand::readback());
        pass.geometryFromBinding = 0;
        desc.passes.push_back(std::move(pass));

        desc.fillPush = [](const vsgpu::PassInfo &info, void *pushData) {
            ReducePush push = {};
            push.width = info.width;
            push.height = info.height;
            push.srcStride = info.strideElements[0];
            push.src2Stride = info.strideElements[1];
            push.groupsX = (info.width + reduceGroup - 1) / reduceGroup;
            std::memcpy(pushData, &push, sizeof(push));
        };

        desc.finishReadback = [st](int, VSFrame *dst, const void *data, const uint32_t *, VSCore *, const VSAPI *vsapi) {
            /* The host finishes what the workgroups started, in the same accumulator
               width the scalar path uses -- which is what makes the integer total
               identical rather than merely close. */
            const ReduceRecord *recs = static_cast<const ReduceRecord *>(data);
            union vs_plane_stats stats = {};
            if (st->isFloat) {
                stats.f.min = INFINITY;
                stats.f.max = -INFINITY;
                for (uint32_t i = 0; i < st->records; i++) {
                    float mn, mx, ac, dc;
                    std::memcpy(&mn, &recs[i].a, 4); std::memcpy(&mx, &recs[i].b, 4);
                    std::memcpy(&ac, &recs[i].c, 4); std::memcpy(&dc, &recs[i].d, 4);
                    stats.f.min = std::min(stats.f.min, mn);
                    stats.f.max = std::max(stats.f.max, mx);
                    stats.f.acc += ac;
                    stats.f.diffacc += dc;
                }
            } else {
                stats.i.min = UINT_MAX;
                stats.i.max = 0;
                for (uint32_t i = 0; i < st->records; i++) {
                    stats.i.min = std::min<unsigned>(stats.i.min, recs[i].a);
                    stats.i.max = std::max<unsigned>(stats.i.max, recs[i].b);
                    stats.i.acc += recs[i].c;
                    stats.i.diffacc += recs[i].d;
                }
            }

            VSMap *dstProps = vsapi->getFramePropertiesRW(dst);
            if (!st->isFloat) {
                vsapi->mapSetInt(dstProps, st->propMin.c_str(), stats.i.min, maReplace);
                vsapi->mapSetInt(dstProps, st->propMax.c_str(), stats.i.max, maReplace);
            } else {
                vsapi->mapSetFloat(dstProps, st->propMin.c_str(), stats.f.min, maReplace);
                vsapi->mapSetFloat(dstProps, st->propMax.c_str(), stats.f.max, maReplace);
            }
            double avg = 0.0;
            double diff = 0.0;
            if (!st->isFloat) {
                avg = stats.i.acc / (double)((int64_t)st->pw * st->ph * (((int64_t)1 << st->bits) - 1));
                if (st->hasSecond)
                    diff = stats.i.diffacc / (double)((int64_t)st->pw * st->ph * (((int64_t)1 << st->bits) - 1));
            } else {
                avg = stats.f.acc / (double)((int64_t)st->pw * st->ph);
                if (st->hasSecond)
                    diff = stats.f.diffacc / (double)((int64_t)st->pw * st->ph);
            }
            vsapi->mapSetFloat(dstProps, st->propAverage.c_str(), avg, maReplace);
            if (st->hasSecond)
                vsapi->mapSetFloat(dstProps, st->propDiff.c_str(), diff, maReplace);
        };

        VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, !d->node2 ? 0 : (vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
        std::string error;
        VSNode *node = vsgpu::createFilter("PlaneStats", desc, deps, hasSecond ? 2 : 1, core, vsapi, error);
        d->node1 = d->node2 = nullptr; /* consumed by the driver either way */
        if (node)
            vsapi->mapConsumeNode(out, "clip", node, maAppend);
        else
            vsapi->mapSetError(out, ("PlaneStats: " + error).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, !d->node2 ? 0 : (vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilterEx(out, "PlaneStats", vi, planeStatsGetFrame, filterFree<PlaneStatsData>, fmParallel, 0, deps, d->node2 ? 2 : 1, d.get(), core);
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


    VSFilterDependency deps[] = {{d->node1, (vi.numFrames >= vi2->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}, {d->node2, 1}};
    vi.numFrames = vi2->numFrames;
    vsapi->createVideoFilterEx(out, "ClipToProp", &vi, clipToPropGetFrame, filterFree<ClipToPropData>, fmParallel, residencyFlags(d->node1, vsapi), deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// PropToClip

typedef struct {
    VSVideoInfo vi;
    std::string prop;
    int index;
    bool gpuOutput;
} PropToClipDataExtra;

typedef SingleNodeData<PropToClipDataExtra> PropToClipData;

static const VSFrame *VS_CC propToClipGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = reinterpret_cast<PropToClipData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int err;
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame *dst = vsapi->mapGetFrame(vsapi->getFramePropertiesRO(src), d->prop.c_str(), d->index, &err);
        vsapi->freeFrame(src);

        if (dst) {
            if (!isSameVideoFormat(&d->vi.format, vsapi->getVideoFrameFormat(dst)) || d->vi.height != vsapi->getFrameHeight(dst, 0) || d->vi.width != vsapi->getFrameWidth(dst, 0)) {
                vsapi->freeFrame(dst);
                vsapi->setFilterError("PropToClip: retrieved frame doesn't match output format or dimensions", frameCtx);
                return nullptr;
            }

            if ((vsapi->getFrameResidency(dst) == nrGPU) != d->gpuOutput) {
                vsapi->freeFrame(dst);
                vsapi->setFilterError("PropToClip: retrieved frame doesn't match the residency determined from frame 0", frameCtx);
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

    d->index = vsapi->mapGetIntSaturated(in, "index", 0, &err);
    if (d->index < 0)
        RETERROR("PropToClip: index can't be negative");

    const VSFrame *src = vsapi->getFrame(0, d->node, errmsg, sizeof(errmsg));
    if (!src)
        RETERROR(("PropToClip: upstream error: " + std::string(errmsg)).c_str());

    const VSFrame *msrc = vsapi->mapGetFrame(vsapi->getFramePropertiesRO(src), d->prop.c_str(), d->index, &err);
    if (err) {
        vsapi->freeFrame(src);
        RETERROR(("PropToClip: no frame stored in property: " + d->prop + " index " + std::to_string(d->index)).c_str());
    }

    d->vi.format = *vsapi->getVideoFrameFormat(msrc);
    d->vi.width = vsapi->getFrameWidth(msrc, 0);
    d->vi.height = vsapi->getFrameHeight(msrc, 0);
    d->gpuOutput = (vsapi->getFrameResidency(msrc) == nrGPU);
    int flags = d->gpuOutput ? ffGPUOutput : 0;
    vsapi->freeFrame(msrc);
    vsapi->freeFrame(src);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilterEx(out, "PropToClip", &d->vi, propToClipGetFrame, filterFree<PropToClipData>, fmParallel, flags, deps, 1, d.get(), core);
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
        RETERROR("SetFrameProp: only one of 'intval', 'floatval', and 'data' can be passed.");

    if (num_ints + num_floats + num_strings == -3)
        RETERROR("SetFrameProp: one of 'intval', 'floatval', or 'data' must be passed.");

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
    vsapi->createVideoFilterEx(out, "SetFrameProp", vsapi->getVideoInfo(d->node), setFramePropGetFrame, filterFree<SetFramePropData>, fmParallel, residencyFlags(d->node, vsapi), deps, 1, d.get(), core);
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
    vsapi->createVideoFilterEx(out, "SetFrameProps", vsapi->getVideoInfo(d->node), setFramePropsGetFrame, setFramePropsFree, fmParallel, residencyFlags(d->node, vsapi), deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// RemoveFrameProps

typedef struct {
    std::vector<std::regex> delProps;
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
            int numKeys = vsapi->mapNumKeys(props);
     
            for (int i = 0; i < numKeys; i++) {
                for (const auto &iter : d->delProps) {
                    // implementation detail that may change: all keys are stored in a std::map internally which means they're sorted and this won't skip anything
                    const char *key = vsapi->mapGetKey(props, i);
                    if (std::regex_match(key, iter)) {
                        vsapi->mapDeleteKey(props, key);
                        --i;
                        numKeys = vsapi->mapNumKeys(props);
                        break;
                    }
                }
            }                
        }

        return dst;
    }

    return nullptr;
}

// Translate a glob pattern ('*' and '?' wildcards) into an anchored ECMAScript
// regex. Every other regex metacharacter is escaped so an arbitrary property
// name can't be misinterpreted as regex syntax or throw std::regex_error.
static std::string globToRegex(const std::string &pattern) {
    std::string r = "^";
    for (char c : pattern) {
        switch (c) {
        case '*':
            r += "(.*)";
            break;
        case '?':
            r += '.';
            break;
        case '.': case '^': case '$': case '+': case '(': case ')':
        case '[': case ']': case '{': case '}': case '|': case '\\':
            r += '\\';
            r += c;
            break;
        default:
            r += c;
            break;
        }
    }
    r += "$";
    return r;
}

static void VS_CC removeFramePropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<RemoveFramePropsData> d(new RemoveFramePropsData(vsapi));

    int num_props = vsapi->mapNumElements(in, "props");
    d->all = (num_props < 0);

    for (int i = 0; i < num_props; i++)
        d->delProps.push_back(std::regex(globToRegex(vsapi->mapGetData(in, "props", i, nullptr)), std::regex::ECMAScript));

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilterEx(out, "RemoveFrameProps", vsapi->getVideoInfo(d->node), removeFramePropsGetFrame, filterFree<RemoveFramePropsData>, fmParallel, residencyFlags(d->node, vsapi), deps, 1, d.get(), core);
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
    vsapi->createVideoFilterEx(out, "SetFieldBased", vsapi->getVideoInfo(d->node), setFieldBasedGetFrame, filterFree<SetFieldBasedData>, fmParallel, residencyFlags(d->node, vsapi), deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// CopyFrameProps

typedef struct {
    std::vector<std::string> copyProps;
} CopyFramePropsDataExtra;

typedef DualNodeData<CopyFramePropsDataExtra> CopyFramePropsData;

static const VSFrame *VS_CC copyFramePropsAllGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
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

static const VSFrame *VS_CC copyFramePropsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CopyFramePropsData *d = reinterpret_cast<CopyFramePropsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *dst_src = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->copyFrame(dst_src, core);
        vsapi->freeFrame(dst_src);

        const VSMap *srcprops = vsapi->getFramePropertiesRO(src);
        VSMap *dstprops = vsapi->getFramePropertiesRW(dst);
        for (const auto &iter : d->copyProps) {
            vsapi->mapDeleteKey(dstprops, iter.c_str());

            // Here's the quite long code to copy any property type from one map to another

            int num = vsapi->mapNumElements(srcprops, iter.c_str());
            int ptype = vsapi->mapGetType(srcprops, iter.c_str());
            if (num == 0) {
                vsapi->mapSetEmpty(dstprops, iter.c_str(), ptype);
            } else if (num > 0) {
                if (ptype == ptInt) {
                    vsapi->mapSetIntArray(dstprops, iter.c_str(), vsapi->mapGetIntArray(srcprops, iter.c_str(), nullptr), num);
                } else if (ptype == ptFloat) {
                    vsapi->mapSetFloatArray(dstprops, iter.c_str(), vsapi->mapGetFloatArray(srcprops, iter.c_str(), nullptr), num);
                } else if (ptype == ptData) {
                    for (int i = 0; i < num; i++)
                        vsapi->mapSetData(dstprops, iter.c_str(), vsapi->mapGetData(srcprops, iter.c_str(), i, nullptr), vsapi->mapGetDataSize(srcprops, iter.c_str(), i, nullptr), vsapi->mapGetDataTypeHint(srcprops, iter.c_str(), i, nullptr), maAppend);
                } else if (ptype == ptAudioNode || ptype == ptVideoNode) {
                    for (int i = 0; i < num; i++)
                        vsapi->mapConsumeNode(dstprops, iter.c_str(), vsapi->mapGetNode(srcprops, iter.c_str(), i, nullptr), maAppend);
                } else if (ptype == ptAudioFrame || ptype == ptVideoFrame) {
                    for (int i = 0; i < num; i++)
                        vsapi->mapConsumeFrame(dstprops, iter.c_str(), vsapi->mapGetFrame(srcprops, iter.c_str(), i, nullptr), maAppend);
                } else if (ptype == ptFunction) {
                    for (int i = 0; i < num; i++)
                        vsapi->mapConsumeFunction(dstprops, iter.c_str(), vsapi->mapGetFunction(srcprops, iter.c_str(), i, nullptr), maAppend);
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC copyFramePropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CopyFramePropsData> d(new CopyFramePropsData(vsapi));

    int num_props = vsapi->mapNumElements(in, "props");
    
    for (int i = 0; i < num_props; i++)
        d->copyProps.push_back(vsapi->mapGetData(in, "props", i, nullptr));

    d->node1 = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->node2 = vsapi->mapGetNode(in, "prop_src", 0, nullptr);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (vsapi->getVideoInfo(d->node1)->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilterEx(out, "CopyFrameProps", vsapi->getVideoInfo(d->node1), d->copyProps.empty() ? copyFramePropsAllGetFrame : copyFramePropsGetFrame, filterFree<CopyFramePropsData>, fmParallel, residencyFlags(d->node1, vsapi), deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void stdlibInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("CropAbs", "clip:vnode:all;width:int;height:int;left:int:opt;top:int:opt;x:int:opt;y:int:opt;", "clip:vnode:all;", cropAbsCreate, 0, plugin);
    vspapi->registerFunction("CropRel", "clip:vnode:all;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode:all;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("Crop", "clip:vnode:all;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", "clip:vnode:all;", cropRelCreate, 0, plugin);
    vspapi->registerFunction("AddBorders", "clip:vnode:all;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;color:float[]:opt;", "clip:vnode:all;", addBordersCreate, 0, plugin);
    vspapi->registerFunction("ShufflePlanes", "clips:vnode[]:all;planes:int[];colorfamily:int;prop_src:vnode:all:opt;", "clip:vnode:all;", shufflePlanesCreate, 0, plugin);
    vspapi->registerFunction("SplitPlanes", "clip:vnode:all;", "clip:vnode[]:all;", splitPlanesCreate, 0, plugin);
    vspapi->registerFunction("SeparateFields", "clip:vnode:all;tff:int:opt;modify_duration:int:opt;", "clip:vnode:all;", separateFieldsCreate, 0, plugin);
    vspapi->registerFunction("DoubleWeave", "clip:vnode:all;tff:int:opt;", "clip:vnode:all;", doubleWeaveCreate, 0, plugin);
    vspapi->registerFunction("FlipVertical", "clip:vnode:all;", "clip:vnode:all;", flipVerticalCreate, 0, plugin);
    vspapi->registerFunction("FlipHorizontal", "clip:vnode:all;", "clip:vnode:all;", flipHorizontalCreate, 0, plugin);
    vspapi->registerFunction("Turn180", "clip:vnode:all;", "clip:vnode:all;", flipHorizontalCreate, (void *)1, plugin);
    vspapi->registerFunction("Turn90", "clip:vnode:all;", "clip:vnode:all;", transposeCreate, (void *)tmTurn90, plugin);
    vspapi->registerFunction("Turn270", "clip:vnode:all;", "clip:vnode:all;", transposeCreate, (void *)tmTurn270, plugin);
    vspapi->registerFunction("StackVertical", "clips:vnode[]:all;", "clip:vnode:all;", stackCreate, (void *)1, plugin);
    vspapi->registerFunction("StackHorizontal", "clips:vnode[]:all;", "clip:vnode:all;", stackCreate, 0, plugin);
    vspapi->registerFunction("BlankClip", "clip:vnode:all:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:float[]:opt;keep:int:opt;varsize:int:opt;varformat:int:opt;gpu:int:opt;", "clip:vnode:all;", blankClipCreate, 0, plugin);
    vspapi->registerFunction("AssumeFPS", "clip:vnode:all;src:vnode:all:opt;fpsnum:int:opt;fpsden:int:opt;", "clip:vnode:all;", assumeFPSCreate, 0, plugin);
    vspapi->registerFunction("FrameEval", "clip:vnode:all;eval:func;prop_src:vnode[]:all:opt;clip_src:vnode[]:all:opt;", "clip:vnode:all;", frameEvalCreate, 0, plugin);
    vspapi->registerFunction("ModifyFrame", "clip:vnode:all;clips:vnode[]:all;selector:func;", "clip:vnode:all;", modifyFrameCreate, 0, plugin);
    vspapi->registerFunction("Transpose", "clip:vnode:all;", "clip:vnode:all;", transposeCreate, 0, plugin);
    vspapi->registerFunction("PEMVerifier", "clip:vnode;upper:float[]:opt;lower:float[]:opt;", "clip:vnode;", pemVerifierCreate, 0, plugin);
    vspapi->registerFunction("PlaneStats", "clipa:vnode:all;clipb:vnode:all:opt;plane:int:opt;prop:data:opt;", "clip:vnode:all;", planeStatsCreate, 0, plugin);
    vspapi->registerFunction("ClipToProp", "clip:vnode:all;mclip:vnode:all;prop:data:opt;", "clip:vnode:all;", clipToPropCreate, 0, plugin);
    vspapi->registerFunction("PropToClip", "clip:vnode:all;prop:data:opt;index:int:opt;", "clip:vnode:all;", propToClipCreate, 0, plugin);
    vspapi->registerFunction("SetFrameProp", "clip:vnode:all;prop:data;intval:int[]:opt;floatval:float[]:opt;data:data[]:opt;", "clip:vnode:all;", setFramePropCreate, 0, plugin);
    vspapi->registerFunction("SetFrameProps", "clip:vnode:all;any", "clip:vnode:all;", setFramePropsCreate, 0, plugin);
    vspapi->registerFunction("RemoveFrameProps", "clip:vnode:all;props:data[]:opt;", "clip:vnode:all;", removeFramePropsCreate, 0, plugin);
    vspapi->registerFunction("SetFieldBased", "clip:vnode:all;value:int;", "clip:vnode:all;", setFieldBasedCreate, 0, plugin);
    vspapi->registerFunction("CopyFrameProps", "clip:vnode:all;prop_src:vnode:all;props:data[]:opt;", "clip:vnode:all;", copyFramePropsCreate, 0, plugin);
}
