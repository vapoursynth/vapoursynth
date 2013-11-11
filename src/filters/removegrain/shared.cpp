/*****************************************************************************

        AvsFilterRemoveGrain/Repair16
        Author: Laurent de Soras, 2012
        Modified for VapourSynth by Fredrik Mellbin 2013

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/

#include "shared.h"

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.removegrainvs", "rgvs", "RemoveGrain VapourSynth Port", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("RemoveGrain", "clip:clip;mode:int[];", removeGrainCreate, 0, plugin);
    registerFunc("Repair", "clip:clip;repairclip:clip;mode:int[];", repairCreate, 0, plugin);
    registerFunc("Clense", "clip:clip;previous:clip:opt;next:clip:opt;planes:int[]:opt;", clenseCreate, reinterpret_cast<void *>(cmNormal), plugin);
    registerFunc("ForwardClense", "clip:clip;planes:int[]:opt;", clenseCreate, reinterpret_cast<void *>(cmForward), plugin);
    registerFunc("BackwardClense", "clip:clip;planes:int[]:opt;", clenseCreate, reinterpret_cast<void *>(cmBackward), plugin);
}
