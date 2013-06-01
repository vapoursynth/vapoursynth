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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "simplefilters.h"
#include "VSHelper.h"

#define RETERROR(x) do { vsapi->setError(out, (x)); return; } while (0)
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

//////////////////////////////////////////
// Shared

#define vs_memset8 memset

static inline void vs_memset16(void *ptr, int value, size_t num) {
	uint16_t *tptr = (uint16_t *)ptr;
	while (num-- > 0)
		*tptr++ = (uint16_t)value;
}

static inline void vs_memset32(void *ptr, int value, size_t num) {
	int32_t *tptr = (int32_t *)ptr;
	while (num-- > 0)
		*tptr++ = (int32_t)value;
}

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
} SingleClipData;

static void VS_CC singleClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *) * instanceData;
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node), 1, node);
}

static void VS_CC singleClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SingleClipData *d = (SingleClipData *)instanceData;
    vsapi->freeNode(d->node);
    free(instanceData);
}

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

// to detect compat formats
static int isCompatFormat(const VSVideoInfo *vi) {
    return vi->format && vi->format->colorFamily == cmCompat;
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
    VSNodeRef *node;
    const VSVideoInfo *vi;
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
    vsapi->setVideoInfo(&vi, 1, node);
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

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int plane;
        int hloop;
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        VSFrameRef *dst = vsapi->newVideoFrame(fi, d->width, d->height, src, core);
        const VSMap *m = vsapi->getFramePropsRO(src);

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
    int err;

    d.x = int64ToIntS(vsapi->propGetInt(in, "x", 0, &err));
    d.y = int64ToIntS(vsapi->propGetInt(in, "y", 0, &err));

    d.height = int64ToIntS(vsapi->propGetInt(in, "height", 0, 0));
    d.width = int64ToIntS(vsapi->propGetInt(in, "width", 0, 0));
    d.node = vsapi->propGetNode(in, "clip", 0, 0);

    d.vi = vsapi->getVideoInfo(d.node);

    if (cropAbsVerify(d.x, d.y, d.width, d.height, d.vi->width, d.vi->height, d.vi->format, msg)) {
        vsapi->freeNode(d.node);
        RETERROR(msg);
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "CropAbs", cropAbsInit, cropAbsGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// CropRel

static void VS_CC cropRelCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    char msg[150];
    CropAbsData d;
    CropAbsData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("CropRel: constant format needed... for now");
    }

    d.x = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d.y = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));

    d.height = d.vi->height - d.y - int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    d.width = d.vi->width - d.x - int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));

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

    vsapi->createFilter(in, out, "CropAbs", cropAbsInit, cropAbsGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// AddBorders

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int left;
    int right;
    int top;
    int bottom;
	union {
		uint32_t i[3];
		float f[3];
	} color;
} AddBordersData;

