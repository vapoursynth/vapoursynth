/* P16: "After device loss every wait returns an error, every retention is still released once,
   and destruction completes." The protocol document calls this the unverified assumption behind
   every unbounded wait, and names the method: force a driver timeout with an infinite shader
   loop, then exercise acquire, waitAll and free.

   The loop is data dependent so no optimizer can remove it: the condition reloads a storage
   buffer word that stays zero. Windows resets the display driver after TdrDelay (2 s by
   default), after which every call on this VkDevice returns VK_ERROR_DEVICE_LOST.

   A watchdog thread kills the process with a message if any step takes too long, so a hang is
   a reportable result rather than a hung terminal. Each step prints before it runs, so the
   last line printed names the step that hung. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static const VSVulkanFunctions *vk;
static VSCore *core;
static VSVulkanCoreHandles handles;
static VSGPUExecPool *pool;

static volatile LONG releaseCalls = 0;
static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";

#define RETAINED 8
static int retainedTag[RETAINED];
static volatile LONG retainedSeen[RETAINED];

static void step(const char *what) {
    strncpy(stepName, what, sizeof(stepName) - 1);
    stepName[sizeof(stepName) - 1] = 0;
    InterlockedIncrement(&watchdogStep);
    printf("  -> %s\n", what);
}

static DWORD WINAPI watchdog(LPVOID p) {
    LONG last = -1;
    int idle = 0;
    (void)p;
    for (;;) {
        Sleep(1000);
        if (watchdogStep == last) {
            if (++idle >= 90) { /* 90 s on one step */
                printf("\nFAIL: stuck for 90 s in: %s\n", stepName);
                printf("FAILED\n");
                fflush(stdout);
                TerminateProcess(GetCurrentProcess(), 1);
            }
        } else {
            idle = 0;
        }
        last = watchdogStep;
    }
}

static void VS_CC countingRelease(void *object) {
    int *tag = (int *)object;
    InterlockedIncrement(&releaseCalls);
    InterlockedIncrement(&retainedSeen[*tag]);
}

static const char *hangShader =
    "#version 450\n"
    "layout(local_size_x = 64) in;\n"
    "layout(std430, set = 0, binding = 0) buffer B { uint data[]; };\n"
    "void main() {\n"
    "    uint acc = gl_GlobalInvocationID.x + 1u;\n"
    /* data[0] is zero and nothing ever writes it, so the condition reloads forever. */
    "    while (data[0] == 0u) {\n"
    "        acc = acc * 1664525u + 1013904223u;\n"
    "        data[1u + (acc & 0xFFFFu)] = acc;\n"
    "    }\n"
    "    data[1] = acc;\n"
    "}\n";

