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
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <VSHelper4.h>
#include "generic.h"
#include "../float16_helper.h"

namespace {

// Scalar float16 pixel: raw IEEE binary16 bits that widen to float32 on read
// (operator float) and are produced by the limit/xrint specialisations below on
// write. Two bytes wide, so arrays, strides and the separable temp buffer size
// exactly like uint16_t. Conversions go through float16_helper (bit fiddling
// when the target lacks F16C), so no _Float16 type is required -- this lets the
// byte/word/float convolution helpers serve half unchanged, just by
// instantiating them with this type. floatToHalf rounds to nearest-even.
struct half_t {
    uint16_t bits;
    operator float() const { return halfToFloat(bits); }
};

template <class T>
T limit(T x, uint16_t maxval)
{
    return static_cast<T>(std::min(static_cast<uint16_t>(x), maxval));
}

template <>
float limit(float x, uint16_t) { return x; }

template <>
half_t limit(half_t x, uint16_t) { return x; }

template <class T>
T xrint(float x)
{
    return static_cast<T>(std::lrint(std::min(std::max(x, static_cast<float>(std::numeric_limits<T>::min())), static_cast<float>(std::numeric_limits<T>::max()))));
}

template <>
float xrint(float x) { return x; }

template <>
half_t xrint(float x) { return half_t{ floatToHalf(x) }; }

template <class T>
T *line_ptr(T *ptr, unsigned i, ptrdiff_t stride)
{
    return (T *)(((unsigned char *)ptr) + static_cast<ptrdiff_t>(i) * stride);
}


template <class T, bool Sobel>
struct PrewittSobelOp {
    typedef T type;

    float scale;

    explicit PrewittSobelOp(const vs_generic_params &params) : scale{ params.scale } {}

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;
        constexpr Signed two = 2;

        Signed gx, gy;

        if (Sobel) {
            gx = static_cast<Signed>(a20) + two * a21 + a22 - a00 - two * a01 - a02;
            gy = static_cast<Signed>(a02) + two * a12 + a22 - a00 - two * a10 - a20;
        } else {
            gx = static_cast<Signed>(a20) + a21 + a22 - a00 - a01 - a02;
            gy = static_cast<Signed>(a02) + a12 + a22 - a00 - a10 - a20;
        }

        float tmp = std::sqrt(static_cast<float>(gx) * gx + static_cast<float>(gy) * gy) * scale;
        return xrint<T>(tmp);
    }
};

template <class T, bool Max>
struct MinMaxOp {
    typedef T type;

    typename std::conditional<std::is_integral<T>::value, int32_t, float>::type threshold;
    uint8_t stencil[8];

    explicit MinMaxOp(const vs_generic_params &params) :
        threshold{ std::is_integral<T>::value ? static_cast<T>(params.threshold) : static_cast<T>(params.thresholdf) },
        stencil{}
    {
        for (unsigned i = 0; i < 8; ++i) {
            stencil[i] = (params.stencil & (1U << i)) ? 0xFF : 0;
        }
    }

    static T reduce(T lhs, T rhs)
    {
        return Max ? std::max(lhs, rhs) : std::min(lhs, rhs);
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        T val = a11;
        val = reduce(val, stencil[0] ? a00 : val);
        val = reduce(val, stencil[1] ? a01 : val);
        val = reduce(val, stencil[2] ? a02 : val);
        val = reduce(val, stencil[3] ? a10 : val);
        val = reduce(val, stencil[4] ? a12 : val);
        val = reduce(val, stencil[5] ? a20 : val);
        val = reduce(val, stencil[6] ? a21 : val);
        val = reduce(val, stencil[7] ? a22 : val);

        Signed minval = std::is_integral<T>::value ? Signed{} : static_cast<Signed>(-INFINITY);
        Signed limit = Max ? static_cast<Signed>(a11) + threshold : std::max(static_cast<Signed>(a11) - threshold, minval);
        return Max ? std::min(static_cast<Signed>(val), limit) : std::max(static_cast<Signed>(val), limit);
    }
};

