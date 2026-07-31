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

/* std.GPUBoxBlur: same arguments and bit identical integer output to std.BoxBlur, but all
   planes and both directions in one node with no transposes, since the kernel takes its
   direction as a push constant.

   Deliberately written against nothing but the public headers — VapourSynth4.h for the
   filter surface and VSVulkan4.h for the GPU one — even though it is compiled into the
   core. Keeping it in its own translation unit is what makes that a guarantee rather than
   a promise: none of the internal Vulkan classes are even in scope here, so this filter
   can only do what an out of tree plugin can do, and it stays an honest reference for one.
   The machinery it therefore owns itself — the pipeline, a timeline, a command buffer ring
   and the retained references keeping sources alive until the GPU is done with them — is
   exactly the machinery any GPU filter owns; see sdk/gpu_invert_example.c for the same
   pattern in C. */

#include "internalfilters.h"
#include "VSVulkan4.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/* Frames in flight per instance: each needs its own command buffer, and the ring is the
   backpressure when more threads call than the GPU keeps up with. */
constexpr int cmdSlots = 4;

struct BlurPush {
    uint32_t width;
    uint32_t height;
    uint32_t srcStride; /* in elements */
    uint32_t dstStride;
    uint32_t radius;
    uint32_t rounding; /* integer variants only */
    uint32_t vertical;
    float invDiv;      /* float variants only */
};

/* One box blur pass over one plane, horizontal or vertical by push constant. Integer
   variants match the CPU filter's math exactly: clamped edges and (sum + rounding)/(2r+1),
   with the caller alternating the rounding term between passes the same way the CPU code
   does. Float variants accumulate in float and multiply by 1/(2r+1); they cannot be bit
   exact against the CPU's running sum, which rounds differently, so they are verified with
   a tolerance instead.

   Compiled at filter creation through compileGPUShader, specialized by a preamble carrying
   SAMPLE_T in {uint8_t, uint16_t} and, with FLOAT_SAMPLES, {float, float16_t}. The preamble
   also supplies #version, which the language demands be the very first token, so the body
   here starts at the extension list. The core caches by source text, so the four variants
   parse once each however many instances a script builds. */
const char boxBlurGlsl[] =
    "#extension GL_EXT_shader_8bit_storage : require\n"
    "#extension GL_EXT_shader_16bit_storage : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n"
    "#ifdef FLOAT_SAMPLES\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n"
    "#endif\n"
    "\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "\n"
    "layout(std430, set = 0, binding = 0) readonly buffer Src { SAMPLE_T srcData[]; };\n"
    "layout(std430, set = 0, binding = 1) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
    "\n"
    "layout(push_constant, std430) uniform Push {\n"
    "    uint width;\n"
    "    uint height;\n"
    "    uint srcStride; /* in elements */\n"
    "    uint dstStride;\n"
    "    uint radius;\n"
    "    uint rounding;  /* integer variants only */\n"
    "    uint vertical;\n"
    "    float invDiv;   /* float variants only */\n"
    "} pc;\n"
    "\n"
    "void main() {\n"
    "    uint x = gl_GlobalInvocationID.x;\n"
    "    uint y = gl_GlobalInvocationID.y;\n"
    "    if (x >= pc.width || y >= pc.height)\n"
    "        return;\n"
    "\n"
    "    int len = int(pc.vertical != 0u ? pc.height : pc.width);\n"
    "    int pos = int(pc.vertical != 0u ? y : x);\n"
    "    int stepSize = int(pc.vertical != 0u ? pc.srcStride : 1u);\n"
    "    int base = int(y * pc.srcStride + x);\n"
    "    int r = int(pc.radius);\n"
    "\n"
    "#ifdef FLOAT_SAMPLES\n"
    "    float acc = 0.0;\n"
    "    for (int k = -r; k <= r; k++) {\n"
    "        int p = clamp(pos + k, 0, len - 1);\n"
    "        acc += float(srcData[uint(base + (p - pos) * stepSize)]);\n"
    "    }\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T(acc * pc.invDiv);\n"
    "#else\n"
    "    uint acc = 0u;\n"
    "    for (int k = -r; k <= r; k++) {\n"
    "        int p = clamp(pos + k, 0, len - 1);\n"
    "        acc += uint(srcData[uint(base + (p - pos) * stepSize)]);\n"
    "    }\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T((acc + pc.rounding) / (2u * pc.radius + 1u));\n"
    "#endif\n"
    "}\n";

