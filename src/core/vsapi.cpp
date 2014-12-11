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

#include "vscore.h"
#include "cpufeatures.h"
#include "vslog.h"
#include <assert.h>
#include <string.h>
#include <string>

void VS_CC configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin) {
    assert(identifier && defaultNamespace && name && plugin);
    plugin->configPlugin(identifier, defaultNamespace, name, apiVersion, !!readOnly);
}

void VS_CC registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) {
    assert(name && args && argsFunc && plugin);
    plugin->registerFunction(name, args, argsFunc, functionData);
}

static const VSFormat *VS_CC getFormatPreset(int id, VSCore *core) {
    assert(core);
    return core->getFormatPreset((VSPresetFormat)id);
}

static const VSFormat *VS_CC registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) {
    assert(core);
    return core->registerFormat((VSColorFamily)colorFamily, (VSSampleType)sampleType, bitsPerSample, subSamplingW, subSamplingH);
}

static const VSFrameRef *VS_CC cloneFrameRef(const VSFrameRef *frame) {
    assert(frame);
    return new VSFrameRef(frame->frame);
}

static VSNodeRef *VS_CC cloneNodeRef(VSNodeRef *node) {
    assert(node);
    return new VSNodeRef(node->clip, node->index);
}

static int VS_CC getStride(const VSFrameRef *frame, int plane) {
    assert(frame);
    return frame->frame->getStride(plane);
}

static const uint8_t *VS_CC getReadPtr(const VSFrameRef *frame, int plane) {
    assert(frame);
    return frame->frame->getReadPtr(plane);
}

static uint8_t *VS_CC getWritePtr(VSFrameRef *frame, int plane) {
    assert(frame);
    return frame->frame->getWritePtr(plane);
}