template <class T>
struct MedianOp {
    typedef T type;

    explicit MedianOp(const vs_generic_params &) {}

    static void compare_exchange(T &lhs, T &rhs)
    {
        T x = lhs;
        T y = rhs;
        lhs = std::min(x, y);
        rhs = std::max(x, y);
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        compare_exchange(a00, a01);
        compare_exchange(a02, a10);
        compare_exchange(a12, a20);
        compare_exchange(a21, a22);

        compare_exchange(a00, a02);
        compare_exchange(a01, a10);
        compare_exchange(a12, a21);
        compare_exchange(a20, a22);

        compare_exchange(a01, a02);
        compare_exchange(a20, a21);

        compare_exchange(a00, a12);
        compare_exchange(a01, a20);
        compare_exchange(a02, a21);
        compare_exchange(a10, a22);

        compare_exchange(a02, a12);
        compare_exchange(a10, a20);

        compare_exchange(a10, a12);

        compare_exchange(a10, a11);
        compare_exchange(a11, a12);
        return a11;
    }
};

template <class T, bool Inflate>
struct DeflateInflateOp {
    typedef T type;

    typename std::conditional<std::is_integral<T>::value, int32_t, float>::type threshold;

    explicit DeflateInflateOp(const vs_generic_params &params) :
        threshold{ std::is_integral<T>::value ? static_cast<T>(params.threshold) : static_cast<T>(params.thresholdf) }
    {}

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, uint32_t, float>::type U;
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        U accum = static_cast<U>(a00) + a01 + a02 + a10 + a12 + a20 + a21 + a22;
        accum = std::is_integral<T>::value ? accum + 4 : accum;

        Signed val = static_cast<Signed>(accum / 8);
        val = Inflate ? std::max(val, static_cast<Signed>(a11)) : std::min(val, static_cast<Signed>(a11));

        Signed minval = std::is_integral<T>::value ? Signed{} : static_cast<Signed>(-INFINITY);
        Signed limit = Inflate ? static_cast<Signed>(a11) + threshold : std::max(static_cast<Signed>(a11) - threshold, minval);
        return Inflate ? std::min(val, limit) : std::max(val, limit);
    }
};

template <class T>
struct ConvolutionOp {
    typedef T type;

    std::array<typename std::conditional<std::is_integral<T>::value, int16_t, float>::type, 9> coeffs;
    float div;
    float bias;
    uint8_t saturate;

    explicit ConvolutionOp(const vs_generic_params &params) :
        coeffs{},
        div{ params.div },
        bias{ params.bias },
        saturate{ params.saturate }
    {
        typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type coeff_type;

        for (unsigned i = 0; i < 9; ++i) {
            coeffs[i] = std::is_integral<T>::value ? static_cast<coeff_type>(params.matrix[i]) : static_cast<coeff_type>(params.matrixf[i]);
        }
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        Signed accum = coeffs[0] * a00;
        accum += coeffs[1] * a01;
        accum += coeffs[2] * a02;
        accum += coeffs[3] * a10;
        accum += coeffs[4] * a11;
        accum += coeffs[5] * a12;
        accum += coeffs[6] * a20;
        accum += coeffs[7] * a21;
        accum += coeffs[8] * a22;

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        return xrint<T>(tmp);
    }
};


// Scalar float16 fallback: store the plane as raw IEEE binary16 bits (uint16_t)
// and wrap any float op so each neighbourhood sample is widened to float32 for
// the arithmetic and the result narrowed back to half. Conversions go through
// float16_helper (pure bit manipulation when the target lacks F16C), so this
// path is compiler- and ISA-agnostic. floatToHalf rounds to nearest-even,
// matching the SIMD store. maxval is 65535 for half, so the driver's uint16_t
// limit() is an identity.
template <class FloatOp>
struct HalfOp {
    typedef uint16_t type;

