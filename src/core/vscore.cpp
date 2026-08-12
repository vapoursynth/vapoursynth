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

#include "vscore.h"
#include "VSHelper4.h"
#include "version.h"
#include "cpufeatures.h"
#include "vsvulkanframe.h"
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <queue>
#include <bitset>
#include <fstream>

#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

// Internal filter headers
#include "internalfilters.h"

using namespace vsh;

static inline bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool isAlphaNumUnderscore(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool isValidIdentifier(const std::string &s) {
    size_t len = s.length();
    if (!len)
        return false;

    if (!isAlpha(s[0]))
        return false;
    for (size_t i = 1; i < len; i++)
        if (!isAlphaNumUnderscore(s[i]))
            return false;
    return true;
}

#ifdef VS_TARGET_OS_WINDOWS
const std::filesystem::path VSCore::libraryExtension = std::filesystem::u8path(".dll");
#elif defined(VS_TARGET_OS_DARWIN)
const std::filesystem::path VSCore::libraryExtension = std::filesystem::u8path(".dylib");
#else
const std::filesystem::path VSCore::libraryExtension = std::filesystem::u8path(".so");
#endif

VSFrameContext::VSFrameContext(NodeOutputKey key, const PVSFrameContext &notify) :
    refcount(1), reqOrder(notify->reqOrder), external(false), lockOnOutput(true), reserveThread(false), frameDone(nullptr),  userData(nullptr), key(key), frameContext() {
    notifyCtxList.push_back(notify);
}

VSFrameContext::VSFrameContext(int n, VSNode *node, VSFrameDoneCallback frameDone, void *userData, bool lockOnOutput, bool reserveThread) :
    refcount(1), reqOrder(0), external(true), lockOnOutput(lockOnOutput), reserveThread(reserveThread), frameDone(frameDone), userData(userData), key(node, n), frameContext() {
}

bool VSFrameContext::setError(const std::string &errorMsg) {
    bool prevState = error;
    error = true;
    if (!prevState)
        errorMessage = errorMsg;
    return prevState;
}

///////////////

bool VSMap::isV3Compatible() const noexcept {
    for (const auto &iter : data->data) {
        if (iter.second->type() == ptAudioNode || iter.second->type() == ptAudioFrame || iter.second->type() == ptUnset)
            return false;
    }
    return true;
}

VSFunction::VSFunction(VSPublicFunction func, void *userData, VSFreeFunctionData freeFunction, VSCore *core, int apiMajor) : refcount(1), func(func), userData(userData), freeFunction(freeFunction), core(core), apiMajor(apiMajor) {
}

VSFunction::~VSFunction() {
    if (freeFunction)
        freeFunction(userData);
}

void VSFunction::call(const VSMap *in, VSMap *out) {
    if (apiMajor == VAPOURSYNTH3_API_MAJOR && !in->isV3Compatible()) {
        vs_internal_vsapi.mapSetError(out, "Function was passed values that are unknown to its API version");
        return;
    }

    func(in, out, userData, core, getVSAPIInternal(apiMajor));
}

///////////////

VSPlaneData::VSPlaneData(size_t dataSize, vs::MemoryUse &mem) noexcept : refcount(1), mem(&mem), size(dataSize + 2 * VSFrame::guardSpace) {
    data = mem.allocate(size);
    assert(data);
    if (!data)
        VS_FATAL_ERROR("Failed to allocate memory for plane. Out of memory.");

#ifdef VS_FRAME_GUARD
    for (size_t i = 0; i < VSFrame::guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
        reinterpret_cast<uint32_t *>(data)[i] = VS_FRAME_GUARD_PATTERN;
        reinterpret_cast<uint32_t *>(data + size - VSFrame::guardSpace)[i] = VS_FRAME_GUARD_PATTERN;
    }
#endif
}

VSPlaneData::VSPlaneData(VSVulkanPlane *plane, VSVulkanDevice *device, size_t dataSize) noexcept
    : refcount(1), mem(nullptr), data(nullptr), size(dataSize), gpu(plane), gpuDevice(device) {
    gpuDevice->addRef();
}

VSPlaneData::VSPlaneData(const VSPlaneData &d) noexcept : refcount(1), mem(d.mem), size(d.size) {
    if (d.gpu)
        VS_FATAL_ERROR("Copy on write of GPU resident planes is not implemented");
    data = mem->allocate(size);
    assert(data);
    if (!data)
        VS_FATAL_ERROR("Failed to allocate memory for plane in copy constructor. Out of memory.");

    memcpy(data, d.data, size);
}

VSPlaneData::~VSPlaneData() {
    if (gpu) {
        /* The producer may still be writing this plane so wait for it */
        if (!gpuDevice->coreFreed())
            waitPlaneHost(*gpuDevice, *gpu);
        gpuDevice->destroyBuffer(gpu->buffer);
        delete gpu; /* releases the plane's timeline reference */
        /* Buffer before device: returning the region keeps MemoryUse alive until this point,
           so the accounting always lands in live memory. */
        gpuDevice->release();
    } else {
        mem->deallocate(data);
    }
}

bool VSPlaneData::unique() noexcept {
    return (refcount == 1);
}

void VSPlaneData::add_ref() noexcept {
    ++refcount;
}

void VSPlaneData::release() noexcept {
    if (!--refcount)
        delete this;
}

///////////////

void VSFrame::setAllocationInfo() noexcept {
    VSNode *currentNode = core->currentProcessingNode;

    if (contentType == mtVideo) {
        char fmtname[32];
        core->getVideoFormatName(format.vf, fmtname);
        if (currentNode)
            debugAllocationInfo = "Video frame #" + std::to_string(allocationSeq++) + " (" + std::string(fmtname) + " " + std::to_string(width) + "x" + std::to_string(height) + ") allocated by " + currentNode->getName();
        else
            debugAllocationInfo = "Video frame #" + std::to_string(allocationSeq++) + " (" + std::string(fmtname) + " " + std::to_string(width) + "x" + std::to_string(height) + ") allocated by <unknown>";
    } else {
        if (currentNode)
            debugAllocationInfo = "Audio frame #" + std::to_string(allocationSeq++) + " (" + std::to_string(width) + ") allocated by " + currentNode->getName();
        else
            debugAllocationInfo = "Audio frame #" + std::to_string(allocationSeq++) + " (" + std::to_string(width) + ") allocated by <unknown>";
    }
}

VSFrame::VSFrame(const VSVideoFormat &f, int width, int height, const VSFrame *propSrc, VSCore *core) noexcept : refcount(1), contentType(mtVideo), v3format(nullptr), width(width), height(height), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }

    if (width <= 0 || height <= 0)
        core->logFatal("Error in frame creation: dimensions are negative (" + std::to_string(width) + "x" + std::to_string(height) + ")");

    format.vf = f;
    numPlanes = format.vf.numPlanes;

    stride[0] = (static_cast<ptrdiff_t>(width) * f.bytesPerSample + (alignment - 1)) & ~(alignment - 1);

    if (numPlanes == 3) {
        ptrdiff_t plane23 = (static_cast<ptrdiff_t>(width >> format.vf.subSamplingW) * format.vf.bytesPerSample + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    data[0] = new VSPlaneData(stride[0] * height, *core->memory);
    if (numPlanes == 3) {
        size_t size23 = stride[1] * (height >> format.vf.subSamplingH);
        data[1] = new VSPlaneData(size23, *core->memory);
        data[2] = new VSPlaneData(size23, *core->memory);
    }

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::VSFrame(const VSVideoFormat &f, int width, int height, const VSFrame *propSrc, VSCore *core, bool gpuFrame) noexcept
    : refcount(1), contentType(mtVideo), v3format(nullptr), width(width), height(height), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    assert(gpuFrame);
    (void)gpuFrame;
    gpuResident = true;
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }

    if (width <= 0 || height <= 0)
        core->logFatal("Error in frame creation: dimensions are negative " + std::to_string(width) + "x" + std::to_string(height));

    format.vf = f;
    numPlanes = format.vf.numPlanes;

    /* Identical stride math to the CPU constructor, which is what keeps uploads flat copies. */
    stride[0] = (static_cast<ptrdiff_t>(width) * format.vf.bytesPerSample + (alignment - 1)) & ~(alignment - 1);
    if (numPlanes == 3) {
        ptrdiff_t plane23 = (static_cast<ptrdiff_t>(width >> format.vf.subSamplingW) * format.vf.bytesPerSample + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    std::string vulkanError;
    VSVulkanDevice *dev = core->vulkanDevice(vulkanError);
    if (!dev)
        core->logFatal("GPU frame requested but no Vulkan device: " + vulkanError);

    for (int i = 0; i < numPlanes; i++) {
        uint32_t pw = static_cast<uint32_t>(i ? width >> format.vf.subSamplingW : width);
        uint32_t ph = static_cast<uint32_t>(i ? height >> format.vf.subSamplingH : height);
        auto *plane = new VSVulkanPlane();
        if (!createGPUPlane(*dev, pw, ph, format.vf.bytesPerSample, stride[i], *plane, vulkanError)) {
            delete plane;
            core->logFatal("Failed to allocate a GPU plane: " + vulkanError);
        }
        data[i] = new VSPlaneData(plane, dev, static_cast<size_t>(stride[i]) * ph);
    }

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::VSFrame(const VSVideoFormat &f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core) noexcept : refcount(1), contentType(mtVideo), v3format(nullptr), width(width), height(height), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }

    if (width <= 0 || height <= 0)
        core->logFatal("Error in frame creation: dimensions are negative " + std::to_string(width) + "x" + std::to_string(height));

    format.vf = f;
    numPlanes = format.vf.numPlanes;

    stride[0] = (static_cast<ptrdiff_t>(width) * format.vf.bytesPerSample + (alignment - 1)) & ~(alignment - 1);

    if (numPlanes == 3) {
        ptrdiff_t plane23 = (static_cast<ptrdiff_t>(width >> format.vf.subSamplingW) * format.vf.bytesPerSample + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    for (int i = 0; i < numPlanes; i++) {
        if (planeSrc[i]) {
            if (i == 0 || !planeSrc[0])
                gpuResident = planeSrc[i]->gpuResident;
            else if (planeSrc[i]->gpuResident != gpuResident)
                core->logFatal("Error in frame creation: mixing GPU and CPU resident source planes in one frame");
        }
    }

    for (int i = 0; i < numPlanes; i++) {
        if (planeSrc[i]) {
            if (plane[i] < 0 || plane[i] >= planeSrc[i]->format.vf.numPlanes)
                core->logFatal("Error in frame creation: plane " + std::to_string(plane[i]) + " does not exist in the source frame");
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                core->logFatal("Error in frame creation: dimensions of plane " + std::to_string(plane[i]) + " do not match. Source: " + std::to_string(planeSrc[i]->getWidth(plane[i])) + "x" + std::to_string(planeSrc[i]->getHeight(plane[i])) + "; destination: " + std::to_string(getWidth(i)) + "x" + std::to_string(getHeight(i)));
            if (planeSrc[i]->gpuResident != gpuResident)
                core->logFatal("Error in frame creation: mixing GPU and CPU resident source planes in one frame");
            data[i] = planeSrc[i]->data[plane[i]];
            data[i]->add_ref();
        } else if (gpuResident) {
            uint32_t pw = static_cast<uint32_t>(i ? width >> format.vf.subSamplingW : width);
            uint32_t ph = static_cast<uint32_t>(i ? height >> format.vf.subSamplingH : height);
            std::string vulkanError;
            VSVulkanDevice *dev = core->vulkanDevice(vulkanError);
            if (!dev)
                core->logFatal("GPU frame requested but no Vulkan device: " + vulkanError);
            auto *gpuPlane = new VSVulkanPlane();
            if (!createGPUPlane(*dev, pw, ph, format.vf.bytesPerSample, stride[i], *gpuPlane, vulkanError)) {
                delete gpuPlane;
                core->logFatal("Failed to allocate a GPU plane: " + vulkanError);
            }
            data[i] = new VSPlaneData(gpuPlane, dev, static_cast<size_t>(stride[i]) * ph);
        } else {
            if (i == 0) {
                data[i] = new VSPlaneData(stride[i] * height, *core->memory);
            } else {
                data[i] = new VSPlaneData(stride[i] * (height >> format.vf.subSamplingH), *core->memory);
            }
        }
    }

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::VSFrame(const VSAudioFormat &f, int numSamples, const VSFrame *propSrc, VSCore *core) noexcept
    : refcount(1), contentType(mtAudio), v3format(nullptr), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }

    if (numSamples <= 0)
        core->logFatal("Error in frame creation: bad number of samples (" + std::to_string(numSamples) + ")");

    format.af = f;
    numPlanes = format.af.numChannels;

    width = numSamples;

    stride[0] = format.af.bytesPerSample * VS_AUDIO_FRAME_SAMPLES;

    data[0] = new VSPlaneData(stride[0] * format.af.numChannels, *core->memory);

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::VSFrame(const VSAudioFormat &f, int numSamples, const VSFrame * const *channelSrc, const int *channel, const VSFrame *propSrc, VSCore *core) noexcept
    : refcount(1), contentType(mtAudio), v3format(nullptr), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }

    if (numSamples <= 0)
        core->logFatal("Error in frame creation: bad number of samples (" + std::to_string(numSamples) + ")");

    format.af = f;
    numPlanes = format.af.numChannels;

    width = numSamples;

    stride[0] = format.af.bytesPerSample * VS_AUDIO_FRAME_SAMPLES;

    data[0] = new VSPlaneData(stride[0] * format.af.numChannels, *core->memory);

    for (int i = 0; i < numPlanes; i++) {
        if (channelSrc[i]) {
            if (channel[i] < 0 || channel[i] >= channelSrc[i]->format.af.numChannels)
                core->logFatal("Error in frame creation: channel " + std::to_string(channel[i]) + " does not exist in the source frame");
            if (channelSrc[i]->getFrameLength() != getFrameLength())
                core->logFatal("Error in frame creation: length of frame does not match. Source: " + std::to_string(channelSrc[i]->getFrameLength()) + "; destination: " + std::to_string(getFrameLength()));
            memcpy(getWritePtr(i), channelSrc[i]->getReadPtr(channel[i]), getFrameLength() * format.af.bytesPerSample);
        }
    }

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::VSFrame(const VSFrame &f) noexcept : refcount(1), v3format(nullptr), core(f.core) {
    frameRefDebug = core->enableFrameRefDebug;
    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.insert(this);
    }
    contentType = f.contentType;
    gpuResident = f.gpuResident;
    data[0] = f.data[0];
    data[1] = f.data[1];
    data[2] = f.data[2];
    data[0]->add_ref();
    if (data[1]) {
        data[1]->add_ref();
        data[2]->add_ref();
    }
    format = f.format;
    numPlanes = f.numPlanes;
    width = f.width;
    height = f.height;
    stride[0] = f.stride[0];
    stride[1] = f.stride[1];
    stride[2] = f.stride[2];
    properties = f.properties;

    if (frameRefDebug)
        setAllocationInfo();
}

VSFrame::~VSFrame() {
    data[0]->release();
    if (data[1]) {
        data[1]->release();
        data[2]->release();
    }

    if (frameRefDebug) {
        std::lock_guard<std::mutex> lock(core->frameRefMutex);
        core->frameRefs.erase(this);
    }
}

const vs3::VSVideoFormat *VSFrame::getVideoFormatV3() const noexcept {
    assert(contentType == mtVideo);
    if (!v3format)
        v3format = core->VideoFormatToV3(format.vf);
    return v3format;
}

ptrdiff_t VSFrame::getStride(int plane) const {
    assert(contentType == mtVideo);
    // numPlanes is the channel count for audio frames and can exceed the array size
    if (plane < 0 || plane >= numPlanes || plane >= 3)
        return 0;
    return stride[plane];
}

const uint8_t *VSFrame::getReadPtr(int plane) const {
    if (plane < 0 || plane >= numPlanes)
        return nullptr;

    if (gpuResident)
        return nullptr;

    if (contentType == mtVideo)
        return data[plane]->data + guardSpace;
    else
        return data[0]->data + guardSpace + plane * stride[0];
}

uint8_t *VSFrame::getWritePtr(int plane) {
    if (plane < 0 || plane >= numPlanes)
        return nullptr;

    if (gpuResident)
        return nullptr;

    // copy the plane data if this isn't the only reference
    if (contentType == mtVideo) {
        if (!data[plane]->unique()) {
            VSPlaneData *old = data[plane];
            data[plane] = new VSPlaneData(*data[plane]);
            old->release();
        }

        return data[plane]->data + guardSpace;
    } else {
        if (!data[0]->unique()) {
            VSPlaneData *old = data[0];
            data[0] = new VSPlaneData(*data[0]);
            old->release();
        }

        return data[0]->data + guardSpace + plane * stride[0];
    }
}

#ifdef VS_FRAME_GUARD
bool VSFrame::verifyGuardPattern() const {
    /* GPU planes carry no guard bands and no host pointer to inspect; out of bounds kernel
       writes are the driver's and the validation layer's jurisdiction. */
    if (gpuResident)
        return true;
    for (int p = 0; p < ((contentType == mtVideo) ? numPlanes : 1); p++) {
        for (size_t i = 0; i < guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
            if (reinterpret_cast<uint32_t *>(data[p]->data)[i] != VS_FRAME_GUARD_PATTERN ||
                reinterpret_cast<uint32_t *>(data[p]->data + data[p]->size - guardSpace)[i] != VS_FRAME_GUARD_PATTERN)
                return false;
        }
    }

    return true;
}
#endif

struct split1 {
    enum empties_t { empties_ok, no_empties };
};

template <typename Container>
Container& split(
    Container& result,
    const typename Container::value_type& s,
    const typename Container::value_type& delimiters,
    split1::empties_t empties = split1::empties_ok) {
    result.clear();
    size_t current;
    size_t next = -1;
    do {
        if (empties == split1::no_empties) {
            next = s.find_first_not_of(delimiters, next + 1);
            if (next == Container::value_type::npos) break;
            next -= 1;
        }
        current = next + 1;
        next = s.find_first_of(delimiters, current);
        result.push_back(s.substr(current, next - current));
    } while (next != Container::value_type::npos);
    return result;
}

void VSPluginFunction::parseArgString(const std::string &argString, std::vector<FilterArgument> &argsOut, int apiMajor) {
    std::vector<std::string> argList;
    split(argList, argString, std::string(";"), split1::no_empties);

    argsOut.reserve(argList.size());
    for (const std::string &arg : argList) {
        std::vector<std::string> argParts;
        split(argParts, arg, std::string(":"), split1::no_empties);

        if (argParts.size() == 1 && argParts[0] == "any") {
            argsOut.emplace_back("", ptUnset, false, false, false);
            continue;
        }

        if (argParts.size() < 2)
            throw std::runtime_error("Invalid argument specifier '" + arg + "'. It appears to be incomplete.");

        bool arr = false;
        bool opt = false;
        bool empty = false;
        VSArgResidency residency = VSArgResidency::CPU;

        VSPropertyType type = ptUnset;
        const std::string &argName = argParts[0];
        std::string &typeName = argParts[1];

        if (typeName.length() > 2 && typeName.substr(typeName.length() - 2) == "[]") {
            typeName.resize(typeName.length() - 2);
            arr = true;
        }

        if (typeName == "int") {
            type = ptInt;
        } else if (typeName == "float") {
            type = ptFloat;
        } else if (typeName == "data") {
            type = ptData;
        } else if ((typeName == "vnode" && apiMajor > VAPOURSYNTH3_API_MAJOR) || (apiMajor == VAPOURSYNTH3_API_MAJOR && typeName == "clip")) {
            type = ptVideoNode;
        } else if (typeName == "anode" && apiMajor > VAPOURSYNTH3_API_MAJOR) {
            type = ptAudioNode;
        } else if ((typeName == "vframe" && apiMajor > VAPOURSYNTH3_API_MAJOR) || (apiMajor == VAPOURSYNTH3_API_MAJOR && typeName == "frame")) {
            type = ptVideoFrame;
        } else if (typeName == "aframe" && apiMajor > VAPOURSYNTH3_API_MAJOR) {
            type = ptAudioFrame;
        } else if (typeName == "func") {
            type = ptFunction;
        } else {
            throw std::runtime_error("Argument '" + argName + "' has invalid type '" + typeName + "'.");
        }

        for (size_t i = 2; i < argParts.size(); i++) {
            if (argParts[i] == "opt") {
                if (opt)
                    throw std::runtime_error("Argument '" + argName + "' has duplicate argument specifier '" + argParts[i] + "'");
                opt = true;
            } else if (argParts[i] == "empty") {
                if (empty)
                    throw std::runtime_error("Argument '" + argName + "' has duplicate argument specifier '" + argParts[i] + "'");
                empty = true;
            } else if ((argParts[i] == "gpu" || argParts[i] == "all") && apiMajor > VAPOURSYNTH3_API_MAJOR) {
                if (type != ptVideoNode && type != ptVideoFrame)
                    throw std::runtime_error("Argument '" + argName + "': residency modifier '" + argParts[i] + "' is only valid on vnode and vframe arguments");
                if (residency != VSArgResidency::CPU)
                    throw std::runtime_error("Argument '" + argName + "' has more than one residency modifier");
                residency = (argParts[i] == "gpu") ? VSArgResidency::GPU : VSArgResidency::All;
            } else {
                throw std::runtime_error("Argument '" + argName + "' has unknown argument modifier '" + argParts[i] + "'");
            }
        }

        if (!isValidIdentifier(argName))
            throw std::runtime_error("Argument name '" + argName + "' contains illegal characters.");

        if (empty && !arr)
            throw std::runtime_error("Argument '" + argName + "' is not an array. Only array arguments can have the empty flag set.");

        argsOut.emplace_back(argName, type, arr, empty, opt, residency);
    }
}

VSPluginFunction::VSPluginFunction(const std::string &name, const std::string &argString, const std::string &returnType, VSPublicFunction func, void *functionData, VSPlugin *plugin)
    : func(func), functionData(functionData), plugin(plugin), name(name), argString(argString), returnType(returnType) {
    parseArgString(argString, inArgs, plugin->apiMajor);
    if (plugin->apiMajor == 3)
        this->argString = getV4ArgString(); // construct to V4 equivalent arg string
    if (returnType != "any")
        parseArgString(returnType, retArgs, plugin->apiMajor);
}

std::string VSPluginFunction::checkValues(const std::vector<FilterArgument> &fargs, const VSMap &m, const char *kind) {
    std::set<std::string> remainingValues;
    for (size_t i = 0; i < m.size(); i++)
        remainingValues.insert(m.key(i));

    for (const FilterArgument &fa : fargs) {
        // ptUnset as a declared type means any value is accepted beyond the explicitly listed ones
        if (fa.type == ptUnset) {
            // this will always be the last entry, therefore it's safe to clear out the remaining ones since they'll
            // already have been checked
            remainingValues.clear();
            continue;
        }

        int propType = vs_internal_vsapi.mapGetType(&m, fa.name.c_str());

        if (propType != ptUnset) {
            remainingValues.erase(fa.name);

            if (fa.type != propType)
                return std::string(kind) + " " + fa.name + " is not of the correct type";

            VSArrayBase *arr = m.find(fa.name);

            if (!fa.arr && arr->size() > 1)
                return std::string(kind) + " " + fa.name + " is not of array type but more than one value was supplied";

            if (!fa.empty && arr->size() < 1)
                return std::string(kind) + " " + fa.name + " does not accept empty arrays";

        } else if (!fa.opt) {
            return std::string(kind) + " " + fa.name + " is required";
        }
    }

    if (!remainingValues.empty()) {
        auto iter = remainingValues.cbegin();
        std::string s = *iter;
        ++iter;
        for (; iter != remainingValues.cend(); ++iter)
            s += ", " + *iter;
        return "no " + std::string(kind) + "(s) named " + s;
    }

    return {};
}

VSMap *VSPluginFunction::invoke(const VSMap &args) {
    VSMap *v = new VSMap;

    try {
        std::string mismatch = checkValues(inArgs, args, "argument");
        if (!mismatch.empty())
            throw VSException(name + ": " + mismatch);

        const VSMap *callArgs = &args;
        std::unique_ptr<VSMap> fixedArgs;
        for (const auto &fa : inArgs) {
            if (fa.type == ptVideoNode && fa.residency != VSArgResidency::All) {
                bool wantGPU = (fa.residency == VSArgResidency::GPU);
                int numElems = vs_internal_vsapi.mapNumElements(callArgs, fa.name.c_str());
                for (int i = 0; i < numElems; i++) {
                    VSNode *node = vs_internal_vsapi.mapGetNode(callArgs, fa.name.c_str(), i, nullptr);
                    bool mismatched = node->isGPUOutput() != wantGPU;
                    if (mismatched) {
                        std::string boundaryError;
                        VSNode *wrapped = plugin->core->wrapGPUBoundary(node, wantGPU, boundaryError);
                        if (!wrapped) {
                            vs_internal_vsapi.freeNode(node);
                            throw VSException(name + ": " + boundaryError);
                        }
                        if (!fixedArgs) {
                            fixedArgs = std::make_unique<VSMap>(&args);
                            callArgs = fixedArgs.get();
                        }
                        /* Rebuild the whole key since elements cannot be replaced in place. */
                        std::vector<VSNode *> nodes;
                        for (int j = 0; j < numElems; j++)
                            nodes.push_back(j == i ? wrapped : vs_internal_vsapi.mapGetNode(fixedArgs.get(), fa.name.c_str(), j, nullptr));
                        for (int j = 0; j < numElems; j++)
                            vs_internal_vsapi.mapConsumeNode(fixedArgs.get(), fa.name.c_str(), nodes[j], j == 0 ? maReplace : maAppend);
                        plugin->core->logMessage(mtInformation, name + ": automatically inserted " +
                            (wantGPU ? "GPUUpload" : "GPUDownload") + " for argument '" + fa.name + "'");
                    }
                    vs_internal_vsapi.freeNode(node);
                }
            } else if (fa.type == ptVideoFrame && fa.residency != VSArgResidency::All) {
                /* Frames cannot be coerced by inserting a transfer node, so a residency
                   mismatch fails at invoke instead of at first pixel access. */
                bool wantGPU = (fa.residency == VSArgResidency::GPU);
                int numElems = vs_internal_vsapi.mapNumElements(callArgs, fa.name.c_str());
                for (int i = 0; i < numElems; i++) {
                    const VSFrame *frame = vs_internal_vsapi.mapGetFrame(callArgs, fa.name.c_str(), i, nullptr);
                    bool mismatched = frame->isGPUResident() != wantGPU;
                    vs_internal_vsapi.freeFrame(frame);
                    if (mismatched)
                        throw VSException(name + ": argument '" + fa.name + "' expects a " +
                            (wantGPU ? "GPU resident frame but a CPU frame was passed" : "CPU frame but a GPU resident frame was passed"));
                }
            }
        }

        bool enableGraphInspection = plugin->core->enableGraphInspection;
        if (enableGraphInspection) {
            plugin->core->functionFrame = std::make_shared<VSFunctionFrame>(name, plugin->getID(), plugin->getNamespace(), new VSMap(&args), plugin->core->functionFrame);
        }
        func(callArgs, v, functionData, plugin->core, getVSAPIInternal(plugin->apiMajor));
        if (enableGraphInspection) {
            assert(plugin->core->functionFrame);
            plugin->core->functionFrame = plugin->core->functionFrame->next;
        }

        if (plugin->apiMajor == VAPOURSYNTH3_API_MAJOR && !v->isV3Compatible())
            plugin->core->logFatal(name + ": filter node returned not yet supported type");

        if (!v->hasError()) {
            for (const auto &ra : retArgs) {
                if (ra.type != ptVideoNode || ra.residency == VSArgResidency::All)
                    continue;
                int numElems = vs_internal_vsapi.mapNumElements(v, ra.name.c_str());
                for (int i = 0; i < numElems; i++) {
                    VSNode *node = vs_internal_vsapi.mapGetNode(v, ra.name.c_str(), i, nullptr);
                    if (node->isGPUOutput() != (ra.residency == VSArgResidency::GPU))
                        plugin->core->logFatal(name + ": node returned under '" + ra.name + (ra.residency == VSArgResidency::GPU ?
                            "' is declared vnode:gpu but was not created with ffGPUOutput" :
                            "' was created with ffGPUOutput but is declared plain vnode"));
                    vs_internal_vsapi.freeNode(node);
                }
            }
        }

        // the declared return type can't be enforced retroactively without breaking existing plugins so only warn for now
        if (returnType != "any" && !v->hasError()) {
            std::string mismatch = checkValues(retArgs, *v, "return value");
            if (!mismatch.empty())
                plugin->core->logMessage(mtWarning, name + ": " + mismatch);
        }

    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(v, e.what());
    }

    return v;
}

bool VSPluginFunction::isV3Compatible() const {
    for (const auto &iter : inArgs)
        if (iter.type == ptAudioNode || iter.type == ptAudioFrame || iter.type == ptUnset || iter.residency == VSArgResidency::GPU)
            return false;
    for (const auto &iter : retArgs)
        if (iter.type == ptAudioNode || iter.type == ptAudioFrame || iter.type == ptUnset || iter.residency == VSArgResidency::GPU)
            return false;
    return true;
}

std::string VSPluginFunction::getV4ArgString() const {
    std::string tmp;
    for (const auto &iter : inArgs) {
        assert(iter.type != ptAudioNode && iter.type != ptAudioFrame);

        tmp += iter.name + ":";

        switch (iter.type) {
            case ptInt:
                tmp += "int"; break;
            case ptFloat:
                tmp += "float"; break;
            case ptData:
                tmp += "data"; break;
            case ptVideoNode:
                tmp += "vnode"; break;
            case ptVideoFrame:
                tmp += "vframe"; break;
            case ptFunction:
                tmp += "func"; break;
            default:
                assert(false);
        }
        if (iter.arr)
            tmp += "[]";
        if (iter.residency == VSArgResidency::GPU)
            tmp += ":gpu";
        else if (iter.residency == VSArgResidency::All)
            tmp += ":all";
        if (iter.opt)
            tmp += ":opt";
        if (iter.empty)
            tmp += ":empty";
        tmp += ";";
    }
    return tmp;
}

std::string VSPluginFunction::getV3ArgString() const {
    std::string tmp;
    for (const auto &iter : inArgs) {
        assert(iter.type != ptAudioNode && iter.type != ptAudioFrame);

        tmp += iter.name + ":";

        switch (iter.type) {
            case ptInt:
                tmp += "int"; break;
            case ptFloat:
                tmp += "float"; break;
            case ptData:
                tmp += "data"; break;
            case ptVideoNode:
                tmp += "clip"; break;
            case ptVideoFrame:
                tmp += "frame"; break;
            case ptFunction:
                tmp += "func"; break;
            default:
                assert(false);
        }
        if (iter.arr)
            tmp += "[]";
        if (iter.opt)
            tmp += ":opt";
        if (iter.empty)
            tmp += ":empty";
        tmp += ";";
    }
    return tmp;
}

const std::string &VSPluginFunction::getName() const {
    return name;
}

const std::string &VSPluginFunction::getArguments() const {
    return argString;
}

const std::string &VSPluginFunction::getReturnType() const {
    return returnType;
}

struct MakeLinearWrapper {
    vs3::VSFilterGetFrame getFrameFunc;
    VSFilterFree freeFunc;
    void *instanceData;
    int threshold;
    int lastFrame = -1;

    MakeLinearWrapper(VSFilterGetFrame g, VSFilterFree f, void *instanceData, int threshold) :
        getFrameFunc(reinterpret_cast<vs3::VSFilterGetFrame>(g)),
        freeFunc(f), instanceData(instanceData), threshold(threshold) {}

    static const VSFrame *VS_CC getFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
    static void VS_CC freeFilter(void *instanceData, VSCore *core, const VSAPI *vsapi);
};

VSNode::VSNode(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
    refcount(1), nodeType(mtVideo), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), serialFrame(-1), processingTime(0) {

    if (flags & ~(vs3::nfNoCache | vs3::nfIsCache | vs3::nfMakeLinear))
        throw VSException("Filter " + name  + " specified unknown flags");

    if ((flags & vs3::nfIsCache) && !(flags & vs3::nfNoCache))
        throw VSException("Filter " + name + " specified an illegal combination of flags (nfNoCache must always be set with nfIsCache)");


    VSMap inval(in);
    init(&inval, out, &this->instanceData, this, core, reinterpret_cast<const vs3::VSAPI3 *>(getVSAPIInternal(3)));

    if (out->hasError()) {
        throw VSException(vs_internal_vsapi.mapGetError(out));
    }

    if (vi.format.colorFamily == 0) {
        throw VSException("Filter " + name + " didn't set videoinfo");
    }

    if (vi.numFrames <= 0) {
        throw VSException("Filter " + name + " returned zero or negative frame count");
    }

    core->filterInstanceCreated();

    // Scan the in map for clips, these are probably the real dependencies
    // Worst case there are false positives and an extra cache gets activated
    // NoCache is generally the equivalent strict spatial for filters

    int requestPattern = !!(flags & vs3::nfNoCache) ? rpNoFrameReuse : rpGeneral;
    int numKeys = vs_internal_vsapi.mapNumKeys(in);

    bool makeLinear = (flags & vs3::nfMakeLinear);
    bool hasVideoNodes = false;
    for (int i = 0; i < numKeys; i++) {
        const char *key = vs_internal_vsapi.mapGetKey(in, i);
        if (vs_internal_vsapi.mapGetType(in, key) == ptVideoNode) {
            int numElems = vs_internal_vsapi.mapNumElements(in, key);
            for (int j = 0; j < numElems; j++) {
                hasVideoNodes = true;
                VSNode *sn = vs_internal_vsapi.mapGetNode(in, key, j, nullptr);
                this->dependencies.push_back({sn, requestPattern});
                sn->addConsumer(this, requestPattern);
            }
        }
    }

    if (makeLinear && !hasVideoNodes) {
        this->apiMajor = 4;
        MakeLinearWrapper *wrapper = new MakeLinearWrapper(filterGetFrame, freeFunc, this->instanceData, setLinear());
        filterGetFrame = MakeLinearWrapper::getFrame;
        this->freeFunc = MakeLinearWrapper::freeFilter;
        this->instanceData = reinterpret_cast<void *>(wrapper);
    }

    updateCacheState();
    if (cacheEnabled)
        registerCache(true);

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::VSNode(const std::string &name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor, VSCore *core) :
    refcount(1), nodeType(mtVideo), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), serialFrame(-1), processingTime(0) {

    if (!core->isValidVideoInfo(*vi))
        throw VSException("The VSVideoInfo structure passed by " + name + " is invalid.");

    this->vi = *vi;
    this->v3vi = core->VideoInfoToV3(*vi);

    core->filterInstanceCreated();

    this->dependencies.reserve(numDeps);
    for (int i = 0; i < numDeps; i++) {
        this->dependencies.push_back(dependencies[i]);
        dependencies[i].source->add_ref();
        dependencies[i].source->addConsumer(this, dependencies[i].requestPattern);
    }

    updateCacheState();
    if (cacheEnabled)
        registerCache(true);

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::VSNode(const std::string &name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor, VSCore *core) :
    refcount(1), nodeType(mtAudio), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), serialFrame(-1), processingTime(0) {

    if (!core->isValidAudioInfo(*ai))
        throw VSException("The VSAudioInfo structure passed by " + name + " is invalid.");

    this->ai = *ai;
    constexpr int64_t maxSamples = std::numeric_limits<int>::max() * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
    if (this->ai.numSamples > maxSamples)
        throw VSException("Filter " + name + " specified " + std::to_string(this->ai.numSamples) + " output samples but " + std::to_string(maxSamples) + " samples is the upper limit");
    this->ai.numFrames = static_cast<int>((this->ai.numSamples + VS_AUDIO_FRAME_SAMPLES - 1) / VS_AUDIO_FRAME_SAMPLES);

    core->filterInstanceCreated();

    this->dependencies.reserve(numDeps);
    for (int i = 0; i < numDeps; i++) {
        this->dependencies.push_back(dependencies[i]);
        dependencies[i].source->add_ref();
        dependencies[i].source->addConsumer(this, dependencies[i].requestPattern);
    }

    updateCacheState();
    if (cacheEnabled)
        registerCache(true);

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::~VSNode() {
    registerCache(false);

    cache.clear();

    for (auto &iter : dependencies) {
        iter.source->removeConsumer(this, iter.requestPattern);
        iter.source->release();
    }

    core->destroyFilterInstance(this);
}

void VSNode::registerCache(bool add) {
    std::lock_guard<std::mutex> lock(core->cacheLock);
    if (add) {
        core->caches.insert(this);
    } else {
        core->caches.erase(this);
    }
}

void VSNode::updateCacheState() {
    if (!cacheOverride) {
        cacheEnabled = (consumers.size() != 1) || (consumers.size() == 1 && consumers[0].requestPattern != rpStrictSpatial && consumers[0].requestPattern != rpNoFrameReuse);
        cacheLastOnly = (consumers.size() == 1) && (consumers[0].requestPattern == rpFrameReuseLastOnly);

        if (!cacheEnabled)
            cache.clear();
    }
}

void VSNode::addConsumer(VSNode *consumer, int strictSpatial) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        consumers.push_back({consumer, strictSpatial});

        updateCacheState();
    }
    registerCache(cacheEnabled);
}

