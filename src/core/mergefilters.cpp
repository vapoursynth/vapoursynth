/*
* Copyright (c) 2012-2020 Fredrik Mellbin
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

#include <cstdlib>
#include <cmath>
#include <memory>
#include <algorithm>
#include "cpufeatures.h"
#include "filtershared.h"
#include "internalfilters.h"
#include "kernel/cpulevel.h"
#include "kernel/merge.h"
#include "VSHelper4.h"

using namespace vsh;

//////////////////////////////////////////
// PreMultiply


typedef VariableNodeData<VIPointerData> PreMultiplyData;

static unsigned getLimitedRangeOffset(const VSFrame *f, const VSVideoInfo *vi, const VSAPI *vsapi) {
    int err;
    int limited = !!vsapi->mapGetInt(vsapi->getFramePropertiesRO(f), "_ColorRange", 0, &err);
    if (err)
        limited = (vi->format.colorFamily == cfGray || vi->format.colorFamily == cfYUV);
    return (limited ? (16 << (vi->format.bitsPerSample - 8)) : 0);
}

static const VSFrame *VS_CC preMultiplyGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PreMultiplyData *d = reinterpret_cast<PreMultiplyData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        if (d->nodes[2])
            vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *src2_23 = 0;
        if (d->nodes[2])
            src2_23 = vsapi->getFrameFilter(n, d->nodes[2], frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src1, plane);
            ptrdiff_t stride = vsapi->getStride(src1, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(plane > 0 ? src2_23 : src2, 0);
            uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
            bool yuvhandling = (plane > 0) && (d->vi->format.colorFamily == cfYUV);
            unsigned offset = getLimitedRangeOffset(src1, d->vi, vsapi);
            unsigned depth = d->vi->format.bitsPerSample;

            void (*func)(const void *, const void *, void *, unsigned, unsigned, unsigned) = nullptr;

            if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                func = vs_premultiply_byte_c;
            else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                func = vs_premultiply_word_c;
            else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                func = vs_premultiply_float_c;

            if (!func)
                continue;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, yuvhandling ? (1 << (depth - 1)) : offset, w);

                srcp1 += stride;
                srcp2 += stride;
                dstp += stride;
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        vsapi->freeFrame(src2_23);
        return dst;
    }

    return nullptr;
}

static void VS_CC preMultiplyCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PreMultiplyData> d(new PreMultiplyData(vsapi));

    d->nodes.resize(3);
    d->nodes[0] = vsapi->mapGetNode(in, "clip", 0, 0);
    d->nodes[1] = vsapi->mapGetNode(in, "alpha", 0, 0);

    d->vi = vsapi->getVideoInfo(d->nodes[0]);

    const VSVideoInfo *alphavi = vsapi->getVideoInfo(d->nodes[1]);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(("PreMultiply: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(d->vi->format, vsapi)).c_str());

    if (alphavi->format.colorFamily != cfGray || alphavi->format.sampleType != d->vi->format.sampleType || alphavi->format.bitsPerSample != d->vi->format.bitsPerSample)
        RETERROR("PreMultiply: alpha clip must be grayscale and same sample format and bitdepth as main clip");

    if (!isConstantVideoFormat(d->vi) || !isConstantVideoFormat(alphavi) || d->vi->width != alphavi->width || d->vi->height != alphavi->height)
        RETERROR("PreMultiply: both clips must have the same constant format and dimensions");

    // do we need to resample the first mask plane and use it for all the planes?
    if ((d->vi->format.numPlanes > 1) && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0)) {
        VSMap *min = vsapi->createMap();
        vsapi->mapSetNode(min, "clip", d->nodes[1], maAppend);
        vsapi->mapSetInt(min, "width", d->vi->width >> d->vi->format.subSamplingW, maAppend);
        vsapi->mapSetInt(min, "height", d->vi->height >> d->vi->format.subSamplingH, maAppend);
        VSMap *mout = vsapi->invoke(vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core), "Bilinear", min);
        d->nodes[2] = vsapi->mapGetNode(mout, "clip", 0, 0);
        vsapi->freeMap(mout);
        vsapi->freeMap(min);
    } else if (d->vi->format.numPlanes > 1) {
        d->nodes[2] = vsapi->addNodeRef(d->nodes[1]);
    }

    VSFilterDependency deps[] = {{ d->nodes[0], rpStrictSpatial }, { d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpGeneral }, { d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpGeneral }};
    vsapi->createVideoFilter(out, "PreMultiply", d->vi, preMultiplyGetFrame, filterFree<PreMultiplyData>, fmParallel, deps, d->nodes[2] ? 3 : 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Merge

typedef struct {
    const VSVideoInfo *vi;
    unsigned weight[3];
    float fweight[3];
    int process[3];
    int cpulevel;
} MergeDataExtra;

typedef DualNodeData<MergeDataExtra> MergeData;

const unsigned MergeShift = 15;

static const VSFrame *VS_CC mergeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeData *d = reinterpret_cast<MergeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = {0, 1, 2};
        const VSFrame *fs[] = { 0, src1, src2 };
        const VSFrame *fr[] = {fs[d->process[0]], fs[d->process[1]], fs[d->process[2]]};
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane] == 0) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, union vs_merge_weight, unsigned) = 0;
                union vs_merge_weight weight;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_c;
                }

                if (!func)
                    continue;

                if (d->vi->format.sampleType == stInteger)
                    weight.u = d->weight[plane];
                else
                    weight.f = d->fweight[plane];

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, weight, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC mergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeData> d(new MergeData(vsapi));

    int nweight = vsapi->mapNumElements(in, "weight");
    for (int i = 0; i < 3; i++)
        d->fweight[i] = 0.5f;
    for (int i = 0; i < nweight; i++)
        d->fweight[i] = (float)vsapi->mapGetFloat(in, "weight", i, 0);

    if (nweight == 2) {
        d->fweight[2] = d->fweight[1];
    } else if (nweight == 1) {
        d->fweight[1] = d->fweight[0];
        d->fweight[2] = d->fweight[0];
    }

    for (int i = 0; i < 3; i++) {
        if (d->fweight[i] < 0 || d->fweight[i] > 1)
            RETERROR("Merge: weights must be between 0 and 1");
        d->weight[i] = std::min<unsigned>((d->fweight[i] * (1 << MergeShift) + 0.5f), (1U << MergeShift) - 1);
    }

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    for (int i = 0; i < 3; i++) {
        d->process[i] = 0;
        if (d->vi->format.sampleType == stInteger) {
            if (d->weight[i] == 0)
                d->process[i] = 1;
            else if (d->weight[i] == 1 << MergeShift)
                d->process[i] = 2;
        } else if (d->vi->format.sampleType == stFloat) {
            if (d->fweight[i] == 0.0f)
                d->process[i] = 1;
            else if (d->fweight[i] == 1.0f)
                d->process[i] = 2;
        }
    }

    d->cpulevel = vs_get_cpulevel(core);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(("Merge: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(d->vi->format, vsapi)).c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR("Merge: both clips must have constant format and dimensions, and the same format and dimensions");

    if (nweight > d->vi->format.numPlanes)
        RETERROR("Merge: more weights given than the number of planes to merge");

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "Merge", d->vi, mergeGetFrame, filterFree<MergeData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MaskedMerge

typedef struct {
    const VSVideoInfo *vi;
    bool premultiplied;
    bool first_plane;
    bool process[3];
    int cpulevel;
} MaskedMergeDataExtra;

typedef VariableNodeData<MaskedMergeDataExtra> MaskedMergeData;

static const VSFrame *VS_CC maskedMergeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = reinterpret_cast<MaskedMergeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
        if (d->nodes[3])
            vsapi->requestFrameFilter(n, d->nodes[3], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *mask = vsapi->getFrameFilter(n, d->nodes[2], frameCtx);
        const VSFrame *mask23 = nullptr;
        unsigned offset1 = getLimitedRangeOffset(src1, d->vi, vsapi);
        unsigned offset2 = getLimitedRangeOffset(src2, d->vi, vsapi);

        const int pl[] = {0, 1, 2};
        const VSFrame *fr[] = {d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1};
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        if (d->nodes[3])
           mask23 = vsapi->getFrameFilter(n, d->nodes[3], frameCtx);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                const uint8_t *maskp = vsapi->getReadPtr((plane && mask23) ? mask23 : mask, d->first_plane ? 0 : plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, const void *, void *, unsigned, unsigned, unsigned) = 0;
                int yuvhandling = (plane > 0) && (d->vi->format.colorFamily == cfYUV);

                if (d->premultiplied && d->vi->format.sampleType == stInteger && offset1 != offset2) {
                    vsapi->freeFrame(src1);
                    vsapi->freeFrame(src2);
                    vsapi->freeFrame(mask);
                    vsapi->freeFrame(mask23);
                    vsapi->freeFrame(dst);
                    vsapi->setFilterError("MaskedMerge: Input frames must have the same range", frameCtx);
                    return nullptr;
                }

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_avx2 : vs_mask_merge_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_avx2 : vs_mask_merge_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_avx2 : vs_mask_merge_float_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_sse2 : vs_mask_merge_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_sse2 : vs_mask_merge_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_sse2 : vs_mask_merge_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_c : vs_mask_merge_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_c : vs_mask_merge_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_c : vs_mask_merge_float_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; y++) {
                    func(srcp1, srcp2, maskp, dstp, depth, yuvhandling ? (1 << (depth - 1)) : offset1, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    maskp += stride;
                    dstp += stride;
                }
            }
        }
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        vsapi->freeFrame(mask);
        vsapi->freeFrame(mask23);
        return dst;
    }

    return nullptr;
}

static void VS_CC maskedMergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MaskedMergeData> d(new MaskedMergeData(vsapi));

    d->nodes.resize(4);

    int err;
    d->nodes[0] = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->nodes[1] = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->nodes[2] = vsapi->mapGetNode(in, "mask", 0, 0);
    d->vi = vsapi->getVideoInfo(d->nodes[0]);
    const VSVideoInfo *maskvi = vsapi->getVideoInfo(d->nodes[2]);
    d->first_plane = !!vsapi->mapGetInt(in, "first_plane", 0, &err);
    d->premultiplied = !!vsapi->mapGetInt(in, "premultiplied", 0, &err);
    // always use the first mask plane for all planes when it is the only one
    if (maskvi->format.numPlanes == 1)
        d->first_plane = 1;

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(("MaskedMerge: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(d->vi->format, vsapi)).c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->nodes[1])))
        RETERROR("MaskedMerge: both clips must have constant format and dimensions, and the same format and dimensions");

    if (maskvi->width != d->vi->width || maskvi->height != d->vi->height || maskvi->format.bitsPerSample != d->vi->format.bitsPerSample
        || (!isSameVideoFormat(&maskvi->format, &d->vi->format) && maskvi->format.colorFamily != cfGray && !d->first_plane))
        RETERROR("MaskedMerge: mask clip must have same dimensions as main clip and be the same format or equivalent grayscale version");

    if (!getProcessPlanesArg(in, out, "MaskedMerge", d->process, vsapi))
        return;

    // do we need to resample the first mask plane and use it for all the planes?
    if ((d->first_plane && d->vi->format.numPlanes > 1) && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0) && (d->process[1] || d->process[2])) {
        VSMap *min = vsapi->createMap();

        if (maskvi->format.numPlanes > 1) {
            // Don't resize the unused second and third planes.
            vsapi->mapSetNode(min, "clips", d->nodes[2], maAppend);
            vsapi->mapSetInt(min, "planes", 0, maAppend);
            vsapi->mapSetInt(min, "colorfamily", cfGray, maAppend);
            VSMap *mout = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShufflePlanes", min);
            VSNode *mask_first_plane = vsapi->mapGetNode(mout, "clip", 0, 0);
            vsapi->freeMap(mout);
            vsapi->clearMap(min);
            vsapi->mapConsumeNode(min, "clip", mask_first_plane, maAppend);
        } else {
            vsapi->mapSetNode(min, "clip", d->nodes[2], maAppend);
        }

        vsapi->mapSetInt(min, "width", d->vi->width >> d->vi->format.subSamplingW, maAppend);
        vsapi->mapSetInt(min, "height", d->vi->height >> d->vi->format.subSamplingH, maAppend);
        VSMap *mout = vsapi->invoke(vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core), "Bilinear", min);
        d->nodes[3] = vsapi->mapGetNode(mout, "clip", 0, 0);
        vsapi->freeMap(mout);
        vsapi->freeMap(min);
    }

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{ d->nodes[0], rpStrictSpatial }, { d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpGeneral }, { d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpGeneral }, { d->nodes[3], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpGeneral }};
    vsapi->createVideoFilter(out, "MaskedMerge", d->vi, maskedMergeGetFrame, filterFree<MaskedMergeData>, fmParallel, deps, d->nodes[3] ? 4 : 3, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MakeDiff

typedef struct {
    const VSVideoInfo *vi;
    bool process[3];
    int cpulevel;
} MakeDiffDataExtra;

typedef DualNodeData<MakeDiffDataExtra> MakeDiffData;

static const VSFrame *VS_CC makeDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MakeDiffData *d = reinterpret_cast<MakeDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1 };
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, depth, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC makeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MakeDiffData> d(new MakeDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(("MakeDiff: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(d->vi->format, vsapi)).c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR("MakeDiff: both clips must have constant format and dimensions, and the same format and dimensions");

    if (!getProcessPlanesArg(in, out, "MakeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "MakeDiff", d->vi, makeDiffGetFrame, filterFree<MakeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MergeDiff

struct MergeDiffDataExtra {
    const VSVideoInfo *vi;
    bool process[3];
    int cpulevel;
};

typedef DualNodeData<MergeDiffDataExtra> MergeDiffData;

static const VSFrame *VS_CC mergeDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeDiffData *d = reinterpret_cast<MergeDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1 };
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src1, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, depth, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC mergeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeDiffData> d(new MergeDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(("MergeDiff: only 8-16 bit integer and 32 bit float input supported, passed " + videoFormatToName(d->vi->format, vsapi)).c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR("MergeDiff: both clips must have constant format and dimensions, and the same format and dimensions");

    if (!getProcessPlanesArg(in, out, "MergeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "MergeDiff", d->vi, mergeDiffGetFrame, filterFree<MergeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void mergeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("PreMultiply", "clip:vnode;alpha:vnode;", "clip:vnode;", preMultiplyCreate, 0, plugin);
    vspapi->registerFunction("Merge", "clipa:vnode;clipb:vnode;weight:float[]:opt;", "clip:vnode;", mergeCreate, 0, plugin);
    vspapi->registerFunction("MaskedMerge", "clipa:vnode;clipb:vnode;mask:vnode;planes:int[]:opt;first_plane:int:opt;premultiplied:int:opt;", "clip:vnode;", maskedMergeCreate, 0, plugin);
    vspapi->registerFunction("MakeDiff", "clipa:vnode;clipb:vnode;planes:int[]:opt;", "clip:vnode;", makeDiffCreate, 0, plugin);
    vspapi->registerFunction("MergeDiff", "clipa:vnode;clipb:vnode;planes:int[]:opt;", "clip:vnode;", mergeDiffCreate, 0, plugin);
}
