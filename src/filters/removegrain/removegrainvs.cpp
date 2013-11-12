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

#define AvsFilterRemoveGrain16_READ_PIX    \
   const int      om = stride_src - 1;     \
   const int      o0 = stride_src    ;     \
   const int      op = stride_src + 1;     \
   __m128i        a1, a2, a3, a4, c, a5, a6, a7, a8; \
   if (sizeof(T) == 1) { \
       __m128i zeroreg = _mm_setzero_si128(); \
       a1 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - op)), zeroreg), mask_sign); \
       a2 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - o0)), zeroreg), mask_sign); \
       a3 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - om)), zeroreg), mask_sign); \
       a4 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - 1 )), zeroreg), mask_sign); \
       c  = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + 0 )), zeroreg), mask_sign); \
       a5 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + 1 )), zeroreg), mask_sign); \
       a6 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + om)), zeroreg), mask_sign); \
       a7 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + o0)), zeroreg), mask_sign); \
       a8 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + op)), zeroreg), mask_sign); \
   } else {                                \
       a1 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - op)), mask_sign); \
       a2 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - o0)), mask_sign); \
       a3 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - om)), mask_sign); \
       a4 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - 1 )), mask_sign); \
       c  = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + 0 )), mask_sign); \
       a5 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + 1 )), mask_sign); \
       a6 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + om)), mask_sign); \
       a7 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + o0)), mask_sign); \
       a8 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + op)), mask_sign); \
   }

#define AvsFilterRemoveGrain16_SORT_AXIS_SSE2   \
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

#define AvsFilterRemoveGrain16_SORT_AXIS_CPP \
    const int      ma1 = std::max(a1, a8);   \
    const int      mi1 = std::min(a1, a8);   \
    const int      ma2 = std::max(a2, a7);   \
    const int      mi2 = std::min(a2, a7);   \
    const int      ma3 = std::max(a3, a6);   \
    const int      mi3 = std::min(a3, a6);   \
    const int      ma4 = std::max(a4, a5);   \
    const int      mi4 = std::min(a4, a5);

class OpRG01 : public LineProcAll {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int        mi = std::min (
            std::min (std::min (a1, a2), std::min (a3, a4)),
            std::min (std::min (a5, a6), std::min (a7, a8))
        );
        const int        ma = std::max (
            std::max (std::max (a1, a2), std::max (a3, a4)),
            std::max (std::max (a5, a6), std::max (a7, a8))
        );

        return (limit (c, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

        const __m128i    mi = _mm_min_epi16 (
            _mm_min_epi16 (_mm_min_epi16 (a1, a2), _mm_min_epi16 (a3, a4)),
            _mm_min_epi16 (_mm_min_epi16 (a5, a6), _mm_min_epi16 (a7, a8))
        );
        const __m128i    ma = _mm_max_epi16 (
            _mm_max_epi16 (_mm_max_epi16 (a1, a2), _mm_max_epi16 (a3, a4)),
            _mm_max_epi16 (_mm_max_epi16 (a5, a6), _mm_max_epi16 (a7, a8))
        );

        return (_mm_min_epi16 (_mm_max_epi16 (c, mi), ma));
    }
#endif
};

class OpRG02 : public LineProcAll {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [7]) + 1);

        return (limit (c, a [2-1], a [7-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

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

        return (_mm_min_epi16 (_mm_max_epi16 (c, a2), a7));
    }
#endif
};

class OpRG03 : public LineProcAll {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [7]) + 1);

        return (limit (c, a [3-1], a [6-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

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

        return (_mm_min_epi16 (_mm_max_epi16 (c, a3), a6));
    }
#endif
};

class OpRG04 : public LineProcAll {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        int                a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

        std::sort (&a [0], (&a [7]) + 1);

        return (limit (c, a [4-1], a [5-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        // http://en.wikipedia.org/wiki/Batcher_odd%E2%80%93even_mergesort

        AvsFilterRemoveGrain16_READ_PIX

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

        return (_mm_min_epi16 (_mm_max_epi16 (c, a4), a5));
    }
#endif
};

class OpRG05 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      c1 = std::abs(c - limit(c, mi1, ma1));
            const int      c2 = std::abs(c - limit(c, mi2, ma2));
            const int      c3 = std::abs(c - limit(c, mi3, ma3));
            const int      c4 = std::abs(c - limit(c, mi4, ma4));

            const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

            if (mindiff == c4) {
                return (limit(c, mi4, ma4));
            } else if (mindiff == c2) {
                return (limit(c, mi2, ma2));
            } else if (mindiff == c3) {
                return (limit(c, mi3, ma3));
            }

            return (limit(c, mi1, ma1));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  cli1 = limit_epi16(c, mi1, ma1);
            const __m128i  cli2 = limit_epi16(c, mi2, ma2);
            const __m128i  cli3 = limit_epi16(c, mi3, ma3);
            const __m128i  cli4 = limit_epi16(c, mi4, ma4);

            const __m128i  cli1u = _mm_xor_si128(cli1, mask_sign);
            const __m128i  cli2u = _mm_xor_si128(cli2, mask_sign);
            const __m128i  cli3u = _mm_xor_si128(cli3, mask_sign);
            const __m128i  cli4u = _mm_xor_si128(cli4, mask_sign);
            const __m128i  cu = _mm_xor_si128(c, mask_sign);

            const __m128i  c1u = abs_dif_epu16(cu, cli1u);
            const __m128i  c2u = abs_dif_epu16(cu, cli2u);
            const __m128i  c3u = abs_dif_epu16(cu, cli3u);
            const __m128i  c4u = abs_dif_epu16(cu, cli4u);

            const __m128i  c1 = _mm_xor_si128(c1u, mask_sign);
            const __m128i  c2 = _mm_xor_si128(c2u, mask_sign);
            const __m128i  c3 = _mm_xor_si128(c3u, mask_sign);
            const __m128i  c4 = _mm_xor_si128(c4u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(c1, c2),
                _mm_min_epi16(c3, c4)
                );

            __m128i        res = cli1;
            res = select_16_equ(mindiff, c3, cli3, res);
            res = select_16_equ(mindiff, c2, cli2, res);
            res = select_16_equ(mindiff, c4, cli4, res);

            return (res);

        }
#endif
};


class OpRG06 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      d1 = ma1 - mi1;
            const int      d2 = ma2 - mi2;
            const int      d3 = ma3 - mi3;
            const int      d4 = ma4 - mi4;

            const int      cli1 = limit(c, mi1, ma1);
            const int      cli2 = limit(c, mi2, ma2);
            const int      cli3 = limit(c, mi3, ma3);
            const int      cli4 = limit(c, mi4, ma4);

            const int      c1 = limit((std::abs(c - cli1) << 1) + d1, 0, 0xFFFF);
            const int      c2 = limit((std::abs(c - cli2) << 1) + d2, 0, 0xFFFF);
            const int      c3 = limit((std::abs(c - cli3) << 1) + d3, 0, 0xFFFF);
            const int      c4 = limit((std::abs(c - cli4) << 1) + d4, 0, 0xFFFF);

            const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

            if (mindiff == c4) {
                return (cli4);
            } else if (mindiff == c2) {
                return (cli2);
            } else if (mindiff == c3) {
                return (cli3);
            }

            return (cli1);
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  d1u = _mm_sub_epi16(ma1, mi1);
            const __m128i  d2u = _mm_sub_epi16(ma2, mi2);
            const __m128i  d3u = _mm_sub_epi16(ma3, mi3);
            const __m128i  d4u = _mm_sub_epi16(ma4, mi4);

            const __m128i  cli1 = limit_epi16(c, mi1, ma1);
            const __m128i  cli2 = limit_epi16(c, mi2, ma2);
            const __m128i  cli3 = limit_epi16(c, mi3, ma3);
            const __m128i  cli4 = limit_epi16(c, mi4, ma4);

            const __m128i  cli1u = _mm_xor_si128(cli1, mask_sign);
            const __m128i  cli2u = _mm_xor_si128(cli2, mask_sign);
            const __m128i  cli3u = _mm_xor_si128(cli3, mask_sign);
            const __m128i  cli4u = _mm_xor_si128(cli4, mask_sign);
            const __m128i  cu = _mm_xor_si128(c, mask_sign);

            const __m128i  ad1u = abs_dif_epu16(cu, cli1u);
            const __m128i  ad2u = abs_dif_epu16(cu, cli2u);
            const __m128i  ad3u = abs_dif_epu16(cu, cli3u);
            const __m128i  ad4u = abs_dif_epu16(cu, cli4u);

            const __m128i  c1u = _mm_adds_epu16(_mm_adds_epu16(d1u, ad1u), ad1u);
            const __m128i  c2u = _mm_adds_epu16(_mm_adds_epu16(d2u, ad2u), ad2u);
            const __m128i  c3u = _mm_adds_epu16(_mm_adds_epu16(d3u, ad3u), ad3u);
            const __m128i  c4u = _mm_adds_epu16(_mm_adds_epu16(d4u, ad4u), ad4u);

            const __m128i  c1 = _mm_xor_si128(c1u, mask_sign);
            const __m128i  c2 = _mm_xor_si128(c2u, mask_sign);
            const __m128i  c3 = _mm_xor_si128(c3u, mask_sign);
            const __m128i  c4 = _mm_xor_si128(c4u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(c1, c2),
                _mm_min_epi16(c3, c4)
                );

            __m128i        res = cli1;
            res = select_16_equ(mindiff, c3, cli3, res);
            res = select_16_equ(mindiff, c2, cli2, res);
            res = select_16_equ(mindiff, c4, cli4, res);

            return (res);
        }
