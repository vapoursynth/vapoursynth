/*
* Copyright (c) 2017 Fredrik Mellbin
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

#include "internalfilters.h"
#include "VSHelper.h"
#include "filtershared.h"
#include "filtersharedcpp.h"

#include <memory>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace

//////////////////////////////////////////
// BoxBlur

struct BoxBlurData {
    VSNodeRef *node;
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
static void processPlane(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes, int radius, uint8_t *tmp) {
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
static void processPlaneF(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes, int radius, uint8_t *tmp) {
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
static void processPlaneR1(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes) {
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
static void processPlaneR1F(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1F(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width);
        for (int p = 1; p < passes; p++)
            blurHR1F(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width);
        src += stride;
        dst += stride;
    }
}

static const VSFrameRef *VS_CC boxBlurGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
        int bytesPerSample = fi->bytesPerSample;
        int radius = d->radius;
        uint8_t *tmp = (radius > 1 && d->passes > 1) ? new uint8_t[bytesPerSample * vsapi->getFrameWidth(src, 0)] : nullptr;

        const uint8_t *srcp = vsapi->getReadPtr(src, 0);
        int stride = vsapi->getStride(src, 0);
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

static VSNodeRef *applyBoxBlurPlaneFiltering(VSPlugin *stdplugin, VSNodeRef *node, int hradius, int hpasses, int vradius, int vpasses, VSCore *core, const VSAPI *vsapi) {
    bool hblur = (hradius > 0) && (hpasses > 0);
    bool vblur = (vradius > 0) && (vpasses > 0);

    if (hblur) {
        VSMap *vtmp1 = vsapi->createMap();
        VSMap *vtmp2 = vsapi->createMap();
        vsapi->createFilter(vtmp1, vtmp2, "BoxBlur", templateNodeInit<BoxBlurData>, boxBlurGetframe, templateNodeFree<BoxBlurData>, fmParallel, 0, new BoxBlurData{ node, hradius, hpasses }, core);
        node = vsapi->propGetNode(vtmp2, "clip", 0, nullptr);
        vsapi->freeMap(vtmp1);
        vsapi->freeMap(vtmp2);
    }

    if (vblur) {
        VSMap *vtmp1 = vsapi->createMap();
        vsapi->propSetNode(vtmp1, "clip", node, paAppend);
        vsapi->freeNode(node);
        VSMap *vtmp2 = vsapi->invoke(stdplugin, "Transpose", vtmp1);
        vsapi->clearMap(vtmp1);
        node = vsapi->propGetNode(vtmp2, "clip", 0, nullptr);
        vsapi->clearMap(vtmp2);
        vsapi->createFilter(vtmp1, vtmp2, "BoxBlur", templateNodeInit<BoxBlurData>, boxBlurGetframe, templateNodeFree<BoxBlurData>, fmParallel, 0, new BoxBlurData{ node, vradius, vpasses }, core);
        vsapi->freeMap(vtmp1);
        vtmp1 = vsapi->invoke(stdplugin, "Transpose", vtmp2);
        vsapi->freeMap(vtmp2);
        node = vsapi->propGetNode(vtmp1, "clip", 0, nullptr);
        vsapi->freeMap(vtmp1);
    }

    return node;
}

static void VS_CC boxBlurCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, 0);

    try {
        int err;
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);

        shared816FFormatCheck(vi->format);

        bool process[3];
        getPlanesArg(in, process, vsapi);

        int hradius = int64ToIntS(vsapi->propGetInt(in, "hradius", 0, &err));
        if (err)
            hradius = 1;
        int hpasses = int64ToIntS(vsapi->propGetInt(in, "hpasses", 0, &err));
        if (err)
            hpasses = 1;
        bool hblur = (hradius > 0) && (hpasses > 0);

        int vradius = int64ToIntS(vsapi->propGetInt(in, "vradius", 0, &err));
        if (err)
            vradius = 1;
        int vpasses = int64ToIntS(vsapi->propGetInt(in, "vpasses", 0, &err));
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

        VSPlugin *stdplugin = vsapi->getPluginById("com.vapoursynth.std", core);

        if (vi->format->numPlanes == 1) {
            VSNodeRef *tmpnode = applyBoxBlurPlaneFiltering(stdplugin, node, hradius, hpasses, vradius, vpasses, core, vsapi);
            node = nullptr;
            vsapi->propSetNode(out, "clip", tmpnode, paAppend);
            vsapi->freeNode(tmpnode);
        } else {
            VSMap *mergeargs = vsapi->createMap();
            int64_t psrc[3] = { 0, process[1] ? 0 : 1, process[2] ? 0 : 2 };
            vsapi->propSetIntArray(mergeargs, "planes", psrc, 3);
            vsapi->propSetInt(mergeargs, "colorfamily", vi->format->colorFamily, paAppend);

            for (int plane = 0; plane < vi->format->numPlanes; plane++) {
                if (process[plane]) {
                    VSMap *vtmp1 = vsapi->createMap();
                    vsapi->propSetNode(vtmp1, "clips", node, paAppend);
                    vsapi->propSetInt(vtmp1, "planes", plane, paAppend);
                    vsapi->propSetInt(vtmp1, "colorfamily", cmGray, paAppend);
                    VSMap *vtmp2 = vsapi->invoke(stdplugin, "ShufflePlanes", vtmp1);
                    vsapi->freeMap(vtmp1);
                    VSNodeRef *tmpnode = vsapi->propGetNode(vtmp2, "clip", 0, nullptr);
                    vsapi->freeMap(vtmp2);
                    tmpnode = applyBoxBlurPlaneFiltering(stdplugin, tmpnode, hradius, hpasses, vradius, vpasses, core, vsapi);
                    vsapi->propSetNode(mergeargs, "clips", tmpnode, paAppend);
                    vsapi->freeNode(tmpnode);
                } else {
                    vsapi->propSetNode(mergeargs, "clips", node, paAppend);
                }
            }

            vsapi->freeNode(node);
            node = nullptr;

            VSMap *retmap = vsapi->invoke(stdplugin, "ShufflePlanes", mergeargs);
            vsapi->freeMap(mergeargs);
            VSNodeRef *tmpnode = vsapi->propGetNode(retmap, "clip", 0, nullptr);
            vsapi->freeMap(retmap);
            vsapi->propSetNode(out, "clip", tmpnode, paAppend);
            vsapi->freeNode(tmpnode);
        }

    } catch (const std::exception &e) {
        vsapi->freeNode(node);
        RETERROR(("BoxBlur: "_s + e.what()).c_str());
    }
}

//////////////////////////////////////////
// Init

void VS_CC boxBlurInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("BoxBlur", "clip:clip;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;", boxBlurCreate, 0, plugin);
}
