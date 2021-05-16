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

#ifndef VSCORE_H
#define VSCORE_H

#include "VapourSynth4.h"
#include "VapourSynth3.h"
#include "vslog.h"
#include "intrusive_ptr.h"
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cassert>
#include <vector>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <random>
#include <algorithm>
#include <tuple>
#include <chrono>

#ifdef VS_TARGET_OS_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#ifdef VS_USE_MIMALLOC
#   include <mimalloc-override.h>
#endif

#ifdef VS_FRAME_GUARD
static const uint32_t VS_FRAME_GUARD_PATTERN = 0xDEADBEEF;
#endif

#define VS_FATAL_ERROR(msg) do { fprintf(stderr, "%s\n", (msg)); std::terminate(); } while (false);


// Internal only filter mode for use by caches to make requests more linear
const int fmUnorderedLinear = fmFrameState + 1;

struct VSFrameRef;
struct VSCore;
class VSCache;
struct VSNode;
struct VSNodeRef;
class VSThreadPool;
struct VSFrameContext;
struct VSFunctionRef;
class VSMapData;

typedef vs_intrusive_ptr<VSFrameRef> PVSFrameRef;
typedef vs_intrusive_ptr<VSNode> PVSNode;
typedef vs_intrusive_ptr<VSNodeRef> PVSNodeRef;
typedef vs_intrusive_ptr<VSFunctionRef> PVSFunctionRef;
typedef vs_intrusive_ptr<VSFrameContext> PVSFrameContext;

extern const VSPLUGINAPI vs_internal_vspapi;
extern const VSAPI vs_internal_vsapi;
extern const vs3::VSAPI3 vs_internal_vsapi3;
const VSAPI *getVSAPIInternal(int apiMajor);

class VSException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

typedef std::tuple<VSNode *, int, int> NodeOutputKey;

template<>
struct std::hash<NodeOutputKey> {
    inline size_t operator()(const NodeOutputKey &val) const {  
        return reinterpret_cast<size_t>(std::get<0>(val)) + (static_cast<size_t>(std::get<1>(val)) << 16) + (static_cast<size_t>(std::get<2>(val)) << 24);
    }
};

struct VSFunctionRef {
private:
    std::atomic<long> refcount;
    VSPublicFunction func;
    void *userData;
    VSFreeFunctionData freeFunction;
    VSCore *core;
    int apiMajor;
    ~VSFunctionRef();
public:
    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        assert(refcount > 0);
        if (--refcount == 0)
            delete this;
    }

    VSFunctionRef(VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core, int apiMajor);
    void call(const VSMap *in, VSMap *out);
};

class VSArrayBase {
protected:
    std::atomic<long> refcount;
    VSPropType ftype;
    size_t fsize = 0;
    explicit VSArrayBase(VSPropType type) : refcount(1), ftype(type) {}
    virtual ~VSArrayBase() {}
public:
    VSPropType type() const {
        return ftype;
    }

    size_t size() const {
        return fsize;
    }

    bool unique() const noexcept {
        return (refcount == 1);
    }

    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        assert(refcount > 0);
        if (--refcount == 0)
            delete this;
    }

    virtual VSArrayBase *copy() const noexcept = 0;
};

typedef vs_intrusive_ptr<VSArrayBase> PVSArrayBase;

template<typename T, VSPropType propType>
class VSArray final : public VSArrayBase {
private:
    T singleData;
    std::vector<T> data;
public:
    explicit VSArray() noexcept : VSArrayBase(propType) {}

    explicit VSArray(const VSArray &other) noexcept : VSArrayBase(other.ftype) {
        fsize = other.fsize;
        if (fsize == 1)
            singleData = other.singleData;
        else if (fsize > 1)
            data = other.data;
    }

    explicit VSArray(const T *val, size_t count) noexcept : VSArrayBase(propType) { // only enable for POD types
        fsize = count;
        if (count == 1) {
            singleData = *val;
        } else {
            data.resize(count);
            memcpy(data.data(), val, sizeof(T) * count);
        }
    }

