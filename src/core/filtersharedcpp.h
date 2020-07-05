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

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include <stdexcept>
#include <string>
#include <vector>

typedef struct NoExtraData {
} NoExtraData;

typedef struct {
    const VSVideoInfo *vi;
} VIPointerData;

template<typename T>
struct SingleNodeData : public T {
private:
    const VSAPI *vsapi;
public:
    VSNodeRef *node = nullptr;

    explicit SingleNodeData(const VSAPI *vsapi) noexcept : T({}), vsapi(vsapi) {
    }

    ~SingleNodeData() {
        vsapi->freeNode(node);
    }
};

template<typename T>
struct DualNodeData : public T {
private:
    const VSAPI *vsapi;
public:
    VSNodeRef *node1 = nullptr;
    VSNodeRef *node2 = nullptr;

    explicit DualNodeData(const VSAPI *vsapi) noexcept : vsapi(vsapi) {
    }

    ~DualNodeData() {
        vsapi->freeNode(node1);
        vsapi->freeNode(node2);
    }
};

template<typename T>
struct VariableNodeData : public T {
private:
    const VSAPI *vsapi;
public:
    std::vector<VSNodeRef *> node;

    explicit VariableNodeData(const VSAPI *vsapi) noexcept : vsapi(vsapi) {
    }

    ~VariableNodeData() {
        for (auto iter : node)
            vsapi->freeNode(iter);
    }
};

template<typename T>
static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    delete reinterpret_cast<T *>(instanceData);
}

static inline bool getProcessPlanesArg(const VSMap *in, VSMap *out, const char *filterName, bool process[3], const VSAPI *vsapi) {
    int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int64_t o = vsapi->propGetInt(in, "planes", i, nullptr);

        if (o < 0 || o >= 3) {
            vsapi->setError(out, (filterName + std::string(": plane index out of range")).c_str());
            return false;
        }
 
        if (process[o]) {
            vsapi->setError(out, (filterName + std::string(": plane specified twice")).c_str());
            return false;
        }

        process[o] = true;
    }

    return true;
}

static bool is8to16orFloatFormatCheck(const VSVideoFormat &fi, bool allowVariable = false, bool allowCompat = false) {
    if (fi.colorFamily == cfUndefined && !allowVariable)
        return false;

    if ((fi.colorFamily == cfCompatBGR32 || fi.colorFamily == cfCompatYUY2) && !allowCompat)
        return false;

    if ((fi.sampleType == stInteger && fi.bitsPerSample > 16) || (fi.sampleType == stFloat && fi.bitsPerSample != 32))
        return false;

    return true;
}



///////////////////////////// NEW FUNCTIONS ABOVE

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

static inline void getPlanePixelRangeArgs(const VSVideoFormat &fi, const VSMap *in, const char *propName, uint16_t *ival, float *fval, RangeArgumentHandling mode, const VSAPI *vsapi) {
    if (vsapi->propNumElements(in, propName) > fi.numPlanes)
        throw std::runtime_error(std::string(propName) + " has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        bool uv = (plane > 0 && (fi.colorFamily == cfYUV || fi.colorFamily == cfYCoCg));
        double temp = vsapi->propGetFloat(in, propName, plane, &err);
        if (err) {
            if (prevValid) {
                ival[plane] = ival[plane - 1];
                fval[plane] = fval[plane - 1];
            } else if (mode == RangeLower) { // bottom of pixel range
                ival[plane] = 0;
                fval[plane] = uv ? -.5f : 0;
            } else if (mode == RangeUpper) { // top of pixel range
                ival[plane] = (1 << fi.bitsPerSample) - 1;
                fval[plane] = uv ? .5f : 1.f;
            } else if (mode == RangeMiddle) { // middle of pixel range
                ival[plane] = (1 << fi.bitsPerSample) / 2;
                fval[plane] = uv ? 0.f : .5f;
            }
        } else {
            if (fi.sampleType == stInteger) {
                int64_t temp2 = static_cast<int64_t>(temp + .5);
                if ((temp2 < 0) || (temp2 >(1 << fi.bitsPerSample) - 1))
                    throw std::runtime_error(std::string(propName) + " out of range");
                ival[plane] = static_cast<uint16_t>(temp2);
            } else {
                fval[plane] = static_cast<float>(temp);
            }
            prevValid = true;
        }
    }
}

static void shared816FFormatCheck(const VSVideoFormat &fi, bool allowVariable = false) {
    if (fi.colorFamily == cfUndefined && !allowVariable)
        throw std::runtime_error("Cannot process variable format.");

    if (fi.colorFamily != cfUndefined) {
        if (fi.colorFamily == cfCompatBGR32 || fi.colorFamily == cfCompatYUY2)
            throw std::runtime_error("Cannot process compat formats.");

        if ((fi.sampleType == stInteger && fi.bitsPerSample > 16) || (fi.sampleType == stFloat && fi.bitsPerSample != 32))
            throw std::runtime_error("Only clips with 8..16 bits integer per sample or float supported.");
    }
}

template<typename T>
static void getPlaneArgs(const VSVideoFormat *fi, const VSMap *in, const char *propName, T *val, T def, const VSAPI *vsapi) {
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
static void fillTestPlane(uint8_t *dst, int stride, int height) {
    T *dstp = reinterpret_cast<T *>(dst);
    for (int i = 0; i < (stride / sizeof(T)) * height; i++)
        *dstp++ = i;
}

#endif