static void VS_CC addBordersInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AddBordersData *d = (AddBordersData *) * instanceData;
    VSVideoInfo vi = *d->vi;
    vi.height += vi.height ? d->top + d->bottom : 0;
    vi.width += vi.width ? d->left + d->right : 0;
    vsapi->setVideoInfo(&vi, 1, node);
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
            return 0;
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
            int padl = (d->left >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            int padr = (d->right >> (plane ? fi->subSamplingW : 0)) * fi->bytesPerSample;
			int color = d->color.i[plane];

			switch (d->vi->format->bytesPerSample) {
			case 1:
				vs_memset8(dstdata, color, padt * dststride);
				break;
			case 2:
				vs_memset16(dstdata, color, padt * dststride / 2);
				break;
			case 4:
				vs_memset32(dstdata, color, padt * dststride / 4);
				break;
			}
            dstdata += padt * dststride; 

            for (hloop = 0; hloop < srcheight; hloop++) {
				switch (d->vi->format->bytesPerSample) {
				case 1:
					vs_memset8(dstdata, color, padl);
					memcpy(dstdata + padl, srcdata, rowsize);
					vs_memset8(dstdata + padl + rowsize, color, padr);
					break;
				case 2:
					vs_memset16(dstdata, color, padl / 2);
					memcpy(dstdata + padl, srcdata, rowsize);
					vs_memset16(dstdata + padl + rowsize, color, padr / 2);
					break;
				case 4:
					vs_memset32(dstdata, color, padl / 4);
					memcpy(dstdata + padl, srcdata, rowsize);
					vs_memset32(dstdata + padl + rowsize, color, padr / 4);
					break;
				}

                dstdata += dststride;
                srcdata += srcstride;
            }

			switch (d->vi->format->bytesPerSample) {
			case 1:
				vs_memset8(dstdata, color, padb * dststride);
				break;
			case 2:
				vs_memset16(dstdata, color, padb * dststride / 2);
				break;
			case 4:
				vs_memset32(dstdata, color, padb * dststride / 4);
				break;
			}
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
    int err, ncolors, i;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
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

	ncolors = vsapi->propNumElements(in, "color");

    if (ncolors == d.vi->format->numPlanes) {
        for (i = 0; i < ncolors; i++) {
			double color = vsapi->propGetFloat(in, "color", i, 0);
			if (d.vi->format->sampleType == stInteger) {
				d.color.i[i] = (int)(color + 0.5);
				if (color < 0 || d.color.i[i] >= ((uint64_t)1 << d.vi->format->bitsPerSample))
					RETERROR("AddBorders: color value out of range");
			} else {
				d.color.f[i] = (float)color;
				if (d.vi->format->colorFamily == cmRGB || i == 0) {
					if (color < 0 || color > 1)
						RETERROR("AddBorders: color value out of range");
				} else {
					if (color < -0.5 || color > 0.5)
						RETERROR("AddBorders: color value out of range");
				}
			}
        }
    } else if (ncolors > 0) {
        RETERROR("AddBorders: invalid number of color values specified");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "AddBorders", addBordersInit, addBordersGetframe, singleClipFree, fmParallel, 0, data, core);
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
// ShufflePlanes

typedef struct {
    VSNodeRef *node[3];
    VSVideoInfo vi;
    int plane[3];
    int format;
} ShufflePlanesData;

static void VS_CC shufflePlanesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ShufflePlanesData *d = (ShufflePlanesData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
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

            dst = vsapi->newVideoFrame2(d->vi.format, d->vi.width, d->vi.height, src, d->plane, src[0], core);

            for (i = 0; i < 3; i++)
                vsapi->freeFrame(src[i]);

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

            dst = vsapi->newVideoFrame2(d->vi.format ? d->vi.format : vsapi->registerFormat(cmGray, fi->sampleType, fi->bitsPerSample, 0, 0, core),
                vsapi->getFrameWidth(src, d->plane[0]), vsapi->getFrameHeight(src, d->plane[0]), &src, d->plane, src, core);

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
    int nclips = vsapi->propNumElements(in, "clips");
    int nplanes = vsapi->propNumElements(in, "planes");
    int i;
    int err;
    int outplanes;

    for (i = 0; i < 3; i++) {
        d.plane[i] = 0;
        d.node[i] = 0;
    }

    d.format = int64ToIntS(vsapi->propGetInt(in, "format", 0, 0));

    if (d.format != cmRGB && d.format != cmYUV && d.format != cmYCoCg && d.format != cmGray)
        RETERROR("ShufflePlanes: invalid output colorspace");

    outplanes = (d.format == cmGray ? 1 : 3);

    // please don't make this assumption if you ever write a plugin, it's only accepted in the core where all existing color families may be known
    if (nclips > outplanes)
        RETERROR("ShufflePlanes: 1-3 clips need to be specified");

    if (nplanes > outplanes)
        RETERROR("ShufflePlanes: too many planes specified");

    for (i = 0; i < nplanes; i++)
        d.plane[i] = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

    for (i = 0; i < 3; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, &err);

    for (i = 0; i < 3; i++) {
        if (d.node[i] && isCompatFormat(vsapi->getVideoInfo(d.node[i])))
            SHUFFLEERROR("ShufflePlanes: compat formats not supported");
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

    vsapi->createFilter(in, out, "ShufflePlanes", shufflePlanesInit, shufflePlanesGetframe, shufflePlanesFree, fmParallel, 0, data, core);
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
// SeparateFields

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
} SeparateFieldsData;

static void VS_CC separateFieldsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;
    d->vi.numFrames *= 2;
    d->vi.height /= 2;
    d->vi.fpsNum *= 2;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC separateFieldsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SeparateFieldsData *d = (SeparateFieldsData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / 2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n / 2, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        int plane;
        vsapi->propSetInt(vsapi->getFramePropsRW(dst), "_Field", ((n & 1) ^ d->tff), 0);

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

    d.tff = !!vsapi->propGetInt(in, "tff", 0, 0);
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

    vsapi->createFilter(in, out, "SeparateFields", separateFieldsInit, separateFieldsGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// DoubleWeave

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    int tff;
} DoubleWeaveData;

static void VS_CC doubleWeaveInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    DoubleWeaveData *d = (DoubleWeaveData *) * instanceData;
    d->vi.height *= 2;
    vsapi->setVideoInfo(&d->vi, 1, node);
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

    d.tff = !!vsapi->propGetInt(in, "tff", 0, 0);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("DoubleWeave: clip must be constant format");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "DoubleWeave", doubleWeaveInit, doubleWeaveGetframe, singleClipFree, fmParallel, 0, data, core);
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

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "FlipVertical", singleClipInit, flipVerticalGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// FlipHorizontal

typedef struct {
    VSNodeRef *node;
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

    d.flip = (intptr_t)(userData);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, d.flip ? "Turn180" : "FlipHorizontal", singleClipInit, flipHorizontalGetframe, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Stack

typedef struct {
    VSNodeRef **node;
    VSVideoInfo vi;
    int numclips;
    int vertical;
} StackData;

static void VS_CC stackInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    StackData *d = (StackData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
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
                    vs_bitblt(dstp, dst_stride,
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
    free(d);
}

static void VS_CC stackCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    StackData d;
    StackData *data;
    VSNodeRef *cref;
    int i;

    d.vertical = (intptr_t)userData;
    d.numclips = vsapi->propNumElements(in, "clips");

    if (d.numclips == 1) { // passthrough for the special case with only one clip
        cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, 0);
        vsapi->freeNode(cref);
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

        vsapi->createFilter(in, out, d.vertical ? "StackVertical" : "StackHorizontal", stackInit, stackGetframe, stackFree, fmParallel, 0, data, core);
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
    vsapi->setVideoInfo(&d->vi, 1, node);
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
    VSNodeRef *node;
    int hasvi = 0;
    int format = 0;
	union {
		uint32_t i[3];
		float f[3];
	} color;
    int ncolors;
    int plane;
    int64_t temp;
    int err;
    int i;
    int compat = 0;
    memset(&d.vi, 0, sizeof(d.vi));

	for (i = 0; i < 3; i++)
		color.i[i] = 0;

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

    if (err) {
        if (!hasvi)
            d.vi.width = 640;
    }
    else
        d.vi.width = int64ToIntS(temp);

    temp = vsapi->propGetInt(in, "height", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.height = 480;
    }
    else
        d.vi.height = int64ToIntS(temp);

    temp = vsapi->propGetInt(in, "fpsnum", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.fpsNum = 24;
    }
    else
        d.vi.fpsNum = temp;

    temp = vsapi->propGetInt(in, "fpsden", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.fpsDen = 1;
    }
    else
        d.vi.fpsDen = temp;

    if (d.vi.fpsDen < 1 || d.vi.fpsNum < 1)
        RETERROR("BlankClip: Invalid framerate specified");

    format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));

    if (err) {
        if (!hasvi)
            d.vi.format = vsapi->getFormatPreset(pfRGB24, core);
    }
    else
        d.vi.format = vsapi->getFormatPreset(format, core);

    if (!d.vi.format)
        RETERROR("BlankClip: Invalid format");

	if (d.vi.format->sampleType != stInteger && !(d.vi.format->sampleType == stFloat && d.vi.format->bitsPerSample == 32))
        RETERROR("BlankClip: Invalid output format specified");

    temp = vsapi->propGetInt(in, "length", 0, &err);

    if (err) {
        if (!hasvi)
            d.vi.numFrames = int64ToIntS((d.vi.fpsNum * 10) / d.vi.fpsDen);
    }
    else
        d.vi.numFrames = int64ToIntS(temp);
    if (d.vi.width <= 0 || d.vi.width % (1 << d.vi.format->subSamplingW))
        RETERROR("BlankClip: Invalid width");

    if (d.vi.height <= 0 || d.vi.height % (1 << d.vi.format->subSamplingH))
        RETERROR("BlankClip: Invalid height");

    if (d.vi.numFrames < 0)
        RETERROR("BlankClip: Invalid length");

    ncolors = vsapi->propNumElements(in, "color");

    if (ncolors == d.vi.format->numPlanes) {
        for (i = 0; i < ncolors; i++) {
			double lcolor = vsapi->propGetFloat(in, "color", i, 0);
			if (d.vi.format->sampleType == stInteger) {
				color.i[i] = (int)(lcolor + 0.5);
				if (lcolor < 0 || color.i[i] >= ((int64_t)1 << d.vi.format->bitsPerSample))
					RETERROR("BlankClip: color value out of range");
			} else {
				color.f[i] = (float)lcolor;
				if (d.vi.format->colorFamily == cmRGB || i == 0) {
					if (lcolor < 0 || lcolor > 1)
						RETERROR("BlankClip: color value out of range");
				} else {
					if (lcolor < -0.5 || lcolor > 0.5)
						RETERROR("BlankClip: color value out of range");
				}
			}
        }
    } else if (ncolors > 0) {
        RETERROR("BlankClip: invalid number of color values specified");
    }

    d.f = vsapi->newVideoFrame(d.vi.format, d.vi.width, d.vi.height, 0, core);

    for (plane = 0; plane < d.vi.format->numPlanes; plane++) {
        switch (d.vi.format->bytesPerSample) {
        case 1:
			vs_memset8(vsapi->getWritePtr(d.f, plane), color.i[plane], vsapi->getStride(d.f, plane) * vsapi->getFrameHeight(d.f, plane));
            break;
        case 2:
			vs_memset16(vsapi->getWritePtr(d.f, plane), color.i[plane], vsapi->getStride(d.f, plane) * vsapi->getFrameHeight(d.f, plane) / 2);
            break;
        case 4:
			vs_memset32(vsapi->getWritePtr(d.f, plane), color.i[plane], vsapi->getStride(d.f, plane) * vsapi->getFrameHeight(d.f, plane) / 4);
            break;
        }
    }

    vsapi->propSetInt(vsapi->getFramePropsRW(d.f), "_DurationNum", d.vi.fpsDen, 0);
    vsapi->propSetInt(vsapi->getFramePropsRW(d.f), "_DurationDen", d.vi.fpsNum, 0);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "BlankClip", blankClipInit, blankClipGetframe, blankClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// AssumeFPS

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
} AssumeFPSData;

static void VS_CC assumeFPSInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AssumeFPSData *d = (AssumeFPSData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
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
    VSNodeRef *src;
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

    vsapi->createFilter(in, out, "AssumeFPS", assumeFPSInit, assumeFPSGetframe, singleClipFree, fmParallel, nfNoCache, data, core);
}

//////////////////////////////////////////
// Lut

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    void *lut;
    int process[3];
} LutData;

