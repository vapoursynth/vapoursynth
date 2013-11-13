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

#ifndef VSCORE_H
#define VSCORE_H

#include <QtCore/QMap>
#include <QtCore/QByteArray>
#include "VapourSynth.h"
#include "vslog.h"
#include <stdlib.h>
#include <stdexcept>
#include <vector>
#include <set>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#ifdef VS_TARGET_OS_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <Windows.h>
#else
#    include <dlfcn.h>
#endif

class VSFrame;
struct VSCore;
struct VSNode;
class VSThreadPool;
class FrameContext;
class ExtFunction;

typedef std::shared_ptr<VSFrame> PVideoFrame;
typedef std::weak_ptr<VSFrame> WVideoFrame;
typedef std::shared_ptr<VSNode> PVideoNode;
typedef std::shared_ptr<ExtFunction> PExtFunction;
typedef std::shared_ptr<FrameContext> PFrameContext;

extern const VSAPI vsapi;
const VSAPI *getVSAPIInternal(int version);

class VSException : public std::runtime_error {
public:
    VSException(const char *descr) : std::runtime_error(descr) {}
    VSException(const std::string &descr) : std::runtime_error(descr) {}
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
public:
    ExtFunction(VSPublicFunction func, void *userData, VSFreeFuncData free) : func(func), userData(userData), free(free) {}
    ~ExtFunction() {
        if (free) free(userData);
    }
    void call(const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi) {
        func(in, out, userData, core, vsapi);
    }
};

class VSVariant {
public:
    enum VSVType { vUnset, vInt, vFloat, vData, vNode, vFrame, vMethod };
    VSVariant(VSVType vtype = vUnset);
    VSVariant(const VSVariant &v);
    ~VSVariant();

    int size() const;
    VSVType getType() const;

    void append(int64_t val);
    void append(double val);
    void append(const std::string &val);
    void append(const VSNodeRef &val);
    void append(const PVideoFrame &val);
    void append(const PExtFunction &val);

    template<typename T>
    const T &getValue(int index) const {
        return reinterpret_cast<std::vector<T>*>(storage)->at(index);
    }

private:
    VSVType vtype;
    int internalSize;
    void *storage;

    void initStorage(VSVType t);
};

struct VSMap : public QMap<QByteArray, VSVariant> {
public:
    VSVariant &operator[](const QByteArray &key) {
        return QMap<QByteArray, VSVariant>::operator[](key);
    }
    const VSVariant operator[](const QByteArray &key) const {
        return QMap<QByteArray, VSVariant>::operator[](key);
    }
    void setError(const std::string &error) {
        clear();
        VSVariant v(VSVariant::vData);
        v.append(error);
        insert("_Error", v);
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
    std::atomic<unsigned> usedKiloBytes;
    bool freeOnZero;
    int64_t maxMemoryUse;
public:
    void add(unsigned bytes) {
        usedKiloBytes.fetch_add((bytes + 1023) / 1024);
    }
    void subtract(unsigned bytes) {
        usedKiloBytes.fetch_sub((bytes + 1023) / 1024);
    }
    int64_t memoryUse() {
        int64_t temp = usedKiloBytes;
        return temp * 1024;
    }
    int64_t getLimit() {
        return maxMemoryUse;
    }
    int64_t setMaxMemoryUse(int64_t bytes) {
        if (bytes <= 0)
            vsFatal("Maximum memory usage set to a negative number");
        maxMemoryUse = bytes;
        return maxMemoryUse;
    }
    bool isOverLimit() {
        return memoryUse() > maxMemoryUse;
    }
    void signalFree() {
        freeOnZero = true;
        if (!usedKiloBytes)
            delete this;
    }
    MemoryUse() : freeOnZero(false) {
        maxMemoryUse = 1024*1024*1024;
    }
};

class VSPlaneData {
private:
    MemoryUse *mem;
    uint32_t size;
public:
    uint8_t *data;
    VSPlaneData(uint32_t size, MemoryUse *mem);
    VSPlaneData(const VSPlaneData &d);
    ~VSPlaneData();
};

typedef std::shared_ptr<VSPlaneData> VSPlaneDataPtr;

class VSFrame {
private:
    enum FrameLocation { flLocal = 0, flGPU = 1 };
    const VSFormat *format;
    VSPlaneDataPtr data[3];
    int width;
    int height;
    int stride[3];
    FrameLocation frameLocation;
    VSMap properties;
public:
    static const int alignment = 32;

    VSFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc, VSCore *core);
    VSFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core);
    VSFrame(const VSFrame &f);

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
};

