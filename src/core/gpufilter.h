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

   Inline code over the public VSVULKANAPI rather than an entry in it, so it can change shape
   freely: like VSHelper4.h, every consumer compiles the version it saw and nothing here is an
   ABI commitment. Filters needing what this does not model -- indirect dispatch, their own
   descriptor layouts -- use VSVULKANAPI directly, and can still take its exec pool for the
   same lifetime and synchronization guarantees.

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

/* Bindings per pass. Sized for the widest thing in the tree: AverageFrames takes up to 31
   input clips plus the output. The program list itself is unbounded; a program only costs
   its pipeline. */
constexpr int maxBindings = 32;

/* What a pass binds at one descriptor slot. Never a VkBuffer: the driver resolves these to
   buffers per plane, which is what keeps a declaring filter free of Vulkan types. */
struct Operand {
    enum Kind { SourcePlane, OutputPlane, Scratch, Constant, Readback, FrameData };
    Kind kind = SourcePlane;
    int clip = 0;         /* which source clip, for multi input filters */
    int frameOffset = 0;  /* temporal: 0 is the frame being produced */
    int slot = 0;         /* scratch index */
    /* Which plane of that clip to read. -1, the default, is the plane being produced, suiting
       any filter that treats its planes independently. A single plane mask or alpha applied
       to all three output planes names 0 instead; without that the driver would ask a one
       plane frame for its plane 1 and get nothing.

       This says nothing about geometry -- the dispatch is over the output, so the named plane
       must be the size of the plane being written. A luma resolution mask against subsampled
       chroma needs a resample, not this. */
    int plane = -1;

    static Operand source(int clip = 0, int frameOffset = 0) {
        Operand o; o.kind = SourcePlane; o.clip = clip; o.frameOffset = frameOffset; return o;
    }
    static Operand sourcePlane(int plane, int clip = 0, int frameOffset = 0) {
        Operand o = source(clip, frameOffset); o.plane = plane; return o;
    }
    static Operand output() { Operand o; o.kind = OutputPlane; return o; }
    static Operand scratch(int slot) { Operand o; o.kind = Scratch; o.slot = slot; return o; }
    /* Data uploaded once at create and read by every frame: a lookup table, typically. */
    static Operand constant(int slot) { Operand o; o.kind = Constant; o.slot = slot; return o; }
    /* The per-frame host visible buffer a reducing filter writes its partials into; see
       FilterDesc::readbackBytes. */
    static Operand readback() { Operand o; o.kind = Readback; return o; }
    /* The per-frame device local buffer prepareFrameData filled; see
       FilterDesc::frameDataBytes. */
    static Operand frameData() { Operand o; o.kind = FrameData; return o; }
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
    /* Must agree with the layout(local_size_x/y) the kernel declares: this is what the
       dispatch is divided by, and nothing checks the two against each other. The simple
       layer below derives both from simpleLocalSize so they cannot drift; a hand written
       program states the number twice and has to keep it that way. */
    uint32_t localSizeX = 16;
    uint32_t localSizeY = 16;
    /* Specialization constants applied at pipeline creation; entries index into specData, and
       ids a module does not declare are ignored. Several programs may share one glsl string
       and differ only here: the source is parsed once (the shader cache is keyed by text) and
       each pipeline folds its constants, dead branches included, at creation -- which is why
       a program per plane beats branching on a push constant inside one kernel. */
    std::vector<uint8_t> specData;
    std::vector<VkSpecializationMapEntry> specEntries;
    /* Compute-stage subgroup control, for kernels whose lane arithmetic bakes the subgroup
       width in: a reduction sizing partials as workgroup / SGSIZE, a filter mapping one
       subgroup per work item. RDNA hardware runs wave32 or wave64 and the driver picks per
       pipeline unless told, so pinning the size is what keeps such a kernel correct, and
       requiring full subgroups makes per-subgroup bookkeeping exact. Both device features are
       in the core's required set, but pinning also needs
       VkPhysicalDeviceVulkan13Properties::requiredSubgroupSizeStages to cover compute; the
       filter checks that, the driver applies these as given. */
    uint32_t requiredSubgroupSize = 0;
    bool requireFullSubgroups = false;
};

struct PassInfo;

struct Pass {
    int program = 0;
    std::vector<Operand> bindings;
    /* Dispatch geometry comes from the output plane by default, which is what geometry
       changing filters need. Naming a binding dispatches over that binding's plane instead,
       which is what a filter whose passes differ in shape needs -- a stack runs one pass per
       input, each sized to that input. -1 leaves it off. */
    int geometryFromBinding = -1;
    /* Drop the barrier before this pass. Only for a pass that cannot observe what an earlier
       one in the same submission wrote: disjoint output regions, typically. The default is
       the safe one, so this is opt in per pass. */
    bool independent = false;
    /* Which processed planes this pass runs for; all of them by default. A filter whose
       program differs per plane declares one pass per plane (or per group of planes that
       share a body) instead of branching on the plane inside one kernel, where every
       dispatch would pay the register pressure of the heaviest plane's code. Every
       processed plane must be covered by at least one pass. */
    bool planes[3] = { true, true, true };
    /* Adjusts the dispatch grid after PassInfo is filled: the default grid covers the
       geometry plane, which is wrong for a pass over a derived layout -- a padded field
       plane, a 1D list of pixels. Mutate info.width/height; the dispatch divides them by
       the program's local size, and fillPush sees the adjusted values. */
    std::function<void(PassInfo &)> reshape;
};

