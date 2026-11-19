/*
* Copyright (c) 2026 Fredrik Mellbin
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

#ifndef VSTILED_H
#define VSTILED_H

#include "VapourSynth4.h"
#include <string>
#include <vector>

/* What the core keeps of the description a tiled filter, a frame map or a plane map is created
   from, as far as the three have it in common; vstiled.cpp has the rest of each. A node made from
   one (VSNode::getFusable) is what a tiled node can make part of its tree instead of requesting
   its frames. It is the node's instanceData as well, and immutable once the node exists. */
struct VSFusableFilter {
    enum class Kind { Tiled, FrameMap, PlaneMap };

    const Kind kind;
    std::vector<VSNode *> inputs; /* the references are the node's dependencies */
    VSFilterBeginFrame beginFrame = nullptr;
    VSFilterFree freeFunc = nullptr;
    void *instanceData = nullptr;

    explicit VSFusableFilter(Kind kind) : kind(kind) {
    }

    virtual ~VSFusableFilter() = default;
};

/* createTiledVideoFilter, createFrameMapVideoFilter and createPlaneMapVideoFilter: validates the
   description and makes the node, whose getFrame is the core's own. Returns null with the reason
   in errorMessage on failure, in which case nothing of the description is freed. */
VSNode *createFusableFilterNode(const char *name, const VSTiledFilterDesc *desc, VSCore *core, std::string &errorMessage);
VSNode *createFusableFilterNode(const char *name, const VSFrameMapFilterDesc *desc, VSCore *core, std::string &errorMessage);
VSNode *createFusableFilterNode(const char *name, const VSPlaneMapFilterDesc *desc, VSCore *core, std::string &errorMessage);

#endif // VSTILED_H
