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

#include "shared.h"

#ifdef VS_TARGET_CPU_X86

class ConvSigned
{
public:
    static __forceinline __m128i cv (__m128i a, __m128i m)
    {
        return (_mm_xor_si128 (a, m));
    }
};


class ConvUnsigned
{
public:
    static __forceinline __m128i cv (__m128i a, __m128i m)
    {
        return (a);
    }
};

#define AvsFilterRepair16_READ_PIX    \
   const ptrdiff_t      om = stride_src2 - 1;     \
   const ptrdiff_t      o0 = stride_src2    ;     \
   const ptrdiff_t      op = stride_src2 + 1;     \
   __m128i        cr, a1, a2, a3, a4, c, a5, a6, a7, a8; \
   if (sizeof(T) == 1) { \
       __m128i zeroreg = _mm_setzero_si128(); \
       cr = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src1_ptr + 0)), zeroreg), mask_sign); \
       a1 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr - op)), zeroreg), mask_sign); \
       a2 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr - o0)), zeroreg), mask_sign); \
       a3 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr - om)), zeroreg), mask_sign); \
       a4 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr - 1 )), zeroreg), mask_sign); \
       c  = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr + 0 )), zeroreg), mask_sign); \
       a5 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr + 1 )), zeroreg), mask_sign); \
       a6 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr + om)), zeroreg), mask_sign); \
       a7 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr + o0)), zeroreg), mask_sign); \
       a8 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src2_ptr + op)), zeroreg), mask_sign); \
   } else {     \
       cr = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src1_ptr + 0 )), mask_sign); \
       a1 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr - op)), mask_sign); \
       a2 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr - o0)), mask_sign); \
       a3 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr - om)), mask_sign); \
       a4 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr - 1 )), mask_sign); \
       c  = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr + 0 )), mask_sign); \
       a5 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr + 1 )), mask_sign); \
       a6 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr + om)), mask_sign); \
       a7 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr + o0)), mask_sign); \
       a8 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src2_ptr + op)), mask_sign); \
   }

#define AvsFilterRepair16_SORT_AXIS_SSE2   \
    const __m128i  ma1 = _mm_max_epi16(a1, a8); \
    const __m128i  mi1 = _mm_min_epi16(a1, a8); \
    const __m128i  ma2 = _mm_max_epi16(a2, a7); \
    const __m128i  mi2 = _mm_min_epi16(a2, a7); \
    const __m128i  ma3 = _mm_max_epi16(a3, a6); \
    const __m128i  mi3 = _mm_min_epi16(a3, a6); \
    const __m128i  ma4 = _mm_max_epi16(a4, a5); \
    const __m128i  mi4 = _mm_min_epi16(a4, a5);

#else

class ConvSigned
{
};


class ConvUnsigned
{
};
#endif

#define AvsFilterRepair16_SORT_AXIS_CPP \
    const int      ma1 = std::max(a1, a8);   \
    const int      mi1 = std::min(a1, a8);   \
    const int      ma2 = std::max(a2, a7);   \
    const int      mi2 = std::min(a2, a7);   \
    const int      ma3 = std::max(a3, a6);   \
    const int      mi3 = std::min(a3, a6);   \
    const int      ma4 = std::max(a4, a5);   \
    const int      mi4 = std::min(a4, a5);


class OpRG01
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int        mi = std::min (std::min (
            std::min (std::min (a1, a2), std::min (a3, a4)),
            std::min (std::min (a5, a6), std::min (a7, a8))
        ), c);
        const int        ma = std::max (std::max (
            std::max (std::max (a1, a2), std::max (a3, a4)),
            std::max (std::max (a5, a6), std::max (a7, a8))
        ), c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i    mi = _mm_min_epi16 (_mm_min_epi16 (
            _mm_min_epi16 (_mm_min_epi16 (a1, a2), _mm_min_epi16 (a3, a4)),
            _mm_min_epi16 (_mm_min_epi16 (a5, a6), _mm_min_epi16 (a7, a8))
        ), c);
        const __m128i    ma = _mm_max_epi16 (_mm_max_epi16 (
            _mm_max_epi16 (_mm_max_epi16 (a1, a2), _mm_max_epi16 (a3, a4)),
            _mm_max_epi16 (_mm_max_epi16 (a5, a6), _mm_max_epi16 (a7, a8))
        ), c);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, mi), ma));
    }
#endif
};

class OpRG02
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [9] = { a1, a2, a3, a4, c, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [8]) + 1);

        return (limit (cr, a [2-1], a [7]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a8);

        sort_pair (a1,  c);
        sort_pair (a2, a5);
        sort_pair (a3, a6);
        sort_pair (a4, a7);
        sort_pair ( c, a8);

        sort_pair (a1, a3);
        sort_pair ( c, a6);
        sort_pair (a2, a4);
        sort_pair (a5, a7);

        sort_pair (a3, a8);

        sort_pair (a3,  c);
        sort_pair (a6, a8);
        sort_pair (a4, a5);

        a2 = _mm_max_epi16 (a1, a2);    // sort_pair (a1, a2);
        a3 = _mm_min_epi16 (a3, a4);    // sort_pair (a3, a4);
        sort_pair ( c, a5);
        a7 = _mm_max_epi16 (a6, a7);    // sort_pair (a6, a7);

        sort_pair (a2, a8);

        a2 = _mm_min_epi16 (a2,  c);    // sort_pair (a2,  c);
        a8 = _mm_max_epi16 (a5, a8);    // sort_pair (a5, a8);

        a2 = _mm_min_epi16 (a2, a3);    // sort_pair (a2, a3);
        a7 = _mm_min_epi16 (a7, a8);    // sort_pair (a7, a8);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, a2), a7));
    }
#endif
};

class OpRG03
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [9] = { a1, a2, a3, a4, c, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [8]) + 1);

        return (limit (cr, a [3-1], a [6]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a8);

        sort_pair (a1,  c);
        sort_pair (a2, a5);
        sort_pair (a3, a6);
        sort_pair (a4, a7);
        sort_pair ( c, a8);

        sort_pair (a1, a3);
        sort_pair ( c, a6);
        sort_pair (a2, a4);
        sort_pair (a5, a7);

        sort_pair (a3, a8);

        sort_pair (a3,  c);
        sort_pair (a6, a8);
        sort_pair (a4, a5);

        a2 = _mm_max_epi16 (a1, a2);    // sort_pair (a1, a2);
        sort_pair (a3, a4);
        sort_pair ( c, a5);
        a6 = _mm_min_epi16 (a6, a7);    // sort_pair (a6, a7);

        sort_pair (a2, a8);

        a2 = _mm_min_epi16 (a2,  c);    // sort_pair (a2,  c);
        a6 = _mm_max_epi16 (a4, a6);    // sort_pair (a4, a6);
        a5 = _mm_min_epi16 (a5, a8);    // sort_pair (a5, a8);

        a3 = _mm_max_epi16 (a2, a3);    // sort_pair (a2, a3);
        a6 = _mm_max_epi16 (a5, a6);    // sort_pair (a5, a6);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, a3), a6));
    }
#endif
};

class OpRG04
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [9] = { a1, a2, a3, a4, c, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [8]) + 1);

        return (limit (cr, a [4-1], a [5]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        // http://jgamble.ripco.net/cgi-bin/nw.cgi?inputs=9&algorithm=batcher&output=text

        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a8);

        sort_pair (a1,  c);
        sort_pair (a2, a5);
        sort_pair (a3, a6);
        sort_pair (a4, a7);
        sort_pair ( c, a8);

        sort_pair (a1, a3);
        sort_pair ( c, a6);
        sort_pair (a2, a4);
        sort_pair (a5, a7);

        sort_pair (a3, a8);

        sort_pair (a3,  c);
        sort_pair (a6, a8);
        sort_pair (a4, a5);

        a2 = _mm_max_epi16 (a1, a2);    // sort_pair (a1, a2);
        a4 = _mm_max_epi16 (a3, a4);    // sort_pair (a3, a4);
        sort_pair ( c, a5);
        a6 = _mm_min_epi16 (a6, a7);    // sort_pair (a6, a7);

        sort_pair (a2, a8);

        c  = _mm_max_epi16 (a2,  c);    // sort_pair (a2,  c);
        sort_pair (a4, a6);
        a5 = _mm_min_epi16 (a5, a8);    // sort_pair (a5, a8);

        a4 = _mm_min_epi16 (a4,  c);    // sort_pair (a4,  c);
        a5 = _mm_min_epi16 (a5, a6);    // sort_pair (a5, a6);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, a4), a5));
    }
#endif
};

