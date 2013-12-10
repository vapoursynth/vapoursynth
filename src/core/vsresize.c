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

#include <stdlib.h>
#include <libswscale/swscale.h>
#ifdef _MSC_VER
#define inline _inline
#endif
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include "vsresize.h"
#include "VSHelper.h"
#include "filtershared.h"

static enum PixelFormat formatIdToPixelFormat(int id) {
    switch (id) {
    case pfGray8:
        return PIX_FMT_GRAY8;
    case pfGray16:
        return PIX_FMT_GRAY16;
    case pfYUV420P8:
        return PIX_FMT_YUV420P;
    case pfYUV422P8:
        return PIX_FMT_YUV422P;
    case pfYUV444P8:
        return PIX_FMT_YUV444P;
    case pfYUV410P8:
        return PIX_FMT_YUV410P;
    case pfYUV411P8:
        return PIX_FMT_YUV411P;
    case pfYUV440P8:
        return PIX_FMT_YUV440P;

    case pfYUV420P9:
        return PIX_FMT_YUV420P9;
    case pfYUV422P9:
        return PIX_FMT_YUV422P9;
    case pfYUV444P9:
        return PIX_FMT_YUV444P9;

    case pfYUV420P10:
        return PIX_FMT_YUV420P10;
    case pfYUV422P10:
        return PIX_FMT_YUV422P10;
    case pfYUV444P10:
        return PIX_FMT_YUV444P10;

    case pfYUV420P16:
        return PIX_FMT_YUV420P16;
    case pfYUV422P16:
        return PIX_FMT_YUV422P16;
    case pfYUV444P16:
        return PIX_FMT_YUV444P16;

    case pfRGB24:
        return PIX_FMT_GBRP;
    case pfRGB27:
        return PIX_FMT_GBRP9;
    case pfRGB30:
        return PIX_FMT_GBRP10;
    case pfRGB48:
        return PIX_FMT_GBRP16;

    case pfCompatBGR32:
        return PIX_FMT_RGB32;
    case pfCompatYUY2:
        return PIX_FMT_YUYV422;
    default:
        return PIX_FMT_NONE;
    }
}

//////////////////////////////////////////
// Resize

struct SwsContext *getSwsContext(int SrcW, int SrcH, enum PixelFormat SrcFormat, int SrcColorSpace, int SrcColorRange, int DstW, int DstH, enum PixelFormat DstFormat, int DstColorSpace, int DstColorRange, int64_t Flags) {
    struct SwsContext *Context = sws_alloc_context();
    // 0 = limited range, 1 = full range
    int SrcRange = SrcColorRange == AVCOL_RANGE_JPEG;
    int DstRange = DstColorRange == AVCOL_RANGE_JPEG;

    Flags |= SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND | SWS_BITEXACT;

    if (!Context) return 0;

    av_opt_set_int(Context, "sws_flags",  Flags, 0);
    av_opt_set_int(Context, "srcw",       SrcW, 0);
    av_opt_set_int(Context, "srch",       SrcH, 0);
    av_opt_set_int(Context, "dstw",       DstW, 0);
    av_opt_set_int(Context, "dsth",       DstH, 0);
    av_opt_set_int(Context, "src_range",  SrcRange, 0);
    av_opt_set_int(Context, "dst_range",  DstRange, 0);
    av_opt_set_int(Context, "src_format", SrcFormat, 0);
    av_opt_set_int(Context, "dst_format", DstFormat, 0);

    sws_setColorspaceDetails(Context,
                             sws_getCoefficients(SrcColorSpace), SrcRange,
                             sws_getCoefficients(DstColorSpace), DstRange,
                             0, 1 << 16, 1 << 16);

    if (sws_init_context(Context, 0, 0) < 0) {
        sws_freeContext(Context);
        return 0;
    }

    return Context;
}

enum AVColorSpace GetAssumedColorSpace(int W, int H) {
    if (W > 1024 || H >= 600)
        return AVCOL_SPC_BT709;
    else
        return AVCOL_SPC_BT470BG;
}

typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;
    struct SwsContext *context;
    const VSFormat *lsrcformat;
    int lsrcw;
    int lsrch;
    int dstrange;
    int flags;
} ResizeData;

