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
#include "vsvulkanframe.h"
#include "vsvulkanshader.h"
#include "VSVulkan4.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace vsh;

static constexpr int64_t staticZero = 0;
static constexpr int64_t staticOne = 1;

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
    VSFrameContext *ctx = new VSFrameContext(n, clip, fdc, userData, true, false);

    if (n < 0 || n >= numFrames)
        ctx->setError("Invalid frame number " + std::to_string(n) + " requested, clip only has " + std::to_string(numFrames) + " frames");

    clip->getFrame(ctx);
}

struct GetFrameWaiter {
    std::mutex b;
    std::condition_variable a;
    const VSFrame *r = nullptr;
    bool done = false;
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
    g->done = true;
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
    node->getFrame(new VSFrameContext(n, node, &frameWaiterCallback, &g, false, true));
    g.a.wait(l, [&g] { return g.done; });
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

static const char *remapColorRange(const char *key) {
    return !strcmp(key, "_ColorRange") ? "_Range" : key;
}

static int VS_CC mapNumElements(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(remapColorRange(key));
    return val ? static_cast<int>(val->size()) : -1;
}

static int VS_CC mapGetType(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(remapColorRange(key));
    return val ? val->type() : ptUnset;
}

static char VS_CC propGetType3(const VSMap *map, const char *key) VS_NOEXCEPT {
    assert(map && key);
    VSArrayBase *val = map->find(remapColorRange(key));
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

static int64_t flipRangeProperty(int64_t range) {
    if (range == 0)
        return 1;
    else if (range == 1)
        return 0;
    else
        return range;
}

static int64_t VS_CC mapGetInt(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT {
    bool remapColorRange = !strcmp(key, "_ColorRange");
    VSArrayBase *arr = propGetShared(map, remapColorRange ? "_Range" : key, index, error, ptInt);
    if (arr)
        return remapColorRange ? flipRangeProperty(reinterpret_cast<const VSIntArray *>(arr)->at(index)) : reinterpret_cast<const VSIntArray *>(arr)->at(index);
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
    bool result = map->erase(key);
    if (!strcmp(key, "_ColorRange"))
        result = map->erase("_Range") || result;
    return result;
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

    std::string skey = (type == ptInt) ? remapColorRange(key) : key;
    if (map->find(skey))
        return 1;

    switch (type) {
        case ptInt:
            map->insert(skey, new VSIntArray);
            break;
        case ptFloat:
            map->insert(skey, new VSFloatArray);
            break;
        case ptData:
            map->insert(skey, new VSDataArray);
            break;
        case ptVideoNode:
            map->insert(skey, new VSVideoNodeArray);
            break;
        case ptAudioNode:
            map->insert(skey, new VSAudioNodeArray);
            break;
        case ptVideoFrame:
            map->insert(skey, new VSVideoFrameArray);
            break;
        case ptAudioFrame:
            map->insert(skey, new VSAudioFrameArray);
            break;
        case ptFunction:
            map->insert(skey, new VSFunctionArray);
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
    bool remapRange = !strcmp(key, "_ColorRange");
    const char *key_ = remapRange ? "_Range" : key;
    return !propSetShared<int64_t, ptInt>(map, key_, remapRange ? flipRangeProperty(i) : i, append);
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
    for (size_t i = 0; i < frameCtx->availableFrames.size(); i++) {
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
    if (bytes <= 0)
        return core->memory->limit();
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
    bool remapRange = !strcmp(key, "_ColorRange");
    const VSArrayBase *arr = propGetShared(map, remapRange ? "_Range" : key, 0, error, ptInt);
    if (arr) {
        if (remapRange) {
            auto *intArr = reinterpret_cast<const VSIntArray *>(arr);
            if (intArr->size() == 1)
                return flipRangeProperty(intArr->at(0)) == 1 ? &staticOne : &staticZero;
            else
                return intArr->getDataPointer();

        } else {
            return reinterpret_cast<const VSIntArray *>(arr)->getDataPointer();
        }
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
    bool remapRange = !strcmp(key, "_ColorRange");
    if (remapRange) {
        std::vector<int64_t> flipped;
        flipped.resize(size);
        for (int j = 0; j < size; j++)
            flipped[j] = flipRangeProperty(i[j]);
        map->insert("_Range", new VSIntArray(flipped.data(), size));
    } else {
        map->insert(key, new VSIntArray(i, size));
    }
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

static void VS_CC getCoreInfo(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT {
    assert(core && info);
    core->getCoreInfo(*info);
}

static void VS_CC getCoreInfo2(VSCore *core, VSCoreInfo2 *info) VS_NOEXCEPT {
    assert(core && info);
    core->getCoreInfo2(*info);
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

static void VS_CC createVideoFilterEx(VSMap *out, const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(out && name && vi && getFrame && core);
    if (flags & ~static_cast<int>(ffGPUOutput)) {
        vs_internal_vsapi.mapSetError(out, (std::string(name) + ": unknown filter flags passed to createVideoFilterEx").c_str());
        return;
    }
    core->createVideoFilter(out, name, vi, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
    if ((flags & ffGPUOutput) && !vs_internal_vsapi.mapGetError(out)) {
        int numElems = vs_internal_vsapi.mapNumElements(out, "clip");
        VSNode *node = vs_internal_vsapi.mapGetNode(out, "clip", numElems - 1, nullptr);
        node->setGPUOutput();
        vs_internal_vsapi.freeNode(node);
    }
}

static VSNode *VS_CC createVideoFilterEx2(const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT {
    assert(name && vi && getFrame && core);
    if (flags & ~static_cast<int>(ffGPUOutput)) {
        core->logMessage(mtCritical, std::string(name) + ": unknown filter flags passed to createVideoFilterEx2");
        return nullptr;
    }
    VSNode *node = core->createVideoFilter(name, vi, getFrame, free, static_cast<VSFilterMode>(filterMode), dependencies, numDeps, instanceData, VAPOURSYNTH_API_MAJOR);
    if (node && (flags & ffGPUOutput))
        node->setGPUOutput();
    return node;
}

static int VS_CC setLinearFilter(VSNode *node) VS_NOEXCEPT {
    return node->setLinear();
}

static const VSVULKANAPI *VS_CC getVulkanAPIImpl(int version) VS_NOEXCEPT;

static int VS_CC getNodeResidency(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return node->isGPUOutput() ? nrGPU : nrCPU;
}

static int VS_CC getFrameResidency(const VSFrame *frame) VS_NOEXCEPT {
    assert(frame);
    return frame->isGPUResident() ? nrGPU : nrCPU;
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

static const char *VS_CC getNodeCreationPluginID(VSNode *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->getNodeCreationPluginID(level);
}

static const char *VS_CC getNodeCreationPluginNS(VSNode *node, int level) VS_NOEXCEPT {
    assert(node);
    return node->getNodeCreationPluginNS(level);
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

static int64_t VS_CC getNodeProcessingTime(VSNode *node, int reset) VS_NOEXCEPT {
    assert(node);
    return node->getProcessingTime(!!reset);
}

static int64_t VS_CC getFreedNodeProcessingTime(VSCore *core, int reset) VS_NOEXCEPT {
    assert(core);
    return core->getFreedNodeProcessingTime(!!reset);
}

static int VS_CC getNumNodeDependencies(VSNode *node) VS_NOEXCEPT {
    assert(node);
    return static_cast<int>(node->getNumDependencies());
}

static const VSFilterDependency *VS_CC getNodeDependency(VSNode *node, int index) VS_NOEXCEPT {
    return node->getDependency(index);
}

static void VS_CC clearNodeCache(VSNode *node) VS_NOEXCEPT {
    assert(node);
    node->clearCache(false);
}

static void VS_CC clearCoreCaches(VSCore *core) VS_NOEXCEPT {
    assert(core);
    core->clearCaches(false);
}

static int VS_CC getCoreNodeTiming(VSCore *core) VS_NOEXCEPT {
    assert(core);
    return core->getNodeTiming();
}

static void VS_CC setCoreNodeTiming(VSCore *core, int enable) VS_NOEXCEPT {
    assert(core);
    core->setNodeTiming(!!enable);
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
    &getCoreInfo,
    &getAPIVersion,

    &logMessage,
    &addLogHandler,
    &removeLogHandler,

    &clearNodeCache,
    &clearCoreCaches,

    &getNodeName,
    &getNodeFilterMode,
    &getNumNodeDependencies,
    &getNodeDependency,

    &getCoreNodeTiming,
    &setCoreNodeTiming,
    &getNodeProcessingTime,
    &getFreedNodeProcessingTime,

    &getCoreInfo2,

    &createVideoFilterEx,
    &createVideoFilterEx2,
    &getVulkanAPIImpl,
    &getNodeResidency,
    &getFrameResidency,

    &getNodeCreationFunctionName,
    &getNodeCreationPluginID,
    &getNodeCreationPluginNS,
    &getNodeCreationFunctionArguments
};

//////////////////////////////////////////
// The Vulkan API surface, raw handles plus per plane buffer access; see VSVulkan4.h

static void copyVulkanError(const std::string &message, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    if (errorMessage && errorMessageSize > 0)
        snprintf(errorMessage, static_cast<size_t>(errorMessageSize), "%s", message.c_str());
}

static int VS_CC vkSetVulkanDevice(VSCore *core, int deviceIndex, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core);
    std::string err;
    if (!core->setVulkanDevice(deviceIndex, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    return 0;
}

static int VS_CC vkGetVulkanHandles(VSCore *core, VSVulkanCoreHandles *handles, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core && handles);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    handles->instance = dev->instance();
    handles->physicalDevice = dev->physicalDevice();
    handles->device = dev->device();
    handles->getInstanceProcAddr = dev->getInstanceProcAddr();
    handles->computeQueueFamily = dev->computeQueue().familyIndex();
    handles->computeQueueIndex = dev->computeQueue().queueIndex();
    handles->transferQueueFamily = dev->transferQueue().familyIndex();
    handles->transferQueueIndex = dev->transferQueue().queueIndex();
    return 0;
}

static int VS_CC vkGetVulkanCoreInfo(VSCore *core, VSVulkanCoreInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core && info);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    snprintf(info->deviceName, sizeof(info->deviceName), "%s", dev->properties().deviceName);
    const VkPhysicalDeviceMemoryProperties &memProps = dev->memoryProperties();
    VkDeviceSize largest = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && memProps.memoryHeaps[i].size > largest)
            largest = memProps.memoryHeaps[i].size;
    }
    info->deviceMemory = static_cast<int64_t>(largest);
    info->budget = static_cast<int64_t>(dev->memoryBudget());
    info->allocated = static_cast<int64_t>(core->memory->gpu_allocated_bytes());
    info->limit = static_cast<int64_t>(core->memory->gpu_limit());
    memcpy(info->deviceUUID, dev->deviceUUID(), VK_UUID_SIZE);
    memcpy(info->deviceLUID, dev->deviceLUID(), VK_LUID_SIZE);
    info->deviceNodeMask = dev->deviceNodeMask();
    info->deviceLUIDValid = dev->deviceLUIDValid() ? 1 : 0;
    info->unifiedMemory = dev->unifiedMemory() ? 1 : 0;
    info->exportHandleType = static_cast<int>(dev->exportHandleType());
    info->semaphoreExportHandleType = static_cast<int>(dev->semaphoreExportHandleType());
    return 0;
}

static int64_t VS_CC vkSetMaxVRAMUse(int64_t bytes, VSCore *core) VS_NOEXCEPT {
    assert(core);
    if (bytes <= 0)
        return static_cast<int64_t>(core->memory->gpu_limit());
    int64_t limit = static_cast<int64_t>(core->memory->set_gpu_limit(static_cast<size_t>(bytes)));
    /* The in-flight retention budget follows the limit; a no-op until the device exists,
       whose creation derives it from the same place. */
    core->refreshVulkanExecBudget();
    return limit;
}

static void VS_CC vkLockVulkanQueue(VSCore *core, int queue) VS_NOEXCEPT {
    assert(core);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev)
        return;
    (queue == vqTransfer ? dev->transferQueue() : dev->computeQueue()).lock();
}

static void VS_CC vkUnlockVulkanQueue(VSCore *core, int queue) VS_NOEXCEPT {
    assert(core);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev)
        return;
    (queue == vqTransfer ? dev->transferQueue() : dev->computeQueue()).unlock();
}

static VSFrame *VS_CC vkNewGPUVideoFrame(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT {
    assert(format && core);
    return new VSFrame(*format, width, height, propSrc, core, true);
}

static int VS_CC vkGetGPUPlane(const VSFrame *frame, int plane, VSVulkanPlaneInfo *info) VS_NOEXCEPT {
    assert(frame && info);
    const VSVulkanPlane *gpuPlane = frame->getGPUPlane(plane);
    if (!gpuPlane)
        return 1;
    info->buffer = gpuPlane->buffer.buffer;
    info->bufferSize = gpuPlane->buffer.size;
    info->readySemaphore = gpuPlane->readyTimeline ? gpuPlane->readyTimeline->semaphore() : VK_NULL_HANDLE;
    info->readyValue = gpuPlane->readyValue;
    return 0;
}

static void VS_CC vkSetGPUPlaneProducer(VSFrame *frame, int plane, VSGPUTimeline *timeline, uint64_t value) VS_NOEXCEPT {
    assert(frame);
    VSVulkanPlane *gpuPlane = frame->getGPUPlane(plane);
    if (!gpuPlane)
        return;
    setPlaneProducer(*gpuPlane, reinterpret_cast<VSVulkanTimeline *>(timeline), value);
}

static const VSVulkanFunctions *VS_CC vkGetVulkanFunctions(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    return &dev->vk;
}

static VSGPUBuffer *VS_CC vkCreateGPUBuffer(VSCore *core, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags,
    VSVulkanBufferInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core && info);
    if (size == 0) {
        copyVulkanError("A GPU buffer needs a nonzero size", errorMessage, errorMessageSize);
        return nullptr;
    }
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    auto handle = std::make_unique<VSGPUBuffer>();
    if (!dev->createBufferPooled(handle->buffer, size, usage, requiredFlags, preferredFlags, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    /* The reference makes a late destroy free memory instead of crashing, same as frames;
       being late on purpose is still wrong, per the destruction rule in the header. */
    handle->device = dev;
    dev->addRef();
    info->buffer = handle->buffer.buffer;
    info->address = handle->buffer.address;
    info->mapped = handle->buffer.mapped;
    info->size = handle->buffer.size;
    info->memoryFlags = handle->buffer.memoryFlags;
    return handle.release();
}

static void VS_CC vkDestroyGPUBuffer(VSGPUBuffer *buffer) VS_NOEXCEPT {
    if (!buffer)
        return;
    buffer->device->destroyBuffer(buffer->buffer);
    buffer->device->release();
    delete buffer;
}

static int VS_CC vkExportGPUPlane(const VSFrame *frame, int plane, VSVulkanExportedMemory *out,
    char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(frame && out);
    const VSVulkanPlane *gpuPlane = frame->getGPUPlane(plane);
    VSVulkanDevice *dev = frame->getGPUDevice();
    if (!gpuPlane || !dev) {
        copyVulkanError("The frame is not GPU resident or the plane does not exist", errorMessage, errorMessageSize);
        return 1;
    }
    if (!dev->exportHandleType()) {
        copyVulkanError("Memory export is not available on this device", errorMessage, errorMessageSize);
        return 1;
    }
    const VSVulkanAllocator::Block *block = gpuPlane->buffer.poolBlock;
    if (!block || !block->exportable) {
        copyVulkanError("The plane is not backed by exportable pooled memory", errorMessage, errorMessageSize);
        return 1;
    }
    std::string err;
    intptr_t handle = 0;
    if (!dev->exportMemory(gpuPlane->buffer.memory, handle, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    out->memoryId = block->exportId;
    out->memorySize = block->size;
    out->offset = gpuPlane->buffer.poolOffset;
    out->size = gpuPlane->buffer.size;
    out->handleType = static_cast<int>(dev->exportHandleType());
    out->handle = handle;
    return 0;
}

static int VS_CC vkWaitGPUFrame(const VSFrame *frame, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(frame);
    VSVulkanDevice *dev = frame->getGPUDevice();
    if (!dev) {
        copyVulkanError("The frame is not GPU resident", errorMessage, errorMessageSize);
        return 1;
    }
    /* The producer pairs ride the flush submission as device side waits, which is what pulls
       the producers' writes into the availability operation's scope; deduplicated to the
       highest value per timeline. */
    const VSVideoFormat *fmt = frame->getVideoFormat();
    VkSemaphore sems[3] = {};
    uint64_t values[3] = {};
    uint32_t waitCount = 0;
    for (int p = 0; p < fmt->numPlanes; p++) {
        const VSVulkanPlane *gpuPlane = frame->getGPUPlane(p);
        if (!gpuPlane || !gpuPlane->readyTimeline)
            continue;
        VkSemaphore planeSem = gpuPlane->readyTimeline->semaphore();
        uint32_t w = 0;
        for (; w < waitCount; w++) {
            if (sems[w] == planeSem) {
                if (gpuPlane->readyValue > values[w])
                    values[w] = gpuPlane->readyValue;
                break;
            }
        }
        if (w == waitCount) {
            sems[waitCount] = planeSem;
            values[waitCount] = gpuPlane->readyValue;
            waitCount++;
        }
    }
    std::string err;
    if (!dev->flushDeviceWrites(sems, values, waitCount, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    return 0;
}

static int VS_CC vkExportGPUSemaphore(VSCore *core, VkSemaphore semaphore, VSVulkanExportedSemaphore *out,
    char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core && out);
    if (!semaphore) {
        copyVulkanError("Cannot export a null semaphore", errorMessage, errorMessageSize);
        return 1;
    }
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    intptr_t handle = 0;
    if (!dev->exportSemaphore(semaphore, handle, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    out->handleType = static_cast<int>(dev->semaphoreExportHandleType());
    out->handle = handle;
    return 0;
}

static VSGPUShader *VS_CC vkCompileGPUShader(VSCore *core, int language, const char *source, char *errorLog, int errorLogSize) VS_NOEXCEPT {
    assert(core && source);
    std::string err;
    auto code = core->compileShaderCached(language, source, err);
    if (!code) {
        copyVulkanError(err, errorLog, errorLogSize);
        return nullptr;
    }
    return new VSGPUShader{ std::move(code) };
}

static const uint32_t *VS_CC vkGetGPUShaderCode(const VSGPUShader *shader, size_t *sizeInBytes) VS_NOEXCEPT {
    assert(shader);
    if (sizeInBytes)
        *sizeInBytes = shader->code->size() * sizeof(uint32_t);
    return shader->code->data();
}

static void VS_CC vkFreeGPUShader(VSGPUShader *shader) VS_NOEXCEPT {
    delete shader;
}

/* Release callbacks for whatever a context was told to keep alive; both run once the
   submission that used them has completed. */
static void releaseRetainedFrame(void *object) {
    static_cast<VSFrame *>(object)->release();
}

static void releaseRetainedBuffer(void *object) {
    VSGPUBuffer *buffer = static_cast<VSGPUBuffer *>(object);
    buffer->device->destroyBuffer(buffer->buffer);
    buffer->device->release();
    delete buffer;
}

static VSGPUExecPool *VS_CC vkCreateGPUExecPool(VSCore *core, int queue,
    char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    auto pool = std::make_unique<VSGPUExecPool>();
    VSVulkanQueue &q = (queue == vqTransfer) ? dev->transferQueue() : dev->computeQueue();
    /* The context count is core knowledge, not filter knowledge: worker threads bound how
       many recordings can even be concurrent, two is the floor that overlaps recording with
       execution at all, and past eight a single node is a fan-in point where extra depth
       just queues. Contexts are a command pool and buffer each — cheap on purpose, since
       the memory queued submissions pin is bounded separately, in bytes, by the device's
       admission gate. Sized from the thread count at creation; later setThreadCount calls
       do not resize existing pools. */
    size_t threads = core->threadPool->threadCount();
    uint32_t contextCount = static_cast<uint32_t>(threads < 2 ? 2 : (threads > 8 ? 8 : threads));
    if (!pool->pool.init(*dev, q, contextCount, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    /* Same late destroy safety net frames and buffers have. */
    pool->device = dev;
    dev->addRef();
    return pool.release();
}

static void VS_CC vkFreeGPUExecPool(VSGPUExecPool *pool) VS_NOEXCEPT {
    if (!pool)
        return;
    VSVulkanDevice *dev = pool->device;
    delete pool; /* the exec pool destructor drains the GPU and releases what it holds */
    dev->release();
}

static VSGPUExecContext *VS_CC vkGPUExecAcquire(VSGPUExecPool *pool, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(pool);
    std::string err;
    VSVulkanExecContext *inner = pool->pool.acquire(err);
    if (!inner) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    auto ctx = std::make_unique<VSGPUExecContext>();
    ctx->owner = pool;
    ctx->context = inner;
    return ctx.release();
}

static VkCommandBuffer VS_CC vkGPUExecCommandBuffer(VSGPUExecContext *context) VS_NOEXCEPT {
    assert(context);
    return context->context->commandBuffer();
}

static void VS_CC vkGPUExecReadsFrame(VSGPUExecContext *context, const VSFrame *frame) VS_NOEXCEPT {
    assert(context && frame);
    const VSVideoFormat *fmt = frame->getVideoFormat();
    for (int p = 0; fmt && p < fmt->numPlanes; p++) {
        const VSVulkanPlane *plane = frame->getGPUPlane(p);
        if (plane)
            context->waits.add(plane->readyTimeline, plane->readyValue);
    }
    /* The context's own reference, so the caller's lifetime stays its own business. The
       frame's bytes count against the device's in-flight retention budget while queued. */
    VSFrame *owned = const_cast<VSFrame *>(frame);
    owned->add_ref();
    context->owner->pool.retain(*context->context, releaseRetainedFrame, owned,
        frame->isGPUResident() ? owned->totalByteSize() : 0);
}

static void VS_CC vkGPUExecWritesPlane(VSGPUExecContext *context, VSFrame *frame, int plane) VS_NOEXCEPT {
    assert(context && frame);
    context->publish.push_back({ frame, plane });
}

static void VS_CC vkGPUExecUsesBuffer(VSGPUExecContext *context, VSGPUBuffer *buffer) VS_NOEXCEPT {
    assert(context);
    if (buffer)
        context->owner->pool.retain(*context->context, releaseRetainedBuffer, buffer, buffer->buffer.poolSize);
}

static int VS_CC vkGPUExecSubmit(VSGPUExecContext *context, uint64_t *signaledValue, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(context);
    std::unique_ptr<VSGPUExecContext> owned(context); /* consumed either way */
    std::string err;
    uint64_t value = 0;
    if (!context->owner->pool.submit(*context->context, err, &value, context->waits.data(), context->waits.size())) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    if (signaledValue)
        *signaledValue = value;
    for (const auto &target : context->publish) {
        VSVulkanPlane *plane = target.frame->getGPUPlane(target.plane);
        if (plane)
            setPlaneProducer(*plane, context->owner->pool.timelineObject(), value);
    }
    return 0;
}

static void VS_CC vkGPUExecAbandon(VSGPUExecContext *context) VS_NOEXCEPT {
    if (!context)
        return;
    std::unique_ptr<VSGPUExecContext> owned(context);
    context->owner->pool.abandon(*context->context);
}

static VSGPUTimeline *VS_CC vkGPUExecPoolTimeline(VSGPUExecPool *pool) VS_NOEXCEPT {
    assert(pool);
    return reinterpret_cast<VSGPUTimeline *>(pool->pool.timelineObject());
}

static VSGPUTimeline *VS_CC vkCreateGPUTimeline(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(core);
    std::string err;
    VSVulkanDevice *dev = core->vulkanDevice(err);
    if (!dev) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    VSVulkanTimeline *timeline = VSVulkanTimeline::create(*dev, err);
    if (!timeline) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return nullptr;
    }
    return reinterpret_cast<VSGPUTimeline *>(timeline);
}

static void VS_CC vkFreeGPUTimeline(VSGPUTimeline *timeline) VS_NOEXCEPT {
    if (timeline)
        reinterpret_cast<VSVulkanTimeline *>(timeline)->release();
}

static void VS_CC vkAddGPUTimelineRef(VSGPUTimeline *timeline) VS_NOEXCEPT {
    if (timeline)
        reinterpret_cast<VSVulkanTimeline *>(timeline)->addRef();
}

static VkSemaphore VS_CC vkGetGPUTimelineSemaphore(VSGPUTimeline *timeline) VS_NOEXCEPT {
    return timeline ? reinterpret_cast<VSVulkanTimeline *>(timeline)->semaphore() : VK_NULL_HANDLE;
}

static int VS_CC vkGPUExecPoolWaitIdle(VSGPUExecPool *pool, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    assert(pool);
    std::string err;
    if (!pool->pool.waitAll(err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return 1;
    }
    return 0;
}

static int VS_CC vkEnumerateVulkanDevices(VSVulkanDeviceListEntry *entries, int maxEntries, char *errorMessage, int errorMessageSize) VS_NOEXCEPT {
    std::vector<VSVulkanDeviceInfo> devices;
    std::string err;
    if (!VSVulkanDevice::enumerateDevices(devices, err)) {
        copyVulkanError(err, errorMessage, errorMessageSize);
        return -1;
    }
    for (int i = 0; i < maxEntries && i < static_cast<int>(devices.size()); i++) {
        VSVulkanDeviceListEntry &entry = entries[i];
        snprintf(entry.deviceName, sizeof(entry.deviceName), "%s", devices[i].name.c_str());
        entry.apiVersion = devices[i].apiVersion;
        entry.deviceType = static_cast<int>(devices[i].type);
        entry.deviceMemory = static_cast<int64_t>(devices[i].deviceLocalMemory);
        entry.usable = devices[i].usable ? 1 : 0;
        snprintf(entry.unusableReason, sizeof(entry.unusableReason), "%s", devices[i].reason.c_str());
        memcpy(entry.deviceUUID, devices[i].uuid, VK_UUID_SIZE);
        memcpy(entry.deviceLUID, devices[i].luid, VK_LUID_SIZE);
        entry.deviceNodeMask = devices[i].nodeMask;
        entry.deviceLUIDValid = devices[i].luidValid ? 1 : 0;
    }
    return static_cast<int>(devices.size());
}

const VSVULKANAPI vs_internal_vsvulkanapi = {
    &vkSetVulkanDevice,
    &vkGetVulkanHandles,
    &vkGetVulkanCoreInfo,
    &vkSetMaxVRAMUse,
    &vkLockVulkanQueue,
    &vkUnlockVulkanQueue,
    &vkNewGPUVideoFrame,
    &vkGetGPUPlane,
    &vkSetGPUPlaneProducer,
    &vkEnumerateVulkanDevices,
    &vkGetVulkanFunctions,
    &vkCreateGPUBuffer,
    &vkDestroyGPUBuffer,
    &vkExportGPUPlane,
    &vkWaitGPUFrame,
    &vkExportGPUSemaphore,
    &vkCompileGPUShader,
    &vkGetGPUShaderCode,
    &vkFreeGPUShader,
    &vkCreateGPUExecPool,
    &vkFreeGPUExecPool,
    &vkGPUExecAcquire,
    &vkGPUExecCommandBuffer,
    &vkGPUExecReadsFrame,
    &vkGPUExecWritesPlane,
    &vkGPUExecUsesBuffer,
    &vkGPUExecSubmit,
    &vkGPUExecAbandon,
    &vkGPUExecPoolWaitIdle,
    &vkGPUExecPoolTimeline,
    &vkCreateGPUTimeline,
    &vkFreeGPUTimeline,
    &vkAddGPUTimelineRef,
    &vkGetGPUTimelineSemaphore
};

static const VSVULKANAPI *VS_CC getVulkanAPIImpl(int version) VS_NOEXCEPT {
    /* Every version is a prefix of the next by the append only rule, so anything current or
       older is served from the same structs. */
    return (version >= 1 && version <= VSVULKAN_API_VERSION) ? &vs_internal_vsvulkanapi : nullptr;
}

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
    &getCoreInfo
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
