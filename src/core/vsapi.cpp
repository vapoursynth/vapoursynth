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
#include "cpufeatures.h"
#include "vslog.h"
#include <cassert>
#include <cstring>
#include <string>

void VS_CC vs_internal_configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin) VS_NOEXCEPT {
    assert(identifier && defaultNamespace && name && plugin);
    plugin->configPlugin(identifier, defaultNamespace, name, apiVersion, !!readOnly);
}

void VS_CC vs_internal_registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT {
    assert(name && args && argsFunc && plugin);
    plugin->registerFunction(name, args, argsFunc, functionData);
}

static const VSFormat *VS_CC getFormatPreset(int id, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->getFormatPreset((VSPresetFormat)id);
}

static const VSFormat *VS_CC registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->registerFormat((VSColorFamily)colorFamily, (VSSampleType)sampleType, bitsPerSample, subSamplingW, subSamplingH);
}

static const VSFrameRef *VS_CC cloneFrameRef(const VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    return new VSFrameRef(frame->frame);
}

static VSNodeRef *VS_CC cloneNodeRef(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    return new VSNodeRef(node->clip, node->index);
}

static int VS_CC getStride(const VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->frame->getStride(plane);
}

static const uint8_t *VS_CC getReadPtr(const VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->frame->getReadPtr(plane);
}

static uint8_t *VS_CC getWritePtr(VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->frame->getWritePtr(plane);
}

static void VS_CC getFrameAsync(int n, VSNodeRef *clip, VSFrameDoneCallback fdc, void *userData) VS_NOEXCEPT {
    assert(clip && fdc);
    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (n < 0 || (numFrames && n >= numFrames)) {
        PFrameContext ctx(std::make_shared<FrameContext>(n, clip->index, clip, fdc, userData));
        ctx->setError("Invalid frame number " + std::to_string(n) + " requested, clip only has " + std::to_string(numFrames) + " frames");
        clip->clip->getFrame(ctx);
    } else {
        clip->clip->getFrame(std::make_shared<FrameContext>(n, clip->index, clip, fdc, userData));
    }
}

struct GetFrameWaiter {
    std::mutex b;
    std::condition_variable a;
    const VSFrameRef *r;
    char *errorMsg;
    int bufSize;
    GetFrameWaiter(char *errorMsg, int bufSize) : errorMsg(errorMsg), bufSize(bufSize) {}
};

static void VS_CC frameWaiterCallback(void *userData, const VSFrameRef *frame, int n, VSNodeRef *node, const char *errorMsg) VS_NOEXCEPT {
    GetFrameWaiter *g = static_cast<GetFrameWaiter *>(userData);
    std::lock_guard<std::mutex> l(g->b);
    g->r = frame;
    if (g->errorMsg && g->bufSize > 0) {
        memset(g->errorMsg, 0, g->bufSize);
        if (errorMsg) {
            strncpy(g->errorMsg, errorMsg, g->bufSize);
            g->errorMsg[g->bufSize - 1] = 0;
        }
    }
    g->a.notify_one();
}

static const VSFrameRef *VS_CC getFrame(int n, VSNodeRef *clip, char *errorMsg, int bufSize) VS_NOEXCEPT {
    assert(clip);
    GetFrameWaiter g(errorMsg, bufSize);
    std::unique_lock<std::mutex> l(g.b);
    VSNode *node = clip->clip.get();
    bool isWorker = node->isWorkerThread();
    if (isWorker)
        node->releaseThread();
    node->getFrame(std::make_shared<FrameContext>(n, clip->index, clip, &frameWaiterCallback, &g, false));
    g.a.wait(l);
    if (isWorker)
        node->reserveThread();
    return g.r;
}

static void VS_CC requestFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(clip && frameCtx);
    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    frameCtx->reqList.push_back(std::make_shared<FrameContext>(n, clip->index, clip->clip.get(), frameCtx->ctx));
}

