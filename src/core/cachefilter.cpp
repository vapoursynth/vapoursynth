/*
* Copyright (c) 2012-2016 Fredrik Mellbin
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

#include "cachefilter.h"
#include "VSHelper.h"
#include <string>
#include <algorithm>


VSCache::CacheAction VSCache::recommendSize() {
    // fixme, constants pulled out of my ass
    int total = hits + nearMiss + farMiss;

    if (total == 0)
        return caClear;

    if (total < 30) {
        clearStats();
        return caNoChange; // not enough requests to know what to do so keep it this way
    }

    bool shrink = (nearMiss == 0 && hits == 0 && ((farMiss * 10) / total >= 9));
    bool grow = ((nearMiss * 10) / total >= 1);
#ifdef VS_CACHE_DEBUG
    vsWarning("Cache (%p) stats (%s): %d %d %d %d, size: %d", (void *)this, shrink ? "shrink" : (grow ? "grow" : "keep"), total, farMiss, nearMiss, hits, maxSize);
#endif
    if (grow) { // growing the cache would be beneficial
        clearStats();
        return caGrow;
    } else if (shrink) { // probably a linear scan, no reason to waste space here
        clearStats();
        return caShrink;
    } else {
        clearStats();
        return caNoChange; // probably fine the way it is
    }
}

inline VSCache::VSCache(int maxSize, int maxHistorySize, bool fixedSize)
    : maxSize(maxSize), maxHistorySize(maxHistorySize), fixedSize(fixedSize) {
    clear();
}

inline PVideoFrame VSCache::object(const int key) {
    return this->relink(key);
}


inline PVideoFrame VSCache::operator[](const int key) {
    return object(key);
}

inline bool VSCache::remove(const int key) {
    auto i = hash.find(key);

    if (i == hash.end()) {
        return false;
    } else {
        unlink(i->second);
        return true;
    }
}


bool VSCache::insert(const int akey, const PVideoFrame &aobject) {
    assert(aobject);
    assert(akey >= 0);
    remove(akey);
    trim(maxSize - 1, maxHistorySize);
    auto i = hash.insert(std::make_pair(akey, Node(akey, aobject)));
    currentSize++;
    Node *n = &i.first->second;

    if (first)
        first->prevNode = n;

    n->nextNode = first;
    first = n;

    if (!last)
        last = first;

    trim(maxSize, maxHistorySize);

    return true;
}


void VSCache::trim(int max, int maxHistory) {
    // first adjust the number of cached frames and extra history length
    while (currentSize > max) {
        if (!weakpoint)
            weakpoint = last;
        else
            weakpoint = weakpoint->prevNode;

        if (weakpoint)
            weakpoint->frame.reset();

        currentSize--;
        historySize++;
    }

    // remove history until the tail is small enough
    while (last && historySize > maxHistory) {
        unlink(*last);
    }
}

void VSCache::adjustSize(bool needMemory) {
    if (!fixedSize) {
        if (!needMemory) {
            switch (recommendSize()) {
            case VSCache::caClear:
                clear();
                break;
            case VSCache::caGrow:
                setMaxFrames(getMaxFrames() + 2);
                break;
            case VSCache::caShrink:
                setMaxFrames(std::max(getMaxFrames() - 1, 1));
                break;
            default:;
            }
        } else {
            switch (recommendSize()) {
            case VSCache::caClear:
                clear();
                break;
            case VSCache::caShrink:
                if (getMaxFrames() <= 2)
                    clear();
                setMaxFrames(std::max(getMaxFrames() - 2, 1));
                break;
            case VSCache::caNoChange:
                if (getMaxFrames() <= 1)
                    clear();
                setMaxFrames(std::max(getMaxFrames() - 1, 1));
                break;
            default:;
            }
        }
    }
}

static void VS_CC cacheInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = (CacheInstance *)*instanceData;
    c->node = node;
    vsapi->setVideoInfo(vsapi->getVideoInfo(c->clip), 1, node);
}

// controls how many frames beyond the number of threads is a good margin to catch bigger temporal radius filters that are out of order, just a guess
static const int extraFrames = 7;

static const VSFrameRef *VS_CC cacheGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = static_cast<CacheInstance *>(*instanceData);

    intptr_t *fd = (intptr_t *)frameData;

    if (activationReason == arInitial) {
        PVideoFrame f(c->cache[n]);

        if (f)
            return new VSFrameRef(f);

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
                const VSFrameRef *r = vsapi->getFrameFilter((int)i, c->clip, frameCtx);
                c->cache.insert((int)i, r->frame);
                vsapi->freeFrame(r);
            }
        }

        const VSFrameRef *r = vsapi->getFrameFilter(n, c->clip, frameCtx);
        c->cache.insert(n, r->frame);
        return r;
    }

    return nullptr;
}

static void VS_CC cacheFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = static_cast<CacheInstance *>(instanceData);
    c->removeCache();
    vsapi->freeNode(c->clip);
    delete c;
}

static std::atomic<unsigned> cacheId(1);

static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNodeRef *video = vsapi->propGetNode(in, "clip", 0, nullptr);
    int err;
    bool fixed = !!vsapi->propGetInt(in, "fixed", 0, &err);
    CacheInstance *c = new CacheInstance(video, core, fixed);
    VSCoreInfo ci;
    vsapi->getCoreInfo2(core, &ci);
    c->numThreads = ci.numThreads;

    c->makeLinear = !!(vsapi->getVideoInfo(video)->flags & nfMakeLinear);
    if (vsapi->propGetInt(in, "make_linear", 0, &err))
        c->makeLinear = true;

    int size = int64ToIntS(vsapi->propGetInt(in, "size", 0, &err));

    if (!err && size > 0)
        c->cache.setMaxFrames(size);
    else if (c->makeLinear)
        c->cache.setMaxFrames(std::max((c->numThreads + extraFrames) * 2, 20 + c->numThreads));
    else
        c->cache.setMaxFrames(20 + c->numThreads);

    vsapi->createFilter(in, out, ("Cache" + std::to_string(cacheId++)).c_str(), cacheInit, cacheGetframe, cacheFree, c->makeLinear ? fmUnorderedLinear : fmUnordered, nfNoCache | nfIsCache, c, core);

    c->addCache();
}

void VS_CC cacheInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("Cache", "clip:clip;size:int:opt;fixed:int:opt;make_linear:int:opt;", createCacheFilter, nullptr, plugin);
}
