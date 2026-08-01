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
#include <cstdint>
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
#include "filtershared.h"
#include "float16_helper.h"
#include "gpufilter.h"
#include "version.h"
#include "VSVulkan4.h"

namespace {

// ---- plane averaging kernels (AverageFrames) -------------------------------
// Pure scalar C, written to auto-vectorise: the source count is a compile-time
// template parameter (AverageFrames caps it at 31) so the reduction unrolls, and
// __restrict on the row pointers lets the store be proven not to alias the
// sources. Build this file with -fno-math-errno so lrintf lowers to a vector
// convert instead of a libcall.

// Round to nearest, ties to even (matches SSE cvtps_epi32).
static inline int32_t round_nearest_i32(float x)
{
#if defined(__aarch64__)
    // lrintf returns long, so the vectorizer widens through f64 (frintx + fcvtl +
    // fcvtzs.2d + repack per half). roundevenf keeps 32-bit lanes (frintn + fcvtzs)
    // and gives the same ties-to-even result independent of the rounding mode.
    return static_cast<int32_t>(__builtin_roundevenf(x));
#else
    return static_cast<int32_t>(std::lrintf(x));
#endif
}

template <class T, unsigned N>
static void average_int_n(const int *weights, const void * const *srcs, void *dst_, int scale, int32_t maxval, int32_t bias, unsigned w, unsigned h, ptrdiff_t stride)
{
    float rscale = 1.0f / static_cast<float>(scale);

    for (unsigned i = 0; i < h; ++i) {
        T *__restrict dst = reinterpret_cast<T *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);
        const T * __restrict rows[N];
        for (unsigned k = 0; k < N; ++k)
            rows[k] = reinterpret_cast<const T *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            int32_t accum = 0;
            for (unsigned k = 0; k < N; ++k)
                accum += (static_cast<int32_t>(rows[k][j]) - bias) * weights[k];

            int32_t r = round_nearest_i32(static_cast<float>(accum) * rscale) + bias;
            dst[j] = static_cast<T>(std::min(std::max(r, 0), maxval));
        }
    }
}

template <class T>
static void average_int_runtime(const int *weights, const void * const *srcs, unsigned num_srcs, void *dst_, int scale, int32_t maxval, int32_t bias, unsigned w, unsigned h, ptrdiff_t stride)
{
    float rscale = 1.0f / static_cast<float>(scale);

    for (unsigned i = 0; i < h; ++i) {
        T *__restrict dst = reinterpret_cast<T *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            int32_t accum = 0;
            for (unsigned k = 0; k < num_srcs; ++k) {
                const T *src = reinterpret_cast<const T *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);
                accum += (static_cast<int32_t>(src[j]) - bias) * weights[k];
            }

            int32_t r = round_nearest_i32(static_cast<float>(accum) * rscale) + bias;
            dst[j] = static_cast<T>(std::min(std::max(r, 0), maxval));
        }
    }
}

template <unsigned N>
static void average_float_n(const float *weights, const void * const *srcs, void *dst_, float rscale, unsigned w, unsigned h, ptrdiff_t stride)
{
    for (unsigned i = 0; i < h; ++i) {
        float *__restrict dst = reinterpret_cast<float *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);
        const float * __restrict rows[N];
        for (unsigned k = 0; k < N; ++k)
            rows[k] = reinterpret_cast<const float *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            float accum = 0.0f;
            for (unsigned k = 0; k < N; ++k)
                accum += rows[k][j] * weights[k];
            dst[j] = accum * rscale;
        }
    }
}

static void average_float_runtime(const float *weights, const void * const *srcs, unsigned num_srcs, void *dst_, float rscale, unsigned w, unsigned h, ptrdiff_t stride)
{
    for (unsigned i = 0; i < h; ++i) {
        float *__restrict dst = reinterpret_cast<float *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            float accum = 0.0f;
            for (unsigned k = 0; k < num_srcs; ++k) {
                const float *src = reinterpret_cast<const float *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);
                accum += src[j] * weights[k];
            }
            dst[j] = accum * rscale;
        }
    }
}