/* Everything the push constant callback could want about the dispatch it is filling. The
   strides are resolved per binding, which matters as soon as a filter ping pongs: a pass
   reading scratch has the output plane's stride on its input, not the source frame's. */
struct PassInfo {
    int plane = 0;
    int pass = 0;
    uint32_t width = 0;   /* of the plane this pass dispatches over */
    uint32_t height = 0;
    /* Of the reference clip's plane at the reference offset, which is clip 0 at offset 0 for
       everything but AverageFrames, and is what a geometry changing filter needs. */
    uint32_t srcWidth = 0;
    uint32_t srcHeight = 0;
    uint32_t strideElements[maxBindings] = {}; /* per binding, in samples */
    /* Per binding device addresses, for pointer-ABI kernels that take their buffers as
       GL_EXT_buffer_reference pointers in push constants instead of descriptors; see
       FilterDesc::operandAddresses. Zero for operands whose buffer carries no address --
       frame planes, until the frame allocator opts its blocks in -- so pointer kernels
       stage planes through address-enabled scratch. */
    VkDeviceAddress addresses[maxBindings] = {};
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
    /* Optional per-slot overrides: a nonzero size replaces the plane-derived default (a
       float intermediate over an 8 bit output outgrows its plane; a padded working plane
       outgrows every plane), and extraUsage is added to the storage bit. Slots past the
       end of the vector keep the defaults. Explicit sizes are also what admit scratch
       into side effect filters, which have no processed plane to size from. */
    struct ScratchDef {
        VkDeviceSize bytes = 0;
        VkBufferUsageFlags extraUsage = 0;
    };
    std::vector<ScratchDef> scratchDefs;
    bool process[3] = { true, true, true };
    /* Which source an unprocessed plane is shared from. Almost always clip 0, but a filter
       that degenerates to one of its inputs per plane -- Merge at weight 1, say -- needs to
       name the other one instead of computing an identity. */
    int shareClip[3] = { 0, 0, 0 };
    /* The temporal offset on clip 0 of the frame the output is really derived from: it
       supplies frame properties, and the moment any shared plane is taken at. Zero for
       almost everything, but a filter reading a window around the frame -- AverageFrames --
       means its centre, not the oldest frame in the window, which is what the fetch order
       would otherwise give. */
    int refOffset = 0;
    /* Blobs uploaded to device local memory at create, addressed by Operand::constant.
       Device local rather than host visible because the reads are random per pixel, which
       over PCIe would cost more than the filter saves. */
    std::vector<std::vector<uint8_t>> constants;
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

    /* prepareFrame's big sibling: per-frame data too large for a push constant block, staged
       to device local memory ahead of the first pass -- a frame property array feeding an
       expression, a table recomputed per frame. prepareFrameData runs once the source frames
       have arrived, so it can read their properties, and fills frameDataBytes bytes straight
       into the staging buffer; the driver copies that into a device local buffer bound through
       Operand::frameData() and fences the copy against the first dispatch. Both buffers are
       per frame and retire with the submission, so concurrent frames never share. Returning
       false fails the frame with the message. */
    uint32_t frameDataBytes = 0;
    std::function<bool(int n, const VSFrame *const *sources, int numSources,
        const VSAPI *vsapi, void *data, std::string &error)> prepareFrameData;

    /* A filter that computes something about frames rather than new planes -- PlaneStats
       -- declares itself a side effect: every plane is shared through untouched, and the
       pass list runs once per frame (as plane 0) instead of once per processed plane.
       Operand::output() has no meaning then; the passes read explicitly chosen source
       planes and write the readback buffer. Scratch needs explicit scratchDefs sizes in
       this mode, since there is no processed plane to size from. */
    bool sideEffect = false;
    /* When nonzero, every frame gets a host visible buffer of this many bytes bound through
       Operand::readback(). After submitting, the driver waits for exactly this frame's
       submission on the pool timeline -- concurrent frames keep flowing -- and hands the
       mapped bytes to finishReadback, which finishes the reduction on the host and writes
       frame properties. The one shape that blocks in getFrame; filters that only produce
       planes never need it, their producer pairs carrying the synchronization. */
    uint32_t readbackBytes = 0;
    std::function<void(int n, VSFrame *dst, const void *data, const uint32_t *params,
        VSCore *core, const VSAPI *vsapi)> finishReadback;

    /* Pointer-ABI support. When set, the driver's scratch, constant, readback and frame data
       buffers carry VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT and every pass sees its
       operands' device addresses in PassInfo::addresses, for fillPush to place into push
       constants for GL_EXT_buffer_reference kernels. Descriptor writes still happen and a
       kernel may ignore them; frame plane operands report zero, so pointer kernels stage
       planes through address-enabled scratch. This is the ABI a fused chain wants, its operand
       set varying per composition where descriptor layout permutations would not scale. */
    bool operandAddresses = false;
};

