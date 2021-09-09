/*
* Copyright (c) 2017-2020 Fredrik Mellbin
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

#include <memory>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include "internalfilters.h"
#include "VSHelper4.h"
#include "filtershared.h"

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace

//////////////////////////////////////////
// BoxBlur

struct BoxBlurData {
    VSNode *node;
    int radius, passes;
};

template<typename T>
static void blurH(const T * VS_RESTRICT src, T * VS_RESTRICT dst, const int width, const int radius, const unsigned div, const unsigned round) {
    unsigned acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = (acc + round) / div;
        acc -= src[std::max(x - radius, 0)];
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            acc += src[x + radius];
            dst[x] = (acc + round) / div;
            acc -= src[x - radius];
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = (acc + round) / div;
            acc -= src[std::max(x - radius, 0)];
        }
    }
}

template<typename T>
static void processPlane(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes, int radius, uint8_t *tmp) {
    const unsigned div = radius * 2 + 1;
    const unsigned round = div - 1;
    for (int h = 0; h < height; h++) {
        uint8_t *dst1 = (passes & 1) ? dst : tmp;
        uint8_t *dst2 = (passes & 1) ? tmp : dst;
        blurH(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst1), width, radius, div, round);
        for (int p = 1; p < passes; p++) {
            blurH(reinterpret_cast<const T *>(dst1), reinterpret_cast<T *>(dst2), width, radius, div, (p & 1) ? 0 : round);
            std::swap(dst1, dst2);
        }
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, const int width, const int radius, const T div) {
    T acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = acc * div;
        acc -= src[std::max(x - radius, 0)];
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            acc += src[x + radius];
            dst[x] = acc * div;
            acc -= src[x - radius];
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = acc * div;
            acc -= src[std::max(x - radius, 0)];
        }
    }
}

template<typename T>
static void processPlaneF(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes, int radius, uint8_t *tmp) {
    const T div = static_cast<T>(1) / (radius * 2 + 1);
    for (int h = 0; h < height; h++) {
        uint8_t *dst1 = (passes & 1) ? dst : tmp;
        uint8_t *dst2 = (passes & 1) ? tmp : dst;
        blurHF(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst1), width, radius, div);
        for (int p = 1; p < passes; p++) {
            blurHF(reinterpret_cast<const T *>(dst1), reinterpret_cast<T *>(dst2), width, radius, div);
            std::swap(dst1, dst2);
        }
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHR1(const T *src, T *dst, int width, const unsigned round) {
    unsigned tmp[2] = { src[0], src[1] };
    unsigned acc = tmp[0] * 2 + tmp[1];
    dst[0] = (acc + round) / 3;
    acc -= tmp[0];

    unsigned v = src[2];
    acc += v;
    dst[1] = (acc + round) / 3;
    acc -= tmp[0];
    tmp[0] = v;

    for (int x = 2; x < width - 2; x += 2) {
        v = src[x + 1];
        acc += v;
        dst[x] = (acc + round) / 3;
        acc -= tmp[1];
        tmp[1] = v;

        v = src[x + 2];
        acc += v;
        dst[x + 1] = (acc + round) / 3;
        acc -= tmp[0];
        tmp[0] = v;
    }

    if (width & 1) {
        acc += tmp[0];
        dst[width - 1] = (acc + round) / 3;
    } else {
        v = src[width - 1];
        acc += v;
        dst[width - 2] = (acc + round) / 3;
        acc -= tmp[1];

        acc += v;
        dst[width - 1] = (acc + round) / 3;
    }
}

template<typename T>
static void processPlaneR1(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width, 2);
        for (int p = 1; p < passes; p++)
            blurHR1(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width, (p & 1) ? 0 : 2);
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHR1F(const T *src, T *dst, int width) {
    T tmp[2] = { src[0], src[1] };
    T acc = tmp[0] * 2 + tmp[1];
    const T div = static_cast<T>(1) / 3;
    dst[0] = acc * div;
    acc -= tmp[0];

    T v = src[2];
    acc += v;
    dst[1] = acc * div;
    acc -= tmp[0];
    tmp[0] = v;

    for (int x = 2; x < width - 2; x += 2) {
        v = src[x + 1];
        acc += v;
        dst[x] = acc * div;
        acc -= tmp[1];
        tmp[1] = v;

        v = src[x + 2];
        acc += v;
        dst[x + 1] = acc * div;
        acc -= tmp[0];
        tmp[0] = v;
    }

    if (width & 1) {
        acc += tmp[0];
        dst[width - 1] = acc * div;
    }
    else {
        v = src[width - 1];
        acc += v;
        dst[width - 2] = acc * div;
        acc -= tmp[1];

        acc += v;
        dst[width - 1] = acc * div;
    }
}

template<typename T>
static void processPlaneR1F(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1F(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width);
        for (int p = 1; p < passes; p++)
            blurHR1F(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width);
        src += stride;
        dst += stride;
    }
}

static const VSFrame *VS_CC boxBlurGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
        int bytesPerSample = fi->bytesPerSample;
        int radius = d->radius;
        uint8_t *tmp = (radius > 1 && d->passes > 1) ? new uint8_t[bytesPerSample * vsapi->getFrameWidth(src, 0)] : nullptr;

        const uint8_t *srcp = vsapi->getReadPtr(src, 0);
        ptrdiff_t stride = vsapi->getStride(src, 0);
        uint8_t *dstp = vsapi->getWritePtr(dst, 0);
        int h = vsapi->getFrameHeight(src, 0);
        int w = vsapi->getFrameWidth(src, 0);

        if (radius == 1) {
            if (bytesPerSample == 1)
                processPlaneR1<uint8_t>(srcp, dstp, stride, w, h, d->passes);
            else if (bytesPerSample == 2)
                processPlaneR1<uint16_t>(srcp, dstp, stride, w, h, d->passes);
            else
                processPlaneR1F<float>(srcp, dstp, stride, w, h, d->passes);
        } else {
            if (bytesPerSample == 1)
                processPlane<uint8_t>(srcp, dstp, stride, w, h, d->passes, radius, tmp);
            else if (bytesPerSample == 2)
                processPlane<uint16_t>(srcp, dstp, stride, w, h, d->passes, radius, tmp);
            else
                processPlaneF<float>(srcp, dstp, stride, w, h, d->passes, radius, tmp);
        }

        delete[] tmp;

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC boxBlurFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

static VSNode *applyBoxBlurPlaneFiltering(VSPlugin *stdplugin, VSNode *node, int hradius, int hpasses, int vradius, int vpasses, VSCore *core, const VSAPI *vsapi) {
    bool hblur = (hradius > 0) && (hpasses > 0);
    bool vblur = (vradius > 0) && (vpasses > 0);

    if (hblur) {
        VSFilterDependency deps[] = {{node, rpStrictSpatial}};
        node = vsapi->createVideoFilter2("BoxBlur", vsapi->getVideoInfo(node), boxBlurGetframe, boxBlurFree, fmParallel, deps, 1, new BoxBlurData{node, hradius, hpasses}, core);
    }

    if (vblur) {
        VSMap *vtmp1 = vsapi->createMap();
        vsapi->mapConsumeNode(vtmp1, "clip", node, maAppend);
        VSMap *vtmp2 = vsapi->invoke(stdplugin, "Transpose", vtmp1);
        vsapi->clearMap(vtmp1);
        node = vsapi->mapGetNode(vtmp2, "clip", 0, nullptr);
        vsapi->clearMap(vtmp2);
        VSFilterDependency deps[] = {{node, rpStrictSpatial}};
        vsapi->createVideoFilter(vtmp2, "BoxBlur", vsapi->getVideoInfo(node), boxBlurGetframe, boxBlurFree, fmParallel, deps, 1, new BoxBlurData{ node, vradius, vpasses }, core);
        vsapi->freeMap(vtmp1);
        vtmp1 = vsapi->invoke(stdplugin, "Transpose", vtmp2);
        vsapi->freeMap(vtmp2);
        node = vsapi->mapGetNode(vtmp1, "clip", 0, nullptr);
        vsapi->freeMap(vtmp1);
    }

    return node;
}

static void VS_CC boxBlurCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, 0);

    try {
        int err;
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);

        if (!is8to16orFloatFormat(vi->format))
            throw std::runtime_error("clip must be constant format and of integer 8-16 bit type or 32 bit float");

        bool process[3];
        getPlanesArg(in, process, vsapi);

        int hradius = vsapi->mapGetIntSaturated(in, "hradius", 0, &err);
        if (err)
            hradius = 1;
        int hpasses = vsapi->mapGetIntSaturated(in, "hpasses", 0, &err);
        if (err)
            hpasses = 1;
        bool hblur = (hradius > 0) && (hpasses > 0);

        int vradius = vsapi->mapGetIntSaturated(in, "vradius", 0, &err);
        if (err)
            vradius = 1;
        int vpasses = vsapi->mapGetIntSaturated(in, "vpasses", 0, &err);
        if (err)
            vpasses = 1;
        bool vblur = (vradius > 0) && (vpasses > 0);

        if (hpasses < 0 || vpasses < 0)
            throw std::runtime_error("number of passes can't be negative");

        if (hradius < 0 || vradius < 0)
            throw std::runtime_error("radius can't be negative");

        if (hradius > 30000 || vradius > 30000)
            throw std::runtime_error("radius must be less than 30000");

        if (!hblur && !vblur)
            throw std::runtime_error("nothing to be performed");

        VSPlugin *stdplugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);

        if (vi->format.numPlanes == 1) {
            VSNode *tmpnode = applyBoxBlurPlaneFiltering(stdplugin, node, hradius, hpasses, vradius, vpasses, core, vsapi);
            node = nullptr;
            vsapi->mapSetNode(out, "clip", tmpnode, maAppend);
            vsapi->freeNode(tmpnode);
        } else {
            VSMap *mergeargs = vsapi->createMap();
            int64_t psrc[3] = { 0, process[1] ? 0 : 1, process[2] ? 0 : 2 };
            vsapi->mapSetIntArray(mergeargs, "planes", psrc, 3);
            vsapi->mapSetInt(mergeargs, "colorfamily", vi->format.colorFamily, maAppend);

            for (int plane = 0; plane < vi->format.numPlanes; plane++) {
                if (process[plane]) {
                    VSMap *vtmp1 = vsapi->createMap();
                    vsapi->mapSetNode(vtmp1, "clips", node, maAppend);
                    vsapi->mapSetInt(vtmp1, "planes", plane, maAppend);
                    vsapi->mapSetInt(vtmp1, "colorfamily", cfGray, maAppend);
                    VSMap *vtmp2 = vsapi->invoke(stdplugin, "ShufflePlanes", vtmp1);
                    vsapi->freeMap(vtmp1);
                    VSNode *tmpnode = vsapi->mapGetNode(vtmp2, "clip", 0, nullptr);
                    vsapi->freeMap(vtmp2);
                    tmpnode = applyBoxBlurPlaneFiltering(stdplugin, tmpnode, hradius, hpasses, vradius, vpasses, core, vsapi);
                    vsapi->mapConsumeNode(mergeargs, "clips", tmpnode, maAppend);
                } else {
                    vsapi->mapSetNode(mergeargs, "clips", node, maAppend);
                }
            }

            vsapi->freeNode(node);
            node = nullptr;

            VSMap *retmap = vsapi->invoke(stdplugin, "ShufflePlanes", mergeargs);
            vsapi->freeMap(mergeargs);
            vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(retmap, "clip", 0, nullptr), maAppend);
            vsapi->freeMap(retmap);
        }

    } catch (const std::exception &e) {
        vsapi->freeNode(node);
        RETERROR(("BoxBlur: "_s + e.what()).c_str());
    }
}

//////////////////////////////////////////
// Init

void boxBlurInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("BoxBlur", "clip:vnode;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;", "clip:vnode;", boxBlurCreate, 0, plugin);
}