template <unsigned N>
static void average_half_n(const float *weights, const void * const *srcs, void *dst_, float rscale, unsigned w, unsigned h, ptrdiff_t stride)
{
    for (unsigned i = 0; i < h; ++i) {
        uint16_t *__restrict dst = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);
        const uint16_t * __restrict rows[N];
        for (unsigned k = 0; k < N; ++k)
            rows[k] = reinterpret_cast<const uint16_t *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            float accum = 0.0f;
            for (unsigned k = 0; k < N; ++k)
                accum += halfToFloat(rows[k][j]) * weights[k];
            dst[j] = floatToHalf(accum * rscale);
        }
    }
}

static void average_half_runtime(const float *weights, const void * const *srcs, unsigned num_srcs, void *dst_, float rscale, unsigned w, unsigned h, ptrdiff_t stride)
{
    for (unsigned i = 0; i < h; ++i) {
        uint16_t *__restrict dst = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(dst_) + static_cast<ptrdiff_t>(i) * stride);

        for (unsigned j = 0; j < w; ++j) {
            float accum = 0.0f;
            for (unsigned k = 0; k < num_srcs; ++k) {
                const uint16_t *src = reinterpret_cast<const uint16_t *>(static_cast<const uint8_t *>(srcs[k]) + static_cast<ptrdiff_t>(i) * stride);
                accum += halfToFloat(src[j]) * weights[k];
            }
            dst[j] = floatToHalf(accum * rscale);
        }
    }
}

template <class T>
static void average_int(const int *weights, const void * const *srcs, unsigned num_srcs, void *dst, int scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride, bool chroma)
{
    int32_t maxval = (1L << depth) - 1;
    int32_t bias = chroma ? (1L << (depth - 1)) : 0;

#define AVG_CASE(k) case k: average_int_n<T, k>(weights, srcs, dst, scale, maxval, bias, w, h, stride); return;
    switch (num_srcs) {
    AVG_CASE(1)  AVG_CASE(2)  AVG_CASE(3)  AVG_CASE(4)  AVG_CASE(5)  AVG_CASE(6)  AVG_CASE(7)  AVG_CASE(8)
    AVG_CASE(9)  AVG_CASE(10) AVG_CASE(11) AVG_CASE(12) AVG_CASE(13) AVG_CASE(14) AVG_CASE(15) AVG_CASE(16)
    AVG_CASE(17) AVG_CASE(18) AVG_CASE(19) AVG_CASE(20) AVG_CASE(21) AVG_CASE(22) AVG_CASE(23) AVG_CASE(24)
    AVG_CASE(25) AVG_CASE(26) AVG_CASE(27) AVG_CASE(28) AVG_CASE(29) AVG_CASE(30) AVG_CASE(31)
    default: average_int_runtime<T>(weights, srcs, num_srcs, dst, scale, maxval, bias, w, h, stride); return;
    }
#undef AVG_CASE
}

static void average_float(const float *weights, const void * const *srcs, unsigned num_srcs, void *dst, float scale, unsigned w, unsigned h, ptrdiff_t stride)
{
    float rscale = 1.0f / scale;

#define AVG_CASE(k) case k: average_float_n<k>(weights, srcs, dst, rscale, w, h, stride); return;
    switch (num_srcs) {
    AVG_CASE(1)  AVG_CASE(2)  AVG_CASE(3)  AVG_CASE(4)  AVG_CASE(5)  AVG_CASE(6)  AVG_CASE(7)  AVG_CASE(8)
    AVG_CASE(9)  AVG_CASE(10) AVG_CASE(11) AVG_CASE(12) AVG_CASE(13) AVG_CASE(14) AVG_CASE(15) AVG_CASE(16)
    AVG_CASE(17) AVG_CASE(18) AVG_CASE(19) AVG_CASE(20) AVG_CASE(21) AVG_CASE(22) AVG_CASE(23) AVG_CASE(24)
    AVG_CASE(25) AVG_CASE(26) AVG_CASE(27) AVG_CASE(28) AVG_CASE(29) AVG_CASE(30) AVG_CASE(31)
    default: average_float_runtime(weights, srcs, num_srcs, dst, rscale, w, h, stride); return;
    }
#undef AVG_CASE
}

