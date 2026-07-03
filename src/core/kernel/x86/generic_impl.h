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

/*
* Shared, ISA-neutral implementation of the 3x3 generic filters (Prewitt, Sobel,
* Minimum, Maximum, Median, Deflate, Inflate, 3x3 Convolution). Each per-ISA
* translation unit (generic_sse2/avx2/avx512.cpp) defines a `Backend` struct of
* SIMD primitives and includes this file to instantiate the full kernel set.
*
* These ops are "column vertical": every output pixel is a function of its own
* column neighbourhood only, so unpack/pack stay lane-local and the lane
* partition is preserved end to end -- the byte and float kernels port across
* vector widths unchanged. The one genuine SSE2-vs-AVX2 divergence is the 16-bit
* domain: SSE2 lacks unsigned 16-bit min/max so it works in a signed-biased
* domain (Backend::wsign / wmin / wmax / pack_word), while AVX2/AVX-512 use native
* epu16 ops (those Backend hooks become identity / native). Both are bit-exact
* with the C reference (kernel/generic.cpp) on integer formats; float is not
* bit-exact across tiers (FMA / reduction order), matching long-standing
* behaviour.
*/

#ifndef GENERIC_IMPL_H
#define GENERIC_IMPL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
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

constexpr uint8_t STENCIL_ALL = 0xFF;
constexpr uint8_t STENCIL_H = 0x18;
constexpr uint8_t STENCIL_V = 0x42;
constexpr uint8_t STENCIL_PLUS = STENCIL_H | STENCIL_V;

// Per-pixel-type memory traits, built from a Backend.
template <class B>
struct ByteTraits {
    typedef uint8_t T;
    typedef typename B::ivec vec_type;
    static constexpr unsigned vec_len = B::BYTE_LEN;
    static vec_type load(const uint8_t *p) { return B::byte_load(p); }
    static vec_type loadu(const uint8_t *p) { return B::byte_loadu(p); }
    static void store(uint8_t *p, vec_type x) { B::byte_store(p, x); }
    static vec_type shl_insert_lo(vec_type x, uint8_t y) { return B::byte_shl_insert_lo(x, y); }
    static vec_type shr_insert(vec_type x, uint8_t y, unsigned idx) { return B::byte_shr_insert(x, y, idx); }
};

template <class B>
struct WordTraits {
    typedef uint16_t T;
    typedef typename B::ivec vec_type;
    static constexpr unsigned vec_len = B::WORD_LEN;
    static vec_type load(const uint16_t *p) { return B::word_load(p); }
    static vec_type loadu(const uint16_t *p) { return B::word_loadu(p); }
    static void store(uint16_t *p, vec_type x) { B::word_store(p, x); }
    static vec_type shl_insert_lo(vec_type x, uint16_t y) { return B::word_shl_insert_lo(x, y); }
    static vec_type shr_insert(vec_type x, uint16_t y, unsigned idx) { return B::word_shr_insert(x, y, idx); }
};

template <class B>
struct FloatTraits {
    typedef float T;
    typedef typename B::fvec vec_type;
    static constexpr unsigned vec_len = B::FLOAT_LEN;
    static vec_type load(const float *p) { return B::float_load(p); }
    static vec_type loadu(const float *p) { return B::float_loadu(p); }
    static void store(float *p, vec_type x) { B::float_store(p, x); }
    static vec_type shl_insert_lo(vec_type x, float y) { return B::float_shl_insert_lo(x, y); }
    static vec_type shr_insert(vec_type x, float y, unsigned idx) { return B::float_shr_insert(x, y, idx); }
};

// MSVC 32-bit only allows up to 3 vector arguments to be passed by value.
#define OP_ARGS const vec_type &a00_, const vec_type &a01_, const vec_type &a02_, const vec_type &a10_, const vec_type &a11_, const vec_type &a12_, const vec_type &a20_, const vec_type &a21_, const vec_type &a22_
#define PROLOGUE() \
  auto a00 = a00_; auto a01 = a01_; auto a02 = a02_; \
  auto a10 = a10_; auto a11 = a11_; auto a12 = a12_; \
  auto a20 = a20_; auto a21 = a21_; auto a22 = a22_;


/* ---- Prewitt / Sobel --------------------------------------------------- */

struct PrewittSobelTraits {
    float scale;
    explicit PrewittSobelTraits(const vs_generic_params &params) : scale{ params.scale } {}
};

template <class B, bool Sobel>
struct PrewittSobelByte : PrewittSobelTraits, ByteTraits<B> {
    using PrewittSobelTraits::PrewittSobelTraits;
    typedef typename B::ivec vec_type;

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;
#define LO(x) (B::unpacklo8(x, B::zero_i()))
#define HI(x) (B::unpackhi8(x, B::zero_i()))
        auto gx_lo = B::sub16(LO(a22), LO(a00));
        auto gx_hi = B::sub16(HI(a22), HI(a00));
        auto gy_lo = gx_lo;
        auto gy_hi = gx_hi;

        gx_lo = B::add16(gx_lo, LO(a20));
        gx_lo = B::add16(gx_lo, Sobel ? B::slli16(LO(a21)) : LO(a21));
        gx_lo = B::sub16(gx_lo, Sobel ? B::slli16(LO(a01)) : LO(a01));
        gx_lo = B::sub16(gx_lo, LO(a02));
        gx_hi = B::add16(gx_hi, HI(a20));
        gx_hi = B::add16(gx_hi, Sobel ? B::slli16(HI(a21)) : HI(a21));
        gx_hi = B::sub16(gx_hi, Sobel ? B::slli16(HI(a01)) : HI(a01));
        gx_hi = B::sub16(gx_hi, HI(a02));

        gy_lo = B::add16(gy_lo, LO(a02));
        gy_lo = B::add16(gy_lo, Sobel ? B::slli16(LO(a12)) : LO(a12));
        gy_lo = B::sub16(gy_lo, Sobel ? B::slli16(LO(a10)) : LO(a10));
        gy_lo = B::sub16(gy_lo, LO(a20));
        gy_hi = B::add16(gy_hi, HI(a02));
        gy_hi = B::add16(gy_hi, Sobel ? B::slli16(HI(a12)) : HI(a12));
        gy_hi = B::sub16(gy_hi, Sobel ? B::slli16(HI(a10)) : HI(a10));
        gy_hi = B::sub16(gy_hi, HI(a20));

