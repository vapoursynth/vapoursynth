//  Copyright (c) 2012 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

///////////////////////////////////////////////////////////////////////////////
// Licensing notes for this file:
// This file contains the implementation of all base functions and therefore
// makes quite good example code showing how the VapourSynth API may be used.
// You are welcome to copy and paste code from single filters here and I will
// not be too picky with attribution.
// I also hope that if someone writes another backend for the VapourSynth API
// they will include this library of standard functions so that the
// implementations don't diverge too much in base functionality.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "vsstdlib.h"

#define RETERROR(x) do { vsapi->setError(out, (x)); return; } while (0)
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

int zero = 0;

void bitblt(uint8_t *dstp, int dst_stride, const uint8_t *srcp, int src_stride, int row_size, int height) {
    if (src_stride == dst_stride && dst_stride == row_size) {
        memcpy(dstp, srcp, row_size * height);
    } else {
        int i;

        for (i = 0; i < height; i++) {
            memcpy(dstp, srcp, row_size);
            dstp += dst_stride;
            srcp += src_stride;
        }
    }
}

//////////////////////////////////////////
// Shared

typedef struct {
    const VSNodeRef *node;
    const VSVideoInfo *vi;
} SingleClipData;

static void VS_CC singleClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node), node);
}

static void VS_CC singleClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *)instanceData;
    vsapi->freeNode(d->node);
    free(instanceData);
}

static int findCommonVi(const VSNodeRef **nodes, int num, VSVideoInfo *outvi, int ignorelength, const VSAPI *vsapi) {
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

// does the format change between frames?
static int isConstantFormat(const VSVideoInfo *vi) {
    return vi->height > 0 && vi->width > 0 && vi->format;
}

// to detect compat formats
static int isCompatFormat(const VSVideoInfo *vi) {
    return vi->format && vi->format->colorFamily == cmCompat;
}

// if the video is the same format for processing purposes, not
static int isSameFormat(const VSVideoInfo *v1, const VSVideoInfo *v2) {
    return v1->height == v2->height && v1->width == v2->width && v1->format == v2->format;
}

static int planeWidth(const VSVideoInfo *vi, int plane) {
    return vi->width >> (plane ? vi->format->subSamplingW : 0);
}

static int planeHeight(const VSVideoInfo *vi, int plane) {
    return vi->height >> (plane ? vi->format->subSamplingH : 0);
}

//////////////////////////////////////////
// CropAbs

typedef struct {
    const VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *xprop;
    const char *yprop;
    int x;
    int y;
    int width;
    int height;
} CropAbsData;

static void VS_CC cropAbsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    CropAbsData *d = (CropAbsData *) * instanceData;
    VSVideoInfo vi = *d->vi;
    vi.height = d->height;
    vi.width = d->width;
    vsapi->setVideoInfo(&vi, node);
}

static int cropAbsVerify(int x, int y, int width, int height, int srcwidth, int srcheight, const VSFormat *fi, char *msg) {
    msg[0] = 0;

    if (y < 0 || x < 0)
        sprintf(msg, "CropAbs: negative corner coordinates not allowed");

    if (width <= 0 || height <= 0)
        sprintf(msg, "CropAbs: negative/zero cropping dimensions not allowed");

    if (srcheight > 0 && srcwidth > 0)
        if (srcheight < height + y || srcwidth < width + x)
            sprintf(msg, "CropAbs: cropped area extends beyond frame dimensions");

    if (fi) {
        if (width % (1 << fi->subSamplingW))
            sprintf(msg, "CropAbs: cropped area needs to have mod %d width", 1 << fi->subSamplingW);

        if (height % (1 << fi->subSamplingH))
            sprintf(msg, "CropAbs: cropped area needs to have mod %d height", 1 << fi->subSamplingH);

        if (x % (1 << fi->subSamplingW))
            sprintf(msg, "CropAbs: cropped area needs to have mod %d width offset", 1 << fi->subSamplingW);

        if (y % (1 << fi->subSamplingH))
            sprintf(msg, "CropAbs: cropped area needs to have mod %d height offset", 1 << fi->subSamplingH);
    }

    if (msg[0])
        return 1;
    else
        return 0;
}