static const VSFrameRef *VS_CC getFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(clip && frameCtx);

    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    auto ref = frameCtx->ctx->availableFrames.find(NodeOutputKey(clip->clip.get(), n, clip->index));
    if (ref != frameCtx->ctx->availableFrames.end())
        return new VSFrameRef(ref->second);
    return nullptr;
}

static void VS_CC freeFrame(const VSFrameRef *frame) VS_NOEXCEPT {
    delete frame;
}

static void VS_CC freeNode(VSNodeRef *clip) VS_NOEXCEPT {
    delete clip;
}

static VSFrameRef *VS_CC newVideoFrame(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return new VSFrameRef(core->newVideoFrame(format, width, height, propSrc ? propSrc->frame.get() : nullptr));
}

static VSFrameRef *VS_CC newVideoFrame2(const VSFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    VSFrame *fp[3];
    for (int i = 0; i < format->numPlanes; i++)
        fp[i] = planeSrc[i] ? planeSrc[i]->frame.get() : nullptr;
    return new VSFrameRef(core->newVideoFrame(format, width, height, fp, planes, propSrc ? propSrc->frame.get() : nullptr));
}

static VSFrameRef *VS_CC copyFrame(const VSFrameRef *frame, VSCore *core) VS_NOEXCEPT {
    assert(frame && core);
    return new VSFrameRef(core->copyFrame(frame->frame));
}

static void VS_CC copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) VS_NOEXCEPT {
    assert(src && dst && core);
    core->copyFrameProps(src->frame, dst->frame);
}

static void VS_CC createFilter(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(in && out && name && init && getFrame && core);
    if (!name)
        vsFatal("NULL name pointer passed to createFilter()");
    core->createFilter(in, out, name, init, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, VAPOURSYNTH_API_MAJOR);
}

static void VS_CC setError(VSMap *map, const char *errorMessage) VS_NOEXCEPT {
    assert(map && errorMessage);
    map->setError(errorMessage ? errorMessage : "Error: no error specified");
}

static const char *VS_CC getError(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    if (map->hasError())
        return map->getErrorMessage().c_str();
    else
        return nullptr;
}

static void VS_CC setFilterError(const char *errorMessage, VSFrameContext *context) VS_NOEXCEPT {
    assert(errorMessage && context);
    context->ctx->setError(errorMessage);
}

//property access functions
static const VSVideoInfo *VS_CC getVideoInfo(VSNodeRef *c) VS_NOEXCEPT {
    assert(c);
    return &c->clip->getVideoInfo(c->index);
}

static void VS_CC setVideoInfo(const VSVideoInfo *vi, int numOutputs, VSNode *c) VS_NOEXCEPT {
    assert(vi && numOutputs > 0 && c);
    c->setVideoInfo(vi, numOutputs);
}

static const VSFormat *VS_CC getFrameFormat(const VSFrameRef *f) VS_NOEXCEPT {
    assert(f);
    return f->frame->getFormat();
}

static int VS_CC getFrameWidth(const VSFrameRef *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->frame->getWidth(plane);
}

static int VS_CC getFrameHeight(const VSFrameRef *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->frame->getHeight(plane);
}

static const VSMap *VS_CC getFramePropsRO(const VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->frame->getConstProperties();
}

static VSMap *VS_CC getFramePropsRW(VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->frame->getProperties();
}

static int VS_CC propNumKeys(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    return static_cast<int>(map->size());
}

static const char *VS_CC propGetKey(const VSMap *map, int index) VS_NOEXCEPT {
    assert(map);
    if (index < 0 || static_cast<size_t>(index) >= map->size())
        vsFatal(("propGetKey: Out of bounds index " + std::to_string(index) + " passed. Valid range: [0," + std::to_string(map->size() - 1) + "]").c_str());

    return map->key(index);
}

static int propNumElementsInternal(const VSMap *map, const std::string &key) VS_NOEXCEPT {
    VSVariant *val = map->find(key);
    return val ? val->size() : -1;
}


static int VS_CC propNumElements(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    return propNumElementsInternal(map, key);
}