    virtual VSArrayBase *copy() const noexcept {
        return new VSArray(*this);
    }

    const T *getDataPointer() const noexcept { // only enable for POD types
        if (fsize == 1)
            return &singleData;
        else
            return data.data();
    }

    void push_back(const T &val) noexcept {
        if (fsize == 0) {
            singleData = val;
        } else if (fsize == 1) {
            data.reserve(8);
            data.push_back(std::move(singleData));
            data.push_back(val);
        } else {
            if (data.capacity() == data.size())
                data.reserve(data.capacity() * 2);
            data.push_back(val);
        }
        fsize++;
    }

    const T &at(size_t pos) const noexcept {
        assert(pos < fsize);
        if (fsize == 1)
            return singleData;
        else
            return data.at(pos);
    }
};

class VSMapData {
public:
    VSDataTypeHint typeHint;
    std::string data;
};


typedef VSArray<int64_t, ptInt> VSIntArray;
typedef VSArray<double, ptFloat> VSFloatArray;
typedef VSArray<VSMapData, ptData> VSDataArray;
typedef VSArray<PVSNodeRef, ptVideoNode> VSVideoNodeArray;
typedef VSArray<PVSNodeRef, ptAudioNode> VSAudioNodeArray;
typedef VSArray<PVSFrameRef, ptVideoFrame> VSVideoFrameArray;
typedef VSArray<PVSFrameRef, ptAudioFrame> VSAudioFrameArray;
typedef VSArray<PVSFunctionRef, ptFunction> VSFunctionArray;

class VSMapStorage {
private:
    std::atomic<long> refcount;
public:
    std::map<std::string, PVSArrayBase> data;
    bool error;

    explicit VSMapStorage() : refcount(1), error(false) {}

    explicit VSMapStorage(const VSMapStorage &s) : refcount(1), data(s.data), error(s.error) {
    }

    bool unique() noexcept {
        return (refcount == 1);
    };

    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        assert(refcount > 0);
        if (--refcount == 0)
            delete this;
    }
};

typedef vs_intrusive_ptr<VSMapStorage> PVSMapStorage;

struct VSMap {
private:
    PVSMapStorage data;
public:
    VSMap(const VSMap *map = nullptr) : data(map ? map->data : new VSMapStorage()) {
    }

    VSMap &operator=(const VSMap &map) {
        data = map.data;
        return *this;
    }

    bool detach() {
        if (!data->unique()) {
            data = new VSMapStorage(*data);
            return true;
        }
        return false;
    }

    VSArrayBase *find(const std::string &key) const {
        auto it = data->data.find(key);
        return (it == data->data.end()) ? nullptr : it->second.get();
    }

    VSArrayBase *detach(const std::string &key) {
        detach();
        auto it = data->data.find(key);
        if (it != data->data.end()) {
            if (!it->second->unique())
                it->second = it->second->copy();
            return it->second.get();
        }
        return nullptr;
    }

    bool erase(const std::string &key) {
        auto it = data->data.find(key);
        if (it != data->data.end()) {
            if (detach())
                it = data->data.find(key);
            data->data.erase(it);
            return true;
        }
        return false;
    }

    void insert(const std::string &key, VSArrayBase *val) {
        detach();
        auto it = data->data.find(key);
        if (it != data->data.end()) {
            it->second = val;
        } else {
            data->data.insert(std::make_pair(key, val));
        }
    }

    void copy(const VSMap *src) {
        detach();
        for (auto &iter : src->data->data)
            data->data[iter.first] = iter.second;
    }

    size_t size() const {
        return data->data.size();
    }

    void clear() {
        if (data->unique())
            data->data.clear();
        else
            data = new VSMapStorage();
    }

    const char *key(size_t n) const {
        if (n >= size())
            return nullptr;
        auto iter = data->data.cbegin();
        std::advance(iter, n);
        return iter->first.c_str();
    }

    void setError(const std::string &errMsg) {
        clear();
        VSDataArray *arr = new VSDataArray();
        arr->push_back({ dtUtf8, errMsg });
        data->data.insert(std::make_pair("_Error", arr));
        data->error = true;
    }

