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

#include "vscore.h"
#include "cpufeatures.h"

void VS_CC configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin) {
    plugin->configPlugin(identifier, defaultNamespace, name, apiVersion, readOnly);
}

void VS_CC registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) {
    plugin->registerFunction(name, args, argsFunc, functionData);
}

static const VSFormat *VS_CC getFormatPreset(int id, VSCore *core) {
    return core->getFormatPreset((VSPresetFormat)id);
}

static const VSFormat *VS_CC registerFormat(int colorFamily, int sampleType, int bytesPerSample, int subSamplingW, int subSamplingH, VSCore *core) {
    return core->registerFormat((VSColorFamily)colorFamily, (VSSampleType)sampleType, bytesPerSample, subSamplingW, subSamplingH);
}

static const VSFrameRef *VS_CC cloneFrameRef(const VSFrameRef *frame) {
    Q_ASSERT(frame);
    return new VSFrameRef(frame->frame);
}

static VSNodeRef *VS_CC cloneNodeRef(VSNodeRef *node) {
    Q_ASSERT(node);
    return new VSNodeRef(node->clip, node->index);
}

static int VS_CC getStride(const VSFrameRef *frame, int plane) {
    return frame->frame->getStride(plane);
}

static const uint8_t *VS_CC getReadPtr(const VSFrameRef *frame, int plane) {
    return frame->frame->getReadPtr(plane);
}

static uint8_t *VS_CC getWritePtr(VSFrameRef *frame, int plane) {
    return frame->frame->getWritePtr(plane);
}

static void VS_CC getFrameAsync(int n, VSNodeRef *clip, VSFrameDoneCallback fdc, void *userData) {
    PFrameContext g(new FrameContext(n, clip->index, clip, fdc, userData));
    clip->clip->getFrame(g);
}

struct GetFrameWaiter {
    QMutex b;
    QWaitCondition a;
    const VSFrameRef *r;
    char *errorMsg;
    int bufSize;
    GetFrameWaiter(char *errorMsg, int bufSize) : errorMsg(errorMsg), bufSize(bufSize) {}
};

static void VS_CC frameWaiterCallback(void *userData, const VSFrameRef *frame, int n, VSNodeRef *node, const char *errorMsg) {
    GetFrameWaiter *g = (GetFrameWaiter *)userData;
    QMutexLocker l(&g->b);
    g->r = frame;
    memset(g->errorMsg, 0, g->bufSize);
    if (errorMsg) {
        strncpy(g->errorMsg, errorMsg, g->bufSize - 1);
		g->errorMsg[g->bufSize - 1] = 0;
	}
    g->a.wakeOne();
}

static const VSFrameRef *VS_CC getFrame(int n, VSNodeRef *clip, char *errorMsg, int bufSize) {
    GetFrameWaiter g(errorMsg, bufSize);
    QMutexLocker l(&g.b);
    getFrameAsync(n, clip, &frameWaiterCallback, &g);
    g.a.wait(&g.b);
    return g.r;
}

static void VS_CC requestFrameFilter(int n, VSNodeRef *clip, VSFrameContext *ctxHandle) {
    PFrameContext f(*(PFrameContext *)ctxHandle);
    PFrameContext g(new FrameContext(n, clip->index, clip->clip.data(), f));
    clip->clip->getFrame(g);
}

static const VSFrameRef *VS_CC getFrameFilter(int n, VSNodeRef *clip, VSFrameContext *ctxHandle) {
    PFrameContext f(*(PFrameContext *)ctxHandle);
    PVideoFrame g = f->availableFrames.value(NodeOutputKey(clip->clip.data(), n, clip->index));

    if (g)
        return new VSFrameRef(g);
    else
        return NULL;
}

static void VS_CC freeFrame(const VSFrameRef *frame) {
    delete frame;
}

static void VS_CC freeNode(VSNodeRef *clip) {
    delete clip;
}

static VSFrameRef *VS_CC newVideoFrame(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) {
    Q_ASSERT(format);
    return new VSFrameRef(core->newVideoFrame(format, width, height, propSrc ? propSrc->frame.data() : NULL));
}

static VSFrameRef *VS_CC newVideoFrame2(const VSFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) {
    Q_ASSERT(format);
    VSFrame *fp[3];
    for (int i = 0; i < format->numPlanes; i++)
        fp[i] = planeSrc[i] ? planeSrc[i]->frame.data() : NULL;
    return new VSFrameRef(core->newVideoFrame(format, width, height, fp, planes, propSrc ? propSrc->frame.data() : NULL));
}

