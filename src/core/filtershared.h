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

#ifndef FILTERSHARED_H
#define FILTERSHARED_H

#include "VapourSynth4.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <limits>

#define RETERROR(x) do { vsapi->mapSetError(out, (x)); return; } while (0)

// to get the width/height of a plane easily when not having a frame around
static inline int planeWidth(const VSVideoInfo *vi, int plane) {
    return vi->width >> (plane ? vi->format.subSamplingW : 0);
}

static inline int planeHeight(const VSVideoInfo *vi, int plane) {
    return vi->height >> (plane ? vi->format.subSamplingH : 0);
}

static inline int residencyFlags(VSNode *node, const VSAPI *vsapi) {
    return (vsapi->getNodeResidency(node) == nrGPU) ? ffGPUOutput : 0;
}

enum class ClipResidency { AllCPU, AllGPU, Mixed };

struct ClipResidencyResult {
    ClipResidency kind;
    int mixedAt; /* Mixed only: the first clip that disagrees with clip 0 */
};

/* The one residency rule for every filter taking several clips -- all of them on the CPU
   or all of them on the GPU -- checked in one place so the policy and its message cannot
   drift apart between filters. */
static inline ClipResidencyResult residencyOfClips(VSNode *const *nodes, int numNodes, const VSAPI *vsapi) {
    const bool gpu = vsapi->getNodeResidency(nodes[0]) == nrGPU;
    for (int i = 1; i < numNodes; i++)
        if ((vsapi->getNodeResidency(nodes[i]) == nrGPU) != gpu)
            return { ClipResidency::Mixed, i };
    return { gpu ? ClipResidency::AllGPU : ClipResidency::AllCPU, 0 };
}

/* For the filters that keep their two inputs as plain members rather than a vector. */
static inline ClipResidencyResult residencyOfClips(VSNode *a, VSNode *b, const VSAPI *vsapi) {
    VSNode *nodes[2] = { a, b };
    return residencyOfClips(nodes, 2, vsapi);
}

static inline std::string residencyMismatchError(const char *filterName, int mixedAt) {
    return std::string(filterName) + ": clips are mismatched in residency starting at clip #" +
        std::to_string(mixedAt) + "; all clips must be CPU or all GPU, insert GPUUpload or GPUDownload to make them match";
}

// get the triplet representing black for any colorspace (works for union with float too since it's always 0)
static inline void setBlack(uint32_t color[3], const VSVideoFormat *format) {
    for (int i = 0; i < 3; i++)
        color[i] = 0;
    if (format->sampleType == stInteger && format->colorFamily == cfYUV)
        color[1] = color[2] = (1 << (format->bitsPerSample - 1));
}

static inline int floatToIntS(float f) {
    if (f > static_cast<float>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    else if (f < static_cast<float>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    else
        return static_cast<int>(lround(f));
}

static inline std::string videoFormatToName(const VSVideoFormat &f, const VSAPI *vsapi) {
    char buffer[32] = {};
    if (vsapi->getVideoFormatName(&f, buffer))
        return buffer;
    else
        return "ERROR";
}

static inline std::string videoInfoToString(const VSVideoInfo *f, const VSAPI *vsapi) {
    return videoFormatToName(f->format, vsapi) + ((f->width == 0 || f->height == 0) ? "[undefined]" : ("[" + std::to_string(f->width) + "x" + std::to_string(f->height) + "]"));
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
    VSNode *node = nullptr;

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
    VSNode *node1 = nullptr;
    VSNode *node2 = nullptr;

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
    std::vector<VSNode *> nodes;

    explicit VariableNodeData(const VSAPI *vsapi) noexcept : T(), vsapi(vsapi) {
    }

    ~VariableNodeData() {
        for (auto iter : nodes)
            vsapi->freeNode(iter);
    }
};

template<typename T>
static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    delete reinterpret_cast<T *>(instanceData);
}

static inline bool getProcessPlanesArg(const VSMap *in, VSMap *out, const char *filterName, bool process[3], const VSAPI *vsapi) {
    int m = vsapi->mapNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int64_t o = vsapi->mapGetInt(in, "planes", i, nullptr);

        if (o < 0 || o >= 3) {
            vsapi->mapSetError(out, (filterName + std::string(": plane index out of range")).c_str());
            return false;
        }

        if (process[o]) {
            vsapi->mapSetError(out, (filterName + std::string(": plane specified twice")).c_str());
            return false;
        }

        process[o] = true;
    }

    return true;
}

static inline bool is8to16orFloatFormat(const VSVideoFormat &fi, bool allowVariable = false) {
    if (fi.colorFamily == cfUndefined && !allowVariable)
        return false;

    // Half (16-bit float) is accepted everywhere now, so the only float restriction is 16/32-bit.
    if ((fi.sampleType == stInteger && fi.bitsPerSample > 16) || (fi.sampleType == stFloat && !(fi.bitsPerSample == 32 || fi.bitsPerSample == 16)))
        return false;

    return true;
}

static inline std::string invalidVideoFormatMessage(
    const VSVideoFormat &fi, const VSAPI *vsapi, const char * filterName = nullptr, bool allowVariable = false,
    bool isFrame = false
) {
    std::ostringstream errorMessage;

    if (filterName)
        errorMessage << filterName << ": ";

    errorMessage << "Input " << (isFrame ? "frame" : "clip") << " must be";

    if (!allowVariable)
        errorMessage << " constant format";

    errorMessage << " 8..16 bit integer or 16-32 bit float, passed " << videoFormatToName(fi, vsapi) << ".";

    return errorMessage.str();
}

template<typename T>
static inline void vs_memset(void *ptr, T value, size_t num) {
    T *dstPtr = reinterpret_cast<T *>(ptr);
    std::fill(dstPtr, dstPtr + num, value);
}

static inline void getPlanesArg(const VSMap *in, bool *process, const VSAPI *vsapi) {
    int m = vsapi->mapNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int o = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);

        if (o < 0 || o >= 3)
            throw std::runtime_error("plane index out of range");

        if (process[o])
            throw std::runtime_error("plane specified twice");

        process[o] = true;
    }
}

#endif // FILTERSHARED_H
