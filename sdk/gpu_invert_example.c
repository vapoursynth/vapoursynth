/*
* GPU filter example: bitwise invert of 8-16 bit integer clips, running entirely on the core's
* Vulkan device through the public API in VSVulkan4.h, built on the EXECUTION POOL -- the
* recommended shape for a GPU filter. The pool is the plumbing every submission needs
* regardless of what it records, and using it reduces a filter's obligations to three:
*
*   - create the pool once (createGPUExecPool); the core sizes its context ring from the
*     worker thread count, and acquiring beyond it waits out the oldest submission, which is
*     the intended backpressure
*   - per frame: acquire a context, declare what the submission touches (gpuExecReadsFrame for
*     every source, gpuExecWritesPlane for every plane written), record ordinary Vulkan into
*     the context's command buffer, submit
*   - destroy the pool in the free callback; it drains the device first, so everything a
*     submission still held is released safely
*
* Everything else is the pool's problem: waiting the sources' producer pairs device side,
* keeping the frames alive until the submission completes, taking the queue lock, allocating
* timeline values in queue order, and publishing the producer pairs on the written planes.
* gpu_invert_raw_example.c is the same filter with all of that spelled out by hand, for
* filters that need what the pool does not model.
*
* What no abstraction takes over: calling Vulkan through the core's ready loaded dispatch
* table (getVulkanFunctions) and building the pipeline (push descriptors, SPIR-V chained via
* maintenance5, kernel source compiled by the core at create).
*
* The kernel is dst[i] = ~src[i] over 32 bit words, which is exact pixel inversion for 8 and
* 16 bit integer samples, padding included.
*/

#define VS_USE_API_43
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernel ships as readable source and the core compiles it at filter creation through
   compileGPUShader -- the runtime compilation path, no SPIR-V blob and no build time shader
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

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    const VSVULKANAPI *vkapi;
    VSVulkanCoreHandles h;
    const VSVulkanFunctions *vk; /* the core's dispatch table, everything Vulkan goes through it */

    VkDescriptorSetLayout setLayout;
    VkPipelineLayout pipeLayout;
    VkPipeline pipeline;
    VSGPUExecPool *pool;
} InvertData;

static const VSFrame *VS_CC invertGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = d->vkapi->newGPUVideoFrame(fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
        VSGPUExecContext *ctx;
        VkCommandBuffer cmd;
        char err[512] = { 0 };
        int p;

        if (!dst) {
            vsapi->setFilterError("InvertGPU: failed to allocate the output frame", frameCtx);
            vsapi->freeFrame(src);
            return NULL;
        }

        /* Claim a recording. Acquire waits out the oldest in flight submission when every
           context is busy, which is what bounds this filter's frames in flight. */
        ctx = d->vkapi->gpuExecAcquire(d->pool, err, sizeof(err));
        if (!ctx) {
            vsapi->setFilterError(err, frameCtx);
            vsapi->freeFrame(dst);
            vsapi->freeFrame(src);
            return NULL;
        }

        /* Declaring the read is what makes the sources' producer pairs device side waits of
           this submission AND keeps the frame alive until it completes; the context takes
           its own reference, so this filter frees src normally below. */
        d->vkapi->gpuExecReadsFrame(ctx, src);

        cmd = d->vkapi->gpuExecCommandBuffer(ctx);
        d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);

        for (p = 0; p < fmt->numPlanes; p++) {
            VSVulkanPlaneInfo srcPlane, dstPlane;
            VkDescriptorBufferInfo bufferInfo[2];
            VkWriteDescriptorSet writes[2];
            VkPushConstantsInfo pushInfo;
            uint32_t words;
            int b;

            /* Declaring the write is what publishes the pool's (timeline, value) pair on the
               plane when the submission goes out, so consumers wait on the device instead of
               the host. */
            d->vkapi->gpuExecWritesPlane(ctx, dst, p);

            d->vkapi->getGPUPlane(src, p, &srcPlane);
            d->vkapi->getGPUPlane(dst, p, &dstPlane);

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

        /* Ends recording, takes the queue lock, allocates the timeline value in queue order,
           submits, and publishes the producer pairs declared above. The context is consumed
           either way. */
        if (d->vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err))) {
            vsapi->setFilterError(err, frameCtx);
            vsapi->freeFrame(dst);
            vsapi->freeFrame(src);
            return NULL;
        }

        /* The GPU may still be reading src, but the context holds its own reference; this
           one is simply no longer needed. */
        vsapi->freeFrame(src);
        return dst;
    }

    return NULL;
}

static void VS_CC invertFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    /* The pool drains the device before returning, so the pipeline objects a submission was
       still using are safe to destroy only after this point. */
    if (d->pool)
        d->vkapi->freeGPUExecPool(d->pool);
    if (d->pipeline)
        d->vk->vkDestroyPipeline(d->h.device, d->pipeline, NULL);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, NULL);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, NULL);
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
    int b;

    d->node = vsapi->mapGetNode(in, "clip", 0, NULL);
    d->vi = *vsapi->getVideoInfo(d->node);

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

    /* The core sizes the context ring from its worker thread count, and separately bounds
       the memory queued submissions pin across all pools. The pool's timeline is created
       exportable wherever the device allows, so foreign APIs can wait the producer pairs it
       publishes. */
    d->pool = d->vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!d->pool) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

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
    if (d->pool)
        d->vkapi->freeGPUExecPool(d->pool);
    if (d->pipeline)
        d->vk->vkDestroyPipeline(d->h.device, d->pipeline, NULL);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, NULL);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, NULL);
    if (d->node)
        vsapi->freeNode(d->node);
    free(d);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.gpuinvert", "vkexample", "Out of tree GPU filter example", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("InvertGPU", "clip:vnode:gpu;", "clip:vnode:gpu;", invertCreate, NULL, plugin);
}
