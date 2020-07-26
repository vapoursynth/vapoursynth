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
#include "cpufeatures.h"
#include "vslog.h"
#include "VSHelper4.h"
#include <cassert>
#include <cstring>
#include <string>

using namespace vsh;

static int VS_CC configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int pluginVersion, int apiVersion, int flags, VSPlugin *plugin) VS_NOEXCEPT {
    assert(identifier && defaultNamespace && name && plugin);
    return plugin->configPlugin(identifier, defaultNamespace, name, pluginVersion, apiVersion, flags);
}

static int VS_CC registerFunction(const char *name, const char *args, const char *returnType, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT {
    assert(name && args && returnType && argsFunc && plugin);
    return plugin->registerFunction(name, args, returnType, argsFunc, functionData);
}

static void VS_CC registerFunction3(const char *name, const char *args, vs3::VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT {
    assert(name && args && argsFunc && plugin);
    plugin->registerFunction(name, args, "any", reinterpret_cast<VSPublicFunction>(argsFunc), functionData);
}

static const vs3::VSVideoFormat *VS_CC getFormatPreset3(int id, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->getVideoFormat3(id);
}

static const vs3::VSVideoFormat *VS_CC registerFormat3(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->queryVideoFormat3(static_cast<vs3::VSColorFamily>(colorFamily), static_cast<VSSampleType>(sampleType), bitsPerSample, subSamplingW, subSamplingH);
}

static const VSFrameRef *VS_CC cloneFrameRef(const VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    const_cast<VSFrameRef *>(frame)->add_ref();
    return frame;
}

static VSNodeRef *VS_CC cloneNodeRef(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    node->add_ref();
    return node;
}

static ptrdiff_t VS_CC getStride(const VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getStride(plane);
}

static int VS_CC getStride3(const VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return static_cast<int>(frame->getStride(plane));
}

static const uint8_t *VS_CC getReadPtr(const VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getReadPtr(plane);
}

static uint8_t *VS_CC getWritePtr(VSFrameRef *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getWritePtr(plane);
}

static void VS_CC getFrameAsync(int n, VSNodeRef *clip, VSFrameDoneCallback fdc, void *userData) VS_NOEXCEPT {
    assert(clip && fdc);
    int numFrames = (clip->clip->getNodeType() == mtVideo) ? clip->clip->getVideoInfo(clip->index).numFrames : clip->clip->getAudioInfo(clip->index).numFrames;
    if (n < 0 || (numFrames && n >= numFrames)) {
        VSFrameContext *ctx = new VSFrameContext(n, clip->index, clip, fdc, userData);
        ctx->setError("Invalid frame number " + std::to_string(n) + " requested, clip only has " + std::to_string(numFrames) + " frames");
        clip->clip->getFrame(ctx);
    } else {
        clip->clip->getFrame(new VSFrameContext(n, clip->index, clip, fdc, userData));
    }
}

struct GetFrameWaiter {
    std::mutex b;
    std::condition_variable a;
    const VSFrameRef *r = nullptr;
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
    VSNode *node = clip->clip;
    bool isWorker = node->isWorkerThread();
    if (isWorker)
        node->releaseThread();
    node->getFrame(new VSFrameContext(n, clip->index, clip, &frameWaiterCallback, &g, false));
    g.a.wait(l);
    if (isWorker)
        node->reserveThread();
    return g.r;
}

static void VS_CC requestFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(clip && frameCtx);
    int numFrames = (clip->clip->getNodeType() == mtVideo) ? clip->clip->getVideoInfo(clip->index).numFrames : clip->clip->getAudioInfo(clip->index).numFrames;
    if (n >= numFrames)
        n = numFrames - 1;
    frameCtx->reqList.emplace_back(new VSFrameContext(n, clip->index, clip->clip, PVSFrameContext(frameCtx, true)));
}

static const VSFrameRef *VS_CC getFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(clip && frameCtx);

    int numFrames = (clip->clip->getNodeType() == mtVideo) ? clip->clip->getVideoInfo(clip->index).numFrames : clip->clip->getAudioInfo(clip->index).numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    auto key = NodeOutputKey(clip->clip, n, clip->index);
    for (size_t i = 0; i < frameCtx->availableFrames.size(); i++) {
        const auto &tmp = frameCtx->availableFrames[i];
        if (tmp.first == key) {
            tmp.second->add_ref();
            return tmp.second.get();
        }
    }
    return nullptr;
}