static void average_half(const float *weights, const void * const *srcs, unsigned num_srcs, void *dst, float scale, unsigned w, unsigned h, ptrdiff_t stride)
{
    float rscale = 1.0f / scale;

#define AVG_CASE(k) case k: average_half_n<k>(weights, srcs, dst, rscale, w, h, stride); return;
    switch (num_srcs) {
    AVG_CASE(1)  AVG_CASE(2)  AVG_CASE(3)  AVG_CASE(4)  AVG_CASE(5)  AVG_CASE(6)  AVG_CASE(7)  AVG_CASE(8)
    AVG_CASE(9)  AVG_CASE(10) AVG_CASE(11) AVG_CASE(12) AVG_CASE(13) AVG_CASE(14) AVG_CASE(15) AVG_CASE(16)
    AVG_CASE(17) AVG_CASE(18) AVG_CASE(19) AVG_CASE(20) AVG_CASE(21) AVG_CASE(22) AVG_CASE(23) AVG_CASE(24)
    AVG_CASE(25) AVG_CASE(26) AVG_CASE(27) AVG_CASE(28) AVG_CASE(29) AVG_CASE(30) AVG_CASE(31)
    default: average_half_runtime(weights, srcs, num_srcs, dst, rscale, w, h, stride); return;
    }
#undef AVG_CASE
}

using namespace std::string_literals;
using namespace vsh;

///////////////////////////////////////
// AverageFrames

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

/* Up to 31 inputs and a weight set that scenechange can rewrite per frame, so this declares
   a FilterDesc rather than going through SimpleFilter, whose three input cap and fixed
   parameter block neither fits. One pass, one binding per input plus the output. */
