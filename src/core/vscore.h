/*
* Copyright (c) 2012 Fredrik Mellbin
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

#include <QtCore/QtCore>
//#include <vld.h>
#include "VapourSynth.h"
#include <stdlib.h>
#include <stdexcept>
#ifdef VS_TARGET_OS_WINDOWS
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

class VSFrame;
struct VSCore;
struct VSNode;
class FrameContext;
class ExtFunction;

typedef QSharedPointer<VSFrame> PVideoFrame;
typedef QWeakPointer<VSFrame> WVideoFrame;
typedef QSharedPointer<VSNode> PVideoNode;
typedef QSharedPointer<ExtFunction> PExtFunction;
typedef QSharedPointer<FrameContext> PFrameContext;

extern const VSAPI vsapi;
const VSAPI *getVSAPIInternal(int version);

class VSException : public std::runtime_error {
public:
    VSException(const char *descr) : std::runtime_error(descr) { }
};

typedef QPair<VSNode *, int> FrameKey;

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
typedef QList<int64_t> IntList;
typedef QList<double> FloatList;
typedef QList<QByteArray> DataList;
typedef QList<VSNodeRef> NodeList;
typedef QList<PVideoFrame> FrameList;
typedef QList<PExtFunction> FuncList;

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
    void append(const QByteArray &val);
    void append(const VSNodeRef &val);
    void append(const PVideoFrame &val);
    void append(const PExtFunction &val);

    template<typename T>
    const T &getValue(int index) const {
        return reinterpret_cast<QList<T>*>(storage)->at(index);
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
    void setError(const QByteArray &error) {
        clear();
        VSVariant v(VSVariant::vData);
        v.append(error);
        insert("_Error", v);
    }
};

struct VSFrameRef {
    PVideoFrame frame;
    VSFrameRef(const PVideoFrame &frame) : frame(frame) {}
};

struct VSNodeRef {
    PVideoNode clip;
    int index;
    VSNodeRef(const PVideoNode &clip, int index) : clip(clip), index(index) {}
};

struct VSFuncRef {
    PExtFunction func;
    VSFuncRef(const PExtFunction &func) : func(func) {}
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
    QByteArray name;
    FilterArgumentType type;
    bool arr;
    bool empty;
    bool opt;
    FilterArgument(const QByteArray &name, FilterArgumentType type, bool arr, bool empty, bool opt)
        : name(name), type(type), arr(arr), empty(empty), opt(opt) {}
};

class MemoryUse {
private:
    QAtomicInt usedKiloBytes;
    bool freeOnZero;
    int64_t maxMemoryUse;
public:
    void add(long bytes) {
        usedKiloBytes.fetchAndAddOrdered((bytes + 1023) / 1024);
    }
    void subtract(long bytes) {
        usedKiloBytes.fetchAndAddOrdered(-((bytes + 1023) / 1024));
    }
    int64_t memoryUse() {
        unsigned int temp = (unsigned int)usedKiloBytes;
        return ((int64_t)usedKiloBytes) * 1024;
    }
    int64_t getLimit() {
        return maxMemoryUse;
    }
    int64_t setMaxMemoryUse(int64_t bytes) {
        if (bytes <= 0)
            qFatal("Maximum memory usage set to a negative number");
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

class VSFrameData : public QSharedData {
private:
    MemoryUse *mem;
    quint32 size;
public:
    uint8_t *data;
    VSFrameData(quint32 size, MemoryUse *mem);
    VSFrameData(const VSFrameData &d);
    ~VSFrameData();
};

class VSFrame {
private:
    enum FrameLocation { flLocal = 0, flGPU = 1 };
    const VSFormat *format;
    QSharedDataPointer<VSFrameData> data[3];
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

typedef QLinkedList<PFrameContext> VSThreadData;

class FrameContext {
    friend class VSThreadPool;
    friend class VSThread;
private:
    int numFrameRequests;
    int n;
    VSNodeRef *node;
    VSNode *clip;
    PVideoFrame returnedFrame;
    PFrameContext upstreamContext;
    PFrameContext notificationChain;
    bool error;
    QByteArray errorMessage;
    void *userData;
    VSFrameDoneCallback frameDone;
public:
    QThreadStorage<VSThreadData *> *tlRequests;
    QMap<NodeOutputKey, PVideoFrame> availableFrames;
    int lastCompletedN;
    int index;
    VSNodeRef *lastCompletedNode;

    void *frameContext;
    void setError(const QByteArray &errorMsg);
    inline bool hasError() {
        return error;
    }
    const QByteArray &getErrorMessage() {
        return errorMessage;
    }
    FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext);
    FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData);
};

struct VSNode {
    friend class VSThread;
private:
    void *instanceData;
    VSFilterInit init;
    VSFilterGetFrame filterGetFrame;
    VSFilterFree free;
    QByteArray name;
    VSCore *core;
    QVector<VSVideoInfo> vi;
    int flags;
    int apiVersion;
    bool hasVi;
    VSFilterMode filterMode;

    // for keeping track of when a filter is busy in the exclusive section and with which frame
    // used for fmSerial and fmParallel (mutex only)
    QMutex serialMutex;
    int serialFrame;
    // to prevent multiple calls at the same time for the same frame
    // this is used exclusively by fmParallel
    // fmParallelRequests use this in combination with serialMutex to signal when all its frames are ready
    QMutex concurrentFramesMutex;
    QSet<int> concurrentFrames;

    PVideoFrame getFrameInternal(int n, int activationReason, const PFrameContext &frameCtx);
public:
    VSNode(const VSMap *in, VSMap *out, const QByteArray &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion, VSCore *core);

    ~VSNode();

    void getFrame(const PFrameContext &ct);

    const VSVideoInfo &getVideoInfo(int index) {
        if (index < 0 || index >= vi.size())
            qFatal("Out of bounds videoinfo index");
        return vi[index];
    }
    void setVideoInfo(const VSVideoInfo *vi, int numOutputs) {
        if (numOutputs < 1)
            qFatal("Video filter needs to have at least one output");
        for (int i = 0; i < numOutputs; i++) {
            if ((!!vi[i].height) ^ (!!vi[i].width))
                qFatal("Variable dimension clips must have both width and height set to 0");
            this->vi.append(vi[i]);
            this->vi[i].flags = flags;
        }
        hasVi = true;
    }

    int getNumOutputs() const {
        return vi.size();
    }

	const QByteArray &getName() const {
		return name;
	}

	// to get around encapsulation a bit, more elegant than making everything friends in this case
	void reserveThread();
	void releaseThread();
	bool isWorkerThread();

    void notifyCache(bool needMemory);
};

class VSThreadPool;

class VSThread : public QThread {
private:
    VSThreadPool *owner;
    bool stop;
public:
    void run();
    void stopThread();
    VSThread(VSThreadPool *owner);
};

class VSThreadPool {
    friend class VSThread;
    friend struct VSCore;
private:
    VSCore *core;
    QMutex lock;
    QMutex callbackLock;
    QSet<VSThread *> allThreads;
    QLinkedList<PFrameContext> tasks;
    QMap<NodeOutputKey, PFrameContext> allContexts;
    QAtomicInt ticks;
    QWaitCondition newWork;
    QAtomicInt activeThreads;
	QAtomicInt idleThreads;
	int maxThreads;
    void wakeThread();
    void notifyCaches(bool needMemory);
    void startInternal(const PFrameContext &context);
	void spawnThread();
public:
    VSThreadPool(VSCore *core, int threads);
    ~VSThreadPool();
    void returnFrame(const PFrameContext &rCtx, const PVideoFrame &f);
    void returnFrame(const PFrameContext &rCtx, const QByteArray &errMsg);
    int	activeThreadCount() const;
    int	threadCount() const;
	void setThreadCount(int threads);
    void start(const PFrameContext &context);
    void waitForDone();
    void releaseThread();
    void reserveThread();
	bool isWorkerThread();
};

class VSFunction {
public:
    QList<FilterArgument> args;
    QByteArray name;
    QByteArray argString;
    void *functionData;
    VSPublicFunction func;
    VSFunction(const QByteArray &name, const QByteArray &argString, VSPublicFunction func, void *functionData);
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
    QByteArray filename;
    QList<VSFunction> funcs;
    VSCore *core;
public:
    QByteArray fullname;
    QByteArray fnamespace;
    QByteArray identifier;
    VSPlugin(VSCore *core);
    VSPlugin(const QByteArray &filename, const QByteArray &forcedNamespace, VSCore *core);
    ~VSPlugin();
    void lock() {
        readOnly = true;
    };
    void enableCompat() {
        compat = true;
    }
    void configPlugin(const QByteArray &identifier, const QByteArray &defaultNamespace, const QByteArray &fullname, int apiVersion, bool readOnly);
    void registerFunction(const QByteArray &name, const QByteArray &args, VSPublicFunction argsFunc, void *functionData);
    VSMap invoke(const QByteArray &funcName, const VSMap &args);
    VSMap getFunctions();
};



class VSCache;

struct VSCore {
    friend struct VSNode;
    friend class VSFrame;
    friend class VSThreadPool;
    friend class CacheInstance;
private:
    QMap<QByteArray, VSPlugin *> plugins;
    QMutex pluginLock;
    QHash<int, VSFormat *> formats;
    QMutex formatLock;
    int formatIdOffset;
    VSCoreInfo coreInfo;
    QList<VSNode *> caches;
    QMutex cacheLock;

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

    void loadPlugin(const QByteArray &filename, const QByteArray &forcedNamespace = QByteArray());
    void createFilter(const VSMap *in, VSMap *out, const QByteArray &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion);

    VSMap getPlugins();
    VSPlugin *getPluginById(const QByteArray &identifier);
    VSPlugin *getPluginByNs(const QByteArray &ns);

    int64_t setMaxCacheSize(int64_t bytes);
    int getAPIVersion();
    const VSCoreInfo &getCoreInfo();

    VSCore(int threads);
    ~VSCore();
};

#endif // VSCORE_H
