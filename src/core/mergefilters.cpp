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
#include <string>
#include <algorithm>
#include "cpufeatures.h"
#include "filtershared.h"
#include "internalfilters.h"
#include "kernel/cpulevel.h"
#include "kernel/merge.h"
#include "VSHelper4.h"
#include "VSConstants4.h"

using namespace vsh;

//////////////////////////////////////////
// Chroma-location-aware mask resampling helpers shared by MaskedMerge and PreMultiply.
//
// When a single-plane mask/alpha at luma resolution is applied to a YUV clip
// with subsampled chroma, the mask must be downsampled to chroma resolution
// with output samples landing on luma-aligned positions implied by the
// source's _ChromaLocation. zimg's chromaloc handling only works for true U/V
// plane resamples, so we instead invoke resize plugin on the GRAY mask with
// explicit src_left/src_top shifts, pre-creating one candidate per
// VSChromaLocation value, dispatched based on each source frame's
// _ChromaLocation (guessing left when property is absent or invalid).

static constexpr int numChromaLocations = 6; // VSC_CHROMA_LEFT (0) to VSC_CHROMA_BOTTOM (5)

// src_left/src_top (in source luma pixels) for downsampling a luma-resolution mask to
// chroma resolution while aligning output samples to chromaloc-implied luma
// position. The default centered downsample places output at luma offset (s-1)/2
// within a block of s luma pixels. Shift by the chromaloc offset minus that center.
static void getChromalocLumaShift(int chromaloc, int subSamplingW, int subSamplingH,
                                  double *src_left, double *src_top) {
    int s_w = 1 << subSamplingW;
    int s_h = 1 << subSamplingH;
    double center_w = (s_w - 1) / 2.0;
    double center_h = (s_h - 1) / 2.0;

    double h_offset = center_w;
    double v_offset = center_h;

    switch (chromaloc) {
        case VSC_CHROMA_LEFT:         h_offset = 0.0;       v_offset = center_h; break;
        case VSC_CHROMA_CENTER:       h_offset = center_w;  v_offset = center_h; break;
        case VSC_CHROMA_TOP_LEFT:     h_offset = 0.0;       v_offset = 0.0;      break;
        case VSC_CHROMA_TOP:          h_offset = center_w;  v_offset = 0.0;      break;
        case VSC_CHROMA_BOTTOM_LEFT:  h_offset = 0.0;       v_offset = s_h - 1;  break;
        case VSC_CHROMA_BOTTOM:       h_offset = center_w;  v_offset = s_h - 1;  break;
        default: break;
    }

    *src_left = h_offset - center_w;
    *src_top = v_offset - center_h;
}

// Pre-create one resize candidate per VSChromaLocation via src_left/src_top
// shift. Return empty string on success. On fail, fill out with nullptrs and
// return error string. Caller is responsible for freeing any resulting nodes.
static std::string createChromaResizeCandidates(VSNode *mask_node, int dst_width, int dst_height,
                                              int subSamplingW, int subSamplingH,
                                              VSNode *out[numChromaLocations],
                                              VSCore *core, const VSAPI *vsapi) {
    for (int i = 0; i < numChromaLocations; i++)
        out[i] = nullptr;

    VSPlugin *resize_plugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core);
    if (!resize_plugin)
        return "resize plugin not available";

    for (int loc = 0; loc < numChromaLocations; loc++) {
        double src_left, src_top;
        getChromalocLumaShift(loc, subSamplingW, subSamplingH, &src_left, &src_top);

        VSMap *args = vsapi->createMap();
        vsapi->mapSetNode(args, "clip", mask_node, maAppend);
        vsapi->mapSetInt(args, "width", dst_width, maAppend);
        vsapi->mapSetInt(args, "height", dst_height, maAppend);
        vsapi->mapSetFloat(args, "src_left", src_left, maAppend);
        vsapi->mapSetFloat(args, "src_top", src_top, maAppend);

        VSMap *result = vsapi->invoke(resize_plugin, "Bilinear", args);
        vsapi->freeMap(args);

        const char *invoke_err = vsapi->mapGetError(result);
        if (invoke_err) {
            std::string msg = std::string("resize.Bilinear failed: ") + invoke_err;
            vsapi->freeMap(result);
            for (int j = 0; j < loc; j++) {
                vsapi->freeNode(out[j]);
                out[j] = nullptr;
            }
            return msg;
        }

        out[loc] = vsapi->mapGetNode(result, "clip", 0, nullptr);
        vsapi->freeMap(result);
    }
    return {};
}