static char VS_CC propGetType(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    const char a[] = { 'u', 'i', 'f', 's', 'c', 'v', 'm' };
    VSVariant *val = map->find(key);
    return val ? a[val->getType()] : 'u';
}

#define PROP_GET_SHARED(vt, retexpr) \
    assert(map && key); \
    if (map->hasError()) \
        vsFatal("Attempted to read key '%s' from a map with error set: %s", key, map->getErrorMessage().c_str()); \
    int err = 0; \
    VSVariant *l = map->find(key); \
    if (l && l->getType() == (vt)) { \
        if (index >= 0 && static_cast<size_t>(index) < l->size()) { \
            if (error) \
                *error = 0; \
            return (retexpr); \
        } else { \
            err |= peIndex; \
        } \
    } else if (l) { \
        err |= peType; \
    } else { \
        err = peUnset; \
    } \
    if (!error) \
        vsFatal("Property read unsuccessful but no error output: %s", key); \
    *error = err; \
    return 0;

static int64_t VS_CC propGetInt(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vInt, l->getValue<int64_t>(index))
}

static double VS_CC propGetFloat(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vFloat, l->getValue<double>(index))
}

static const char *VS_CC propGetData(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vData, l->getValue<VSMapData>(index)->c_str())
}

static int VS_CC propGetDataSize(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vData, static_cast<int>(l->getValue<VSMapData>(index)->size()))
}

static VSNodeRef *VS_CC propGetNode(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vNode, new VSNodeRef(l->getValue<VSNodeRef>(index)))
}

static const VSFrameRef *VS_CC propGetFrame(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vFrame, new VSFrameRef(l->getValue<PVideoFrame>(index)))
}

static int VS_CC propDeleteKey(VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    return map->erase(key);
}

