/* L19: a reset while workers are parked in the admission gate.

   execAdmissionGate blocks acquire while in-flight retained bytes exceed the budget (a quarter of
   the VRAM limit). It has explicit reset handling -- deviceLost() re-read every round, and the
   progress counter checked against the reset sentinel -- and none of it has ever run: L10 parked
   workers in gpuExecWaitValue, not the gate. This parks them in the gate with a hanging submission
   holding more bytes than the budget, then lets the driver reset it.

   What a woken worker may legitimately get depends on which exit the gate took, so the probe
   records the release count at the moment each acquire returned. Section 2a of the protocol
   measured that this driver stays silent through the wait and the counter query: the reset
   force-completes the hanging submission, the sweep releases its retentions, the bytes leave
   the total, and the gate opens on its ordinary exit with the loss not yet latched -- a context
   is then correct, and the next submit is the call that reports. A context handed out while
   the bytes were still above the budget means the gate left through its deviceLost exit and
   acquire still claimed a slot, which the protocol says it must not.

   Passes when every gated acquire returns within a few seconds of the reset, any context it got
   came with the bytes already released, the next submit reports the loss and the acquire after
   it returns NULL, the retentions are released exactly once, and teardown completes. A gated
   thread that never returns is the finding. */
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
static volatile LONG watchdogStep = 0;
static volatile LONG releaseCalls = 0;
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
    LONG last = -1; int idle = 0; (void)p;
    for (;;) {
        Sleep(1000);
        if (InterlockedGet(&watchdogStep) == last) {
            if (++idle >= 90) { printf("\nFAIL: stuck for 90 s in: %s\nFAILED\n", stepName); fflush(stdout); TerminateProcess(GetCurrentProcess(), 1); }
        } else idle = 0;
        last = InterlockedGet(&watchdogStep);
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

#define GATED 4
static volatile LONG gatedReturned = 0;
static int gatedGotContext[GATED];
static LONG gatedReleasesAtReturn[GATED];
static unsigned long long gatedMs[GATED];
static DWORD WINAPI gated(LPVOID p) {
    int idx = (int)(size_t)p; char err[512] = { 0 };
    unsigned long long t0 = probe_millis();
    VSGPUExecContext *c = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    gatedMs[idx] = probe_millis() - t0;
    gatedReleasesAtReturn[idx] = InterlockedGet(&releaseCalls);
    gatedGotContext[idx] = c != NULL;
    if (c) vkapi->gpuExecAbandon(c);
    InterlockedIncrement(&gatedReturned);
    return 0;
}
static const char *drainName(int r) {
    if (r == gdDrained) return "gdDrained";
    if (r == gdIncomplete) return "gdIncomplete";
    if (r == gdDeviceLost) return "gdDeviceLost";
    return "unknown";
}
static int latchedBefore = 0, nextSubmitReported = 0, acquireAfterNull = 0;
int main(void) {
    VSVulkanCoreInfo ci;
    long long budgetMB, perRetMB;
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
    uint64_t submitted = 0;
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    probe_setenv("VS_VULKAN_MAX_VRAM_MB", "64"); /* budget = limit / 4 = 16 MB */
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    printf("L19: a reset with %d workers parked in the admission gate\n     (the display will freeze for a second while the driver resets)\n\n", GATED);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &handles, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!vk) { printf("no functions: %s\n", err); return 3; }
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { printf("no info: %s\n", err); return 3; }
    /* The env limit is floored by the core, so size the retention from what it actually granted:
       the gate budget is a quarter of the limit, and the first run of this probe retained a
       fixed 64 MB against a limit it never read, gated nobody, and misreported healthy acquires
       as contexts handed out on a reset device. Aim for twice the budget. */
    budgetMB = (long long)(ci.limit >> 20) / 4;
    perRetMB = (budgetMB * 2) / RETAINED + 1;
    printf("     limit %lld MB, gate budget %lld MB, retaining %d x %lld MB = %lld MB\n",
        (long long)(ci.limit >> 20), budgetMB, RETAINED, perRetMB, perRetMB * RETAINED);

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

    step("submit the hanging dispatch retaining more bytes than the whole budget");
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    {
        VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
        vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descWrite);
        vk->vkCmdDispatch(cmd, 4096, 1, 1);
    }
    for (i = 0; i < RETAINED; i++) { retainedTag[i] = i; vkapi->gpuExecRetain(ctx, &countingRelease, &retainedTag[i], (int64_t)perRetMB << 20); }
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) { printf("gpuExecSubmit failed: %s\n", err); return 3; }
    printf("     submitted value %llu; %lld MB in flight against a %lld MB budget\n",
        (unsigned long long)submitted, perRetMB * RETAINED, budgetMB);

    step("park workers in acquire -- they should block in the admission gate");
    {
        HANDLE t[GATED];
        for (i = 0; i < GATED; i++) t[i] = CreateThread(NULL, 0, gated, (LPVOID)(size_t)i, 0, NULL);
        Sleep(1500);
        printf("     after 1.5 s: %ld of %d returned (0 = they are gated as intended)\n", InterlockedGet(&gatedReturned), GATED);
        if (InterlockedGet(&gatedReturned) != 0) {
            /* No verdict about resets can come from a run where the gate never engaged. */
            printf("\nNOT GATED: %ld worker(s) acquired on a healthy device before any reset, so the\n"
                   "           retention did not exceed the real budget. Setup failure, not a core result.\n", InterlockedGet(&gatedReturned));
            printf("FAILED (setup)\n");
            return 2;
        }
        step("wait for the driver to reset the hang, and for the gate to notice");
        WaitForMultipleObjects(GATED, t, TRUE, INFINITE);
        for (i = 0; i < GATED; i++) CloseHandle(t[i]);
    }
    printf("\nper-worker: (a context is correct only once the bytes had left: releases %d of %d at return)\n", RETAINED, RETAINED);
    for (i = 0; i < GATED; i++)
        printf("  worker %d: returned after %5llu ms, got context: %s, releases at return: %ld of %d\n",
            i, gatedMs[i], gatedGotContext[i] ? "YES" : "no", gatedReleasesAtReturn[i], RETAINED);

    step("the call after: the next submit is what reports the loss, and the acquire after it must refuse");
    {
        VSGPUExecContext *after = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        uint64_t v = 0;
        int drained;
        if (!after) {
            printf("     acquire: NULL (%s) -- the loss was already latched\n", err);
            latchedBefore = 1;
        } else {
            err[0] = 0;
            nextSubmitReported = vkapi->gpuExecSubmit(after, &v, err, sizeof(err)) != 0;
            printf("     submit: %s%s%s\n", nextSubmitReported ? "reported" : "ACCEPTED (silent)", nextSubmitReported ? " -- " : "", nextSubmitReported ? err : "");
            err[0] = 0;
            after = vkapi->gpuExecAcquire(pool, err, sizeof(err));
            acquireAfterNull = after == NULL;
            printf("     acquire after it: %s%s%s\n", after ? "a context" : "NULL", after ? "" : " -- ", after ? "" : err);
            if (after) vkapi->gpuExecAbandon(after);
        }
        err[0] = 0;
        drained = vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
        printf("     poolWaitIdle: %d (%s)%s%s\n", drained, drainName(drained), drained ? " -- " : "", drained ? err : "");
    }

    step("free the pool");
    vkapi->freeGPUExecPool(pool);
    vkapi->destroyGPUBuffer(buffer);
    vk->vkDestroyPipeline(handles.device, pipeline, NULL);
    vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
    vk->vkDestroyShaderModule(handles.device, module, NULL);
    vkapi->freeGPUShader(shader);
    step("freeCore must complete");
    vsapi->freeCore(core);

    printf("\nresults:\n  every gated acquire returned: yes\n");
    printf("  release callbacks: %ld of %d\n", InterlockedGet(&releaseCalls), RETAINED);
    for (i = 0; i < RETAINED; i++)
        if (retainedSeen[i] != 1) { printf("  FAIL: retention %d released %ld times\nFAILED\n", i, retainedSeen[i]); return 1; }
    if (InterlockedGet(&releaseCalls) != RETAINED) { printf("FAILED\n"); return 1; }
    for (i = 0; i < GATED; i++)
        if (gatedGotContext[i] && gatedReleasesAtReturn[i] != RETAINED) {
            printf("  FAIL: worker %d got a context with %ld of %d releases run, so the bytes were still above the\n"
                   "        budget: the gate left through its deviceLost exit and acquire still claimed a slot\nFAILED\n", i, gatedReleasesAtReturn[i], RETAINED);
            return 1;
        }
    if (latchedBefore) {
        printf("  the loss was latched before the call after; nothing to measure there\n");
    } else if (!nextSubmitReported) {
        printf("  FINDING: the next submit was accepted on the reset device -- the stack section 2a calls\n"
               "           unknown, one that stays silent on the submit too. The core cannot report this reset.\nFAILED\n");
        return 1;
    } else if (!acquireAfterNull) {
        printf("  FAIL: the acquire after a reported loss returned a context\nFAILED\n");
        return 1;
    }
    printf("  teardown completed\nALL PASS\n");
    return 0;
}
