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

#include "reorderfilters.h"
#include "VSHelper.h"
#include "filtershared.h"
#include <stdlib.h>

//////////////////////////////////////////
// Shared

static int findCommonVi(VSNodeRef **nodes, int num, VSVideoInfo *outvi, int ignorelength, const VSAPI *vsapi) {
    int mismatch = 0;
    int i;
    const VSVideoInfo *vi;
    *outvi = *vsapi->getVideoInfo(nodes[0]);

    for (i = 1; i < num; i++) {
        vi = vsapi->getVideoInfo(nodes[i]);

        if (outvi->width != vi->width || outvi->height != vi->height) {
            outvi->width = 0;
            outvi->height = 0;
            mismatch = 1;
        }

        if (outvi->format != vi->format) {
            outvi->format = 0;
            mismatch = 1;
        }

        if ((outvi->numFrames < vi->numFrames && outvi->numFrames) || (!vi->numFrames && outvi->numFrames)) {
            outvi->numFrames = vi->numFrames;

            if (!ignorelength)
                mismatch = 1;
        }
    }

    return mismatch;
}

//////////////////////////////////////////
// Trim

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int first;
    int last;
    int length;
    int trimlen;
} TrimData;

static void VS_CC trimInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TrimData *d = (TrimData *) * instanceData;
    d->vi.numFrames = d->trimlen;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC trimGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TrimData *d = (TrimData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n + d->first, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n + d->first, d->node, frameCtx);
    }

    return 0;
}

static void VS_CC trimCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    TrimData d;
    TrimData *data;
    int firstset;
    int lastset;
    int lengthset;
    int err;
    d.first = 0;
    d.last = -1;
    d.length = -1;

    d.first = int64ToIntS(vsapi->propGetInt(in, "first", 0, &err));
    firstset = !err;
    d.last = int64ToIntS(vsapi->propGetInt(in, "last", 0, &err));
    lastset =  !err;
    d.length = int64ToIntS(vsapi->propGetInt(in, "length", 0, &err));
    lengthset = !err;

    if (lastset && lengthset)
        RETERROR("Trim: both last frame and length specified");

    if (lastset && d.last < d.first)
        RETERROR("Trim: invalid last frame specified");

    if (lengthset && d.length < 1)
        RETERROR("Trim: invalid length specified");

    if (d.first < 0)
        RETERROR("Trim: invalid first frame specified");

    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    d.vi = *vsapi->getVideoInfo(d.node);

    if ((lastset && d.vi.numFrames && d.last >= d.vi.numFrames) || (lengthset && d.vi.numFrames && (d.first + d.length) > d.vi.numFrames) || (d.vi.numFrames && d.vi.numFrames <= d.first)) {
        vsapi->freeNode(d.node);
        RETERROR("Trim: last frame beyond clip end");
    }

    if (lastset) {
        d.trimlen = d.last - d.first + 1;
    } else if (lengthset) {
        d.trimlen = d.length;
    } else if (d.vi.numFrames) {
        d.trimlen = d.vi.numFrames - d.first;
    } else {
        d.trimlen = 0;
    }

    // obvious nop() so just pass through the input clip
    if ((!firstset && !lastset && !lengthset) || (d.trimlen && d.trimlen == d.vi.numFrames)) {
        vsapi->propSetNode(out, "clip", d.node, 0);
        vsapi->freeNode(d.node);
        return;
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Trim", trimInit, trimGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// Interleave

typedef struct {
    VSNodeRef **node;
    VSVideoInfo vi;
    int numclips;
} InterleaveData;

static void VS_CC interleaveInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = (InterleaveData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC interleaveGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = (InterleaveData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / d->numclips, d->node[n % d->numclips], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n / d->numclips, d->node[n % d->numclips], frameCtx);
    }

    return 0;
}

static void VS_CC interleaveFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = (InterleaveData *)instanceData;
    int i;

    for (i = 0; i < d->numclips; i++)
        vsapi->freeNode(d->node[i]);

    free(d->node);
    free(d);
}