void VSNode::removeConsumer(VSNode *consumer, int strictSpatial) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto iter = consumers.begin(); iter != consumers.end(); ++iter) {
            if (iter->source == consumer && iter->requestPattern == strictSpatial) {
                consumers.erase(iter);
                break;
            }
        }

        updateCacheState();
    }
    registerCache(cacheEnabled);
}


void VSNode::getFrame(const PVSFrameContext &ct) {
    core->threadPool->startExternal(ct);
}

const VSVideoInfo &VSNode::getVideoInfo() const {
    return vi;
}

const vs3::VSVideoInfo &VSNode::getVideoInfo3() const {
    return v3vi;
}

const VSAudioInfo &VSNode::getAudioInfo() const {
    return ai;
}

void VSNode::setVideoInfo3(const vs3::VSVideoInfo *vi, int numOutputs) {
    if (numOutputs < 1)
        core->logFatal("setVideoInfo: Video filter " + name + " needs to have at least one output");
    if (numOutputs > 1)
        core->logMessage(mtWarning, "setVideoInfo: Video filter " + name + " has more than one output node but only the first one will be returned");

    if ((!!vi->height) ^ (!!vi->width))
        core->logFatal("setVideoInfo: Variable dimension clips must have both width and height set to 0");
    if (vi->format && !core->isValidFormatPointer(vi->format))
        core->logFatal("setVideoInfo: The VSVideoFormat pointer passed by " + name + " was not obtained from registerFormat() or getFormatPreset()");
    int64_t num = vi->fpsNum;
    int64_t den = vi->fpsDen;
    reduceRational(&num, &den);
    if (num != vi->fpsNum || den != vi->fpsDen)
        core->logFatal("setVideoInfo: The frame rate specified by " + name + " must be a reduced fraction. Instead, it is " + std::to_string(vi->fpsNum) + "/" + std::to_string(vi->fpsDen) + ")");

    this->v3vi = *vi;
    this->v3vi.flags = vs3::nfNoCache | vs3::nfIsCache;
    this->vi = core->VideoInfoFromV3(this->v3vi);
}

