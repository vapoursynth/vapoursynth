/*
 * Simple morphological filters
 *
 * Copyright (c) 2014, Martin Herkt <lachs0r@srsfckn.biz>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <math.h>
#include <stdio.h>

#include "VapourSynth4.h"
#include "VSHelper4.h"

#include "morpho.h"
#include "morpho_selems.h"
#include "morpho_filters.h"

static void VS_CC MorphoCreate(const VSMap *in, VSMap *out, void *userData,
                               VSCore *core, const VSAPI *vsapi)
{
    MorphoData d, *data;
    char msg[80];
    int err;

    int shapec;
    for (shapec = 0; SElemFuncs[shapec + 1] != NULL; shapec++);

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (d.vi.format.colorFamily == cfUndefined) {
        sprintf(msg, "Only constant format input supported");
        goto error;
    }

    if (d.vi.format.sampleType != stInteger ||
        d.vi.format.bytesPerSample > 2) {

        sprintf(msg, "Only 8-16 bit int formats supported");
        goto error;
    }

    d.size = vsapi->mapGetIntSaturated(in, "size", 0, &err);

    if (err)
        d.size = 5;

    if (d.size < 2) {
        sprintf(msg, "Structuring element size must be greater than 1");
        goto error;
    }

    d.shape = vsapi->mapGetIntSaturated(in, "shape", 0, &err);

    if (err)
        d.shape = 0;

    if (d.shape < 0 || d.shape > shapec) {
        sprintf(msg, "Structuring element shape must be in range 0-%d",
                shapec);
        goto error;
    }

    d.filter = (uintptr_t)userData;

    int pads = d.size + (d.size % 2 == 0);

    d.selem = calloc(1, sizeof(uint8_t) * pads * pads);
    if (!d.selem) {
        vsapi->mapSetError(out, "Failed to allocate structuring element");
        goto error;
    }

    SElemFuncs[d.shape](d.selem, d.size);

    data = malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = {{d.node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, FilterNames[d.filter], &data->vi, MorphoGetFrame, MorphoFree, fmParallel, deps, 1, data, core);

    return;

error:
    vsapi->freeNode(d.node);
    vsapi->mapSetError(out, msg);
}

static const VSFrame *VS_CC MorphoGetFrame(int n, int activationReason,
                                              void *instanceData,
                                              void **frameData,
                                              VSFrameContext *frameCtx,
                                              VSCore *core,
                                              const VSAPI *vsapi)
{
    MorphoData *d = (MorphoData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width,
                                               d->vi.height, src, core);

        int i;

        for (i = 0; i < d->vi.format.numPlanes; i++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, i);
            uint8_t *dstp = vsapi->getWritePtr(dst, i);
            int width = vsapi->getFrameWidth(src, i);
            int height = vsapi->getFrameHeight(src, i);
            ptrdiff_t stride = vsapi->getStride(src, i);

            FilterFuncs[d->filter](srcp, dstp, width, height, stride, d);
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC MorphoFree(void *instanceData, VSCore *core,
                             const VSAPI *vsapi)
{
    MorphoData *d = (MorphoData *)instanceData;

    vsapi->freeNode(d->node);
    free(d->selem);
    free(d);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("biz.srsfckn.morpho", "morpho",
        "Simple morphological filters.", VS_MAKE_VERSION(1, 0),
        VAPOURSYNTH_API_VERSION, 0, plugin);

    for (uintptr_t i = 0; FilterFuncs[i] && FilterNames[i]; i++)
        vspapi->registerFunction(FilterNames[i], "clip:vnode;size:int:opt;shape:int:opt", "clip:vnode;", MorphoCreate, (void *)i, plugin);
}
