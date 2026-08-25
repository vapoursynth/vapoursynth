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

#include "vsvulkanshader.h"

#include <cassert>

//////////////////////////////////////////
// Runtime GLSL compilation through the statically embedded glslang. Kept in this file so
// glslang headers stay out of every other translation unit.

#include "vscore.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <mutex>

std::shared_ptr<const std::vector<uint32_t>> vsCompileGLSLCompute(const char *source, std::string &errorMessage) {
    /* Process wide one time setup, deliberately never torn down: multiple cores and
       threads share it and glslang's global state cannot be finalized safely while any
       other thread might still compile. */
    static std::once_flag initFlag;
    std::call_once(initFlag, []() { glslang_initialize_process(); });

    glslang_input_t input = {};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = GLSLANG_STAGE_COMPUTE;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_4;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_6;
    input.code = source;
    input.default_version = 460;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    glslang_shader_t *shader = glslang_shader_create(&input);
    if (!shader) {
        errorMessage = "glslang shader object creation failed";
        return nullptr;
    }

    if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input)) {
        errorMessage = glslang_shader_get_info_log(shader);
        glslang_shader_delete(shader);
        return nullptr;
    }

    glslang_program_t *program = glslang_program_create();
    if (!program) {
        errorMessage = "glslang program object creation failed";
        glslang_shader_delete(shader);
        return nullptr;
    }
    glslang_program_add_shader(program, shader);
    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        errorMessage = glslang_program_get_info_log(program);
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return nullptr;
    }

    glslang_program_SPIRV_generate(program, GLSLANG_STAGE_COMPUTE);

    /* The word buffer is owned by the program object, so it must be copied out before the
       teardown below. */
    size_t words = glslang_program_SPIRV_get_size(program);
    const unsigned int *ptr = glslang_program_SPIRV_get_ptr(program);
    auto code = std::make_shared<std::vector<uint32_t>>(ptr, ptr + words);

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    if (code->empty() || (*code)[0] != 0x07230203u) {
        errorMessage = "glslang produced no SPIR-V";
        return nullptr;
    }
    return code;
}

std::shared_ptr<const std::vector<uint32_t>> VSCore::compileShaderCached(int language, const char *source, std::string &errorMessage) {
    if (language != 0) {
        errorMessage = "unknown shader language " + std::to_string(language) + ", this core only knows GLSL (0)";
        return nullptr;
    }

    /* Keyed by the full source text prefixed with the language, so a hash collision can
       never hand out the wrong kernel; sources are small enough that retaining them is
       irrelevant next to the blobs. */
    std::string key(1, static_cast<char>(language));
    key += source;

    {
        std::lock_guard<std::mutex> lock(shaderCacheLock);
        auto it = shaderCache.find(key);
        if (it != shaderCache.end())
            return it->second;
    }

    /* Compiled outside the lock: 10-40 ms per kernel must not serialize unrelated filter
       creation. Concurrent misses on the same source waste a compile and the first insert
       wins, preserving pointer identity for every consumer. */
    auto code = vsCompileGLSLCompute(source, errorMessage);
    if (!code)
        return nullptr;

    std::lock_guard<std::mutex> lock(shaderCacheLock);
    auto inserted = shaderCache.emplace(std::move(key), std::move(code));
    return inserted.first->second;
}
