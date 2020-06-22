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

#include "internalfilters.h"
#include "VSHelper.h"
#include "filtershared.h"
#include "filtersharedcpp.h"

#include <cstdlib>
#include <cstdio>
#include <cinttypes>
#include <memory>
#include <limits>
#include <string>
#include <algorithm>
#include <vector>
#include <set>
#include <bitset>

/////////////
// TODO:
// channels_out should probably be a list in order to not be exceptionally confusing in audiomix
// improve memory access pattern in audiomix, processing input and output in blocks of a few thousand samples should lead to much better cache locality
// remove the std::set usage since it's shit code


//////////////////////////////////////////
// AudioTrim

typedef struct {
    VSNodeRef *node;
    VSAudioInfo ai;
    int64_t first;
} AudioTrimData;

static const VSFrameRef *VS_CC audioTrimGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioTrimData *d = reinterpret_cast<AudioTrimData *>(*instanceData);

    int64_t startSample = n * (int64_t)d->ai.format->samplesPerFrame + d->first;
    int startFrame = (int)(startSample / d->ai.format->samplesPerFrame);
    int length = std::min<int64_t>(d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame), d->ai.format->samplesPerFrame);

    if (startSample % d->ai.format->samplesPerFrame == 0 && n != d->ai.numFrames - 1) { // pass through audio frames when possible
        if (activationReason == arInitial) {
            vsapi->requestFrameFilter(startFrame, d->node, frameCtx);
        } else if (activationReason == arAllFramesReady) {
            const VSFrameRef *src = vsapi->getFrameFilter(startFrame, d->node, frameCtx);
            if (length == vsapi->getFrameLength(src))
                return src;
            VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, length, src, core);
            for (int channel = 0; channel < d->ai.format->numChannels; channel++)
                memcpy(vsapi->getWritePtr(dst, channel), vsapi->getReadPtr(src, channel), length * d->ai.format->bytesPerSample);
            vsapi->freeFrame(src);
            return dst;
        }
    } else {
        int numSrc1Samples = d->ai.format->samplesPerFrame - (startSample % d->ai.format->samplesPerFrame);
        if (activationReason == arInitial) {
            vsapi->requestFrameFilter(startFrame, d->node, frameCtx);
            if (numSrc1Samples < length)
                vsapi->requestFrameFilter(startFrame + 1, d->node, frameCtx);
        } else if (activationReason == arAllFramesReady) {
            const VSFrameRef *src1 = vsapi->getFrameFilter(startFrame, d->node, frameCtx);
            VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, length, src1, core);
            for (int channel = 0; channel < d->ai.format->numChannels; channel++)
                memcpy(vsapi->getWritePtr(dst, channel), vsapi->getReadPtr(src1, channel) + (d->ai.format->samplesPerFrame - numSrc1Samples) * d->ai.format->bytesPerSample, numSrc1Samples * d->ai.format->bytesPerSample);
            vsapi->freeFrame(src1);

            if (length > numSrc1Samples) {
                const VSFrameRef *src2 = vsapi->getFrameFilter(startFrame + 1, d->node, frameCtx);
                for (int channel = 0; channel < d->ai.format->numChannels; channel++)
                    memcpy(vsapi->getWritePtr(dst, channel) + numSrc1Samples * d->ai.format->bytesPerSample, vsapi->getReadPtr(src2, channel), (length - numSrc1Samples) * d->ai.format->bytesPerSample);
                vsapi->freeFrame(src2);
            }

            return dst;
        }
    }

    return 0;
}

