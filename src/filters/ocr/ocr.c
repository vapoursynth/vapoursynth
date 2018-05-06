/*
 * Tesseract-based OCR filter
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

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <tesseract/capi.h>

#include "VapourSynth.h"
#include "VSHelper.h"

typedef struct OCRData {
    VSNodeRef *node;
    VSVideoInfo vi;

    VSMap *options;
    char *datapath;
    char *language;
} OCRData;

static void VS_CC OCRInit(VSMap *in, VSMap *out, void **instanceData,
                             VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    OCRData *d = (OCRData *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static void VS_CC OCRFree(void *instanceData, VSCore *core,
                             const VSAPI *vsapi)
{
    OCRData *d = (OCRData *)instanceData;

    vsapi->freeNode(d->node);
    vsapi->freeMap(d->options);
    free(d->datapath);
    free(d->language);
    free(d);
}

static const VSFrameRef *VS_CC OCRGetFrame(int n, int activationReason,
                                           void **instanceData,
                                           void **frameData,
                                           VSFrameContext *frameCtx,
                                           VSCore *core,
                                           const VSAPI *vsapi)
{
    OCRData *d = (OCRData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        VSMap *m = vsapi->getFramePropsRW(dst);

        const uint8_t *srcp = vsapi->getReadPtr(src, 0);
        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);
        int stride = vsapi->getStride(src, 0);

        TessBaseAPI *api = TessBaseAPICreate();
        if (TessBaseAPIInit3(api, d->datapath, d->language) == -1) {
            vsapi->setFilterError("Failed to initialize Tesseract", frameCtx);

            TessBaseAPIDelete(api);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);

            return 0;
        }

        if (d->options) {
            int i, err;
            int nopts = vsapi->propNumElements(d->options, "options");

            for (i = 0; i < nopts; i += 2) {
                const char *key = vsapi->propGetData(d->options, "options",
                                                     i, &err);
                const char *value = vsapi->propGetData(d->options, "options",
                                                       i + 1, &err);

                if (!TessBaseAPISetVariable(api, key, value)) {
                    char msg[200];

                    snprintf(msg, 200,
                             "Failed to set Tesseract option '%s'", key);

                    vsapi->setFilterError(msg, frameCtx);

                    TessBaseAPIEnd(api);
                    TessBaseAPIDelete(api);
                    vsapi->freeFrame(src);
                    vsapi->freeFrame(dst);

                    return 0;
                }
            }
        }

        {
            unsigned i;

            char *result = TessBaseAPIRect(api, srcp, 1,
                                           stride, 0, 0, width, height);
            int *confs = TessBaseAPIAllWordConfidences(api);
            int length = strlen(result);

            for (; length > 0 && isspace(result[length - 1]); length--);
            vsapi->propSetData(m, "OCRString", result, length, paReplace);

            for (i = 0; confs[i] != -1; i++) {
                vsapi->propSetInt(m, "OCRConfidence", confs[i], paAppend);
            }

            free(confs);
            free(result);
        }

        TessBaseAPIEnd(api);
        TessBaseAPIDelete(api);
        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

/* Tesseract requires zero-terminated strings for API functions like
   SetVariable. This is to make extra sure that we have them. */
static char *szterm(const char *data, int size) {
    if (size > 0) {
        char *tmp = malloc(size + 1);

        if (!tmp)
            return NULL;

        memcpy(tmp, data, size);
        tmp[size] = '\0';

        return tmp;
    }

    return NULL;
}

static void VS_CC OCRCreate(const VSMap *in, VSMap *out, void *userData,
                               VSCore *core, const VSAPI *vsapi)
{
    OCRData d, *data;
    const char *msg;
    int err, nopts;

    int size;
    const char *opt;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);
    d.options = NULL;
    d.datapath = NULL;
    d.language = NULL;

    if (!d.vi.format) {
        msg = "Only constant format input supported";
        goto error;
    }

    if (d.vi.format->sampleType != stInteger ||
        d.vi.format->bytesPerSample != 1 ||
        d.vi.format->colorFamily != cmGray) {

        msg = "Only grayscale 8-bit int formats supported";
        goto error;
    }

    if ((nopts = vsapi->propNumElements(in, "options")) > 0) {
        if (nopts % 2) {
            msg = "Options must be key,value pairs";
            goto error;
        } else {
            int i;

            d.options = vsapi->createMap();

            for (i = 0; i < nopts; i++) {
                char *tmp;

                opt = vsapi->propGetData(in, "options", i, &err);
                size = vsapi->propGetDataSize(in, "options", i, &err);

                if (err) {
                    msg = "Failed to read an option";
                    goto error;
                }

                if (size == 0) {
                    msg = "Options and their values must have non-zero length";
                    goto error;
                }

                tmp = szterm(opt, size);

                if (!tmp) {
                    msg = "Failed to allocate memory for option";
                    goto error;
                }

                vsapi->propSetData(d.options, "options",
                                   tmp, size + 1, paAppend);

                free(tmp);
            }
        }
    }

    opt = vsapi->propGetData(in, "datapath", 0, &err);
    size = vsapi->propGetDataSize(in, "datapath", 0, &err);

    if (!err) {
        d.datapath = szterm(opt, size);
    }

    opt = vsapi->propGetData(in, "language", 0, &err);
    size = vsapi->propGetDataSize(in, "language", 0, &err);

    if (!err) {
        d.language = szterm(opt, size);
#ifdef _WIN32
    } else {
        VSPlugin *ocr_plugin = vsapi->getPluginById("biz.srsfckn.ocr", core);
        const char *plugin_path = vsapi->getPluginPath(ocr_plugin);
        char *last_slash = strrchr(plugin_path, '/');
        d.datapath = szterm(plugin_path, last_slash - plugin_path + 1);
#endif
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "OCR", OCRInit,
                        OCRGetFrame, OCRFree, fmParallel, 0, data, core);

    return;

error:
    vsapi->freeNode(d.node);
    vsapi->freeMap(d.options);
    free(d.datapath);
    free(d.language);
    vsapi->setError(out, msg);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc,
                                            VSRegisterFunction registerFunc,
                                            VSPlugin *plugin);

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc,
                                            VSRegisterFunction registerFunc,
                                            VSPlugin *plugin)
{
    configFunc("biz.srsfckn.ocr", "ocr", "Tesseract OCR Filter",
               VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Recognize",
                 "clip:clip;datapath:data:opt;language:data:opt;options:data[]:opt",
                 OCRCreate, 0, plugin);
}
