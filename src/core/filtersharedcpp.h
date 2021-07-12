/*
* Copyright (c) 2016 Fredrik Mellbin
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

#ifndef FILTERSHAREDCPP_H
#define FILTERSHAREDCPP_H

#include "VapourSynth.h"
#include "VSHelper.h"
#include <stdexcept>
#include <string>

enum RangeArgumentHandling {
    RangeLower,
    RangeUpper,
    RangeMiddle
};

static inline void getPlanesArg(const VSMap *in, bool *process, const VSAPI *vsapi) {
    int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int o = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

        if (o < 0 || o >= 3)
            throw std::runtime_error("plane index out of range");

        if (process[o])
            throw std::runtime_error("plane specified twice");

        process[o] = true;
    }
}

static inline void getPlanePixelRangeArgs(const VSFormat *fi, const VSMap *in, const char *propName, uint16_t *ival, float *fval, RangeArgumentHandling mode, bool mask, const VSAPI *vsapi) {
    if (vsapi->propNumElements(in, propName) > fi->numPlanes)
        throw std::runtime_error(std::string(propName) + " has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        bool uv = (!mask && plane > 0 && (fi->colorFamily == cmYUV || fi->colorFamily == cmYCoCg));
        double temp = vsapi->propGetFloat(in, propName, plane, &err);
        if (err) {
            if (prevValid) {
                ival[plane] = ival[plane - 1];
                fval[plane] = fval[plane - 1];
            } else if (mode == RangeLower) { // bottom of pixel range
                ival[plane] = 0;
                fval[plane] = uv ? -.5f : 0;
            } else if (mode == RangeUpper) { // top of pixel range
                ival[plane] = (1 << fi->bitsPerSample) - 1;
                fval[plane] = uv ? .5f : 1.f;
            } else if (mode == RangeMiddle) { // middle of pixel range
                ival[plane] = (1 << fi->bitsPerSample) / 2;
                fval[plane] = uv ? 0.f : .5f;
            }
        } else {
            if (fi->sampleType == stInteger) {
                int64_t temp2 = static_cast<int64_t>(temp + .5);
                if ((temp2 < 0) || (temp2 >(1 << fi->bitsPerSample) - 1))
                    throw std::runtime_error(std::string(propName) + " out of range");
                ival[plane] = static_cast<uint16_t>(temp2);
            } else {
                fval[plane] = static_cast<float>(temp);
            }
            prevValid = true;
        }
    }
}

static void shared816FFormatCheck(const VSFormat *fi, bool allowVariable = false) {
    if (!fi && !allowVariable)
        throw std::runtime_error("Cannot process variable format.");

    if (fi) {
        if (fi->colorFamily == cmCompat)
            throw std::runtime_error("Cannot process compat formats.");

        if ((fi->sampleType == stInteger && fi->bitsPerSample > 16) || (fi->sampleType == stFloat && fi->bitsPerSample != 32))
            throw std::runtime_error("Only clips with 8..16 bits integer per sample or float supported.");
    }
}

template<typename T>
static void getPlaneArgs(const VSFormat *fi, const VSMap *in, const char *propName, T *val, T def, const VSAPI *vsapi) {
    if (vsapi->propNumElements(in, propName) > fi->numPlanes)
        throw std::runtime_error(std::string(propName) + " has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        T temp = vsapi->propGetFloat(in, propName, plane, &err);
        if (err) {
            val[plane] = prevValid ? val[plane - 1] : def;
        } else {
            val[plane] = temp;
            prevValid = true;
        }
    }
}

template<typename T>
static void VS_CC templateNodeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    T *d = reinterpret_cast<T *>(*instanceData);
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node), 1, node);
}

template<typename T>
static void VS_CC templateNodeCustomViInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    T *d = reinterpret_cast<T *>(* instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
}

template<typename T>
static void VS_CC templateNodeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    T *d = reinterpret_cast<T *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

template<typename T>
static void fillTestPlane(uint8_t *dst, int stride, int height) {
    T *dstp = reinterpret_cast<T *>(dst);
    for (int i = 0; i < (stride / sizeof(T)) * height; i++)
        *dstp++ = i;
}

#endif