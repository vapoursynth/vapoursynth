/*
* Copyright (c) 2012-2021 Fredrik Mellbin
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

#include "VapourSynth4.h"
#include "VapourSynth3.h"

/*

    Note that the version is stored in several places, in addition to version.h it's also in:
    src/cython/vapoursynth.pyx (__version__)
    setup.py (version = "" near the bottom)
    configure.ac (number on first line in [])
    installer/vsinstaller.iss (Version define at top)
    installer/setup.py (CURRENT_RELEASE at the top)
    doc/conf.py (version = '' near the top)

*/

#define XSTR(x) STR(x)
#define STR(x) #x
#define VAPOURSYNTH_CORE_VERSION 57
#define VAPOURSYNTH_INTERNAL_PLUGIN_VERSION VS_MAKE_VERSION(VAPOURSYNTH_CORE_VERSION, 0)
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
    "Copyright (c) 2012-2021 Fredrik Mellbin\n" \
    "Core R" XSTR(VAPOURSYNTH_CORE_VERSION) "\n" \
    "API R" XSTR(VAPOURSYNTH_API_MAJOR) "." XSTR(VAPOURSYNTH_API_MINOR) "\n" \
    "API R" XSTR(VAPOURSYNTH3_API_MAJOR) "." XSTR(VAPOURSYNTH3_API_MINOR) "\n" \
    VS_OPTIONS_TEXT