static void VS_CC lutInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    LutData *d = (LutData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
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
        const int pl[] = {0, 1, 2};
        const VSFrameRef *fr[] = {d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src};
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

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
                            ((uint16_t *)dstp)[x] =  lut[((uint16_t *)srcp)[x]];

                        dstp += dst_stride;
                        srcp += src_stride;
                    }
                }
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
        o = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

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

            lut[i] = (uint8_t)v;
        }
    } else {
        uint16_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node);
                RETERROR("Lut: lut value out of range");
            }

            lut[i] = (uint16_t)v;
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Lut", lutInit, lutGetframe, lutFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Lut2

typedef struct {
    VSNodeRef *node[2];
    const VSVideoInfo *vi[2];
    VSVideoInfo *vi_out;
    void *lut;
    int process[3];
} Lut2Data;

#define LUT2_PROCESS(X_CAST, Y_CAST, DST_CAST) \
    do { \
        for (hl = 0; hl < h; hl++) { \
            for (x = 0; x < w; x++) { \
                ((DST_CAST *)dstp)[x] =  lut[(((Y_CAST *)srcpy)[x] << shift) + ((X_CAST *)srcpx)[x]]; \
            } \
            dstp += dst_stride; \
            srcpx += srcx_stride; \
            srcpy += srcy_stride; \
        } \
    } while(0)

