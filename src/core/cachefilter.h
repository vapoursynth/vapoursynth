/*
* Copyright (c) 2012 Fredrik Mellbin
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
* License along with Libav; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef CACHEFILTER_H
#define CACHEFILTER_H

#include <QtCore/qhash.h>
#include "vscore.h"

class VSCache {
private:
    struct Node {
        inline Node() : key(-1) {}
        inline Node(const PVideoFrame &frame) : key(0), frame(frame), weakFrame(frame.toWeakRef()), prevNode(0), nextNode(0) {}
        int key;
        PVideoFrame frame;
        WVideoFrame weakFrame;
        Node *prevNode;
        Node *nextNode;
    };

    Node *first;
    Node *weakpoint;
    Node *last;

    QHash<int, Node> hash;

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

        hash.remove(n.key);
    }

    inline PVideoFrame relink(const int key) {
        QHash<int, Node>::iterator i = hash.find(key);

        if (QHash<int, Node>::const_iterator(i) == hash.constEnd()) {
            farMiss++;
            return PVideoFrame();
        }

        Node &n = *i;

        if (!n.frame) {
            nearMiss++;
            n.frame = n.weakFrame.toStrongRef();

            if (!n.frame)
                return PVideoFrame();

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
                weakpoint->frame.clear();
            }
        } else if (&n == origWeakPoint || historySize > maxHistorySize) {
            weakpoint = weakpoint->prevNode;
            weakpoint->frame.clear();
        }

        Q_ASSERT(historySize <= maxHistorySize);

        return n.frame;
    }

    Q_DISABLE_COPY(VSCache)

public:
    enum CacheAction {
        caGrow,
        caNoChange,
        caShrink,
        caClear
    };

    VSCache(int maxSize, int maxHistorySize);
    ~VSCache() {
        clear();
    }

    int getMaxFrames() const {
        return maxSize;
    }
    void setMaxFrames(int m) {
        maxSize = m;
        trim(maxSize, maxHistorySize);
    }
    int getMaxHistory() const {
        return maxHistorySize;
    }
    void setMaxHistory(int m) {
        maxHistorySize = m;
        trim(maxSize, maxHistorySize);
    }

    inline int size() const {
        return hash.size();
    }
    inline QList<int> keys() const {
        return hash.keys();
    }

    void clear();
    void clearStats();

    bool insert(const int key, const PVideoFrame &object);
    PVideoFrame object(const int key) const;
    inline bool contains(const int key) const {
        return hash.contains(key);
    }
    PVideoFrame operator[](const int key) const;

    bool remove(const int key);

    CacheAction recommendSize();
private:
    void trim(int max, int maxHistory);

};

#endif // CACHEFILTER_H