class OpRG05
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int mal1 = std::max(std::max(a1, a8), c);
        const int mil1 = std::min(std::min(a1, a8), c);

        const int mal2 = std::max(std::max(a2, a7), c);
        const int mil2 = std::min(std::min(a2, a7), c);

        const int mal3 = std::max(std::max(a3, a6), c);
        const int mil3 = std::min(std::min(a3, a6), c);

        const int mal4 = std::max(std::max(a4, a5), c);
        const int mil4 = std::min(std::min(a4, a5), c);

        const int clipped1 = limit(cr, mil1, mal1);
        const int clipped2 = limit(cr, mil2, mal2);
        const int clipped3 = limit(cr, mil3, mal3);
        const int clipped4 = limit(cr, mil4, mal4);

        const int c1 = std::abs(cr - clipped1);
        const int c2 = std::abs(cr - clipped2);
        const int c3 = std::abs(cr - clipped3);
        const int c4 = std::abs(cr - clipped4);

        const int mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        if (mindiff == c4)
            return clipped4;
        else if (mindiff == c2)
            return clipped2;
        else if (mindiff == c3)
            return clipped3;
        else
            return clipped1;
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i mal1 = _mm_max_epi16(_mm_max_epi16(a1, a8), c);
        const __m128i mil1 = _mm_min_epi16(_mm_min_epi16(a1, a8), c);

        const __m128i mal2 = _mm_max_epi16(_mm_max_epi16(a2, a7), c);
        const __m128i mil2 = _mm_min_epi16(_mm_min_epi16(a2, a7), c);

        const __m128i mal3 = _mm_max_epi16(_mm_max_epi16(a3, a6), c);
        const __m128i mil3 = _mm_min_epi16(_mm_min_epi16(a3, a6), c);

        const __m128i mal4 = _mm_max_epi16(_mm_max_epi16(a4, a5), c);
        const __m128i mil4 = _mm_min_epi16(_mm_min_epi16(a4, a5), c);

        const __m128i clipped1 = limit_epi16(cr, mil1, mal1);
        const __m128i clipped2 = limit_epi16(cr, mil2, mal2);
        const __m128i clipped3 = limit_epi16(cr, mil3, mal3);
        const __m128i clipped4 = limit_epi16(cr, mil4, mal4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cru = _mm_xor_si128(cr, mask_sign);

        const __m128i c1u = abs_dif_epu16(cru, clipped1u);
        const __m128i c2u = abs_dif_epu16(cru, clipped2u);
        const __m128i c3u = abs_dif_epu16(cru, clipped3u);
        const __m128i c4u = abs_dif_epu16(cru, clipped4u);

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, clipped1, cr);
        result = select_16_equ(mindiff, c3, clipped3, result);
        result = select_16_equ(mindiff, c2, clipped2, result);
        return select_16_equ(mindiff, c4, clipped4, result);
}
#endif
};

class OpRG06
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int mal1 = std::max(std::max(a1, a8), c);
        const int mil1 = std::min(std::min(a1, a8), c);

        const int mal2 = std::max(std::max(a2, a7), c);
        const int mil2 = std::min(std::min(a2, a7), c);

        const int mal3 = std::max(std::max(a3, a6), c);
        const int mil3 = std::min(std::min(a3, a6), c);

        const int mal4 = std::max(std::max(a4, a5), c);
        const int mil4 = std::min(std::min(a4, a5), c);

        const int d1 = mal1 - mil1;
        const int d2 = mal2 - mil2;
        const int d3 = mal3 - mil3;
        const int d4 = mal4 - mil4;

        const int clipped1 = limit(cr, mil1, mal1);
        const int clipped2 = limit(cr, mil2, mal2);
        const int clipped3 = limit(cr, mil3, mal3);
        const int clipped4 = limit(cr, mil4, mal4);

        const int c1 = limit((std::abs(cr - clipped1) << 1) + d1, 0, 0xFFFF);
        const int c2 = limit((std::abs(cr - clipped2) << 1) + d2, 0, 0xFFFF);
        const int c3 = limit((std::abs(cr - clipped3) << 1) + d3, 0, 0xFFFF);
        const int c4 = limit((std::abs(cr - clipped4) << 1) + d4, 0, 0xFFFF);

        const int mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        if (mindiff == c4)
            return clipped4;
        else if (mindiff == c2)
            return clipped2;
        else if (mindiff == c3)
            return clipped3;
        else
            return clipped1;
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i mal1 = _mm_max_epi16(_mm_max_epi16(a1, a8), c);
        const __m128i mil1 = _mm_min_epi16(_mm_min_epi16(a1, a8), c);

        const __m128i mal2 = _mm_max_epi16(_mm_max_epi16(a2, a7), c);
        const __m128i mil2 = _mm_min_epi16(_mm_min_epi16(a2, a7), c);

        const __m128i mal3 = _mm_max_epi16(_mm_max_epi16(a3, a6), c);
        const __m128i mil3 = _mm_min_epi16(_mm_min_epi16(a3, a6), c);

        const __m128i mal4 = _mm_max_epi16(_mm_max_epi16(a4, a5), c);
        const __m128i mil4 = _mm_min_epi16(_mm_min_epi16(a4, a5), c);

        const __m128i d1 = _mm_sub_epi16(mal1, mil1);
        const __m128i d2 = _mm_sub_epi16(mal2, mil2);
        const __m128i d3 = _mm_sub_epi16(mal3, mil3);
        const __m128i d4 = _mm_sub_epi16(mal4, mil4);

        const __m128i clipped1 = limit_epi16(cr, mil1, mal1);
        const __m128i clipped2 = limit_epi16(cr, mil2, mal2);
        const __m128i clipped3 = limit_epi16(cr, mil3, mal3);
        const __m128i clipped4 = limit_epi16(cr, mil4, mal4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cru = _mm_xor_si128(cr, mask_sign);

        const __m128i absdiff1 = abs_dif_epu16(cru, clipped1u);
        const __m128i absdiff2 = abs_dif_epu16(cru, clipped2u);
        const __m128i absdiff3 = abs_dif_epu16(cru, clipped3u);
        const __m128i absdiff4 = abs_dif_epu16(cru, clipped4u);

        const __m128i c1u = _mm_adds_epu16(_mm_adds_epu16(absdiff1, absdiff1), d1);
        const __m128i c2u = _mm_adds_epu16(_mm_adds_epu16(absdiff2, absdiff2), d2);
        const __m128i c3u = _mm_adds_epu16(_mm_adds_epu16(absdiff3, absdiff3), d3);
        const __m128i c4u = _mm_adds_epu16(_mm_adds_epu16(absdiff4, absdiff4), d4);

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, clipped1, cr);
        result = select_16_equ(mindiff, c3, clipped3, result);
        result = select_16_equ(mindiff, c2, clipped2, result);
        return select_16_equ(mindiff, c4, clipped4, result);
    }
#endif
};

class OpRG07
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int mal1 = std::max(std::max(a1, a8), c);
        const int mil1 = std::min(std::min(a1, a8), c);

        const int mal2 = std::max(std::max(a2, a7), c);
        const int mil2 = std::min(std::min(a2, a7), c);

        const int mal3 = std::max(std::max(a3, a6), c);
        const int mil3 = std::min(std::min(a3, a6), c);

        const int mal4 = std::max(std::max(a4, a5), c);
        const int mil4 = std::min(std::min(a4, a5), c);

        const int d1 = mal1 - mil1;
        const int d2 = mal2 - mil2;
        const int d3 = mal3 - mil3;
        const int d4 = mal4 - mil4;

        const int clipped1 = limit(cr, mil1, mal1);
        const int clipped2 = limit(cr, mil2, mal2);
        const int clipped3 = limit(cr, mil3, mal3);
        const int clipped4 = limit(cr, mil4, mal4);

        const int c1 = std::abs(cr - clipped1) + d1;
        const int c2 = std::abs(cr - clipped2) + d2;
        const int c3 = std::abs(cr - clipped3) + d3;
        const int c4 = std::abs(cr - clipped4) + d4;

        const int mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        if (mindiff == c4)
            return clipped4;
        else if (mindiff == c2)
            return clipped2;
        else if (mindiff == c3)
            return clipped3;
        else
            return clipped1;
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i mal1 = _mm_max_epi16(_mm_max_epi16(a1, a8), c);
        const __m128i mil1 = _mm_min_epi16(_mm_min_epi16(a1, a8), c);

        const __m128i mal2 = _mm_max_epi16(_mm_max_epi16(a2, a7), c);
        const __m128i mil2 = _mm_min_epi16(_mm_min_epi16(a2, a7), c);

        const __m128i mal3 = _mm_max_epi16(_mm_max_epi16(a3, a6), c);
        const __m128i mil3 = _mm_min_epi16(_mm_min_epi16(a3, a6), c);

        const __m128i mal4 = _mm_max_epi16(_mm_max_epi16(a4, a5), c);
        const __m128i mil4 = _mm_min_epi16(_mm_min_epi16(a4, a5), c);

        const __m128i d1 = _mm_sub_epi16(mal1, mil1);
        const __m128i d2 = _mm_sub_epi16(mal2, mil2);
        const __m128i d3 = _mm_sub_epi16(mal3, mil3);
        const __m128i d4 = _mm_sub_epi16(mal4, mil4);

        const __m128i clipped1 = limit_epi16(cr, mil1, mal1);
        const __m128i clipped2 = limit_epi16(cr, mil2, mal2);
        const __m128i clipped3 = limit_epi16(cr, mil3, mal3);
        const __m128i clipped4 = limit_epi16(cr, mil4, mal4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cru = _mm_xor_si128(cr, mask_sign);

        //todo: what happens when this overflows?
        const __m128i c1u = _mm_adds_epu16(abs_dif_epu16(cru, clipped1u), d1);
        const __m128i c2u = _mm_adds_epu16(abs_dif_epu16(cru, clipped2u), d2);
        const __m128i c3u = _mm_adds_epu16(abs_dif_epu16(cru, clipped3u), d3);
        const __m128i c4u = _mm_adds_epu16(abs_dif_epu16(cru, clipped4u), d4);

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, clipped1, cr);
        result = select_16_equ(mindiff, c3, clipped3, result);
        result = select_16_equ(mindiff, c2, clipped2, result);
        return select_16_equ(mindiff, c4, clipped4, result);
    }
