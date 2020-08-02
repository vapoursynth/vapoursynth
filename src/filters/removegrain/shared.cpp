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

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.removegrainvs", "rgvs", "RemoveGrain VapourSynth Port", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("RemoveGrain", "clip:vnode;mode:int[];", "clip:vnode;", removeGrainCreate, nullptr, plugin);
    vspapi->registerFunction("Repair", "clip:vnode;repairclip:vnode;mode:int[];", "clip:vnode;", repairCreate, nullptr, plugin);
    vspapi->registerFunction("Clense", "clip:vnode;previous:vnode:opt;next:vnode:opt;planes:int[]:opt;", "clip:vnode;", clenseCreate, reinterpret_cast<void *>(cmNormal), plugin);
    vspapi->registerFunction("ForwardClense", "clip:vnode;planes:int[]:opt;", "clip:vnode;", clenseCreate, reinterpret_cast<void *>(cmForward), plugin);
    vspapi->registerFunction("BackwardClense", "clip:vnode;planes:int[]:opt;", "clip:vnode;", clenseCreate, reinterpret_cast<void *>(cmBackward), plugin);
    vspapi->registerFunction("VerticalCleaner", "clip:vnode;mode:int[];", "clip:vnode;", verticalCleanerCreate, nullptr, plugin);
}
