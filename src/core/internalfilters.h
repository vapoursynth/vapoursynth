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

#include <string>
#include "VapourSynth4.h"

void stdlibInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void internalFiltersInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void mergeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void reorderInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void audioInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void audioResamplingInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void exprInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void genericInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void lutInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void boxBlurInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void averageFramesInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
void gpuTransferInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi);

/* Builds a GPU resize node into out and returns true, or returns false with the decline
   reason after touching nothing, in which case the caller runs the scalar graph. */
bool createGPUResize(const VSMap *in, VSMap *out, const char *kernelName, bool deinterlace,
    VSCore *core, const VSAPI *vsapi, std::string &decline);

/* Reports the plan the compute path would run for these arguments and properties without
   touching a device; registered by vsresize.cpp so tests can pin planner decisions. */
void VS_CC gpuResizePlanDebug(const VSMap *in, VSMap *out, void *userData, VSCore *core,
    const VSAPI *vsapi);


#endif // INTERNALFILTERS_H
