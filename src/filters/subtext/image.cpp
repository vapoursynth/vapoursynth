#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include "VapourSynth.h"
#include "VSHelper.h"

#include "common.h"


static const int64_t unused_colour = (int64_t)1 << 42;


typedef struct Subtitle {
    std::vector<AVPacket> packets;
    int start_frame;
    int end_frame; // Actually first frame where subtitle is not displayed.
} Subtitle;


typedef struct ImageFileData {
    std::string filter_name;

    VSNodeRef *clip;

    VSVideoInfo vi;

    VSFrameRef *blank_rgb;
    VSFrameRef *blank_alpha;

    const VSFrameRef *last_frame;
    int last_subtitle;

    std::vector<Subtitle> subtitles;

    std::vector<int64_t> palette;

    bool gray;

    bool flatten;

    AVCodecContext *avctx;
} ImageFileData;


static void VS_CC imageFileInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    (void)in;
    (void)out;
    (void)core;

    ImageFileData *d = (ImageFileData *) * instanceData;

    vsapi->setVideoInfo(&d->vi, 1, node);
}


static int findSubtitleIndex(int frame, const std::vector<Subtitle> &subtitles) {
    for (size_t i = 0; i < subtitles.size(); i++)
        if (subtitles[i].start_frame <= frame && frame < subtitles[i].end_frame)
            return i;

    return -1;
}


static void makePaletteGray(uint32_t *palette) {
    for (int i = 0; i < AVPALETTE_COUNT; i++) {
        uint32_t a = palette[i] >> 24;
        uint32_t r = (palette[i] >> 16) & 0xff;
        uint32_t g = (palette[i] >> 8) & 0xff;
        uint32_t b = palette[i] & 0xff;

        g = (r + g + b) / 3;

        palette[i] = (a << 24) | (g << 16) | (g << 8) | g;
    }
}