    FloatOp inner;

    explicit HalfOp(const vs_generic_params &params) : inner{ params } {}

    uint16_t op(uint16_t a00, uint16_t a01, uint16_t a02, uint16_t a10, uint16_t a11, uint16_t a12, uint16_t a20, uint16_t a21, uint16_t a22) const
    {
        return floatToHalf(inner.op(
            halfToFloat(a00), halfToFloat(a01), halfToFloat(a02),
            halfToFloat(a10), halfToFloat(a11), halfToFloat(a12),
            halfToFloat(a20), halfToFloat(a21), halfToFloat(a22)));
    }
};


template <class Traits>
void filter_plane_3x3(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename Traits::type T;

    Traits traits{ params };
    uint16_t maxval = params.maxval;

    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? 0 : i - 1;
        unsigned below_idx = i == height - 1 ? height - 1 : i + 1;

        const T *srcp0 = static_cast<const T *>(line_ptr(src, above_idx, src_stride));
        const T *srcp1 = static_cast<const T *>(line_ptr(src, i, src_stride));
        const T *srcp2 = static_cast<const T *>(line_ptr(src, below_idx, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        {
            unsigned a = 0;
            unsigned b = 0;
            unsigned c = width > 1 ? 1 : 0;

            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[0] = limit(x, maxval);
        }

        for (unsigned i = 1; i < width - 1; ++i) {
            unsigned a = i - 1;
            unsigned b = i;
            unsigned c = i + 1;

            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[i] = limit(x, maxval);
        }

        if (width > 1) {
            unsigned a = width - 2;
            unsigned b = width - 1;
            unsigned c = width - 1;
            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[width - 1] = limit(x, maxval);
        }
    }
}

// Half-sample symmetric mirror of an out-of-range coordinate, matching the legacy
// 5x5 edge handling: a single reflection (-1 -> 0, len -> len-1) then a clamp, so a
// filter wider than the plane still stays in bounds.
inline unsigned mirror_index(int pos, int len)
{
    if (pos < 0)
        pos = -pos - 1;
    else if (pos >= len)
        pos = 2 * len - 1 - pos;
    if (pos < 0)
        pos = 0;
    else if (pos >= len)
        pos = len - 1;
    return static_cast<unsigned>(pos);
}

// Square (non-separable) NxN convolution, N odd. Plain scalar so the existing per-format
// C dispatch reaches it, but written to auto-vectorise: __restrict row and destination
// pointers prove the store cannot alias the loads, and N is a compile-time constant. Only
// the first/last N/2 columns take the mirrored-index slow path; the interior is the hot
// loop. Small kernels (N<=7) vectorise a per-pixel reduction with the N*N taps unrolled;
// larger kernels exceed the unroller's budget and would fall back to scalar, so their
// interior is register-blocked (see below) to keep the inner loop vectorised. Every path
// accumulates in the same (tap-column outer, row inner) order, so 5x5 results are bit-exact
// with the historic kernel (integer) and unchanged (float). Integer accumulation is int32,
// widened to int64 for 16-bit input at N>5 where the worst case (N*N * 1023 * 65535)
// exceeds int32.
template <class T, unsigned N>
void conv_plane_square(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename std::conditional<std::is_integral<T>::value,
        typename std::conditional<(sizeof(T) > 1 && N > 5), int64_t, int32_t>::type,
        float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;
    constexpr unsigned S = N / 2;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    const unsigned edge = std::min(width, S);

    for (unsigned i = 0; i < height; ++i) {
        const T *__restrict rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = static_cast<const T *>(line_ptr(src, mirror_index(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(height)), src_stride));
        T *__restrict dst_p = static_cast<T *>(line_ptr(dst, i, dst_stride));

        // Left edge columns (mirrored horizontal indexing).
        for (unsigned j = 0; j < edge; ++j) {
            Accum accum = 0;
            for (unsigned k = 0; k < N; ++k) {
                unsigned col = mirror_index(static_cast<int>(j) + static_cast<int>(k) - static_cast<int>(S), static_cast<int>(width));
                for (unsigned r = 0; r < N; ++r)
                    accum += coeffs[r * N + k] * static_cast<Accum>(rows[r][col]);
            }
            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dst_p[j] = limit(xrint<T>(tmp), maxval);
        }

        // Interior columns (no bounds logic). Both paths accumulate in the same
        // (tap-column outer, row inner) order, so results are bit-exact with the historic
        // 5x5 kernel regardless of which is chosen.
        if constexpr (N <= 7) {
            // Small kernels: the compiler fully unrolls the N*N taps and vectorises the
            // per-pixel reduction across columns directly.
            for (unsigned j = S; j + S < width; ++j) {
                Accum accum = 0;
                for (unsigned k = 0; k < N; ++k)
                    for (unsigned r = 0; r < N; ++r)
                        accum += coeffs[r * N + k] * static_cast<Accum>(rows[r][j - S + k]);
                float tmp = static_cast<float>(accum) * div + bias;
                tmp = saturate ? tmp : std::fabs(tmp);
                dst_p[j] = limit(xrint<T>(tmp), maxval);
            }
        } else if (width > 2 * S) {
            // Large kernels: N*N exceeds the unroller's budget, so a plain per-pixel
            // reduction stays scalar. Process the interior in register-blocked strips of
            // BLK instead -- acc[] lives in vector registers across the tap loop and the
            // innermost loop is a contiguous multiply-add the compiler always vectorises.
            const unsigned interior_end = width - S;
            constexpr unsigned BLK = 16;
            unsigned j = S;
            for (; j + BLK <= interior_end; j += BLK) {
                Accum acc[BLK];
                for (unsigned t = 0; t < BLK; ++t)
                    acc[t] = 0;
                for (unsigned k = 0; k < N; ++k)
                    for (unsigned r = 0; r < N; ++r) {
                        Weight w = coeffs[r * N + k];
                        const T *__restrict row = rows[r] + (j - S + k);
                        for (unsigned t = 0; t < BLK; ++t)
                            acc[t] += w * static_cast<Accum>(row[t]);
                    }
                for (unsigned t = 0; t < BLK; ++t) {
                    float tmp = static_cast<float>(acc[t]) * div + bias;
                    tmp = saturate ? tmp : std::fabs(tmp);
                    dst_p[j + t] = limit(xrint<T>(tmp), maxval);
                }
            }
            for (; j < interior_end; ++j) {
                Accum accum = 0;
                for (unsigned k = 0; k < N; ++k)
                    for (unsigned r = 0; r < N; ++r)
                        accum += coeffs[r * N + k] * static_cast<Accum>(rows[r][j - S + k]);
                float tmp = static_cast<float>(accum) * div + bias;
                tmp = saturate ? tmp : std::fabs(tmp);
                dst_p[j] = limit(xrint<T>(tmp), maxval);
            }
        }

        // Right edge columns (mirrored horizontal indexing).
        for (unsigned j = std::max(S, width - edge); j < width; ++j) {
            Accum accum = 0;
            for (unsigned k = 0; k < N; ++k) {
                unsigned col = mirror_index(static_cast<int>(j) + static_cast<int>(k) - static_cast<int>(S), static_cast<int>(width));
                for (unsigned r = 0; r < N; ++r)
                    accum += coeffs[r * N + k] * static_cast<Accum>(rows[r][col]);
            }
            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dst_p[j] = limit(xrint<T>(tmp), maxval);
        }
    }
}

template <class T>
void conv_scanline_h(const void *src, void *dst, const vs_generic_params &params, unsigned width)
{
    typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    const T *srcp = static_cast<const T *>(src);
    T *dstp = static_cast<T *>(dst);

    for (unsigned j = 0; j < std::min(width, support); ++j) {
        unsigned dist_from_right = width - 1 - j;

        Accum accum = 0;

        for (unsigned k = 0; k < support; ++k) {
            unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
            accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
            accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
        }

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        dstp[j] = limit(xrint<T>(tmp), maxval);
    }

    for (unsigned j = support; j < width - std::min(width, support); ++j) {
        Accum accum = 0;

        for (unsigned k = 0; k < fwidth; ++k) {
            accum += coeffs[k] * static_cast<Accum>(srcp[j - support + k]);
        }

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        dstp[j] = limit(xrint<T>(tmp), maxval);
    }

    for (unsigned j = std::max(support, width - std::min(width, support)); j < width; ++j) {
        unsigned dist_from_right = width - 1 - j;

        Accum accum = 0;

        for (unsigned k = 0; k < support; ++k) {
            unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
            accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
            accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
        }

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        dstp[j] = limit(xrint<T>(tmp), maxval);
    }
}

template <class T>
void conv_scanline_v(const void * const src[], void *dst, const vs_generic_params &params, unsigned width)
{
    typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    unsigned fwidth = params.matrixsize;

    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    T *dstp = static_cast<T *>(dst);

    for (unsigned j = 0; j < width; ++j) {
        Accum accum = 0;

        for (unsigned k = 0; k < fwidth; ++k) {
            accum += coeffs[k] * static_cast<Accum>(static_cast<const T *>(src[k])[j]);
        }

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        dstp[j] = limit(xrint<T>(tmp), maxval);
    }
}

template <class T>
void conv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    for (unsigned i = 0; i < height; ++i) {
        const void *srcp = line_ptr(src, i, src_stride);
        void *dstp = line_ptr(dst, i, dst_stride);

        conv_scanline_h<T>(srcp, dstp, params, width);
    }
}