    bool hasError() const {
        return data->error;
    }

    const char *getErrorMessage() const {
        if (data->error) {
            return reinterpret_cast<VSDataArray *>(data->data.at("_Error").get())->at(0).data.c_str();
        } else {
            return nullptr;
        }
    }

    bool isV3Compatible() const noexcept;
    bool hasCompatNodes() const noexcept;
};

struct VSNodeRef {
private:
    std::atomic<long> refcount;
public:
    VSNode *clip;
    int index;
    VSNodeRef(VSNode *clip, int index) noexcept : refcount(1), clip(clip), index(index) {}
    ~VSNodeRef() {};

    void add_ref() noexcept;
    void release() noexcept;
};

class FilterArgument {
public:
    std::string name;
    VSPropType type;
    
    bool arr;
    bool empty;
    bool opt;
    FilterArgument(const std::string &name, VSPropType type, bool arr, bool empty, bool opt)
        : name(name), type(type), arr(arr), empty(empty), opt(opt) {}
};

class MemoryUse {
private:
    std::atomic<size_t> used;
    std::atomic<size_t> maxMemoryUse;
    bool freeOnZero;
public:
    void add(size_t bytes);
    void subtract(size_t bytes);
    size_t memoryUse();
    size_t getLimit();
    int64_t setMaxMemoryUse(int64_t bytes);
    bool isOverLimit();
    void signalFree();
    MemoryUse();
};

class VSPlaneData {
private:
    std::atomic<long> refcount;
    MemoryUse &mem;
    ~VSPlaneData();
public:
    uint8_t *data;
    const size_t size;
    VSPlaneData(size_t dataSize, MemoryUse &mem) noexcept;
    VSPlaneData(const VSPlaneData &d) noexcept;
    bool unique() noexcept;
    void add_ref() noexcept;
    void release() noexcept;
};

struct VSFrameRef {
private:
    std::atomic<long> refcount;
    VSMediaType contentType;
    union {
        VSVideoFormat vf;
        VSAudioFormat af;
    } format;
    mutable std::atomic<const vs3::VSVideoFormat *> v3format; /* API 3 compatibility */
    VSPlaneData *data[3] = {}; /* only the first data pointer is ever used for audio and is subdivided using the internal offset in height */
    int width; /* stores number of samples for audio */
    int height;
    ptrdiff_t stride[3] = {}; /* stride[0] stores internal offset between audio channels */
    int numPlanes;
    VSMap properties;
    VSCore *core;
public:
    static int alignment;

#ifdef VS_FRAME_GUARD
    static const int guardSpace = 64;
#else
    static const int guardSpace = 0;
#endif

    VSFrameRef(const VSVideoFormat &f, int width, int height, const VSFrameRef *propSrc, VSCore *core) noexcept;
    VSFrameRef(const VSVideoFormat &f, int width, int height, const VSFrameRef * const *planeSrc, const int *plane, const VSFrameRef *propSrc, VSCore *core) noexcept;
    VSFrameRef(const VSAudioFormat &f, int numSamples, const VSFrameRef *propSrc, VSCore *core) noexcept;
    VSFrameRef(const VSAudioFormat &f, int numSamples, const VSFrameRef * const *channelSrc, const int *channel, const VSFrameRef *propSrc, VSCore *core) noexcept;
    VSFrameRef(const VSFrameRef &f) noexcept;
    ~VSFrameRef();

    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        if (--refcount == 0)
            delete this;
    }

    VSMediaType getFrameType() const {
        return contentType;
    }

    VSMap &getProperties() {
        return properties;
    }
    const VSMap &getConstProperties() const {
        return properties;
    }
    void setProperties(const VSMap &properties) {
        this->properties = properties;
    }
    const VSVideoFormat *getVideoFormat() const {
        assert(contentType == mtVideo);
        return &format.vf;
    }
    const vs3::VSVideoFormat *getVideoFormatV3() const noexcept;

    int getWidth(int plane) const {
        assert(contentType == mtVideo);
        return width >> (plane ? format.vf.subSamplingW : 0);
    }
    int getHeight(int plane) const {
        assert(contentType == mtVideo);
        return height >> (plane ? format.vf.subSamplingH : 0);
    }
    const VSAudioFormat *getAudioFormat() const {
        assert(contentType == mtAudio);
        return &format.af;
    }
    int getSampleRate() const {
        assert(contentType == mtAudio);
        return height;
    }
    int getFrameLength() const {
        assert(contentType == mtAudio);
        return width;
    }
    ptrdiff_t getStride(int plane) const;
    const uint8_t *getReadPtr(int plane) const;
    uint8_t *getWritePtr(int plane);

