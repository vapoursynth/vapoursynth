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
    char bmi1;
    char bmi2;
    char cmpxchg16b;
    char avx512_f;
    char avx512_cd;
    char avx512_bw;
    char avx512_dq;
    char avx512_vl;
    char avx512_ifma;
    char avx512_vbmi;
    char avx512_vbmi2;
    char avx512_vnni;
    char avx512_bitalg;
    char avx512_vpopcntdq;
    char avx512_bf16;
    char avx512_fp16;
    char gfni;
    char vaes;
    char rdseed;
    char avx512;
#elif defined(VS_TARGET_CPU_ARM64)
    // On AArch64, NEON (ASIMD) is part of the baseline and always present.
    char dotprod;     /* FEAT_DotProd: sdot/udot */
    char fp16;        /* FEAT_FP16: fullfp16 arithmetic */
    char fhm;         /* FEAT_FHM: fmlal/fmlal2 widening f16 MAC */
    char i8mm;        /* FEAT_I8MM: usdot/ummla */
    char sve;         /* FEAT_SVE, non-streaming (not set on Apple silicon) */
    char sve2;        /* FEAT_SVE2 */
    char sme;         /* FEAT_SME */
    char sme2;        /* FEAT_SME2 */
    char sme_i16i64;  /* FEAT_SME_I16I64: 16-bit int outer products into ZA64 */
    char sme_f64f64;  /* FEAT_SME_F64F64 */
#endif
} CPUFeatures;

const CPUFeatures *getCPUFeatures(void);
int doGetX86ABILevel(void);

#ifdef __cplusplus
}
#endif

#endif