static void VS_CC freeFrame(const VSFrameRef *frame) VS_NOEXCEPT {
    if (frame)
        const_cast<VSFrameRef *>(frame)->release();
}

static void VS_CC freeNode(VSNodeRef *clip) VS_NOEXCEPT {
    if (clip)
        clip->release();
}

static VSFrameRef *VS_CC newVideoFrame(const VSVideoFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return core->newVideoFrame(*format, width, height, propSrc);
}

static VSFrameRef *VS_CC newVideoFrame3(const vs3::VSVideoFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    VSVideoFormat v4;
    core->VideoFormatFromV3(v4, format);
    return core->newVideoFrame(v4, width, height, propSrc);
}

static VSFrameRef *VS_CC newVideoFrame2(const VSVideoFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return core->newVideoFrame(*format, width, height, planeSrc, planes, propSrc);
}

static VSFrameRef *VS_CC newVideoFrame23(const vs3::VSVideoFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    VSVideoFormat v4;
    core->VideoFormatFromV3(v4, format);
    return core->newVideoFrame(v4, width, height, planeSrc, planes, propSrc);
}

static VSFrameRef *VS_CC copyFrame(const VSFrameRef *frame, VSCore *core) VS_NOEXCEPT {
    assert(frame && core);
    return core->copyFrame(*frame);
}

static void VS_CC copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) VS_NOEXCEPT {
    assert(src && dst && core);
    core->copyFrameProps(*src, *dst);
}

static void VS_CC createFilter3(const VSMap *in, VSMap *out, const char *name, vs3::VSFilterInit init, vs3::VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(in && out && name && init && getFrame && core);

    VSFilterMode fm;
    switch (filterMode) {
        case vs3::fmParallel:
            fm = fmParallel;
            break;
        case vs3::fmParallelRequests:
            fm = fmParallelRequests;
            break;
        case vs3::fmUnordered:
            fm = fmUnordered;
            break;
        case vs3::fmSerial:
            fm = fmSerial;
            break;
        default:
            fm = fmParallel;
            core->logMessage(mtFatal, "Invalid filter mode");
    }
    core->createFilter3(in, out, name, init, reinterpret_cast<VSFilterGetFrame>(getFrame), free, fm, flags, instanceData, VAPOURSYNTH3_API_MAJOR);
}

static void VS_CC setError(VSMap *map, const char *errorMessage) VS_NOEXCEPT {
    assert(map && errorMessage);
    map->setError(errorMessage ? errorMessage : "Error: no error specified");
}

static const char *VS_CC getError(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    return map->getErrorMessage();
}

static void VS_CC setFilterError(const char *errorMessage, VSFrameContext *context) VS_NOEXCEPT {
    assert(errorMessage && context);
    context->setError(errorMessage);
}

static const VSVideoInfo *VS_CC getVideoInfo(VSNodeRef *node) VS_NOEXCEPT {
    assert(node && node->clip->getNodeType() == mtVideo);
    return &node->clip->getVideoInfo(node->index);
}

static const vs3::VSVideoInfo *VS_CC getVideoInfo3(VSNodeRef *c) VS_NOEXCEPT {
    assert(c);
    return &c->clip->getVideoInfo3(c->index);
}

static void VS_CC setVideoInfo3(const vs3::VSVideoInfo *vi, int numOutputs, vs3::VSNode *c) VS_NOEXCEPT {
    assert(vi && numOutputs > 0 && c);
    reinterpret_cast<VSNode *>(c)->setVideoInfo3(vi, numOutputs);
}

static const VSVideoFormat *VS_CC getVideoFrameFormat(const VSFrameRef *f) VS_NOEXCEPT {
    assert(f);
    return f->getVideoFormat();
}

static const vs3::VSVideoFormat *VS_CC getFrameFormat3(const VSFrameRef *f) VS_NOEXCEPT {
    assert(f);
    return f->getVideoFormatV3();
}

static int VS_CC getFrameWidth(const VSFrameRef *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->getWidth(plane);
}

static int VS_CC getFrameHeight(const VSFrameRef *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->getHeight(plane);
}

static const VSMap *VS_CC getFramePropsRO(const VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->getConstProperties();
}

static VSMap *VS_CC getFramePropsRW(VSFrameRef *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->getProperties();
}

static int VS_CC propNumKeys(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    return static_cast<int>(map->size());
}

