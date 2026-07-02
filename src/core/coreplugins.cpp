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

#include "internalfilters.h"
#include "version.h"
#include "VSHelper4.h"

//////////////////////////////////////////
// Plugin entry point

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(VSH_STD_PLUGIN_ID, "std", "VapourSynth Core Functions", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, pcModifiable, plugin);
    exprInitialize(plugin, vspapi);
    genericInitialize(plugin, vspapi);
    lutInitialize(plugin, vspapi);
    boxBlurInitialize(plugin, vspapi);
    averageFramesInitialize(plugin, vspapi);
    mergeInitialize(plugin, vspapi);
    reorderInitialize(plugin, vspapi);
    audioInitialize(plugin, vspapi);
    stdlibInitialize(plugin, vspapi);
}
