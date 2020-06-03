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

#ifndef VSCORE_H
#define VSCORE_H

#include "VapourSynth.h"
#include "vslog.h"
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cassert>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <random>
#include <algorithm>
#ifdef VS_TARGET_OS_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    define VS_FRAME_POOL
#else
#    include <dlfcn.h>
#endif

#ifdef VS_FRAME_GUARD
static const uint32_t VS_FRAME_GUARD_PATTERN = 0xDEADBEEF;
#endif

// Internal only filter mode for use by caches to make requests more linear
const int fmUnorderedLinear = fmUnordered + 13;

class VSFrame;
struct VSCore;
class VSCache;
struct VSNode;
class VSThreadPool;
class FrameContext;
class ExtFunction;

typedef std::shared_ptr<VSFrame> PVideoFrame;
typedef std::weak_ptr<VSFrame> WVideoFrame;
typedef std::shared_ptr<VSNode> PVideoNode;
typedef std::shared_ptr<ExtFunction> PExtFunction;
typedef std::shared_ptr<FrameContext> PFrameContext;

extern const VSAPI vs_internal_vsapi;
const VSAPI *getVSAPIInternal(int apiMajor);

class VSException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class NodeOutputKey {
private:
    VSNode *node;
    int n;
    int index;
public:
    NodeOutputKey(VSNode *node, int n, int index) : node(node), n(n), index(index) {}
    inline bool operator==(const NodeOutputKey &v) const {
        return node == v.node && n == v.n && index == v.index;
    }
    inline bool operator<(const NodeOutputKey &v) const {
        return (node < v.node) || (node == v.node && n < v.n) || (node == v.node && n == v.n && index < v.index);
    }
};

// variant types
typedef std::shared_ptr<std::string> VSMapData;
typedef std::vector<int64_t> IntList;
typedef std::vector<double> FloatList;
typedef std::vector<VSMapData> DataList;
typedef std::vector<VSNodeRef> NodeList;
typedef std::vector<PVideoFrame> FrameList;
typedef std::vector<PExtFunction> FuncList;

class ExtFunction {
private:
    VSPublicFunction func;
    void *userData;
    VSFreeFuncData free;
    VSCore *core;
    const VSAPI *vsapi;
public:
    ExtFunction(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const VSAPI *vsapi);
    ~ExtFunction();
    void call(const VSMap *in, VSMap *out);
};

class VSVariant {
public:
    enum VSVType { vUnset, vInt, vFloat, vData, vNode, vFrame, vMethod };
    VSVariant(VSVType vtype = vUnset);
    VSVariant(const VSVariant &v);
    VSVariant(VSVariant &&v);
    ~VSVariant();

    size_t size() const;
    VSVType getType() const;

    void append(int64_t val);
    void append(double val);
    void append(const std::string &val);
    void append(const VSNodeRef &val);
    void append(const PVideoFrame &val);
    void append(const PExtFunction &val);

    template<typename T>
    const T &getValue(size_t index) const {
        return reinterpret_cast<std::vector<T>*>(storage)->at(index);
    }

    template<typename T>
    const T *getArray() const {
        return reinterpret_cast<std::vector<T>*>(storage)->data();
    }

    template<typename T>
    void setArray(const T *val, size_t size) {
        assert(val && !storage);
        std::vector<T> *vect = new std::vector<T>(size);
        if (size)
            memcpy(vect->data(), val, size * sizeof(T));
        internalSize = size;
        storage = vect;
    }

private:
    VSVType vtype;
    size_t internalSize;
    void *storage;

    void initStorage(VSVType t);
};

class VSMapStorage {
private:
    std::atomic<int> refCount;
public:
    std::map<std::string, VSVariant> data;
    bool error;

    VSMapStorage() : refCount(1), error(false) {}

    VSMapStorage(const VSMapStorage &s) : refCount(1), data(s.data), error(s.error) {}

    bool unique() {
        return (refCount == 1);
    };

    void addRef() {
        ++refCount;
    }

    void release() {
        if (!--refCount)
            delete this;
    }
};

struct VSMap {
private:
    VSMapStorage *data;
public:
    VSMap() : data(new VSMapStorage()) {}

    VSMap(const VSMap &map) : data(map.data) {
        data->addRef();
    }

    VSMap(VSMap &&map) : data(map.data) {
        map.data = new VSMapStorage();
    }

    ~VSMap() {
        data->release();
    }

    VSMap &operator=(const VSMap &map) {
        data->release();
        data = map.data;
        data->addRef();
        return *this;
    }

