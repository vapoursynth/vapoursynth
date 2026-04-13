/*
* Copyright (c) 2012-2026 Fredrik Mellbin
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

static void vs_cpu_cpuid(int index, int subindex, int* eax, int* ebx, int* ecx, int* edx) {
    *eax = 0;
    *ebx = 0;
    *ecx = 0;
    *edx = 0;
#ifdef _MSC_VER
    int regs[4];
    __cpuidex(regs, index, subindex);
    *eax = regs[0];
    *ebx = regs[1];
    *ecx = regs[2];
    *edx = regs[3];
#elif defined(__GNUC__)
    __cpuid_count(index, subindex, *eax, *ebx, *ecx, *edx);
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
    vs_cpu_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
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
    cpuFeatures->cmpxchg16b = !!(ecx & (1 << 13));

    if ((ecx & (1 << 27)) && (ecx & (1 << 28))) {
        xedxeax = vs_cpu_xgetbv(0);
        cpuFeatures->avx = ((xedxeax & 0x06) == 0x06);
        if (cpuFeatures->avx) {
            eax = 0;
            ebx = 0;
            ecx = 0;
            edx = 0;
            vs_cpu_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
            cpuFeatures->avx2 = !!(ebx & (1 << 5));
            cpuFeatures->bmi1 = !!(ebx & (1 << 3));
            cpuFeatures->bmi2 = !!(ebx & (1 << 8));

            cpuFeatures->avx512_f = !!(ebx & (1 << 16)) && ((xedxeax & 0xE0) == 0xE0);

            if (cpuFeatures->avx512_f) {
                cpuFeatures->avx512_cd   = !!(ebx & (1 << 28));
                cpuFeatures->avx512_bw   = !!(ebx & (1 << 30));
                cpuFeatures->avx512_dq   = !!(ebx & (1 << 17));
                cpuFeatures->avx512_vl   = !!(ebx & (1 << 31));
                cpuFeatures->avx512_ifma = !!(ebx & (1 << 21));
                cpuFeatures->avx512_vbmi = !!(ecx & (1 << 1));
                cpuFeatures->avx512_vbmi2 = !!(ecx & (1 << 6));
                cpuFeatures->avx512_vnni = !!(ecx & (1 << 11));
                cpuFeatures->avx512_bitalg = !!(ecx & (1 << 12));
                cpuFeatures->avx512_vpopcntdq = !!(ecx & (1 << 14));
            }

            cpuFeatures->gfni = !!(ecx & (1 << 8));
            cpuFeatures->vaes = !!(ecx & (1 << 9));
            cpuFeatures->rdseed = !!(ebx & (1 << 18));

            vs_cpu_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
            int max_ext_level = eax;
            if (max_ext_level >= 1) {
                vs_cpu_cpuid(7, 1, &eax, &ebx, &ecx, &edx);
                cpuFeatures->avx512_bf16 = !!(eax & (1 << 5));
            }
        }
    }
}

int doGetX86ABILevel(void) {
    auto c = getCPUFeatures();
    if (!(c->cmpxchg16b && c->popcnt && c->avx2 && c->f16c && c->fma3 && c->bmi2 && c->movbe))
        return 1;
    if (!(c->avx512_f && c->avx512_bw && c->avx512_cd && c->avx512_dq && c->avx512_vl && c->vaes && c->avx512_ifma && c->avx512_bf16 && c->avx512_vbmi && c->avx512_vbmi2 && c->avx512_vnni && c->avx512_bitalg && c->avx512_vpopcntdq && c->gfni))
        return 2;
    return 3;
}

#else
static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    *cpuFeatures = {};
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
