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

#ifndef CACHEFILTER_H
#define CACHEFILTER_H

#include "vscore.h"

/*
class CacheInstance {
public:
    int lastN = -1;
    int numThreads = 0;

    CacheInstance(VSNode *clip, VSCore *core, bool fixedSize) {}

    void addCache(VSNode *clip) {
        std::lock_guard<std::mutex> lock(core->cacheLock);
        assert(clip);
        node = clip;
        core->caches.insert(node);
    }

    void removeCache() {
        std::lock_guard<std::mutex> lock(core->cacheLock);
        core->caches.erase(node);
    }
};

void VS_CC cacheInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
*/
#endif // CACHEFILTER_H
