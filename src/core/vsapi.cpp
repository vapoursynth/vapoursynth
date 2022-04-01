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

static const VSFrame *VS_CC addFrameRef(const VSFrame *frame) VS_NOEXCEPT {
    assert(frame);
    const_cast<VSFrame *>(frame)->add_ref();
    return frame;
}

static VSNode *VS_CC addNodeRef(VSNode *node) VS_NOEXCEPT {
    assert(node);
    node->add_ref();
    return node;
}

static ptrdiff_t VS_CC getStride(const VSFrame *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getStride(plane);
}

static int VS_CC getStride3(const VSFrame *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return static_cast<int>(frame->getStride(plane));
}

static const uint8_t *VS_CC getReadPtr(const VSFrame *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getReadPtr(plane);
}

static uint8_t *VS_CC getWritePtr(VSFrame *frame, int plane) VS_NOEXCEPT {
    assert(frame);
    return frame->getWritePtr(plane);
}

static void VS_CC getFrameAsync(int n, VSNode *clip, VSFrameDoneCallback fdc, void *userData) VS_NOEXCEPT {
    assert(clip && fdc);
    int numFrames = (clip->getNodeType() == mtVideo) ? clip->getVideoInfo().numFrames : clip->getAudioInfo().numFrames;
    VSFrameContext *ctx = new VSFrameContext(n, clip, fdc, userData, true);

    if (n < 0 || n >= numFrames)
        ctx->setError("Invalid frame number " + std::to_string(n) + " requested, clip only has " + std::to_string(numFrames) + " frames");

    clip->getFrame(ctx);
}

struct GetFrameWaiter {
    std::mutex b;
    std::condition_variable a;
    const VSFrame *r = nullptr;
    char *errorMsg;
    int bufSize;
    GetFrameWaiter(char *errorMsg, int bufSize) : errorMsg(errorMsg), bufSize(bufSize) {}
};

static void VS_CC frameWaiterCallback(void *userData, const VSFrame *frame, int n, VSNode *node, const char *errorMsg) VS_NOEXCEPT {
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

static const VSFrame *VS_CC getFrame(int n, VSNode *node, char *errorMsg, int bufSize) VS_NOEXCEPT {
    assert(node);
    int numFrames = (node->getNodeType() == mtVideo) ? node->getVideoInfo().numFrames : node->getAudioInfo().numFrames;
    if (n < 0 || n >= numFrames) {
        if (errorMsg && bufSize > 0) {
            memset(errorMsg, 0, bufSize);
            if (errorMsg) {
                strncpy(errorMsg, ("Invalid frame number " + std::to_string(n) + " requested, clip only has " + std::to_string(numFrames) + " frames").c_str(), bufSize);
                errorMsg[bufSize - 1] = 0;
            }
        }
        return nullptr;
    }

    GetFrameWaiter g(errorMsg, bufSize);
    std::unique_lock<std::mutex> l(g.b);
    bool isWorker = node->isWorkerThread();
    if (isWorker)
        node->releaseThread();
    node->getFrame(new VSFrameContext(n, node, &frameWaiterCallback, &g, false));
    g.a.wait(l);
    if (isWorker)
        node->reserveThread();
    return g.r;
}

static void VS_CC requestFrameFilter(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && frameCtx);
    int numFrames = (node->getNodeType() == mtVideo) ? node->getVideoInfo().numFrames : node->getAudioInfo().numFrames;
    if (n >= numFrames)
        n = numFrames - 1;
    frameCtx->reqList.emplace_back(NodeOutputKey(node, n));
}

static const VSFrame *VS_CC getFrameFilter(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && frameCtx);

    int numFrames = (node->getNodeType() == mtVideo) ? node->getVideoInfo().numFrames : node->getAudioInfo().numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    auto key = NodeOutputKey(node, n);
    for (size_t i = 0; i < frameCtx->availableFrames.size(); i++) {
        const auto &tmp = frameCtx->availableFrames[i];
        if (tmp.first == key) {
            tmp.second->add_ref();
            return tmp.second.get();
        }
    }
    return nullptr;
}