struct GPUBoxBlurData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    bool process[3] = {};
    int hradius = 0, hpasses = 0, vradius = 0, vpasses = 0;

    VSCore *core = nullptr;
    const VSVULKANAPI *vkapi = nullptr;
    const VSVulkanFunctions *vk = nullptr; /* the core's dispatch table; everything goes through it */
    VSVulkanCoreHandles h = {};
    VkQueue computeQueue = VK_NULL_HANDLE; /* the handles carry family and index; the queue is fetched */

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd[cmdSlots] = {};
    uint64_t slotValue[cmdSlots] = {};
    int nextSlot = 0;

    /* This filter's own timeline: it signals rising values and publishes them as the
       producer pairs of the planes it writes, so consumers wait on the device instead of
       the host. It must outlive every consumer, so it lives and dies with the instance. */
    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t nextValue = 0;

    /* Source frames and scratch buffers whose lifetime has to reach past the call: the GPU
       is still reading them when getFrame returns. Released once the timeline says the
       submission that used them completed. */
    struct Retained {
        const VSFrame *frame;
        VSGPUBuffer *buffer;
        uint64_t value;
    };
    std::vector<Retained> retained;

    std::mutex lock; /* the instance runs fmParallel; this covers the rings and the values */
};

/* Frees everything the GPU has demonstrably finished with. Non blocking: the counter query
   is a plain read, and whatever is still pending simply stays for the next call. */
void sweepRetained(GPUBoxBlurData *d, const VSAPI *vsapi) {
    uint64_t completed = 0;
    if (d->vk->vkGetSemaphoreCounterValue(d->h.device, d->timeline, &completed) != VK_SUCCESS)
        return;
    size_t kept = 0;
    for (size_t i = 0; i < d->retained.size(); i++) {
        if (d->retained[i].value <= completed) {
            if (d->retained[i].frame)
                vsapi->freeFrame(d->retained[i].frame);
            if (d->retained[i].buffer)
                d->vkapi->destroyGPUBuffer(d->retained[i].buffer);
        } else {
            d->retained[kept++] = d->retained[i];
        }
    }
    d->retained.resize(kept);
}

void computeToComputeBarrier(const VSVulkanFunctions *vk, VkCommandBuffer cmd) {
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vk->vkCmdPipelineBarrier2(cmd, &dependency);
}