// Read _ChromaLocation from frameprops, guessing left when prop absent or out
// of range. Always returns a VSChromaLocation index.
static int resolveChromaLocation(const VSFrame *frame, const VSAPI *vsapi) {
    int err;
    int64_t raw = vsapi->mapGetInt(vsapi->getFramePropertiesRO(frame), "_ChromaLocation", 0, &err);
    if (err || raw < VSC_CHROMA_LEFT || raw > VSC_CHROMA_BOTTOM)
        return VSC_CHROMA_LEFT;
    return static_cast<int>(raw);
}

//////////////////////////////////////////
// PreMultiply


typedef struct {
    const VSVideoInfo *vi;
    bool chroma_dispatch;  // If true, nodes 2-7 are 6 per-chromaloc resizes
    int cpulevel;
} PreMultiplyDataExtra;

typedef VariableNodeData<PreMultiplyDataExtra> PreMultiplyData;

static unsigned getLimitedRangeOffset(const VSFrame *f, const VSVideoInfo *vi, const VSAPI *vsapi) {
    int err;
    bool limited = (vsapi->mapGetInt(vsapi->getFramePropertiesRO(f), "_Range", 0, &err) == VSC_RANGE_LIMITED);
    if (err)
        limited = (vi->format.colorFamily == cfGray || vi->format.colorFamily == cfYUV);
    return (limited ? (16 << (vi->format.bitsPerSample - 8)) : 0);
}