static void VS_CC lut2Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    Lut2Data *d = (Lut2Data *) * instanceData;
    vsapi->setVideoInfo(d->vi_out, 1, node);
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
        const VSFormat *fi = d->vi_out->format;
        const int pl[] = {0, 1, 2};
        const VSFrameRef *fr[] = {d->process[0] ? 0 : srcx, d->process[1] ? 0 : srcx, d->process[2] ? 0 : srcx};
        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(srcx, 0), vsapi->getFrameHeight(srcx, 0), fr, pl, srcx, core);

        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcpx = vsapi->getReadPtr(srcx, plane);
            const uint8_t *srcpy = vsapi->getReadPtr(srcy, plane);
            int srcx_stride = vsapi->getStride(srcx, plane);
            int srcy_stride = vsapi->getStride(srcy, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(srcx, plane);

            if (d->process[plane]) {
                int shift = d->vi[0]->format->bitsPerSample;
                int hl;
                int w = vsapi->getFrameWidth(srcx, plane);
                int x;

                if (fi->bytesPerSample == 1) {
                    const uint8_t *lut = (uint8_t *)d->lut;

                    if (d->vi[0]->format->bitsPerSample == 8 && d->vi[1]->format->bitsPerSample == 8) {
                        LUT2_PROCESS(uint8_t, uint8_t, uint8_t);
                    } else if (d->vi[0]->format->bitsPerSample == 8 && d->vi[1]->format->bitsPerSample > 8) {
                        LUT2_PROCESS(uint8_t, uint16_t, uint8_t);
                    } else if (d->vi[0]->format->bitsPerSample > 8 && d->vi[1]->format->bitsPerSample == 8) {
                        LUT2_PROCESS(uint16_t, uint8_t, uint8_t);
                    } else {
                        LUT2_PROCESS(uint16_t, uint16_t, uint8_t);
                    }
                } else {
                    const uint16_t *lut = (uint16_t *)d->lut;

                    if (d->vi[0]->format->bitsPerSample == 8 && d->vi[1]->format->bitsPerSample == 8) {
                        LUT2_PROCESS(uint8_t, uint8_t, uint16_t);
                    } else if (d->vi[0]->format->bitsPerSample == 8 && d->vi[1]->format->bitsPerSample > 8) {
                        LUT2_PROCESS(uint8_t, uint16_t, uint16_t);
                    } else if (d->vi[0]->format->bitsPerSample > 8 && d->vi[1]->format->bitsPerSample == 8) {
                        LUT2_PROCESS(uint16_t, uint8_t, uint16_t);
                    } else {
                        LUT2_PROCESS(uint16_t, uint16_t, uint16_t);
                    }
                }
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
    int i;
    int n, m, o;
	int err;
	int bits;

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
        o = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

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

    bits = int64ToIntS(vsapi->propGetInt(in, "bits", 0, &err));
    if (bits == 0) {
        bits = d.vi[0]->format->bitsPerSample;
    } else if (bits < 8 || bits > 16) {
        vsapi->freeNode(d.node[0]);
        vsapi->freeNode(d.node[1]);
        RETERROR("Lut2: Output format must be between 8 and 16 bits.");
    }

    d.vi_out = (VSVideoInfo *)malloc(sizeof(VSVideoInfo));
    *d.vi_out = *d.vi[0];
    d.vi_out->format = vsapi->registerFormat(d.vi[0]->format->colorFamily, d.vi[0]->format->sampleType, bits, d.vi[0]->format->subSamplingW, d.vi[0]->format->subSamplingH, core);

    if (bits == 8)
        d.lut = malloc(sizeof(uint8_t) * n);
    else
        d.lut = malloc(sizeof(uint16_t) * n);

    if (bits == 8) {
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

            lut[i] = (uint8_t)v;
        }
    } else {
        uint16_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int err;
            int64_t v = vsapi->propGetInt(in, "lut", i, &err);

            if (v < 0 || v >= n) {
                free(d.lut);
                vsapi->freeNode(d.node[0]);
                vsapi->freeNode(d.node[1]);
                RETERROR("Lut2: lut value out of range");
            }

            lut[i] = (uint16_t)v;
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Lut2", lut2Init, lut2Getframe, lut2Free, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// SelectClip

typedef struct {
    VSNodeRef **node;
    VSNodeRef **src;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    int numnode;
    int numsrc;
    VSMap *in;
    VSMap *out;
} SelectClipData;

static void VS_CC selectClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    SelectClipData *d = (SelectClipData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
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
        int idx = (int) * frameData;

        if (idx < 0) {
            int err;
            const VSFrameRef *f;
            vsapi->propSetInt(d->in, "n", n, paAppend);

            for (i = 0; i < d->numsrc; i++) {
                f = vsapi->getFrameFilter(n, d->src[i], frameCtx);
                vsapi->propSetFrame(d->in, "f", f, paAppend);
                vsapi->freeFrame(f);
            }

            vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
            vsapi->clearMap(d->in);
            if (vsapi->getError(d->out)) {
                vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
                vsapi->clearMap(d->out);
                return 0;
            }
            idx = int64ToIntS(vsapi->propGetInt(d->out, "val", 0, &err));
            vsapi->clearMap(d->out);

            if (idx < 0 || idx >= d->numnode) {
                vsapi->setFilterError("SelectClip: returned index out of bounds", frameCtx);
                return 0;
            }

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
    vsapi->freeFunc(d->func);
    free(d->node);
    free(d->src);
    free(d);
}

static void VS_CC selectClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    SelectClipData d;
    SelectClipData *data;
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

    d.in = vsapi->createMap();
    d.out = vsapi->createMap();

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "SelectClip", selectClipInit, selectClipGetFrame, selectClipFree, fmUnordered, nfNoCache, data, core);
}

//////////////////////////////////////////
// ModifyFrame

typedef struct {
    VSNodeRef **node;
    const VSVideoInfo *vi;
    VSFuncRef *func;
    VSMap *in;
    VSMap *out;
    int numnode;
} ModifyFrameData;

static void VS_CC modifyFrameInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC modifyFrameGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *) * instanceData;
    int i;

    if (activationReason == arInitial) {
        for (i = 0; i < d->numnode; i++)
            vsapi->requestFrameFilter(n, d->node[i], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *f;
        int err;

        vsapi->propSetInt(d->in, "n", n, 0);

        for (i = 0; i < d->numnode; i++) {
            f = vsapi->getFrameFilter(n, d->node[i], frameCtx);
            vsapi->propSetFrame(d->in, "f", f, 1);
            vsapi->freeFrame(f);
        }

        vsapi->callFunc(d->func, d->in, d->out, core, vsapi);
        vsapi->clearMap(d->in);

        if (vsapi->getError(d->out)) {
            vsapi->setFilterError(vsapi->getError(d->out), frameCtx);
            vsapi->clearMap(d->out);
            return 0;
        }

        f = vsapi->propGetFrame(d->out, "val", 0, &err);
        if (err) {
            vsapi->setFilterError("Returned value not a frame", frameCtx);
            vsapi->clearMap(d->out);
            return 0;
        }

        vsapi->clearMap(d->out);
        return f;
    }

    return 0;
}

static void VS_CC modifyFrameFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData *d = (ModifyFrameData *)instanceData;
    int i;
    for (i = 0; i < d->numnode; i++)
        vsapi->freeNode(d->node[i]);
    vsapi->freeMap(d->in);
    vsapi->freeMap(d->out);
    free(d->node);
    free(d);
}

static void VS_CC modifyFrameCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ModifyFrameData d;
    ModifyFrameData *data;
    int i;

    d.numnode = vsapi->propNumElements(in, "clips");
    d.node = malloc(d.numnode * sizeof(d.node[0]));
    for (i = 0; i < d.numnode; i++)
        d.node[i] = vsapi->propGetNode(in, "clips", i, 0);

    d.vi = vsapi->getVideoInfo(d.node[0]);
    d.func = vsapi->propGetFunc(in, "selector", 0, 0);
    d.in = vsapi->createMap();
    d.out = vsapi->createMap();

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ModifyFrame", modifyFrameInit, modifyFrameGetFrame, modifyFrameFree, fmUnordered, 0, data, core);
}

//////////////////////////////////////////
// Transpose

extern void vs_transpose_word(const uint8_t *src, int srcstride, uint8_t *dst, int dststride);
extern void vs_transpose_word_partial(const uint8_t *src, int srcstride, uint8_t *dst, int dststride, int dst_lines);
extern void vs_transpose_byte(const uint8_t *src, int srcstride, uint8_t *dst, int dststride);
extern void vs_transpose_byte_partial(const uint8_t *src, int srcstride, uint8_t *dst, int dststride, int dst_lines);

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
} TransposeData;

