/* L21: a reset that is reported while workers are still parked in the admission gate.

   L19 parks workers behind a hanging submission, and on this driver the reset force-completes
   that submission: its bytes leave the total and the gate opens on its ordinary exit before
   anything has latched the loss, so the gate's deviceLost exit never runs. This probe holds the
   gate shut with a submission the reset cannot complete -- a consumer queued behind the hang
   that waits, on the GPU, for a producer value nothing ever signals -- so the bytes stay above
   the budget through the reset. The main thread then makes the call section 2a measured as the
   one that reports, a submit on a second pool, and the loss is latched with the workers still
   parked. The only way out of the gate is now its deviceLost exit, and the question is what
   acquire hands the woken thread: the protocol says that after the latch acquire fails with
   deviceLostMessage, and acquire tests that flag only before the gate.

   Passes when the latch releases every parked worker promptly with NULL, a fresh acquire after
   the latch returns NULL, every retention is released exactly once and teardown completes. A
   worker handed a context after the latch is the contract gap; a worker still parked long after
   the latch is the hang. A host signal of the producer value is the release valve for a driver
   that stays silent on the submit too, so teardown never depends on the answer. */
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
static VSGPUExecPool *poolA, *poolB;
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
static const char *drainName(int r) {
    if (r == gdDrained) return "gdDrained";
    if (r == gdIncomplete) return "gdIncomplete";
    if (r == gdDeviceLost) return "gdDeviceLost";
    return "unknown";
}
static const char *hangShader =
    "#version 450\n"
    "layout(local_size_x = 64) in;\n"
    "layout(std430, set = 0, binding = 0) buffer B { uint data[]; };\n"
    "void main() {\n"
    "    uint acc = gl_GlobalInvocationID.x + 1u;\n"
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
static unsigned long long gatedAt[GATED];
static char gatedErr[GATED][256];
static unsigned long long parkedAt;
static DWORD WINAPI gated(LPVOID p) {
    int idx = (int)(size_t)p; char err[256] = { 0 };
    VSGPUExecContext *c = vkapi->gpuExecAcquire(poolA, err, sizeof(err));
    gatedAt[idx] = probe_millis();
    gatedReleasesAtReturn[idx] = InterlockedGet(&releaseCalls);
    gatedGotContext[idx] = c != NULL;
    strncpy(gatedErr[idx], err, sizeof(gatedErr[idx]) - 1);
    if (c) vkapi->gpuExecAbandon(c);
    InterlockedIncrement(&gatedReturned);
    return 0;
}
/* Polls the worker count rather than re-joining handles, so a timeout can be followed by
   another wait on the same threads. */
