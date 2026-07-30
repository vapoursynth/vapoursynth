/*
* GPU filter example: plane statistics (min/max/average) of 8-16 bit integer clips, computed
* on the GPU and delivered as frame properties, with the input planes passed through untouched.
* Where gpu_invert_example.c shows the asynchronous map pattern, this shows the reduce pattern,
* and with it the two things reductions need that maps do not:
*
*   - scratch memory that is not a frame plane: a partials buffer sized by the dispatch
*     geometry and a tiny host visible readback buffer, both taken per call from the core's
*     pooled VRAM allocator through createGPUBuffer/destroyGPUBuffer. The pool recycles
*     same size regions through free lists, so per call allocate/destroy is cheap, counted
*     against the VRAM limit, and visible to the thread pool's admission control.
*
*   - a legitimate host wait: properties are CPU data, so the filter must wait for its own
*     submission before it can return the frame. That sync point is also why this example
*     needs none of the invert example's machinery — no retained source ring, no command
*     buffer slot ring, no producer publication: everything is finished before returning.
*     The scratch destruction rule (only after your submissions complete) is satisfied the
*     same way.
*
* The output frame shares every input plane (newVideoFrame2), producer pairs riding along
* unchanged, so downstream GPU filters chain on without this filter ever touching pixel data.
* The core guarantees the input is GPU resident: the signature says vknode.
*
* SPIR-V sources: gpu_planestats_pass1.comp / gpu_planestats_pass2.comp, regenerated into
* gpu_planestats_spv.h by regenerate_example_shaders.ps1.
*/

#define VS_USE_API_43
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_planestats_spv.h"

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    int plane;
    const VSVULKANAPI *vkapi;
    VSVulkanCoreHandles h;
    const VSVulkanFunctions *vk;

    VkQueue computeQueue;
    VkDescriptorSetLayout setLayout; /* both passes: two storage buffers, push descriptors */
    VkPipelineLayout pipeLayout;
    VkPipeline pass1;
    VkPipeline pass2;

    /* Only signaled so this filter can wait for its own submissions; nothing downstream ever
       sees it since no plane is produced here. Values are allocated under the queue lock. */
    VkSemaphore timeline;
    uint64_t nextValue;
} StatsData;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;     /* in elements */
    uint32_t groupCount; /* pass 2 only */
} StatsPush;