        auto gxy_lolo = B::unpacklo16(gx_lo, gy_lo);
        auto gxy_lohi = B::unpackhi16(gx_lo, gy_lo);
        auto gxy_hilo = B::unpacklo16(gx_hi, gy_hi);
        auto gxy_hihi = B::unpackhi16(gx_hi, gy_hi);
        gxy_lolo = B::madd16(gxy_lolo, gxy_lolo);
        gxy_lohi = B::madd16(gxy_lohi, gxy_lohi);
        gxy_hilo = B::madd16(gxy_hilo, gxy_hilo);
        gxy_hihi = B::madd16(gxy_hihi, gxy_hihi);

        auto sc = B::set1_f(scale);
        auto f_lolo = B::fmul(B::fsqrt(B::cvt_f(gxy_lolo)), sc);
        auto f_lohi = B::fmul(B::fsqrt(B::cvt_f(gxy_lohi)), sc);
        auto f_hilo = B::fmul(B::fsqrt(B::cvt_f(gxy_hilo)), sc);
        auto f_hihi = B::fmul(B::fsqrt(B::cvt_f(gxy_hihi)), sc);

        auto tmpi_lo = B::packs32(B::cvt_i(f_lolo), B::cvt_i(f_lohi));
        auto tmpi_hi = B::packs32(B::cvt_i(f_hilo), B::cvt_i(f_hihi));
        return B::packus16(tmpi_lo, tmpi_hi);
#undef HI
#undef LO
    }
};

template <class B, bool Sobel>
struct PrewittSobelWord : PrewittSobelTraits, WordTraits<B> {
    typename B::ivec maxval;

    explicit PrewittSobelWord(const vs_generic_params &params) :
        PrewittSobelTraits(params),
        maxval(B::wmaxval(params.maxval))
    {}

    typedef typename B::ivec vec_type;

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;
#define LO(x) (B::unpacklo16(x, B::zero_i()))
#define HI(x) (B::unpackhi16(x, B::zero_i()))
        auto gx_lo = B::sub32(LO(a22), LO(a00));
        auto gx_hi = B::sub32(HI(a22), HI(a00));
        auto gy_lo = gx_lo;
        auto gy_hi = gx_hi;

        gx_lo = B::add32(gx_lo, LO(a20));
        gx_lo = B::add32(gx_lo, Sobel ? B::slli32(LO(a21)) : LO(a21));
        gx_lo = B::sub32(gx_lo, Sobel ? B::slli32(LO(a01)) : LO(a01));
        gx_lo = B::sub32(gx_lo, LO(a02));
        gx_hi = B::add32(gx_hi, HI(a20));
        gx_hi = B::add32(gx_hi, Sobel ? B::slli32(HI(a21)) : HI(a21));
        gx_hi = B::sub32(gx_hi, Sobel ? B::slli32(HI(a01)) : HI(a01));
        gx_hi = B::sub32(gx_hi, HI(a02));

        gy_lo = B::add32(gy_lo, LO(a02));
        gy_lo = B::add32(gy_lo, Sobel ? B::slli32(LO(a12)) : LO(a12));
        gy_lo = B::sub32(gy_lo, Sobel ? B::slli32(LO(a10)) : LO(a10));
        gy_lo = B::sub32(gy_lo, LO(a20));
        gy_hi = B::add32(gy_hi, HI(a02));
        gy_hi = B::add32(gy_hi, Sobel ? B::slli32(HI(a12)) : HI(a12));
        gy_hi = B::sub32(gy_hi, Sobel ? B::slli32(HI(a10)) : HI(a10));
        gy_hi = B::sub32(gy_hi, HI(a20));

        auto gxsq_lo = B::cvt_f(gx_lo);
        auto gxsq_hi = B::cvt_f(gx_hi);
        auto gysq_lo = B::cvt_f(gy_lo);
        auto gysq_hi = B::cvt_f(gy_hi);
        gxsq_lo = B::fmul(gxsq_lo, gxsq_lo);
        gxsq_hi = B::fmul(gxsq_hi, gxsq_hi);
        gysq_lo = B::fmul(gysq_lo, gysq_lo);
        gysq_hi = B::fmul(gysq_hi, gysq_hi);

        auto sc = B::set1_f(scale);
        auto gxy_lo = B::fmul(B::fsqrt(B::fadd(gxsq_lo, gysq_lo)), sc);
        auto gxy_hi = B::fmul(B::fsqrt(B::fadd(gxsq_hi, gysq_hi)), sc);

        auto tmpi_lo = B::wsign32(B::cvt_i(gxy_lo));
        auto tmpi_hi = B::wsign32(B::cvt_i(gxy_hi));
        auto tmp = B::pack_word(tmpi_lo, tmpi_hi);
        tmp = B::wmin(tmp, maxval);
        return B::wunsign(tmp);
#undef HI
#undef LO
    }
};

template <class B, bool Sobel>
struct PrewittSobelFloat : PrewittSobelTraits, FloatTraits<B> {
    using PrewittSobelTraits::PrewittSobelTraits;
    typedef typename B::fvec vec_type;

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        (void)a11;
        auto two = B::set1_f(2.0f);
        auto gx = B::fsub(a22, a00);
        auto gy = gx;

        gx = B::fadd(gx, a20);
        gx = B::fadd(gx, Sobel ? B::fmul(a21, two) : a21);
        gx = B::fsub(gx, Sobel ? B::fmul(a01, two) : a01);
        gx = B::fsub(gx, a02);

        gy = B::fadd(gy, a02);
        gy = B::fadd(gy, Sobel ? B::fmul(a12, two) : a12);
        gy = B::fsub(gy, Sobel ? B::fmul(a10, two) : a10);
        gy = B::fsub(gy, a20);

        gx = B::fmul(gx, gx);
        gy = B::fmul(gy, gy);
        auto tmp = B::fsqrt(B::fadd(gx, gy));
        return B::fmul(tmp, B::set1_f(scale));
    }
};


/* ---- Minimum / Maximum (variable stencil) ------------------------------ */

