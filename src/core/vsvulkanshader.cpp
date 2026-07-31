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

VSVulkanComputePipeline::~VSVulkanComputePipeline() {
    if (!dev)
        return;
    if (pipeline)
        dev->vk.vkDestroyPipeline(dev->device(), pipeline, nullptr);
    if (pipelineLayout)
        dev->vk.vkDestroyPipelineLayout(dev->device(), pipelineLayout, nullptr);
    if (setLayout)
        dev->vk.vkDestroyDescriptorSetLayout(dev->device(), setLayout, nullptr);
}

bool VSVulkanComputePipeline::init(VSVulkanDevice &device, const uint32_t *spirv, size_t spirvBytes,
    uint32_t storageBufferCount, uint32_t pushConstantBytes, std::string &errorMessage) {
    if (dev) {
        errorMessage = "VSVulkanComputePipeline cannot be initialized twice";
        return false;
    }
    if (!spirv || !spirvBytes || storageBufferCount == 0 || storageBufferCount > maxBindings) {
        errorMessage = "Invalid pipeline description";
        return false;
    }

    dev = &device;
    declaredBuffers = storageBufferCount;
    declaredPushBytes = pushConstantBytes;

    VkDescriptorSetLayoutBinding bindings[maxBindings] = {};
    for (uint32_t i = 0; i < storageBufferCount; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    setInfo.bindingCount = storageBufferCount;
    setInfo.pBindings = bindings;
    VkResult res = dev->vk.vkCreateDescriptorSetLayout(dev->device(), &setInfo, nullptr, &setLayout);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateDescriptorSetLayout failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = pushConstantBytes;
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    if (pushConstantBytes) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &range;
    }
    res = dev->vk.vkCreatePipelineLayout(dev->device(), &layoutInfo, nullptr, &pipelineLayout);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreatePipelineLayout failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    /* maintenance5: the SPIR-V rides along in the stage's pNext, no module object needed. */
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirvBytes;
    moduleInfo.pCode = spirv;
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.pNext = &moduleInfo;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    res = dev->vk.vkCreateComputePipelines(dev->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateComputePipelines failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    return true;
}

void VSVulkanComputePipeline::recordDispatch(VSVulkanExecContext &context, const VkBuffer *buffers, uint32_t bufferCount,
    const void *pushData, uint32_t pushBytes, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
    assert(bufferCount == declaredBuffers && pushBytes == declaredPushBytes);

    dev->vk.vkCmdBindPipeline(context.commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    VkDescriptorBufferInfo bufferInfos[maxBindings] = {};
    VkWriteDescriptorSet writes[maxBindings] = {};
    for (uint32_t i = 0; i < bufferCount; i++) {
        bufferInfos[i].buffer = buffers[i];
        bufferInfos[i].range = VK_WHOLE_SIZE;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    dev->vk.vkCmdPushDescriptorSet(context.commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0,
        bufferCount, writes);

    if (pushBytes) {
        VkPushConstantsInfo pushInfo = {};
        pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
        pushInfo.layout = pipelineLayout;
        pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushInfo.size = pushBytes;
        pushInfo.pValues = pushData;
        dev->vk.vkCmdPushConstants2(context.commandBuffer(), &pushInfo);
    }

    dev->vk.vkCmdDispatch(context.commandBuffer(), groupsX, groupsY, groupsZ);
}

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