namespace detail {

struct Instance {
    FilterDesc desc;
    const VSVULKANAPI *vkapi = nullptr;
    const VSVulkanFunctions *vk = nullptr;
    VSVulkanCoreHandles handles = {};
    VSGPUExecPool *pool = nullptr;
    /* The pool's timeline, kept raw for the per submission readback waits; the pool owns
       it and outlives every use here. */
    VkSemaphore poolTimelineSem = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<VkPipelineLayout> pipeLayouts;
    std::vector<VkPipeline> pipelines;
    std::vector<VSGPUBuffer *> constantBuffers;
    std::vector<VSVulkanBufferInfo> constantInfo;

    ~Instance() {
        if (!vk)
            return;
        /* The pool drains the device before it returns, so anything a submission was
           still reading is safe to destroy only after this point. */
        if (pool)
            vkapi->freeGPUExecPool(pool);
        for (VSGPUBuffer *b : constantBuffers)
            vkapi->destroyGPUBuffer(b);
        for (size_t i = 0; i < pipelines.size(); i++) {
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
    /* The write bit makes the barrier sound for write-after-write pass pairs. No filter
       written by hand has two passes writing the same bytes today, but a generated pass
       chain -- fusion emitting ping-pong or accumulation steps -- cannot promise that,
       and the wider second scope costs nothing measurable on a barrier already there. */
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
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
        if (!desc.nodes.empty())
            vsapi->requestFrameFilter(sourceIndex(0, desc.refOffset), desc.nodes[0], frameCtx);
        /* A clip only named as a share source has no binding to be picked up above. */
        for (int p = 0; p < 3 && p < desc.vi.format.numPlanes; p++) {
            if (!desc.process[p])
                vsapi->requestFrameFilter(sourceIndex(desc.shareClip[p], desc.refOffset), desc.nodes[desc.shareClip[p]], frameCtx);
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

    const VSFrame *first = desc.nodes.empty() ? nullptr : fetch(0, desc.refOffset);
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
            planeSrc[p] = desc.process[p] ? nullptr : fetch(desc.shareClip[p], desc.refOffset);
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
       and submitting an empty command buffer would only cost a round trip. A side effect
       filter's planes all share too, but running its passes is the whole point. */
    if (!processAny && !desc.sideEffect) {
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

    /* Scratch is sized for the largest processed plane unless its slot says otherwise,
       and lives for one submission. */
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
            VkDeviceSize bytes = maxBytes;
            VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (i < static_cast<int>(desc.scratchDefs.size())) {
                if (desc.scratchDefs[i].bytes)
                    bytes = desc.scratchDefs[i].bytes;
                usage |= desc.scratchDefs[i].extraUsage;
            }
            if (desc.operandAddresses)
                usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            VSGPUBuffer *buffer = inst->vkapi->createGPUBuffer(core, bytes, usage,
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

    /* The per-frame readback buffer, when the filter asked for one. Deliberately not
       handed to the context: it must outlive the submission for the host to read it, so
       it is destroyed below once finishReadback has run. */
    VSVulkanBufferInfo readbackInfo = {};
    VSGPUBuffer *readbackBuffer = nullptr;
    if (desc.readbackBytes > 0) {
        readbackBuffer = inst->vkapi->createGPUBuffer(core, desc.readbackBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                (desc.operandAddresses ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0),
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &readbackInfo, err, sizeof(err));
        if (!readbackBuffer) {
            vsapi->setFilterError((std::string("GPU filter: ") + err).c_str(), frameCtx);
            inst->vkapi->gpuExecAbandon(ctx);
            releaseSources();
            vsapi->freeFrame(dst);
            return nullptr;
        }
    }

    /* The per-frame data pair: prepareFrameData fills host visible staging, a copy at the
       top of the command buffer moves it device local -- kernels read it per pixel, which
       over PCIe would cost more than the data is worth -- and both buffers are handed to
       the context, retiring with the submission. */
    VSVulkanBufferInfo frameDataInfo = {}, frameDataStaging = {};
    if (desc.frameDataBytes > 0) {
        auto frameDataFail = [&](const char *message) -> const VSFrame * {
            vsapi->setFilterError(message, frameCtx);
            inst->vkapi->gpuExecAbandon(ctx);
            if (readbackBuffer)
                inst->vkapi->destroyGPUBuffer(readbackBuffer);
            releaseSources();
            vsapi->freeFrame(dst);
            return nullptr;
        };
        VSGPUBuffer *device = inst->vkapi->createGPUBuffer(core, desc.frameDataBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                (desc.operandAddresses ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &frameDataInfo, err, sizeof(err));
        if (!device)
            return frameDataFail((std::string("GPU filter: ") + err).c_str());
        inst->vkapi->gpuExecUsesBuffer(ctx, device);
        VSGPUBuffer *staging = inst->vkapi->createGPUBuffer(core, desc.frameDataBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
            &frameDataStaging, err, sizeof(err));
        if (!staging)
            return frameDataFail((std::string("GPU filter: ") + err).c_str());
        inst->vkapi->gpuExecUsesBuffer(ctx, staging);
        std::string prepareError;
        if (!desc.prepareFrameData(n, sourceFrames.data(), static_cast<int>(sourceFrames.size()),
                vsapi, frameDataStaging.mapped, prepareError))
            return frameDataFail(prepareError.c_str());
    }

    VkCommandBuffer cmd = inst->vkapi->gpuExecCommandBuffer(ctx);
    if (desc.frameDataBytes > 0) {
        VkBufferCopy2 region = {};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        region.size = desc.frameDataBytes;
        VkCopyBufferInfo2 copyInfo = {};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copyInfo.srcBuffer = frameDataStaging.buffer;
        copyInfo.dstBuffer = frameDataInfo.buffer;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        inst->vk->vkCmdCopyBuffer2(cmd, &copyInfo);
        /* The inter-pass barriers are compute to compute; the copy needs its own fence
           ahead of the first dispatch that reads it. */
        VkMemoryBarrier2 mb = {};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        inst->vk->vkCmdPipelineBarrier2(cmd, &dep);
    }
    bool firstDispatch = true;
    /* Local, not a member: this node runs fmParallel, so concurrent frames would otherwise
       fill the same buffer and race. Harmless for a filter whose push constants are the
       same every frame, which is why it took one whose parameters vary per frame to show. */
    std::vector<uint8_t> pushScratch;

    /* A side effect filter records its pass list once, as plane 0; everything else
       records it per processed plane. */
    const int planeCount = desc.sideEffect ? 1 : fmt->numPlanes;
    for (int p = 0; p < planeCount; p++) {
        if (!desc.sideEffect && !desc.process[p])
            continue;
        VSVulkanPlaneInfo dstPlane = {};
        uint32_t dstStrideElems = 0;
        if (!desc.sideEffect) {
            inst->vkapi->gpuExecWritesPlane(ctx, dst, p);

            if (inst->vkapi->getGPUPlane(dst, p, &dstPlane)) {
                vsapi->setFilterError("GPU filter: output frame is not GPU resident", frameCtx);
                inst->vkapi->gpuExecAbandon(ctx);
                if (readbackBuffer)
                    inst->vkapi->destroyGPUBuffer(readbackBuffer);
                releaseSources();
                vsapi->freeFrame(dst);
                return nullptr;
            }

            /* Scratch has no stride of its own: it is allocated to hold the widest
               processed plane, so every pass addresses it with that plane's output
               stride. */
            dstStrideElems = static_cast<uint32_t>(vsapi->getStride(dst, p) / fmt->bytesPerSample);
        }

        for (size_t i = 0; i < desc.passes.size(); i++) {
            const Pass &pass = desc.passes[i];
            if (!pass.planes[p])
                continue;
            const Program &prog = desc.programs[pass.program];

            PassInfo info;
            info.plane = p;
            info.pass = static_cast<int>(i);
            info.bindingCount = static_cast<int>(pass.bindings.size());

            VkDescriptorBufferInfo bufferInfo[maxBindings] = {};
            VkWriteDescriptorSet writes[maxBindings] = {};
            std::string bindError;
            for (size_t b = 0; b < pass.bindings.size(); b++) {
                const Operand &op = pass.bindings[b];
                VkBuffer buffer = VK_NULL_HANDLE;
                uint32_t strideElems = dstStrideElems;
                if (op.kind == Operand::SourcePlane) {
                    const VSFrame *srcFrame = fetch(op.clip, op.frameOffset);
                    const int srcPlane = op.plane >= 0 ? op.plane : p;
                    VSVulkanPlaneInfo planeInfo;
                    /* Reported rather than bound: a null VkBuffer in a descriptor is
                       undefined behaviour that surfaces as a device loss somewhere else
                       entirely, so the two ways to get one -- a source that turned out not
                       to be resident, and a plane index the source does not have -- say so
                       here instead. */
                    if (inst->vkapi->getGPUPlane(srcFrame, srcPlane, &planeInfo)) {
                        bindError = "GPU filter: clip " + std::to_string(op.clip) +
                            " has no GPU resident plane " + std::to_string(srcPlane);
                        break;
                    }
                    buffer = planeInfo.buffer;
                    /* Each source is measured in its own sample size, not the output's:
                       Expr accepts clips whose format differs from what it writes, and
                       dividing an 8 bit source's stride by a 16 bit output would halve it. */
                    const VSVideoFormat *srcFmt = vsapi->getVideoFrameFormat(srcFrame);
                    strideElems = static_cast<uint32_t>(vsapi->getStride(srcFrame, srcPlane) / srcFmt->bytesPerSample);
                } else if (op.kind == Operand::OutputPlane) {
                    buffer = dstPlane.buffer;
                } else if (op.kind == Operand::Constant) {
                    buffer = inst->constantInfo[op.slot].buffer;
                    info.addresses[b] = inst->constantInfo[op.slot].address;
                } else if (op.kind == Operand::Readback) {
                    buffer = readbackInfo.buffer;
                    info.addresses[b] = readbackInfo.address;
                } else if (op.kind == Operand::FrameData) {
                    buffer = frameDataInfo.buffer;
                    info.addresses[b] = frameDataInfo.address;
                } else {
                    buffer = scratch[op.slot].buffer;
                    info.addresses[b] = scratch[op.slot].address;
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
            if (!bindError.empty()) {
                vsapi->setFilterError(bindError.c_str(), frameCtx);
                inst->vkapi->gpuExecAbandon(ctx);
                if (readbackBuffer)
                    inst->vkapi->destroyGPUBuffer(readbackBuffer);
                releaseSources();
                vsapi->freeFrame(dst);
                return nullptr;
            }

            if (!firstDispatch && !pass.independent)
                barrier(inst->vk, cmd);
            firstDispatch = false;

            inst->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, inst->pipelines[pass.program]);
            inst->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, inst->pipeLayouts[pass.program],
                0, static_cast<uint32_t>(pass.bindings.size()), writes);

            const VSFrame *geometry = dst;
            int geometryPlane = p;
            if (pass.geometryFromBinding >= 0 && pass.geometryFromBinding < static_cast<int>(pass.bindings.size())) {
                const Operand &g = pass.bindings[pass.geometryFromBinding];
                if (g.kind == Operand::SourcePlane) {
                    geometry = fetch(g.clip, g.frameOffset);
                    /* A binding that names its plane is measured on that plane, or the
                       geometry would come from one the pass never reads. */
                    geometryPlane = g.plane >= 0 ? g.plane : p;
                }
            }
            info.width = static_cast<uint32_t>(vsapi->getFrameWidth(geometry, geometryPlane));
            info.height = static_cast<uint32_t>(vsapi->getFrameHeight(geometry, geometryPlane));
            info.srcWidth = static_cast<uint32_t>(vsapi->getFrameWidth(first ? first : dst, p));
            info.srcHeight = static_cast<uint32_t>(vsapi->getFrameHeight(first ? first : dst, p));
            info.frameParams = frameParamData;
            if (pass.reshape)
                pass.reshape(info);

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

    uint64_t signaled = 0;
    if (inst->vkapi->gpuExecSubmit(ctx, readbackBuffer ? &signaled : nullptr, err, sizeof(err))) {
        vsapi->setFilterError((std::string("GPU filter: ") + err).c_str(), frameCtx);
        if (readbackBuffer)
            inst->vkapi->destroyGPUBuffer(readbackBuffer);
        releaseSources();
        vsapi->freeFrame(dst);
        return nullptr;
    }

    if (readbackBuffer) {
        /* Wait for exactly this frame's submission on the pool timeline -- concurrent
           frames keep their own submissions flowing -- then let the filter finish the
           reduction on the host and write its properties. */
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &inst->poolTimelineSem;
        waitInfo.pValues = &signaled;
        if (inst->vk->vkWaitSemaphores(inst->handles.device, &waitInfo, UINT64_MAX) != VK_SUCCESS) {
            vsapi->setFilterError("GPU filter: waiting for the readback failed", frameCtx);
            inst->vkapi->destroyGPUBuffer(readbackBuffer);
            releaseSources();
            vsapi->freeFrame(dst);
            return nullptr;
        }
        desc.finishReadback(n, dst, readbackInfo.mapped, frameParamData, core, vsapi);
        inst->vkapi->destroyGPUBuffer(readbackBuffer);
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

    /* Kernels compile from desc.vi's format at create and frames are allocated at its
       dimensions, so a variable clip has nothing to build from -- cfUndefined format, zero
       width and height. Checked here rather than per filter because a filter that forgets
       does not get a wrong answer: it gets a kernel compiled for an undefined sample type, or
       a zero sized frame allocation, i.e. an access violation or a fatal error. */
    if (desc.vi.format.colorFamily == cfUndefined || desc.vi.width <= 0 || desc.vi.height <= 0)
        return fail("the GPU path needs a clip with constant format and dimensions; "
            "insert GPUDownload to process a variable clip on the CPU");

    /* The frame loop builds its descriptor writes in fixed size stack arrays and indexes
       scratch, constants and nodes with whatever the operands say, none of it checked once
       the frames are flowing. This is the one place a filter that declared something
       impossible can be turned away with a message. AverageFrames sits exactly on the
       binding limit at 31 taps plus its output, so the bound is real, not theoretical. */
    for (const Program &prog : desc.programs) {
        if (prog.storageBufferCount < 1 || prog.storageBufferCount > maxBindings)
            return fail("a program must declare between 1 and " + std::to_string(maxBindings) +
                " storage buffers, not " + std::to_string(prog.storageBufferCount));
    }
    for (const Pass &pass : desc.passes) {
        if (pass.program < 0 || pass.program >= static_cast<int>(desc.programs.size()))
            return fail("a pass names program " + std::to_string(pass.program) + ", which does not exist");
        if (pass.bindings.empty() || pass.bindings.size() > static_cast<size_t>(maxBindings))
            return fail("a pass must bind between 1 and " + std::to_string(maxBindings) + " operands");
        if (pass.bindings.size() > static_cast<size_t>(desc.programs[pass.program].storageBufferCount))
            return fail("a pass binds more operands than its program declares storage buffers");
        for (const Operand &op : pass.bindings) {
            if (op.kind == Operand::SourcePlane &&
                    (op.clip < 0 || op.clip >= static_cast<int>(desc.nodes.size())))
                return fail("a binding reads clip " + std::to_string(op.clip) + ", which was not given");
            if (op.kind == Operand::Scratch && (op.slot < 0 || op.slot >= desc.scratchCount))
                return fail("a binding names scratch buffer " + std::to_string(op.slot) +
                    ", but only " + std::to_string(desc.scratchCount) + " were requested");
            if (op.kind == Operand::Constant &&
                    (op.slot < 0 || op.slot >= static_cast<int>(desc.constants.size())))
                return fail("a binding names constant buffer " + std::to_string(op.slot) +
                    ", which does not exist");
            if (op.kind == Operand::Readback && desc.readbackBytes == 0)
                return fail("a binding names the readback buffer, but readbackBytes is zero");
            if (op.kind == Operand::FrameData && desc.frameDataBytes == 0)
                return fail("a binding names the frame data buffer, but frameDataBytes is zero");
            if (op.kind == Operand::OutputPlane && desc.sideEffect)
                return fail("a side effect filter has no output planes to bind");
        }
    }
    if (desc.readbackBytes > 0 && !desc.finishReadback)
        return fail("readbackBytes without finishReadback reads back to nowhere");
    if (desc.frameDataBytes > 0 && !desc.prepareFrameData)
        return fail("frameDataBytes without prepareFrameData has nothing to fill the buffer");
    if (desc.sideEffect) {
        for (int p = 0; p < 3; p++)
            if (desc.process[p])
                return fail("a side effect filter processes no planes; every plane is shared");
        for (int i = 0; i < desc.scratchCount; i++)
            if (i >= static_cast<int>(desc.scratchDefs.size()) || desc.scratchDefs[i].bytes == 0)
                return fail("a side effect filter has no processed plane to size scratch from; give scratch buffer " +
                    std::to_string(i) + " an explicit size");
    }
    for (int p = 0; p < 3 && p < desc.vi.format.numPlanes; p++) {
        if (!desc.process[p] && (desc.shareClip[p] < 0 || desc.shareClip[p] >= static_cast<int>(desc.nodes.size())))
            return fail("an unprocessed plane shares from a clip that was not given");
        if (desc.process[p]) {
            bool covered = false;
            for (const Pass &pass : desc.passes)
                covered = covered || pass.planes[p];
            if (!covered)
                return fail("processed plane " + std::to_string(p) + " is not covered by any pass");
        }
    }
    char err[512] = { 0 };
    inst->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
    if (!inst->vkapi)
        return fail("the GPU API is not available");
    if (inst->vkapi->getVulkanHandles(core, &inst->handles, err, sizeof(err)))
        return fail(err);
    inst->vk = inst->vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!inst->vk)
        return fail(err);

    inst->setLayouts.resize(desc.programs.size(), VK_NULL_HANDLE);
    inst->pipeLayouts.resize(desc.programs.size(), VK_NULL_HANDLE);
    inst->pipelines.resize(desc.programs.size(), VK_NULL_HANDLE);
    for (size_t idx = 0; idx < desc.programs.size(); idx++) {
        const Program &prog = desc.programs[idx];

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

        VkDescriptorSetLayoutBinding bindings[maxBindings] = {};
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
        VkSpecializationInfo specInfo = {};
        if (!prog.specEntries.empty()) {
            specInfo.mapEntryCount = static_cast<uint32_t>(prog.specEntries.size());
            specInfo.pMapEntries = prog.specEntries.data();
            specInfo.dataSize = prog.specData.size();
            specInfo.pData = prog.specData.data();
            pipeInfo.stage.pSpecializationInfo = &specInfo;
        }
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo reqSg = {};
        if (prog.requiredSubgroupSize) {
            reqSg.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
            reqSg.requiredSubgroupSize = prog.requiredSubgroupSize;
            reqSg.pNext = pipeInfo.stage.pNext;
            pipeInfo.stage.pNext = &reqSg;
        }
        if (prog.requireFullSubgroups)
            pipeInfo.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
        if (inst->vk->vkCreateComputePipelines(inst->handles.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &inst->pipelines[idx]) != VK_SUCCESS)
            return fail("compute pipeline creation failed");
    }

    inst->pool = inst->vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!inst->pool)
        return fail(err);
    inst->poolTimelineSem = inst->vkapi->getGPUTimelineSemaphore(inst->vkapi->gpuExecPoolTimeline(inst->pool));

    /* Constants are staged and copied once, here, so every frame afterwards reads device
       local memory. The staging buffer is handed to the context, which destroys it when the
       copy retires; the device local one belongs to the instance and outlives every
       submission, which the pool guarantees by draining before it is freed. */
    for (const std::vector<uint8_t> &blob : desc.constants) {
        if (blob.empty())
            return fail("a constant buffer cannot be empty");

        VSVulkanBufferInfo deviceInfo = {}, stagingInfo = {};
        VSGPUBuffer *device = inst->vkapi->createGPUBuffer(core, blob.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                (desc.operandAddresses ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &deviceInfo, err, sizeof(err));
        if (!device)
            return fail(err);
        inst->constantBuffers.push_back(device);
        inst->constantInfo.push_back(deviceInfo);

        VSGPUBuffer *staging = inst->vkapi->createGPUBuffer(core, blob.size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
            &stagingInfo, err, sizeof(err));
        if (!staging)
            return fail(err);
        std::memcpy(stagingInfo.mapped, blob.data(), blob.size());

        VSGPUExecContext *ctx = inst->vkapi->gpuExecAcquire(inst->pool, err, sizeof(err));
        if (!ctx) {
            inst->vkapi->destroyGPUBuffer(staging);
            return fail(err);
        }
        inst->vkapi->gpuExecUsesBuffer(ctx, staging);
        VkBufferCopy2 region = {};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        region.size = blob.size();
        VkCopyBufferInfo2 copyInfo = {};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copyInfo.srcBuffer = stagingInfo.buffer;
        copyInfo.dstBuffer = deviceInfo.buffer;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        inst->vk->vkCmdCopyBuffer2(inst->vkapi->gpuExecCommandBuffer(ctx), &copyInfo);
        if (inst->vkapi->gpuExecSubmit(ctx, nullptr, err, sizeof(err)))
            return fail(err);
    }
    /* The first frame must not race the upload, and there is no producer pair on a buffer
       to wait on, so this is the one place the driver blocks. */
    if (!desc.constants.empty() && inst->vkapi->gpuExecPoolWaitIdle(inst->pool, err, sizeof(err)))
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

   Nearly every pixel filter is one dispatch per processed plane, one output, one to three
   sources read at the same coordinate or a small neighbourhood around it, and a few scalar
   parameters varying per plane. Through FilterDesc that means repeating a prologue, a bounds
   check and a push constant struct in every filter, so this layer supplies all three.

   A filter gives one GLSL statement block for integer formats and one for float, mirroring
   the CPU side's processPlane/processPlaneF split, plus a callback filling a plane's parameter
   block. Format specialization, bindings, geometry and edge clamping helpers come from here.

   Filters that do not fit -- multiple passes, scratch, geometry changes, their own descriptor
   layout -- drop to FilterDesc directly; the two compose, since this only builds one. */

constexpr int simpleMaxInputs = 3;
constexpr int simpleFloatParams = 32; /* enough for a 5x5 convolution matrix */
constexpr int simpleUintParams = 8;
/* Emitted into the kernel and handed to Program, so the declared workgroup and the dispatch
   arithmetic always come from the same number. */
constexpr uint32_t simpleLocalSize = 16;

struct SimplePush {
    uint32_t width, height;         /* of the output plane, which is what is dispatched over */
    uint32_t srcWidth, srcHeight;   /* of the reference clip's plane; differs once geometry changes */
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
    /* Set when the sources are not in the output's format -- Lut writing a different depth,
       MakeFullDiff widening by a bit. srcFormat covers every input; srcFormats overrides it
       per input, which MergeFullDiff needs since it takes a narrow clip and a wide diff.
       The destination keeps the output format either way. */
    const VSVideoFormat *srcFormat = nullptr;
    const VSVideoFormat *srcFormats[simpleMaxInputs] = { nullptr, nullptr, nullptr };
    /* Which plane of each input to read; -1, the default, follows the plane being produced.
       A grayscale mask or alpha applied to every plane of the output names 0. Only legal
       while that plane is the size of the plane being written -- see Operand::plane. */
    int srcPlane[simpleMaxInputs] = { -1, -1, -1 };
    /* Uploaded once at create and bound after the output, readable as lut0, lut1, ... */
    std::vector<std::vector<uint8_t>> constants;
    /* The element type the constants are read as; defaults to the output's. */
    const char *constantType = nullptr;
    bool process[3] = { true, true, true };
    int shareClip[3] = { 0, 0, 0 }; /* source for planes this filter does not compute */
    /* Fills the per plane parameter block. Called once per plane per frame. */
    std::function<void(int plane, float *f, uint32_t *u)> fill;
    /* Optional host side frame property fixup; see FilterDesc::finishFrame. */
    std::function<void(int n, VSFrame *dst, const VSFrame *const *sources, int numSources,
        const uint32_t *params, VSCore *core, const VSAPI *vsapi)> finishFrame;

    /* What the core is told about which source frames this filter asks for, applied to every
       input. The default suits the pointwise and neighbourhood filters that are most of the
       tree; a filter setting mapFrame reads an index other than n and must say so, since
       rpStrictSpatial on a node's only consumer switches that node's cache off, and claiming
       it falsely reruns the whole upstream for every output frame that revisits a source.
       Match what the filter's scalar path declares. */
    int requestPattern = rpStrictSpatial;

    /* Optional source frame index mapping and per frame parameters; see FilterDesc. When
       prepare is set, fillFrame runs instead of fill so the body can reach what it produced. */
    std::function<int(int n, int clip, int frameOffset)> mapFrame;
    int prepareParams = 0;
    std::function<bool(int n, const VSFrame *const *sources, int numSources,
        const VSAPI *vsapi, uint32_t *params, std::string &error)> prepare;
    std::function<void(int plane, const uint32_t *params, float *f, uint32_t *u)> fillFrame;
};

namespace detail {

inline const char *sampleTypeName(const VSVideoFormat &f) {
    if (f.sampleType == stFloat)
        return f.bytesPerSample == 2 ? "float16_t" : "float";
    /* Four byte integers are not a sample format anyone stores video in, but MakeFullDiff
       produces one: 16 bit input widens to 17, which no longer fits two bytes. */
    if (f.bytesPerSample == 4)
        return "uint";
    return f.bytesPerSample == 1 ? "uint8_t" : "uint16_t";
}

inline std::string simpleSource(const SimpleFilter &sf, const VSVideoFormat &fmt) {
    const bool isFloat = fmt.sampleType == stFloat;
    /* The sources may be in a different format than the output; SRC_T covers them and
       SAMPLE_T the destination, which are the same type unless a filter says otherwise. */
    const VSVideoFormat &srcFmt = sf.srcFormat ? *sf.srcFormat : fmt;
    const bool isHalf = (isFloat && fmt.bytesPerSample == 2) ||
                        (srcFmt.sampleType == stFloat && srcFmt.bytesPerSample == 2);

    /* One place decides how a format is spelled, for the destination as much as for the
       sources: a second copy of this chain is how a four byte integer output silently came
       out declared as uint16_t. */
    std::string s = "#version 460\n";
    s += std::string("#define SAMPLE_T ") + sampleTypeName(fmt) + "\n";
    for (int i = 0; i < sf.inputs; i++) {
        const VSVideoFormat &f = sf.srcFormats[i] ? *sf.srcFormats[i] : srcFmt;
        s += "#define SRC" + std::to_string(i) + "_T " + sampleTypeName(f) + "\n";
    }
    s += std::string("#define LUT_T ") + (sf.constantType ? sf.constantType : sampleTypeName(fmt)) + "\n";

    s += "#extension GL_EXT_shader_8bit_storage : require\n"
         "#extension GL_EXT_shader_16bit_storage : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n";
    if (isHalf)
        s += "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";

    s += "\nlayout(local_size_x = " + std::to_string(simpleLocalSize) +
         ", local_size_y = " + std::to_string(simpleLocalSize) + ") in;\n\n"
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
             " { SRC" + n + "_T s" + n + "[]; };\n";
    }
    /* Half output rounds however the device rounds. SPIR-V leaves 32 to 16 bit FConvert
       implementation defined, and this driver truncates toward zero where floatToHalf and the
       JIT's vcvtps2ph round to nearest even, so ~41% of samples land one ulp low. Pinning it
       would mean RoundingModeRTE through VK_KHR_shader_float_controls or narrowing by hand,
       neither worth it for a format whose last bit does not matter. packHalf2x16 is not the
       shortcut it looks like: GLSL calls it round to nearest even, but it lowers to
       V_CVT_PKRTZ_F16_F32, round toward zero. */
    s += "layout(std430, set = 0, binding = " + std::to_string(sf.inputs) +
         ") writeonly buffer Dst { SAMPLE_T dstData[]; };\n";
    for (size_t i = 0; i < sf.constants.size(); i++) {
        const std::string ci = std::to_string(i);
        s += "layout(std430, set = 0, binding = " + std::to_string(sf.inputs + 1 + static_cast<int>(i)) +
             ") readonly buffer Lut" + ci + " { LUT_T lut" + ci + "[]; };\n";
    }
    s += "\n";

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
         /* Vulkan specifies sqrt to 3 ulp where the scalar paths this is checked against get a
            correctly rounded SQRTSS. One Newton step recovers the difference: fma computes the
            residual s - y*y exactly, so the correction is good to well under an ulp of y. Two
            flops on top of a square root, and unlike an fp64 root it asks nothing of the
            device, so it is the default. */
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
    program.storageBufferCount = sf.inputs + 1 + static_cast<int>(sf.constants.size());
    program.pushConstantBytes = sizeof(SimplePush);
    program.localSizeX = simpleLocalSize;
    program.localSizeY = simpleLocalSize;
    desc.programs.push_back(std::move(program));

    /* Constants bind after the output, matching the order simpleSource declares them in.
       They are last so srcStrideElements and dstStrideElements keep meaning what they did. */
    desc.constants = sf.constants;
    Pass pass;
    for (int i = 0; i < sf.inputs; i++)
        pass.bindings.push_back(Operand::sourcePlane(sf.srcPlane[i], i));
    pass.bindings.push_back(Operand::output());
    for (size_t i = 0; i < sf.constants.size(); i++)
        pass.bindings.push_back(Operand::constant(static_cast<int>(i)));
    desc.passes.push_back(std::move(pass));

    desc.finishFrame = sf.finishFrame;
    desc.mapFrame = sf.mapFrame;
    desc.prepareFrame = sf.prepare;
    desc.frameParamCount = sf.prepareParams;

    const auto fill = sf.fill;
    const auto fillFrame = sf.fillFrame;
    /* The output is at this binding, not at the end: constants follow it. */
    const int dstBinding = sf.inputs;
    desc.fillPush = [fill, fillFrame, dstBinding](const PassInfo &info, void *pushData) {
        SimplePush push = {};
        push.width = info.width;
        push.height = info.height;
        push.srcWidth = info.srcWidth;
        push.srcHeight = info.srcHeight;
        for (int i = 0; i < simpleMaxInputs && i < dstBinding; i++)
            push.srcStride[i] = info.strideElements[i];
        push.dstStride = info.strideElements[dstBinding];
        if (fillFrame)
            fillFrame(info.plane, info.frameParams, push.f, push.u);
        else if (fill)
            fill(info.plane, push.f, push.u);
        std::memcpy(pushData, &push, sizeof(push));
    };

    std::vector<VSFilterDependency> deps;
    for (int i = 0; i < numNodes; i++) {
        /* A secondary clip shorter than the output is legal and the driver clamps its requests
           to the last frame, which is frame reuse rather than strict spatial access. Declaring
           strict anyway would disable the upstream cache -- one consumer claiming it is enough
           -- and recompute that clamped last frame for every output past the short clip's end.
           Derive the pattern per clip, as the scalar creation sites do. */
        int pattern = sf.requestPattern;
        if (pattern == rpStrictSpatial && vi->numFrames > vsapi->getVideoInfo(nodes[i])->numFrames)
            pattern = rpFrameReuseLastOnly;
        deps.push_back({ nodes[i], pattern });
    }
    return createFilter(sf.name, desc, deps.data(), numNodes, core, vsapi, errorMessage);
}

} // namespace vsgpu

#endif
