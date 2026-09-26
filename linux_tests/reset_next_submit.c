/* L1: after a GPU reset that the core did not notice, does the NEXT submission report device
   loss?

   This is the open question from finding F8. A Linux/RADV reset was measured signalling every
   timeline to exactly the value the abandoned submissions were going to signal, so the core's
   wait reports completion and a filter takes stale bytes as its result. What decides how bad
   that is, is whether the driver refuses the next submission: the kernel marks the context whose
   job caused the reset as guilty, and if that surfaces as VK_ERROR_DEVICE_LOST then the core
   latches the loss on its own submit path and every later frame becomes a visible error. If it
   does not, every frame after a reset is silently wrong for the life of the core.

   The probe runs a long finite arithmetic shader whose result is checked against a CPU
   reference, so a dispatch that never executed is caught by the value rather than inferred from
   a timeout. Then it asks the question: acquire, submit, drain, twice.

   Usage: reset_next_submit [iterations]     default 1000000000

   Start smaller (1000000, 10000000, 100000000) to confirm the shader and the checking work on
   the machine before reaching for a count that resets the GPU. The display will freeze while the
   driver recovers. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"
#include "probe_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static const VSVulkanFunctions *vk;
static VSCore *core;
static VSVulkanCoreHandles handles;
static VSGPUExecPool *pool;

static volatile LONG releaseCalls = 0;
static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";

static const uint32_t SEED = 0x12345678u;

static void step(const char *what) {
    strncpy(stepName, what, sizeof(stepName) - 1);
    stepName[sizeof(stepName) - 1] = 0;
    InterlockedIncrement(&watchdogStep);
    printf("  -> %s\n", what);
}

/* A hang has to be a reportable result, not a hung terminal. The last line printed names the
   step that stopped making progress. */