    void detach() {
        if (!data->unique()) {
            VSMapStorage *old = data;
            data = new VSMapStorage(*data);
            old->release();
        }
    }

    bool contains(const std::string &key) const {
        return !!data->data.count(key);
    }

    VSVariant &at(const std::string &key) const {
        return data->data.at(key);
    }

    VSVariant &operator[](const std::string &key) const {
        // implicit creation is unwanted so make sure it doesn't happen by wrapping at() instead
        return data->data.at(key);
    }

    VSVariant *find(const std::string &key) const {
        auto it = data->data.find(key);
        return it == data->data.end() ? nullptr : &it->second;
    }

    bool erase(const std::string &key) {
        detach();
        return data->data.erase(key) > 0;
    }

    bool insert(const std::string &key, VSVariant &&v) {
        detach();
        data->data.erase(key);
        data->data.insert(std::make_pair(key, v));
        return true;
    }

    size_t size() const {
        return data->data.size();
    }

    void clear() {
        data->release();
        data = new VSMapStorage();
    }

    const char *key(int n) const {
        if (n >= static_cast<int>(size()))
            return nullptr;
        auto iter = data->data.cbegin();
        std::advance(iter, n);
        return iter->first.c_str();
    }

    const std::map<std::string, VSVariant> &getStorage() const {
        return data->data;
    }

    void setError(const std::string &errMsg) {
        clear();
        VSVariant v(VSVariant::vData);
        v.append(errMsg);
        insert("_Error", std::move(v));
        data->error = true;
    }

    bool hasError() const {
        return data->error;
    }

    const std::string &getErrorMessage() const {
        return *((*this)["_Error"].getValue<VSMapData>(0).get());
    }
};



struct VSFrameRef {
    PVideoFrame frame;
    VSFrameRef(const PVideoFrame &frame) : frame(frame) {}
    VSFrameRef(PVideoFrame &&frame) : frame(frame) {}
};

struct VSNodeRef {
    PVideoNode clip;
    int index;
    VSNodeRef(const PVideoNode &clip, int index) : clip(clip), index(index) {}
    VSNodeRef(PVideoNode &&clip, int index) : clip(clip), index(index) {}
};

struct VSFuncRef {
    PExtFunction func;
    VSFuncRef(const PExtFunction &func) : func(func) {}
    VSFuncRef(PExtFunction &&func) : func(func) {}
};

enum FilterArgumentType {
    faNone = -1,
    faInt = 0,
    faFloat,
    faData,
    faClip,
    faFrame,
    faFunc
};

class FilterArgument {
public:
    std::string name;
    FilterArgumentType type;
    bool arr;
    bool empty;
    bool opt;
    FilterArgument(const std::string &name, FilterArgumentType type, bool arr, bool empty, bool opt)
        : name(name), type(type), arr(arr), empty(empty), opt(opt) {}
};

class MemoryUse {
private:
    struct BlockHeader {
        size_t size; // Size of memory allocation, minus header and padding.
        bool large : 1; // Memory is allocated with large pages.
    };
    static_assert(sizeof(BlockHeader) <= 16, "block header too large");

    std::atomic<size_t> used;
    size_t maxMemoryUse;
    bool freeOnZero;
    bool largePageEnabled;
    bool memoryWarningIssued;
    std::multimap<size_t, uint8_t *> buffers;
    size_t unusedBufferSize;
    std::minstd_rand generator;
    std::mutex mutex;

    static bool largePageSupported();
    static size_t largePageSize();

    // May allocate more than the requested amount.
    void *allocateLargePage(size_t bytes) const;
    void freeLargePage(void *ptr) const;
    void *allocateMemory(size_t bytes) const;
    void freeMemory(void *ptr) const;
    bool isGoodFit(size_t requested, size_t actual) const;
public:
    void add(size_t bytes);
    void subtract(size_t bytes);
    uint8_t *allocBuffer(size_t bytes);
    void freeBuffer(uint8_t *buf);
    size_t memoryUse();
    size_t getLimit();
    int64_t setMaxMemoryUse(int64_t bytes);
    bool isOverLimit();
    void signalFree();
    MemoryUse();
    ~MemoryUse();
};

class VSPlaneData {
private:
    std::atomic<int> refCount;
    MemoryUse &mem;
public:
    uint8_t *data;
    const size_t size;
    VSPlaneData(size_t dataSize, MemoryUse &mem);
    VSPlaneData(const VSPlaneData &d);
    ~VSPlaneData();
    bool unique();
    void addRef();
    void release();
};