#endif
};

class OpRG07 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      d1 = ma1 - mi1;
            const int      d2 = ma2 - mi2;
            const int      d3 = ma3 - mi3;
            const int      d4 = ma4 - mi4;

            const int      cli1 = limit(c, mi1, ma1);
            const int      cli2 = limit(c, mi2, ma2);
            const int      cli3 = limit(c, mi3, ma3);
            const int      cli4 = limit(c, mi4, ma4);

            const int      c1 = std::abs(c - cli1) + d1;
            const int      c2 = std::abs(c - cli2) + d2;
            const int      c3 = std::abs(c - cli3) + d3;
            const int      c4 = std::abs(c - cli4) + d4;

            const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

            if (mindiff == c4) {
                return (cli4);
            } else if (mindiff == c2) {
                return (cli2);
            } else if (mindiff == c3) {
                return (cli3);
            }

            return (cli1);
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  d1u = _mm_sub_epi16(ma1, mi1);
            const __m128i  d2u = _mm_sub_epi16(ma2, mi2);
            const __m128i  d3u = _mm_sub_epi16(ma3, mi3);
            const __m128i  d4u = _mm_sub_epi16(ma4, mi4);

            const __m128i  cli1 = limit_epi16(c, mi1, ma1);
            const __m128i  cli2 = limit_epi16(c, mi2, ma2);
            const __m128i  cli3 = limit_epi16(c, mi3, ma3);
            const __m128i  cli4 = limit_epi16(c, mi4, ma4);

            const __m128i  cli1u = _mm_xor_si128(cli1, mask_sign);
            const __m128i  cli2u = _mm_xor_si128(cli2, mask_sign);
            const __m128i  cli3u = _mm_xor_si128(cli3, mask_sign);
            const __m128i  cli4u = _mm_xor_si128(cli4, mask_sign);
            const __m128i  cu = _mm_xor_si128(c, mask_sign);

            const __m128i  ad1u = abs_dif_epu16(cu, cli1u);
            const __m128i  ad2u = abs_dif_epu16(cu, cli2u);
            const __m128i  ad3u = abs_dif_epu16(cu, cli3u);
            const __m128i  ad4u = abs_dif_epu16(cu, cli4u);

            const __m128i  c1u = _mm_adds_epu16(d1u, ad1u);
            const __m128i  c2u = _mm_adds_epu16(d2u, ad2u);
            const __m128i  c3u = _mm_adds_epu16(d3u, ad3u);
            const __m128i  c4u = _mm_adds_epu16(d4u, ad4u);

            const __m128i  c1 = _mm_xor_si128(c1u, mask_sign);
            const __m128i  c2 = _mm_xor_si128(c2u, mask_sign);
            const __m128i  c3 = _mm_xor_si128(c3u, mask_sign);
            const __m128i  c4 = _mm_xor_si128(c4u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(c1, c2),
                _mm_min_epi16(c3, c4)
                );

            __m128i        res = cli1;
            res = select_16_equ(mindiff, c3, cli3, res);
            res = select_16_equ(mindiff, c2, cli2, res);
            res = select_16_equ(mindiff, c4, cli4, res);

            return (res);
        }