static void VS_CC getFrameAsync(int n, VSNodeRef *clip, VSFrameDoneCallback fdc, void *userData) {
    assert(clip && fdc);
    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (n < 0 || (numFrames && n >= numFrames)) {
        PFrameContext ctx(std::make_shared<FrameContext>(n, clip->index, clip, fdc, userData));
        ctx->setError("Invalid frame number requested, clip only has " + std::to_string(numFrames) + " frames");
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

static void VS_CC frameWaiterCallback(void *userData, const VSFrameRef *frame, int n, VSNodeRef *node, const char *errorMsg) {
    GetFrameWaiter *g = (GetFrameWaiter *)userData;
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

static const VSFrameRef *VS_CC getFrame(int n, VSNodeRef *clip, char *errorMsg, int bufSize) {
    assert(clip);
    GetFrameWaiter g(errorMsg, bufSize);
    std::unique_lock<std::mutex> l(g.b);
    VSNode *node = clip->clip.get();
    bool isWorker = node->isWorkerThread();
    if (isWorker)
        node->releaseThread();
    node->getFrame(std::make_shared<FrameContext>(n, clip->index, clip, &frameWaiterCallback, &g));
    g.a.wait(l);
    if (isWorker)
        node->reserveThread();
    return g.r;
}

static void VS_CC requestFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) {
    assert(clip && frameCtx);
    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    frameCtx->reqList.push_back(std::make_shared<FrameContext>(n, clip->index, clip->clip.get(), frameCtx->ctx));
}

static const VSFrameRef *VS_CC getFrameFilter(int n, VSNodeRef *clip, VSFrameContext *frameCtx) {
    assert(clip && frameCtx);

    int numFrames = clip->clip->getVideoInfo(clip->index).numFrames;
    if (numFrames && n >= numFrames)
        n = numFrames - 1;
    auto ref = frameCtx->ctx->availableFrames.find(NodeOutputKey(clip->clip.get(), n, clip->index));
    if (ref != frameCtx->ctx->availableFrames.end())
        return new VSFrameRef(ref->second);
    return nullptr;
}

static void VS_CC freeFrame(const VSFrameRef *frame) {
    delete frame;
}

static void VS_CC freeNode(VSNodeRef *clip) {
    delete clip;
}

static VSFrameRef *VS_CC newVideoFrame(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) {
    assert(format && core);
    return new VSFrameRef(core->newVideoFrame(format, width, height, propSrc ? propSrc->frame.get() : NULL));
}

static VSFrameRef *VS_CC newVideoFrame2(const VSFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) {
    assert(format && core);
    VSFrame *fp[3];
    for (int i = 0; i < format->numPlanes; i++)
        fp[i] = planeSrc[i] ? planeSrc[i]->frame.get() : NULL;
    return new VSFrameRef(core->newVideoFrame(format, width, height, fp, planes, propSrc ? propSrc->frame.get() : NULL));
}

static VSFrameRef *VS_CC copyFrame(const VSFrameRef *frame, VSCore *core) {
    assert(frame && core);
    return new VSFrameRef(core->copyFrame(frame->frame));
}

static void VS_CC copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) {
    assert(src && dst && core);
    core->copyFrameProps(src->frame, dst->frame);
}

static void VS_CC createFilter(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) {
    assert(in && out && name && init && getFrame && core);
    if (!name)
        vsFatal("NULL name pointer passed to createFilter()");
    core->createFilter(in, out, name, init, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, VAPOURSYNTH_API_VERSION);
}

static void VS_CC setError(VSMap *map, const char *errorMessage) {
    assert(map && errorMessage);
    map->setError(errorMessage ? errorMessage : "Error: no error specified");
}

static const char *VS_CC getError(const VSMap *map) {
    assert(map);
    if (map->size() == 1 && map->contains("_Error") && (*map)["_Error"].size() > 0)
        return (*map)["_Error"].getValue<VSMapData>(0)->c_str();
    else
        return NULL;
}

static void VS_CC setFilterError(const char *errorMessage, VSFrameContext *context) {
    assert(errorMessage && context);
    context->ctx->setError(errorMessage);
}

//property access functions
static const VSVideoInfo *VS_CC getVideoInfo(VSNodeRef *c) {
    assert(c);
    return &c->clip->getVideoInfo(c->index);
}

static void VS_CC setVideoInfo(const VSVideoInfo *vi, int numOutputs, VSNode *c) {
    assert(vi && numOutputs > 0 && c);
    c->setVideoInfo(vi, numOutputs);
}

static const VSFormat *VS_CC getFrameFormat(const VSFrameRef *f) {
    assert(f);
    return f->frame->getFormat();
}

static int VS_CC getFrameWidth(const VSFrameRef *f, int plane) {
    assert(f);
    return f->frame->getWidth(plane);
}

static int VS_CC getFrameHeight(const VSFrameRef *f, int plane) {
    assert(f);
    return f->frame->getHeight(plane);
}

static const VSMap *VS_CC getFramePropsRO(const VSFrameRef *frame) {
    assert(frame);
    return &frame->frame->getConstProperties();
}

static VSMap *VS_CC getFramePropsRW(VSFrameRef *frame) {
    assert(frame);
    return &frame->frame->getProperties();
}

static int VS_CC propNumKeys(const VSMap *props) {
    assert(props);
    return static_cast<int>(props->size());
}

static const char *VS_CC propGetKey(const VSMap *props, int index) {
    assert(props);
    if (index < 0 || static_cast<size_t>(index) >= props->size())
        vsFatal("Out of bound index");

    return props->key(index);
}

static int VS_CC propNumElements(const VSMap *props, const char *name) {
    assert(props && name);
    if (!props->contains(name))
        return -1;

    return (*props)[name].size();
}

static char VS_CC propGetType(const VSMap *props, const char *name) {
    assert(props && name);
    if (!props->contains(name))
        return 'u';

    const char a[] = { 'u', 'i', 'f', 's', 'c', 'v', 'm'};
    return a[(*props)[name].getType()];
}

static int getPropErrorCheck(const VSMap *props, const char *name, int index, int *error, int type) {
    assert(props && name);
    int err = 0;

    if (getError(props))
        vsFatal("Attempted to read from a map with error set: %s", getError(props));

    if (!props->contains(name))
        err |= peUnset;

    if (!err && (*props)[name].getType() != type)
        err |= peType;

    int c = propNumElements(props, name);

    if ((!err && c <= index) || index < 0)
        err |= peIndex;

    if (err && !error)
        vsFatal("Property read unsuccessful but no error output: %s", name);

    if (error)
        *error = err;

    return err;
}

static int64_t VS_CC propGetInt(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vInt);

    if (err)
        return 0;

    return (*props)[name].getValue<int64_t>(index);
}

