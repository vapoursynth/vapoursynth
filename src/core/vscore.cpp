/*
* Copyright (c) 2012-2015 Fredrik Mellbin
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
#include "VSHelper.h"
#include "version.h"
#ifndef VS_TARGET_OS_WINDOWS
#include <dirent.h>
#include <stddef.h>
#include <unistd.h>
#include "settings.h"
#endif
#include <assert.h>

#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

#ifdef VS_TARGET_OS_WINDOWS
#include <shlobj.h>
#include <codecvt>
#endif

// Internal filter headers
extern "C" {
#include "lutfilters.h"
#include "mergefilters.h"
#include "reorderfilters.h"
#include "simplefilters.h"
#include "vsresize.h"
}
#include "cachefilter.h"
#include "exprfilter.h"
#include "textfilter.h"
#include "genericfilters.h"

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
static std::wstring readRegistryValue(const std::wstring keyName, const std::wstring &valueName) {
    HKEY hKey;
#ifdef _WIN64
    LONG lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
#else
    LONG lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName.c_str(), 0, KEY_READ, &hKey);
#endif
    if (lRes != ERROR_SUCCESS)
        return std::wstring();
    WCHAR szBuffer[512];
    DWORD dwBufferSize = sizeof(szBuffer);
    ULONG nError;
    nError = RegQueryValueEx(hKey, valueName.c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
    RegCloseKey(hKey);
    if (ERROR_SUCCESS == nError)
        return szBuffer;
    return std::wstring();
}
#endif

FrameContext::FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext) :
numFrameRequests(0), n(n), clip(clip), upstreamContext(upstreamContext), userData(NULL), frameDone(NULL), error(false), node(NULL), lastCompletedN(-1), index(index), lastCompletedNode(NULL), frameContext(NULL) {
}

FrameContext::FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData) :
numFrameRequests(0), n(n), clip(node->clip.get()), userData(userData), frameDone(frameDone), error(false), node(node), lastCompletedN(-1), index(index), lastCompletedNode(NULL), frameContext(NULL) {
}

bool FrameContext::setError(const std::string &errorMsg) {
    bool prevState = error;
    error = true;
    if (!prevState)
        errorMessage = errorMsg;
    return prevState;
}

///////////////


VSVariant::VSVariant(VSVType vtype) : vtype(vtype), internalSize(0), storage(NULL) {
}

VSVariant::VSVariant(const VSVariant &v) : vtype(v.vtype), internalSize(v.internalSize), storage(nullptr) {
    if (internalSize) {
        switch (vtype) {
        case VSVariant::vInt:
            storage = new IntList(*reinterpret_cast<IntList *>(v.storage)); break;
        case VSVariant::vFloat:
            storage = new FloatList(*reinterpret_cast<FloatList *>(v.storage)); break;
        case VSVariant::vData:
            storage = new DataList(*reinterpret_cast<DataList *>(v.storage)); break;
        case VSVariant::vNode:
            storage = new NodeList(*reinterpret_cast<NodeList *>(v.storage)); break;
        case VSVariant::vFrame:
            storage = new FrameList(*reinterpret_cast<FrameList *>(v.storage)); break;
        case VSVariant::vMethod:
            storage = new FuncList(*reinterpret_cast<FuncList *>(v.storage)); break;
        }
    }
}

VSVariant::VSVariant(VSVariant &&v) : vtype(v.vtype), internalSize(v.internalSize), storage(v.storage) {
    v.vtype = vUnset;
    v.storage = nullptr;
    v.internalSize = 0;
}

VSVariant::~VSVariant() {
    if (storage) {
        switch (vtype) {
        case VSVariant::vInt:
            delete reinterpret_cast<IntList *>(storage); break;
        case VSVariant::vFloat:
            delete reinterpret_cast<FloatList *>(storage); break;
        case VSVariant::vData:
            delete reinterpret_cast<DataList *>(storage); break;
        case VSVariant::vNode:
            delete reinterpret_cast<NodeList *>(storage); break;
        case VSVariant::vFrame:
            delete reinterpret_cast<FrameList *>(storage); break;
        case VSVariant::vMethod:
            delete reinterpret_cast<FuncList *>(storage); break;
        }
    }
}

size_t VSVariant::size() const {
    return internalSize;
}

VSVariant::VSVType VSVariant::getType() const {
    return vtype;
}

void VSVariant::append(int64_t val) {
    initStorage(vInt);
    reinterpret_cast<IntList *>(storage)->push_back(val);
    internalSize++;
}

void VSVariant::append(double val) {
    initStorage(vFloat);
    reinterpret_cast<FloatList *>(storage)->push_back(val);
    internalSize++;
}

void VSVariant::append(const std::string &val) {
    initStorage(vData);
    reinterpret_cast<DataList *>(storage)->push_back(std::make_shared<std::string>(val));
    internalSize++;
}

void VSVariant::append(const VSNodeRef &val) {
    initStorage(vNode);
    reinterpret_cast<NodeList *>(storage)->push_back(val);
    internalSize++;
}

void VSVariant::append(const PVideoFrame &val) {
    initStorage(vFrame);
    reinterpret_cast<FrameList *>(storage)->push_back(val);
    internalSize++;
}

void VSVariant::append(const PExtFunction &val) {
    initStorage(vMethod);
    reinterpret_cast<FuncList *>(storage)->push_back(val);
    internalSize++;
}

void VSVariant::initStorage(VSVType t) {
    assert(vtype == vUnset || vtype == t);
    vtype = t;
    if (!storage) {
        switch (t) {
        case VSVariant::vInt:
            storage = new IntList(); break;
        case VSVariant::vFloat:
            storage = new FloatList(); break;
        case VSVariant::vData:
            storage = new DataList(); break;
        case VSVariant::vNode:
            storage = new NodeList(); break;
        case VSVariant::vFrame:
            storage = new FrameList(); break;
        case VSVariant::vMethod:
            storage = new FuncList(); break;
        }
    }
}

///////////////

VSPlaneData::VSPlaneData(uint32_t dataSize, MemoryUse *mem) : mem(mem), size(dataSize + 2 * VSFrame::guardSpace) {
    data = vs_aligned_malloc<uint8_t>(size + 2 * VSFrame::guardSpace, VSFrame::alignment);
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for planes. Out of memory.");
    mem->add(size);
#ifdef VS_FRAME_GUARD
    for (size_t i = 0; i < VSFrame::guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
        reinterpret_cast<uint32_t *>(data)[i] = VS_FRAME_GUARD_PATTERN;
        reinterpret_cast<uint32_t *>(data + size - VSFrame::guardSpace)[i] = VS_FRAME_GUARD_PATTERN;
    }
#endif
}

VSPlaneData::VSPlaneData(const VSPlaneData &d) : mem(d.mem), size(d.size) {
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for plane in copy constructor. Out of memory.");
    mem->add(size);
    memcpy(data, d.data, size);
}

VSPlaneData::~VSPlaneData() {
    vs_aligned_free(data);
    mem->subtract(size);
}

///////////////

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc, VSCore *core) : format(f), width(width), height(height) {
    if (!f || width <= 0 || height <= 0)
        vsFatal("Invalid new frame");

    if (propSrc)
        properties = propSrc->properties;

    stride[0] = (width * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (f->numPlanes == 3) {
        int plane23 = ((width >> f->subSamplingW) * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    data[0] = std::make_shared<VSPlaneData>(stride[0] * height, core->memory);
    if (f->numPlanes == 3) {
        int size23 = stride[1] * (height >> f->subSamplingH);
        data[1] = std::make_shared<VSPlaneData>(size23, core->memory);
        data[2] = std::make_shared<VSPlaneData>(size23, core->memory);
    }
}

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core) : format(f), width(width), height(height) {
    if (!f || width <= 0 || height <= 0)
        vsFatal("Invalid new frame");

    if (propSrc)
        properties = propSrc->properties;

    stride[0] = (width * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (f->numPlanes == 3) {
        int plane23 = ((width >> f->subSamplingW) * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    for (int i = 0; i < format->numPlanes; i++) {
        if (planeSrc[i]) {
            if (plane[i] < 0 || plane[i] >= planeSrc[i]->format->numPlanes)
                vsFatal("Plane does not exist, error in frame creation");
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                vsFatal("Copied plane dimensions do not match, error in frame creation");
            data[i] = planeSrc[i]->data[plane[i]];
        } else {
            if (i == 0) {
                data[i] = std::make_shared<VSPlaneData>(stride[0] * height, core->memory);
            } else {
                data[i] = std::make_shared<VSPlaneData>(stride[i] * (height >> f->subSamplingH), core->memory);
            }
        }
    }
}

VSFrame::VSFrame(const VSFrame &f) {
    data[0] = f.data[0];
    data[1] = f.data[1];
    data[2] = f.data[2];
    format = f.format;
    width = f.width;
    height = f.height;
    stride[0] = f.stride[0];
    stride[1] = f.stride[1];
    stride[2] = f.stride[2];
    properties = f.properties;
}

int VSFrame::getStride(int plane) const {
    assert(plane >= 0 && plane < 3);
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane stride requested");
    return stride[plane];
}

const uint8_t *VSFrame::getReadPtr(int plane) const {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane requested");

    return data[plane]->data + guardSpace;
}

uint8_t *VSFrame::getWritePtr(int plane) {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane requested");

    // copy the plane data if this isn't the only reference
    if (!data[plane].unique())
        data[plane] = std::make_shared<VSPlaneData>(*data[plane].get());

    return data[plane]->data + guardSpace;
}

#ifdef VS_FRAME_GUARD
bool VSFrame::verifyGuardPattern() {
    for (int p = 0; p < format->numPlanes; p++) {
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

VSFunction::VSFunction(const std::string &argString, VSPublicFunction func, void *functionData)
    : argString(argString), functionData(functionData), func(func) {
    std::list<std::string> argList;
    split(argList, argString, std::string(";"), split1::no_empties);
    for(const std::string &arg : argList) {
        std::vector<std::string> argParts;
        split(argParts, arg, std::string(":"), split1::no_empties);

        if (argParts.size() < 2)
            vsFatal("Invalid argument specifier: %s", arg.c_str());

        bool arr = false;
        enum FilterArgumentType type = faNone;
        const std::string &argName = argParts[0];
        const std::string &typeName = argParts[1];

        if (typeName == "int")
            type = faInt;
        else if (typeName == "float")
            type = faFloat;
        else if (typeName == "data")
            type = faData;
        else if (typeName == "clip")
            type = faClip;
        else if (typeName == "frame")
            type = faFrame;
        else if (typeName == "func")
            type = faFunc;
        else {
            arr = true;

            if (typeName == "int[]")
                type = faInt;
            else if (typeName == "float[]")
                type = faFloat;
            else if (typeName == "data[]")
                type = faData;
            else if (typeName == "clip[]")
                type = faClip;
            else if (typeName == "frame[]")
                type = faFrame;
            else if (typeName == "func[]")
                type = faFunc;
            else
                vsFatal("Invalid arg string: %s", typeName.c_str());
        }

        bool opt = false;
        bool empty = false;

        for (size_t i = 2; i < argParts.size(); i++) {
            if (argParts[i] == "opt") {
                if (opt)
                    vsFatal("Duplicate argument specifier: %s", argParts[i].c_str());
                opt = true;
            } else if (argParts[i] == "empty") {
                if (empty)
                    vsFatal("Duplicate argument specifier: %s", argParts[i].c_str());
                empty = true;
            }  else {
                vsFatal("Unknown argument modifier: %s", argParts[i].c_str());
            }
        }

        if (!isValidIdentifier(argName))
            vsFatal("Illegal argument identifier specified for function");

        if (empty && !arr)
            vsFatal("Only array arguments can have the empty flag set");

        args.push_back(FilterArgument(argName, type, arr, empty, opt));
    }
}

VSNode::VSNode(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
instanceData(instanceData), name(name), init(init), filterGetFrame(getFrame), free(free), filterMode(filterMode), apiMajor(apiMajor), core(core), flags(flags), hasVi(false), serialFrame(-1) {

    if (flags & ~(nfNoCache | nfIsCache))
        vsFatal("Filter %s specified unknown flags", name.c_str());

    if ((flags & nfIsCache) && !(flags & nfNoCache))
        vsFatal("Filter %s specified an illegal combination of flags (%d)", name.c_str(), flags);

    core->filterInstanceCreated();
    VSMap inval(*in);
    init(&inval, out, &this->instanceData, this, core, getVSAPIInternal(apiMajor));

    if (out->hasError()) {
        core->filterInstanceDestroyed();
        throw VSException(vsapi.getError(out));
    }

    if (!hasVi)
        vsFatal("Filter %s didn't set vi", name.c_str());

    for (const auto &iter : vi) {
        if (iter.numFrames <= 0) {
            core->filterInstanceDestroyed();
            throw VSException("Filter creation aborted, zero (unknown) and negative length clips not allowed");
        }
    }
}

VSNode::~VSNode() {
    if (free)
        free(instanceData, core, &vsapi);

    core->filterInstanceDestroyed();
}

void VSNode::getFrame(const PFrameContext &ct) {
    core->threadPool->start(ct);
}

const VSVideoInfo &VSNode::getVideoInfo(int index) {
    if (index < 0 || index >= (int)vi.size())
        vsFatal("Out of bounds videoinfo index");
    return vi[index];
}

void VSNode::setVideoInfo(const VSVideoInfo *vi, int numOutputs) {
    if (numOutputs < 1)
        vsFatal("Video filter needs to have at least one output");
    for (int i = 0; i < numOutputs; i++) {
        if ((!!vi[i].height) ^ (!!vi[i].width))
            vsFatal("Variable dimension clips must have both width and height set to 0");
        if (vi[i].format && !core->isValidFormatPointer(vi[i].format))
            vsFatal("The VSFormat pointer passed to setVideoInfo() was not gotten from registerFormat() or getFormatPreset()");

        this->vi.push_back(vi[i]);
        this->vi[i].flags = flags;
    }
    hasVi = true;
}

PVideoFrame VSNode::getFrameInternal(int n, int activationReason, VSFrameContext &frameCtx) {
    const VSFrameRef *r = filterGetFrame(n, activationReason, &instanceData, &frameCtx.ctx->frameContext, &frameCtx, core, &vsapi);

#ifdef VS_TARGET_CPU_X86
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected after return from %s", name.c_str());
#endif
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after return from %s", name.c_str());
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after return from %s", name.c_str());
#endif

    if (r) {
        PVideoFrame p(std::move(r->frame));
        delete r;
        const VSFormat *fi = p->getFormat();
        const VSVideoInfo &lvi = vi[frameCtx.ctx->index];

        if (!lvi.format && fi->colorFamily == cmCompat)
            vsFatal("Illegal compat frame returned");
        else if (lvi.format && lvi.format != fi)
            vsFatal("Frame returned not of the declared type");
        else if ((lvi.width || lvi.height) && (p->getWidth(0) != lvi.width || p->getHeight(0) != lvi.height))
            vsFatal("Frame returned of not of the declared dimensions");

#ifdef VS_FRAME_GUARD
        if (!p->verifyGuardPattern())
            vsFatal("Guard memory corrupted in frame %d returned from %s", n, name.c_str());
#endif

        return p;
    }

    PVideoFrame p;
    return p;
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
    CacheInstance *cache = (CacheInstance *)instanceData;
    cache->cache.adjustSize(needMemory);
}

PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc) {
    return std::make_shared<VSFrame>(f, width, height, propSrc, this);
}

PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *planes, const VSFrame *propSrc) {
    return std::make_shared<VSFrame>(f, width, height, planeSrc, planes, propSrc, this);
}

PVideoFrame VSCore::copyFrame(const PVideoFrame &srcf) {
    return std::make_shared<VSFrame>(*srcf.get());
}

void VSCore::copyFrameProps(const PVideoFrame &src, PVideoFrame &dst) {
    dst->setProperties(src->getProperties());
}

const VSFormat *VSCore::getFormatPreset(int id) {
    std::lock_guard<std::mutex> lock(formatLock);

    auto f = formats.find(id);
    if (f != formats.end())
        return f->second;
    return nullptr;
}

const VSFormat *VSCore::registerFormat(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name, int id) {
    // this is to make exact format comparisons easy by simply allowing pointer comparison

    // block nonsense formats
    if (subSamplingH < 0 || subSamplingW < 0 || subSamplingH > 4 || subSamplingW > 4)
        return nullptr;

    if (sampleType < 0 || sampleType > 1)
        return nullptr;

    if (colorFamily == cmRGB && (subSamplingH != 0 || subSamplingW != 0))
        return nullptr;

    if (sampleType == stFloat && (bitsPerSample != 16 && bitsPerSample != 32))
        return nullptr;

    if (bitsPerSample < 8 || bitsPerSample > 32)
        return nullptr;

    if (colorFamily == cmCompat && !name)
        return nullptr;

    std::lock_guard<std::mutex> lock(formatLock);

    for (const auto &iter : formats) {
        const VSFormat *f = iter.second;

        if (f->colorFamily == colorFamily && f->sampleType == sampleType
                && f->subSamplingW == subSamplingW && f->subSamplingH == subSamplingH && f->bitsPerSample == bitsPerSample)
            return f;
    }

    VSFormat *f = new VSFormat();
    memset(f->name, 0, sizeof(f->name));

    if (name) {
        strcpy(f->name, name);
    } else {
        const char *sampleTypeStr = "";
        if (sampleType == stFloat)
            sampleTypeStr = (bitsPerSample == 32) ? "S" : "H";

        const char *yuvName = nullptr;

        switch (colorFamily) {
        case cmGray:
            sprintf(f->name, "Gray%s%d", sampleTypeStr, bitsPerSample);
            break;
        case cmRGB:
            sprintf(f->name, "RGB%s%d", sampleTypeStr, bitsPerSample * 3);
            break;
        case cmYUV:
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
                sprintf(f->name, "YUV%sP%s%d", yuvName, sampleTypeStr, bitsPerSample);
            else
                sprintf(f->name, "YUVssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
            break;
        case cmYCoCg:
            sprintf(f->name, "YCoCgssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
            break;
        }
    }

    if (id != pfNone)
        f->id = id;
    else
        f->id = colorFamily + formatIdOffset++;

    f->colorFamily = colorFamily;
    f->sampleType = sampleType;
    f->bitsPerSample = bitsPerSample;
    f->bytesPerSample = 1;

    while (f->bytesPerSample * 8 < bitsPerSample)
        f->bytesPerSample *= 2;

    f->subSamplingW = subSamplingW;
    f->subSamplingH = subSamplingH;
    f->numPlanes = (colorFamily == cmGray || colorFamily == cmCompat) ? 1 : 3;

    formats.insert(std::make_pair(f->id, f));
    return f;
}

bool VSCore::isValidFormatPointer(const VSFormat *f) {
    std::lock_guard<std::mutex> lock(formatLock);

    for (const auto &iter : formats) {
        if (iter.second == f)
            return true;
    }
    return false;
}

const VSCoreInfo &VSCore::getCoreInfo() {
    coreInfo.versionString = VAPOURSYNTH_VERSION_STRING;
    coreInfo.core = VAPOURSYNTH_CORE_VERSION;
    coreInfo.api = VAPOURSYNTH_API_VERSION;
    coreInfo.numThreads = threadPool->threadCount();
    coreInfo.maxFramebufferSize = memory->getLimit();
    coreInfo.usedFramebufferSize = memory->memoryUse();
    return coreInfo;
}

void VS_CC configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin);
void VS_CC registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);

static void VS_CC loadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
        int err;
        const char *forcens = vsapi->propGetData(in, "forcens", 0, &err);
        if (!forcens)
            forcens = "";
        const char *forceid = vsapi->propGetData(in, "forceid", 0, &err);
        if (!forceid)
            forceid = "";
        core->loadPlugin(vsapi->propGetData(in, "path", 0, 0), forcens, forceid);
    } catch (VSException &e) {
        vsapi->setError(out, e.what());
    }
}

void VS_CC loadPluginInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("LoadPlugin", "path:data;forcens:data:opt;forceid:data:opt;", &loadPlugin, NULL, plugin);
}

// not the most elegant way but avoids the mess that would happen if avscompat.h was included
#if defined(VS_TARGET_OS_WINDOWS) && defined(VS_FEATURE_AVISYNTH)
extern "C" void VS_CC avsWrapperInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
#endif

void VSCore::registerFormats() {
    // Register known formats with informational names
    registerFormat(cmGray, stInteger,  8, 0, 0, "Gray8", pfGray8);
    registerFormat(cmGray, stInteger, 16, 0, 0, "Gray16", pfGray16);

    registerFormat(cmGray, stFloat,   16, 0, 0, "GrayH", pfGrayH);
    registerFormat(cmGray, stFloat,   32, 0, 0, "GrayS", pfGrayS);

    registerFormat(cmYUV,  stInteger, 8, 1, 1, "YUV420P8", pfYUV420P8);
    registerFormat(cmYUV,  stInteger, 8, 1, 0, "YUV422P8", pfYUV422P8);
    registerFormat(cmYUV,  stInteger, 8, 0, 0, "YUV444P8", pfYUV444P8);
    registerFormat(cmYUV,  stInteger, 8, 2, 2, "YUV410P8", pfYUV410P8);
    registerFormat(cmYUV,  stInteger, 8, 2, 0, "YUV411P8", pfYUV411P8);
    registerFormat(cmYUV,  stInteger, 8, 0, 1, "YUV440P8", pfYUV440P8);

    registerFormat(cmYUV,  stInteger, 9, 1, 1, "YUV420P9", pfYUV420P9);
    registerFormat(cmYUV,  stInteger, 9, 1, 0, "YUV422P9", pfYUV422P9);
    registerFormat(cmYUV,  stInteger, 9, 0, 0, "YUV444P9", pfYUV444P9);

    registerFormat(cmYUV,  stInteger, 10, 1, 1, "YUV420P10", pfYUV420P10);
    registerFormat(cmYUV,  stInteger, 10, 1, 0, "YUV422P10", pfYUV422P10);
    registerFormat(cmYUV,  stInteger, 10, 0, 0, "YUV444P10", pfYUV444P10);

    registerFormat(cmYUV,  stInteger, 16, 1, 1, "YUV420P16", pfYUV420P16);
    registerFormat(cmYUV,  stInteger, 16, 1, 0, "YUV422P16", pfYUV422P16);
    registerFormat(cmYUV,  stInteger, 16, 0, 0, "YUV444P16", pfYUV444P16);

    registerFormat(cmYUV,  stFloat,   16, 0, 0, "YUV444PH", pfYUV444PH);
    registerFormat(cmYUV,  stFloat,   32, 0, 0, "YUV444PS", pfYUV444PS);

    registerFormat(cmRGB,  stInteger, 8, 0, 0, "RGB24", pfRGB24);
    registerFormat(cmRGB,  stInteger, 9, 0, 0, "RGB27", pfRGB27);
    registerFormat(cmRGB,  stInteger, 10, 0, 0, "RGB30", pfRGB30);
    registerFormat(cmRGB,  stInteger, 16, 0, 0, "RGB48", pfRGB48);

    registerFormat(cmRGB,  stFloat,   16, 0, 0, "RGBH", pfRGBH);
    registerFormat(cmRGB,  stFloat,   32, 0, 0, "RGBS", pfRGBS);

    registerFormat(cmCompat,  stInteger, 32, 0, 0, "CompatBGR32", pfCompatBGR32);
    registerFormat(cmCompat,  stInteger, 16, 1, 0, "CompatYUY2", pfCompatYUY2);
}


#ifdef VS_TARGET_OS_WINDOWS
bool VSCore::loadAllPluginsInPath(const std::wstring &path, const std::wstring &filter) {
#else
bool VSCore::loadAllPluginsInPath(const std::string &path, const std::string &filter) {
#endif
    if (path.empty())
        return false;

#ifdef VS_TARGET_OS_WINDOWS
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    std::wstring wPath = path + L"\\" + filter;
    WIN32_FIND_DATA findData;
    HANDLE findHandle = FindFirstFile(wPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return false;
    do {
        try {
            loadPlugin(conversion.to_bytes(path + L"\\" + findData.cFileName));
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

    size_t len = offsetof(struct dirent, d_name) + name_max + 1;

    while (true) {
        struct dirent *entry = (struct dirent *)malloc(len);
        struct dirent *result;
        readdir_r(dir, entry, &result);
        if (!result)
            break;

        std::string name(entry->d_name);
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

        free(entry);
    }

    if (closedir(dir)) {
        // Shouldn't happen
    }
#endif

    return true;
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

VSCore::VSCore(int threads) : coreFreed(false), numFilterInstances(1), formatIdOffset(1000), memory(new MemoryUse()) {
#ifdef VS_TARGET_CPU_X86
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected when creating new core");
#endif
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected when creating new core. Any other FPU state warnings after this one should be ignored.");
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected when creating new core");
#endif

    threadPool = new VSThreadPool(this, threads);

    registerFormats();

    // The internal plugin units, the loading is a bit special so they can get special flags
    VSPlugin *p;

    // Initialize internal plugins
#if defined(VS_TARGET_OS_WINDOWS) && defined(VS_FEATURE_AVISYNTH)
    p = new VSPlugin(this);
    avsWrapperInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::make_pair(p->id, p));
    p->enableCompat();
#endif

    p = new VSPlugin(this);
    configPlugin("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 0, p);
    loadPluginInitialize(::configPlugin, ::registerFunction, p);
    cacheInitialize(::configPlugin, ::registerFunction, p);
    exprInitialize(::configPlugin, ::registerFunction, p);
    genericInitialize(::configPlugin, ::registerFunction, p);
    lutInitialize(::configPlugin, ::registerFunction, p);
    mergeInitialize(::configPlugin, ::registerFunction, p);
    reorderInitialize(::configPlugin, ::registerFunction, p);
    stdlibInitialize(::configPlugin, ::registerFunction, p);
    p->enableCompat();
    p->lock();

    plugins.insert(std::make_pair(p->id, p));
    p = new VSPlugin(this);
    resizeInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::make_pair(p->id, p));
    p->enableCompat();

    plugins.insert(std::make_pair(p->id, p));
    p = new VSPlugin(this);
    textInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::make_pair(p->id, p));
    p->enableCompat();

#ifdef VS_TARGET_OS_WINDOWS
    const std::wstring filter = L"*.dll";
    // Autoload user specific plugins first so a user can always override
    std::vector<wchar_t> appDataBuffer(MAX_PATH + 1);
    SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataBuffer.data());

#ifdef _WIN64
    std::wstring bits(L"64");
#else
    std::wstring bits(L"32");
#endif

    std::wstring appDataPath = std::wstring(appDataBuffer.data()) + L"\\VapourSynth\\plugins" + bits;

    // Autoload per user plugins
    loadAllPluginsInPath(appDataPath, filter);

    // Autoload bundled plugins
    std::wstring corePluginPath = readRegistryValue(L"Software\\VapourSynth", L"CorePlugins" + bits);
    if (!loadAllPluginsInPath(corePluginPath, filter))
        vsCritical("Core plugin autoloading failed. Installation is broken?");

    // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
    // and accidentally block updated bundled versions
    std::wstring globalPluginPath = readRegistryValue(L"Software\\VapourSynth", L"Plugins" + bits);
    loadAllPluginsInPath(globalPluginPath, filter);
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
    const char *error = vsapi.getError(settings);
    if (error) {
        vsWarning("%s\n", error);
    } else {
        int err;
        const char *tmp;

        tmp = vsapi.propGetData(settings, "UserPluginDir", 0, &err);
        std::string userPluginDir(tmp ? tmp : "");

        tmp = vsapi.propGetData(settings, "SystemPluginDir", 0, &err);
        std::string systemPluginDir(tmp ? tmp : VS_PATH_PLUGINDIR);

        tmp = vsapi.propGetData(settings, "AutoloadUserPluginDir", 0, &err);
        bool autoloadUserPluginDir = tmp ? std::string(tmp) == "true" : true;

        tmp = vsapi.propGetData(settings, "AutoloadSystemPluginDir", 0, &err);
        bool autoloadSystemPluginDir = tmp ? std::string(tmp) == "true" : true;

        if (autoloadUserPluginDir && !userPluginDir.empty()) {
            if (!loadAllPluginsInPath(userPluginDir, filter)) {
                vsWarning("Autoloading the user plugin dir '%s' failed. Directory doesn't exist?", userPluginDir.c_str());
            }
        }

        if (autoloadSystemPluginDir) {
            if (!loadAllPluginsInPath(systemPluginDir, filter)) {
                vsCritical("Autoloading the system plugin dir '%s' failed. Directory doesn't exist?", systemPluginDir.c_str());
            }
        }
    }

    vsapi.freeMap(settings);
#endif
}

void VSCore::freeCore() {
    if (coreFreed)
        vsFatal("Double free of core");
    coreFreed = true;
    // Release the extra filter instance that always keeps the core alive
    filterInstanceDestroyed();
}

VSCore::~VSCore() {
    memory->signalFree();
    delete threadPool;
    for(const auto &iter : plugins)
        delete iter.second;
    plugins.clear();
    for (const auto &iter : formats)
        delete iter.second;
    formats.clear();
}

VSMap VSCore::getPlugins() {
    VSMap m;
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    int num = 0;
    for (const auto &iter : plugins) {
        std::string b = iter.second->fnamespace + ";" + iter.second->id + ";" + iter.second->fullname;
        vsapi.propSetData(&m, ("Plugin" + std::to_string(++num)).c_str(), b.c_str(), static_cast<int>(b.size()), paReplace);
    }
    return m;
}

VSPlugin *VSCore::getPluginById(const std::string &identifier) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    auto p = plugins.find(identifier);
    if (p != plugins.end())
        return p->second;
    return nullptr;
}

VSPlugin *VSCore::getPluginByNs(const std::string &ns) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    for (const auto &iter : plugins) {
        if (iter.second->fnamespace == ns)
            return iter.second;
    }
    return nullptr;
}

void VSCore::loadPlugin(const std::string &filename, const std::string &forcedNamespace, const std::string &forcedId) {
    VSPlugin *p = new VSPlugin(filename, forcedNamespace, forcedId, this);

    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    if (getPluginById(p->id)) {
        std::string error = "Plugin " + filename + " already loaded (" + p->id + ")";
        delete p;
        throw VSException(error);
    }

    if (getPluginByNs(p->fnamespace)) {
        std::string error = "Plugin load failed, namespace " + p->fnamespace + " already populated (" + filename + ")";
        delete p;
        throw VSException(error);
    }

    plugins.insert(std::make_pair(p->id, p));
}

void VSCore::createFilter(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        PVideoNode node(std::make_shared<VSNode>(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiMajor, this));
        for (size_t i = 0; i < node->getNumOutputs(); i++) {
            // fixme, not that elegant but saves more variant poking code
            VSNodeRef *ref = new VSNodeRef(node, static_cast<int>(i));
            vsapi.propSetNode(out, "clip", ref, paAppend);
            delete ref;
        }
    } catch (VSException &e) {
        vsapi.setError(out, e.what());
    }
}

VSPlugin::VSPlugin(VSCore *core)
    : apiMajor(0), apiMinor(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), core(core) {
}

VSPlugin::VSPlugin(const std::string &relFilename, const std::string &forcedNamespace, const std::string &forcedId, VSCore *core)
    : apiMajor(0), apiMinor(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), core(core), fnamespace(forcedNamespace), id(forcedId) {
#ifdef VS_TARGET_OS_WINDOWS
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    std::wstring wPath = conversion.from_bytes(relFilename);
    std::vector<wchar_t> fullPathBuffer(32767 + 1); // add 1 since msdn sucks at mentioning whether or not it includes the final null
    if (wPath.substr(0, 4) != L"\\\\?\\")
        wPath = L"\\\\?\\" + wPath;
    GetFullPathName(wPath.c_str(), static_cast<DWORD>(fullPathBuffer.size()), fullPathBuffer.data(), nullptr);
    wPath = fullPathBuffer.data();
    if (wPath.substr(0, 4) == L"\\\\?\\")
        wPath = wPath.substr(4);
    filename = conversion.to_bytes(wPath);
    for (auto &iter : filename)
        if (iter == '\\')
            iter = '/';

    libHandle = LoadLibraryEx(wPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);

    if (!libHandle)
        throw VSException("Failed to load " + relFilename);

    VSInitPlugin pluginInit = (VSInitPlugin)GetProcAddress(libHandle, "VapourSynthPluginInit");

    if (!pluginInit)
        pluginInit = (VSInitPlugin)GetProcAddress(libHandle, "_VapourSynthPluginInit@12");

    if (!pluginInit) {
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
            throw VSException("Failed to load " + relFilename + ". Error given: " + std::string(dlError));
        else
            throw VSException("Failed to load " + relFilename);
    }

    VSInitPlugin pluginInit = (VSInitPlugin)dlsym(libHandle, "VapourSynthPluginInit");

    if (!pluginInit) {
        dlclose(libHandle);
        throw VSException("No entry point found in " + relFilename);
    }


#endif
    pluginInit(&::configPlugin, &::registerFunction, this);

#ifdef VS_TARGET_CPU_X86
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected after loading %s", fullname.c_str());
#endif
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after loading %s", fullname.c_str());
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after loading %s", fullname.c_str());
#endif

    if (readOnlySet)
        readOnly = true;

    if (apiMajor != VAPOURSYNTH_API_MAJOR || apiMinor > VAPOURSYNTH_API_MINOR) {
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
    if (libHandle)
        FreeLibrary(libHandle);
#else
    if (libHandle)
        dlclose(libHandle);
#endif
}

void VSPlugin::configPlugin(const std::string &identifier, const std::string &defaultNamespace, const std::string &fullname, int apiVersion, bool readOnly) {
    if (hasConfig)
        vsFatal("Attempted to configure plugin %s twice", identifier.c_str());

    if (id.empty())
        id = identifier;

    if (fnamespace.empty())
        fnamespace = defaultNamespace;

    this->fullname = fullname;

    apiMajor = apiVersion;
    if (apiMajor >= 0x10000) {
        apiMinor = (apiMajor & 0xFFFF);
        apiMajor >>= 16;
    }

    readOnlySet = readOnly;
    hasConfig = true;
}

void VSPlugin::registerFunction(const std::string &name, const std::string &args, VSPublicFunction argsFunc, void *functionData) {
    if (readOnly)
        vsFatal("Tried to modify read only namespace");

    if (!isValidIdentifier(name))
        vsFatal("Illegal identifier specified for function");

    if (funcs.count(name))
        vsFatal("Duplicate function registered");

    if (!readOnly) {
        std::lock_guard<std::mutex> lock(registerFunctionLock);
        funcs.insert(std::make_pair(name, VSFunction(args, argsFunc, functionData)));
    } else {
        funcs.insert(std::make_pair(name, VSFunction(args, argsFunc, functionData)));
    }
}

static bool hasCompatNodes(const VSMap &m) {
    for (const auto &vsv : m.getStorage()) {
        if (vsv.second.getType() == VSVariant::vNode) {
            for (size_t i = 0; i < vsv.second.size(); i++) {
                for (size_t j = 0; j < vsv.second.getValue<VSNodeRef>(i).clip->getNumOutputs(); j++) {
                    const VSNodeRef &ref = vsv.second.getValue<VSNodeRef>(i);
                    const VSVideoInfo &vi = ref.clip->getVideoInfo(static_cast<int>(j));
                    if (vi.format && vi.format->colorFamily == cmCompat)
                        return true;
                }
            }
        }
    }
    return false;
}

static bool hasForeignNodes(const VSMap &m, const VSCore *core) {
    for (const auto &vsv : m.getStorage()) {
        if (vsv.second.getType() == VSVariant::vNode) {
            for (size_t i = 0; i < vsv.second.size(); i++) {
                for (size_t j = 0; j < vsv.second.getValue<VSNodeRef>(i).clip->getNumOutputs(); j++) {
                    const VSNodeRef &ref = vsv.second.getValue<VSNodeRef>(i);
                    if (!ref.clip->isRightCore(core))
                        return true;
                }
            }
        }
    }
    return false;
}

VSMap VSPlugin::invoke(const std::string &funcName, const VSMap &args) {
    const char lookup[] = { 'i', 'f', 's', 'c', 'v', 'm' };
    VSMap v;

    try {
        if (funcs.count(funcName)) {
            const VSFunction &f = funcs[funcName];
            if (!compat && hasCompatNodes(args))
                throw VSException(funcName + ": only special filters may accept compat input");
            if (hasForeignNodes(args, core))
                throw VSException(funcName + ": nodes foreign to this core passed as input, improper api usage detected");

            std::set<std::string> remainingArgs;
            for (const auto &key : args.getStorage())
                remainingArgs.insert(key.first);

            for (const FilterArgument &fa : f.args) {
                char c = vsapi.propGetType(&args, fa.name.c_str());

                if (c != 'u') {
                    remainingArgs.erase(fa.name);

                    if (lookup[(int)fa.type] != c)
                        throw VSException(funcName + ": argument " + fa.name + " is not of the correct type");

                    if (!fa.arr && args[fa.name.c_str()].size() > 1)
                        throw VSException(funcName + ": argument " + fa.name + " is not of array type but more than one value was supplied");

                    if (!fa.empty && args[fa.name.c_str()].size() < 1)
                        throw VSException(funcName + ": argument " + fa.name + " does not accept empty arrays");

                } else if (!fa.opt) {
                    throw VSException(funcName + ": argument " + fa.name + " is required");
                }
            }

            if (!remainingArgs.empty()) {
                auto iter = remainingArgs.cbegin();
                std::string s = *iter;
                ++iter;
                for (; iter != remainingArgs.cend(); ++iter)
                    s += ", " + *iter;
                throw VSException(funcName + ": no argument(s) named " + s);
            }

            f.func(&args, &v, f.functionData, core, getVSAPIInternal(apiMajor));

            if (!compat && hasCompatNodes(v))
                vsFatal("%s: illegal filter node returning a compat format detected, DO NOT USE THE COMPAT FORMATS IN NEW FILTERS", funcName.c_str());

            return v;
        }
    } catch (VSException &e) {
        vsapi.setError(&v, e.what());
        return v;
    }

    vsapi.setError(&v, ("Function '" + funcName + "' not found in " + fullname).c_str());
    return v;
}

VSMap VSPlugin::getFunctions() {
    VSMap m;
    for (const auto & f : funcs) {
        std::string b = f.first + ";" + f.second.argString;
        vsapi.propSetData(&m, f.first.c_str(), b.c_str(), static_cast<int>(b.size()), paReplace);
    }
    return m;
}

