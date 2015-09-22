/*
* Copyright (c) 2012-2015 Fredrik Mellbin
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

#include "VapourSynth.h"

#define XSTR(x) STR(x)
#define STR(x) #x
#define VAPOURSYNTH_CORE_VERSION 28
#if defined(VS_FRAME_GUARD) && !defined(NDEBUG)
#define VS_OPTIONS_TEXT "Options: Frame Guard + Extra Assertions\n"
#elif defined(VS_FRAME_GUARD)
#define VS_OPTIONS_TEXT "Options: Frame Guard\n"
#elif !defined(NDEBUG)
#define VS_OPTIONS_TEXT "Options: Extra Assertions\n"
#else
#define VS_OPTIONS_TEXT "Options: -\n"
#endif
#define VAPOURSYNTH_VERSION_STRING "VapourSynth Video Processing Library\n" \
    "Copyright (c) 2012-2015 Fredrik Mellbin\n" \
    "Core R" XSTR(VAPOURSYNTH_CORE_VERSION) "\n" \
    "API R" XSTR(VAPOURSYNTH_API_MAJOR) "." XSTR(VAPOURSYNTH_API_MINOR) "\n" \
    VS_OPTIONS_TEXT
