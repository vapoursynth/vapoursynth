/*
* Copyright (c) 2012-2017 Fredrik Mellbin
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
#include "cpufeatures.h"
#ifndef VS_TARGET_OS_WINDOWS
#include <dirent.h>
#include <cstddef>
#include <unistd.h>
#include "settings.h"
#endif
#include <cassert>
#include <queue>

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

#ifdef VS_TARGET_OS_DARWIN
#define thread_local __thread
#endif

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

FrameContext::FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext) :
    reqOrder(upstreamContext->reqOrder), numFrameRequests(0), n(n), clip(clip), upstreamContext(upstreamContext), userData(nullptr), frameDone(nullptr), error(false), lockOnOutput(true), node(nullptr), lastCompletedN(-1), index(index), lastCompletedNode(nullptr), frameContext(nullptr) {
}

FrameContext::FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData, bool lockOnOutput) :
    reqOrder(0), numFrameRequests(0), n(n), clip(node->clip.get()), userData(userData), frameDone(frameDone), error(false), lockOnOutput(lockOnOutput), node(node), lastCompletedN(-1), index(index), lastCompletedNode(nullptr), frameContext(nullptr) {
}

bool FrameContext::setError(const std::string &errorMsg) {
    bool prevState = error;
    error = true;
    if (!prevState)
        errorMessage = errorMsg;
    return prevState;
}

///////////////

ExtFunction::ExtFunction(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const VSAPI *vsapi) : func(func), userData(userData), free(free), core(core), vsapi(vsapi) {
    core->functionInstanceCreated();
}

ExtFunction::~ExtFunction() {
    if (free)
        free(userData);
    core->functionInstanceDestroyed();
}

void ExtFunction::call(const VSMap *in, VSMap *out) {
    func(in, out, userData, core, vsapi);
}

///////////////

VSVariant::VSVariant(VSVType vtype) : vtype(vtype), internalSize(0), storage(nullptr) {
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
        default:;
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
        default:;
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
        default:;
        }
    }
}

///////////////

static bool isWindowsLargePageBroken() {
    // A Windows bug exists where a VirtualAlloc call immediately after VirtualFree
    // yields a page that has not been zeroed. The returned page is asynchronously
    // zeroed a few milliseconds later, resulting in memory corruption. The same bug
    // allows VirtualFree to return before the page has been unmapped.
    static const bool broken = []() -> bool {
#ifdef VS_TARGET_OS_WINDOWS
        size_t size = GetLargePageMinimum();

        for (int i = 0; i < 100; ++i) {
            void *ptr = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
            if (!ptr)
                return true;

            for (size_t n = 0; n < 64; ++n) {
                if (static_cast<uint8_t *>(ptr)[n]) {
                    vsWarning("Windows 10 VirtualAlloc bug detected: update to version 1803+");
                    return true;
                }
            }
            memset(ptr, 0xFF, 64);

            if (VirtualFree(ptr, 0, MEM_RELEASE) != TRUE)
                return true;
            if (!IsBadReadPtr(ptr, 1)) {
                vsWarning("Windows 10 VirtualAlloc bug detected: update to version 1803+");
                return true;
            }
        }
#endif // VS_TARGET_OS_WINDOWS
        return false;
    }();
    return broken;
}

/* static */ bool MemoryUse::largePageSupported() {
    // Disable large pages on 32-bit to avoid memory fragmentation.
    if (sizeof(void *) < 8)
        return false;

    static const bool supported = []() -> bool {
#ifdef VS_TARGET_OS_WINDOWS
        HANDLE token = INVALID_HANDLE_VALUE;
        TOKEN_PRIVILEGES priv = {};

        if (!(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)))
            return false;

        if (!(LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &priv.Privileges[0].Luid))) {
            CloseHandle(token);
            return false;
        }

        priv.PrivilegeCount = 1;
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!(AdjustTokenPrivileges(token, FALSE, &priv, 0, nullptr, 0))) {
            CloseHandle(token);
            return false;
        }

        CloseHandle(token);
        return true;
#else
        return false;
#endif // VS_TARGET_OS_WINDOWS
    }();
    return supported;
}