namespace {

constexpr int avgMaxWeights = 31;

struct AveragePush {
    uint32_t width, height;
    uint32_t srcStride, dstStride; /* every input shares clip 0's plane geometry */
    float rscale;
    int32_t maxval, bias, isFloat;
    float weights[avgMaxWeights];
};

const char averageGlsl[] =
    "#extension GL_EXT_shader_8bit_storage : require\n"
    "#extension GL_EXT_shader_16bit_storage : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n"
    "#ifdef FLOAT_SAMPLES\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n"
    "#endif\n"
    "\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(std430, set = 0, binding = 0) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
    "layout(push_constant) uniform PC {\n"
    "    uint width, height, srcStride, dstStride;\n"
    "    float rscale;\n"
    "    int maxval, bias, isFloat;\n"
    "    float weights[31];\n"
    "} pc;\n"
    "SRC_DECLS\n"
    "\n"
    "void main() {\n"
    "    uint x = gl_GlobalInvocationID.x;\n"
    "    uint y = gl_GlobalInvocationID.y;\n"
    "    if (x >= pc.width || y >= pc.height) return;\n"
    "    uint idx = y * pc.srcStride + x;\n"
    "#ifdef FLOAT_SAMPLES\n"
    /* Float accumulates the samples themselves; the weights are already float. */
    "    float accum = 0.0;\n"
    "SRC_ACCUM_F\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T(accum * pc.rscale);\n"
    "#else\n"
    /* Integer accumulates exactly in int32 against the bias, exactly as the scalar path
       does, and only crosses into float for the scale and the tie to even rounding. */
    "    int accum = 0;\n"
    "SRC_ACCUM_I\n"
    "    int r = int(roundEven(float(accum) * pc.rscale)) + pc.bias;\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T(clamp(r, 0, pc.maxval));\n"
    "#endif\n"
    "}\n";

/* The input count is fixed at create, so the bindings and the accumulate loop are emitted
   rather than looped over -- it keeps the weights as literals in the addressing and lets the
   compiler unroll something it can see the extent of. */
std::string averageSource(const VSVideoFormat &fmt, int numInputs) {
    std::string decls, accumF, accumI;
    for (int i = 0; i < numInputs; i++) {
        const std::string k = std::to_string(i);
        decls += "layout(std430, set = 0, binding = " + std::to_string(i + 1) +
                 ") readonly buffer Src" + k + " { SAMPLE_T s" + k + "[]; };\n";
        accumF += "    accum += float(s" + k + "[idx]) * pc.weights[" + k + "];\n";
        accumI += "    accum += (int(s" + k + "[idx]) - pc.bias) * int(pc.weights[" + k + "]);\n";
    }

    std::string preamble = "#version 460\n";
    if (fmt.sampleType == stInteger)
        preamble += fmt.bytesPerSample == 1 ? "#define SAMPLE_T uint8_t\n" : "#define SAMPLE_T uint16_t\n";
    else if (fmt.bytesPerSample == 4)
        preamble += "#define SAMPLE_T float\n#define FLOAT_SAMPLES\n";
    else
        preamble += "#define SAMPLE_T float16_t\n#define FLOAT_SAMPLES\n";

    std::string s = preamble + averageGlsl;
    auto replace = [&s](const char *token, const std::string &with) {
        const size_t at = s.find(token);
        if (at != std::string::npos)
            s.replace(at, strlen(token), with);
    };
    replace("SRC_DECLS\n", decls);
    replace("SRC_ACCUM_F\n", accumF);
    replace("SRC_ACCUM_I\n", accumI);
    return s;
}

} // namespace

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
            if (!d->process[plane])
                continue;

            bool chroma = (plane == 1 || plane == 2) && fi->colorFamily == cfYUV;

            const void *src_ptrs[32];
            for (unsigned n = 0; n < frames.size(); ++n)
                src_ptrs[n] = vsapi->getReadPtr(frames[n], plane);

            unsigned num = static_cast<unsigned>(frames.size());
            void *dstp = vsapi->getWritePtr(dst, plane);
            unsigned pw = vsapi->getFrameWidth(dst, plane);
            unsigned ph = vsapi->getFrameHeight(dst, plane);
            ptrdiff_t pstride = vsapi->getStride(dst, plane);

            if (fi->bytesPerSample == 1)
                average_int<uint8_t>(weights.data(), src_ptrs, num, dstp, static_cast<int>(d->scale), fi->bitsPerSample, pw, ph, pstride, chroma);
            else if (fi->sampleType == stInteger && fi->bytesPerSample == 2)
                average_int<uint16_t>(weights.data(), src_ptrs, num, dstp, static_cast<int>(d->scale), fi->bitsPerSample, pw, ph, pstride, chroma);
            else if (fi->bytesPerSample == 2)
                average_half(fweights.data(), src_ptrs, num, dstp, d->fscale, pw, ph, pstride);
            else
                average_float(fweights.data(), src_ptrs, num, dstp, d->fscale, pw, ph, pstride);
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
            throw std::runtime_error(invalidVideoFormatMessage(d->vi.format, vsapi, nullptr));
        if (!d->vi.width || !d->vi.height)
            throw std::runtime_error("clips must have constant dimensions");

        for (size_t i = 1; i < d->nodes.size(); i++) {
            const VSVideoInfo *vi = vsapi->getVideoInfo(d->nodes[i]);
            d->vi.numFrames = std::max(d->vi.numFrames, vi->numFrames);
            if (!isSameVideoInfo(&d->vi, vi))
                throw std::runtime_error(("All clips must have the same format, passed " + videoInfoToString(&d->vi, vsapi) + " and " + videoInfoToString(vi, vsapi) + " in clip #" + std::to_string(i)).c_str());
        }

        for (int i = 0; i < numWeights; i++) {
            double w = vsapi->mapGetFloat(in, "weights", i, 0);
            if (!std::isfinite(w))
                throw std::runtime_error("weights must be finite");
            d->fweights.push_back(static_cast<float>(w));
            if (d->vi.format.sampleType == stInteger) {
                double r = std::round(w);
                if (r < -1023.0 || r > 1023.0)
                    throw std::runtime_error("coefficients may only be between -1023 and 1023");
                d->weights.push_back(static_cast<int>(r));
            } else {
                // Required because weights.size() is referenced to get the radius
                d->weights.push_back(0);
            }
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
            if (!std::isfinite(scale))
                throw std::runtime_error("scale must be a positive number");
            if (d->vi.format.sampleType == stInteger) {
                int scalei = floatToIntS(scale);
                if (scalei < 1)
                    throw std::runtime_error("scale must be a positive number");
                d->scale = scalei;
            } else {
                d->fscale = scale;
                if (d->fscale < FLT_EPSILON)
                    throw std::runtime_error("scale must be a positive number");
            }
        }

        getPlanesArg(in, d->process, vsapi);

    } catch (const std::runtime_error &e) {
        vsapi->mapSetError(out, ("AverageFrames: "s + e.what()).c_str());
        return;
    }

    std::vector<VSFilterDependency> deps;
    if (numNodes == 1) {
        deps.push_back({d->nodes[0], rpGeneral});
    } else {
        for (int i = 0; i < numNodes; i++)
            deps.push_back({d->nodes[i], (vsapi->getVideoInfo(d->nodes[i])->numFrames >= d->vi.numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    }
    bool allGPU = true, anyGPU = false;
    for (int i = 0; i < numNodes; i++) {
        const bool gpu = vsapi->getNodeResidency(d->nodes[i]) == nrGPU;
        allGPU = allGPU && gpu;
        anyGPU = anyGPU || gpu;
    }
    if (anyGPU && !allGPU)
        RETERROR("AverageFrames: clips are mismatched in residency; all clips must be CPU or all GPU, insert GPUUpload or GPUDownload to make them match");

    if (allGPU) {
        const int taps = static_cast<int>(d->weights.size());
        const bool single = numNodes == 1;

        vsgpu::FilterDesc desc;
        desc.vi = d->vi;
        for (int p = 0; p < 3; p++)
            desc.process[p] = d->process[p];
        /* Single clip mode reads a window centred on n from the one clip; otherwise each
           clip contributes its own frame n. Either way the driver clamps at the ends, which
           is the same saturation the scalar path applies with its max(0, fn). */
        if (single) {
            desc.nodes.push_back(d->nodes[0]);
            desc.mapFrame = [taps](int n, int, int frameOffset) { return n + frameOffset - taps / 2; };
            /* Properties and any unprocessed plane come from the centre tap, which is the
               frame this output actually corresponds to. */
            desc.refOffset = taps / 2;
        } else {
            desc.nodes = d->nodes;
        }

        vsgpu::Program program;
        program.glsl = averageSource(d->vi.format, taps);
        program.storageBufferCount = taps + 1;
        program.pushConstantBytes = sizeof(AveragePush);
        desc.programs.push_back(std::move(program));

        vsgpu::Pass pass;
        pass.bindings.push_back(vsgpu::Operand::output());
        for (int i = 0; i < taps; i++)
            pass.bindings.push_back(single ? vsgpu::Operand::source(0, i) : vsgpu::Operand::source(i));
        desc.passes.push_back(std::move(pass));

        /* Scenechange folds the weights of frames across the cut into the centre tap, so the
           effective weights are only known once the frames are in hand. */
        const std::vector<int> baseWeights = d->weights;
        const std::vector<float> baseFWeights = d->fweights;
        const bool useSceneChange = d->useSceneChange;
        const bool isFloat = d->vi.format.sampleType == stFloat;
        desc.frameParamCount = avgMaxWeights;
        desc.prepareFrame = [baseWeights, baseFWeights, useSceneChange, isFloat, taps](
                int, const VSFrame *const *sources, int, const VSAPI *vsapi,
                uint32_t *params, std::string &) {
            std::vector<int> weights(baseWeights);
            std::vector<float> fweights(baseFWeights);
            if (useSceneChange) {
                int fromFrame = 0, toFrame = taps;
                for (int i = taps / 2; i > 0; i--) {
                    int err;
                    if (vsapi->mapGetInt(vsapi->getFramePropertiesRO(sources[i]), "_SceneChangePrev", 0, &err)) {
                        fromFrame = i;
                        break;
                    }
                }
                for (int i = taps / 2; i < taps - 1; i++) {
                    int err;
                    if (vsapi->mapGetInt(vsapi->getFramePropertiesRO(sources[i]), "_SceneChangeNext", 0, &err)) {
                        toFrame = i;
                        break;
                    }
                }
                if (isFloat) {
                    float acc = 0;
                    for (int i = toFrame + 1; i < taps; i++) { acc += fweights[i]; fweights[i] = 0; }
                    for (int i = 0; i < fromFrame; i++) { acc += fweights[i]; fweights[i] = 0; }
                    fweights[taps / 2] += acc;
                } else {
                    int acc = 0;
                    for (int i = toFrame + 1; i < taps; i++) { acc += weights[i]; weights[i] = 0; }
                    for (int i = 0; i < fromFrame; i++) { acc += weights[i]; weights[i] = 0; }
                    weights[taps / 2] += acc;
                }
            }
            /* Carried as float bits either way; an integer weight is small enough to survive
               the trip exactly, which keeps one parameter block for both sample types. */
            for (int i = 0; i < taps; i++) {
                const float w = isFloat ? fweights[i] : static_cast<float>(weights[i]);
                std::memcpy(&params[i], &w, sizeof(w));
            }
            return true;
        };

        const uint32_t bits = d->vi.format.bitsPerSample;
        const uint32_t colorFamily = d->vi.format.colorFamily;
        const float fscale = d->fscale;
        const unsigned scale = d->scale;
        desc.fillPush = [taps, isFloat, bits, colorFamily, fscale, scale](
                const vsgpu::PassInfo &info, void *pushData) {
            AveragePush push = {};
            push.width = info.width;
            push.height = info.height;
            /* Binding 0 is the output; the inputs follow and all share its plane geometry. */
            push.dstStride = info.strideElements[0];
            push.srcStride = info.strideElements[1];
            push.rscale = 1.0f / (isFloat ? fscale : static_cast<float>(scale));
            push.maxval = static_cast<int32_t>((1u << bits) - 1);
            const bool chroma = (info.plane == 1 || info.plane == 2) && colorFamily == cfYUV;
            push.bias = chroma ? static_cast<int32_t>(1u << (bits - 1)) : 0;
            push.isFloat = isFloat;
            for (int i = 0; i < taps && i < avgMaxWeights; i++)
                std::memcpy(&push.weights[i], &info.frameParams[i], sizeof(float));
            std::memcpy(pushData, &push, sizeof(push));
        };

        std::string error;
        VSNode *result = vsgpu::createFilter("AverageFrames", desc, deps.data(), numNodes, core, vsapi, error);
        d->nodes.clear(); /* consumed either way */
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("AverageFrames: " + error).c_str());
        return;
    }

    vsapi->createVideoFilter(out, "AverageFrames", &d->vi, averageFramesGetFrame, filterFree<AverageFrameData>, fmParallel, deps.data(), numNodes, d.get(), core);
    d.release();
}

} // namespace

///////////////////////////////////////
// Init

void averageFramesInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("AverageFrames", "clips:vnode[]:all;weights:float[];scale:float:opt;scenechange:int:opt;planes:int[]:opt;", "clip:vnode:all;", averageFramesCreate, 0, plugin);
}
