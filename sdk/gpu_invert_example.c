/*
* GPU filter example: bitwise invert of 8-16 bit integer clips, running entirely on the core's
* Vulkan device through the public API in VSVulkan4.h. This demonstrates every obligation an
* out of tree GPU filter has:
*
*   - calling Vulkan through the core's ready loaded dispatch table (getVulkanFunctions);
*     nothing is linked and nothing is loaded by hand. The handles' getInstanceProcAddr
*     remains available for entry points outside the curated table.
*   - creating its own pipeline (push descriptors, SPIR-V chained via maintenance5)
*   - reading source planes by waiting their (semaphore, value) producer pairs device side
*   - submitting on the shared compute queue with the queue lock held, allocating its own
*     timeline values inside that same lock so signals reach the queue in increasing order
*   - publishing producer pairs for the planes it wrote through setGPUPlaneProducer
*   - keeping source frames alive until its submissions complete, swept without blocking
*     through vkGetSemaphoreCounterValue
*
* The kernel is dst[i] = ~src[i] over 32 bit words, which is exact pixel inversion for 8 and
* 16 bit integer samples, padding included. SPIR-V source: bench_vulkan/invert.comp.
*/

#define VS_USE_API_43
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef SRWLOCK InstanceLock;
#define LOCK_INIT(l) InitializeSRWLock(l)
#define LOCK_ACQUIRE(l) AcquireSRWLockExclusive(l)
#define LOCK_RELEASE(l) ReleaseSRWLockExclusive(l)
#define LOCK_FREE(l)
#else
#include <pthread.h>
typedef pthread_mutex_t InstanceLock;
#define LOCK_INIT(l) pthread_mutex_init(l, NULL)
#define LOCK_ACQUIRE(l) pthread_mutex_lock(l)
#define LOCK_RELEASE(l) pthread_mutex_unlock(l)
#define LOCK_FREE(l) pthread_mutex_destroy(l)
#endif

/* The kernel ships as readable source and the core compiles it at filter creation through
   compileGPUShader — the runtime compilation path, no SPIR-V blob and no build time shader
   toolchain. The accepted dialect is pinned by the core: #version 460 compute, Vulkan 1.4
   client, SPIR-V 1.6 target. gpu_planestats_example keeps the committed blob pattern; both
   feed the identical pipeline creation. Bindings match the push descriptor writes below:
   binding 0 is the source, binding 1 the destination. */
static const char invertGlsl[] =
    "#version 460\n"
    "layout(local_size_x = 256) in;\n"
    "layout(push_constant) uniform PC { uint count; } pc;\n"
    "layout(set = 0, binding = 0, std430) readonly buffer Src { uint srcWords[]; };\n"
    "layout(set = 0, binding = 1, std430) writeonly buffer Dst { uint dstWords[]; };\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    if (i < pc.count)\n"
    "        dstWords[i] = ~srcWords[i];\n"
    "}\n";

#define CMD_SLOTS 4
#define MAX_RETAINED 64

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    VSCore *core;
    const VSVULKANAPI *vkapi;
    VSVulkanCoreHandles h;
    const VSVulkanFunctions *vk; /* the core's dispatch table, everything Vulkan goes through it */

    VkQueue computeQueue;
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout pipeLayout;
    VkPipeline pipeline;
    VkCommandPool cmdPool;
    VkCommandBuffer cmd[CMD_SLOTS];
    uint64_t slotValue[CMD_SLOTS];
    int nextSlot;

    /* The filter's own timeline: it signals rising values and publishes them as the producer
       pairs of the frames it writes. Counted and owned by the core, so releasing it in the
       free callback is enough -- frames still carrying it as their producer hold their own
       reference and keep the semaphore alive for exactly as long as they need it. The raw
       handle is cached beside it for signalling and counter queries. */
    VSGPUTimeline *timeline;
    VkSemaphore timelineSem;
    uint64_t nextValue;

    /* Source frames whose references are held until the submission reading them completes.
       Swept opportunistically with the non blocking counter query. */
    struct { const VSFrame *frame; uint64_t value; } retained[MAX_RETAINED];
    int retainedCount;

    InstanceLock lock;
} InvertData;

static void sweepRetained(InvertData *d, const VSAPI *vsapi) {
    uint64_t completed = 0;
    int i, kept = 0;
    if (d->vk->vkGetSemaphoreCounterValue(d->h.device, d->timelineSem, &completed) != VK_SUCCESS)
        return;
    for (i = 0; i < d->retainedCount; i++) {
        if (d->retained[i].value <= completed)
            vsapi->freeFrame(d->retained[i].frame);
        else
            d->retained[kept++] = d->retained[i];
    }
    d->retainedCount = kept;
}