const char *VSNode::getCreationFunctionName(int level) const {
    if (core->enableGraphInspection) {
        VSFunctionFrame *frame = functionFrame.get();
        for (int i = 0; i < level; i++) {
            if (frame)
                frame = frame->next.get();
        }

        if (frame)
            return frame->name.c_str();
    }
    return nullptr;
}

const char *VSNode::getNodeCreationPluginID(int level) const {
    if (core->enableGraphInspection) {
        VSFunctionFrame *frame = functionFrame.get();
        for (int i = 0; i < level; i++) {
            if (frame)
                frame = frame->next.get();
        }

        if (frame)
            return frame->pluginID.c_str();
    }
    return nullptr;
}


const char *VSNode::getNodeCreationPluginNS(int level) const {
    if (core->enableGraphInspection) {
        VSFunctionFrame *frame = functionFrame.get();
        for (int i = 0; i < level; i++) {
            if (frame)
                frame = frame->next.get();
        }

        if (frame)
            return frame->ns.c_str();
    }
    return nullptr;
}



const VSMap *VSNode::getCreationFunctionArguments(int level) const {
    if (core->enableGraphInspection) {
        VSFunctionFrame *frame = functionFrame.get();
        for (int i = 0; i < level; i++) {
            if (frame)
                frame = frame->next.get();
        }

        if (frame)
            return frame->args;
    }
    return nullptr;
}

int VSNode::setLinear() {
    {
        size_t threadCount = core->threadPool->threadCount();
        std::lock_guard<std::mutex> lock(cacheMutex);
        cacheLinear = true;
        cacheOverride = true;
        cacheEnabled = true;
        cacheLastOnly = false;
        cache.setFixedSize(true);
        cache.setMaxFrames(static_cast<int>(threadCount) * 2 + 20);
    }
    registerCache(cacheEnabled);
    return cache.getMaxFrames() / 2;
}

void VSNode::setCacheMode(int mode) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);

        if (cacheLinear || mode < -1 || mode > 1) {
            // simply disregard cache mode changes for linear filters
            return;
        }

        if (mode == -1) {
            cacheOverride = false;
            updateCacheState();
        } else if (mode == 0) {
            cacheOverride = true;
            cacheEnabled = false;
            cacheLastOnly = false;
        } else if (mode == 1) {
            cacheOverride = true;
            cacheEnabled = true;
            cacheLastOnly = false;
        }

        // always reset to defaults on mode change
        cache.setFixedSize(false);
        cache.resetSizeTuning();
        if (!cacheEnabled)
            cache.clear();
    }
    registerCache(cacheEnabled);
}

void VSNode::setCacheOptions(int fixedSize, int maxSize, int maxHistorySize) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (fixedSize >= 0)
        cache.setFixedSize(!!fixedSize);
    if (maxSize >= 0)
        cache.setUserMaxFrames(maxSize);
    if (maxHistorySize >= 0)
        cache.setMaxHistory(maxHistorySize);
}

PVSFrame VSNode::getCachedFrameInternal(int n) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cacheEnabled)
        return cache.object(n);
    else
        return nullptr;
}