#endif
};

class OpRG08
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int mal1 = std::max(std::max(a1, a8), c);
        const int mil1 = std::min(std::min(a1, a8), c);

        const int mal2 = std::max(std::max(a2, a7), c);
        const int mil2 = std::min(std::min(a2, a7), c);

        const int mal3 = std::max(std::max(a3, a6), c);
        const int mil3 = std::min(std::min(a3, a6), c);

        const int mal4 = std::max(std::max(a4, a5), c);
        const int mil4 = std::min(std::min(a4, a5), c);

        const int d1 = mal1 - mil1;
        const int d2 = mal2 - mil2;
        const int d3 = mal3 - mil3;
        const int d4 = mal4 - mil4;

        const int clipped1 = limit(cr, mil1, mal1);
        const int clipped2 = limit(cr, mil2, mal2);
        const int clipped3 = limit(cr, mil3, mal3);
        const int clipped4 = limit(cr, mil4, mal4);

        const int c1 = limit(std::abs(cr - clipped1) + (d1 << 1), 0, 0xFFFF);
        const int c2 = limit(std::abs(cr - clipped2) + (d2 << 1), 0, 0xFFFF);
        const int c3 = limit(std::abs(cr - clipped3) + (d3 << 1), 0, 0xFFFF);
        const int c4 = limit(std::abs(cr - clipped4) + (d4 << 1), 0, 0xFFFF);

        const int mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        if (mindiff == c4)
            return clipped4;
        else if (mindiff == c2)
            return clipped2;
        else if (mindiff == c3)
            return clipped3;
        else
            return clipped1;
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i mal1 = _mm_max_epi16(_mm_max_epi16(a1, a8), c);
        const __m128i mil1 = _mm_min_epi16(_mm_min_epi16(a1, a8), c);

        const __m128i mal2 = _mm_max_epi16(_mm_max_epi16(a2, a7), c);
        const __m128i mil2 = _mm_min_epi16(_mm_min_epi16(a2, a7), c);

        const __m128i mal3 = _mm_max_epi16(_mm_max_epi16(a3, a6), c);
        const __m128i mil3 = _mm_min_epi16(_mm_min_epi16(a3, a6), c);

        const __m128i mal4 = _mm_max_epi16(_mm_max_epi16(a4, a5), c);
        const __m128i mil4 = _mm_min_epi16(_mm_min_epi16(a4, a5), c);

        const __m128i d1 = _mm_sub_epi16(mal1, mil1);
        const __m128i d2 = _mm_sub_epi16(mal2, mil2);
        const __m128i d3 = _mm_sub_epi16(mal3, mil3);
        const __m128i d4 = _mm_sub_epi16(mal4, mil4);

        const __m128i clipped1 = limit_epi16(cr, mil1, mal1);
        const __m128i clipped2 = limit_epi16(cr, mil2, mal2);
        const __m128i clipped3 = limit_epi16(cr, mil3, mal3);
        const __m128i clipped4 = limit_epi16(cr, mil4, mal4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cru = _mm_xor_si128(cr, mask_sign);

        const __m128i c1u = _mm_adds_epu16(abs_dif_epu16(cru, clipped1u), _mm_adds_epu16(d1, d1));
        const __m128i c2u = _mm_adds_epu16(abs_dif_epu16(cru, clipped2u), _mm_adds_epu16(d2, d2));
        const __m128i c3u = _mm_adds_epu16(abs_dif_epu16(cru, clipped3u), _mm_adds_epu16(d3, d3));
        const __m128i c4u = _mm_adds_epu16(abs_dif_epu16(cru, clipped4u), _mm_adds_epu16(d4, d4));

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, clipped1, cr);
        result = select_16_equ(mindiff, c3, clipped3, result);
        result = select_16_equ(mindiff, c2, clipped2, result);
        return select_16_equ(mindiff, c4, clipped4, result);
    }
#endif
};

class OpRG09
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int mal1 = std::max(std::max(a1, a8), c);
        const int mil1 = std::min(std::min(a1, a8), c);

        const int mal2 = std::max(std::max(a2, a7), c);
        const int mil2 = std::min(std::min(a2, a7), c);

        const int mal3 = std::max(std::max(a3, a6), c);
        const int mil3 = std::min(std::min(a3, a6), c);

        const int mal4 = std::max(std::max(a4, a5), c);
        const int mil4 = std::min(std::min(a4, a5), c);

        const int d1 = mal1 - mil1;
        const int d2 = mal2 - mil2;
        const int d3 = mal3 - mil3;
        const int d4 = mal4 - mil4;

        const int mindiff = std::min(std::min(d1, d2), std::min(d3, d4));

        if (mindiff == d4)
            return limit(cr, mil4, mal4);
        else if (mindiff == d2)
            return limit(cr, mil2, mal2);
        else if (mindiff == d3)
            return limit(cr, mil3, mal3);
        else
            return limit(cr, mil1, mal1);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i mal1 = _mm_max_epi16(_mm_max_epi16(a1, a8), c);
        const __m128i mil1 = _mm_min_epi16(_mm_min_epi16(a1, a8), c);

        const __m128i mal2 = _mm_max_epi16(_mm_max_epi16(a2, a7), c);
        const __m128i mil2 = _mm_min_epi16(_mm_min_epi16(a2, a7), c);

        const __m128i mal3 = _mm_max_epi16(_mm_max_epi16(a3, a6), c);
        const __m128i mil3 = _mm_min_epi16(_mm_min_epi16(a3, a6), c);

        const __m128i mal4 = _mm_max_epi16(_mm_max_epi16(a4, a5), c);
        const __m128i mil4 = _mm_min_epi16(_mm_min_epi16(a4, a5), c);

        const __m128i d1 = _mm_sub_epi16(mal1, mil1);
        const __m128i d2 = _mm_sub_epi16(mal2, mil2);
        const __m128i d3 = _mm_sub_epi16(mal3, mil3);
        const __m128i d4 = _mm_sub_epi16(mal4, mil4);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4));

        __m128i result = select_16_equ(mindiff, d1, limit_epi16(cr, mil1, mal1), cr);
        result = select_16_equ(mindiff, d3, limit_epi16(cr, mil3, mal3), result);
        result = select_16_equ(mindiff, d2, limit_epi16(cr, mil2, mal2), result);
        return select_16_equ(mindiff, d4, limit_epi16(cr, mil4, mal4), result);
    }
#endif
};

class OpRG10
{
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int d1 = std::abs(cr - a1);
        const int d2 = std::abs(cr - a2);
        const int d3 = std::abs(cr - a3);
        const int d4 = std::abs(cr - a4);
        const int d5 = std::abs(cr - a5);
        const int d6 = std::abs(cr - a6);
        const int d7 = std::abs(cr - a7);
        const int d8 = std::abs(cr - a8);
        const int dc = std::abs(cr - c);

        const int mindiff = std::min(std::min(std::min(std::min(d1, d2), std::min(d3, d4)), std::min(std::min(d5, d6), std::min(d7, d8))), dc);

        if (mindiff == d7)
            return a7;
        else if (mindiff == d8)
            return a8;
        else if (mindiff == d6)
            return a6;
        else if (mindiff == d2)
            return a2;
        else if (mindiff == d3)
            return a3;
        else if (mindiff == d1)
            return a1;
        else if (mindiff == d5)
            return a5;
        else if (mindiff == dc)
            return c;
        else
            return a4;
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i d1u = abs_dif_epu16(cr, a1);
        const __m128i d2u = abs_dif_epu16(cr, a2);
        const __m128i d3u = abs_dif_epu16(cr, a3);
        const __m128i d4u = abs_dif_epu16(cr, a4);
        const __m128i d5u = abs_dif_epu16(cr, a5);
        const __m128i d6u = abs_dif_epu16(cr, a6);
        const __m128i d7u = abs_dif_epu16(cr, a7);
        const __m128i d8u = abs_dif_epu16(cr, a8);
        const __m128i dcu = abs_dif_epu16(cr, c);

