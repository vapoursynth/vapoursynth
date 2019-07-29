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

#include <string.h>

#include "cpufeatures.h"

#ifdef VS_TARGET_CPU_X86

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

static void vs_cpu_cpuid(int index, int* eax, int* ebx, int* ecx, int* edx) {
    *eax = 0;
    *ebx = 0;
    *ecx = 0;
    *edx = 0;
#ifdef _MSC_VER
    int regs[4];
    __cpuidex(regs, index, 0);
    *eax = regs[0];
    *ebx = regs[1];
    *ecx = regs[2];
    *edx = regs[3];
#elif defined(__GNUC__)
    __cpuid_count(index, 0, eax, ebx, ecx, edx);
#else
#error Unknown compiler, can't get cpuid
#endif
}

static unsigned long long vs_cpu_xgetbv(unsigned ecx) {
#if defined(_MSC_VER)
    return _xgetbv(ecx);
#elif defined(__GNUC__)
    unsigned eax, edx;
    __asm("xgetbv" : "=a"(eax), "=d"(edx) : "c"(ecx) : );
    return (((unsigned long long)edx) << 32) | eax;
#else
    return 0;
#endif
}

void getCPUFeatures(CPUFeatures *cpuFeatures) {
    memset(cpuFeatures, 0, sizeof(CPUFeatures));

    int eax = 0;
    int ebx = 0;
    int ecx = 0;
    int edx = 0;
    long long xedxeax = 0;
    vs_cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
    cpuFeatures->can_run_vs = !!(edx & (1 << 26)); //sse2
    cpuFeatures->sse3 = !!(ecx & 1);
    cpuFeatures->ssse3 = !!(ecx & (1 << 9));
    cpuFeatures->sse4_1 = !!(ecx & (1 << 19));
    cpuFeatures->sse4_2 = !!(ecx & (1 << 20));
    cpuFeatures->fma3 = !!(ecx & (1 << 12));
    cpuFeatures->f16c = !!(ecx & (1 << 29));
    cpuFeatures->aes = !!(ecx & (1 << 25));
    cpuFeatures->movbe = !!(ecx & (1 << 22));
    cpuFeatures->popcnt = !!(ecx & (1 << 23));

    if ((ecx & (1 << 27)) && (ecx & (1 << 28))) {
        xedxeax = vs_cpu_xgetbv(0);       
        cpuFeatures->avx = ((xedxeax & 0x06) == 0x06);
        if (cpuFeatures->avx) {
            eax = 0;
            ebx = 0;
            ecx = 0;
            edx = 0;
            vs_cpu_cpuid(7, &eax, &ebx, &ecx, &edx);
            cpuFeatures->avx2 = !!(ebx & (1 << 5));
            cpuFeatures->avx512_f = !!(ebx & (1 << 16)) && ((xedxeax & 0xE0) == 0xE0);

            if (cpuFeatures->avx512_f) {
                cpuFeatures->avx512_cd = !!(ebx & (1 << 28));
                cpuFeatures->avx512_bw = !!(ebx & (1 << 30));
                cpuFeatures->avx512_dq = !!(ebx & (1 << 17));
                cpuFeatures->avx512_vl = !!(ebx & (1 << 31));
            }
        }
    }
}
#elif defined(VS_TARGET_OS_LINUX)
#include <sys/auxv.h>

void getCPUFeatures(CPUFeatures *cpuFeatures) {
    memset(cpuFeatures, 0, sizeof(CPUFeatures));

    unsigned long long hwcap = getauxval(AT_HWCAP);

    cpuFeatures->can_run_vs = 1;

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
#warning "Do not know how to get CPU features on Linux."
#endif
}
#else
#warning "Do not know how to get CPU features."
#endif