#endif
};

class OpRG08 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      d1 = ma1 - mi1;
            const int      d2 = ma2 - mi2;
            const int      d3 = ma3 - mi3;
            const int      d4 = ma4 - mi4;

            const int      cli1 = limit(c, mi1, ma1);
            const int      cli2 = limit(c, mi2, ma2);
            const int      cli3 = limit(c, mi3, ma3);
            const int      cli4 = limit(c, mi4, ma4);

            const int      c1 = limit(std::abs(c - cli1) + (d1 << 1), 0, 0xFFFF);
            const int      c2 = limit(std::abs(c - cli2) + (d2 << 1), 0, 0xFFFF);
            const int      c3 = limit(std::abs(c - cli3) + (d3 << 1), 0, 0xFFFF);
            const int      c4 = limit(std::abs(c - cli4) + (d4 << 1), 0, 0xFFFF);

            const int      mindiff = std::min(std::min(c1, c2), std::min(c3, c4));

            if (mindiff == c4) {
                return (cli4);
            } else if (mindiff == c2) {
                return (cli2);
            } else if (mindiff == c3) {
                return (cli3);
            }

            return (cli1);
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  d1u = _mm_sub_epi16(ma1, mi1);
            const __m128i  d2u = _mm_sub_epi16(ma2, mi2);
            const __m128i  d3u = _mm_sub_epi16(ma3, mi3);
            const __m128i  d4u = _mm_sub_epi16(ma4, mi4);

            const __m128i  cli1 = limit_epi16(c, mi1, ma1);
            const __m128i  cli2 = limit_epi16(c, mi2, ma2);
            const __m128i  cli3 = limit_epi16(c, mi3, ma3);
            const __m128i  cli4 = limit_epi16(c, mi4, ma4);

            const __m128i  cli1u = _mm_xor_si128(cli1, mask_sign);
            const __m128i  cli2u = _mm_xor_si128(cli2, mask_sign);
            const __m128i  cli3u = _mm_xor_si128(cli3, mask_sign);
            const __m128i  cli4u = _mm_xor_si128(cli4, mask_sign);
            const __m128i  cu = _mm_xor_si128(c, mask_sign);

            const __m128i  ad1u = abs_dif_epu16(cu, cli1u);
            const __m128i  ad2u = abs_dif_epu16(cu, cli2u);
            const __m128i  ad3u = abs_dif_epu16(cu, cli3u);
            const __m128i  ad4u = abs_dif_epu16(cu, cli4u);

            const __m128i  c1u = _mm_adds_epu16(_mm_adds_epu16(d1u, d1u), ad1u);
            const __m128i  c2u = _mm_adds_epu16(_mm_adds_epu16(d2u, d2u), ad2u);
            const __m128i  c3u = _mm_adds_epu16(_mm_adds_epu16(d3u, d3u), ad3u);
            const __m128i  c4u = _mm_adds_epu16(_mm_adds_epu16(d4u, d4u), ad4u);

            const __m128i  c1 = _mm_xor_si128(c1u, mask_sign);
            const __m128i  c2 = _mm_xor_si128(c2u, mask_sign);
            const __m128i  c3 = _mm_xor_si128(c3u, mask_sign);
            const __m128i  c4 = _mm_xor_si128(c4u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(c1, c2),
                _mm_min_epi16(c3, c4)
                );

            __m128i        res = cli1;
            res = select_16_equ(mindiff, c3, cli3, res);
            res = select_16_equ(mindiff, c2, cli2, res);
            res = select_16_equ(mindiff, c4, cli4, res);

            return (res);
        }
#endif
};
class OpRG09 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      d1 = ma1 - mi1;
            const int      d2 = ma2 - mi2;
            const int      d3 = ma3 - mi3;
            const int      d4 = ma4 - mi4;

            const int      mindiff = std::min(std::min(d1, d2), std::min(d3, d4));

            if (mindiff == d4) {
                return (limit(c, mi4, ma4));
            } else if (mindiff == d2) {
                return (limit(c, mi2, ma2));
            } else if (mindiff == d3) {
                return (limit(c, mi3, ma3));
            }

            return (limit(c, mi1, ma1));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  cli1 = limit_epi16(c, mi1, ma1);
            const __m128i  cli2 = limit_epi16(c, mi2, ma2);
            const __m128i  cli3 = limit_epi16(c, mi3, ma3);
            const __m128i  cli4 = limit_epi16(c, mi4, ma4);

            const __m128i  d1u = _mm_sub_epi16(ma1, mi1);
            const __m128i  d2u = _mm_sub_epi16(ma2, mi2);
            const __m128i  d3u = _mm_sub_epi16(ma3, mi3);
            const __m128i  d4u = _mm_sub_epi16(ma4, mi4);

            const __m128i  d1 = _mm_xor_si128(d1u, mask_sign);
            const __m128i  d2 = _mm_xor_si128(d2u, mask_sign);
            const __m128i  d3 = _mm_xor_si128(d3u, mask_sign);
            const __m128i  d4 = _mm_xor_si128(d4u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(d1, d2),
                _mm_min_epi16(d3, d4)
                );

            __m128i        res = cli1;
            res = select_16_equ(mindiff, d3, cli3, res);
            res = select_16_equ(mindiff, d2, cli2, res);
            res = select_16_equ(mindiff, d4, cli4, res);

            return (res);
        }
#endif
};
class OpRG10 : public LineProcAll {
public:
    typedef	ConvUnsigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      d1 = std::abs(c - a1);
            const int      d2 = std::abs(c - a2);
            const int      d3 = std::abs(c - a3);
            const int      d4 = std::abs(c - a4);
            const int      d5 = std::abs(c - a5);
            const int      d6 = std::abs(c - a6);
            const int      d7 = std::abs(c - a7);
            const int      d8 = std::abs(c - a8);

            const int      mindiff = std::min(
                std::min(std::min(d1, d2), std::min(d3, d4)),
                std::min(std::min(d5, d6), std::min(d7, d8))
                );

