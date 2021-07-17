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
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include <VapourSynth4.h>
#include <VSHelper4.h>
#include "../src/core/filtershared.h"
#include "../src/core/version.h"

#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace

using namespace vsh;

///////////////////////////////////////
// SCDetect

typedef struct {
    double threshold;
} SCDetectDataExtra;

typedef DualNodeData<SCDetectDataExtra> SCDetectData;

static const VSFrame *VS_CC scDetectGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SCDetectData *d = static_cast<SCDetectData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(std::max(n - 1, 0), d->node2, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *prevframe = vsapi->getFrameFilter(std::max(n - 1, 0), d->node2, frameCtx);
        const VSFrame *nextframe = vsapi->getFrameFilter(n, d->node2, frameCtx);

        double prevdiff = vsapi->mapGetFloat(vsapi->getFramePropertiesRO(prevframe), "SCPlaneStatsDiff", 0, nullptr);
        double nextdiff = vsapi->mapGetFloat(vsapi->getFramePropertiesRO(nextframe), "SCPlaneStatsDiff", 0, nullptr);

        VSFrame *dst = vsapi->copyFrame(src, core);
        VSMap *rwprops = vsapi->getFramePropertiesRW(dst);
        vsapi->mapSetInt(rwprops, "_SceneChangePrev", prevdiff > d->threshold, maReplace);
        vsapi->mapSetInt(rwprops, "_SceneChangeNext", nextdiff > d->threshold, maReplace);
        vsapi->freeFrame(src);
        vsapi->freeFrame(prevframe);
        vsapi->freeFrame(nextframe);

        return dst;
    }

    return nullptr;
}

static void VS_CC scDetectCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SCDetectData> d(new SCDetectData(vsapi));
    int err;
    d->threshold = vsapi->mapGetFloat(in, "threshold", 0, &err);
    if (err)
        d->threshold = 0.1;
    d->node1 = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);

    try {
        if (d->threshold < 0.0 || d->threshold > 1.0)
            throw std::runtime_error("threshold must be between 0 and 1");
        if (!is8to16orFloatFormat(vi->format))
            throw std::runtime_error("clip must be constant format and of integer 8-16 bit type or 32 bit float");
        if (vi->numFrames == 1)
            throw std::runtime_error("clip must have more than one frame");

        VSPlugin *stdplugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);
        VSMap *invmap = vsapi->createMap();
        VSMap *invmap2 = nullptr;
        vsapi->mapSetNode(invmap, "clip", d->node1, maAppend);
        vsapi->mapSetInt(invmap, "first", 1, maAppend);
        invmap2 = vsapi->invoke(stdplugin, "Trim", invmap);
        vsapi->clearMap(invmap);
        vsapi->mapSetNode(invmap, "clipa", d->node1, maAppend);
        vsapi->mapConsumeNode(invmap, "clipb", vsapi->mapGetNode(invmap2, "clip", 0, nullptr), maAppend);
        vsapi->mapSetData(invmap, "prop", "SCPlaneStats", -1, dtUtf8, maAppend);
        vsapi->mapSetInt(invmap, "plane", 0, maAppend);
        vsapi->freeMap(invmap2);
        invmap2 = vsapi->invoke(stdplugin, "PlaneStats", invmap);
        vsapi->freeMap(invmap);
        d->node2 = vsapi->mapGetNode(invmap2, "clip", 0, nullptr);
        vsapi->freeMap(invmap2);
    } catch (const std::runtime_error &e) {
        vsapi->mapSetError(out, ("SCDetect: "_s + e.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{ d->node1, rpStrictSpatial }, { d->node2, rpGeneral }};
    vsapi->createVideoFilter(out, "SCDetect", vi, scDetectGetFrame, filterFree<SCDetectData>, fmParallel, deps, 2, d.release(), core);
}

///////////////////////////////////////
// AverageFrames

namespace {
typedef struct {
    std::vector<int> weights;
    std::vector<float> fweights;
    VSVideoInfo vi;
    unsigned scale;
    float fscale;
    bool useSceneChange;
    bool process[3];
} AverageFrameDataExtra;

typedef VariableNodeData<AverageFrameDataExtra> AverageFrameData;
} // namespace

template <typename T>
static void averageFramesI(const AverageFrameData *d, const VSFrame * const *srcs, VSFrame *dst, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane) / sizeof(T);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);

    const T *srcpp[32];
    const size_t numSrcs = d->weights.size();

    std::transform(srcs, srcs + numSrcs, srcpp, [=](const VSFrame *f) {
        return reinterpret_cast<const T *>(vsapi->getReadPtr(f, plane));
    });

    T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));

    unsigned maxVal = (1U << d->vi.format.bitsPerSample) - 1;
    unsigned bias = 0;

    if ((plane == 1 || plane == 2) && d->vi.format.colorFamily == cfYUV)
        bias = 1U << (d->vi.format.bitsPerSample - 1);

    int scale = d->scale;
    int round = scale / 2;

    for (int h = 0; h < height; ++h) {
        for (int w = 0; w < width; ++w) {
            int acc = 0;

            for (size_t i = 0; i < numSrcs; ++i) {
                T val = srcpp[i][w] - bias;
                acc += static_cast<int>(val) * d->weights[i];
            }

            acc = (acc + round) / scale;
            acc = std::min(std::max(acc, 0), static_cast<int>(maxVal));
            dstp[w] = static_cast<T>(acc);
        }

        std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const T *ptr) { return ptr + stride; });
        dstp += stride;
    }
}