class FrameContext {
    friend class VSThreadPool;
    friend void runTasks(VSThreadPool *owner, volatile bool &stop);
private:
    int numFrameRequests;
    int n;
    VSNodeRef *node;
    VSNode *clip;
    PVideoFrame returnedFrame;
    PFrameContext upstreamContext;
    PFrameContext notificationChain;
    bool error;
    std::string errorMessage;
    void *userData;
    VSFrameDoneCallback frameDone;
public:
    std::map<NodeOutputKey, PVideoFrame> availableFrames;
    int lastCompletedN;
    int index;
    VSNodeRef *lastCompletedNode;

    void *frameContext;
    void setError(const std::string &errorMsg);
    inline bool hasError() {
        return error;
    }
    const std::string &getErrorMessage() {
        return errorMessage;
    }
    FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext);
    FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData);
};

struct VSNode {
    friend class VSThreadPool;
private:
    void *instanceData;
    VSFilterInit init;
    VSFilterGetFrame filterGetFrame;
    VSFilterFree free;
    std::string name;
    VSCore *core;
    std::vector<VSVideoInfo> vi;
    int flags;
    int apiVersion;
    bool hasVi;
    VSFilterMode filterMode;

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
    VSNode(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion, VSCore *core);

    ~VSNode();

    void getFrame(const PFrameContext &ct);

    const VSVideoInfo &getVideoInfo(int index);

    void setVideoInfo(const VSVideoInfo *vi, int numOutputs);

    int getNumOutputs() const {
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
    std::atomic<unsigned> ticks;
    std::condition_variable newWork;
    std::atomic<unsigned> activeThreads;
    std::atomic<unsigned> idleThreads;
    unsigned maxThreads;
    std::atomic<bool> stopThreads;
    void wakeThread();
    void notifyCaches(bool needMemory);
    void startInternal(const PFrameContext &context);
    void spawnThread();
    static void runTasks(VSThreadPool *owner, std::atomic<bool> &stop);
public:
    VSThreadPool(VSCore *core, int threads);
    ~VSThreadPool();
    void returnFrame(const PFrameContext &rCtx, const PVideoFrame &f);
    void returnFrame(const PFrameContext &rCtx, const std::string &errMsg);
    int activeThreadCount() const;
    int threadCount() const;
    void setThreadCount(int threads);
    void start(const PFrameContext &context);
    void waitForDone();
    void releaseThread();
    void reserveThread();
    bool isWorkerThread();
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
    int apiVersion;
    bool hasConfig;
    bool readOnly;
    bool readOnlySet;
    bool compat;
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE libHandle;
#else
    void *libHandle;
#endif
    std::string filename;
    std::map<std::string, VSFunction> funcs;
    VSCore *core;
public:
    std::string fullname;
    std::string fnamespace;
    std::string identifier;
    VSPlugin(VSCore *core);
    VSPlugin(const std::string &filename, const std::string &forcedNamespace, VSCore *core);
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



class VSCache;

struct VSCore {
    friend struct VSNode;
    friend class VSFrame;
    friend class VSThreadPool;
    friend class CacheInstance;
private:
    std::map<std::string, VSPlugin *> plugins;
    std::recursive_mutex pluginLock;
    std::map<int, VSFormat *> formats;
    std::mutex formatLock;
    int formatIdOffset;
    VSCoreInfo coreInfo;
    std::set<VSNode *> caches;
    std::mutex cacheLock;

    void registerFormats();
    bool loadAllPluginsInPath(const QString &path, const QString &filter);
public:
    VSThreadPool *threadPool;
    MemoryUse *memory;

    PVideoFrame newVideoFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc);
    PVideoFrame newVideoFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *planes, const VSFrame *propSrc);
    PVideoFrame copyFrame(const PVideoFrame &srcf);
    void copyFrameProps(const PVideoFrame &src, PVideoFrame &dst);

    const VSFormat *getFormatPreset(int id);
    const VSFormat *registerFormat(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name = NULL, int id = pfNone);
    bool isValidFormatPointer(const VSFormat *f);

    void loadPlugin(const std::string &filename, const std::string &forcedNamespace = std::string());
    void createFilter(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion);

    VSMap getPlugins();
    VSPlugin *getPluginById(const std::string &identifier);
    VSPlugin *getPluginByNs(const std::string &ns);

    int64_t setMaxCacheSize(int64_t bytes);
    int getAPIVersion();
    const VSCoreInfo &getCoreInfo();

    VSCore(int threads);
    ~VSCore();
};

#endif // VSCORE_H
