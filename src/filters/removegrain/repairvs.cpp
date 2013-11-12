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
   const int      om = stride_src2 - 1;     \
   const int      o0 = stride_src2    ;     \
   const int      op = stride_src2 + 1;     \
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
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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
                                                // sort_pair (a4, a6);
        a8 = _mm_max_epi16 (a5, a8);    // sort_pair (a5, a8);

        a2 = _mm_min_epi16 (a2, a3);    // sort_pair (a2, a3);
                                                // sort_pair (a4,  c);
                                                // sort_pair (a5, a6);
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
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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
                                                // sort_pair (a4,  c);
        a6 = _mm_max_epi16 (a5, a6);    // sort_pair (a5, a6);
                                                // sort_pair (a7, a8);

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
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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

                                                // sort_pair (a2, a3);
        a4 = _mm_min_epi16 (a4,  c);    // sort_pair (a4,  c);
        a5 = _mm_min_epi16 (a5, a6);    // sort_pair (a5, a6);
                                                // sort_pair (a7, a8);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, a4), a5));
    }
#endif
};

class OpRG12
{
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
        int                a [10] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [2-1], c);
        const int        ma = std::max (a [7-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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
        int                a [10] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [3-1], c);
        const int        ma = std::max (a [6-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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
        int                a [10] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [0]) + 8);
        const int        mi = std::min (a [4-1], c);
        const int        ma = std::max (a [5-1], c);

        return (limit (cr, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src1_ptr, const T *src2_ptr, int stride_src2, __m128i mask_sign) {
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

                                                // sort_pair (a2, a3);
        sort_pair (a4, a5);
                                                // sort_pair (a6, a7);

        const __m128i    mi = _mm_min_epi16 (c, a4);
        const __m128i    ma = _mm_max_epi16 (c, a5);

        return (_mm_min_epi16 (_mm_max_epi16 (cr, mi), ma));
    }
#endif
};

class OpRG15 {
public:
    static __forceinline int
        rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
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
};

class OpRG16 {
public:
    static __forceinline int
        rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
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
};

class OpRG17 {
public:
    static __forceinline int
        rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
            AvsFilterRepair16_SORT_AXIS_CPP

            const int      l = std::max(std::max(mi1, mi2), std::max(mi3, mi4));
            const int      u = std::min(std::min(ma1, ma2), std::min(ma3, ma4));

            const int      mi = std::min(std::min(l, u), c);
            const int      ma = std::max(std::max(l, u), c);

            return (limit(cr, mi, ma));
        }
};

class OpRG18 {
public:
    static __forceinline int
        rg(int cr, int a1, int a2, int a3, int a4, int c, int a5, int a6, int a7, int a8) {
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
};


template <class OP, class T>
class PlaneProc {
public:

static void process_subplane_cpp (const T *src1_ptr, const T *src2_ptr, T *dst_ptr, int stride, int width, int height)
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

static void process_row_cpp (T *dst_ptr, const T *src1_ptr, const T *src2_ptr, int stride_src, int x_beg, int x_end)
{
    const int      om = stride_src - 1;
    const int      o0 = stride_src    ;
    const int      op = stride_src + 1;

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
static void process_subplane_sse2 (const T *src1_ptr, const T *src2_ptr, T *dst_ptr, int stride, int width, int height)
{
    const int        y_b = 1;
    const int        y_e = height - 1;

    dst_ptr += y_b * stride;
    src1_ptr += y_b * stride;
    src2_ptr += y_b * stride;

    const __m128i    mask_sign = _mm_set1_epi16 (-0x8000);

    const int        x_e =   width - 1;
    const int        w8  = ((width - 2) & -8) + 1;
    const int        w7  = x_e - w8;

    for (int y = y_b; y < y_e; ++y)
    {
        dst_ptr [0] = src1_ptr [0];
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
static void do_process_plane_sse2 (const VSFrameRef *src1_frame, const VSFrameRef *src2_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int        w             = vsapi->getFrameWidth(src1_frame, plane_id);
    const int        h             = vsapi->getFrameHeight(src1_frame, plane_id);
    T1 *            dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const int        stride        = vsapi->getStride(src1_frame, plane_id);

    const T1*        src1_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src1_frame, plane_id));
    const T1*        src2_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src2_frame, plane_id));

    // First line
    memcpy (dst_ptr, src1_ptr, stride);

    // Main content
    PlaneProc<OP1, T1>::process_subplane_sse2(src1_ptr, src2_ptr, dst_ptr, stride/sizeof(T1), w, h);