class VSFrame {
private:
    const VSFormat *format;
    VSPlaneData *data[3];
    int width;
    int height;
    int stride[3];
    VSMap properties;
public:
    static int alignment;

#ifdef VS_FRAME_GUARD
    static const int guardSpace = 64;
#else
    static const int guardSpace = 0;
#endif

    VSFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc, VSCore *core);
    VSFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core);
    VSFrame(const VSFrame &f);
    ~VSFrame();

    VSMap &getProperties() {
        return properties;
    }
    const VSMap &getConstProperties() const {
        return properties;
    }
    void setProperties(const VSMap &properties) {
        this->properties = properties;
    }
    const VSFormat *getFormat() const {
        return format;
    }
    int getWidth(int plane) const {
        return width >> (plane ? format->subSamplingW : 0);
    }
    int getHeight(int plane) const {
        return height >> (plane ? format->subSamplingH : 0);
    }
    int getStride(int plane) const;
    const uint8_t *getReadPtr(int plane) const;
    uint8_t *getWritePtr(int plane);

#ifdef VS_FRAME_GUARD
    bool verifyGuardPattern();
#endif
};

class FrameContext {
    friend class VSThreadPool;
private:
    uintptr_t reqOrder;
    unsigned numFrameRequests;
    int n;
    VSNode *clip;
    PVideoFrame returnedFrame;
    PFrameContext upstreamContext;
    PFrameContext notificationChain;
    void *userData;
    VSFrameDoneCallback frameDone;
    std::string errorMessage;
    bool error;
    bool lockOnOutput;
public:
    VSNodeRef *node;
    std::map<NodeOutputKey, PVideoFrame> availableFrames;
    int lastCompletedN;
    int index;
    VSNodeRef *lastCompletedNode;

    void *frameContext;
    bool setError(const std::string &errorMsg);
    inline bool hasError() const {
        return error;
    }
    const std::string &getErrorMessage() {
        return errorMessage;
    }
    FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext);
    FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData, bool lockOnOutput = true);
};

struct VSNode {
    friend class VSThreadPool;
    friend struct VSCore;
private:
    void *instanceData;
    std::string name;
    VSFilterInit init;
    VSFilterGetFrame filterGetFrame;
    VSFilterFree free;
    VSFilterMode filterMode;

    int apiMajor;
    VSCore *core;
    int flags;
    bool hasVi;
    std::vector<VSVideoInfo> vi;

    // for keeping track of when a filter is busy in the exclusive section and with which frame
    // used for fmSerial and fmParallel (mutex only)
    std::mutex serialMutex;
    int serialFrame;
    // to prevent multiple calls at the same time for the same frame
    // this is used exclusively by fmParallel
    // fmParallelRequests use this in combination with serialMutex to signal when all its frames are ready
    std::mutex concurrentFramesMutex;
    std::set<int> concurrentFrames;

    PVideoFrame getFrameInternal(int n, int activationReason, VSFrameContext &frameCtx);
public:
    VSNode(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor, VSCore *core);

    ~VSNode();

    bool isRightCore(const VSCore *core2) const {
        return core == core2;
    }

    void getFrame(const PFrameContext &ct);

    const VSVideoInfo &getVideoInfo(int index);

    void setVideoInfo(const VSVideoInfo *vi, int numOutputs);

    size_t getNumOutputs() const {
        return vi.size();
    }

    const std::string &getName() const {
        return name;
    }

    // to get around encapsulation a bit, more elegant than making everything friends in this case
    void reserveThread();
    void releaseThread();
    bool isWorkerThread();

    void notifyCache(bool needMemory);
};

struct VSFrameContext {
    PFrameContext &ctx;
    std::vector<PFrameContext> reqList;
    VSFrameContext(PFrameContext &ctx) : ctx(ctx) {}
};