static void averageFramesF(const AverageFrameData *d, const VSFrame * const *srcs, VSFrame *dst, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane) / sizeof(float);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);

    const float *srcpp[32];
    const size_t numSrcs = d->weights.size();

    std::transform(srcs, srcs + numSrcs, srcpp, [=](const VSFrame *f) {
        return reinterpret_cast<const float *>(vsapi->getReadPtr(f, plane));
    });

    float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));
    float scale = 1.0f / d->fscale;

    for (int h = 0; h < height; ++h) {
        for (int w = 0; w < width; ++w) {
            float acc = 0;
            for (size_t i = 0; i < numSrcs; ++i)
                acc += srcpp[i][w] * d->fweights[i];
            dstp[w] = acc * scale;
        }

        std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const float *ptr) { return ptr + stride; });
        dstp += stride;
    }
}

#ifdef VS_TARGET_CPU_X86
static void averageFramesByteSSE2(const AverageFrameData *d, const VSFrame * const *srcs, VSFrame *dst, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);

    const uint8_t *srcpp[32];
    const size_t numSrcs = d->weights.size();

    std::transform(srcs, srcs + numSrcs, srcpp, [=](const VSFrame *f) { return vsapi->getReadPtr(f, plane); });
    if (numSrcs % 2)
        srcpp[numSrcs] = srcpp[numSrcs - 1];

    uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

    __m128i weights[16];

    for (size_t i = 0; i < (numSrcs & ~1); i += 2) {
        uint16_t weight_lo = static_cast<int16_t>(d->weights[i]);
        uint16_t weight_hi = static_cast<int16_t>(d->weights[i + 1]);
        weights[i / 2] = _mm_set1_epi32((static_cast<uint32_t>(weight_hi) << 16) | weight_lo);
    }
    if (numSrcs % 2)
        weights[numSrcs / 2] = _mm_set1_epi32(static_cast<uint16_t>(d->weights[numSrcs - 1]));

    __m128 scale = _mm_set_ps1(1.0f / d->scale);

    if ((plane == 1 || plane == 2) && d->vi.format.colorFamily == cfYUV) {
        __m128i bias = _mm_set1_epi8(128);

        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; w += 16) {
                __m128i accum_lolo = _mm_setzero_si128();
                __m128i accum_lohi = _mm_setzero_si128();
                __m128i accum_hilo = _mm_setzero_si128();
                __m128i accum_hihi = _mm_setzero_si128();

                for (size_t i = 0; i < numSrcs; i += 2) {
                    __m128i coeffs = weights[i / 2];
                    __m128i v1 = _mm_sub_epi8(_mm_load_si128((const __m128i *)(srcpp[i + 0] + w)), bias);
                    __m128i v2 = _mm_sub_epi8(_mm_load_si128((const __m128i *)(srcpp[i + 1] + w)), bias);
                    __m128i v1_sign = _mm_cmplt_epi8(v1, _mm_setzero_si128());
                    __m128i v2_sign = _mm_cmplt_epi8(v2, _mm_setzero_si128());

                    __m128i v1_lo = _mm_unpacklo_epi8(v1, v1_sign);
                    __m128i v1_hi = _mm_unpackhi_epi8(v1, v1_sign);
                    __m128i v2_lo = _mm_unpacklo_epi8(v2, v2_sign);
                    __m128i v2_hi = _mm_unpackhi_epi8(v2, v2_sign);

                    accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_lo, v2_lo)));
                    accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_lo, v2_lo)));
                    accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_hi, v2_hi)));
                    accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_hi, v2_hi)));
                }

                __m128 accumf_lolo = _mm_cvtepi32_ps(accum_lolo);
                __m128 accumf_lohi = _mm_cvtepi32_ps(accum_lohi);
                __m128 accumf_hilo = _mm_cvtepi32_ps(accum_hilo);
                __m128 accumf_hihi = _mm_cvtepi32_ps(accum_hihi);
                accumf_lolo = _mm_mul_ps(accumf_lolo, scale);
                accumf_lohi = _mm_mul_ps(accumf_lohi, scale);
                accumf_hilo = _mm_mul_ps(accumf_hilo, scale);
                accumf_hihi = _mm_mul_ps(accumf_hihi, scale);

                accum_lolo = _mm_cvtps_epi32(accumf_lolo);
                accum_lohi = _mm_cvtps_epi32(accumf_lohi);
                accum_hilo = _mm_cvtps_epi32(accumf_hilo);
                accum_hihi = _mm_cvtps_epi32(accumf_hihi);

                accum_lolo = _mm_packs_epi32(accum_lolo, accum_lohi);
                accum_hilo = _mm_packs_epi32(accum_hilo, accum_hihi);
                accum_lolo = _mm_packs_epi16(accum_lolo, accum_hilo);

                accum_lolo = _mm_add_epi8(accum_lolo, bias);
                _mm_store_si128((__m128i *)(dstp + w), accum_lolo);
            }

            std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const uint8_t *ptr) { return ptr + stride; });
            dstp += stride;
        }
    } else {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; w += 16) {
                __m128i accum_lolo = _mm_setzero_si128();
                __m128i accum_lohi = _mm_setzero_si128();
                __m128i accum_hilo = _mm_setzero_si128();
                __m128i accum_hihi = _mm_setzero_si128();

                for (size_t i = 0; i < numSrcs; i += 2) {
                    __m128i coeffs = weights[i / 2];
                    __m128i v1 = _mm_load_si128((const __m128i *)(srcpp[i + 0] + w));
                    __m128i v2 = _mm_load_si128((const __m128i *)(srcpp[i + 1] + w));

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
                accumf_lolo = _mm_mul_ps(accumf_lolo, scale);
                accumf_lohi = _mm_mul_ps(accumf_lohi, scale);
                accumf_hilo = _mm_mul_ps(accumf_hilo, scale);
                accumf_hihi = _mm_mul_ps(accumf_hihi, scale);

                accum_lolo = _mm_cvtps_epi32(accumf_lolo);
                accum_lohi = _mm_cvtps_epi32(accumf_lohi);
                accum_hilo = _mm_cvtps_epi32(accumf_hilo);
                accum_hihi = _mm_cvtps_epi32(accumf_hihi);

                accum_lolo = _mm_packs_epi32(accum_lolo, accum_lohi);
                accum_hilo = _mm_packs_epi32(accum_hilo, accum_hihi);
                accum_lolo = _mm_packus_epi16(accum_lolo, accum_hilo);

                _mm_store_si128((__m128i *)(dstp + w), accum_lolo);
            }

            std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const uint8_t *ptr) { return ptr + stride; });
            dstp += stride;
        }
    }
}

