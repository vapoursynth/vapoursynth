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

#include <string.h>
#include <ass/ass.h>
#include <time.h>
#include <inttypes.h>

#include "VapourSynth4.h"
#include "VSHelper4.h"

#include "common.h"

#define _r(c) ((c) >> 24)
#define _g(c) (((c) >> 16) & 0xFF)
#define _b(c) (((c) >> 8) & 0xFF)
#define _a(c) (255 - ((c) & 0xFF))

#define div256(x) (((x + 128) >> 8))
#define div255(x) ((div256(x + div256(x))))

#define blend(srcA, srcRGB, dstA, dstRGB, outA)  \
    ((srcA * 255 * srcRGB + (dstRGB * dstA * (255 - srcA))) / outA)


struct AssTime {
    time_t seconds;
    int milliseconds;
};
typedef struct AssTime AssTime;

struct AssData {
    VSNode *node;
    VSVideoInfo vi[2];

    int lastn;
    VSFrame *lastframe;

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

    int startframe;
    int endframe;
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

    if(count == 0) {
        size_t in_len = strlen(in);
        res = malloc(in_len + 1);
        memcpy(res, in, in_len + 1);
        return res;
    }

    siz = (strlen(in) - strlen(str) * count + strlen(repl) * count) *
          sizeof(char) + 1;