static void VS_CC freeFrame(const VSFrame *frame) VS_NOEXCEPT {
    if (frame)
        const_cast<VSFrame *>(frame)->release();
}

static void VS_CC freeNode(VSNode *clip) VS_NOEXCEPT {
    if (clip)
        clip->release();
}

static VSFrame *VS_CC newVideoFrame(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return new VSFrame(*format, width, height, propSrc, core);
}

static VSFrame *VS_CC newVideoFrame3(const vs3::VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    VSVideoFormat v4;
    if (core->VideoFormatFromV3(v4, format))
        return new VSFrame(v4, width, height, propSrc, core);
    else
        return nullptr;
}

static VSFrame *VS_CC newVideoFrame2(const VSVideoFormat *format, int width, int height, const VSFrame **planeSrc, const int *planes, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return new VSFrame(*format, width, height, planeSrc, planes, propSrc, core);
}

static VSFrame *VS_CC newVideoFrame23(const vs3::VSVideoFormat *format, int width, int height, const VSFrame **planeSrc, const int *planes, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    VSVideoFormat v4;
    if (core->VideoFormatFromV3(v4, format))
        return new VSFrame(v4, width, height, planeSrc, planes, propSrc, core);
    else
        return nullptr;
}

static VSFrame *VS_CC copyFrame(const VSFrame *frame, VSCore *core) VS_NOEXCEPT {
    assert(frame && core);
    return new VSFrame(*frame);
}

static void VS_CC copyFrameProps3(const VSFrame *src, VSFrame *dst, VSCore *core) VS_NOEXCEPT {
    assert(src && dst && core);
    dst->setProperties(src->getConstProperties());
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
        case vs3::fmFrameState:
            fm = fmFrameState;
            break;
        default:
            core->logFatal("Invalid filter mode specified for " + std::string(name));
    }
    core->createFilter3(in, out, name, init, reinterpret_cast<VSFilterGetFrame>(getFrame), free, fm, flags, instanceData, VAPOURSYNTH3_API_MAJOR);
}

static void VS_CC mapSetError(VSMap *map, const char *errorMessage) VS_NOEXCEPT {
    assert(map && errorMessage);
    map->setError(errorMessage ? errorMessage : "Error: no error specified");
}

static const char *VS_CC mapGetError(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    return map->getErrorMessage();
}

static void VS_CC setFilterError(const char *errorMessage, VSFrameContext *context) VS_NOEXCEPT {
    assert(errorMessage && context);
    context->setError(errorMessage);
}

static const VSVideoInfo *VS_CC getVideoInfo(VSNode *node) VS_NOEXCEPT {
    assert(node && node->getNodeType() == mtVideo);
    return &node->getVideoInfo();
}

static const vs3::VSVideoInfo *VS_CC getVideoInfo3(VSNode *c) VS_NOEXCEPT {
    assert(c);
    return &c->getVideoInfo3();
}

static void VS_CC setVideoInfo3(const vs3::VSVideoInfo *vi, int numOutputs, VSNode *c) VS_NOEXCEPT {
    assert(vi && numOutputs > 0 && c);
    c->setVideoInfo3(vi, numOutputs);
}

static const VSVideoFormat *VS_CC getVideoFrameFormat(const VSFrame *f) VS_NOEXCEPT {
    assert(f);
    return f->getVideoFormat();
}

static const vs3::VSVideoFormat *VS_CC getFrameFormat3(const VSFrame *f) VS_NOEXCEPT {
    assert(f);
    return f->getVideoFormatV3();
}

static int VS_CC getFrameWidth(const VSFrame *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->getWidth(plane);
}

static int VS_CC getFrameHeight(const VSFrame *f, int plane) VS_NOEXCEPT {
    assert(f);
    assert(plane >= 0);
    return f->getHeight(plane);
}

static const VSMap *VS_CC getFramePropertiesRO(const VSFrame *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->getConstProperties();
}

static VSMap *VS_CC getFramePropertiesRW(VSFrame *frame) VS_NOEXCEPT {
    assert(frame);
    return &frame->getProperties();
}