static void VS_CC transposeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TransposeData *d = (TransposeData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
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
#if 1 // x86-4ever
                modwidth = width & ~3;
                modheight = height & ~3;

                for (y = 0; y < modheight; y += 4) {
                    for (x = 0; x < modwidth; x += 4)
                        vs_transpose_word(srcp + src_stride * y + x * 2, src_stride, dstp + dst_stride * x + y * 2, dst_stride);

                    partial_lines = width - modwidth;

                    if (partial_lines > 0)
                        vs_transpose_word_partial(srcp + src_stride * y + x * 2, src_stride, dstp + dst_stride * x + y * 2, dst_stride, partial_lines);
                }

                src_stride /= 2;
                dst_stride /= 2;

                for (y = modheight; y < height; y++)
                    for (x = 0; x < width; x++)
                        ((uint16_t *)dstp)[dst_stride * x + y] = ((const uint16_t *)srcp)[src_stride * y + x];

                break;
#else
                src_stride /= 2;
                dst_stride /= 2;
                for (y = 0; y < height; y++)
                    for (x = 0; x < width; x++)
                        ((uint16_t *)dstp)[dst_stride * x + y] = ((const uint16_t *)srcp)[src_stride * y + x];
                break;
#endif
            case 4:
                src_stride /= 4;
                dst_stride /= 4;
                for (y = 0; y < height; y++)
                    for (x = 0; x < width; x++)
                        ((uint32_t *)dstp)[dst_stride * x + y] = ((const uint32_t *)srcp)[src_stride * y + x];
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
    int temp;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    temp = d.vi.width;
    d.vi.width = d.vi.height;
    d.vi.height = temp;

    if (!isConstantFormat(&d.vi) || d.vi.format->id == pfCompatYUY2) {
        vsapi->freeNode(d.node);
        RETERROR("Transpose: clip must be constant format and not CompatYuy2");
    }

    if (d.vi.format->subSamplingH != d.vi.format->subSamplingW) {
        vsapi->freeNode(d.node);
        RETERROR("Transpose: Clip must have same subsampling in both dimensions");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Transpose", transposeInit, transposeGetFrame, transposeFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// LevelVerifier

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int64_t upper[3];
    int64_t lower[3];
} PEMVerifierData;

static void VS_CC pemVerifierInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC pemVerifierGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int plane;
        int x;
        int y;
        int width;
        int height;
        const uint8_t *srcp;
        int src_stride;
        char strbuf[512];

        for (plane = 0; plane < d->vi->format->numPlanes; plane++) {
            width = vsapi->getFrameWidth(src, plane);
            height = vsapi->getFrameHeight(src, plane);
            srcp = vsapi->getReadPtr(src, plane);
            src_stride = vsapi->getStride(src, plane);

            switch (d->vi->format->bytesPerSample) {
            case 1:
                for (y = 0; y < height; y++) {
                    for (x = 0; x < width; x++)
                        if (srcp[x] < d->lower[plane] || srcp[x] > d->upper[plane]) {
                            sprintf(strbuf, "PEMVerifier: Illegal sample value (%d) at: plane: %d Y: %d, X: %d, Frame: %d", (int)srcp[x], plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return 0;
                        }
                    srcp += src_stride;
                }
                break;
            case 2:
                for (y = 0; y < height; y++) {
                    for (x = 0; x < width; x++)
                        if (((const uint16_t *)srcp)[x] < d->lower[plane] || ((const uint16_t *)srcp)[x] > d->upper[plane]) {
                            sprintf(strbuf, "PEMVerifier: Illegal sample value (%d) at: plane: %d Y: %d, X: %d, Frame: %d", (int)((const uint16_t *)srcp)[x], plane, y, x, n);
                            vsapi->setFilterError(strbuf, frameCtx);
                            vsapi->freeFrame(src);
                            return 0;
                        }
                    srcp += src_stride;
                }
                break;
            }
        }
        return src;
    }
    return 0;
}

static void VS_CC pemVerifierFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData *d = (PEMVerifierData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC pemVerifierCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PEMVerifierData d;
    PEMVerifierData *data;
    int i;
    int numupper = vsapi->propNumElements(in, "upper");
    int numlower = vsapi->propNumElements(in, "lower");

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || isCompatFormat(d.vi) || d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 &&  d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: clip must be constant format and of integer 8-16 bit type");
    }

    if (numlower < 0) {
        for (i = 0; i < d.vi->format->numPlanes; i++)
            d.lower[i] = 0;
    } else if (numlower == d.vi->format->numPlanes) {
        for (i = 0; i < d.vi->format->numPlanes; i++) {
            d.lower[i] = vsapi->propGetInt(in, "lower", i, 0);
            if (d.lower[i] < 0 || d.lower[i] >= ((int64_t)1 << d.vi->format->bitsPerSample)) {
                vsapi->freeNode(d.node);
                RETERROR("PEMVerifier: Invalid lower bound given");
            }
        }
    } else {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: number of lower plane limits does not match the number of planes");
    }

    if (numupper < 0) {
        for (i = 0; i < d.vi->format->numPlanes; i++)
            d.upper[i] = ((int64_t)1 << d.vi->format->bitsPerSample)-1;
    } else if (numupper == d.vi->format->numPlanes) {
        for (i = 0; i < d.vi->format->numPlanes; i++) {
            d.upper[i] = vsapi->propGetInt(in, "upper", i, 0);
            if (d.upper[i] < d.lower[i] || d.upper[i] >= ((int64_t)1 << d.vi->format->bitsPerSample)) {
                vsapi->freeNode(d.node);
                RETERROR("PEMVerifier: Invalid upper bound given");
            }
        }
    } else {
        vsapi->freeNode(d.node);
        RETERROR("PEMVerifier: number of upper plane limits does not match the number of planes");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PEMVerifier", pemVerifierInit, pemVerifierGetFrame, pemVerifierFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PlaneAverage

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *prop;
    int plane;
} PlaneAverageData;

static void VS_CC planeAverageInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PlaneAverageData *d = (PlaneAverageData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC planeAverageGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PlaneAverageData *d = (PlaneAverageData *) * instanceData;
    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        int x;
        int y;
        int width = vsapi->getFrameWidth(src, d->plane);
        int height = vsapi->getFrameHeight(src, d->plane);
        const uint8_t *srcp = vsapi->getReadPtr(src, d->plane);
        int src_stride = vsapi->getStride(src, d->plane);
        int64_t acc = 0;

        switch (d->vi->format->bytesPerSample) {
        case 1:
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    acc += srcp[x];
                srcp += src_stride;
            }
            break;
        case 2:
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    acc += ((const uint16_t *)srcp)[x];
                srcp += src_stride;
            }
            break;
        }
        
        vsapi->propSetFloat(vsapi->getFramePropsRW(dst), d->prop, acc / (double)(width * height * (((int64_t)1 << d->vi->format->bitsPerSample) - 1)), paReplace);

        vsapi->freeFrame(src);
        return dst;
    }
    return 0;
}