static const char *VS_CC propGetKey(const VSMap *map, int index) VS_NOEXCEPT {
    assert(map);
    return map->key(static_cast<size_t>(index));
}

static int VS_CC propNumElements(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    return val ? static_cast<int>(val->size()) : -1;
}

static int VS_CC propGetType(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    return val ? val->type() : ptUnset;
}

static char VS_CC propGetType3(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    VSPropType pt = val ? val->type() : ptUnset;
    switch (pt) {
        case ptInt:
            return vs3::ptInt;
        case ptFloat:
            return vs3::ptFloat;
        case ptData:
            return vs3::ptData;
        case ptVideoNode:
            return vs3::ptNode;
        case ptVideoFrame:
            return vs3::ptFrame;
        case ptFunction:
            return vs3::ptFunction;
        default:
            return vs3::ptUnset;
    }
}

static VSArrayBase *propGetShared(const VSMap *map, const char *key, int index, int *error, VSPropType propType) noexcept {
    assert(map && key && index >= 0);

    if (error)
        *error = 0;

    if (map->hasError()) {
        if (error)
            *error = peError;
        else
            VS_FATAL_ERROR(("Property read unsuccessful on map with error set but no error output: " + std::string(key)).c_str());
        return nullptr;
    }

    VSArrayBase *arr = map->find(key);

    if (!arr) {
        if (error)
            *error = peUnset;
        else
            VS_FATAL_ERROR(("Property read unsuccessful due to missing key but no error output: " + std::string(key)).c_str());
        return nullptr;
    }

    if (index < 0 || index >= static_cast<int>(arr->size())) {
        if (error)
            *error = peIndex;
        else
            VS_FATAL_ERROR(("Property read unsuccessful due to out of bounds index but no error output: " + std::string(key)).c_str());
        return nullptr;
    }

    if (arr->type() != propType) {
        if (error)
            *error = peType;
        else
            VS_FATAL_ERROR(("Property read unsuccessful due to wrong type but no error output: " + std::string(key)).c_str());
        return nullptr;
    }

    return arr;
}

static int64_t VS_CC propGetInt(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptInt);
    if (arr)
        return reinterpret_cast<const VSIntArray *>(arr)->at(index);
    else
        return 0;
}

static int VS_CC propGetSaturatedInt(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    return int64ToIntS(propGetInt(map, key, index, error));
}

static double VS_CC propGetFloat(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptFloat);
    if (arr)
        return reinterpret_cast<const VSFloatArray *>(arr)->at(index);
    else
        return 0;
}

static float VS_CC propGetSaturatedFloat(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    return doubleToFloatS(propGetFloat(map, key, index, error));
}

static const char *VS_CC propGetData(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return reinterpret_cast<const VSDataArray *>(arr)->at(index).data.c_str();
    else
        return nullptr;
}

static int VS_CC propGetDataSize(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return static_cast<int>(reinterpret_cast<const VSDataArray *>(arr)->at(index).data.size());
    else
        return -1;
}

static int VS_CC propGetDataType(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return reinterpret_cast<const VSDataArray *>(arr)->at(index).typeHint;
    else
        return dtUnknown;
}

static VSNodeRef *VS_CC propGetNode(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    int dummyError;
    VSArrayBase *arr = propGetShared(map, key, index, &dummyError, ptVideoNode);
    if (arr) {
        VSNodeRef *ref = reinterpret_cast<VSVideoNodeArray *>(arr)->at(index).get();
        ref->add_ref();
        return ref;
    } else {
        arr = propGetShared(map, key, index, error, ptAudioNode);
        if (arr) {
            VSNodeRef *ref = reinterpret_cast<VSAudioNodeArray *>(arr)->at(index).get();
            ref->add_ref();
            return ref;
        } else {
            return nullptr;
        }
    }
}