static void averageFramesWordSSE2(const AverageFrameData *d, const VSFrame * const *srcs, VSFrame *dst, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane) / sizeof(uint16_t);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);

    const uint16_t *srcpp[32];
    const size_t numSrcs = d->weights.size();

    std::transform(srcs, srcs + numSrcs, srcpp, [=](const VSFrame *f) {
        return reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(f, plane));
    });
    if (numSrcs % 2)
        srcpp[numSrcs] = srcpp[numSrcs - 1];

    uint16_t * VS_RESTRICT dstp = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(dst, plane));

    __m128i weights[16];
    __m128 scale = _mm_set_ps1(1.0f / d->scale);

    for (size_t i = 0; i < (numSrcs & ~1); i += 2) {
        uint16_t weight_lo = static_cast<int16_t>(d->weights[i]);
        uint16_t weight_hi = static_cast<int16_t>(d->weights[i + 1]);
        weights[i / 2] = _mm_set1_epi32((static_cast<uint32_t>(weight_hi) << 16) | weight_lo);
    }
    if (numSrcs % 2)
        weights[numSrcs / 2] = _mm_set1_epi32(static_cast<uint16_t>(d->weights[numSrcs - 1]));

    if ((plane == 1 || plane == 2) && d->vi.format.colorFamily == cfYUV) {
        __m128i bias = _mm_set1_epi16(1U << (d->vi.format.bitsPerSample - 1));
        __m128i maxVal = _mm_sub_epi16(_mm_set1_epi16((1U << d->vi.format.bitsPerSample) - 1), bias);
        __m128i minVal = _mm_sub_epi16(_mm_setzero_si128(), bias);

        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; w += 8) {
                __m128i accum_lo = _mm_setzero_si128();
                __m128i accum_hi = _mm_setzero_si128();

                for (size_t i = 0; i < numSrcs; i += 2) {
                    __m128i coeffs = weights[i / 2];
                    __m128i v1 = _mm_sub_epi16(_mm_load_si128((const __m128i *)(srcpp[i + 0] + w)), bias);
                    __m128i v2 = _mm_sub_epi16(_mm_load_si128((const __m128i *)(srcpp[i + 1] + w)), bias);

                    accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1, v2)));
                    accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1, v2)));
                }

                __m128 accumf_lo = _mm_cvtepi32_ps(accum_lo);
                __m128 accumf_hi = _mm_cvtepi32_ps(accum_hi);

                accumf_lo = _mm_mul_ps(accumf_lo, scale);
                accumf_hi = _mm_mul_ps(accumf_hi, scale);

                accum_lo = _mm_cvtps_epi32(accumf_lo);
                accum_hi = _mm_cvtps_epi32(accumf_hi);

                accum_lo = _mm_packs_epi32(accum_lo, accum_hi);
                accum_lo = _mm_max_epi16(accum_lo, minVal);
                accum_lo = _mm_min_epi16(accum_lo, maxVal);
                accum_lo = _mm_add_epi16(accum_lo, bias);
                _mm_store_si128((__m128i *)(dstp + w), accum_lo);
            }

            std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const uint16_t *ptr) { return ptr + stride; });
            dstp += stride;
        }
    } else {
        __m128i accumbias = _mm_setzero_si128();
        __m128i maxVal = _mm_add_epi16(_mm_set1_epi16((1U << d->vi.format.bitsPerSample) - 1), _mm_set1_epi16(INT16_MIN));

        for (size_t i = 0; i < (numSrcs + 1) / 2; ++i) {
            accumbias = _mm_add_epi32(accumbias, _mm_madd_epi16(_mm_set1_epi16(INT16_MIN), weights[i]));
        }

        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; w += 8) {
                __m128i accum_lo = _mm_setzero_si128();
                __m128i accum_hi = _mm_setzero_si128();

                for (size_t i = 0; i < numSrcs; i += 2) {
                    __m128i coeffs = weights[i / 2];
                    __m128i v1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcpp[i + 0] + w)), _mm_set1_epi16(INT16_MIN));
                    __m128i v2 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcpp[i + 1] + w)), _mm_set1_epi16(INT16_MIN));

                    accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1, v2)));
                    accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1, v2)));
                }
                accum_lo = _mm_sub_epi32(accum_lo, accumbias);
                accum_hi = _mm_sub_epi32(accum_hi, accumbias);

                __m128 accumf_lo = _mm_cvtepi32_ps(accum_lo);
                __m128 accumf_hi = _mm_cvtepi32_ps(accum_hi);

                accumf_lo = _mm_mul_ps(accumf_lo, scale);
                accumf_hi = _mm_mul_ps(accumf_hi, scale);

                accum_lo = _mm_cvtps_epi32(accumf_lo);
                accum_hi = _mm_cvtps_epi32(accumf_hi);

                accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(INT16_MIN));
                accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(INT16_MIN));
                accum_lo = _mm_packs_epi32(accum_lo, accum_hi);

                accum_lo = _mm_min_epi16(accum_lo, maxVal);
                accum_lo = _mm_sub_epi16(accum_lo, _mm_set1_epi16(INT16_MIN));
                _mm_store_si128((__m128i *)(dstp + w), accum_lo);
            }

            std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const uint16_t *ptr) { return ptr + stride; });
            dstp += stride;
        }
    }
}