static void VS_CC planeAverageCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PlaneAverageData d;
    PlaneAverageData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || isCompatFormat(d.vi) || d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 &&  d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node);
        RETERROR("PlaneAverage: clip must be constant format and of integer 8-16 bit type");
    }

    d.prop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        d.prop = "PlaneAverage";
    d.plane = int64ToIntS(vsapi->propGetInt(in, "plane", 0, 0));
    if (d.plane < 0 || d.plane >= d.vi->format->numPlanes) {
        vsapi->freeNode(d.node);
        RETERROR("PlaneAverage: invalid plane specified");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PlaneAverage", planeAverageInit, planeAverageGetFrame, singleClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PlaneDifference

typedef struct {
    VSNodeRef *node1;
    VSNodeRef *node2;
    const VSVideoInfo *vi;
    const char *prop;
    int plane;
} PlaneDifferenceData;

static void VS_CC planeDifferenceInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PlaneDifferenceData *d = (PlaneDifferenceData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC planeDifferenceGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PlaneDifferenceData *d = (PlaneDifferenceData *) * instanceData;
    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        int x;
        int y;
        int width = vsapi->getFrameWidth(src1, d->plane);
        int height = vsapi->getFrameHeight(src1, d->plane);
        const uint8_t *srcp1 = vsapi->getReadPtr(src1, d->plane);
        const uint8_t *srcp2 = vsapi->getReadPtr(src2, d->plane);
        int src_stride = vsapi->getStride(src1, d->plane);
        int64_t acc = 0;

        switch (d->vi->format->bytesPerSample) {
        case 1:
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    acc += abs(srcp1[x] - srcp2[x]);
                srcp1 += src_stride;
                srcp2 += src_stride;
            }
            break;
        case 2:
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    acc += abs(((const uint16_t *)srcp1)[x] - ((const uint16_t *)srcp2)[x]);
                srcp1 += src_stride;
                srcp2 += src_stride;
            }
            break;
        }
        
        vsapi->propSetFloat(vsapi->getFramePropsRW(dst), d->prop, acc / (double)(width * height * (((int64_t)1 << d->vi->format->bitsPerSample) - 1)), paReplace);
        
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }
    return 0;
}

static void VS_CC planeDifferenceFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PlaneDifferenceData *d = (PlaneDifferenceData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    free(d);
}

static void VS_CC planeDifferenceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PlaneDifferenceData d;
    PlaneDifferenceData *data;
    int err;

    if (vsapi->propNumElements(in, "clips") != 2)
        RETERROR("PlaneDifference: exactly two clips are required as input");
    d.node1 = vsapi->propGetNode(in, "clips", 0, 0);
    d.node2 = vsapi->propGetNode(in, "clips", 1, 0);
    d.vi = vsapi->getVideoInfo(d.node1);

    if (!isConstantFormat(d.vi) || isCompatFormat(d.vi) || !isSameFormat(d.vi, vsapi->getVideoInfo(d.node2)) || d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 &&  d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("PlaneDifference: clips must be the same format, constant format and of integer 8-16 bit type");
    }

    d.prop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        d.prop = "PlaneDifference";
    d.plane = int64ToIntS(vsapi->propGetInt(in, "plane", 0, 0));
    if (d.plane < 0 || d.plane >= d.vi->format->numPlanes) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("PlaneDifference: invalid plane specified");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PlaneDifference", planeDifferenceInit, planeDifferenceGetFrame, planeDifferenceFree, fmParallel, 0, data, core);
}


//////////////////////////////////////////
// ClipToProp

typedef struct {
    VSNodeRef *node1;
    VSNodeRef *node2;
    const VSVideoInfo *vi;
    const char *prop;
} ClipToPropData;

static void VS_CC clipToPropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC clipToPropGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src1, core);
        vsapi->propSetFrame(vsapi->getFramePropsRW(dst), d->prop, src2, paReplace);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return 0;
}

static void VS_CC clipToPropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData *d = (ClipToPropData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    free(d);
}

static void VS_CC clipToPropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ClipToPropData d;
    ClipToPropData *data;
    int err;

    d.prop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        d.prop = "_Alpha";

    d.node1 = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node1);
    d.node2 = vsapi->propGetNode(in, "mclip", 0, 0);

    if (!isConstantFormat(d.vi) || !isConstantFormat(vsapi->getVideoInfo(d.node2))) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("ClipToProp: clip must be constant format");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "ClipToProp", clipToPropInit, clipToPropGetFrame, clipToPropFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// PropToClip

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    const char *prop;
} PropToClipData;

static void VS_CC propToClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC propToClipGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *) * instanceData;
    int err;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *dst = vsapi->propGetFrame(vsapi->getFramePropsRO(src), d->prop, 0, &err);
        vsapi->freeFrame(src);

        if (dst) {
            return dst;
        } else {
            vsapi->setFilterError("PropToClip: Failed to extract frame from specified property", frameCtx);
            return 0;
        }
    }

    return 0;
}

static void VS_CC propToClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    PropToClipData *d = (PropToClipData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC propToClipCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    PropToClipData d;
    PropToClipData *data;
    int err;
    char errmsg[512];
    const VSFrameRef *src;
    const VSFrameRef *msrc;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    d.prop = vsapi->propGetData(in, "prop", 0, &err);
    if (err)
        d.prop = "_Alpha";

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("PropToClip: clip must be constant format");
    }

    src = vsapi->getFrame(0, d.node, errmsg, sizeof(errmsg));
    msrc = vsapi->propGetFrame(vsapi->getFramePropsRO(src), d.prop, 0, &err);

    d.vi.format = vsapi->getFrameFormat(msrc);
    d.vi.width = vsapi->getFrameWidth(msrc, 0);
    d.vi.height = vsapi->getFrameHeight(msrc, 0);
    vsapi->freeFrame(msrc);
    vsapi->freeFrame(src);

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "PropToClip", propToClipInit, propToClipGetFrame, propToClipFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Merge

typedef struct {
    VSNodeRef *node1;
    VSNodeRef *node2;
    const VSVideoInfo *vi;
    int weight[3];
    float fweight[3];
    int process[3];
} MergeData;

const int MergeShift = 15;

static void VS_CC mergeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    MergeData *d = (MergeData *)*instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC mergeGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeData *d = (MergeData *)*instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = {0, 1, 2};
        const VSFrameRef *fs[] = { 0, src1, src2 };
        const VSFrameRef *fr[] = {fs[d->process[0]], fs[d->process[1]], fs[d->process[2]]};
        VSFrameRef *dst = vsapi->newVideoFrame2(d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        int plane;
        int x, y;
        for (plane = 0; plane < d->vi->format->numPlanes; plane++) {
            if (d->process[plane] == 0) {
                int weight = d->weight[plane];
                float fweight = d->fweight[plane];
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                int stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t *dstp = vsapi->getWritePtr(dst, plane);

                if (d->vi->format->sampleType == stInteger) {
                    const int round = 1 << (MergeShift - 1);
                    if (d->vi->format->bytesPerSample == 1) {
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                dstp[x] = srcp1[x] + (((srcp2[x] - srcp1[x]) * weight + round) >> MergeShift);
                            srcp1 += stride;
                            srcp2 += stride;
                            dstp += stride;
                        }
                    } else if (d->vi->format->bytesPerSample == 2) {
                        const int round = 1 << (MergeShift - 1);
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                ((uint16_t *)dstp)[x] = ((const uint16_t *)srcp1)[x] + (((((const uint16_t *)srcp2)[x] - ((const uint16_t *)srcp1)[x]) * weight + round) >> MergeShift);
                            srcp1 += stride;
                            srcp2 += stride;
                            dstp += stride;
                        }
                    }
                } else if (d->vi->format->sampleType == stFloat) {
                    if (d->vi->format->bytesPerSample == 4) {
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                ((float *)dstp)[x] = (((const float *)srcp1)[x] + (((const float *)srcp2)[x] - ((const float *)srcp1)[x]) * fweight);
                            srcp1 += stride;
                            srcp2 += stride;
                            dstp += stride;
                        }
                    }
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return 0;
}