static const VSFrameRef *VS_CC cropAbsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CropAbsData *d = (CropAbsData *) * instanceData;
    char msg[150];

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        int hloop;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, d->width, d->height, src, core);
        int x = d->x;
        int y = d->y;
        const VSMap *m = vsapi->getFramePropsRO(src);
        int err;
        int64_t temp;

        if (d->xprop) {
            temp = vsapi->propGetInt(m, "x_prop", 0, &err);

            if (!err)
                d->x = temp;
        }

        if (d->yprop) {
            temp = vsapi->propGetInt(m, "y_prop", 0, &err);

            if (!err)
                d->y = temp;
        }

        if (cropAbsVerify(x, y, d->width, d->height, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fi, msg)) {
            vsapi->setFilterError(msg, frameCtx);
            return NULL;
        }

        // now that argument validation is over we can spend the next few lines actually cropping
        for (plane = 0; plane < fi->numPlanes; plane++) {
            int rowsize = (d->width >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            int srcstride = vsapi->getStride(src, plane);
            int dststride = vsapi->getStride(dst, plane);
            int pheight = vsapi->getFrameHeight(dst, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            srcdata += srcstride * (d->y >> (plane ? fi->subSamplingH : 0));
            srcdata += (d->x >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;

            for (hloop = 0; hloop < pheight; hloop++) {
                memcpy(dstdata, srcdata, rowsize);
                srcdata += srcstride;
                dstdata += dststride;
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC cropAbsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    CropAbsData d;
    CropAbsData *data;
    const VSNodeRef *cref;
    int err;

    d.x = vsapi->propGetInt(in, "x", 0, &err);
    d.xprop = vsapi->propGetData(in, "x_prop", 0, &err);
    d.y = vsapi->propGetInt(in, "y", 0, &err);
    d.yprop = vsapi->propGetData(in, "y_prop", 0, &err);

    d.height = vsapi->propGetInt(in, "height", 0, 0);
    d.width = vsapi->propGetInt(in, "width", 0, 0);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    d.vi = vsapi->getVideoInfo(d.node);

    if (cropAbsVerify(d.x, d.y, d.width, d.height, d.vi->width, d.vi->height, d.vi->format, msg)) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "CropAbs", cropAbsInit, cropAbsGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// CropRel

static void VS_CC cropRelCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    CropAbsData d;
    CropAbsData *data;
    const VSNodeRef *cref;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("CropRel: constant format needed... for now");
    }

    d.x = vsapi->propGetInt(in, "left", 0, &err);
    d.y = vsapi->propGetInt(in, "top", 0, &err);

    d.height = d.vi->height - d.y - vsapi->propGetInt(in, "bottom", 0, &err);
    d.width = d.vi->width - d.x - vsapi->propGetInt(in, "right", 0, &err);

    // passthrough for the no cropping case
    if (d.x == 0 && d.y == 0 && d.width == d.vi->width && d.height == d.vi->height) {
        vsapi->propSetNode(out, "clip", d.node, 0);
        vsapi->freeNode(d.node);
        return;
    }

    if (cropAbsVerify(d.x, d.y, d.width, d.height, d.vi->width, d.vi->height, d.vi->format, msg)) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "CropAbs", cropAbsInit, cropAbsGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// AddBorders

typedef struct {
    const VSNodeRef *node;
    const VSVideoInfo *vi;
    int left;
    int right;
    int top;
    int bottom;
} AddBordersData;

static void VS_CC addBordersInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = (AddBordersData *) * instanceData;
    VSVideoInfo vi = *d->vi;
    vi.height += vi.height ? d->top + d->bottom : 0;
    vi.width += vi.width ? d->left + d->right : 0;
    vsapi->setVideoInfo(&vi, node);
}

static int addBordersVerify(int left, int right, int top, int bottom, const VSFormat *fi, char *msg) {
    msg[0] = 0;

    if (fi) {
        if (left % (1 << fi->subSamplingW))
            sprintf(msg, "AddBorders: added area needs to have mod %d width", 1 << fi->subSamplingW);

        if (right % (1 << fi->subSamplingW))
            sprintf(msg, "AddBorders: added area needs to have mod %d width", 1 << fi->subSamplingW);

        if (top % (1 << fi->subSamplingH))
            sprintf(msg, "AddBorders: added area needs to have mod %d height", 1 << fi->subSamplingH);

        if (bottom % (1 << fi->subSamplingH))
            sprintf(msg, "AddBorders: added area needs to have mod %d height", 1 << fi->subSamplingH);
    }

    if (msg[0])
        return 1;
    else
        return 0;
}

static const VSFrameRef *VS_CC addBordersGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = (AddBordersData *) * instanceData;
    char msg[150];

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        int hloop;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst;

        if (addBordersVerify(d->left, d->right, d->top, d->bottom, fi, msg)) {
            vsapi->setFilterError(msg, frameCtx);
            return NULL;
        }

        dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0) + d->left + d->right, vsapi->getFrameHeight(src, 0) + d->top + d->bottom, src, core);

        // now that argument validation is over we can spend the next few lines actually adding borders
        for (plane = 0; plane < fi->numPlanes; plane++) {
            int rowsize = vsapi->getFrameWidth(src, plane) * fi->bytesPerSample;
            int srcstride = vsapi->getStride(src, plane);
            int dststride = vsapi->getStride(dst, plane);
            int srcheight = vsapi->getFrameHeight(src, plane);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane);
            uint8_t *dstdata = vsapi->getWritePtr(dst, plane);
            int padt = d->top >> (plane ? fi->subSamplingH : 0);
            int padb = d->bottom >> (plane ? fi->subSamplingH : 0);
            int padl = d->left >> (plane ? fi->subSamplingW : 0) * fi->bytesPerSample;
            int padr = d->right >> (plane ? fi->subSamplingW : 0) * fi->bytesPerSample;
            memset(dstdata, 0, padt * dststride);
            dstdata += padt * dststride;

            for (hloop = 0; hloop < srcheight; hloop++) {
                memset(dstdata, 0, padl);
                memcpy(dstdata + padl, srcdata, rowsize);
                memset(dstdata + padl + rowsize, 0, padr);
                dstdata += dststride;
                srcdata += srcstride;
            }

            memset(dstdata, 0, padb * dststride);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC addBordersCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    AddBordersData d;
    AddBordersData *data;
    const VSNodeRef *cref;
    int err;

    d.left = vsapi->propGetInt(in, "left", 0, &err);
    d.right = vsapi->propGetInt(in, "right", 0, &err);
    d.top = vsapi->propGetInt(in, "top", 0, &err);
    d.bottom = vsapi->propGetInt(in, "bottom", 0, &err);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    d.vi = vsapi->getVideoInfo(d.node);

    if (isCompatFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("AddBorders: compat formats not supported");
    }

    if (addBordersVerify(d.left, d.right, d.top, d.bottom, d.vi->format, msg)) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "AddBorders", addBordersInit, addBordersGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Trim

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo vi;
    int first;
    int last;
    int length;
    int trimlen;
} TrimData;

static void VS_CC trimInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TrimData *d = (TrimData *) * instanceData;
    d->vi.numFrames = d->trimlen;
    vsapi->setVideoInfo(&d->vi, node);
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
    const VSNodeRef *cref;
    int firstset;
    int lastset;
    int lengthset;
    int err;
    d.first = 0;
    d.last = -1;
    d.length = -1;

    d.first = vsapi->propGetInt(in, "first", 0, &err);
    firstset = !err;
    d.last = vsapi->propGetInt(in, "last", 0, &err);
    lastset =  !err;
    d.length = vsapi->propGetInt(in, "length", 0, &err);
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

    cref = vsapi->createFilter(in, out, "Trim", trimInit, trimGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Interleave

typedef struct {
    const VSNodeRef **node;
    VSVideoInfo vi;
    int numclips;
} InterleaveData;

static void VS_CC interleaveInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = (InterleaveData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
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
}

static void VS_CC interleaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InterleaveData d;
    InterleaveData *data;
    const VSNodeRef *cref;
    int mismatch;
    int i;
    int err;
    int compat;

    mismatch = vsapi->propGetInt(in, "mismatch", 0, &err);
    d.numclips = vsapi->propNumElements(in, "clips");

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
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

        d.vi.numFrames *= d.numclips;
        d.vi.fpsNum *= d.numclips;

        data = malloc(sizeof(d));
        *data = d;

        cref = vsapi->createFilter(in, out, "Interleave", interleaveInit, interleaveGetframe, interleaveFree, fmParallel, nfNoCache, data, core);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
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
    const VSNodeRef *cref;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->numFrames) {
        vsapi->freeNode(d.node);
        RETERROR("Reverse: cannot reverse clips with unknown length");
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "Reverse", singleClipInit, reverseGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Loop

typedef struct {
    const VSNodeRef *node;
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

    vsapi->setVideoInfo(&vi, node);
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
    const VSNodeRef *cref;
    int err;

    d.times = vsapi->propGetInt(in, "times", 0, &err);
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

    cref = vsapi->createFilter(in, out, "Loop", loopInit, loopGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// ShufflePlanes

typedef struct {
    const VSNodeRef *node[3];
    VSVideoInfo vi;
    int plane[3];
    int format;
} ShufflePlanesData;

static void VS_CC shufflePlanesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
}

static const VSFrameRef *VS_CC shufflePlanesGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node[0], frameCtx);

        if (d->node[1] && d->node[1] != d->node[0])
            vsapi->requestFrameFilter(n, d->node[1], frameCtx);

        if (d->node[2] && d->node[2] != d->node[0] && d->node[2] != d->node[1])
            vsapi->requestFrameFilter(n, d->node[2], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        if (d->vi.format->colorFamily != cmGray) {
            int i;
            const VSFrameRef *src[3];
            VSFrameRef *dst;

            for (i = 0; i < 3; i++)
                src[i] = vsapi->getFrameFilter(n, d->node[i], frameCtx);

            dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src[0], core);

            for (i = 0; i < 3; i++) {
                memcpy(
                    vsapi->getWritePtr(dst, i),
                    vsapi->getReadPtr(src[i], d->plane[i]),
                    vsapi->getStride(dst, i) * vsapi->getFrameHeight(dst, i));
                vsapi->freeFrame(src[i]);
            }

            return dst;
        } else {
            VSFrameRef *dst;
            const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
            const VSFormat *fi = vsapi->getFrameFormat(src);

            if (d->plane[0] >= fi->numPlanes) {
                vsapi->freeFrame(src);
                vsapi->setFilterError("ShufflePlanes: invalid plane specified", frameCtx);
                return 0;
            }

            dst = vsapi->newVideoFrame(
                      d->vi.format ? d->vi.format : vsapi->registerFormat(cmGray, fi->sampleType, fi->bitsPerSample, 0, 0, core),
                      vsapi->getFrameWidth(src, d->plane[0]),
                      vsapi->getFrameHeight(src, d->plane[0]),
                      src,
                      core);

            memcpy(
                vsapi->getWritePtr(dst, 0),
                vsapi->getReadPtr(src, d->plane[0]),
                vsapi->getStride(dst, 0) * vsapi->getFrameHeight(dst, 0));

            vsapi->freeFrame(src);
            return dst;
        }
    }

    return 0;
}