static void VS_CC interleaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InterleaveData d;
    InterleaveData *data;
    VSNodeRef *cref;
    int i;
    int err;
    int compat;

    int mismatch = !!vsapi->propGetInt(in, "mismatch", 0, &err);
    int extend = !!vsapi->propGetInt(in, "extend", 0, &err);
    d.numclips = vsapi->propNumElements(in, "clips");

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
    } else {
        d.node = malloc(sizeof(d.node[0]) * d.numclips);
        compat = 0;

        for (i = 0; i < d.numclips; i++) {
            d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

            if (isCompatFormat(vsapi->getVideoInfo(d.node[i])))
                compat = 1;
        }

        if (findCommonVi(d.node, d.numclips, &d.vi, 1, vsapi) && (!mismatch || compat)) {
            for (i = 0; i < d.numclips; i++)
                vsapi->freeNode(d.node[i]);

            free(d.node);
            RETERROR("Interleave: clip property mismatch");
        }

        if (extend) {
            d.vi.numFrames *= d.numclips;
        } else if (d.vi.numFrames) {
            // this is exactly how avisynth does it
            d.vi.numFrames = (vsapi->getVideoInfo(d.node[0])->numFrames - 1) * d.numclips + 1;
            for (i = 1; i < d.numclips; i++)
                d.vi.numFrames = MAX(d.vi.numFrames, (vsapi->getVideoInfo(d.node[i])->numFrames - 1) * d.numclips + i + 1);
        }
        d.vi.fpsNum *= d.numclips;

        data = malloc(sizeof(d));
        *data = d;

        vsapi->createFilter(in, out, "Interleave", interleaveInit, interleaveGetframe, interleaveFree, fmParallel, nfNoCache, data, core);
    }
}

//////////////////////////////////////////
// Reverse

static const VSFrameRef *VS_CC reverseGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(MAX(d->vi->numFrames - n - 1, 0), d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(MAX(d->vi->numFrames - n - 1, 0), d->node, frameCtx);
    }

    return 0;
}

static void VS_CC reverseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData d;
    SingleClipData *data;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->numFrames) {
        vsapi->freeNode(d.node);
        RETERROR("Reverse: cannot reverse clips with unknown length");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Reverse", singleClipInit, reverseGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// Loop

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int times;
} LoopData;

static void VS_CC loopInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    LoopData *d = (LoopData *) * instanceData;
    VSVideoInfo vi = *d->vi;

    if (d->times > 0) // loop x times
        vi.numFrames *= d->times;
    else // loop forever
        vi.numFrames = 0;

    vsapi->setVideoInfo(&vi, 1, node);
}

static const VSFrameRef *VS_CC loopGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LoopData *d = (LoopData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n % d->vi->numFrames, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n % d->vi->numFrames, d->node, frameCtx);
    }

    return 0;
}

static void VS_CC loopCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    LoopData d;
    LoopData *data;
    int err;

    d.times = int64ToIntS(vsapi->propGetInt(in, "times", 0, &err));
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->numFrames) {
        vsapi->freeNode(d.node);
        RETERROR("Loop: cannot loop clips with unknown length");
    }

    // early termination for the trivial case
    if (d.times == 1) {
        vsapi->propSetNode(out, "clip", d.node, 0);
        vsapi->freeNode(d.node);
        return;
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Loop", loopInit, loopGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// SelectEvery

typedef struct {
    VSNodeRef *node;
    int cycle;
    int *offsets;
    int num;
} SelectEveryData;

static void VS_CC selectEveryInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = (SelectEveryData *) * instanceData;
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);
    int i;
    int inputnframes = vi.numFrames;
    if (inputnframes) {
        vi.numFrames = (inputnframes / d->cycle) * d->num;
        for (i = 0; i < d->num; i++)
            if (d->offsets[i] < inputnframes % d->cycle)
                vi.numFrames++;
    }
    vi.fpsDen *= d->cycle;
    vi.fpsNum *= d->num;
    vsapi->setVideoInfo(&vi, 1, node);
}

static const VSFrameRef *VS_CC selectEveryGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = (SelectEveryData *) * instanceData;

    if (activationReason == arInitial) {
        *frameData = (void *)(intptr_t)((n / d->num) * d->cycle + d->offsets[n % d->num]);
        vsapi->requestFrameFilter((intptr_t)*frameData, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter((intptr_t) * frameData, d->node, frameCtx);
    }

    return 0;
}

static void VS_CC selectEveryFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = (SelectEveryData *)instanceData;
    free(d->offsets);
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC selectEveryCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData d;
    SelectEveryData *data;
    int i;
    d.cycle = int64ToIntS(vsapi->propGetInt(in, "cycle", 0, 0));

    if (d.cycle <= 1)
        RETERROR("SelectEvery: invalid cycle size");

    d.num = vsapi->propNumElements(in, "offsets");
    d.offsets = malloc(sizeof(d.offsets[0]) * d.num);

    for (i = 0; i < d.num; i++) {
        d.offsets[i] = int64ToIntS(vsapi->propGetInt(in, "offsets", i, 0));

        if (d.offsets[i] < 0 || d.offsets[i] >= d.cycle) {
            free(d.offsets);
            RETERROR("SelectEvery: invalid offset specified");
        }
    }

    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "SelectEvery", selectEveryInit, selectEveryGetframe, selectEveryFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// Splice