/* static */ size_t MemoryUse::largePageSize() {
    static const size_t size = []() -> size_t {
#ifdef VS_TARGET_OS_WINDOWS
        return GetLargePageMinimum();
#else
        return 2 * (1UL << 20);
#endif
    }();
    return size;
}

void *MemoryUse::allocateLargePage(size_t bytes) const {
    if (!largePageEnabled)
        return nullptr;

    size_t granularity = largePageSize();
    size_t allocBytes = VSFrame::alignment + bytes;
    allocBytes = (allocBytes + (granularity - 1)) & ~(granularity - 1);
    assert(allocBytes % granularity == 0);

    // Don't allocate a large page if it would conflict with the buffer recycling logic.
    if (!isGoodFit(bytes, allocBytes - VSFrame::alignment))
        return nullptr;

    void *ptr = nullptr;
#ifdef VS_TARGET_OS_WINDOWS
    ptr = VirtualAlloc(nullptr, allocBytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
#else
    ptr = vs_aligned_malloc(allocBytes, VSFrame::alignment);
#endif
    if (!ptr)
        return nullptr;

    BlockHeader *header = new (ptr) BlockHeader;
    header->size = allocBytes - VSFrame::alignment;
    header->large = true;
    return ptr;
}

void MemoryUse::freeLargePage(void *ptr) const {
#ifdef VS_TARGET_OS_WINDOWS
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    vs_aligned_free(ptr);
#endif
}

void *MemoryUse::allocateMemory(size_t bytes) const {
    void *ptr = allocateLargePage(bytes);
    if (ptr)
        return ptr;

    ptr = vs_aligned_malloc(VSFrame::alignment + bytes, VSFrame::alignment);
    if (!ptr)
        vsFatal("out of memory: %zu", bytes);

    BlockHeader *header = new (ptr) BlockHeader;
    header->size = bytes;
    header->large = false;
    return ptr;
}

void MemoryUse::freeMemory(void *ptr) const {
    const BlockHeader *header = static_cast<const BlockHeader *>(ptr);
    if (header->large)
        freeLargePage(ptr);
    else
        vs_aligned_free(ptr);
}

bool MemoryUse::isGoodFit(size_t requested, size_t actual) const {
    return actual <= requested + requested / 8;
}

void MemoryUse::add(size_t bytes) {
    used.fetch_add(bytes);
}

void MemoryUse::subtract(size_t bytes) {
    used.fetch_sub(bytes);
    if (freeOnZero && !used)
        delete this;
}

uint8_t *MemoryUse::allocBuffer(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    auto iter = buffers.lower_bound(bytes);
    if (iter != buffers.end()) {
        if (isGoodFit(bytes, iter->first)) {
            unusedBufferSize -= iter->first;
            uint8_t *buf = iter->second;
            buffers.erase(iter);
            return buf + VSFrame::alignment;
        }
    }

    uint8_t *buf = static_cast<uint8_t *>(allocateMemory(bytes));
    return buf + VSFrame::alignment;
}

void MemoryUse::freeBuffer(uint8_t *buf) {
    assert(buf);

    std::lock_guard<std::mutex> lock(mutex);
    buf -= VSFrame::alignment;

    const BlockHeader *header = reinterpret_cast<const BlockHeader *>(buf);
    if (!header->size)
        vsFatal("Memory corruption detected. Windows bug?");

    buffers.emplace(std::make_pair(header->size, buf));
    unusedBufferSize += header->size;

    size_t memoryUsed = used;
    while (memoryUsed + unusedBufferSize > maxMemoryUse && !buffers.empty()) {
        if (!memoryWarningIssued) {
            vsWarning("Script exceeded memory limit. Consider raising cache size.");
            memoryWarningIssued = true;
        }
        std::uniform_int_distribution<size_t> randSrc(0, buffers.size() - 1);
        auto iter = buffers.begin();
        std::advance(iter, randSrc(generator));
        assert(unusedBufferSize >= iter->first);
        unusedBufferSize -= iter->first;
        freeMemory(iter->second);
        buffers.erase(iter);
    }
}

size_t MemoryUse::memoryUse() {
    return used;
}

size_t MemoryUse::getLimit() {
    std::lock_guard<std::mutex> lock(mutex);
    return maxMemoryUse;
}

int64_t MemoryUse::setMaxMemoryUse(int64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    if (bytes > 0 && static_cast<uint64_t>(bytes) <= SIZE_MAX)
        maxMemoryUse = static_cast<size_t>(bytes);
    return maxMemoryUse;
}

bool MemoryUse::isOverLimit() {
    return used > maxMemoryUse;
}

void MemoryUse::signalFree() {
    freeOnZero = true;
    if (!used)
        delete this;
}

MemoryUse::MemoryUse() : used(0), freeOnZero(false), largePageEnabled(largePageSupported()), memoryWarningIssued(false), unusedBufferSize(0) {
    assert(VSFrame::alignment >= sizeof(BlockHeader));

    // If the Windows VirtualAlloc bug is present, it is not safe to use large pages by default,
    // because another application could trigger the bug.
    //if (isWindowsLargePageBroken())
    //    largePageEnabled = false;

    // Always disable large pages at the moment
    largePageEnabled = false;

    // 1GB
    setMaxMemoryUse(1024 * 1024 * 1024);

    // set 4GB as default on systems with (probably) 64bit address space
    if (sizeof(void *) >= 8)
        setMaxMemoryUse(static_cast<int64_t>(4) * 1024 * 1024 * 1024);
}

MemoryUse::~MemoryUse() {
    for (auto &iter : buffers)
        freeMemory(iter.second);
}

///////////////

VSPlaneData::VSPlaneData(size_t dataSize, MemoryUse &mem) : refCount(1), mem(mem), size(dataSize + 2 * VSFrame::guardSpace) {
#ifdef VS_FRAME_POOL
    data = mem.allocBuffer(size + 2 * VSFrame::guardSpace);
#else
    data = vs_aligned_malloc<uint8_t>(size + 2 * VSFrame::guardSpace, VSFrame::alignment);
#endif
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for planes. Out of memory.");
    mem.add(size);
#ifdef VS_FRAME_GUARD
    for (size_t i = 0; i < VSFrame::guardSpace / sizeof(VS_FRAME_GUARD_PATTERN); i++) {
        reinterpret_cast<uint32_t *>(data)[i] = VS_FRAME_GUARD_PATTERN;
        reinterpret_cast<uint32_t *>(data + size - VSFrame::guardSpace)[i] = VS_FRAME_GUARD_PATTERN;
    }
#endif
}

VSPlaneData::VSPlaneData(const VSPlaneData &d) : refCount(1), mem(d.mem), size(d.size) {
#ifdef VS_FRAME_POOL
    data = mem.allocBuffer(size);
#else
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
#endif
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for plane in copy constructor. Out of memory.");
    mem.add(size);
    memcpy(data, d.data, size);
}

VSPlaneData::~VSPlaneData() {
#ifdef VS_FRAME_POOL
    mem.freeBuffer(data);
#else
    vs_aligned_free(data);
#endif
    mem.subtract(size);
}

bool VSPlaneData::unique() {
    return (refCount == 1);
}

void VSPlaneData::addRef() {
    ++refCount;
}

void VSPlaneData::release() {
    if (!--refCount)
        delete this;
}

///////////////

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc, VSCore *core) : format(f), data(), width(width), height(height) {
    if (!f)
        vsFatal("Error in frame creation: null format");

    if (width <= 0 || height <= 0)
        vsFatal("Error in frame creation: dimensions are negative (%dx%d)", width, height);

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

    data[0] = new VSPlaneData(stride[0] * height, *core->memory);
    if (f->numPlanes == 3) {
        int size23 = stride[1] * (height >> f->subSamplingH);
        data[1] = new VSPlaneData(size23, *core->memory);
        data[2] = new VSPlaneData(size23, *core->memory);
    }
}

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core) : format(f), data(), width(width), height(height) {
    if (!f)
        vsFatal("Error in frame creation: null format");

    if (width <= 0 || height <= 0)
        vsFatal("Error in frame creation: dimensions are negative (%dx%d)", width, height);

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
                vsFatal("Error in frame creation: plane %d does not exist in the source frame", plane[i]);
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                vsFatal("Error in frame creation: dimensions of plane %d do not match. Source: %dx%d; destination: %dx%d", plane[i], planeSrc[i]->getWidth(plane[i]), planeSrc[i]->getHeight(plane[i]), getWidth(i), getHeight(i));
            data[i] = planeSrc[i]->data[plane[i]];
            data[i]->addRef();
        } else {
            if (i == 0) {
                data[i] = new VSPlaneData(stride[i] * height, *core->memory);
            } else {
                data[i] = new VSPlaneData(stride[i] * (height >> f->subSamplingH), *core->memory);
            }
        }
    }
}