            if (mindiff == d7) { return (a7); }
            if (mindiff == d8) { return (a8); }
            if (mindiff == d6) { return (a6); }
            if (mindiff == d2) { return (a2); }
            if (mindiff == d3) { return (a3); }
            if (mindiff == d1) { return (a1); }
            if (mindiff == d5) { return (a5); }

            return (a4);
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX

                const __m128i  d1u = abs_dif_epu16(c, a1);
            const __m128i  d2u = abs_dif_epu16(c, a2);
            const __m128i  d3u = abs_dif_epu16(c, a3);
            const __m128i  d4u = abs_dif_epu16(c, a4);
            const __m128i  d5u = abs_dif_epu16(c, a5);
            const __m128i  d6u = abs_dif_epu16(c, a6);
            const __m128i  d7u = abs_dif_epu16(c, a7);
            const __m128i  d8u = abs_dif_epu16(c, a8);

            const __m128i  d1 = _mm_xor_si128(d1u, mask_sign);
            const __m128i  d2 = _mm_xor_si128(d2u, mask_sign);
            const __m128i  d3 = _mm_xor_si128(d3u, mask_sign);
            const __m128i  d4 = _mm_xor_si128(d4u, mask_sign);
            const __m128i  d5 = _mm_xor_si128(d5u, mask_sign);
            const __m128i  d6 = _mm_xor_si128(d6u, mask_sign);
            const __m128i  d7 = _mm_xor_si128(d7u, mask_sign);
            const __m128i  d8 = _mm_xor_si128(d8u, mask_sign);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(_mm_min_epi16(d1, d2), _mm_min_epi16(d3, d4)),
                _mm_min_epi16(_mm_min_epi16(d5, d6), _mm_min_epi16(d7, d8))
                );

            __m128i        res = a4;
            res = select_16_equ(mindiff, d5, a5, res);
            res = select_16_equ(mindiff, d1, a1, res);
            res = select_16_equ(mindiff, d3, a3, res);
            res = select_16_equ(mindiff, d2, a2, res);
            res = select_16_equ(mindiff, d6, a6, res);
            res = select_16_equ(mindiff, d8, a8, res);
            res = select_16_equ(mindiff, d7, a7, res);

            return (res);
        }
#endif
};


#ifdef VS_TARGET_CPU_X86
class OpRG12sse2
{
public:
    typedef    ConvUnsigned    ConvSign;

    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

        const __m128i    bias =
            _mm_load_si128 (reinterpret_cast <const __m128i *> (_bias));

        const __m128i    a13  = _mm_avg_epu16 (a1, a3);
        const __m128i    a123 = _mm_avg_epu16 (a2, a13);

        const __m128i    a68  = _mm_avg_epu16 (a6, a8);
        const __m128i    a678 = _mm_avg_epu16 (a7, a68);

        const __m128i    a45  = _mm_avg_epu16 (a4, a5);
        const __m128i    a4c5 = _mm_avg_epu16 (c, a45);

        const __m128i    a123678  = _mm_avg_epu16 (a123, a678);
        const __m128i    a123678b = _mm_subs_epu16 (a123678, bias);
        const __m128i    val      = _mm_avg_epu16 (a4c5, a123678b);

        return (val);
    }
private:
    ALIGNED_ARRAY(static const uint16_t
                        _bias [8], 16);

};
#endif

class OpRG11 : public LineProcAll {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int        sum = 4 * c + 2 * (a2 + a4 + a5 + a7) + a1 + a3 + a6 + a8;
        const int        val = (sum + 8) >> 4;

        return (val);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        return (OpRG12sse2::rg (src_ptr, stride_src, mask_sign));
    }
#endif
};

class OpRG12 : public LineProcAll {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        return (OpRG11::rg (c, a1, a2, a3, a4, a5, a6, a7, a8));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX
        return (OpRG12sse2::rg(src_ptr, stride_src, mask_sign));
    }
#endif
};

class OpRG1314 {
public:
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      d1 = std::abs(a1 - a8);
            const int      d2 = std::abs(a2 - a7);
            const int      d3 = std::abs(a3 - a6);

            const int      mindiff = std::min(std::min(d1, d2), d3);

            if (mindiff == d2) {
                return ((a2 + a7 + 1) >> 1);
            }
            if (mindiff == d3) {
                return ((a3 + a6 + 1) >> 1);
            }

            return ((a1 + a8 + 1) >> 1);
        }
};
class OpRG13 : public OpRG1314, public LineProcEven {};
class OpRG14 : public OpRG1314, public LineProcOdd {};
class OpRG1516 {
public:
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      d1 = std::abs(a1 - a8);
            const int      d2 = std::abs(a2 - a7);
            const int      d3 = std::abs(a3 - a6);

            const int      mindiff = std::min(std::min(d1, d2), d3);
            const int      average = (2 * (a2 + a7) + a1 + a3 + a6 + a8 + 4) >> 3;

            if (mindiff == d2) {
                return (limit(average, std::min(a2, a7), std::max(a2, a7)));
            }
            if (mindiff == d3) {
                return (limit(average, std::min(a3, a6), std::max(a3, a6)));
            }

            return (limit(average, std::min(a1, a8), std::max(a1, a8)));
        }
};
class OpRG15 : public OpRG1516, public LineProcEven {};
class OpRG16 : public OpRG1516, public LineProcOdd {};
class OpRG17 : public LineProcAll {
public:
    typedef	ConvSigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      l = std::max(std::max(mi1, mi2), std::max(mi3, mi4));
            const int      u = std::min(std::min(ma1, ma2), std::min(ma3, ma4));

            return (limit(c, std::min(l, u), std::max(l, u)));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX
                AvsFilterRemoveGrain16_SORT_AXIS_SSE2

                const __m128i  l = _mm_max_epi16(
                _mm_max_epi16(mi1, mi2),
                _mm_max_epi16(mi3, mi4)
                );
            const __m128i  u = _mm_min_epi16(
                _mm_min_epi16(ma1, ma2),
                _mm_min_epi16(ma3, ma4)
                );
            const __m128i  mi = _mm_min_epi16(l, u);
            const __m128i  ma = _mm_max_epi16(l, u);

            return (limit_epi16(c, mi, ma));
        }
#endif
};

