/*
* Copyright (c) 2012-2019 Fredrik Mellbin
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

#include <cstring>
#include "cpulevel.h"
#include "../vscore.h"

int vs_get_cpulevel(const struct VSCore *core) {
    return core->getCpuLevel();
}

int vs_set_cpulevel(struct VSCore *core, int level) {
    return core->setCpuLevel(level);
}

int vs_cpulevel_from_str(const char *name) {
    if (!strcmp(name, "none"))
        return VS_CPU_LEVEL_NONE;
#ifdef VS_TARGET_CPU_X86
    else if (!strcmp(name, "sse2"))
        return VS_CPU_LEVEL_SSE2;
    else if (!strcmp(name, "avx2"))
        return VS_CPU_LEVEL_AVX2;
#endif
    else
        return VS_CPU_LEVEL_MAX;
}

const char *vs_cpulevel_to_str(int level) {
    if (level <= VS_CPU_LEVEL_NONE)
        return "none";
#ifdef VS_TARGET_CPU_X86
    else if (level <= VS_CPU_LEVEL_SSE2)
        return "sse2";
    else if (level <= VS_CPU_LEVEL_AVX2)
        return "avx2";
#endif
    else
        return "";
}