VSFrame::VSFrame(const VSFrame &f) {
    data[0] = f.data[0];
    data[1] = f.data[1];
    data[2] = f.data[2];
    data[0]->addRef();
    if (data[1]) {
        data[1]->addRef();
        data[2]->addRef();
    }
    format = f.format;
    width = f.width;
    height = f.height;
    stride[0] = f.stride[0];
    stride[1] = f.stride[1];
    stride[2] = f.stride[2];
    properties = f.properties;
}

VSFrame::~VSFrame() {
    data[0]->release();
    if (data[1]) {
        data[1]->release();
        data[2]->release();
    }
}

int VSFrame::getStride(int plane) const {
    assert(plane >= 0 && plane < 3);
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Requested stride of nonexistent plane %d", plane);
    return stride[plane];
}

const uint8_t *VSFrame::getReadPtr(int plane) const {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Requested read pointer for nonexistent plane %d", plane);

    return data[plane]->data + guardSpace;
}

uint8_t *VSFrame::getWritePtr(int plane) {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Requested write pointer for nonexistent plane %d", plane);

    // copy the plane data if this isn't the only reference
    if (!data[plane]->unique()) {
        VSPlaneData *old = data[plane];
        data[plane] = new VSPlaneData(*data[plane]);
        old->release();
    }

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
    std::vector<std::string> argList;
    split(argList, argString, std::string(";"), split1::no_empties);
    for(const std::string &arg : argList) {
        std::vector<std::string> argParts;
        split(argParts, arg, std::string(":"), split1::no_empties);

        if (argParts.size() < 2)
            vsFatal("Invalid argument specifier '%s'. It appears to be incomplete.", arg.c_str());

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
                vsFatal("Argument '%s' has invalid type '%s'.", argName.c_str(), typeName.c_str());
        }

        bool opt = false;
        bool empty = false;

        for (size_t i = 2; i < argParts.size(); i++) {
            if (argParts[i] == "opt") {
                if (opt)
                    vsFatal("Argument '%s' has duplicate argument specifier '%s'", argName.c_str(), argParts[i].c_str());
                opt = true;
            } else if (argParts[i] == "empty") {
                if (empty)
                    vsFatal("Argument '%s' has duplicate argument specifier '%s'", argName.c_str(), argParts[i].c_str());
                empty = true;
            }  else {
                vsFatal("Argument '%s' has unknown argument modifier '%s'", argName.c_str(), argParts[i].c_str());
            }
        }

        if (!isValidIdentifier(argName))
            vsFatal("Argument name '%s' contains illegal characters.", argName.c_str());

        if (empty && !arr)
            vsFatal("Argument '%s' is not an array. Only array arguments can have the empty flag set.", argName.c_str());

        args.push_back(FilterArgument(argName, type, arr, empty, opt));
    }
}

