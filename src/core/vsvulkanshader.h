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

#ifndef VSVULKANSHADER_H
#define VSVULKANSHADER_H

#include <memory>
#include <string>
#include <vector>
#include <cstdint>


/* The public opaque runtime compiled shader handle: an immutable shared SPIR-V blob, so
   handles stay valid independently of the compiling core (whose cache holds another
   reference) and of each other. */
struct VSGPUShader {
    std::shared_ptr<const std::vector<uint32_t>> code;
};

/* Compiles compute stage GLSL for the pinned dialect (#version 460, Vulkan 1.4 client,
   SPIR-V 1.6) through the statically embedded glslang. Pure CPU work; thread safe for
   concurrent calls. Returns null with errorMessage filled on failure. */
std::shared_ptr<const std::vector<uint32_t>> vsCompileGLSLCompute(const char *source, std::string &errorMessage);

#endif
