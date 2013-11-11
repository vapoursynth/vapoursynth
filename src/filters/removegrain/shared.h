/*****************************************************************************

        AvsFilterRemoveGrain/Repair16
        Author: Laurent de Soras, 2012
        Modified for VapourSynth by Fredrik Mellbin 2013

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/

#ifndef SHARED_H
#define SHARED_H

#include "VapourSynth.h"
#include "VSHelper.h"
#include <stdint.h>
#include <algorithm>
#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

#if defined(_MSC_VER)
#define _ALLOW_KEYWORD_MACROS
#define alignas(x) __declspec(align(x))
#define ALIGNED_ARRAY(decl, alignment) alignas(alignment) decl
#else
#define __forceinline inline
#define ALIGNED_ARRAY(decl, alignment) __attribute__((aligned(16))) decl
#endif

template <class T>
static __forceinline T limit (T x, T mi, T ma)
{
    return ((x < mi) ? mi : ((x > ma) ? ma : x));
}

static inline __m128i	limit_epi16(const __m128i &x, const __m128i &mi, const __m128i &ma) {
    return (_mm_max_epi16(_mm_min_epi16(x, ma), mi));
}

static inline __m128i	abs_dif_epu16(const __m128i &a, const __m128i &b) {
    const __m128i  p = _mm_subs_epu16(a, b);
    const __m128i  n = _mm_subs_epu16(b, a);

    return (_mm_or_si128(p, n));
}

static inline __m128i	select(const __m128i &cond, const __m128i &v_t, const __m128i &v_f) {
    const __m128i  cond_1 = _mm_and_si128(cond, v_t);
    const __m128i  cond_0 = _mm_andnot_si128(cond, v_f);
    const __m128i  res = _mm_or_si128(cond_0, cond_1);

    return (res);
}

static inline __m128i	select_16_equ(const __m128i &lhs, const __m128i &rhs, const __m128i &v_t, const __m128i &v_f) {
    const __m128i  cond = _mm_cmpeq_epi16(lhs, rhs);

    return (select(cond, v_t, v_f));
}

static inline void add_x16_s32(__m128i &dst0, __m128i &dst1, __m128i src, __m128i msw) {
    const __m128i  res0 = _mm_unpacklo_epi16(src, msw);
    const __m128i  res1 = _mm_unpackhi_epi16(src, msw);

    dst0 = _mm_add_epi32(dst0, res0);
    dst1 = _mm_add_epi32(dst1, res1);
}

static inline __m128i	mul_s32_s15_s16(__m128i src0, __m128i src1, __m128i coef) {
    const __m128i  hi0 = _mm_mulhi_epu16(src0, coef);
    const __m128i  lo0 = _mm_mullo_epi16(src0, coef);
    const __m128i  hi1 = _mm_mulhi_epu16(src1, coef);
    const __m128i  lo1 = _mm_mullo_epi16(src1, coef);

    const __m128i	hi0s = _mm_slli_epi32(hi0, 16);
    const __m128i	hi1s = _mm_slli_epi32(hi1, 16);

    __m128i	      sum0 = _mm_add_epi32(hi0s, lo0);
    __m128i	      sum1 = _mm_add_epi32(hi1s, lo1);
    sum0 = _mm_srai_epi32(sum0, 16);
    sum1 = _mm_srai_epi32(sum1, 16);

    const __m128i	res = _mm_packs_epi32(sum0, sum1);

    return (res);
}

class LineProcAll {
public:
    static inline bool skip_line(int) { return (false); }
};
class LineProcEven {
public:
    static inline bool skip_line(int y) { return ((y & 1) != 0); }
};
class LineProcOdd {
public:
    static inline bool skip_line(int y) { return ((y & 1) == 0); }
};

#ifdef VS_TARGET_CPU_X86
static __forceinline void sort_pair (__m128i &a1, __m128i &a2)
{
    const __m128i    tmp = _mm_min_epi16 (a1, a2);
    a2 = _mm_max_epi16 (a1, a2);
    a1 = tmp;
}

static __forceinline void sort_pair (__m128i &mi, __m128i &ma, __m128i a1, __m128i a2)
{
    mi = _mm_min_epi16 (a1, a2);
    ma = _mm_max_epi16 (a1, a2);
}
#endif

enum cleanseMode {
    cmNormal,
    cmForward,
    cmBackward
};

void VS_CC removeGrainCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC repairCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC clenseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

#endif
