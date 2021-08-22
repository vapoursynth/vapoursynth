#include <stddef.h>
#include <stdio.h>

#include "VapourSynth4.h"
#include "VSHelper4.h"


void blendSubtitles(VSNode *clip, VSNode *subs, const VSMap *in, VSMap *out, const char *filter_name, char *error, size_t error_size, VSCore *core, const VSAPI *vsapi) {
    int err;

    VSPlugin *std_plugin = vsapi->getPluginByID("com.vapoursynth.std", core);
    VSPlugin *resize_plugin = vsapi->getPluginByID("com.vapoursynth.resize", core);

    VSMap *args, *ret;

    args = vsapi->createMap();
    vsapi->mapSetNode(args, "clip", subs, maReplace);
    ret = vsapi->invoke(std_plugin, "PropToClip", args);
    vsapi->freeMap(args);
    VSNode *alpha = vsapi->mapGetNode(ret, "clip", 0, NULL);
    vsapi->freeMap(ret);


    args = vsapi->createMap();
    vsapi->mapSetNode(args, "clip", subs, maReplace);
    vsapi->mapSetNode(args, "alpha", alpha, maReplace);

    ret = vsapi->invoke(std_plugin, "PreMultiply", args);
    vsapi->freeMap(args);
    if (vsapi->mapGetError(ret)) {
        snprintf(error, error_size, "%s: %s", filter_name, vsapi->mapGetError(ret));
        vsapi->mapSetError(out, error);
        vsapi->freeMap(ret);
        vsapi->freeNode(alpha);
        return;
    }

    subs = vsapi->mapGetNode(ret, "clip", 0, NULL);
    vsapi->freeMap(ret);

    const VSVideoInfo *clip_vi = vsapi->getVideoInfo(clip);
    const VSVideoInfo *subs_vi = vsapi->getVideoInfo(subs);
    const VSVideoInfo *alpha_vi = vsapi->getVideoInfo(alpha);

    int unsuitable_format = !vsh_isSameVideoFormat(&clip_vi->format, &subs_vi->format);
    int unsuitable_dimensions =
            clip_vi->width != subs_vi->width ||
            clip_vi->height != subs_vi->height;

    if (unsuitable_format || unsuitable_dimensions) {
        args = vsapi->createMap();
        vsapi->mapConsumeNode(args, "clip", subs, maReplace);

        if (unsuitable_format) {
            vsapi->mapSetInt(args, "format", vsapi->queryVideoFormatID(clip_vi->format.colorFamily, clip_vi->format.sampleType, clip_vi->format.bitsPerSample, clip_vi->format.subSamplingW, clip_vi->format.subSamplingH, core), maReplace);

            int matrix = vsapi->mapGetIntSaturated(in, "matrix", 0, &err);
            if (!err)
                vsapi->mapSetInt(args, "matrix", matrix, maReplace);
            const char *matrix_s = vsapi->mapGetData(in, "matrix_s", 0, &err);
            if (!err)
                vsapi->mapSetData(args, "matrix_s", matrix_s, -1, dtUtf8, maReplace);

            int transfer = vsapi->mapGetIntSaturated(in, "transfer", 0, &err);
            if (!err)
                vsapi->mapSetInt(args, "transfer", transfer, maReplace);
            const char *transfer_s = vsapi->mapGetData(in, "transfer_s", 0, &err);
            if (!err)
                vsapi->mapSetData(args, "transfer_s", transfer_s, -1, dtUtf8, maReplace);

            int primaries = vsapi->mapGetIntSaturated(in, "primaries", 0, &err);
            if (!err)
                vsapi->mapSetInt(args, "primaries", primaries, maReplace);
            const char *primaries_s = vsapi->mapGetData(in, "primaries_s", 0, &err);
            if (!err)
                vsapi->mapSetData(args, "primaries_s", primaries_s, -1, dtUtf8, maReplace);

            int range = vsapi->mapGetIntSaturated(in, "range", 0, &err);
            if (!err)
                vsapi->mapSetInt(args, "range", range, maReplace);

            if (clip_vi->format.colorFamily != cfRGB &&
                vsapi->mapGetType(in, "matrix") == ptUnset &&
                vsapi->mapGetType(in, "matrix_s") == ptUnset)
                vsapi->mapSetData(args, "matrix_s", "709", -1, dtUtf8, maReplace);
        }

        if (unsuitable_dimensions) {
            vsapi->mapSetInt(args, "width", clip_vi->width, maReplace);
            vsapi->mapSetInt(args, "height", clip_vi->height, maReplace);
        }

        ret = vsapi->invoke(resize_plugin, "Bicubic", args);
        vsapi->freeMap(args);
        if (vsapi->mapGetError(ret)) {
            snprintf(error, error_size, "%s: %s", filter_name, vsapi->mapGetError(ret));
            vsapi->mapSetError(out, error);
            vsapi->freeMap(ret);
            vsapi->freeNode(alpha);
            return;
        }

        subs = vsapi->mapGetNode(ret, "clip", 0, NULL);
        vsapi->freeMap(ret);
    }


    unsuitable_format =
            clip_vi->format.bitsPerSample != alpha_vi->format.bitsPerSample ||
            clip_vi->format.sampleType != alpha_vi->format.sampleType;

    if (unsuitable_format || unsuitable_dimensions) {
        args = vsapi->createMap();
        vsapi->mapConsumeNode(args, "clip", alpha, maReplace);

        if (unsuitable_format) {
            uint32_t alpha_format = vsapi->queryVideoFormatID(alpha_vi->format.colorFamily,
                                                                 clip_vi->format.sampleType,
                                                                 clip_vi->format.bitsPerSample,
                                                                 alpha_vi->format.subSamplingW,
                                                                 alpha_vi->format.subSamplingH,
                                                                 core);
            if (!alpha_format) {
                snprintf(error, error_size,
                         "%s: Failed to register format with "
                         "color family %d, "
                         "sample type %d, "
                         "bit depth %d, "
                         "horizontal subsampling %d, "
                         "vertical subsampling %d.",
                         filter_name,
                         alpha_vi->format.colorFamily,
                         clip_vi->format.sampleType,
                         clip_vi->format.bitsPerSample,
                         alpha_vi->format.subSamplingW,
                         alpha_vi->format.subSamplingH);
                vsapi->mapSetError(out, error);
                vsapi->freeNode(subs);
                vsapi->freeMap(args);
                return;
            }

            vsapi->mapSetInt(args, "format", alpha_format, maReplace);
        }

        if (unsuitable_dimensions) {
            vsapi->mapSetInt(args, "width", clip_vi->width, maReplace);
            vsapi->mapSetInt(args, "height", clip_vi->height, maReplace);
        }

        ret = vsapi->invoke(resize_plugin, "Bicubic", args);
        vsapi->freeMap(args);
        if (vsapi->mapGetError(ret)) {
            snprintf(error, error_size, "%s: %s", filter_name, vsapi->mapGetError(ret));
            vsapi->mapSetError(out, error);
            vsapi->freeMap(ret);
            vsapi->freeNode(subs);
            return;
        }

        alpha = vsapi->mapGetNode(ret, "clip", 0, NULL);
        vsapi->freeMap(ret);
    }

    args = vsapi->createMap();
    vsapi->mapSetNode(args, "clipa", clip, maReplace);
    vsapi->mapConsumeNode(args, "clipb", subs, maReplace);
    vsapi->mapConsumeNode(args, "mask", alpha, maReplace);
    vsapi->mapSetInt(args, "premultiplied", 1, maReplace);

    ret = vsapi->invoke(std_plugin, "MaskedMerge", args);
    vsapi->freeMap(args);
    if (vsapi->mapGetError(ret)) {
        snprintf(error, error_size, "%s: %s", filter_name, vsapi->mapGetError(ret));
        vsapi->mapSetError(out, error);
        vsapi->freeMap(ret);
        return;
    }

    vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(ret, "clip", 0, NULL), maReplace);
    vsapi->freeMap(ret);
}