template <class Derived, class vec_type>
struct MinMaxTraits {
    vec_type mask00, mask01, mask02, mask10, mask12, mask20, mask21, mask22;

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

template <class B, bool Max>
struct MinMaxByte : MinMaxTraits<MinMaxByte<B, Max>, typename B::ivec>, ByteTraits<B> {
    typedef typename B::ivec vec_type;
    typedef MinMaxTraits<MinMaxByte<B, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type enabled_mask() { return Max ? B::set1_i8(UINT8_MAX) : B::zero_i(); }
    static vec_type disabled_mask() { return Max ? B::zero_i() : B::set1_i8(UINT8_MAX); }
    static vec_type reduce(vec_type lhs, vec_type rhs, vec_type mask)
    {
        return Max ? B::max_u8(lhs, B::and_i(mask, rhs)) : B::min_u8(lhs, B::or_i(mask, rhs));
    }

    explicit MinMaxByte(const vs_generic_params &params) :
        MMT(params),
        threshold(B::set1_i8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        vec_type limit = Max ? B::adds_u8(a11, threshold) : B::subs_u8(a11, threshold);
        return Max ? B::min_u8(val, limit) : B::max_u8(val, limit);
    }
};

template <class B, bool Max>
struct MinMaxWord : MinMaxTraits<MinMaxWord<B, Max>, typename B::ivec>, WordTraits<B> {
    typedef typename B::ivec vec_type;
    typedef MinMaxTraits<MinMaxWord<B, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type enabled_mask() { return Max ? B::set1_i16(static_cast<int16_t>(UINT16_MAX)) : B::zero_i(); }
    static vec_type disabled_mask() { return Max ? B::zero_i() : B::set1_i16(static_cast<int16_t>(UINT16_MAX)); }
    static vec_type reduce(vec_type lhs, vec_type rhs, vec_type mask)
    {
        rhs = Max ? B::and_i(mask, rhs) : B::or_i(mask, rhs);
        rhs = B::wsign(rhs);
        return Max ? B::wmax(lhs, rhs) : B::wmin(lhs, rhs);
    }

    explicit MinMaxWord(const vs_generic_params &params) :
        MMT(params),
        threshold(B::set1_i16(static_cast<int16_t>(params.threshold)))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type a11s = B::wsign(a11);
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11s, a12, a20, a21, a22);
        vec_type limit = Max ? B::adds_u16(a11, threshold) : B::subs_u16(a11, threshold);
        limit = B::wsign(limit);
        val = Max ? B::wmin(val, limit) : B::wmax(val, limit);
        return B::wunsign(val);
    }
};

template <class B, bool Max>
struct MinMaxFloat : MinMaxTraits<MinMaxFloat<B, Max>, typename B::fvec>, FloatTraits<B> {
    typedef typename B::fvec vec_type;
    typedef MinMaxTraits<MinMaxFloat<B, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type enabled_mask() { return Max ? B::set1_f(INFINITY) : B::set1_f(-INFINITY); }
    static vec_type disabled_mask() { return Max ? B::set1_f(-INFINITY) : B::set1_f(INFINITY); }
    static vec_type reduce(vec_type lhs, vec_type rhs, vec_type mask)
    {
        // INFINITY is not a bit mask, so use min/max on rhs instead of and/or.
        return Max ? B::fmax(lhs, B::fmin(rhs, mask)) : B::fmin(lhs, B::fmax(rhs, mask));
    }

    explicit MinMaxFloat(const vs_generic_params &params) :
        MMT(params),
        threshold(B::set1_f(params.thresholdf))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        vec_type limit = Max ? B::fadd(a11, threshold) : B::fsub(a11, threshold);
        return Max ? B::fmin(val, limit) : B::fmax(val, limit);
    }
};


/* ---- Minimum / Maximum (fixed stencil) --------------------------------- */

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

template <class B, uint8_t Stencil, bool Max>
struct MinMaxFixedByte : MinMaxFixedTraits<Stencil, MinMaxFixedByte<B, Stencil, Max>, typename B::ivec>, ByteTraits<B> {
    typedef typename B::ivec vec_type;
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedByte<B, Stencil, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type reduce(vec_type lhs, vec_type rhs) { return Max ? B::max_u8(lhs, rhs) : B::min_u8(lhs, rhs); }

    explicit MinMaxFixedByte(const vs_generic_params &params) :
        threshold(B::set1_i8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        vec_type limit = Max ? B::adds_u8(a11, threshold) : B::subs_u8(a11, threshold);
        return Max ? B::min_u8(val, limit) : B::max_u8(val, limit);
    }
};

template <class B, uint8_t Stencil, bool Max>
struct MinMaxFixedWord : MinMaxFixedTraits<Stencil, MinMaxFixedWord<B, Stencil, Max>, typename B::ivec>, WordTraits<B> {
    typedef typename B::ivec vec_type;
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedWord<B, Stencil, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type reduce(vec_type lhs, vec_type rhs)
    {
        rhs = B::wsign(rhs);
        return Max ? B::wmax(lhs, rhs) : B::wmin(lhs, rhs);
    }

    explicit MinMaxFixedWord(const vs_generic_params &params) :
        threshold(B::set1_i16(static_cast<int16_t>(params.threshold)))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type a11s = B::wsign(a11);
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11s, a12, a20, a21, a22);
        vec_type limit = Max ? B::adds_u16(a11, threshold) : B::subs_u16(a11, threshold);
        limit = B::wsign(limit);
        val = Max ? B::wmin(val, limit) : B::wmax(val, limit);
        return B::wunsign(val);
    }
};

template <class B, uint8_t Stencil, bool Max>
struct MinMaxFixedFloat : MinMaxFixedTraits<Stencil, MinMaxFixedFloat<B, Stencil, Max>, typename B::fvec>, FloatTraits<B> {
    typedef typename B::fvec vec_type;
    typedef MinMaxFixedTraits<Stencil, MinMaxFixedFloat<B, Stencil, Max>, vec_type> MMT;
    vec_type threshold;

    static vec_type reduce(vec_type lhs, vec_type rhs) { return Max ? B::fmax(lhs, rhs) : B::fmin(lhs, rhs); }