#ifdef VS_FRAME_GUARD
    bool verifyGuardPattern();
#endif
};

#define NUM_FRAMECONTEXT_FAST_REQS 10

template<typename T, size_t staticSize>
class SemiStaticVector {
private:
    size_t numElems = 0;
    typename std::aligned_storage<sizeof(T), alignof(T)>::type staticData[staticSize];
    std::vector<T> dynamicData;
    void freeStatic() noexcept {
        for (size_t pos = 0; pos < std::min(numElems, staticSize); ++pos)
            reinterpret_cast<T *>(&staticData[pos])->~T();
    }
public:
    template<typename ...Args> void emplace_back(Args &&... args) noexcept {
        if (numElems < staticSize) {
            new(&staticData[numElems]) T(std::forward<Args>(args)...);
        } else {
            dynamicData.emplace_back(std::forward<Args>(args)...);
        }
        numElems++;
    }

    void push_back(const T &val) noexcept {
        if (numElems < staticSize) {
            new(&staticData[numElems]) T(val);
        } else {
            dynamicData.push_back(val);
        }
        numElems++;
    }

    void push_back(T &&val) noexcept {
        if (numElems < staticSize) {
            new(&staticData[numElems]) T(val);
        } else {
            dynamicData.push_back(val);
        }
        numElems++;
    }

    const T &operator[](size_t pos) const noexcept {
        assert(pos < numElems);
        if (pos < staticSize)
            return *reinterpret_cast<const T *>(&staticData[pos]);
        else
            return dynamicData[pos - staticSize];
    }

    T &operator[](size_t pos) noexcept {
        assert(pos < numElems);
        if (pos < staticSize)
            return *reinterpret_cast<T *>(&staticData[pos]);
        else
            return dynamicData[pos - staticSize];
    }

    size_t size() const noexcept {
        return numElems;
    }

    void clear() noexcept {
        freeStatic();
        dynamicData.clear();
        numElems = 0;
    }

    ~SemiStaticVector() {
        freeStatic();
    }
};

struct VSFrameContext {
    friend class VSThreadPool;
private:
    std::atomic<long> refcount;
    size_t reqOrder;
    size_t numFrameRequests = 0;
    int n;
    VSNode *clip;
    PVSFrameRef returnedFrame;
    PVSFrameContext upstreamContext;
    PVSFrameContext notificationChain;
    void *userData;
    VSFrameDoneCallback frameDone;
    std::string errorMessage;
    bool error = false;
    bool lockOnOutput;
public:
    SemiStaticVector<PVSFrameContext, NUM_FRAMECONTEXT_FAST_REQS> reqList;
    SemiStaticVector<std::pair<NodeOutputKey, PVSFrameRef>, NUM_FRAMECONTEXT_FAST_REQS> availableFrames;

    VSNodeRef *node;
    int index;
    void *frameContext[4];

    // only used for queryCompletedFrame
    int lastCompletedN = -1;
    VSNodeRef *lastCompletedNode = nullptr;
    //

    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        assert(refcount > 0);
        if (--refcount == 0)
            delete this;
    }

    bool hasError() const {
        return error;
    }

    const std::string &getErrorMessage() {
        return errorMessage;
    }

    bool setError(const std::string &errorMsg);
    VSFrameContext(int n, int index, VSNode *clip, const PVSFrameContext &upstreamContext);
    VSFrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData, bool lockOnOutput = true);
};