static double VS_CC propGetFloat(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vFloat);

    if (err)
        return 0;

    return (*props)[name].getValue<double>(index);
}

static const char *VS_CC propGetData(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vData);

    if (err)
        return 0;

    return (*props)[name].getValue<VSMapData>(index)->c_str();
}

static int VS_CC propGetDataSize(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vData);

    if (err)
        return 0;

    return static_cast<int>((*props)[name].getValue<VSMapData>(index)->size());
}

static VSNodeRef *VS_CC propGetNode(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vNode);

    if (err)
        return 0;

    return new VSNodeRef((*props)[name].getValue<VSNodeRef>(index));
}

static const VSFrameRef *VS_CC propGetFrame(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vFrame);

    if (err)
        return 0;

    return new VSFrameRef((*props)[name].getValue<PVideoFrame>(index));
}

static int VS_CC propDeleteKey(VSMap *props, const char *name) {
    assert(props && name);
    return props->erase(name);
}

static void sharedPropSet(VSMap *props, const char *name, int &append) {
    assert(props && name);
    if (append != paReplace && append != paAppend && append != paTouch)
        vsFatal("Invalid prop append mode given");

    if (append == paReplace) {
        props->erase(name);
        append = paAppend;
    }
}

static int VS_CC propSetInt(VSMap *props, const char *name, int64_t i, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vInt)
            return 1;
        else if (append == paAppend)
            l.append(i);
    } else {
        VSVariant l(VSVariant::vInt);
        if (append == paAppend)
            l.append(i);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetFloat(VSMap *props, const char *name, double d, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vFloat)
            return 1;
        else if (append == paAppend)
            l.append(d);
    } else {
        VSVariant l(VSVariant::vFloat);
        if (append == paAppend)
            l.append(d);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetData(VSMap *props, const char *name, const char *d, int length, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vData)
            return 1;
        else if (append == paAppend)
            l.append(d);
    } else {
        VSVariant l(VSVariant::vData);
        if (append == paAppend)
            l.append(length >= 0 ? std::string(d, length) : std::string(d));
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetNode(VSMap *props, const char *name, VSNodeRef *clip, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vNode)
            return 1;
        else if (append == paAppend)
            l.append(*clip);
    } else {
        VSVariant l(VSVariant::vNode);
        if (append == paAppend)
            l.append(*clip);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetFrame(VSMap *props, const char *name, const VSFrameRef *frame, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vFrame)
            return 1;
        else if (append == paAppend)
            l.append(frame->frame);
    } else {
        VSVariant l(VSVariant::vFrame);
        if (append == paAppend)
            l.append(frame->frame);
        props->insert(name, l);
    }

    return 0;
}

static VSMap *VS_CC invoke(VSPlugin *plugin, const char *name, const VSMap *args) {
    assert(plugin && name && args);
    return new VSMap(plugin->invoke(name, *args));
}

static VSMap *VS_CC createMap() {
    return new VSMap();
}

static void VS_CC freeMap(VSMap *map) {
    delete map;
}

static void VS_CC clearMap(VSMap *map) {
    assert(map);
    map->clear();
}

static VSCore *VS_CC createCore(int threads) {
    return new VSCore(threads);
}

static void VS_CC freeCore(VSCore *core) {
    if (core)
        core->freeCore();
}

static VSPlugin *VS_CC getPluginById(const char *identifier, VSCore *core) {
    assert(identifier && core);
    return core->getPluginById(identifier);
}

