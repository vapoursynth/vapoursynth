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

/* createTiledVideoFilter: validates the description and makes the node, whose getFrame is the
   core's own. Returns null with the reason in errorMessage on failure, in which case nothing of
   the description is freed. */
VSNode *createTiledFilterNode(const char *name, const VSTiledFilterDesc *desc, VSCore *core, std::string &errorMessage);

/* createFrameMapVideoFilter, in the same way. */
VSNode *createFrameMapFilterNode(const char *name, const VSFrameMapFilterDesc *desc, VSCore *core, std::string &errorMessage);

/* createPlaneMapVideoFilter, in the same way. */
VSNode *createPlaneMapFilterNode(const char *name, const VSPlaneMapFilterDesc *desc, VSCore *core, std::string &errorMessage);

#endif // VSTILED_H