struct VSFunctionFrame;
typedef std::shared_ptr<VSFunctionFrame> PVSFunctionFrame;

struct VSFunctionFrame {
    std::string name;
    const VSMap *args;
    VSFunctionFrame(const std::string &name, const VSMap *args, PVSFunctionFrame next) : name(name), args(args), next(next) {};
    ~VSFunctionFrame() { delete args; }
    PVSFunctionFrame next;
};



struct VSNode {
    friend class VSThreadPool;
    friend struct VSCore;
private:
    std::atomic<long> refcount;
    VSMediaType nodeType;
    bool frameReadyNotify = false;
    void *instanceData;
    std::string name;
    VSFilterGetFrame filterGetFrame;
    VSFilterFree freeFunc = nullptr;
    VSFilterMode filterMode;

    int apiMajor;
    VSCore *core;
    PVSFunctionFrame functionFrame;
    int flags;
    std::vector<VSVideoInfo> vi;
    std::vector<vs3::VSVideoInfo> v3vi;
    std::vector<VSAudioInfo> ai;

    // for keeping track of when a filter is busy in the exclusive section and with which frame
    // used for fmFrameState and fmParallel (mutex only)
    std::mutex serialMutex;
    int serialFrame;
    // to prevent multiple calls at the same time for the same frame
    // this is only used by fmParallel and fmParallelRequests when frameReadyNotify is set
    std::mutex concurrentFramesMutex;
    std::set<int> concurrentFrames;

    std::atomic<int64_t> processingTime;

    PVSFrameRef getFrameInternal(int n, int activationReason, VSFrameContext *frameCtx);
public:
    VSNode(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core); // V3 compatibility
    VSNode(const std::string &name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core);
    VSNode(const std::string &name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core);
    ~VSNode();

    void add_ref() noexcept {
        ++refcount;
    }

    void release() noexcept {
        assert(refcount > 0);
        if (--refcount == 0)
            delete this;
    }

    int getApiMajor() const {
        return apiMajor;
    }

    VSMediaType getNodeType() const {
        return nodeType;
    }

    VSFilterMode getFilterMode() const {
        return filterMode;
    }

    int64_t getFilterTime() const {
        return processingTime;
    }

    int getNodeFlags() const {
        return flags;
    }

    bool isRightCore(const VSCore *core2) const {
        return core == core2;
    }

    void getFrame(const PVSFrameContext &ct);

    const VSVideoInfo &getVideoInfo(int index) const;
    const vs3::VSVideoInfo &getVideoInfo3(int index) const;
    const VSAudioInfo &getAudioInfo(int index) const;

    void setVideoInfo3(const vs3::VSVideoInfo *vi, int numOutputs);

    size_t getNumOutputs() const {
        return (nodeType == mtVideo) ? vi.size() : ai.size();
    }

    const std::string &getName() const {
        return name;
    }

    const char *getCreationFunctionName(int level) const;
    const VSMap *getCreationFunctionArguments(int level) const;
    void setFilterRelation(VSNodeRef **dependencies, int numDeps);

    // to get around encapsulation a bit, more elegant than making everything friends in this case
    void reserveThread();
    void releaseThread();
    bool isWorkerThread();

    void notifyCache(bool needMemory);
};

class VSThreadPool {
private:
    VSCore *core;
    std::mutex lock;
    std::mutex callbackLock;
    std::map<std::thread::id, std::thread *> allThreads;
    std::list<PVSFrameContext> tasks;
    std::unordered_map<NodeOutputKey, PVSFrameContext> allContexts;
    std::condition_variable newWork;
    std::condition_variable allIdle;
    std::atomic<size_t> activeThreads;
    std::atomic<size_t> idleThreads;
    std::atomic<size_t> reqCounter;
    size_t maxThreads;
    std::atomic<bool> stopThreads;
    std::atomic<size_t> ticks;
    size_t getNumAvailableThreads();
    void wakeThread();
    void startInternal(const PVSFrameContext &context);
    void spawnThread();
    static void runTasks(VSThreadPool *owner, std::atomic<bool> &stop);
    static bool taskCmp(const PVSFrameContext &a, const PVSFrameContext &b);
public:
    VSThreadPool(VSCore *core);
    ~VSThreadPool();
    void returnFrame(const PVSFrameContext &rCtx, const PVSFrameRef &f);
    void returnFrame(const PVSFrameContext &rCtx, const std::string &errMsg);
    size_t threadCount();
    size_t setThreadCount(size_t threads);
    void start(const PVSFrameContext &context);
    void releaseThread();
    void reserveThread();
    bool isWorkerThread();
    void waitForDone();
};