VSNode::VSNode(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core) :
instanceData(instanceData), name(name), init(init), filterGetFrame(getFrame), free(free), filterMode(filterMode), apiMajor(apiMajor), core(core), flags(flags), hasVi(false), serialFrame(-1) {

    if (flags & ~(nfNoCache | nfIsCache | nfMakeLinear))
        throw VSException("Filter " + name  + " specified unknown flags");

    if ((flags & nfIsCache) && !(flags & nfNoCache))
        throw VSException("Filter " + name + " specified an illegal combination of flags (nfNoCache must always be set with nfIsCache)");

    core->filterInstanceCreated();
    VSMap inval(*in);
    init(&inval, out, &this->instanceData, this, core, getVSAPIInternal(apiMajor));

    if (out->hasError()) {
        core->filterInstanceDestroyed();
        throw VSException(vs_internal_vsapi.getError(out));
    }

    if (!hasVi) {
        core->filterInstanceDestroyed();
        throw VSException("Filter " + name + " didn't set vi");
    }

    for (const auto &iter : vi) {
        if (iter.numFrames <= 0) {
            core->filterInstanceDestroyed();
            throw VSException("Filter " + name + " returned zero or negative frame count");
        }
    }
}

VSNode::~VSNode() {
    core->destroyFilterInstance(this);
}