static void VS_CC mergeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    MergeData *d = (MergeData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    free(d);
}

static void VS_CC mergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    MergeData d;
    MergeData *data;
    int nweight;
    int i;

    nweight = vsapi->propNumElements(in, "weight");
    for (i = 0; i < 3; i++)
        d.fweight[i] = 0.5;
    for (i = 0; i < nweight; i++)
        d.fweight[i] = (float)vsapi->propGetFloat(in, "weight", i, 0);

    if (nweight == 2) {
        d.fweight[2] = d.fweight[1];
    } else if (nweight == 1) {
        d.fweight[1] = d.fweight[0];
        d.fweight[2] = d.fweight[0];
    }

    for (i = 0; i < 3; i++) {
        if (d.fweight[i] < 0 || d.fweight[i] > 1)
            RETERROR("Merge: weights must be between 0 and 1");
        d.weight[i] = (int)(d.fweight[i] * (1 << MergeShift) + 0.5f);
    }

    if (vsapi->propNumElements(in, "clips") != 2)
        RETERROR("Merge: exactly two input clips must be specified");

    d.node1 = vsapi->propGetNode(in, "clips", 0, 0);
    d.node2 = vsapi->propGetNode(in, "clips", 1, 0);
    d.vi = vsapi->getVideoInfo(d.node1);

    for (i = 0; i < 3; i++) {
        d.process[i] = 0;
        if (d.vi->format->sampleType == stInteger) {
            if (d.weight[i] == 0)
                d.process[i] = 1;
            else if (d.weight[i] == 1 << MergeShift)
                d.process[i] = 2;
        } else if (d.vi->format->sampleType == stFloat) {
            if (d.fweight[i] == 0.0f)
                d.process[i] = 1;
            else if (d.fweight[i] == 1.0f)
                d.process[i] = 2;
        }
    }

    if (!isConstantFormat(d.vi) || !isSameFormat(d.vi, vsapi->getVideoInfo(d.node2))
		|| isCompatFormat(vsapi->getVideoInfo(d.node1)) || isCompatFormat(vsapi->getVideoInfo(d.node2))) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("Merge: both clips must be constant format and also be the same format and dimensions");
    }

    if ((d.vi->format->sampleType == stInteger && d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2)
        || (d.vi->format->sampleType == stFloat && d.vi->format->bytesPerSample != 4)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("Merge: only 8-16 bit integer and 32 bit float input supported");
    }

    if (nweight > d.vi->format->numPlanes) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        RETERROR("Merge: more weights given than the number of planes to merge");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Merge", mergeInit, mergeGetFrame, mergeFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// MaskedMerge

typedef struct {
    const VSVideoInfo *vi;
    VSNodeRef *node1;
    VSNodeRef *node2;
    VSNodeRef *mask;
    VSNodeRef *mask23;
    int first_plane;
    int process[3];
} MaskedMergeData;

static void VS_CC maskedMergeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = (MaskedMergeData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC maskedMergeGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = (MaskedMergeData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
        vsapi->requestFrameFilter(n, d->mask, frameCtx);
        if (d->mask23)
            vsapi->requestFrameFilter(n, d->mask23, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const VSFrameRef *mask = vsapi->getFrameFilter(n, d->mask, frameCtx);
        const VSFrameRef *mask23 = 0;
        const int pl[] = {0, 1, 2};
        const VSFrameRef *fr[] = {d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1};
        VSFrameRef *dst = vsapi->newVideoFrame2(d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        int plane;
        int x, y;
        if (d->mask23)
           mask23 = vsapi->getFrameFilter(n, d->mask23, frameCtx);
        for (plane = 0; plane < d->vi->format->numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                int stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                const uint8_t *maskp = vsapi->getReadPtr((plane && mask23) ? mask23 : mask, d->first_plane ? 0 : plane);
                uint8_t *dstp = vsapi->getWritePtr(dst, plane);

                if (d->vi->format->sampleType == stInteger) {
                    if (d->vi->format->bytesPerSample == 1) {
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                dstp[x] = srcp1[x] + (((srcp2[x] - srcp1[x]) * (maskp[x] > 2 ? maskp[x] + 1 : maskp[x]) + 128) >> 8);
                            srcp1 += stride;
                            srcp2 += stride;
                            maskp += stride;
                            dstp += stride;
                        }
                    } else if (d->vi->format->bytesPerSample == 2) {
                        int shift = d->vi->format->bitsPerSample;
                        int round = 1 << (shift - 1);
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                ((uint16_t *)dstp)[x] = ((const uint16_t *)srcp1)[x] + (((((const uint16_t *)srcp2)[x]
                                    - ((const uint16_t *)srcp1)[x]) * (((const uint16_t *)maskp)[x] > 2 ? ((const uint16_t *)maskp)[x] + 1 : ((const uint16_t *)maskp)[x]) + round) >> shift);
                            srcp1 += stride;
                            srcp2 += stride;
                            maskp += stride;
                            dstp += stride;
                        }
                    }
                } else if (d->vi->format->sampleType == stFloat) {
                    if (d->vi->format->bytesPerSample == 4) {
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++)
                                ((float *)dstp)[x] = ((const float *)srcp1)[x] + ((((const float *)srcp2)[x] - ((const float *)srcp1)[x]) * ((const float *)maskp)[x]);
                            srcp1 += stride;
                            srcp2 += stride;
                            maskp += stride;
                            dstp += stride;
                        }
                    }
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        vsapi->freeFrame(mask);
        vsapi->freeFrame(mask23);
        return dst;
    }

    return 0;
}

static void VS_CC maskedMergeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = (MaskedMergeData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    vsapi->freeNode(d->mask);
    vsapi->freeNode(d->mask23);
    free(d);
}

