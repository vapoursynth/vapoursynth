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
    /* ecx */
    #define bit_SSE3	(1 << 0)
    #define bit_SSSE3	(1 << 9)
    #define bit_FMA		(1 << 12)
    #define bit_SSE4_1	(1 << 19)
    #define bit_SSE4_2	(1 << 20)
    #define bit_MOVBE	(1 << 22)
    #define bit_POPCNT	(1 << 23)
    #define bit_AES		(1 << 25)
    #define bit_OSXSAVE	(1 << 27)
    #define bit_AVX		(1 << 28)
    #define bit_F16C	(1 << 29)
    /* edx */
    #define bit_SSE2	(1 << 26)
    /* Extended Features (%eax == 7) */
    /* ebx */
    #define bit_AVX2	(1 << 5)
    #define bit_AVX512F	(1 << 16)
    #define bit_AVX512DQ	(1 << 17)
    #define bit_AVX512CD	(1 << 28)
    #define bit_AVX512BW	(1 << 30)
    #define bit_AVX512VL	(1u << 31)
#else
    #include <cpuid.h>
#endif

/* xgetbv mask (ecx) */
#define _XCR_XFEATURE_ENABLED_MASK 0x0

static void vs_cpu_cpuid(int index, int* eax, int* ebx, int* ecx, int* edx) {
    *eax = *ebx = *ecx = *edx = 0;
#ifdef _MSC_VER
    int regs[4];
    __cpuidex(regs, index, 0);
    *eax = regs[0];
    *ebx = regs[1];
    *ecx = regs[2];
    *edx = regs[3];
#elif defined(__GNUC__)
    __cpuid_count(index, 0, *eax, *ebx, *ecx, *edx);
#else
#error Unknown compiler, cannot get cpuid
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

    int eax, ebx, ecx, edx;
    long long xedxeax;
    vs_cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
    cpuFeatures->can_run_vs = !!(edx & bit_SSE2);
    cpuFeatures->sse3 = !!(ecx & bit_SSE3);
    cpuFeatures->ssse3 = !!(ecx & bit_SSSE3);
    cpuFeatures->sse4_1 = !!(ecx & bit_SSE4_1);
    cpuFeatures->sse4_2 = !!(ecx & bit_SSE4_2);
    cpuFeatures->fma3 = !!(ecx & bit_FMA);
    cpuFeatures->f16c = !!(ecx & bit_F16C);
    cpuFeatures->aes = !!(ecx & bit_AES);
    cpuFeatures->movbe = !!(ecx & bit_MOVBE);
    cpuFeatures->popcnt = !!(ecx & bit_POPCNT);

    if ((ecx & bit_OSXSAVE) && (ecx & bit_AVX)) {
        xedxeax = vs_cpu_xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        cpuFeatures->avx = !!(xedxeax & 0x06);
        if (cpuFeatures->avx) {
            vs_cpu_cpuid(7, &eax, &ebx, &ecx, &edx);
            cpuFeatures->avx2 = !!(ebx & bit_AVX2);
            cpuFeatures->avx512_f = !!(ebx & bit_AVX512F) && !!(xedxeax & 0xE0);

            if (cpuFeatures->avx512_f) {
                cpuFeatures->avx512_cd = !!(ebx & bit_AVX512CD);
                cpuFeatures->avx512_bw = !!(ebx & bit_AVX512BW);
                cpuFeatures->avx512_dq = !!(ebx & bit_AVX512DQ);
                cpuFeatures->avx512_vl = !!(ebx & bit_AVX512VL);
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