void VSNode::getFrame(const PFrameContext &ct) {
    core->threadPool->start(ct);
}

const VSVideoInfo &VSNode::getVideoInfo(int index) {
    if (index < 0 || index >= static_cast<int>(vi.size()))
        vsFatal("getVideoInfo: Out of bounds videoinfo index %d. Valid range: [0,%d].", index, static_cast<int>(vi.size() - 1));
    return vi[index];
}

void VSNode::setVideoInfo(const VSVideoInfo *vi, int numOutputs) {
    if (numOutputs < 1)
        vsFatal("setVideoInfo: Video filter %s needs to have at least one output (%d were given).", name.c_str(), numOutputs);
    for (int i = 0; i < numOutputs; i++) {
        if ((!!vi[i].height) ^ (!!vi[i].width))
            vsFatal("setVideoInfo: Variable dimension clips must have both width and height set to 0. Dimensions given by filter %s: %dx%d.", name.c_str(), vi[i].width, vi[i].height);
        if (vi[i].format && !core->isValidFormatPointer(vi[i].format))
            vsFatal("setVideoInfo: The VSFormat pointer passed by %s was not obtained from registerFormat() or getFormatPreset().", name.c_str());
        int64_t num = vi[i].fpsNum;
        int64_t den = vi[i].fpsDen;
        vs_normalizeRational(&num, &den);
        if (num != vi[i].fpsNum || den != vi[i].fpsDen)
            vsFatal(("setVideoInfo: The frame rate specified by " + name + " must be a reduced fraction. (Instead, it is " + std::to_string(vi[i].fpsNum) + "/" + std::to_string(vi[i].fpsDen) + ".)").c_str());

        this->vi.push_back(vi[i]);
        this->vi[i].flags = flags;
    }
    hasVi = true;
}

PVideoFrame VSNode::getFrameInternal(int n, int activationReason, VSFrameContext &frameCtx) {
    const VSFrameRef *r = filterGetFrame(n, activationReason, &instanceData, &frameCtx.ctx->frameContext, &frameCtx, core, &vs_internal_vsapi);

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after return from %s", name.c_str());
#endif

    if (r) {
        PVideoFrame p(std::move(r->frame));
        delete r;
        const VSFormat *fi = p->getFormat();
        const VSVideoInfo &lvi = vi[frameCtx.ctx->index];

        if (!lvi.format && fi->colorFamily == cmCompat)
            vsFatal("Illegal compat frame returned by %s.", name.c_str());
        else if (lvi.format && lvi.format != fi)
            vsFatal("Filter %s declared the format %s (id %d), but it returned a frame with the format %s (id %d).", name.c_str(), lvi.format->name, lvi.format->id, fi->name, fi->id);
        else if ((lvi.width || lvi.height) && (p->getWidth(0) != lvi.width || p->getHeight(0) != lvi.height))
            vsFatal("Filter %s declared the size %dx%d, but it returned a frame with the size %dx%d.", name.c_str(), lvi.width, lvi.height, p->getWidth(0), p->getHeight(0));

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
            snprintf(f->name, sizeof(f->name), "Gray%s%d", sampleTypeStr, bitsPerSample);
            break;
        case cmRGB:
            snprintf(f->name, sizeof(f->name), "RGB%s%d", sampleTypeStr, bitsPerSample * 3);
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
                snprintf(f->name, sizeof(f->name), "YUV%sP%s%d", yuvName, sampleTypeStr, bitsPerSample);
            else
                snprintf(f->name, sizeof(f->name), "YUVssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
            break;
        case cmYCoCg:
            snprintf(f->name, sizeof(f->name), "YCoCgssw%dssh%dP%s%d", subSamplingW, subSamplingH, sampleTypeStr, bitsPerSample);
            break;
        default:;
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
    getCoreInfo2(coreInfo);
    return coreInfo;
}

void VSCore::getCoreInfo2(VSCoreInfo &info) {
    info.versionString = VAPOURSYNTH_VERSION_STRING;
    info.core = VAPOURSYNTH_CORE_VERSION;
    info.api = VAPOURSYNTH_API_VERSION;
    info.numThreads = threadPool->threadCount();
    info.maxFramebufferSize = memory->getLimit();
    info.usedFramebufferSize = memory->memoryUse();
}

void VS_CC vs_internal_configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin);
void VS_CC vs_internal_registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);