int main(void) {
    char err[512] = { 0 };
    char log[4096] = { 0 };
    VSGPUShader *shader;
    const uint32_t *spirv;
    size_t spirvBytes = 0;
    VSGPUBuffer *buffer;
    VSVulkanBufferInfo binfo;
    VkShaderModule module = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorBufferInfo dbInfo;
    VkWriteDescriptorSet descWrite;
    VSGPUExecContext *ctx;
    int i, waitFailed, acquireFailed, allocFailed, lostReported = 0;
    uint64_t submitted = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    printf("P16: forcing a GPU hang, then checking the waits, the retentions and teardown\n");
    printf("     (the display will freeze for a second while the driver resets)\n\n");

    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &handles, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!vk) { printf("no functions: %s\n", err); return 3; }

    step("compile the hanging shader");
    shader = vkapi->compileGPUShader(core, 0, hangShader, log, sizeof(log));
    if (!shader) { printf("compile failed: %s\n", log); return 3; }
    spirv = vkapi->getGPUShaderCode(shader, &spirvBytes);

    step("create the storage buffer");
    buffer = vkapi->createGPUBuffer(core, 1 << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
        &binfo, err, sizeof(err));
    if (!buffer) { printf("createGPUBuffer failed: %s\n", err); return 3; }
    memset(binfo.mapped, 0, 1 << 20); /* data[0] == 0 forever */

    step("build the compute pipeline");
    {
        VkShaderModuleCreateInfo smInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkDescriptorSetLayoutBinding binding = { 0 };
        VkDescriptorSetLayoutCreateInfo slInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        VkPipelineLayoutCreateInfo plInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkComputePipelineCreateInfo cpInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

        smInfo.codeSize = spirvBytes;
        smInfo.pCode = spirv;
        if (vk->vkCreateShaderModule(handles.device, &smInfo, NULL, &module) != VK_SUCCESS) {
            printf("vkCreateShaderModule failed\n"); return 3;
        }
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        slInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
        slInfo.bindingCount = 1;
        slInfo.pBindings = &binding;
        vk->vkCreateDescriptorSetLayout(handles.device, &slInfo, NULL, &setLayout);
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &setLayout;
        vk->vkCreatePipelineLayout(handles.device, &plInfo, NULL, &pipelineLayout);
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = module;
        cpInfo.stage.pName = "main";
        cpInfo.layout = pipelineLayout;
        if (vk->vkCreateComputePipelines(handles.device, VK_NULL_HANDLE, 1, &cpInfo, NULL, &pipeline) != VK_SUCCESS) {
            printf("vkCreateComputePipelines failed\n"); return 3;
        }
        memset(&descWrite, 0, sizeof(descWrite));
        dbInfo.buffer = binfo.buffer;
        dbInfo.offset = 0;
        dbInfo.range = VK_WHOLE_SIZE;
        descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrite.dstBinding = 0;
        descWrite.descriptorCount = 1;
        descWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descWrite.pBufferInfo = &dbInfo;
    }

    step("create the exec pool");
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }

    step("record and submit the hanging dispatch, with 8 retentions on it");
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    {
        VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
        vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descWrite);
        vk->vkCmdDispatch(cmd, 4096, 1, 1);
    }
    for (i = 0; i < RETAINED; i++) {
        retainedTag[i] = i;
        vkapi->gpuExecRetain(ctx, &countingRelease, &retainedTag[i], 1 << 20);
    }
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) {
        printf("gpuExecSubmit failed: %s\n", err); return 3;
    }
    printf("     submitted, signalling value %llu; waiting for the driver to reset\n",
        (unsigned long long)submitted);

    /* What does the driver actually report on this timeline after the reset? The answer
       decides what a fix can key on, so look before touching any core entry point. */
    step("watch the pool timeline directly through the driver");
    {
        VkSemaphore sem = vkapi->getGPUTimelineSemaphore(vkapi->gpuExecPoolTimeline(pool));
        int round;
        for (round = 0; round < 40; round++) {
            uint64_t counter = 12345;
            VkResult cr = vk->vkGetSemaphoreCounterValue(handles.device, sem, &counter);
            VkSemaphoreWaitInfo wi;
            uint64_t target = 1;
            VkResult wr;
            memset(&wi, 0, sizeof(wi));
            wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wi.semaphoreCount = 1;
            wi.pSemaphores = &sem;
            wi.pValues = &target;
            wr = vk->vkWaitSemaphores(handles.device, &wi, 200000000ull);
            printf("     t=%4d ms  counter: VkResult %d value %llu | wait for 1: VkResult %d\n",
                round * 400, (int)cr, (unsigned long long)counter, (int)wr);
            if (cr != VK_SUCCESS || wr == VK_SUCCESS || (cr == VK_SUCCESS && counter > 1))
                break;
            Sleep(200);
        }
    }

    /* Which call actually reports the loss? That is what a fix inside detachCompleted, which
       runs under execPoolsMutex and so may not touch the queue lock, has to key on. */
    step("ask every cheap call whether the device is lost");
    {
        VkSemaphore sem = vkapi->getGPUTimelineSemaphore(vkapi->gpuExecPoolTimeline(pool));
        uint64_t counter = 0;
        VkResult r;
        VkSemaphore fresh = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo si;
        r = vk->vkGetSemaphoreCounterValue(handles.device, sem, &counter);
        printf("     vkGetSemaphoreCounterValue: VkResult %d, value %llu (pool submitted 1)\n",
            (int)r, (unsigned long long)counter);
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        r = vk->vkCreateSemaphore(handles.device, &si, NULL, &fresh);
        printf("     vkCreateSemaphore:          VkResult %d\n", (int)r);
        if (r == VK_SUCCESS)
            vk->vkDestroySemaphore(handles.device, fresh, NULL);
        r = vk->vkDeviceWaitIdle(handles.device);
        printf("     vkDeviceWaitIdle:           VkResult %d\n", (int)r);
        printf("     (VK_ERROR_DEVICE_LOST is -4)\n");
    }

    step("gpuExecPoolWaitIdle must return an error, not hang");
    waitFailed = vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    printf("     returned %d%s%s\n", waitFailed, waitFailed ? ": " : " (SUCCESS -- no device loss?)",
        waitFailed ? err : "");

    /* Which failure it was is what the filter destructors act on: a reset has to come back as
       gdDeviceLost, or they take the give-up branch and strand the pool, its retentions and the
       device with it. */
    lostReported = (waitFailed == gdDeviceLost);
    printf("     that is %s\n", lostReported ? "gdDeviceLost, as a reset must report"
        : (waitFailed == gdIncomplete ? "gdIncomplete -- WRONG for a reset" : "gdDrained"));

    step("gpuExecAcquire must return an error, not hang");
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    acquireFailed = (ctx == NULL);
    if (ctx) {
        printf("     acquired a context anyway; abandoning it\n");
        vkapi->gpuExecAbandon(ctx);
    } else {
        printf("     returned NULL: %s\n", err);
    }

    step("allocateGPUMemory must not hang (its ladder sweeps and waits)");
    {
        VkMemoryRequirements req;
        VSVulkanMemoryInfo minfo;
        VSGPUMemory *mem;
        req.size = 4 << 20;
        req.alignment = 256;
        req.memoryTypeBits = 0xFFFFFFFFu;
        mem = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &minfo, err, sizeof(err));
        allocFailed = (mem == NULL);
        if (mem)
            vkapi->freeGPUMemory(mem);
        printf("     %s\n", mem ? "succeeded" : err);
    }

    step("freeGPUExecPool must complete and release every retention exactly once");
    vkapi->freeGPUExecPool(pool);

    step("destroy the pipeline objects");
    vk->vkDestroyPipeline(handles.device, pipeline, NULL);
    vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
    vk->vkDestroyShaderModule(handles.device, module, NULL);
    vkapi->freeGPUShader(shader);

    step("destroyGPUBuffer must complete");
    vkapi->destroyGPUBuffer(buffer);

    step("freeCore must complete");
    vsapi->freeCore(core);

    step("done");
    printf("\nresults:\n");
    printf("  waits returned an error rather than hanging: waitIdle=%s acquire=%s\n",
        waitFailed ? "yes" : "NO (returned success)", acquireFailed ? "yes" : "NO (succeeded)");
    printf("  allocation after loss: %s\n", allocFailed ? "failed cleanly" : "succeeded");
    printf("  the drain reported gdDeviceLost: %s\n", lostReported ? "reported" : "NOT REPORTED");
    if (!lostReported)
        printf("    (expected on RADV, not a core defect: a reset that signals the exact pending\n"
               "     values is indistinguishable from completion, and only a later SUBMIT reports\n"
               "     the loss. See section 2a of vsvulkanexec_protocol.md.)\n");

    /* What this probe asserts is that the core SURVIVES a reset: every retention released exactly
       once and a clean teardown. Whether the driver reports the loss through these particular
       calls is a driver property, recorded above rather than judged here -- the header's "every
       call returns VK_ERROR_DEVICE_LOST" is Windows TDR behaviour, and gating the verdict on it
       skipped the retention check on precisely the drivers where it matters most. */
    printf("  release callbacks: %ld of %d\n", releaseCalls, RETAINED);
    for (i = 0; i < RETAINED; i++) {
        if (retainedSeen[i] != 1) {
            printf("  FAIL: retention %d released %ld times\n", i, retainedSeen[i]);
            printf("FAILED\n");
            return 1;
        }
    }
    if (releaseCalls != RETAINED) {
        printf("FAILED\n");
        return 1;
    }
    printf("  teardown completed\n");
    printf("ALL PASS\n");
    return 0;
}