static const VSFrameRef *VS_CC imageFileGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    (void)frameData;

    ImageFileData *d = (ImageFileData *) *instanceData;

    if (activationReason == arInitial) {
        int subtitle_index;
        if (d->flatten) {
            subtitle_index = n;
        } else {
            subtitle_index = findSubtitleIndex(n, d->subtitles);
            if (subtitle_index == d->last_subtitle)
                return vsapi->cloneFrameRef(d->last_frame);
        }

        VSFrameRef *rgb = vsapi->copyFrame(d->blank_rgb, core);
        VSFrameRef *alpha = vsapi->copyFrame(d->blank_alpha, core);

        if (subtitle_index > -1) {
            if (d->avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE &&
                d->last_subtitle != subtitle_index - 1) {
                // Random access in PGS doesn't quite work without decoding some previous subtitles.
                // 5 was not enough. 10 seems to work.
                for (int s = std::max(0, subtitle_index - 10); s < subtitle_index; s++) {
                    const Subtitle &sub = d->subtitles[s];

                    int got_subtitle = 0;

                    AVSubtitle avsub;

                    for (size_t i = 0; i < sub.packets.size(); i++) {
                        AVPacket packet = sub.packets[i];

                        avcodec_decode_subtitle2(d->avctx, &avsub, &got_subtitle, &packet);

                        if (got_subtitle)
                            avsubtitle_free(&avsub);
                    }
                }
            }

            d->last_subtitle = subtitle_index;

            const Subtitle &sub = d->subtitles[subtitle_index];

            int got_subtitle = 0;

            AVSubtitle avsub;

            for (size_t i = 0; i < sub.packets.size(); i++) {
                AVPacket packet = sub.packets[i];

                if (avcodec_decode_subtitle2(d->avctx, &avsub, &got_subtitle, &packet) < 0) {
                    vsapi->setFilterError((d->filter_name + ": Failed to decode subtitle.").c_str(), frameCtx);

                    vsapi->freeFrame(rgb);
                    vsapi->freeFrame(alpha);

                    return nullptr;
                }

                if (got_subtitle && i < sub.packets.size() - 1) {
                    vsapi->setFilterError((d->filter_name + ": Got subtitle sooner than expected.").c_str(), frameCtx);

                    vsapi->freeFrame(rgb);
                    vsapi->freeFrame(alpha);

                    return nullptr;
                }
            }

            if (!got_subtitle) {
                vsapi->setFilterError((d->filter_name + ": Got no subtitle after decoding all the packets.").c_str(), frameCtx);

                vsapi->freeFrame(rgb);
                vsapi->freeFrame(alpha);

                return nullptr;
            }

            if (avsub.num_rects == 0) {
                vsapi->setFilterError((d->filter_name + ": Got subtitle with num_rects=0.").c_str(), frameCtx);

                vsapi->freeFrame(rgb);
                vsapi->freeFrame(alpha);

                return nullptr;
            }

            for (unsigned r = 0; r < avsub.num_rects; r++) {
                AVSubtitleRect *rect = avsub.rects[r];

                if (rect->w <= 0 || rect->h <= 0 || rect->type != SUBTITLE_BITMAP)
                    continue;

#ifdef VS_HAVE_AVSUBTITLERECT_AVPICTURE
                uint8_t **rect_data = rect->pict.data;
                int *rect_linesize = rect->pict.linesize;
#else
                uint8_t **rect_data = rect->data;
                int *rect_linesize = rect->linesize;
#endif

                uint32_t palette[AVPALETTE_COUNT];
                memcpy(palette, rect_data[1], AVPALETTE_SIZE);
                for (size_t i = 0; i < d->palette.size(); i++)
                    if (d->palette[i] != unused_colour)
                        palette[i] = d->palette[i];

                if (d->gray)
                    makePaletteGray(palette);

                const uint8_t *input = rect_data[0];

                uint8_t *dst_a = vsapi->getWritePtr(alpha, 0);
                uint8_t *dst_r = vsapi->getWritePtr(rgb, 0);
                uint8_t *dst_g = vsapi->getWritePtr(rgb, 1);
                uint8_t *dst_b = vsapi->getWritePtr(rgb, 2);
                int stride = vsapi->getStride(rgb, 0);

                dst_a += rect->y * stride + rect->x;
                dst_r += rect->y * stride + rect->x;
                dst_g += rect->y * stride + rect->x;
                dst_b += rect->y * stride + rect->x;

                for (int y = 0; y < rect->h; y++) {
                    for (int x = 0; x < rect->w; x++) {
                        uint32_t argb = palette[input[x]];

                        dst_a[x] = (argb >> 24) & 0xff;
                        dst_r[x] = (argb >> 16) & 0xff;
                        dst_g[x] = (argb >> 8) & 0xff;
                        dst_b[x] = argb & 0xff;
                    }

                    input += rect_linesize[0];
                    dst_a += stride;
                    dst_r += stride;
                    dst_g += stride;
                    dst_b += stride;
                }
            }

            avsubtitle_free(&avsub);
        }


        VSMap *rgb_props = vsapi->getFramePropsRW(rgb);

        vsapi->propSetFrame(rgb_props, "_Alpha", alpha, paReplace);
        vsapi->freeFrame(alpha);

        if (subtitle_index > -1) {
            vsapi->freeFrame(d->last_frame);
            d->last_frame = vsapi->cloneFrameRef(rgb);
        }

        return rgb;
    }

    return nullptr;
}


static void VS_CC imageFileFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    (void)core;

    ImageFileData *d = (ImageFileData *)instanceData;

    vsapi->freeNode(d->clip);

    vsapi->freeFrame(d->blank_rgb);
    vsapi->freeFrame(d->blank_alpha);
    vsapi->freeFrame(d->last_frame);

    for (auto sub = d->subtitles.begin(); sub != d->subtitles.end(); sub++)
        for (auto packet = sub->packets.begin(); packet != sub->packets.end(); packet++)
            av_packet_unref(&(*packet));

    avcodec_close(d->avctx);
    avcodec_free_context(&d->avctx);

    delete d;
}


static bool isSupportedCodecID(AVCodecID codec_id) {
    return codec_id == AV_CODEC_ID_DVD_SUBTITLE ||
           codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE;
}


static int timestampToFrameNumber(int64_t pts, const AVRational &time_base, int64_t fpsnum, int64_t fpsden) {
    int64_t num = time_base.num;
    int64_t den = time_base.den;

    muldivRational(&num, &den, fpsnum, fpsden);

    muldivRational(&num, &den, pts, 1);

    return (int)(num / den);
}


/*
static void printTimestamp(int64_t pts, const AVRational &time_base, bool end) {
    double time = (double)pts * time_base.num / time_base.den;
    int64_t sec = (int64_t)time;
    int64_t milliseconds = (int64_t)((time - sec) * 1000);

    int64_t hours = sec / 3600;
    int64_t minutes = (sec / 60) % 60;
    int64_t seconds = sec % 60;

    fprintf(stdout, "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64, hours, minutes, seconds, milliseconds);
    if (end)
        fprintf(stdout, "\n");
    else
        fprintf(stdout, " --> ");
}
*/


