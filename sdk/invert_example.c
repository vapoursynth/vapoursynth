//////////////////////////////////////////
// This file contains a simple invert
// filter that's commented to show
// the basics of the filter api.
// This file may make more sense when
// read from the bottom and up.

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo *vi;
    int enable;
} InvertData;

// This function is called immediated after vsapi->createFilter(). This is the only place where the video
// properties may be set. In this case we simply use the same as the input clip.
static void VS_CC invertInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    vsapi->setVideoInfo(d->vi, node);
}

static const VSFrameRef *VS_CC invertGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *frame = vsapi->getFrameFilter(n, d->node, frameCtx);
  


        return frame;
    }

    return 0;
}

// Free all allocated data on filter destruction
static void VS_CC invertFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

// 
static void VS_CC invertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InvertData d;
    InvertData *data;
    const VSNodeRef *cref;
    int err;

    // Get a clip reference from the input arguments. This must be freed later.
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    // If a property read fails for some reason (index out of bounds/wrong type)
    // then err will have flags set to indicate why and 0 will be returned. This
    // can be very useful to know when having optional arguments. Since we have
    // strict checking because of what we wrote in the argument string the only reason
    // this could fail is when the value wasn't set by the user.
    // And when it's not set we want it to default to enabled.
    d.enabled = vsapi->propGetNode(in, "enable", 0, &err);
    if (err)
        d.enabled = 1;

    // Let's pretend the only allowed values are 1 or 0...
    if (d.enabled < 0 || d.enabled > 1) {
        vsapi->setError(out, "Invert: enabled must be 0 or 1");
        vsapi->freeNode(d.node);
        return;
    }

    // I usually keep the filter data struct on the stack and don't allocate it
    // until all the input validation is done.
    data = malloc(sizeof(d));
    *data = d;

    // Create a new filter and returns a reference to it. Always pass on the in an out
    // arguments or unexpected thigns may happen. The name should be something that's
    // easy to connect to the filter, like its function name.
    // The three function pointers handle initialization, frame processing and filter destruction.
    // The filtermode is very important to get right as it controls how threading of the filter
    // is handled. In general you should only use fmParallel whenever possible. This is if you
    // need to modify no shared data at all when the filter is running.
    // For more complicated filters fmParallelRequests is usually easier to achieve as an
    // be prefetched in parallel but no
    // The others can be considered special cases where fmSerial is useful to source filters and
    // fmUnordered is useful when a filter's state may change even when deciding which frames to
    // prefetch (such as a cache filter).
    // If you filter is really fast (such as a filter that only resorts frames) you should set the
    // nfNoCache flag to make the caching work smoother.
    cref = vsapi->createFilter(in, out, "Invert", invertInit, invertGetFrame, invertFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Init

// This is the entry point that is called when a plugin is loaded. You are only supposed
// to call the two provided functions here.
// configFunc sets the id, namespace, and long name of the plugin (the last 3 arguments
// never need to be changed for a normal plugin).
//
// id: Needs to be a "reverse" url and unique among all plugins.
//   It is inspired by how android packages dientify themselves.
//   If you don't own a domain then make one up that's related
//   to the plugin name.
//
// namespace: Should only use [a-z_] and not be too long.
//
// full name: Any name that describes the plugin nicely.
//
// registerFunc is called once for each function you want to register. Function names
// should be CamelCase. The argument string has this format:
// name:type; or name:type:flag1:flag2....;
// All argument name should be lowercase and only use [a-z_].
// The valid types are int,float,data,clip,frame,func. [] can be appended to allow arrays
// of tyhe type to be passed (numbers:int[])
// The available flags are opt, to make an argument optional, and link which will not be
// explained here.

void VS_CC invertlibInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.example.invert", "invert", "VapourSynth Invert Example", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Filter", "clip:clip;enable:int:opt;", invertCreate, 0, plugin);
}