template <class T>
void conv_plane_v(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }

        conv_scanline_v<T>(srcp, dstp, params, width);
    }
}

template <class T>
void conv_plane_x(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    void *tmp = vsh::vsh_aligned_malloc(width * sizeof(T), 64);
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }

        conv_scanline_v<T>(srcp, tmp, params, width);
        conv_scanline_h<T>(tmp, dstp, params, width);
    }

    vsh::vsh_aligned_free(tmp);
}

} // namespace


void vs_generic_3x3_prewitt_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<uint8_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<uint16_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<float>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<uint8_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<uint16_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<float>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<PrewittSobelOp<float, false>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<PrewittSobelOp<float, true>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<MinMaxOp<float, false>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<MinMaxOp<float, true>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<MedianOp<float>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<DeflateInflateOp<float, false>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<DeflateInflateOp<float, true>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<HalfOp<ConvolutionOp<float>>>(src, src_stride, dst, dst_stride, *params, width, height);
}

// Square (mode 's') convolution entry points: one plain-C function per (size, pixel).
// N is a compile-time template argument so conv_plane_square auto-vectorises per size.
#define VS_SQUARE_CONV_ENTRY(SIZE, N) \
    void vs_generic_##SIZE##_conv_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_square<uint8_t, N>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_##SIZE##_conv_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_square<uint16_t, N>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_##SIZE##_conv_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_square<float, N>(src, src_stride, dst, dst_stride, *params, width, height); } \
    void vs_generic_##SIZE##_conv_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { conv_plane_square<half_t, N>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SQUARE_CONV_ENTRY(5x5, 5)
VS_SQUARE_CONV_ENTRY(7x7, 7)
VS_SQUARE_CONV_ENTRY(9x9, 9)
VS_SQUARE_CONV_ENTRY(11x11, 11)

#undef VS_SQUARE_CONV_ENTRY

void vs_generic_1d_conv_h_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<half_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<half_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_half_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<half_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

