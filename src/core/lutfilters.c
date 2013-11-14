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

#include "lutfilters.h"
#include "VSHelper.h"
#include "filtershared.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

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

static void VS_CC funcToLut(char *buff, int n, uint8_t *lut, VSFuncRef *func, VSCore *core, const VSAPI *vsapi) {
    VSMap *in = vsapi->createMap();
    VSMap *out = vsapi->createMap();
    int i;
    int64_t v;
    int err;
    const char *ret;

    if (n == (1 << 8)) {
        for (i = 0; i < n; i++) {
            vsapi->propSetInt(in, "x", i, paReplace);
            vsapi->callFunc(func, in, out, core, vsapi);

            ret = vsapi->getError(out);
            if (ret) {
                strcpy(buff, ret);
                break;
            }

            v = vsapi->propGetInt(out, "val", 0, &err);
            vsapi->clearMap(out);

            if (v < 0 || v >= n) {
                sprintf(buff, "Lut: function(%d) returned invalid value %"PRIi64, i, v);
                break;
            }

            lut[i] = (uint8_t)v;
        }
    } else {
        uint16_t *t = (uint16_t *)lut;

        for (i = 0; i < n; i++) {
            vsapi->propSetInt(in, "x", i, paReplace);
            vsapi->callFunc(func, in, out, core, vsapi);

            ret = vsapi->getError(out);
            if (ret) {
                strcpy(buff, ret);
                break;
            }

            v = vsapi->propGetInt(out, "val", 0, &err);
            vsapi->clearMap(out);

            if (v < 0 || v >= n) {
                sprintf(buff, "Lut: function(%d) returned invalid value %"PRIi64, i, v);
                break;
            }

            t[i] = (uint16_t)v;
        }
    }

    vsapi->freeMap(in);
    vsapi->freeMap(out);
}

