/*
* Copyright (c) 2012-2019 Fredrik Mellbin
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

#include <algorithm>
#include <cmath>
#include <emmintrin.h>
#include "../generic.h"

#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

namespace {

template <class T>
T *line_ptr(T *ptr, unsigned i, ptrdiff_t stride)
{
    return (T *)(((unsigned char *)ptr) + static_cast<ptrdiff_t>(i) * stride);
}

__m128i mm_blendv_epi8(__m128i a, __m128i b, __m128i mask)
{
    return _mm_or_si128(_mm_andnot_si128(mask, a), _mm_and_si128(mask, b));
}


struct ByteTraits {
    typedef uint8_t T;
    typedef __m128i vec_type;
    static constexpr unsigned vec_len = 16;

    static __m128i load(const uint8_t *ptr) { return _mm_load_si128((const __m128i *)(ptr)); }
    static __m128i loadu(const uint8_t *ptr) { return _mm_loadu_si128((const __m128i *)(ptr)); }
    static void store(uint8_t *ptr, __m128i x) { _mm_store_si128((__m128i *)ptr, x); }

    static __m128i shl_insert_lo(__m128i x, uint8_t y)
    {
        return _mm_or_si128(_mm_cvtsi32_si128(y), _mm_slli_si128(x, 1));
    }

    static __m128i shr_insert(__m128i x, uint8_t y, unsigned idx)
    {
        __m128i mask = _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm_cmpeq_epi8(mask, _mm_set1_epi8(idx));
        return mm_blendv_epi8(_mm_srli_si128(x, 1), _mm_set1_epi8(y), mask);
    }
};

struct WordTraits {
    typedef uint16_t T;
    typedef __m128i vec_type;
    static constexpr unsigned vec_len = 8;

    static __m128i load(const uint16_t *ptr) { return _mm_load_si128((const __m128i *)(ptr)); }
    static __m128i loadu(const uint16_t *ptr) { return _mm_loadu_si128((const __m128i *)(ptr)); }
    static void store(uint16_t *ptr, __m128i x) { _mm_store_si128((__m128i *)ptr, x); }

    static __m128i shl_insert_lo(__m128i x, uint16_t y)
    {
        return _mm_or_si128(_mm_cvtsi32_si128(y), _mm_slli_si128(x, 2));
    }

    static __m128i shr_insert(__m128i x, uint16_t y, unsigned idx)
    {
        __m128i mask = _mm_set_epi16(7, 6, 5, 4, 3, 2, 1, 0);
        mask = _mm_cmpeq_epi16(mask, _mm_set1_epi16(idx));
        return mm_blendv_epi8(_mm_srli_si128(x, 2), _mm_set1_epi16(y), mask);
    }
};

struct FloatTraits {
    typedef float T;
    typedef __m128 vec_type;
    static constexpr unsigned vec_len = 4;

    static __m128 load(const float *ptr) { return _mm_load_ps(ptr); }
    static __m128 loadu(const float *ptr) { return _mm_loadu_ps(ptr); }
    static void store(float *ptr, __m128 x) { _mm_store_ps(ptr, x); }

    static __m128 shl_insert_lo(__m128 x, float y)
    {
        return _mm_or_ps(_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 4)), _mm_load_ss(&y));
    }

    static __m128 shr_insert(__m128 x, float y, unsigned idx)
    {
        __m128i mask = _mm_set_epi32(3, 2, 1, 0);
        mask = _mm_cmpeq_epi32(mask, _mm_set1_epi32(idx));
        return _mm_castsi128_ps(mm_blendv_epi8(_mm_srli_si128(_mm_castps_si128(x), 4), _mm_castps_si128(_mm_set_ps1(y)), mask));
    }
};


// MSVC 32-bit only allows up to 3 vector arguments to be passed by value.
#define OP_ARGS const vec_type &a00_, const vec_type &a01_, const vec_type &a02_, const vec_type &a10_, const vec_type &a11_, const vec_type &a12_, const vec_type &a20_, const vec_type &a21_, const vec_type &a22_
#define PROLOGUE() \
  auto a00 = a00_; auto a01 = a01_; auto a02 = a02_; \
  auto a10 = a10_; auto a11 = a11_; auto a12 = a12_; \
  auto a20 = a20_; auto a21 = a21_; auto a22 = a22_;

struct PrewittSobelTraits {
    float scale;

    explicit PrewittSobelTraits(const vs_generic_params &params) : scale{ params.scale } {}
};

template <bool Sobel>
struct PrewittSobelByte : PrewittSobelTraits, ByteTraits {
    using PrewittSobelTraits::PrewittSobelTraits;

    FORCE_INLINE __m128i op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;

#define UNPCKLO(x) (_mm_unpacklo_epi8(x, _mm_setzero_si128()))
#define UNPCKHI(x) (_mm_unpackhi_epi8(x, _mm_setzero_si128()))
        __m128i gx_lo = _mm_sub_epi16(UNPCKLO(a22), UNPCKLO(a00));
        __m128i gx_hi = _mm_sub_epi16(UNPCKHI(a22), UNPCKHI(a00));
        __m128i gy_lo = gx_lo;
        __m128i gy_hi = gx_hi;

        gx_lo = _mm_add_epi16(gx_lo, UNPCKLO(a20));
        gx_lo = _mm_add_epi16(gx_lo, Sobel ? _mm_slli_epi16(UNPCKLO(a21), 1) : UNPCKLO(a21));
        gx_lo = _mm_sub_epi16(gx_lo, Sobel ? _mm_slli_epi16(UNPCKLO(a01), 1) : UNPCKLO(a01));
        gx_lo = _mm_sub_epi16(gx_lo, UNPCKLO(a02));

        gx_hi = _mm_add_epi16(gx_hi, UNPCKHI(a20));
        gx_hi = _mm_add_epi16(gx_hi, Sobel ? _mm_slli_epi16(UNPCKHI(a21), 1) : UNPCKHI(a21));
        gx_hi = _mm_sub_epi16(gx_hi, Sobel ? _mm_slli_epi16(UNPCKHI(a01), 1) : UNPCKHI(a01));
        gx_hi = _mm_sub_epi16(gx_hi, UNPCKHI(a02));

        gy_lo = _mm_add_epi16(gy_lo, UNPCKLO(a02));
        gy_lo = _mm_add_epi16(gy_lo, Sobel ? _mm_slli_epi16(UNPCKLO(a12), 1) : UNPCKLO(a12));
        gy_lo = _mm_sub_epi16(gy_lo, Sobel ? _mm_slli_epi16(UNPCKLO(a10), 1) : UNPCKLO(a10));
        gy_lo = _mm_sub_epi16(gy_lo, UNPCKLO(a20));

        gy_hi = _mm_add_epi16(gy_hi, UNPCKHI(a02));
        gy_hi = _mm_add_epi16(gy_hi, Sobel ? _mm_slli_epi16(UNPCKHI(a12), 1) : UNPCKHI(a12));
        gy_hi = _mm_sub_epi16(gy_hi, Sobel ? _mm_slli_epi16(UNPCKHI(a10), 1) : UNPCKHI(a10));
        gy_hi = _mm_sub_epi16(gy_hi, UNPCKHI(a20));

        __m128i gxy_lolo = _mm_unpacklo_epi16(gx_lo, gy_lo);
        __m128i gxy_lohi = _mm_unpackhi_epi16(gx_lo, gy_lo);
        __m128i gxy_hilo = _mm_unpacklo_epi16(gx_hi, gy_hi);
        __m128i gxy_hihi = _mm_unpackhi_epi16(gx_hi, gy_hi);
        gxy_lolo = _mm_madd_epi16(gxy_lolo, gxy_lolo);
        gxy_lohi = _mm_madd_epi16(gxy_lohi, gxy_lohi);
        gxy_hilo = _mm_madd_epi16(gxy_hilo, gxy_hilo);
        gxy_hihi = _mm_madd_epi16(gxy_hihi, gxy_hihi);

        __m128 tmpf_lolo = _mm_sqrt_ps(_mm_cvtepi32_ps(gxy_lolo));
        __m128 tmpf_lohi = _mm_sqrt_ps(_mm_cvtepi32_ps(gxy_lohi));
        __m128 tmpf_hilo = _mm_sqrt_ps(_mm_cvtepi32_ps(gxy_hilo));
        __m128 tmpf_hihi = _mm_sqrt_ps(_mm_cvtepi32_ps(gxy_hihi));
        tmpf_lolo = _mm_mul_ps(tmpf_lolo, _mm_set_ps1(scale));
        tmpf_lohi = _mm_mul_ps(tmpf_lohi, _mm_set_ps1(scale));
        tmpf_hilo = _mm_mul_ps(tmpf_hilo, _mm_set_ps1(scale));
        tmpf_hihi = _mm_mul_ps(tmpf_hihi, _mm_set_ps1(scale));

        __m128i tmpi_lo = _mm_packs_epi32(_mm_cvtps_epi32(tmpf_lolo), _mm_cvtps_epi32(tmpf_lohi));
        __m128i tmpi_hi = _mm_packs_epi32(_mm_cvtps_epi32(tmpf_hilo), _mm_cvtps_epi32(tmpf_hihi));
        return _mm_packus_epi16(tmpi_lo, tmpi_hi);
#undef UNPCKHI
#undef UNPCKLO
    }
};

template <bool Sobel>
struct PrewittSobelWord : PrewittSobelTraits, WordTraits {
    __m128i maxval;

    static uint32_t interleave(uint16_t a, uint16_t b)
    {
        return (static_cast<uint32_t>(b) << 16) | a;
    }

    explicit PrewittSobelWord(const vs_generic_params &params) :
        PrewittSobelTraits(params),
        maxval(_mm_set1_epi16(params.maxval + INT16_MIN))
    {}

    FORCE_INLINE __m128i op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;

#define UNPCKLO(x) (_mm_unpacklo_epi16(x, _mm_setzero_si128()))
#define UNPCKHI(x) (_mm_unpackhi_epi16(x, _mm_setzero_si128()))
        __m128i gx_lo = _mm_sub_epi32(UNPCKLO(a22), UNPCKLO(a00));
        __m128i gx_hi = _mm_sub_epi32(UNPCKHI(a22), UNPCKHI(a00));
        __m128i gy_lo = gx_lo;
        __m128i gy_hi = gx_hi;

        gx_lo = _mm_add_epi32(gx_lo, UNPCKLO(a20));
        gx_lo = _mm_add_epi32(gx_lo, Sobel ? _mm_slli_epi32(UNPCKLO(a21), 1) : UNPCKLO(a21));
        gx_lo = _mm_sub_epi32(gx_lo, Sobel ? _mm_slli_epi32(UNPCKLO(a01), 1) : UNPCKLO(a01));
        gx_lo = _mm_sub_epi32(gx_lo, UNPCKLO(a02));

        gx_hi = _mm_add_epi32(gx_hi, UNPCKHI(a20));
        gx_hi = _mm_add_epi32(gx_hi, Sobel ? _mm_slli_epi32(UNPCKHI(a21), 1) : UNPCKHI(a21));
        gx_hi = _mm_sub_epi32(gx_hi, Sobel ? _mm_slli_epi32(UNPCKHI(a01), 1) : UNPCKHI(a01));
        gx_hi = _mm_sub_epi32(gx_hi, UNPCKHI(a02));

        gy_lo = _mm_add_epi32(gy_lo, UNPCKLO(a02));
        gy_lo = _mm_add_epi32(gy_lo, Sobel ? _mm_slli_epi32(UNPCKLO(a12), 1) : UNPCKLO(a12));
        gy_lo = _mm_sub_epi32(gy_lo, Sobel ? _mm_slli_epi32(UNPCKLO(a10), 1) : UNPCKLO(a10));
        gy_lo = _mm_sub_epi32(gy_lo, UNPCKLO(a20));

        gy_hi = _mm_add_epi32(gy_hi, UNPCKHI(a02));
        gy_hi = _mm_add_epi32(gy_hi, Sobel ? _mm_slli_epi32(UNPCKHI(a12), 1) : UNPCKHI(a12));
        gy_hi = _mm_sub_epi32(gy_hi, Sobel ? _mm_slli_epi32(UNPCKHI(a10), 1) : UNPCKHI(a10));
        gy_hi = _mm_sub_epi32(gy_hi, UNPCKHI(a20));

        __m128 gxsq_lo = _mm_cvtepi32_ps(gx_lo);
        __m128 gxsq_hi = _mm_cvtepi32_ps(gx_hi);
        __m128 gysq_lo = _mm_cvtepi32_ps(gy_lo);
        __m128 gysq_hi = _mm_cvtepi32_ps(gy_hi);
        gxsq_lo = _mm_mul_ps(gxsq_lo, gxsq_lo);
        gxsq_hi = _mm_mul_ps(gxsq_hi, gxsq_hi);
        gysq_lo = _mm_mul_ps(gysq_lo, gysq_lo);
        gysq_hi = _mm_mul_ps(gysq_hi, gysq_hi);

        __m128 gxy_lo = _mm_add_ps(gxsq_lo, gysq_lo);
        __m128 gxy_hi = _mm_add_ps(gxsq_hi, gysq_hi);
        gxy_lo = _mm_sqrt_ps(gxy_lo);
        gxy_lo = _mm_mul_ps(gxy_lo, _mm_set_ps1(scale));
        gxy_hi = _mm_sqrt_ps(gxy_hi);
        gxy_hi = _mm_mul_ps(gxy_hi, _mm_set_ps1(scale));

        __m128i tmpi_lo = _mm_cvtps_epi32(gxy_lo);
        __m128i tmpi_hi = _mm_cvtps_epi32(gxy_hi);
        tmpi_lo = _mm_add_epi32(tmpi_lo, _mm_set1_epi32(INT16_MIN));
        tmpi_hi = _mm_add_epi32(tmpi_hi, _mm_set1_epi32(INT16_MIN));

        __m128i tmp = _mm_packs_epi32(tmpi_lo, tmpi_hi);
        tmp = _mm_min_epi16(tmp, maxval);
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));
        return tmp;
#undef UNPCKHI
#undef UNPCKLO
    }
};

template <bool Sobel>
struct PrewittSobelFloat : PrewittSobelTraits, FloatTraits {
    using PrewittSobelTraits::PrewittSobelTraits;

    FORCE_INLINE __m128 op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;

        __m128 gx = _mm_sub_ps(a22, a00);
        __m128 gy = gx;

        gx = _mm_add_ps(gx, a20);
        gx = _mm_add_ps(gx, Sobel ? _mm_mul_ps(a21, _mm_set_ps1(2.0f)) : a21);
        gx = _mm_sub_ps(gx, Sobel ? _mm_mul_ps(a01, _mm_set_ps1(2.0f)) : a01);
        gx = _mm_sub_ps(gx, a02);

        gy = _mm_add_ps(gy, a02);
        gy = _mm_add_ps(gy, Sobel ? _mm_mul_ps(a12, _mm_set_ps1(2.0f)) : a12);
        gy = _mm_sub_ps(gy, Sobel ? _mm_mul_ps(a10, _mm_set_ps1(2.0f)) : a10);
        gy = _mm_sub_ps(gy, a20);

        gx = _mm_mul_ps(gx, gx);
        gy = _mm_mul_ps(gy, gy);

        __m128 tmp = _mm_add_ps(gx, gy);
        tmp = _mm_sqrt_ps(tmp);
        tmp = _mm_mul_ps(tmp, _mm_set_ps1(scale));
        return tmp;
    }
};

template <class Derived, class vec_type>
struct MinMaxTraits {
    vec_type mask00;
    vec_type mask01;
    vec_type mask02;
    vec_type mask10;
    vec_type mask12;
    vec_type mask20;
    vec_type mask21;
    vec_type mask22;

    explicit MinMaxTraits(const vs_generic_params &params) :
        mask00((params.stencil & 0x01) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask01((params.stencil & 0x02) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask02((params.stencil & 0x04) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask10((params.stencil & 0x08) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask12((params.stencil & 0x10) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask20((params.stencil & 0x20) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask21((params.stencil & 0x40) ? Derived::enabled_mask() : Derived::disabled_mask()),
        mask22((params.stencil & 0x80) ? Derived::enabled_mask() : Derived::disabled_mask())
    {}

    FORCE_INLINE vec_type apply_stencil(OP_ARGS)
    {
        PROLOGUE();

        vec_type val = a11;
        val = Derived::reduce(val, a00, mask00);
        val = Derived::reduce(val, a01, mask01);
        val = Derived::reduce(val, a02, mask02);
        val = Derived::reduce(val, a10, mask10);
        val = Derived::reduce(val, a12, mask12);
        val = Derived::reduce(val, a20, mask20);
        val = Derived::reduce(val, a21, mask21);
        val = Derived::reduce(val, a22, mask22);
        return val;
    }
};

template <bool Max>
static __m128i limit_diff_epu8(__m128i val, __m128i orig, __m128i threshold)
{
    __m128i limit = Max ? _mm_adds_epu8(orig, threshold) : _mm_subs_epu8(orig, threshold);
    val = Max ? _mm_min_epu8(val, limit) : _mm_max_epu8(val, limit);
    return val;
}

template <bool Max>
static __m128i limit_diff_epi16(__m128i val, __m128i orig, __m128i threshold)
{
    // val in signed domain. orig and threshold in unsigned domain.
    __m128i limit = Max ? _mm_adds_epu16(orig, threshold) : _mm_subs_epu16(orig, threshold);
    limit = _mm_add_epi16(limit, _mm_set1_epi16(INT16_MIN));
    val = Max ? _mm_min_epi16(val, limit) : _mm_max_epi16(val, limit);
    return val;
}

template <bool Max>
static __m128 limit_diff_ps(__m128 val, __m128 orig, __m128 threshold)
{
    __m128 limit = Max ? _mm_add_ps(orig, threshold) : _mm_sub_ps(orig, threshold);
    val = Max ? _mm_min_ps(val, limit) : _mm_max_ps(val, limit);
    return val;
}

template <bool Max>
struct MinMaxByte : MinMaxTraits<MinMaxByte<Max>, __m128i>, ByteTraits {
    typedef MinMaxTraits<MinMaxByte<Max>, __m128i> MinMaxTraitsT;
    __m128i threshold;

    static __m128i enabled_mask() { return Max ? _mm_set1_epi8(UINT8_MAX) : _mm_setzero_si128(); }
    static __m128i disabled_mask() { return Max ? _mm_setzero_si128() : _mm_set1_epi8(UINT8_MAX); }

    static __m128i reduce(__m128i lhs, __m128i rhs, __m128i mask)
    {
        return Max ? _mm_max_epu8(lhs, _mm_and_si128(mask, rhs)) : _mm_min_epu8(lhs, _mm_or_si128(mask, rhs));
    }

    explicit MinMaxByte(const vs_generic_params &params) :
        MinMaxTraitsT(params),
        threshold(_mm_set1_epi8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128i val = MinMaxTraitsT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        return limit_diff_epu8<Max>(val, a11, threshold);
    }
};

template <bool Max>
struct MinMaxWord : MinMaxTraits<MinMaxWord<Max>, __m128i>, WordTraits {
    typedef MinMaxTraits<MinMaxWord<Max>, __m128i> MinMaxTraitsT;
    __m128i threshold;

    static __m128i enabled_mask() { return Max ? _mm_set1_epi16(UINT16_MAX) : _mm_setzero_si128(); }
    static __m128i disabled_mask() { return Max ? _mm_setzero_si128() : _mm_set1_epi16(UINT16_MAX); }

    FORCE_INLINE static __m128i reduce(__m128i lhs, __m128i rhs, __m128i mask)
    {
        // lhs is in signed domain. rhs is in unsigned domain (for masking).
        rhs = Max ? _mm_and_si128(mask, rhs) : _mm_or_si128(mask, rhs);
        rhs = _mm_add_epi16(rhs, _mm_set1_epi16(INT16_MIN));
        return Max ? _mm_max_epi16(lhs, rhs) : _mm_min_epi16(lhs, rhs);
    }

    explicit MinMaxWord(const vs_generic_params &params) :
        MinMaxTraitsT(params),
        threshold(_mm_set1_epi16(params.threshold))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128i a11_signed = _mm_add_epi16(a11, _mm_set1_epi16(INT16_MIN));
        __m128i val = MinMaxTraitsT::apply_stencil(a00, a01, a02, a10, a11_signed, a12, a20, a21, a22);
        val = limit_diff_epi16<Max>(val, a11, threshold);
        val = _mm_sub_epi16(val, _mm_set1_epi16(INT16_MIN));
        return val;
    }
};

template <bool Max>
struct MinMaxFloat : MinMaxTraits<MinMaxFloat<Max>, __m128>, FloatTraits {
    typedef MinMaxTraits<MinMaxFloat<Max>, __m128> MinMaxTraitsT;
    __m128 threshold;

    static __m128 enabled_mask() { return Max ? _mm_set_ps1(INFINITY) : _mm_set_ps1(-INFINITY); }
    static __m128 disabled_mask() { return Max ? _mm_set_ps1(-INFINITY) : _mm_set_ps1(INFINITY); }

    FORCE_INLINE static __m128 reduce(__m128 lhs, __m128 rhs, __m128 mask)
    {
        // INFINITY is not a bit mask, so need to use min/max on rhs instead of and/or.
        return Max ? _mm_max_ps(lhs, _mm_min_ps(rhs, mask)) : _mm_min_ps(lhs, _mm_max_ps(rhs, mask));
    }

    explicit MinMaxFloat(const vs_generic_params &params) :
        MinMaxTraitsT(params),
        threshold(_mm_set_ps1(params.thresholdf))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128 val = MinMaxTraitsT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        return limit_diff_ps<Max>(val, a11, threshold);
    }
};

constexpr uint8_t STENCIL_ALL = 0xFF;
constexpr uint8_t STENCIL_H = 0x18;
constexpr uint8_t STENCIL_V = 0x42;
constexpr uint8_t STENCIL_PLUS = STENCIL_H | STENCIL_V;

template <uint8_t Stencil, class Derived, class vec_type>
struct MinMaxFixedTraits {
    static FORCE_INLINE vec_type apply_stencil(OP_ARGS)
    {
        PROLOGUE();

        vec_type val = a11;
        val = (Stencil & 0x01) ? Derived::reduce(val, a00) : val;
        val = (Stencil & 0x02) ? Derived::reduce(val, a01) : val;
        val = (Stencil & 0x04) ? Derived::reduce(val, a02) : val;
        val = (Stencil & 0x08) ? Derived::reduce(val, a10) : val;
        val = (Stencil & 0x10) ? Derived::reduce(val, a12) : val;
        val = (Stencil & 0x20) ? Derived::reduce(val, a20) : val;
        val = (Stencil & 0x40) ? Derived::reduce(val, a21) : val;
        val = (Stencil & 0x80) ? Derived::reduce(val, a22) : val;
        return val;
    }
};

template <uint8_t Stencil, bool Max>
struct MinMaxFixedByte : MinMaxFixedTraits<Stencil, MinMaxFixedByte<Stencil, Max>, __m128i>, ByteTraits {
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedByte, __m128i> MinMaxFixedTraitsT;
    __m128i threshold;

    static __m128i reduce(__m128i lhs, __m128i rhs)
    {
        return Max ? _mm_max_epu8(lhs, rhs) : _mm_min_epu8(lhs, rhs);
    }

    explicit MinMaxFixedByte(const vs_generic_params &params) :
        threshold(_mm_set1_epi8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128i val = MinMaxFixedTraitsT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        return limit_diff_epu8<Max>(val, a11, threshold);
    }
};

template <uint8_t Stencil, bool Max>
struct MinMaxFixedWord : MinMaxFixedTraits<Stencil, MinMaxFixedWord<Stencil, Max>, __m128i>, WordTraits {
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedWord, __m128i> MinMaxFixedTraitsT;
    __m128i threshold;

    static __m128i reduce(__m128i lhs, __m128i rhs)
    {
        // lhs in signed domain. rhs in unsigned domain.
        return Max ? _mm_max_epi16(lhs, _mm_add_epi16(rhs, _mm_set1_epi16(INT16_MIN))) : _mm_min_epi16(lhs, _mm_add_epi16(rhs, _mm_set1_epi16(INT16_MIN)));
    }

    explicit MinMaxFixedWord(const vs_generic_params &params) :
        threshold(_mm_set1_epi16(params.threshold))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128i a11_signed = _mm_add_epi16(a11, _mm_set1_epi16(INT16_MIN));
        __m128i val = MinMaxFixedTraitsT::apply_stencil(a00, a01, a02, a10, a11_signed, a12, a20, a21, a22);
        val = limit_diff_epi16<Max>(val, a11, threshold);
        val = _mm_sub_epi16(val, _mm_set1_epi16(INT16_MIN));
        return val;
    }
};

template <uint8_t Stencil, bool Max>
struct MinMaxFixedFloat : MinMaxFixedTraits<Stencil, MinMaxFixedFloat<Stencil, Max>, __m128>, FloatTraits {
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedFloat<Stencil, Max>, __m128> MinMaxFixedTraitsT;
    __m128 threshold;

    FORCE_INLINE static __m128 reduce(__m128 lhs, __m128 rhs)
    {
        return Max ? _mm_max_ps(lhs, rhs) : _mm_min_ps(lhs, rhs);
    }

    explicit MinMaxFixedFloat(const vs_generic_params &params) : threshold(_mm_set_ps1(params.thresholdf)) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128 val = MinMaxFixedTraitsT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        return limit_diff_ps<Max>(val, a11, threshold);
    }
};

template <class Derived, class vec_type>
struct MedianTraits {
    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        Derived::compare_exchange(a00, a01);
        Derived::compare_exchange(a02, a10);
        Derived::compare_exchange(a12, a20);
        Derived::compare_exchange(a21, a22);

        Derived::compare_exchange(a00, a02);
        Derived::compare_exchange(a01, a10);
        Derived::compare_exchange(a12, a21);
        Derived::compare_exchange(a20, a22);

        Derived::compare_exchange(a01, a02);
        Derived::compare_exchange(a20, a21);

        a12 = Derived::max(a00, a12);
        a20 = Derived::max(a01, a20);
        a02 = Derived::min(a02, a21);
        a10 = Derived::min(a10, a22);

        a12 = Derived::max(a02, a12);
        a10 = Derived::min(a10, a20);

        Derived::compare_exchange(a10, a12);

        a11 = Derived::max(a10, a11);
        a11 = Derived::min(a11, a12);
        return a11;
    }
};

struct MedianByte : MedianTraits<MedianByte, __m128i>, ByteTraits {
    static __m128i min(__m128i lhs, __m128i rhs) { return _mm_min_epu8(lhs, rhs); }
    static __m128i max(__m128i lhs, __m128i rhs) { return _mm_max_epu8(lhs, rhs); }

    static FORCE_INLINE void compare_exchange(__m128i &lhs, __m128i &rhs)
    {
        __m128i a = lhs;
        __m128i b = rhs;
        lhs = _mm_min_epu8(a, b);
        rhs = _mm_max_epu8(a, b);
    }

    explicit MedianByte(const vs_generic_params &) {}
};

struct MedianWord : MedianTraits<MedianWord, __m128i>, WordTraits {
    static __m128i min(__m128i lhs, __m128i rhs) { return _mm_min_epi16(lhs, rhs); }
    static __m128i max(__m128i lhs, __m128i rhs) { return _mm_max_epi16(lhs, rhs); }

    static FORCE_INLINE void compare_exchange(__m128i &lhs, __m128i &rhs)
    {
        __m128i a = lhs;
        __m128i b = rhs;
        lhs = _mm_min_epi16(a, b);
        rhs = _mm_max_epi16(a, b);
    }

    explicit MedianWord(const vs_generic_params &) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

#define SIGNED16(x) (_mm_add_epi16((x), _mm_set1_epi16(INT16_MIN)))
        a00 = SIGNED16(a00); a01 = SIGNED16(a01); a02 = SIGNED16(a02);
        a10 = SIGNED16(a10); a11 = SIGNED16(a11); a12 = SIGNED16(a12);
        a20 = SIGNED16(a20); a21 = SIGNED16(a21); a22 = SIGNED16(a22);
#undef SIGNED16

        __m128i val = MedianTraits<MedianWord, __m128i>::op(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        val = _mm_sub_epi16(val, _mm_set1_epi16(INT16_MIN));
        return val;
    }
};

struct MedianFloat : MedianTraits<MedianFloat, __m128>, FloatTraits {
    static __m128 min(__m128 lhs, __m128 rhs) { return _mm_min_ps(lhs, rhs); }
    static __m128 max(__m128 lhs, __m128 rhs) { return _mm_max_ps(lhs, rhs); }

    static FORCE_INLINE void compare_exchange(__m128 &lhs, __m128 &rhs)
    {
        __m128 a = lhs;
        __m128 b = rhs;
        lhs = _mm_min_ps(a, b);
        rhs = _mm_max_ps(a, b);
    }

    explicit MedianFloat(const vs_generic_params &) {}
};

template <bool Inflate>
struct DeflateInflateByte : ByteTraits {
    __m128i threshold;

    explicit DeflateInflateByte(const vs_generic_params &params) :
        threshold(_mm_set1_epi8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

#define UNPCKLO(x) (_mm_unpacklo_epi8(x, _mm_setzero_si128()))
#define UNPCKHI(x) (_mm_unpackhi_epi8(x, _mm_setzero_si128()))
        __m128i accum_lo = UNPCKLO(a00);
        __m128i accum_hi = UNPCKHI(a00);
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a01));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a01));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a02));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a02));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a10));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a10));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a12));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a12));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a20));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a20));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a21));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a21));
        accum_lo = _mm_add_epi16(accum_lo, UNPCKLO(a22));
        accum_hi = _mm_add_epi16(accum_hi, UNPCKHI(a22));
        accum_lo = _mm_add_epi16(accum_lo, _mm_set1_epi16(4));
        accum_hi = _mm_add_epi16(accum_hi, _mm_set1_epi16(4));

        accum_lo = _mm_srli_epi16(accum_lo, 3);
        accum_hi = _mm_srli_epi16(accum_hi, 3);

        __m128i tmp = _mm_packus_epi16(accum_lo, accum_hi);
        tmp = Inflate ? _mm_max_epu8(tmp, a11) : _mm_min_epu8(tmp, a11);

        __m128i limit = Inflate ? _mm_adds_epu8(a11, threshold) : _mm_subs_epu8(a11, threshold);
        tmp = Inflate ? _mm_min_epu8(tmp, limit) : _mm_max_epu8(tmp, limit);

        return tmp;
#undef UNPCKHI
#undef UNPCKLO
    }
};

template <bool Inflate>
struct DeflateInflateWord : WordTraits {
    __m128i threshold;

    explicit DeflateInflateWord(const vs_generic_params &params) : threshold(_mm_set1_epi16(params.threshold)) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

#define UNPCKLO(x) (_mm_unpacklo_epi16(x, _mm_setzero_si128()))
#define UNPCKHI(x) (_mm_unpackhi_epi16(x, _mm_setzero_si128()))
        __m128i accum_lo = UNPCKLO(a00);
        __m128i accum_hi = UNPCKHI(a00);
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a01));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a01));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a02));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a02));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a10));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a10));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a12));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a12));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a20));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a20));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a21));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a21));
        accum_lo = _mm_add_epi32(accum_lo, UNPCKLO(a22));
        accum_hi = _mm_add_epi32(accum_hi, UNPCKHI(a22));
        accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(4));
        accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(4));

        accum_lo = _mm_srli_epi32(accum_lo, 3);
        accum_hi = _mm_srli_epi32(accum_hi, 3);
        accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(INT16_MIN));
        accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(INT16_MIN));

        __m128i tmp = _mm_packs_epi32(accum_lo, accum_hi);
        __m128i a11_signed = _mm_add_epi16(a11, _mm_set1_epi16(INT16_MIN));
        tmp = Inflate ? _mm_max_epi16(tmp, a11_signed) : _mm_min_epi16(tmp, a11_signed);

        __m128i limit = Inflate ? _mm_adds_epu16(a11, threshold) : _mm_subs_epu16(a11, threshold);
        limit = _mm_add_epi16(limit, _mm_set1_epi16(INT16_MIN));
        tmp = Inflate ? _mm_min_epi16(tmp, limit) : _mm_max_epi16(tmp, limit);
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));

        return tmp;
#undef UNPCKHI
#undef UNPCKLO
    }
};

template <bool Inflate>
struct DeflateInflateFloat : FloatTraits {
    __m128 threshold;

    explicit DeflateInflateFloat(const vs_generic_params &params) : threshold(_mm_set_ps1(params.thresholdf)) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128 accum0 = _mm_add_ps(a00, a01);
        __m128 accum1 = _mm_add_ps(a02, a10);
        accum0 = _mm_add_ps(accum0, a12);
        accum1 = _mm_add_ps(accum1, a20);
        accum0 = _mm_add_ps(accum0, a21);
        accum1 = _mm_add_ps(accum1, a22);

        __m128 tmp = _mm_add_ps(accum0, accum1);
        tmp = _mm_mul_ps(tmp, _mm_set_ps1(1.0f / 8.0f));
        tmp = Inflate ? _mm_max_ps(tmp, a11) : _mm_min_ps(tmp, a11);

        __m128 limit = Inflate ? _mm_add_ps(a11, threshold) : _mm_sub_ps(a11, threshold);
        tmp = Inflate ? _mm_min_ps(tmp, limit) : _mm_max_ps(tmp, limit);

        return tmp;
    }
};

struct ConvolutionTraits {
    __m128 div;
    __m128 bias;
    __m128 saturate_mask;

    explicit ConvolutionTraits(const vs_generic_params &params) :
        div(_mm_set_ps1(params.div)),
        bias(_mm_set_ps1(params.bias)),
        saturate_mask(_mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF)))
    {}
};

struct ConvolutionIntTraits : ConvolutionTraits {
    __m128i c00_01, c02_10, c11_12, c20_21, c22_xx;

    static uint32_t interleave(int16_t a, int16_t b) { return (static_cast<uint32_t>(b) << 16) | static_cast<uint16_t>(a); }

    explicit ConvolutionIntTraits(const vs_generic_params &params) :
        ConvolutionTraits(params),
        c00_01(_mm_set1_epi32(interleave(params.matrix[0], params.matrix[1]))),
        c02_10(_mm_set1_epi32(interleave(params.matrix[2], params.matrix[3]))),
        c11_12(_mm_set1_epi32(interleave(params.matrix[4], params.matrix[5]))),
        c20_21(_mm_set1_epi32(interleave(params.matrix[6], params.matrix[7]))),
        c22_xx(_mm_set1_epi32(interleave(params.matrix[8], 0)))
    {}
};

struct ConvolutionByte : ConvolutionIntTraits, ByteTraits {
    using ConvolutionIntTraits::ConvolutionIntTraits;

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

#define UNPCKLO(x) (_mm_unpacklo_epi8(x, _mm_setzero_si128()))
#define UNPCKHI(x) (_mm_unpackhi_epi8(x, _mm_setzero_si128()))
        __m128i accum_lolo, accum_lohi, accum_hilo, accum_hihi;
        __m128i tmp0_lo, tmp0_hi, tmp1_lo, tmp1_hi;

        tmp0_lo = UNPCKLO(a00);
        tmp0_hi = UNPCKHI(a00);
        tmp1_lo = UNPCKLO(a01);
        tmp1_hi = UNPCKHI(a01);
        accum_lolo = _mm_madd_epi16(c00_01, _mm_unpacklo_epi16(tmp0_lo, tmp1_lo));
        accum_lohi = _mm_madd_epi16(c00_01, _mm_unpackhi_epi16(tmp0_lo, tmp1_lo));
        accum_hilo = _mm_madd_epi16(c00_01, _mm_unpacklo_epi16(tmp0_hi, tmp1_hi));
        accum_hihi = _mm_madd_epi16(c00_01, _mm_unpackhi_epi16(tmp0_hi, tmp1_hi));

        tmp0_lo = UNPCKLO(a02);
        tmp0_hi = UNPCKHI(a02);
        tmp1_lo = UNPCKLO(a10);
        tmp1_hi = UNPCKHI(a10);
        accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(c02_10, _mm_unpacklo_epi16(tmp0_lo, tmp1_lo)));
        accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(c02_10, _mm_unpackhi_epi16(tmp0_lo, tmp1_lo)));
        accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(c02_10, _mm_unpacklo_epi16(tmp0_hi, tmp1_hi)));
        accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(c02_10, _mm_unpackhi_epi16(tmp0_hi, tmp1_hi)));

        tmp0_lo = UNPCKLO(a11);
        tmp0_hi = UNPCKHI(a11);
        tmp1_lo = UNPCKLO(a12);
        tmp1_hi = UNPCKHI(a12);
        accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(c11_12, _mm_unpacklo_epi16(tmp0_lo, tmp1_lo)));
        accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(c11_12, _mm_unpackhi_epi16(tmp0_lo, tmp1_lo)));
        accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(c11_12, _mm_unpacklo_epi16(tmp0_hi, tmp1_hi)));
        accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(c11_12, _mm_unpackhi_epi16(tmp0_hi, tmp1_hi)));

        tmp0_lo = UNPCKLO(a20);
        tmp0_hi = UNPCKHI(a20);
        tmp1_lo = UNPCKLO(a21);
        tmp1_hi = UNPCKHI(a21);
        accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(c20_21, _mm_unpacklo_epi16(tmp0_lo, tmp1_lo)));
        accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(c20_21, _mm_unpackhi_epi16(tmp0_lo, tmp1_lo)));
        accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(c20_21, _mm_unpacklo_epi16(tmp0_hi, tmp1_hi)));
        accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(c20_21, _mm_unpackhi_epi16(tmp0_hi, tmp1_hi)));

        tmp0_lo = UNPCKLO(a22);
        tmp0_hi = UNPCKHI(a22);
        accum_lolo = _mm_add_epi32(accum_lolo, _mm_madd_epi16(c22_xx, _mm_unpacklo_epi16(tmp0_lo, _mm_setzero_si128())));
        accum_lohi = _mm_add_epi32(accum_lohi, _mm_madd_epi16(c22_xx, _mm_unpackhi_epi16(tmp0_lo, _mm_setzero_si128())));
        accum_hilo = _mm_add_epi32(accum_hilo, _mm_madd_epi16(c22_xx, _mm_unpacklo_epi16(tmp0_hi, _mm_setzero_si128())));
        accum_hihi = _mm_add_epi32(accum_hihi, _mm_madd_epi16(c22_xx, _mm_unpackhi_epi16(tmp0_hi, _mm_setzero_si128())));

        __m128 tmpf_lolo = _mm_cvtepi32_ps(accum_lolo);
        __m128 tmpf_lohi = _mm_cvtepi32_ps(accum_lohi);
        __m128 tmpf_hilo = _mm_cvtepi32_ps(accum_hilo);
        __m128 tmpf_hihi = _mm_cvtepi32_ps(accum_hihi);
        tmpf_lolo = _mm_add_ps(_mm_mul_ps(tmpf_lolo, div), bias);
        tmpf_lohi = _mm_add_ps(_mm_mul_ps(tmpf_lohi, div), bias);
        tmpf_hilo = _mm_add_ps(_mm_mul_ps(tmpf_hilo, div), bias);
        tmpf_hihi = _mm_add_ps(_mm_mul_ps(tmpf_hihi, div), bias);
        tmpf_lolo = _mm_and_ps(tmpf_lolo, saturate_mask);
        tmpf_lohi = _mm_and_ps(tmpf_lohi, saturate_mask);
        tmpf_hilo = _mm_and_ps(tmpf_hilo, saturate_mask);
        tmpf_hihi = _mm_and_ps(tmpf_hihi, saturate_mask);

        accum_lolo = _mm_cvtps_epi32(tmpf_lolo);
        accum_lohi = _mm_cvtps_epi32(tmpf_lohi);
        accum_hilo = _mm_cvtps_epi32(tmpf_hilo);
        accum_hihi = _mm_cvtps_epi32(tmpf_hihi);

        accum_lolo = _mm_packs_epi32(accum_lolo, accum_lohi);
        accum_hilo = _mm_packs_epi32(accum_hilo, accum_hihi);
        accum_lolo = _mm_packus_epi16(accum_lolo, accum_hilo);
        return accum_lolo;
#undef UNPCKHI
#undef UNPCKLO
    }
};

struct ConvolutionWord : ConvolutionIntTraits, WordTraits {
    __m128i maxval;

    explicit ConvolutionWord(const vs_generic_params &params) :
        ConvolutionIntTraits(params),
        maxval(_mm_set1_epi16(static_cast<int16_t>(static_cast<int32_t>(params.maxval) + INT16_MIN)))
    {
        int32_t x = 0;

        for (unsigned i = 0; i < 9; ++i) {
            x += params.matrix[i];
        }

        // Use the 10th weight to subtract the bias "INT16_MIN * sum(matrix)"
        c22_xx = _mm_set1_epi32(interleave(params.matrix[8], static_cast<int16_t>(-x)));
    }

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128i accum_lo, accum_hi;

        a00 = _mm_add_epi16(a00, _mm_set1_epi16(INT16_MIN));
        a01 = _mm_add_epi16(a01, _mm_set1_epi16(INT16_MIN));
        a02 = _mm_add_epi16(a02, _mm_set1_epi16(INT16_MIN));
        a10 = _mm_add_epi16(a10, _mm_set1_epi16(INT16_MIN));
        a11 = _mm_add_epi16(a11, _mm_set1_epi16(INT16_MIN));
        a12 = _mm_add_epi16(a12, _mm_set1_epi16(INT16_MIN));
        a20 = _mm_add_epi16(a20, _mm_set1_epi16(INT16_MIN));
        a21 = _mm_add_epi16(a21, _mm_set1_epi16(INT16_MIN));
        a22 = _mm_add_epi16(a22, _mm_set1_epi16(INT16_MIN));

        accum_lo = _mm_madd_epi16(c00_01, _mm_unpacklo_epi16(a00, a01));
        accum_hi = _mm_madd_epi16(c00_01, _mm_unpackhi_epi16(a00, a01));
        accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(c02_10, _mm_unpacklo_epi16(a02, a10)));
        accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(c02_10, _mm_unpackhi_epi16(a02, a10)));
        accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(c11_12, _mm_unpacklo_epi16(a11, a12)));
        accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(c11_12, _mm_unpackhi_epi16(a11, a12)));
        accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(c20_21, _mm_unpacklo_epi16(a20, a21)));
        accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(c20_21, _mm_unpackhi_epi16(a20, a21)));
        accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(c22_xx, _mm_unpacklo_epi16(a22, _mm_set1_epi16(INT16_MIN))));
        accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(c22_xx, _mm_unpackhi_epi16(a22, _mm_set1_epi16(INT16_MIN))));

        __m128 tmpf_lo = _mm_cvtepi32_ps(accum_lo);
        __m128 tmpf_hi = _mm_cvtepi32_ps(accum_hi);
        tmpf_lo = _mm_add_ps(_mm_mul_ps(tmpf_lo, div), bias);
        tmpf_hi = _mm_add_ps(_mm_mul_ps(tmpf_hi, div), bias);
        tmpf_lo = _mm_and_ps(tmpf_lo, saturate_mask);
        tmpf_hi = _mm_and_ps(tmpf_hi, saturate_mask);

        accum_lo = _mm_cvtps_epi32(tmpf_lo);
        accum_hi = _mm_cvtps_epi32(tmpf_hi);
        accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(INT16_MIN));
        accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(INT16_MIN));

        __m128i tmp = _mm_packs_epi32(accum_lo, accum_hi);
        tmp = _mm_min_epi16(tmp, maxval);
        tmp = _mm_sub_epi16(tmp, _mm_set1_epi16(INT16_MIN));
        return tmp;
    }
};

struct ConvolutionFloat : ConvolutionTraits, FloatTraits {
    __m128 c00, c01, c02, c10, c11, c12, c20, c21, c22;

    explicit ConvolutionFloat(const vs_generic_params &params) :
        ConvolutionTraits(params),
        c00(_mm_set_ps1(params.matrixf[0] * params.div)),
        c01(_mm_set_ps1(params.matrixf[1] * params.div)),
        c02(_mm_set_ps1(params.matrixf[2] * params.div)),
        c10(_mm_set_ps1(params.matrixf[3] * params.div)),
        c11(_mm_set_ps1(params.matrixf[4] * params.div)),
        c12(_mm_set_ps1(params.matrixf[5] * params.div)),
        c20(_mm_set_ps1(params.matrixf[6] * params.div)),
        c21(_mm_set_ps1(params.matrixf[7] * params.div)),
        c22(_mm_set_ps1(params.matrixf[8] * params.div))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();

        __m128 accum0 = _mm_mul_ps(c00, a00);
        __m128 accum1 = _mm_mul_ps(c01, a01);
        accum0 = _mm_add_ps(accum0, _mm_mul_ps(c02, a02));
        accum1 = _mm_add_ps(accum1, _mm_mul_ps(c10, a10));
        accum0 = _mm_add_ps(accum0, _mm_mul_ps(c11, a11));
        accum1 = _mm_add_ps(accum1, _mm_mul_ps(c12, a12));
        accum0 = _mm_add_ps(accum0, _mm_mul_ps(c20, a20));
        accum1 = _mm_add_ps(accum1, _mm_mul_ps(c21, a21));
        accum0 = _mm_add_ps(accum0, _mm_mul_ps(c22, a22));
        accum1 = _mm_add_ps(accum1, bias);

        __m128 tmp = _mm_add_ps(accum0, accum1);
        tmp = _mm_and_ps(tmp, saturate_mask);
        return tmp;
    }
};

#undef PROLOGUE
#undef OP_ARGS


template <class Traits>
void filter_plane_3x3(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename Traits::T T;
    typedef typename Traits::vec_type vec_type;

    Traits traits{ params };

    unsigned vec_end = (width - 1) & ~(Traits::vec_len - 1);

#define INVOKE(p0, p1, p2) (traits.op(Traits::loadu(p0 - 1), Traits::load(p0), Traits::loadu(p0 + 1), Traits::loadu(p1 - 1), Traits::load(p1), Traits::loadu(p1 + 1), Traits::loadu(p2 - 1), Traits::load(p2), Traits::loadu(p2 + 1)))
    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? std::min(1U, height - 1) : i - 1;
        unsigned below_idx = i == height - 1 ? height - std::min(2U, height) : i + 1;

        const T *srcp0 = static_cast<const T *>(line_ptr(src, above_idx, src_stride));
        const T *srcp1 = static_cast<const T *>(line_ptr(src, i, src_stride));
        const T *srcp2 = static_cast<const T *>(line_ptr(src, below_idx, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        {
            vec_type a01 = Traits::load(srcp0);
            vec_type a11 = Traits::load(srcp1);
            vec_type a21 = Traits::load(srcp2);

            vec_type a00 = Traits::shl_insert_lo(a01, srcp0[std::min(1U, width - 1)]);
            vec_type a10 = Traits::shl_insert_lo(a11, srcp1[std::min(1U, width - 1)]);
            vec_type a20 = Traits::shl_insert_lo(a21, srcp2[std::min(1U, width - 1)]);

            vec_type a02, a12, a22;
            if (width > Traits::vec_len) {
                a02 = Traits::loadu(srcp0 + 1);
                a12 = Traits::loadu(srcp1 + 1);
                a22 = Traits::loadu(srcp2 + 1);
            } else {
                a02 = Traits::shr_insert(a01, srcp0[width - std::min(2U, width)], width - 1);
                a12 = Traits::shr_insert(a11, srcp1[width - std::min(2U, width)], width - 1);
                a22 = Traits::shr_insert(a21, srcp2[width - std::min(2U, width)], width - 1);
            }

            vec_type val = traits.op(a00, a01, a02, a10, a11, a12, a20, a21, a22);
            Traits::store(dstp + 0, val);
        }

        for (unsigned j = Traits::vec_len; j < vec_end; j += Traits::vec_len) {
            vec_type val = INVOKE(srcp0 + j, srcp1 + j, srcp2 + j);
            Traits::store(dstp + j, val);
        }

        if (vec_end >= Traits::vec_len) {
            vec_type a00 = Traits::loadu(srcp0 + vec_end - 1);
            vec_type a10 = Traits::loadu(srcp1 + vec_end - 1);
            vec_type a20 = Traits::loadu(srcp2 + vec_end - 1);

            vec_type a01 = Traits::load(srcp0 + vec_end);
            vec_type a11 = Traits::load(srcp1 + vec_end);
            vec_type a21 = Traits::load(srcp2 + vec_end);

            vec_type a02 = Traits::shr_insert(a01, srcp0[width - 2], width - vec_end - 1);
            vec_type a12 = Traits::shr_insert(a11, srcp1[width - 2], width - vec_end - 1);
            vec_type a22 = Traits::shr_insert(a21, srcp2[width - 2], width - vec_end - 1);

            vec_type val = traits.op(a00, a01, a02, a10, a11, a12, a20, a21, a22);
            Traits::store(dstp + vec_end, val);
        }
    }
#undef INVOKE
}

} // namespace


void vs_generic_3x3_prewitt_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelByte<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelWord<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelFloat<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelByte<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelWord<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelFloat<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_H, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_V, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_PLUS, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_ALL, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxByte<false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_min_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_H, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_V, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_PLUS, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_ALL, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxWord<false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_min_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_H, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_V, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_PLUS, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_ALL, false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxFloat<false>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_max_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_H, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_V, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_PLUS, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedByte<STENCIL_ALL, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxByte<true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_max_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_H, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_V, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_PLUS, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedWord<STENCIL_ALL, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxWord<true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_max_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    switch (params->stencil) {
    case STENCIL_H:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_H, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_V:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_V, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_PLUS:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_PLUS, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    case STENCIL_ALL:
        filter_plane_3x3<MinMaxFixedFloat<STENCIL_ALL, true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    default:
        filter_plane_3x3<MinMaxFloat<true>>(src, src_stride, dst, dst_stride, *params, width, height);
        break;
    }
}

void vs_generic_3x3_median_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianByte>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianWord>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianFloat>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateByte<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateWord<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateFloat<false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateByte<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateWord<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateFloat<true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionByte>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionWord>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionFloat>(src, src_stride, dst, dst_stride, *params, width, height);
}