    explicit MinMaxFixedFloat(const vs_generic_params &params) : threshold(B::set1_f(params.thresholdf)) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        vec_type val = MMT::apply_stencil(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        vec_type limit = Max ? B::fadd(a11, threshold) : B::fsub(a11, threshold);
        return Max ? B::fmin(val, limit) : B::fmax(val, limit);
    }
};


/* ---- Median ------------------------------------------------------------ */

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

template <class B>
struct MedianByte : MedianTraits<MedianByte<B>, typename B::ivec>, ByteTraits<B> {
    typedef typename B::ivec vec_type;
    static vec_type min(vec_type a, vec_type b) { return B::min_u8(a, b); }
    static vec_type max(vec_type a, vec_type b) { return B::max_u8(a, b); }
    static FORCE_INLINE void compare_exchange(vec_type &lhs, vec_type &rhs)
    {
        vec_type a = lhs, b = rhs;
        lhs = B::min_u8(a, b);
        rhs = B::max_u8(a, b);
    }
    explicit MedianByte(const vs_generic_params &) {}
};

template <class B>
struct MedianWord : MedianTraits<MedianWord<B>, typename B::ivec>, WordTraits<B> {
    typedef typename B::ivec vec_type;
    static vec_type min(vec_type a, vec_type b) { return B::wmin(a, b); }
    static vec_type max(vec_type a, vec_type b) { return B::wmax(a, b); }
    static FORCE_INLINE void compare_exchange(vec_type &lhs, vec_type &rhs)
    {
        vec_type a = lhs, b = rhs;
        lhs = B::wmin(a, b);
        rhs = B::wmax(a, b);
    }
    explicit MedianWord(const vs_generic_params &) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        a00 = B::wsign(a00); a01 = B::wsign(a01); a02 = B::wsign(a02);
        a10 = B::wsign(a10); a11 = B::wsign(a11); a12 = B::wsign(a12);
        a20 = B::wsign(a20); a21 = B::wsign(a21); a22 = B::wsign(a22);
        vec_type val = MedianTraits<MedianWord<B>, vec_type>::op(a00, a01, a02, a10, a11, a12, a20, a21, a22);
        return B::wunsign(val);
    }
};

template <class B>
struct MedianFloat : MedianTraits<MedianFloat<B>, typename B::fvec>, FloatTraits<B> {
    typedef typename B::fvec vec_type;
    static vec_type min(vec_type a, vec_type b) { return B::fmin(a, b); }
    static vec_type max(vec_type a, vec_type b) { return B::fmax(a, b); }
    static FORCE_INLINE void compare_exchange(vec_type &lhs, vec_type &rhs)
    {
        vec_type a = lhs, b = rhs;
        lhs = B::fmin(a, b);
        rhs = B::fmax(a, b);
    }
    explicit MedianFloat(const vs_generic_params &) {}
};


/* ---- Deflate / Inflate ------------------------------------------------- */

template <class B, bool Inflate>
struct DeflateInflateByte : ByteTraits<B> {
    typedef typename B::ivec vec_type;
    vec_type threshold;
    explicit DeflateInflateByte(const vs_generic_params &params) :
        threshold(B::set1_i8(static_cast<uint8_t>(std::min(params.threshold, static_cast<uint16_t>(UINT8_MAX)))))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
#define LO(x) (B::unpacklo8(x, B::zero_i()))
#define HI(x) (B::unpackhi8(x, B::zero_i()))
        auto accum_lo = LO(a00);
        auto accum_hi = HI(a00);
        accum_lo = B::add16(accum_lo, LO(a01)); accum_hi = B::add16(accum_hi, HI(a01));
        accum_lo = B::add16(accum_lo, LO(a02)); accum_hi = B::add16(accum_hi, HI(a02));
        accum_lo = B::add16(accum_lo, LO(a10)); accum_hi = B::add16(accum_hi, HI(a10));
        accum_lo = B::add16(accum_lo, LO(a12)); accum_hi = B::add16(accum_hi, HI(a12));
        accum_lo = B::add16(accum_lo, LO(a20)); accum_hi = B::add16(accum_hi, HI(a20));
        accum_lo = B::add16(accum_lo, LO(a21)); accum_hi = B::add16(accum_hi, HI(a21));
        accum_lo = B::add16(accum_lo, LO(a22)); accum_hi = B::add16(accum_hi, HI(a22));
        accum_lo = B::add16(accum_lo, B::set1_i16(4)); accum_hi = B::add16(accum_hi, B::set1_i16(4));
        accum_lo = B::srli16_3(accum_lo); accum_hi = B::srli16_3(accum_hi);

        auto tmp = B::packus16(accum_lo, accum_hi);
        tmp = Inflate ? B::max_u8(tmp, a11) : B::min_u8(tmp, a11);
        auto limit = Inflate ? B::adds_u8(a11, threshold) : B::subs_u8(a11, threshold);
        tmp = Inflate ? B::min_u8(tmp, limit) : B::max_u8(tmp, limit);
        return tmp;
#undef HI
#undef LO
    }
};

template <class B, bool Inflate>
struct DeflateInflateWord : WordTraits<B> {
    typedef typename B::ivec vec_type;
    vec_type threshold;
    explicit DeflateInflateWord(const vs_generic_params &params) : threshold(B::set1_i16(static_cast<int16_t>(params.threshold))) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
#define LO(x) (B::unpacklo16(x, B::zero_i()))
#define HI(x) (B::unpackhi16(x, B::zero_i()))
        auto accum_lo = LO(a00);
        auto accum_hi = HI(a00);
        accum_lo = B::add32(accum_lo, LO(a01)); accum_hi = B::add32(accum_hi, HI(a01));
        accum_lo = B::add32(accum_lo, LO(a02)); accum_hi = B::add32(accum_hi, HI(a02));
        accum_lo = B::add32(accum_lo, LO(a10)); accum_hi = B::add32(accum_hi, HI(a10));
        accum_lo = B::add32(accum_lo, LO(a12)); accum_hi = B::add32(accum_hi, HI(a12));
        accum_lo = B::add32(accum_lo, LO(a20)); accum_hi = B::add32(accum_hi, HI(a20));
        accum_lo = B::add32(accum_lo, LO(a21)); accum_hi = B::add32(accum_hi, HI(a21));
        accum_lo = B::add32(accum_lo, LO(a22)); accum_hi = B::add32(accum_hi, HI(a22));
        accum_lo = B::add32(accum_lo, B::set1_i32(4)); accum_hi = B::add32(accum_hi, B::set1_i32(4));
        accum_lo = B::srli32_3(accum_lo); accum_hi = B::srli32_3(accum_hi);
        accum_lo = B::wsign32(accum_lo); accum_hi = B::wsign32(accum_hi);