static const VSFrameRef *VS_CC propGetFrame(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    int dummyError;
    VSArrayBase *arr = propGetShared(map, key, index, &dummyError, ptData);
    if (arr) {
        VSFrameRef *ref = reinterpret_cast<VSVideoFrameArray *>(arr)->at(index).get();
        ref->add_ref();
        return ref;
    } else {
        arr = propGetShared(map, key, index, error, ptData);
        if (arr) {
            VSFrameRef *ref = reinterpret_cast<VSAudioFrameArray *>(arr)->at(index).get();
            ref->add_ref();
            return ref;
        } else {
            return nullptr;
        }
    }
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

static bool isValidVSMapKey(const char *s) {
    if (!s)
        return false;
    if (!isAlphaUnderscore(*s))
        return false;
    s++;
    while (*s) {
        if (!isAlphaNumUnderscore(*s))
            return false;
        s++;
    }
    return true;
}


static int VS_CC propSetEmpty(VSMap *map, const char *key, int type) VS_NOEXCEPT {
    assert(map && key);
    if (!isValidVSMapKey(key))
        return 1;

    std::string skey = key;
    if (map->find(skey))
        return 1;

    switch (type) {
        case ptInt:
            map->insert(key, new VSIntArray);
            break;
        case ptFloat:
            map->insert(key, new VSFloatArray);
            break;
        case ptData:
            map->insert(key, new VSDataArray);
            break;
        case ptVideoNode:
            map->insert(key, new VSVideoNodeArray);
            break;
        case ptAudioNode:
            map->insert(key, new VSAudioNodeArray);
            break;
        case ptVideoFrame:
            map->insert(key, new VSVideoFrameArray);
            break;
        case ptAudioFrame:
            map->insert(key, new VSAudioFrameArray);
            break;
        case ptFunction:
            map->insert(key, new VSFunctionArray);
            break;
        default:
            return 1;
    }
    return 0;
}

template<typename T, VSPropType propType>
bool propSetShared(VSMap *map, const char *key, const T &val, int append) {
    assert(map && key);
    if (append != paReplace && append != paAppend && append != vs3::paTouch)
        VS_FATAL_ERROR(("Invalid prop append mode given when setting key '" + std::string(key) + "'").c_str());

    if (!isValidVSMapKey(key))
        return false;
    std::string skey = key;

    if (append == paReplace) {
        VSArray<T, propType> *v = new VSArray<T, propType>();
        v->push_back(val);
        map->insert(key, v);
        return true;
    } else if (append == paAppend) {
        VSArrayBase *arr = map->find(skey);
        if (arr && arr->type() == propType) {
            arr = map->detach(skey);
            reinterpret_cast<VSArray<T, propType> *>(arr)->push_back(val);
            return true;
        } else if (arr) {
            return false;
        } else {
            VSArray<T, propType> *v = new VSArray<T, propType>();
            v->push_back(val);
            map->insert(key, v);
            return true;
        }
    } else /* if (append == vs3::paTouch) */ {
        return !propSetEmpty(map, key, propType);
    }
}

static int VS_CC propSetInt(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT {
    return !propSetShared<int64_t, ptInt>(map, key, i, append);
}

static int VS_CC propSetFloat(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT {
    return !propSetShared<double, ptFloat>(map, key, d, append);
}

static int VS_CC propSetData(VSMap *map, const char *key, const char *d, int length, int type, int append) VS_NOEXCEPT {
    return !propSetShared<VSMapData, ptData>(map, key, { static_cast<VSDataType>(type), (length >= 0) ? std::string(d, length) : std::string(d) }, append);
}

static int VS_CC propSetData3(VSMap *map, const char *key, const char *d, int length, int append) VS_NOEXCEPT {
    return propSetData(map, key, d, length, dtUnknown, append);
}

static int VS_CC propSetNode(VSMap *map, const char *key, VSNodeRef *node, int append) VS_NOEXCEPT {
    if (node == nullptr || node->clip->getNodeType() == mtVideo)
        return !propSetShared<PVSNodeRef, ptVideoNode>(map, key, { node, true }, append);
    else
        return !propSetShared<PVSNodeRef, ptAudioNode>(map, key, { node, true }, append);
}

static int VS_CC propSetFrame(VSMap *map, const char *key, const VSFrameRef *frame, int append) VS_NOEXCEPT {
    if (frame == nullptr || frame->getFrameType() == mtVideo)
        return !propSetShared<PVSFrameRef, ptVideoFrame>(map, key, { const_cast<VSFrameRef *>(frame), true }, append);
    else
        return !propSetShared<PVSFrameRef, ptAudioFrame>(map, key, { const_cast<VSFrameRef *>(frame), true }, append);
}

static VSMap *VS_CC invoke(VSPlugin *plugin, const char *name, const VSMap *args) VS_NOEXCEPT {
    assert(plugin && name && args);
    return plugin->invoke(name, *args);
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

static VSCore *VS_CC createCore(int flags) VS_NOEXCEPT {
    return new VSCore(flags);
}

static VSCore *VS_CC createCore3(int threads) VS_NOEXCEPT {
    VSCore *core = new VSCore(0);
    if (core)
        core->threadPool->setThreadCount(threads);
    return core;
}

static void VS_CC freeCore(VSCore *core) VS_NOEXCEPT {
    if (core)
        core->freeCore();
}

static VSPlugin *VS_CC getPluginByID(const char *identifier, VSCore *core) VS_NOEXCEPT {
    assert(identifier && core);
    return core->getPluginByID(identifier);
}

static VSPlugin *VS_CC getPluginByNamespace(const char *ns, VSCore *core) VS_NOEXCEPT {
    assert(ns && core);
    return core->getPluginByNamespace(ns);
}

static VSPlugin *VS_CC getNextPlugin(VSPlugin *plugin, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->getNextPlugin(plugin);
}

const char *VS_CC getPluginName(VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    return plugin->getName().c_str();
}

const char *VS_CC getPluginID(VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    return plugin->getID().c_str();
}

const char *VS_CC getPluginNamespace(VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    return plugin->getNamespace().c_str();
}

static VSPluginFunction *VS_CC getNextPluginFunction(VSPluginFunction *func, VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    return plugin->getNextFunction(func);
}

static VSPluginFunction *VS_CC getPluginFunctionByName(const char *name, VSPlugin *plugin) VS_NOEXCEPT {
    assert(name && plugin);
    return plugin->getFunctionByName(name);
}

static const char *VS_CC getPluginFunctionName(VSPluginFunction *func) VS_NOEXCEPT {
    assert(func);
    return func->getName().c_str();
}

static const char *VS_CC getPluginFunctionArguments(VSPluginFunction *func) VS_NOEXCEPT {
    assert(func);
    return func->getArguments().c_str();
}

static const char *VS_CC getPluginFunctionReturnType(VSPluginFunction *func) VS_NOEXCEPT {
    assert(func);
    return func->getReturnType().c_str();
}

static VSMap *VS_CC getPlugins3(VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->getPlugins3();
}

static VSMap *VS_CC getFunctions3(VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    VSMap *m = new VSMap();
    plugin->getFunctions3(m);
    return m;
}

static const VSCoreInfo *VS_CC getCoreInfo3(VSCore *core) VS_NOEXCEPT {
    assert(core);
    return &core->getCoreInfo3();
}

static VSFuncRef *VS_CC propGetFunc(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, index, error, ptFunction);
    if (arr) {
        VSFuncRef *ref = reinterpret_cast<const VSFunctionArray *>(arr)->at(index).get();
        ref->add_ref();
        return ref;
    } else {
        return nullptr;
    }
}

static int VS_CC propSetFunc(VSMap *map, const char *key, VSFuncRef *func, int append) VS_NOEXCEPT {
    return !propSetShared<PVSFuncRef, ptFunction>(map, key, { func, true }, append);
}

static void VS_CC callFunc(VSFuncRef *func, const VSMap *in, VSMap *out) VS_NOEXCEPT {
    assert(func && in && out);
    func->call(in, out);
}

static void VS_CC callFunc3(VSFuncRef *func, const VSMap *in, VSMap *out, VSCore *core, const vs3::VSAPI3 *vsapi) VS_NOEXCEPT {
    assert(func && in && out);
    func->call(in, out);
}

static VSFuncRef *VS_CC createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core) VS_NOEXCEPT {
    assert(func && core);
    return new VSFuncRef(func, userData, free, core, VAPOURSYNTH_API_MAJOR);
}

static VSFuncRef *VS_CC createFunc3(vs3::VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const vs3::VSAPI3 *vsapi) VS_NOEXCEPT {
    assert(func && core && vsapi);
    return new VSFuncRef(reinterpret_cast<VSPublicFunction>(func), userData, free, core, VAPOURSYNTH3_API_MAJOR);
}

static void VS_CC freeFunc(VSFuncRef *f) VS_NOEXCEPT {
    if (f)
        f->release();
}

static void VS_CC queryCompletedFrame(VSNodeRef **node, int *n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && n && frameCtx);
    *node = frameCtx->lastCompletedNode;
    *n = frameCtx->lastCompletedN;
}

static void VS_CC releaseFrameEarly(VSNodeRef *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && frameCtx);
    auto key = NodeOutputKey(node->clip, n, node->index);
    for (size_t i = 0; i < frameCtx->reqList.size(); i++) {
        auto &tmp = frameCtx->availableFrames[i];
        if (tmp.first == key) {
            tmp.first = NodeOutputKey(nullptr, -1, -1);
            tmp.second.reset();
        }
    }
}