class OpRG18 : public LineProcAll {
public:
    typedef	ConvUnsigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      d1 = std::max(std::abs(c - a1), std::abs(c - a8));
            const int      d2 = std::max(std::abs(c - a2), std::abs(c - a7));
            const int      d3 = std::max(std::abs(c - a3), std::abs(c - a6));
            const int      d4 = std::max(std::abs(c - a4), std::abs(c - a5));

            const int      mindiff = std::min(std::min(d1, d2), std::min(d3, d4));

            if (mindiff == d4) {
                return (limit(c, std::min(a4, a5), std::max(a4, a5)));
            }
            if (mindiff == d2) {
                return (limit(c, std::min(a2, a7), std::max(a2, a7)));
            }
            if (mindiff == d3) {
                return (limit(c, std::min(a3, a6), std::max(a3, a6)));
            }

            return (limit(c, std::min(a1, a8), std::max(a1, a8)));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX

                const __m128i  absdiff1u = abs_dif_epu16(c, a1);
            const __m128i  absdiff2u = abs_dif_epu16(c, a2);
            const __m128i  absdiff3u = abs_dif_epu16(c, a3);
            const __m128i  absdiff4u = abs_dif_epu16(c, a4);
            const __m128i  absdiff5u = abs_dif_epu16(c, a5);
            const __m128i  absdiff6u = abs_dif_epu16(c, a6);
            const __m128i  absdiff7u = abs_dif_epu16(c, a7);
            const __m128i  absdiff8u = abs_dif_epu16(c, a8);

            const __m128i  absdiff1 = _mm_xor_si128(absdiff1u, mask_sign);
            const __m128i  absdiff2 = _mm_xor_si128(absdiff2u, mask_sign);
            const __m128i  absdiff3 = _mm_xor_si128(absdiff3u, mask_sign);
            const __m128i  absdiff4 = _mm_xor_si128(absdiff4u, mask_sign);
            const __m128i  absdiff5 = _mm_xor_si128(absdiff5u, mask_sign);
            const __m128i  absdiff6 = _mm_xor_si128(absdiff6u, mask_sign);
            const __m128i  absdiff7 = _mm_xor_si128(absdiff7u, mask_sign);
            const __m128i  absdiff8 = _mm_xor_si128(absdiff8u, mask_sign);

            const __m128i  d1 = _mm_max_epi16(absdiff1, absdiff8);
            const __m128i  d2 = _mm_max_epi16(absdiff2, absdiff7);
            const __m128i  d3 = _mm_max_epi16(absdiff3, absdiff6);
            const __m128i  d4 = _mm_max_epi16(absdiff4, absdiff5);

            const __m128i  mindiff = _mm_min_epi16(
                _mm_min_epi16(d1, d2),
                _mm_min_epi16(d3, d4)
                );

            const __m128i  a1s = _mm_xor_si128(a1, mask_sign);
            const __m128i  a2s = _mm_xor_si128(a2, mask_sign);
            const __m128i  a3s = _mm_xor_si128(a3, mask_sign);
            const __m128i  a4s = _mm_xor_si128(a4, mask_sign);
            const __m128i  a5s = _mm_xor_si128(a5, mask_sign);
            const __m128i  a6s = _mm_xor_si128(a6, mask_sign);
            const __m128i  a7s = _mm_xor_si128(a7, mask_sign);
            const __m128i  a8s = _mm_xor_si128(a8, mask_sign);
            const __m128i  cs = _mm_xor_si128(c, mask_sign);

            const __m128i  ma1 = _mm_max_epi16(a1s, a8s);
            const __m128i  mi1 = _mm_min_epi16(a1s, a8s);
            const __m128i  ma2 = _mm_max_epi16(a2s, a7s);
            const __m128i  mi2 = _mm_min_epi16(a2s, a7s);
            const __m128i  ma3 = _mm_max_epi16(a3s, a6s);
            const __m128i  mi3 = _mm_min_epi16(a3s, a6s);
            const __m128i  ma4 = _mm_max_epi16(a4s, a5s);
            const __m128i  mi4 = _mm_min_epi16(a4s, a5s);

            const __m128i  cli1 = limit_epi16(cs, mi1, ma1);
            const __m128i  cli2 = limit_epi16(cs, mi2, ma2);
            const __m128i  cli3 = limit_epi16(cs, mi3, ma3);
            const __m128i  cli4 = limit_epi16(cs, mi4, ma4);

            __m128i        res = cli1;
            res = select_16_equ(mindiff, d3, cli3, res);
            res = select_16_equ(mindiff, d2, cli2, res);
            res = select_16_equ(mindiff, d4, cli4, res);

            return (_mm_xor_si128(res, mask_sign));
        }
#endif
};

class OpRG19 : public LineProcAll {
public:
    typedef    ConvUnsigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int        sum = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
        const int        val = (sum + 4) >> 3;

        return (val);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

        const __m128i    bias =
            _mm_load_si128 (reinterpret_cast <const __m128i *> (_bias));

        const __m128i    a13    = _mm_avg_epu16 (a1, a3);
        const __m128i    a68    = _mm_avg_epu16 (a6, a8);
        const __m128i    a1368  = _mm_avg_epu16 (a13, a68);
        const __m128i    a1368b = _mm_subs_epu16 (a1368, bias);
        const __m128i    a25    = _mm_avg_epu16 (a2, a5);
        const __m128i    a47    = _mm_avg_epu16 (a4, a7);
        const __m128i    a2457  = _mm_avg_epu16 (a25, a47);
        const __m128i    val    = _mm_avg_epu16 (a1368b, a2457);

        return (val);
    }
private:
    ALIGNED_ARRAY(static const uint16_t
                        _bias [8], 16);
#endif
};

class OpRG20 : public LineProcAll {
public:
    typedef    ConvSigned    ConvSign;
    static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int        sum = a1 + a2 + a3 + a4 + c + a5 + a6 + a7 + a8;
        const int        val = (sum + 4) / 9;

        return (val);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
        AvsFilterRemoveGrain16_READ_PIX

            const __m128i  zero = _mm_setzero_si128();

        __m128i        sum_0 =
            _mm_load_si128(reinterpret_cast <const __m128i *> (_bias));
        __m128i        sum_1 = sum_0;