        auto tmp = B::pack_word(accum_lo, accum_hi);
        auto a11s = B::wsign(a11);
        tmp = Inflate ? B::wmax(tmp, a11s) : B::wmin(tmp, a11s);
        auto limit = Inflate ? B::adds_u16(a11, threshold) : B::subs_u16(a11, threshold);
        limit = B::wsign(limit);
        tmp = Inflate ? B::wmin(tmp, limit) : B::wmax(tmp, limit);
        return B::wunsign(tmp);
#undef HI
#undef LO
    }
};

template <class B, bool Inflate>
struct DeflateInflateFloat : FloatTraits<B> {
    typedef typename B::fvec vec_type;
    vec_type threshold;
    explicit DeflateInflateFloat(const vs_generic_params &params) : threshold(B::set1_f(params.thresholdf)) {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        auto accum0 = B::fadd(a00, a01);
        auto accum1 = B::fadd(a02, a10);
        accum0 = B::fadd(accum0, a12);
        accum1 = B::fadd(accum1, a20);
        accum0 = B::fadd(accum0, a21);
        accum1 = B::fadd(accum1, a22);
        auto tmp = B::fadd(accum0, accum1);
        tmp = B::fmul(tmp, B::set1_f(1.0f / 8.0f));
        tmp = Inflate ? B::fmax(tmp, a11) : B::fmin(tmp, a11);
        auto limit = Inflate ? B::fadd(a11, threshold) : B::fsub(a11, threshold);
        tmp = Inflate ? B::fmin(tmp, limit) : B::fmax(tmp, limit);
        return tmp;
    }
};


/* ---- 3x3 Convolution --------------------------------------------------- */

template <class B>
struct ConvolutionTraits {
    typename B::fvec div, bias, saturate_mask;
    explicit ConvolutionTraits(const vs_generic_params &params) :
        div(B::set1_f(params.div)),
        bias(B::set1_f(params.bias)),
        saturate_mask(B::satmask(params.saturate))
    {}
};

template <class B>
struct ConvolutionIntTraits : ConvolutionTraits<B> {
    typename B::ivec c00_01, c02_10, c11_12, c20_21, c22_xx;
    static uint32_t interleave(int16_t a, int16_t b) { return (static_cast<uint32_t>(b) << 16) | static_cast<uint16_t>(a); }
    explicit ConvolutionIntTraits(const vs_generic_params &params) :
        ConvolutionTraits<B>(params),
        c00_01(B::set1_i32(interleave(params.matrix[0], params.matrix[1]))),
        c02_10(B::set1_i32(interleave(params.matrix[2], params.matrix[3]))),
        c11_12(B::set1_i32(interleave(params.matrix[4], params.matrix[5]))),
        c20_21(B::set1_i32(interleave(params.matrix[6], params.matrix[7]))),
        c22_xx(B::set1_i32(interleave(params.matrix[8], 0)))
    {}
};

template <class B>
struct ConvolutionByte : ConvolutionIntTraits<B>, ByteTraits<B> {
    using ConvolutionIntTraits<B>::ConvolutionIntTraits;
    typedef typename B::ivec vec_type;
    using ConvolutionIntTraits<B>::c00_01;
    using ConvolutionIntTraits<B>::c02_10;
    using ConvolutionIntTraits<B>::c11_12;
    using ConvolutionIntTraits<B>::c20_21;
    using ConvolutionIntTraits<B>::c22_xx;
    using ConvolutionTraits<B>::div;
    using ConvolutionTraits<B>::bias;
    using ConvolutionTraits<B>::saturate_mask;

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
#define LO(x) (B::unpacklo8(x, B::zero_i()))
#define HI(x) (B::unpackhi8(x, B::zero_i()))
        vec_type accum_lolo, accum_lohi, accum_hilo, accum_hihi;
        vec_type t0_lo, t0_hi, t1_lo, t1_hi;

        t0_lo = LO(a00); t0_hi = HI(a00); t1_lo = LO(a01); t1_hi = HI(a01);
        accum_lolo = B::madd16(c00_01, B::unpacklo16(t0_lo, t1_lo));
        accum_lohi = B::madd16(c00_01, B::unpackhi16(t0_lo, t1_lo));
        accum_hilo = B::madd16(c00_01, B::unpacklo16(t0_hi, t1_hi));
        accum_hihi = B::madd16(c00_01, B::unpackhi16(t0_hi, t1_hi));

        t0_lo = LO(a02); t0_hi = HI(a02); t1_lo = LO(a10); t1_hi = HI(a10);
        accum_lolo = B::add32(accum_lolo, B::madd16(c02_10, B::unpacklo16(t0_lo, t1_lo)));
        accum_lohi = B::add32(accum_lohi, B::madd16(c02_10, B::unpackhi16(t0_lo, t1_lo)));
        accum_hilo = B::add32(accum_hilo, B::madd16(c02_10, B::unpacklo16(t0_hi, t1_hi)));
        accum_hihi = B::add32(accum_hihi, B::madd16(c02_10, B::unpackhi16(t0_hi, t1_hi)));

        t0_lo = LO(a11); t0_hi = HI(a11); t1_lo = LO(a12); t1_hi = HI(a12);
        accum_lolo = B::add32(accum_lolo, B::madd16(c11_12, B::unpacklo16(t0_lo, t1_lo)));
        accum_lohi = B::add32(accum_lohi, B::madd16(c11_12, B::unpackhi16(t0_lo, t1_lo)));
        accum_hilo = B::add32(accum_hilo, B::madd16(c11_12, B::unpacklo16(t0_hi, t1_hi)));
        accum_hihi = B::add32(accum_hihi, B::madd16(c11_12, B::unpackhi16(t0_hi, t1_hi)));