static void VS_CC shufflePlanesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *)instanceData;
    int i;

    for (i = 0; i < 3; i++)
        vsapi->freeNode(d->node[i]);

    free(d);
}

#define SHUFFLEERROR(x) do { for (i = 0; i < 3; i++) vsapi->freeNode(d.node[i]); RETERROR(x); } while (0)

static int findSubSampling(int s1, int s2) {
    int i;

    for (i = 0; i < 6; i++)
        if (s1 - (s2 << i) == 0)
            return i;

    return -1;
}

static void VS_CC shufflePlanesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData d;
    ShufflePlanesData *data;
    const VSNodeRef *cref;
    int nclips = vsapi->propNumElements(in, "clips");
    int nplanes = vsapi->propNumElements(in, "planes");
    int i;
    int err;
    int outplanes;

    for (i = 0; i < 3; i++) {
        d.plane[i] = 0;
        d.node[i] = 0;
    }

    d.format = vsapi->propGetInt(in, "format", 0, 0);

    if (d.format != cmRGB && d.format != cmYUV && d.format != cmYCoCg && d.format != cmGray)
        RETERROR("ShufflePlanes: invalid output colorspace");

    outplanes = (d.format == cmGray ? 1 : 3);

    // please don't make this assumption if you ever write a plugin, it's only accepted in the core where all existing color families may be known
    if (nclips > outplanes)
        RETERROR("ShufflePlanes: 1-3 clips need to be specified");

    if (nplanes > outplanes)
        RETERROR("ShufflePlanes: too many planes specified");

    for (i = 0; i < nplanes; i++)
        d.plane[i] = vsapi->propGetInt(in, "planes", i, 0);

    for (i = 0; i < 3; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, &err);

    for (i = 0; i < 3; i++) {
        if (d.node[i] && isCompatFormat(vsapi->getVideoInfo(d.node[i])))
            SHUFFLEERROR("ShufflePlanes: compat formats not supported");

        //if (d.plane[i] < 0 || (vsapi->getVideoInfo(d.node[i])->format && d.plane[i] >= vsapi->getVideoInfo(d.node[i])->format->numPlanes))
        //    SHUFFLEERROR("ShufflePlanes: invalid plane specified");
    }

    if (d.format != cmGray && nclips == 1) {
        d.node[1] = vsapi->cloneNodeRef(d.node[0]);
        d.node[2] = vsapi->cloneNodeRef(d.node[0]);
    } else if (d.format != cmGray && nclips == 2) {
        d.node[2] = vsapi->cloneNodeRef(d.node[1]);
    }

    for (i = 0; i < outplanes; i++) {
        if (d.plane[i] < 0 || (vsapi->getVideoInfo(d.node[i])->format && d.plane[i] >= vsapi->getVideoInfo(d.node[i])->format->numPlanes))
            SHUFFLEERROR("ShufflePlanes: invalid plane specified");
    }

    d.vi = *vsapi->getVideoInfo(d.node[0]);

    // compatible format checks
    if (d.format == cmGray) {
        // gray is always compatible and special, it can work with variable input size clips
        if (d.vi.format)
            d.vi.format = vsapi->registerFormat(cmGray, d.vi.format->sampleType, d.vi.format->bitsPerSample, 0, 0, core);
        d.vi.width = planeWidth(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        d.vi.height = planeHeight(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
    } else {
        // no variable size video with more than one plane, it's just crazy
        int c0height = planeHeight(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        int c0width = planeWidth(vsapi->getVideoInfo(d.node[0]), d.plane[0]);
        int c1height = planeHeight(vsapi->getVideoInfo(d.node[1]), d.plane[1]);
        int c1width = planeWidth(vsapi->getVideoInfo(d.node[1]), d.plane[1]);
        int c2height = planeHeight(vsapi->getVideoInfo(d.node[2]), d.plane[2]);
        int c2width = planeWidth(vsapi->getVideoInfo(d.node[2]), d.plane[2]);
        int ssH;
        int ssW;

        if (!isConstantFormat(&d.vi) || !isConstantFormat(vsapi->getVideoInfo(d.node[1])) || !isConstantFormat(vsapi->getVideoInfo(d.node[2])))
            SHUFFLEERROR("ShufflePlanes: constant format video required");

        if (c1width != c2width || c1height != c2height)
            SHUFFLEERROR("ShufflePlanes: plane 1 and 2 size must match");

        ssH = findSubSampling(c0height, c1height);
        ssW = findSubSampling(c0width, c1width);

        if (ssH < 0 || ssW < 0)
            SHUFFLEERROR("ShufflePlanes: plane 1 and 2 not same size or subsampled multiples");

        for (i = 1; i < 3; i++) {
            const VSVideoInfo *pvi = vsapi->getVideoInfo(d.node[i]);

            if (d.vi.numFrames && (!pvi->numFrames || d.vi.numFrames < pvi->numFrames))
                d.vi.numFrames = pvi->numFrames;

            // simple binary compatibility
            if (d.vi.format->bitsPerSample != pvi->format->bitsPerSample ||
                    d.vi.format->sampleType != pvi->format->sampleType)
                SHUFFLEERROR("ShufflePlanes: plane 1 and 2 do not have binary compatible storage");
        }

        d.vi.format = vsapi->registerFormat(d.format, d.vi.format->sampleType, d.vi.format->bitsPerSample, ssW, ssH, core);
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "ShufflePlanes", shufflePlanesInit, shufflePlanesGetframe, shufflePlanesFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// SelectEvery

typedef struct {
    const VSNodeRef *node;
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
    vsapi->setVideoInfo(&vi, node);
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
}

static void VS_CC selectEveryCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData d;
    SelectEveryData *data;
    const VSNodeRef *cref;
    int i;
    d.cycle = vsapi->propGetInt(in, "cycle", 0, 0);

    if (d.cycle <= 1)
        RETERROR("SelectEvery: invalid cycle size");

    d.num = vsapi->propNumElements(in, "offsets");
    d.offsets = malloc(sizeof(d.offsets[0]) * d.num);

    for (i = 0; i < d.num; i++) {
        d.offsets[i] = vsapi->propGetInt(in, "offsets", i, 0);

        if (d.offsets[i] < 0 || d.offsets[i] >= d.cycle) {
            free(d.offsets);
            RETERROR("SelectEvery: invalid offset specified");
        }
    }

    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "SelectEvery", selectEveryInit, selectEveryGetframe, selectEveryFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// SeparateFields

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
} SeparateFieldsData;

static void VS_CC separateFieldsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;
    d->vi.numFrames *= 2;
    d->vi.height /= 2;
    d->vi.fpsNum *= 2;
    vsapi->setVideoInfo(&d->vi, node);
}

static const VSFrameRef *VS_CC separateFieldsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / 2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n / 2, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        int plane;
        vsapi->propSetInt(vsapi->getFramePropsRW(dst), "_Field", ((n & 1) ^ d->tff) + 1, 0);

        for (plane = 0; plane < d->vi.format->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(dst, plane);
            int hl;

            if (!((n & 1) ^ d->tff))
                srcp += src_stride;

            src_stride *= 2;

            for (hl = 0; hl < h; hl++) {
                memcpy(dstp, srcp, dst_stride);
                srcp += src_stride;
                dstp += dst_stride;
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC separateFieldsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData d;
    SeparateFieldsData *data;
    const VSNodeRef *cref;

    d.tff = vsapi->propGetInt(in, "tff", 0, 0);
    d.tff = !!d.tff;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("SeparateFields: clip must be constant format");
    }

    if (d.vi.height % (1 << (d.vi.format->subSamplingH + 1))) {
        vsapi->freeNode(d.node);
        RETERROR("SeparateFields: clip height must be mod 2 in the smallest subsampled plane");
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "SeparateFields", separateFieldsInit, separateFieldsGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// DoubleWeave

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
} DoubleWeaveData;

static void VS_CC doubleWeaveInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = (DoubleWeaveData *) * instanceData;
    d->vi.height *= 2;
    vsapi->setVideoInfo(&d->vi, node);
}

static const VSFrameRef *VS_CC doubleWeaveGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = (DoubleWeaveData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n + 1, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        int par = (n & 1) ^ d->tff;
        const VSFrameRef *srctop = vsapi->getFrameFilter(n + !par, d->node, frameCtx);
        const VSFrameRef *srcbtn = vsapi->getFrameFilter(n + par, d->node, frameCtx);

        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, srctop, core);
        vsapi->propDeleteKey(vsapi->getFramePropsRW(dst), "_Field");

        for (plane = 0; plane < d->vi.format->numPlanes; plane++) {
            const uint8_t *srcptop = vsapi->getReadPtr(srctop, plane);
            const uint8_t *srcpbtn = vsapi->getReadPtr(srcbtn, plane);
            int src_stride = vsapi->getStride(srcbtn, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(srctop, plane);
            int hl;

            for (hl = 0; hl < h; hl++) {
                memcpy(dstp, srcptop, dst_stride);
                dstp += dst_stride;
                memcpy(dstp, srcpbtn, dst_stride);
                srcpbtn += src_stride;
                srcptop += src_stride;
                dstp += dst_stride;
            }
        }

        vsapi->freeFrame(srcbtn);
        vsapi->freeFrame(srctop);
        return dst;
    }

    return 0;
}

static void VS_CC doubleWeaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData d;
    DoubleWeaveData *data;
    const VSNodeRef *cref;

    d.tff = vsapi->propGetInt(in, "tff", 0, 0);
    d.tff = !!d.tff;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("DoubleWeave: clip must be constant format");
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "DoubleWeave", doubleWeaveInit, doubleWeaveGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Splice

typedef struct {
    const VSNodeRef **node;
    VSVideoInfo vi;
    int *numframes;
    int numclips;
} SpliceData;

static void VS_CC spliceInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SpliceData *d = (SpliceData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
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
}

static void VS_CC spliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SpliceData d;
    SpliceData *data;
    const VSNodeRef *cref;
    int mismatch;
    int i;
    int err;
    int compat = 0;

    d.numclips = vsapi->propNumElements(in, "clips");
    mismatch = vsapi->propGetInt(in, "mismatch", 0, &err);

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
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

        cref = vsapi->createFilter(in, out, "Splice", spliceInit, spliceGetframe, spliceFree, fmParallel, nfNoCache, data, core);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
    }
}

