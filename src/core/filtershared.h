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
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#define RETERROR(x) do { vsapi->setError(out, (x)); return; } while (0)

// to detect compat formats
static inline bool isCompatFormat(const VSVideoFormat *format) {
    return format->colorFamily == cfCompatBGR32 || format->colorFamily == cfCompatYUY2;
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

static inline int floatToIntS(float f) {
    if (f > static_cast<float>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    else if (f < static_cast<float>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    else
        return static_cast<int>(lround(f));
}

// Convenience structs for *NodeData templates

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

    explicit SingleNodeData(const VSAPI *vsapi) noexcept : T(), vsapi(vsapi) {
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

    explicit DualNodeData(const VSAPI *vsapi) noexcept : T(), vsapi(vsapi) {
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

    explicit VariableNodeData(const VSAPI *vsapi) noexcept : T(), vsapi(vsapi) {
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

static bool is8to16orFloatFormat(const VSVideoFormat &fi, bool allowVariable = false, bool allowCompat = false) {
    if (fi.colorFamily == cfUndefined && !allowVariable)
        return false;

    if ((fi.colorFamily == cfCompatBGR32 || fi.colorFamily == cfCompatYUY2) && !allowCompat)
        return false;

    if ((fi.sampleType == stInteger && fi.bitsPerSample > 16) || (fi.sampleType == stFloat && fi.bitsPerSample != 32))
        return false;

    return true;
}

template<typename T>
static inline void vs_memset(void *ptr, T value, size_t num) {
    T *dstPtr = reinterpret_cast<T *>(ptr);
    std::fill(dstPtr, dstPtr + num, value);
}

static inline void getPlanesArg(const VSMap *in, bool *process, const VSAPI *vsapi) {
    int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int o = vsapi->propGetSaturatedInt(in, "planes", i, nullptr);

        if (o < 0 || o >= 3)
            throw std::runtime_error("plane index out of range");

        if (process[o])
            throw std::runtime_error("plane specified twice");

        process[o] = true;
    }
}

#endif // FILTERSHARED_H
