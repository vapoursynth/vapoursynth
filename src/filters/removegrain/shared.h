#ifndef SHARED_H
#define SHARED_H

#include "VapourSynth.h"
#include "VSHelper.h"
#include <cstdint>
#include <algorithm>
#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

template <class T>
static __forceinline T limit (T x, T mi, T ma)
{
	return ((x < mi) ? mi : ((x > ma) ? ma : x));
}

#ifdef VS_TARGET_CPU_X86
static __forceinline void sort_pair (__m128i &a1, __m128i &a2)
{
	const __m128i	tmp = _mm_min_epi16 (a1, a2);
	a2 = _mm_max_epi16 (a1, a2);
	a1 = tmp;
}

static __forceinline void sort_pair (__m128i &mi, __m128i &ma, __m128i a1, __m128i a2)
{
	mi = _mm_min_epi16 (a1, a2);
	ma = _mm_max_epi16 (a1, a2);
}
#endif

void VS_CC removeGrainCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC repairCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

#endif