//////////////////////////////////////////
// FlipVertical

static const VSFrameRef *VS_CC flipVerticalGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);
            int hl;
            dstp += dst_stride * (h - 1);

            for (hl = 0; hl < h; hl++) {
                memcpy(dstp, srcp, dst_stride);
                dstp -= dst_stride;
                srcp += src_stride;
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC flipVerticalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData d;
    SingleClipData *data;
    const VSNodeRef *cref;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "FlipVertical", singleClipInit, flipVerticalGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// FlipHorizontal

typedef struct {
    const VSNodeRef *node;
    int flip;
} FlipHorizontalData;

static const VSFrameRef *VS_CC flipHorizontalGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    // optimize, pshufb, pshufw, palignr could make flipping a lot faster
    FlipHorizontalData *d = (FlipHorizontalData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);
            int hl;
            int w = vsapi->getFrameWidth(src, plane) - 1;
            int x;

            if (d->flip) {
                dstp += dst_stride * (h - 1);
                dst_stride = -dst_stride;
            }

            switch (fi->bytesPerSample) {
            case 1:

                for (hl = 0; hl < h; hl++) {
                    for (x = 0; x <= w; x++)
                        dstp[w - x] = srcp[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            case 2:

                for (hl = 0; hl < h; hl++) {
                    const int16_t *srcp16 = (const int16_t *)srcp;
                    int16_t *dstp16 = (int16_t *)dstp;

                    for (x = 0; x <= w; x++)
                        dstp16[w - x] = srcp16[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            case 4:

                for (hl = 0; hl < h; hl++) {
                    const int32_t *srcp16 = (const int32_t *)srcp;
                    int32_t *dstp16 = (int32_t *)dstp;

                    for (x = 0; x <= w; x++)
                        dstp16[w - x] = srcp16[x];

                    dstp += dst_stride;
                    srcp += src_stride;
                }

                break;
            default:
                vsapi->setFilterError("FlipHorizontal: Unsupported sample size", frameCtx);
            }

        }

        vsapi->freeFrame(src);
        return dst;

    }

    return 0;
}

static void VS_CC flipHorizontalCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FlipHorizontalData d;
    FlipHorizontalData *data;
    const VSNodeRef *cref;

    d.flip = (intptr_t)(userData);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, d.flip ? "Turn180" : "FlipHorizontal", singleClipInit, flipHorizontalGetframe, singleClipFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Stack

typedef struct {
    const VSNodeRef **node;
    VSVideoInfo vi;
    int numclips;
    int vertical;
} StackData;

static void VS_CC stackInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
}

static const VSFrameRef *VS_CC stackGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *) * instanceData;
    int i;

    if (activationReason == arInitial) {
        for (i = 0; i < d->numclips; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node[0], frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        vsapi->freeFrame(src);

        for (plane = 0; plane < d->vi.format->numPlanes; plane++) {
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);

            for (i = 0; i < d->numclips; i++) {
                src = vsapi->getFrameFilter(n, d->node[i], frameCtx);

                if (d->vertical) {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int size = dst_stride * vsapi->getFrameHeight(src, plane);
                    memcpy(dstp, srcp, size);
                    dstp += size;
                } else {
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int src_stride = vsapi->getStride(src, plane);
                    int rowsize = vsapi->getFrameWidth(src, plane) * d->vi.format->bytesPerSample;
                    bitblt(dstp, dst_stride,
                           srcp, src_stride,
                           rowsize,
                           vsapi->getFrameHeight(src, plane));
                    dstp += rowsize;
                }

                vsapi->freeFrame(src);
            }
        }

        return dst;
    }

    return 0;
}