static VSFrameRef *VS_CC copyFrame(const VSFrameRef *frame, VSCore *core) {
    return new VSFrameRef(core->copyFrame(frame->frame));
}

static void VS_CC copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) {
    core->copyFrameProps(src->frame, dst->frame);
}

static void VS_CC createFilter(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) {
    core->createFilter(in, out, name, init, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, VAPOURSYNTH_API_VERSION);
}

static void VS_CC setError(VSMap *map, const char *errorMessage) {
    map->clear();
    VSVariant l(VSVariant::vData);
    l.s.append(errorMessage ? errorMessage : "Error: no error specified");
    map->insert("_Error", l);
}

static const char *VS_CC getError(const VSMap *map) {
    if (map->contains("_Error"))
        return (*map)["_Error"].s[0].constData();
    else
        return NULL;
}

static void VS_CC setFilterError(const char *errorMessage, VSFrameContext *context) {
    PFrameContext f(*(PFrameContext *)context);
    f->setError(errorMessage);
}

//property access functions
static const VSVideoInfo *VS_CC getVideoInfo(VSNodeRef *c) {
    return &c->clip->getVideoInfo(c->index);
}

static void VS_CC setVideoInfo(const VSVideoInfo *vi, int numOutputs, VSNode *c) {
    c->setVideoInfo(vi, numOutputs);
}

static const VSFormat *VS_CC getFrameFormat(const VSFrameRef *f) {
    return f->frame->getFormat();
}

static int VS_CC getFrameWidth(const VSFrameRef *f, int plane) {
    return f->frame->getWidth(plane);
}

static int VS_CC getFrameHeight(const VSFrameRef *f, int plane) {
    return f->frame->getHeight(plane);
}

static const VSMap *VS_CC getFramePropsRO(const VSFrameRef *frame) {
    return &frame->frame->getConstProperties();
}

static VSMap *VS_CC getFramePropsRW(VSFrameRef *frame) {
    return &frame->frame->getProperties();
}

static int VS_CC propNumKeys(const VSMap *props) {
    return props->keys().count();
}

static const char *VS_CC propGetKey(const VSMap *props, int index) {
    if (index < 0 || index >= props->count())
        qFatal("Out of bound index");

    return props->keys()[index].constData();
}

static int VS_CC propNumElements(const VSMap *props, const char *name) {
    if (!props->contains(name))
        return -1;

    const VSVariant &val = (*props)[name];
    return val.count();
}

static char VS_CC propGetType(const VSMap *props, const char *name) {
    if (!props->contains(name))
        return 'u';

    const char a[] = { 'u', 'i', 'f', 's', 'c', 'v', 'm'};
    const VSVariant &val = (*props)[name];
    return a[val.vtype];
}

static int getPropErrorCheck(const VSMap *props, const char *name, int index, int *error, int type) {
    int err = 0;

    if (getError(props))
        qFatal("Attempted to read from a map with error set: %s", getError(props));

    if (!props->contains(name))
        err |= peUnset;

    if (!err && props->value(name).vtype != type)
        err |= peType;

    int c = propNumElements(props, name);

    if ((!err && c <= index) || index < 0)
        err |= peIndex;

    if (err && !error)
        qFatal("Property read unsuccessful but no error output: %s", name);

    if (error)
        *error = err;

    return err;
}

static int64_t VS_CC propGetInt(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vInt);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return l.i[index];
}

static double VS_CC propGetFloat(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vFloat);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return l.f[index];
}

static const char *VS_CC propGetData(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vData);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return l.s[index].constData();
}

static int VS_CC propGetDataSize(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vData);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return l.s[index].size();
}

static VSNodeRef *VS_CC propGetNode(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vNode);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return new VSNodeRef(l.c[index]);
}

static const VSFrameRef *VS_CC propGetFrame(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vFrame);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return new VSFrameRef(l.v[index]);
}

static int VS_CC propDeleteKey(VSMap *props, const char *name) {
    return props->remove(name);
}

static void sharedPropSet(VSMap *props, const char *name, int &append) {
    if (append != paReplace && append != paAppend && append != paTouch)
        qFatal("Invalid prop append mode given");

    if (append == paReplace) {
        props->remove(name);
        append = paAppend;
    }
}