        const __m128i d1 = _mm_xor_si128(d1u, mask_sign);
        const __m128i d2 = _mm_xor_si128(d2u, mask_sign);
        const __m128i d3 = _mm_xor_si128(d3u, mask_sign);
        const __m128i d4 = _mm_xor_si128(d4u, mask_sign);
        const __m128i d5 = _mm_xor_si128(d5u, mask_sign);
        const __m128i d6 = _mm_xor_si128(d6u, mask_sign);
        const __m128i d7 = _mm_xor_si128(d7u, mask_sign);
        const __m128i d8 = _mm_xor_si128(d8u, mask_sign);
        const __m128i dc = _mm_xor_si128(dcu, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(_mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4)), _mm_min_epi16(_mm_min_epi16(d5, d6), _mm_min_epi16(d7, d8))), dc);

        __m128i result = select_16_equ(mindiff, d4, a4, c);
        result = select_16_equ(mindiff, dc, c, result);
        result = select_16_equ(mindiff, d5, a5, result);
        result = select_16_equ(mindiff, d1, a1, result);
        result = select_16_equ(mindiff, d3, a3, result);
        result = select_16_equ(mindiff, d2, a2, result);
        result = select_16_equ(mindiff, d6, a6, result);
        result = select_16_equ(mindiff, d8, a8, result);
        return select_16_equ(mindiff, d7, a7, result);
    }
#endif
};

class OpRG12
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [2-1], c);
        const int        ma = std::max (a [7-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a2);
        sort_pair (a3, a4);
        sort_pair (a5, a6);
        sort_pair (a7, a8);

        sort_pair (a1, a3);
        sort_pair (a2, a4);
        sort_pair (a5, a7);
        sort_pair (a6, a8);

        sort_pair (a2, a3);
        sort_pair (a6, a7);

        a5 = _mm_max_epi16 (a1, a5);    // sort_pair (a1, a5);
        sort_pair (a2, a6);
        sort_pair (a3, a7);
        a4 = _mm_min_epi16 (a4, a8);    // sort_pair (a4, a8);

        a3 = _mm_min_epi16 (a3, a5);    // sort_pair (a3, a5);
        a6 = _mm_max_epi16 (a4, a6);    // sort_pair (a4, a6);

        a2 = _mm_min_epi16 (a2, a3);    // sort_pair (a2, a3);
        a7 = _mm_max_epi16 (a6, a7);    // sort_pair (a6, a7);

        const __m128i    mi = _mm_min_epi16 (c, a2);
        const __m128i    ma = _mm_max_epi16 (c, a7);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, mi), ma));
    }
#endif
};

class OpRG13
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [3-1], c);
        const int        ma = std::max (a [6-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a2);
        sort_pair (a3, a4);
        sort_pair (a5, a6);
        sort_pair (a7, a8);

        sort_pair (a1, a3);
        sort_pair (a2, a4);
        sort_pair (a5, a7);
        sort_pair (a6, a8);

        sort_pair (a2, a3);
        sort_pair (a6, a7);

        a5 = _mm_max_epi16 (a1, a5);    // sort_pair (a1, a5);
        sort_pair (a2, a6);
        sort_pair (a3, a7);
        a4 = _mm_min_epi16 (a4, a8);    // sort_pair (a4, a8);

        a3 = _mm_min_epi16 (a3, a5);    // sort_pair (a3, a5);
        a6 = _mm_max_epi16 (a4, a6);    // sort_pair (a4, a6);

        a3 = _mm_max_epi16 (a2, a3);    // sort_pair (a2, a3);
        a6 = _mm_min_epi16 (a6, a7);    // sort_pair (a6, a7);

        const __m128i    mi = _mm_min_epi16 (c, a3);
        const __m128i    ma = _mm_max_epi16 (c, a6);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, mi), ma));
    }
#endif
};

class OpRG14
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [4-1], c);
        const int        ma = std::max (a [5-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        sort_pair (a1, a2);
        sort_pair (a3, a4);
        sort_pair (a5, a6);
        sort_pair (a7, a8);

        sort_pair (a1, a3);
        sort_pair (a2, a4);
        sort_pair (a5, a7);
        sort_pair (a6, a8);

        sort_pair (a2, a3);
        sort_pair (a6, a7);

        a5 = _mm_max_epi16 (a1, a5);    // sort_pair (a1, a5);
        a6 = _mm_max_epi16 (a2, a6);    // sort_pair (a2, a6);
        a3 = _mm_min_epi16 (a3, a7);    // sort_pair (a3, a7);
        a4 = _mm_min_epi16 (a4, a8);    // sort_pair (a4, a8);

        a5 = _mm_max_epi16 (a3, a5);    // sort_pair (a3, a5);
        a4 = _mm_min_epi16 (a4, a6);    // sort_pair (a4, a6);

        sort_pair (a4, a5);

        const __m128i    mi = _mm_min_epi16 (c, a4);
        const __m128i    ma = _mm_max_epi16 (c, a5);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, mi), ma));
    }
#endif
};

class OpRG15 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        AvsFilterRepair16_SORT_AXIS_CPP

        const int      c1 = std::abs(c - limit(c, mi1, ma1));
        const int      c2 = std::abs(c - limit(c, mi2, ma2));
        const int      c3 = std::abs(c - limit(c, mi3, ma3));
        const int      c4 = std::abs(c - limit(c, mi4, ma4));

        const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        int            mi;
        int            ma;
        if (mindiff == c4) {
            mi = mi4;
            ma = ma4;
        } else if (mindiff == c2) {
            mi = mi2;
            ma = ma2;
        } else if (mindiff == c3) {
            mi = mi3;
            ma = ma3;
        } else {
            mi = mi1;
            ma = ma1;
        }

        mi = std::min(mi, c);
        ma = std::max(ma, c);

        return (limit(cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX
        AvsFilterRepair16_SORT_AXIS_SSE2

        const __m128i cma1 = _mm_max_epi16(c, ma1);
        const __m128i cma2 = _mm_max_epi16(c, ma2);
        const __m128i cma3 = _mm_max_epi16(c, ma3);
        const __m128i cma4 = _mm_max_epi16(c, ma4);

        const __m128i cmi1 = _mm_min_epi16(c, mi1);
        const __m128i cmi2 = _mm_min_epi16(c, mi2);
        const __m128i cmi3 = _mm_min_epi16(c, mi3);
        const __m128i cmi4 = _mm_min_epi16(c, mi4);

        const __m128i clipped1 = limit_epi16(c, mi1, ma1);
        const __m128i clipped2 = limit_epi16(c, mi2, ma2);
        const __m128i clipped3 = limit_epi16(c, mi3, ma3);
        const __m128i clipped4 = limit_epi16(c, mi4, ma4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cu = _mm_xor_si128(c, mask_sign);

        const __m128i c1u = abs_dif_epu16(cu, clipped1u);
        const __m128i c2u = abs_dif_epu16(cu, clipped2u);
        const __m128i c3u = abs_dif_epu16(cu, clipped3u);
        const __m128i c4u = abs_dif_epu16(cu, clipped4u);

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, limit_epi16(cr, cmi1, cma1), cr);
        result = select_16_equ(mindiff, c3, limit_epi16(cr, cmi3, cma3), result);
        result = select_16_equ(mindiff, c2, limit_epi16(cr, cmi2, cma2), result);
        return select_16_equ(mindiff, c4, limit_epi16(cr, cmi4, cma4), result);
    }
#endif
};

class OpRG16 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        AvsFilterRepair16_SORT_AXIS_CPP

        const int      d1 = ma1 - mi1;
        const int      d2 = ma2 - mi2;
        const int      d3 = ma3 - mi3;
        const int      d4 = ma4 - mi4;

        const int      c1 = limit((std::abs(c - limit(c, mi1, ma1)) << 1) + d1, 0, 0xFFFF);
        const int      c2 = limit((std::abs(c - limit(c, mi2, ma2)) << 1) + d2, 0, 0xFFFF);
        const int      c3 = limit((std::abs(c - limit(c, mi3, ma3)) << 1) + d3, 0, 0xFFFF);
        const int      c4 = limit((std::abs(c - limit(c, mi4, ma4)) << 1) + d4, 0, 0xFFFF);

        const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

        int            mi;
        int            ma;
        if (mindiff == c4) {
            mi = mi4;
            ma = ma4;
        } else if (mindiff == c2) {
            mi = mi2;
            ma = ma2;
        } else if (mindiff == c3) {
            mi = mi3;
            ma = ma3;
        } else {
            mi = mi1;
            ma = ma1;
        }

        mi = std::min(mi, c);
        ma = std::max(ma, c);

        return (limit(cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX
        AvsFilterRepair16_SORT_AXIS_SSE2

        const __m128i cma1 = _mm_max_epi16(c, ma1);
        const __m128i cma2 = _mm_max_epi16(c, ma2);
        const __m128i cma3 = _mm_max_epi16(c, ma3);
        const __m128i cma4 = _mm_max_epi16(c, ma4);

        const __m128i cmi1 = _mm_min_epi16(c, mi1);
        const __m128i cmi2 = _mm_min_epi16(c, mi2);
        const __m128i cmi3 = _mm_min_epi16(c, mi3);
        const __m128i cmi4 = _mm_min_epi16(c, mi4);

        const __m128i d1 = _mm_sub_epi16(ma1, mi1);
        const __m128i d2 = _mm_sub_epi16(ma2, mi2);
        const __m128i d3 = _mm_sub_epi16(ma3, mi3);
        const __m128i d4 = _mm_sub_epi16(ma4, mi4);

        const __m128i clipped1 = limit_epi16(c, mi1, ma1);
        const __m128i clipped2 = limit_epi16(c, mi2, ma2);
        const __m128i clipped3 = limit_epi16(c, mi3, ma3);
        const __m128i clipped4 = limit_epi16(c, mi4, ma4);

        const __m128i clipped1u = _mm_xor_si128(clipped1, mask_sign);
        const __m128i clipped2u = _mm_xor_si128(clipped2, mask_sign);
        const __m128i clipped3u = _mm_xor_si128(clipped3, mask_sign);
        const __m128i clipped4u = _mm_xor_si128(clipped4, mask_sign);
        const __m128i cu = _mm_xor_si128(c, mask_sign);

        const __m128i absdiff1 = abs_dif_epu16(cu, clipped1u);
        const __m128i absdiff2 = abs_dif_epu16(cu, clipped2u);
        const __m128i absdiff3 = abs_dif_epu16(cu, clipped3u);
        const __m128i absdiff4 = abs_dif_epu16(cu, clipped4u);

        const __m128i c1u = _mm_adds_epu16(_mm_adds_epu16(absdiff1, absdiff1), d1);
        const __m128i c2u = _mm_adds_epu16(_mm_adds_epu16(absdiff2, absdiff2), d2);
        const __m128i c3u = _mm_adds_epu16(_mm_adds_epu16(absdiff3, absdiff3), d3);
        const __m128i c4u = _mm_adds_epu16(_mm_adds_epu16(absdiff4, absdiff4), d4);

        const __m128i c1 = _mm_xor_si128(c1u, mask_sign);
        const __m128i c2 = _mm_xor_si128(c2u, mask_sign);
        const __m128i c3 = _mm_xor_si128(c3u, mask_sign);
        const __m128i c4 = _mm_xor_si128(c4u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(c1, c2), _mm_min_epi16(c3, c4));

        __m128i result = select_16_equ(mindiff, c1, limit_epi16(cr, cmi1, cma1), cr);
        result = select_16_equ(mindiff, c3, limit_epi16(cr, cmi3, cma3), result);
        result = select_16_equ(mindiff, c2, limit_epi16(cr, cmi2, cma2), result);
        return select_16_equ(mindiff, c4, limit_epi16(cr, cmi4, cma4), result);
    }
#endif
};