    res = malloc(VSMAX(siz, strlen(in) * sizeof(char) + 1));
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

static void assRender(VSFrame *dst, VSFrame *alpha, const VSAPI *vsapi,
                      ASS_Image *img)
{
    uint8_t *planes[4];
    int strides[4], p;

    for(p = 0; p < 4; p++) {
        VSFrame *fr = p == 3 ? alpha : dst;

        planes[p] = vsapi->getWritePtr(fr, p % 3);
        strides[p] = vsapi->getStride(fr, p % 3);
        memset(planes[p], 0, strides[p] * vsapi->getFrameHeight(fr, p % 3));
    }

    while(img) {
        uint8_t *dstp[4], *alphap, *sp, color[4];
        uint16_t outa;
        int x, y, k;

        if(img->w == 0 || img->h == 0) {
            img = img->next;
            continue;
        }

        color[0] = _r(img->color);
        color[1] = _g(img->color);
        color[2] = _b(img->color);
        color[3] = _a(img->color);

        dstp[0] = planes[0] + (strides[0] * img->dst_y) + img->dst_x;
        dstp[1] = planes[1] + (strides[1] * img->dst_y) + img->dst_x;
        dstp[2] = planes[2] + (strides[2] * img->dst_y) + img->dst_x;
        dstp[3] = planes[3] + (strides[3] * img->dst_y) + img->dst_x;
        alphap = dstp[3];
        sp = img->bitmap;

        for(y = 0; y < img->h; y++) {
            for(x = 0; x < img->w; x++) {
                k = div255(sp[x] * color[3]);
                outa = k * 255 + (alphap[x] * (255 - k));

                if(outa != 0) {
                    dstp[0][x] = blend(k, color[0], alphap[x], dstp[0][x], outa);
                    dstp[1][x] = blend(k, color[1], alphap[x], dstp[1][x], outa);
                    dstp[2][x] = blend(k, color[2], alphap[x], dstp[2][x], outa);
                    dstp[3][x] = div255(outa);
                }
            }

            dstp[0] += strides[0];
            dstp[1] += strides[1];
            dstp[2] += strides[2];
            dstp[3] += strides[3];
            alphap += strides[3];
            sp += img->stride;
        }

        img = img->next;
    }
}

static const VSFrame *VS_CC assGetFrame(int n, int activationReason,
        void *instanceData, void **frameData,
        VSFrameContext *frameCtx, VSCore *core,
        const VSAPI *vsapi)
{
    AssData *d = (AssData *) instanceData;

    if(n != d->lastn) {
        ASS_Image *img;
        int64_t ts = 0;
        int changed;

        ts = (int64_t)n * 1000 * d->vi[0].fpsDen / d->vi[0].fpsNum;

        img = ass_render_frame(d->ass_renderer, d->ass, ts, &changed);

        if (changed) {
            VSFrame *dst = vsapi->newVideoFrame(&d->vi[0].format,
                                                   d->vi[0].width,
                                                   d->vi[0].height,
                                                   NULL, core);

            VSFrame *a = vsapi->newVideoFrame(&d->vi[1].format,
                                                 d->vi[1].width,
                                                 d->vi[1].height,
                                                 NULL, core);

            assRender(dst, a, vsapi, img);
            vsapi->mapConsumeFrame(vsapi->getFramePropertiesRW(dst), "_Alpha", a, maAppend);
            vsapi->freeFrame(d->lastframe);
            d->lastframe = dst;
        }

        d->lastn = n;
    }

    return vsapi->addFrameRef(d->lastframe);
}

static void VS_CC assFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    AssData *d = (AssData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeFrame(d->lastframe);
    ass_renderer_done(d->ass_renderer);
    ass_library_done(d->ass_library);
    ass_free_track(d->ass);
    free(d);
}

static int frameToTime(int frame, int64_t fpsNum, int64_t fpsDen, char *str, size_t str_size)
{
    int64_t timeint, time_ms;
    time_t time_secs;
    struct tm * ti;
    if(frame != 0 && (INT64_MAX / fpsDen) < ((int64_t)frame * 100)) {
        //Overflow would occur
        return 0;
    }
    else {
        timeint = (int64_t)frame * 100 * fpsDen / fpsNum;
    }
    time_secs = timeint / 100;
    time_ms = timeint % 100;
    ti = gmtime(&time_secs);
    snprintf(str, str_size, "%d:%02d:%02d.%02" PRIu64, ti->tm_hour, ti->tm_min, ti->tm_sec, time_ms);

    return 1;
}


char *convertToUtf8(const char *file_name, const char *charset, int64_t *file_size, char *error, size_t error_size);
ASS_Track *convertToASS(const char *file_name, const char *contents, size_t contents_size, ASS_Library *ass_library, const char *user_style, const char *charset, char *error, size_t error_size);


static void VS_CC assRenderCreate(const VSMap *in, VSMap *out, void *userData,
                                  VSCore *core, const VSAPI *vsapi)
{
    AssData d;
    AssData *data;
    int err, i;

    const char *filter_name = (const char *)userData;

#define ERROR_SIZE 512
    char error[ERROR_SIZE] = { 0 };

    d.lastn = -1;
    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi[0] = *vsapi->getVideoInfo(d.node);

    if (d.vi[0].format.colorFamily == cfUndefined || !d.vi[0].width || !d.vi[0].height)
        snprintf(error, ERROR_SIZE, "%s: clip must have known format and dimensions.", filter_name);

    vsapi->getVideoFormatByID(&d.vi[0].format, pfRGB24, core);
    d.vi[1] = d.vi[0];
    vsapi->getVideoFormatByID(&d.vi[1].format, pfGray8, core);

    d.lastframe = NULL;

    d.file = vsapi->mapGetData(in, "file", 0, &err);

    if(err) {
        d.file = NULL;
        d.text = vsapi->mapGetData(in, "text", 0, &err);

        d.startframe = vsapi->mapGetIntSaturated(in, "start", 0, &err);
        if (err) {
            d.startframe = 0;
        }
        else if(d.startframe > d.vi[0].numFrames) {
            snprintf(error, ERROR_SIZE, "%s: start must be smaller than the clip length", filter_name);
        }
        
        
        d.endframe = vsapi->mapGetIntSaturated(in, "end", 0, &err);
        if(err) {
            d.endframe = d.vi[0].numFrames;
        }
        else if(d.endframe > d.vi[0].numFrames) {
            snprintf(error, ERROR_SIZE, "%s: end must be smaller than the clip length", filter_name);
        }
        else if(!(d.startframe < d.endframe)) {
            snprintf(error, ERROR_SIZE, "%s: end must be larger than start", filter_name);
        }
    }

    d.style = vsapi->mapGetData(in, "style", 0, &err);

    if(err && !d.file) {
        d.style = "sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1";
    }

    d.charset = vsapi->mapGetData(in, "charset", 0, &err);

    if(err)
        d.charset = "UTF-8";

    d.fontdir = vsapi->mapGetData(in, "fontdir", 0, &err);

    if(err)
        d.fontdir = 0;

    d.scale = vsapi->mapGetFloat(in, "scale", 0, &err);

    if(err)
        d.scale = 1;

    if(d.scale <= 0)
        snprintf(error, ERROR_SIZE, "%s: scale must be greater than 0", filter_name);

    d.linespacing = vsapi->mapGetFloat(in, "linespacing", 0, &err);

    if(d.linespacing < 0)
        snprintf(error, ERROR_SIZE, "%s: linespacing must be positive", filter_name);

    d.sar = vsapi->mapGetFloat(in, "sar", 0, &err);

    if(d.sar < 0)
        snprintf(error, ERROR_SIZE, "%s: sar must be positive", filter_name);

    for(i = 0; i < 4; i++) {
        d.margins[i] = vsapi->mapGetIntSaturated(in, "margins", i, &err);

        if(d.margins[i] < 0) {
            snprintf(error, ERROR_SIZE, "%s: margins must be positive", filter_name);
            break;
        }
    }

    d.debuglevel = vsapi->mapGetInt(in, "debuglevel", 0, &err);

    if(error[0]) {
        vsapi->mapSetError(out, error);
        vsapi->freeNode(d.node);
        return;
    }

    d.ass_library = ass_library_init();

    if(!d.ass_library) {
        snprintf(error, ERROR_SIZE, "%s: failed to initialize ASS library", filter_name);
        vsapi->mapSetError(out, error);
        vsapi->freeNode(d.node);
        return;
    }

    ass_set_message_cb(d.ass_library, assDebugCallback, (void *)d.debuglevel);
    ass_set_extract_fonts(d.ass_library, 0);
    ass_set_style_overrides(d.ass_library, 0);

    d.ass_renderer = ass_renderer_init(d.ass_library);

    if(!d.ass_renderer) {
        snprintf(error, ERROR_SIZE, "%s: failed to initialize ASS renderer", filter_name);
        vsapi->mapSetError(out, error);
        vsapi->freeNode(d.node);
        ass_library_done(d.ass_library);
        return;
    }

    ass_set_font_scale(d.ass_renderer, d.scale);
    ass_set_frame_size(d.ass_renderer, d.vi[0].width, d.vi[0].height);
    ass_set_margins(d.ass_renderer,
                    d.margins[0], d.margins[1], d.margins[2], d.margins[3]);
    ass_set_use_margins(d.ass_renderer, 0);

    if(d.linespacing)
        ass_set_line_spacing(d.ass_renderer, d.linespacing);

    if(d.sar) {
        ass_set_aspect_ratio(d.ass_renderer,
                             (double)d.vi[0].width /
                             d.vi[0].height * d.sar, 1);
    }

    if(d.fontdir)
        ass_set_fonts_dir(d.ass_library, d.fontdir);

    ass_set_fonts(d.ass_renderer, NULL, NULL, 1, NULL, 1);

    if(d.file == NULL) {
#define BUFFER_SIZE 16
        char *str, *text, x[BUFFER_SIZE], y[BUFFER_SIZE], start[BUFFER_SIZE] = { 0 }, end[BUFFER_SIZE] = { 0 };
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
                          "Dialogue: 0,%s,%s,Default,,0,0,0,,%s\n";

        snprintf(x, BUFFER_SIZE, "%d", d.vi[0].width);
        snprintf(y, BUFFER_SIZE, "%d", d.vi[0].height);

        if (!frameToTime(d.startframe, d.vi[0].fpsNum, d.vi[0].fpsDen, start, BUFFER_SIZE) ||
            !frameToTime(d.endframe, d.vi[0].fpsNum, d.vi[0].fpsDen, end, BUFFER_SIZE)) {
            snprintf(error, ERROR_SIZE, "%s: Unable to calculate %s time", filter_name, start[0] ? "end" : "start");
            vsapi->mapSetError(out, error);
            vsapi->freeNode(d.node);
            ass_renderer_done(d.ass_renderer);
            ass_library_done(d.ass_library);
            return;
        }

        text = strrepl(d.text, "\n", "\\N");

        siz = (strlen(fmt) + strlen(x) + strlen(y) + strlen(d.style) +
               strlen(start) + strlen(end) + strlen(text)) * sizeof(char);

        str = malloc(siz);
        snprintf(str, siz, fmt, x, y, d.style, start, end, text);

        free(text);

        d.ass = ass_new_track(d.ass_library);
        ass_process_data(d.ass, str, strlen(str));

        free(str);
    } else {
        snprintf(error, ERROR_SIZE, "%s: ", filter_name);

        int64_t contents_size;
        char *contents = convertToUtf8(d.file, d.charset, &contents_size, error + strlen(error), ERROR_SIZE - strlen(error));

        if (contents) {
            d.ass = ass_read_memory(d.ass_library, contents, contents_size, NULL);

            if (!d.ass)
                d.ass = convertToASS(d.file, contents, contents_size, d.ass_library, d.style, d.charset, error + strlen(error), ERROR_SIZE - strlen(error));

            free(contents);
        }

        if (!contents || !d.ass) {
            vsapi->mapSetError(out, error);
            vsapi->freeNode(d.node);
            ass_renderer_done(d.ass_renderer);
            ass_library_done(d.ass_library);
            return;
        }
    }

    VSFrame *frame = vsapi->newVideoFrame(&d.vi[0].format,
                                       d.vi[0].width,
                                       d.vi[0].height,
                                       NULL, core);

    VSFrame *alpha = vsapi->newVideoFrame(&d.vi[1].format,
                                       d.vi[1].width,
                                       d.vi[1].height,
                                       NULL, core);

    for (int plane = 0; plane < d.vi[0].format.numPlanes; plane++)
        memset(vsapi->getWritePtr(frame, plane), 0, vsapi->getStride(frame, plane) * vsapi->getFrameHeight(frame, plane));
    memset(vsapi->getWritePtr(alpha, 0), 0, vsapi->getStride(alpha, 0) *vsapi->getFrameHeight(alpha, 0));

    vsapi->mapConsumeFrame(vsapi->getFramePropertiesRW(frame), "_Alpha", alpha, maAppend);

    d->lastframe = frame;

    data = malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = {{ d.node, rpStrictSpatial }};
    vsapi->createVideoFilter(out, filter_name, d.vi, assGetFrame, assFree,
                        fmUnordered, deps, 1, data, core);

    int blend = !!vsapi->mapGetInt(in, "blend", 0, &err);
    if (err)
        blend = 1;

    if (blend) {
        VSNode *subs = vsapi->mapGetNode(out, "clip", 0, NULL);

        blendSubtitles(d.node, subs, in, out, filter_name, error, ERROR_SIZE, core, vsapi);

        vsapi->freeNode(subs);

        if (vsapi->mapGetError(out))
            return;
    }
}

void VS_CC imageFileCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("biz.srsfckn.subtext", "sub",
        "A subtitling filter based on libass and FFmpeg.",
        VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);

#define COMMON_TEXTFILE_PARAMS \
    "debuglevel:int:opt;" \
    "fontdir:data:opt;" \
    "linespacing:float:opt;" \
    "margins:int[]:opt;" \
    "sar:float:opt;" \
    "style:data:opt;"

#define COMMON_PARAMS \
    "blend:int:opt;" \
    "matrix:int:opt;" \
    "matrix_s:data:opt;" \
    "transfer:int:opt;" \
    "transfer_s:data:opt;" \
    "primaries:int:opt;" \
    "primaries_s:data:opt;" \
    "range:int:opt;"

    vspapi->registerFunction("TextFile",
                 "clip:vnode;"
                 "file:data;"
                 "charset:data:opt;"
                 "scale:float:opt;"
                 COMMON_TEXTFILE_PARAMS
                 COMMON_PARAMS
                 , "clip:vnode;", assRenderCreate, (void *)"TextFile", plugin);
    vspapi->registerFunction("Subtitle",
                 "clip:vnode;"
                 "text:data;"
                 "start:int:opt;"
                 "end:int:opt;"
                 COMMON_TEXTFILE_PARAMS
                 COMMON_PARAMS
                 , "clip:vnode;", assRenderCreate, (void *)"Subtitle", plugin);
    vspapi->registerFunction("ImageFile",
                 "clip:vnode;"
                 "file:data;"
                 "id:int:opt;"
                 "palette:int[]:opt;"
                 "gray:int:opt;"
                 "info:int:opt;"
                 "flatten:int:opt;"
                 COMMON_PARAMS
                 , "clip:vnode;", imageFileCreate, 0, plugin);

#undef COMMON_PARAMS
#undef COMMON_TEXTFILE_PARAMS
}