static void VS_CC audioTrimCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioTrimData> d(new AudioTrimData());

    int err;
    int64_t trimlen;

    d->first = int64ToIntS(vsapi->propGetInt(in, "first", 0, &err));
    bool firstset = !err;
    int64_t last = int64ToIntS(vsapi->propGetInt(in, "last", 0, &err));
    bool lastset = !err;
    int64_t length = int64ToIntS(vsapi->propGetInt(in, "length", 0, &err));
    bool lengthset = !err;

    if (lastset && lengthset)
        RETERROR("AudioTrim: both last sample and length specified");

    if (lastset && last < d->first)
        RETERROR("AudioTrim: invalid last sample specified (last is less than first)");

    if (lengthset && length < 1)
        RETERROR("AudioTrim: invalid length specified (less than 1)");

    if (d->first < 0)
        RETERROR("Trim: invalid first frame specified (less than 0)");

    d->node = vsapi->propGetNode(in, "clip", 0, 0);

    d->ai = *vsapi->getAudioInfo(d->node);

    if ((lastset && last >= d->ai.numSamples) || (lengthset && (d->first + length) > d->ai.numSamples) || (d->ai.numSamples <= d->first)) {
        vsapi->freeNode(d->node);
        RETERROR("AudioTrim: last sample beyond clip end");
    }

    if (lastset) {
        trimlen = last - d->first + 1;
    } else if (lengthset) {
        trimlen = length;
    } else {
        trimlen = d->ai.numSamples - d->first;
    }

    // obvious nop() so just pass through the input clip
    if ((!firstset && !lastset && !lengthset) || (trimlen && trimlen == d->ai.numSamples)) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        vsapi->freeNode(d->node);
        return;
    }

    d->ai.numSamples = trimlen;

    vsapi->createAudioFilter(out, "AudioTrim", &d->ai, 1, audioTrimGetframe, templateNodeFree<AudioTrimData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioSplice

typedef struct {
    VSAudioInfo ai;
    std::vector<VSNodeRef *> nodes;
    std::vector<int64_t> numSamples;
    std::vector<int64_t> cumSamples;
    std::vector<int> numFrames;
} AudioSpliceData;


static const VSFrameRef *VS_CC audioSpliceGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioSpliceData *d = reinterpret_cast<AudioSpliceData *>(*instanceData);

    int64_t sampleStart = n * static_cast<int64_t>(d->ai.format->samplesPerFrame);
    int64_t remainingSamples = std::min<int64_t>(d->ai.format->samplesPerFrame, d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame));

    if (activationReason == arInitial) {
        for (size_t i = 0; i < d->cumSamples.size(); i++) {
            if (d->cumSamples[i] > sampleStart) {
                int64_t currentStartSample = sampleStart - ((i > 0) ? d->cumSamples[i - 1] : 0);
                int64_t reqStartOffset = currentStartSample % d->ai.format->samplesPerFrame;
                int reqFrame = static_cast<int>(currentStartSample / d->ai.format->samplesPerFrame);
                int64_t reqStart = reqFrame * static_cast<int64_t>(d->ai.format->samplesPerFrame);
                do {
                    int64_t reqSamples = std::min<int64_t>(d->ai.format->samplesPerFrame - reqStartOffset, d->numSamples[i] - reqStart);
                    reqStartOffset = 0;
                    vsapi->requestFrameFilter(reqFrame++, d->nodes[i], frameCtx);
                    remainingSamples -= reqSamples;
                    reqStart += reqSamples;
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
        int samplesOut = std::min<int64_t>(d->ai.format->samplesPerFrame, d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame));        
        VSFrameRef *dst = nullptr;
        size_t dstOffset = 0;

        for (size_t i = 0; i < d->cumSamples.size(); i++) {
            if (d->cumSamples[i] > sampleStart) {
                int64_t currentStartSample = sampleStart - ((i > 0) ? d->cumSamples[i - 1] : 0);
                int64_t reqStartOffset = currentStartSample % d->ai.format->samplesPerFrame;
                int reqFrame = static_cast<int>(currentStartSample / d->ai.format->samplesPerFrame);
                do {
                    const VSFrameRef *src = vsapi->getFrameFilter(reqFrame++, d->nodes[i], frameCtx);
                    int length = vsapi->getFrameLength(src) - reqStartOffset;
                    reqStartOffset = 0;
                    if (!dst)
                        dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, remainingSamples, src, core);

                    for (int p = 0; p < d->ai.format->numChannels; p++)
                        memcpy(vsapi->getWritePtr(dst, p) + dstOffset, vsapi->getReadPtr(src, p) + reqStartOffset * d->ai.format->bytesPerSample, std::min<int>(length, remainingSamples) * d->ai.format->bytesPerSample);

                    dstOffset += length * d->ai.format->bytesPerSample;
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

    return 0;
}

static void VS_CC audioSpliceFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    AudioSpliceData *d = reinterpret_cast<AudioSpliceData *>(instanceData);
    for (auto iter : d->nodes)
        vsapi->freeNode(iter);
    delete d;
}

static void VS_CC audioSpliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int numNodes = vsapi->propNumElements(in, "clips");
    if (numNodes == 1) {
        VSNodeRef *node = vsapi->propGetNode(in, "clips", 0, nullptr);
        vsapi->propSetNode(out, "clip", node, paAppend);
        vsapi->freeNode(node);
    }
  
    std::unique_ptr<AudioSpliceData> d(new AudioSpliceData);

    d->nodes.reserve(numNodes);
    for (int i = 0; i < numNodes; i++)
        d->nodes.push_back(vsapi->propGetNode(in, "clips", i, nullptr));

    d->ai = *vsapi->getAudioInfo(d->nodes[0]);

    for (int i = 1; i < numNodes; i++) {
        if (!isSameAudioFormat(&d->ai, vsapi->getAudioInfo(d->nodes[i]))) {
            for (auto iter : d->nodes)
                vsapi->freeNode(iter);
            RETERROR("AudioSplice: format mismatch");
        }
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
        d->cumSamples.push_back(d->cumSamples.back() + d->numSamples[i]);
    }

    // check for too long clip
    /*
    if (d->ai.numSamples < d->numSamples1 || d->ai.numSamples < d->numSamples2) {
        vsapi->freeNode(d->node1);
        vsapi->freeNode(d->node2);
        RETERROR("AudioSplice: the resulting clip is too long");
    }
    */

    vsapi->createAudioFilter(out, "AudioSplice", &d->ai, 1, audioSpliceGetframe, audioSpliceFree, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioLoop

struct AudioLoopData {
    VSNodeRef *node;
    VSAudioInfo ai;
    int srcFrames;
    int64_t srcSamples;
};

static const VSFrameRef *VS_CC audioLoopGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioLoopData *d = reinterpret_cast<AudioLoopData *>(*instanceData);

    int64_t reqStart = n * d->ai.format->samplesPerFrame;
    reqStart = reqStart % d->srcSamples;
    int reqStartFrame = reqStart / d->ai.format->samplesPerFrame;
    int reqFrame = reqStartFrame;
    int64_t reqStartOffset = reqStart % d->ai.format->samplesPerFrame;
    int64_t remainingSamples = std::min<int64_t>(d->ai.format->samplesPerFrame, d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame));

    if (activationReason == arInitial) {
        do {
            int64_t reqSamples = std::min<int64_t>(d->ai.format->samplesPerFrame - reqStartOffset, d->srcSamples - reqStart);
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
        VSFrameRef *dst = nullptr;
        size_t dstOffset = 0;

        do {       
            const VSFrameRef *src = vsapi->getFrameFilter(reqFrame++, d->node, frameCtx);
            int length = vsapi->getFrameLength(src) - reqStartOffset;
            reqStartOffset = 0;
            if (!dst)
                dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, remainingSamples, src, core);

            for (int p = 0; p < d->ai.format->numChannels; p++)
                memcpy(vsapi->getWritePtr(dst, p) + dstOffset, vsapi->getReadPtr(src, p) + reqStartOffset * d->ai.format->bytesPerSample, std::min<int>(length, remainingSamples) * d->ai.format->bytesPerSample);

            dstOffset += length * d->ai.format->bytesPerSample;
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
    std::unique_ptr<AudioLoopData> d(new AudioLoopData());
    int64_t times = vsapi->propGetInt(in, "times", 0, &error);
    if (times < 0) {
        vsapi->setError(out, "AudioLoop: cannot repeat clip a negative number of times"); 
        return;
    }

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->ai = *vsapi->getAudioInfo(d->node);
    d->srcSamples = d->ai.numSamples;
    d->srcFrames = d->ai.numFrames;

    // early termination for the trivial case
    if (times == 1) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        vsapi->freeNode(d->node);
        return;
    }

    if (times > 0) {
        if (d->ai.numSamples > (std::numeric_limits<int>::max() * static_cast<int64_t>(d->ai.format->samplesPerFrame)) / times) {
            vsapi->setError(out, "AudioLoop: resulting clip is too long");
            return;
        }
        d->ai.numSamples *= times;
    } else {
        d->ai.numSamples = std::numeric_limits<int>::max() * static_cast<int64_t>(d->ai.format->samplesPerFrame);
    }

    vsapi->createAudioFilter(out, "AudioLoop", &d->ai, 1, audioLoopGetFrame, templateNodeFree<AudioLoopData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioReverse

struct AudioReverseData {
    VSNodeRef *node;
    const VSAudioInfo *ai;
};

template<typename T>
static const VSFrameRef *VS_CC audioReverseGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioReverseData *d = reinterpret_cast<AudioReverseData *>(*instanceData);
    int n1 = d->ai->numFrames - 1 - n;
    int n2 = std::max(d->ai->numFrames - 2 - n, 0);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n1, d->node, frameCtx);
        if (d->ai->numSamples % d->ai->format->samplesPerFrame != 0)
            vsapi->requestFrameFilter(n2, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        int dstLength = std::min<int64_t>(d->ai->format->samplesPerFrame, d->ai->numSamples - n * static_cast<int64_t>(d->ai->format->samplesPerFrame));
        const VSFrameRef *src1 = vsapi->getFrameFilter(n1, d->node, frameCtx);
        size_t l1 = vsapi->getFrameLength(src1);
        size_t s1offset = l1 - (d->ai->numSamples % d->ai->format->samplesPerFrame);
        if (s1offset == d->ai->format->samplesPerFrame)
            s1offset = 0;
        size_t s1samples = vsapi->getFrameLength(src1) - s1offset;

        VSFrameRef *dst = vsapi->newAudioFrame(d->ai->format, d->ai->sampleRate, dstLength, src1, core);

        for (int p = 0; p < d->ai->format->numChannels; p++) {
            const T *src1Ptr = reinterpret_cast<const T *>(vsapi->getReadPtr(src1, p));
            T *dstPtr = reinterpret_cast<T *>(vsapi->getWritePtr(dst, p));
            for (size_t i = 0; i < s1samples; i++)
                dstPtr[i] = src1Ptr[l1 - i - 1 - s1offset];
        }

        size_t remaining = dstLength - s1samples;
        vsapi->freeFrame(src1);

        if (remaining > 0) {
            const VSFrameRef *src2 = vsapi->getFrameFilter(n2, d->node, frameCtx);
            size_t l2 = vsapi->getFrameLength(src2);
            for (int p = 0; p < d->ai->format->numChannels; p++) {
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
    std::unique_ptr<AudioReverseData> d(new AudioReverseData());
    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->ai = vsapi->getAudioInfo(d->node);

    if (d->ai->format->bytesPerSample == 2)
        vsapi->createAudioFilter(out, "AudioReverse", d->ai, 1, audioReverseGetFrame<int16_t>, templateNodeFree<AudioReverseData>, fmParallel, 0, d.get(), core);
    else
        vsapi->createAudioFilter(out, "AudioReverse", d->ai, 1, audioReverseGetFrame<int32_t>, templateNodeFree<AudioReverseData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AudioMix

struct AudioMixDataNode {
    VSNodeRef *node;
    int numFrames;
    int idx;
    std::vector<float> weights;
};

struct AudioMixData {
    std::vector<VSNodeRef *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<AudioMixDataNode> sourceNodes;
    VSAudioInfo ai;
};

template<typename T>
static const VSFrameRef *VS_CC audioMixGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioMixData *d = reinterpret_cast<AudioMixData *>(*instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {     
        int numOutChannels = d->ai.format->numChannels;
        std::vector<const T *> srcPtrs;
        std::vector<const VSFrameRef *> srcFrames;
        srcPtrs.reserve(d->sourceNodes.size());
        srcFrames.reserve(d->sourceNodes.size());
        for (size_t idx = 0; idx < d->sourceNodes.size(); idx++) {
            const VSFrameRef *src = vsapi->getFrameFilter(n, d->sourceNodes[idx].node, frameCtx);                
            srcPtrs.push_back(reinterpret_cast<const T *>(vsapi->getReadPtr(src, d->sourceNodes[idx].idx)));
            srcFrames.push_back(src);
        }

        int srcLength = vsapi->getFrameLength(srcFrames[0]);
        VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, srcLength, srcFrames[0], core);

        std::vector<T *> dstPtrs;
        dstPtrs.reserve(d->sourceNodes.size());
        for (int idx = 0; idx < numOutChannels; idx++)
            dstPtrs.push_back(reinterpret_cast<T *>(vsapi->getWritePtr(dst, idx)));

        for (int i = 0; i < srcLength; i++) {
            for (size_t dstIdx = 0; dstIdx < numOutChannels; dstIdx++) {
                double tmp = 0;
                for (size_t srcIdx = 0; srcIdx < srcPtrs.size(); srcIdx++)
                    tmp += static_cast<double>(srcPtrs[srcIdx][i]) * d->sourceNodes[srcIdx].weights[dstIdx];

                if (std::numeric_limits<T>::is_integer) {
                    if (sizeof(T) == 2) {
                        dstPtrs[dstIdx][i] = static_cast<int16_t>(tmp);
                    } else if (sizeof(T) == 4) {
                        tmp = std::min<float>(tmp, (static_cast<int64_t>(1) << (d->ai.format->bitsPerSample - 1)) - 1);
                        dstPtrs[dstIdx][i] = static_cast<int32_t>(tmp);
                    }
                } else {
                    dstPtrs[dstIdx][i] = static_cast<T>(tmp);
                }
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
    for (const auto iter : d->sourceNodes)
        vsapi->freeNode(iter.node);
    delete d;
}

static void VS_CC audioMixCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioMixData> d(new AudioMixData());
    int numSrcNodes = vsapi->propNumElements(in, "clips");
    int numMatrixWeights = vsapi->propNumElements(in, "matrix");
    int64_t channels_out = vsapi->propGetInt(in, "channels_out", 0, nullptr);

    std::bitset<64> tmp(channels_out);
    int numOutChannels = tmp.count();
    int numSrcChannels = 0;

    for (int i = 0; i < numSrcNodes; i++) {
        VSNodeRef *node = vsapi->propGetNode(in, "clips", std::min(numSrcNodes - 1, i), nullptr);
        const VSAudioFormat *f = vsapi->getAudioInfo(node)->format;
        for (int j = 0; j < f->numChannels; j++) {
            d->sourceNodes.push_back({ (j > 0) ?  vsapi->cloneNodeRef(node) : node, j });
            numSrcChannels++;
        }
    }

    if (numSrcNodes > numSrcChannels) {
        vsapi->setError(out, "AudioMix: cannot have more input nodes than selected input channels");
        for (const auto iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    if (numOutChannels * numSrcChannels != numMatrixWeights) {
        vsapi->setError(out, "AudioMix: the number of matrix weights must equal (input channels * output channels)");
        for (const auto iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    const char *err = nullptr;

    d->ai = *vsapi->getAudioInfo(d->sourceNodes[0].node);
    for (size_t i = 0; i < d->sourceNodes.size(); i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->sourceNodes[i].node);
        if (!(ai->format->channelLayout & (static_cast<int64_t>(1) << d->sourceNodes[i].idx))) {
            err = "AudioMix: specified channel is not present in input";
            break;
        }
        if (ai->numSamples != d->ai.numSamples || ai->sampleRate != d->ai.sampleRate || ai->format->bitsPerSample != d->ai.format->bitsPerSample || ai->format->sampleType != d->ai.format->sampleType) {
            err = "AudioMix: all inputs must have the same length, samplerate, bits per sample and sample type";
            break;
        }
        // recalculate channel number to a simple index (add as a vsapi function?)
        int idx = 0;
        for (int j = 0; j < d->sourceNodes[i].idx; j++)
            if (ai->format->channelLayout & (static_cast<int64_t>(1) << j))
                idx++;
        d->ai.numSamples = std::max(d->ai.numSamples, ai->numSamples);
        for (int j = 0; j < numOutChannels; j++)
            d->sourceNodes[i].weights.push_back(vsapi->propGetFloat(in, "matrix", j * numSrcChannels + i, nullptr));
        d->sourceNodes[i].numFrames = ai->numFrames;
        d->sourceNodes[i].idx = idx;
    }

    d->ai.format = vsapi->queryAudioFormat(d->ai.format->sampleType, d->ai.format->bitsPerSample, channels_out, core);
    if (!d->ai.format) {
        err = "AudioMix: invalid output channnel configuration";
    }

    if (err) {
        vsapi->setError(out, err);
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    std::set<VSNodeRef *> nodeSet;

    for (const auto &iter : d->sourceNodes)
        nodeSet.insert(iter.node);

    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    if (d->ai.format->sampleType == stFloat)
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, 1, audioMixGetFrame<float>, audioMixFree, fmParallel, 0, d.get(), core);
    else if (d->ai.format->bytesPerSample == 2)
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, 1, audioMixGetFrame<int16_t>, audioMixFree, fmParallel, 0, d.get(), core);
    else
        vsapi->createAudioFilter(out, "AudioMix", &d->ai, 1, audioMixGetFrame<int32_t>, audioMixFree, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// ShuffleChannels

struct ShuffleChannelsDataNode {
    VSNodeRef *node;
    int idx;
    int dstIdx;
    int numFrames;

    inline bool operator<(const ShuffleChannelsDataNode &other) const {
        return dstIdx < other.dstIdx;
    }
};

struct ShuffleChannelsData {
    std::vector<VSNodeRef *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<ShuffleChannelsDataNode> sourceNodes;
    VSAudioInfo ai;
};

static const VSFrameRef *VS_CC shuffleChannelsGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShuffleChannelsData *d = reinterpret_cast<ShuffleChannelsData *>(*instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSFrameRef *dst = nullptr;
        int dstLength = std::min<int64_t>(d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame), d->ai.format->samplesPerFrame);
        for (int idx = 0; idx < d->sourceNodes.size(); idx++) {
            const VSFrameRef *src = vsapi->getFrameFilter(n, d->sourceNodes[idx].node, frameCtx);;
            int srcLength = (n < d->sourceNodes[idx].numFrames) ? vsapi->getFrameLength(src) : 0;
            int copyLength = std::min(dstLength, srcLength);
            int zeroLength = dstLength - copyLength;
            if (!dst)
                dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, dstLength, src, core);
            if (copyLength > 0)
                memcpy(vsapi->getWritePtr(dst, idx), vsapi->getReadPtr(src, d->sourceNodes[idx].idx), copyLength * d->ai.format->bytesPerSample);
            if (zeroLength > 0)
                memset(vsapi->getWritePtr(dst, idx) + copyLength * d->ai.format->bytesPerSample, 0, zeroLength * d->ai.format->bytesPerSample);
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
    int numSrcNodes = vsapi->propNumElements(in, "clip");
    int numSrcChannels = vsapi->propNumElements(in, "channels_in");
    int numDstChannels = vsapi->propNumElements(in, "channels_out");

    if (numSrcChannels != numDstChannels) {
        vsapi->setError(out, "ShuffleChannels: must have the same number of input and output channels");
        return;
    }

    if (numSrcNodes > numSrcChannels) {
        vsapi->setError(out, "ShuffleChannels: cannot have more input nodes than selected input channels");
        return;
    }

    uint64_t channelLayout = 0;

    for (int i = 0; i < numSrcChannels; i++) {
        int channel = int64ToIntS(vsapi->propGetInt(in, "channels_in", i, nullptr));
        int dstChannel = int64ToIntS(vsapi->propGetInt(in, "channels_out", i, nullptr));
        channelLayout |= (static_cast<uint64_t>(1) << dstChannel);
        VSNodeRef *node = vsapi->propGetNode(in, "clip", std::min(numSrcNodes - 1, i), nullptr);
        d->sourceNodes.push_back({ node, channel, dstChannel });
    }

    std::sort(d->sourceNodes.begin(), d->sourceNodes.end());

    const char *err = nullptr;

    d->ai = *vsapi->getAudioInfo(d->sourceNodes[0].node);
    for (size_t i = 0; i < d->sourceNodes.size(); i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->sourceNodes[i].node);
        if (ai->sampleRate != d->ai.sampleRate || ai->format->bitsPerSample != d->ai.format->bitsPerSample || ai->format->sampleType != d->ai.format->sampleType) {
            err = "ShuffleChannels: all inputs must have the same samplerate, bits per sample and sample type";
            break;
        }
        // recalculate channel number to a simple index (add as a vsapi function?)
        if (d->sourceNodes[i].idx < 0) {
            d->sourceNodes[i].idx = (-d->sourceNodes[i].idx) - 1;
            if (ai->format->numChannels <= d->sourceNodes[i].idx) {
                err = "ShuffleChannels: specified channel is not present in input";
                break;
            }
        } else {
            if ((d->sourceNodes[i].idx > 0) && !(ai->format->channelLayout & (static_cast<int64_t>(1) << d->sourceNodes[i].idx))) {
                err = "ShuffleChannels: specified channel is not present in input";
                break;
            }
            int idx = 0;
            for (int j = 0; j < d->sourceNodes[i].idx; j++)
                if (ai->format->channelLayout & (static_cast<int64_t>(1) << j))
                    idx++;
            d->sourceNodes[i].idx = idx;
        }
        d->sourceNodes[i].numFrames = ai->numFrames;
        d->ai.numSamples = std::max(d->ai.numSamples, ai->numSamples);
    }

    d->ai.format = vsapi->queryAudioFormat(d->ai.format->sampleType, d->ai.format->bitsPerSample, channelLayout, core);

    if (!d->ai.format)
        err = "ShuffleChannels: invalid output channnel configuration";
    else if (d->ai.format->numChannels != numDstChannels)
        err = "ShuffleChannels: output channel specified twice";

    if (err) {
        vsapi->setError(out, err);
        for (const auto &iter : d->sourceNodes)
            vsapi->freeNode(iter.node);
        return;
    }

    std::set<VSNodeRef *> nodeSet;

    for (const auto &iter : d->sourceNodes)
        nodeSet.insert(iter.node);

    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    vsapi->createAudioFilter(out, "ShuffleChannels", &d->ai, 1, shuffleChannelsGetFrame, shuffleChannelsFree, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// SplitChannels

struct SplitChannelsData {
    std::vector<VSAudioInfo> ai;
    VSNodeRef *node;
    int numChannels;
};

static const VSFrameRef *VS_CC splitChannelsGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SplitChannelsData *d = reinterpret_cast<SplitChannelsData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int outIdx = vsapi->getOutputIndex(frameCtx);
        int length = vsapi->getFrameLength(src);
        VSFrameRef *dst = vsapi->newAudioFrame(d->ai[outIdx].format, d->ai[outIdx].sampleRate, length, src, core);
        memcpy(vsapi->getWritePtr(dst, 0), vsapi->getReadPtr(src, outIdx), d->ai[outIdx].format->bytesPerSample * length);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC splitChannelsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    SplitChannelsData *d = reinterpret_cast<SplitChannelsData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC splitChannelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SplitChannelsData> d(new SplitChannelsData());
    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    VSAudioInfo ai = *vsapi->getAudioInfo(d->node);
    uint64_t channelLayout = ai.format->channelLayout;
    d->numChannels = ai.format->numChannels;
    d->ai.reserve(d->numChannels);
    size_t index = 0;
    for (int i = 0; i < d->numChannels; i++) {
        while (!(channelLayout & (static_cast<uint64_t>(1) << index)))
            index++;
        ai.format = vsapi->queryAudioFormat(ai.format->sampleType, ai.format->bitsPerSample, (static_cast<uint64_t>(1) << index++), core);
        d->ai.push_back(ai);
    }

    vsapi->createAudioFilter(out, "SplitChannels", d->ai.data(), d->numChannels, splitChannelsGetFrame, splitChannelsFree, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// AssumeSampleRate

typedef struct {
    VSNodeRef *node;
} AssumeSampleRateData;

static const VSFrameRef *VS_CC assumeSampleRateGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AssumeSampleRateData *d = reinterpret_cast<AssumeSampleRateData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n, d->node, frameCtx);
    }

    return 0;
}

static void VS_CC assumeSampleRateCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AssumeSampleRateData> d(new AssumeSampleRateData());
    bool hassamplerate = false;
    bool hassrc = false;
    int err;

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    VSAudioInfo ai = *vsapi->getAudioInfo(d->node);

    ai.sampleRate = int64ToIntS(vsapi->propGetInt(in, "samplerate", 0, &err));
    if (!err)
        hassamplerate = true;

    VSNodeRef *src = vsapi->propGetNode(in, "src", 0, &err);

    if (!err) {
        const VSVideoInfo *vi = vsapi->getVideoInfo(src);
        ai.sampleRate = vsapi->getAudioInfo(d->node)->sampleRate;
        vsapi->freeNode(src);
        hassrc = true;
    }

    if (hassamplerate == hassrc) {
        vsapi->freeNode(d->node);
        RETERROR("AssumeSampleRate: need to specify source clip or samplerate");
    }

    if (ai.sampleRate < 1) {
        vsapi->freeNode(d->node);
        RETERROR("AssumeSampleRate: invalid samplerate specified");
    }

    vsapi->createAudioFilter(out, "AssumeSampleRate", &ai, 1, assumeSampleRateGetframe, templateNodeFree<AssumeSampleRateData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// BlankAudio

typedef struct {
    VSFrameRef *f;
    VSAudioInfo ai;
    bool keep;
} BlankAudioData;

static const VSFrameRef *VS_CC blankAudioGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BlankAudioData *d = reinterpret_cast<BlankAudioData *>(*instanceData);

    if (activationReason == arInitial) {
        VSFrameRef *frame = NULL;
        if (!d->f) {
            int samples = std::min<int>(d->ai.format->samplesPerFrame, d->ai.numSamples - n * (int64_t)d->ai.format->samplesPerFrame);
            frame = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, samples, NULL, core);
            for (int channel = 0; channel < d->ai.format->numChannels; channel++)
                memset(vsapi->getWritePtr(frame, channel), 0, samples * d->ai.format->bytesPerSample);
        }

        if (d->keep) {
            if (frame)
                d->f = frame;
            return vsapi->cloneFrameRef(d->f);
        } else {
            return frame;
        }
    }

    return 0;
}

static void VS_CC blankAudioFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BlankAudioData *d = reinterpret_cast<BlankAudioData *>(instanceData);
    vsapi->freeFrame(d->f);
    delete d;
}

static void VS_CC blankAudioCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BlankAudioData> d(new BlankAudioData());

    int err;

    int64_t channels = vsapi->propGetInt(in, "channels", 0, &err);
    if (err)
        channels = (1 << vsacFrontLeft) | (1 << vsacFrontRight);

    int bits = int64ToIntS(vsapi->propGetInt(in, "bits", 0, &err));
    if (err)
        bits = 16;

    bool isfloat = !!vsapi->propGetInt(in, "isfloat", 0, &err);

    d->keep = !!vsapi->propGetInt(in, "keep", 0, &err);

    d->ai.sampleRate = int64ToIntS(vsapi->propGetInt(in, "samplerate", 0, &err));
    if (err)
        d->ai.sampleRate = 44100;

    d->ai.numSamples = vsapi->propGetInt(in, "length", 0, &err);
    if (err)
        d->ai.numSamples = static_cast<int64_t>(d->ai.sampleRate) * 60 * 60;

    if (d->ai.sampleRate <= 0)
        RETERROR("BlankAudio: invalid sample rate");

    if (d->ai.numSamples <= 0)
        RETERROR("BlankAudio: invalid length");

    d->ai.format = vsapi->queryAudioFormat(isfloat ? stFloat : stInteger, bits, channels, core);

    if (!d->ai.format)
        RETERROR("BlankAudio: invalid format");

    vsapi->createAudioFilter(out, "BlankAudio", &d->ai, 1, blankAudioGetframe, blankAudioFree, d->keep ? fmUnordered : fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// TestAudio

typedef struct {
    VSAudioInfo ai;
} TestAudioData;

static const VSFrameRef *VS_CC testAudioGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TestAudioData *d = reinterpret_cast<TestAudioData *>(*instanceData);

    if (activationReason == arInitial) {
        int samples = std::min<int64_t>(d->ai.format->samplesPerFrame, d->ai.numSamples - n * static_cast<int64_t>(d->ai.format->samplesPerFrame));
        int64_t startSample = n * (int64_t)d->ai.format->samplesPerFrame;
        VSFrameRef *frame = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, samples, NULL, core);
        for (int channel = 0; channel < d->ai.format->numChannels; channel++) {
            uint16_t *w = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, channel));
            for (int i = 0; i < samples; i++)
                w[i] = (startSample + i) % 0xFFFF;
        }
        return frame;
    }

    return 0;
}

static void VS_CC testAudioFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TestAudioData *d = reinterpret_cast<TestAudioData *>(instanceData);
    delete d;
}

static void VS_CC testAudioCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TestAudioData> d(new TestAudioData());

    int err;

    int64_t channels = vsapi->propGetInt(in, "channels", 0, &err);
    if (err)
        channels = (1 << vsacFrontLeft) | (1 << vsacFrontRight);

    int bits = int64ToIntS(vsapi->propGetInt(in, "bits", 0, &err));
    if (err)
        bits = 16;

    if (bits != 16)
        RETERROR("TestAudio: bits must be 16!");

    bool isfloat = !!vsapi->propGetInt(in, "isfloat", 0, &err);

    d->ai.sampleRate = int64ToIntS(vsapi->propGetInt(in, "samplerate", 0, &err));
    if (err)
        d->ai.sampleRate = 44100;

    d->ai.numSamples = vsapi->propGetInt(in, "length", 0, &err);
    if (err)
        d->ai.numSamples = static_cast<int64_t>(d->ai.sampleRate) * 60 * 60;

    if (d->ai.sampleRate <= 0)
        RETERROR("TestAudio: invalid sample rate");

    if (d->ai.numSamples <= 0)
        RETERROR("TestAudio: invalid length");

    d->ai.format = vsapi->queryAudioFormat(isfloat ? stFloat : stInteger, bits, channels, core);

    if (!d->ai.format)
        RETERROR("TestAudio: invalid format");

    vsapi->createAudioFilter(out, "TestAudio", &d->ai, 1, testAudioGetframe, testAudioFree, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void VS_CC audioInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("AudioTrim", "clip:anode;first:int:opt;last:int:opt;length:int:opt;", audioTrimCreate, 0, plugin);
    registerFunc("AudioSplice", "clips:anode[];", audioSpliceCreate, 0, plugin);
    registerFunc("AudioLoop", "clip:anode;times:int:opt;", audioLoopCreate, 0, plugin);
    registerFunc("AudioReverse", "clip:anode;", audioReverseCreate, 0, plugin);
    registerFunc("AudioMix", "clips:anode[];matrix:float[];channels_out:int;", audioMixCreate, 0, plugin);
    registerFunc("ShuffleChannels", "clip:anode[];channels_in:int[];channels_out:int[];", shuffleChannelsCreate, 0, plugin);
    registerFunc("SplitChannels", "clip:anode;", splitChannelsCreate, 0, plugin);
    registerFunc("AssumeSampleRate", "clip:anode;src:anode:opt;samplerate:int:opt;", assumeSampleRateCreate, 0, plugin);
    registerFunc("BlankAudio", "channels:int:opt;bits:int:opt;isfloat:int:opt;samplerate:int:opt;length:int:opt;keep:int:opt;", blankAudioCreate, 0, plugin);
    registerFunc("TestAudio", "channels:int:opt;bits:int:opt;isfloat:int:opt;samplerate:int:opt;length:int:opt;", testAudioCreate, 0, plugin);
}