class OpRG17 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        AvsFilterRepair16_SORT_AXIS_CPP

        const int      l = std::max(std::max(mi1, mi2), std::max(mi3, mi4));
        const int      u = std::min(std::min(ma1, ma2), std::min(ma3, ma4));

        const int      mi = std::min(std::min(l, u), c);
        const int      ma = std::max(std::max(l, u), c);

        return (limit(cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX
        AvsFilterRepair16_SORT_AXIS_SSE2

        const __m128i lower = _mm_max_epi16(_mm_max_epi16(mi1, mi2), _mm_max_epi16(mi3, mi4));
        const __m128i upper = _mm_min_epi16(_mm_min_epi16(ma1, ma2), _mm_min_epi16(ma3, ma4));

        const __m128i real_upper = _mm_max_epi16(_mm_max_epi16(upper, lower), c);
        const __m128i real_lower = _mm_min_epi16(_mm_min_epi16(upper, lower), c);

        return limit_epi16(cr, real_lower, real_upper);
    }
#endif
};

class OpRG18 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int      d1 = std::max(std::abs(c - a1), std::abs(c - a8));
        const int      d2 = std::max(std::abs(c - a2), std::abs(c - a7));
        const int      d3 = std::max(std::abs(c - a3), std::abs(c - a6));
        const int      d4 = std::max(std::abs(c - a4), std::abs(c - a5));

        const int      mindiff = std::min(std::min(d1, d2), std::min(d3, d4));

        int            mi;
        int            ma;
        if (mindiff == d4) {
            mi = std::min(a4, a5);
            ma = std::max(a4, a5);
        } else if (mindiff == d2) {
            mi = std::min(a2, a7);
            ma = std::max(a2, a7);
        } else if (mindiff == d3) {
            mi = std::min(a3, a6);
            ma = std::max(a3, a6);
        } else {
            mi = std::min(a1, a8);
            ma = std::max(a1, a8);
        }

        mi = std::min(mi, c);
        ma = std::max(ma, c);

        return (limit(cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i a1u = _mm_xor_si128(a1, mask_sign);
        const __m128i a2u = _mm_xor_si128(a2, mask_sign);
        const __m128i a3u = _mm_xor_si128(a3, mask_sign);
        const __m128i a4u = _mm_xor_si128(a4, mask_sign);
        const __m128i a5u = _mm_xor_si128(a5, mask_sign);
        const __m128i a6u = _mm_xor_si128(a6, mask_sign);
        const __m128i a7u = _mm_xor_si128(a7, mask_sign);
        const __m128i a8u = _mm_xor_si128(a8, mask_sign);
        const __m128i cu = _mm_xor_si128(c, mask_sign);

        const __m128i absdiff1u = abs_dif_epu16(cu, a1u);
        const __m128i absdiff2u = abs_dif_epu16(cu, a2u);
        const __m128i absdiff3u = abs_dif_epu16(cu, a3u);
        const __m128i absdiff4u = abs_dif_epu16(cu, a4u);
        const __m128i absdiff5u = abs_dif_epu16(cu, a5u);
        const __m128i absdiff6u = abs_dif_epu16(cu, a6u);
        const __m128i absdiff7u = abs_dif_epu16(cu, a7u);
        const __m128i absdiff8u = abs_dif_epu16(cu, a8u);

        const __m128i absdiff1 = _mm_xor_si128(absdiff1u, mask_sign);
        const __m128i absdiff2 = _mm_xor_si128(absdiff2u, mask_sign);
        const __m128i absdiff3 = _mm_xor_si128(absdiff3u, mask_sign);
        const __m128i absdiff4 = _mm_xor_si128(absdiff4u, mask_sign);
        const __m128i absdiff5 = _mm_xor_si128(absdiff5u, mask_sign);
        const __m128i absdiff6 = _mm_xor_si128(absdiff6u, mask_sign);
        const __m128i absdiff7 = _mm_xor_si128(absdiff7u, mask_sign);
        const __m128i absdiff8 = _mm_xor_si128(absdiff8u, mask_sign);

        const __m128i d1 = _mm_max_epi16(absdiff1, absdiff8);
        const __m128i d2 = _mm_max_epi16(absdiff2, absdiff7);
        const __m128i d3 = _mm_max_epi16(absdiff3, absdiff6);
        const __m128i d4 = _mm_max_epi16(absdiff4, absdiff5);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4));

        const __m128i mi1 = _mm_min_epi16(c, _mm_min_epi16(a1, a8));
        const __m128i mi2 = _mm_min_epi16(c, _mm_min_epi16(a2, a7));
        const __m128i mi3 = _mm_min_epi16(c, _mm_min_epi16(a3, a6));
        const __m128i mi4 = _mm_min_epi16(c, _mm_min_epi16(a4, a5));

        const __m128i ma1 = _mm_max_epi16(c, _mm_max_epi16(a1, a8));
        const __m128i ma2 = _mm_max_epi16(c, _mm_max_epi16(a2, a7));
        const __m128i ma3 = _mm_max_epi16(c, _mm_max_epi16(a3, a6));
        const __m128i ma4 = _mm_max_epi16(c, _mm_max_epi16(a4, a5));

        const __m128i c1 = limit_epi16(cr, mi1, ma1);
        const __m128i c2 = limit_epi16(cr, mi2, ma2);
        const __m128i c3 = limit_epi16(cr, mi3, ma3);
        const __m128i c4 = limit_epi16(cr, mi4, ma4);

        __m128i result = select_16_equ(mindiff, d1, c1, cr);
        result = select_16_equ(mindiff, d3, c3, result);
        result = select_16_equ(mindiff, d2, c2, result);
        return select_16_equ(mindiff, d4, c4, result);
    }
#endif
};

class OpRG19 {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int d1 = std::abs(c - a1);
        const int d2 = std::abs(c - a2);
        const int d3 = std::abs(c - a3);
        const int d4 = std::abs(c - a4);
        const int d5 = std::abs(c - a5);
        const int d6 = std::abs(c - a6);
        const int d7 = std::abs(c - a7);
        const int d8 = std::abs(c - a8);

        const int mindiff = std::min(std::min(std::min(d1, d2), std::min(d3, d4)), std::min(std::min(d5, d6), std::min(d7, d8)));

        return limit(cr, limit(c - mindiff, 0, 0xFFFF), limit(c + mindiff, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i d1u = abs_dif_epu16(c, a1);
        const __m128i d2u = abs_dif_epu16(c, a2);
        const __m128i d3u = abs_dif_epu16(c, a3);
        const __m128i d4u = abs_dif_epu16(c, a4);
        const __m128i d5u = abs_dif_epu16(c, a5);
        const __m128i d6u = abs_dif_epu16(c, a6);
        const __m128i d7u = abs_dif_epu16(c, a7);
        const __m128i d8u = abs_dif_epu16(c, a8);

        const __m128i d1 = _mm_xor_si128(d1u, mask_sign);
        const __m128i d2 = _mm_xor_si128(d2u, mask_sign);
        const __m128i d3 = _mm_xor_si128(d3u, mask_sign);
        const __m128i d4 = _mm_xor_si128(d4u, mask_sign);
        const __m128i d5 = _mm_xor_si128(d5u, mask_sign);
        const __m128i d6 = _mm_xor_si128(d6u, mask_sign);
        const __m128i d7 = _mm_xor_si128(d7u, mask_sign);
        const __m128i d8 = _mm_xor_si128(d8u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4)), _mm_min_epi16(_mm_min_epi16(d5, d6), _mm_min_epi16(d7, d8)));