PVSFrame VSNode::getFrameInternal(int n, int activationReason, VSFrameContext *frameCtx) {
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    bool enableFilterTiming = core->enableFilterTiming;
    if (enableFilterTiming)
        startTime = std::chrono::high_resolution_clock::now();

    vs::MemoryUse::CallTracking savedTracking = vs::MemoryUse::begin_call_tracking();

    core->currentProcessingNode = this;
    const VSFrame *r = (apiMajor == VAPOURSYNTH_API_MAJOR) ? filterGetFrame(n, activationReason, instanceData, frameCtx->frameContext, frameCtx, core, &vs_internal_vsapi) : reinterpret_cast<vs3::VSFilterGetFrame>(filterGetFrame)(n, activationReason, &instanceData, frameCtx->frameContext, frameCtx, core, &vs_internal_vsapi3);
    core->currentProcessingNode = nullptr;

    vs::MemoryUse::CallPeaks callPeaks = vs::MemoryUse::end_call_tracking(savedTracking);
    updateTransientAllocEstimate(callPeaks.host, callPeaks.gpu);

    if (enableFilterTiming) {
        std::chrono::nanoseconds duration = std::chrono::high_resolution_clock::now() - startTime;
        processingTime.fetch_add(duration.count(), std::memory_order_relaxed);
    }
#ifdef VS_TARGET_CPU_X86
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after return from "+ name);
#endif

    if (r) {
        assert(r->getFrameType());

        if (r->getFrameType() == mtVideo) {
            const VSVideoFormat *fi = r->getVideoFormat();

            if (vi.format.colorFamily != cfUndefined && !isSameVideoFormat(&vi.format, fi))
                core->logFatal("Filter " + name + " returned a frame that's not of the declared format");
            else if ((vi.width || vi.height) && (r->getWidth(0) != vi.width || r->getHeight(0) != vi.height))
                core->logFatal("Filter " + name + " declared the size " + std::to_string(vi.width) + "x" + std::to_string(vi.height) + ", but it returned a frame with the size " + std::to_string(r->getWidth(0)) + "x" + std::to_string(r->getHeight(0)));
            else if (r->isGPUResident() != gpuOutput)
                core->logFatal("Filter " + name + (gpuOutput ?
                    " is declared to return GPU resident frames but returned a CPU frame" :
                    " returned a GPU resident frame without being created with ffGPUOutput"));
        } else {
            const VSAudioFormat *fi = r->getAudioFormat();

            int expectedSamples = (n < ai.numFrames - 1) ? VS_AUDIO_FRAME_SAMPLES : (((ai.numSamples % VS_AUDIO_FRAME_SAMPLES) ? (ai.numSamples % VS_AUDIO_FRAME_SAMPLES) : VS_AUDIO_FRAME_SAMPLES));

            if (ai.format.bitsPerSample != fi->bitsPerSample || ai.format.sampleType != fi->sampleType || ai.format.channelLayout != fi->channelLayout) {
                core->logFatal("Filter " + name + " returned a frame that's not of the declared format");
            } else if (expectedSamples != r->getFrameLength()) {
                core->logFatal("Filter " + name + " returned audio frame with " + std::to_string(r->getFrameLength()) + " samples but " + std::to_string(expectedSamples) + " expected from declared length");
            }
        }

#ifdef VS_FRAME_GUARD
        if (!r->verifyGuardPattern())
            core->logFatal("Guard memory corrupted in frame " + std::to_string(n) + " returned from " + name);
#endif

        PVSFrame ref(const_cast<VSFrame *>(r));

        if (cacheEnabled) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            int lastFrame = (nodeType == mtVideo ? vi.numFrames : ai.numFrames) - 1;
            if (cacheEnabled && (!cacheLastOnly || n == lastFrame))
                cache.insert(n, ref);
        }

        return ref;
    }

    return nullptr;
}

const VSFrame *VS_CC MakeLinearWrapper::getFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MakeLinearWrapper *node = reinterpret_cast<MakeLinearWrapper *>(instanceData);

    if (activationReason == arInitial) {
        const vs3::VSAPI3 *vsapi3 = reinterpret_cast<const vs3::VSAPI3 *>(getVSAPIInternal(3));
        if (node->lastFrame < n && node->lastFrame > n - node->threshold) {
            for (int i = node->lastFrame + 1; i < n; i++) {
                const VSFrame *frame = node->getFrameFunc(i, activationReason, &node->instanceData, frameData, frameCtx, core, vsapi3);
                // exit if an error was set (or later trigger a fatal error if we accidentally wrapped a filter that requests frames)
                if (!frame)
                    return nullptr;
                vsapi->cacheFrame(frame, i, frameCtx);
                vsapi->freeFrame(frame);
            }
        }

        const VSFrame *frame = node->getFrameFunc(n, activationReason, &node->instanceData, frameData, frameCtx, core, vsapi3);
        node->lastFrame = n;
        return frame;
    }

    return nullptr;
}

void VS_CC MakeLinearWrapper::freeFilter(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    MakeLinearWrapper *node = reinterpret_cast<MakeLinearWrapper *>(instanceData);
    if (node->freeFunc)
        node->freeFunc(node->instanceData, core, getVSAPIInternal(3));
    delete node;
}

void VSNode::cacheFrame(const VSFrame *frame, int n) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    assert(cacheLinear);
    cache.insert(n, {const_cast<VSFrame *>(frame), true});
}

void VSNode::clearCache(bool resetSize) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.clear();
    if (resetSize && !cacheLinear && !cache.getFixedSize())
        cache.setMaxFrames(cache.reSeedSize());
}

void VSNode::notifyCache(bool memoryComfortable, uint64_t completedExtFrames) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.adjustSize(memoryComfortable, completedExtFrames);
}

VSNode::CachePressureInfo VSNode::getCachePressureInfo() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return { cache.bytesHeld(), cache.recentValue(), cache.getFixedSize() };
}

size_t VSNode::evictCacheBytes(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cache.dropLRUFrames(maxBytes);
}

void VSNode::updateTransientAllocEstimate(int64_t hostSample, int64_t gpuSample) {
    // biased towards remembering peaks since rare big allocations are exactly what admission
    // control in the thread pool needs to predict, decays slowly when calls allocate less
    int64_t est = transientAllocEstimate.load(std::memory_order_relaxed);
    if (est >= 0)
        hostSample = std::max(hostSample, est - (est >> 7));
    transientAllocEstimate.store(hostSample, std::memory_order_relaxed);

    est = gpuTransientAllocEstimate.load(std::memory_order_relaxed);
    if (est >= 0)
        gpuSample = std::max(gpuSample, est - (est >> 7));
    gpuTransientAllocEstimate.store(gpuSample, std::memory_order_relaxed);
}

int64_t VSNode::expectedGPUTransientAllocation() const {
    int64_t est = gpuTransientAllocEstimate.load(std::memory_order_relaxed);
    if (est >= 0)
        return est;
    if (!gpuOutput)
        return 0;
    /* Same first call guess as the host side: the output frame plus as much again in scratch. */
    int64_t frameBytes = 0;
    if (nodeType == mtVideo && vi.width > 0 && vi.height > 0) {
        for (int p = 0; p < vi.format.numPlanes; p++) {
            int64_t w = vi.width >> (p ? vi.format.subSamplingW : 0);
            int64_t h = vi.height >> (p ? vi.format.subSamplingH : 0);
            frameBytes += w * h * vi.format.bytesPerSample;
        }
    }
    return 2 * frameBytes;
}

int64_t VSNode::expectedTransientAllocation() const {
    int64_t est = transientAllocEstimate.load(std::memory_order_relaxed);
    if (est >= 0)
        return est;
    // no call measured yet, assume the output frame plus as much again in temporary allocations,
    // variable format clips simply get no estimate until the first call completes
    int64_t frameBytes = 0;
    if (nodeType == mtVideo) {
        if (vi.width > 0 && vi.height > 0) {
            for (int p = 0; p < vi.format.numPlanes; p++) {
                int64_t w = vi.width >> (p ? vi.format.subSamplingW : 0);
                int64_t h = vi.height >> (p ? vi.format.subSamplingH : 0);
                frameBytes += w * h * vi.format.bytesPerSample;
            }
        }
    } else {
        frameBytes = static_cast<int64_t>(ai.format.bytesPerSample) * ai.format.numChannels * VS_AUDIO_FRAME_SAMPLES;
    }
    return 2 * frameBytes;
}

void VSCore::notifyCaches(bool hostNeedsMemory, bool gpuNeedsMemory) {
    uint64_t completedExtFrames = threadPool->getCompletedExternalFrames();
    std::lock_guard<std::mutex> lock(cacheLock);

    /* Exec pool retentions whose submissions have completed are pure dead weight — the
       sources and scratch of work that already finished, normally dropped at the context's
       next acquire. A pool gone idle never acquires again and parks its whole in-flight
       footprint, which for a deep chain of heavy filters is gigabytes, so every sweep
       releases what has completed; steady state pools lose nothing they would not have
       released moments later anyway. */
    if (vulkanDev)
        vulkanDev->sweepExecPools();

    if (hostNeedsMemory || gpuNeedsMemory) {
        // free the excess in a single pass by taking frames from the caches where each held byte
        // has demonstrably provided the least value instead of uniformly decaying every cache;
        // host and GPU memory are separate pools with separate excesses, and a cache only holds
        // frames of its node's residency, so eviction targets the pool that is actually over
        auto poolExcess = [](bool over, size_t allocated, size_t limit) -> size_t {
            if (!over)
                return 0;
            size_t targetUsage = limit - limit / 10;
            return (allocated > targetUsage) ? allocated - targetUsage : 0;
        };
        size_t hostExcess = poolExcess(hostNeedsMemory, memory->allocated_bytes(), memory->limit());
        size_t gpuExcess = poolExcess(gpuNeedsMemory, memory->gpu_allocated_bytes(), memory->gpu_limit());

        const bool combinedPressure = memory->over_combined_limit();

        struct CacheEntry {
            VSNode *node;
            int value;
            bool gpu;
        };

        std::vector<CacheEntry> entries;
        entries.reserve(caches.size());
        for (auto &node : caches) {
            VSNode::CachePressureInfo info = node->getCachePressureInfo();
            if (!info.fixedSize && info.bytes > 0)
                entries.push_back({ node, info.value, node->isGPUOutput() });
        }

        // take frames from the caches that provided the least value recently, caches without a
        // single hit or near miss hold pure dead weight and naturally sort first
        std::sort(entries.begin(), entries.end(), [](const CacheEntry &a, const CacheEntry &b) {
            return a.value < b.value;
        });

        for (const CacheEntry &entry : entries) {
            size_t &excess = entry.gpu ? gpuExcess : hostExcess;
            if (excess == 0)
                continue;
            excess -= std::min(entry.node->evictCacheBytes(excess), excess);
            if (hostExcess == 0 && gpuExcess == 0)
                break;
        }

        // evicted GPU frames only return regions to the buckets; trimming hands fully idle
        // blocks back to the driver so the freed VRAM is real for the rest of the system,
        // which on unified memory is the host pool too. Worth attempting under combined
        // pressure even when the GPU pool is inside its own limit: it is the only action
        // that returns VRAM at all, and it costs a walk of the block list when it fails.
        if ((gpuNeedsMemory || combinedPressure) && vulkanDev)
            vulkanDev->trimAllocator();
    } else {
        // caches only get to grow while memory usage stays comfortably under the limit of the
        // pool their frames actually live in
        size_t memLimit = memory->limit();
        bool memoryComfortable = memory->allocated_bytes() < memLimit - memLimit * 3 / 20;
        size_t gpuLimit = memory->gpu_limit();
        bool gpuComfortable = gpuLimit == 0 || memory->gpu_allocated_bytes() < gpuLimit - gpuLimit * 3 / 20;
        /* Blocks whose regions all sit banked in the free lists cost the rest of the system
           real VRAM — the driver has them committed however idle they are, and on a card
           whose limit is most of its memory that is the compositor's working set. A burst
           leaves this state behind; steady use never accumulates much banked at once, so the
           quarter-limit threshold keeps the trim from fighting the recycling it exists for. */
        if (vulkanDev && gpuLimit) {
            VSVulkanAllocatorStats stats = vulkanDev->allocatorStats();
            if (stats.blockBytes - stats.usedBytes > gpuLimit / 4)
                vulkanDev->trimAllocator();
        }
        /* Neither pool gets to grow on a share of RAM the other is already using. */
        if (memory->unified() && memory->combined_limit()) {
            size_t ceiling = memory->combined_limit();
            bool combinedComfortable =
                (memory->allocated_bytes() + memory->gpu_allocated_bytes()) < ceiling - ceiling * 3 / 20;
            memoryComfortable = memoryComfortable && combinedComfortable;
            gpuComfortable = gpuComfortable && combinedComfortable;
        }
        for (auto &cache : caches)
            cache->notifyCache(cache->isGPUOutput() ? gpuComfortable : memoryComfortable, completedExtFrames);
    }
}

const vs3::VSVideoFormat *VSCore::getV3VideoFormat(int id) {
    std::lock_guard<std::mutex> lock(videoFormatLock);

    auto f = videoFormats.find(id);
    if (f != videoFormats.end())
        return &f->second;

    return nullptr;
}

const vs3::VSVideoFormat *VSCore::getVideoFormat3(int id) {
    if ((id & 0xFF000000) == 0 && (id & 0x00FFFFFF)) {
        return getV3VideoFormat(id);
    } else {
        return queryVideoFormat3(ColorFamilyToV3((id >> 28) & 0xF), static_cast<VSSampleType>((id >> 24) & 0xF), (id >> 16) & 0xFF, (id >> 8) & 0xFF, (id >> 0) & 0xFF);
    }
}

bool VSCore::queryVideoFormat(VSVideoFormat &f, VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) noexcept {
    f = {};
    if (colorFamily == cfUndefined)
        return true;

    if (!isValidVideoFormat(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH))
        return false;

    f.colorFamily = colorFamily;
    f.sampleType = sampleType;
    f.bitsPerSample = bitsPerSample;
    f.bytesPerSample = 1;

    while (f.bytesPerSample * 8 < bitsPerSample)
        f.bytesPerSample <<= 1;

    f.subSamplingW = subSamplingW;
    f.subSamplingH = subSamplingH;
    f.numPlanes = (colorFamily == cfGray) ? 1 : 3;

    return true;
}

bool VSCore::getVideoFormatByID(VSVideoFormat &f, uint32_t id) noexcept {
    // is a V3 id?
    if ((id & 0xFF000000) == 0 && (id & 0x00FFFFFF)) {
        return VideoFormatFromV3(f, getV3VideoFormat(id));
    } else {
        return queryVideoFormat(f, static_cast<VSColorFamily>((id >> 28) & 0xF), static_cast<VSSampleType>((id >> 24) & 0xF), (id >> 16) & 0xFF, (id >> 8) & 0xFF, (id >> 0) & 0xFF);
    }
}