extern "C" void VS_CC imageFileCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    (void)userData;

    ImageFileData d = { };
    ImageFileData *data;

    d.filter_name = "ImageFile";

    int err;

    d.clip = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.clip);

    const char *file = vsapi->propGetData(in, "file", 0, nullptr);

    int id = int64ToIntS(vsapi->propGetInt(in, "id", 0, &err));
    if (err)
        id = -1;

    int palette_size = vsapi->propNumElements(in, "palette");
    if (palette_size > AVPALETTE_COUNT) {
        vsapi->setError(out, (d.filter_name + ": the palette can have at most " + std::to_string(AVPALETTE_COUNT) + " elements.").c_str());

        vsapi->freeNode(d.clip);

        return;
    }
    if (palette_size > 0) {
        const int64_t *palette = vsapi->propGetIntArray(in, "palette", nullptr);
        d.palette.resize(palette_size);
        for (int i = 0; i < palette_size; i++) {
            if (palette[i] < 0 || (palette[i] > UINT32_MAX && palette[i] != unused_colour)) {
                vsapi->setError(out, (d.filter_name + ": palette[" + std::to_string(i) + "] has an invalid value.").c_str());

                vsapi->freeNode(d.clip);

                return;
            }
            d.palette[i] = palette[i];
        }
    }

    d.gray = !!vsapi->propGetInt(in, "gray", 0, &err);

    int info = !!vsapi->propGetInt(in, "info", 0, &err);

    d.vi.format = vsapi->getFormatPreset(pfRGB24, core);


    av_log_set_level(AV_LOG_PANIC);

    int ret = 0;

    AVFormatContext *fctx = nullptr;

    try {
        ret = avformat_open_input(&fctx, file, nullptr, nullptr);
        if (ret < 0)
            throw std::string("avformat_open_input failed: ");

        ret = avformat_find_stream_info(fctx, NULL);
        if (ret < 0)
            throw std::string("avformat_find_stream_info failed: ");
    } catch (const std::string &e) {
        char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        std::string error;

        if (!av_strerror(ret, av_error, AV_ERROR_MAX_STRING_SIZE))
            error = av_error;
        else
            error = strerror(err);

        vsapi->setError(out, (d.filter_name + ": " + e + error).c_str());

        if (fctx)
            avformat_close_input(&fctx);

        vsapi->freeNode(d.clip);

        return;
    }

    if (fctx->iformat->name != std::string("vobsub") &&
        fctx->iformat->name != std::string("sup")) {
        vsapi->setError(out, (d.filter_name + ": unsupported file format '" + fctx->iformat->name + "'.").c_str());

        avformat_close_input(&fctx);

        vsapi->freeNode(d.clip);

        return;
    }

    if (fctx->nb_streams == 0) {
        vsapi->setError(out, (d.filter_name + ": no streams found.").c_str());

        avformat_close_input(&fctx);

        vsapi->freeNode(d.clip);

        return;
    }

    int stream_index = -1;

    try {
        if (id > -1) {
            for (unsigned i = 0; i < fctx->nb_streams; i++) {
                if (fctx->streams[i]->id == id) {
                    stream_index = i;
                    break;
                }
            }

            if (stream_index == -1)
                throw std::string("there is no stream with the chosen id.");

            if (!isSupportedCodecID(fctx->streams[stream_index]->codecpar->codec_id))
                throw std::string("selected stream has unsupported format.");
        } else {
            for (unsigned i = 0; i < fctx->nb_streams; i++) {
                if (isSupportedCodecID(fctx->streams[i]->codecpar->codec_id)) {
                    stream_index = i;
                    break;
                }
            }

            if (stream_index == -1)
                throw std::string("no supported subtitle streams found.");
        }

        for (unsigned i = 0; i < fctx->nb_streams; i++)
            if ((int)i != stream_index)
                fctx->streams[i]->discard = AVDISCARD_ALL;

        AVCodecID codec_id = fctx->streams[stream_index]->codecpar->codec_id;

        const AVCodec *decoder = avcodec_find_decoder(codec_id);
        if (!decoder)
            throw std::string("failed to find decoder for '") + avcodec_get_name(codec_id) + "'.";

        d.avctx = avcodec_alloc_context3(decoder);
        if (!d.avctx)
            throw std::string("failed to allocate AVCodecContext.");

        int extradata_size = fctx->streams[stream_index]->codecpar->extradata_size;
        if (extradata_size) {
            d.avctx->extradata_size = extradata_size;
            d.avctx->extradata = (uint8_t *)av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(d.avctx->extradata, fctx->streams[stream_index]->codecpar->extradata, extradata_size);
        }

        ret = avcodec_open2(d.avctx, decoder, nullptr);
        if (ret < 0)
            throw std::string("failed to open AVCodecContext.");
    } catch (const std::string &e) {
        vsapi->setError(out, (d.filter_name + ": " + e).c_str());

        avformat_close_input(&fctx);

        if (d.avctx)
            avcodec_free_context(&d.avctx);

        vsapi->freeNode(d.clip);

        return;
    }


    av_opt_get_image_size(fctx->streams[stream_index]->codecpar, "video_size", 0, &d.vi.width, &d.vi.height);

    Subtitle current_subtitle = { };

    AVPacket packet;
    av_init_packet(&packet);

    AVSubtitle avsub;

    while (av_read_frame(fctx, &packet) == 0) {
        if (packet.stream_index != stream_index) {
            av_packet_unref(&packet);
            continue;
        }

        int got_avsub = 0;

        AVPacket decoded_packet = packet;

        ret = avcodec_decode_subtitle2(d.avctx, &avsub, &got_avsub, &decoded_packet);
        if (ret < 0) {
            av_packet_unref(&packet);
            continue;
        }

        if (got_avsub) {
            const AVRational &time_base = fctx->streams[stream_index]->time_base;

            if (avsub.num_rects) {
                current_subtitle.packets.push_back(packet);

                int64_t start_time = current_subtitle.packets.front().pts;
                if (fctx->streams[stream_index]->codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE) {
                    start_time += avsub.start_display_time;

                    current_subtitle.end_frame = timestampToFrameNumber(packet.pts + avsub.end_display_time, time_base, d.vi.fpsNum, d.vi.fpsDen);
                    // If it doesn't say when it should end, display it until the next one.
                    if (avsub.end_display_time == 0)
                        current_subtitle.end_frame = 0;
                }

                current_subtitle.start_frame = timestampToFrameNumber(start_time, time_base, d.vi.fpsNum, d.vi.fpsDen);

                d.subtitles.push_back(current_subtitle);
                current_subtitle.packets.clear();
            } else {
                Subtitle &previous_subtitle = d.subtitles.back();
                if (d.subtitles.size()) // The first AVSubtitle may be empty.
                    previous_subtitle.end_frame = timestampToFrameNumber(current_subtitle.packets.front().pts, time_base, d.vi.fpsNum, d.vi.fpsDen);

                for (auto p : current_subtitle.packets)
                    av_packet_unref(&p);

                current_subtitle.packets.clear();

                av_packet_unref(&packet);
            }

            avsubtitle_free(&avsub);
        } else {
            current_subtitle.packets.push_back(packet);
        }
    }

    if (d.subtitles.size() == 0) {
        vsapi->setError(out, (d.filter_name + ": no usable subtitle pictures found.").c_str());

        avformat_close_input(&fctx);

        if (d.avctx)
            avcodec_free_context(&d.avctx);

        vsapi->freeNode(d.clip);

        return;
    }

    // Sometimes there is no AVSubtitle with num_rects = 0 in between two AVSubtitles with num_rects > 0 (PGS).
    // Sometimes end_display_time is 0 (VOBSUB).
    // In such cases end_frame is 0, so we correct it.
    for (size_t i = 0; i < d.subtitles.size(); i++) {
        if (d.subtitles[i].end_frame == 0) {
            if (i < d.subtitles.size() - 1)
                d.subtitles[i].end_frame = d.subtitles[i + 1].start_frame;
            else
                d.subtitles[i].end_frame = d.vi.numFrames;
        }
    }


    d.blank_rgb = vsapi->newVideoFrame(d.vi.format, d.vi.width, d.vi.height, nullptr, core);
    d.blank_alpha = vsapi->newVideoFrame(vsapi->getFormatPreset(pfGray8, core), d.vi.width, d.vi.height, nullptr, core);
    vsapi->propSetInt(vsapi->getFramePropsRW(d.blank_alpha), "_ColorRange", 0, paReplace);

    for (int i = 0; i < 4; i++) {
        uint8_t *ptr = vsapi->getWritePtr(i < 3 ? d.blank_rgb : d.blank_alpha, i % 3);
        int stride = vsapi->getStride(i < 3 ? d.blank_rgb : d.blank_alpha, i % 3);

        for (int y = 0; y < d.vi.height; y++) {
            memset(ptr, 0, d.vi.width);

            ptr += stride;
        }
    }

    d.last_subtitle = INT_MIN;


    d.flatten = !!vsapi->propGetInt(in, "flatten", 0, &err);
    if (d.flatten)
        d.vi.numFrames = (int)d.subtitles.size();

    data = new ImageFileData(d);

    vsapi->createFilter(in, out, d.filter_name.c_str(), imageFileInit, imageFileGetFrame, imageFileFree, fmUnordered, 0, data, core);

    if (vsapi->getError(out)) {
        avformat_close_input(&fctx);

        return;
    }

    bool blend = !!vsapi->propGetInt(in, "blend", 0, &err);
    if (err)
        blend = true;

    if (d.flatten)
        blend = false;

    if (blend) {
        VSNodeRef *subs = vsapi->propGetNode(out, "clip", 0, NULL);

        VSPlugin *std_plugin = vsapi->getPluginById("com.vapoursynth.std", core);

        VSMap *args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", subs, paReplace);

        VSMap *vsret = vsapi->invoke(std_plugin, "PropToClip", args);
        vsapi->freeMap(args);
        if (vsapi->getError(vsret)) {
            vsapi->setError(out, (d.filter_name + ": " + vsapi->getError(vsret)).c_str());
            vsapi->freeMap(vsret);
            vsapi->freeNode(subs);
            avformat_close_input(&fctx);

            return;
        }

        VSNodeRef *alpha = vsapi->propGetNode(vsret, "clip", 0, NULL);
        vsapi->freeMap(vsret);

#define ERROR_SIZE 512
        char error[ERROR_SIZE] = { 0 };

        blendSubtitles(d.clip, subs, alpha, in, out, d.filter_name.c_str(), error, ERROR_SIZE, core, vsapi);
#undef ERROR_SIZE

        vsapi->freeNode(subs);
        vsapi->freeNode(alpha);

        if (vsapi->getError(out)) {
            avformat_close_input(&fctx);

            return;
        }
    }


    if (info) {
        std::string desc("Supported subtitle streams:\n");

        for (unsigned i = 0; i < fctx->nb_streams; i++) {
            AVCodecID codec_id = fctx->streams[i]->codecpar->codec_id;

            if (!isSupportedCodecID(codec_id))
                continue;

            char stream_id[100] = { 0 };
            snprintf(stream_id, 99, "0x%x", fctx->streams[i]->id);

            desc += "Id: ";
            desc += stream_id;

            AVDictionaryEntry *language = av_dict_get(fctx->streams[i]->metadata, "language", nullptr, AV_DICT_MATCH_CASE);
            if (language) {
                desc += ", language: ";
                desc += language->value;
            }

            int width, height;
            av_opt_get_image_size(fctx->streams[i]->codecpar, "video_size", 0, &width, &height);
            desc += ", size: ";
            desc += std::to_string(width);
            desc += "x";
            desc += std::to_string(height);

            desc += ", type: ";
            desc += avcodec_get_name(codec_id);

            desc += "\n";
        }

        desc.pop_back();


        VSPlugin *text_plugin = vsapi->getPluginById("com.vapoursynth.text", core);

        VSMap *args = vsapi->createMap();

        VSNodeRef *clip = vsapi->propGetNode(out, "clip", 0, nullptr);
        vsapi->propSetNode(args, "clip", clip, paReplace);
        vsapi->freeNode(clip);

        vsapi->propSetData(args, "text", desc.c_str(), desc.size(), paReplace);

        VSMap *vsret = vsapi->invoke(text_plugin, "Text", args);
        vsapi->freeMap(args);
        if (vsapi->getError(vsret)) {
            vsapi->setError(out, (d.filter_name + ": failed to invoke text.Text: " + vsapi->getError(vsret)).c_str());
            vsapi->freeMap(vsret);
            avformat_close_input(&fctx);
            return;
        }
        clip = vsapi->propGetNode(vsret, "clip", 0, nullptr);
        vsapi->freeMap(vsret);
        vsapi->propSetNode(out, "clip", clip, paReplace);
        vsapi->freeNode(clip);
    }


    avformat_close_input(&fctx);
}