struct VSPluginFunction {
private:
    VSPublicFunction func;
    void *functionData;
    VSPlugin *plugin;
    std::string name;
    std::string argString;
    std::string returnType;
    std::vector<FilterArgument> inArgs;
    std::vector<FilterArgument> retArgs;
    static void parseArgString(const std::string &argString, std::vector<FilterArgument> &argsOut, int apiMajor);
public:
    VSPluginFunction(const std::string &name, const std::string &argString, const std::string &returnType, VSPublicFunction func, void *functionData, VSPlugin *plugin);
    VSMap *invoke(const VSMap &args, bool addCache);
    const std::string &getName() const;
    const std::string &getArguments() const;
    const std::string &getReturnType() const;
    bool isV3Compatible() const;
    std::string getV4ArgString() const;
    std::string getV3ArgString() const;
};


struct VSPlugin {
    friend struct VSPluginFunction;
private:
    int apiMajor = 0;
    int apiMinor = 0;
    int pluginVersion = 0;
    bool hasConfig = false;
    bool readOnly = false;
    bool readOnlySet = false;
    bool compat = false;
    std::string filename;
    std::string fullname;
    std::string fnamespace;
    std::string id;
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE libHandle;
#else
    void *libHandle;
#endif
    std::map<std::string, VSPluginFunction> funcs;
    std::mutex functionLock;
    VSCore *core;
public:
    explicit VSPlugin(VSCore *core);
    VSPlugin(const std::string &relFilename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath, VSCore *core);
    ~VSPlugin();
    void lock() { readOnly = true; }
    void enableCompat() { compat = true; }
    bool configPlugin(const std::string &identifier, const std::string &pluginsNamespace, const std::string &fullname, int pluginVersion, int apiVersion, int flags);
    bool registerFunction(const std::string &name, const std::string &args, const std::string &returnType, VSPublicFunction argsFunc, void *functionData);
    VSMap *invoke(const std::string &funcName, const VSMap &args, bool addCache);
    VSPluginFunction *getNextFunction(VSPluginFunction *func);
    VSPluginFunction *getFunctionByName(const std::string name);
    const std::string &getName() const { return fullname; }
    const std::string &getID() const { return id; }
    const std::string &getNamespace() const { return fnamespace; }
    const std::string &getFilename() const { return filename; }
    int getPluginVersion() const { return pluginVersion; }
    void getFunctions3(VSMap *out) const;
};

struct VSLogHandle {
    VSLogHandler handler;
    VSLogHandlerFree freeFunc;
    void *userData;
    ~VSLogHandle() {
        if (freeFunc)
            freeFunc(userData);
    }
};

struct VSCore {
    friend class CacheInstance;
private:
    //number of filter instances plus one, freeing the core reduces it by one
    // the core will be freed once it reaches 0
    bool coreFreed;
    std::atomic<long> numFilterInstances;
    std::atomic<long> numFunctionInstances;

    std::map<std::string, VSPlugin *> plugins;
    std::recursive_mutex pluginLock;
    std::map<int, vs3::VSVideoFormat> videoFormats;
    std::mutex videoFormatLock;
    int videoFormatIdOffset = 1000;
    VSCoreInfo coreInfo;
    std::set<VSNode *> caches;
    std::mutex cacheLock;

    std::atomic<int> cpuLevel;

    ~VSCore();

    void registerFormats();

    std::mutex logMutex;
    std::set<VSLogHandle *> messageHandlers;
public:
    VSThreadPool *threadPool;
    MemoryUse *memory;

