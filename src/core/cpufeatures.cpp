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
                cpuFeatures->avx512_fp16 = !!(edx & (1 << 23));
            }

            cpuFeatures->avx512 = cpuFeatures->avx512_f && cpuFeatures->avx512_cd &&
                cpuFeatures->avx512_bw && cpuFeatures->avx512_dq && cpuFeatures->avx512_vl;

            cpuFeatures->gfni = !!(ecx & (1 << 8));
            cpuFeatures->vaes = !!(ecx & (1 << 9));
            cpuFeatures->rdseed = !!(ebx & (1 << 18));

            if (cpuFeatures->avx512_f && eax >= 1) {
                eax = 0;
                ebx = 0;
                ecx = 0;
                edx = 0;
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
    if (!c->avx512)
        return 2;
    if (!(c->avx512 && c->vaes && c->avx512_ifma && c->avx512_vbmi && c->avx512_vbmi2 && c->avx512_vnni && c->avx512_bitalg && c->avx512_vpopcntdq && c->gfni))
        return 3;
    return 4;
}

#elif defined(VS_TARGET_CPU_ARM64)

#if defined(__APPLE__)
#include <sys/sysctl.h>

static char vs_sysctl_flag(const char *name) {
    int val = 0;
    size_t len = sizeof(val);
    if (sysctlbyname(name, &val, &len, nullptr, 0) != 0)
        return 0;
    return !!val;
}

static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    *cpuFeatures = {};
    cpuFeatures->can_run_vs = 1;
    cpuFeatures->dotprod = vs_sysctl_flag("hw.optional.arm.FEAT_DotProd");
    cpuFeatures->fp16 = vs_sysctl_flag("hw.optional.arm.FEAT_FP16");
    cpuFeatures->fhm = vs_sysctl_flag("hw.optional.arm.FEAT_FHM");
    cpuFeatures->i8mm = vs_sysctl_flag("hw.optional.arm.FEAT_I8MM");
    // Apple Silicon has no non-streaming SVE; SVE instructions exist only inside
    // SME streaming mode. Leave sve/sve2 unset so no non-streaming kernel is picked.
    cpuFeatures->sme = vs_sysctl_flag("hw.optional.arm.FEAT_SME");
    cpuFeatures->sme2 = vs_sysctl_flag("hw.optional.arm.FEAT_SME2");
    cpuFeatures->sme_i16i64 = vs_sysctl_flag("hw.optional.arm.FEAT_SME_I16I64");
    cpuFeatures->sme_f64f64 = vs_sysctl_flag("hw.optional.arm.FEAT_SME_F64F64");
}

#elif defined(__linux__)
#include <sys/auxv.h>

/* Bits from linux arch/arm64/include/uapi/asm/hwcap.h; defined here so old
   toolchain headers don't limit runtime detection. */
#ifndef HWCAP_FPHP
#define HWCAP_FPHP (1UL << 9)
#endif
#ifndef HWCAP_ASIMDHP
#define HWCAP_ASIMDHP (1UL << 10)
#endif
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1UL << 20)
#endif
#ifndef HWCAP_ASIMDFHM
#define HWCAP_ASIMDFHM (1UL << 23)
#endif
#ifndef HWCAP_SVE
#define HWCAP_SVE (1UL << 22)
#endif
#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1UL << 1)
#endif
#ifndef HWCAP2_I8MM
#define HWCAP2_I8MM (1UL << 13)
#endif
#ifndef HWCAP2_SME
#define HWCAP2_SME (1UL << 23)
#endif
#ifndef HWCAP2_SME_I16I64
#define HWCAP2_SME_I16I64 (1UL << 24)
#endif
#ifndef HWCAP2_SME_F64F64
#define HWCAP2_SME_F64F64 (1UL << 25)
#endif
#ifndef HWCAP2_SME2
#define HWCAP2_SME2 (1UL << 37)
#endif

static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    *cpuFeatures = {};
    cpuFeatures->can_run_vs = 1;
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    cpuFeatures->dotprod = !!(hwcap & HWCAP_ASIMDDP);
    cpuFeatures->fp16 = !!(hwcap & HWCAP_FPHP) && !!(hwcap & HWCAP_ASIMDHP);
    cpuFeatures->fhm = !!(hwcap & HWCAP_ASIMDFHM);
    cpuFeatures->i8mm = !!(hwcap2 & HWCAP2_I8MM);
    cpuFeatures->sve = !!(hwcap & HWCAP_SVE);
    cpuFeatures->sve2 = !!(hwcap2 & HWCAP2_SVE2);
    cpuFeatures->sme = !!(hwcap2 & HWCAP2_SME);
    cpuFeatures->sme2 = !!(hwcap2 & HWCAP2_SME2);
    cpuFeatures->sme_i16i64 = !!(hwcap2 & HWCAP2_SME_I16I64);
    cpuFeatures->sme_f64f64 = !!(hwcap2 & HWCAP2_SME_F64F64);
}

#else
static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    *cpuFeatures = {};
    cpuFeatures->can_run_vs = 1;
}
#endif

#elif defined(VS_TARGET_CPU_RISCV)
static void doGetCPUFeatures(CPUFeatures *cpuFeatures) {
    *cpuFeatures = {};
    cpuFeatures->can_run_vs = 1;
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