static int waitWorkers(unsigned ms) {
    unsigned long long until = probe_millis() + ms;
    while (InterlockedGet(&gatedReturned) < GATED && probe_millis() < until) Sleep(20);
    return InterlockedGet(&gatedReturned) == GATED;
}
static PFN_vkSignalSemaphore signalSemaphore;
static VSGPUTimeline *timeline;
static int valveUsed = 0;
static void valve(void) {
    VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
    VkResult vr;
    if (valveUsed) return;
    valveUsed = 1;
    if (!signalSemaphore) { printf("     (no host signal available; nothing to release the consumer with)\n"); return; }
    si.semaphore = vkapi->getGPUTimelineSemaphore(timeline);
    si.value = 1;
    vr = signalSemaphore(handles.device, &si);
    printf("     vkSignalSemaphore(producer value 1): %d\n", (int)vr);
}

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
    VSVideoFormat fmt;
    VSFrame *frame;
    VSGPUExecContext *ctx, *ctxLatch;
    uint64_t hangValue = 0, blockedValue = 0, latchValue = 0;
    HANDLE t[GATED];
    unsigned long long t0, latchAt = 0, resetAt = 0, valveAt = 0;
    int i, r, reported = 0, reportedBy = 0, afterNull = 0, failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    printf("L21: a reset reported while %d workers are parked in the admission gate\n     (the display will freeze for a second while the driver resets)\n\n", GATED);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &handles, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!vk) { printf("no functions: %s\n", err); return 3; }
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { printf("no info: %s\n", err); return 3; }
    budgetMB = (long long)(ci.limit >> 20) / 4;
    perRetMB = (budgetMB * 2) / RETAINED + 1;
    printf("     limit %lld MB, gate budget %lld MB, retaining %d x %lld MB = %lld MB\n",
        (long long)(ci.limit >> 20), budgetMB, RETAINED, perRetMB, perRetMB * RETAINED);
    {
        PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)handles.getInstanceProcAddr(handles.instance, "vkGetDeviceProcAddr");
        if (gdpa) signalSemaphore = (PFN_vkSignalSemaphore)gdpa(handles.device, "vkSignalSemaphore");
        printf("     release valve (host signal): %s\n", signalSemaphore ? "resolved" : "UNAVAILABLE");
    }

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

    step("create two exec pools: A carries the hang and the blocked consumer, B the call that reports");
    poolA = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!poolA) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    poolB = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!poolB) { printf("createGPUExecPool failed: %s\n", err); return 3; }

    step("a frame whose producer is a value nothing will signal");
    timeline = vkapi->createGPUTimeline(core, err, sizeof(err));
    if (!timeline) { printf("createGPUTimeline failed: %s\n", err); return 3; }
    vsapi->queryVideoFormat(&fmt, cfGray, stInteger, 8, 0, 0, core);
    frame = vkapi->newGPUVideoFrame(&fmt, 256, 256, NULL, core);
    if (!frame) { printf("newGPUVideoFrame failed\n"); return 3; }
    vkapi->setGPUPlaneProducer(frame, 0, timeline, 1);

    step("hold a context on pool B now, while the gate is open, for the call that will report");
    ctxLatch = vkapi->gpuExecAcquire(poolB, err, sizeof(err));
    if (!ctxLatch) { printf("gpuExecAcquire failed: %s\n", err); return 3; }

    step("submit the hanging dispatch on pool A, retaining nothing");
    ctx = vkapi->gpuExecAcquire(poolA, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    {
        VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
        vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descWrite);
        vk->vkCmdDispatch(cmd, 4096, 1, 1);
    }
    if (vkapi->gpuExecSubmit(ctx, &hangValue, err, sizeof(err))) { printf("gpuExecSubmit failed: %s\n", err); return 3; }

    step("queue a consumer behind it that waits on the GPU for the unsignalled producer, retaining twice the budget");
    ctx = vkapi->gpuExecAcquire(poolA, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    vkapi->gpuExecReadsFrame(ctx, frame);
    for (i = 0; i < RETAINED; i++) { retainedTag[i] = i; vkapi->gpuExecRetain(ctx, &countingRelease, &retainedTag[i], (int64_t)perRetMB << 20); }
    if (vkapi->gpuExecSubmit(ctx, &blockedValue, err, sizeof(err))) { printf("gpuExecSubmit failed: %s\n", err); return 3; }
    printf("     hang signals %llu, consumer signals %llu; %lld MB in flight against a %lld MB budget\n",
        (unsigned long long)hangValue, (unsigned long long)blockedValue, perRetMB * RETAINED, budgetMB);

    step("park workers in acquire on pool A -- they should block in the admission gate");
    parkedAt = probe_millis();
    for (i = 0; i < GATED; i++) t[i] = CreateThread(NULL, 0, gated, (LPVOID)(size_t)i, 0, NULL);
    Sleep(1500);
    printf("     after 1.5 s: %ld of %d returned (0 = they are gated as intended)\n", InterlockedGet(&gatedReturned), GATED);
    if (InterlockedGet(&gatedReturned) != 0) {
        printf("\nNOT GATED: a worker acquired on a healthy device before any reset. Setup failure, not a core result.\nFAILED (setup)\n");
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 2);
    }

    step("wait for the driver to reset the hang: gpuExecWaitValue on the hang's value");
    t0 = probe_millis();
    err[0] = 0;
    r = vkapi->gpuExecWaitValue(poolA, hangValue, err, sizeof(err));
    resetAt = probe_millis();
    printf("     returned %d (%s) after %llu ms%s%s\n", r, drainName(r), resetAt - t0, r ? " -- " : "", r ? err : "");
    if (r == gdDeviceLost) { reported = 1; reportedBy = 1; latchAt = t0; }
    Sleep(500); /* two of the gate's polls, so a worker the reset alone would free has left */
    printf("     after the reset, before the call that reports: %ld of %d workers returned, releases %ld of %d\n",
        InterlockedGet(&gatedReturned), GATED, InterlockedGet(&releaseCalls), RETAINED);
    if (InterlockedGet(&gatedReturned) == GATED)
        printf("     (the reset completed the dependent consumer too, so this run degenerates into L19)\n");

    step("the call that reports: submit on pool B");
    if (!latchAt) latchAt = probe_millis();
    err[0] = 0;
    if (vkapi->gpuExecSubmit(ctxLatch, &latchValue, err, sizeof(err))) {
        printf("     submit: reported -- %s\n", err);
        if (!reported) { reported = 1; reportedBy = 2; }
    } else {
        printf("     submit: ACCEPTED, signal value %llu (silent on this call%s)\n", (unsigned long long)latchValue,
            reported ? ", but the wait had already latched the loss" : "");
        if (!reported) printf("     the loss is not latched; the parked workers have no exit but the valve\n");
    }

    step("wait up to 10 s for the parked workers");
    if (waitWorkers(10000)) {
        printf("     all %d returned\n", GATED);
    } else {
        printf("     %ld of %d returned within 10 s\n", InterlockedGet(&gatedReturned), GATED);
        if (reported) { printf("  FAIL: workers still parked 10 s after the loss was latched\n"); failed = 1; }
        step("release valve: host-signal the producer value so the consumer can complete");
        valveAt = probe_millis();
        valve();
        if (!waitWorkers(10000)) {
            printf("  FAIL: workers still parked 10 s after the valve\nFAILED\n"); fflush(stdout);
            TerminateProcess(GetCurrentProcess(), 1);
        }
        printf("     all %d returned after the valve\n", GATED);
    }
    WaitForMultipleObjects(GATED, t, TRUE, INFINITE);
    for (i = 0; i < GATED; i++) CloseHandle(t[i]);

    printf("\nper-worker: (reset at ~+%llu ms; %s%s)\n", resetAt - parkedAt,
        reportedBy == 1 ? "the host wait reported it" : reportedBy == 2 ? "the submit after it reported it" : "nothing reported it before the valve",
        valveAt ? "; valve used" : "");
    if (valveAt) printf("  (valve at +%llu ms)\n", valveAt - parkedAt);
    for (i = 0; i < GATED; i++)
        printf("  worker %d: returned at +%5llu ms, got %s, releases at return: %ld of %d%s%s\n",
            i, gatedAt[i] - parkedAt,
            gatedGotContext[i] ? "a CONTEXT" : "NULL", gatedReleasesAtReturn[i], RETAINED,
            gatedGotContext[i] ? "" : " -- ", gatedGotContext[i] ? "" : gatedErr[i]);

    step("a fresh acquire on pool A after the latch");
    if (!reported) valve(); /* a healthy device would park this one too */
    err[0] = 0;
    ctx = vkapi->gpuExecAcquire(poolA, err, sizeof(err));
    afterNull = ctx == NULL;
    printf("     acquire: %s%s%s\n", ctx ? "a context" : "NULL", ctx ? "" : " -- ", ctx ? "" : err);
    if (ctx) vkapi->gpuExecAbandon(ctx);

    step("release valve, then drain both pools");
    valve();
    err[0] = 0; r = vkapi->gpuExecPoolWaitIdle(poolA, err, sizeof(err));
    printf("     pool A: %d (%s)%s%s\n", r, drainName(r), r ? " -- " : "", r ? err : "");
    err[0] = 0; r = vkapi->gpuExecPoolWaitIdle(poolB, err, sizeof(err));
    printf("     pool B: %d (%s)%s%s\n", r, drainName(r), r ? " -- " : "", r ? err : "");
    step("free the pools");
    vkapi->freeGPUExecPool(poolA);
    vkapi->freeGPUExecPool(poolB);
    step("free the frame -- its plane waits out the producer");
    t0 = probe_millis();
    vsapi->freeFrame(frame);
    printf("     freeFrame returned after %llu ms\n", probe_millis() - t0);
    vkapi->freeGPUTimeline(timeline);
    vkapi->destroyGPUBuffer(buffer);
    vk->vkDestroyPipeline(handles.device, pipeline, NULL);
    vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
    vk->vkDestroyShaderModule(handles.device, module, NULL);
    vkapi->freeGPUShader(shader);
    step("freeCore must complete");
    vsapi->freeCore(core);

    printf("\nresults:\n  release callbacks: %ld of %d\n", InterlockedGet(&releaseCalls), RETAINED);
    for (i = 0; i < RETAINED; i++)
        if (retainedSeen[i] != 1) { printf("  FAIL: retention %d released %ld times\n", i, retainedSeen[i]); failed = 1; }
    if (InterlockedGet(&releaseCalls) != RETAINED) failed = 1;
    if (!reported)
        printf("  NOTE: neither the host wait nor the submit after the reset reported it. The submit was accepted\n"
               "        because the queue already held a submission waiting on an unsignalled value, and this\n"
               "        stack then defers later submissions in userspace, so vkQueueSubmit2 had nothing to report;\n"
               "        the loss reached the core through a wait once the valve let the deferred work be submitted.\n");
    for (i = 0; i < GATED; i++) {
        if (gatedGotContext[i] && gatedReleasesAtReturn[i] != RETAINED) {
            printf("  CONTRACT GAP: worker %d was handed a context with the bytes still above the budget, so the gate\n"
                   "        left on the loss and acquire still claimed a slot.\n", i);
            failed = 1;
        } else if (!gatedGotContext[i] && !strstr(gatedErr[i], "reset"))
            printf("  NOTE: worker %d was refused for another reason: %s\n", i, gatedErr[i]);
    }
    if (!afterNull) { printf("  FAIL: a fresh acquire after the workers had heard the loss returned a context\n"); failed = 1; }
    if (failed) { printf("FAILED\n"); return 1; }
    printf("  every parked worker left the gate on the loss and was refused; teardown completed\nALL PASS\n");
    return 0;
}