static void VS_CC loadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
        int err;
        const char *forcens = vsapi->propGetData(in, "forcens", 0, &err);
        if (!forcens)
            forcens = "";
        const char *forceid = vsapi->propGetData(in, "forceid", 0, &err);
        if (!forceid)
            forceid = "";
        bool altSearchPath = !!vsapi->propGetInt(in, "altsearchpath", 0, &err);
        core->loadPlugin(vsapi->propGetData(in, "path", 0, nullptr), forcens, forceid, altSearchPath);
    } catch (VSException &e) {
        vsapi->setError(out, e.what());
    }
}

void VS_CC loadPluginInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("LoadPlugin", "path:data;altsearchpath:int:opt;forcens:data:opt;forceid:data:opt;", &loadPlugin, nullptr, plugin);
}

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

    registerFormat(cmYUV,  stInteger, 12, 1, 1, "YUV420P12", pfYUV420P12);
    registerFormat(cmYUV,  stInteger, 12, 1, 0, "YUV422P12", pfYUV422P12);
    registerFormat(cmYUV,  stInteger, 12, 0, 0, "YUV444P12", pfYUV444P12);

    registerFormat(cmYUV,  stInteger, 14, 1, 1, "YUV420P14", pfYUV420P14);
    registerFormat(cmYUV,  stInteger, 14, 1, 0, "YUV422P14", pfYUV422P14);
    registerFormat(cmYUV,  stInteger, 14, 0, 0, "YUV444P14", pfYUV444P14);

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

    registerFormat(cmCompat, stInteger, 32, 0, 0, "CompatBGR32", pfCompatBGR32);
    registerFormat(cmCompat, stInteger, 16, 1, 0, "CompatYUY2", pfCompatYUY2);
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

struct VSCoreShittyList {
    VSFilterFree free;
    void *instanceData;
    VSCoreShittyList *next;
    // add stuff like vsapi here if multiple versions end up floating around for compatibility
};

void VSCore::destroyFilterInstance(VSNode *node) {
    static thread_local int freeDepth = 0;
    static thread_local VSCoreShittyList *nodeFreeList = nullptr;
    freeDepth++;

    if (node->free) {
        nodeFreeList = new VSCoreShittyList({ node->free, node->instanceData, nodeFreeList });
    } else {
        filterInstanceDestroyed();
    }

    if (freeDepth == 1) {
        while (nodeFreeList) {
            VSCoreShittyList *current = nodeFreeList;
            nodeFreeList = current->next;
            current->free(current->instanceData, this, &vs_internal_vsapi);
            delete current;
            filterInstanceDestroyed();
        }
    }

    freeDepth--;
}

