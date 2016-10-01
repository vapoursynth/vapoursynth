#include <stddef.h>
#include <stdio.h>

#include "VapourSynth.h"
#include "VSHelper.h"


void blendSubtitles(VSNodeRef *clip, VSNodeRef *subs, VSNodeRef *alpha, const VSMap *in, VSMap *out, const char *filter_name, char *error, size_t error_size, VSCore *core, const VSAPI *vsapi) {
    int err;

    VSPlugin *std_plugin = vsapi->getPluginById("com.vapoursynth.std", core);
    VSPlugin *resize_plugin = vsapi->getPluginById("com.vapoursynth.resize", core);

    subs = vsapi->cloneNodeRef(subs);
    alpha = vsapi->cloneNodeRef(alpha);

    VSMap *args, *ret;

    args = vsapi->createMap();
    vsapi->propSetNode(args, "clip", subs, paReplace);
    vsapi->freeNode(subs);
    vsapi->propSetNode(args, "alpha", alpha, paReplace);

    ret = vsapi->invoke(std_plugin, "PreMultiply", args);
    vsapi->freeMap(args);
    if (vsapi->getError(ret)) {
        snprintf(error, error_size, "%s: %s", filter_name, vsapi->getError(ret));
        vsapi->setError(out, error);
        vsapi->freeMap(ret);
        vsapi->freeNode(alpha);
        return;
    }

    subs = vsapi->propGetNode(ret, "clip", 0, NULL);
    vsapi->freeMap(ret);

    const VSVideoInfo *clip_vi = vsapi->getVideoInfo(clip);
    const VSVideoInfo *subs_vi = vsapi->getVideoInfo(subs);
    const VSVideoInfo *alpha_vi = vsapi->getVideoInfo(alpha);

    int unsuitable_format = clip_vi->format != subs_vi->format;
    int unsuitable_dimensions =
            clip_vi->width != subs_vi->width ||
            clip_vi->height != subs_vi->height;

    if (unsuitable_format || unsuitable_dimensions) {
        args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", subs, paReplace);
        vsapi->freeNode(subs);

        if (unsuitable_format) {
            vsapi->propSetInt(args, "format", clip_vi->format->id, paReplace);

            int matrix = int64ToIntS(vsapi->propGetInt(in, "matrix", 0, &err));
            if (!err)
                vsapi->propSetInt(args, "matrix", matrix, paReplace);
            const char *matrix_s = vsapi->propGetData(in, "matrix_s", 0, &err);
            if (!err)
                vsapi->propSetData(args, "matrix_s", matrix_s, -1, paReplace);

            int transfer = int64ToIntS(vsapi->propGetInt(in, "transfer", 0, &err));
            if (!err)
                vsapi->propSetInt(args, "transfer", transfer, paReplace);
            const char *transfer_s = vsapi->propGetData(in, "transfer_s", 0, &err);
            if (!err)
                vsapi->propSetData(args, "transfer_s", transfer_s, -1, paReplace);

            int primaries = int64ToIntS(vsapi->propGetInt(in, "primaries", 0, &err));
            if (!err)
                vsapi->propSetInt(args, "primaries", primaries, paReplace);
            const char *primaries_s = vsapi->propGetData(in, "primaries_s", 0, &err);
            if (!err)
                vsapi->propSetData(args, "primaries_s", primaries_s, -1, paReplace);

            if (clip_vi->format->colorFamily != cmRGB &&
                vsapi->propGetType(in, "matrix") == ptUnset &&
                vsapi->propGetType(in, "matrix_s") == ptUnset)
                vsapi->propSetData(args, "matrix_s", "709", -1, paReplace);
        }

        if (unsuitable_dimensions) {
            vsapi->propSetInt(args, "width", clip_vi->width, paReplace);
            vsapi->propSetInt(args, "height", clip_vi->height, paReplace);
        }

        ret = vsapi->invoke(resize_plugin, "Bicubic", args);
        vsapi->freeMap(args);
        if (vsapi->getError(ret)) {
            snprintf(error, error_size, "%s: %s", filter_name, vsapi->getError(ret));
            vsapi->setError(out, error);
            vsapi->freeMap(ret);
            vsapi->freeNode(alpha);
            return;
        }

        subs = vsapi->propGetNode(ret, "clip", 0, NULL);
        vsapi->freeMap(ret);
    }


    unsuitable_format =
            clip_vi->format->bitsPerSample != alpha_vi->format->bitsPerSample ||
            clip_vi->format->sampleType != alpha_vi->format->sampleType;

    if (unsuitable_format || unsuitable_dimensions) {
        args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", alpha, paReplace);
        vsapi->freeNode(alpha);

        if (unsuitable_format) {
            const VSFormat *alpha_format = vsapi->registerFormat(alpha_vi->format->colorFamily,
                                                                 clip_vi->format->sampleType,
                                                                 clip_vi->format->bitsPerSample,
                                                                 alpha_vi->format->subSamplingW,
                                                                 alpha_vi->format->subSamplingH,
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
                         alpha_vi->format->colorFamily,
                         clip_vi->format->sampleType,
                         clip_vi->format->bitsPerSample,
                         alpha_vi->format->subSamplingW,
                         alpha_vi->format->subSamplingH);
                vsapi->setError(out, error);
                vsapi->freeNode(subs);
                vsapi->freeMap(args);
                return;
            }

            vsapi->propSetInt(args, "format", alpha_format->id, paReplace);
        }

        if (unsuitable_dimensions) {
            vsapi->propSetInt(args, "width", clip_vi->width, paReplace);
            vsapi->propSetInt(args, "height", clip_vi->height, paReplace);
        }

        ret = vsapi->invoke(resize_plugin, "Bicubic", args);
        vsapi->freeMap(args);
        if (vsapi->getError(ret)) {
            snprintf(error, error_size, "%s: %s", filter_name, vsapi->getError(ret));
            vsapi->setError(out, error);
            vsapi->freeMap(ret);
            vsapi->freeNode(subs);
            return;
        }

        alpha = vsapi->propGetNode(ret, "clip", 0, NULL);
        vsapi->freeMap(ret);
    }

    args = vsapi->createMap();
    vsapi->propSetNode(args, "clipa", clip, paReplace);
    vsapi->propSetNode(args, "clipb", subs, paReplace);
    vsapi->freeNode(subs);
    vsapi->propSetNode(args, "mask", alpha, paReplace);
    vsapi->freeNode(alpha);
    vsapi->propSetInt(args, "premultiplied", 1, paReplace);

    ret = vsapi->invoke(std_plugin, "MaskedMerge", args);
    vsapi->freeMap(args);
    if (vsapi->getError(ret)) {
        snprintf(error, error_size, "%s: %s", filter_name, vsapi->getError(ret));
        vsapi->setError(out, error);
        vsapi->freeMap(ret);
        return;
    }

    clip = vsapi->propGetNode(ret, "clip", 0, NULL);
    vsapi->freeMap(ret);
    vsapi->propSetNode(out, "clip", clip, paReplace);
    vsapi->freeNode(clip);
}