const VSFrame *VS_CC gpuBoxBlurGetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    } else if (activationReason != arAllFramesReady) {
        return nullptr;
    }

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
    int w = vsapi->getFrameWidth(src, 0);
    int h = vsapi->getFrameHeight(src, 0);

    /* Unprocessed planes are shared straight from the source, which keeps them on the
       device with their own producer pairs intact; when every plane is processed there is
       nothing to share and a plain GPU frame is what is wanted. */
    bool shareAny = false;
    for (int p = 0; p < fmt->numPlanes; p++)
        shareAny = shareAny || !d->process[p];

    VSFrame *dst;
    if (shareAny) {
        const VSFrame *planeSrc[3] = {};
        int planes[3] = { 0, 1, 2 };
        for (int p = 0; p < fmt->numPlanes; p++)
            planeSrc[p] = d->process[p] ? nullptr : src;
        dst = vsapi->newVideoFrame2(fmt, w, h, planeSrc, planes, src, core);
    } else {
        dst = d->vkapi->newGPUVideoFrame(fmt, w, h, src, core);
    }
    if (!dst) {
        vsapi->setFilterError("GPUBoxBlur: failed to allocate the output frame", frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    char err[512] = { 0 };
    std::lock_guard<std::mutex> instanceLock(d->lock);
    sweepRetained(d, vsapi);

    /* The ping pong scratch buffer, sized for the largest processed plane, needed as soon
       as any plane runs more than one pass. */
    int totalPasses = (d->hradius > 0 ? d->hpasses : 0) + (d->vradius > 0 ? d->vpasses : 0);
    VSGPUBuffer *tmp = nullptr;
    VkBuffer tmpBuffer = VK_NULL_HANDLE;
    if (totalPasses > 1) {
        VkDeviceSize maxBytes = 0;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (!d->process[p])
                continue;
            VSVulkanPlaneInfo info;
            if (d->vkapi->getGPUPlane(src, p, &info)) {
                vsapi->setFilterError("GPUBoxBlur: source frame is not GPU resident", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                return nullptr;
            }
            maxBytes = std::max(maxBytes, info.bufferSize);
        }
        VSVulkanBufferInfo tmpInfo = {};
        tmp = d->vkapi->createGPUBuffer(core, maxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &tmpInfo, err, sizeof(err));
        if (!tmp) {
            vsapi->setFilterError((std::string("GPUBoxBlur: ") + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
        tmpBuffer = tmpInfo.buffer;
    }

    /* Claim a command buffer slot and wait out whatever the GPU still owes it. The wait is
       instant except when this instance is already cmdSlots frames deep, which is the
       intended backpressure. */
    int slot = d->nextSlot;
    d->nextSlot = (d->nextSlot + 1) % cmdSlots;
    if (d->slotValue[slot]) {
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &d->timeline;
        waitInfo.pValues = &d->slotValue[slot];
        if (d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX) != VK_SUCCESS) {
            vsapi->setFilterError("GPUBoxBlur: waiting for a command buffer slot failed", frameCtx);
            if (tmp)
                d->vkapi->destroyGPUBuffer(tmp);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
    }

    VkCommandBuffer cmd = d->cmd[slot];
    d->vk->vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d->vk->vkBeginCommandBuffer(cmd, &begin);
    d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);

    /* Every plane this submission reads contributes its producer pair as a device side
       wait, deduplicated to the highest value per timeline the way the contract asks. */
    VkSemaphoreSubmitInfo waits[3] = {};
    uint32_t waitCount = 0;
    auto addWait = [&](VkSemaphore semaphore, uint64_t value) {
        if (!semaphore)
            return;
        for (uint32_t i = 0; i < waitCount; i++) {
            if (waits[i].semaphore == semaphore) {
                waits[i].value = std::max(waits[i].value, value);
                return;
            }
        }
        waits[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waits[waitCount].semaphore = semaphore;
        waits[waitCount].value = value;
        waits[waitCount].stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        waitCount++;
    };

    bool firstDispatch = true;
    for (int p = 0; p < fmt->numPlanes; p++) {
        if (!d->process[p])
            continue;

        VSVulkanPlaneInfo srcPlane, dstPlane;
        if (d->vkapi->getGPUPlane(src, p, &srcPlane) || d->vkapi->getGPUPlane(dst, p, &dstPlane)) {
            vsapi->setFilterError("GPUBoxBlur: frames are not GPU resident", frameCtx);
            d->vk->vkEndCommandBuffer(cmd);
            if (tmp)
                d->vkapi->destroyGPUBuffer(tmp);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
        addWait(srcPlane.readySemaphore, srcPlane.readyValue);

        uint32_t planeWidth = static_cast<uint32_t>(vsapi->getFrameWidth(src, p));
        uint32_t planeHeight = static_cast<uint32_t>(vsapi->getFrameHeight(src, p));
        uint32_t strideElems = static_cast<uint32_t>(vsapi->getStride(src, p) / fmt->bytesPerSample);

        /* The pass schedule replicates the CPU filter exactly: horizontal passes first with
           rounding div-1, 0, div-1, ... then vertical ones restarting the pattern with their
           own divisor. Float formats have no rounding term, just the reciprocal divisor. */
        struct Pass { uint32_t radius, rounding, vertical; float invDiv; };
        std::vector<Pass> schedule;
        if (d->hradius > 0) {
            for (int i = 0; i < d->hpasses; i++)
                schedule.push_back({ static_cast<uint32_t>(d->hradius), !(i & 1) ? 2u * d->hradius : 0u, 0,
                    1.0f / (2 * d->hradius + 1) });
        }
        if (d->vradius > 0) {
            for (int i = 0; i < d->vpasses; i++)
                schedule.push_back({ static_cast<uint32_t>(d->vradius), !(i & 1) ? 2u * d->vradius : 0u, 1,
                    1.0f / (2 * d->vradius + 1) });
        }

        VkBuffer srcBuf = srcPlane.buffer;
        const int passes = static_cast<int>(schedule.size());
        for (int i = 0; i < passes; i++) {
            /* Alternate so the final pass always lands in the destination plane. */
            VkBuffer target = ((passes - 1 - i) % 2 == 0) ? dstPlane.buffer : tmpBuffer;
            if (!firstDispatch)
                computeToComputeBarrier(d->vk, cmd);
            firstDispatch = false;

            VkDescriptorBufferInfo bufferInfo[2] = {};
            VkWriteDescriptorSet writes[2] = {};
            bufferInfo[0].buffer = srcBuf;
            bufferInfo[0].range = VK_WHOLE_SIZE;
            bufferInfo[1].buffer = target;
            bufferInfo[1].range = VK_WHOLE_SIZE;
            for (int b = 0; b < 2; b++) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstBinding = static_cast<uint32_t>(b);
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].pBufferInfo = &bufferInfo[b];
            }
            d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeLayout, 0, 2, writes);

            BlurPush push = {};
            push.width = planeWidth;
            push.height = planeHeight;
            push.srcStride = strideElems;
            push.dstStride = strideElems;
            push.radius = schedule[i].radius;
            push.rounding = schedule[i].rounding;
            push.vertical = schedule[i].vertical;
            push.invDiv = schedule[i].invDiv;
            VkPushConstantsInfo pushInfo = {};
            pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
            pushInfo.layout = d->pipeLayout;
            pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushInfo.size = sizeof(push);
            pushInfo.pValues = &push;
            d->vk->vkCmdPushConstants2(cmd, &pushInfo);

            d->vk->vkCmdDispatch(cmd, (planeWidth + 15) / 16, (planeHeight + 15) / 16, 1);
            srcBuf = target;
        }
    }

    d->vk->vkEndCommandBuffer(cmd);

    /* Value allocation and submission stay together under the queue lock, since timeline
       signal values must reach the queue in increasing order. */
    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo signal = {};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = d->timeline;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = waitCount;
    submit.pWaitSemaphoreInfos = waitCount ? waits : nullptr;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;

    uint64_t value;
    VkResult res;
    d->vkapi->lockVulkanQueue(core, vqCompute);
    value = d->nextValue + 1;
    signal.value = value;
    res = d->vk->vkQueueSubmit2(d->computeQueue, 1, &submit, VK_NULL_HANDLE);
    if (res == VK_SUCCESS)
        d->nextValue = value;
    d->vkapi->unlockVulkanQueue(core, vqCompute);

    if (res != VK_SUCCESS) {
        vsapi->setFilterError("GPUBoxBlur: vkQueueSubmit2 failed", frameCtx);
        if (tmp)
            d->vkapi->destroyGPUBuffer(tmp);
        vsapi->freeFrame(src);
        vsapi->freeFrame(dst);
        return nullptr;
    }

    d->slotValue[slot] = value;
    /* The source frame and the scratch buffer must outlive the submission reading them. */
    d->retained.push_back({ src, nullptr, value });
    if (tmp)
        d->retained.push_back({ nullptr, tmp, value });

    /* Only the planes this submission wrote get the new producer pair; shared planes keep
       the one they arrived with, which consumers wait on independently. */
    for (int p = 0; p < fmt->numPlanes; p++) {
        if (d->process[p])
            d->vkapi->setGPUPlaneProducer(dst, p, d->timeline, value);
    }

    return dst;
}

void VS_CC gpuBoxBlurFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);

    /* Everything below is destroyed while submissions may still reference it, so drain
       first; the timeline reaching the last value issued means the GPU is done. */
    if (d->timeline && d->nextValue) {
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &d->timeline;
        waitInfo.pValues = &d->nextValue;
        d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX);
    }
    for (const auto &r : d->retained) {
        if (r.frame)
            vsapi->freeFrame(r.frame);
        if (r.buffer)
            d->vkapi->destroyGPUBuffer(r.buffer);
    }

    if (d->cmdPool)
        d->vk->vkDestroyCommandPool(d->h.device, d->cmdPool, nullptr);
    if (d->pipeline)
        d->vk->vkDestroyPipeline(d->h.device, d->pipeline, nullptr);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, nullptr);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, nullptr);
    if (d->timeline)
        d->vk->vkDestroySemaphore(d->h.device, d->timeline, nullptr);

    if (d->node)
        vsapi->freeNode(d->node);
    delete d;
}

