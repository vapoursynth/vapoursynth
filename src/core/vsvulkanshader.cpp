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
