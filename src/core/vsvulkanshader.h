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

#include "vsvulkanexec.h"

/* One compute pipeline around one build time compiled SPIR-V blob, with the fixed shape every
   plane filter shares: N storage buffers at bindings 0..N-1 of set 0 and a push constant block.
   Push descriptors mean there are no pools and no sets to allocate or recycle, and maintenance5
   chains the SPIR-V straight into pipeline creation so not even a shader module object exists.
   What remains is exactly three handles, created once per filter instance and used from any
   thread, since recording into distinct command buffers needs no synchronization. */
class VSVulkanComputePipeline {
public:
    static constexpr uint32_t maxBindings = 8;

    VSVulkanComputePipeline() = default;
    ~VSVulkanComputePipeline();
    VSVulkanComputePipeline(const VSVulkanComputePipeline &) = delete;
    VSVulkanComputePipeline &operator=(const VSVulkanComputePipeline &) = delete;

    /* The device must outlive this object. Single shot like the rest of the layer. */
    bool init(VSVulkanDevice &device, const uint32_t *spirv, size_t spirvBytes,
        uint32_t storageBufferCount, uint32_t pushConstantBytes, std::string &errorMessage);

    /* Records bind + push descriptors + push constants + dispatch into an acquired context.
       Buffer count and push size must match what init() declared. */
    void recordDispatch(VSVulkanExecContext &context, const VkBuffer *buffers, uint32_t bufferCount,
        const void *pushData, uint32_t pushBytes, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);

private:
    VSVulkanDevice *dev = nullptr;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint32_t declaredBuffers = 0;
    uint32_t declaredPushBytes = 0;
};

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