static const VSFrame *VS_CC preMultiplyGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PreMultiplyData *d = reinterpret_cast<PreMultiplyData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        if (d->vi->format.numPlanes > 1 && !d->chroma_dispatch) {
            // No subsampling: reuse alpha for chroma planes (nodes[2] is an alpha alias).
            vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
        }
    } else if (activationReason == arAllFramesReady && d->chroma_dispatch && !frameData[0]) {
        // Defer chroma-resize candidate selection until we've read source frame's _ChromaLocation.
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        int loc = resolveChromaLocation(src1, vsapi);
        vsapi->freeFrame(src1);

        VSNode *chroma_candidate = d->nodes[2 + loc];
        frameData[0] = chroma_candidate;
        vsapi->requestFrameFilter(n, chroma_candidate, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *src2_23 = nullptr;
        if (d->vi->format.numPlanes > 1) {
            VSNode *chroma_node = d->chroma_dispatch
                ? reinterpret_cast<VSNode *>(frameData[0])
                : d->nodes[2];
            src2_23 = vsapi->getFrameFilter(n, chroma_node, frameCtx);
        }
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

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_half_avx2;
            }
#endif

            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                    func = vs_premultiply_byte_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_word_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                    func = vs_premultiply_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_half_c;
            }

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

    d->nodes.resize(2);
    d->nodes[0] = vsapi->mapGetNode(in, "clip", 0, 0);
    d->nodes[1] = vsapi->mapGetNode(in, "alpha", 0, 0);

    d->vi = vsapi->getVideoInfo(d->nodes[0]);

    const VSVideoInfo *alphavi = vsapi->getVideoInfo(d->nodes[1]);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "PreMultiply").c_str());

    if (alphavi->format.colorFamily != cfGray || alphavi->format.sampleType != d->vi->format.sampleType || alphavi->format.bitsPerSample != d->vi->format.bitsPerSample)
        RETERROR("PreMultiply: alpha clip must be grayscale and same sample format and bitdepth as main clip");

    if (!isConstantVideoFormat(d->vi) || !isConstantVideoFormat(alphavi) || d->vi->width != alphavi->width || d->vi->height != alphavi->height)
        RETERROR("PreMultiply: both clips must have the same constant format and dimensions");

    d->cpulevel = vs_get_cpulevel(core);

    bool subsampled = (d->vi->format.numPlanes > 1) && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0);
    d->chroma_dispatch = subsampled;

    if (subsampled) {
        VSNode *candidates[numChromaLocations] = {};
        std::string err_msg = createChromaResizeCandidates(d->nodes[1],
            d->vi->width >> d->vi->format.subSamplingW,
            d->vi->height >> d->vi->format.subSamplingH,
            d->vi->format.subSamplingW, d->vi->format.subSamplingH,
            candidates, core, vsapi);
        if (!err_msg.empty())
            RETERROR(("PreMultiply: " + err_msg).c_str());
        d->nodes.resize(2 + numChromaLocations);
        for (int i = 0; i < numChromaLocations; i++)
            d->nodes[2 + i] = candidates[i];
    } else if (d->vi->format.numPlanes > 1) {
        d->nodes.resize(3);
        d->nodes[2] = vsapi->addNodeRef(d->nodes[1]);
    }

    std::vector<VSFilterDependency> deps;
    deps.push_back({ d->nodes[0], rpStrictSpatial });
    deps.push_back({ d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    if (subsampled) {
        for (int i = 0; i < numChromaLocations; i++)
            deps.push_back({ d->nodes[2 + i], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2 + i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    } else if (d->vi->format.numPlanes > 1) {
        deps.push_back({ d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    }

    vsapi->createVideoFilter(out, "PreMultiply", d->vi, preMultiplyGetFrame, filterFree<PreMultiplyData>, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_half_avx2;
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_half_c;
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
            else if (d->fweight[i] == 1.0f)
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
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "Merge").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("Merge: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (nweight > d->vi->format.numPlanes)
        RETERROR("Merge: more weights given than the number of planes to merge");

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
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
    bool chroma_dispatch;   // If true, nodes 3-8 are 6 per-chromaloc resizes of mask
} MaskedMergeDataExtra;

typedef VariableNodeData<MaskedMergeDataExtra> MaskedMergeData;

static const VSFrame *VS_CC maskedMergeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = reinterpret_cast<MaskedMergeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
    } else if (activationReason == arAllFramesReady && d->chroma_dispatch && !frameData[0]) {
        // Defer chroma-resize selection until we've read source frame's _ChromaLocation.
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        int loc = resolveChromaLocation(src1, vsapi);
        vsapi->freeFrame(src1);

        VSNode *chroma_candidate = d->nodes[3 + loc];
        frameData[0] = chroma_candidate;
        vsapi->requestFrameFilter(n, chroma_candidate, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *mask = vsapi->getFrameFilter(n, d->nodes[2], frameCtx);
        const VSFrame *mask23 = nullptr;

        // With subsampled chroma, clipa and clipb chroma must sample the same
        // locations to be meaningfully merged.
        if (d->vi->format.subSamplingW > 0 || d->vi->format.subSamplingH > 0) {
            int loc1 = resolveChromaLocation(src1, vsapi);
            int loc2 = resolveChromaLocation(src2, vsapi);
            if (loc1 != loc2) {
                vsapi->freeFrame(src1);
                vsapi->freeFrame(src2);
                vsapi->freeFrame(mask);
                vsapi->setFilterError(("MaskedMerge: clipa and clipb have different chroma locations (_ChromaLocation "
                    + std::to_string(loc1) + " vs " + std::to_string(loc2) + ")").c_str(), frameCtx);
                return nullptr;
            }
        }

        unsigned offset1 = getLimitedRangeOffset(src1, d->vi, vsapi);
        unsigned offset2 = getLimitedRangeOffset(src2, d->vi, vsapi);

        const int pl[] = {0, 1, 2};
        const VSFrame *fr[] = {d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1};
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        if (d->chroma_dispatch)
            mask23 = vsapi->getFrameFilter(n, reinterpret_cast<VSNode *>(frameData[0]), frameCtx);
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
                if (getCPUFeatures()->avx512 && d->cpulevel >= VS_CPU_LEVEL_AVX512) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_avx512 : vs_mask_merge_byte_avx512;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_avx512 : vs_mask_merge_word_avx512;
                }
                if (!func && getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_avx2 : vs_mask_merge_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_avx2 : vs_mask_merge_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_avx2 : vs_mask_merge_float_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_half_avx2 : vs_mask_merge_half_avx2;
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_half_c : vs_mask_merge_half_c;
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

    d->nodes.resize(3);

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
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MaskedMerge").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->nodes[1])))
        RETERROR(("MaskedMerge: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->nodes[1]), vsapi)).c_str());

    if (maskvi->width != d->vi->width || maskvi->height != d->vi->height || maskvi->format.bitsPerSample != d->vi->format.bitsPerSample
        || (!isSameVideoFormat(&maskvi->format, &d->vi->format) && maskvi->format.colorFamily != cfGray && !d->first_plane))
        RETERROR(("MaskedMerge: mask clip must have same dimensions as main clip and be the same format or grayscale, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(maskvi, vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MaskedMerge", d->process, vsapi))
        return;

    // Do we need to resample the first mask plane and use it for the chroma planes?
    bool need_chroma_resize = (d->first_plane && d->vi->format.numPlanes > 1)
                              && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0)
                              && (d->process[1] || d->process[2]);
    d->chroma_dispatch = need_chroma_resize;

    if (need_chroma_resize) {
        // If the mask has more than 1 plane, extract plane 0 first so the
        // resize candidates only resample the plane we'll read.
        VSNode *single_plane_mask = nullptr;
        if (maskvi->format.numPlanes > 1) {
            VSMap *min = vsapi->createMap();
            vsapi->mapSetNode(min, "clips", d->nodes[2], maAppend);
            vsapi->mapSetInt(min, "planes", 0, maAppend);
            vsapi->mapSetInt(min, "colorfamily", cfGray, maAppend);
            VSMap *mout = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShufflePlanes", min);
            vsapi->freeMap(min);
            const char *shuffle_err = vsapi->mapGetError(mout);
            if (shuffle_err) {
                std::string msg = std::string("MaskedMerge: ShufflePlanes failed: ") + shuffle_err;
                vsapi->freeMap(mout);
                RETERROR(msg.c_str());
            }
            single_plane_mask = vsapi->mapGetNode(mout, "clip", 0, 0);
            vsapi->freeMap(mout);
        } else {
            single_plane_mask = vsapi->addNodeRef(d->nodes[2]);
        }

        VSNode *candidates[numChromaLocations] = {};
        std::string err_msg = createChromaResizeCandidates(single_plane_mask,
            d->vi->width >> d->vi->format.subSamplingW,
            d->vi->height >> d->vi->format.subSamplingH,
            d->vi->format.subSamplingW, d->vi->format.subSamplingH,
            candidates, core, vsapi);
        vsapi->freeNode(single_plane_mask);
        if (!err_msg.empty())
            RETERROR(("MaskedMerge: " + err_msg).c_str());

        d->nodes.resize(3 + numChromaLocations);
        for (int i = 0; i < numChromaLocations; i++)
            d->nodes[3 + i] = candidates[i];
    }

    d->cpulevel = vs_get_cpulevel(core);

    std::vector<VSFilterDependency> deps;
    deps.push_back({ d->nodes[0], rpStrictSpatial });
    deps.push_back({ d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    deps.push_back({ d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    if (need_chroma_resize) {
        for (int i = 0; i < numChromaLocations; i++)
            deps.push_back({ d->nodes[3 + i], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[3 + i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    }

    vsapi->createVideoFilter(out, "MaskedMerge", d->vi, maskedMergeGetFrame, filterFree<MaskedMergeData>, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_half_avx2;
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_half_c;
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
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MakeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MakeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MakeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MakeDiff", d->vi, makeDiffGetFrame, filterFree<MakeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MakeFullDiff

typedef struct {
    const VSVideoInfo *vi;
    VSVideoInfo outvi;
    int cpulevel;
} MakeFullDiffDataExtra;

typedef DualNodeData<MakeFullDiffDataExtra> MakeFullDiffData;

static const VSFrame *VS_CC makeFullDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MakeFullDiffData *d = reinterpret_cast<MakeFullDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->outvi.format, d->outvi.width, d->outvi.height, src1, core);
        for (int plane = 0; plane < d->outvi.format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src2, plane);
            ptrdiff_t srcstride = vsapi->getStride(src1, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
            uint8_t *VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

            void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_avx2;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_makediff_half_avx2;
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_sse2;
            }
#endif

            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 8)
                    func = vs_makefulldiff_byte_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample < 16)
                    func = vs_makefulldiff_word_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 16)
                    func = vs_makefulldiff_word_dword_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_makediff_half_c;
            }

            if (!func)
                continue;

            int depth = d->vi->format.bitsPerSample;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, w);
                srcp1 += srcstride;
                srcp2 += srcstride;
                dstp += dststride;
            }

        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC makeFullDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MakeFullDiffData> d(new MakeFullDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MakeFullDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MakeFullDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    d->outvi = *d->vi;
    if (d->outvi.format.sampleType == stInteger) {
        d->outvi.format.bitsPerSample++;
        d->outvi.format.bytesPerSample = (d->outvi.format.bitsPerSample > 16) ? 4 : 2;
    }

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = { {d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MakeFullDiff", &d->outvi, makeFullDiffGetFrame, filterFree<MakeFullDiffData>, fmParallel, deps, 2, d.get(), core);
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_half_avx2;
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
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_half_c;
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
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MergeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MergeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MergeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MergeDiff", d->vi, mergeDiffGetFrame, filterFree<MergeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MergeFullDiff

struct MergeFullDiffDataExtra {
    const VSVideoInfo *vi;
    int cpulevel;
};

typedef DualNodeData<MergeFullDiffDataExtra> MergeFullDiffData;

static const VSFrame *VS_CC mergeFullDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeFullDiffData *d = reinterpret_cast<MergeFullDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src1, plane);
            ptrdiff_t src1stride = vsapi->getStride(src1, plane);
            ptrdiff_t src2stride = vsapi->getStride(src2, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
            uint8_t *VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

            void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_avx2;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_mergediff_half_avx2;
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_sse2;
            }
#endif
            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 8)
                    func = vs_mergefulldiff_word_byte_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample < 16)
                    func = vs_mergefulldiff_word_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 16)
                    func = vs_mergefulldiff_dword_word_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_mergediff_half_c;
            }

            if (!func)
                continue;

            int depth = d->vi->format.bitsPerSample;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, w);
                srcp1 += src1stride;
                srcp2 += src2stride;
                dstp += dststride;
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static bool mergeFullDiffIsCompatibleVideoInfo(const VSVideoInfo *v1, const VSVideoInfo *v2) {
    return v1->height == v2->height && v1->width == v2->width && v1->format.colorFamily == v2->format.colorFamily && v1->format.sampleType == v2->format.sampleType && v1->format.bitsPerSample == v2->format.bitsPerSample - 1 && v1->format.subSamplingW == v2->format.subSamplingW && v1->format.subSamplingH == v2->format.subSamplingH;
}

static void VS_CC mergeFullDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeFullDiffData> d(new MergeFullDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MergeFullDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !mergeFullDiffIsCompatibleVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MergeFullDiff: both clips must have the same (bitdepth+1 for second clip) constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    d->cpulevel = vs_get_cpulevel(core);

    VSFilterDependency deps[] = { {d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly} };
    vsapi->createVideoFilter(out, "MergeFullDiff", d->vi, mergeFullDiffGetFrame, filterFree<MergeFullDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void mergeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("PreMultiply", "clip:vnode;alpha:vnode;", "clip:vnode;", preMultiplyCreate, 0, plugin);
    vspapi->registerFunction("Merge", "clipa:vnode;clipb:vnode;weight:float[]:opt;", "clip:vnode;", mergeCreate, 0, plugin);
    vspapi->registerFunction("MaskedMerge", "clipa:vnode;clipb:vnode;mask:vnode;planes:int[]:opt;first_plane:int:opt;premultiplied:int:opt;", "clip:vnode;", maskedMergeCreate, 0, plugin);
    vspapi->registerFunction("MakeDiff", "clipa:vnode;clipb:vnode;planes:int[]:opt;", "clip:vnode;", makeDiffCreate, 0, plugin);
    vspapi->registerFunction("MakeFullDiff", "clipa:vnode;clipb:vnode;", "clip:vnode;", makeFullDiffCreate, 0, plugin);
    vspapi->registerFunction("MergeDiff", "clipa:vnode;clipb:vnode;planes:int[]:opt;", "clip:vnode;", mergeDiffCreate, 0, plugin);
    vspapi->registerFunction("MergeFullDiff", "clipa:vnode;clipb:vnode;", "clip:vnode;", mergeFullDiffCreate, 0, plugin);
}
