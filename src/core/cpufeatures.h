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
    bool can_run_vs;
#ifdef VS_TARGET_CPU_X86
    // On x86, all features up to sse2 are required.
    bool sse3;
    bool ssse3;
    bool sse4_1;
    bool sse4_2;
    bool fma3;
    bool avx;
#elif defined(VS_TARGET_CPU_ARM)
    // On ARM, VFP-D16+ (16 double registers or more) is required.
    bool half_fp;
    bool edsp;
    bool iwmmxt;
    bool neon;
    bool fast_mult;
    bool idiv_a;
#elif defined(VS_TARGET_CPU_POWERPC)
    // On PowerPC, FPU and MMU are required.
    bool altivec;
    bool spe;
    bool efp_single;
    bool efp_double;
    bool dfp;
    bool vsx;
#else
#error No VS_TARGET_CPU_* defined/handled!
#endif
};

void getCPUFeatures(CPUFeatures *cpuFeatures);
