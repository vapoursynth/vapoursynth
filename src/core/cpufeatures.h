/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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
#elif defined(VS_TARGET_CPU_ARM)
    // On ARM, VFP-D16+ (16 double registers or more) is required.
    char half_fp;
    char edsp;
    char iwmmxt;
    char neon;
    char fast_mult;
    char idiv_a;
#elif defined(VS_TARGET_CPU_POWERPC)
    // On PowerPC, FPU and MMU are required.
    char altivec;
    char spe;
    char efp_single;
    char efp_double;
    char dfp;
    char vsx;
#else
#error No VS_TARGET_CPU_* defined/handled!
#endif
} CPUFeatures;

#ifdef __cplusplus
#define CPU_FEATURES_EXTERN_C extern "C"
#else
#define CPU_FEATURES_EXTERN_C
#endif

CPU_FEATURES_EXTERN_C void getCPUFeatures(CPUFeatures *cpuFeatures);