static const VSFrame *VS_CC invertGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = d->vkapi->newGPUVideoFrame(fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
        VkSemaphoreSubmitInfo waits[3];
        uint32_t waitCount = 0;
        VkCommandBufferBeginInfo begin;
        VkCommandBufferSubmitInfo cmdInfo;
        VkSemaphoreSubmitInfo signalInfo;
        VkSubmitInfo2 submit;
        VkCommandBuffer cmd;
        uint64_t value, completed = 0;
        int slot, p;

        LOCK_ACQUIRE(&d->lock);
        sweepRetained(d, vsapi);

        /* One command buffer per in flight submission; reusing a slot waits out its previous
           life first. An example may block holding the instance lock, a production filter
           would rather size the ring generously. */
        slot = d->nextSlot;
        d->nextSlot = (d->nextSlot + 1) % CMD_SLOTS;
        d->vk->vkGetSemaphoreCounterValue(d->h.device, d->timelineSem, &completed);
        if (d->slotValue[slot] > completed) {
            VkSemaphoreWaitInfo waitInfo;
            memset(&waitInfo, 0, sizeof(waitInfo));
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &d->timelineSem;
            waitInfo.pValues = &d->slotValue[slot];
            d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX);
        }
        cmd = d->cmd[slot];

        d->vk->vkResetCommandBuffer(cmd, 0);
        memset(&begin, 0, sizeof(begin));
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        d->vk->vkBeginCommandBuffer(cmd, &begin);
        d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);

        for (p = 0; p < fmt->numPlanes; p++) {
            VSVulkanPlaneInfo srcPlane, dstPlane;
            VkDescriptorBufferInfo bufferInfo[2];
            VkWriteDescriptorSet writes[2];
            VkPushConstantsInfo pushInfo;
            uint32_t words;
            int b;

            d->vkapi->getGPUPlane(src, p, &srcPlane);
            d->vkapi->getGPUPlane(dst, p, &dstPlane);

            /* Wait each source plane's producer device side; duplicates are harmless here and
               a real filter would deduplicate to the highest value per semaphore. */
            if (srcPlane.readySemaphore && waitCount < 3) {
                memset(&waits[waitCount], 0, sizeof(waits[0]));
                waits[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                waits[waitCount].semaphore = srcPlane.readySemaphore;
                waits[waitCount].value = srcPlane.readyValue;
                waits[waitCount].stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                waitCount++;
            }

            memset(bufferInfo, 0, sizeof(bufferInfo));
            memset(writes, 0, sizeof(writes));
            bufferInfo[0].buffer = srcPlane.buffer;
            bufferInfo[0].range = VK_WHOLE_SIZE;
            bufferInfo[1].buffer = dstPlane.buffer;
            bufferInfo[1].range = VK_WHOLE_SIZE;
            for (b = 0; b < 2; b++) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstBinding = (uint32_t)b;
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].pBufferInfo = &bufferInfo[b];
            }
            d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeLayout, 0, 2, writes);

            /* The table carries the Vulkan 1.4 spellings, so push constants go through the 2
               variant with its info struct. */
            words = (uint32_t)(srcPlane.bufferSize / 4);
            memset(&pushInfo, 0, sizeof(pushInfo));
            pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
            pushInfo.layout = d->pipeLayout;
            pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushInfo.size = sizeof(words);
            pushInfo.pValues = &words;
            d->vk->vkCmdPushConstants2(cmd, &pushInfo);
            d->vk->vkCmdDispatch(cmd, (words + 255) / 256, 1, 1);
        }
        d->vk->vkEndCommandBuffer(cmd);

        memset(&cmdInfo, 0, sizeof(cmdInfo));
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdInfo.commandBuffer = cmd;
        memset(&signalInfo, 0, sizeof(signalInfo));
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = d->timelineSem;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memset(&submit, 0, sizeof(submit));
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.waitSemaphoreInfoCount = waitCount;
        submit.pWaitSemaphoreInfos = waitCount ? waits : NULL;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signalInfo;

        /* Value allocation and submission belong inside the queue lock together: this is what
           keeps this instance's timeline signals reaching the queue in increasing order while
           other filters, and the core itself, submit concurrently. */
        d->vkapi->lockVulkanQueue(core, vqCompute);
        value = ++d->nextValue;
        signalInfo.value = value;
        d->vk->vkQueueSubmit2(d->computeQueue, 1, &submit, VK_NULL_HANDLE);
        d->vkapi->unlockVulkanQueue(core, vqCompute);

        d->slotValue[slot] = value;
        for (p = 0; p < fmt->numPlanes; p++)
            d->vkapi->setGPUPlaneProducer(dst, p, d->timeline, value);

        /* The source reference is handed to the retained ring instead of being freed: the GPU
           may still be reading it long after this function returns. */
        if (d->retainedCount < MAX_RETAINED) {
            d->retained[d->retainedCount].frame = src;
            d->retained[d->retainedCount].value = value;
            d->retainedCount++;
        } else {
            /* Ring full: fall back to a blocking wait so correctness never depends on luck. */
            VkSemaphoreWaitInfo waitInfo;
            memset(&waitInfo, 0, sizeof(waitInfo));
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &d->timelineSem;
            waitInfo.pValues = &value;
            d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX);
            vsapi->freeFrame(src);
        }
        LOCK_RELEASE(&d->lock);

        return dst;
    }

    return NULL;
}