static VSPlugin *VS_CC getPluginByNs(const char *ns, VSCore *core) {
    assert(ns && core);
    return core->getPluginByNs(ns);
}

static VSMap *VS_CC getPlugins(VSCore *core) {
    assert(core);
    return new VSMap(core->getPlugins());
}

static VSMap *VS_CC getFunctions(VSPlugin *plugin) {
    assert(plugin);
    return new VSMap(plugin->getFunctions());
}

static const VSCoreInfo *VS_CC getCoreInfo(VSCore *core) {
    assert(core);
    return &core->getCoreInfo();
}

static VSFuncRef *VS_CC propGetFunc(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vMethod);

    if (err)
        return 0;

    return new VSFuncRef((*props)[name].getValue<PExtFunction>(index));
}

static int VS_CC propSetFunc(VSMap *props, const char *name, VSFuncRef *func, int append) {
    assert(props && name && func);

    if (!append)
        props->erase(name);

    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.getType() != VSVariant::vMethod)
            return 1;
        else
            l.append(func->func);
    } else {
        VSVariant l(VSVariant::vMethod);
        l.append(func->func);
        props->insert(name, l);
    }

    return 0;
}

static void VS_CC callFunc(VSFuncRef *func, const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi) {
    assert(func && in && out);
    func->func->call(in, out);
}

static VSFuncRef *VS_CC createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const VSAPI *vsapi) {
    assert(func && core && vsapi);
    return new VSFuncRef(std::make_shared<ExtFunction>(func, userData, free, core, vsapi));
}

static void VS_CC freeFunc(VSFuncRef *f) {
    delete f;
}

static void VS_CC queryCompletedFrame(VSNodeRef **node, int *n, VSFrameContext *frameCtx) {
    assert(node && n && frameCtx);
    *node = frameCtx->ctx->lastCompletedNode;
    *n = frameCtx->ctx->lastCompletedN;
}

static void VS_CC releaseFrameEarly(VSNodeRef *node, int n, VSFrameContext *frameCtx) {
    assert(node && frameCtx);
    frameCtx->ctx->availableFrames.erase(NodeOutputKey(node->clip.get(), n, node->index));
}

static VSFuncRef *VS_CC cloneFuncRef(VSFuncRef *f) {
    assert(f);
    return new VSFuncRef(f->func);
}

static int64_t VS_CC setMaxCacheSize(int64_t bytes, VSCore *core) {
    assert(core);
    return core->setMaxCacheSize(bytes);
}

static int VS_CC getOutputIndex(VSFrameContext *frameCtx) {
    assert(frameCtx);
    return frameCtx->ctx->index;
}

static void VS_CC setMessageHandler(VSMessageHandler handler, void *userData) {
    vsSetMessageHandler(handler, userData);
}

static int VS_CC setThreadCount(int threads, VSCore *core) {
    assert(core);
    core->threadPool->setThreadCount(threads);
    return core->threadPool->threadCount();
}

static const char *VS_CC getPluginPath(const VSPlugin *plugin) {
    if (!plugin)
        vsFatal("NULL passed to getPluginPath");
    if (!plugin->filename.empty())
        return plugin->filename.c_str();
    else
        return nullptr;
}

const VSAPI vsapi = {
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
    &registerFunction,
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

    &getPluginPath
};

///////////////////////////////

const VSAPI *getVSAPIInternal(int version) {
    if (version == VAPOURSYNTH_API_VERSION) {
        return &vsapi;
    } else {
        vsFatal("Internally requested API version %d not supported", version);
        return NULL;
    }
}

const VSAPI *VS_CC getVapourSynthAPI(int version) {
    CPUFeatures f;
    getCPUFeatures(&f);
    if (!f.can_run_vs) {
        vsWarning("System does not meet minimum requirements to run VapourSynth");
        return NULL;
    } else if (version == VAPOURSYNTH_API_VERSION) {
        return &vsapi;
    } else {
        return NULL;
    }
}
