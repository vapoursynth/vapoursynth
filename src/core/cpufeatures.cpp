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

#include <stdint.h>
#include "cpufeatures.h"

#ifdef VS_TARGET_CPU_X86
extern "C" void vs_cpuid_wrapper(uint32_t *ecx, uint32_t *edx);

void getCPUFeatures(CPUFeatures *cpuFeatures) {
    uint32_t ecx = 0;
    uint32_t edx = 0;
    vs_cpuid_wrapper(&ecx, &edx);
    cpuFeatures->can_run_vs = !!(edx & (1 << 26));
    cpuFeatures->sse3 = !!(ecx & 1);
    cpuFeatures->ssse3 = !!(ecx & (1 << 9));
    cpuFeatures->sse4_1 = !!(ecx & (1 << 19));
    cpuFeatures->sse4_2 = !!(ecx & (1 << 20));
    cpuFeatures->fma3 = !!(ecx & (1 << 12));
    cpuFeatures->avx = !!(ecx & (1 << 28));
}
#elif defined(VS_TARGET_OS_LINUX)
#include <sys/auxv.h>

void getCPUFeatures(CPUFeatures *cpuFeatures) {
    unsigned long long hwcap = getauxval(AT_HWCAP);

#ifdef VS_TARGET_CPU_ARM
    cpuFeatures->half_fp = !!(hwcap & HWCAP_ARM_HALF);
    cpuFeatures->edsp = !!(hwcap & HWCAP_ARM_EDSP);
    cpuFeatures->iwmmxt = !!(hwcap & HWCAP_ARM_IWMMXT);
    cpuFeatures->neon = !!(hwcap & HWCAP_ARM_NEON);
    cpuFeatures->fast_mult = !!(hwcap & HWCAP_ARM_FAST_MULT);
    cpuFeatures->idiv_a = !!(hwcap & HWCAP_ARM_IDIVA);
#elif defined(VS_TARGET_CPU_POWERPC)
    cpuFeatures->altivec = !!(hwcap & PPC_FEATURE_HAS_ALTIVEC);
    cpuFeatures->spe = !!(hwcap & PPC_FEATURE_HAS_SPE);
    cpuFeatures->efp_single = !!(hwcap & PPC_FEATURE_HAS_EFP_SINGLE);
    cpuFeatures->efp_double = !!(hwcap & PPC_FEATURE_HAS_EFP_DOUBLE);
    cpuFeatures->dfp = !!(hwcap & PPC_FEATURE_HAS_DFP);
    cpuFeatures->vsx = !!(hwcap & PPC_FEATURE_HAS_VSX);
#else
#error Do not know how to get CPU features on Linux.
#endif
}
#else
#error Do not know how to get CPU features.
#endif
