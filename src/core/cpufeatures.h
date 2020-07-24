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

#ifndef CPUFEATURES_H
#define CPUFEATURES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CPUFeatures {
    // This is to determine if the cpu is up to the minimum requirements in terms of supported instructions
    // that the VapourSynth core uses.
    char can_run_vs;
#ifdef VS_TARGET_CPU_X86
    // On x86, all features up to sse2 are required.
    char sse3;
    char ssse3;
    char sse4_1;
    char sse4_2;
    char fma3;
    char avx;
    char avx2;
    char f16c;
    char aes;
    char movbe;
    char popcnt;
    char avx512_f;
    char avx512_cd;
    char avx512_bw;
    char avx512_dq;
    char avx512_vl;
#endif
} CPUFeatures;

const CPUFeatures *getCPUFeatures(void);

#ifdef __cplusplus
}
#endif

#endif