        add_x16_s32(sum_0, sum_1, c, zero);
        add_x16_s32(sum_0, sum_1, a1, zero);
        add_x16_s32(sum_0, sum_1, a2, zero);
        add_x16_s32(sum_0, sum_1, a3, zero);
        add_x16_s32(sum_0, sum_1, a4, zero);
        add_x16_s32(sum_0, sum_1, a5, zero);
        add_x16_s32(sum_0, sum_1, a6, zero);
        add_x16_s32(sum_0, sum_1, a7, zero);
        add_x16_s32(sum_0, sum_1, a8, zero);

        const __m128i  mult =
            _mm_load_si128(reinterpret_cast <const __m128i *> (_mult));
        const __m128i  val = mul_s32_s15_s16(sum_0, sum_1, mult);

        return (_mm_xor_si128(val, mask_sign));
    }
private:
    ALIGNED_ARRAY(static const int32_t
        _bias[4], 16);
    ALIGNED_ARRAY(static const uint16_t
        _mult[8], 16);
#endif
};

class OpRG21 : public LineProcAll {
public:
    typedef	ConvUnsigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      l1l = (a1 + a8) >> 1;
            const int      l2l = (a2 + a7) >> 1;
            const int      l3l = (a3 + a6) >> 1;
            const int      l4l = (a4 + a5) >> 1;

            const int      l1h = (a1 + a8 + 1) >> 1;
            const int      l2h = (a2 + a7 + 1) >> 1;
            const int      l3h = (a3 + a6 + 1) >> 1;
            const int      l4h = (a4 + a5 + 1) >> 1;

            const int      mi = std::min(std::min(l1l, l2l), std::min(l3l, l4l));
            const int      ma = std::max(std::max(l1h, l2h), std::max(l3h, l4h));

            return (limit(c, mi, ma));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX

                const __m128i  bit0 =
                _mm_load_si128(reinterpret_cast <const __m128i *> (_bit0));

            const __m128i  odd1 = _mm_and_si128(_mm_xor_si128(a1, a8), bit0);
            const __m128i  odd2 = _mm_and_si128(_mm_xor_si128(a2, a7), bit0);
            const __m128i  odd3 = _mm_and_si128(_mm_xor_si128(a3, a6), bit0);
            const __m128i  odd4 = _mm_and_si128(_mm_xor_si128(a4, a5), bit0);

            const __m128i  l1hu = _mm_avg_epu16(a1, a8);
            const __m128i  l2hu = _mm_avg_epu16(a2, a7);
            const __m128i  l3hu = _mm_avg_epu16(a3, a6);
            const __m128i  l4hu = _mm_avg_epu16(a4, a5);

            const __m128i  l1h = _mm_xor_si128(l1hu, mask_sign);
            const __m128i  l2h = _mm_xor_si128(l2hu, mask_sign);
            const __m128i  l3h = _mm_xor_si128(l3hu, mask_sign);
            const __m128i  l4h = _mm_xor_si128(l4hu, mask_sign);

            const __m128i  l1l = _mm_subs_epi16(l1h, odd1);
            const __m128i  l2l = _mm_subs_epi16(l2h, odd2);
            const __m128i  l3l = _mm_subs_epi16(l3h, odd3);
            const __m128i  l4l = _mm_subs_epi16(l4h, odd4);

            const __m128i  mi = _mm_min_epi16(
                _mm_min_epi16(l1l, l2l),
                _mm_min_epi16(l3l, l4l)
                );
            const __m128i  ma = _mm_max_epi16(
                _mm_max_epi16(l1h, l2h),
                _mm_max_epi16(l3h, l4h)
                );

            const __m128i  cs = _mm_xor_si128(c, mask_sign);
            const __m128i  res = limit_epi16(cs, mi, ma);

            return (_mm_xor_si128(res, mask_sign));
        }
private:
    ALIGNED_ARRAY(static const uint16_t _bit0[8], 16);
#endif
};


class OpRG22 : public LineProcAll {
public:
    typedef	ConvUnsigned	ConvSign;
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            const int      l1 = (a1 + a8 + 1) >> 1;
            const int      l2 = (a2 + a7 + 1) >> 1;
            const int      l3 = (a3 + a6 + 1) >> 1;
            const int      l4 = (a4 + a5 + 1) >> 1;

            const int      mi = std::min(std::min(l1, l2), std::min(l3, l4));
            const int      ma = std::max(std::max(l1, l2), std::max(l3, l4));

            return (limit(c, mi, ma));
        }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
    static __forceinline __m128i rg(const T *src_ptr, int stride_src, __m128i mask_sign) {
            AvsFilterRemoveGrain16_READ_PIX

                const __m128i  l1u = _mm_avg_epu16(a1, a8);
            const __m128i  l2u = _mm_avg_epu16(a2, a7);
            const __m128i  l3u = _mm_avg_epu16(a3, a6);
            const __m128i  l4u = _mm_avg_epu16(a4, a5);

            const __m128i  l1 = _mm_xor_si128(l1u, mask_sign);
            const __m128i  l2 = _mm_xor_si128(l2u, mask_sign);
            const __m128i  l3 = _mm_xor_si128(l3u, mask_sign);
            const __m128i  l4 = _mm_xor_si128(l4u, mask_sign);

            const __m128i  mi = _mm_min_epi16(
                _mm_min_epi16(l1, l2),
                _mm_min_epi16(l3, l4)
                );
            const __m128i  ma = _mm_max_epi16(
                _mm_max_epi16(l1, l2),
                _mm_max_epi16(l3, l4)
                );

            const __m128i  cs = _mm_xor_si128(c, mask_sign);
            const __m128i  res = limit_epi16(cs, mi, ma);

            return (_mm_xor_si128(res, mask_sign));
        }
#endif
};

class OpRG23 : public LineProcAll {
public:
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      linediff1 = ma1 - mi1;
            const int      linediff2 = ma2 - mi2;
            const int      linediff3 = ma3 - mi3;
            const int      linediff4 = ma4 - mi4;

            const int      u1 = std::min(c - ma1, linediff1);
            const int      u2 = std::min(c - ma2, linediff2);
            const int      u3 = std::min(c - ma3, linediff3);
            const int      u4 = std::min(c - ma4, linediff4);
            const int      u = std::max(
                std::max(std::max(u1, u2), std::max(u3, u4)),
                0
                );

            const int      d1 = std::min(mi1 - c, linediff1);
            const int      d2 = std::min(mi2 - c, linediff2);
            const int      d3 = std::min(mi3 - c, linediff3);
            const int      d4 = std::min(mi4 - c, linediff4);
            const int      d = std::max(
                std::max(std::max(d1, d2), std::max(d3, d4)),
                0
                );