static void VS_CC invertFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    if (d->timelineSem && d->nextValue) {
        VkSemaphoreWaitInfo waitInfo;
        memset(&waitInfo, 0, sizeof(waitInfo));
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &d->timelineSem;
        waitInfo.pValues = &d->nextValue;
        d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX);
    }
    sweepRetained(d, vsapi);
    if (d->cmdPool)
        d->vk->vkDestroyCommandPool(d->h.device, d->cmdPool, NULL);
    if (d->pipeline)
        d->vk->vkDestroyPipeline(d->h.device, d->pipeline, NULL);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, NULL);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, NULL);
    /* Just this filter's reference; frames still naming it keep the semaphore alive. */
    if (d->timeline)
        d->vkapi->freeGPUTimeline(d->timeline);
    LOCK_FREE(&d->lock);
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC invertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)calloc(1, sizeof(InvertData));
    char err[512] = { 0 };
    VSGPUShader *shader = NULL;
    const uint32_t *shaderCode;
    size_t shaderCodeSize;
    VkDescriptorSetLayoutBinding bindings[2];
    VkDescriptorSetLayoutCreateInfo setInfo;
    VkPushConstantRange range;
    VkPipelineLayoutCreateInfo layoutInfo;
    VkShaderModuleCreateInfo moduleInfo;
    VkComputePipelineCreateInfo pipeInfo;
    VkCommandPoolCreateInfo poolInfo;
    VkCommandBufferAllocateInfo allocInfo;
    VkDeviceQueueInfo2 queueInfo;
    int b;

    d->node = vsapi->mapGetNode(in, "clip", 0, NULL);
    d->vi = *vsapi->getVideoInfo(d->node);
    d->core = core;

    if (d->vi.format.colorFamily == cfUndefined || d->vi.format.sampleType != stInteger ||
        (d->vi.format.bytesPerSample != 1 && d->vi.format.bytesPerSample != 2)) {
        vsapi->mapSetError(out, "InvertGPU: only constant format 8-16 bit integer clips are supported");
        goto fail;
    }

    d->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
    if (!d->vkapi) {
        vsapi->mapSetError(out, "InvertGPU: Vulkan API not available");
        goto fail;
    }
    if (d->vkapi->getVulkanHandles(core, &d->h, err, sizeof(err))) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

    /* Everything Vulkan from here on goes through the core's dispatch table. */
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
    range.size = sizeof(uint32_t);
    memset(&layoutInfo, 0, sizeof(layoutInfo));
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &d->setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    d->vk->vkCreatePipelineLayout(d->h.device, &layoutInfo, NULL, &d->pipeLayout);

    /* Compile the kernel source through the core; the words come back cached, so a second
       instance of this filter reuses the parse. The handle only needs to live until the
       pipeline holds the code. */
    shader = d->vkapi->compileGPUShader(core, slGLSL, invertGlsl, err, sizeof(err));
    if (!shader) {
        vsapi->mapSetError(out, err);
        goto fail;
    }
    shaderCode = d->vkapi->getGPUShaderCode(shader, &shaderCodeSize);

    /* maintenance5 is part of the required feature set, so the SPIR-V can ride along in the
       stage's pNext with no shader module object. */
    memset(&moduleInfo, 0, sizeof(moduleInfo));
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = shaderCodeSize;
    moduleInfo.pCode = shaderCode;
    memset(&pipeInfo, 0, sizeof(pipeInfo));
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.pNext = &moduleInfo;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = d->pipeLayout;
    if (d->vk->vkCreateComputePipelines(d->h.device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &d->pipeline) != VK_SUCCESS) {
        vsapi->mapSetError(out, "InvertGPU: pipeline creation failed");
        goto fail;
    }
    d->vkapi->freeGPUShader(shader);
    shader = NULL;

    /* The core makes it exportable wherever the device can, so CUDA and other Vulkan devices
       consuming this filter's frames may import the producer pair and wait it device side
       instead of falling back to waitGPUFrame. Costs nothing when nobody imports it. */
    d->timeline = d->vkapi->createGPUTimeline(core, err, sizeof(err));
    if (!d->timeline) {
        vsapi->mapSetError(out, err);
        invertFree(d, core, vsapi);
        return;
    }
    d->timelineSem = d->vkapi->getGPUTimelineSemaphore(d->timeline);

    memset(&poolInfo, 0, sizeof(poolInfo));
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = d->h.computeQueueFamily;
    d->vk->vkCreateCommandPool(d->h.device, &poolInfo, NULL, &d->cmdPool);
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = d->cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = CMD_SLOTS;
    d->vk->vkAllocateCommandBuffers(d->h.device, &allocInfo, d->cmd);

    LOCK_INIT(&d->lock);

    {
        VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
        vsapi->createVideoFilterEx(out, "InvertGPU", &d->vi, invertGetFrame, invertFree, fmParallel, ffGPUOutput, deps, 1, d, core);
    }
    if (vsapi->mapGetError(out))
        goto fail;
    return;

fail:
    if (shader)
        d->vkapi->freeGPUShader(shader);
    if (d->node)
        vsapi->freeNode(d->node);
    free(d);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.gpuinvert", "vkexample", "Out of tree GPU filter example", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("InvertGPU", "clip:vnode:gpu;", "clip:vnode:gpu;", invertCreate, NULL, plugin);
}