static VSFuncRef *VS_CC cloneFuncRef(VSFuncRef *func) VS_NOEXCEPT {
    assert(func);
    func->add_ref();
    return func;
}

static int64_t VS_CC setMaxCacheSize(int64_t bytes, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->memory->setMaxMemoryUse(bytes);
}

static int VS_CC getOutputIndex(VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(frameCtx);
    return frameCtx->index;
}

static void VS_CC setMessageHandler(VSMessageHandler handler, void *userData) VS_NOEXCEPT {
    vsSetMessageHandler3(handler, userData);
}

static int VS_CC setThreadCount(int threads, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return static_cast<int>(core->threadPool->setThreadCount(threads));
}

static const char *VS_CC getPluginPath(const VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    if (!plugin)
        return nullptr;
    if (!plugin->getFilename().empty())
        return plugin->getFilename().c_str();
    else
        return nullptr;
}

static int VS_CC getPluginVersion(const VSPlugin *plugin) VS_NOEXCEPT {
    assert(plugin);
    if (!plugin)
        return -1;
    return plugin->getPluginVersion();
}

static const int64_t *VS_CC propGetIntArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, 0, error, ptInt);
    if (arr) {
        return reinterpret_cast<const VSIntArray *>(arr)->getDataPointer();
    } else {
        return nullptr;
    }
}

