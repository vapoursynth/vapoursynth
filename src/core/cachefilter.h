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

#ifndef CACHEFILTER_H
#define CACHEFILTER_H

#include "vscore.h"
#include <unordered_map>
#include <assert.h>

class VSCache {
private:
    struct Node {
        inline Node() : key(-1) {}
        inline Node(int key, const PVideoFrame &frame) : key(key), frame(frame), weakFrame(frame), prevNode(0), nextNode(0) {}
        int key;
        PVideoFrame frame;
        WVideoFrame weakFrame;
        Node *prevNode;
        Node *nextNode;
    };

    Node *first;
    Node *weakpoint;
    Node *last;

    std::unordered_map<int, Node> hash;

    bool fixedSize;

    int maxSize;
    int currentSize;
    int maxHistorySize;
    int historySize;

    int hits;
    int nearMiss;
    int farMiss;

    inline void unlink(Node &n) {
        if (&n == weakpoint)
            weakpoint = weakpoint->nextNode;

        if (n.prevNode)
            n.prevNode->nextNode = n.nextNode;

        if (n.nextNode)
            n.nextNode->prevNode = n.prevNode;

        if (last == &n)
            last = n.prevNode;

        if (first == &n)
            first = n.nextNode;

        if (n.frame)
            currentSize--;
        else
            historySize--;

        hash.erase(n.key);
    }

    inline PVideoFrame relink(const int key) {
        auto i = hash.find(key);

        if (i == hash.end()) {
            farMiss++;
            return PVideoFrame();
        }

        Node &n = i->second;

        if (!n.frame) {
            nearMiss++;
            try {
                n.frame = PVideoFrame(n.weakFrame);
            } catch (std::bad_weak_ptr &) {
                return PVideoFrame();
            }                

            currentSize++;
            historySize--;
        }

        hits++;
        Node *origWeakPoint = weakpoint;

        if (&n == origWeakPoint)
            weakpoint = weakpoint->nextNode;

        if (first != &n) {
            if (n.prevNode)
                n.prevNode->nextNode = n.nextNode;

            if (n.nextNode)
                n.nextNode->prevNode = n.prevNode;

            if (last == &n)
                last = n.prevNode;

            n.prevNode = 0;
            n.nextNode = first;
            first->prevNode = &n;
            first = &n;
        }

        if (!weakpoint) {
            if (currentSize > maxSize) {
                weakpoint = last;
                weakpoint->frame.reset();
            }
        } else if (&n == origWeakPoint || historySize > maxHistorySize) {
            weakpoint = weakpoint->prevNode;
            weakpoint->frame.reset();
        }

        assert(historySize <= maxHistorySize);

        return n.frame;
    }

public:
    enum CacheAction {
        caGrow,
        caNoChange,
        caShrink,
        caClear
    };

    VSCache(int maxSize, int maxHistorySize, bool fixedSize);
    ~VSCache() {
        clear();
    }

    inline int getMaxFrames() const {
        return maxSize;
    }
    inline void setMaxFrames(int m) {
        maxSize = m;
        trim(maxSize, maxHistorySize);
    }
    inline int getMaxHistory() const {
        return maxHistorySize;
    }
    inline void setMaxHistory(int m) {
        maxHistorySize = m;
        trim(maxSize, maxHistorySize);
    }

    inline size_t size() const {
        return hash.size();
    }

    inline void clear() {
        hash.clear();
        first = NULL;
        last = NULL;
        weakpoint = NULL;
        currentSize = 0;
        historySize = 0;
        clearStats();
    }

    inline void clearStats() {
        hits = 0;
        nearMiss = 0;
        farMiss = 0;
    }

    bool insert(const int key, const PVideoFrame &object);
    PVideoFrame object(const int key);
    inline bool contains(const int key) const {
        return hash.count(key) > 0;
    }
    PVideoFrame operator[](const int key);

    bool remove(const int key);



    CacheAction recommendSize();

    void adjustSize(bool needMemory);
private:
    void trim(int max, int maxHistory);

};

class CacheInstance {
public:
    VSCache cache;
    VSNodeRef *clip;
    VSNode *node;
    VSCore *core;
    CacheInstance(VSNodeRef *clip, VSNode *node, VSCore *core, bool fixedSize) : cache(20, 20, fixedSize), clip(clip), node(node), core(core) { }
    void addCache() {
        std::lock_guard<std::mutex> lock(core->cacheLock); 
        core->caches.insert(node);
    }
    void removeCache() {
        std::lock_guard<std::mutex> lock(core->cacheLock);
        core->caches.erase(node);
    }
};

void VS_CC cacheInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);

#endif // CACHEFILTER_H