uint32_t VSCore::queryVideoFormatID(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) const noexcept {
    if (!isValidVideoFormat(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH) || colorFamily == cfUndefined)
        return 0;
    return ((colorFamily & 0xF) << 28) | ((sampleType & 0xF) << 24) | ((bitsPerSample & 0xFF) << 16) | ((subSamplingW & 0xFF) << 8) | ((subSamplingH & 0xFF) << 0);
}

const vs3::VSVideoFormat *VSCore::queryVideoFormat3(vs3::VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name, int id) noexcept {
    if (subSamplingH < 0 || subSamplingW < 0 || subSamplingH > 4 || subSamplingW > 4)
        return nullptr;

    if (sampleType < 0 || sampleType > 1)
        return nullptr;

    if (colorFamily == vs3::cmRGB && (subSamplingH != 0 || subSamplingW != 0))
        return nullptr;

    if (sampleType == stFloat && (bitsPerSample != 16 && bitsPerSample != 32))
        return nullptr;

    if (bitsPerSample < 8 || bitsPerSample > 32)
        return nullptr;

    if (colorFamily == vs3::cmCompat && !name)
        return nullptr;

    std::lock_guard<std::mutex> lock(videoFormatLock);

    for (const auto &iter : videoFormats) {
        const vs3::VSVideoFormat &f = iter.second;

        if (f.colorFamily == colorFamily && f.sampleType == sampleType
                && f.subSamplingW == subSamplingW && f.subSamplingH == subSamplingH && f.bitsPerSample == bitsPerSample)
            return &f;
    }

    vs3::VSVideoFormat f{};

    if (name) {
        strcpy(f.name, name);
    } else {
        char suffix[16];
        if (sampleType == stFloat)
            strcpy(suffix, (bitsPerSample == 32) ? "S" : "H");
        else
            snprintf(suffix, sizeof(suffix), "%d", (colorFamily == vs3::cmRGB ? 3:1) * bitsPerSample);

        const char *yuvName = nullptr;

        switch (colorFamily) {
        case vs3::cmGray:
            snprintf(f.name, sizeof(f.name), "Gray%s", suffix);
            break;
        case vs3::cmRGB:
            snprintf(f.name, sizeof(f.name), "RGB%s", suffix);
            break;
        case vs3::cmYUV:
            if (subSamplingW == 1 && subSamplingH == 1)
                yuvName = "420";
            else if (subSamplingW == 1 && subSamplingH == 0)
                yuvName = "422";
            else if (subSamplingW == 0 && subSamplingH == 0)
                yuvName = "444";
            else if (subSamplingW == 2 && subSamplingH == 2)
                yuvName = "410";
            else if (subSamplingW == 2 && subSamplingH == 0)
                yuvName = "411";
            else if (subSamplingW == 0 && subSamplingH == 1)
                yuvName = "440";
            if (yuvName)
                snprintf(f.name, sizeof(f.name), "YUV%sP%s", yuvName, suffix);
            else
                snprintf(f.name, sizeof(f.name), "YUVssw%dssh%dP%s", subSamplingW, subSamplingH, suffix);
            break;
        case vs3::cmYCoCg:
            snprintf(f.name, sizeof(f.name), "YCoCgssw%dssh%dP%s", subSamplingW, subSamplingH, suffix);
            break;
        default:;
        }
    }

    if (id != 0)
        f.id = id;
    else
        f.id = colorFamily + videoFormatIdOffset++;

    f.colorFamily = colorFamily;
    f.sampleType = sampleType;
    f.bitsPerSample = bitsPerSample;
    f.bytesPerSample = 1;

    while (f.bytesPerSample * 8 < bitsPerSample)
        f.bytesPerSample *= 2;

    f.subSamplingW = subSamplingW;
    f.subSamplingH = subSamplingH;
    f.numPlanes = (colorFamily == vs3::cmGray || colorFamily == vs3::cmCompat) ? 1 : 3;

    videoFormats.insert(std::make_pair(f.id, f));
    return &videoFormats[f.id];
}

bool VSCore::queryAudioFormat(VSAudioFormat &f, VSSampleType sampleType, int bitsPerSample, uint64_t channelLayout) noexcept {
    if (!isValidAudioFormat(sampleType, bitsPerSample, channelLayout))
        return false;

    f = {};

    f.sampleType = sampleType;
    f.bitsPerSample = bitsPerSample;
    f.bytesPerSample = 1;

    while (f.bytesPerSample * 8 < bitsPerSample)
        f.bytesPerSample <<= 1;

    std::bitset<sizeof(channelLayout) * 8> bits{ channelLayout };
    f.numChannels = static_cast<int>(bits.count());
    f.channelLayout = channelLayout;

    return true;
}

bool VSCore::isValidFormatPointer(const void *f) {
    std::lock_guard<std::mutex> lock(videoFormatLock);

    for (const auto &iter : videoFormats) {
        if (&iter.second == f)
            return true;
    }
    return false;
}

VSLogHandle *VSCore::addLogHandler(VSLogHandler handler, VSLogHandlerFree freeFunc, void *userData) {
    std::lock_guard<std::recursive_mutex> lock(logMutex);
    VSLogHandle *handle = *(messageHandlers.insert(new VSLogHandle{ handler, freeFunc, userData }).first);

    for (const auto &iter : storedMessages)
        handler(iter.first, iter.second.c_str(), userData);
    if (storedMessages.size() == maxStoredLogMessages)
        handler(mtWarning, "Log messages after this point may have been discarded due to the buffer reaching its max size", userData);
    storedMessages.clear();
    return handle;
}

bool VSCore::removeLogHandler(VSLogHandle *rec) {
    std::lock_guard<std::recursive_mutex> lock(logMutex);
    auto f = messageHandlers.find(rec);
    if (f != messageHandlers.end()) {
        delete rec;
        messageHandlers.erase(f);
        return true;
    } else {
        return false;
    }
}

void VSCore::logMessage(VSMessageType type, const char *msg) {
    assert(msg);
    std::lock_guard<std::recursive_mutex> lock(logMutex);
    for (auto iter : messageHandlers)
        iter->handler(type, msg, iter->userData);
    if (messageHandlers.empty() && storedMessages.size() < maxStoredLogMessages)
        storedMessages.push_back(std::make_pair(type, msg));

    switch (type) {
        case mtDebug:
            vsLog3(vs3::mtDebug, "%s", msg);
            break;
        case mtInformation:
        case mtWarning:
            vsLog3(vs3::mtWarning, "%s", msg);
            break;
        case mtCritical:
            vsLog3(vs3::mtCritical, "%s", msg);
            break;
        case mtFatal:
            vsLog3(vs3::mtFatal, "%s", msg);
            break;
    }

    if (type == mtFatal) {
        fprintf(stderr, "VapourSynth encountered a fatal error: %s\n", msg);
        assert(false);
        std::terminate();
    }
}

void VSCore::logMessage(VSMessageType type, const std::string &msg) {
    logMessage(type, msg.c_str());
}

[[noreturn]] void VSCore::logFatal(const char *msg) {
    logMessage(mtFatal, msg);
    std::terminate();
}

[[noreturn]] void VSCore::logFatal(const std::string &msg) {
    logMessage(mtFatal, msg);
    std::terminate();
}

static void vulkanLogBridge(int severity, const char *message, void *userData) {
    VSCore *core = static_cast<VSCore *>(userData);
    VSMessageType type = mtInformation;
    if (severity == VS_VK_LOG_ERROR)
        type = mtCritical;
    else if (severity == VS_VK_LOG_WARNING)
        type = mtWarning;
    core->logMessage(type, message);
}

bool VSCore::createVulkanDeviceLocked(int deviceIndex) {
    vulkanDeviceTried = true;
    auto dev = std::make_unique<VSVulkanDevice>();
    dev->setLogCallback(vulkanLogBridge, this);
    /* Validation is a development switch, so an environment variable rather than API surface. */
    if (!dev->create(deviceIndex, std::getenv("VS_VULKAN_VALIDATION") != nullptr, vulkanDeviceError))
        return false;

    /* VRAM flows into the same MemoryUse as host memory, wired before the first pooled
       allocation can happen. The callback context cannot dangle: MemoryUse gates its post
       core self delete on the GPU pool too, so it lives at least until the last region is
       returned through here. */
    dev->setAllocationCallback([](int64_t delta, void *userData) {
        static_cast<vs::MemoryUse *>(userData)->account_gpu(delta);
    }, memory);
    /* The allocator's last resort before failing a frame: evict every cached GPU frame.
       Blunt on purpose — this only runs once the driver has already refused an allocation,
       where half measures just fail again a few frames later. Cleared in the destructor
       because the device can outlive the core. */
    dev->setPressureCallback([](void *userData) {
        static_cast<VSCore *>(userData)->gpuMemoryPanic();
    }, this);
    size_t budget = static_cast<size_t>(dev->memoryBudget());
    /* Two thirds of the live budget, no more: the remainder has to absorb everything the limit
       does not cover -- desktop fluctuation after this one-time sample, admission overshoot,
       and above all the transient working sets of large filters, whose per-call estimates begin
       as guesses of twice the output frame and only learn from completed calls, so a heavy
       filter's first wave lands before the pressure machinery has numbers. A fifth of headroom
       left enforcement no room on a card whose desktop already held a gigabyte; scripts wanting
       more raise it with one setMaxVRAMUse call. */
    size_t defaultLimit = budget - budget / 3;

    /* Unified memory: the budget's heap is the same RAM the host limit is drawn against, so
       the two defaults would together promise more of the machine than it has -- half again as
       much on a device reporting all of RAM as device local. The GPU share yields, GPU
       residency being opt-in while the host limit may have been set deliberately.

       An eighth of RAM, because what the GPU pool takes it gives back slowly: blocks are big so
       a single large frame always fits, and a block only returns to the driver once every
       region in it is free, so one surviving cached frame holds 128 MB. Claiming too little
       costs a setMaxVRAMUse call; claiming too much cannot be undone when the host needs it
       back. */
    const size_t combinedCeiling = memory->physical_memory() - memory->physical_memory() / 4;
    if (dev->unifiedMemory() && memory->physical_memory()) {
        const size_t hostLimit = memory->limit();
        const size_t headroom = combinedCeiling > hostLimit ? combinedCeiling - hostLimit : 0;
        defaultLimit = std::min(defaultLimit, memory->physical_memory() / 8);
        defaultLimit = std::min(defaultLimit, headroom);
        /* A host limit large enough to swallow the ceiling would otherwise leave nothing at
           all, and a zero GPU limit stalls every GPU task in the thread pool's admission
           control rather than merely making the cache small. */
        defaultLimit = std::max(defaultLimit, static_cast<size_t>(256) << 20);
        memory->set_unified(combinedCeiling);
    }

    /* The override exists for testing the pressure paths */
    if (const char *envLimit = std::getenv("VS_VULKAN_MAX_VRAM_MB"))
        memory->set_gpu_limit(static_cast<size_t>(std::strtoull(envLimit, nullptr, 10)) << 20);
    else if (!memory->gpu_limit())
        memory->set_gpu_limit(defaultLimit);
    /* A quarter of the limit for queued-but-unexecuted submissions: enough depth to hide
       recording behind execution many times over, while the rest of the budget stays with
       frames that have reuse value. setMaxVRAMUse keeps it in step. */
    dev->setExecRetainedBudget(memory->gpu_limit() / 4);

    auto trans = std::make_unique<VSVulkanTransfer>();
    if (!trans->init(*dev, 4, vulkanDeviceError))
        return false;
    /* Makes the transfer machinery take its staging paths even where a direct one is
       available, which is otherwise decided by the memory the driver handed back and so
       cannot be varied on a given machine. The point is to be able to measure the two
       against each other: the direct upload has the CPU write the plane across the BAR,
       while staging hands the copy to the DMA engine, and which of those wins is a property
       of the platform rather than something to assume. */
    if (std::getenv("VS_VULKAN_FORCE_STAGING"))
        trans->setForceStaging(true);
    vulkanDev = dev.release(); /* the core's reference; the device was born with it counted */
    vulkanTrans = std::move(trans);
    /* Silently halving a limit is the kind of thing that costs someone an afternoon later, so
       the unified case says what it did and what the two pools now add up to. */
    std::string limitInfo = "VRAM limit " + std::to_string(memory->gpu_limit() >> 20) +
        " MB of " + std::to_string(budget >> 20) + " MB budget";
    if (memory->unified())
        limitInfo += ", unified memory so it shares system RAM with the " +
            std::to_string(memory->limit() >> 20) + " MB host limit (combined ceiling " +
            std::to_string(memory->combined_limit() >> 20) + " MB)";
    logMessage(mtInformation, "Vulkan device: " + std::string(vulkanDev->properties().deviceName) +
        ", " + limitInfo);
    return true;
}

VSVulkanTransfer *VSCore::vulkanTransfer(std::string &errorMessage) {
    vulkanDevice(errorMessage);
    return vulkanTrans.get();
}

VSNode *VSCore::wrapGPUBoundary(VSNode *node, bool toGPU, std::string &errorMessage) {
    VSPlugin *stdPlugin = getPluginByID(VSH_STD_PLUGIN_ID);
    assert(stdPlugin);

    VSMap in;
    vs_internal_vsapi.mapSetNode(&in, "clip", node, maAppend);
    VSMap *out = stdPlugin->invoke(toGPU ? "GPUUpload" : "GPUDownload", in);
    const char *invokeError = vs_internal_vsapi.mapGetError(out);
    if (invokeError) {
        errorMessage = invokeError;
        vs_internal_vsapi.freeMap(out);
        return nullptr;
    }
    VSNode *wrapped = vs_internal_vsapi.mapGetNode(out, "clip", 0, nullptr);
    vs_internal_vsapi.freeMap(out);
    return wrapped;
}

VSVulkanDevice *VSCore::vulkanDevice(std::string &errorMessage) {
    std::lock_guard<std::mutex> lock(vulkanDeviceLock);
    if (!vulkanDeviceTried && !createVulkanDeviceLocked(-1))
        logMessage(mtWarning, "Vulkan device creation failed: " + vulkanDeviceError);
    if (!vulkanDev) {
        errorMessage = vulkanDeviceError;
        return nullptr;
    }
    return vulkanDev;
}