static const VSFrame *VS_CC statsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    StatsData *d = (StatsData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        const VSFrame *planeSrc[3] = { src, src, src };
        const int planeNo[3] = { 0, 1, 2 };
        VSFrame *dst;
        VSGPUBuffer *partials = NULL, *result = NULL;
        VSVulkanBufferInfo partialsInfo, resultInfo;
        VSVulkanPlaneInfo srcPlane;
        VkCommandPoolCreateInfo poolInfo;
        VkCommandBufferAllocateInfo allocInfo;
        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkCommandBuffer cmd;
        VkCommandBufferBeginInfo begin;
        VkDescriptorBufferInfo bufferInfo[2];
        VkWriteDescriptorSet writes[2];
        VkPushConstantsInfo pushInfo;
        VkMemoryBarrier2 barrier;
        VkDependencyInfo dep;
        VkSemaphoreSubmitInfo waitInfo, signalInfo;
        VkCommandBufferSubmitInfo cmdInfo;
        VkSubmitInfo2 submit;
        VkSemaphoreWaitInfo hostWait;
        StatsPush pc;
        char err[512] = { 0 };
        const uint32_t *res32;
        double sum, maxVal;
        uint64_t value;
        uint32_t pw, ph, gx, gy, groups;
        int b;

        pw = (uint32_t)vsapi->getFrameWidth(src, d->plane);
        ph = (uint32_t)vsapi->getFrameHeight(src, d->plane);
        gx = (pw + 15) / 16;
        gy = (ph + 15) / 16;
        groups = gx * gy;

        /* Every plane shared, properties copied from the source: this filter only adds props. */
        dst = vsapi->newVideoFrame2(fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), planeSrc, planeNo, src, core);

        /* The scratch: pass 1 partials in VRAM, the 16 byte final result in host visible
           memory read directly after the wait. HOST_COHERENT is required so no explicit
           invalidate is needed; HOST_CACHED is preferred because CPU reads from uncached
           write combined memory are painfully slow. */
        partials = d->vkapi->createGPUBuffer(core, (VkDeviceSize)groups * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &partialsInfo, err, sizeof(err));
        result = partials ? d->vkapi->createGPUBuffer(core, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &resultInfo, err, sizeof(err)) : NULL;
        if (!result)
            goto error;

        /* A transient pool per call keeps concurrent calls of this instance fully independent;
           its cost is noise next to the wait this filter must do anyway. */
        memset(&poolInfo, 0, sizeof(poolInfo));
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = d->h.computeQueueFamily;
        if (d->vk->vkCreateCommandPool(d->h.device, &poolInfo, NULL, &cmdPool) != VK_SUCCESS) {
            snprintf(err, sizeof(err), "command pool creation failed");
            goto error;
        }
        memset(&allocInfo, 0, sizeof(allocInfo));
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        d->vk->vkAllocateCommandBuffers(d->h.device, &allocInfo, &cmd);

        memset(&begin, 0, sizeof(begin));
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        d->vk->vkBeginCommandBuffer(cmd, &begin);

        d->vkapi->getGPUPlane(src, d->plane, &srcPlane);

        memset(bufferInfo, 0, sizeof(bufferInfo));
        memset(writes, 0, sizeof(writes));
        for (b = 0; b < 2; b++) {
            bufferInfo[b].range = VK_WHOLE_SIZE;
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstBinding = (uint32_t)b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &bufferInfo[b];
        }

        pc.width = pw;
        pc.height = ph;
        pc.stride = (uint32_t)(vsapi->getStride(src, d->plane) / fmt->bytesPerSample);
        pc.groupCount = groups;
        memset(&pushInfo, 0, sizeof(pushInfo));
        pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
        pushInfo.layout = d->pipeLayout;
        pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushInfo.size = sizeof(pc);
        pushInfo.pValues = &pc;
        d->vk->vkCmdPushConstants2(cmd, &pushInfo);

        /* Pass 1: plane -> per workgroup partials. */
        bufferInfo[0].buffer = srcPlane.buffer;
        bufferInfo[1].buffer = partialsInfo.buffer;
        d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pass1);
        d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeLayout, 0, 2, writes);
        d->vk->vkCmdDispatch(cmd, gx, gy, 1);

        /* Pass 2 reads what pass 1 wrote. */
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        memset(&dep, 0, sizeof(dep));
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        d->vk->vkCmdPipelineBarrier2(cmd, &dep);

        /* Pass 2: partials -> the final 16 bytes. */
        bufferInfo[0].buffer = partialsInfo.buffer;
        bufferInfo[1].buffer = resultInfo.buffer;
        d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pass2);
        d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeLayout, 0, 2, writes);
        d->vk->vkCmdDispatch(cmd, 1, 1, 1);

        d->vk->vkEndCommandBuffer(cmd);

        /* Wait the read plane's producer device side, exactly like a plane consumer. */
        memset(&waitInfo, 0, sizeof(waitInfo));
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = srcPlane.readySemaphore;
        waitInfo.value = srcPlane.readyValue;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memset(&signalInfo, 0, sizeof(signalInfo));
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = d->timeline;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memset(&cmdInfo, 0, sizeof(cmdInfo));
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdInfo.commandBuffer = cmd;
        memset(&submit, 0, sizeof(submit));
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.waitSemaphoreInfoCount = srcPlane.readySemaphore ? 1 : 0;
        submit.pWaitSemaphoreInfos = srcPlane.readySemaphore ? &waitInfo : NULL;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signalInfo;

        d->vkapi->lockVulkanQueue(core, vqCompute);
        value = ++d->nextValue;
        signalInfo.value = value;
        d->vk->vkQueueSubmit2(d->computeQueue, 1, &submit, VK_NULL_HANDLE);
        d->vkapi->unlockVulkanQueue(core, vqCompute);

        /* The mandatory sync point: properties are CPU data. After this wait the submission is
           done, so the source may be freed at once and the scratch destroyed immediately —
           the buffer destruction rule satisfied trivially. */
        memset(&hostWait, 0, sizeof(hostWait));
        hostWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        hostWait.semaphoreCount = 1;
        hostWait.pSemaphores = &d->timeline;
        hostWait.pValues = &value;
        d->vk->vkWaitSemaphores(d->h.device, &hostWait, UINT64_MAX);

        res32 = (const uint32_t *)resultInfo.mapped;
        sum = (double)res32[1] * 4294967296.0 + (double)res32[0];
        maxVal = (double)(((int64_t)1 << fmt->bitsPerSample) - 1);
        {
            VSMap *props = vsapi->getFramePropertiesRW(dst);
            vsapi->mapSetInt(props, "PlaneStatsGPUMin", (int64_t)res32[2], maReplace);
            vsapi->mapSetInt(props, "PlaneStatsGPUMax", (int64_t)res32[3], maReplace);
            /* Identical normalization to std.PlaneStats so the two are directly comparable. */
            vsapi->mapSetFloat(props, "PlaneStatsGPUAverage", sum / ((double)pw * ph * maxVal), maReplace);
        }

        d->vk->vkDestroyCommandPool(d->h.device, cmdPool, NULL);
        d->vkapi->destroyGPUBuffer(partials);
        d->vkapi->destroyGPUBuffer(result);
        vsapi->freeFrame(src);
        return dst;