class VSThreadPool {
    friend struct VSCore;
private:
    VSCore *core;
    std::mutex lock;
    std::mutex callbackLock;
    std::map<std::thread::id, std::thread *> allThreads;
    std::list<PFrameContext> tasks;
    std::map<NodeOutputKey, PFrameContext> allContexts;
    std::condition_variable newWork;
    std::condition_variable allIdle;
    std::atomic<unsigned> activeThreads;
    std::atomic<unsigned> idleThreads;
    std::atomic<uintptr_t> reqCounter;
    unsigned maxThreads;
    std::atomic<bool> stopThreads;
    std::atomic<unsigned> ticks;
    int getNumAvailableThreads();
    void wakeThread();
    void notifyCaches(bool needMemory);
    void startInternal(const PFrameContext &context);
    void spawnThread();
    static void runTasks(VSThreadPool *owner, std::atomic<bool> &stop);
    static bool taskCmp(const PFrameContext &a, const PFrameContext &b);
public:
    VSThreadPool(VSCore *core, int threads);
    ~VSThreadPool();
    void returnFrame(const PFrameContext &rCtx, const PVideoFrame &f);
    void returnFrame(const PFrameContext &rCtx, const std::string &errMsg);
    int threadCount();
    int setThreadCount(int threads);
    void start(const PFrameContext &context);
    void releaseThread();
    void reserveThread();
    bool isWorkerThread();
    void waitForDone();
};

class VSFunction {
public:
    std::vector<FilterArgument> args;
    std::string argString;
    void *functionData;
    VSPublicFunction func;
    VSFunction(const std::string &argString, VSPublicFunction func, void *functionData);
    VSFunction() : functionData(nullptr), func(nullptr) {}
};


struct VSPlugin {
private:
    int apiMajor;
    int apiMinor;
    bool hasConfig;
    bool readOnly;
    bool readOnlySet;
    bool compat;
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE libHandle;
#else
    void *libHandle;
#endif
    std::map<std::string, VSFunction> funcs;
    std::mutex registerFunctionLock;
    VSCore *core;
public:
    std::string filename;
    std::string fullname;
    std::string fnamespace;
    std::string id;
    explicit VSPlugin(VSCore *core);
    VSPlugin(const std::string &relFilename, const std::string &forcedNamespace, const std::string &forcedId, bool altSearchPath, VSCore *core);
    ~VSPlugin();
    void lock() {
        readOnly = true;
    };
    void enableCompat() {
        compat = true;
    }
    void configPlugin(const std::string &identifier, const std::string &defaultNamespace, const std::string &fullname, int apiVersion, bool readOnly);
    void registerFunction(const std::string &name, const std::string &args, VSPublicFunction argsFunc, void *functionData);
    VSMap invoke(const std::string &funcName, const VSMap &args);
    VSMap getFunctions();
};

struct VSCore {
    friend class VSFrame;
    friend class VSThreadPool;
    friend class CacheInstance;
private:
    //number of filter instances plus one, freeing the core reduces it by one
    // the core will be freed once it reaches 0
    bool coreFreed;
    std::atomic_int numFilterInstances;
    std::atomic_int numFunctionInstances;

    std::map<std::string, VSPlugin *> plugins;
    std::recursive_mutex pluginLock;
    std::map<int, VSFormat *> formats;
    std::mutex formatLock;
    int formatIdOffset;
    VSCoreInfo coreInfo;
    std::set<VSNode *> caches;
    std::mutex cacheLock;

    std::atomic_int cpuLevel;

    ~VSCore();

    void registerFormats();
#ifdef VS_TARGET_OS_WINDOWS
    bool loadAllPluginsInPath(const std::wstring &path, const std::wstring &filter);
#else
    bool loadAllPluginsInPath(const std::string &path, const std::string &filter);
#endif
public:
    VSThreadPool *threadPool;
    MemoryUse *memory;

    PVideoFrame newVideoFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc);
    PVideoFrame newVideoFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *planes, const VSFrame *propSrc);
    PVideoFrame copyFrame(const PVideoFrame &srcf);
    void copyFrameProps(const PVideoFrame &src, PVideoFrame &dst);

    const VSFormat *getFormatPreset(int id);
    const VSFormat *registerFormat(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name = nullptr, int id = pfNone);
    bool isValidFormatPointer(const VSFormat *f);

    void loadPlugin(const std::string &filename, const std::string &forcedNamespace = std::string(), const std::string &forcedId = std::string(), bool altSearchPath = false);
    void createFilter(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiMajor);

    int getCpuLevel() const;
    int setCpuLevel(int cpu);

    VSMap getPlugins();
    VSPlugin *getPluginById(const std::string &identifier);
    VSPlugin *getPluginByNs(const std::string &ns);

    const VSCoreInfo &getCoreInfo();
    void getCoreInfo2(VSCoreInfo &info);

    void functionInstanceCreated();
    void functionInstanceDestroyed();
    void filterInstanceCreated();
    void filterInstanceDestroyed();
    void destroyFilterInstance(VSNode *node);

    explicit VSCore(int threads);
    void freeCore();
};

#endif // VSCORE_H