bool VSCore::setVulkanDevice(int deviceIndex, std::string &errorMessage) {
    std::lock_guard<std::mutex> lock(vulkanDeviceLock);
    if (vulkanDeviceTried) {
        errorMessage = vulkanDev ? "setVulkanDevice must be called before the Vulkan device is first used"
            : "Vulkan device creation already failed: " + vulkanDeviceError;
        return false;
    }
    if (!createVulkanDeviceLocked(deviceIndex)) {
        errorMessage = vulkanDeviceError;
        return false;
    }
    return true;
}

bool VSCore::isValidVideoFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) noexcept {
    if (colorFamily != cfUndefined && colorFamily != cfGray && colorFamily != cfYUV && colorFamily != cfRGB)
        return false;

    if (colorFamily == cfUndefined && subSamplingH == 0 && subSamplingW == 0 && bitsPerSample == 0 && sampleType == stInteger)
        return true;

    if (sampleType != stInteger && sampleType != stFloat)
        return false;

    if (sampleType == stFloat && (bitsPerSample != 16 && bitsPerSample != 32))
        return false;

    if (subSamplingH < 0 || subSamplingW < 0 || subSamplingH > 4 || subSamplingW > 4)
        return false;

    if ((colorFamily == cfRGB || colorFamily == cfGray) && (subSamplingH != 0 || subSamplingW != 0))
        return false;

    if (bitsPerSample < 8 || bitsPerSample > 32)
        return false;

    return true;
}

bool VSCore::isValidVideoFormat(const VSVideoFormat &format) noexcept {
    if (!isValidVideoFormat(format.colorFamily, format.sampleType, format.bitsPerSample, format.subSamplingW, format.subSamplingH))
        return false;

    if (format.colorFamily == cfUndefined)
        return (format.bytesPerSample == 0 && format.numPlanes == 0);

    if (format.numPlanes != ((format.colorFamily == cfYUV || format.colorFamily == cfRGB) ? 3 : 1))
        return false;

    if (format.bitsPerSample == 8 && format.bytesPerSample != 1)
        return false;
    else if (format.bitsPerSample > 8 && format.bitsPerSample <= 16 && format.bytesPerSample != 2)
        return false;
    else if (format.bitsPerSample > 16 && format.bytesPerSample != 4)
        return false;

    return true;
}

bool VSCore::isValidAudioFormat(int sampleType, int bitsPerSample, uint64_t channelLayout) noexcept {
    if (sampleType != stInteger && sampleType != stFloat)
        return false;

    if (bitsPerSample < 16 || bitsPerSample > 32)
        return false;

    if (sampleType == stFloat && bitsPerSample != 32)
        return false;

    if (channelLayout == 0)
        return false;

    return true;
}

bool VSCore::isValidAudioFormat(const VSAudioFormat &format) noexcept {
    if (!isValidAudioFormat(format.sampleType, format.bitsPerSample, format.channelLayout))
        return false;

    std::bitset<sizeof(format.channelLayout) * 8> bits{ format.channelLayout };
    if (format.numChannels != static_cast<int>(bits.count()))
        return false;

    if (format.bitsPerSample == 16 && format.bytesPerSample != 2)
        return false;
    else if (format.bitsPerSample > 16 && format.bytesPerSample != 4)
        return false;

    return true;
}

bool VSCore::isValidVideoInfo(const VSVideoInfo &vi) noexcept {
    if (!isValidVideoFormat(vi.format))
        return false;

    if (vi.fpsDen < 0 || vi.fpsNum < 0 || vi.height < 0 || vi.width < 0 || vi.numFrames < 1)
        return false;

    int64_t num = vi.fpsNum;
    int64_t den = vi.fpsDen;
    reduceRational(&num, &den);
    if (num != vi.fpsNum || den != vi.fpsDen)
        return false;

    if ((!!vi.height) ^ (!!vi.width))
        return false;

    return true;
}

bool VSCore::isValidAudioInfo(const VSAudioInfo &ai) noexcept {
    if (!isValidAudioFormat(ai.format))
        return false;

    if (ai.numSamples < 1 || ai.sampleRate < 1)
        return false;

    // numFrames isn't checked since it's implicit and set correctly whenever a VSAudioInfo is consumed

    return true;
}

/////////////////////////////////////////////////
// V3 compatibility helpers

VSColorFamily VSCore::ColorFamilyFromV3(int colorFamily) noexcept {
    switch (colorFamily) {
        case vs3::cmGray:
            return cfGray;
        case vs3::cmYUV:
        case vs3::cmYCoCg:
            return cfYUV;
        case vs3::cmRGB:
            return cfRGB;
        default:
            assert(false);
            return cfGray;
    }
}

vs3::VSColorFamily VSCore::ColorFamilyToV3(int colorFamily) noexcept {
    switch (colorFamily) {
        case cfGray:
            return vs3::cmGray;
        case cfYUV:
            return vs3::cmYUV;
        case cfRGB:
            return vs3::cmRGB;
        default:
            assert(false);
            return vs3::cmGray;
    }
}

const vs3::VSVideoFormat *VSCore::VideoFormatToV3(const VSVideoFormat &format) noexcept {
    if (format.colorFamily == cfUndefined)
        return nullptr;
    else
        return queryVideoFormat3(ColorFamilyToV3(format.colorFamily), static_cast<VSSampleType>(format.sampleType), format.bitsPerSample, format.subSamplingW, format.subSamplingH);
}

bool VSCore::VideoFormatFromV3(VSVideoFormat &out, const vs3::VSVideoFormat *format) noexcept {
    if (!format || format->id == vs3::pfCompatBGR32 || format->id == vs3::pfCompatYUY2)
        return queryVideoFormat(out, cfUndefined, stInteger, 0, 0, 0);
    else
        return queryVideoFormat(out, ColorFamilyFromV3(format->colorFamily), static_cast<VSSampleType>(format->sampleType), format->bitsPerSample, format->subSamplingW, format->subSamplingH);
}

vs3::VSVideoInfo VSCore::VideoInfoToV3(const VSVideoInfo &vi) noexcept {
    vs3::VSVideoInfo v3 = {};
    v3.format = VideoFormatToV3(vi.format);
    v3.fpsDen = vi.fpsDen;
    v3.fpsNum = vi.fpsNum;
    v3.numFrames = vi.numFrames;
    v3.width = vi.width;
    v3.height = vi.height;
    v3.flags = vs3::nfNoCache | vs3::nfIsCache;
    return v3;
}

VSVideoInfo VSCore::VideoInfoFromV3(const vs3::VSVideoInfo &vi) noexcept {
    VSVideoInfo v = {};
    VideoFormatFromV3(v.format, vi.format);
    v.fpsDen = vi.fpsDen;
    v.fpsNum = vi.fpsNum;
    v.numFrames = vi.numFrames;
    v.width = vi.width;
    v.height = vi.height;
    return v;
}

/////////////////////////////////////////////////

const VSCoreInfo &VSCore::getCoreInfo3() {
    getCoreInfo(coreInfo);
    return coreInfo;
}

void VSCore::getCoreInfo(VSCoreInfo &info) const {
    info.versionString = VAPOURSYNTH_VERSION_STRING;
    info.core = VAPOURSYNTH_CORE_VERSION;
    info.api = VAPOURSYNTH_API_VERSION;
    info.numThreads = static_cast<int>(threadPool->threadCount());
    info.maxFramebufferSize = memory->limit();
    info.usedFramebufferSize = memory->allocated_bytes();
}

void VSCore::getCoreInfo2(VSCoreInfo2 &info) const {
    info.versionString = VAPOURSYNTH_VERSION_STRING;
    info.coreVersion = VAPOURSYNTH_CORE_VERSION;
    info.apiVersion = VAPOURSYNTH_API_VERSION;
    info.creationFlags = creationFlags;
    info.numThreads = static_cast<int>(threadPool->threadCount());
    info.maxFramebufferSize = memory->limit();
    info.usedFramebufferSize = memory->allocated_bytes();
}

bool VSCore::getAudioFormatName(const VSAudioFormat &format, char *buffer) noexcept {
    if (!isValidAudioFormat(format.sampleType, format.bitsPerSample, format.channelLayout))
        return false;
    if (format.sampleType == stFloat)
        snprintf(buffer, 32, "Audio%dF (%d CH)", format.bitsPerSample, format.numChannels);
    else
        snprintf(buffer, 32, "Audio%d (%d CH)", format.bitsPerSample, format.numChannels);
    return true;
}

bool VSCore::getVideoFormatName(const VSVideoFormat &format, char *buffer) noexcept {
    if (!isValidVideoFormat(format.colorFamily, format.sampleType, format.bitsPerSample, format.subSamplingW, format.subSamplingH))
        return false;

    char suffix[16];
    if (format.sampleType == stFloat)
        strcpy(suffix, (format.bitsPerSample == 32) ? "S" : "H");
    else
        snprintf(suffix, sizeof(suffix), "%d", (format.colorFamily == cfRGB ? 3:1) * format.bitsPerSample);

    const char *yuvName = nullptr;

    switch (format.colorFamily) {
        case cfGray:
            snprintf(buffer, 32, "Gray%s", suffix);
            break;
        case cfRGB:
            snprintf(buffer, 32, "RGB%s", suffix);
            break;
        case cfYUV:
            if (format.subSamplingW == 1 && format.subSamplingH == 1)
                yuvName = "420";
            else if (format.subSamplingW == 1 && format.subSamplingH == 0)
                yuvName = "422";
            else if (format.subSamplingW == 0 && format.subSamplingH == 0)
                yuvName = "444";
            else if (format.subSamplingW == 2 && format.subSamplingH == 2)
                yuvName = "410";
            else if (format.subSamplingW == 2 && format.subSamplingH == 0)
                yuvName = "411";
            else if (format.subSamplingW == 0 && format.subSamplingH == 1)
                yuvName = "440";
            if (yuvName)
                snprintf(buffer, 32, "YUV%sP%s", yuvName, suffix);
            else
                snprintf(buffer, 32, "YUVssw%dssh%dP%s", format.subSamplingW, format.subSamplingH, suffix);
            break;
        case cfUndefined:
            snprintf(buffer, 32, "Undefined");
            break;
    }
    return true;
}

static void VS_CC loadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
        int err;
        const char *forcens = vsapi->mapGetData(in, "forcens", 0, &err);
        if (!forcens)
            forcens = "";
        const char *forceid = vsapi->mapGetData(in, "forceid", 0, &err);
        if (!forceid)
            forceid = "";
        bool altSearchPath = !!vsapi->mapGetInt(in, "altsearchpath", 0, &err);
        core->loadPlugin(std::filesystem::u8path(vsapi->mapGetData(in, "path", 0, nullptr)), true, forcens, forceid, altSearchPath);
    } catch (VSException &e) {
        vsapi->mapSetError(out, e.what());
    }
}

static void VS_CC loadAllPlugins(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
        core->loadAllPluginsInPath(std::filesystem::u8path(vsapi->mapGetData(in, "path", 0, nullptr)));
    } catch (VSException &e) {
        vsapi->mapSetError(out, e.what());
    }
}

void VS_CC loadPluginInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("LoadPlugin", "path:data;altsearchpath:int:opt;forcens:data:opt;forceid:data:opt;", "", &loadPlugin, nullptr, plugin);
    vspapi->registerFunction("LoadAllPlugins", "path:data;", "", &loadAllPlugins, nullptr, plugin);
}

void VSCore::registerFormats() {
    // Register known formats with informational names
    queryVideoFormat3(vs3::cmGray, stInteger,  8, 0, 0, "Gray8", vs3::pfGray8);
    queryVideoFormat3(vs3::cmGray, stInteger, 16, 0, 0, "Gray16", vs3::pfGray16);

    queryVideoFormat3(vs3::cmGray, stFloat,   16, 0, 0, "GrayH", vs3::pfGrayH);
    queryVideoFormat3(vs3::cmGray, stFloat,   32, 0, 0, "GrayS", vs3::pfGrayS);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 1, 1, "YUV420P8", vs3::pfYUV420P8);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 1, 0, "YUV422P8", vs3::pfYUV422P8);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 0, 0, "YUV444P8", vs3::pfYUV444P8);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 2, 2, "YUV410P8", vs3::pfYUV410P8);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 2, 0, "YUV411P8", vs3::pfYUV411P8);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 8, 0, 1, "YUV440P8", vs3::pfYUV440P8);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 9, 1, 1, "YUV420P9", vs3::pfYUV420P9);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 9, 1, 0, "YUV422P9", vs3::pfYUV422P9);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 9, 0, 0, "YUV444P9", vs3::pfYUV444P9);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 10, 1, 1, "YUV420P10", vs3::pfYUV420P10);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 10, 1, 0, "YUV422P10", vs3::pfYUV422P10);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 10, 0, 0, "YUV444P10", vs3::pfYUV444P10);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 12, 1, 1, "YUV420P12", vs3::pfYUV420P12);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 12, 1, 0, "YUV422P12", vs3::pfYUV422P12);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 12, 0, 0, "YUV444P12", vs3::pfYUV444P12);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 14, 1, 1, "YUV420P14", vs3::pfYUV420P14);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 14, 1, 0, "YUV422P14", vs3::pfYUV422P14);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 14, 0, 0, "YUV444P14", vs3::pfYUV444P14);

    queryVideoFormat3(vs3::cmYUV,  stInteger, 16, 1, 1, "YUV420P16", vs3::pfYUV420P16);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 16, 1, 0, "YUV422P16", vs3::pfYUV422P16);
    queryVideoFormat3(vs3::cmYUV,  stInteger, 16, 0, 0, "YUV444P16", vs3::pfYUV444P16);

    queryVideoFormat3(vs3::cmYUV,  stFloat,   16, 0, 0, "YUV444PH", vs3::pfYUV444PH);
    queryVideoFormat3(vs3::cmYUV,  stFloat,   32, 0, 0, "YUV444PS", vs3::pfYUV444PS);

    queryVideoFormat3(vs3::cmRGB,  stInteger, 8, 0, 0, "RGB24", vs3::pfRGB24);
    queryVideoFormat3(vs3::cmRGB,  stInteger, 9, 0, 0, "RGB27", vs3::pfRGB27);
    queryVideoFormat3(vs3::cmRGB,  stInteger, 10, 0, 0, "RGB30", vs3::pfRGB30);
    queryVideoFormat3(vs3::cmRGB,  stInteger, 16, 0, 0, "RGB48", vs3::pfRGB48);

    queryVideoFormat3(vs3::cmRGB,  stFloat,   16, 0, 0, "RGBH", vs3::pfRGBH);
    queryVideoFormat3(vs3::cmRGB,  stFloat,   32, 0, 0, "RGBS", vs3::pfRGBS);

    queryVideoFormat3(vs3::cmCompat, stInteger, 32, 0, 0, "CompatBGR32", vs3::pfCompatBGR32);
    queryVideoFormat3(vs3::cmCompat, stInteger, 16, 1, 0, "CompatYUY2", vs3::pfCompatYUY2);
}

