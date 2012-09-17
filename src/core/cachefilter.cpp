//  Copyright (c) 2012 Fredrik Mellbin
//
//  This file is part of VapourSynth.
//
//  VapourSynth is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as
//  published by the Free Software Foundation, either version 3 of the
//  License, or (at your option) any later version.
//
//  VapourSynth is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with VapourSynth.  If not, see <http://www.gnu.org/licenses/>.

#include <QtCore/QtCore>
#include "vscore.h"
#include "cachefilter.h"

VSCache::CacheAction VSCache::recommendSize() {
    // fixme, constants pulled out of my ass
    int total = hits + nearMiss + farMiss;

    if (total == 0)
        return caClear;

    if (total < 30)
        return caNoChange; // not enough requests to know what to do so keep it this way

    if ((float)nearMiss / total > 0.2) // growing the cache would be beneficial
        return caGrow;
    else if ((float)farMiss / total > 0.9) // probably a linear scan, no reason to waste space here
        return caShrink;

    return caNoChange; // probably fine the way it is
}

inline VSCache::VSCache(int maxSize, int maxHistorySize)
    : maxSize(maxSize), maxHistorySize(maxHistorySize) {
    clear();
}

inline void VSCache::clear() {
    hash.clear();
    first = NULL;
    last = NULL;
    weakpoint = NULL;
    currentSize = 0;
    historySize = 0;
    clearStats();
}

inline void VSCache::clearStats() {
    hits = 0;
    nearMiss = 0;
    farMiss = 0;
}


inline PVideoFrame VSCache::object(const int key) const {
    return const_cast<VSCache *>(this)->relink(key);
}


inline PVideoFrame VSCache::operator[](const int key) const {
    return object(key);
}


inline bool VSCache::remove(const int key) {
    QHash<int, Node>::iterator i = hash.find(key);

    if (QHash<int, Node>::const_iterator(i) == hash.constEnd()) {
        return false;
    } else {
        unlink(*i);
        return true;
    }
}


bool VSCache::insert(const int akey, const PVideoFrame &aobject) {
    Q_ASSERT(aobject);
    Q_ASSERT(akey >= 0);
    remove(akey);
    trim(maxSize - 1, maxHistorySize);
    Node sn(aobject);
    QHash<int, Node>::iterator i = hash.insert(akey, sn);
    currentSize++;
    Node *n = &i.value();
    n->key = i.key();

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
    // first adjust the number of cached frames
    while (currentSize > max) {
        if (!weakpoint)
            weakpoint = last;
        else
            weakpoint = weakpoint->prevNode;

        if (weakpoint)
            weakpoint->frame.clear();

        currentSize--;
        historySize++;
    }

    // remove history until the tail is small enough
    Node *n = last;

    while (n && historySize > maxHistory) {
        Node *u = n;
        n = n->prevNode;
        unlink(*u);
    }
}

// cache filter start

class CacheInstance {
public:
    VSCache cache;
    const VSNodeRef *clip;
    VSNode *node;
    bool fixedsize;
    CacheInstance(const VSNodeRef *clip, VSNode *node) : cache(20, 20), clip(clip), node(node), fixedsize(false) { }
};

static void VS_CC cacheInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    const VSNodeRef *video = vsapi->propGetNode(in, "clip", 0, 0);
    CacheInstance *c = new CacheInstance(video, node);
    int err;
    int fixed = vsapi->propGetInt(in, "fixed", 0, &err);

    if (!err)
        c->fixedsize = (bool)fixed;

    int size = vsapi->propGetInt(in, "size", 0, &err);

    if (!err && size > 0)
        c->cache.setMaxFrames(size);

    *instanceData = c;
    vsapi->setVideoInfo(vsapi->getVideoInfo(video), node);
    core->caches.append(node);
}

static const VSFrameRef *VS_CC cacheGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = (CacheInstance *) * instanceData;

    if (activationReason == arInitial) {
        if (n == cCacheTick) {
            if (!c->fixedsize)
                switch (c->cache.recommendSize()) {
                case VSCache::caClear:
                    c->cache.clear();
                case VSCache::caNoChange:
                    return NULL;
                case VSCache::caGrow:
                    c->cache.setMaxFrames(c->cache.getMaxFrames() + 3);
                    return NULL;
                case VSCache::caShrink:
                    c->cache.setMaxFrames(qMax(c->cache.getMaxFrames() - 1, 1));
                    return NULL;
                }
        } else if (n == cNeedMemory) {
            if (!c->fixedsize)
                switch (c->cache.recommendSize()) {
                case VSCache::caClear:
                    c->cache.clear();
                    return NULL;
                case VSCache::caGrow:
                    return NULL;
                case VSCache::caShrink:
                    c->cache.setMaxFrames(qMax(c->cache.getMaxFrames() - 2, 1));
                    return NULL;
                case VSCache::caNoChange:
                    c->cache.setMaxFrames(qMax(c->cache.getMaxFrames() - 1, 1));
                    return NULL;
                }
        } else {
            PVideoFrame f(c->cache[n]);

            if (f)
                return new VSFrameRef(f);

            vsapi->requestFrameFilter(n, c->clip, frameCtx);
            return NULL;
        }
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *r = vsapi->getFrameFilter(n, c->clip, frameCtx);
        c->cache.insert(n, r->frame);
        return r;
    }

    return NULL;
}

static void VS_CC cacheFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    CacheInstance *c = (CacheInstance *)instanceData;
    vsapi->freeNode(c->clip);
    core->caches.removeOne(c->node);
    delete c;
}

static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const VSNodeRef *cref = vsapi->createFilter(in, out, "Cache", cacheInit, cacheGetframe, cacheFree, fmUnordered, nfNoCache, userData, core);
    vsapi->propSetNode(out, "clip", cref, 0);
    vsapi->freeNode(cref);
}

extern "C" void VS_CC cacheInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("Cache", "clip:clip;size:int:opt;fixed:int:opt;", &createCacheFilter, NULL, plugin);
}