static int VS_CC propSetInt(VSMap *props, const char *name, int64_t i, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vInt)
            return 1;
        else if (append == paAppend)
            l.i.append(i);
    } else {
        VSVariant l(VSVariant::vInt);
        if (append == paAppend)
            l.i.append(i);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetFloat(VSMap *props, const char *name, double d, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vFloat)
            return 1;
        else if (append == paAppend)
            l.f.append(d);
    } else {
        VSVariant l(VSVariant::vFloat);
        if (append == paAppend)
            l.f.append(d);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetData(VSMap *props, const char *name, const char *d, int length, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vData)
            return 1;
        else if (append == paAppend)
            l.s.append(d);
    } else {
        VSVariant l(VSVariant::vData);
        if (append == paAppend)
            l.s.append(length >= 0 ? QByteArray(d, length) : QByteArray(d));
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetNode(VSMap *props, const char *name, VSNodeRef *clip, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vNode)
            return 1;
        else if (append == paAppend)
            l.c.append(*clip);
    } else {
        VSVariant l(VSVariant::vNode);
        if (append == paAppend)
            l.c.append(*clip);
        props->insert(name, l);
    }

    return 0;
}

static int VS_CC propSetFrame(VSMap *props, const char *name, const VSFrameRef *frame, int append) {
    sharedPropSet(props, name, append);
    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vFrame)
            return 1;
        else if (append == paAppend)
            l.v.append(frame->frame);
    } else {
        VSVariant l(VSVariant::vFrame);
        if (append == paAppend)
            l.v.append(frame->frame);
        props->insert(name, l);
    }

    return 0;
}

static VSMap *VS_CC invoke(VSPlugin *plugin, const char *name, const VSMap *args) {
    Q_ASSERT(plugin);
    return new VSMap(plugin->invoke(name, *args));
}

static VSMap *VS_CC newMap() {
    return new VSMap();
}

static void VS_CC freeMap(VSMap *map) {
    delete map;
}

static void VS_CC clearMap(VSMap *map) {
    map->clear();
}

static VSCore *VS_CC createCore(int threads) {
    return new VSCore(threads);
}

static void VS_CC freeCore(VSCore *core) {
    delete core;
}

static VSPlugin *VS_CC getPluginId(const char *identifier, VSCore *core) {
    return core->getPluginId(identifier);
}

static VSPlugin *VS_CC getPluginNs(const char *ns, VSCore *core) {
    return core->getPluginNs(ns);
}

static VSMap *VS_CC getPlugins(VSCore *core) {
    return new VSMap(core->getPlugins());
}

static VSMap *VS_CC getFunctions(VSPlugin *plugin) {
    return new VSMap(plugin->getFunctions());
}

static const VSCoreInfo *VS_CC getCoreInfo(VSCore *core) {
    return &core->getCoreInfo();
}

static VSFuncRef *VS_CC propGetFunc(const VSMap *props, const char *name, int index, int *error) {
    int err = getPropErrorCheck(props, name, index, error, VSVariant::vMethod);

    if (err)
        return 0;

    const VSVariant &l = (*props)[name];
    return new VSFuncRef(l.m[index]);
}

static int VS_CC propSetFunc(VSMap *props, const char *name, VSFuncRef *func, int append) {
    if (!append)
        props->remove(name);

    if (props->contains(name)) {
        VSVariant &l = (*props)[name];

        if (l.vtype != VSVariant::vMethod)
            return 1;
        else
            l.m.append(func->func);
    } else {
        VSVariant l(VSVariant::vMethod);
        l.m.append(func->func);
        props->insert(name, l);
    }

    return 0;
}

static void VS_CC callFunc(VSFuncRef *func, const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi) {
    func->func->call(in, out, core, vsapi);
}

static VSFuncRef *VS_CC createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free) {
    return new VSFuncRef(PExtFunction(new ExtFunction(func, userData, free)));
}

static void VS_CC freeFunc(VSFuncRef *f) {
    delete f;
}

static void VS_CC queryCompletedFrame(VSNodeRef **node, int *n, VSFrameContext *frameCtx) {
    PFrameContext f(*(PFrameContext *)frameCtx);
    *node = f->lastCompletedNode;
    *n = f->lastCompletedN;
}

static void VS_CC releaseFrameEarly(VSNodeRef *node, int n, VSFrameContext *frameCtx) {
    PFrameContext f(*(PFrameContext *)frameCtx);
    f->availableFrames.remove(NodeOutputKey(node->clip.data(), n, node->index));
}

static VSFuncRef *VS_CC cloneFuncRef(VSFuncRef *f) {
    return new VSFuncRef(f->func);
}

static int64_t VS_CC setMaxCacheSize(int64_t bytes, VSCore *core) {
    return core->setMaxCacheSize(bytes);
}

static int VS_CC getOutputIndex(VSFrameContext *frameCtx) {
    PFrameContext f(*(PFrameContext *)frameCtx);
    return f->index;
}

static VSMessageHandler handler = NULL;

void vsMessageHandler(QtMsgType type, const char *msg) {
    handler(type, msg);
    if (type == QtFatalMsg)
        abort();
}

