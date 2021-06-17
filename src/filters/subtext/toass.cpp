
#include <algorithm>
#include <cstdio>
#include <string>

#include <iconv.h>
#include <ass/ass.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}


struct MemoryFile {
    const char *data;
    int64_t total_size;
    int64_t current_position;


    static int64_t seek(void *opaque, int64_t offset, int whence);

    static int readPacket(void *opaque, uint8_t *buf, int bytes_to_read);
};


int64_t MemoryFile::seek(void *opaque, int64_t offset, int whence) {
    if (whence & AVSEEK_FORCE)
        whence &= ~AVSEEK_FORCE;

    MemoryFile *mf = (MemoryFile *)opaque;

    if (whence == AVSEEK_SIZE) {
        return mf->total_size;
    } else if (whence == SEEK_SET) {
    } else if (whence == SEEK_CUR) {
        offset += mf->current_position;
    } else if (whence == SEEK_END) {
        offset += mf->total_size;
    } else {
        return -1;
    }

    mf->current_position = offset;
    return mf->current_position;
}


int MemoryFile::readPacket(void *opaque, uint8_t *buf, int bytes_to_read) {
    MemoryFile *mf = (MemoryFile *)opaque;

    int bytes_read = std::min((int64_t)bytes_to_read, mf->total_size - mf->current_position);

    memcpy(buf, mf->data + mf->current_position, bytes_read);

    mf->current_position += bytes_read;

    return bytes_read;
}