static void averageFramesFloatSSE2(const AverageFrameData *d, const VSFrame * const *srcs, VSFrame *dst, int plane, const VSAPI *vsapi) {
    ptrdiff_t stride = vsapi->getStride(dst, plane) / sizeof(float);
    int width = vsapi->getFrameWidth(dst, plane);
    int height = vsapi->getFrameHeight(dst, plane);

    const float *srcpp[32];
    const size_t numSrcs = d->weights.size();

    std::transform(srcs, srcs + numSrcs, srcpp, [=](const VSFrame *f) {
        return reinterpret_cast<const float *>(vsapi->getReadPtr(f, plane));
    });

    float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));

    __m128 weights[32];
    __m128 scale = _mm_set_ps1(1.0f / d->fscale);

    for (size_t i = 0; i < numSrcs; ++i)
        weights[i] = _mm_set_ps1(d->fweights[i]);

    for (int h = 0; h < height; ++h) {
        for (int w = 0; w < width; w += 4) {
            __m128 acc = _mm_setzero_ps();
            for (size_t i = 0; i < numSrcs; ++i) {
                __m128 x = _mm_load_ps(srcpp[i] + w);
                x = _mm_mul_ps(weights[i], x);
                acc = _mm_add_ps(acc, x);
            }
            acc = _mm_mul_ps(acc, scale);
            _mm_store_ps(dstp + w, acc);
        }

        std::transform(srcpp, srcpp + numSrcs, srcpp, [=](const float *ptr) { return ptr + stride; });
        dstp += stride;
    }
}
#endif

