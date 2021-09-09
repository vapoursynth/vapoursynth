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

#ifndef CPULEVEL_H
#define CPULEVEL_H

#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VS_CPU_LEVEL_NONE = 0,
#ifdef VS_TARGET_CPU_X86
    VS_CPU_LEVEL_SSE2 = 1,
    VS_CPU_LEVEL_AVX2 = 2,
#endif
    VS_CPU_LEVEL_MAX = INT_MAX
};

struct VSCore;

int vs_get_cpulevel(const struct VSCore *core);
int vs_set_cpulevel(struct VSCore *core, int level);

int vs_cpulevel_from_str(const char *name);
const char *vs_cpulevel_to_str(int level);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