static DWORD WINAPI watchdog(LPVOID p) {
    LONG last = -1;
    int idle = 0;
    (void)p;
    for (;;) {
        Sleep(1000);
        if (watchdogStep == last) {
            if (++idle >= 120) {
                printf("\nFAIL: stuck for 120 s in: %s\n", stepName);
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
    (void)object;
    InterlockedIncrement(&releaseCalls);
}

/* The loop bound and the seed come out of the buffer rather than a push constant, so the
   compiler cannot fold the loop away and the same binary sweeps any count. */
static const char *workShader =
    "#version 450\n"
    "layout(local_size_x = 1) in;\n"
    "layout(std430, set = 0, binding = 0) buffer B { uint data[]; };\n"
    "void main() {\n"
    "    uint n = data[1];\n"
    "    uint acc = data[2];\n"
    "    for (uint i = 0u; i < n; i++)\n"
    "        acc = acc * 1664525u + 1013904223u;\n"
    "    data[0] = acc;\n"
    "}\n";

static uint32_t referenceValue(uint32_t iterations, uint32_t seed) {
    uint32_t acc = seed;
    uint32_t i;
    for (i = 0; i < iterations; i++)
        acc = acc * 1664525u + 1013904223u;
    return acc;
}

static const char *drainName(int r) {
    if (r == gdDrained) return "gdDrained";
    if (r == gdIncomplete) return "gdIncomplete";
    if (r == gdDeviceLost) return "gdDeviceLost";
    return "unknown";
}

/* One round of the actual question. Returns nonzero once anything reported the loss. */
static int askAgain(int round) {
    char err[512] = { 0 };
    VSGPUExecContext *ctx;
    int submitted, drained;
    uint64_t value = 0;

    printf("  round %d:\n", round);
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) {
        printf("    gpuExecAcquire: NULL (%s)\n", err);
        printf("    -> the core has latched the loss; acquire is the call that reports it\n");
        return 1;
    }
    printf("    gpuExecAcquire: ok\n");

    err[0] = 0;
    submitted = vkapi->gpuExecSubmit(ctx, &value, err, sizeof(err));
    printf("    gpuExecSubmit:  %d%s%s\n", submitted, submitted ? " -- " : "", submitted ? err : "");
    if (!submitted)
        printf("                    accepted, signal value %llu\n", (unsigned long long)value);

    err[0] = 0;
    drained = vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    printf("    poolWaitIdle:   %d (%s)%s%s\n", drained, drainName(drained),
        drained ? " -- " : "", drained ? err : "");
    return submitted != 0 || drained == gdDeviceLost;
}

int main(int argc, char **argv) {
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
    uint32_t iterations = 1000000000u;
    uint32_t expected, got;
    uint64_t submitted = 0, t0, t1;
    int waited, drained, reported = 0, matched;
    uint32_t *words;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1)
        iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    printf("L1: does the next submission report device loss after an unnoticed reset?\n");
    printf("    %lu iterations, seed 0x%08lx\n\n",
        (unsigned long)iterations, (unsigned long)SEED);

    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &handles, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!vk) { printf("no functions: %s\n", err); return 3; }
    {
        VSVulkanCoreInfo info;
        if (!vkapi->getVulkanCoreInfo(core, &info, err, sizeof(err))) {
            printf("    device: %s\n", info.deviceName);
            printf("    unified memory: %s\n", info.unifiedMemory ? "yes" : "no");
        }
        printf("    compute queue family %u, transfer queue family %u -> dedicated transfer: %s\n\n",
            handles.computeQueueFamily, handles.transferQueueFamily,
            handles.computeQueueFamily == handles.transferQueueFamily ? "no" : "yes");
    }

    step("compute the CPU reference");
    expected = referenceValue(iterations, SEED);
    printf("     expected result: %lu (0x%08lx)\n", (unsigned long)expected, (unsigned long)expected);

    step("compile the work shader");
    shader = vkapi->compileGPUShader(core, 0, workShader, log, sizeof(log));
    if (!shader) { printf("compile failed: %s\n", log); return 3; }
    spirv = vkapi->getGPUShaderCode(shader, &spirvBytes);

    step("create the storage buffer and seed it");
    buffer = vkapi->createGPUBuffer(core, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
        &binfo, err, sizeof(err));
    if (!buffer) { printf("createGPUBuffer failed: %s\n", err); return 3; }
    words = (uint32_t *)binfo.mapped;
    memset(words, 0, 4096);
    /* data[0] starts at the seed, so a dispatch that never ran leaves the seed behind and the
       check below sees the input rather than a plausible-looking zero. */
    words[0] = SEED;
    words[1] = iterations;
    words[2] = SEED;

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

    step("record and submit the dispatch");
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    {
        VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
        VkMemoryBarrier2 barrier;
        VkDependencyInfo dep;
        vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descWrite);
        vk->vkCmdDispatch(cmd, 1, 1, 1);
        /* The host reads data[0] below, and the dispatch completing does not by itself put its
           writes in the host's reach. Without this an unchanged word would have a second
           possible explanation, which is exactly the ambiguity this probe exists to remove. */
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        memset(&dep, 0, sizeof(dep));
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        vk->vkCmdPipelineBarrier2(cmd, &dep);
    }
    vkapi->gpuExecRetain(ctx, &countingRelease, (void *)&SEED, 0);
    t0 = probe_millis();
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) {
        printf("gpuExecSubmit failed: %s\n", err); return 3;
    }
    printf("     submitted, signalling value %llu\n", (unsigned long long)submitted);

    step("wait for it through the core, which is the call that would report a reset");
    err[0] = 0;
    waited = vkapi->gpuExecWaitValue(pool, submitted, err, sizeof(err));
    t1 = probe_millis();
    printf("     gpuExecWaitValue: %d (%s)%s%s\n", waited, drainName(waited),
        waited ? " -- " : "", waited ? err : "");
    printf("     submit to wait return: %llu ms\n", (unsigned long long)(t1 - t0));

    step("check the result against the CPU reference");
    got = words[0];
    matched = (got == expected);
    printf("     got      %lu (0x%08lx)\n", (unsigned long)got, (unsigned long)got);
    printf("     expected %lu (0x%08lx)%s\n", (unsigned long)expected, (unsigned long)expected,
        got == SEED ? "   [got is the untouched seed: the dispatch did not run]" : "");
    printf("     %s\n", matched ? "MATCH" : "MISMATCH");

    step("drain the pool");
    err[0] = 0;
    drained = vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    printf("     gpuExecPoolWaitIdle: %d (%s)%s%s\n", drained, drainName(drained),
        drained ? " -- " : "", drained ? err : "");
    printf("     release callbacks: %ld of 1\n", (long)releaseCalls);

    step("THE QUESTION: submit again on the same device");
    reported = askAgain(1);
    reported |= askAgain(2);

    printf("\nresults:\n");
    printf("  wait reported:            %s\n", drainName(waited));
    printf("  result matched reference: %s\n", matched ? "yes" : "NO");
    printf("  drain reported:           %s\n", drainName(drained));
    printf("  a later call reported the loss: %s\n", reported ? "yes" : "no");
    printf("\nverdict: ");
    if (matched && waited == gdDrained) {
        printf("no reset happened. Raise the iteration count until the result mismatches.\n");
    } else if (!matched && waited != gdDrained) {
        printf("the reset was detected. The core reported it and no stale result was\n");
        printf("         accepted, which is the behaviour the Windows sentinel gives.\n");
    } else if (!matched && waited == gdDrained && reported) {
        printf("F8 confirmed, and BOUNDED. The wait accepted stale output, but a later\n");
        printf("         call reported the loss, so the damage is the work in flight at the reset\n");
        printf("         and every frame after it becomes a visible error. Record this in section\n");
        printf("         2a of vsvulkanexec_protocol.md.\n");
    } else if (!matched && waited == gdDrained && !reported) {
        printf("F8 confirmed and UNBOUNDED. The wait accepted stale output and nothing\n");
        printf("         afterwards reported the loss, so every frame for the life of the core is\n");
        printf("         silently wrong. This is worse than currently documented and reopens the\n");
        printf("         question of proof-of-execution checking.\n");
    } else {
        printf("unexpected combination; record the whole log.\n");
    }

    step("teardown");
    if (vsGPUDrainSafeToDestroy(vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err)))) {
        if (pipeline) vk->vkDestroyPipeline(handles.device, pipeline, NULL);
        if (pipelineLayout) vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
        if (setLayout) vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
        if (module) vk->vkDestroyShaderModule(handles.device, module, NULL);
        vkapi->destroyGPUBuffer(buffer);
    } else {
        printf("     drain failed, so the pipeline objects and the buffer are left alone\n");
    }
    vkapi->freeGPUShader(shader);
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    printf("     teardown completed\n");
    return matched ? 0 : 4;
}