        t0_lo = LO(a20); t0_hi = HI(a20); t1_lo = LO(a21); t1_hi = HI(a21);
        accum_lolo = B::add32(accum_lolo, B::madd16(c20_21, B::unpacklo16(t0_lo, t1_lo)));
        accum_lohi = B::add32(accum_lohi, B::madd16(c20_21, B::unpackhi16(t0_lo, t1_lo)));
        accum_hilo = B::add32(accum_hilo, B::madd16(c20_21, B::unpacklo16(t0_hi, t1_hi)));
        accum_hihi = B::add32(accum_hihi, B::madd16(c20_21, B::unpackhi16(t0_hi, t1_hi)));

        t0_lo = LO(a22); t0_hi = HI(a22);
        accum_lolo = B::add32(accum_lolo, B::madd16(c22_xx, B::unpacklo16(t0_lo, B::zero_i())));
        accum_lohi = B::add32(accum_lohi, B::madd16(c22_xx, B::unpackhi16(t0_lo, B::zero_i())));
        accum_hilo = B::add32(accum_hilo, B::madd16(c22_xx, B::unpacklo16(t0_hi, B::zero_i())));
        accum_hihi = B::add32(accum_hihi, B::madd16(c22_xx, B::unpackhi16(t0_hi, B::zero_i())));

        auto f_lolo = B::fand(B::fadd(B::fmul(B::cvt_f(accum_lolo), div), bias), saturate_mask);
        auto f_lohi = B::fand(B::fadd(B::fmul(B::cvt_f(accum_lohi), div), bias), saturate_mask);
        auto f_hilo = B::fand(B::fadd(B::fmul(B::cvt_f(accum_hilo), div), bias), saturate_mask);
        auto f_hihi = B::fand(B::fadd(B::fmul(B::cvt_f(accum_hihi), div), bias), saturate_mask);

        accum_lolo = B::packs32(B::cvt_i(f_lolo), B::cvt_i(f_lohi));
        accum_hilo = B::packs32(B::cvt_i(f_hilo), B::cvt_i(f_hihi));
        return B::packus16(accum_lolo, accum_hilo);
#undef HI
#undef LO
    }
};

template <class B>
struct ConvolutionWord : ConvolutionIntTraits<B>, WordTraits<B> {
    typedef typename B::ivec vec_type;
    using ConvolutionIntTraits<B>::c00_01;
    using ConvolutionIntTraits<B>::c02_10;
    using ConvolutionIntTraits<B>::c11_12;
    using ConvolutionIntTraits<B>::c20_21;
    using ConvolutionIntTraits<B>::c22_xx;
    using ConvolutionTraits<B>::div;
    using ConvolutionTraits<B>::bias;
    using ConvolutionTraits<B>::saturate_mask;
    vec_type maxval;

    explicit ConvolutionWord(const vs_generic_params &params) :
        ConvolutionIntTraits<B>(params),
        maxval(B::wmaxval(params.maxval))
    {
        int32_t x = 0;
        for (unsigned i = 0; i < 9; ++i)
            x += params.matrix[i];
        // Use the 10th weight to subtract the bias "INT16_MIN * sum(matrix)".
        c22_xx = B::set1_i32(ConvolutionIntTraits<B>::interleave(params.matrix[8], static_cast<int16_t>(-x)));
    }

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        auto bias16 = B::set1_i16(INT16_MIN);
        a00 = B::add16(a00, bias16); a01 = B::add16(a01, bias16); a02 = B::add16(a02, bias16);
        a10 = B::add16(a10, bias16); a11 = B::add16(a11, bias16); a12 = B::add16(a12, bias16);
        a20 = B::add16(a20, bias16); a21 = B::add16(a21, bias16); a22 = B::add16(a22, bias16);

        auto accum_lo = B::madd16(c00_01, B::unpacklo16(a00, a01));
        auto accum_hi = B::madd16(c00_01, B::unpackhi16(a00, a01));
        accum_lo = B::add32(accum_lo, B::madd16(c02_10, B::unpacklo16(a02, a10)));
        accum_hi = B::add32(accum_hi, B::madd16(c02_10, B::unpackhi16(a02, a10)));
        accum_lo = B::add32(accum_lo, B::madd16(c11_12, B::unpacklo16(a11, a12)));
        accum_hi = B::add32(accum_hi, B::madd16(c11_12, B::unpackhi16(a11, a12)));
        accum_lo = B::add32(accum_lo, B::madd16(c20_21, B::unpacklo16(a20, a21)));
        accum_hi = B::add32(accum_hi, B::madd16(c20_21, B::unpackhi16(a20, a21)));
        accum_lo = B::add32(accum_lo, B::madd16(c22_xx, B::unpacklo16(a22, bias16)));
        accum_hi = B::add32(accum_hi, B::madd16(c22_xx, B::unpackhi16(a22, bias16)));

        auto f_lo = B::fand(B::fadd(B::fmul(B::cvt_f(accum_lo), div), bias), saturate_mask);
        auto f_hi = B::fand(B::fadd(B::fmul(B::cvt_f(accum_hi), div), bias), saturate_mask);
        accum_lo = B::wsign32(B::cvt_i(f_lo));
        accum_hi = B::wsign32(B::cvt_i(f_hi));

        auto tmp = B::pack_word(accum_lo, accum_hi);
        tmp = B::wmin(tmp, maxval);
        return B::wunsign(tmp);
    }
};

template <class B>
struct ConvolutionFloat : ConvolutionTraits<B>, FloatTraits<B> {
    typedef typename B::fvec vec_type;
    using ConvolutionTraits<B>::bias;
    using ConvolutionTraits<B>::saturate_mask;
    vec_type c00, c01, c02, c10, c11, c12, c20, c21, c22;

    explicit ConvolutionFloat(const vs_generic_params &params) :
        ConvolutionTraits<B>(params),
        c00(B::set1_f(params.matrixf[0] * params.div)),
        c01(B::set1_f(params.matrixf[1] * params.div)),
        c02(B::set1_f(params.matrixf[2] * params.div)),
        c10(B::set1_f(params.matrixf[3] * params.div)),
        c11(B::set1_f(params.matrixf[4] * params.div)),
        c12(B::set1_f(params.matrixf[5] * params.div)),
        c20(B::set1_f(params.matrixf[6] * params.div)),
        c21(B::set1_f(params.matrixf[7] * params.div)),
        c22(B::set1_f(params.matrixf[8] * params.div))
    {}