static const VSFrame *VS_CC averageFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AverageFrameData *d = static_cast<AverageFrameData *>(instanceData);
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
        std::vector<const VSFrame *> frames(d->weights.size());

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

        const VSFrame *center = (singleClipMode ? frames[frames.size() / 2] : frames[0]);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(center);

        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = {
            d->process[0] ? nullptr : center,
            d->process[1] ? nullptr : center,
            d->process[2] ? nullptr : center
        };

        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(center, 0), vsapi->getFrameHeight(center, 0), fr, pl, center, core);

        std::vector<int> weights(d->weights);
        std::vector<float> fweights(d->fweights);

        if (d->useSceneChange) {
            int fromFrame = 0;
            int toFrame = static_cast<int>(weights.size());

            for (int i = static_cast<int>(weights.size()) / 2; i > 0; i--) {
                const VSMap *props = vsapi->getFramePropertiesRO(frames[i]);
                int err;
                if (vsapi->mapGetInt(props, "_SceneChangePrev", 0, &err)) {
                    fromFrame = i;
                    break;
                }
            }

            for (int i = static_cast<int>(weights.size()) / 2; i < static_cast<int>(weights.size()) - 1; i++) {
                const VSMap *props = vsapi->getFramePropertiesRO(frames[i]);
                int err;
                if (vsapi->mapGetInt(props, "_SceneChangeNext", 0, &err)) {
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
                    averageFramesByteSSE2(d, frames.data(), dst, plane, vsapi);
                else if (fi->bytesPerSample == 2)
                    averageFramesWordSSE2(d, frames.data(), dst, plane, vsapi);
                else
                    averageFramesFloatSSE2(d, frames.data(), dst, plane, vsapi);
#else
                if (fi->bytesPerSample == 1)
                    averageFramesI<uint8_t>(d, frames.data(), dst, plane, vsapi);
                else if (fi->bytesPerSample == 2)
                    averageFramesI<uint16_t>(d, frames.data(), dst, plane, vsapi);
                else
                    averageFramesF(d, frames.data(), dst, plane, vsapi);
#endif
            }
        }

        for (auto iter : frames)
            vsapi->freeFrame(iter);

        return dst;
    }

    return nullptr;
}