VSCore::VSCore(int threads) :
    coreFreed(false),
    numFilterInstances(1),
    numFunctionInstances(0),
    formatIdOffset(1000),
    cpuLevel(INT_MAX),
    memory(new MemoryUse()) {
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected when creating new core");
#endif

    threadPool = new VSThreadPool(this, threads);

    registerFormats();

    // The internal plugin units, the loading is a bit special so they can get special flags
    VSPlugin *p;

    // Initialize internal plugins
    p = new VSPlugin(this);
    ::vs_internal_configPlugin("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 0, p);
    loadPluginInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    cacheInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    exprInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    genericInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    lutInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    boxBlurInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    mergeInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    reorderInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    stdlibInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    p->enableCompat();
    p->lock();

    plugins.insert(std::make_pair(p->id, p));
    p = new VSPlugin(this);
    resizeInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    plugins.insert(std::make_pair(p->id, p));
    p->enableCompat();

    plugins.insert(std::make_pair(p->id, p));
    p = new VSPlugin(this);
    textInitialize(::vs_internal_configPlugin, ::vs_internal_registerFunction, p);
    plugins.insert(std::make_pair(p->id, p));
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
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&vs_internal_configPlugin, &module);
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
            vsCritical("Core plugin autoloading failed. Installation is broken?");

        // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
        // and accidentally block updated bundled versions
        std::wstring globalPluginPath = dllPath + L"vapoursynth" + bits + L"\\plugins";
        loadAllPluginsInPath(globalPluginPath, filter);
    } else {
        // Autoload user specific plugins first so a user can always override
        std::vector<wchar_t> appDataBuffer(MAX_PATH + 1);
        if (SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataBuffer.data()) != S_OK)
            SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_DEFAULT, appDataBuffer.data());

        std::wstring appDataPath = std::wstring(appDataBuffer.data()) + L"\\VapourSynth\\plugins" + bits;

        // Autoload per user plugins
        loadAllPluginsInPath(appDataPath, filter);

        // Autoload bundled plugins
        std::wstring corePluginPath = readRegistryValue(VS_INSTALL_REGKEY, L"CorePlugins");
        if (!loadAllPluginsInPath(corePluginPath, filter))
            vsCritical("Core plugin autoloading failed. Installation is broken?");

        // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
        // and accidentally block updated bundled versions
        std::wstring globalPluginPath = readRegistryValue(VS_INSTALL_REGKEY, L"Plugins");
        loadAllPluginsInPath(globalPluginPath, filter);
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
    const char *error = vs_internal_vsapi.getError(settings);
    if (error) {
        vsWarning("%s\n", error);
    } else {
        int err;
        const char *tmp;

        tmp = vs_internal_vsapi.propGetData(settings, "UserPluginDir", 0, &err);
        std::string userPluginDir(tmp ? tmp : "");

        tmp = vs_internal_vsapi.propGetData(settings, "SystemPluginDir", 0, &err);
        std::string systemPluginDir(tmp ? tmp : VS_PATH_PLUGINDIR);

        tmp = vs_internal_vsapi.propGetData(settings, "AutoloadUserPluginDir", 0, &err);
        bool autoloadUserPluginDir = tmp ? std::string(tmp) == "true" : true;

        tmp = vs_internal_vsapi.propGetData(settings, "AutoloadSystemPluginDir", 0, &err);
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

    vs_internal_vsapi.freeMap(settings);
#endif
}