bool VSCore::loadPluginManifest(const std::filesystem::path &path) {
    std::filesystem::path manifestPath = path;
    manifestPath /= "manifest.vs";

#ifdef VS_TARGET_OS_WINDOWS
    FILE *f = _wfopen(manifestPath.c_str(), L"rb");
#else
    FILE *f = fopen(manifestPath.c_str(), "rb");
#endif

    if (!f)
        return false;

    std::string contents;
    contents.resize(10000);
    size_t numRead = fread(contents.data(), 1, contents.size(), f);
    contents.resize(numRead);

    fclose(f);

    if (contents.empty()) {
        logMessage(mtCritical, ("Couldn't read contents of manifest file: " + manifestPath.u8string()).c_str());
        return true;
    }

    std::vector<std::string> lines;

    size_t lastPos = contents.find_first_not_of("\r\n", 0);
    size_t pos = contents.find_first_of("\r\n", lastPos + 1);
    while (pos != std::string::npos) {
        lines.push_back(contents.substr(lastPos, pos - lastPos));
        lastPos = contents.find_first_not_of("\r\n", pos + 1);
        pos = contents.find_first_of("\r\n", lastPos);
    }
    if (lastPos != std::string::npos)
        lines.push_back(contents.substr(lastPos));

    if (lines.empty() || lines[0] != "[VapourSynth Manifest V1]") {
        logMessage(mtCritical, ("Invalid header in manifest: " + manifestPath.u8string()).c_str());
        return true;
    }

    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty())
            continue;
        std::filesystem::path pluginPath = path;
        pluginPath /= std::filesystem::u8path(lines[i]);
        pluginPath += libraryExtension;
        try {
            loadPlugin(pluginPath, true);
        } catch (VSNoEntryPointException &) {
            logMessage(mtCritical, ("Manifest declared plugin has no entry point: " + pluginPath.u8string()).c_str());
        } catch (VSException &e) {
            logMessage(mtCritical, e.what());
        }
    }

    return true;
}

bool VSCore::loadAllPluginsInPath(const std::filesystem::path &path, bool pluginRoot) {
    if (path.empty())
        return false;

    if (pluginRoot || !loadPluginManifest(path)) {
        try {
            for (const auto &iter : std::filesystem::directory_iterator(path)) {
                std::error_code ec;
                if (iter.is_regular_file(ec) && !ec && iter.path().extension() == libraryExtension) {
                    try {
                        loadPlugin(iter.path());
                    } catch (VSNoEntryPointException &) {
                        // do nothing since we may encounter supporting dlls without an entry point
                    } catch (VSException &e) {
                        logMessage(mtWarning, e.what());
                    }
                } else if (iter.is_directory(ec) && !ec) {
                    loadAllPluginsInPath(iter.path());
                }
            }
        } catch (std::filesystem::filesystem_error &) {
            return false;
        }
    }

    return true;
}

void VSCore::filterInstanceCreated() noexcept {
    ++numFilterInstances;
}

void VSCore::filterInstanceDestroyed() noexcept {
    if (!--numFilterInstances) {
        assert(coreFreed);
        delete this;
    }
}

struct VSCoreShittyFreeList {
    VSFilterFree freeFunc;
    void *instanceData;
    int apiMajor;
    VSCoreShittyFreeList *next;
};

void VSCore::destroyFilterInstance(VSNode *node) {
    static thread_local int freeDepth = 0;
    static thread_local VSCoreShittyFreeList *nodeFreeList = nullptr;
    freeDepth++;

    if (enableFilterTiming)
        freedNodeProcessingTime += node->processingTime;

    if (node->freeFunc) {
        nodeFreeList = new VSCoreShittyFreeList({ node->freeFunc, node->instanceData, node->apiMajor, nodeFreeList });
    } else {
        filterInstanceDestroyed();
    }

    if (freeDepth == 1) {
        while (nodeFreeList) {
            VSCoreShittyFreeList *current = nodeFreeList;
            nodeFreeList = current->next;
            current->freeFunc(current->instanceData, this, getVSAPIInternal(current->apiMajor));
            delete current;
            filterInstanceDestroyed();
        }
    }

    freeDepth--;
}

void VSCore::clearCaches(bool resetSize) {
    std::lock_guard<std::mutex> lock(cacheLock);
    for (const auto &iter : caches)
        iter->clearCache(resetSize);
}

void VSCore::refreshVulkanExecBudget() {
    if (vulkanDev)
        vulkanDev->setExecRetainedBudget(memory->gpu_limit() / 4);
}

void VSCore::gpuMemoryPanic() {
    /* The allocator's escalation path: the driver refused an allocation, so every cached GPU
       frame goes, fixed size caches included — a failed frame is strictly worse than any
       cache miss. Host caches are left alone; their bytes cannot become VRAM. The exec pool
       sweep has already run by the time this is called. */
    {
        std::lock_guard<std::mutex> lock(cacheLock);
        for (const auto &iter : caches) {
            if (iter->isGPUOutput())
                iter->clearCache(true);
        }
    }
    if (vulkanDev)
        vulkanDev->trimAllocator();
}

bool VSCore::getNodeTiming() noexcept {
    return enableFilterTiming;
}

void VSCore::setNodeTiming(bool enable) noexcept {
    enableFilterTiming = enable;
}

int64_t VSCore::getFreedNodeProcessingTime(bool reset) noexcept {
    if (reset)
        return freedNodeProcessingTime.exchange(0);
    return freedNodeProcessingTime.load();
}

std::filesystem::path VSCore::getLibraryPath() {
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE module;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&vs_internal_vsapi, &module);
    std::vector<wchar_t> pathBuf(65536);
    GetModuleFileNameW(module, pathBuf.data(), (DWORD)pathBuf.size());
    return pathBuf.data();
#else
    Dl_info info = {};
    if (dladdr(&vs_internal_vsapi, &info))
        return info.dli_fname;
#endif
    return {};
}

// Internal plugin initialization functions
void textInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void resizeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void internalFiltersInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);

VSCore::VSCore(int flags) :
    numFilterInstances(1),
    creationFlags(flags & (ccfEnableGraphInspection | ccfDisableAutoLoading | ccfDisableLibraryUnloading | ccfEnableFrameRefDebug)),
    freedNodeProcessingTime(0),
    videoFormatIdOffset(1000),
    cpuLevel(INT_MAX),
    memory(new vs::MemoryUse()),
    enableGraphInspection(creationFlags & ccfEnableGraphInspection),
    enableFrameRefDebug(creationFlags & ccfEnableFrameRefDebug) {

#ifdef VS_TARGET_CPU_X86
    if (!vs_isSSEStateOk())
        logFatal("Bad SSE state detected when creating new core");
#endif

    disableLibraryUnloading = !!(creationFlags & ccfDisableLibraryUnloading);
    bool disableAutoLoading = !!(creationFlags & ccfDisableAutoLoading);
    threadPool = new VSThreadPool(this);

    registerFormats();

    auto libraryPath = getLibraryPath();

    auto libraryExtension = libraryPath.extension();
    if (libraryExtension != ".dll" && libraryExtension != ".so" && libraryExtension != ".dylib") {
        libraryPath.replace_extension("");
        libraryExtension = libraryPath.extension();
    }
    libraryPath = libraryPath.parent_path();
    assert(!libraryPath.empty());

    // The internal plugin units, the loading is a bit special so they can get special flags
    auto internalPluginPath = libraryPath / "libvapoursynthfilters";
    internalPluginPath += libraryExtension;
    VSPlugin *p = nullptr;
    // Bypass normal pllugin loading to use special settings for the internal filters
    try {
        p = new VSPlugin(internalPluginPath, "", "", false, true, "_", this);
    } catch (VSException &e) {
        logFatal("Failed to load " + internalPluginPath.u8string() + " with error: " + e.what());
    }

    loadPluginInitialize(p, &vs_internal_vspapi);
    internalFiltersInitialize(p, &vs_internal_vspapi);
    p->lock();
    plugins.insert(std::make_pair(p->getID(), p));

    p = new VSPlugin(this);
    resizeInitialize(p, &vs_internal_vspapi);
    plugins.insert(std::make_pair(p->getID(), p));

    p = new VSPlugin(this);
    textInitialize(p, &vs_internal_vspapi);
    plugins.insert(std::make_pair(p->getID(), p));



    // New site-packages relative plugin loading
    if (!disableAutoLoading) {
#ifdef VS_TARGET_OS_WINDOWS
        loadAllPluginsInPath(libraryPath / L"plugins", true);

        const wchar_t *extraPluginPath = _wgetenv(L"VAPOURSYNTH_EXTRA_PLUGIN_PATH");
        if (extraPluginPath && wcslen(extraPluginPath) > 0)
            loadAllPluginsInPath(extraPluginPath, true);
#else
        loadAllPluginsInPath(libraryPath / "plugins", true);

        const char *extraPluginPath = std::getenv("VAPOURSYNTH_EXTRA_PLUGIN_PATH");
        if (extraPluginPath && strlen(extraPluginPath) > 0)
            loadAllPluginsInPath(extraPluginPath, true);
#endif
    }
}

void VSCore::freeCore() {
    auto safe_to_string = [](auto x) -> std::string { try { return std::to_string(x); } catch (...) { return ""; } };

    if (coreFreed)
        logFatal("Double free of core");
    coreFreed = true;
    threadPool->waitForDone();
    if (numFilterInstances > 1)
        logMessage(mtWarning, "Core freed but " + safe_to_string(numFilterInstances.load() - 1) + " filter instance(s) still exist");
    if (memory->allocated_bytes())
        logMessage(mtWarning, "Core freed but " + safe_to_string(memory->allocated_bytes()) + " bytes still allocated in framebuffers");
    if (vulkanDev) {
        uint64_t gpuLive = vulkanDev->allocatorStats().usedBytes;
        if (gpuLive)
            logMessage(mtWarning, "Core freed but " + safe_to_string(gpuLive) + " bytes still allocated in GPU framebuffers");
    }
    // Remove all message handlers on free to prevent a zombie core from crashing the whole application by calling a no longer usable
    // message handler
    while (!messageHandlers.empty())
        removeLogHandler(*messageHandlers.begin());
    // Release the extra filter instance that always keeps the core alive
    filterInstanceDestroyed();
}

VSCore::~VSCore() {
    delete threadPool;
    for(const auto &iter : plugins)
        delete iter.second;
    plugins.clear();
    vulkanTrans.reset();
    if (vulkanDev) {
        /* The device may outlive this core through surviving frames, and a late pooled
           allocation must not call back into a deleted core. The account callback stays:
           MemoryUse gates its own deletion on the GPU pool reaching zero. */
        vulkanDev->setPressureCallback(nullptr, nullptr);
        vulkanDev->onCoreFreed();
        vulkanDev = nullptr;
    }
    memory->on_core_freed();
}

VSMap *VSCore::getPlugins3() {
    VSMap *m = new VSMap;
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    int num = 0;
    for (const auto &iter : plugins) {
        std::string b = iter.second->getNamespace() + ";" + iter.second->getID() + ";" + iter.second->getName();
        vs_internal_vsapi.mapSetData(m, ("Plugin" + std::to_string(++num)).c_str(), b.c_str(), static_cast<int>(b.size()), dtUtf8, maReplace);
    }
    return m;
}

VSPlugin *VSCore::getPluginByID(const std::string &identifier) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    auto p = plugins.find(identifier);
    if (p != plugins.end())
        return p->second;
    return nullptr;
}

VSPlugin *VSCore::getPluginByNamespace(const std::string &ns) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    for (const auto &iter : plugins) {
        if (iter.second->getNamespace() == ns)
            return iter.second;
    }
    return nullptr;
}

VSPlugin *VSCore::getNextPlugin(VSPlugin *plugin) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    if (plugin == nullptr) {
        return (plugins.begin() != plugins.end()) ? plugins.begin()->second : nullptr;
    } else {
        auto it = plugins.find(plugin->getID());
        if (it != plugins.end())
            ++it;
        return (it != plugins.end()) ? it->second : nullptr;
    }
}

void VSCore::loadPlugin(const std::filesystem::path &filename, bool loadCPUOptimized, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath) {
    std::unique_ptr<VSPlugin> p(new VSPlugin(filename, forcedNamespace, forcedId, altSearchPath, loadCPUOptimized, ".", this));

    std::lock_guard<std::recursive_mutex> lock(pluginLock);

    VSPlugin *already_loaded_plugin = getPluginByID(p->getID());
    if (already_loaded_plugin) {
        std::string error = "Plugin " + filename.u8string() + " already loaded (" + p->getID() + ")";
        if (already_loaded_plugin->getFilename().size())
            error += " from " + already_loaded_plugin->getFilename();
        throw VSException(error);
    }

    already_loaded_plugin = getPluginByNamespace(p->getNamespace());
    if (already_loaded_plugin) {
        std::string error = "Plugin load of " + filename.u8string() + " failed, namespace " + p->getNamespace() + " already populated";
        if (already_loaded_plugin->getFilename().size())
            error += " by " + already_loaded_plugin->getFilename();
        throw VSException(error);
    }

    plugins.insert(std::make_pair(p->getID(), p.get()));
    p.release();
}

void VSCore::createFilter3(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiMajor, this);
        vs_internal_vsapi.mapConsumeNode(out, "clip", node, maAppend);
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