extern "C" ASS_Track *convertToASS(const char *file_name, const char *contents, size_t contents_size, ASS_Library *ass_library, const char *user_style, const char *charset, char *error, size_t error_size) {
    av_log_set_level(AV_LOG_PANIC); /// would be good to have a parameter for this

    MemoryFile memory_file = { };
    memory_file.data = contents;
    memory_file.total_size = contents_size;

    AVFormatContext *fctx = nullptr;
    uint8_t *avio_buffer = nullptr;

    try {
        fctx = avformat_alloc_context();
        if (!fctx)
            throw std::string("failed to allocate AVFormatContext.");

        const int avio_buffer_size = 4 * 1024;

        avio_buffer = (uint8_t *)av_malloc(avio_buffer_size);
        if (!avio_buffer)
            throw std::string("failed to allocate buffer for AVIOContext.");

        fctx->pb = avio_alloc_context(avio_buffer, avio_buffer_size, 0, &memory_file, MemoryFile::readPacket, nullptr, MemoryFile::seek);
        if (!fctx->pb)
            throw std::string("failed to allocate AVIOContext.");
    } catch (const std::string &e) {
        snprintf(error, error_size, "%s", e.c_str());

        if (!fctx || !fctx->pb)
            av_freep(&avio_buffer);

        if (fctx)
            avformat_free_context(fctx);

        return nullptr;
    }

    int ret = 0;

    try {
        ret = avformat_open_input(&fctx, file_name, nullptr, nullptr);
        if (ret < 0)
            throw std::string("avformat_open_input failed: ");

        ret = avformat_find_stream_info(fctx, nullptr);
        if (ret < 0)
            throw std::string("avformat_find_stream_info failed: ");
    } catch (const std::string &e) {
        char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };

        snprintf(error, error_size, "%s%s.", e.c_str(), av_strerror(ret, av_error, AV_ERROR_MAX_STRING_SIZE) ? strerror(ret) : av_error);

        avformat_close_input(&fctx);

        return nullptr;
    }

    if (fctx->nb_streams == 0) {
        snprintf(error, error_size, "no streams found.");

        avformat_close_input(&fctx);

        return nullptr;
    }

    int stream_index = 0;

    enum AVCodecID codec_id = fctx->streams[stream_index]->codecpar->codec_id;

    AVCodecContext *avctx = nullptr;

    ret = 0;

    try {
        const AVCodecDescriptor *descriptor = avcodec_descriptor_get(codec_id);
        if (descriptor->type != AVMEDIA_TYPE_SUBTITLE || !(descriptor->props & AV_CODEC_PROP_TEXT_SUB))
            throw std::string("file is not a text subtitle.");

        const AVCodec *decoder = avcodec_find_decoder(codec_id);
        if (!decoder)
            throw std::string("failed to find decoder for '") + avcodec_get_name(codec_id) + "'.";

        avctx = avcodec_alloc_context3(decoder);
        if (!avctx)
            throw std::string("failed to allocate AVCodecContext.");

        int extradata_size = fctx->streams[stream_index]->codecpar->extradata_size;
        if (extradata_size) {
            avctx->extradata_size = extradata_size;
            avctx->extradata = (uint8_t *)av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                throw std::string("failed to allocate extradata.");

            memcpy(avctx->extradata, fctx->streams[stream_index]->codecpar->extradata, extradata_size);
        }

        ret = avcodec_open2(avctx, decoder, NULL);
        if (ret < 0)
            throw std::string("failed to open AVCodecContext.");

        if (!avctx->subtitle_header_size)
            throw std::string("no subtitle header found in AVCodecContext.");
    } catch (const std::string &e) {
        if (avctx)
            avcodec_free_context(&avctx);

        avformat_close_input(&fctx);

        snprintf(error, error_size, "%s", e.c_str());

        return nullptr;
    }

    avctx->time_base = av_make_q(1, 1000);
    avctx->pkt_timebase = avctx->time_base;


    std::string ass_file;

    if (user_style) {
        std::string subtitle_header((char *)avctx->subtitle_header, avctx->subtitle_header_size);

        size_t style_position = subtitle_header.find("Style:");

        if (style_position == std::string::npos) {
            snprintf(error, error_size, "subtitle header in AVCodecContext has no styles.");

            avcodec_free_context(&avctx);
            avformat_close_input(&fctx);

            return nullptr;
        }

        size_t events_position = subtitle_header.find("[Events]");

        if (events_position == std::string::npos) {
            snprintf(error, error_size, "subtitle header in AVCodecContext has no [Events] section.");

            avcodec_free_context(&avctx);
            avformat_close_input(&fctx);

            return nullptr;
        }

        size_t events_size = avctx->subtitle_header_size - events_position;

        ass_file.append((char *)avctx->subtitle_header, style_position);
        ass_file.append("Style: Default,");
        ass_file.append(user_style);
        ass_file.append("\n");
        ass_file.append((char *)avctx->subtitle_header + events_position, events_size);
    } else {
        ass_file.append((char *)avctx->subtitle_header, avctx->subtitle_header_size);
    }


    AVPacket packet;
    av_init_packet(&packet);

    AVSubtitle avsub;

    int total_events = 0;
    int failed_decoding = 0;

    while (av_read_frame(fctx, &packet) == 0) {
        if (packet.stream_index != stream_index) {
            av_packet_unref(&packet);
            continue;
        }

        total_events++;

        int got_avsub = 0;

        AVPacket decoded_packet = packet;

        ret = avcodec_decode_subtitle2(avctx, &avsub, &got_avsub, &decoded_packet);
        if (ret < 0 || !got_avsub) {
            av_packet_unref(&packet);
            failed_decoding++;
            continue;
        }

        for (unsigned i = 0; i < avsub.num_rects; i++) {
            AVSubtitleRect *rect = avsub.rects[i];

            if (rect->type != SUBTITLE_ASS || !rect->ass || !rect->ass[0]) {
                avsubtitle_free(&avsub);
                av_packet_unref(&packet);
                continue;
            }

            ass_file += rect->ass;
        }

        avsubtitle_free(&avsub);
        av_packet_unref(&packet);
    }

    avcodec_free_context(&avctx);
    avformat_close_input(&fctx);

    if (failed_decoding > 0) {
        snprintf(error, error_size, "failed to decode %d events out of %d. Are you sure '%s' is the correct charset?", failed_decoding, total_events, charset);

        return nullptr;
    }

    ASS_Track *track = ass_read_memory(ass_library, (char *)ass_file.c_str(), ass_file.size(), nullptr);

    return track;
}