    // Last line
    const int        lp = (h - 1) * stride/sizeof(T1);
    memcpy (dst_ptr + lp, src1_ptr + lp, stride);
}

#endif

template <class OP1, class T1>
static void do_process_plane_cpp (const VSFrameRef *src1_frame, const VSFrameRef *src2_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int        w             = vsapi->getFrameWidth(src1_frame, plane_id);
    const int        h             = vsapi->getFrameHeight(src1_frame, plane_id);
    T1 *            dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const int        stride        = vsapi->getStride(src1_frame, plane_id);

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
    VSNodeRef *node1;
    VSNodeRef *node2;
    const VSVideoInfo *vi;
    int mode[3];
} RepairData;

static void VS_CC repairInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    RepairData *d = (RepairData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC repairGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    RepairData *d = (RepairData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src1_frame = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrameRef *src2_frame = vsapi->getFrameFilter(n, d->node2, frameCtx);
        int planes[3] = {0, 1, 2};
        const VSFrameRef * cp_planes[3] = { d->mode[0] ? NULL : src1_frame, d->mode[1] ? NULL : src1_frame, d->mode[2] ? NULL : src1_frame };
        VSFrameRef *dst_frame = vsapi->newVideoFrame2(vsapi->getFrameFormat(src1_frame), vsapi->getFrameWidth(src1_frame, 0), vsapi->getFrameHeight(src1_frame, 0), cp_planes, planes, src1_frame, core);


#define PROC_ARGS_16(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint16_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint8_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;

#ifdef VS_TARGET_CPU_X86
#define PROC_ARGS_16_FAST(op) PlaneProc <op, uint16_t>::do_process_plane_sse2<op, uint16_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8_FAST(op) PlaneProc <op, uint8_t>::do_process_plane_sse2<op, uint8_t>(src1_frame, src2_frame, dst_frame, i, vsapi); break;
#else
#define PROC_ARGS_16_FAST(op) PROC_ARGS_16(op)
#define PROC_ARGS_8_FAST(op) PROC_ARGS_8(op)
#endif


        if (d->vi->format->bytesPerSample == 1) {
            for (int i = 0; i < d->vi->format->numPlanes; i++) {
                switch (d->mode[i])
                {
                    case  1: PROC_ARGS_8_FAST(OpRG01)
                    case  2: PROC_ARGS_8_FAST(OpRG02)
                    case  3: PROC_ARGS_8_FAST(OpRG03)
                    case  4: PROC_ARGS_8_FAST(OpRG04)
                    case 11: PROC_ARGS_8_FAST(OpRG01)
                    case 12: PROC_ARGS_8_FAST(OpRG12)
                    case 13: PROC_ARGS_8_FAST(OpRG13)
                    case 14: PROC_ARGS_8_FAST(OpRG14)
                    case 15: PROC_ARGS_8(OpRG15)
                    case 16: PROC_ARGS_8(OpRG16)
                    case 17: PROC_ARGS_8(OpRG17)
                    case 18: PROC_ARGS_8(OpRG18)
                    default: break;
                }
            }
        } else {
            for (int i = 0; i < d->vi->format->numPlanes; i++) {
                switch (d->mode[i])
                {
                    case  1: PROC_ARGS_16_FAST(OpRG01)
                    case  2: PROC_ARGS_16_FAST(OpRG02)
                    case  3: PROC_ARGS_16_FAST(OpRG03)
                    case  4: PROC_ARGS_16_FAST(OpRG04)
                    case 11: PROC_ARGS_16_FAST(OpRG01)
                    case 12: PROC_ARGS_16_FAST(OpRG12)
                    case 13: PROC_ARGS_16_FAST(OpRG13)
                    case 14: PROC_ARGS_16_FAST(OpRG14)
                    case 15: PROC_ARGS_16(OpRG15)
                    case 16: PROC_ARGS_16(OpRG16)
                    case 17: PROC_ARGS_16(OpRG17)
                    case 18: PROC_ARGS_16(OpRG18)
                    default: break;
                }
            }
        }

        vsapi->freeFrame(src1_frame);
        vsapi->freeFrame(src2_frame);
        return dst_frame;
    }

    return 0;
}

static void VS_CC repairFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    RepairData *d = (RepairData *)instanceData;
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->node2);
    delete d;
}

void VS_CC repairCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    RepairData d;

    d.node1 = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node1);

    if (!isConstantFormat(d.vi)) {
        vsapi->freeNode(d.node1);
        vsapi->setError(out, "Repair: Only constant format input supported");
        return;
    }

    d.node2 = vsapi->propGetNode(in, "repairclip", 0, 0);

    if (!isSameFormat(d.vi, vsapi->getVideoInfo(d.node2))) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->setError(out, "Repair: Input clips must have the same format");
        return;
    }

    if (d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->setError(out, "Repair: Only 8-16 bit int formats supported");
        return;
    }

    int n = d.vi->format->numPlanes;
    int m = vsapi->propNumElements(in, "mode");
    if (n < m) {
        vsapi->freeNode(d.node1);
        vsapi->freeNode(d.node2);
        vsapi->setError(out, "Repair: Number of modes specified must be equal or fewer than the number of input planes");
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (i < m) {
            d.mode[i] = int64ToIntS(vsapi->propGetInt(in, "mode", i, NULL));
            if (d.mode[i] < 0 || d.mode[i] > 18 || (d.mode[i] > 4 && d.mode[i] < 11)) 
            {
                vsapi->freeNode(d.node1);
                vsapi->freeNode(d.node2);
                vsapi->setError(out, "Repair: Invalid mode specified, only 0-4, 11-18 supported");
                return;
            }
        } else {
            d.mode[i] = d.mode[i - 1];
        }
    }

    RepairData *data = new RepairData;
    *data = d;

    vsapi->createFilter(in, out, "Repair", repairInit, repairGetFrame, repairFree, fmParallel, 0, data, core);
}