static void VS_CC stackFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *)instanceData;
    int i;

    for (i = 0; i < d->numclips; i++)
        vsapi->freeNode(d->node[i]);

    free(d->node);
}

static void VS_CC stackCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    StackData d;
    StackData *data;
    const VSNodeRef *cref;
    int i;

    d.vertical = (intptr_t)userData;
    d.numclips = vsapi->propNumElements(in, "clips");

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
    } else {
        d.node = malloc(sizeof(d.node[0]) * d.numclips);

        for (i = 0; i < d.numclips; i++)
            d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

        d.vi = *vsapi->getVideoInfo(d.node[0]);

        for (i = 1; i < d.numclips; i++) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(d.node[i]);

            if (!vi->numFrames)
                d.vi.numFrames = 0;
            else if (d.vi.numFrames && d.vi.numFrames < vi->numFrames)
                d.vi.numFrames = vi->numFrames;

            if (!isConstantFormat(vi) || vi->format != d.vi.format || (d.vertical && vi->width != d.vi.width) || (!d.vertical && vi->height != d.vi.height)) {
                for (i = 0; i < d.numclips; i++)
                    vsapi->freeNode(d.node[i]);

                free(d.node);
                RETERROR("Stack: clip format and dimensions must match");
            }

            if (d.vertical)
                d.vi.height += vi->height;
            else
                d.vi.width += vi->width;
        }

        data = malloc(sizeof(d));
        *data = d;

        cref = vsapi->createFilter(in, out, d.vertical ? "StackVertical" : "StackHorizontal", stackInit, stackGetframe, stackFree, fmParallel, 0, data, core);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
        return;
    }
}

//////////////////////////////////////////
// BlankClip

typedef struct {
    VSFrameRef *f;
    VSVideoInfo vi;
} BlankClipData;

static void VS_CC blankClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC blankClipGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *) * instanceData;

    if (activationReason == arInitial) {
        return vsapi->cloneFrameRef(d->f);
    }

    return 0;
}

static void VS_CC blankClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BlankClipData *d = (BlankClipData *)instanceData;
    vsapi->freeFrame(d->f);
    free(d);
}

static void VS_CC blankClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    BlankClipData d;
    BlankClipData *data;
    const VSNodeRef *cref;
    const VSNodeRef *node;
    int hasvi = 0;
    int format = 0;
    int color[3] = { 0, 0, 0 };
    int ncolors;
    int plane;
    int64_t temp;
    int err;
    int i;
    int compat = 0;
    memset(&d.vi, 0, sizeof(d.vi));

    node = vsapi->propGetNode(in, "clip", 0, &err);

    if (!err) {
        d.vi = *vsapi->getVideoInfo(node);
        compat = isCompatFormat(&d.vi);
        vsapi->freeNode(node);
        hasvi = 1;
    }

    if (compat)
        RETERROR("BlankClip: compat formats not supported");

    temp = vsapi->propGetInt(in, "width", 0, &err);

    if (err && !hasvi)
        d.vi.width = 640;
    else
        d.vi.width = temp;

    temp = vsapi->propGetInt(in, "height", 0, &err);

    if (err && !hasvi)
        d.vi.height = 480;
    else
        d.vi.height = temp;

    temp = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (err && !hasvi)
        d.vi.fpsNum = 24;
    else
        d.vi.fpsNum = temp;

    temp = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err && !hasvi)
        d.vi.fpsDen = 1;
    else
        d.vi.fpsDen = temp;

    if (d.vi.fpsDen < 1 || d.vi.fpsNum < 1)
        RETERROR("BlankClip: Invalid framerate specified");

    format = vsapi->propGetInt(in, "format", 0, &err);

    if (err && !hasvi)
        d.vi.format = vsapi->getFormatPreset(pfRGB24, core);
    else
        d.vi.format = vsapi->getFormatPreset(format, core);

    if (!d.vi.format)
        RETERROR("BlankClip: Invalid format");

    temp = vsapi->propGetInt(in, "length", 0, &err);

    if (err && !hasvi)
        d.vi.numFrames = (d.vi.fpsNum * 10) / d.vi.fpsDen;
    else
        d.vi.numFrames = temp;

    if (d.vi.width <= 0 || d.vi.width % (1 << d.vi.format->subSamplingW))
        RETERROR("BlankClip: Invalid width");

    if (d.vi.height <= 0 || d.vi.height % (1 << d.vi.format->subSamplingH))
        RETERROR("BlankClip: Invalid height");

    if (d.vi.numFrames < 0)
        RETERROR("BlankClip: Invalid length");

    ncolors = vsapi->propNumElements(in, "color");

    if (ncolors == d.vi.format->numPlanes) {
        for (i = 0; i < ncolors; i++)
            color[i] = vsapi->propGetInt(in, "color", i, 0);
    } else if (ncolors) {
        RETERROR("BlankClip: invalid number of color values specified");
    }

    d.f = vsapi->newVideoFrame(d.vi.format, d.vi.width, d.vi.height, 0, core);

    for (plane = 0; plane < d.vi.format->numPlanes; plane++)
        memset(vsapi->getWritePtr(d.f, plane), color[plane], vsapi->getStride(d.f, plane) * vsapi->getFrameHeight(d.f, plane));

    vsapi->propSetInt(vsapi->getFramePropsRW(d.f), "_DurationNum", d.vi.fpsDen, 0);
    vsapi->propSetInt(vsapi->getFramePropsRW(d.f), "_DurationDen", d.vi.fpsNum, 0);

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "BlankClip", blankClipInit, blankClipGetframe, blankClipFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// AssumeFPS

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo vi;
} AssumeFPSData;