error:
        if (cmdPool)
            d->vk->vkDestroyCommandPool(d->h.device, cmdPool, NULL);
        if (partials)
            d->vkapi->destroyGPUBuffer(partials);
        if (result)
            d->vkapi->destroyGPUBuffer(result);
        vsapi->setFilterError((err[0] ? err : "PlaneStatsGPU: frame processing failed"), frameCtx);
        vsapi->freeFrame(src);
        vsapi->freeFrame(dst);
        return NULL;
    }

    return NULL;
}

static void VS_CC statsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    StatsData *d = (StatsData *)instanceData;
    /* Nothing can be in flight here: every call waits out its own submission before
       returning, which is what lets this free skip the wait the invert example needs. */
    if (d->pass1)
        d->vk->vkDestroyPipeline(d->h.device, d->pass1, NULL);
    if (d->pass2)
        d->vk->vkDestroyPipeline(d->h.device, d->pass2, NULL);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, NULL);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, NULL);
    if (d->timeline)
        d->vk->vkDestroySemaphore(d->h.device, d->timeline, NULL);
    vsapi->freeNode(d->node);
    free(d);
}

static VkPipeline statsMakePipeline(StatsData *d, const uint32_t *spv, size_t spvSize) {
    VkShaderModuleCreateInfo moduleInfo;
    VkComputePipelineCreateInfo pipeInfo;
    VkPipeline pipeline = VK_NULL_HANDLE;

    memset(&moduleInfo, 0, sizeof(moduleInfo));
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spvSize;
    moduleInfo.pCode = spv;
    memset(&pipeInfo, 0, sizeof(pipeInfo));
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.pNext = &moduleInfo;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = d->pipeLayout;
    d->vk->vkCreateComputePipelines(d->h.device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &pipeline);
    return pipeline;
}

