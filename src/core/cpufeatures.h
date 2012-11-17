/*
* Copyright (c) 2012 Fredrik Mellbin
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

struct CPUFeatures {
    // This is to determine if the cpu is up to the minimum requirements in terms of supported instructions
    // that the VapourSynth core uses.
    // On x86 all features up to sse2 are required.
    bool can_run_vs;
    // x86
    bool sse3;
    bool ssse3;
    bool sse4_1;
    bool sse4_2;
    bool fma3;
    bool avx;
};

void getCPUFeatures(CPUFeatures *cpuFeatures);