static void VS_CC assumeFPSInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = (AssumeFPSData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC assumeFPSGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = (AssumeFPSData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        VSMap *m = vsapi->getFramePropsRW(dst);
        vsapi->freeFrame(src);
        vsapi->propSetInt(m, "_DurationNum", d->vi.fpsDen, 0);
        vsapi->propSetInt(m, "_DurationDen", d->vi.fpsNum, 0);
        return dst;
    }

    return 0;
}

static void VS_CC assumeFPSCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData d;
    AssumeFPSData *data;
    const VSNodeRef *src;
    const VSNodeRef *cref;
    int hasfps = 0;
    int hassrc = 0;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    src = vsapi->propGetNode(in, "src", 0, &err);

    if (!err) {
        const VSVideoInfo *vi = vsapi->getVideoInfo(src);
        d.vi.fpsNum = vi->fpsNum;
        d.vi.fpsDen = vi->fpsDen;
        vsapi->freeNode(src);
        hassrc = 1;
    }

    d.vi.fpsNum = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (!err)
        hasfps = 1;

    d.vi.fpsDen = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err)
        d.vi.fpsDen = 1;

    if (hasfps == hassrc) {
        vsapi->freeNode(d.node);
        RETERROR("AssumeFPS: Need to specify source clip or fps");
    }

    if (d.vi.fpsDen < 1 || d.vi.fpsNum < 1) {
        vsapi->freeNode(d.node);
        RETERROR("AssumeFPS: Invalid framerate specified");
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "AssumeFPS", assumeFPSInit, assumeFPSGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Lut

typedef struct {
    const VSNodeRef *node;
    const VSVideoInfo *vi;
    void *lut;
    int process[3];
} LutData;

static void VS_CC lutInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    LutData *d = (LutData *) * instanceData;
    vsapi->setVideoInfo(d->vi, node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC lutGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LutData *d = (LutData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);

            if (d->process[plane]) {

                int hl;
                int w = vsapi->getFrameWidth(src, plane);
                int x;

                if (fi->bytesPerSample == 1) {
                    const uint8_t *lut = (uint8_t *)d->lut;

                    for (hl = 0; hl < h; hl++) {
                        for (x = 0; x < w; x++)
                            dstp[x] =  lut[srcp[x]];

                        dstp += dst_stride;
                        srcp += src_stride;
                    }
                } else {
                    const uint16_t *lut = (uint16_t *)d->lut;

                    for (hl = 0; hl < h; hl++) {
                        for (x = 0; x < w; x++)
                            ((uint16_t *)dstp)[x] =  lut[srcp[x]];

                        dstp += dst_stride;
                        srcp += src_stride;
                    }
                }
            } else {
                memcpy(dstp, srcp, src_stride * h);
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return 0;
}

static void VS_CC lutFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    LutData *d = (LutData *)instanceData;
    vsapi->freeNode(d->node);
    free(d->lut);
    free(d);
}

static void VS_CC lutCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    LutData d;
    LutData *data;
    const VSNodeRef *cref;
    int i;
    int n, m, o;

    for (i = 0; i < 3; i++)
        d.process[i] = 0;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample > 16 || isCompatFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: only clips with integer samples and up to 16 bit per channel precision supported");
    }

    n = d.vi->format->numPlanes;
    m = vsapi->propNumElements(in, "planes");

    for (i = 0; i < m; i++) {
        o = vsapi->propGetInt(in, "planes", i, 0);

        if (o < 0 || o >= n) {
            vsapi->freeNode(d.node);
            RETERROR("Lut: plane index out of range");
        }

        if (d.process[o]) {
            vsapi->freeNode(d.node);
            RETERROR("Lut: plane specified twice");
        }

        d.process[o] = 1;
    }

    n = 1 << d.vi->format->bitsPerSample;

    if (vsapi->propNumElements(in, "lut") != n) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: bad lut length");
    }

    d.lut = malloc(d.vi->format->bytesPerSample * n);

    if (d.vi->format->bytesPerSample == 1) {
        uint8_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node);
                RETERROR("Lut: lut value out of range");
            }

            lut[i] = v;
        }
    } else {
        uint16_t *lut = d.lut;

        for (i = 0; i < n; n++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node);
                RETERROR("Lut: lut value out of range");
            }

            lut[i] = v;
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "Lut", lutInit, lutGetframe, lutFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Lut2

typedef struct {
    const VSNodeRef *node[2];
    const VSVideoInfo *vi[2];
    void *lut;
    int process[3];
} Lut2Data;

static void VS_CC lut2Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    Lut2Data *d = (Lut2Data *) * instanceData;
    vsapi->setVideoInfo(d->vi[0], node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC lut2Getframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    Lut2Data *d = (Lut2Data *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node[0], frameCtx);
        vsapi->requestFrameFilter(n, d->node[1], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        const VSFrameRef *srcx = vsapi->getFrameFilter(n, d->node[0], frameCtx);
        const VSFrameRef *srcy = vsapi->getFrameFilter(n, d->node[1], frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(srcx);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(srcx, 0), vsapi->getFrameHeight(srcx, 0), srcx, core);

        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcpx = vsapi->getReadPtr(srcx, plane);
            const uint8_t *srcpy = vsapi->getReadPtr(srcy, plane);
            int src_stride = vsapi->getStride(srcx, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(srcx, plane);

            if (d->process[plane]) {
                int shift = fi->bitsPerSample;
                int hl;
                int w = vsapi->getFrameWidth(srcx, plane);
                int x;

                if (fi->bytesPerSample == 1) {
                    const uint8_t *lut = (uint8_t *)d->lut;

                    for (hl = 0; hl < h; hl++) {
                        for (x = 0; x < w; x++)
                            dstp[x] =  lut[(srcpy[x] << shift) + srcpx[x]];

                        dstp += dst_stride;
                        srcpx += src_stride;
                        srcpy += src_stride;
                    }
                } else {
                    const uint16_t *lut = (uint16_t *)d->lut;

                    for (hl = 0; hl < h; hl++) {
                        for (x = 0; x < w; x++)
                            ((uint16_t *)dstp)[x] =  lut[(srcpy[x] << shift) + srcpx[x]];

                        dstp += dst_stride;
                        srcpx += src_stride;
                        srcpy += src_stride;
                    }
                }
            } else {
                memcpy(dstp, srcpx, src_stride * h);
            }
        }

        vsapi->freeFrame(srcx);
        vsapi->freeFrame(srcy);
        return dst;
    }

    return 0;
}

static void VS_CC lut2Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    Lut2Data *d = (Lut2Data *)instanceData;
    vsapi->freeNode(d->node[0]);
    vsapi->freeNode(d->node[1]);
    free(d->lut);
    free(d);
}

