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

//////////////////////////////////////////
// AudioMix

struct AudioMixDataDataNode {
    VSNodeRef *node;
    int idx;
    std::vector<float> weights;
};

struct AudioMixData {
    std::vector<VSNodeRef *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<AudioMixDataDataNode> sourceNodes;
    VSAudioInfo ai;
};

static const VSFrameRef *VS_CC audioMixGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioMixData *d = reinterpret_cast<AudioMixData *>(*instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        /*
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSAudioFormat *fi = vsapi->getAudioFrameFormat(src);
        size_t inChannels = fi->numChannels;

        VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, vsapi->getFrameLength(src), src, core);



        


        vsapi->freeFrame(src);
        return dst;
        */
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
    std::unique_ptr<AudioMixData> d(new AudioMixData);
    int numSrcNodes = vsapi->propNumElements(in, "clip");
    int numMatrixWeights = vsapi->propNumElements(in, "matrix");
    int numSrcChannels = vsapi->propNumElements(in, "channels_in");
    int64_t channels_out = vsapi->propGetInt(in, "chanels_out", 0, nullptr);

    if (numSrcNodes > numSrcChannels) {
        vsapi->setError(out, "AudioMix: cannot have more input nodes than selected input channels");
        return;
    }

    std::bitset<64> tmp(channels_out);
    size_t numOutChannels = tmp.count();
    if (numOutChannels * numSrcChannels != numMatrixWeights) {
        vsapi->setError(out, "AudioMix: the number of matrix weights must equal input channels * output channels");
        return;
    }

    for (int i = 0; i < numSrcChannels; i++) {
        int channel = int64ToIntS(vsapi->propGetInt(in, "channels_in", i, nullptr));
        VSNodeRef *node = vsapi->propGetNode(in, "clip", std::min(numSrcNodes - 1, i), nullptr);
        d->sourceNodes.push_back({ node, channel });
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

    d->reqNodes.reserve(nodeSet.size());
    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    vsapi->createAudioFilter(in, out, "AudioMix", &d->ai, 1, audioMixGetframe, audioMixFree, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// ShuffleChannels

struct ShuffleChannelsDataNode {
    VSNodeRef *node;
    int idx;
};

struct ShuffleChannelsData {
    std::vector<VSNodeRef *> reqNodes; // a list of all distinct nodes in sourceNodes to reduce function calls
    std::vector<ShuffleChannelsDataNode> sourceNodes;
    VSAudioInfo ai;
};

static const VSFrameRef *VS_CC shuffleChannelsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ShuffleChannelsData *d = reinterpret_cast<ShuffleChannelsData *>(*instanceData);

    if (activationReason == arInitial) {
        for (const auto &iter : d->reqNodes)
            vsapi->requestFrameFilter(n, iter, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        VSFrameRef *dst = nullptr; 
        int outIdx = 0;
        for (const auto iter : d->sourceNodes) {
            const VSFrameRef *src = vsapi->getFrameFilter(n, iter.node, frameCtx);
            if (!dst)
                dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, vsapi->getFrameLength(src), src, core);
            memcpy(vsapi->getWritePtr(dst, outIdx), vsapi->getReadPtr(src, iter.idx), vsapi->getFrameLength(src) * d->ai.format->bytesPerSample);
            outIdx++;
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
    std::unique_ptr<ShuffleChannelsData> d(new ShuffleChannelsData);
    int numSrcNodes = vsapi->propNumElements(in, "clip");
    int numSrcChannels = vsapi->propNumElements(in, "channels_in");
    int64_t channels_out = vsapi->propGetInt(in, "chanels_out", 0, nullptr);

    if (numSrcNodes > numSrcChannels) {
        vsapi->setError(out, "ShuffleChannels: cannot have more input nodes than selected input channels");
        return;
    }

    std::bitset<64> tmp(channels_out);
    if (tmp.count() != numSrcChannels) {
        vsapi->setError(out, "ShuffleChannels: number of input channels doesn't match number of outputs");
        return;
    }

    for (int i = 0; i < numSrcChannels; i++) {
        int channel = int64ToIntS(vsapi->propGetInt(in, "channels_in", i, nullptr));
        VSNodeRef *node = vsapi->propGetNode(in, "clip", std::min(numSrcNodes - 1, i), nullptr);
        d->sourceNodes.push_back({ node, channel });
    }

    const char *err = nullptr;

    d->ai = *vsapi->getAudioInfo(d->sourceNodes[0].node);
    for (size_t i = 0; i < d->sourceNodes.size(); i++) {
        const VSAudioInfo *ai = vsapi->getAudioInfo(d->sourceNodes[i].node);
        if (!(ai->format->channelLayout & (static_cast<int64_t>(1) << d->sourceNodes[i].idx))) {
            err = "ShuffleChannels: specified channel is not present in input";
            break;
        }
        if (ai->numSamples != d->ai.numSamples || ai->sampleRate != d->ai.sampleRate || ai->format->bitsPerSample != d->ai.format->bitsPerSample || ai->format->sampleType != d->ai.format->sampleType) {
            err = "ShuffleChannels: all inputs must have the same length, samplerate, bits per sample and sample type";
            break;
        }
        // recalculate channel number to a simple index (add as a vsapi function?)
        int idx = 0;
        for (int j = 0; j < d->sourceNodes[i].idx; j++)
            if (ai->format->channelLayout & (static_cast<int64_t>(1) << j))
                idx++;
        d->sourceNodes[i].idx = idx;
    }

    d->ai.format = vsapi->queryAudioFormat(d->ai.format->sampleType, d->ai.format->bitsPerSample, channels_out, core);
    if (!d->ai.format) {
        err = "ShuffleChannels: invalid output channnel configuration";
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

    d->reqNodes.reserve(nodeSet.size());
    for (const auto &iter : nodeSet)
        d->reqNodes.push_back(iter);

    vsapi->createAudioFilter(in, out, "ShuffleChannels", &d->ai, 1, shuffleChannelsGetframe, shuffleChannelsFree, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// SplitChannels

struct SplitChannelsData {
    VSNodeRef *node;
    VSAudioInfo ai;
    int numChannels;
};

static const VSFrameRef *VS_CC splitChannelsGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SplitChannelsData *d = reinterpret_cast<SplitChannelsData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int length = vsapi->getFrameLength(src);
        VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, length, src, core);
        memcpy(vsapi->getWritePtr(dst, 0), vsapi->getReadPtr(src, vsapi->getOutputIndex(frameCtx)), d->ai.format->bytesPerSample * length);
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
    std::unique_ptr<SplitChannelsData> d(new SplitChannelsData);
    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->ai = *vsapi->getAudioInfo(d->node);
    d->numChannels = d->ai.format->numChannels;
    d->ai.format = vsapi->queryAudioFormat(d->ai.format->sampleType, d->ai.format->bitsPerSample, (1 << vsacFrontLeft), core);
    std::vector<VSAudioInfo> aiVec;
    aiVec.reserve(d->numChannels);
    for (int i = 0; i < d->numChannels; i++)
        aiVec.push_back(d->ai);
    vsapi->createAudioFilter(in, out, "SplitChannels", aiVec.data(), d->numChannels, splitChannelsGetframe, splitChannelsFree, fmParallel, 0, d.release(), core);
}

//////////////////////////////////////////
// Init

void VS_CC audioInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    //registerFunc("AudioMix", "clip:anode[];matrix:float[];channels_out:int;", audioMixCreate, 0, plugin);
    registerFunc("ShuffleChannels", "clip:anode[];channels_in:int[];channels_out:int;", shuffleChannelsCreate, 0, plugin);
    registerFunc("SplitChannels", "clip:anode;", splitChannelsCreate, 0, plugin);
}