static int VS_CC mapNumKeys(const VSMap *map) VS_NOEXCEPT {
    assert(map);
    return static_cast<int>(map->size());
}

static const char *VS_CC mapGetKey(const VSMap *map, int index) VS_NOEXCEPT {
    assert(map);
    return map->key(static_cast<size_t>(index));
}

static int VS_CC mapNumElements(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    return val ? static_cast<int>(val->size()) : -1;
}

static int VS_CC mapGetType(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    return val ? val->type() : ptUnset;
}

static char VS_CC propGetType3(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(key);
    VSPropertyType pt = val ? val->type() : ptUnset;
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

static VSArrayBase *propGetShared(const VSMap *map, const char *key, int index, int *error, VSPropertyType propType) noexcept {
    assert(map && key && index >= 0);

    if (error)
        *error = peSuccess;

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

static int64_t VS_CC mapGetInt(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptInt);
    if (arr)
        return reinterpret_cast<const VSIntArray *>(arr)->at(index);
    else
        return 0;
}

static int VS_CC mapGetIntSaturated(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    return int64ToIntS(mapGetInt(map, key, index, error));
}

static double VS_CC mapGetFloat(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptFloat);
    if (arr)
        return reinterpret_cast<const VSFloatArray *>(arr)->at(index);
    else
        return 0;
}

static float VS_CC mapGetFloatSaturated(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    return doubleToFloatS(mapGetFloat(map, key, index, error));
}

static const char *VS_CC mapGetData(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return reinterpret_cast<const VSDataArray *>(arr)->at(index).data.c_str();
    else
        return nullptr;
}

static int VS_CC mapGetDataSize(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return static_cast<int>(reinterpret_cast<const VSDataArray *>(arr)->at(index).data.size());
    else
        return -1;
}

static int VS_CC mapGetDataTypeHint(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    VSArrayBase *arr = propGetShared(map, key, index, error, ptData);
    if (arr)
        return reinterpret_cast<const VSDataArray *>(arr)->at(index).typeHint;
    else
        return dtUnknown;
}

static VSNode *VS_CC mapGetNode(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    int dummyError;
    VSArrayBase *arr = propGetShared(map, key, index, &dummyError, ptVideoNode);
    if (arr) {
        VSNode *ref = reinterpret_cast<VSVideoNodeArray *>(arr)->at(index).get();
        ref->add_ref();
        if (error)
            *error = dummyError;
        return ref;
    } else {
        arr = propGetShared(map, key, index, error, ptAudioNode);
        if (arr) {
            VSNode *ref = reinterpret_cast<VSAudioNodeArray *>(arr)->at(index).get();
            ref->add_ref();
            return ref;
        } else {
            return nullptr;
        }
    }
}

static const VSFrame *VS_CC mapGetFrame(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    int dummyError;
    VSArrayBase *arr = propGetShared(map, key, index, &dummyError, ptVideoFrame);
    if (arr) {
        VSFrame *ref = reinterpret_cast<VSVideoFrameArray *>(arr)->at(index).get();
        ref->add_ref();
        if (error)
            *error = dummyError;
        return ref;
    } else {
        arr = propGetShared(map, key, index, error, ptAudioFrame);
        if (arr) {
            VSFrame *ref = reinterpret_cast<VSAudioFrameArray *>(arr)->at(index).get();
            ref->add_ref();
            return ref;
        } else {
            return nullptr;
        }
    }
}