static inline bool isAlphaUnderscore(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool isAlphaNumUnderscore(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool isValidVSMapKey(const std::string &s) {
    size_t len = s.length();
    if (!len)
        return false;

    if (!isAlphaUnderscore(s[0]))
        return false;
    for (size_t i = 1; i < len; i++)
        if (!isAlphaNumUnderscore(s[i]))
            return false;
    return true;
}

#define PROP_SET_SHARED(vv, appendexpr) \
    assert(map && key); \
    if (append != paReplace && append != paAppend && append != paTouch) \
        vsFatal("Invalid prop append mode given when setting key '%s'", key); \
    std::string skey = key; \
    if (!isValidVSMapKey(skey)) \
        return 1; \
    if (append != paReplace && map->contains(skey)) { \
        VSVariant &l = map->at(skey); \
        if (l.getType() != (vv)) \
            return 1; \
        else if (append == paAppend) \
            l.append(appendexpr); \
    } else { \
        VSVariant l((vv)); \
        if (append != paTouch) \
            l.append(appendexpr); \
        map->insert(skey, std::move(l)); \
    } \
    return 0;


static int VS_CC propSetInt(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT {
    PROP_SET_SHARED(VSVariant::vInt, i)
}

static int VS_CC propSetFloat(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT {
    PROP_SET_SHARED(VSVariant::vFloat, d)
}

static int VS_CC propSetData(VSMap *map, const char *key, const char *d, int length, int append) VS_NOEXCEPT {
    PROP_SET_SHARED(VSVariant::vData, length >= 0 ? std::string(d, length) : std::string(d))
}

static int VS_CC propSetNode(VSMap *map, const char *key, VSNodeRef *clip, int append) VS_NOEXCEPT {
    PROP_SET_SHARED(VSVariant::vNode, *clip)
}

static int VS_CC propSetFrame(VSMap *map, const char *key, const VSFrameRef *frame, int append) VS_NOEXCEPT {
    PROP_SET_SHARED(VSVariant::vFrame, frame->frame)
}

static VSMap *VS_CC invoke(VSPlugin *plugin, const char *name, const VSMap *args) VS_NOEXCEPT {
    assert(plugin && name && args);
    return new VSMap(plugin->invoke(name, *args));
}

static VSMap *VS_CC createMap() VS_NOEXCEPT {
    return new VSMap();
}

static void VS_CC freeMap(VSMap *map) VS_NOEXCEPT {
    delete map;
}

static void VS_CC clearMap(VSMap *map) VS_NOEXCEPT {
    assert(map);
    map->clear();
}

static VSCore *VS_CC createCore(int threads) VS_NOEXCEPT {
    return new VSCore(threads);
}

static void VS_CC freeCore(VSCore *core) VS_NOEXCEPT {
    if (core)
        core->freeCore();
}

static VSPlugin *VS_CC getPluginById(const char *identifier, VSCore *core) VS_NOEXCEPT {
    assert(identifier && core);
    return core->getPluginById(identifier);
}

static VSPlugin *VS_CC getPluginByNs(const char *ns, VSCore *core) VS_NOEXCEPT {
    assert(ns && core);
    return core->getPluginByNs(ns);
}

static VSMap *VS_CC getPlugins(VSCore *core) VS_NOEXCEPT {
    assert(core);
    return new VSMap(core->getPlugins());
}

static VSMap *VS_CC getFunctions(VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    return new VSMap(plugin->getFunctions());
}

static const VSCoreInfo *VS_CC getCoreInfo(VSCore *core) VS_NOEXCEPT {
    assert(core);
    return &core->getCoreInfo();
}

static VSFuncRef *VS_CC propGetFunc(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    PROP_GET_SHARED(VSVariant::vMethod, new VSFuncRef(l->getValue<PExtFunction>(index)))
}

static int VS_CC propSetFunc(VSMap *map, const char *key, VSFuncRef *func, int append) VS_NOEXCEPT {
    assert(func);
    PROP_SET_SHARED(VSVariant::vMethod, func->func)
}

static void VS_CC callFunc(VSFuncRef *func, const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi) VS_NOEXCEPT {
    assert(func && in && out);
    func->func->call(in, out);
}

static VSFuncRef *VS_CC createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const VSAPI *vsapi) VS_NOEXCEPT {
    assert(func && core && vsapi);
    return new VSFuncRef(std::make_shared<ExtFunction>(func, userData, free, core, vsapi));
}

static void VS_CC freeFunc(VSFuncRef *f) VS_NOEXCEPT {
    delete f;
}

static void VS_CC queryCompletedFrame(VSNodeRef **node, int *n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && n && frameCtx);
    *node = frameCtx->ctx->lastCompletedNode;
    *n = frameCtx->ctx->lastCompletedN;
}

static void VS_CC releaseFrameEarly(VSNodeRef *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && frameCtx);
    frameCtx->ctx->availableFrames.erase(NodeOutputKey(node->clip.get(), n, node->index));
}

static VSFuncRef *VS_CC cloneFuncRef(VSFuncRef *f) VS_NOEXCEPT {
    assert(f);
    return new VSFuncRef(f->func);
}

static int64_t VS_CC setMaxCacheSize(int64_t bytes, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->memory->setMaxMemoryUse(bytes);
}

static int VS_CC getOutputIndex(VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(frameCtx);
    return frameCtx->ctx->index;
}

static void VS_CC setMessageHandler(VSMessageHandler handler, void *userData) VS_NOEXCEPT {
    vsSetMessageHandler(handler, userData);
}

static int VS_CC setThreadCount(int threads, VSCore *core) VS_NOEXCEPT {
    assert(core);
    core->threadPool->setThreadCount(threads);
    return core->threadPool->threadCount();
}

static const char *VS_CC getPluginPath(const VSPlugin *plugin) VS_NOEXCEPT {
    if (!plugin)
        vsFatal("NULL passed to getPluginPath");
    if (!plugin->filename.empty())
        return plugin->filename.c_str();
    else
        return nullptr;
}

static const int64_t *VS_CC propGetIntArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    int index = 0;
    PROP_GET_SHARED(VSVariant::vInt, l->getArray<int64_t>())
}

