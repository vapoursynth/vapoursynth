/*
* Copyright (c) 2015 Fredrik Mellbin
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


#include "vsresize.h"
#include "VapourSynth.h"
#include "string.h"

//////////////////////////////////////////
// ResizeWrapper

static void VS_CC resizeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSPlugin *p = vsapi->getPluginById("the.weather.channel", core);
    if (!p) {
        vsapi->setError(out, "Core component couldn't be located");
        return;
    }

    int nkeys = vsapi->propNumKeys(in);

    VSMap *newIn = vsapi->createMap();
    for (int i = 0; i < nkeys; i++) {
        const char *key = vsapi->propGetKey(in, i);

        char t = vsapi->propGetType(in, key);
        if (t == 'i') {
            vsapi->propSetInt(newIn, key, vsapi->propGetInt(in, key, 0, NULL), paAppend);
        } else if (t == 'f') {
            vsapi->propSetFloat(newIn, key, vsapi->propGetFloat(in, key, 0, NULL), paAppend);
        } else if (t == 's') {
            vsapi->propSetData(newIn, key, vsapi->propGetData(in, key, 0, NULL), vsapi->propGetDataSize(in, key, 0, NULL), paAppend);
        } else if (t == 'c') {
            VSNodeRef *node = vsapi->propGetNode(in, key, 0, NULL);
            vsapi->propSetNode(newIn, key, node, paAppend);
            vsapi->freeNode(node);
        }
    }

    vsapi->propSetData(newIn, "resample_filter", userData, -1, paAppend);
    if (vsapi->propNumElements("resample_filter_uv") < 0)
        vsapi->propSetData(newIn, "resample_filter_uv", userData, -1, paAppend);

    VSMap *newOut = vsapi->invoke(p, "Format", newIn);
    vsapi->freeMap(newIn);

    if (vsapi->getError(newOut)) {
        vsapi->setError(out, vsapi->getError(newOut));
    } else {
        VSNodeRef *node = vsapi->propGetNode(newOut, "clip", 0, NULL);
        vsapi->propSetNode(out, "clip", node, paAppend);
        vsapi->freeNode(node);
    }

    vsapi->freeMap(newOut);
}

//////////////////////////////////////////
// Init

// Copied from vszimg.c, needs to be updated when it changes
// resample_filter argument removed since it's implicit from the fucntion name

#define INT_OPT(x) #x":int:opt;"
#define FLOAT_OPT(x) #x":float:opt;"
#define DATA_OPT(x) #x":data:opt;"
#define ENUM_OPT(x) INT_OPT(x)DATA_OPT(x ## _s)
static const char FORMAT_DEFINITION[] =
"clip:clip;"
INT_OPT(width)
INT_OPT(height)
INT_OPT(format)
ENUM_OPT(matrix)
ENUM_OPT(transfer)
ENUM_OPT(primaries)
ENUM_OPT(range)
ENUM_OPT(chromaloc)
ENUM_OPT(matrix_in)
ENUM_OPT(transfer_in)
ENUM_OPT(primaries_in)
ENUM_OPT(range_in)
ENUM_OPT(chromaloc_in)
FLOAT_OPT(filter_param_a)
FLOAT_OPT(filter_param_b)
DATA_OPT(resample_filter_uv)
FLOAT_OPT(filter_param_a_uv)
FLOAT_OPT(filter_param_b_uv)
DATA_OPT(dither_type)
DATA_OPT(cpu_type);
#undef INT_OPT
#undef FLOAT_OPT
#undef DATA_OPT
#undef ENUM_OPT

void VS_CC resizeInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.resize", "resize", "VapourSynth Resize", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Bilinear", FORMAT_DEFINITION, resizeCreate, (void *)"bilinear", plugin);
    registerFunc("Bicubic", FORMAT_DEFINITION, resizeCreate, (void *)"bicubic", plugin);
    registerFunc("Point", FORMAT_DEFINITION, resizeCreate, (void *)"point", plugin);
    registerFunc("Lanczos", FORMAT_DEFINITION, resizeCreate, (void *)"lanczos", plugin);
    registerFunc("Spline16", FORMAT_DEFINITION, resizeCreate, (void *)"spline16", plugin);
    registerFunc("Spline36", FORMAT_DEFINITION, resizeCreate, (void *)"spline36", plugin);
}