    FORCE_INLINE vec_type op(OP_ARGS)
    {
        PROLOGUE();
        auto accum0 = B::fmul(c00, a00);
        auto accum1 = B::fmul(c01, a01);
        accum0 = B::fmadd(c02, a02, accum0);
        accum1 = B::fmadd(c10, a10, accum1);
        accum0 = B::fmadd(c11, a11, accum0);
        accum1 = B::fmadd(c12, a12, accum1);
        accum0 = B::fmadd(c20, a20, accum0);
        accum1 = B::fmadd(c21, a21, accum1);
        accum0 = B::fmadd(c22, a22, accum0);
        accum1 = B::fadd(accum1, bias);
        auto tmp = B::fadd(accum0, accum1);
        return B::fand(tmp, saturate_mask);
    }
};

#undef PROLOGUE
#undef OP_ARGS


/* ---- driver ------------------------------------------------------------ */

template <class Traits>
void filter_plane_3x3(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename Traits::T T;
    typedef typename Traits::vec_type vec_type;

    Traits traits{ params };
    unsigned vec_end = (width - 1) & ~(Traits::vec_len - 1);

#define INVOKE(p0, p1, p2) (traits.op(Traits::loadu(p0 - 1), Traits::load(p0), Traits::loadu(p0 + 1), Traits::loadu(p1 - 1), Traits::load(p1), Traits::loadu(p1 + 1), Traits::loadu(p2 - 1), Traits::load(p2), Traits::loadu(p2 + 1)))
    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? 0 : i - 1;
        unsigned below_idx = i == height - 1 ? height - 1 : i + 1;

        const T *srcp0 = static_cast<const T *>(line_ptr(src, above_idx, src_stride));
        const T *srcp1 = static_cast<const T *>(line_ptr(src, i, src_stride));
        const T *srcp2 = static_cast<const T *>(line_ptr(src, below_idx, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        {
            vec_type a01 = Traits::load(srcp0);
            vec_type a11 = Traits::load(srcp1);
            vec_type a21 = Traits::load(srcp2);

            vec_type a00 = Traits::shl_insert_lo(a01, srcp0[0]);
            vec_type a10 = Traits::shl_insert_lo(a11, srcp1[0]);
            vec_type a20 = Traits::shl_insert_lo(a21, srcp2[0]);

            vec_type a02, a12, a22;
            if (width > Traits::vec_len) {
                a02 = Traits::loadu(srcp0 + 1);
                a12 = Traits::loadu(srcp1 + 1);
                a22 = Traits::loadu(srcp2 + 1);
            } else {
                a02 = Traits::shr_insert(a01, srcp0[width - 1], width - 1);
                a12 = Traits::shr_insert(a11, srcp1[width - 1], width - 1);
                a22 = Traits::shr_insert(a21, srcp2[width - 1], width - 1);
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

            vec_type a02 = Traits::shr_insert(a01, srcp0[width - 1], width - vec_end - 1);
            vec_type a12 = Traits::shr_insert(a11, srcp1[width - 1], width - vec_end - 1);
            vec_type a22 = Traits::shr_insert(a21, srcp2[width - 1], width - vec_end - 1);

            vec_type val = traits.op(a00, a01, a02, a10, a11, a12, a20, a21, a22);
            Traits::store(dstp + vec_end, val);
        }
    }
#undef INVOKE
}


/* ---- half (float16) memory adapter ------------------------------------- */
/*
* Wraps any float op struct so the plane is loaded/stored as IEEE-754 binary16
* via F16C, while every arithmetic op stays in float32 (the inherited op() runs
* unchanged on B::fvec). Only instantiated in the AVX2/AVX-512 tiers, where F16C
* is guaranteed; SSE2/no-SIMD reject half at the filter level. Stores round to
* nearest-even to match the scalar half conversion.
*/
template <class FloatOp, class B>
struct HalfMem : FloatOp {
    using FloatOp::FloatOp;
    typedef _Float16 T;
    static typename B::fvec load(const _Float16 *p) { return B::half_load(p); }
    static typename B::fvec loadu(const _Float16 *p) { return B::half_loadu(p); }
    static void store(_Float16 *p, typename B::fvec x) { B::half_store(p, x); }
    static typename B::fvec shl_insert_lo(typename B::fvec x, _Float16 y) { return B::float_shl_insert_lo(x, static_cast<float>(y)); }
    static typename B::fvec shr_insert(typename B::fvec x, _Float16 y, unsigned idx) { return B::float_shr_insert(x, static_cast<float>(y), idx); }
};

} // namespace


/* Emit the 24 exported 3x3 entry points for one ISA tier. */
#define VS_GENERIC_3X3(kernel, TRAITS, SUFFIX) \
    void vs_generic_3x3_##kernel##_byte_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Byte<BACKEND>>(src, ss, dst, ds, *p, w, h); } \
    void vs_generic_3x3_##kernel##_word_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Word<BACKEND>>(src, ss, dst, ds, *p, w, h); } \
    void vs_generic_3x3_##kernel##_float_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Float<BACKEND>>(src, ss, dst, ds, *p, w, h); }

// Min/Max additionally specialise on the common fixed stencils.
#define VS_GENERIC_MINMAX(kernel, MAX, SUFFIX) \
    static void minmax_##kernel##_byte_dispatch(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { \
        switch (p->stencil) { \
        case STENCIL_H: filter_plane_3x3<MinMaxFixedByte<BACKEND, STENCIL_H, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_V: filter_plane_3x3<MinMaxFixedByte<BACKEND, STENCIL_V, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_PLUS: filter_plane_3x3<MinMaxFixedByte<BACKEND, STENCIL_PLUS, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_ALL: filter_plane_3x3<MinMaxFixedByte<BACKEND, STENCIL_ALL, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        default: filter_plane_3x3<MinMaxByte<BACKEND, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        } \
    } \
    static void minmax_##kernel##_word_dispatch(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { \
        switch (p->stencil) { \
        case STENCIL_H: filter_plane_3x3<MinMaxFixedWord<BACKEND, STENCIL_H, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_V: filter_plane_3x3<MinMaxFixedWord<BACKEND, STENCIL_V, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_PLUS: filter_plane_3x3<MinMaxFixedWord<BACKEND, STENCIL_PLUS, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_ALL: filter_plane_3x3<MinMaxFixedWord<BACKEND, STENCIL_ALL, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        default: filter_plane_3x3<MinMaxWord<BACKEND, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        } \
    } \
    static void minmax_##kernel##_float_dispatch(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { \
        switch (p->stencil) { \
        case STENCIL_H: filter_plane_3x3<MinMaxFixedFloat<BACKEND, STENCIL_H, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_V: filter_plane_3x3<MinMaxFixedFloat<BACKEND, STENCIL_V, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_PLUS: filter_plane_3x3<MinMaxFixedFloat<BACKEND, STENCIL_PLUS, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_ALL: filter_plane_3x3<MinMaxFixedFloat<BACKEND, STENCIL_ALL, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        default: filter_plane_3x3<MinMaxFloat<BACKEND, MAX>>(src, ss, dst, ds, *p, w, h); break; \
        } \
    } \
    void vs_generic_3x3_##kernel##_byte_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { minmax_##kernel##_byte_dispatch(src, ss, dst, ds, p, w, h); } \
    void vs_generic_3x3_##kernel##_word_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { minmax_##kernel##_word_dispatch(src, ss, dst, ds, p, w, h); } \
    void vs_generic_3x3_##kernel##_float_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { minmax_##kernel##_float_dispatch(src, ss, dst, ds, p, w, h); }