void VS_CC gpuBoxBlurCreate(const VSMap *in, VSMap *out, void *, VSCore *core, const VSAPI *vsapi) {
    auto d = std::make_unique<GPUBoxBlurData>();
    char err[512] = { 0 };

    try {
        int err2;
        d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
        d->core = core;
        const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);
        d->vi = *vi;

        if (vi->format.colorFamily == cfUndefined)
            throw std::runtime_error("clips with variable format are not supported");
        if (vi->format.bytesPerSample != 1 && vi->format.bytesPerSample != 2 && vi->format.bytesPerSample != 4)
            throw std::runtime_error("unsupported sample size");

        /* Same argument handling as std.BoxBlur so scripts can switch by changing the name. */
        for (int p = 0; p < 3; p++)
            d->process[p] = vi->format.numPlanes > p;
        int numPlaneArgs = vsapi->mapNumElements(in, "planes");
        if (numPlaneArgs > 0) {
            for (int p = 0; p < 3; p++)
                d->process[p] = false;
            for (int i = 0; i < numPlaneArgs; i++) {
                int plane = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);
                if (plane < 0 || plane >= vi->format.numPlanes)
                    throw std::runtime_error("plane index out of range");
                if (d->process[plane])
                    throw std::runtime_error("plane specified twice");
                d->process[plane] = true;
            }
        }

        d->hradius = vsapi->mapGetIntSaturated(in, "hradius", 0, &err2);
        if (err2)
            d->hradius = 1;
        d->hpasses = vsapi->mapGetIntSaturated(in, "hpasses", 0, &err2);
        if (err2)
            d->hpasses = 1;
        d->vradius = vsapi->mapGetIntSaturated(in, "vradius", 0, &err2);
        if (err2)
            d->vradius = 1;
        d->vpasses = vsapi->mapGetIntSaturated(in, "vpasses", 0, &err2);
        if (err2)
            d->vpasses = 1;

        if (d->hpasses < 0 || d->vpasses < 0)
            throw std::runtime_error("number of passes can't be negative");
        if (d->hradius < 0 || d->vradius < 0)
            throw std::runtime_error("radius can't be negative");
        if (d->hradius > 30000 || d->vradius > 30000)
            throw std::runtime_error("radius must be less than 30000");
        bool hblur = (d->hradius > 0) && (d->hpasses > 0);
        bool vblur = (d->vradius > 0) && (d->vpasses > 0);
        if (!hblur && !vblur)
            throw std::runtime_error("nothing to be performed");
        if (!hblur)
            d->hradius = 0;
        if (!vblur)
            d->vradius = 0;
        if (vi->width < 4 || vi->height < 4)
            throw std::runtime_error("dimensions must be at least 4x4");

        d->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
        if (!d->vkapi)
            throw std::runtime_error("the GPU API is not available");
        /* Brings the device up on first use, exactly as it does for a plugin. */
        if (d->vkapi->getVulkanHandles(core, &d->h, err, sizeof(err)))
            throw std::runtime_error(err);
        d->vk = d->vkapi->getVulkanFunctions(core, err, sizeof(err));
        if (!d->vk)
            throw std::runtime_error(err);

        VkDeviceQueueInfo2 queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
        queueInfo.queueFamilyIndex = d->h.computeQueueFamily;
        queueInfo.queueIndex = d->h.computeQueueIndex;
        d->vk->vkGetDeviceQueue2(d->h.device, &queueInfo, &d->computeQueue);

        /* Half precision samples need the optional feature; everything else this filter
           uses is in the required set. */
        VSVulkanCoreInfo coreInfo = {};
        if (d->vkapi->getVulkanCoreInfo(core, &coreInfo, err, sizeof(err)))
            throw std::runtime_error(err);

        std::string preamble = "#version 460\n";
        if (vi->format.sampleType == stInteger) {
            preamble += vi->format.bytesPerSample == 1 ? "#define SAMPLE_T uint8_t\n" : "#define SAMPLE_T uint16_t\n";
        } else if (vi->format.bytesPerSample == 4) {
            preamble += "#define SAMPLE_T float\n#define FLOAT_SAMPLES\n";
        } else {
            preamble += "#define SAMPLE_T float16_t\n#define FLOAT_SAMPLES\n";
        }

        VSGPUShader *shader = d->vkapi->compileGPUShader(core, slGLSL, (preamble + boxBlurGlsl).c_str(), err, sizeof(err));
        if (!shader) {
            /* A half precision kernel is the one variant a conformant device may refuse. */
            if (vi->format.sampleType == stFloat && vi->format.bytesPerSample == 2)
                throw std::runtime_error("half precision formats need the shaderFloat16 feature, which this device lacks");
            throw std::runtime_error(std::string("kernel failed to compile: ") + err);
        }
        size_t shaderBytes = 0;
        const uint32_t *shaderCode = d->vkapi->getGPUShaderCode(shader, &shaderBytes);

        VkDescriptorSetLayoutBinding bindings[2] = {};
        for (int b = 0; b < 2; b++) {
            bindings[b].binding = static_cast<uint32_t>(b);
            bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo setInfo = {};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
        setInfo.bindingCount = 2;
        setInfo.pBindings = bindings;
        if (d->vk->vkCreateDescriptorSetLayout(d->h.device, &setInfo, nullptr, &d->setLayout) != VK_SUCCESS) {
            d->vkapi->freeGPUShader(shader);
            throw std::runtime_error("descriptor set layout creation failed");
        }

        VkPushConstantRange range = {};
        range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        range.size = sizeof(BlurPush);
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &d->setLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &range;
        if (d->vk->vkCreatePipelineLayout(d->h.device, &layoutInfo, nullptr, &d->pipeLayout) != VK_SUCCESS) {
            d->vkapi->freeGPUShader(shader);
            throw std::runtime_error("pipeline layout creation failed");
        }

        /* maintenance5 is in the required feature set, so the SPIR-V rides along in the
           stage's pNext and no shader module object is needed. */
        VkShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = shaderBytes;
        moduleInfo.pCode = shaderCode;
        VkComputePipelineCreateInfo pipeInfo = {};
        pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.pNext = &moduleInfo;
        pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.pName = "main";
        pipeInfo.layout = d->pipeLayout;
        VkResult pipeRes = d->vk->vkCreateComputePipelines(d->h.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &d->pipeline);
        d->vkapi->freeGPUShader(shader); /* the pipeline owns the code now */
        if (pipeRes != VK_SUCCESS)
            throw std::runtime_error("compute pipeline creation failed");

        /* Created exportable when the device can, so foreign APIs consuming this filter's
           frames can import the producer pair instead of host waiting. */
        VkExportSemaphoreCreateInfo semExport = {};
        semExport.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        semExport.handleTypes = static_cast<VkExternalSemaphoreHandleTypeFlags>(coreInfo.semaphoreExportHandleType);
        VkSemaphoreTypeCreateInfo semType = {};
        semType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semType.pNext = coreInfo.semaphoreExportHandleType ? &semExport : nullptr;
        semType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &semType;
        if (d->vk->vkCreateSemaphore(d->h.device, &semInfo, nullptr, &d->timeline) != VK_SUCCESS)
            throw std::runtime_error("timeline semaphore creation failed");

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = d->h.computeQueueFamily;
        if (d->vk->vkCreateCommandPool(d->h.device, &poolInfo, nullptr, &d->cmdPool) != VK_SUCCESS)
            throw std::runtime_error("command pool creation failed");
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = d->cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = cmdSlots;
        if (d->vk->vkAllocateCommandBuffers(d->h.device, &allocInfo, d->cmd) != VK_SUCCESS)
            throw std::runtime_error("command buffer allocation failed");

        VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
        vsapi->createVideoFilterEx(out, "GPUBoxBlur", &d->vi, gpuBoxBlurGetFrame, gpuBoxBlurFree,
            fmParallel, ffGPUOutput, deps, 1, d.get(), core);
        if (vsapi->mapGetError(out)) {
            /* The free callback never runs when creation itself failed. */
            gpuBoxBlurFree(d.release(), core, vsapi);
            return;
        }
        d.release();
    } catch (const std::exception &e) {
        vsapi->mapSetError(out, (std::string("GPUBoxBlur: ") + e.what()).c_str());
        if (d)
            gpuBoxBlurFree(d.release(), core, vsapi);
    }
}

} // namespace

void gpuBoxBlurInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("GPUBoxBlur",
        "clip:vnode:gpu;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;",
        "clip:vnode:gpu;", gpuBoxBlurCreate, nullptr, plugin);
}