static const double *VS_CC propGetFloatArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    int index = 0;
    PROP_GET_SHARED(VSVariant::vFloat, l->getArray<double>())
}

static int VS_CC propSetIntArray(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT {
    assert(map && key && size >= 0);
    if (size < 0)
        return 1;
    std::string skey = key;
    if (!isValidVSMapKey(skey))
        return 1;
    VSVariant l(VSVariant::vInt);
    l.setArray(i, size);
    map->insert(skey, std::move(l));
    return 0;
}

static int VS_CC propSetFloatArray(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT {
    assert(map && key && size >= 0);
    if (size < 0)
        return 1;
    std::string skey = key;
    if (!isValidVSMapKey(skey))
        return 1;
    VSVariant l(VSVariant::vFloat);
    l.setArray(d, size);
    map->insert(skey, std::move(l));
    return 0;
}

static void VS_CC logMessage(int msgType, const char *msg) VS_NOEXCEPT {
    vsLog(__FILE__, __LINE__, static_cast<VSMessageType>(msgType), "%s", msg);
}

const VSAPI vs_internal_vsapi = {
    &createCore,
    &freeCore,
    &getCoreInfo,

    &cloneFrameRef,
    &cloneNodeRef,
    &cloneFuncRef,

    &freeFrame,
    &freeNode,
    &freeFunc,

    &newVideoFrame,
    &copyFrame,
    &copyFrameProps,
    &vs_internal_registerFunction,
    &getPluginById,
    &getPluginByNs,
    &getPlugins,
    &getFunctions,
    &createFilter,
    &setError,
    &getError,
    &setFilterError,
    &invoke,
    &getFormatPreset,
    &registerFormat,
    &getFrame,
    &getFrameAsync,
    &getFrameFilter,
    &requestFrameFilter,
    &queryCompletedFrame,
    &releaseFrameEarly,

    &getStride,
    &getReadPtr,
    &getWritePtr,

    &createFunc,
    &callFunc,

    &createMap,
    &freeMap,
    &clearMap,

    &getVideoInfo,
    &setVideoInfo,
    &getFrameFormat,
    &getFrameWidth,
    &getFrameHeight,
    &getFramePropsRO,
    &getFramePropsRW,

    &propNumKeys,
    &propGetKey,
    &propNumElements,
    &propGetType,
    &propGetInt,
    &propGetFloat,
    &propGetData,
    &propGetDataSize,
    &propGetNode,
    &propGetFrame,
    &propGetFunc,
    &propDeleteKey,
    &propSetInt,
    &propSetFloat,
    &propSetData,
    &propSetNode,
    &propSetFrame,
    &propSetFunc,

    &setMaxCacheSize,
    &getOutputIndex,
    &newVideoFrame2,

    &setMessageHandler,
    &setThreadCount,

    &getPluginPath,

    &propGetIntArray,
    &propGetFloatArray,
    &propSetIntArray,
    &propSetFloatArray,

    &logMessage
};

///////////////////////////////

const VSAPI *getVSAPIInternal(int apiMajor) {
    if (apiMajor == VAPOURSYNTH_API_MAJOR) {
        return &vs_internal_vsapi;
    } else {
        vsFatal("Internally requested API version %d not supported", apiMajor);
        return nullptr;
    }
}

const VSAPI *VS_CC getVapourSynthAPI(int version) VS_NOEXCEPT {
    int apiMajor = version;
    int apiMinor = 0;
    if (apiMajor >= 0x10000) {
        apiMinor = (apiMajor & 0xFFFF);
        apiMajor >>= 16;
    }

    CPUFeatures f;
    getCPUFeatures(&f);
    if (!f.can_run_vs) {
        return nullptr;
    } else if (apiMajor == VAPOURSYNTH_API_MAJOR && apiMinor <= VAPOURSYNTH_API_MINOR) {
        return &vs_internal_vsapi;
    } else {
        return nullptr;
    }
}
