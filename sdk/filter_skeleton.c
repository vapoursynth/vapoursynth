//////////////////////////////////////////
// This file contains a simple filter
// skeleton you can use to get started.
// With no changes it simply passes
// frames through.

#include "VapourSynth.h"
#include "VapourSynth.h"

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo *vi;
} FilterData;

static void VS_CC filterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    FilterData *d = (FilterData *) * instanceData;
    vsapi->setVideoInfo(d->vi, node);
}

static const VSFrameRef *VS_CC filterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FilterData *d = (FilterData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *frame = vsapi->getFrameFilter(n, d->node, frameCtx);
  
        // your code here...

        return frame;
    }

    return 0;
}

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    FilterData *d = (FilterData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FilterData d;
    FilterData *data;
    const VSNodeRef *cref;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "Filter", filterInit, filterGetFrame, filterFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Init

void VS_CC filterlibInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.example.filter", "filter", "VapourSynth Filter Skeleton", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Filter", "clip:clip;", filterCreate, 0, plugin);
}
