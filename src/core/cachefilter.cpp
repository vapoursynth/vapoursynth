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


#include <string>
#include <algorithm>
#include "VSHelper4.h"
#include "vscore.h"

/*
// controls how many frames beyond the number of threads is a good margin to catch bigger temporal radius filters that are out of order, just a guess
static const int extraFrames = 7;

static const VSFrame *VS_CC cacheGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = static_cast<CacheInstance *>(instanceData);

    intptr_t *fd = (intptr_t *)frameData;

    if (activationReason == arInitial) {
        PVSFrame f = c->cache.object(n);

        if (f) {
            f->add_ref();
            return f.get();
        }

        if (c->makeLinear && n != c->lastN + 1 && n > c->lastN && n < c->lastN + c->numThreads + extraFrames) {
            for (int i = c->lastN + 1; i <= n; i++)
                vsapi->requestFrameFilter(i, c->clip, frameCtx);
            *fd = c->lastN;
        } else {
            vsapi->requestFrameFilter(n, c->clip, frameCtx);
            *fd = -2;
        }

        c->lastN = n;
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        if (*fd >= -1) {
            for (intptr_t i = *fd + 1; i < n; i++) {
                const VSFrame *r = vsapi->getFrameFilter((int)i, c->clip, frameCtx);
                c->cache.insert((int)i, const_cast<VSFrame *>(r));
            }
        }

        const VSFrame *r = vsapi->getFrameFilter(n, c->clip, frameCtx);
        c->cache.insert(n, PVSFrame(const_cast<VSFrame *>(r), true));
        return r;
    }

    return nullptr;
}


static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    static std::atomic<size_t> cacheId(1);
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    int err;
    bool fixed = !!vsapi->mapGetInt(in, "fixed", 0, &err);
    CacheInstance *c = new CacheInstance(node, core, fixed);
    VSCoreInfo ci;
    vsapi->getCoreInfo(core, &ci);
    c->numThreads = ci.numThreads;
    c->makeLinear = !!vsapi->mapGetInt(in, "make_linear", 0, &err);

    int size = vsapi->mapGetIntSaturated(in, "size", 0, &err);

    if (!err && size > 0)
        c->cache.setMaxFrames(size);
    else if (c->makeLinear)
        c->cache.setMaxFrames(std::max((c->numThreads + extraFrames) * 2, 20 + c->numThreads));
    else
        c->cache.setMaxFrames(20);

    if (userData)
        vsapi->createAudioFilter(out, ("AudioCache" + std::to_string(cacheId++)).c_str(), vsapi->getAudioInfo(node), cacheGetframe, cacheFree, c->makeLinear ? fmUnorderedLinear : fmUnordered, nfNoCache, c, core);
    else
        vsapi->createVideoFilter(out, ("VideoCache" + std::to_string(cacheId++)).c_str(), vsapi->getVideoInfo(node), cacheGetframe, cacheFree, c->makeLinear ? fmUnorderedLinear : fmUnordered, nfNoCache, c, core);

    VSNode *self = vsapi->mapGetNode(out, "clip", 0, nullptr);
    c->addCache(self);
    vsapi->freeNode(self);
}


*/