/*
* Copyright (c) 2026 Fredrik Mellbin
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

#ifndef VSGPUGLSL_H
#define VSGPUGLSL_H

#include <initializer_list>
#include <string>

#include "VapourSynth4.h"

/* How video formats are spelled in generated GLSL, in one place for every kernel in the
   tree and out of it. Seven filters used to carry their own copy of this mapping and of
   the #extension block below, drifting in small ways -- which of them said float16 and
   when, whether four byte integers had a spelling at all -- and a change meant finding
   every copy. */

namespace vsgpu {

/* The GLSL element type a sample of this format is declared and loaded as. Four byte
   integers are not a format anyone stores video in, but MakeFullDiff produces one -- 16
   bit input widens to 17 bits -- so the mapping covers them rather than leaving the case
   to whoever copies it next: a second copy of this chain is how such an output once came
   out declared as uint16_t. */
inline const char *glslElementType(const VSVideoFormat &f) {
    if (f.sampleType == stFloat)
        return f.bytesPerSample == 2 ? "float16_t" : "float";
    if (f.bytesPerSample == 4)
        return "uint";
    return f.bytesPerSample == 1 ? "uint8_t" : "uint16_t";
}

/* Whether spelling this format needs the float16 arithmetic extension. */
inline bool glslUsesFloat16(const VSVideoFormat &f) {
    return f.sampleType == stFloat && f.bytesPerSample == 2;
}

/* The #extension block admitting those spellings in buffer declarations and arithmetic.
   The float16 line rides only where something actually spells the type: the compiler
   accepts the directive regardless of what follows, but leaving it out keeps the source
   an honest record of what each kernel uses. */
inline std::string glslTypePreamble(bool needFloat16) {
    std::string s =
        "#extension GL_EXT_shader_8bit_storage : require\n"
        "#extension GL_EXT_shader_16bit_storage : require\n"
        "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
        "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n";
    if (needFloat16)
        s += "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";
    return s;
}

/* The same block for a fixed set of formats: the union decides the float16 line, so a
   filter with mixed input and output formats states them all and forgets about it. */
inline std::string glslTypePreamble(std::initializer_list<const VSVideoFormat *> formats) {
    bool half = false;
    for (const VSVideoFormat *f : formats)
        half = half || (f && glslUsesFloat16(*f));
    return glslTypePreamble(half);
}

/* sqrt with one Newton correction. Vulkan specifies sqrt to 3 ulp where the scalar paths
   the kernels are checked against get a correctly rounded SQRTSS; fma computes the
   residual s - y*y exactly, so the correction is good to well under an ulp of y. Two
   flops on top of a square root, and unlike an fp64 root it asks nothing of the device.
   The sequence is a numeric contract, shared so Expr and the SimpleFilter kernels cannot
   drift to different last bits. */
inline constexpr char glslVsSqrt[] =
    "float vsSqrt(float s) {\n"
    "    float y = sqrt(s);\n"
    "    if (!(y > 0.0) || isinf(y)) return y;\n"
    "    precise float r = fma(-y, y, s);\n"
    "    return y + r / (y + y);\n"
    "}\n";

} // namespace vsgpu

#endif // VSGPUGLSL_H
