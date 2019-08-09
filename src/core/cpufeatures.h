/*
* Copyright (c) 2012-2017 Fredrik Mellbin
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

#define FLAG_FIELD(name) unsigned short name : 1

typedef struct {
    // This is to determine if the cpu is up to the minimum requirements in terms of supported instructions
    // that the VapourSynth core uses.
    FLAG_FIELD(can_run_vs);
#ifdef VS_TARGET_CPU_X86
    // On x86, all features up to sse2 are required.
    FLAG_FIELD(sse3);
    FLAG_FIELD(ssse3);
    FLAG_FIELD(sse4_1);
    FLAG_FIELD(sse4_2);
    FLAG_FIELD(fma3);
    FLAG_FIELD(avx);
    FLAG_FIELD(avx2);
    FLAG_FIELD(f16c);
    FLAG_FIELD(aes);
    FLAG_FIELD(movbe);
    FLAG_FIELD(popcnt);
    FLAG_FIELD(avx512_f);
    FLAG_FIELD(avx512_cd);
    FLAG_FIELD(avx512_bw);
    FLAG_FIELD(avx512_dq);
    FLAG_FIELD(avx512_vl);
#elif defined(VS_TARGET_CPU_ARM)
    // On ARM, VFP-D16+ (16 double registers or more) is required.
    FLAG_FIELD(half_fp);
    FLAG_FIELD(edsp);
    FLAG_FIELD(iwmmxt);
    FLAG_FIELD(neon);
    FLAG_FIELD(fast_mult);
    FLAG_FIELD(idiv_a);
#elif defined(VS_TARGET_CPU_POWERPC)
    // On PowerPC, FPU and MMU are required.
    FLAG_FIELD(altivec);
    FLAG_FIELD(spe);
    FLAG_FIELD(efp_single);
    FLAG_FIELD(efp_double);
    FLAG_FIELD(dfp);
    FLAG_FIELD(vsx);
#else
    #warning "No VS_TARGET_CPU_* defined/handled!"
#endif
} CPUFeatures;

#ifdef __cplusplus
    #define CPU_FEATURES_EXTERN_C extern "C"
#else
    #define CPU_FEATURES_EXTERN_C
#endif

CPU_FEATURES_EXTERN_C void getCPUFeatures(CPUFeatures *cpuFeatures);

#endif