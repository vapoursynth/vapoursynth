/*
* Copyright (c) 2020 Fredrik Mellbin
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
#include <cstdio>
#include <cinttypes>
#include <memory>
#include <limits>
#include <string>
#include <algorithm>
#include <vector>
#include <set>
#include "internalfilters.h"
#include "VSHelper4.h"
#include "filtershared.h"

using namespace vsh;

//////////////////////////////////////////
// AudioTrim

typedef struct {
    VSAudioInfo ai;
    int64_t first;
} AudioTrimDataExtra;

typedef SingleNodeData<AudioTrimDataExtra> AudioTrimData;

static const VSFrame *VS_CC audioTrimGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioTrimData *d = reinterpret_cast<AudioTrimData *>(instanceData);

    int64_t startSample = n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES) + d->first;
    int startFrame = (int)(startSample / VS_AUDIO_FRAME_SAMPLES);
    int length = static_cast<int>(std::min<int64_t>(d->ai.numSamples - n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES), VS_AUDIO_FRAME_SAMPLES));

    if (startSample % VS_AUDIO_FRAME_SAMPLES == 0 && n != d->ai.numFrames - 1) { // pass through audio frames when possible
        if (activationReason == arInitial) {
            vsapi->requestFrameFilter(startFrame, d->node, frameCtx);
        } else if (activationReason == arAllFramesReady) {
            const VSFrame *src = vsapi->getFrameFilter(startFrame, d->node, frameCtx);
            if (length == vsapi->getFrameLength(src))
                return src;
            VSFrame *dst = vsapi->newAudioFrame(&d->ai.format, length, src, core);
            for (int channel = 0; channel < d->ai.format.numChannels; channel++)
                memcpy(vsapi->getWritePtr(dst, channel), vsapi->getReadPtr(src, channel), length * d->ai.format.bytesPerSample);
            vsapi->freeFrame(src);
            return dst;
        }
    } else {
        int numSrc1Samples = VS_AUDIO_FRAME_SAMPLES - (startSample % VS_AUDIO_FRAME_SAMPLES);
        if (activationReason == arInitial) {
            vsapi->requestFrameFilter(startFrame, d->node, frameCtx);
            if (numSrc1Samples < length)
                vsapi->requestFrameFilter(startFrame + 1, d->node, frameCtx);
        } else if (activationReason == arAllFramesReady) {
            const VSFrame *src1 = vsapi->getFrameFilter(startFrame, d->node, frameCtx);
            VSFrame *dst = vsapi->newAudioFrame(&d->ai.format, length, src1, core);
            for (int channel = 0; channel < d->ai.format.numChannels; channel++)
                memcpy(vsapi->getWritePtr(dst, channel), vsapi->getReadPtr(src1, channel) + (VS_AUDIO_FRAME_SAMPLES - numSrc1Samples) * d->ai.format.bytesPerSample, numSrc1Samples * d->ai.format.bytesPerSample);
            vsapi->freeFrame(src1);

            if (length > numSrc1Samples) {
                const VSFrame *src2 = vsapi->getFrameFilter(startFrame + 1, d->node, frameCtx);
                for (int channel = 0; channel < d->ai.format.numChannels; channel++)
                    memcpy(vsapi->getWritePtr(dst, channel) + numSrc1Samples * d->ai.format.bytesPerSample, vsapi->getReadPtr(src2, channel), (length - numSrc1Samples) * d->ai.format.bytesPerSample);
                vsapi->freeFrame(src2);
            }

            return dst;
        }
    }

    return nullptr;
}

static void VS_CC audioTrimCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioTrimData> d(new AudioTrimData(vsapi));

    int err;
    int64_t trimlen;

    d->first = vsapi->mapGetIntSaturated(in, "first", 0, &err);
    bool firstset = !err;
    int64_t last = vsapi->mapGetIntSaturated(in, "last", 0, &err);
    bool lastset = !err;
    int64_t length = vsapi->mapGetIntSaturated(in, "length", 0, &err);
    bool lengthset = !err;

    if (lastset && lengthset)
        RETERROR("AudioTrim: both last sample and length specified");

    if (lastset && last < d->first)
        RETERROR("AudioTrim: invalid last sample specified (last is less than first)");

    if (lengthset && length < 1)
        RETERROR("AudioTrim: invalid length specified (less than 1)");

    if (d->first < 0)
        RETERROR("AudioTrim: invalid first frame specified (less than 0)");

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    d->ai = *vsapi->getAudioInfo(d->node);

    if ((lastset && last >= d->ai.numSamples) || (lengthset && (d->first + length) > d->ai.numSamples) || (d->ai.numSamples <= d->first))
        RETERROR("AudioTrim: last sample beyond clip end");

    if (lastset) {
        trimlen = last - d->first + 1;
    } else if (lengthset) {
        trimlen = length;
    } else {
        trimlen = d->ai.numSamples - d->first;
    }

    // obvious nop() so just pass through the input clip
    if ((!firstset && !lastset && !lengthset) || (trimlen && trimlen == d->ai.numSamples)) {
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    d->ai.numSamples = trimlen;

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createAudioFilter(out, "AudioTrim", &d->ai, audioTrimGetframe, filterFree<AudioTrimData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioSplice

typedef struct {
    VSAudioInfo ai;
    std::vector<int64_t> numSamples;
    std::vector<int64_t> cumSamples;
    std::vector<int> numFrames;
} AudioSpliceDataExtra;

typedef VariableNodeData<AudioSpliceDataExtra> AudioSpliceData;

static const VSFrame *VS_CC audioSpliceGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioSpliceData *d = reinterpret_cast<AudioSpliceData *>(instanceData);

    int64_t sampleStart = n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
    int remainingSamples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai.numSamples - sampleStart));

    if (activationReason == arInitial) {
        for (size_t i = 0; i < d->cumSamples.size(); i++) {
            if (d->cumSamples[i] > sampleStart) {
                int64_t currentStartSample = sampleStart - ((i > 0) ? d->cumSamples[i - 1] : 0);
                int64_t reqStartOffset = currentStartSample % VS_AUDIO_FRAME_SAMPLES;
                int reqFrame = static_cast<int>(currentStartSample / VS_AUDIO_FRAME_SAMPLES);
                do {
                    int64_t reqStart = reqFrame * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
                    int reqSamples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES - reqStartOffset, d->numSamples[i] - reqStart));
                    reqStartOffset = 0;
                    vsapi->requestFrameFilter(reqFrame, d->nodes[i], frameCtx);
                    remainingSamples -= reqSamples;
                    reqStart += reqSamples;
                    reqFrame++;
                    if (reqFrame > d->numFrames[i] - 1) {
                        reqFrame = 0;
                        reqStart = 0;
                        i++;
                    }
                } while (remainingSamples > 0);
                break;
            }
        }
    } else if (activationReason == arAllFramesReady) {
        VSFrame *dst = nullptr;
        size_t dstOffset = 0;

        for (size_t i = 0; i < d->cumSamples.size(); i++) {
            if (d->cumSamples[i] > sampleStart) {
                int64_t currentStartSample = sampleStart - ((i > 0) ? d->cumSamples[i - 1] : 0);
                int reqStartOffset = static_cast<int>(currentStartSample % VS_AUDIO_FRAME_SAMPLES);
                int reqFrame = static_cast<int>(currentStartSample / VS_AUDIO_FRAME_SAMPLES);
                do {
                    const VSFrame *src = vsapi->getFrameFilter(reqFrame++, d->nodes[i], frameCtx);
                    int length = vsapi->getFrameLength(src) - reqStartOffset;
                    if (!dst)
                        dst = vsapi->newAudioFrame(&d->ai.format, remainingSamples, src, core);

                    for (int p = 0; p < d->ai.format.numChannels; p++)
                        memcpy(vsapi->getWritePtr(dst, p) + dstOffset, vsapi->getReadPtr(src, p) + reqStartOffset * d->ai.format.bytesPerSample, std::min(length, remainingSamples) * d->ai.format.bytesPerSample);

                    reqStartOffset = 0;
                    dstOffset += length * d->ai.format.bytesPerSample;
                    remainingSamples -= length;
                    if (reqFrame > d->numFrames[i] - 1) {
                        reqFrame = 0;
                        i++;
                    }
                    vsapi->freeFrame(src);
                } while (remainingSamples > 0);
                break;
            }
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC audioSpliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int numNodes = vsapi->mapNumElements(in, "clips");
    if (numNodes == 1) {
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clips", 0, nullptr), maAppend);
        return;
    }
  
    std::unique_ptr<AudioSpliceData> d(new AudioSpliceData(vsapi));

    d->nodes.reserve(numNodes);
    for (int i = 0; i < numNodes; i++)
        d->nodes.push_back(vsapi->mapGetNode(in, "clips", i, nullptr));

    d->ai = *vsapi->getAudioInfo(d->nodes[0]);

    for (int i = 1; i < numNodes; i++) {
        if (!isSameAudioInfo(&d->ai, vsapi->getAudioInfo(d->nodes[i])))
            RETERROR("AudioSplice: format mismatch");
    }

    d->ai.numSamples = 0;
    for (int i = 0; i < numNodes; i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->nodes[i]);
        d->numSamples.push_back(ai->numSamples);
        d->numFrames.push_back(ai->numFrames);
        d->ai.numSamples += ai->numSamples;
    }

    d->cumSamples.push_back(d->numSamples[0]);
    for (int i = 1; i < numNodes; i++) {
        int64_t totalSamples = d->cumSamples.back() + d->numSamples[i];
        if (totalSamples > std::numeric_limits<int>::max() * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES))
            RETERROR("AudioSplice: the resulting clip is too long");
        d->cumSamples.push_back(totalSamples);
    }

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < numNodes; i++)
        deps.push_back({d->nodes[i], (i == 0) ? rpNoFrameReuse : rpGeneral});
    vsapi->createAudioFilter(out, "AudioSplice", &d->ai, audioSpliceGetframe, filterFree<AudioSpliceData>, fmParallel, deps.data(), numNodes, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioLoop

struct AudioLoopDataExtra {
    VSAudioInfo ai;
    int64_t srcSamples;
    int srcFrames;
};

typedef SingleNodeData<AudioLoopDataExtra> AudioLoopData;

static const VSFrame *VS_CC audioLoopGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioLoopData *d = reinterpret_cast<AudioLoopData *>(instanceData);

    int64_t reqStart = n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
    reqStart = reqStart % d->srcSamples;
    int reqStartFrame = static_cast<int>(reqStart / VS_AUDIO_FRAME_SAMPLES);
    int reqFrame = reqStartFrame;
    int reqStartOffset = static_cast<int>(reqStart % VS_AUDIO_FRAME_SAMPLES);
    int remainingSamples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai.numSamples - n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES)));

    if (activationReason == arInitial) {
        do {
            int reqSamples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES - reqStartOffset, d->srcSamples - reqStart));
            reqStartOffset = 0;
            vsapi->requestFrameFilter(reqFrame++, d->node, frameCtx);
            remainingSamples -= reqSamples;
            reqStart += reqSamples;
            if (reqFrame > d->srcFrames - 1) {
                reqFrame = 0;
                reqStart = 0;
            }
        } while (remainingSamples > 0 && reqFrame != reqStartFrame);
    } else if (activationReason == arAllFramesReady) {
        VSFrame *dst = nullptr;
        size_t dstOffset = 0;

        do {       
            const VSFrame *src = vsapi->getFrameFilter(reqFrame++, d->node, frameCtx);
            int length = vsapi->getFrameLength(src) - reqStartOffset;

            if (!dst)
                dst = vsapi->newAudioFrame(&d->ai.format, remainingSamples, src, core);

            for (int p = 0; p < d->ai.format.numChannels; p++)
                memcpy(vsapi->getWritePtr(dst, p) + dstOffset, vsapi->getReadPtr(src, p) + reqStartOffset * d->ai.format.bytesPerSample, std::min<int>(length, remainingSamples) * d->ai.format.bytesPerSample);

            reqStartOffset = 0;
            dstOffset += length * d->ai.format.bytesPerSample;
            remainingSamples -= length;
            if (reqFrame > d->srcFrames - 1)
                reqFrame = 0;
            vsapi->freeFrame(src);
        } while (remainingSamples > 0);

        return dst;
    }

    return nullptr;
}

static void VS_CC audioLoopCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int error;
    std::unique_ptr<AudioLoopData> d(new AudioLoopData(vsapi));
    int64_t times = vsapi->mapGetInt(in, "times", 0, &error);
    if (times < 0)
        RETERROR("AudioLoop: cannot repeat clip a negative number of times");

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->ai = *vsapi->getAudioInfo(d->node);
    d->srcSamples = d->ai.numSamples;
    d->srcFrames = d->ai.numFrames;

    // early termination for the trivial case
    if (times == 1) {
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    if (times > 0) {
        if (d->ai.numSamples > (std::numeric_limits<int>::max() * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES)) / times)
            RETERROR("AudioLoop: resulting clip is too long");
        d->ai.numSamples *= times;
    } else {
        d->ai.numSamples = std::numeric_limits<int>::max() * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
    }

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createAudioFilter(out, "AudioLoop", &d->ai, audioLoopGetFrame, filterFree<AudioLoopData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioReverse

struct AudioReverseDataExtra {
    const VSAudioInfo *ai;
};

typedef SingleNodeData<AudioReverseDataExtra> AudioReverseData;

template<typename T>
static const VSFrame *VS_CC audioReverseGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioReverseData *d = reinterpret_cast<AudioReverseData *>(instanceData);
    int n1 = d->ai->numFrames - 1 - n;
    int n2 = std::max(d->ai->numFrames - 2 - n, 0);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n1, d->node, frameCtx);
        if (d->ai->numSamples % VS_AUDIO_FRAME_SAMPLES != 0)
            vsapi->requestFrameFilter(n2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int dstLength = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai->numSamples - n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES)));
        const VSFrame *src1 = vsapi->getFrameFilter(n1, d->node, frameCtx);
        size_t l1 = vsapi->getFrameLength(src1);
        size_t s1offset = l1 - (d->ai->numSamples % VS_AUDIO_FRAME_SAMPLES);
        if (s1offset == VS_AUDIO_FRAME_SAMPLES)
            s1offset = 0;
        size_t s1samples = vsapi->getFrameLength(src1) - s1offset;

        VSFrame *dst = vsapi->newAudioFrame(&d->ai->format, dstLength, src1, core);

        for (int p = 0; p < d->ai->format.numChannels; p++) {
            const T *src1Ptr = reinterpret_cast<const T *>(vsapi->getReadPtr(src1, p));
            T *dstPtr = reinterpret_cast<T *>(vsapi->getWritePtr(dst, p));
            for (size_t i = 0; i < s1samples; i++)
                dstPtr[i] = src1Ptr[l1 - i - 1 - s1offset];
        }

        size_t remaining = dstLength - s1samples;
        vsapi->freeFrame(src1);

        if (remaining > 0) {
            const VSFrame *src2 = vsapi->getFrameFilter(n2, d->node, frameCtx);
            size_t l2 = vsapi->getFrameLength(src2);
            for (int p = 0; p < d->ai->format.numChannels; p++) {
                const T *src2Ptr = reinterpret_cast<const T *>(vsapi->getReadPtr(src2, p));
                T *dstPtr = reinterpret_cast<T *>(vsapi->getWritePtr(dst, p)) + s1samples;
                for (size_t i = 0; i < remaining; i++)
                    dstPtr[i] = src2Ptr[l2 - i - 1];
            }
            vsapi->freeFrame(src2);
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC audioReverseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioReverseData> d(new AudioReverseData(vsapi));
    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->ai = vsapi->getAudioInfo(d->node);

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    if (d->ai->format.bytesPerSample == 2)
        vsapi->createAudioFilter(out, "AudioReverse", d->ai, audioReverseGetFrame<int16_t>, filterFree<AudioReverseData>, fmParallel, deps, 1, d.get(), core);
    else
        vsapi->createAudioFilter(out, "AudioReverse", d->ai, audioReverseGetFrame<int32_t>, filterFree<AudioReverseData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioGain

struct AudioGainDataExtra {
    std::vector<float> gain;
    const VSAudioInfo *ai;
};

typedef SingleNodeData<AudioGainDataExtra> AudioGainData;

template<typename T>
static const VSFrame *VS_CC audioGainGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioGainData *d = reinterpret_cast<AudioGainData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int length = vsapi->getFrameLength(src);
        VSFrame *dst = vsapi->newAudioFrame(&d->ai->format, length, src, core);

        for (int p = 0; p < d->ai->format.numChannels; p++) {
            float gain = d->gain[(d->gain.size() > 1) ? p : 0];
            const T *srcPtr = reinterpret_cast<const T *>(vsapi->getReadPtr(src, p));
            T *dstPtr = reinterpret_cast<T *>(vsapi->getWritePtr(dst, p));
            for (int i = 0; i < length; i++)
                dstPtr[i] = static_cast<T>(srcPtr[i] * gain);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC audioGainCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioGainData> d(new AudioGainData(vsapi));
    int numGainValues = vsapi->mapNumElements(in, "gain");
    for (int i = 0; i < numGainValues; i++)
        d->gain.push_back(vsapi->mapGetFloat(in, "gain", i, nullptr));

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->ai = vsapi->getAudioInfo(d->node);

    if (numGainValues != 1 && numGainValues != d->ai->format.numChannels)
        RETERROR("AudioGain: must provide one gain value per channel or a single value used for all channels");

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    if (d->ai->format.bytesPerSample == 4 && d->ai->format.sampleType == stFloat)
        vsapi->createAudioFilter(out, "AudioGain", d->ai, audioGainGetFrame<float>, filterFree<AudioGainData>, fmParallel, deps, 1, d.get(), core);
    else if (d->ai->format.bytesPerSample == 2)
        vsapi->createAudioFilter(out, "AudioGain", d->ai, audioGainGetFrame<int16_t>, filterFree<AudioGainData>, fmParallel, deps, 1, d.get(), core);
    else
        vsapi->createAudioFilter(out, "AudioGain", d->ai, audioGainGetFrame<int32_t>, filterFree<AudioGainData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioMix

struct AudioMixDataNode {
    VSNode *node;
    int idx;
    int numFrames;
    std::vector<double> weights;
};

struct AudioMixData {
    std::vector<VSNode *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<AudioMixDataNode> sourceNodes;
    std::vector<int> outputIdx;
    VSAudioInfo ai;
};

template<typename T>
static const VSFrame *VS_CC audioMixGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioMixData *d = reinterpret_cast<AudioMixData *>(instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {     
        int numOutChannels = d->ai.format.numChannels;
        std::vector<const T *> srcPtrs;
        std::vector<const VSFrame *> srcFrames;
        srcPtrs.reserve(d->sourceNodes.size());
        srcFrames.reserve(d->sourceNodes.size());
        for (size_t idx = 0; idx < d->sourceNodes.size(); idx++) {
            const VSFrame *src = vsapi->getFrameFilter(n, d->sourceNodes[idx].node, frameCtx);                
            srcPtrs.push_back(reinterpret_cast<const T *>(vsapi->getReadPtr(src, d->sourceNodes[idx].idx)));
            srcFrames.push_back(src);
        }

        int srcLength = vsapi->getFrameLength(srcFrames[0]);
        VSFrame *dst = vsapi->newAudioFrame(&d->ai.format, srcLength, srcFrames[0], core);

        std::vector<T *> dstPtrs;
        dstPtrs.resize(numOutChannels);
        for (int idx = 0; idx < numOutChannels; idx++)
            dstPtrs[idx] = reinterpret_cast<T *>(vsapi->getWritePtr(dst, d->outputIdx[idx]));

        for (int i = 0; i < srcLength; i++) {
            for (size_t dstIdx = 0; dstIdx < static_cast<size_t>(numOutChannels); dstIdx++) {
                double tmp = 0;
                for (size_t srcIdx = 0; srcIdx < srcPtrs.size(); srcIdx++)
                    tmp += static_cast<double>(srcPtrs[srcIdx][i]) * d->sourceNodes[srcIdx].weights[dstIdx];

                dstPtrs[dstIdx][i] = static_cast<T>(tmp);
            }
        }

        for (auto iter : srcFrames)
            vsapi->freeFrame(iter);

        return dst;
    }

    return nullptr;
}

static void VS_CC audioMixFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    AudioMixData *d = reinterpret_cast<AudioMixData *>(instanceData);
    for (const auto &iter : d->sourceNodes)
        vsapi->freeNode(iter.node);
    delete d;
}

static void VS_CC audioMixCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioMixData> d(new AudioMixData());
    int numSrcNodes = vsapi->mapNumElements(in, "clips");
    int numMatrixWeights = vsapi->mapNumElements(in, "matrix");
    int numDstChannels = vsapi->mapNumElements(in, "channels_out");
    uint64_t channelLayout = 0;

    for (int i = 0; i < numDstChannels; i++) {
        int channel = vsapi->mapGetIntSaturated(in, "channels_out", i, nullptr);
        channelLayout |= static_cast<uint64_t>(1) << channel;
    }

    for (int i = 0; i < numDstChannels; i++) {
        int channel = vsapi->mapGetIntSaturated(in, "channels_out", i, nullptr);
        int pos = 0;
        for (int j = 0; j < channel; j++) {
            if ((static_cast<uint64_t>(1) << j) & channelLayout)
                pos++;
        }
        d->outputIdx.push_back(pos);
    }
    
    int numSrcChannels = 0;

    for (int i = 0; i < numSrcNodes; i++) {
        VSNode *node = vsapi->mapGetNode(in, "clips", std::min(numSrcNodes - 1, i), nullptr);
        const VSAudioFormat &f = vsapi->getAudioInfo(node)->format;
        for (int j = 0; j < f.numChannels; j++) {
            d->sourceNodes.push_back({ (j > 0) ? vsapi->addNodeRef(node) : node, j, -1, {} });
            numSrcChannels++;
        }
    }

    if (numSrcNodes > numSrcChannels) {
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        RETERROR("AudioMix: cannot have more input nodes than selected input channels");
    }

    if (numDstChannels * numSrcChannels != numMatrixWeights) {
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        RETERROR("AudioMix: the number of matrix weights must equal (input channels * output channels)");
    }

    const char *err = nullptr;

    d->ai = *vsapi->getAudioInfo(d->sourceNodes[0].node);
    for (int i = 0; i < static_cast<int>(d->sourceNodes.size()); i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->sourceNodes[i].node);
        if (ai->numSamples != d->ai.numSamples || ai->sampleRate != d->ai.sampleRate || ai->format.bitsPerSample != d->ai.format.bitsPerSample || ai->format.sampleType != d->ai.format.sampleType) {
            err = "AudioMix: all inputs must have the same length, samplerate, bits per sample and sample type";
            break;
        }

        d->ai.numSamples = std::max(d->ai.numSamples, ai->numSamples);
        for (int j = 0; j < numDstChannels; j++)
            d->sourceNodes[i].weights.push_back(vsapi->mapGetFloat(in, "matrix", j * numSrcChannels + i, nullptr));
        d->sourceNodes[i].numFrames = ai->numFrames;
    }

    if (!vsapi->queryAudioFormat(&d->ai.format, d->ai.format.sampleType, d->ai.format.bitsPerSample, channelLayout, core))
        err = "AudioMix: invalid output channel configuration";
    else if (d->ai.format.numChannels != numDstChannels)
        err = "AudioMix: output channel specified twice";

    if (err) {
        vsapi->mapSetError(out, err);
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    std::set<VSNode *> nodeSet;
    for (const auto &iter : d->sourceNodes)
        nodeSet.insert(iter.node);
    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    std::vector<VSFilterDependency> deps;
    for (const auto &iter : d->reqNodes)
        deps.push_back({iter, rpStrictSpatial});
    if (d->ai.format.sampleType == stFloat)
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, audioMixGetFrame<float>, audioMixFree, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    else if (d->ai.format.bytesPerSample == 2)
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, audioMixGetFrame<int16_t>, audioMixFree, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    else
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, audioMixGetFrame<int32_t>, audioMixFree, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    d.release();
}

//////////////////////////////////////////
// ShuffleChannels

struct ShuffleChannelsDataNode {
    VSNode *node;
    int idx;
    int dstIdx;
    int numFrames;

    inline bool operator<(const ShuffleChannelsDataNode &other) const {
        return dstIdx < other.dstIdx;
    }
};

struct ShuffleChannelsData {
    std::vector<VSNode *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<ShuffleChannelsDataNode> sourceNodes;
    VSAudioInfo ai;
};

static const VSFrame *VS_CC shuffleChannelsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShuffleChannelsData *d = reinterpret_cast<ShuffleChannelsData *>(instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSFrame *dst = nullptr;
        int dstLength = static_cast<int>(std::min<int64_t>(d->ai.numSamples - n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES), VS_AUDIO_FRAME_SAMPLES));
        for (int idx = 0; idx < static_cast<int>(d->sourceNodes.size()); idx++) {
            const VSFrame *src = vsapi->getFrameFilter(n, d->sourceNodes[idx].node, frameCtx);;
            int srcLength = (n < d->sourceNodes[idx].numFrames) ? vsapi->getFrameLength(src) : 0;
            int copyLength = std::min(dstLength, srcLength);
            int zeroLength = dstLength - copyLength;
            if (!dst)
                dst = vsapi->newAudioFrame(&d->ai.format, dstLength, src, core);
            if (copyLength > 0)
                memcpy(vsapi->getWritePtr(dst, idx), vsapi->getReadPtr(src, d->sourceNodes[idx].idx), copyLength * d->ai.format.bytesPerSample);
            if (zeroLength > 0)
                memset(vsapi->getWritePtr(dst, idx) + copyLength * d->ai.format.bytesPerSample, 0, zeroLength * d->ai.format.bytesPerSample);
            vsapi->freeFrame(src);
        }

        return dst;
    }

    return nullptr;
}

static void VS_CC shuffleChannelsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ShuffleChannelsData *d = reinterpret_cast<ShuffleChannelsData *>(instanceData);
    for (const auto iter : d->sourceNodes)
        vsapi->freeNode(iter.node);
    delete d;
}

static void VS_CC shuffleChannelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ShuffleChannelsData> d(new ShuffleChannelsData());
    int numSrcNodes = vsapi->mapNumElements(in, "clips");
    int numSrcChannels = vsapi->mapNumElements(in, "channels_in");
    int numDstChannels = vsapi->mapNumElements(in, "channels_out");

    if (numSrcChannels != numDstChannels)
        RETERROR("ShuffleChannels: must have the same number of channels_in and channels_out");

    if (numSrcNodes > numSrcChannels)
        RETERROR("ShuffleChannels: cannot have more input nodes than selected input channels");

    uint64_t channelLayout = 0;

    for (int i = 0; i < numSrcChannels; i++) {
        int channel = vsapi->mapGetIntSaturated(in, "channels_in", i, nullptr);
        int dstChannel = vsapi->mapGetIntSaturated(in, "channels_out", i, nullptr);
        channelLayout |= (static_cast<uint64_t>(1) << dstChannel);
        VSNode *node = vsapi->mapGetNode(in, "clips", std::min(numSrcNodes - 1, i), nullptr);
        d->sourceNodes.push_back({node, channel, dstChannel, -1});
    }

    std::sort(d->sourceNodes.begin(), d->sourceNodes.end());

    const char *err = nullptr;

    d->ai = *vsapi->getAudioInfo(d->sourceNodes[0].node);
    for (size_t i = 0; i < d->sourceNodes.size(); i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->sourceNodes[i].node);
        if (ai->sampleRate != d->ai.sampleRate || ai->format.bitsPerSample != d->ai.format.bitsPerSample || ai->format.sampleType != d->ai.format.sampleType) {
            err = "ShuffleChannels: all inputs must have the same samplerate, bits per sample and sample type";
            break;
        }
        // recalculate channel number to a simple index (add as a vsapi function?)
        if (d->sourceNodes[i].idx < 0) {
            d->sourceNodes[i].idx = (-d->sourceNodes[i].idx) - 1;
            if (ai->format.numChannels <= d->sourceNodes[i].idx) {
                err = "ShuffleChannels: specified channel is not present in input";
                break;
            }
        } else {
            if ((d->sourceNodes[i].idx > 0) && !(ai->format.channelLayout & (static_cast<uint64_t>(1) << d->sourceNodes[i].idx))) {
                err = "ShuffleChannels: specified channel is not present in input";
                break;
            }
            int idx = 0;
            for (int j = 0; j < d->sourceNodes[i].idx; j++)
                if (ai->format.channelLayout & (static_cast<uint64_t>(1) << j))
                    idx++;
            d->sourceNodes[i].idx = idx;
        }
        d->sourceNodes[i].numFrames = ai->numFrames;
        d->ai.numSamples = std::max(d->ai.numSamples, ai->numSamples);
    }

    if (!vsapi->queryAudioFormat(&d->ai.format, d->ai.format.sampleType, d->ai.format.bitsPerSample, channelLayout, core))
        err = "ShuffleChannels: invalid output channel configuration";
    else if (d->ai.format.numChannels != numDstChannels)
        err = "ShuffleChannels: output channel specified twice";

    if (err) {
        vsapi->mapSetError(out, err);
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    // This is to reduce the total number of requests to save some scheduling time later
    std::set<VSNode *> nodeSet;
    for (const auto &iter : d->sourceNodes)
        nodeSet.insert(iter.node);
    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    std::vector<VSFilterDependency> deps;
    for (const auto &iter : d->reqNodes)
        deps.push_back({iter, (d->ai.numFrames <= vsapi->getVideoInfo(iter)->numFrames) ? rpStrictSpatial : rpGeneral});

    vsapi->createAudioFilter(out, "ShuffleChannels", &d->ai, shuffleChannelsGetFrame, shuffleChannelsFree, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SplitChannels

static void VS_CC splitChannelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSAudioInfo *ai = vsapi->getAudioInfo(node);
    uint64_t channelLayout = ai->format.channelLayout;
    int numChannels = ai->format.numChannels;
    
    // Pass through when nothing to do
    if (numChannels == 1) {
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
        return;
    }
    
    VSMap *map = vsapi->createMap();
    vsapi->mapConsumeNode(map, "clips", node, maAppend);

    size_t index = 0;
    for (int i = 0; i < numChannels; i++) {
        while (!(channelLayout & (static_cast<uint64_t>(1) << index)))
            index++;
        vsapi->mapSetInt(map, "channels_in", index, maReplace);
        vsapi->mapSetInt(map, "channels_out", index, maReplace);
        VSMap *tmp = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShuffleChannels", map);
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(tmp, "clip", 0, nullptr), maAppend);
        vsapi->freeMap(tmp);
        index++;
    }

    vsapi->freeMap(map);
}

//////////////////////////////////////////
// AssumeSampleRate

typedef SingleNodeData<NoExtraData> AssumeSampleRateData;

static const VSFrame *VS_CC assumeSampleRateGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeSampleRateData *d = reinterpret_cast<AssumeSampleRateData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n, d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC assumeSampleRateCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AssumeSampleRateData> d(new AssumeSampleRateData(vsapi));
    bool hassamplerate = false;
    bool hassrc = false;
    int err;

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSAudioInfo ai = *vsapi->getAudioInfo(d->node);

    ai.sampleRate = vsapi->mapGetIntSaturated(in, "samplerate", 0, &err);
    if (!err)
        hassamplerate = true;

    VSNode *src = vsapi->mapGetNode(in, "src", 0, &err);

    if (!err) {
        ai.sampleRate = vsapi->getAudioInfo(d->node)->sampleRate;
        vsapi->freeNode(src);
        hassrc = true;
    }

    if ((hassamplerate && hassrc) || (!hassamplerate && !hassrc))
        RETERROR("AssumeSampleRate: need to specify source clip or samplerate");

    if (ai.sampleRate < 1)
        RETERROR("AssumeSampleRate: invalid samplerate specified");

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createAudioFilter(out, "AssumeSampleRate", &ai, assumeSampleRateGetframe, filterFree<AssumeSampleRateData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// BlankAudio

typedef struct {
    VSFrame *f;
    VSAudioInfo ai;
    bool keep;
} BlankAudioData;

static const VSFrame *VS_CC blankAudioGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankAudioData *d = reinterpret_cast<BlankAudioData *>(instanceData);

    if (activationReason == arInitial) {
        VSFrame *frame = nullptr;
        if (!d->f) {
            int samples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai.numSamples - n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES)));
            frame = vsapi->newAudioFrame(&d->ai.format, samples, nullptr, core);
            for (int channel = 0; channel < d->ai.format.numChannels; channel++)
                memset(vsapi->getWritePtr(frame, channel), 0, samples * d->ai.format.bytesPerSample);
        }

        if (d->keep) {
            if (frame)
                d->f = frame;
            return vsapi->addFrameRef(d->f);
        } else {
            return frame;
        }
    }

    return nullptr;
}

static void VS_CC blankAudioFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BlankAudioData *d = reinterpret_cast<BlankAudioData *>(instanceData);
    vsapi->freeFrame(d->f);
    delete d;
}

static void VS_CC blankAudioCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BlankAudioData> d(new BlankAudioData());
    bool hasai = false;
    int tmp1;
    int64_t tmp2;
    int err;


    VSNode *node = vsapi->mapGetNode(in, "clip", 0, &err);

    if (!err) {
        d->ai = *vsapi->getAudioInfo(node);
        vsapi->freeNode(node);
        hasai = true;
    }

    int numChannelElems = vsapi->mapNumElements(in, "channels");

    if (numChannelElems <= 0) {
        if (!hasai)
            d->ai.format.channelLayout = (1 << acFrontLeft) | (1 << acFrontRight);
    } else {
        d->ai.format.channelLayout = 0;
        for (int i = 0; i < numChannelElems; i++) {
            uint64_t ctemp = static_cast<uint64_t>(1) << vsapi->mapGetInt(in, "channels", i, nullptr);
            if (d->ai.format.channelLayout & ctemp)
                RETERROR("BlankAudio: channel specified twice");
            d->ai.format.channelLayout |= ctemp;
        }
    }

    tmp1 = vsapi->mapGetIntSaturated(in, "bits", 0, &err);

    if (err) {
        if (!hasai)
            d->ai.format.bitsPerSample = 16;
    } else {
        d->ai.format.bitsPerSample = tmp1;
    }

    tmp2 = vsapi->mapGetInt(in, "sampletype", 0, &err);

    if (err) {
        if (!hasai)
            d->ai.format.sampleType = stInteger;
    } else {
        d->ai.format.sampleType = (tmp2 ? stFloat : stInteger);
    }

    d->keep = !!vsapi->mapGetInt(in, "keep", 0, &err);

    tmp1 = vsapi->mapGetIntSaturated(in, "samplerate", 0, &err);

    if (err) {
        if (!hasai)
            d->ai.sampleRate = 44100;
    } else {
        d->ai.sampleRate = tmp1;
    }

    tmp2 = vsapi->mapGetInt(in, "length", 0, &err);

    if (err) {
        if (!hasai)
            d->ai.numSamples = static_cast<int64_t>(d->ai.sampleRate) * 10;
    } else {
        d->ai.numSamples = tmp2;
    }

    if (d->ai.sampleRate <= 0)
        RETERROR("BlankAudio: invalid sample rate");

    if (d->ai.numSamples <= 0)
        RETERROR("BlankAudio: invalid length");

    if (!vsapi->queryAudioFormat(&d->ai.format, d->ai.format.sampleType, d->ai.format.bitsPerSample, d->ai.format.channelLayout, core))
        RETERROR("BlankAudio: invalid format");

    vsapi->createAudioFilter(out, "BlankAudio", &d->ai, blankAudioGetframe, blankAudioFree, d->keep ? fmUnordered : fmParallel, nullptr, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// TestAudio

typedef struct {
    VSAudioInfo ai;
} TestAudioData;

static const VSFrame *VS_CC testAudioGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TestAudioData *d = reinterpret_cast<TestAudioData *>(instanceData);

    if (activationReason == arInitial) {
        int64_t startSample = n * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
        int samples = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai.numSamples - startSample));
        VSFrame *frame = vsapi->newAudioFrame(&d->ai.format, samples, nullptr, core);
        for (int channel = 0; channel < d->ai.format.numChannels; channel++) {
            uint16_t *w = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, channel));
            for (int i = 0; i < samples; i++)
                w[i] = (startSample + i) % 0xFFFF;
        }
        return frame;
    }

    return nullptr;
}

static void VS_CC testAudioCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TestAudioData> d(new TestAudioData());

    int err;

    int numChannelElems = vsapi->mapNumElements(in, "channels");
    uint64_t channels = (1 << acFrontLeft) | (1 << acFrontRight);
    if (numChannelElems > 0) {
        channels = 0;
        for (int i = 0; i < numChannelElems; i++) {
            uint64_t ctemp = static_cast<uint64_t>(1) << vsapi->mapGetInt(in, "channels", i, nullptr);
            if (channels & ctemp)
                RETERROR("TestAudio: channel specified twice");
            channels |= ctemp;
        }
    }

    int bits = vsapi->mapGetIntSaturated(in, "bits", 0, &err);
    if (err)
        bits = 16;

    if (bits != 16)
        RETERROR("TestAudio: bits must be 16!");

    bool isfloat = !!vsapi->mapGetInt(in, "isfloat", 0, &err);

    d->ai.sampleRate = vsapi->mapGetIntSaturated(in, "samplerate", 0, &err);
    if (err)
        d->ai.sampleRate = 44100;

    d->ai.numSamples = vsapi->mapGetInt(in, "length", 0, &err);
    if (err)
        d->ai.numSamples = static_cast<int64_t>(d->ai.sampleRate) * 60 * 60;

    if (d->ai.sampleRate <= 0)
        RETERROR("TestAudio: invalid sample rate");

    if (d->ai.numSamples <= 0)
        RETERROR("TestAudio: invalid length");

    if (!vsapi->queryAudioFormat(&d->ai.format, isfloat ? stFloat : stInteger, bits, channels, core))
        RETERROR("TestAudio: invalid format");

    vsapi->createAudioFilter(out, "TestAudio", &d->ai, testAudioGetframe, filterFree<TestAudioData>, fmParallel, nullptr, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void audioInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("AudioTrim", "clip:anode;first:int:opt;last:int:opt;length:int:opt;", "clip:anode;", audioTrimCreate, 0, plugin);
    vspapi->registerFunction("AudioSplice", "clips:anode[];", "clip:anode;", audioSpliceCreate, 0, plugin);
    vspapi->registerFunction("AudioLoop", "clip:anode;times:int:opt;", "clip:anode;", audioLoopCreate, 0, plugin);
    vspapi->registerFunction("AudioReverse", "clip:anode;", "clip:anode;", audioReverseCreate, 0, plugin);
    vspapi->registerFunction("AudioGain", "clip:anode;gain:float[]:opt;", "clip:anode;", audioGainCreate, 0, plugin);
    vspapi->registerFunction("AudioMix", "clips:anode[];matrix:float[];channels_out:int[];", "clip:anode;", audioMixCreate, 0, plugin);
    vspapi->registerFunction("ShuffleChannels", "clips:anode[];channels_in:int[];channels_out:int[];", "clip:anode;", shuffleChannelsCreate, 0, plugin);
    vspapi->registerFunction("SplitChannels", "clip:anode;", "clip:anode[];", splitChannelsCreate, 0, plugin);
    vspapi->registerFunction("AssumeSampleRate", "clip:anode;src:anode:opt;samplerate:int:opt;", "clip:anode;", assumeSampleRateCreate, 0, plugin);
    vspapi->registerFunction("BlankAudio", "clip:anode:opt;channels:int[]:opt;bits:int:opt;sampletype:int:opt;samplerate:int:opt;length:int:opt;keep:int:opt;", "clip:anode;", blankAudioCreate, 0, plugin);
    vspapi->registerFunction("TestAudio", "channels:int[]:opt;bits:int:opt;isfloat:int:opt;samplerate:int:opt;length:int:opt;", "clip:anode;", testAudioCreate, 0, plugin);
}
