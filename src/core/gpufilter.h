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
#include <cstring>
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
    uint32_t srcWidth = 0;  /* of clip 0's plane, which a geometry changing filter needs */
    uint32_t srcHeight = 0;
    uint32_t strideElements[8] = {}; /* per binding, in samples */
    const uint32_t *frameParams = nullptr; /* whatever prepareFrame produced, if anything */

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
    /* Which source an unprocessed plane is shared from. Almost always clip 0, but a filter
       that degenerates to one of its inputs per plane -- Merge at weight 1, say -- needs to
       name the other one instead of computing an identity. */
    int shareClip[3] = { 0, 0, 0 };
    VSVideoInfo vi = {};
    /* Fills pushConstantBytes worth of push constants for one dispatch. */
    std::function<void(const PassInfo &, void *push)> fillPush;
    /* Optional, run after the work is recorded: frame properties are host side metadata
       that no kernel touches, so a filter rewriting them -- Crop flipping _FieldBased on an
       odd offset, say -- does it here rather than needing a getFrame of its own. */
    std::function<void(int n, VSFrame *dst, const VSFrame *const *sources, int numSources,
        const uint32_t *params, VSCore *core, const VSAPI *vsapi)> finishFrame;

    /* Which source frame an output frame reads, per clip and declared offset. The default
       below is n + offset; a filter whose output runs at a different rate than its input --
       SeparateFields emitting two frames per source frame -- replaces it. */
    std::function<int(int n, int clip, int frameOffset)> mapFrame;

    /* Optional, run before any push constants are filled: fills frameParamCount uint32s
       from the source frames themselves. Needed whenever a kernel parameter is a property
       of the frame rather than of the filter -- field order read from _Field, say, which is
       not known until the frame arrives. Returning false fails the frame with the message.
       The parameters live on the stack of the call, so this stays reentrant. */
    int frameParamCount = 0;
    std::function<bool(int n, const VSFrame *const *sources, int numSources,
        const VSAPI *vsapi, uint32_t *params, std::string &error)> prepareFrame;
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

    auto sourceIndex = [&](int clip, int offset) {
        const int want = desc.mapFrame ? desc.mapFrame(n, clip, offset) : n + offset;
        return std::clamp(want, 0, vsapi->getVideoInfo(desc.nodes[clip])->numFrames - 1);
    };

    if (activationReason == arInitial) {
        for (const Pass &pass : desc.passes) {
            for (const Operand &op : pass.bindings) {
                if (op.kind != Operand::SourcePlane)
                    continue;
                vsapi->requestFrameFilter(sourceIndex(op.clip, op.frameOffset), desc.nodes[op.clip], frameCtx);
            }
        }
        /* A clip only named as a share source has no binding to be picked up above. */
        for (int p = 0; p < 3 && p < desc.vi.format.numPlanes; p++) {
            if (!desc.process[p])
                vsapi->requestFrameFilter(sourceIndex(desc.shareClip[p], 0), desc.nodes[desc.shareClip[p]], frameCtx);
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
        const VSFrame *f = vsapi->getFrameFilter(sourceIndex(clip, offset), desc.nodes[clip], frameCtx);
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
            planeSrc[p] = desc.process[p] ? nullptr : fetch(desc.shareClip[p], 0);
        dst = vsapi->newVideoFrame2(fmt, w, h, planeSrc, planeIdx, first, core);
    } else {
        dst = inst->vkapi->newGPUVideoFrame(fmt, w, h, first, core);
    }
    if (!dst) {
        vsapi->setFilterError("GPU filter: failed to allocate the output frame", frameCtx);
        releaseSources();
        return nullptr;
    }

    std::vector<const VSFrame *> sourceFrames;
    for (const SourceFrame &s : sources)
        sourceFrames.push_back(s.frame);

    /* Local, so concurrent frames on this node never see each other's parameters. */
    std::vector<uint32_t> frameParams(static_cast<size_t>(desc.frameParamCount));
    if (desc.prepareFrame) {
        std::string prepareError;
        if (!desc.prepareFrame(n, sourceFrames.data(), static_cast<int>(sourceFrames.size()),
                vsapi, frameParams.data(), prepareError)) {
            vsapi->setFilterError(prepareError.c_str(), frameCtx);
            releaseSources();
            vsapi->freeFrame(dst);
            return nullptr;
        }
    }
    const uint32_t *frameParamData = frameParams.empty() ? nullptr : frameParams.data();
    auto finish = [&]() {
        if (desc.finishFrame)
            desc.finishFrame(n, dst, sourceFrames.data(), static_cast<int>(sourceFrames.size()),
                frameParamData, core, vsapi);
    };

    /* Nothing to run: every plane shares from the source, so the frame is already complete
       and submitting an empty command buffer would only cost a round trip. */
    if (!processAny) {
        finish();
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
    /* Local, not a member: this node runs fmParallel, so concurrent frames would otherwise
       fill the same buffer and race. Harmless for a filter whose push constants are the
       same every frame, which is why it took one whose parameters vary per frame to show. */
    std::vector<uint8_t> pushScratch;

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
            info.srcWidth = static_cast<uint32_t>(vsapi->getFrameWidth(first ? first : dst, p));
            info.srcHeight = static_cast<uint32_t>(vsapi->getFrameHeight(first ? first : dst, p));
            info.frameParams = frameParamData;

            if (prog.pushConstantBytes > 0 && desc.fillPush) {
                pushScratch.assign(static_cast<size_t>(prog.pushConstantBytes), 0);
                desc.fillPush(info, pushScratch.data());
                VkPushConstantsInfo pushInfo = {};
                pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
                pushInfo.layout = inst->pipeLayouts[pass.program];
                pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushInfo.size = static_cast<uint32_t>(prog.pushConstantBytes);
                pushInfo.pValues = pushScratch.data();
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

    finish();
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
        /* Half precision is the one variant a conformant device may legitimately refuse,
           and the raw compiler log does not say so; every consumer wants this hint. */
        if (desc.vi.format.sampleType == stFloat && desc.vi.format.bytesPerSample == 2)
            errorMessage += " (half precision formats need the shaderFloat16 feature, which this device may lack)";
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

/* ---------------------------------------------------------------------------------------
   The common case, one step up from FilterDesc.

   Nearly every pixel filter is: one dispatch per processed plane, one output, one to three
   sources read at the same coordinate or in a small neighbourhood around it, and a handful
   of scalar parameters that vary per plane. Declaring that through FilterDesc means
   repeating a prologue, a bounds check and a push constant struct in every filter, so this
   layer supplies all three and leaves the filter with its per pixel expression.

   A filter gives one GLSL statement block for integer formats and one for float, mirroring
   how the CPU side already splits processPlane from processPlaneF, plus a callback filling
   the parameter block for a plane. Everything past that -- format specialization, bindings,
   geometry, edge clamping helpers -- comes from here.

   Filters that do not fit (multiple passes, scratch, geometry changes, their own descriptor
   layout) drop to FilterDesc directly; the two compose, since this only builds one. */

constexpr int simpleMaxInputs = 3;
constexpr int simpleFloatParams = 32; /* enough for a 5x5 convolution matrix */
constexpr int simpleUintParams = 8;

struct SimplePush {
    uint32_t width, height;         /* of the output plane, which is what is dispatched over */
    uint32_t srcWidth, srcHeight;   /* of clip 0's plane; differs once geometry changes */
    uint32_t srcStride[simpleMaxInputs];
    uint32_t dstStride;
    float f[simpleFloatParams];
    uint32_t u[simpleUintParams];
};

struct SimpleFilter {
    const char *name = nullptr;
    int inputs = 1;
    /* GLSL statements with int x, int y in scope and the SRCn/STORE macros available.
       bodyInt serves stInteger, bodyFloat serves both float widths; give only the one that
       applies when a filter rejects the other sample type. */
    std::string bodyInt;
    std::string bodyFloat;
    /* Anything the body needs beyond the macros: helper functions, extra defines. */
    std::string prelude;
    bool process[3] = { true, true, true };
    int shareClip[3] = { 0, 0, 0 }; /* source for planes this filter does not compute */
    /* Fills the per plane parameter block. Called once per plane per frame. */
    std::function<void(int plane, float *f, uint32_t *u)> fill;
    /* Optional host side frame property fixup; see FilterDesc::finishFrame. */
    std::function<void(int n, VSFrame *dst, const VSFrame *const *sources, int numSources,
        const uint32_t *params, VSCore *core, const VSAPI *vsapi)> finishFrame;

    /* Optional source frame index mapping and per frame parameters; see FilterDesc. When
       prepare is set, fillFrame runs instead of fill so the body can reach what it produced. */
    std::function<int(int n, int clip, int frameOffset)> mapFrame;
    int prepareParams = 0;
    std::function<bool(int n, const VSFrame *const *sources, int numSources,
        const VSAPI *vsapi, uint32_t *params, std::string &error)> prepare;
    std::function<void(int plane, const uint32_t *params, float *f, uint32_t *u)> fillFrame;
};

namespace detail {

inline std::string simpleSource(const SimpleFilter &sf, const VSVideoFormat &fmt) {
    const bool isFloat = fmt.sampleType == stFloat;
    const bool isHalf = isFloat && fmt.bytesPerSample == 2;

    std::string s = "#version 460\n";
    if (isHalf)
        s += "#define SAMPLE_T float16_t\n";
    else if (isFloat)
        s += "#define SAMPLE_T float\n";
    else
        s += fmt.bytesPerSample == 1 ? "#define SAMPLE_T uint8_t\n" : "#define SAMPLE_T uint16_t\n";

    s += "#extension GL_EXT_shader_8bit_storage : require\n"
         "#extension GL_EXT_shader_16bit_storage : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n";
    if (isHalf)
        s += "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";

    s += "\nlayout(local_size_x = 16, local_size_y = 16) in;\n\n"
         "layout(push_constant) uniform PC {\n"
         "    uint width, height;\n"
         "    uint srcWidth, srcHeight;\n"
         "    uint srcStride[" + std::to_string(simpleMaxInputs) + "];\n"
         "    uint dstStride;\n"
         "    float f[" + std::to_string(simpleFloatParams) + "];\n"
         "    uint u[" + std::to_string(simpleUintParams) + "];\n"
         "} pc;\n\n";

    for (int i = 0; i < sf.inputs; i++) {
        const std::string n = std::to_string(i);
        s += "layout(std430, set = 0, binding = " + n + ") readonly buffer Src" + n +
             " { SAMPLE_T s" + n + "[]; };\n";
    }
    s += "layout(std430, set = 0, binding = " + std::to_string(sf.inputs) +
         ") writeonly buffer Dst { SAMPLE_T dstData[]; };\n\n";

    /* Two edge rules, because the CPU tree uses both: SRCn replicates the edge sample,
       MSRCn reflects around it half-sample symmetrically. Which one a filter wants is a
       property of the filter, not of this layer -- 3x3 convolution replicates while every
       wider one mirrors -- so both are always available and neither is a default a
       pointwise body would ever notice, since it only asks for (x, y). */
    s += "#define CX(xx) uint(clamp((xx), 0, int(pc.width) - 1))\n"
         "#define CY(yy) uint(clamp((yy), 0, int(pc.height) - 1))\n"
         "int vsMirror(int pos, int len) {\n"
         "    if (pos < 0) pos = -pos - 1;\n"
         "    else if (pos >= len) pos = 2 * len - 1 - pos;\n"
         "    return clamp(pos, 0, len - 1);\n"
         "}\n"
         /* Vulkan specifies sqrt to 3 ulp, where the scalar paths this tree is checked
            against get a correctly rounded SQRTSS. One Newton step recovers the difference:
            fma computes the residual s - y*y exactly, so the correction is exact to well
            under an ulp of y and the final add rounds to the right result. Cheap enough to
            be the default -- two flops on top of a square root -- and unlike doing the root
            in fp64 it asks nothing of the device. */
         "float vsSqrt(float s) {\n"
         "    float y = sqrt(s);\n"
         "    if (!(y > 0.0) || isinf(y)) return y;\n"
         "    precise float r = fma(-y, y, s);\n"
         "    return y + r / (y + y);\n"
         "}\n"
         "#define MX(xx) uint(vsMirror((xx), int(pc.width)))\n"
         "#define MY(yy) uint(vsMirror((yy), int(pc.height)))\n";
    /* SRCn and MSRCn bound against the plane being written, which is the same plane a
       filter that does not move pixels is reading. GSRCn bounds against the source instead,
       for the filters where the two differ -- a transpose dispatches over an output whose
       width is the source's height. */
    s += "#define SCX(xx) uint(clamp((xx), 0, int(pc.srcWidth) - 1))\n"
         "#define SCY(yy) uint(clamp((yy), 0, int(pc.srcHeight) - 1))\n";
    for (int i = 0; i < sf.inputs; i++) {
        const std::string n = std::to_string(i);
        s += "#define SRC" + n + "(xx, yy) s" + n + "[CY(yy) * pc.srcStride[" + n + "] + CX(xx)]\n";
        s += "#define MSRC" + n + "(xx, yy) s" + n + "[MY(yy) * pc.srcStride[" + n + "] + MX(xx)]\n";
        s += "#define GSRC" + n + "(xx, yy) s" + n + "[SCY(yy) * pc.srcStride[" + n + "] + SCX(xx)]\n";
    }
    s += "#define STORE(v) dstData[uint(y) * pc.dstStride + uint(x)] = SAMPLE_T(v)\n\n";

    if (!sf.prelude.empty())
        s += sf.prelude + "\n";

    s += "void main() {\n"
         "    int x = int(gl_GlobalInvocationID.x);\n"
         "    int y = int(gl_GlobalInvocationID.y);\n"
         "    if (uint(x) >= pc.width || uint(y) >= pc.height) return;\n";
    s += isFloat ? sf.bodyFloat : sf.bodyInt;
    s += "\n}\n";
    return s;
}

} // namespace detail

/* Consumes the nodes either way, like createFilter. */
inline VSNode *createSimpleFilter(const SimpleFilter &sf, VSNode * const *nodes, int numNodes,
    const VSVideoInfo *vi, VSCore *core, const VSAPI *vsapi, std::string &errorMessage) {
    FilterDesc desc;
    desc.vi = *vi;
    for (int i = 0; i < numNodes; i++)
        desc.nodes.push_back(nodes[i]);
    for (int p = 0; p < 3; p++) {
        desc.process[p] = sf.process[p];
        desc.shareClip[p] = sf.shareClip[p];
    }

    auto fail = [&](const std::string &message) -> VSNode * {
        errorMessage = message;
        for (VSNode *node : desc.nodes)
            vsapi->freeNode(node);
        return nullptr;
    };

    if (numNodes != sf.inputs || sf.inputs < 1 || sf.inputs > simpleMaxInputs)
        return fail("wrong number of source clips for the declared kernel");
    if ((vi->format.sampleType == stFloat ? sf.bodyFloat : sf.bodyInt).empty())
        return fail("the format is not supported on the GPU path");

    Program program;
    program.glsl = detail::simpleSource(sf, vi->format);
    program.storageBufferCount = sf.inputs + 1;
    program.pushConstantBytes = sizeof(SimplePush);
    desc.programs.push_back(std::move(program));

    Pass pass;
    for (int i = 0; i < sf.inputs; i++)
        pass.bindings.push_back(Operand::source(i));
    pass.bindings.push_back(Operand::output());
    desc.passes.push_back(std::move(pass));

    desc.finishFrame = sf.finishFrame;
    desc.mapFrame = sf.mapFrame;
    desc.prepareFrame = sf.prepare;
    desc.frameParamCount = sf.prepareParams;

    const auto fill = sf.fill;
    const auto fillFrame = sf.fillFrame;
    desc.fillPush = [fill, fillFrame](const PassInfo &info, void *pushData) {
        SimplePush push = {};
        push.width = info.width;
        push.height = info.height;
        push.srcWidth = info.srcWidth;
        push.srcHeight = info.srcHeight;
        for (int i = 0; i < simpleMaxInputs && i < info.bindingCount - 1; i++)
            push.srcStride[i] = info.strideElements[i];
        push.dstStride = info.dstStrideElements();
        if (fillFrame)
            fillFrame(info.plane, info.frameParams, push.f, push.u);
        else if (fill)
            fill(info.plane, push.f, push.u);
        std::memcpy(pushData, &push, sizeof(push));
    };

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < numNodes; i++)
        deps.push_back({ nodes[i], rpStrictSpatial });
    return createFilter(sf.name, desc, deps.data(), numNodes, core, vsapi, errorMessage);
}

} // namespace vsgpu

#endif