            return (c - u + d);  // This probably will never overflow.
        }
};
class OpRG24 : public LineProcAll {
public:
    static __forceinline int
        rg(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
            AvsFilterRemoveGrain16_SORT_AXIS_CPP

                const int      linediff1 = ma1 - mi1;
            const int      linediff2 = ma2 - mi2;
            const int      linediff3 = ma3 - mi3;
            const int      linediff4 = ma4 - mi4;

            const int      tu1 = c - ma1;
            const int      tu2 = c - ma2;
            const int      tu3 = c - ma3;
            const int      tu4 = c - ma4;

            const int      u1 = std::min(tu1, linediff1 - tu1);
            const int      u2 = std::min(tu2, linediff2 - tu2);
            const int      u3 = std::min(tu3, linediff3 - tu3);
            const int      u4 = std::min(tu4, linediff4 - tu4);
            const int      u = std::max(
                std::max(std::max(u1, u2), std::max(u3, u4)),
                0
                );

            const int      td1 = mi1 - c;
            const int      td2 = mi2 - c;
            const int      td3 = mi3 - c;
            const int      td4 = mi4 - c;

            const int      d1 = std::min(td1, linediff1 - td1);
            const int      d2 = std::min(td2, linediff2 - td2);
            const int      d3 = std::min(td3, linediff3 - td3);
            const int      d4 = std::min(td4, linediff4 - td4);
            const int      d = std::max(
                std::max(std::max(d1, d2), std::max(d3, d4)),
                0
                );

            return (c - u + d);  // This probably will never overflow.
        }
};





#ifdef VS_TARGET_CPU_X86
ALIGNED_ARRAY(const uint16_t    OpRG12sse2::_bias [8], 16) =
{ 1, 1, 1, 1, 1, 1, 1, 1 };

ALIGNED_ARRAY(const uint16_t    OpRG19::_bias [8], 16) =
{ 1, 1, 1, 1, 1, 1, 1, 1 };

ALIGNED_ARRAY(const int32_t OpRG20::_bias[4], 16) =
{ -0x8000 * 9, -0x8000 * 9, -0x8000 * 9, -0x8000 * 9 };

ALIGNED_ARRAY(const uint16_t OpRG20::_mult[8], 16) =
{ 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282 };	// (1^16 + 4) / 9

ALIGNED_ARRAY(const uint16_t OpRG21::_bit0[8], 16) =
{ 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001 };
#endif

template <class OP, class T>
class PlaneProc {
public:

static void process_subplane_cpp (const T *src_ptr, int stride_src, T *dst_ptr, int stride_dst, int width, int height)
{
    const int        y_b = 1;
    const int        y_e = height - 1;

    dst_ptr += y_b * stride_dst;
    src_ptr += y_b * stride_src;

    const int        x_e = width - 1;

    for (int y = y_b; y < y_e; ++y)
    {
        if (OP::skip_line(y)) {
            memcpy(dst_ptr, src_ptr, stride_dst);
        } else {

            dst_ptr[0] = src_ptr[0];

            process_row_cpp(
                dst_ptr,
                src_ptr,
                stride_src,
                1,
                x_e
                );

            dst_ptr[x_e] = src_ptr[x_e];
        }

        dst_ptr += stride_dst;
        src_ptr += stride_src;
    }
}

static void process_row_cpp (T *dst_ptr, const T *src_ptr, int stride_src, int x_beg, int x_end)
{
    const int      om = stride_src - 1;
    const int      o0 = stride_src    ;
    const int      op = stride_src + 1;

    src_ptr += x_beg;

    for (int x = x_beg; x < x_end; ++x)
    {
        const int        a1 = src_ptr [-op];
        const int        a2 = src_ptr [-o0];
        const int        a3 = src_ptr [-om];
        const int        a4 = src_ptr [-1 ];
        const int        c  = src_ptr [ 0 ];
        const int        a5 = src_ptr [ 1 ];
        const int        a6 = src_ptr [ om];
        const int        a7 = src_ptr [ o0];
        const int        a8 = src_ptr [ op];

        const int        res = OP::rg (c, a1, a2, a3, a4, a5, a6, a7, a8);

        dst_ptr [x] = res;

        ++ src_ptr;
    }
}

#ifdef VS_TARGET_CPU_X86
static void process_subplane_sse2 (const T *src_ptr, int stride_src, T *dst_ptr, int stride_dst, int width, int height)
{
    const int        y_b = 1;
    const int        y_e = height - 1;

    dst_ptr += y_b * stride_dst;
    src_ptr += y_b * stride_src;

    const __m128i    mask_sign = _mm_set1_epi16 (-0x8000);

    const int        x_e =   width - 1;
    const int        w8  = ((width - 2) & -8) + 1;
    const int        w7  = x_e - w8;

    for (int y = y_b; y < y_e; ++y)
    {

        if (OP::skip_line(y)) {
            memcpy(dst_ptr, src_ptr, stride_dst);
        } else {
            dst_ptr[0] = src_ptr[0];
            dst_ptr[0] = src_ptr[0];

            for (int x = 1; x < w8; x += 8) {
                __m128i            res = OP::rg(
                    src_ptr + x,
                    stride_src,
                    mask_sign
                    );

                res = OP::ConvSign::cv(res, mask_sign);
                if (sizeof(T) == 1)
                    _mm_storel_epi64(reinterpret_cast<__m128i *>(dst_ptr + x), _mm_packus_epi16(res, res));
                else
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst_ptr + x), res);
            }

            process_row_cpp(
                dst_ptr,
                src_ptr,
                stride_src,
                w8,
                x_e
                );

            dst_ptr[x_e] = src_ptr[x_e];
        }
        dst_ptr += stride_dst;
        src_ptr += stride_src;
    }
}

template <class OP1, class T1>
static void do_process_plane_sse2 (const VSFrameRef *src_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int        w             = vsapi->getFrameWidth(src_frame, plane_id);
    const int        h             = vsapi->getFrameHeight(src_frame, plane_id);
    T1 *                dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const int        stride        = vsapi->getStride(dst_frame, plane_id);

    const T1*        src_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src_frame, plane_id));

    // First line
    memcpy (dst_ptr, src_ptr, stride);

    // Main content
    PlaneProc<OP1, T1>::process_subplane_sse2(src_ptr, stride/sizeof(T1), dst_ptr, stride/sizeof(T1), w, h);

    // Last line
    const int        lp = (h - 1) * stride/sizeof(T1);
    memcpy (dst_ptr + lp, src_ptr + lp, stride);
}