        const __m128i mindiffu = _mm_xor_si128(mindiff, mask_sign);

        const __m128i mi = _mm_xor_si128(_mm_subs_epu16(c, mindiffu), mask_sign);
        const __m128i ma = _mm_xor_si128(_mm_adds_epu16(c, mindiffu), mask_sign);

        return _mm_xor_si128(limit_epi16(_mm_xor_si128(cr, mask_sign), mi, ma), mask_sign);
    }
#endif
};

class OpRG20 {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int d1 = std::abs(c - a1);
        const int d2 = std::abs(c - a2);
        const int d3 = std::abs(c - a3);
        const int d4 = std::abs(c - a4);
        const int d5 = std::abs(c - a5);
        const int d6 = std::abs(c - a6);
        const int d7 = std::abs(c - a7);
        const int d8 = std::abs(c - a8);

        int mindiff = std::min(d1, d2);
        int maxdiff = std::max(d1, d2);

        maxdiff = limit(maxdiff, mindiff, d3);
        mindiff = std::min(mindiff, d3);

        maxdiff = limit(maxdiff, mindiff, d4);
        mindiff = std::min(mindiff, d4);

        maxdiff = limit(maxdiff, mindiff, d5);
        mindiff = std::min(mindiff, d5);

        maxdiff = limit(maxdiff, mindiff, d6);
        mindiff = std::min(mindiff, d6);

        maxdiff = limit(maxdiff, mindiff, d7);
        mindiff = std::min(mindiff, d7);

        maxdiff = limit(maxdiff, mindiff, d8);

        return limit(cr, limit(c - maxdiff, 0, 0xFFFF), limit(c + maxdiff, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i d1u = abs_dif_epu16(c, a1);
        const __m128i d2u = abs_dif_epu16(c, a2);
        const __m128i d3u = abs_dif_epu16(c, a3);
        const __m128i d4u = abs_dif_epu16(c, a4);
        const __m128i d5u = abs_dif_epu16(c, a5);
        const __m128i d6u = abs_dif_epu16(c, a6);
        const __m128i d7u = abs_dif_epu16(c, a7);
        const __m128i d8u = abs_dif_epu16(c, a8);

        const __m128i d1 = _mm_xor_si128(d1u, mask_sign);
        const __m128i d2 = _mm_xor_si128(d2u, mask_sign);
        const __m128i d3 = _mm_xor_si128(d3u, mask_sign);
        const __m128i d4 = _mm_xor_si128(d4u, mask_sign);
        const __m128i d5 = _mm_xor_si128(d5u, mask_sign);
        const __m128i d6 = _mm_xor_si128(d6u, mask_sign);
        const __m128i d7 = _mm_xor_si128(d7u, mask_sign);
        const __m128i d8 = _mm_xor_si128(d8u, mask_sign);

        __m128i mindiff = _mm_min_epi16(d1, d2);
        __m128i maxdiff = _mm_max_epi16(d1, d2);

        maxdiff = limit_epi16(maxdiff, mindiff, d3);
        mindiff = _mm_min_epi16(mindiff, d3);

        maxdiff = limit_epi16(maxdiff, mindiff, d4);
        mindiff = _mm_min_epi16(mindiff, d4);

        maxdiff = limit_epi16(maxdiff, mindiff, d5);
        mindiff = _mm_min_epi16(mindiff, d5);

        maxdiff = limit_epi16(maxdiff, mindiff, d6);
        mindiff = _mm_min_epi16(mindiff, d6);

        maxdiff = limit_epi16(maxdiff, mindiff, d7);
        mindiff = _mm_min_epi16(mindiff, d7);

        maxdiff = limit_epi16(maxdiff, mindiff, d8);

        const __m128i maxdiffu = _mm_xor_si128(maxdiff, mask_sign);

        const __m128i mi = _mm_xor_si128(_mm_subs_epu16(c, maxdiffu), mask_sign);
        const __m128i ma = _mm_xor_si128(_mm_adds_epu16(c, maxdiffu), mask_sign);

        return _mm_xor_si128(limit_epi16(_mm_xor_si128(cr, mask_sign), mi, ma), mask_sign);
    }
#endif
};

class OpRG21 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        AvsFilterRepair16_SORT_AXIS_CPP

        const int d1 = limit(ma1 - c, 0, 0xFFFF);
        const int d2 = limit(ma2 - c, 0, 0xFFFF);
        const int d3 = limit(ma3 - c, 0, 0xFFFF);
        const int d4 = limit(ma4 - c, 0, 0xFFFF);

        const int rd1 = limit(c - mi1, 0, 0xFFFF);
        const int rd2 = limit(c - mi2, 0, 0xFFFF);
        const int rd3 = limit(c - mi3, 0, 0xFFFF);
        const int rd4 = limit(c - mi4, 0, 0xFFFF);

        const int u1 = std::max(d1, rd1);
        const int u2 = std::max(d2, rd2);
        const int u3 = std::max(d3, rd3);
        const int u4 = std::max(d4, rd4);

        const int u = std::min(std::min(u1, u2), std::min(u3, u4));

        return limit(cr, limit(c - u, 0, 0xFFFF), limit(c + u, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX
        AvsFilterRepair16_SORT_AXIS_SSE2

        const __m128i d1 = _mm_subs_epi16(ma1, c);
        const __m128i d2 = _mm_subs_epi16(ma2, c);
        const __m128i d3 = _mm_subs_epi16(ma3, c);
        const __m128i d4 = _mm_subs_epi16(ma4, c);

        const __m128i rd1 = _mm_subs_epi16(c, mi1);
        const __m128i rd2 = _mm_subs_epi16(c, mi2);
        const __m128i rd3 = _mm_subs_epi16(c, mi3);
        const __m128i rd4 = _mm_subs_epi16(c, mi4);

        const __m128i u1 = _mm_max_epi16(d1, rd1);
        const __m128i u2 = _mm_max_epi16(d2, rd2);
        const __m128i u3 = _mm_max_epi16(d3, rd3);
        const __m128i u4 = _mm_max_epi16(d4, rd4);

        const __m128i u = _mm_min_epi16(_mm_min_epi16(u1, u2), _mm_min_epi16(u3, u4));

        const __m128i mi = _mm_subs_epi16(c, u);
        const __m128i ma = _mm_adds_epi16(c, u);

        return limit_epi16(cr, mi, ma);
    }
#endif
};

class OpRG22 {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int d1 = std::abs(cr - a1);
        const int d2 = std::abs(cr - a2);
        const int d3 = std::abs(cr - a3);
        const int d4 = std::abs(cr - a4);
        const int d5 = std::abs(cr - a5);
        const int d6 = std::abs(cr - a6);
        const int d7 = std::abs(cr - a7);
        const int d8 = std::abs(cr - a8);

        const int mindiff = std::min(std::min(std::min(d1, d2), std::min(d3, d4)), std::min(std::min(d5, d6), std::min(d7, d8)));

        return limit(c, limit(cr - mindiff, 0, 0xFFFF), limit(cr + mindiff, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i d1u = abs_dif_epu16(cr, a1);
        const __m128i d2u = abs_dif_epu16(cr, a2);
        const __m128i d3u = abs_dif_epu16(cr, a3);
        const __m128i d4u = abs_dif_epu16(cr, a4);
        const __m128i d5u = abs_dif_epu16(cr, a5);
        const __m128i d6u = abs_dif_epu16(cr, a6);
        const __m128i d7u = abs_dif_epu16(cr, a7);
        const __m128i d8u = abs_dif_epu16(cr, a8);

        const __m128i d1 = _mm_xor_si128(d1u, mask_sign);
        const __m128i d2 = _mm_xor_si128(d2u, mask_sign);
        const __m128i d3 = _mm_xor_si128(d3u, mask_sign);
        const __m128i d4 = _mm_xor_si128(d4u, mask_sign);
        const __m128i d5 = _mm_xor_si128(d5u, mask_sign);
        const __m128i d6 = _mm_xor_si128(d6u, mask_sign);
        const __m128i d7 = _mm_xor_si128(d7u, mask_sign);
        const __m128i d8 = _mm_xor_si128(d8u, mask_sign);

        const __m128i mindiff = _mm_min_epi16(_mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4)), _mm_min_epi16(_mm_min_epi16(d5, d6), _mm_min_epi16(d7, d8)));

        const __m128i mindiffu = _mm_xor_si128(mindiff, mask_sign);

        const __m128i mi = _mm_xor_si128(_mm_subs_epu16(cr, mindiffu), mask_sign);
        const __m128i ma = _mm_xor_si128(_mm_adds_epu16(cr, mindiffu), mask_sign);

        return _mm_xor_si128(limit_epi16(_mm_xor_si128(c, mask_sign), mi, ma), mask_sign);
    }
#endif
};

