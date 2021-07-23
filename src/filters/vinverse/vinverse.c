/*
 * Vinverse, a simple filter to remove residual combing.
 *
 * VapourSynth port by Martin Herkt
 *
 * Copyright (C) 2006 Kevin Stone
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>

#include "VapourSynth4.h"
#include "VSHelper4.h"

struct VinverseData {
    VSNode *node;
    VSVideoInfo vi;

    double sstr;
    double scl;
    int amnt;

    int *dlut;
};
typedef struct VinverseData VinverseData;

static void Vinverse(const uint8_t *src, uint8_t *dst,
                     int width, int height, ptrdiff_t stride, VinverseData *d)
{
    int x, y;

    for (y = 0; y < height; y++) {
        const uint8_t *srcpp = y <  2 ? src + stride * 2 : src - stride * 2;
        const uint8_t *srcp  = y == 0 ? src + stride     : src - stride;
        const uint8_t *srcn  = y == height - 1 ? src - stride     : src + stride;
        const uint8_t *srcnn = y >  height - 3 ? src - stride * 2 : src + stride * 2;

        for (x = 0; x < width; x++) {
            uint8_t b3p = (srcp[x] + (src[x] << 1) + srcn[x] + 2) >> 2;
            uint8_t b6p = (srcpp[x] + ((srcp[x] + srcn[x]) << 2) +
                          src[x] * 6 + srcnn[x] + 8) >> 4;

            int d1 = src[x] - b3p + 255;
            int d2 = b3p - b6p + 255;
            int df = b3p + d->dlut[(d1 << 9) + d2];

            int minm = VSMAX(src[x] - d->amnt, 0);
            int maxm = VSMIN(src[x] + d->amnt, 255);

            if (df <= minm)
                dst[x] = minm;
            else if (df >= maxm)
                dst[x] = maxm;
            else
                dst[x] = df;
        }

        src += stride;
        dst += stride;
    }
}

static const VSFrame *VS_CC VinverseGetFrame(int n, int activationReason,
                                                void *instanceData,
                                                void **frameData,
                                                VSFrameContext *frameCtx,
                                                VSCore *core,
                                                const VSAPI *vsapi)
{
    VinverseData *d = (VinverseData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width,
                                               d->vi.height, src, core);

        for (int i = 0; i < d->vi.format.numPlanes; i++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, i);
            uint8_t *dstp = vsapi->getWritePtr(dst, i);
            int width = vsapi->getFrameWidth(src, i);
            int height = vsapi->getFrameHeight(src, i);
            ptrdiff_t stride = vsapi->getStride(dst, i);

            Vinverse(srcp, dstp, width, height, stride, d);
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC VinverseFree(void *instanceData,
                               VSCore *core, const VSAPI *vsapi)
{
    VinverseData *d = (VinverseData *)instanceData;

    free(d->dlut);
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC VinverseCreate(const VSMap *in, VSMap *out, void *userData,
                                  VSCore *core, const VSAPI *vsapi)
{
    VinverseData d, *data;
    int err;

    d.dlut = NULL;
    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (d.vi.format.colorFamily == cfUndefined) {
        vsapi->mapSetError(out, "Only constant format input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi.format.sampleType != stInteger ||
        d.vi.format.bytesPerSample != 1) {

        vsapi->mapSetError(out, "Only 8-bit int formats supported");
        vsapi->freeNode(d.node);
        return;
    }

    d.sstr = vsapi->mapGetFloat(in, "sstr", 0, &err);

    if (err)
        d.sstr = 2.7;

    d.amnt = vsapi->mapGetIntSaturated(in, "amnt", 0, &err);

    if (err)
        d.amnt = 255;

    if (d.amnt < 1 || d.amnt > 255) {
        vsapi->mapSetError(out, "amnt must be greater than 0 and less than 256");
        vsapi->freeNode(d.node);
        return;
    }

    d.scl = vsapi->mapGetFloat(in, "scl", 0, &err);

    if (err)
        d.scl = 0.25;

    d.dlut = malloc(512 * 511 * sizeof(int));

    if (!d.dlut) {
        vsapi->mapSetError(out, "malloc failure (dlut)");
        vsapi->freeNode(d.node);
        return;
    }

    for (int x = -255; x <= 255; x++) {
        for (int y = -255; y <= 255; y++) {
            double y2 = y * d.sstr;
            double da = fabs((double)x) < fabs(y2) ? x : y2;
            d.dlut[((x + 255) << 9) + (y + 255)] = (double)x * y2 < 0.0 ?
                (int)(da * d.scl) : (int)da;
        }
    }

    data = malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { { d.node, rpStrictSpatial } };
    vsapi->createVideoFilter(out, "Vinverse", &data->vi, VinverseGetFrame, VinverseFree, fmParallel, deps, 1, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi)
{
    vspapi->configPlugin("biz.srsfckn.Vinverse", "vinverse",
               "A simple filter to remove residual combing.",
                VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Vinverse",
                 "clip:vnode;"
                 "sstr:float:opt;"
                 "amnt:int:opt;"
                 "scl:float:opt;",
                 "clip:vnode;",
                 VinverseCreate, NULL, plugin);
}
