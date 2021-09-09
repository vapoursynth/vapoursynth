/*
* Copyright (c) 2013-2020 Fredrik Mellbin
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

#ifndef INTERNALFILTERS_H
#define INTERNALFILTERS_H

#include "VapourSynth4.h"

#ifdef VS_USE_MIMALLOC
#   include <mimalloc-override.h>
#endif

void stdlibInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void mergeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void reorderInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void audioInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void textInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void exprInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void genericInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void lutInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void boxBlurInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void resizeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void averageFramesInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);


#endif // INTERNALFILTERS_H
