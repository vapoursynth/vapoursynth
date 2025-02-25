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
#include <cstring>

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
    __cpuid_count(index, 0, *eax, *ebx, *ecx, *edx);
#else
#error "Unknown compiler, can't get cpuid"
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

static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
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

#elif defined(VS_TARGET_CPU_ARM)

#if defined(__APPLE__)
    #include <sys/sysctl.h>
    static inline bool have_feature(const char *feature) {
        int64_t feature_present = 0;
        size_t size = sizeof(feature_present);
        if (sysctlbyname(feature, &feature_present, &size, NULL, 0) != 0) {
            return false;
        }
        return feature_present;
    }
#elif defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <sys/auxv.h>
    #define VS_HWCAP_NEON (1 << 12)
    #define VS_HWCAP_ASIMDDP (1 << 20)
    #define VS_HWCAP_SVE (1 << 22)
    #define VS_HWCAP2_SVE2 (1 << 1)
    #define VS_HWCAP2_I8MM (1 << 13)
#endif

static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    memset(cpuFeatures, 0, sizeof(CPUFeatures));
    cpuFeatures->can_run_vs = 1;

#ifdef __aarch64__
    // NEON is mandatory for AArch64
    cpuFeatures->neon = 1;

    #if defined(__APPLE__)
        cpuFeatures->neon_dotprod = have_feature("hw.optional.arm.FEAT_DotProd");
        cpuFeatures->neon_i8mm = have_feature("hw.optional.arm.FEAT_I8MM");
    #elif defined(_WIN32)
        #if defined(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE)
            cpuFeatures->neon_dotprod = IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);
        #endif
        #if defined(PF_ARM_SVE_I8MM_INSTRUCTIONS_AVAILABLE)
            cpuFeatures->neon_i8mm = IsProcessorFeaturePresent(PF_ARM_SVE_I8MM_INSTRUCTIONS_AVAILABLE);
        #endif
        #if defined(PF_ARM_SVE_INSTRUCTIONS_AVAILABLE)
            cpuFeatures->sve = IsProcessorFeaturePresent(PF_ARM_SVE_INSTRUCTIONS_AVAILABLE);
        #endif
        #if defined(PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE)
            cpuFeatures->sve2 = IsProcessorFeaturePresent(PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE);
        #endif
    #elif defined(__linux__)
        unsigned long hwcap = getauxval(AT_HWCAP);
        unsigned long hwcap2 = getauxval(AT_HWCAP2);
        
        cpuFeatures->neon_dotprod = !!(hwcap & VS_HWCAP_ASIMDDP);
        cpuFeatures->neon_i8mm = !!(hwcap2 & VS_HWCAP2_I8MM);
        cpuFeatures->sve = !!(hwcap & VS_HWCAP_SVE);
        cpuFeatures->sve2 = !!(hwcap2 & VS_HWCAP2_SVE2);
    #endif

#else // 32-bit ARM
    #if defined(__APPLE__)
        // Modern Apple 32-bit ARM devices all support NEON
        cpuFeatures->neon = 1;
    #elif defined(_WIN32)
        cpuFeatures->neon = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);
    #elif defined(__linux__)
        cpuFeatures->neon = !!(getauxval(AT_HWCAP) & VS_HWCAP_NEON);
    #else
        #if defined(__ARM_NEON) || defined(__ARM_NEON__)
            cpuFeatures->neon = 1;
        #endif
    #endif
#endif
}

#else // Other architectures

static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    memset(cpuFeatures, 0, sizeof(CPUFeatures));
    cpuFeatures->can_run_vs = 1;
}

#endif

const CPUFeatures *getCPUFeatures(void) {
    static CPUFeatures features = []()
    {
        CPUFeatures tmp;
        doGetCPUFeatures(&tmp);
        return tmp;
    }();

    return &features;
}