static void VS_CC resizeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ResizeData *d = (ResizeData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC resizeGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ResizeData *d = (ResizeData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const int rgb_map[] = {2,0,1};
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        int w = vsapi->getFrameWidth(src, 0);
        int h = vsapi->getFrameHeight(src, 0);
        const VSFormat *fi = vsapi->getFrameFormat(src);
        int i;
        const uint8_t *srcp[3];
        uint8_t *dstp[3];
        int src_stride[3];
        int dst_stride[3];
        // swcale expect gbr plane order
        int switchsrc = 0;
        int switchdst = 0;
        // flip output on compat rgb
        int flip_src;
        int flip_dst;

        if (!d->context || d->lsrcformat != fi || d->lsrcw != w || d->lsrch != h) {
            int srcid = formatIdToPixelFormat(fi->id);

            if (srcid == PIX_FMT_NONE) {
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                vsapi->setFilterError("Resize: input format not supported", frameCtx);
                return 0;
            }

            if (d->context)
                sws_freeContext(d->context);

            d->context = getSwsContext(
                             w, h, srcid, GetAssumedColorSpace(w, h), AVCOL_RANGE_UNSPECIFIED,
                             d->vi.width, d->vi.height, formatIdToPixelFormat(d->vi.format->id), GetAssumedColorSpace(d->vi.width, d->vi.height), AVCOL_RANGE_UNSPECIFIED,
                             d->flags);

            if (!d->context) {
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                vsapi->setFilterError("Resize: context creation failed", frameCtx);
                return 0;
            }

            d->lsrcformat = fi;
            d->lsrcw = w;
            d->lsrch = h;
        }

        switchsrc = fi->colorFamily == cmRGB;
        switchdst = d->vi.format->colorFamily == cmRGB;
        flip_src = (fi->id == pfCompatBGR32);
        flip_dst = (d->vi.format->id == pfCompatBGR32);

        if (flip_src) {
            for (i = 0; i < vsapi->getFrameFormat(src)->numPlanes; i++) {
                srcp[switchsrc ? rgb_map[i] : i] = vsapi->getReadPtr(src, i) + (vsapi->getFrameHeight(src, i) - 1)*vsapi->getStride(src, i);
                src_stride[switchsrc ? rgb_map[i] : i] = -vsapi->getStride(src, i);
            }
        } else {
            for (i = 0; i < vsapi->getFrameFormat(src)->numPlanes; i++) {
                srcp[switchsrc ? rgb_map[i] : i] = vsapi->getReadPtr(src, i);
                src_stride[switchsrc ? rgb_map[i] : i] = vsapi->getStride(src, i);
            }
        }

        if (flip_dst) {
            for (i = 0; i < d->vi.format->numPlanes; i++) {
                dstp[switchdst ? rgb_map[i] : i] = vsapi->getWritePtr(dst, i) + (vsapi->getFrameHeight(dst, i) - 1)*vsapi->getStride(dst, i);;
                dst_stride[switchdst ? rgb_map[i] : i] = -vsapi->getStride(dst, i);
            }
        } else {
            for (i = 0; i < d->vi.format->numPlanes; i++) {
                dstp[switchdst ? rgb_map[i] : i] = vsapi->getWritePtr(dst, i);
                dst_stride[switchdst ? rgb_map[i] : i] = vsapi->getStride(dst, i);
            }
        }

        sws_scale(d->context, srcp, src_stride, 0, h, dstp, dst_stride);
        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC resizeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ResizeData *d = (ResizeData *)instanceData;

    if (d->context)
        sws_freeContext(d->context);

    vsapi->freeNode(d->node);
    free(instanceData);
}

static void VS_CC resizeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ResizeData d;
    ResizeData *data;
    int id;
    int dstwidth;
    int dstheight;
    int pf;
    int err;
    d.context = 0;
    d.dstrange = 0;
    d.lsrcformat = 0;
    d.lsrch = 0;
    d.lsrcw = 0;
    d.node = 0;
    d.flags = int64ToIntS((intptr_t)userData);
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    if (d.vi.format && formatIdToPixelFormat(d.vi.format->id) == PIX_FMT_NONE)
        RETERROR("Resize: input format not supported");

    dstwidth = int64ToIntS(vsapi->propGetInt(in, "width", 0, &err));

    if (err)
        dstwidth = d.vi.width;

    dstheight = int64ToIntS(vsapi->propGetInt(in, "height", 0, &err));

    if (err)
        dstheight = d.vi.height;

    id = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));

    if (err && d.vi.format)
        id = d.vi.format->id;

    if (dstwidth > 0)
        d.vi.width = dstwidth;

    if (dstheight > 0)
        d.vi.height = dstheight;

    pf = formatIdToPixelFormat(id);

    if (pf == PIX_FMT_NONE) {
        vsapi->freeNode(d.node);
        RETERROR("Resize: unsupported output format");
    }

    d.vi.format = vsapi->getFormatPreset(id, core);

    if ((d.vi.width % (1 << d.vi.format->subSamplingW)) || (d.vi.height % (1 << d.vi.format->subSamplingH))) {
        vsapi->freeNode(d.node);
        RETERROR("Resize: mod requirements of the target colorspace not fulfilled");
    }

    if (!isConstantFormat(&d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("Resize: output format not constant, set width, height and format");
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "Resize", resizeInit, resizeGetframe, resizeFree, fmParallelRequests, 0, data, core);
}

//////////////////////////////////////////
// Init

void VS_CC resizeInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    const char *a = "clip:clip;width:int:opt;height:int:opt;format:int:opt;yuvrange:int:opt;";
    configFunc("com.vapoursynth.resize", "resize", "VapourSynth Resize", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Bilinear", a, resizeCreate, (void *)SWS_BILINEAR, plugin);
    registerFunc("Bicubic", a, resizeCreate, (void *)SWS_BICUBIC, plugin);
    registerFunc("Point", a, resizeCreate, (void *)SWS_POINT, plugin);
    registerFunc("Gauss", a, resizeCreate, (void *)SWS_GAUSS, plugin);
    registerFunc("Sinc", a, resizeCreate, (void *)SWS_SINC, plugin);
    registerFunc("Lanczos", a, resizeCreate, (void *)SWS_LANCZOS, plugin);
    registerFunc("Spline", a, resizeCreate, (void *)SWS_SPLINE, plugin);
}
