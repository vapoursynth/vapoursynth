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

/*
* Scalar helpers shared by the AArch64 kernel TUs. No SIMD includes here.
* These replicate kernel/generic.cpp expressions exactly, so the SIMD
* kernels' scalar edge columns match the C tier.
*/

#ifndef ARM_CONV_SCALAR_H
#define ARM_CONV_SCALAR_H

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <type_traits>

namespace {

inline unsigned nc_mirror(int pos, int len)
{
    if (pos < 0) pos = -pos - 1;
    else if (pos >= len) pos = 2 * len - 1 - pos;
    if (pos < 0) pos = 0;
    else if (pos >= len) pos = len - 1;
    return static_cast<unsigned>(pos);
}

inline float nc_half_to_float(uint16_t bits)
{
    __fp16 h;
    __builtin_memcpy(&h, &bits, sizeof(h));
    return static_cast<float>(h);
}

inline uint16_t nc_float_to_half(float x)
{
    __fp16 h = static_cast<__fp16>(x);
    uint16_t bits;
    __builtin_memcpy(&bits, &h, sizeof(bits));
    return bits;
}

// Scalar reference for a single pixel of square NxN convolution; bit-exact
// with kernel/generic.cpp conv_plane_square (same (tap-column outer, row
// inner) order, same mirror, same rounding/clamp). Used for edge columns.
template <class T, class Acc, class Weight, unsigned N>
inline T nc_sq_scalar_px(const T *const *rows, unsigned j, unsigned S, unsigned W,
                         const Weight *coeffs, float div, float bias, bool saturate, uint16_t maxval)
{
    Acc accum = 0;
    for (unsigned k = 0; k < N; ++k) {
        unsigned col = nc_mirror(static_cast<int>(j) + static_cast<int>(k) - static_cast<int>(S), static_cast<int>(W));
        for (unsigned r = 0; r < N; ++r)
            accum += coeffs[r * N + k] * static_cast<Acc>(rows[r][col]);
    }
    float tmp = static_cast<float>(accum) * div + bias;
    tmp = saturate ? tmp : std::fabs(tmp);
    if constexpr (std::is_integral<T>::value) {
        float c = std::min(std::max(tmp, 0.0f), static_cast<float>(sizeof(T) == 1 ? 255 : 65535));
        long v = std::lrint(c);
        return static_cast<T>(std::min<long>(v, maxval));
    } else {
        return static_cast<T>(tmp);
    }
}

// Scalar edge reference for half (binary16) planes: samples widen to float32
// for the arithmetic and the result narrows back to half, mirroring the C
// fallback's HalfOp/conv paths.
template <unsigned N>
inline uint16_t nc_sq_scalar_px_half(const uint16_t *const *rows, unsigned j, unsigned S, unsigned W,
                                     const float *coeffs, float div, float bias, bool saturate)
{
    float accum = 0.0f;
    for (unsigned k = 0; k < N; ++k) {
        unsigned col = nc_mirror(static_cast<int>(j) + static_cast<int>(k) - static_cast<int>(S), static_cast<int>(W));
        for (unsigned r = 0; r < N; ++r)
            accum += coeffs[r * N + k] * nc_half_to_float(rows[r][col]);
    }
    float tmp = accum * div + bias;
    tmp = saturate ? tmp : std::fabs(tmp);
    return nc_float_to_half(tmp);
}

} // namespace

#endif // ARM_CONV_SCALAR_H
