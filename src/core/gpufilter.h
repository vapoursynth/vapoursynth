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

#ifndef VS_GPUFILTER_H
#define VS_GPUFILTER_H

/* A driver for the shape almost every GPU filter has: for each processed plane, run a fixed
   list of compute passes whose bindings are frame planes, scratch buffers or constant
   buffers, with push constants that vary per plane and pass. A filter using it declares
   that list once and supplies one callback that fills the push constants; the driver owns
   the frame loop -- output allocation with plane sharing, scratch, the exec context,
   producer waits, retention, barriers between passes, dispatch geometry, producer
   publication and submission.

   Deliberately a header of inline code built on the public VSVULKANAPI, not an entry in it.
   That means it can change shape freely: every consumer compiles the version it saw, the
   way VSHelper4.h works, so nothing here is an ABI commitment. Filters that need what this
   does not model -- indirect dispatch, specialization constants, reductions that read back
   on the host -- use VSVULKANAPI directly instead, and can still take the exec pool from it
   for the same lifetime and synchronization guarantees.

   Compiles anywhere the public headers do; the core builds it into the filters plugin. */

#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsgpu {

/* What a pass binds at one descriptor slot. Never a VkBuffer: the driver resolves these to
   buffers per plane, which is what keeps a declaring filter free of Vulkan types. */
struct Operand {
    enum Kind { SourcePlane, OutputPlane, Scratch };
    Kind kind = SourcePlane;
    int clip = 0;         /* which source clip, for multi input filters */
    int frameOffset = 0;  /* temporal: 0 is the frame being produced */
    int slot = 0;         /* scratch index */

    static Operand source(int clip = 0, int frameOffset = 0) {
        Operand o; o.kind = SourcePlane; o.clip = clip; o.frameOffset = frameOffset; return o;
    }
    static Operand output() { Operand o; o.kind = OutputPlane; return o; }
    static Operand scratch(int slot) { Operand o; o.kind = Scratch; o.slot = slot; return o; }
};

/* A kernel, given either as source for the driver to compile or as SPIR-V a filter built
   some other way. Source is the usual case: it keeps VSGPUShader lifetime out of the
   filter, since the pipeline owns the code once it is built and the shader object is only
   ever an intermediate. */
struct Program {
    std::string glsl;                 /* compiled at create time when non empty */
    const uint32_t *spirv = nullptr;  /* used directly otherwise */
    size_t spirvBytes = 0;
    int storageBufferCount = 2;
    int pushConstantBytes = 0;
    uint32_t localSizeX = 16;
    uint32_t localSizeY = 16;
};

struct Pass {
    int program = 0;
    std::vector<Operand> bindings;
    /* Dispatch geometry comes from the output plane by default, which is what geometry
       changing filters need; passes writing scratch shaped like the source say so. */
    bool geometryFromSource = false;
};

/* Everything the push constant callback could want about the dispatch it is filling. The
   strides are resolved per binding, which matters as soon as a filter ping pongs: a pass
   reading scratch has the output plane's stride on its input, not the source frame's. */
struct PassInfo {
    int plane = 0;
    int pass = 0;
    uint32_t width = 0;   /* of the plane this pass dispatches over */
    uint32_t height = 0;
    uint32_t strideElements[8] = {}; /* per binding, in samples */

    uint32_t srcStrideElements() const { return strideElements[0]; }
    uint32_t dstStrideElements() const { return strideElements[bindingCount - 1]; }
    int bindingCount = 0;
};

struct FilterDesc {
    std::vector<VSNode *> nodes;   /* source clips; ownership passes to the driver */
    std::vector<Program> programs;
    std::vector<Pass> passes;
    int scratchCount = 0;          /* plane sized scratch buffers, per frame */
    bool process[3] = { true, true, true };
    VSVideoInfo vi = {};
    /* Fills pushConstantBytes worth of push constants for one dispatch. */
    std::function<void(const PassInfo &, void *push)> fillPush;
};

namespace detail {

struct Instance {
    FilterDesc desc;
    const VSVULKANAPI *vkapi = nullptr;
    const VSVulkanFunctions *vk = nullptr;
    VSVulkanCoreHandles handles = {};
    VSGPUExecPool *pool = nullptr;
    VkDescriptorSetLayout setLayouts[8] = {};
    VkPipelineLayout pipeLayouts[8] = {};
    VkPipeline pipelines[8] = {};
    int pipelineCount = 0;
    std::vector<uint8_t> pushScratch;