void VSCore::createVideoFilter(VSMap *out, const std::string &name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(name, vi, getFrame, free, filterMode, dependencies, numDeps, instanceData, apiMajor, this);
        vs_internal_vsapi.mapConsumeNode(out, "clip", node, maAppend);
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

VSNode *VSCore::createVideoFilter(const std::string &name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor) {
    try {
        return new VSNode(name, vi, getFrame, free, filterMode, dependencies, numDeps, instanceData, apiMajor, this);
    } catch (VSException &) {
        return nullptr;
    }
}

void VSCore::createAudioFilter(VSMap *out, const std::string &name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(name, ai, getFrame, free, filterMode, dependencies, numDeps, instanceData, apiMajor, this);
        vs_internal_vsapi.mapConsumeNode(out, "clip", node, maAppend);
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

VSNode *VSCore::createAudioFilter(const std::string &name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, int apiMajor) {
    try {
        return new VSNode(name, ai, getFrame, free, filterMode, dependencies, numDeps, instanceData, apiMajor, this);
    } catch (VSException &) {
        return nullptr;
    }
}

VSPlugin::VSPlugin(VSCore *core)
    : libHandle(0), core(core) {
}

int VSCore::getCpuLevel() const {
    return cpuLevel;
}

int VSCore::setCpuLevel(int cpu) {
    return cpuLevel.exchange(cpu);
}

static void VS_CC configPlugin3(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin) VS_NOEXCEPT {
    assert(identifier && defaultNamespace && name && plugin);
    plugin->configPlugin(identifier, defaultNamespace, name, -1, apiVersion, readOnly ? 0 : pcModifiable);
}

VSPlugin::VSPlugin(const std::filesystem::path &relFilename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath, bool loadCPUOptimized, std::string separator, VSCore *core)
    : fnamespace(forcedNamespace), id(forcedId), core(core) {
    std::filesystem::path fullPath = std::filesystem::absolute(relFilename);

#ifdef VS_TARGET_CPU_X86
    if (loadCPUOptimized) {
        int abiLevel = doGetX86ABILevel();

        auto tryABILevel = [&fullPath, &separator](int level) -> bool {
            std::filesystem::path newExtension; 
            if (level == 4)
                newExtension = std::filesystem::u8path(separator + "zn4");
            else if (level == 3)
                newExtension = std::filesystem::u8path(separator + "avx512");
            else if (level == 2)
                newExtension = std::filesystem::u8path(separator + "avx2");
            newExtension += fullPath.extension();

            std::filesystem::path abiLevelPath = std::filesystem::path(fullPath).replace_extension();
            abiLevelPath += newExtension;
            if (std::filesystem::exists(abiLevelPath)) {
                fullPath = abiLevelPath;
                return true;
            }
            return false;
            };

        for (int level = abiLevel; level > 1; --level)
            if (tryABILevel(level))
                break;
    }
#endif

    filename = fullPath.generic_u8string();

#ifdef VS_TARGET_OS_WINDOWS
    libHandle = LoadLibraryEx(fullPath.c_str(), nullptr, altSearchPath ? 0 : (LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR));

    if (libHandle == nullptr) {
        DWORD lastError = GetLastError();

        if (lastError == 126)
            throw VSException("Failed to load " + relFilename.u8string() + ". GetLastError() returned " + std::to_string(lastError) + ". The file you tried to load or one of its dependencies is probably missing.");
        throw VSException("Failed to load " + relFilename.u8string() + ". GetLastError() returned " + std::to_string(lastError) + ".");
    }

    VSInitPlugin pluginInit = reinterpret_cast<VSInitPlugin>(GetProcAddress(libHandle, "VapourSynthPluginInit2"));

    if (!pluginInit)
        pluginInit = reinterpret_cast<VSInitPlugin>(GetProcAddress(libHandle, "_VapourSynthPluginInit2@8"));

    vs3::VSInitPlugin pluginInit3 = nullptr;
    if (!pluginInit)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(GetProcAddress(libHandle, "VapourSynthPluginInit"));

    if (!pluginInit3)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(GetProcAddress(libHandle, "_VapourSynthPluginInit@12"));

    if (pluginInit3)
        core->logMessage(mtWarning, "Plugin " + relFilename.u8string() + " is using API3 which is deprecated and will be removed shortly.");

    if (!pluginInit && !pluginInit3) {
        if (!core->disableLibraryUnloading)
            FreeLibrary(libHandle);
        throw VSNoEntryPointException("No entry point found in " + relFilename.u8string());
    }
#else
    libHandle = dlopen(fullPath.c_str(), RTLD_LAZY);

    if (!libHandle) {
        const char *dlError = dlerror();
        if (dlError)
            throw VSException("Failed to load " + relFilename.u8string() + ". Error given: " + dlError);
        else
            throw VSException("Failed to load " + relFilename.u8string());
    }

    VSInitPlugin pluginInit = reinterpret_cast<VSInitPlugin>(dlsym(libHandle, "VapourSynthPluginInit2"));
    vs3::VSInitPlugin pluginInit3 = nullptr;
    if (!pluginInit3)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(dlsym(libHandle, "VapourSynthPluginInit"));

    if (!pluginInit && !pluginInit3) {
        if (!core->disableLibraryUnloading)
            dlclose(libHandle);
        throw VSException("No entry point found in " + relFilename.u8string());
    }


#endif
    if (pluginInit)
        pluginInit(this, &vs_internal_vspapi);
    else
        pluginInit3(configPlugin3, vs_internal_vsapi3.registerFunction, this);

#ifdef VS_TARGET_CPU_X86
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after loading " + filename);
#endif

    if (readOnlySet)
        readOnly = true;

    bool supported = (apiMajor == VAPOURSYNTH_API_MAJOR && apiMinor <= VAPOURSYNTH_API_MINOR) || (apiMajor == VAPOURSYNTH3_API_MAJOR && apiMinor <= VAPOURSYNTH3_API_MINOR);

    if (!supported) {
#ifdef VS_TARGET_OS_WINDOWS
        if (!core->disableLibraryUnloading)
            FreeLibrary(libHandle);
#else
        if (!core->disableLibraryUnloading)
            dlclose(libHandle);
#endif
        throw VSException("Core only supports API R" + std::to_string(VAPOURSYNTH_API_MAJOR) + "." + std::to_string(VAPOURSYNTH_API_MINOR) + " but the loaded plugin requires API R" + std::to_string(apiMajor) + "." + std::to_string(apiMinor) + "; Filename: " + relFilename.u8string() + "; Name: " + fullname);
    }
}

VSPlugin::~VSPlugin() {
#ifdef VS_TARGET_OS_WINDOWS
    if (libHandle && !core->disableLibraryUnloading)
        FreeLibrary(libHandle);
#else
    if (libHandle && !core->disableLibraryUnloading)
        dlclose(libHandle);
#endif
}

bool VSPlugin::configPlugin(const std::string &identifier, const std::string &pluginNamespace, const std::string &fullname, int pluginVersion, int apiVersion, int flags) {
    if (hasConfig)
        core->logFatal("Attempted to configure plugin " + identifier + " twice");

    if (flags & ~pcModifiable)
        core->logFatal("Invalid flags passed to configPlugin() by " + identifier);

    if (id.empty())
        id = identifier;

    if (fnamespace.empty())
        fnamespace = pluginNamespace;

    this->pluginVersion = pluginVersion;
    this->fullname = fullname;

    apiMajor = apiVersion;
    if (apiMajor >= 0x10000) {
        apiMinor = (apiMajor & 0xFFFF);
        apiMajor >>= 16;
    }

    readOnlySet = !(flags & pcModifiable);
    hasConfig = true;
    return true;
}

bool VSPlugin::registerFunction(const std::string &name, const std::string &args, const std::string &returnType, VSPublicFunction argsFunc, void *functionData) {
    if (readOnly) {
        core->logMessage(mtCritical, "API MISUSE! Tried to register function " + name + " but plugin " + id + " is read only");
        return false;
    }

    if (!isValidIdentifier(name)) {
        core->logMessage(mtCritical, "API MISUSE! Plugin " + id + " tried to register '" + name + "' which is an illegal identifier");
        return false;
    }

    std::lock_guard<std::mutex> lock(functionLock);

    if (funcs.count(name)) {
        core->logMessage(mtCritical, "API MISUSE! Tried to register function '" + name + "' more than once for plugin " + id);
        return false;
    }

    try {
        funcs.emplace(std::make_pair(name, VSPluginFunction(name, args, returnType, argsFunc, functionData, this)));
    } catch (std::runtime_error &e) {
        core->logMessage(mtCritical, "API MISUSE! Function '" + name + "' failed to register with error: " + e.what());
        return false;
    }

    return true;
}

VSMap *VSPlugin::invoke(const std::string &funcName, const VSMap &args) {
    auto it = funcs.find(funcName);
    if (it != funcs.end()) {
        return it->second.invoke(args);
    } else {
        VSMap *v = new VSMap();
        vs_internal_vsapi.mapSetError(v, ("Function '" + funcName + "' not found in " + id).c_str());
        return v;
    }
}

VSPluginFunction *VSPlugin::getNextFunction(VSPluginFunction *func) {
    std::lock_guard<std::mutex> lock(functionLock);
    if (func == nullptr) {
        return (funcs.begin() != funcs.end()) ? &funcs.begin()->second : nullptr;
    } else {
        auto it = funcs.find(func->getName());
        if (it != funcs.end())
            ++it;
        return (it != funcs.end()) ? &it->second : nullptr;
    }
}

VSPluginFunction *VSPlugin::getFunctionByName(const std::string name) {
    std::lock_guard<std::mutex> lock(functionLock);
    auto it = funcs.find(name);
    if (it != funcs.end())
        return &it->second;
    return nullptr;
}

void VSPlugin::getFunctions3(VSMap *out) const {
    for (const auto &f : funcs) {
        if (f.second.isV3Compatible()) {
            std::string b = f.first + ";" + f.second.getV3ArgString();
            vs_internal_vsapi.mapSetData(out, f.first.c_str(), b.c_str(), static_cast<int>(b.size()), dtUtf8, maReplace);
        }
    }
}

VSNode::VSCache::CacheAction VSNode::VSCache::recommendSize() {
    int total = hits + nearMiss + farMiss;

    if (total == 0)
        return CacheAction::Decay;

    if (total < 30)
        return CacheAction::NoChange; // not enough requests to know what to do so keep it this way

    bool shrink = (nearMiss == 0 && hits == 0); // shrink if there were no hits or even close to hitting
    bool grow = ((nearMiss * 20) >= total); // grow if 5% or more are near misses

    if (grow) { // growing the cache would be beneficial
        clearStats();
        return CacheAction::Grow;
    } else if (shrink) { // probably a linear scan, no reason to waste space here
        clearStats();
        return CacheAction::Shrink;
    } else {
        clearStats();
        return CacheAction::NoChange; // probably fine the way it is
    }
}

inline VSNode::VSCache::VSCache(int maxSize, int maxHistorySize, bool fixedSize)
    : maxSize(maxSize), maxHistorySize(maxHistorySize), fixedSize(fixedSize) {
    clear();
}

inline PVSFrame VSNode::VSCache::object(const int key) {
    return this->relink(key);
}

inline bool VSNode::VSCache::remove(const int key) {
    auto i = hash.find(key);

    if (i == hash.end()) {
        return false;
    } else {
        unlink(i->second);
        return true;
    }
}


bool VSNode::VSCache::insert(const int akey, const PVSFrame &aobject) {
    assert(aobject);
    assert(akey >= 0);
    remove(akey);
    auto i = hash.insert(std::make_pair(akey, Node(akey, aobject)));
    currentSize++;
    currentBytes += aobject->totalByteSize();
    Node *n = &i.first->second;

    if (first)
        first->prevNode = n;

    n->nextNode = first;
    first = n;

    if (!last)
        last = first;

    evictFrames(maxSize, SIZE_MAX);

    return true;
}


size_t VSNode::VSCache::evictFrames(int maxCount, size_t byteBudget) {
    size_t freed = 0;

    // turn cached frames into history nodes until both the count and byte conditions hold
    while (currentSize > maxCount && freed < byteBudget) {
        if (!weakpoint)
            weakpoint = last;
        else
            weakpoint = weakpoint->prevNode;

        if (weakpoint) {
            freed += subtractFrameBytes(*weakpoint);
            weakpoint->frame.reset();
        }

        currentSize--;
        historySize++;
    }

    // remove history until the tail is small enough
    while (last && historySize > maxHistorySize)
        unlink(*last);

    return freed;
}

size_t VSNode::VSCache::dropLRUFrames(size_t maxBytes) {
    size_t freed = evictFrames(0, maxBytes);

    // lower the ceiling to match so the freed memory doesn't immediately get reclaimed but never
    // below the reseed size so the cache can relearn its working set once the pressure is gone
    if (maxSize > reSeedSize())
        maxSize = std::max(reSeedSize(), currentSize);
    return freed;
}

void VSNode::VSCache::adjustSize(bool memoryComfortable, uint64_t completedExtFrames) {
    if (fixedSize)
        return;

    if (hits + nearMiss + farMiss > 0)
        lastActivityExtFrames = completedExtFrames;
    else if (completedExtFrames - lastActivityExtFrames < deadBranchDelay)
        return;

    // recommendSize clears the counters, so the measured reuse distance has to be taken first
    int wantedSize = deepestNearMiss;

    switch (recommendSize()) {
    case VSCache::CacheAction::Decay:
        setMaxFrames(std::max(getMaxFrames() - 2, userSize));
        break;
    case VSCache::CacheAction::Grow:
        if (memoryComfortable)
            setMaxFrames(std::max(getMaxFrames() + 2, wantedSize));
        break;
    case VSCache::CacheAction::Shrink:
        // No hits and nothing even close to one. That is either a linear scan, where the cache is
        // useless and should shrink, or a cycle longer than the window, where it should grow a lot.
        // The two are indistinguishable until the window is extended, so probe further out first
        // and only conclude the cache is useless once a full length history still finds nothing.
        if (getMaxHistory() < cacheHistoryMax)
            setMaxHistory(std::min(getMaxHistory() * 2, cacheHistoryMax));
        else
            setMaxFrames(std::max(getMaxFrames() - 1, userSize));
        break;
    default:
        break;
    }
}

std::string VSCore::getFrameRefInfo() {
    std::lock_guard<std::mutex> lock(frameRefMutex);
    std::string info;
    info.reserve(65000);
    info += "FrameData START (num frames: " + std::to_string(frameRefs.size()) + ")\n";
    for (const auto &frame : frameRefs)
        info += frame->debugAllocationInfo + "\n";
    info += "FrameData END (num frames: " + std::to_string(frameRefs.size()) + ")\n";
    return info;
}

#ifdef VS_TARGET_CPU_X86
static ptrdiff_t alignmentHelper() {
    return getCPUFeatures()->avx512_f ? 64 : 32;
}

ptrdiff_t VSFrame::alignment = alignmentHelper();
#else
ptrdiff_t VSFrame::alignment = 32;
#endif

thread_local VSNode *VSCore::currentProcessingNode = nullptr;
thread_local PVSFunctionFrame VSCore::functionFrame;
std::atomic<uint64_t> VSFrame::allocationSeq = 0;