class OpRG23 {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        const int d1 = std::abs(cr - a1);
        const int d2 = std::abs(cr - a2);
        const int d3 = std::abs(cr - a3);
        const int d4 = std::abs(cr - a4);
        const int d5 = std::abs(cr - a5);
        const int d6 = std::abs(cr - a6);
        const int d7 = std::abs(cr - a7);
        const int d8 = std::abs(cr - a8);

        int mindiff = std::min(d1, d2);
        int maxdiff = std::max(d1, d2);

        maxdiff = limit(maxdiff, mindiff, d3);
        mindiff = std::min(mindiff, d3);

        maxdiff = limit(maxdiff, mindiff, d4);
        mindiff = std::min(mindiff, d4);

        maxdiff = limit(maxdiff, mindiff, d5);
        mindiff = std::min(mindiff, d5);

        maxdiff = limit(maxdiff, mindiff, d6);
        mindiff = std::min(mindiff, d6);

        maxdiff = limit(maxdiff, mindiff, d7);
        mindiff = std::min(mindiff, d7);

        maxdiff = limit(maxdiff, mindiff, d8);

        return limit(c, limit(cr - maxdiff, 0, 0xFFFF), limit(cr + maxdiff, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX

        const __m128i d1u = abs_dif_epu16(cr, a1);
        const __m128i d2u = abs_dif_epu16(cr, a2);
        const __m128i d3u = abs_dif_epu16(cr, a3);
        const __m128i d4u = abs_dif_epu16(cr, a4);
        const __m128i d5u = abs_dif_epu16(cr, a5);
        const __m128i d6u = abs_dif_epu16(cr, a6);
        const __m128i d7u = abs_dif_epu16(cr, a7);
        const __m128i d8u = abs_dif_epu16(cr, a8);

        const __m128i d1 = _mm_xor_si128(d1u, mask_sign);
        const __m128i d2 = _mm_xor_si128(d2u, mask_sign);
        const __m128i d3 = _mm_xor_si128(d3u, mask_sign);
        const __m128i d4 = _mm_xor_si128(d4u, mask_sign);
        const __m128i d5 = _mm_xor_si128(d5u, mask_sign);
        const __m128i d6 = _mm_xor_si128(d6u, mask_sign);
        const __m128i d7 = _mm_xor_si128(d7u, mask_sign);
        const __m128i d8 = _mm_xor_si128(d8u, mask_sign);

        __m128i mindiff = _mm_min_epi16(d1, d2);
        __m128i maxdiff = _mm_max_epi16(d1, d2);

        maxdiff = limit_epi16(maxdiff, mindiff, d3);
        mindiff = _mm_min_epi16(mindiff, d3);

        maxdiff = limit_epi16(maxdiff, mindiff, d4);
        mindiff = _mm_min_epi16(mindiff, d4);

        maxdiff = limit_epi16(maxdiff, mindiff, d5);
        mindiff = _mm_min_epi16(mindiff, d5);

        maxdiff = limit_epi16(maxdiff, mindiff, d6);
        mindiff = _mm_min_epi16(mindiff, d6);

        maxdiff = limit_epi16(maxdiff, mindiff, d7);
        mindiff = _mm_min_epi16(mindiff, d7);

        maxdiff = limit_epi16(maxdiff, mindiff, d8);

        const __m128i maxdiffu = _mm_xor_si128(maxdiff, mask_sign);

        const __m128i mi = _mm_xor_si128(_mm_subs_epu16(cr, maxdiffu), mask_sign);
        const __m128i ma = _mm_xor_si128(_mm_adds_epu16(cr, maxdiffu), mask_sign);

        return _mm_xor_si128(limit_epi16(_mm_xor_si128(c, mask_sign), mi, ma), mask_sign);
    }
#endif
};

class OpRG24 {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        AvsFilterRepair16_SORT_AXIS_CPP

        const int d1 = limit(ma1 - cr, 0, 0xFFFF);
        const int d2 = limit(ma2 - cr, 0, 0xFFFF);
        const int d3 = limit(ma3 - cr, 0, 0xFFFF);
        const int d4 = limit(ma4 - cr, 0, 0xFFFF);

        const int rd1 = limit(cr - mi1, 0, 0xFFFF);
        const int rd2 = limit(cr - mi2, 0, 0xFFFF);
        const int rd3 = limit(cr - mi3, 0, 0xFFFF);
        const int rd4 = limit(cr - mi4, 0, 0xFFFF);

        const int u1 = std::max(d1, rd1);
        const int u2 = std::max(d2, rd2);
        const int u3 = std::max(d3, rd3);
        const int u4 = std::max(d4, rd4);

        const int u = std::min(std::min(u1, u2), std::min(u3, u4));

        return limit(c, limit(cr - u, 0, 0xFFFF), limit(cr + u, 0, 0xFFFF));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src2, __m128i mask_sign) {
        AvsFilterRepair16_READ_PIX
        AvsFilterRepair16_SORT_AXIS_SSE2

        const __m128i d1 = _mm_subs_epi16(ma1, cr);
        const __m128i d2 = _mm_subs_epi16(ma2, cr);
        const __m128i d3 = _mm_subs_epi16(ma3, cr);
        const __m128i d4 = _mm_subs_epi16(ma4, cr);

        const __m128i rd1 = _mm_subs_epi16(cr, mi1);
        const __m128i rd2 = _mm_subs_epi16(cr, mi2);
        const __m128i rd3 = _mm_subs_epi16(cr, mi3);
        const __m128i rd4 = _mm_subs_epi16(cr, mi4);

        const __m128i u1 = _mm_max_epi16(d1, rd1);
        const __m128i u2 = _mm_max_epi16(d2, rd2);
        const __m128i u3 = _mm_max_epi16(d3, rd3);
        const __m128i u4 = _mm_max_epi16(d4, rd4);

        const __m128i u = _mm_min_epi16(_mm_min_epi16(u1, u2), _mm_min_epi16(u3, u4));

        const __m128i mi = _mm_subs_epi16(cr, u);
        const __m128i ma = _mm_adds_epi16(cr, u);

        return limit_epi16(c, mi, ma);
    }
#endif
};


template <class OP, class T>
class PlaneProc {
public:

static void process_subplane_cpp (const T *src1_ptr, const T *src2_ptr, T *dst_ptr, ptrdiff_t stride, int width, int height)
{
    const int        y_b = 1;
    const int        y_e = height - 1;

    dst_ptr += y_b * stride;
    src1_ptr += y_b * stride;
    src2_ptr += y_b * stride;

    const int        x_e = width - 1;

    for (int y = y_b; y < y_e; ++y)
    {
        dst_ptr [0] = src1_ptr [0];

        process_row_cpp (
            dst_ptr,
            src1_ptr,
            src2_ptr,
            stride,
            1,
            x_e
        );

        dst_ptr [x_e] = src1_ptr [x_e];

        dst_ptr += stride;
        src1_ptr += stride;
        src2_ptr += stride;
    }
}

static void process_row_cpp (T *dst_ptr, const T *src1_ptr, const T *src2_ptr, ptrdiff_t stride_src, int x_beg, int x_end)
{
    const ptrdiff_t      om = stride_src - 1;
    const ptrdiff_t      o0 = stride_src    ;
    const ptrdiff_t      op = stride_src + 1;

    src1_ptr += x_beg;
    src2_ptr += x_beg;

    for (int x = x_beg; x < x_end; ++x)
    {
        const int       cr = src1_ptr [0];
        const int        a1 = src2_ptr [-op];
        const int        a2 = src2_ptr [-o0];
        const int        a3 = src2_ptr [-om];
        const int        a4 = src2_ptr [-1 ];
        const int        c  = src2_ptr [ 0 ];
        const int        a5 = src2_ptr [ 1 ];
        const int        a6 = src2_ptr [ om];
        const int        a7 = src2_ptr [ o0];
        const int        a8 = src2_ptr [ op];

        const int        res = OP::rg (cr, a1, a2, a3, a4, c, a5, a6, a7, a8);

        dst_ptr [x] = res;

        ++ src1_ptr;
        ++ src2_ptr;
    }
}

#ifdef VS_TARGET_CPU_X86
static void process_subplane_sse2 (const T *src1_ptr, const T *src2_ptr, T *dst_ptr, ptrdiff_t stride, int width, int height)
{
    const int        y_b = 1;
    const int        y_e = height - 1;

    dst_ptr += y_b * stride;
    src1_ptr += y_b * stride;
    src2_ptr += y_b * stride;

    const __m128i    mask_sign = _mm_set1_epi16 (-0x8000);

    const int        x_e =   width - 1;
    const int        w8  = ((width - 2) & -8) + 1;

    for (int y = y_b; y < y_e; ++y)
    {
        dst_ptr [0] = src1_ptr [0];

        for (int x = 1; x < w8; x += 8)
        {
            __m128i            res = OP::rg (
                src1_ptr + x,
                src2_ptr + x,
                stride,
                mask_sign
            );

            res = OP::ConvSign::cv (res, mask_sign);
            if (sizeof(T) == 1)
                _mm_storel_epi64 (reinterpret_cast<__m128i *>(dst_ptr + x), _mm_packus_epi16 (res, res));
            else
                _mm_storeu_si128 (reinterpret_cast<__m128i *>(dst_ptr + x), res);
        }

        process_row_cpp (
            dst_ptr,
            src1_ptr,
            src2_ptr,
            stride,
            w8,
            x_e
        );

        dst_ptr [x_e] = src1_ptr [x_e];

        dst_ptr += stride;
        src1_ptr += stride;
        src2_ptr += stride;
    }
}

template <class OP1, class T1>
static void do_process_plane_sse2 (const VSFrame *src1_frame, const VSFrame *src2_frame, VSFrame *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int        w             = vsapi->getFrameWidth(src1_frame, plane_id);
    const int        h             = vsapi->getFrameHeight(src1_frame, plane_id);
    T1 *             dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const ptrdiff_t  stride        = vsapi->getStride(src1_frame, plane_id);

    const T1*        src1_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src1_frame, plane_id));
    const T1*        src2_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src2_frame, plane_id));

    // First line
    memcpy (dst_ptr, src1_ptr, stride);

    // Main content
    PlaneProc<OP1, T1>::process_subplane_sse2(src1_ptr, src2_ptr, dst_ptr, stride/sizeof(T1), w, h);

    // Last line
    const ptrdiff_t  lp = (h - 1) * stride/sizeof(T1);
    memcpy (dst_ptr + lp, src1_ptr + lp, stride);
}

