/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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

    //bool shrink = (nearMiss == 0 && hits == 0 && ((farMiss*10) / total >= 9));
    //vsWarning("Cache #%d stats (%s): %d %d %d %d, size: %d", id, shrink ? "shrink" : "keep", total, farMiss, nearMiss, hits, getMaxFrames());
    if ((nearMiss*10) / total >= 1) { // growing the cache would be beneficial
        clearStats();
        return caGrow;
    } else if (nearMiss == 0 && hits == 0 && ((farMiss*10) / total >= 9)) { // probably a linear scan, no reason to waste space here

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
                break;;
            }
        }
    }
}

static void VS_CC cacheInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    VSNodeRef *video = vsapi->propGetNode(in, "clip", 0, 0);
    int err;
    int fixed = !!vsapi->propGetInt(in, "fixed", 0, &err);
    CacheInstance *c = new CacheInstance(video, node, core, !!fixed);

    int size = int64ToIntS(vsapi->propGetInt(in, "size", 0, &err));

    if (!err && size > 0)
        c->cache.setMaxFrames(size);

    *instanceData = c;
    vsapi->setVideoInfo(vsapi->getVideoInfo(video), 1, node);

    c->addCache();
}

static const VSFrameRef *VS_CC cacheGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = (CacheInstance *) * instanceData;

    if (activationReason == arInitial) {
        PVideoFrame f(c->cache[n]);

        if (f)
            return new VSFrameRef(f);

        vsapi->requestFrameFilter(n, c->clip, frameCtx);
        return NULL;
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *r = vsapi->getFrameFilter(n, c->clip, frameCtx);
        c->cache.insert(n, r->frame);
        return r;
    }

    return NULL;
}

static void VS_CC cacheFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = (CacheInstance *)instanceData;
    c->removeCache();
    vsapi->freeNode(c->clip);
    delete c;
}

static std::atomic<unsigned> cacheId(1);

static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    vsapi->createFilter(in, out, ("Cache" + std::to_string(cacheId++)).c_str(), cacheInit, cacheGetframe, cacheFree, fmUnordered, nfNoCache, userData, core);
}

void VS_CC cacheInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("Cache", "clip:clip;size:int:opt;fixed:int:opt;", &createCacheFilter, NULL, plugin);
}
