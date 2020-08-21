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
#ifndef VS_TARGET_OS_WINDOWS
#include <dirent.h>
#include <cstddef>
#include <unistd.h>
#include "settings.h"
#endif
#include <cassert>
#include <queue>
#include <bitset>

#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

#ifdef VS_TARGET_OS_WINDOWS
#include <shlobj.h>
#include <locale>
#include "../common/vsutf16.h"
#endif

// Internal filter headers
#include "internalfilters.h"
#include "cachefilter.h"

#ifdef VS_USE_MIMALLOC
#   include <mimalloc-new-delete.h>
#endif

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
static std::wstring readRegistryValue(const wchar_t *keyName, const wchar_t *valueName) {
    HKEY hKey;
    LONG lRes = RegOpenKeyEx(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &hKey);
    if (lRes != ERROR_SUCCESS) {
        lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, KEY_READ, &hKey);
        if (lRes != ERROR_SUCCESS)
            return std::wstring();
    }
    WCHAR szBuffer[512];
    DWORD dwBufferSize = sizeof(szBuffer);
    ULONG nError;
    nError = RegQueryValueEx(hKey, valueName, 0, nullptr, (LPBYTE)szBuffer, &dwBufferSize);
    RegCloseKey(hKey);
    if (ERROR_SUCCESS == nError)
        return szBuffer;
    return std::wstring();
}
#endif

VSFrameContext::VSFrameContext(int n, int index, VSNode *clip, const PVSFrameContext &upstreamContext) :
    refcount(1), reqOrder(upstreamContext->reqOrder), n(n), clip(clip), upstreamContext(upstreamContext), userData(nullptr), frameDone(nullptr), lockOnOutput(true), node(nullptr), index(index) {
}