#endif

template <class OP1, class T1>
static void do_process_plane_cpp (const VSFrame *src1_frame, const VSFrame *src2_frame, VSFrame *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int         w             = vsapi->getFrameWidth(src1_frame, plane_id);
    const int         h             = vsapi->getFrameHeight(src1_frame, plane_id);
    T1 *             dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const ptrdiff_t  stride        = vsapi->getStride(src1_frame, plane_id);

    const T1*        src1_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src1_frame, plane_id));
    const T1*        src2_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src2_frame, plane_id));

    // First line
    memcpy (dst_ptr, src1_ptr, stride);

    // Main content
    PlaneProc<OP1, T1>::process_subplane_cpp(src1_ptr, src2_ptr, dst_ptr, stride/sizeof(T1), w, h);

    // Last line
    const int        lp = (h - 1) * stride/sizeof(T1);
    memcpy (dst_ptr + lp, src1_ptr + lp, stride);
}

};

typedef struct {
    VSNode *node1;
    VSNode *node2;
    const VSVideoInfo *vi;
    int mode[3];
} RepairData;

static const VSFrame *VS_CC repairGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    RepairData *d = static_cast<RepairData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1_frame = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2_frame = vsapi->getFrameFilter(n, d->node2, frameCtx);
        int planes[3] = {0, 1, 2};
        const VSFrame * cp_planes[3] = { d->mode[0] ? nullptr : src1_frame, d->mode[1] ? nullptr : src1_frame, d->mode[2] ? nullptr : src1_frame };
        VSFrame *dst_frame = vsapi->newVideoFrame2(vsapi->getVideoFrameFormat(src1_frame), vsapi->getFrameWidth(src1_frame, 0), vsapi->getFrameHeight(src1_frame, 0), cp_planes, planes, src1_frame, core);


#define PROC_ARGS_16(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint16_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint8_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;

#ifdef VS_TARGET_CPU_X86
#define PROC_ARGS_16_FAST(op) PlaneProc <op, uint16_t>::do_process_plane_sse2<op, uint16_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8_FAST(op) PlaneProc <op, uint8_t>::do_process_plane_sse2<op, uint8_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#else
#define PROC_ARGS_16_FAST(op) PROC_ARGS_16(op)
#define PROC_ARGS_8_FAST(op) PROC_ARGS_8(op)
#endif


        if (d->vi->format.bytesPerSample == 1) {
            for (int i = 0; i < d->vi->format.numPlanes; i++) {
                switch (d->mode[i])
                {
                    case  1: PROC_ARGS_8_FAST(OpRG01)
                    case  2: PROC_ARGS_8_FAST(OpRG02)
                    case  3: PROC_ARGS_8_FAST(OpRG03)
                    case  4: PROC_ARGS_8_FAST(OpRG04)
                    case  5: PROC_ARGS_8_FAST(OpRG05)
                    case  6: PROC_ARGS_8_FAST(OpRG06)
                    case  7: PROC_ARGS_8_FAST(OpRG07)
                    case  8: PROC_ARGS_8_FAST(OpRG08)
                    case  9: PROC_ARGS_8_FAST(OpRG09)
                    case 10: PROC_ARGS_8_FAST(OpRG10)
                    case 11: PROC_ARGS_8_FAST(OpRG01)
                    case 12: PROC_ARGS_8_FAST(OpRG12)
                    case 13: PROC_ARGS_8_FAST(OpRG13)
                    case 14: PROC_ARGS_8_FAST(OpRG14)
                    case 15: PROC_ARGS_8_FAST(OpRG15)
                    case 16: PROC_ARGS_8_FAST(OpRG16)
                    case 17: PROC_ARGS_8_FAST(OpRG17)
                    case 18: PROC_ARGS_8_FAST(OpRG18)
                    case 19: PROC_ARGS_8_FAST(OpRG19)
                    case 20: PROC_ARGS_8_FAST(OpRG20)
                    case 21: PROC_ARGS_8_FAST(OpRG21)
                    case 22: PROC_ARGS_8_FAST(OpRG22)
                    case 23: PROC_ARGS_8_FAST(OpRG23)
                    case 24: PROC_ARGS_8_FAST(OpRG24)
                    default: break;
                }
            }
        } else {
            for (int i = 0; i < d->vi->format.numPlanes; i++) {
                switch (d->mode[i])
                {
                    case  1: PROC_ARGS_16_FAST(OpRG01)
                    case  2: PROC_ARGS_16_FAST(OpRG02)
                    case  3: PROC_ARGS_16_FAST(OpRG03)
                    case  4: PROC_ARGS_16_FAST(OpRG04)
                    case  5: PROC_ARGS_16_FAST(OpRG05)
                    case  6: PROC_ARGS_16_FAST(OpRG06)
                    case  7: PROC_ARGS_16_FAST(OpRG07)
                    case  8: PROC_ARGS_16_FAST(OpRG08)
                    case  9: PROC_ARGS_16_FAST(OpRG09)
                    case 10: PROC_ARGS_16_FAST(OpRG10)
                    case 11: PROC_ARGS_16_FAST(OpRG01)
                    case 12: PROC_ARGS_16_FAST(OpRG12)
                    case 13: PROC_ARGS_16_FAST(OpRG13)
                    case 14: PROC_ARGS_16_FAST(OpRG14)
                    case 15: PROC_ARGS_16_FAST(OpRG15)
                    case 16: PROC_ARGS_16_FAST(OpRG16)
                    case 17: PROC_ARGS_16_FAST(OpRG17)
                    case 18: PROC_ARGS_16_FAST(OpRG18)
                    case 19: PROC_ARGS_16_FAST(OpRG19)
                    case 20: PROC_ARGS_16_FAST(OpRG20)
                    case 21: PROC_ARGS_16_FAST(OpRG21)
                    case 22: PROC_ARGS_16_FAST(OpRG22)
                    case 23: PROC_ARGS_16_FAST(OpRG23)
                    case 24: PROC_ARGS_16_FAST(OpRG24)
                    default: break;
                }
            }
        }

        vsapi->freeFrame(src1_frame);
        vsapi->freeFrame(src2_frame);
        return dst_frame;
    }

    return nullptr;
}

static void VS_CC repairFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    RepairData *d = static_cast<RepairData *>(instanceData);
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    delete d;
}

void VS_CC repairCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    RepairData d;

    d.node1 = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node1);

    if (!isConstantVideoFormat(d.vi)) {
        vsapi->freeNode(d.node1);
        vsapi->mapSetError(out, "Repair: Only constant format input supported");
        return;
    }

    d.node2 = vsapi->mapGetNode(in, "repairclip", 0, nullptr);

    if (!isSameVideoInfo(d.vi, vsapi->getVideoInfo(d.node2))) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->mapSetError(out, "Repair: Input clips must have the same format");
        return;
    }

    if (d.vi->format.sampleType != stInteger || (d.vi->format.bytesPerSample != 1 && d.vi->format.bytesPerSample != 2)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->mapSetError(out, "Repair: Only 8-16 bit int formats supported");
        return;
    }

    int n = d.vi->format.numPlanes;
    int m = vsapi->mapNumElements(in, "mode");
    if (n < m) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->mapSetError(out, "Repair: Number of modes specified must be equal or fewer than the number of input planes");
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (i < m) {
            d.mode[i] = vsapi->mapGetIntSaturated(in, "mode", i, nullptr);
            if (d.mode[i] < 0 || d.mode[i] > 24)
            {
                vsapi->freeNode(d.node1);
                vsapi->freeNode(d.node2);
                vsapi->mapSetError(out, "Repair: Invalid mode specified, only 0-24 supported");
                return;
            }
        } else {
            d.mode[i] = d.mode[i - 1];
        }
    }

    RepairData *data = new RepairData(d);

    VSFilterDependency deps[] = {{d.node1, rpStrictSpatial}, {d.node2, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "Repair", data->vi, repairGetFrame, repairFree, fmParallel, deps, 2, data, core);
}
