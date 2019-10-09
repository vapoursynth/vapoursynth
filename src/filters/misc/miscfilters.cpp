/*
* Copyright (c) 2016 Fredrik Mellbin & other contributors
*
* This file is part of VapourSynth's miscellaneous filters package.
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

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <memory>
#include <vector>
#include <VapourSynth.h>
#include <VSHelper.h>
#include "../src/core/filtersharedcpp.h"
#include "../src/core/filtershared.h"

#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

///////////////////////////////////////
// SCDetect

typedef struct {
    VSNodeRef *node;
    VSNodeRef *diffnode;
    double threshold;
} SCDetectData;

static const VSFrameRef *VS_CC scDetectGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SCDetectData *d = static_cast<SCDetectData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(std::max(n - 1, 0), d->diffnode, frameCtx);
        vsapi->requestFrameFilter(n, d->diffnode, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *prevframe = vsapi->getFrameFilter(std::max(n - 1, 0), d->diffnode, frameCtx);
        const VSFrameRef *nextframe = vsapi->getFrameFilter(n, d->diffnode, frameCtx);

        double prevdiff = vsapi->propGetFloat(vsapi->getFramePropsRO(prevframe), "SCPlaneStatsDiff", 0, nullptr);
        double nextdiff = vsapi->propGetFloat(vsapi->getFramePropsRO(nextframe), "SCPlaneStatsDiff", 0, nullptr);

        VSFrameRef *dst = vsapi->copyFrame(src, core);
        VSMap *rwprops = vsapi->getFramePropsRW(dst);
        vsapi->propSetInt(rwprops, "_SceneChangePrev", prevdiff > d->threshold, paReplace);
        vsapi->propSetInt(rwprops, "_SceneChangeNext", nextdiff > d->threshold, paReplace);
        vsapi->freeFrame(src);
        vsapi->freeFrame(prevframe);
        vsapi->freeFrame(nextframe);

        return dst;
    }

    return nullptr;
}

static void VS_CC scDetectFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SCDetectData *d = static_cast<SCDetectData *>(instanceData);
    vsapi->freeNode(d->diffnode);
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC scDetectCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SCDetectData> d(new SCDetectData());
    int err;
    d->threshold = vsapi->propGetFloat(in, "threshold", 0, &err);
    if (err)
        d->threshold = 0.1;
    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    try {
        if (d->threshold < 0.0 || d->threshold > 1.0)
            throw std::string("threshold must be between 0 and 1");
        shared816FFormatCheck(vi->format);

        if (vi->numFrames == 1)
            throw std::string("clip must have more than one frame");

        VSMap *invmap = vsapi->createMap();
        VSMap *invmap2 = nullptr;
        vsapi->propSetNode(invmap, "clip", d->node, paAppend);
        vsapi->propSetInt(invmap, "first", 1, paAppend);
        invmap2 = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.std", core), "Trim", invmap);
        VSNodeRef *tempnode = vsapi->propGetNode(invmap2, "clip", 0, nullptr);
        vsapi->freeMap(invmap2);
        vsapi->clearMap(invmap);
        vsapi->propSetNode(invmap, "clipa", d->node, paAppend);
        vsapi->propSetNode(invmap, "clipb", tempnode, paAppend);
        vsapi->propSetData(invmap, "prop", "SCPlaneStats", -1, paAppend);
        vsapi->propSetInt(invmap, "plane", 0, paAppend);
        vsapi->freeNode(tempnode);
        invmap2 = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.std", core), "PlaneStats", invmap);
        vsapi->freeMap(invmap);
        invmap = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.std", core), "Cache", invmap2);
        vsapi->freeMap(invmap2);
        d->diffnode = vsapi->propGetNode(invmap, "clip", 0, nullptr);
        vsapi->freeMap(invmap);
    } catch (const std::string &e) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, ("SCDetect: " + e).c_str());
        return;
    }

    vsapi->createFilter(in, out, "SCDetect", templateNodeInit<SCDetectData>, scDetectGetFrame, scDetectFree, fmParallel, 0, d.release(), core);
}

///////////////////////////////////////
// AverageFrames

typedef struct {
    std::vector<int> weights;
    std::vector<float> fweights;
    std::vector<VSNodeRef *> nodes;
    VSVideoInfo vi;
    unsigned scale;
    float fscale;
    bool useSceneChange;
    bool process[3];
} AverageFrameData;

template<typename T>
static void averageFramesI(const std::vector<const VSFrameRef *> &srcs, VSFrameRef *dst, const int * const VS_RESTRICT weights, unsigned scale, unsigned bits, int plane, const VSAPI *vsapi) {
    int stride = vsapi->getStride(dst, plane) / sizeof(T);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);
    T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));
    std::vector<const T *> srcpv(srcs.size());
    for (size_t i = 0; i < srcpv.size(); i++)
        srcpv[i] = reinterpret_cast<const T *>(vsapi->getReadPtr(srcs[i], plane));

    const size_t numSrcs = srcpv.size();
    const T **srcpp = srcpv.data();
    unsigned maxVal = (1 << bits) - 1;

    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            int acc = 0;
            for (size_t i = 0; i < numSrcs; i++)
                acc += srcpp[i][w] * weights[i];

            unsigned acc2 = std::max(0, acc);
            acc2 += scale - 1;
            acc2 /= scale;
            acc2 = std::min(acc2, maxVal);
            dstp[w] = static_cast<T>(acc2);
        }
        for (size_t i = 0; i < numSrcs; i++)
            srcpp[i] += stride;
        dstp += stride;
    }
}

static void averageFramesF(const std::vector<const VSFrameRef *> &srcs, VSFrameRef *dst, const float * const VS_RESTRICT weights, float scale, int plane, const VSAPI *vsapi) {
    int stride = vsapi->getStride(dst, plane) / sizeof(float);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);
    float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));
    std::vector<const float *> srcpv(srcs.size());
    for (size_t i = 0; i < srcpv.size(); i++)
        srcpv[i] = reinterpret_cast<const float *>(vsapi->getReadPtr(srcs[i], plane));

    const size_t numSrcs = srcpv.size();
    const float **srcpp = srcpv.data();

    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            float acc = 0;
            for (size_t i = 0; i < numSrcs; i++)
                acc += srcpp[i][w] * weights[i];
            dstp[w] = acc * scale;
        }
        for (size_t i = 0; i < numSrcs; i++)
            srcpp[i] += stride;
        dstp += stride;
    }
}

#ifdef VS_TARGET_CPU_X86
static void averageFramesByteSSE2(const std::vector<const VSFrameRef *> &srcs, VSFrameRef *dst, const int * const VS_RESTRICT weights, unsigned scale, unsigned bits, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane);
    unsigned width = vsapi->getFrameWidth(dst, plane);
    unsigned height = vsapi->getFrameHeight(dst, plane);
    uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
    const uint8_t *srcpv[32];
    size_t numSrcs = std::min(srcs.size(), static_cast<size_t>(31));
    __m128i weights_epi16[16];

    for (size_t i = 0; i < numSrcs; i += 2) {
        srcpv[i + 0] = vsapi->getReadPtr(srcs[i], plane);
        srcpv[i + 1] = vsapi->getReadPtr(srcs[std::min(i + 1, numSrcs - 1)], plane);

        uint16_t weights_lo = static_cast<int16_t>(weights[i]);
        uint16_t weights_hi = i + 1 < numSrcs ? static_cast<int16_t>(weights[i + 1]) : static_cast<int16_t>(0);
        weights_epi16[i / 2] = _mm_set1_epi32((static_cast<uint32_t>(weights_hi) << 16) | weights_lo);
    }

    for (size_t h = 0; h < height; ++h) {
        for (size_t w = 0; w < width; w += 16) {
            __m128i accum_lolo = _mm_setzero_si128();
            __m128i accum_lohi = _mm_setzero_si128();
            __m128i accum_hilo = _mm_setzero_si128();
            __m128i accum_hihi = _mm_setzero_si128();

            for (size_t i = 0; i < numSrcs; i += 2) {
                __m128i coeffs = weights_epi16[i / 2];
                __m128i v1 = _mm_load_si128((const __m128i *)(srcpv[i + 0] + w));
                __m128i v2 = _mm_load_si128((const __m128i *)(srcpv[i + 1] + w));
                __m128i v1_lo = _mm_unpacklo_epi8(v1, _mm_setzero_si128());
                __m128i v1_hi = _mm_unpackhi_epi8(v1, _mm_setzero_si128());
                __m128i v2_lo = _mm_unpacklo_epi8(v2, _mm_setzero_si128());
                __m128i v2_hi = _mm_unpackhi_epi8(v2, _mm_setzero_si128());

                accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_lo, v2_lo)));
                accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_lo, v2_lo)));
                accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_hi, v2_hi)));
                accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_hi, v2_hi)));
            }

            __m128 accumf_lolo = _mm_cvtepi32_ps(accum_lolo);
            __m128 accumf_lohi = _mm_cvtepi32_ps(accum_lohi);
            __m128 accumf_hilo = _mm_cvtepi32_ps(accum_hilo);
            __m128 accumf_hihi = _mm_cvtepi32_ps(accum_hihi);
            accumf_lolo = _mm_mul_ps(accumf_lolo, _mm_set_ps1(1.0f / scale));
            accumf_lohi = _mm_mul_ps(accumf_lohi, _mm_set_ps1(1.0f / scale));
            accumf_hilo = _mm_mul_ps(accumf_hilo, _mm_set_ps1(1.0f / scale));
            accumf_hihi = _mm_mul_ps(accumf_hihi, _mm_set_ps1(1.0f / scale));

            accum_lolo = _mm_cvtps_epi32(accumf_lolo);
            accum_lohi = _mm_cvtps_epi32(accumf_lohi);
            accum_hilo = _mm_cvtps_epi32(accumf_hilo);
            accum_hihi = _mm_cvtps_epi32(accumf_hihi);

            accum_lolo = _mm_packs_epi32(accum_lolo, accum_lohi);
            accum_hilo = _mm_packs_epi32(accum_hilo, accum_hihi);
            accum_lolo = _mm_packus_epi16(accum_lolo, accum_hilo);
            _mm_store_si128((__m128i *)(dstp + w), accum_lolo);
        }
        for (size_t i = 0; i < numSrcs; ++i) {
            srcpv[i] += stride;
        }
        dstp += stride;
    }
}

static void averageFramesWordSSE2(const std::vector<const VSFrameRef *> &srcs, VSFrameRef *dst, const int * const VS_RESTRICT weights, unsigned scale, unsigned bits, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane);
    unsigned width = vsapi->getFrameWidth(dst, plane);
    unsigned height = vsapi->getFrameHeight(dst, plane);
    uint16_t * VS_RESTRICT dstp = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(dst, plane));
    const uint16_t *srcpv[32];
    size_t numSrcs = std::min(srcs.size(), static_cast<size_t>(31));
    unsigned maxval = (1U << bits) - 1;
    __m128i weights_epi16[16];
    __m128i bias = _mm_setzero_si128();

    for (size_t i = 0; i < numSrcs; i += 2) {
        srcpv[i + 0] = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(srcs[i], plane));
        srcpv[i + 1] = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(srcs[std::min(i + 1, numSrcs - 1)], plane));

        uint16_t weights_lo = static_cast<int16_t>(weights[i]);
        uint16_t weights_hi = i + 1 < numSrcs ? static_cast<int16_t>(weights[i + 1]) : static_cast<int16_t>(0);
        weights_epi16[i / 2] = _mm_set1_epi32((static_cast<uint32_t>(weights_hi) << 16) | weights_lo);

        bias = _mm_add_epi32(bias, _mm_madd_epi16(weights_epi16[i / 2], _mm_set1_epi16(INT16_MIN)));
    }

    for (size_t h = 0; h < height; ++h) {
        for (size_t w = 0; w < width; w += 8) {
            __m128i accum_lo = _mm_setzero_si128();
            __m128i accum_hi = _mm_setzero_si128();

            for (size_t i = 0; i < numSrcs; i += 2) {
                __m128i coeffs = weights_epi16[i / 2];
                __m128i v1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcpv[i + 0] + w)), _mm_set1_epi16(INT16_MIN));
                __m128i v2 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcpv[i + 1] + w)), _mm_set1_epi16(INT16_MIN));
                __m128i lo = _mm_unpacklo_epi16(v1, v2);
                __m128i hi = _mm_unpackhi_epi16(v1, v2);

                accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(coeffs, lo));
                accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(coeffs, hi));
            }
            accum_lo = _mm_sub_epi32(accum_lo, bias);
            accum_hi = _mm_sub_epi32(accum_hi, bias);

            __m128 accumf_lo = _mm_cvtepi32_ps(accum_lo);
            __m128 accumf_hi = _mm_cvtepi32_ps(accum_hi);
            accumf_lo = _mm_mul_ps(accumf_lo, _mm_set_ps1(1.0f / scale));
            accumf_hi = _mm_mul_ps(accumf_hi, _mm_set_ps1(1.0f / scale));

            accum_lo = _mm_cvtps_epi32(accumf_lo);
            accum_hi = _mm_cvtps_epi32(accumf_hi);
            accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(INT16_MIN));
            accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(INT16_MIN));

            __m128i tmp = _mm_packs_epi32(accum_lo, accum_hi);
            tmp = _mm_min_epi16(tmp, _mm_set1_epi16(static_cast<int16_t>(static_cast<int32_t>(maxval) + INT16_MIN)));
            tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));
            _mm_store_si128((__m128i *)(dstp + w), tmp);
        }
        for (size_t i = 0; i < numSrcs; ++i) {
            srcpv[i] = reinterpret_cast<const uint16_t *>(reinterpret_cast<const uint8_t *>(srcpv[i]) + stride);
        }
        dstp = reinterpret_cast<uint16_t *>(reinterpret_cast<uint8_t *>(dstp) + stride);
    }
}

static void averageFramesFloatSSE2(const std::vector<const VSFrameRef *> &srcs, VSFrameRef *dst, const float * const VS_RESTRICT weights, float scale, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane);
    unsigned width = vsapi->getFrameWidth(dst, plane);
    unsigned height = vsapi->getFrameHeight(dst, plane);
    float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));
    const float *srcpv[31];
    size_t numSrcs = std::min(srcs.size(), static_cast<size_t>(31));
    __m128 weights_ps[31];

    for (size_t i = 0; i < numSrcs; ++i) {
        srcpv[i] = reinterpret_cast<const float *>(vsapi->getReadPtr(srcs[i], plane));
        weights_ps[i] = _mm_set_ps1(weights[i]);
    }

    for (size_t h = 0; h < height; ++h) {
        for (size_t w = 0; w < width; w += 4) {
            __m128 accum = _mm_setzero_ps();

            for (size_t i = 0; i < numSrcs; ++i) {
                accum = _mm_add_ps(accum, _mm_mul_ps(weights_ps[i], _mm_load_ps(srcpv[i] + w)));
            }
            accum = _mm_mul_ps(accum, _mm_set_ps1(scale));
            _mm_store_ps(dstp + w, accum);
        }
        for (size_t i = 0; i < numSrcs; ++i) {
            srcpv[i] = reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(srcpv[i]) + stride);
        }
        dstp = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(dstp) + stride);
    }
}
#endif

static const VSFrameRef *VS_CC averageFramesGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AverageFrameData *d = static_cast<AverageFrameData *>(*instanceData);
    bool singleClipMode = (d->nodes.size() == 1);
    bool clamp = (n > INT_MAX - 1 - (int)(d->weights.size() / 2));
    int lastframe = clamp ? INT_MAX - 1 : n + (int)(d->weights.size() / 2);

    if (activationReason == arInitial) {
        if (singleClipMode) {
            for (int i = std::max(0, n - (int)(d->weights.size() / 2)); i <= lastframe; i++)
                vsapi->requestFrameFilter(i, d->nodes[0], frameCtx);
        } else {
            for (auto iter : d->nodes)
                vsapi->requestFrameFilter(n, iter, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        std::vector<const VSFrameRef *> frames(d->weights.size());

        if (singleClipMode) {
            int fn = n - (int)(d->weights.size() / 2);
            for (size_t i = 0; i < d->weights.size(); i++) {
                frames[i] = vsapi->getFrameFilter(std::max(0, fn), d->nodes[0], frameCtx);
                if (fn < INT_MAX - 1)
                    fn++;
            }
        } else {
            for (size_t i = 0; i < d->weights.size(); i++)
                frames[i] = vsapi->getFrameFilter(n, d->nodes[i], frameCtx);
        }

        const VSFrameRef *center = (singleClipMode ? frames[frames.size() / 2] : frames[0]);
        const VSFormat *fi = vsapi->getFrameFormat(center);

        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = {
            d->process[0] ? nullptr : center,
            d->process[1] ? nullptr : center,
            d->process[2] ? nullptr : center
        };

        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(center, 0), vsapi->getFrameHeight(center, 0), fr, pl, center, core);

        std::vector<int> weights(d->weights);
        std::vector<float> fweights(d->fweights);

        if (d->useSceneChange) {
            int fromFrame = 0;
            int toFrame = weights.size();

            for (int i = weights.size() / 2; i > 0; i--) {
                const VSMap *props = vsapi->getFramePropsRO(frames[i]);
                int err;
                if (vsapi->propGetInt(props, "_SceneChangePrev", 0, &err)) {
                    fromFrame = i;
                    break;
                }
            }

            for (int i = weights.size() / 2; i < static_cast<int>(weights.size()) - 1; i++) {
                const VSMap *props = vsapi->getFramePropsRO(frames[i]);
                int err;
                if (vsapi->propGetInt(props, "_SceneChangeNext", 0, &err)) {
                    toFrame = i;
                    break;
                }
            }

            if (fi->sampleType == stInteger) {
                int acc = 0;

                for (int i = toFrame + 1; i < static_cast<int>(weights.size()); i++) {
                    acc += weights[i];
                    weights[i] = 0;
                }

                for (int i = 0; i < fromFrame; i++) {
                    acc += weights[i];
                    weights[i] = 0;
                }

                weights[weights.size() / 2] += acc;
            } else {
                float acc = 0;

                for (int i = toFrame + 1; i < static_cast<int>(fweights.size()); i++) {
                    acc += fweights[i];
                    fweights[i] = 0;
                }

                for (int i = 0; i < fromFrame; i++) {
                    acc += fweights[i];
                    fweights[i] = 0;
                }

                fweights[fweights.size() / 2] += acc;
            }
        }

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
#ifdef VS_TARGET_CPU_X86
                if (fi->bytesPerSample == 1)
                    averageFramesByteSSE2(frames, dst, weights.data(), d->scale, 8, plane, vsapi);
                else if (fi->bytesPerSample == 2)
                    averageFramesWordSSE2(frames, dst, weights.data(), d->scale, fi->bitsPerSample, plane, vsapi);
                else
                    averageFramesFloatSSE2(frames, dst, fweights.data(), 1 / d->fscale, plane, vsapi);
#else
                if (fi->bytesPerSample == 1)
                    averageFramesI<uint8_t>(frames, dst, weights.data(), d->scale, 8, plane, vsapi);
                else if (fi->bytesPerSample == 2)
                    averageFramesI<uint16_t>(frames, dst, weights.data(), d->scale, fi->bitsPerSample, plane, vsapi);
                else
                    averageFramesF(frames, dst, fweights.data(), 1 / d->fscale, plane, vsapi);
#endif
            }
        }

        for (auto iter : frames)
            vsapi->freeFrame(iter);

        return dst;
    }

    return nullptr;
}

static void VS_CC averageFramesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    AverageFrameData *d = static_cast<AverageFrameData *>(instanceData);
    for (auto iter : d->nodes)
        vsapi->freeNode(iter);
    delete d;
}

static void VS_CC averageFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AverageFrameData> d(new AverageFrameData());
    int numNodes = vsapi->propNumElements(in, "clips");
    int numWeights = vsapi->propNumElements(in, "weights");
    int err;

    try {
        if (numNodes == 1) {
            if ((numWeights % 2) != 1)
                throw std::string("Number of weights must be odd when only one clip supplied");
        } else if (numWeights != numNodes) {
            throw std::string("Number of weights must match number of clips supplied");
        } else if (numWeights > 31 || numNodes > 31) {
            throw std::string("Must use between 1 and 31 weights and input clips");
        }

        d->useSceneChange = !!vsapi->propGetInt(in, "scenechange", 0, &err);
        if (numNodes != 1 && d->useSceneChange)
            throw std::string("Scenechange can only be used in single clip mode");

        for (int i = 0; i < numNodes; i++)
            d->nodes.push_back(vsapi->propGetNode(in, "clips", i, 0));

        d->vi = *vsapi->getVideoInfo(d->nodes[0]);
        shared816FFormatCheck(d->vi.format);

        for (auto iter : d->nodes) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(iter);
            d->vi.numFrames = std::max(d->vi.numFrames, vi->numFrames);
            if (!isSameFormat(&d->vi, vi))
                throw std::string("All clips must have the same format");
        }

        for (int i = 0; i < numWeights; i++) {
            d->fweights.push_back(static_cast<float>(vsapi->propGetFloat(in, "weights", i, 0)));
            d->weights.push_back(std::lround(vsapi->propGetFloat(in, "weights", i, 0)));
            if (d->vi.format->sampleType == stInteger && std::abs(d->weights[i]) > 1023)
                throw std::string("coefficients may only be between -1023 and 1023");
        }

        float scale = vsapi->propGetFloat(in, "scale", 0, &err);
        if (err) {
            float scalef = 0;
            int scalei = 0;
            for (int i = 0; i < numWeights; i++) {
                scalef += d->fweights[i];
                scalei += d->weights[i];
            }
            if (scalei < 1)
                d->scale = 1;
            else
                d->scale = scalei;
            // match behavior of integer even if floating point isn't slower with signed stuff
            if (scalef < FLT_EPSILON)
                d->fscale = 1;
            else
                d->fscale = scalef;
        } else {
            if (d->vi.format->sampleType == stInteger) {
                d->scale = floatToIntS(scale);
                if (d->scale < 1)
                    throw std::string("scale must be a positive number");
            } else {
                d->fscale = scale;
                if (d->fscale < FLT_EPSILON)
                    throw std::string("scale must be a positive number");
            }
        }

        getPlanesArg(in, d->process, vsapi);

    } catch (const std::string &e) {
        for (auto iter : d->nodes)
            vsapi->freeNode(iter);
        vsapi->setError(out, ("AverageFrames: " + e).c_str());
        return;
    }

    vsapi->createFilter(in, out, "AverageFrames", templateNodeCustomViInit<AverageFrameData>, averageFramesGetFrame, averageFramesFree, fmParallel, 0, d.release(), core);
}

///////////////////////////////////////
// Hysteresis

struct HysteresisData {
    VSNodeRef * node1, *node2;
    bool process[3];
    uint16_t peak;
    float lower[3], upper[3];
    size_t labelSize;
};

template<typename T>
static void process_frame_hysteresis(const VSFrameRef * src1, const VSFrameRef * src2, VSFrameRef * dst, const VSFormat *fi, const HysteresisData * d, const VSAPI * vsapi) VS_NOEXCEPT {
    uint8_t * VS_RESTRICT label = nullptr;

    for (int plane = 0; plane < fi->numPlanes; plane++) {
        if (d->process[plane]) {
            if (!label)
                label = new uint8_t[d->labelSize]();
            const int width = vsapi->getFrameWidth(src1, plane);
            const int height = vsapi->getFrameHeight(src1, plane);
            const int stride = vsapi->getStride(src1, plane) / sizeof(T);
            const T * srcp1 = reinterpret_cast<const T *>(vsapi->getReadPtr(src1, plane));
            const T * srcp2 = reinterpret_cast<const T *>(vsapi->getReadPtr(src2, plane));
            T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));

            T lower, upper;
            if (std::is_integral<T>::value) {
                lower = 0;
                upper = d->peak;
            } else {
                lower = d->lower[plane];
                upper = d->upper[plane];
            }

            std::fill_n(dstp, stride * height, lower);

            std::vector<std::pair<int, int>> coordinates;

            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (!label[width * y + x] && srcp1[stride * y + x] > lower && srcp2[stride * y + x] > lower) {
                        label[width * y + x] = std::numeric_limits<uint8_t>::max();
                        dstp[stride * y + x] = upper;

                        coordinates.emplace_back(std::make_pair(x, y));

                        while (!coordinates.empty()) {
                            const auto pos = coordinates.back();
                            coordinates.pop_back();

                            for (int yy = std::max(pos.second - 1, 0); yy <= std::min(pos.second + 1, height - 1); yy++) {
                                for (int xx = std::max(pos.first - 1, 0); xx <= std::min(pos.first + 1, width - 1); xx++) {
                                    if (!label[width * yy + xx] && srcp2[stride * yy + xx] > lower) {
                                        label[width * yy + xx] = std::numeric_limits<uint8_t>::max();
                                        dstp[stride * yy + xx] = upper;

                                        coordinates.emplace_back(std::make_pair(xx, yy));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    delete[] label;
}

static void VS_CC hysteresisInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    HysteresisData * d = static_cast<HysteresisData *>(*instanceData);
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node1), 1, node);
}

static const VSFrameRef *VS_CC hysteresisGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    HysteresisData * d = static_cast<HysteresisData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef * src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const VSFrameRef * fr[]{ d->process[0] ? nullptr : src1, d->process[1] ? nullptr : src1, d->process[2] ? nullptr : src1 };
        const int pl[]{ 0, 1, 2 };
        const VSFormat *fi = vsapi->getFrameFormat(src1);
        VSFrameRef * dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src1, 0), vsapi->getFrameHeight(src1, 0), fr, pl, src1, core);

        if (fi->bytesPerSample == 1)
            process_frame_hysteresis<uint8_t>(src1, src2, dst, fi, d, vsapi);
        else if (fi->bytesPerSample == 2)
            process_frame_hysteresis<uint16_t>(src1, src2, dst, fi, d, vsapi);
        else
            process_frame_hysteresis<float>(src1, src2, dst, fi, d, vsapi);

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC hysteresisFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    HysteresisData *d = static_cast<HysteresisData *>(instanceData);

    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);

    delete d;
}

static void VS_CC hysteresisCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<HysteresisData> d(new HysteresisData());

    d->node1 = vsapi->propGetNode(in, "clipa", 0, nullptr);
    d->node2 = vsapi->propGetNode(in, "clipb", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);

    try {

        if (!isConstantFormat(vi) || (vi->format->sampleType == stInteger && vi->format->bitsPerSample > 16) ||
            (vi->format->sampleType == stFloat && vi->format->bitsPerSample != 32))
            throw std::string("only constant format 8-16 bits integer and 32 bits float input supported");

        if (!isSameFormat(vi, vsapi->getVideoInfo(d->node2)))
            throw std::string("both clips must have the same dimensions and the same format");

        getPlanesArg(in, d->process, vsapi);

        if (vi->format->sampleType == stInteger) {
            d->peak = (1 << vi->format->bitsPerSample) - 1;
        } else {
            for (int plane = 0; plane < vi->format->numPlanes; plane++) {
                if (plane == 0 || vi->format->colorFamily == cmRGB) {
                    d->lower[plane] = 0.f;
                    d->upper[plane] = 1.f;
                } else {
                    d->lower[plane] = -0.5f;
                    d->upper[plane] = 0.5f;
                }
            }
        }

        d->labelSize = vi->width * vi->height;

    } catch (const std::string &e) {
        vsapi->freeNode(d->node1);
        vsapi->freeNode(d->node2);
        vsapi->setError(out, ("Hysteresis: " + e).c_str());
        return;
    }

    vsapi->createFilter(in, out, "Hysteresis", hysteresisInit, hysteresisGetFrame, hysteresisFree, fmParallel, 0, d.release(), core);
}

///////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.misc", "misc", "Miscellaneous filters", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("SCDetect", "clip:clip;threshold:float:opt;", scDetectCreate, 0, plugin);
    registerFunc("AverageFrames", "clips:clip[];weights:float[];scale:float:opt;scenechange:int:opt;planes:int[]:opt;", averageFramesCreate, 0, plugin);
    registerFunc("Hysteresis", "clipa:clip;clipb:clip;planes:int[]:opt;", hysteresisCreate, nullptr, plugin);
}