static void VS_CC lut2Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    Lut2Data d;
    Lut2Data *data;
    const VSNodeRef *cref;
    int i;
    int n, m, o;

    for (i = 0; i < 3; i++)
        d.process[i] = 0;

    if (vsapi->propNumElements(in, "clips") != 2)
        RETERROR("Lut2: needs 2 clips");

    d.node[0] = vsapi->propGetNode(in, "clips", 0, 0);
    d.node[1] = vsapi->propGetNode(in, "clips", 1, 0);
    d.vi[0] = vsapi->getVideoInfo(d.node[0]);
    d.vi[1] = vsapi->getVideoInfo(d.node[1]);

    if (!isConstantFormat(d.vi[0]) || !isConstantFormat(d.vi[1])
            || d.vi[0]->format->sampleType != stInteger || d.vi[1]->format->sampleType != stInteger
            || d.vi[0]->format->bitsPerSample + d.vi[1]->format->bitsPerSample > 20
            || d.vi[0]->format->subSamplingH != d.vi[1]->format->subSamplingH
            || d.vi[0]->format->subSamplingW != d.vi[1]->format->subSamplingW
            || d.vi[0]->width != d.vi[1]->width || d.vi[0]->height != d.vi[1]->height || isCompatFormat(d.vi[0]) || isCompatFormat(d.vi[1])) {
        vsapi->freeNode(d.node[0]);
        vsapi->freeNode(d.node[1]);
        RETERROR("Lut2: only clips with integer samples, same dimensions, same subsampling and up to a total of 20 indexing bits supported");
    }

    n = d.vi[0]->format->numPlanes;
    m = vsapi->propNumElements(in, "planes");

    for (i = 0; i < m; i++) {
        o = vsapi->propGetInt(in, "planes", i, 0);

        if (o < 0 || o >= n) {
            vsapi->freeNode(d.node[0]);
            vsapi->freeNode(d.node[1]);
            RETERROR("Lut2: plane index out of range");
        }

        if (d.process[o]) {
            vsapi->freeNode(d.node[0]);
            vsapi->freeNode(d.node[1]);
            RETERROR("Lut2: plane specified twice");
        }

        d.process[o] = 1;
    }

    n = 1 << (d.vi[0]->format->bitsPerSample + d.vi[1]->format->bitsPerSample);

    if (vsapi->propNumElements(in, "lut") != n) {
        vsapi->freeNode(d.node[0]);
        vsapi->freeNode(d.node[1]);
        RETERROR("Lut2: bad lut length");
    }

    d.lut = malloc(d.vi[0]->format->bytesPerSample * n);

    if (d.vi[0]->format->bytesPerSample == 1) {
        uint8_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node[0]);
                vsapi->freeNode(d.node[1]);
                RETERROR("Lut2: lut value out of range");
            }

            lut[i] = v;
        }
    } else {
        uint16_t *lut = d.lut;

        for (i = 0; i < n; n++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node[0]);
                vsapi->freeNode(d.node[1]);
                RETERROR("Lut2: lut value out of range");
            }

            lut[i] = v;
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "Lut2", lut2Init, lut2Getframe, lut2Free, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// SelectClip

typedef struct {
    const VSNodeRef **node;
    const VSNodeRef **src;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    int numnode;
    int numsrc;
    VSMap *in;
    VSMap *out;
} SelectClipData;

static void VS_CC selectClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SelectClipData *d = (SelectClipData *) * instanceData;
    vsapi->setVideoInfo(d->vi, node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC selectClipGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SelectClipData *d = (SelectClipData *) * instanceData;
    int i;

    if (activationReason == arInitial) {
        *frameData = (void *) - 1;

        for (i = 0; i < d->numsrc; i++)
            vsapi->requestFrameFilter(n, d->src[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        intptr_t idx = (intptr_t) * frameData;

        if (idx < 0) {
            int err;
            const VSFrameRef *f;
            vsapi->propSetInt(d->in, "N", n, 0);

            for (i = 0; i < d->numsrc; i++) {
                f = vsapi->getFrameFilter(n, d->src[i], frameCtx);
                vsapi->propSetFrame(d->in, "F", f, 1);
                vsapi->freeFrame(f);
            }

            vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
            vsapi->clearMap(d->in);
            idx = vsapi->propGetInt(d->out, "val", 0, &err);

            if (idx < 0 || idx >= d->numnode)
                idx = 0;

            vsapi->clearMap(d->out);
            *frameData = (void *)idx;
            // special case where the needed frame has already been fetched as a property source
            f = vsapi->getFrameFilter(n, d->node[idx], frameCtx);

            if (f)
                return f;
            else
                vsapi->requestFrameFilter(n, d->node[idx], frameCtx);
        } else {
            return vsapi->getFrameFilter(n, d->node[idx], frameCtx);
        }
    }

    return 0;
}

static void VS_CC selectClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SelectClipData *d = (SelectClipData *)instanceData;
    int i;

    for (i = 0; i < d->numnode; i++)
        vsapi->freeNode(d->node[i]);

    for (i = 0; i < d->numsrc; i++)
        vsapi->freeNode(d->src[i]);

    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    free(d->node);
    free(d->src);
    free(d);
}

static void VS_CC selectClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SelectClipData d;
    SelectClipData *data;
    const VSNodeRef *cref;
    int i;

    d.numnode = vsapi->propNumElements(in, "clips");
    d.numsrc = vsapi->propNumElements(in, "src");

    if (d.numnode < 2)
        RETERROR("SelectClip: needs at least 2 input clips to select from");

    d.node = malloc(d.numnode * sizeof(d.node[0]));

    for (i = 0; i < d.numnode; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

    d.vi = vsapi->getVideoInfo(d.node[0]);

    for (i = 1; i < d.numnode; i++)
        if (!isConstantFormat(d.vi) || !isSameFormat(d.vi, vsapi->getVideoInfo(d.node[i]))) {
            for (i = 0; i < d.numnode; i++)
                vsapi->freeNode(d.node[i]);

            free(d.node);
            RETERROR("SelectClip: index clips must be the same constant format");
        }

    d.func = vsapi->propGetFunc(in, "selector", 0, 0);
    d.src = malloc(d.numsrc * sizeof(d.src[0]));

    for (i = 0; i < d.numsrc; i++)
        d.src[i] = vsapi->propGetNode(in, "src", i, 0);

    d.in = vsapi->newMap();
    d.out = vsapi->newMap();

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "SelectClip", selectClipInit, selectClipGetFrame, selectClipFree, fmUnordered, nfNoCache, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// ModifyProps

typedef struct {
    const VSNodeRef *node;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    VSMap *in;
} ModifyPropsData;

static void VS_CC modifyPropsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ModifyPropsData *d = (ModifyPropsData *) * instanceData;
    vsapi->setVideoInfo(d->vi, node);
    vsapi->clearMap(in);
}

static const VSFrameRef *VS_CC modifyPropsGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ModifyPropsData *d = (ModifyPropsData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *f;
        VSFrameRef *fmod;
        vsapi->propSetInt(d->in, "N", n, 0);
        f = vsapi->getFrameFilter(n, d->node, frameCtx);
        fmod = vsapi->copyFrame(f, core);
        vsapi->propSetFrame(d->in, "F0", f, 0);
        vsapi->callFunc(d->func, d->in, vsapi->getFramePropsRW(fmod), core, vsapi);
        vsapi->clearMap(d->in);
        vsapi->freeFrame(f);
        return fmod;
    }

    return 0;
}

static void VS_CC modifyPropsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ModifyPropsData *d = (ModifyPropsData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeMap(d->in);
    free(d);
}

