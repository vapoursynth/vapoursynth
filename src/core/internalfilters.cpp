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
#include "kernel/cpulevel.h"

//////////////////////////////////////////
// Cache compatibility filter, does nothing

static void VS_CC createCacheFilter(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;
    bool makeLinear = !!vsapi->mapGetInt(in, "make_linear", 0, &err);
    if (makeLinear)
        vsapi->logMessage(mtCritical, "Explicitly instantiated a Cache with make_linear set. This is no longer possible and the original clip has been passed through instead which may cause severe issues.", core);
    else
        vsapi->logMessage(mtWarning, "Explicitly instantiated a Cache. This is no longer possible and the original clip has been passed through instead.", core);
    vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clip", 0, nullptr), maAppend);
}

//////////////////////////////////////////
// SetAudio/VideoCache

static void VS_CC setCache(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    int mode = vsapi->mapGetIntSaturated(in, "mode", 0, &err);
    if (!err)
        vsapi->setCacheMode(node, mode);
    int fixedsize = vsapi->mapGetIntSaturated(in, "fixedsize", 0, &err);
    if (err)
        fixedsize = -1;
    int maxsize = vsapi->mapGetIntSaturated(in, "maxsize", 0, &err);
    if (err)
        maxsize = -1;
    int maxhistory = vsapi->mapGetIntSaturated(in, "maxhistory", 0, &err);
    if (err)
        maxhistory = -1;
    vsapi->setCacheOptions(node, fixedsize, maxsize, maxhistory);
    vsapi->freeNode(node);
}

//////////////////////////////////////////
// SetMaxCpu

static void VS_CC setMaxCpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *str = vsapi->mapGetData(in, "cpu", 0, nullptr);
    int level = vs_cpulevel_from_str(str);
    level = vs_set_cpulevel(core, level);
    str = vs_cpulevel_to_str(level);
    vsapi->mapSetData(out, "cpu", str, -1, dtUtf8, maReplace);
}

//////////////////////////////////////////
// Init

void internalFiltersInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Cache", "clip:vnode;size:int:opt;fixed:int:opt;make_linear:int:opt;", "clip:vnode;", createCacheFilter, nullptr, plugin);
    vspapi->registerFunction("SetAudioCache", "clip:anode;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetVideoCache", "clip:vnode;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetMaxCPU", "cpu:data;", "cpu:data;", setMaxCpu, 0, plugin);
}