typedef struct {
    VSNodeRef **node;
    VSVideoInfo vi;
    int *numframes;
    int numclips;
} SpliceData;

static void VS_CC spliceInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SpliceData *d = (SpliceData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

typedef struct {
    int f;
    int idx;
} SpliceCtx;

static const VSFrameRef *VS_CC spliceGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SpliceData *d = (SpliceData *) * instanceData;

    if (activationReason == arInitial) {
        int i;
        int frame = 0;
        int idx = 0;
        int cumframe = 0;
        SpliceCtx *s = malloc(sizeof(SpliceCtx));

        for (i = 0; i < d->numclips; i++) {
            if ((n >= cumframe && n < cumframe + d->numframes[i]) || i == d->numclips - 1) {
                idx = i;
                frame = n - cumframe;
                break;
            }

            cumframe += d->numframes[i];
        }

        *frameData = s;
        s->f = frame;
        s->idx = idx;
        vsapi->requestFrameFilter(frame, d->node[idx], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        SpliceCtx *s = (SpliceCtx *) * frameData;
        const VSFrameRef *f = vsapi->getFrameFilter(s->f, d->node[s->idx], frameCtx);
        free(s);
        return f;
    }

    return 0;
}

static void VS_CC spliceFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SpliceData *d = (SpliceData *)instanceData;
    int i;

    for (i = 0; i < d->numclips; i++)
        vsapi->freeNode(d->node[i]);

    free(d->node);
    free(d->numframes);
    free(d);
}

static void VS_CC spliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SpliceData d;
    SpliceData *data;
    VSNodeRef *cref;
    int mismatch;
    int i;
    int err;
    int compat = 0;

    d.numclips = vsapi->propNumElements(in, "clips");
    mismatch = !!vsapi->propGetInt(in, "mismatch", 0, &err);

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
    } else {
        d.node = malloc(sizeof(d.node[0]) * d.numclips);

        for (i = 0; i < d.numclips; i++) {
            d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

            if (isCompatFormat(vsapi->getVideoInfo(d.node[i])))
                compat = 1;
        }

        if (findCommonVi(d.node, d.numclips, &d.vi, 0, vsapi) && (!mismatch || compat) && !isSameFormat(&d.vi, vsapi->getVideoInfo(d.node[0]))) {
            for (i = 0; i < d.numclips; i++)
                vsapi->freeNode(d.node[i]);

            free(d.node);
            RETERROR("Splice: clip property mismatch");
        }

        d.numframes = malloc(sizeof(d.numframes[0]) * d.numclips);
        d.vi.numFrames = 0;

        for (i = 0; i < d.numclips; i++) {
            d.numframes[i] = (vsapi->getVideoInfo(d.node[i]))->numFrames;
            d.vi.numFrames += d.numframes[i];

            if (d.numframes[i] == 0 && i != d.numclips - 1) {
                for (i = 0; i < d.numclips; i++)
                    vsapi->freeNode(d.node[i]);

                free(d.node);
                free(d.numframes);
                RETERROR("Splice: unknown length clips can only be last in a splice operation");
            }
        }

        if (d.numframes[d.numclips - 1] == 0)
            d.vi.numFrames = 0;

        data = malloc(sizeof(d));
        *data = d;

        vsapi->createFilter(in, out, "Splice", spliceInit, spliceGetframe, spliceFree, fmParallel, nfNoCache, data, core);
    }
}

//////////////////////////////////////////
// Init

void VS_CC reorderInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Trim", "clip:clip;first:int:opt;last:int:opt;length:int:opt;", trimCreate, 0, plugin);;
    registerFunc("Reverse", "clip:clip;", reverseCreate, 0, plugin);
    registerFunc("Loop", "clip:clip;times:int:opt;", loopCreate, 0, plugin);
    registerFunc("Interleave", "clips:clip[];extend:int:opt;mismatch:int:opt;", interleaveCreate, 0, plugin);
    registerFunc("SelectEvery", "clip:clip;cycle:int;offsets:int[];", selectEveryCreate, 0, plugin);
    registerFunc("Splice", "clips:clip[];mismatch:int:opt;", spliceCreate, 0, plugin);
}
