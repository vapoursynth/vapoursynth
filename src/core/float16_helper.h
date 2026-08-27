/*
* Copyright (c) 2020 Fredrik Mellbin
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

#ifndef FLOAT16_HELPER_H
#define FLOAT16_HELPER_H

#include <cstdint>
#include <bit>
#include <cmath>

// Use the native _Float16 type wherever the compiler provides it, hardware lowering or
// not: where the target lacks conversion instructions the compiler's soft float builtins
// do the same job as the manual implementation below, and unlike it they round ties to
// even. __FLT16_MANT_DIG__ is the compiler's own statement that the type is usable on
// the current target, which is what the old any-non-x86 test got wrong: GCC has no
// _Float16 at all on 32-bit ARM without -mfp16-format=ieee, or on POWER, and the type
// being missing is a hard compile error rather than something a fallback can catch
// later. The one environment that still has to insist on hardware (F16C) is the MSVC
// runtime, where the soft builtins __truncsfhf2 / __extendhfsf2 live in compiler-rt
// builtins that clang-cl + lld-link never link.
#if (defined(__clang__) || defined(__GNUC__)) && defined(__FLT16_MANT_DIG__) && (defined(__F16C__) || !defined(_MSC_VER))
#  define VS_HAVE_NATIVE_FLOAT16 1
#else
#  define VS_HAVE_NATIVE_FLOAT16 0
#endif

static constexpr inline float halfToFloat(uint16_t h) {
#if VS_HAVE_NATIVE_FLOAT16
    return std::bit_cast<_Float16>(h);
#else
    uint32_t magic = (uint32_t)113 << 23;
    uint32_t shifted_exp = 0x7C00UL << 13;   // half exponent mask, shifted into float position
    uint32_t f;

    f = (uint32_t)(h & 0x7FFF) << 13;        // line up exponent + mantissa (23 - 10 = 13)
    uint32_t exp = f & shifted_exp;          // isolate the shifted half exponent
    f += (uint32_t)(127 - 15) << 23;         // rebias exponent (half bias 15 -> float bias 127)

    if (exp == shifted_exp) {                // Inf / NaN: extra exponent adjust to all-ones
        f += (uint32_t)(128 - 16) << 23;
    } else if (exp == 0) {                   // zero / denormal: renormalize via magic subtract
        f += (uint32_t)1 << 23;
        f = std::bit_cast<uint32_t>(std::bit_cast<float>(f) - std::bit_cast<float>(magic));
    }

    f |= (uint32_t)(h & 0x8000) << 16;       // restore sign
    return std::bit_cast<float>(f);
#endif
}

static constexpr inline uint16_t floatToHalf(float x) {
#if VS_HAVE_NATIVE_FLOAT16
    return std::bit_cast<uint16_t>(_Float16(x));
#else
    float magic = std::bit_cast<float>((uint32_t)15 << 23);
    uint32_t inf = 255UL << 23;
    uint32_t f16inf = 31UL << 23;
    uint32_t sign_mask = 0x80000000UL;
    uint32_t round_mask = ~0x0FFFU;
    uint16_t ret;
    uint32_t f = std::bit_cast<uint32_t>(x);
    uint32_t sign = f & sign_mask;
    f ^= sign;

    if (f >= inf) {
        ret = f > inf ? 0x7E00 : 0x7C00;
    } else {
        f &= round_mask;
        f = std::bit_cast<uint32_t>(std::bit_cast<float>(f) * magic);
        f -= round_mask;

        if (f > f16inf)
            f = f16inf;

        ret = (uint16_t)(f >> 13);
    }

    ret |= (uint16_t)(sign >> 16);
    return ret;
#endif
}

static constexpr inline int isInfHalf(uint16_t v) {
    // Since this would rely on std::inf(_Float16) being implemented which it isn't
    // in the MSVC STL that clang-cl also uses this is bit fiddling instead 
    return (v & 0x7C00) == 0x7C00;
}

#endif