static void VS_CC modifyPropsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ModifyPropsData d;
    ModifyPropsData *data;
    const VSNodeRef *cref;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);
    d.func = vsapi->propGetFunc(in, "selector", 0, 0);
    d.in = vsapi->newMap();

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "ModifyProps", selectClipInit, selectClipGetFrame, selectClipFree, fmUnordered, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Transpose

extern void vs_transpose_word(const uint8_t *src, int srcstride, uint8_t *dst, int dststride);
extern void vs_transpose_word_partial(const uint8_t *src, int srcstride, uint8_t *dst, int dststride, int dst_lines);
extern void vs_transpose_byte(const uint8_t *src, int srcstride, uint8_t *dst, int dststride);
extern void vs_transpose_byte_partial(const uint8_t *src, int srcstride, uint8_t *dst, int dststride, int dst_lines);

typedef struct {
    const VSNodeRef *node;
    VSVideoInfo vi;
    VSFuncRef *func;
    VSMap *in;
} TransposeData;

static void VS_CC transposeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, node);
}

static const VSFrameRef *VS_CC transposeGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        int plane;
        int x;
        int y;
        int width, modwidth;
        int height, modheight;
        const uint8_t *srcp;
        int src_stride;
        uint8_t *dstp;
        int dst_stride;
        int partial_lines;

        for (plane = 0; plane < d->vi.format->numPlanes; plane++) {
            width = vsapi->getFrameWidth(src, plane);
            height = vsapi->getFrameHeight(src, plane);
            srcp = vsapi->getReadPtr(src, plane);
            src_stride = vsapi->getStride(src, plane);
            dstp = vsapi->getWritePtr(dst, plane);
            dst_stride = vsapi->getStride(dst, plane);

            switch (d->vi.format->bytesPerSample) {
            case 1:
#if 1 // x86-4ever
                modwidth = width & ~7;
                modheight = height & ~7;

                for (y = 0; y < modheight; y += 8) {
                    for (x = 0; x < modwidth; x += 8)
                        vs_transpose_byte(srcp + src_stride * y + x, src_stride, dstp + dst_stride * x + y, dst_stride);

                    partial_lines = width - modwidth;

                    if (partial_lines > 0)
                        vs_transpose_byte_partial(srcp + src_stride * y + x, src_stride, dstp + dst_stride * x + y, dst_stride, partial_lines);
                }

                for (y = modheight; y < height; y++)
                    for (x = 0; x < width; x++)
                        dstp[dst_stride * x + y] = srcp[src_stride * y + x];

                break;
#else
                for (y = 0; y < height; y++)
                    for (x = 0; x < width; x++)
                        dstp[dst_stride * x + y] = srcp[src_stride * y + x];
                break;
#endif
            case 2:
                width -= 3;

                for (y = 0; y < height; y += 4) {
                    for (x = 0; x < width; x += 4)
                        vs_transpose_word(srcp + src_stride * y + x * 2, src_stride, dstp + dst_stride * x + y * 2, dst_stride);

                    partial_lines = width + 3 - x;

                    if (partial_lines > 0)
                        vs_transpose_word_partial(srcp + src_stride * y + x * 2, src_stride, dstp + dst_stride * x + y * 2, dst_stride, partial_lines);
                }

                break;
            case 4:
                ;
                break;
            }
        }

        vsapi->freeFrame(src);
        return dst;

    }

    return 0;
}

static void VS_CC transposeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC transposeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    TransposeData d;
    TransposeData *data;
    const VSNodeRef *cref;
    int temp;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    temp = d.vi.width;
    d.vi.width = d.vi.height;
    d.vi.height = temp;

    if (!isConstantFormat(&d.vi) || (d.vi.format->bytesPerSample != 1 && d.vi.format->bytesPerSample != 2) || isCompatFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("Transpose: clip must be constant format and be 1 or 2 bytes per sample");
    }

    if (d.vi.format->subSamplingH != d.vi.format->subSamplingW) {
        vsapi->freeNode(d.node);
        RETERROR("Transpose: Clip must have same subsampling in both dimensions");
    }

    data = malloc(sizeof(d));
    *data = d;

    cref = vsapi->createFilter(in, out, "Transpose", transposeInit, transposeGetFrame, transposeFree, fmParallel, 0, data, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
    return;
}

//////////////////////////////////////////
// Init

void VS_CC stdlibInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("CropAbs", "clip:clip;width:int;height:int;x:int:opt:link;y:int:opt:link;", cropAbsCreate, 0, plugin);
    registerFunc("CropRel", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", cropRelCreate, 0, plugin);
    registerFunc("AddBorders", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", addBordersCreate, 0, plugin);
    registerFunc("Trim", "clip:clip;first:int:opt;last:int:opt;length:int:opt;", trimCreate, 0, plugin);;
    registerFunc("Reverse", "clip:clip;", reverseCreate, 0, plugin);
    registerFunc("Loop", "clip:clip;times:int:opt;", loopCreate, 0, plugin);
    registerFunc("Interleave", "clips:clip[];mismatch:int:opt;", interleaveCreate, 0, plugin);
    registerFunc("ShufflePlanes", "clips:clip[];planes:int[];format:int;", shufflePlanesCreate, 0, plugin);
    registerFunc("SelectEvery", "clip:clip;cycle:int;offsets:int[];", selectEveryCreate, 0, plugin);
    registerFunc("SeparateFields", "clip:clip;tff:int;", separateFieldsCreate, 0, plugin);
    registerFunc("DoubleWeave", "clip:clip;tff:int;", doubleWeaveCreate, 0, plugin);
    registerFunc("Splice", "clips:clip[];mismatch:int:opt;", spliceCreate, 0, plugin);
    registerFunc("FlipVertical", "clip:clip;", flipVerticalCreate, 0, plugin);
    registerFunc("FlipHorizontal", "clip:clip;", flipHorizontalCreate, 0, plugin);
    registerFunc("Turn180", "clip:clip;", flipHorizontalCreate, (void *)1, plugin);
    registerFunc("StackVertical", "clips:clip[];", stackCreate, (void *)1, plugin);
    registerFunc("StackHorizontal", "clips:clip[];", stackCreate, 0, plugin);
    registerFunc("BlankClip", "clip:clip:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:int[]:opt;", blankClipCreate, 0, plugin);
    registerFunc("AssumeFPS", "clip:clip;src:clip:opt;fpsnum:int:opt;fpsden:int:opt;", assumeFPSCreate, 0, plugin);
    registerFunc("Lut", "clip:clip;lut:int[];planes:int[];", lutCreate, 0, plugin);
    registerFunc("Lut2", "clips:clip[];lut:int[];planes:int[];", lut2Create, 0, plugin);
    registerFunc("SelectClip", "clips:clip[];selector:func;src:clip[]:opt;", selectClipCreate, 0, plugin);
    registerFunc("ModifyProps", "clip:clip;selector:func;", modifyPropsCreate, 0, plugin);
    registerFunc("Transpose", "clip:clip;", transposeCreate, 0, plugin);
}
