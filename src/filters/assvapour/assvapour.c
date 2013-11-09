/*
 * AssVapour
 *
 * Copyright (c) 2012, Martin Herkt <lachs0r@srsfckn.biz>
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

#define _BSD_SOURCE
#include <string.h>
#include <ass/ass.h>

#include "VapourSynth.h"
#include "VSHelper.h"

#define MAX(a, b) a > b ? a : b

#define _r(c) ((c) >> 24)
#define _g(c) (((c) >> 16) & 0xFF)
#define _b(c) (((c) >> 8) & 0xFF)
#define _a(c) (255 - ((c) & 0xFF))

#define blend(srcA, srcRGB, dstA, dstRGB, outA)  \
    (((srcA * 255 * srcRGB + (dstRGB * dstA * (255 - srcA))) / outA + 255) >> 8)

struct AssData {
    VSNodeRef *node;
    VSVideoInfo vi[2];

    int lastn;
    VSFrameRef *lastframe;
    VSFrameRef *lastalpha;

    const char *file;
    const char *text;
    const char *style;
    const char *charset;
    const char *fontdir;
    double scale;
    double linespacing;
    double sar;
    int margins[4];
    intptr_t debuglevel;

    ASS_Track *ass;
    ASS_Library *ass_library;
    ASS_Renderer *ass_renderer;
};
typedef struct AssData AssData;

static char *strrepl(const char *in, const char *str, const char *repl)
{
    size_t siz;
    char *res, *outptr;
    const char *inptr;
    int count = 0;

    inptr = in;

    while((inptr = strstr(inptr, str))) {
        count++;
        inptr++;
    }

    if(count == 0)
        return strdup(in);

    siz = (strlen(in) - strlen(str) * count + strlen(repl) * count) *
          sizeof(char) + 1;

    res = malloc(MAX(siz, strlen(in) * sizeof(char) + 1));
    strcpy(res, in);

    outptr = res;
    inptr = in;

    while((outptr = strstr(outptr, str)) && (inptr = strstr(inptr, str))) {
        outptr[0] = '\0';
        strcat(outptr, repl);
        strcat(outptr, (inptr + strlen(str)));

        outptr++;
        inptr++;
    }

    return res;
}

static void assDebugCallback(int level, const char *fmt, va_list va, void *data)
{
    if(level < (intptr_t)data) {
        fprintf(stderr, "libass: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

static void VS_CC assInit(VSMap *in, VSMap *out, void **instanceData,
                          VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    AssData *d = (AssData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 2, node);

    d->ass_library = ass_library_init();

    if(!d->ass_library) {
        vsapi->setError(out, "failed to initialize ASS library");
        return;
    }

    ass_set_message_cb(d->ass_library, assDebugCallback, (void *)d->debuglevel);
    ass_set_extract_fonts(d->ass_library, 0);
    ass_set_style_overrides(d->ass_library, 0);

    d->ass_renderer = ass_renderer_init(d->ass_library);

    if(!d->ass_renderer) {
        vsapi->setError(out, "failed to initialize ASS renderer");
        return;
    }

    ass_set_font_scale(d->ass_renderer, d->scale);
    ass_set_frame_size(d->ass_renderer, d->vi[0].width, d->vi[0].height);
    ass_set_margins(d->ass_renderer,
                    d->margins[0], d->margins[1], d->margins[2], d->margins[3]);
    ass_set_use_margins(d->ass_renderer, 1);

    if(d->linespacing)
        ass_set_line_spacing(d->ass_renderer, d->linespacing);

    if(d->sar) {
        ass_set_aspect_ratio(d->ass_renderer,
                             (double)d->vi[0].width /
                             d->vi[0].height * d->sar, 1);
    }

    if(d->fontdir)
        ass_set_fonts_dir(d->ass_library, d->fontdir);

    ass_set_fonts(d->ass_renderer, NULL, NULL, 1, NULL, 1);

    if(d->file == NULL) {
        char *str, *text, x[16], y[16];
        size_t siz;
        const char *fmt = "[Script Info]\n"
                          "ScriptType: v4.00+\n"
                          "PlayResX: %s\n"
                          "PlayResY: %s\n"
                          "[V4+ Styles]\n"
                          "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                          "Style: Default,%s\n"
                          "[Events]\n"
                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,%s\n";

        sprintf(x, "%d", d->vi[0].width);
        sprintf(y, "%d", d->vi[0].height);

        text = strrepl(d->text, "\n", "\\N");

        siz = (strlen(fmt) + strlen(x) + strlen(y) + strlen(d->style) +
               strlen(text)) * sizeof(char);

        str = malloc(siz);
        sprintf(str, fmt, x, y, d->style, text);

        free(text);

        d->ass = ass_new_track(d->ass_library);
        ass_process_data(d->ass, str, strlen(str));

        free(str);
    } else {
        d->ass = ass_read_file(d->ass_library, (char *)d->file, (char *)d->charset);
    }

    if(!d->ass) {
        vsapi->setError(out, "unable to parse input file");
        return;
    }
}

static void assRender(VSFrameRef *dst, VSFrameRef *alpha, const VSAPI *vsapi,
                      ASS_Image *img)
{
    uint8_t *planes[4];
    int strides[4], p;

    for(p = 0; p < 4; p++) {
        VSFrameRef *fr = p == 3 ? alpha : dst;

        planes[p] = vsapi->getWritePtr(fr, p % 3);
        strides[p] = vsapi->getStride(fr, p % 3);
        memset(planes[p], 0, strides[p] * vsapi->getFrameHeight(fr, p % 3));
    }

    while(img) {
        uint8_t *dstp, *alphap, *sp, color[4], outa;
        int x, y, k;

        if(img->w == 0 || img->h == 0) {
            img = img->next;
            continue;
        }

        color[0] = _r(img->color);
        color[1] = _g(img->color);
        color[2] = _b(img->color);
        color[3] = _a(img->color);

        for(p = 0; p < 4; p++) {
            dstp = planes[p] + (strides[p] * img->dst_y) + img->dst_x;
            alphap = planes[3] + (strides[3] * img->dst_y) + img->dst_x;
            sp = img->bitmap;

            for(y = 0; y < img->h; y++) {
                for(x = 0; x < img->w; x++) {
                    k = (sp[x] * color[3] + 255) >> 8;
                    outa = (k * 255 + (alphap[x] * (255 - k)) + 255) >> 8;

                    if(p == 3)
                        dstp[x] = outa;
                    else if(outa != 0)
                        dstp[x] = blend(k, color[p], alphap[x], dstp[x], outa);
                }

                dstp += strides[p];
                alphap += strides[3];
                sp += img->stride;
            }
        }

        img = img->next;
    }
}

static const VSFrameRef *VS_CC assGetFrame(int n, int activationReason,
        void **instanceData, void **frameData,
        VSFrameContext *frameCtx, VSCore *core,
        const VSAPI *vsapi)
{
    AssData *d = (AssData *) * instanceData;

    if(n != d->lastn) {
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi[0].format,
                                               d->vi[0].width,
                                               d->vi[0].height,
                                               NULL, core);

        VSFrameRef *a = vsapi->newVideoFrame(d->vi[1].format,
                                             d->vi[1].width,
                                             d->vi[1].height,
                                             NULL, core);

        ASS_Image *img;
        int64_t ts = 0;

        if(d->file != NULL)
            ts = (int64_t)n * 1000 * d->vi[0].fpsDen / d->vi[0].fpsNum;

        img = ass_render_frame(d->ass_renderer, d->ass, ts, NULL);

        assRender(dst, a, vsapi, img);
        d->lastn = n;
        vsapi->freeFrame(d->lastframe);
        vsapi->freeFrame(d->lastalpha);
        d->lastframe = dst;
        d->lastalpha = a;
    }

    if(vsapi->getOutputIndex(frameCtx) == 0)
        return vsapi->cloneFrameRef(d->lastframe);
    else
        return vsapi->cloneFrameRef(d->lastalpha);
}

static void VS_CC assFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    AssData *d = (AssData *)instanceData;
    vsapi->freeNode(d->node);
    ass_renderer_done(d->ass_renderer);
    ass_library_done(d->ass_library);
    ass_free_track(d->ass);
    free(d);
}

static void VS_CC assRenderCreate(const VSMap *in, VSMap *out, void *userData,
                                  VSCore *core, const VSAPI *vsapi)
{
    AssData d;
    AssData *data;
    int err, i;

    d.lastn = -1;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi[0] = *vsapi->getVideoInfo(d.node);
    d.vi[0].format = vsapi->getFormatPreset(pfRGB24, core);
    d.vi[1] = d.vi[0];
    d.vi[1].format = vsapi->getFormatPreset(pfGray8, core);

    d.lastalpha = NULL;
    d.lastframe = NULL;

    d.file = vsapi->propGetData(in, "file", 0, &err);

    if(err) {
        d.file = NULL;
        d.text = vsapi->propGetData(in, "text", 0, &err);

        d.style = vsapi->propGetData(in, "style", 0, &err);

        if(err) {
            d.style = "sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1";
        }
    }

    d.charset = vsapi->propGetData(in, "charset", 0, &err);

    if(err)
        d.charset = "UTF-8";

    d.fontdir = vsapi->propGetData(in, "fontdir", 0, &err);

    if(err)
        d.fontdir = 0;

    d.scale = vsapi->propGetFloat(in, "scale", 0, &err);

    if(err)
        d.scale = 1;

    if(d.scale <= 0)
        vsapi->setError(out, "scale must be greater than 0");

    d.linespacing = vsapi->propGetFloat(in, "linespacing", 0, &err);

    if(d.linespacing < 0)
        vsapi->setError(out, "linespacing must be positive");

    d.sar = vsapi->propGetFloat(in, "sar", 0, &err);

    if(d.sar < 0)
        vsapi->setError(out, "sar must be positive");

    for(i = 0; i < 4; i++) {
        d.margins[i] = vsapi->propGetInt(in, "margins", i, &err);

        if(d.margins[i] < 0) {
            vsapi->setError(out, "margins must be positive");
            break;
        }
    }

    d.debuglevel = vsapi->propGetInt(in, "debuglevel", 0, &err);

    if(vsapi->getError(out)) {
        vsapi->freeNode(d.node);
        return;
    }

    data = malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "AssRender", assInit, assGetFrame, assFree,
                        fmSerial, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc,
                                 VSRegisterFunction registerFunc,
                                 VSPlugin *plugin)
{
    configFunc("biz.srsfckn.vapour", "assvapour",
               "A subtitling filter based on libass.",
               VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("AssRender",
                 "clip:clip;"
                 "file:data;"
                 "charset:data:opt;"
                 "debuglevel:int:opt;"
                 "fontdir:data:opt;"
                 "linespacing:float:opt;"
                 "margins:int[]:opt;"
                 "sar:float:opt;"
                 "scale:float:opt;",
                 assRenderCreate, 0, plugin);
    registerFunc("Subtitle",
                 "clip:clip;"
                 "text:data;"
                 "debuglevel:int:opt;"
                 "fontdir:data:opt;"
                 "linespacing:float:opt;"
                 "margins:int[]:opt;"
                 "sar:float:opt;"
                 "style:data:opt;",
                 assRenderCreate, 0, plugin);
}