static void VS_CC setMessageHandler(VSMessageHandler handler) {
    if (handler) {
        ::handler = handler;
        qInstallMsgHandler(vsMessageHandler);
    } else {
        qInstallMsgHandler(NULL);
        ::handler = NULL;
    }
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
    &getPluginId,
    &getPluginNs,
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

    &newMap,
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

    &setMessageHandler
};

//////////////////////////
// R2 compat junk

typedef void (VS_CC *VSSetVideoInfo_R2)(const VSVideoInfo *vi, VSNode *node);
typedef VSNodeRef *(VS_CC *VSCreateFilter_R2)(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core);

static void VS_CC setVideoInfoR2(const VSVideoInfo *vi, VSNode *c) {
    setVideoInfo(vi, 1, c);
}

static VSNodeRef *VS_CC createFilterR2(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) {
    core->createFilter(in, out, name, init, getFrame, free, static_cast<VSFilterMode>(filterMode), flags, instanceData, 2);
    return propGetNode(out, "clip", 0, NULL);
}

struct VSAPI_R2 {
    VSCreateCore createCore;
    VSFreeCore freeCore;
    VSGetCoreInfo getCoreInfo;

    VSCloneFrameRef cloneFrameRef;
    VSCloneNodeRef cloneNodeRef;
    VSCloneFuncRef cloneFuncRef;

    VSFreeFrame freeFrame;
    VSFreeNode freeNode;
    VSFreeFunc freeFunc;

    VSNewVideoFrame newVideoFrame;
    VSCopyFrame copyFrame;
    VSCopyFrameProps copyFrameProps;

    VSRegisterFunction registerFunction;
    VSGetPluginId getPluginId;
    VSGetPluginNs getPluginNs;
    VSGetPlugins getPlugins;
    VSGetFunctions getFunctions;
    VSCreateFilter_R2 createFilter;
    VSSetError setError;
    VSGetError getError;
    VSSetFilterError setFilterError;
    VSInvoke invoke;

    VSGetFormatPreset getFormatPreset;
    VSRegisterFormat registerFormat;

    VSGetFrame getFrame;
    VSGetFrameAsync getFrameAsync;
    VSGetFrameFilter getFrameFilter;
    VSRequestFrameFilter requestFrameFilter;
    VSQueryCompletedFrame queryCompletedFrame;
    VSReleaseFrameEarly releaseFrameEarly;

    VSGetStride getStride;
    VSGetReadPtr getReadPtr;
    VSGetWritePtr getWritePtr;

    VSCreateFunc createFunc;
    VSCallFunc callFunc;

    //property access functions
    VSCreateMap createMap;
    VSFreeMap freeMap;
    VSClearMap clearMap;

    VSGetVideoInfo getVideoInfo;
    VSSetVideoInfo_R2 setVideoInfo;
    VSGetFrameFormat getFrameFormat;
    VSGetFrameWidth getFrameWidth;
    VSGetFrameHeight getFrameHeight;
    VSGetFramePropsRO getFramePropsRO;
    VSGetFramePropsRW getFramePropsRW;

    VSPropNumKeys propNumKeys;
    VSPropGetKey propGetKey;
    VSPropNumElements propNumElements;
    VSPropGetType propGetType;
    VSPropGetInt propGetInt;
    VSPropGetFloat propGetFloat;
    VSPropGetData propGetData;
    VSPropGetDataSize propGetDataSize;
    VSPropGetNode propGetNode;
    VSPropGetFrame propGetFrame;
    VSPropGetFunc propGetFunc;

    VSPropDeleteKey propDeleteKey;
    VSPropSetInt propSetInt;
    VSPropSetFloat propSetFloat;
    VSPropSetData propSetData;
    VSPropSetNode propSetNode;
    VSPropSetFrame propSetFrame;
    VSPropSetFunc propSetFunc;

    VSSetMaxCacheSize setMaxCacheSize;
    VSGetOutputIndex getOutputIndex;
};

///////////////////////////////

const VSAPI *getVSAPIInternal(int version) {
    if (version == VAPOURSYNTH_API_VERSION) {
        return &vsapi;
    } else {
        qFatal("Internally requested API version %d", version);
        return NULL;
    }
}

const VSAPI *VS_CC getVapourSynthAPI(int version) {
    CPUFeatures f;
    getCPUFeatures(&f);
    if (!f.can_run_vs) {
        qWarning("System does not meet minimum requirements to run VapourSynth");
        return NULL;
    } else if (version == VAPOURSYNTH_API_VERSION) {
        return &vsapi;
    } else {
        return NULL;
    }
}