static void VS_CC maskedMergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData d;
    MaskedMergeData *data;
    const VSVideoInfo *maskvi;
    int err;
    int m, n, o, i;
    VSMap *mout, *min;

    if (vsapi->propNumElements(in, "clips") != 2)
        RETERROR("MaskedMerge: exactly two input clips must be specified");

    d.mask23 = 0;
    d.node1 = vsapi->propGetNode(in, "clips", 0, 0);
    d.node2 = vsapi->propGetNode(in, "clips", 1, 0);
    d.mask = vsapi->propGetNode(in, "mask", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node1);
    maskvi = vsapi->getVideoInfo(d.mask);
    d.first_plane = !!vsapi->propGetInt(in, "first_plane", 0, &err);
    // always use the first mask plane for all planes when it is the only one
    if (maskvi->format->numPlanes == 1)
        d.first_plane = 1;

    if (!isConstantFormat(d.vi) || !isSameFormat(d.vi, vsapi->getVideoInfo(d.node2))
		|| isCompatFormat(vsapi->getVideoInfo(d.node1)) || isCompatFormat(vsapi->getVideoInfo(d.node2)) || isCompatFormat(maskvi)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->freeNode(d.mask);
        RETERROR("MaskedMerge: both clips must be constant format and have the same format and dimensions");
    }

    if ((d.vi->format->sampleType == stInteger && d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2)
        || (d.vi->format->sampleType == stFloat && d.vi->format->bytesPerSample != 4)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->freeNode(d.mask);
        RETERROR("MaskedMerge: only 8-16 bit integer and 32 bit float input supported");
    }

    if (maskvi->width != d.vi->width || maskvi->height != d.vi->height || maskvi->format->bitsPerSample != d.vi->format->bitsPerSample
        || (maskvi->format != d.vi->format && maskvi->format->colorFamily != cmGray)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->freeNode(d.mask);
        RETERROR("MaskedMerge: mask clip must have same dimensions as main clip and be the same format or equivalent grayscale version");
    }

    n = d.vi->format->numPlanes;
    m = vsapi->propNumElements(in, "planes");

    for (i = 0; i < 3; i++)
        d.process[i] = m <= 0;

    for (i = 0; i < m; i++) {
        o = int64ToIntS(vsapi->propGetInt(in, "planes", i, 0));

        if (o < 0 || o >= n) {
            vsapi->freeNode(d.node1);
            vsapi->freeNode(d.node2);
            vsapi->freeNode(d.mask);
            RETERROR("MaskedMerge: plane index out of range");
        }

        if (d.process[o]) {
            vsapi->freeNode(d.node1);
            vsapi->freeNode(d.node2);
            vsapi->freeNode(d.mask);
            RETERROR("MaskedMerge: plane specified twice");
        }

        d.process[o] = 1;
    }

    // do we need to resample the first mask plane and use it for all the planes?
    if ((d.first_plane && d.vi->format->numPlanes > 1) && (d.vi->format->subSamplingH > 0 || d.vi->format->subSamplingW > 0) && (d.process[1] || d.process[2])) {
        min = vsapi->createMap();
        vsapi->propSetNode(min, "clip", d.mask, paAppend);
        vsapi->propSetInt(min, "width", d.vi->width >> d.vi->format->subSamplingW, paAppend);
        vsapi->propSetInt(min, "height", d.vi->height >> d.vi->format->subSamplingH, paAppend);
        mout = vsapi->invoke(vsapi->getPluginId("com.vapoursynth.resize", core), "Bilinear", min);
        d.mask23 = vsapi->propGetNode(mout, "clip", 0, 0);
        vsapi->freeMap(mout);
        vsapi->freeMap(min);
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "MaskedMerge", maskedMergeInit, maskedMergeGetFrame, maskedMergeFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

void VS_CC stdlibInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("CropAbs", "clip:clip;width:int;height:int;x:int:opt;y:int:opt;", cropAbsCreate, 0, plugin);
    registerFunc("CropRel", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;", cropRelCreate, 0, plugin);
    registerFunc("AddBorders", "clip:clip;left:int:opt;right:int:opt;top:int:opt;bottom:int:opt;color:float[]:opt;", addBordersCreate, 0, plugin);
    registerFunc("Trim", "clip:clip;first:int:opt;last:int:opt;length:int:opt;", trimCreate, 0, plugin);;
    registerFunc("Reverse", "clip:clip;", reverseCreate, 0, plugin);
    registerFunc("Loop", "clip:clip;times:int:opt;", loopCreate, 0, plugin);
    registerFunc("Interleave", "clips:clip[];extend:int:opt;mismatch:int:opt;", interleaveCreate, 0, plugin);
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
    registerFunc("BlankClip", "clip:clip:opt;width:int:opt;height:int:opt;format:int:opt;length:int:opt;fpsnum:int:opt;fpsden:int:opt;color:float[]:opt;", blankClipCreate, 0, plugin);
    registerFunc("AssumeFPS", "clip:clip;src:clip:opt;fpsnum:int:opt;fpsden:int:opt;", assumeFPSCreate, 0, plugin);
    registerFunc("Lut", "clip:clip;lut:int[];planes:int[];", lutCreate, 0, plugin);
    registerFunc("Lut2", "clips:clip[];lut:int[];planes:int[];bits:int:opt;", lut2Create, 0, plugin);
    registerFunc("SelectClip", "clips:clip[];src:clip[];selector:func;", selectClipCreate, 0, plugin);
    registerFunc("ModifyFrame", "clips:clip[];selector:func;", modifyFrameCreate, 0, plugin);
    registerFunc("Transpose", "clip:clip;", transposeCreate, 0, plugin);
    registerFunc("PEMVerifier", "clip:clip;upper:int[]:opt;lower:int[]:opt;", pemVerifierCreate, 0, plugin);
    registerFunc("PlaneAverage", "clip:clip;plane:int;prop:data:opt;", planeAverageCreate, 0, plugin);
    registerFunc("PlaneDifference", "clips:clip[];plane:int;prop:data:opt;", planeDifferenceCreate, 0, plugin);
    registerFunc("ClipToProp", "clip:clip;mclip:clip;prop:data:opt;", clipToPropCreate, 0, plugin);
    registerFunc("PropToClip", "clip:clip;prop:data:opt;", propToClipCreate, 0, plugin);
    registerFunc("Merge", "clips:clip[];weight:float[]:opt;", mergeCreate, 0, plugin);
    registerFunc("MaskedMerge", "clips:clip[];mask:clip;planes:int[]:opt;first_plane:int:opt;", maskedMergeCreate, 0, plugin);
}
