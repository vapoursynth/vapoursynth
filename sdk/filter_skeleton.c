//////////////////////////////////////////
// This file contains a simple filter
// skeleton you can use to get started.
// With no changes it simply passes
// frames through.

#include "VapourSynth4.h"
#include "VSHelper4.h"

typedef struct {
    VSNode *node;
    const VSVideoInfo *vi;
} FilterData;


static const VSFrame *VS_CC filterGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FilterData *d = (FilterData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *frame = vsapi->getFrameFilter(n, d->node, frameCtx);

        /* your code here... */

        return frame;
    }

    return NULL;
}

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    FilterData *d = (FilterData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FilterData d;
    FilterData *data;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    data = (FilterData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = {{d.node, rpGeneral}}; /* Depending the the request patterns you may want to change this */
    vsapi->createVideoFilter(out, "Filter", data->vi, filterGetFrame, filterFree, fmParallel, deps, 1, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.filter", "filter", "VapourSynth Filter Skeleton", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Filter", "clip:vnode;", "clip:vnode;", filterCreate, NULL, plugin);
}