// PrewittSobel/DeflateInflate take a bool template parameter (the third arg to the struct).
#define VS_GENERIC_3X3_B(kernel, TRAITS, FLAG, SUFFIX) \
    void vs_generic_3x3_##kernel##_byte_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Byte<BACKEND, FLAG>>(src, ss, dst, ds, *p, w, h); } \
    void vs_generic_3x3_##kernel##_word_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Word<BACKEND, FLAG>>(src, ss, dst, ds, *p, w, h); } \
    void vs_generic_3x3_##kernel##_float_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<TRAITS##Float<BACKEND, FLAG>>(src, ss, dst, ds, *p, w, h); }

#define VS_GENERIC_ENTRYPOINTS(SUFFIX) \
    VS_GENERIC_3X3_B(prewitt, PrewittSobel, false, SUFFIX) \
    VS_GENERIC_3X3_B(sobel, PrewittSobel, true, SUFFIX) \
    VS_GENERIC_MINMAX(min, false, SUFFIX) \
    VS_GENERIC_MINMAX(max, true, SUFFIX) \
    VS_GENERIC_3X3(median, Median, SUFFIX) \
    VS_GENERIC_3X3_B(deflate, DeflateInflate, false, SUFFIX) \
    VS_GENERIC_3X3_B(inflate, DeflateInflate, true, SUFFIX) \
    VS_GENERIC_3X3(conv, Convolution, SUFFIX)

/* Half (float16) 3x3 entry points -- reuse the float ops through HalfMem, which
   loads/stores half and keeps arithmetic in float32. F16C tiers (avx2/avx512) only. */
#define VS_GENERIC_3X3_HALF(kernel, TRAITS, SUFFIX) \
    void vs_generic_3x3_##kernel##_half_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<HalfMem<TRAITS##Float<BACKEND>, BACKEND>>(src, ss, dst, ds, *p, w, h); }

#define VS_GENERIC_3X3_B_HALF(kernel, TRAITS, FLAG, SUFFIX) \
    void vs_generic_3x3_##kernel##_half_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { filter_plane_3x3<HalfMem<TRAITS##Float<BACKEND, FLAG>, BACKEND>>(src, ss, dst, ds, *p, w, h); }

#define VS_GENERIC_MINMAX_HALF(kernel, MAX, SUFFIX) \
    static void minmax_##kernel##_half_dispatch(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { \
        switch (p->stencil) { \
        case STENCIL_H: filter_plane_3x3<HalfMem<MinMaxFixedFloat<BACKEND, STENCIL_H, MAX>, BACKEND>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_V: filter_plane_3x3<HalfMem<MinMaxFixedFloat<BACKEND, STENCIL_V, MAX>, BACKEND>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_PLUS: filter_plane_3x3<HalfMem<MinMaxFixedFloat<BACKEND, STENCIL_PLUS, MAX>, BACKEND>>(src, ss, dst, ds, *p, w, h); break; \
        case STENCIL_ALL: filter_plane_3x3<HalfMem<MinMaxFixedFloat<BACKEND, STENCIL_ALL, MAX>, BACKEND>>(src, ss, dst, ds, *p, w, h); break; \
        default: filter_plane_3x3<HalfMem<MinMaxFloat<BACKEND, MAX>, BACKEND>>(src, ss, dst, ds, *p, w, h); break; \
        } \
    } \
    void vs_generic_3x3_##kernel##_half_##SUFFIX(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds, const struct vs_generic_params *p, unsigned w, unsigned h) \
    { minmax_##kernel##_half_dispatch(src, ss, dst, ds, p, w, h); }

#define VS_GENERIC_ENTRYPOINTS_HALF(SUFFIX) \
    VS_GENERIC_3X3_B_HALF(prewitt, PrewittSobel, false, SUFFIX) \
    VS_GENERIC_3X3_B_HALF(sobel, PrewittSobel, true, SUFFIX) \
    VS_GENERIC_MINMAX_HALF(min, false, SUFFIX) \
    VS_GENERIC_MINMAX_HALF(max, true, SUFFIX) \
    VS_GENERIC_3X3_HALF(median, Median, SUFFIX) \
    VS_GENERIC_3X3_B_HALF(deflate, DeflateInflate, false, SUFFIX) \
    VS_GENERIC_3X3_B_HALF(inflate, DeflateInflate, true, SUFFIX) \
    VS_GENERIC_3X3_HALF(conv, Convolution, SUFFIX)

#endif // GENERIC_IMPL_H