VSFrameContext::VSFrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData, bool lockOnOutput) :
    refcount(1), reqOrder(0), n(n), clip(node->clip), userData(userData), frameDone(frameDone), lockOnOutput(lockOnOutput), node(node), index(index) {
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

VSFunctionRef::VSFunctionRef(VSPublicFunction func, void *userData, VSFreeFunctionData freeFunction, VSCore *core, int apiMajor) : refcount(1), func(func), userData(userData), freeFunction(freeFunction), core(core), apiMajor(apiMajor) {
    core->functionInstanceCreated();
}

VSFunctionRef::~VSFunctionRef() {
    if (freeFunction)
        freeFunction(userData);
    core->functionInstanceDestroyed();
}

void VSFunctionRef::call(const VSMap *in, VSMap *out) {
    if (apiMajor == VAPOURSYNTH3_API_MAJOR && !in->isV3Compatible()) {
        vs_internal_vsapi.mapSetError(out, "Function was passed values that are unknown to its API version");
        return;
    }
        
    func(in, out, userData, core, getVSAPIInternal(apiMajor));
}

void MemoryUse::add(size_t bytes) {
    used.fetch_add(bytes, std::memory_order_relaxed);
}

void MemoryUse::subtract(size_t bytes) {
    size_t tmp = used.fetch_sub(bytes, std::memory_order_relaxed) - bytes;
    if (freeOnZero && !tmp)
        delete this;
}

size_t MemoryUse::memoryUse() {
    return used;
}

size_t MemoryUse::getLimit() {
    return maxMemoryUse.load(std::memory_order_relaxed);
}

int64_t MemoryUse::setMaxMemoryUse(int64_t bytes) {
    if (bytes > 0 && static_cast<uint64_t>(bytes) <= SIZE_MAX)
        maxMemoryUse.store(static_cast<size_t>(bytes), std::memory_order_seq_cst);
    return maxMemoryUse;
}

bool MemoryUse::isOverLimit() {
    return used.load(std::memory_order_relaxed) > maxMemoryUse.load(std::memory_order_relaxed);
}

void MemoryUse::signalFree() {
    freeOnZero = true;
    if (!used)
        delete this;
}

MemoryUse::MemoryUse() : used(0), freeOnZero(false) {
    // 1GB
    setMaxMemoryUse(1024 * 1024 * 1024);

    // set 4GB as default on systems with (probably) 64bit address space
    if (sizeof(void *) >= 8)
        setMaxMemoryUse(static_cast<int64_t>(4) * 1024 * 1024 * 1024);
}

///////////////

VSPlaneData::VSPlaneData(size_t dataSize, MemoryUse &mem) noexcept : refcount(1), mem(mem), size(dataSize + 2 * VSFrameRef::guardSpace) {
    data = vsh_aligned_malloc<uint8_t>(size + 2 * VSFrameRef::guardSpace, VSFrameRef::alignment);
    assert(data);
    if (!data)
        VS_FATAL_ERROR("Failed to allocate memory for plane. Out of memory.");

    mem.add(size);
#ifdef VS_FRAME_GUARD
    for (size_t i = 0; i < VSFrameRef::guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
        reinterpret_cast<uint32_t *>(data)[i] = VS_FRAME_GUARD_PATTERN;
        reinterpret_cast<uint32_t *>(data + size - VSFrameRef::guardSpace)[i] = VS_FRAME_GUARD_PATTERN;
    }
#endif
}

VSPlaneData::VSPlaneData(const VSPlaneData &d) noexcept : refcount(1), mem(d.mem), size(d.size) {
    data = vsh_aligned_malloc<uint8_t>(size, VSFrameRef::alignment);
    assert(data);
    if (!data)
        VS_FATAL_ERROR("Failed to allocate memory for plane in copy constructor. Out of memory.");

    mem.add(size);
    memcpy(data, d.data, size);
}

VSPlaneData::~VSPlaneData() {
    vsh_aligned_free(data);
    mem.subtract(size);
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

VSFrameRef::VSFrameRef(const VSVideoFormat &f, int width, int height, const VSFrameRef *propSrc, VSCore *core) noexcept : refcount(1), contentType(mtVideo), width(width), height(height), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    if (width <= 0 || height <= 0)
        core->logFatal("Error in frame creation: dimensions are negative (" + std::to_string(width) + "x" + std::to_string(height) + ")");

    format.vf = f;
    numPlanes = format.vf.numPlanes;

    stride[0] = (width * (f.bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (numPlanes == 3) {
        int plane23 = ((width >> format.vf.subSamplingW) * (format.vf.bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
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
}

VSFrameRef::VSFrameRef(const VSVideoFormat &f, int width, int height, const VSFrameRef * const *planeSrc, const int *plane, const VSFrameRef *propSrc, VSCore *core) noexcept : refcount(1), contentType(mtVideo), width(width), height(height), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    if (width <= 0 || height <= 0)
        core->logFatal("Error in frame creation: dimensions are negative " + std::to_string(width) + "x" + std::to_string(height));

    format.vf = f;
    numPlanes = format.vf.numPlanes;

    stride[0] = (width * (format.vf.bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (numPlanes == 3) {
        int plane23 = ((width >> format.vf.subSamplingW) * (format.vf.bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    for (int i = 0; i < numPlanes; i++) {
        if (planeSrc[i]) {
            if (plane[i] < 0 || plane[i] >= planeSrc[i]->format.vf.numPlanes)
                core->logFatal("Error in frame creation: plane " + std::to_string(plane[i]) + " does not exist in the source frame");
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                core->logFatal("Error in frame creation: dimensions of plane " + std::to_string(plane[i]) + " do not match. Source: " + std::to_string(planeSrc[i]->getWidth(plane[i])) + "x" + std::to_string(planeSrc[i]->getHeight(plane[i])) + "; destination: " + std::to_string(getWidth(i)) + "x" + std::to_string(getHeight(i)));
            data[i] = planeSrc[i]->data[plane[i]];
            data[i]->add_ref();
        } else {
            if (i == 0) {
                data[i] = new VSPlaneData(stride[i] * height, *core->memory);
            } else {
                data[i] = new VSPlaneData(stride[i] * (height >> format.vf.subSamplingH), *core->memory);
            }
        }
    }
}

VSFrameRef::VSFrameRef(const VSAudioFormat &f, int numSamples, const VSFrameRef *propSrc, VSCore *core) noexcept
    : refcount(1), contentType(mtAudio), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
    if (numSamples <= 0)
        core->logFatal("Error in frame creation: bad number of samples (" + std::to_string(numSamples) + ")");
    
    format.af = f;
    numPlanes = format.af.numChannels;

    width = numSamples;

    stride[0] = format.af.bytesPerSample * VS_AUDIO_FRAME_SAMPLES;

    data[0] = new VSPlaneData(stride[0] * format.af.numChannels, *core->memory);
}

VSFrameRef::VSFrameRef(const VSAudioFormat &f, int numSamples, const VSFrameRef * const *channelSrc, const int *channel, const VSFrameRef *propSrc, VSCore *core) noexcept
    : refcount(1), contentType(mtAudio), properties(propSrc ? &propSrc->properties : nullptr), core(core) {
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
}

VSFrameRef::VSFrameRef(const VSFrameRef &f) noexcept : refcount(1) {
    contentType = f.contentType;
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
    core = f.core;
}

VSFrameRef::~VSFrameRef() {
    data[0]->release();
    if (data[1]) {
        data[1]->release();
        data[2]->release();
    }
}

const vs3::VSVideoFormat *VSFrameRef::getVideoFormatV3() const noexcept {
    assert(contentType == mtVideo);
    if (!v3format)
        v3format = core->VideoFormatToV3(format.vf);
    return v3format;
}

ptrdiff_t VSFrameRef::getStride(int plane) const {
    assert(contentType == mtVideo);
    if (plane < 0 || plane >= numPlanes)
        return 0;
    return stride[plane];
}

const uint8_t *VSFrameRef::getReadPtr(int plane) const {
    if (plane < 0 || plane >= numPlanes)
        return nullptr;

    if (contentType == mtVideo)
        return data[plane]->data + guardSpace;
    else
        return data[0]->data + guardSpace + plane * stride[0];
}

uint8_t *VSFrameRef::getWritePtr(int plane) {
    if (plane < 0 || plane >= numPlanes)
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
bool VSFrameRef::verifyGuardPattern() {
    for (int p = 0; p < ((contentType == mtVideo) ? numPlanes : 1); p++) {
        for (size_t i = 0; i < guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
            uint32_t p1 = reinterpret_cast<uint32_t *>(data[p]->data)[i];
            uint32_t p2 = reinterpret_cast<uint32_t *>(data[p]->data + data[p]->size - guardSpace)[i];
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

        VSPropType type = ptUnset;
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
            } else {
                throw std::runtime_error("Argument '" + argName + "' has unknown argument modifier '" + argParts[i] + "'");
            }
        }

        if (!isValidIdentifier(argName))
            throw std::runtime_error("Argument name '" + argName + "' contains illegal characters.");

        if (empty && !arr)
            throw std::runtime_error("Argument '" + argName + "' is not an array. Only array arguments can have the empty flag set.");

        argsOut.emplace_back(argName, type, arr, empty, opt);
    }
}

VSPluginFunction::VSPluginFunction(const std::string &name, const std::string &argString, const std::string &returnType, VSPublicFunction func, void *functionData, VSPlugin *plugin)
    : name(name), argString(argString), returnType(returnType), func(func), functionData(functionData), plugin(plugin) {
    parseArgString(argString, inArgs, plugin->apiMajor);
    if (plugin->apiMajor == 3)
        this->argString = getV4ArgString(); // construct to V4 equivalent arg string
    if (returnType != "any")
        parseArgString(returnType, retArgs, plugin->apiMajor);
}

VSMap *VSPluginFunction::invoke(const VSMap &args) {
    std::unique_ptr<VSMap> v(new VSMap);

    try {
        if (!plugin->compat && args.hasCompatNodes())
            throw VSException(name + ": only special filters may accept compat input");

        std::set<std::string> remainingArgs;
        for (size_t i = 0; i < args.size(); i++)
            remainingArgs.insert(args.key(i));

        for (const FilterArgument &fa : inArgs) {
            // ptUnset as an argument type means any value is accepted beyond the declared ones
            if (fa.type == ptUnset) {
                // this will always be the last argument, therefore it's safe to clear out the remaining ones since they'll
                // already have been checked
                remainingArgs.clear();
                continue;
            }

            int propType = vs_internal_vsapi.mapGetType(&args, fa.name.c_str());

            if (propType != ptUnset) {
                remainingArgs.erase(fa.name);

                if (fa.type != propType)
                    throw VSException(name + ": argument " + fa.name + " is not of the correct type");

                VSArrayBase *arr = args.find(fa.name);

                if (!fa.arr && arr->size() > 1)
                    throw VSException(name + ": argument " + fa.name + " is not of array type but more than one value was supplied");

                if (!fa.empty && arr->size() < 1)
                    throw VSException(name + ": argument " + fa.name + " does not accept empty arrays");

            } else if (!fa.opt) {
                throw VSException(name + ": argument " + fa.name + " is required");
            }
        }

        if (!remainingArgs.empty()) {
            auto iter = remainingArgs.cbegin();
            std::string s = *iter;
            ++iter;
            for (; iter != remainingArgs.cend(); ++iter)
                s += ", " + *iter;
            throw VSException(name + ": no argument(s) named " + s);
        }

        bool enableGraphInspection = plugin->core->enableGraphInspection;
        if (enableGraphInspection) {
            plugin->core->functionFrame = std::make_shared<VSFunctionFrame>(name, new VSMap(&args), plugin->core->functionFrame);
        }
        func(&args, v.get(), functionData, plugin->core, getVSAPIInternal(plugin->apiMajor));
        if (enableGraphInspection) {
            assert(plugin->core->functionFrame);
            plugin->core->functionFrame = plugin->core->functionFrame->next;
        }

        if (!plugin->compat && v->hasCompatNodes())
            plugin->core->logFatal(name + ": filter node returned compat format but only internal filters may do so");

        if (plugin->apiMajor == VAPOURSYNTH3_API_MAJOR && !args.isV3Compatible())
            plugin->core->logFatal(name + ": filter node returned not yet supported type");

        return v.release();

    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(v.get(), e.what());
        return v.release();
    }
}

bool VSPluginFunction::isV3Compatible() const {
    for (const auto &iter : inArgs)
        if (iter.type == ptAudioNode || iter.type == ptAudioFrame)
            return false;
    for (const auto &iter : retArgs)
        if (iter.type == ptAudioNode || iter.type == ptAudioFrame)
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

void VSNodeRef::add_ref() noexcept {
    assert(refcount > 0);
    ++refcount;
}

void VSNodeRef::release() noexcept {
    assert(refcount > 0);
    if (--refcount == 0) {
        clip->release();
        delete this;
    }
}

VSNode::VSNode(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
    refcount(0), nodeType(mtVideo), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), flags(flags), serialFrame(-1) {

    if (flags & ~(nfNoCache | nfIsCache | nfMakeLinear))
        throw VSException("Filter " + name  + " specified unknown flags");

    if ((flags & nfIsCache) && !(flags & nfNoCache))
        throw VSException("Filter " + name + " specified an illegal combination of flags (nfNoCache must always be set with nfIsCache)");

    frameReadyNotify = true;

    core->filterInstanceCreated();
    VSMap inval(in);
    init(&inval, out, &this->instanceData, reinterpret_cast<vs3::VSNode *>(this), core, reinterpret_cast<const vs3::VSAPI3 *>(getVSAPIInternal(3)));

    if (out->hasError()) {
        core->filterInstanceDestroyed();
        throw VSException(vs_internal_vsapi.mapGetError(out));
    }

    if (vi.empty()) {
        core->filterInstanceDestroyed();
        throw VSException("Filter " + name + " didn't set videoinfo");
    }

    for (const auto &iter : vi) {
        if (iter.numFrames <= 0) {
            core->filterInstanceDestroyed();
            throw VSException("Filter " + name + " returned zero or negative frame count");
        }
    }

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::VSNode(const std::string &name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
    refcount(numOutputs), nodeType(mtVideo), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), flags(flags), serialFrame(-1) {

    if (flags & ~(nfNoCache | nfIsCache | nfMakeLinear))
        throw VSException("Filter " + name + " specified unknown flags");

    if ((flags & nfIsCache) && !(flags & nfNoCache))
        throw VSException("Filter " + name + " specified an illegal combination of flags (nfNoCache must always be set with nfIsCache)");

    if (numOutputs < 1)
        throw VSException("Filter " + name + " needs to have at least one output");

    this->vi.reserve(numOutputs);
    this->v3vi.reserve(numOutputs);
    for (int i = 0; i < numOutputs; i++) {
        if (!core->isValidVideoInfo(vi[i]))
            throw VSException("The VSVideoInfo structure passed by " + name + " is invalid.");

        this->vi.push_back(vi[i]);
        this->v3vi.push_back(core->VideoInfoToV3(vi[i]));
        this->v3vi.back().flags = flags;
    }

    core->filterInstanceCreated();

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::VSNode(const std::string &name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree freeFunc, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
    refcount(numOutputs), nodeType(mtAudio), instanceData(instanceData), name(name), filterGetFrame(getFrame), freeFunc(freeFunc), filterMode(filterMode), apiMajor(apiMajor), core(core), flags(flags), serialFrame(-1) {

    if (flags & ~(nfNoCache | nfIsCache | nfMakeLinear))
        throw VSException("Filter " + name + " specified unknown flags");

    if ((flags & nfIsCache) && !(flags & nfNoCache))
        throw VSException("Filter " + name + " specified an illegal combination of flags (nfNoCache must always be set with nfIsCache)");

    if (numOutputs < 1)
        throw VSException("Filter " + name + " needs to have at least one output");

    core->filterInstanceCreated();

    this->ai.reserve(numOutputs);
    for (int i = 0; i < numOutputs; i++) {
        if (!core->isValidAudioInfo(ai[i]))
            throw VSException("The VSAudioInfo structure passed by " + name + " is invalid.");

        this->ai.push_back(ai[i]);
        auto &last = this->ai.back();
        int64_t maxSamples =  std::numeric_limits<int>::max() * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
        if (last.numSamples > maxSamples)
            throw VSException("Filter " + name + " specified " + std::to_string(last.numSamples) + " output samples but " + std::to_string(maxSamples) + " samples is the upper limit");
        last.numFrames = static_cast<int>((last.numSamples + VS_AUDIO_FRAME_SAMPLES - 1) / VS_AUDIO_FRAME_SAMPLES);
    }

    if (core->enableGraphInspection) {
        functionFrame = core->functionFrame;
    }
}

VSNode::~VSNode() {
    core->destroyFilterInstance(this);
}

void VSNode::getFrame(const PVSFrameContext &ct) {
    core->threadPool->start(ct);
}

const VSVideoInfo &VSNode::getVideoInfo(int index) const {
    assert(index >= 0 && index < static_cast<int>(vi.size()));
    return vi[index];
}

const vs3::VSVideoInfo &VSNode::getVideoInfo3(int index) const {
    assert(index >= 0 && index < static_cast<int>(v3vi.size()));
    return v3vi[index];
}

const VSAudioInfo &VSNode::getAudioInfo(int index) const {
    assert(index >= 0 && index < static_cast<int>(ai.size()));
    return ai[index];
}

void VSNode::setVideoInfo3(const vs3::VSVideoInfo *vi, int numOutputs) {
    if (numOutputs < 1)
        core->logFatal("setVideoInfo: Video filter " + name + " needs to have at least one output");
    for (int i = 0; i < numOutputs; i++) {
        if ((!!vi[i].height) ^ (!!vi[i].width))
            core->logFatal("setVideoInfo: Variable dimension clips must have both width and height set to 0");
        if (vi[i].format && !core->isValidFormatPointer(vi[i].format))
            core->logFatal("setVideoInfo: The VSVideoFormat pointer passed by " + name + " was not obtained from registerFormat() or getFormatPreset()");
        int64_t num = vi[i].fpsNum;
        int64_t den = vi[i].fpsDen;
        reduceRational(&num, &den);
        if (num != vi[i].fpsNum || den != vi[i].fpsDen)
            core->logFatal("setVideoInfo: The frame rate specified by " + name + " must be a reduced fraction. Instead, it is " + std::to_string(vi[i].fpsNum) + "/" + std::to_string(vi[i].fpsDen) + ")");

        this->vi.push_back(core->VideoInfoFromV3(vi[i]));
        this->v3vi.push_back(vi[i]);
        this->v3vi.back().flags = flags;
    }
    refcount = numOutputs;
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

void VSNode::setFilterRelation(VSNodeRef **dependencies, int numDeps) {
    if (core->enableGraphInspection) {
        VSMap *tmp = new VSMap();
        for (int i = 0; i < numDeps; i++)
            vs_internal_vsapi.mapSetNode(tmp, "clip", dependencies[i], paAppend);

        functionFrame = std::make_shared<VSFunctionFrame>("", tmp, functionFrame);
    }
}

PVSFrameRef VSNode::getFrameInternal(int n, int activationReason, VSFrameContext *frameCtx) {
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    bool enableGraphInspection = core->enableGraphInspection;
    if (enableGraphInspection)
        startTime = std::chrono::high_resolution_clock::now();

    const VSFrameRef *r = (apiMajor == VAPOURSYNTH_API_MAJOR) ? filterGetFrame(n, activationReason, instanceData, frameCtx->frameContext, frameCtx, core, &vs_internal_vsapi) : reinterpret_cast<vs3::VSFilterGetFrame>(filterGetFrame)(n, activationReason, &instanceData, frameCtx->frameContext, frameCtx, core, &vs_internal_vsapi3);

    if (enableGraphInspection) {
        std::chrono::nanoseconds duration = std::chrono::high_resolution_clock::now() - startTime;
        processingTime.fetch_add(duration.count(), std::memory_order_relaxed);
    }
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after return from "+ name);
#endif

    if (r) {     
        if (r->getFrameType() == mtVideo) {
            const VSVideoInfo &lvi = vi[frameCtx->index];
            const VSVideoFormat *fi = r->getVideoFormat();

            if (lvi.format.colorFamily == cfUndefined && (fi->colorFamily == cfCompatBGR32 || fi->colorFamily == cfCompatYUY2))
                core->logFatal("Illegal compat frame returned by " + name);
            else if (lvi.format.colorFamily != cfUndefined && !isSameVideoFormat(&lvi.format, fi))
                core->logFatal("Filter " + name + " returned a frame that's not of the declared format");
            else if ((lvi.width || lvi.height) && (r->getWidth(0) != lvi.width || r->getHeight(0) != lvi.height))
                core->logFatal("Filter " + name + " declared the size " + std::to_string(lvi.width) + "x" + std::to_string(lvi.height) + ", but it returned a frame with the size " + std::to_string(r->getWidth(0)) + "x" + std::to_string(r->getHeight(0)));
        } else {
            const VSAudioFormat *fi = r->getAudioFormat();
            const VSAudioInfo &lai = ai[frameCtx->index];

            int expectedSamples = (n < lai.numFrames - 1) ? VS_AUDIO_FRAME_SAMPLES : (((lai.numSamples % VS_AUDIO_FRAME_SAMPLES) ? (lai.numSamples % VS_AUDIO_FRAME_SAMPLES) : VS_AUDIO_FRAME_SAMPLES));

            if (lai.format.bitsPerSample != fi->bitsPerSample || lai.format.sampleType != fi->sampleType || lai.format.channelLayout != fi->channelLayout) {
                core->logFatal("Filter " + name + " returned a frame that's not of the declared format");
            } else if (expectedSamples != r->getFrameLength()) {
                core->logFatal("Filter " + name + " returned audio frame with " + std::to_string(r->getFrameLength()) + " samples but " + std::to_string(expectedSamples) + " expected from declared length");
            }
        }

#ifdef VS_FRAME_GUARD
        if (!r->verifyGuardPattern())
            vsFatal("Guard memory corrupted in frame %d returned from %s", n, name.c_str());
#endif

        return const_cast<VSFrameRef *>(r);
    }

    return nullptr;
}

void VSNode::reserveThread() {
    core->threadPool->reserveThread();
}

void VSNode::releaseThread() {
    core->threadPool->releaseThread();
}

bool VSNode::isWorkerThread() {
    return core->threadPool->isWorkerThread();
}

void VSNode::notifyCache(bool needMemory) {
    std::lock_guard<std::mutex> lock(serialMutex);
    CacheInstance *cache = reinterpret_cast<CacheInstance *>(instanceData);
    cache->cache.adjustSize(needMemory);
}

void VSCore::notifyCaches(bool needMemory) {
    std::lock_guard<std::mutex> lock(cacheLock);
    for (auto &cache : caches)
        cache->notifyCache(needMemory);
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
    f.numPlanes = (colorFamily == cfGray || colorFamily == cfCompatBGR32 || colorFamily == cfCompatYUY2) ? 1 : 3;

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
    if (!isValidVideoFormat(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH))
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
        const char *sampleTypeStr = "";
        if (sampleType == stFloat)
            sampleTypeStr = (bitsPerSample == 32) ? "S" : "H";

        const char *yuvName = nullptr;

        switch (colorFamily) {
        case vs3::cmGray:
            snprintf(f.name, sizeof(f.name), "Gray%s%d", sampleTypeStr, bitsPerSample);
            break;
        case vs3::cmRGB:
            snprintf(f.name, sizeof(f.name), "RGB%s%d", sampleTypeStr, bitsPerSample * 3);
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
                snprintf(f.name, sizeof(f.name), "YUV%sP%s%d", yuvName, sampleTypeStr, bitsPerSample);
            else
                snprintf(f.name, sizeof(f.name), "YUVssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
            break;
        case vs3::cmYCoCg:
            snprintf(f.name, sizeof(f.name), "YCoCgssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
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
    {
        std::lock_guard<std::mutex> lock(videoFormatLock);

        for (const auto &iter : videoFormats) {
            if (&iter.second == f)
                return true;
        }
    }
    return false;
}

VSLogHandle *VSCore::addLogHandler(VSLogHandler handler, VSLogHandlerFree freeFunc, void *userData) {
    std::lock_guard<std::mutex> lock(logMutex);
    return *(messageHandlers.insert(new VSLogHandle{ handler, freeFunc, userData }).first);
}

bool VSCore::removeLogHandler(VSLogHandle *rec) {
    std::lock_guard<std::mutex> lock(logMutex);
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
    std::lock_guard<std::mutex> lock(logMutex);
    for (auto iter : messageHandlers)
        iter->handler(type, msg, iter->userData);

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

bool VSCore::isValidVideoFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) noexcept {
    if (colorFamily != cfUndefined && colorFamily != cfGray && colorFamily != cfYUV && colorFamily != cfRGB && colorFamily != cfCompatBGR32 && colorFamily != cfCompatYUY2)
        return false;

    if (colorFamily == cfUndefined && (subSamplingH != 0 || subSamplingW != 0 || bitsPerSample != 0 || sampleType != stInteger))
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

    if (colorFamily == cfCompatBGR32 && (subSamplingH != 0 || subSamplingW != 0 || bitsPerSample != 32 || sampleType != stInteger))
        return false;

    if (colorFamily == cfCompatYUY2 && (subSamplingH != 0 || subSamplingW != 1 || bitsPerSample != 16 || sampleType != stInteger))
        return false;

    return true;
}

bool VSCore::isValidVideoFormat(const VSVideoFormat &format) noexcept {
    if (!isValidVideoFormat(format.colorFamily, format.sampleType, format.bitsPerSample, format.subSamplingW, format.subSamplingH))
        return false;

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
    if (format.colorFamily == cfCompatBGR32)
        return getV3VideoFormat(vs3::pfCompatBGR32);
    else if (format.colorFamily == cfCompatYUY2)
        return getV3VideoFormat(vs3::pfCompatYUY2);
    else
        return queryVideoFormat3(ColorFamilyToV3(format.colorFamily), static_cast<VSSampleType>(format.sampleType), format.bitsPerSample, format.subSamplingW, format.subSamplingH);
}

bool VSCore::VideoFormatFromV3(VSVideoFormat &out, const vs3::VSVideoFormat *format) noexcept {
    if (!format)
        return queryVideoFormat(out, cfUndefined, stInteger, 0, 0, 0);
    else if (format->id == vs3::pfCompatBGR32)
        return queryVideoFormat(out, cfCompatBGR32, stInteger, 32, 0, 0);
    else if (format->id == vs3::pfCompatYUY2)
        return queryVideoFormat(out, cfCompatYUY2, stInteger, 16, 1, 0);
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

void VSCore::getCoreInfo(VSCoreInfo &info) {
    info.versionString = VAPOURSYNTH_VERSION_STRING;
    info.core = VAPOURSYNTH_CORE_VERSION;
    info.api = VAPOURSYNTH_API_VERSION;
    info.numThreads = static_cast<int>(threadPool->threadCount());
    info.maxFramebufferSize = memory->getLimit();
    info.usedFramebufferSize = memory->memoryUse();
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

    const char *sampleTypeStr = "";
    if (format.sampleType == stFloat)
        sampleTypeStr = (format.bitsPerSample == 32) ? "S" : "H";

    const char *yuvName = nullptr;

    switch (format.colorFamily) {
        case cfGray:
            snprintf(buffer, 32, "Gray%s%d", sampleTypeStr, format.bitsPerSample);
            break;
        case cfRGB:
            snprintf(buffer, 32, "RGB%s%d", sampleTypeStr, format.bitsPerSample * 3);
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
                snprintf(buffer, 32, "YUV%sP%s%d", yuvName, sampleTypeStr, format.bitsPerSample);
            else
                snprintf(buffer, 32, "YUVssw%dssh%dP%s%d", format.subSamplingW, format.subSamplingH, sampleTypeStr, format.bitsPerSample);
            break;
        case cfCompatBGR32:
            snprintf(buffer, 32, "CompatBGR32");
            break;
        case cfCompatYUY2:
            snprintf(buffer, 32, "CompatYUY2");
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
        core->loadPlugin(vsapi->mapGetData(in, "path", 0, nullptr), forcens, forceid, altSearchPath);
    } catch (VSException &e) {
        vsapi->mapSetError(out, e.what());
    }
}

static void VS_CC loadAllPlugins(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
#ifdef VS_TARGET_OS_WINDOWS    
        core->loadAllPluginsInPath(utf16_from_utf8(vsapi->mapGetData(in, "path", 0, nullptr)), L".dll");
#else
    #ifdef VS_TARGET_OS_DARWIN
        core->loadAllPluginsInPath(vsapi->mapGetData(in, "path", 0, nullptr), ".dylib");
    #else
        core->loadAllPluginsInPath(vsapi->mapGetData(in, "path", 0, nullptr), ".so");
    #endif
#endif
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


#ifdef VS_TARGET_OS_WINDOWS
bool VSCore::loadAllPluginsInPath(const std::wstring &path, const std::wstring &filter) {
#else
bool VSCore::loadAllPluginsInPath(const std::string &path, const std::string &filter) {
#endif
    if (path.empty())
        return false;

#ifdef VS_TARGET_OS_WINDOWS
    std::wstring wPath = path + L"\\" + filter;
    WIN32_FIND_DATA findData;
    HANDLE findHandle = FindFirstFile(wPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return false;
    do {
        try {
            loadPlugin(utf16_to_utf8(path + L"\\" + findData.cFileName));
        } catch (VSException &) {
            // Ignore any errors
        }
    } while (FindNextFile(findHandle, &findData));
    FindClose(findHandle);
#else
    DIR *dir = opendir(path.c_str());
    if (!dir)
        return false;

    int name_max = pathconf(path.c_str(), _PC_NAME_MAX);
    if (name_max == -1)
        name_max = 255;

    while (true) {
        struct dirent *result = readdir(dir);
        if (!result) {
            break;
        }

        std::string name(result->d_name);
        // If name ends with filter
        if (name.size() >= filter.size() && name.compare(name.size() - filter.size(), filter.size(), filter) == 0) {
            try {
                std::string fullname;
                fullname.append(path).append("/").append(name);
                loadPlugin(fullname);
            } catch (VSException &) {
                // Ignore any errors
            }
        }
    }

    if (closedir(dir)) {
        // Shouldn't happen
    }
#endif

    return true;
}

void VSCore::functionInstanceCreated() {
    ++numFunctionInstances;
}

void VSCore::functionInstanceDestroyed() {
    --numFunctionInstances;
}

void VSCore::filterInstanceCreated() {
    ++numFilterInstances;
}

void VSCore::filterInstanceDestroyed() {
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

VSCore::VSCore(int flags) :
    coreFreed(false),
    enableGraphInspection(flags & ccfEnableGraphInspection),
    numFilterInstances(1),
    numFunctionInstances(0),
    videoFormatIdOffset(1000),
    cpuLevel(INT_MAX),
    memory(new MemoryUse()) {
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        logFatal("Bad SSE state detected when creating new core");
#endif

    bool disableAutoLoading = !!(flags & ccfDisableAutoLoading);
    threadPool = new VSThreadPool(this);

    registerFormats();

    // The internal plugin units, the loading is a bit special so they can get special flags
    VSPlugin *p;

    // Initialize internal plugins
    p = new VSPlugin(this);
    vs_internal_vspapi.configPlugin(VS_STD_PLUGIN_ID, "std", "VapourSynth Core Functions", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, p);
    loadPluginInitialize(p, &vs_internal_vspapi);
    cacheInitialize(p, &vs_internal_vspapi);
    exprInitialize(p, &vs_internal_vspapi);
    genericInitialize(p, &vs_internal_vspapi);
    lutInitialize(p, &vs_internal_vspapi);
    boxBlurInitialize(p, &vs_internal_vspapi);
    mergeInitialize(p, &vs_internal_vspapi);
    reorderInitialize(p, &vs_internal_vspapi);
    audioInitialize(p, &vs_internal_vspapi);
    stdlibInitialize(p, &vs_internal_vspapi);
    p->enableCompat();
    p->lock();

    plugins.insert(std::make_pair(p->getID(), p));
    p = new VSPlugin(this);
    resizeInitialize(p, &vs_internal_vspapi);
    plugins.insert(std::make_pair(p->getID(), p));
    p->enableCompat();

    plugins.insert(std::make_pair(p->getID(), p));
    p = new VSPlugin(this);
    textInitialize(p, &vs_internal_vspapi);
    plugins.insert(std::make_pair(p->getID(), p));
    p->enableCompat();

#ifdef VS_TARGET_OS_WINDOWS

    const std::wstring filter = L"*.dll";

#ifdef _WIN64
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth"
    std::wstring bits(L"64");
#else
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth-32"
    std::wstring bits(L"32");
#endif

    HMODULE module;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&vs_internal_vsapi, &module);
    std::vector<wchar_t> pathBuf(65536);
    GetModuleFileName(module, pathBuf.data(), (DWORD)pathBuf.size());
    std::wstring dllPath = pathBuf.data();
    dllPath.resize(dllPath.find_last_of('\\') + 1);
    std::wstring portableFilePath = dllPath + L"portable.vs";
    FILE *portableFile = _wfopen(portableFilePath.c_str(), L"rb");
    bool isPortable = !!portableFile;
    if (portableFile)
        fclose(portableFile);

    if (isPortable) {
        // Use alternative search strategy relative to dll path

        // Autoload bundled plugins
        std::wstring corePluginPath = dllPath + L"vapoursynth" + bits + L"\\coreplugins";
        if (!loadAllPluginsInPath(corePluginPath, filter))
            logMessage(mtCritical, "Core plugin autoloading failed. Installation is broken?");

        if (!disableAutoLoading) {
            // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
            // and accidentally block updated bundled versions
            std::wstring globalPluginPath = dllPath + L"vapoursynth" + bits + L"\\plugins";
            loadAllPluginsInPath(globalPluginPath, filter);
        }
    } else {
        // Autoload user specific plugins first so a user can always override
        std::vector<wchar_t> appDataBuffer(MAX_PATH + 1);
        if (SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataBuffer.data()) != S_OK)
            SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_DEFAULT, appDataBuffer.data());

        std::wstring appDataPath = std::wstring(appDataBuffer.data()) + L"\\VapourSynth\\plugins" + bits;

        // Autoload bundled plugins
        std::wstring corePluginPath = readRegistryValue(VS_INSTALL_REGKEY, L"CorePlugins");
        if (!loadAllPluginsInPath(corePluginPath, filter))
            logMessage(mtCritical, "Core plugin autoloading failed. Installation is broken!");

        if (!disableAutoLoading) {
            // Autoload per user plugins
            loadAllPluginsInPath(appDataPath, filter);

            // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
            // and accidentally block user installed ones
            std::wstring globalPluginPath = readRegistryValue(VS_INSTALL_REGKEY, L"Plugins");
            loadAllPluginsInPath(globalPluginPath, filter);
        }
    }

#else
    std::string configFile;
    const char *home = getenv("HOME");
#ifdef VS_TARGET_OS_DARWIN
    std::string filter = ".dylib";
    if (home) {
        configFile.append(home).append("/Library/Application Support/VapourSynth/vapoursynth.conf");
    }
#else
    std::string filter = ".so";
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home) {
        configFile.append(xdg_config_home).append("/vapoursynth/vapoursynth.conf");
    } else if (home) {
        configFile.append(home).append("/.config/vapoursynth/vapoursynth.conf");
    } // If neither exists, an empty string will do.
#endif

    VSMap *settings = readSettings(configFile);
    const char *error = vs_internal_vsapi.mapGetError(settings);
    if (error) {
        logMessage(mtWarning, error);
    } else {
        int err;
        const char *tmp;

        tmp = vs_internal_vsapi.mapGetData(settings, "UserPluginDir", 0, &err);
        std::string userPluginDir(tmp ? tmp : "");

        tmp = vs_internal_vsapi.mapGetData(settings, "SystemPluginDir", 0, &err);
        std::string systemPluginDir(tmp ? tmp : VS_PATH_PLUGINDIR);

        tmp = vs_internal_vsapi.mapGetData(settings, "AutoloadUserPluginDir", 0, &err);
        bool autoloadUserPluginDir = tmp ? std::string(tmp) == "true" : true;

        tmp = vs_internal_vsapi.mapGetData(settings, "AutoloadSystemPluginDir", 0, &err);
        bool autoloadSystemPluginDir = tmp ? std::string(tmp) == "true" : true;

        if (!disableAutoLoading && autoloadUserPluginDir && !userPluginDir.empty()) {
            if (!loadAllPluginsInPath(userPluginDir, filter)) {
                logMessage(mtWarning, "Autoloading the user plugin dir '" + userPluginDir + "' failed. Directory doesn't exist?");
            }
        }

        if (autoloadSystemPluginDir) {
            if (!loadAllPluginsInPath(systemPluginDir, filter)) {
                logMessage(mtCritical, "Autoloading the system plugin dir '" + systemPluginDir + "' failed. Directory doesn't exist?");
            }
        }
    }

    vs_internal_vsapi.freeMap(settings);
#endif
}

void VSCore::freeCore() {
    if (coreFreed)
        logFatal("Double free of core");
    coreFreed = true;
    threadPool->waitForDone();
    if (numFilterInstances > 1)
        logMessage(mtWarning, "Core freed but " + std::to_string(numFilterInstances.load() - 1) + " filter instance(s) still exist");
    if (memory->memoryUse() > 0)
        logMessage(mtWarning, "Core freed but " + std::to_string(memory->memoryUse()) + " bytes still allocated in framebuffers");
    if (numFunctionInstances > 0)
        logMessage(mtWarning, "Core freed but " + std::to_string(numFunctionInstances.load()) + " function instance(s) still exist");
    // Remove all message handlers on free to prevent a zombie core from crashing the whole application by calling a no longer usable
    // message handler
    while (!messageHandlers.empty())
        removeLogHandler(*messageHandlers.begin());
    // Release the extra filter instance that always keeps the core alive
    filterInstanceDestroyed();
}

VSCore::~VSCore() {
    memory->signalFree();
    delete threadPool;
    for(const auto &iter : plugins)
        delete iter.second;
    plugins.clear();
}

VSMap *VSCore::getPlugins3() {
    VSMap *m = new VSMap;
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    int num = 0;
    for (const auto &iter : plugins) {
        std::string b = iter.second->getNamespace() + ";" + iter.second->getID() + ";" + iter.second->getName();
        vs_internal_vsapi.mapSetData(m, ("Plugin" + std::to_string(++num)).c_str(), b.c_str(), static_cast<int>(b.size()), dtUtf8, paReplace);
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

void VSCore::loadPlugin(const std::string &filename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath) {
    VSPlugin *p = new VSPlugin(filename, forcedNamespace, forcedId, altSearchPath, this);

    std::lock_guard<std::recursive_mutex> lock(pluginLock);

    VSPlugin *already_loaded_plugin = getPluginByID(p->getID());
    if (already_loaded_plugin) {
        std::string error = "Plugin " + filename + " already loaded (" + p->getID() + ")";
        if (already_loaded_plugin->getFilename().size())
            error += " from " + already_loaded_plugin->getFilename();
        delete p;
        throw VSException(error);
    }

    already_loaded_plugin = getPluginByNamespace(p->getNamespace());
    if (already_loaded_plugin) {
        std::string error = "Plugin load of " + filename + " failed, namespace " + p->getNamespace() + " already populated";
        if (already_loaded_plugin->getFilename().size())
            error += " by " + already_loaded_plugin->getFilename();
        delete p;
        throw VSException(error);
    }

    plugins.insert(std::make_pair(p->getID(), p));

    // allow avisynth plugins to accept legacy avisynth formats
    if (p->getNamespace() == "avs" && p->getID() == "com.vapoursynth.avisynth")
        p->enableCompat();
}

void VSCore::createFilter3(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiMajor, this);
        for (size_t i = 0; i < node->getNumOutputs(); i++) {
            VSNodeRef *ref = new VSNodeRef(node, static_cast<int>(i));
            vs_internal_vsapi.mapSetNode(out, "clip", ref, paAppend);
            ref->release();
        }
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

void VSCore::createVideoFilter(VSMap *out, const std::string &name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(name, vi, numOutputs, getFrame, free, filterMode, flags, instanceData, apiMajor, this);
        for (size_t i = 0; i < node->getNumOutputs(); i++) {
            VSNodeRef *ref = new VSNodeRef(node, static_cast<int>(i));
            vs_internal_vsapi.mapSetNode(out, "clip", ref, paAppend);
            ref->release();
        }
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

void VSCore::createAudioFilter(VSMap *out, const std::string &name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        VSNode *node = new VSNode(name, ai, numOutputs, getFrame, free, filterMode, flags, instanceData, apiMajor, this);
        for (size_t i = 0; i < node->getNumOutputs(); i++) {
            VSNodeRef *ref = new VSNodeRef(node, static_cast<int>(i));
            vs_internal_vsapi.mapSetNode(out, "clip", ref, paAppend);
            ref->release();
        }
    } catch (VSException &e) {
        vs_internal_vsapi.mapSetError(out, e.what());
    }
}

int VSCore::getCpuLevel() const {
    return cpuLevel;
}

int VSCore::setCpuLevel(int cpu) {
    return cpuLevel.exchange(cpu);
}

VSPlugin::VSPlugin(VSCore *core)
    : libHandle(0), core(core) {
}

static void VS_CC configPlugin3(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin) VS_NOEXCEPT {
    assert(identifier && defaultNamespace && name && plugin);
    plugin->configPlugin(identifier, defaultNamespace, name, -1, apiVersion, readOnly ? 0 : pcModifiable);
}

VSPlugin::VSPlugin(const std::string &relFilename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath, VSCore *core)
    : core(core), fnamespace(forcedNamespace), id(forcedId) {
#ifdef VS_TARGET_OS_WINDOWS
    std::wstring wPath = utf16_from_utf8(relFilename);
    std::vector<wchar_t> fullPathBuffer(32767 + 1); // add 1 since msdn sucks at mentioning whether or not it includes the final null
    if (wPath.substr(0, 4) != L"\\\\?\\")
        wPath = L"\\\\?\\" + wPath;
    GetFullPathName(wPath.c_str(), static_cast<DWORD>(fullPathBuffer.size()), fullPathBuffer.data(), nullptr);
    wPath = fullPathBuffer.data();
    if (wPath.substr(0, 4) == L"\\\\?\\")
        wPath = wPath.substr(4);
    filename = utf16_to_utf8(wPath);
    for (auto &iter : filename)
        if (iter == '\\')
            iter = '/';

    libHandle = LoadLibraryEx(wPath.c_str(), nullptr, altSearchPath ? 0 : (LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR));

    if (libHandle == INVALID_HANDLE_VALUE) {
        DWORD lastError = GetLastError();

        if (lastError == 126)
            throw VSException("Failed to load " + relFilename + ". GetLastError() returned " + std::to_string(lastError) + ". The file you tried to load or one of its dependencies is probably missing.");
        throw VSException("Failed to load " + relFilename + ". GetLastError() returned " + std::to_string(lastError) + ".");
    }

    VSInitPlugin pluginInit = reinterpret_cast<VSInitPlugin>(GetProcAddress(libHandle, "VapourSynthPluginInit2"));

    if (!pluginInit)
        pluginInit = reinterpret_cast<VSInitPlugin>(GetProcAddress(libHandle, "_VapourSynthPluginInit2@8"));

    vs3::VSInitPlugin pluginInit3 = nullptr;
    if (!pluginInit)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(GetProcAddress(libHandle, "VapourSynthPluginInit"));

    if (!pluginInit3)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(GetProcAddress(libHandle, "_VapourSynthPluginInit@12"));

    if (!pluginInit && !pluginInit3) {
        FreeLibrary(libHandle);
        throw VSException("No entry point found in " + relFilename);
    }
#else
    std::vector<char> fullPathBuffer(PATH_MAX + 1);
    if (realpath(relFilename.c_str(), fullPathBuffer.data()))
        filename = fullPathBuffer.data();
    else
        filename = relFilename;

    libHandle = dlopen(filename.c_str(), RTLD_LAZY);

    if (!libHandle) {
        const char *dlError = dlerror();
        if (dlError)
            throw VSException("Failed to load " + relFilename + ". Error given: " + dlError);
        else
            throw VSException("Failed to load " + relFilename);
    }

    VSInitPlugin pluginInit = reinterpret_cast<VSInitPlugin>(dlsym(libHandle, "VapourSynthPluginInit2"));
    vs3::VSInitPlugin pluginInit3 = nullptr;
    if (!pluginInit3)
        pluginInit3 = reinterpret_cast<vs3::VSInitPlugin>(dlsym(libHandle, "VapourSynthPluginInit"));

    if (!pluginInit && !pluginInit3) {
        dlclose(libHandle);
        throw VSException("No entry point found in " + relFilename);
    }


#endif
    if (pluginInit)
        pluginInit(this, &vs_internal_vspapi);
    else
        pluginInit3(configPlugin3, vs_internal_vsapi3.registerFunction, this);

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after loading " + filename);
#endif

    if (readOnlySet)
        readOnly = true;

    bool supported = (apiMajor == VAPOURSYNTH_API_MAJOR && apiMinor <= VAPOURSYNTH_API_MINOR) || (apiMajor == VAPOURSYNTH3_API_MAJOR && apiMinor <= VAPOURSYNTH3_API_MINOR);

    if (!supported) {
#ifdef VS_TARGET_OS_WINDOWS
        FreeLibrary(libHandle);
#else
        dlclose(libHandle);
#endif
        throw VSException("Core only supports API R" + std::to_string(VAPOURSYNTH_API_MAJOR) + "." + std::to_string(VAPOURSYNTH_API_MINOR) + " but the loaded plugin requires API R" + std::to_string(apiMajor) + "." + std::to_string(apiMinor) + "; Filename: " + relFilename + "; Name: " + fullname);
    }
}

VSPlugin::~VSPlugin() {
#ifdef VS_TARGET_OS_WINDOWS
    if (libHandle != INVALID_HANDLE_VALUE)
        FreeLibrary(libHandle);
#else
    if (libHandle)
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

bool VSMap::hasCompatNodes() const noexcept {
    for (const auto &iter : data->data) {
        if (iter.second->type() == ptVideoNode) {
            VSVideoNodeArray *arr = reinterpret_cast<VSVideoNodeArray *>(iter.second.get());
            for (size_t i = 0; i < arr->size(); i++) {
                for (size_t j = 0; j < arr->at(i)->clip->getNumOutputs(); j++) {
                    const VSVideoInfo &vi = arr->at(i)->clip->getVideoInfo(static_cast<int>(j));
                    if (vi.format.colorFamily == cfCompatBGR32 || vi.format.colorFamily == cfCompatYUY2)
                        return true;
                }
            }
        }
    }
    return false;
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
            vs_internal_vsapi.mapSetData(out, f.first.c_str(), b.c_str(), static_cast<int>(b.size()), dtUtf8, paReplace);
        }
    }
}

#ifdef VS_TARGET_CPU_X86
static int alignmentHelper() {
    return getCPUFeatures()->avx512_f ? 64 : 32;
}

int VSFrameRef::alignment = alignmentHelper();
#else
int VSFrameRef::alignment = 32;
#endif

thread_local PVSFunctionFrame VSCore::functionFrame;