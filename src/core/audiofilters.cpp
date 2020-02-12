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

//////////////////////////////////////////
// AudioMix

typedef struct AudioMixData {
    VSNodeRef *node;
    VSAudioInfo ai;
    std::vector<float> coefficients;
    std::vector<int64_t> outChannelOrder;

} AudioMixData;


template<typename T>
static const VSFrameRef *VS_CC audioMixGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioMixData *d = reinterpret_cast<AudioMixData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSAudioFormat *fi = vsapi->getAudioFrameFormat(src);
        size_t inChannels = fi->numChannels;

        VSFrameRef *dst = vsapi->newAudioFrame(d->ai.format, d->ai.sampleRate, vsapi->getFrameLength(src), src, core);



        T maxval = static_cast<T>((static_cast<int64_t>(1) << fi->bitsPerSample) - 1);

        for (int plane = 0; plane < fi->numPlanes; plane++) {

            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                int src_stride = vsapi->getStride(src, plane);
                U * VS_RESTRICT dstp = reinterpret_cast<U *>(vsapi->getWritePtr(dst, plane));
                int dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                const U * VS_RESTRICT lut = reinterpret_cast<const U *>(d->lut);

                for (int hl = 0; hl < h; hl++) {
                    for (int x = 0; x < w; x++)
                        dstp[x] =  lut[std::min(srcp[x], maxval)];

                    dstp += dst_stride / sizeof(U);
                    srcp += src_stride / sizeof(T);
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC audioMixFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    LutData *d = reinterpret_cast<LutData *>(instanceData);
    d->freeNode = vsapi->freeNode;
    delete d;
}

static void VS_CC audioMixCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {

}

//////////////////////////////////////////
// Init

void VS_CC audioInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("AudioMix", "clip:anode[];matrix:int[]:opt;channels_out:int[];", audioMixCreate, 0, plugin);
    registerFunc("ShuffleChannels", "clip:anode[];channels_in:int[];channels_out:int[]:opt;", shuffleChannelsCreate, 0, plugin);
}