static const double *VS_CC propGetFloatArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, 0, error, ptFloat);
    if (arr) {
        return reinterpret_cast<const VSFloatArray *>(arr)->getDataPointer();
    } else {
        return nullptr;
    }
}

static int VS_CC propSetIntArray(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT {
    assert(map && key && size >= 0);
    if (size < 0)
        return 1;
    if (!isValidVSMapKey(key))
        return 1;
    map->insert(key, new VSIntArray(i, size));
    return 0;
}

static int VS_CC propSetFloatArray(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT {
    assert(map && key && size >= 0);
    if (size < 0)
        return 1;
    if (!isValidVSMapKey(key))
        return 1;
    map->insert(key, new VSFloatArray(d, size));
    return 0;
}

static void VS_CC logMessage(int msgType, const char *msg, VSCore *core) VS_NOEXCEPT {
    assert(msg && core);
    core->logMessage(static_cast<VSMessageType>(msgType), msg);
}

static void *VS_CC addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void *userData, VSCore *core) VS_NOEXCEPT {
    assert(handler && core);
    return core->addMessageHandler(handler, free, userData);
}

static int VS_CC removeMessageHandler(void *handle, VSCore *core) VS_NOEXCEPT {
    assert(handle && core);
    return core->removeMessageHandler(reinterpret_cast<VSMessageHandlerRecord *>(handle));
}

static void VS_CC logMessage3(int msgType, const char *msg) VS_NOEXCEPT {
    vsLog3(static_cast<vs3::VSMessageType>(msgType), "%s", msg);
}

static int VS_CC addMessageHandler3(VSMessageHandler handler, VSMessageHandlerFree free, void *userData) VS_NOEXCEPT {
    return vsAddMessageHandler3(handler, free, userData);
}

static int VS_CC removeMessageHandler3(int id) VS_NOEXCEPT {
    return vsRemoveMessageHandler3(id);
}

static void VS_CC getCoreInfo2(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT {
    assert(core && info);
    core->getCoreInfo(*info);
}

static void VS_CC createVideoFilter(VSMap *out, const char *name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(out && name && vi && numOutputs > 0 && getFrame && core);
    core->createVideoFilter(out, name, vi, numOutputs, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, VAPOURSYNTH_API_MAJOR);
}

static void VS_CC createAudioFilter(VSMap *out, const char *name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(out && name && ai && numOutputs > 0 && getFrame && core);
    core->createAudioFilter(out, name, ai, numOutputs, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, VAPOURSYNTH_API_MAJOR);
}

static VSFrameRef *VS_CC newAudioFrame(const VSAudioFormat *format, int numSamples, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core && numSamples > 0);
    return core->newAudioFrame(*format, numSamples, propSrc);
}

static int VS_CC queryAudioFormat(VSAudioFormat *format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore *core) VS_NOEXCEPT {
    assert(format);
    return core->queryAudioFormat(*format, static_cast<VSSampleType>(sampleType), bitsPerSample, channelLayout);
}

static int VS_CC queryVideoFormat(VSVideoFormat *format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT {
    assert(format);
    return core->queryVideoFormat(*format, static_cast<VSColorFamily>(colorFamily), static_cast<VSSampleType>(sampleType), bitsPerSample, subSamplingW, subSamplingH);
}

static uint32_t VS_CC queryVideoFormatID(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->queryVideoFormatID(static_cast<VSColorFamily>(colorFamily), static_cast<VSSampleType>(sampleType), bitsPerSample, subSamplingW, subSamplingH);
}

static int VS_CC queryVideoFormatByID(VSVideoFormat *format, uint32_t id, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return core->queryVideoFormatByID(*format, id);
}

static int VS_CC getAudioFormatName(const VSAudioFormat *format, char *buffer) VS_NOEXCEPT {
    assert(format && buffer);
    return VSCore::getAudioFormatName(*format, buffer);
}

static int VS_CC getVideoFormatName(const VSVideoFormat *format, char *buffer) VS_NOEXCEPT {
    assert(format && buffer);
    return VSCore::getVideoFormatName(*format, buffer);
}

static const VSAudioInfo *VS_CC getAudioInfo(VSNodeRef *node) VS_NOEXCEPT {
    assert(node && node->clip->getNodeType() == mtAudio);
    return &node->clip->getAudioInfo(node->index);
}

static const VSAudioFormat *VS_CC getAudioFrameFormat(const VSFrameRef *f) VS_NOEXCEPT {
    return f->getAudioFormat();
}

static int VS_CC getNodeType(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    return node->clip->getNodeType();
}


static int VS_CC getNodeFlags(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    return node->clip->getNodeFlags();
}

static int VS_CC getFrameType(const VSFrameRef *f) VS_NOEXCEPT {
    assert(f);
    return f->getFrameType();
}

static int VS_CC getFrameLength(const VSFrameRef *f) VS_NOEXCEPT {
    assert(f);
    return f->getFrameLength();
}

static int VS_CC getAPIVersion(void) VS_NOEXCEPT{
    return VAPOURSYNTH_API_VERSION;
}

static const char *VS_CC getNodeCreationFunctionName(VSNodeRef *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->clip->getCreationFunctionName(level);
}

static const VSMap *VS_CC getNodeCreationFunctionArguments(VSNodeRef *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->clip->getCreationFunctionArguments(level);
}

static const char *VS_CC getNodeName(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    return node->clip->getName().c_str();
}

static int VS_CC getNodeIndex(VSNodeRef *node) VS_NOEXCEPT {
    assert(node);
    return node->index;
}

static void VS_CC setInternalFilterRelation(const VSMap *nodeMap, VSNodeRef **dependencies, int numDeps) VS_NOEXCEPT {
    assert(nodeMap && dependencies && dependencies > 0);
    VSNodeRef *ref = propGetNode(nodeMap, "clip", 0, nullptr);
    ref->clip->setFilterRelation(dependencies, numDeps);
    freeNode(ref);
}

const VSPLUGINAPI vs_internal_vspapi {
    &getAPIVersion,
    &configPlugin,
    &registerFunction
};

const VSAPI vs_internal_vsapi = {
    &createVideoFilter,
    &createAudioFilter,
    &freeNode,
    &cloneNodeRef,
    &getNodeType,
    &getNodeFlags,
    &getVideoInfo,
    &getAudioInfo,

    &newVideoFrame,
    &newVideoFrame2,
    &newAudioFrame,
    &freeFrame,
    &cloneFrameRef,
    &copyFrame,
    &copyFrameProps,
    &getFramePropsRO,
    &getFramePropsRW,

    &getStride,
    &getReadPtr,
    &getWritePtr,

    &getVideoFrameFormat,
    &getAudioFrameFormat,
    &getFrameType,
    &getFrameWidth,
    &getFrameHeight,
    &getFrameLength,

    &getVideoFormatName,
    &getAudioFormatName,
    &queryVideoFormat,
    &queryAudioFormat,
    &queryVideoFormatID,
    &queryVideoFormatByID,

    &getFrame,
    &getFrameAsync,
    &getFrameFilter,
    &requestFrameFilter,
    &queryCompletedFrame,
    &releaseFrameEarly,
    &getOutputIndex,
    &setFilterError,

    &createFunc,
    &freeFunc,
    &cloneFuncRef,
    &callFunc,

    &createMap,
    &freeMap,
    &clearMap,

    &setError,
    &getError,

    &propNumKeys,
    &propGetKey,
    &propDeleteKey,
    &propNumElements,
    &propGetType,
    &propSetEmpty,

    &propGetInt,
    &propGetSaturatedInt,
    &propGetIntArray,
    &propSetInt,
    &propSetIntArray,

    &propGetFloat,
    &propGetSaturatedFloat,
    &propGetFloatArray,
    &propSetFloat,
    &propSetFloatArray,

    &propGetData,
    &propGetDataSize,
    &propGetDataType,
    &propSetData,

    &propGetNode,
    &propSetNode,

    &propGetFrame,
    &propSetFrame,

    &propGetFunc,
    &propSetFunc,

    &registerFunction,
    &getPluginByID,
    &getPluginByNamespace,
    &getNextPlugin,
    &getPluginName,
    &getPluginID,
    &getPluginNamespace,
    &getNextPluginFunction,
    &getPluginFunctionByName,
    &getPluginFunctionName,
    &getPluginFunctionArguments,
    &getPluginFunctionReturnType,
    &getPluginPath,
    &getPluginVersion,
    &invoke,

    &createCore,
    &freeCore,
    &setMaxCacheSize,
    &setThreadCount,
    &getCoreInfo2,
    &getAPIVersion,

    &logMessage,
    &addMessageHandler,
    &removeMessageHandler,

    &getNodeCreationFunctionName,
    &getNodeCreationFunctionArguments,
    &getNodeName,
    &getNodeIndex,
    &setInternalFilterRelation
};

const vs3::VSAPI3 vs_internal_vsapi3 = {
    &createCore3,
    &freeCore,
    &getCoreInfo3,

    &cloneFrameRef,
    &cloneNodeRef,
    &cloneFuncRef,

    &freeFrame,
    &freeNode,
    &freeFunc,

    &newVideoFrame3,
    &copyFrame,
    &copyFrameProps,
    &registerFunction3,
    &getPluginByID,
    &getPluginByNamespace,
    &getPlugins3,
    &getFunctions3,
    &createFilter3,
    &setError,
    &getError,
    &setFilterError,
    &invoke,
    &getFormatPreset3,
    &registerFormat3,
    &getFrame,
    &getFrameAsync,
    &getFrameFilter,
    &requestFrameFilter,
    &queryCompletedFrame,
    &releaseFrameEarly,

    &getStride3,
    &getReadPtr,
    &getWritePtr,

    &createFunc3,
    &callFunc3,

    &createMap,
    &freeMap,
    &clearMap,

    &getVideoInfo3,
    &setVideoInfo3,
    &getFrameFormat3,
    &getFrameWidth,
    &getFrameHeight,
    &getFramePropsRO,
    &getFramePropsRW,

    &propNumKeys,
    &propGetKey,
    &propNumElements,
    &propGetType3,
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
    &propSetData3,
    &propSetNode,
    &propSetFrame,
    &propSetFunc,

    &setMaxCacheSize,
    &getOutputIndex,
    &newVideoFrame23,

    &setMessageHandler,
    &setThreadCount,

    &getPluginPath,

    &propGetIntArray,
    &propGetFloatArray,
    &propSetIntArray,
    &propSetFloatArray,

    &logMessage3,
    &addMessageHandler3,
    &removeMessageHandler3,
    &getCoreInfo2
};

///////////////////////////////

const VSAPI *getVSAPIInternal(int apiMajor) {
    if (apiMajor == VAPOURSYNTH_API_MAJOR) {
        return &vs_internal_vsapi;
    }  else if (apiMajor == VAPOURSYNTH3_API_MAJOR) {
            return reinterpret_cast<const VSAPI *>(&vs_internal_vsapi3);
    } else {
        assert(false);
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

    if (!getCPUFeatures()->can_run_vs) {
        return nullptr;
    } else if (apiMajor == VAPOURSYNTH_API_MAJOR && apiMinor <= VAPOURSYNTH_API_MINOR) {
        return &vs_internal_vsapi;
    } else if (apiMajor == VAPOURSYNTH3_API_MAJOR && apiMinor <= VAPOURSYNTH3_API_MINOR) {
        return reinterpret_cast<const VSAPI *>(&vs_internal_vsapi3);
    } else {
        return nullptr;
    }
}