static int VS_CC mapDeleteKey(VSMap *map, const char *key) VS_NOEXCEPT {
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


static int VS_CC mapSetEmpty(VSMap *map, const char *key, int type) VS_NOEXCEPT {
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

template<typename T, VSPropertyType propType>
bool propSetShared(VSMap *map, const char *key, const T &val, int append) {
    assert(map && key);
    if (append != maReplace && append != maAppend && append != vs3::paTouch)
        VS_FATAL_ERROR(("Invalid prop append mode given when setting key '" + std::string(key) + "'").c_str());

    if (!isValidVSMapKey(key))
        return false;
    std::string skey = key;

    if (append == maReplace) {
        VSArray<T, propType> *v = new VSArray<T, propType>();
        v->push_back(val);
        map->insert(key, v);
        return true;
    } else if (append == maAppend) {
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
        return !mapSetEmpty(map, key, propType);
    }
}

static int VS_CC mapSetInt(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT {
    return !propSetShared<int64_t, ptInt>(map, key, i, append);
}

static int VS_CC mapSetFloat(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT {
    return !propSetShared<double, ptFloat>(map, key, d, append);
}

static int VS_CC mapSetData(VSMap *map, const char *key, const char *d, int length, int type, int append) VS_NOEXCEPT {
    return !propSetShared<VSMapData, ptData>(map, key, { static_cast<VSDataTypeHint>(type), (length >= 0) ? std::string(d, length) : std::string(d) }, append);
}

static int VS_CC propSetData3(VSMap *map, const char *key, const char *d, int length, int append) VS_NOEXCEPT {
    return mapSetData(map, key, d, length, dtUnknown, append);
}

static int VS_CC mapSetNode(VSMap *map, const char *key, VSNode *node, int append) VS_NOEXCEPT {
    if (node == nullptr || node->getNodeType() == mtVideo)
        return !propSetShared<PVSNode, ptVideoNode>(map, key, { node, true }, append);
    else
        return !propSetShared<PVSNode, ptAudioNode>(map, key, { node, true }, append);
}

static int VS_CC mapConsumeNode(VSMap *map, const char *key, VSNode *node, int append) VS_NOEXCEPT {
    if (node == nullptr || node->getNodeType() == mtVideo)
        return !propSetShared<PVSNode, ptVideoNode>(map, key, {node, false}, append);
    else
        return !propSetShared<PVSNode, ptAudioNode>(map, key, {node, false}, append);
}

static int VS_CC mapSetFrame(VSMap *map, const char *key, const VSFrame *frame, int append) VS_NOEXCEPT {
    if (frame == nullptr || frame->getFrameType() == mtVideo)
        return !propSetShared<PVSFrame, ptVideoFrame>(map, key, { const_cast<VSFrame *>(frame), true }, append);
    else
        return !propSetShared<PVSFrame, ptAudioFrame>(map, key, { const_cast<VSFrame *>(frame), true }, append);
}

static int VS_CC mapConsumeFrame(VSMap *map, const char *key, const VSFrame *frame, int append) VS_NOEXCEPT {
    if (frame == nullptr || frame->getFrameType() == mtVideo)
        return !propSetShared<PVSFrame, ptVideoFrame>(map, key, {const_cast<VSFrame *>(frame), false}, append);
    else
        return !propSetShared<PVSFrame, ptAudioFrame>(map, key, {const_cast<VSFrame *>(frame), false}, append);
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

static void VS_CC copyMap(const VSMap *src, VSMap *dst) VS_NOEXCEPT {
    assert(src && dst);
    dst->copy(src);
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

static VSFunction *VS_CC mapGetFunction(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, index, error, ptFunction);
    if (arr) {
        VSFunction *ref = reinterpret_cast<const VSFunctionArray *>(arr)->at(index).get();
        ref->add_ref();
        return ref;
    } else {
        return nullptr;
    }
}

static int VS_CC mapSetFunction(VSMap *map, const char *key, VSFunction *func, int append) VS_NOEXCEPT {
    return !propSetShared<PVSFunction, ptFunction>(map, key, { func, true }, append);
}

static int VS_CC mapConsumeFunction(VSMap *map, const char *key, VSFunction *func, int append) VS_NOEXCEPT {
    return !propSetShared<PVSFunction, ptFunction>(map, key, {func, false}, append);
}

static void VS_CC callFunction(VSFunction *func, const VSMap *in, VSMap *out) VS_NOEXCEPT {
    assert(func && in && out);
    func->call(in, out);
}

static void VS_CC callFunction3(VSFunction *func, const VSMap *in, VSMap *out, VSCore *core, const vs3::VSAPI3 *vsapi) VS_NOEXCEPT {
    assert(func && in && out);
    func->call(in, out);
}

static VSFunction *VS_CC createFunction(VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core) VS_NOEXCEPT {
    assert(func && core);
    return new VSFunction(func, userData, free, core, VAPOURSYNTH_API_MAJOR);
}

static VSFunction *VS_CC createFunction3(vs3::VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core, const vs3::VSAPI3 *vsapi) VS_NOEXCEPT {
    assert(func && core && vsapi);
    return new VSFunction(reinterpret_cast<VSPublicFunction>(func), userData, free, core, VAPOURSYNTH3_API_MAJOR);
}

static void VS_CC freeFunction(VSFunction *f) VS_NOEXCEPT {
    if (f)
        f->release();
}

static void VS_CC queryCompletedFrame3(VSNode **node, int *n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && n && frameCtx);
    assert(false);
    *node = nullptr;
    *n = -1;
}

static void VS_CC releaseFrameEarly(VSNode *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(node && frameCtx);
    auto key = NodeOutputKey(node, n);
    for (size_t i = 0; i < frameCtx->reqList.size(); i++) {
        auto &tmp = frameCtx->availableFrames[i];
        if (tmp.first == key) {
            tmp.first = NodeOutputKey(nullptr, -1);
            tmp.second.reset();
        }
    }
}

void VS_CC cacheFrame(const VSFrame *frame, int n, VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(frame && n >= 0 && frameCtx);
    frameCtx->key.first->cacheFrame(frame, n);
}

static VSFunction *VS_CC addFunctionRef(VSFunction *func) VS_NOEXCEPT {
    assert(func);
    func->add_ref();
    return func;
}

static int64_t VS_CC setMaxCacheSize(int64_t bytes, VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->memory->set_limit(bytes);
}

static int VS_CC getOutputIndex(VSFrameContext *frameCtx) VS_NOEXCEPT {
    assert(frameCtx);
    assert(false);
    return 0;
}

static void VS_CC setMessageHandler(VSLogHandler handler, void *userData) VS_NOEXCEPT {
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

static const int64_t *VS_CC mapGetIntArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, 0, error, ptInt);
    if (arr) {
        return reinterpret_cast<const VSIntArray *>(arr)->getDataPointer();
    } else {
        return nullptr;
    }
}

static const double *VS_CC mapGetFloatArray(const VSMap *map, const char *key, int *error) VS_NOEXCEPT {
    const VSArrayBase *arr = propGetShared(map, key, 0, error, ptFloat);
    if (arr) {
        return reinterpret_cast<const VSFloatArray *>(arr)->getDataPointer();
    } else {
        return nullptr;
    }
}

static int VS_CC mapSetIntArray(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT {
    assert(map && key && size >= 0);
    if (size < 0)
        return 1;
    if (!isValidVSMapKey(key))
        return 1;
    map->insert(key, new VSIntArray(i, size));
    return 0;
}

static int VS_CC mapSetFloatArray(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT {
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

static VSLogHandle *VS_CC addLogHandler(VSLogHandler handler, VSLogHandlerFree free, void *userData, VSCore *core) VS_NOEXCEPT {
    assert(handler && core);
    return core->addLogHandler(handler, free, userData);
}

static int VS_CC removeLogHandler(VSLogHandle *handle, VSCore *core) VS_NOEXCEPT {
    assert(handle && core);
    return core->removeLogHandler(reinterpret_cast<VSLogHandle *>(handle));
}

static void VS_CC logMessage3(int msgType, const char *msg) VS_NOEXCEPT {
    vsLog3(static_cast<vs3::VSMessageType>(msgType), "%s", msg);
}

static int VS_CC addMessageHandler3(VSLogHandler handler, VSLogHandlerFree free, void *userData) VS_NOEXCEPT {
    return vsAddMessageHandler3(handler, free, userData);
}

static int VS_CC removeMessageHandler3(int id) VS_NOEXCEPT {
    return vsRemoveMessageHandler3(id);
}

static void VS_CC getCoreInfo2(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT {
    assert(core && info);
    core->getCoreInfo(*info);
}

static void VS_CC createVideoFilter(VSMap *out, const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(out && name && vi && getFrame && core);
    core->createVideoFilter(out, name, vi, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
}

static VSNode *VS_CC createVideoFilter2(const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(name && vi && getFrame && core);
    return core->createVideoFilter(name, vi, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
}

static void VS_CC createAudioFilter(VSMap *out, const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(out && name && ai && getFrame && core);
    core->createAudioFilter(out, name, ai, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
}

static VSNode *VS_CC createAudioFilter2(const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(name && ai && getFrame && core);
    return core->createAudioFilter(name, ai, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
}

static int VS_CC setLinearFilter(VSNode *node) VS_NOEXCEPT {
    return node->setLinear();
}

static VSFrame *VS_CC newAudioFrame(const VSAudioFormat *format, int numSamples, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core && numSamples > 0);
    return new VSFrame(*format, numSamples, propSrc, core);
}

static VSFrame *VS_CC newAudioFrame2(const VSAudioFormat *format, int numSamples, const VSFrame **channelSrc, const int *channels, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core && numSamples > 0 && channelSrc && channels);
    return new VSFrame(*format, numSamples, channelSrc, channels, propSrc, core);
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

static int VS_CC getVideoFormatByID(VSVideoFormat *format, uint32_t id, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return core->getVideoFormatByID(*format, id);
}

static int VS_CC getAudioFormatName(const VSAudioFormat *format, char *buffer) VS_NOEXCEPT {
    assert(format && buffer);
    return VSCore::getAudioFormatName(*format, buffer);
}

static int VS_CC getVideoFormatName(const VSVideoFormat *format, char *buffer) VS_NOEXCEPT {
    assert(format && buffer);
    return VSCore::getVideoFormatName(*format, buffer);
}

static const VSAudioInfo *VS_CC getAudioInfo(VSNode *node) VS_NOEXCEPT {
    assert(node && node->getNodeType() == mtAudio);
    return &node->getAudioInfo();
}

static const VSAudioFormat *VS_CC getAudioFrameFormat(const VSFrame *f) VS_NOEXCEPT {
    return f->getAudioFormat();
}

static int VS_CC getNodeType(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->getNodeType();
}

static void VS_CC setCacheMode(VSNode *node, int mode) VS_NOEXCEPT {
    assert(node && mode >= cmAuto && mode <= cmForceEnable);
    node->setCacheMode(mode);
}

static void VS_CC setCacheOptions(VSNode *node, int fixedSize, int maxSize, int maxHistorySize) VS_NOEXCEPT {
    assert(node);
    node->setCacheOptions(fixedSize, maxSize, maxHistorySize);
}

static int VS_CC getFrameType(const VSFrame *f) VS_NOEXCEPT {
    assert(f);
    return f->getFrameType();
}

static int VS_CC getFrameLength(const VSFrame *f) VS_NOEXCEPT {
    assert(f);
    return f->getFrameLength();
}

static int VS_CC getAPIVersion(void) VS_NOEXCEPT{
    return VAPOURSYNTH_API_VERSION;
}

static const char *VS_CC getNodeCreationFunctionName(VSNode *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->getCreationFunctionName(level);
}

static const VSMap *VS_CC getNodeCreationFunctionArguments(VSNode *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->getCreationFunctionArguments(level);
}

static const char *VS_CC getNodeName(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->getName().c_str();
}

static int VS_CC getNodeFilterMode(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->getFilterMode();
}

static int64_t VS_CC getNodeFilterTime(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->getFilterTime();
}

static const VSFilterDependency *VS_CC getNodeDependencies(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->getDependencies();
}

static int VS_CC getNumNodeDependencies(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return static_cast<int>(node->getNumDependencies());
}

const VSPLUGINAPI vs_internal_vspapi {
    &getAPIVersion,
    &configPlugin,
    &registerFunction
};

const VSAPI vs_internal_vsapi = {
    &createVideoFilter,
    &createVideoFilter2,
    &createAudioFilter,
    &createAudioFilter2,
    &setLinearFilter,
    &setCacheMode,
    &setCacheOptions,

    &freeNode,
    &addNodeRef,
    &getNodeType,
    &getVideoInfo,
    &getAudioInfo,

    &newVideoFrame,
    &newVideoFrame2,
    &newAudioFrame,
    &newAudioFrame2,
    &freeFrame,
    &addFrameRef,
    &copyFrame,
    &getFramePropertiesRO,
    &getFramePropertiesRW,

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
    &getVideoFormatByID,

    &getFrame,
    &getFrameAsync,
    &getFrameFilter,
    &requestFrameFilter,
    &releaseFrameEarly,
    &cacheFrame,
    &setFilterError,

    &createFunction,
    &freeFunction,
    &addFunctionRef,
    &callFunction,

    &createMap,
    &freeMap,
    &clearMap,
    &copyMap,

    &mapSetError,
    &mapGetError,

    &mapNumKeys,
    &mapGetKey,
    &mapDeleteKey,
    &mapNumElements,
    &mapGetType,
    &mapSetEmpty,

    &mapGetInt,
    &mapGetIntSaturated,
    &mapGetIntArray,
    &mapSetInt,
    &mapSetIntArray,

    &mapGetFloat,
    &mapGetFloatSaturated,
    &mapGetFloatArray,
    &mapSetFloat,
    &mapSetFloatArray,

    &mapGetData,
    &mapGetDataSize,
    &mapGetDataTypeHint,
    &mapSetData,

    &mapGetNode,
    &mapSetNode,
    &mapConsumeNode,

    &mapGetFrame,
    &mapSetFrame,
    &mapConsumeFrame,

    &mapGetFunction,
    &mapSetFunction,
    &mapConsumeFunction,

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
    &addLogHandler,
    &removeLogHandler,

    &getNodeCreationFunctionName,
    &getNodeCreationFunctionArguments,
    &getNodeName,
    &getNodeFilterMode,
    &getNodeFilterTime,
    &getNodeDependencies,
    &getNumNodeDependencies
};

const vs3::VSAPI3 vs_internal_vsapi3 = {
    &createCore3,
    &freeCore,
    &getCoreInfo3,

    &addFrameRef,
    &addNodeRef,
    &addFunctionRef,

    &freeFrame,
    &freeNode,
    &freeFunction,

    &newVideoFrame3,
    &copyFrame,
    &copyFrameProps3,
    &registerFunction3,
    &getPluginByID,
    &getPluginByNamespace,
    &getPlugins3,
    &getFunctions3,
    &createFilter3,
    &mapSetError,
    &mapGetError,
    &setFilterError,
    &invoke,
    &getFormatPreset3,
    &registerFormat3,
    &getFrame,
    &getFrameAsync,
    &getFrameFilter,
    &requestFrameFilter,
    &queryCompletedFrame3,
    &releaseFrameEarly,

    &getStride3,
    &getReadPtr,
    &getWritePtr,

    &createFunction3,
    &callFunction3,

    &createMap,
    &freeMap,
    &clearMap,

    &getVideoInfo3,
    &setVideoInfo3,
    &getFrameFormat3,
    &getFrameWidth,
    &getFrameHeight,
    &getFramePropertiesRO,
    &getFramePropertiesRW,

    &mapNumKeys,
    &mapGetKey,
    &mapNumElements,
    &propGetType3,
    &mapGetInt,
    &mapGetFloat,
    &mapGetData,
    &mapGetDataSize,
    &mapGetNode,
    &mapGetFrame,
    &mapGetFunction,
    &mapDeleteKey,
    &mapSetInt,
    &mapSetFloat,
    &propSetData3,
    &mapSetNode,
    &mapSetFrame,
    &mapSetFunction,

    &setMaxCacheSize,
    &getOutputIndex,
    &newVideoFrame23,

    &setMessageHandler,
    &setThreadCount,

    &getPluginPath,

    &mapGetIntArray,
    &mapGetFloatArray,
    &mapSetIntArray,
    &mapSetFloatArray,

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