static void VS_CC lutCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    LutData d;
    LutData *data;
    VSFuncRef *func;
    int i;
    int n, m, o;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample > 16 || isCompatFormat(d.vi)) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: only clips with integer samples and up to 16 bit per channel precision supported");
    }

    n = d.vi->format->numPlanes;
    m = vsapi->propNumElements(in, "planes");

    for (i = 0; i < 3; i++)
        d.process[i] = (m <= 0);

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

    func = vsapi->propGetFunc(in, "function", 0, &err);
    m = vsapi->propNumElements(in, "lut");

    if (m <= 0 && !func) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: Both lut and function are not set");
    }

    if (m > 0 && func) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: Both lut and function are set");
    }

    n = 1 << d.vi->format->bitsPerSample;

    if (m > 0 && m != n) {
        vsapi->freeNode(d.node);
        RETERROR("Lut: bad lut length");
    }

    d.lut = malloc(d.vi->format->bytesPerSample * n);

    if (func) {
        char errMsg[256] = {0};
        funcToLut(errMsg, n, d.lut, func, core, vsapi);
        vsapi->freeFunc(func);

        if (errMsg[0]) {
            free(d.lut);
            vsapi->freeNode(d.node);
            RETERROR(errMsg);
        }
    } else if (d.vi->format->bytesPerSample == 1) {
        uint8_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int64_t v = vsapi->propGetInt(in, "lut", i, 0);

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
            int64_t v = vsapi->propGetInt(in, "lut", i, 0);

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

static void VS_CC funcToLut2(char *buff, int bits, int x, int y, uint8_t *lut, VSFuncRef *func, VSCore *core, const VSAPI *vsapi) {
    VSMap *in = vsapi->createMap();
    VSMap *out = vsapi->createMap();
    int i, j;
    int64_t v;
    int64_t maximum = (1 << bits) - 1;
    int err;
    const char *ret = 0;

    x = 1 << x;
    y = 1 << y;

    if (bits == 8) {
        for (i = 0; i < y; i++) {
            for (j = 0; j < x; j++) {
                vsapi->propSetInt(in, "x", j, paReplace);
                vsapi->propSetInt(in, "y", i, paReplace);
                vsapi->callFunc(func, in, out, core, vsapi);

                ret = vsapi->getError(out);
                if (ret) {
                    strcpy(buff, ret);
                    goto funcToLut2Free;
                }

                v = vsapi->propGetInt(out, "val", 0, &err);
                vsapi->clearMap(out);

                if (v < 0 || v > maximum) {
                    sprintf(buff, "Lut2: function(%d, %d) returned invalid value %"PRIi64, j, i, v);
                    goto funcToLut2Free;
                }

                lut[j + i * x] = (uint8_t)v;
            }
        }
    } else {
        uint16_t *t = (uint16_t *)lut;

        for (i = 0; i < y; i++) {
            for (j = 0; j < x; j++) {
                vsapi->propSetInt(in, "x", j, paReplace);
                vsapi->propSetInt(in, "y", i, paReplace);
                vsapi->callFunc(func, in, out, core, vsapi);

                ret = vsapi->getError(out);
                if (ret) {
                    strcpy(buff, ret);
                    goto funcToLut2Free;
                }

                v = vsapi->propGetInt(out, "val", 0, &err);
                vsapi->clearMap(out);

                if (v < 0 || v > maximum) {
                    sprintf(buff, "Lut2: function(%d, %d) returned invalid value %"PRIi64, j, i, v);
                    goto funcToLut2Free;
                }

                t[j + i * x] = (uint16_t)v;
            }
        }
    }

    funcToLut2Free:
    vsapi->freeMap(in);
    vsapi->freeMap(out);
}

static void VS_CC lut2Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    Lut2Data d;
    Lut2Data *data;
    VSFuncRef *func;
    int i;
    int n, m, o;
    int err;
    int bits;

    d.node[0] = vsapi->propGetNode(in, "clipa", 0, 0);
    d.node[1] = vsapi->propGetNode(in, "clipb", 0, 0);
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

    for (i = 0; i < 3; i++)
        d.process[i] = (m <= 0);

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

    func = vsapi->propGetFunc(in, "function", 0, &err);
    m = vsapi->propNumElements(in, "lut");

    if (m <= 0 && !func) {
        vsapi->freeNode(d.node[0]);
        vsapi->freeNode(d.node[1]);
        RETERROR("Lut2: Both lut and function are not set");
    }

    if (m > 0 && func) {
        vsapi->freeNode(d.node[0]);
        vsapi->freeNode(d.node[1]);
        RETERROR("Lut2: Both lut and function are set");
    }

    n = 1 << (d.vi[0]->format->bitsPerSample + d.vi[1]->format->bitsPerSample);

    if (m > 0 && m != n) {
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

    m = (1 << bits) - 1;

    if (func) {
        char errMsg[256] = {0};
        funcToLut2(errMsg, bits, d.vi[0]->format->bitsPerSample, d.vi[1]->format->bitsPerSample, d.lut, func, core, vsapi);
        vsapi->freeFunc(func);

        if (errMsg[0]) {
            free(d.lut);
            vsapi->freeNode(d.node[0]);
            vsapi->freeNode(d.node[1]);
            RETERROR(errMsg);
        }
    } else if (bits == 8) {
        uint8_t *lut = d.lut;

        for (i = 0; i < n; i++) {
            int64_t v = vsapi->propGetInt(in, "lut", i, 0);

            if (v < 0 || v > m) {
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
            int64_t v = vsapi->propGetInt(in, "lut", i, 0);

            if (v < 0 || v > m) {
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
// Init

void VS_CC lutInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Lut", "clip:clip;planes:int[]:opt;lut:int[]:opt;function:func:opt;", lutCreate, 0, plugin);
    registerFunc("Lut2", "clipa:clip;clipb:clip;planes:int[]:opt;lut:int[]:opt;function:func:opt;bits:int:opt;", lut2Create, 0, plugin);
}