    ~Instance() {
        if (!vk)
            return;
        if (pool)
            vkapi->freeGPUExecPool(pool);
        for (int i = 0; i < pipelineCount; i++) {
            if (pipelines[i])
                vk->vkDestroyPipeline(handles.device, pipelines[i], nullptr);
            if (pipeLayouts[i])
                vk->vkDestroyPipelineLayout(handles.device, pipeLayouts[i], nullptr);
            if (setLayouts[i])
                vk->vkDestroyDescriptorSetLayout(handles.device, setLayouts[i], nullptr);
        }
    }
};

inline void VS_CC driverFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    Instance *inst = static_cast<Instance *>(instanceData);
    for (VSNode *node : inst->desc.nodes)
        vsapi->freeNode(node);
    delete inst;
}

inline void barrier(const VSVulkanFunctions *vk, VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb = {};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
}

inline const VSFrame *VS_CC driverGetFrame(int n, int activationReason, void *instanceData, void **,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    Instance *inst = static_cast<Instance *>(instanceData);
    const FilterDesc &desc = inst->desc;

    if (activationReason == arInitial) {
        for (const Pass &pass : desc.passes) {
            for (const Operand &op : pass.bindings) {
                if (op.kind != Operand::SourcePlane)
                    continue;
                VSNode *node = desc.nodes[op.clip];
                int want = std::clamp(n + op.frameOffset, 0, vsapi->getVideoInfo(node)->numFrames - 1);
                vsapi->requestFrameFilter(want, node, frameCtx);
            }
        }
        return nullptr;
    }
    if (activationReason != arAllFramesReady)
        return nullptr;

    /* Source frames, one per (clip, offset) the pass list mentions. */
    struct SourceFrame { int clip; int offset; const VSFrame *frame; };
    std::vector<SourceFrame> sources;
    auto fetch = [&](int clip, int offset) -> const VSFrame * {
        for (const SourceFrame &s : sources)
            if (s.clip == clip && s.offset == offset)
                return s.frame;
        VSNode *node = desc.nodes[clip];
        int want = std::clamp(n + offset, 0, vsapi->getVideoInfo(node)->numFrames - 1);
        const VSFrame *f = vsapi->getFrameFilter(want, node, frameCtx);
        sources.push_back({ clip, offset, f });
        return f;
    };
    for (const Pass &pass : desc.passes)
        for (const Operand &op : pass.bindings)
            if (op.kind == Operand::SourcePlane)
                fetch(op.clip, op.frameOffset);

    auto releaseSources = [&]() {
        for (const SourceFrame &s : sources)
            vsapi->freeFrame(s.frame);
    };

    const VSFrame *first = sources.empty() ? nullptr : sources[0].frame;
    const VSVideoFormat *fmt = &desc.vi.format;
    const int w = desc.vi.width, h = desc.vi.height;
    char err[512] = { 0 };

    /* Unprocessed planes ride along from the first source, which keeps them on the device
       with their own producer pairs; everything processed is written by this submission. */
    bool shareAny = false, processAny = false;
    for (int p = 0; p < fmt->numPlanes; p++) {
        shareAny = shareAny || !desc.process[p];
        processAny = processAny || desc.process[p];
    }

    VSFrame *dst;
    if (shareAny && first) {
        const VSFrame *planeSrc[3] = {};
        int planeIdx[3] = { 0, 1, 2 };
        for (int p = 0; p < fmt->numPlanes; p++)
            planeSrc[p] = desc.process[p] ? nullptr : first;
        dst = vsapi->newVideoFrame2(fmt, w, h, planeSrc, planeIdx, first, core);
    } else {
        dst = inst->vkapi->newGPUVideoFrame(fmt, w, h, first, core);
    }
    if (!dst) {
        vsapi->setFilterError("GPU filter: failed to allocate the output frame", frameCtx);
        releaseSources();
        return nullptr;
    }

    /* Nothing to run: every plane shares from the source, so the frame is already complete
       and submitting an empty command buffer would only cost a round trip. */
    if (!processAny) {
        releaseSources();
        return dst;
    }

    VSGPUExecContext *ctx = inst->vkapi->gpuExecAcquire(inst->pool, err, sizeof(err));
    if (!ctx) {
        vsapi->setFilterError((std::string("GPU filter: ") + err).c_str(), frameCtx);
        releaseSources();
        vsapi->freeFrame(dst);
        return nullptr;
    }
    for (const SourceFrame &s : sources)
        inst->vkapi->gpuExecReadsFrame(ctx, s.frame);

    /* Scratch is sized for the largest processed plane and lives for one submission. */
    std::vector<VSVulkanBufferInfo> scratch(static_cast<size_t>(desc.scratchCount));
    if (desc.scratchCount > 0) {
        VkDeviceSize maxBytes = 0;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (!desc.process[p])
                continue;
            VSVulkanPlaneInfo info;
            if (!inst->vkapi->getGPUPlane(dst, p, &info))
                maxBytes = std::max(maxBytes, info.bufferSize);
        }
        for (int i = 0; i < desc.scratchCount; i++) {
            VSGPUBuffer *buffer = inst->vkapi->createGPUBuffer(core, maxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &scratch[i], err, sizeof(err));
            if (!buffer) {
                vsapi->setFilterError((std::string("GPU filter: ") + err).c_str(), frameCtx);
                inst->vkapi->gpuExecAbandon(ctx);
                releaseSources();
                vsapi->freeFrame(dst);
                return nullptr;
            }
            inst->vkapi->gpuExecUsesBuffer(ctx, buffer);
        }
    }

    VkCommandBuffer cmd = inst->vkapi->gpuExecCommandBuffer(ctx);
    bool firstDispatch = true;

    for (int p = 0; p < fmt->numPlanes; p++) {
        if (!desc.process[p])
            continue;
        inst->vkapi->gpuExecWritesPlane(ctx, dst, p);

        VSVulkanPlaneInfo dstPlane;
        if (inst->vkapi->getGPUPlane(dst, p, &dstPlane)) {
            vsapi->setFilterError("GPU filter: output frame is not GPU resident", frameCtx);
            inst->vkapi->gpuExecAbandon(ctx);
            releaseSources();
            vsapi->freeFrame(dst);
            return nullptr;
        }

        /* Scratch has no stride of its own: it is allocated to hold the widest processed
           plane, so every pass addresses it with that plane's output stride. */
        const uint32_t dstStrideElems = static_cast<uint32_t>(vsapi->getStride(dst, p) / fmt->bytesPerSample);

        for (size_t i = 0; i < desc.passes.size(); i++) {
            const Pass &pass = desc.passes[i];
            const Program &prog = desc.programs[pass.program];

            PassInfo info;
            info.plane = p;
            info.pass = static_cast<int>(i);
            info.bindingCount = static_cast<int>(pass.bindings.size());

            VkDescriptorBufferInfo bufferInfo[8] = {};
            VkWriteDescriptorSet writes[8] = {};
            for (size_t b = 0; b < pass.bindings.size(); b++) {
                const Operand &op = pass.bindings[b];
                VkBuffer buffer = VK_NULL_HANDLE;
                uint32_t strideElems = dstStrideElems;
                if (op.kind == Operand::SourcePlane) {
                    const VSFrame *srcFrame = fetch(op.clip, op.frameOffset);
                    VSVulkanPlaneInfo planeInfo;
                    if (!inst->vkapi->getGPUPlane(srcFrame, p, &planeInfo))
                        buffer = planeInfo.buffer;
                    strideElems = static_cast<uint32_t>(vsapi->getStride(srcFrame, p) / fmt->bytesPerSample);
                } else if (op.kind == Operand::OutputPlane) {
                    buffer = dstPlane.buffer;
                } else {
                    buffer = scratch[op.slot].buffer;
                }
                info.strideElements[b] = strideElems;
                bufferInfo[b].buffer = buffer;
                bufferInfo[b].range = VK_WHOLE_SIZE;
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstBinding = static_cast<uint32_t>(b);
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].pBufferInfo = &bufferInfo[b];
            }

            if (!firstDispatch)
                barrier(inst->vk, cmd);
            firstDispatch = false;

            inst->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, inst->pipelines[pass.program]);
            inst->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, inst->pipeLayouts[pass.program],
                0, static_cast<uint32_t>(pass.bindings.size()), writes);

            const VSFrame *geometry = pass.geometryFromSource && first ? first : dst;
            info.width = static_cast<uint32_t>(vsapi->getFrameWidth(geometry, p));
            info.height = static_cast<uint32_t>(vsapi->getFrameHeight(geometry, p));

            if (prog.pushConstantBytes > 0 && desc.fillPush) {
                inst->pushScratch.assign(static_cast<size_t>(prog.pushConstantBytes), 0);
                desc.fillPush(info, inst->pushScratch.data());
                VkPushConstantsInfo pushInfo = {};
                pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
                pushInfo.layout = inst->pipeLayouts[pass.program];
                pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushInfo.size = static_cast<uint32_t>(prog.pushConstantBytes);
                pushInfo.pValues = inst->pushScratch.data();
                inst->vk->vkCmdPushConstants2(cmd, &pushInfo);
            }

            inst->vk->vkCmdDispatch(cmd,
                (info.width + prog.localSizeX - 1) / prog.localSizeX,
                (info.height + prog.localSizeY - 1) / prog.localSizeY, 1);
        }
    }

    if (inst->vkapi->gpuExecSubmit(ctx, err, sizeof(err))) {
        vsapi->setFilterError((std::string("GPU filter: ") + err).c_str(), frameCtx);
        releaseSources();
        vsapi->freeFrame(dst);
        return nullptr;
    }

    releaseSources();
    return dst;
}

} // namespace detail