#endif

template <class OP1, class T1>
static void do_process_plane_cpp (const VSFrameRef *src_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int        w             = vsapi->getFrameWidth(src_frame, plane_id);
    const int        h             = vsapi->getFrameHeight(src_frame, plane_id);
    T1 *                dst_ptr       = reinterpret_cast<T1*>(vsapi->getWritePtr(dst_frame, plane_id));
    const int        stride        = vsapi->getStride(dst_frame, plane_id);

    const T1*        src_ptr       = reinterpret_cast<const T1*>(vsapi->getReadPtr(src_frame, plane_id));

    // First line
    memcpy (dst_ptr, src_ptr, stride);

    // Main content
    PlaneProc<OP1, T1>::process_subplane_cpp(src_ptr, stride/sizeof(T1), dst_ptr, stride/sizeof(T1), w, h);

    // Last line
    const int        lp = (h - 1) * stride/sizeof(T1);
    memcpy (dst_ptr + lp, src_ptr + lp, stride);
}

};

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int mode[3];
} RemoveGrainData;

static void VS_CC removeGrainInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC removeGrainGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src_frame = vsapi->getFrameFilter(n, d->node, frameCtx);
        int planes[3] = {0, 1, 2};
        const VSFrameRef * cp_planes[3] = { d->mode[0] ? NULL : src_frame, d->mode[1] ? NULL : src_frame, d->mode[2] ? NULL : src_frame };
        VSFrameRef *dst_frame = vsapi->newVideoFrame2(vsapi->getFrameFormat(src_frame), vsapi->getFrameWidth(src_frame, 0), vsapi->getFrameHeight(src_frame, 0), cp_planes, planes, src_frame, core);


#define PROC_ARGS_16(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint16_t>(src_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint8_t>(src_frame, dst_frame, i, vsapi); break;

#ifdef VS_TARGET_CPU_X86
#define PROC_ARGS_16_FAST(op) PlaneProc <op, uint16_t>::do_process_plane_sse2<op, uint16_t>(src_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8_FAST(op) PlaneProc <op, uint8_t>::do_process_plane_sse2<op, uint8_t>(src_frame, dst_frame, i, vsapi); break;
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
                    case  5: PROC_ARGS_8_FAST(OpRG05)
                    case  6: PROC_ARGS_8_FAST(OpRG06)
                    case  7: PROC_ARGS_8_FAST(OpRG07)
                    case  8: PROC_ARGS_8_FAST(OpRG08)
                    case  9: PROC_ARGS_8_FAST(OpRG09)
                    case 10: PROC_ARGS_8_FAST(OpRG10)
                    case 11: PROC_ARGS_8_FAST(OpRG11)
                    case 12: PROC_ARGS_8_FAST(OpRG12)
                    case 13: PROC_ARGS_8(OpRG13)
                    case 14: PROC_ARGS_8(OpRG14)
                    case 15: PROC_ARGS_8(OpRG15)
                    case 16: PROC_ARGS_8(OpRG16)
                    case 17: PROC_ARGS_8_FAST(OpRG17)
                    case 18: PROC_ARGS_8_FAST(OpRG18)
                    case 19: PROC_ARGS_8_FAST(OpRG19)
                    case 20: PROC_ARGS_8_FAST(OpRG20)
                    case 21: PROC_ARGS_8_FAST(OpRG21)
                    case 22: PROC_ARGS_8_FAST(OpRG22)
                    case 23: PROC_ARGS_8(OpRG23)
                    case 24: PROC_ARGS_8(OpRG24)
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
                case  5: PROC_ARGS_16_FAST(OpRG05)
                case  6: PROC_ARGS_16_FAST(OpRG06)
                case  7: PROC_ARGS_16_FAST(OpRG07)
                case  8: PROC_ARGS_16_FAST(OpRG08)
                case  9: PROC_ARGS_16_FAST(OpRG09)
                case 10: PROC_ARGS_16_FAST(OpRG10)
                case 11: PROC_ARGS_16_FAST(OpRG11)
                case 12: PROC_ARGS_16_FAST(OpRG12)
                case 13: PROC_ARGS_16(OpRG13)
                case 14: PROC_ARGS_16(OpRG14)
                case 15: PROC_ARGS_16(OpRG15)
                case 16: PROC_ARGS_16(OpRG16)
                case 17: PROC_ARGS_16_FAST(OpRG17)
                case 18: PROC_ARGS_16_FAST(OpRG18)
                case 19: PROC_ARGS_16_FAST(OpRG19)
                case 20: PROC_ARGS_16_FAST(OpRG20)
                case 21: PROC_ARGS_16_FAST(OpRG21)
                case 22: PROC_ARGS_16_FAST(OpRG22)
                case 23: PROC_ARGS_16(OpRG23)
                case 24: PROC_ARGS_16(OpRG24)
                    default: break;
                }
            }
        }

        vsapi->freeFrame(src_frame);

        return dst_frame;
    }

    return 0;
}

static void VS_CC removeGrainFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *)instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC removeGrainCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData d;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->format) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Only constant format input supported");
        return;
    }

    if (d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Only 8-16 bit int formats supported");
        return;
    }

    int n = d.vi->format->numPlanes;
    int m = vsapi->propNumElements(in, "mode");
    if (n < m) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Number of modes specified must be equal or fewer than the number of input planes");
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (i < m) {
            d.mode[i] = int64ToIntS(vsapi->propGetInt(in, "mode", i, NULL));
            if (d.mode[i] < 0 || d.mode[i] > 24)
            {
                vsapi->freeNode(d.node);
                vsapi->setError(out, "RemoveGrain: Invalid mode specified, only modes 0-24 supported");
                return;
            }
        } else {
            d.mode[i] = d.mode[i - 1];
        }
    }

    RemoveGrainData *data = new RemoveGrainData;
    *data = d;

    vsapi->createFilter(in, out, "RemoveGrain", removeGrainInit, removeGrainGetFrame, removeGrainFree, fmParallel, 0, data, core);
}