    bool disableLibraryUnloading;

    // Used only for graph inspection
    bool enableGraphInspection; 
    static thread_local PVSFunctionFrame functionFrame;
    //

    void notifyCaches(bool needMemory);
    const vs3::VSVideoFormat *getV3VideoFormat(int id);
    const vs3::VSVideoFormat *getVideoFormat3(int id);
    static bool queryVideoFormat(VSVideoFormat &f, VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) noexcept;
    bool getVideoFormatByID(VSVideoFormat &f, uint32_t id) noexcept;
    uint32_t queryVideoFormatID(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) const noexcept;
    const vs3::VSVideoFormat *queryVideoFormat3(vs3::VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name = nullptr, int id = 0) noexcept;
    static bool queryAudioFormat(VSAudioFormat &f, VSSampleType sampleType, int bitsPerSample, uint64_t channelLayout) noexcept;
    bool isValidFormatPointer(const void *f);
    static bool isValidVideoFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH) noexcept;
    static bool isValidVideoFormat(const VSVideoFormat &format) noexcept;
    static bool isValidAudioFormat(int sampleType, int bitsPerSample, uint64_t channelLayout) noexcept;
    static bool isValidAudioFormat(const VSAudioFormat &format) noexcept;
    static bool isValidVideoInfo(const VSVideoInfo &vi) noexcept;
    static bool isValidAudioInfo(const VSAudioInfo &ai) noexcept;

    VSLogHandle *addLogHandler(VSLogHandler handler, VSLogHandlerFree free, void *userData);
    bool removeLogHandler(VSLogHandle *rec);
    void logMessage(VSMessageType type, const char *msg);
    void logMessage(VSMessageType type, const std::string &msg);
    [[noreturn]] void logFatal(const char *msg);
    [[noreturn]] void logFatal(const std::string &msg);

    /////////////////////////////////////
    // V3 compat helper functions
    static VSColorFamily ColorFamilyFromV3(int colorFamily) noexcept;
    static vs3::VSColorFamily ColorFamilyToV3(int colorFamily) noexcept;
    const vs3::VSVideoFormat *VideoFormatToV3(const VSVideoFormat &format) noexcept;
    bool VideoFormatFromV3(VSVideoFormat &out, const vs3::VSVideoFormat *format) noexcept;
    vs3::VSVideoInfo VideoInfoToV3(const VSVideoInfo &vi) noexcept;
    VSVideoInfo VideoInfoFromV3(const vs3::VSVideoInfo &vi) noexcept;

    void loadPlugin(const std::string &filename, const std::string &forcedNamespace = std::string(), const std::string &forcedId = std::string(), bool altSearchPath = false);

#ifdef VS_TARGET_OS_WINDOWS
    bool loadAllPluginsInPath(const std::wstring &path, const std::wstring &filter);
#else
    bool loadAllPluginsInPath(const std::string &path, const std::string &filter);
#endif

    void createFilter3(const VSMap *in, VSMap *out, const std::string &name, vs3::VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor);
    void createVideoFilter(VSMap *out, const std::string &name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor);
    void createAudioFilter(VSMap *out, const std::string &name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor);

    int getCpuLevel() const;
    int setCpuLevel(int cpu);

    VSMap *getPlugins3();
    VSPlugin *getPluginByID(const std::string &identifier);
    VSPlugin *getPluginByNamespace(const std::string &ns);
    VSPlugin *getNextPlugin(VSPlugin *plugin);

    const VSCoreInfo &getCoreInfo3();
    void getCoreInfo(VSCoreInfo &info);

    static bool getAudioFormatName(const VSAudioFormat &format, char *buffer) noexcept;
    static bool getVideoFormatName(const VSVideoFormat &format, char *buffer) noexcept;

    void functionInstanceCreated();
    void functionInstanceDestroyed();
    void filterInstanceCreated();
    void filterInstanceDestroyed();
    void destroyFilterInstance(VSNode *node);

    explicit VSCore(int flags);
    void freeCore();
};

#endif // VSCORE_H