static void VS_CC averageFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AverageFrameData> d(new AverageFrameData(vsapi));
    int numNodes = vsapi->mapNumElements(in, "clips");
    int numWeights = vsapi->mapNumElements(in, "weights");
    int err;

    try {
        if (numNodes == 1) {
            if ((numWeights % 2) != 1)
                throw std::runtime_error("Number of weights must be odd when only one clip supplied");
        } else if (numWeights != numNodes) {
            throw std::runtime_error("Number of weights must match number of clips supplied");
        }

        if (numWeights > 31 || numNodes > 31) {
            throw std::runtime_error("Must use between 1 and 31 weights and input clips");
        }

        d->useSceneChange = !!vsapi->mapGetInt(in, "scenechange", 0, &err);
        if (numNodes != 1 && d->useSceneChange)
            throw std::runtime_error("Scenechange can only be used in single clip mode");

        for (int i = 0; i < numNodes; i++)
            d->nodes.push_back(vsapi->mapGetNode(in, "clips", i, 0));

        d->vi = *vsapi->getVideoInfo(d->nodes[0]);
        if (!is8to16orFloatFormat(d->vi.format))
            throw std::runtime_error("clips must be constant format and of integer 8-16 bit type or 32 bit float");

        for (auto iter : d->nodes) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(iter);
            d->vi.numFrames = std::max(d->vi.numFrames, vi->numFrames);
            if (!isSameVideoInfo(&d->vi, vi))
                throw std::runtime_error("All clips must have the same format");
        }

        for (int i = 0; i < numWeights; i++) {
            d->fweights.push_back(static_cast<float>(vsapi->mapGetFloat(in, "weights", i, 0)));
            d->weights.push_back(std::lround(vsapi->mapGetFloat(in, "weights", i, 0)));
            if (d->vi.format.sampleType == stInteger && std::abs(d->weights[i]) > 1023)
                throw std::runtime_error("coefficients may only be between -1023 and 1023");
        }

        float scale = static_cast<float>(vsapi->mapGetFloat(in, "scale", 0, &err));
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
            if (d->vi.format.sampleType == stInteger) {
                d->scale = floatToIntS(scale);
                if (d->scale < 1)
                    throw std::runtime_error("scale must be a positive number");
            } else {
                d->fscale = scale;
                if (d->fscale < FLT_EPSILON)
                    throw std::runtime_error("scale must be a positive number");
            }
        }

        getPlanesArg(in, d->process, vsapi);

    } catch (const std::runtime_error &e) {
        for (auto iter : d->nodes)
            vsapi->freeNode(iter);
        vsapi->mapSetError(out, ("AverageFrames: "_s + e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    if (numNodes == 1) {
        deps.push_back({d->nodes[0], rpGeneral});
    } else {
        for (int i = 0; i < numNodes; i++)
            deps.push_back({d->nodes[i], (vsapi->getVideoInfo(d->nodes[i])->numFrames >= d->vi.numFrames) ? rpStrictSpatial : rpGeneral});
    }
    vsapi->createVideoFilter(out, "AverageFrames", &d->vi, averageFramesGetFrame, filterFree<AverageFrameData>, fmParallel, deps.data(), numNodes, d.get(), core);
    d.release();
}

///////////////////////////////////////
// Hysteresis

struct HysteresisExtraData {
    bool process[3];
    uint16_t peak;
    size_t labelSize;
};

typedef DualNodeData<HysteresisExtraData> HysteresisData;

template<typename T>
static void process_frame_hysteresis(const VSFrame * src1, const VSFrame * src2, VSFrame * dst, const VSVideoFormat *fi, const HysteresisData * d, const VSAPI * vsapi) VS_NOEXCEPT {
    uint8_t * VS_RESTRICT label = nullptr;

    for (int plane = 0; plane < fi->numPlanes; plane++) {
        if (d->process[plane]) {
            if (!label)
                label = new uint8_t[d->labelSize]();
            const int width = vsapi->getFrameWidth(src1, plane);
            const int height = vsapi->getFrameHeight(src1, plane);
            const ptrdiff_t stride = vsapi->getStride(src1, plane) / sizeof(T);
            const T * srcp1 = reinterpret_cast<const T *>(vsapi->getReadPtr(src1, plane));
            const T * srcp2 = reinterpret_cast<const T *>(vsapi->getReadPtr(src2, plane));
            T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));

            T lower, upper;
            if (std::is_integral<T>::value) {
                lower = 0;
                upper = d->peak;
            } else {
                lower = 0.f;
                upper = 1.f;
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

static const VSFrame *VS_CC hysteresisGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    HysteresisData * d = static_cast<HysteresisData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame * src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame * src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const VSFrame * fr[]{ d->process[0] ? nullptr : src1, d->process[1] ? nullptr : src1, d->process[2] ? nullptr : src1 };
        const int pl[]{ 0, 1, 2 };
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src1);
        VSFrame * dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src1, 0), vsapi->getFrameHeight(src1, 0), fr, pl, src1, core);

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

static void VS_CC hysteresisCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<HysteresisData> d(new HysteresisData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, nullptr);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node1);

    try {
        
        if (!isConstantVideoFormat(vi) || !is8to16orFloatFormat(vi->format))
            throw std::runtime_error("only constant format 8-16 bits integer and 32 bits float input supported");

        if (!isSameVideoInfo(vi, vsapi->getVideoInfo(d->node2)))
            throw std::runtime_error("both clips must have the same dimensions and the same format");

        getPlanesArg(in, d->process, vsapi);

        if (vi->format.sampleType == stInteger) {
            d->peak = (1 << vi->format.bitsPerSample) - 1;
        }

        d->labelSize = vi->width * vi->height;

    } catch (const std::runtime_error &e) {
        vsapi->mapSetError(out, ("Hysteresis: "_s + e.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpGeneral}};
    vsapi->createVideoFilter(out, "Hysteresis", vi, hysteresisGetFrame, filterFree<HysteresisData>, fmParallel, deps, 2, d.release(), core);
}

///////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.misc", "misc", "Miscellaneous filters", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("SCDetect", "clip:vnode;threshold:float:opt;", "clip:vnode;", scDetectCreate, 0, plugin);
    vspapi->registerFunction("AverageFrames", "clips:vnode[];weights:float[];scale:float:opt;scenechange:int:opt;planes:int[]:opt;", "clip:vnode;", averageFramesCreate, 0, plugin);
    vspapi->registerFunction("Hysteresis", "clipa:vnode;clipb:vnode;planes:int[]:opt;", "clip:vnode;", hysteresisCreate, nullptr, plugin);
}