/* Builds the node. Consumes desc.nodes on success and on failure alike, so the caller never
   has to unwind them. Returns null with errorMessage set. */
inline VSNode *createFilter(const char *name, const FilterDesc &desc, const VSFilterDependency *deps,
    int numDeps, VSCore *core, const VSAPI *vsapi, std::string &errorMessage) {
    auto inst = std::make_unique<detail::Instance>();
    inst->desc = desc;

    auto fail = [&](const std::string &message) -> VSNode * {
        errorMessage = message;
        for (VSNode *node : inst->desc.nodes)
            vsapi->freeNode(node);
        inst->desc.nodes.clear();
        return nullptr;
    };

    if (desc.programs.empty() || desc.passes.empty())
        return fail("a GPU filter needs at least one program and one pass");
    if (desc.programs.size() > 8)
        return fail("too many programs");

    char err[512] = { 0 };
    inst->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
    if (!inst->vkapi)
        return fail("the GPU API is not available");
    if (inst->vkapi->getVulkanHandles(core, &inst->handles, err, sizeof(err)))
        return fail(err);
    inst->vk = inst->vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!inst->vk)
        return fail(err);

    for (const Program &prog : desc.programs) {
        const int idx = inst->pipelineCount;

        /* Compiled here and freed below: the pipeline holds the code from creation on, so
           the shader object never outlives this loop. The core caches by source text, so
           repeated instances of the same kernel parse once. */
        VSGPUShader *shader = nullptr;
        const uint32_t *spirv = prog.spirv;
        size_t spirvBytes = prog.spirvBytes;
        if (!prog.glsl.empty()) {
            shader = inst->vkapi->compileGPUShader(core, slGLSL, prog.glsl.c_str(), err, sizeof(err));
            if (!shader)
                return fail(std::string("kernel failed to compile: ") + err);
            spirv = inst->vkapi->getGPUShaderCode(shader, &spirvBytes);
        }
        if (!spirv || !spirvBytes) {
            if (shader)
                inst->vkapi->freeGPUShader(shader);
            return fail("a program needs either source or SPIR-V");
        }
        struct ShaderGuard {
            const VSVULKANAPI *vkapi; VSGPUShader *shader;
            ~ShaderGuard() { if (shader) vkapi->freeGPUShader(shader); }
        } shaderGuard{ inst->vkapi, shader };

        VkDescriptorSetLayoutBinding bindings[8] = {};
        for (int b = 0; b < prog.storageBufferCount; b++) {
            bindings[b].binding = static_cast<uint32_t>(b);
            bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo setInfo = {};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
        setInfo.bindingCount = static_cast<uint32_t>(prog.storageBufferCount);
        setInfo.pBindings = bindings;
        if (inst->vk->vkCreateDescriptorSetLayout(inst->handles.device, &setInfo, nullptr, &inst->setLayouts[idx]) != VK_SUCCESS)
            return fail("descriptor set layout creation failed");

        VkPushConstantRange range = {};
        range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        range.size = static_cast<uint32_t>(prog.pushConstantBytes);
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &inst->setLayouts[idx];
        if (prog.pushConstantBytes > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &range;
        }
        if (inst->vk->vkCreatePipelineLayout(inst->handles.device, &layoutInfo, nullptr, &inst->pipeLayouts[idx]) != VK_SUCCESS)
            return fail("pipeline layout creation failed");

        VkShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirvBytes;
        moduleInfo.pCode = spirv;
        VkComputePipelineCreateInfo pipeInfo = {};
        pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.pNext = &moduleInfo;
        pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.pName = "main";
        pipeInfo.layout = inst->pipeLayouts[idx];
        if (inst->vk->vkCreateComputePipelines(inst->handles.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &inst->pipelines[idx]) != VK_SUCCESS)
            return fail("compute pipeline creation failed");

        inst->pipelineCount++;
    }

    inst->pool = inst->vkapi->createGPUExecPool(core, vqCompute, 4, err, sizeof(err));
    if (!inst->pool)
        return fail(err);

    VSNode *node = vsapi->createVideoFilterEx2(name, &inst->desc.vi, detail::driverGetFrame, detail::driverFree,
        fmParallel, ffGPUOutput, deps, numDeps, inst.get(), core);
    if (!node)
        return fail("filter creation failed");
    inst.release();
    return node;
}

} // namespace vsgpu

#endif