void VSCore::freeCore() {
    if (coreFreed)
        vsFatal("Double free of core");
    coreFreed = true;
    threadPool->waitForDone();
    if (numFilterInstances > 1)
        vsWarning("Core freed but %d filter instance(s) still exist", numFilterInstances.load() - 1);
    if (memory->memoryUse() > 0)
        vsWarning("Core freed but %llu bytes still allocated in framebuffers", static_cast<unsigned long long>(memory->memoryUse()));
    if (numFunctionInstances > 0)
        vsWarning("Core freed but %d function instance(s) still exist", numFunctionInstances.load());
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
        vs_internal_vsapi.propSetData(&m, ("Plugin" + std::to_string(++num)).c_str(), b.c_str(), static_cast<int>(b.size()), paReplace);
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

void VSCore::loadPlugin(const std::string &filename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath) {
    VSPlugin *p = new VSPlugin(filename, forcedNamespace, forcedId, altSearchPath, this);

    std::lock_guard<std::recursive_mutex> lock(pluginLock);

    VSPlugin *already_loaded_plugin = getPluginById(p->id);
    if (already_loaded_plugin) {
        std::string error = "Plugin " + filename + " already loaded (" + p->id + ")";
        if (already_loaded_plugin->filename.size())
            error += " from " + already_loaded_plugin->filename;
        delete p;
        throw VSException(error);
    }

    already_loaded_plugin = getPluginByNs(p->fnamespace);
    if (already_loaded_plugin) {
        std::string error = "Plugin load of " + filename + " failed, namespace " + p->fnamespace + " already populated";
        if (already_loaded_plugin->filename.size())
            error += " by " + already_loaded_plugin->filename;
        delete p;
        throw VSException(error);
    }

    plugins.insert(std::make_pair(p->id, p));

    // allow avisynth plugins to accept legacy avisynth formats
    if (p->fnamespace == "avs" && p->id == "com.vapoursynth.avisynth")
        p->enableCompat();
}

void VSCore::createFilter(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor) {
    try {
        PVideoNode node(std::make_shared<VSNode>(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiMajor, this));
        for (size_t i = 0; i < node->getNumOutputs(); i++) {
            // fixme, not that elegant but saves more variant poking code
            VSNodeRef *ref = new VSNodeRef(node, static_cast<int>(i));
            vs_internal_vsapi.propSetNode(out, "clip", ref, paAppend);
            delete ref;
        }
    } catch (VSException &e) {
        vs_internal_vsapi.setError(out, e.what());
    }
}

int VSCore::getCpuLevel() const {
    return cpuLevel;
}

int VSCore::setCpuLevel(int cpu) {
    return cpuLevel.exchange(cpu);
}

VSPlugin::VSPlugin(VSCore *core)
    : apiMajor(0), apiMinor(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), core(core) {
}

VSPlugin::VSPlugin(const std::string &relFilename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath, VSCore *core)
    : apiMajor(0), apiMinor(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), core(core), fnamespace(forcedNamespace), id(forcedId) {
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

    if (!libHandle) {
        DWORD lastError = GetLastError();

        if (lastError == 126)
            throw VSException("Failed to load " + relFilename + ". GetLastError() returned " + std::to_string(lastError) + ". The file you tried to load or one of its dependencies is probably missing.");
        throw VSException("Failed to load " + relFilename + ". GetLastError() returned " + std::to_string(lastError) + ".");
    }

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
            throw VSException("Failed to load " + relFilename + ". Error given: " + dlError);
        else
            throw VSException("Failed to load " + relFilename);
    }

    VSInitPlugin pluginInit = (VSInitPlugin)dlsym(libHandle, "VapourSynthPluginInit");

    if (!pluginInit) {
        dlclose(libHandle);
        throw VSException("No entry point found in " + relFilename);
    }


#endif
    pluginInit(::vs_internal_configPlugin, ::vs_internal_registerFunction, this);

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after loading %s", filename.c_str());
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
        vsFatal("Plugin %s tried to modify read only namespace.", filename.c_str());

    if (!isValidIdentifier(name))
        vsFatal("Plugin %s tried to register '%s', an illegal identifier.", filename.c_str(), name.c_str());

    std::lock_guard<std::mutex> lock(registerFunctionLock);

    if (funcs.count(name)) {
        vsWarning("Plugin %s tried to register '%s' more than once. Second registration ignored.", filename.c_str(), name.c_str());
        return;
    }

    funcs.insert(std::make_pair(name, VSFunction(args, argsFunc, functionData)));
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
                char c = vs_internal_vsapi.propGetType(&args, fa.name.c_str());

                if (c != 'u') {
                    remainingArgs.erase(fa.name);

                    if (lookup[static_cast<int>(fa.type)] != c)
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
        vs_internal_vsapi.setError(&v, e.what());
        return v;
    }

    vs_internal_vsapi.setError(&v, ("Function '" + funcName + "' not found in " + id).c_str());
    return v;
}

VSMap VSPlugin::getFunctions() {
    VSMap m;
    for (const auto & f : funcs) {
        std::string b = f.first + ";" + f.second.argString;
        vs_internal_vsapi.propSetData(&m, f.first.c_str(), b.c_str(), static_cast<int>(b.size()), paReplace);
    }
    return m;
}

#ifdef VS_TARGET_CPU_X86
static int alignmentHelper() {
    return getCPUFeatures()->avx512_f ? 64 : 32;
}

int VSFrame::alignment = alignmentHelper();
#else
int VSFrame::alignment = 32;
#endif
