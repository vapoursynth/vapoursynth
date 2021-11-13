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
#include "filtershared.h"
#include "version.h"
#include "kernel/average.h"
#include "kernel/cpulevel.h"

#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }

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

            decltype(&vs_average_plane_byte_luma_c) func = nullptr;
            bool chroma = (plane == 1 || plane == 2) && fi->colorFamily == cfYUV;

#ifdef VS_TARGET_CPU_X86
            if (vs_get_cpulevel(core) >= VS_CPU_LEVEL_SSE2) {
                if (fi->bytesPerSample == 1)
                    func = chroma ? vs_average_plane_byte_chroma_sse2 : vs_average_plane_byte_luma_sse2;
                else if (fi->bytesPerSample == 2)
                    func = chroma ? vs_average_plane_word_chroma_sse2 : vs_average_plane_word_luma_sse2;
                else
                    func = vs_average_plane_float_sse2;
            }
#endif
            if (!func) {
                if (fi->bytesPerSample == 1)
                    func = chroma ? vs_average_plane_byte_chroma_c : vs_average_plane_byte_luma_c;
                else if (fi->bytesPerSample == 2)
                    func = chroma ? vs_average_plane_word_chroma_c : vs_average_plane_word_luma_c;
                else
                    func = vs_average_plane_float_c;
            }

            const void *src_ptrs[32];
            const void *weights_ptr = (fi->bytesPerSample == 1 || fi->bytesPerSample == 2) ? (const void *)weights.data() : fweights.data();
            const void *scale_ptr = (fi->bytesPerSample == 1 || fi->bytesPerSample == 2) ? (const void *)&d->scale : &d->fscale;

            for (unsigned n = 0; n < frames.size(); ++n) {
                src_ptrs[n] = vsapi->getReadPtr(frames[n], plane);
            }

            func(weights_ptr, src_ptrs, static_cast<unsigned>(frames.size()), vsapi->getWritePtr(dst, plane), scale_ptr, fi->bitsPerSample,
                vsapi->getFrameWidth(dst, plane), vsapi->getFrameHeight(dst, plane), vsapi->getStride(dst, plane));
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

} // namespace

///////////////////////////////////////
// Init

void averageFramesInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("AverageFrames", "clips:vnode[];weights:float[];scale:float:opt;scenechange:int:opt;planes:int[]:opt;", "clip:vnode;", averageFramesCreate, 0, plugin);
}