static void VS_CC statsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    StatsData *d = (StatsData *)calloc(1, sizeof(StatsData));
    char err[512] = { 0 };
    VkDescriptorSetLayoutBinding bindings[2];
    VkDescriptorSetLayoutCreateInfo setInfo;
    VkPushConstantRange range;
    VkPipelineLayoutCreateInfo layoutInfo;
    VkSemaphoreTypeCreateInfo semType;
    VkSemaphoreCreateInfo semInfo;
    VkDeviceQueueInfo2 queueInfo;
    int errSet = 0, b;

    d->node = vsapi->mapGetNode(in, "clip", 0, NULL);
    d->vi = *vsapi->getVideoInfo(d->node);
    d->plane = (int)vsapi->mapGetIntSaturated(in, "plane", 0, &errSet);
    if (errSet)
        d->plane = 0;

    if (d->vi.format.colorFamily == cfUndefined || d->vi.format.sampleType != stInteger ||
        (d->vi.format.bytesPerSample != 1 && d->vi.format.bytesPerSample != 2)) {
        vsapi->mapSetError(out, "PlaneStatsGPU: only constant format 8-16 bit integer clips are supported");
        goto fail;
    }
    if (d->plane < 0 || d->plane >= d->vi.format.numPlanes) {
        vsapi->mapSetError(out, "PlaneStatsGPU: invalid plane");
        goto fail;
    }

    d->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
    if (!d->vkapi) {
        vsapi->mapSetError(out, "PlaneStatsGPU: Vulkan API not available");
        goto fail;
    }
    if (d->vkapi->getVulkanHandles(core, &d->h, err, sizeof(err))) {
        vsapi->mapSetError(out, err);
        goto fail;
    }
    d->vk = d->vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!d->vk) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

    memset(&queueInfo, 0, sizeof(queueInfo));
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queueInfo.queueFamilyIndex = d->h.computeQueueFamily;
    queueInfo.queueIndex = d->h.computeQueueIndex;
    d->vk->vkGetDeviceQueue2(d->h.device, &queueInfo, &d->computeQueue);

    /* Both passes use the same shape: two storage buffers and the shared push range. */
    memset(bindings, 0, sizeof(bindings));
    for (b = 0; b < 2; b++) {
        bindings[b].binding = (uint32_t)b;
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    memset(&setInfo, 0, sizeof(setInfo));
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    setInfo.bindingCount = 2;
    setInfo.pBindings = bindings;
    d->vk->vkCreateDescriptorSetLayout(d->h.device, &setInfo, NULL, &d->setLayout);

    memset(&range, 0, sizeof(range));
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = sizeof(StatsPush);
    memset(&layoutInfo, 0, sizeof(layoutInfo));
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &d->setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    d->vk->vkCreatePipelineLayout(d->h.device, &layoutInfo, NULL, &d->pipeLayout);

    if (d->vi.format.bytesPerSample == 1)
        d->pass1 = statsMakePipeline(d, planeStats1Spv8, sizeof(planeStats1Spv8));
    else
        d->pass1 = statsMakePipeline(d, planeStats1Spv16, sizeof(planeStats1Spv16));
    d->pass2 = statsMakePipeline(d, planeStats2Spv, sizeof(planeStats2Spv));
    if (!d->pass1 || !d->pass2) {
        vsapi->mapSetError(out, "PlaneStatsGPU: pipeline creation failed");
        goto fail;
    }

    memset(&semType, 0, sizeof(semType));
    semType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    memset(&semInfo, 0, sizeof(semInfo));
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &semType;
    d->vk->vkCreateSemaphore(d->h.device, &semInfo, NULL, &d->timeline);

    {
        VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
        vsapi->createVideoFilterEx(out, "PlaneStatsGPU", &d->vi, statsGetFrame, statsFree, fmParallel, ffGPUOutput, deps, 1, d, core);
    }
    if (vsapi->mapGetError(out))
        goto fail;
    return;

fail:
    if (d->pass1 && d->vk)
        d->vk->vkDestroyPipeline(d->h.device, d->pass1, NULL);
    if (d->pass2 && d->vk)
        d->vk->vkDestroyPipeline(d->h.device, d->pass2, NULL);
    if (d->pipeLayout && d->vk)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, NULL);
    if (d->setLayout && d->vk)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, NULL);
    if (d->node)
        vsapi->freeNode(d->node);
    free(d);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.gpuplanestats", "vkstats", "Out of tree GPU reduction example", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("PlaneStatsGPU", "clip:vknode;plane:int:opt;", "clip:vknode;", statsCreate, NULL, plugin);
}
