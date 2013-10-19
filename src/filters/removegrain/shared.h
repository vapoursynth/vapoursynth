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