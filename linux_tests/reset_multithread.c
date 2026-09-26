/* L10: a reset with several workers already parked in waits -- "the ordinary shape of a reset".

   Section 2a of vsvulkanexec_protocol.md describes exactly this and the fix written for it:
   "a wait already in progress when another thread latches the loss: the flag is read once more
   before a successful wait reports completion. Without it every worker sitting in a wait at the
   moment of a reset returned success and handed out its frame, while the worker that happened to
   submit next was already being told the device was gone -- with several workers in flight that
   is the ordinary shape of a reset."

   Both existing reset probes are single threaded, so that re-read has never executed. This one
   builds the shape it was written for: N workers parked in gpuExecWaitValue on a hanging
   submission, one latcher that submits after the reset (the call that reports the loss, per the
   L1 measurement), and a record of what every parked waiter returned and whether the loss had
   already been latched when it did.

   A waiter returning gdDrained after the loss is latched is what the re-read exists to prevent.
   Timing is reported rather than asserted where the two events can genuinely interleave. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

#define WORKERS 6

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static const VSVulkanFunctions *vk;
static VSCore *core;
static VSVulkanCoreHandles handles;
static VSGPUExecPool *pool;

static volatile LONG watchdogStep = 0;
static volatile LONG lossLatched = 0;
static volatile LONG releaseCalls = 0;
static volatile LONG waitersDone = 0;
static char stepName[128] = "startup";

static uint64_t hangingValue;
static int workerResult[WORKERS];
static int workerSawLatched[WORKERS];
static unsigned long long workerMs[WORKERS];

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
        if (InterlockedGet(&watchdogStep) == last) {
            if (++idle >= 90) {
                printf("\nFAIL: stuck for 90 s in: %s\n", stepName);
                printf("FAILED\n");
                fflush(stdout);
                TerminateProcess(GetCurrentProcess(), 1);
            }
        } else {
            idle = 0;
        }
        last = InterlockedGet(&watchdogStep);
    }
}

static void VS_CC countingRelease(void *object) {
    int *tag = (int *)object;
    InterlockedIncrement(&releaseCalls);
    InterlockedIncrement(&retainedSeen[*tag]);
}

/* Parks on the hanging submission's value. Whatever the reset does, this must come back. */
static DWORD WINAPI waiter(LPVOID p) {
    int idx = (int)(size_t)p;
    char err[512] = { 0 };
    unsigned long long t0 = probe_millis();
    workerResult[idx] = vkapi->gpuExecWaitValue(pool, hangingValue, err, sizeof(err));
    workerSawLatched[idx] = (int)InterlockedGet(&lossLatched);
    workerMs[idx] = probe_millis() - t0;
    InterlockedIncrement(&waitersDone);
    return 0;
}

/* Submits in a tight loop rather than after a fixed delay. Per L1 the submit is the call that
   reports the loss, and the interleaving section 2a's re-read guards is a wait STILL IN PROGRESS
   when another thread latches. A latcher that sleeps until after the waiters have returned can
   never produce that ordering; polling from the start is what gives the two windows a chance to
   overlap. Stops as soon as it has latched, or when the waiters are all done. */
static DWORD WINAPI latcher(LPVOID p) {
    char err[512] = { 0 };
    VSGPUExecContext *c;
    uint64_t v = 0;
    int submits = 0;
    (void)p;
    while (!InterlockedGet(&lossLatched) && InterlockedGet(&waitersDone) < WORKERS) {
        err[0] = 0;
        c = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!c) {
            printf("     latcher: acquire refused after %d submits (%s)\n", submits, err);
            InterlockedExchange(&lossLatched, 1);
            return 0;
        }
        if (vkapi->gpuExecSubmit(c, &v, err, sizeof(err))) {
            printf("     latcher: submit reported the loss after %d clean submits\n", submits);
            InterlockedExchange(&lossLatched, 1);
            return 0;
        }
        submits++;
        Sleep(20);
    }
    printf("     latcher: stopped after %d submits without seeing the loss\n", submits);
    return 0;
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
    HANDLE threads[WORKERS + 1];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    printf("L10: a reset with %d workers already parked in waits\n", WORKERS);
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

    step("submit the hanging dispatch with 8 retentions on it");
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
    if (vkapi->gpuExecSubmit(ctx, &hangingValue, err, sizeof(err))) {
        printf("gpuExecSubmit failed: %s\n", err); return 3;
    }
    printf("     submitted, signalling value %llu\n", (unsigned long long)hangingValue);

    step("park the workers in gpuExecWaitValue, then let one submit after the reset");
    for (i = 0; i < WORKERS; i++)
        threads[i] = CreateThread(NULL, 0, waiter, (LPVOID)(size_t)i, 0, NULL);
    threads[WORKERS] = CreateThread(NULL, 0, latcher, NULL, 0, NULL);

    step("every parked wait must come back -- none may sit there forever");
    WaitForMultipleObjects(WORKERS + 1, threads, TRUE, INFINITE);
    for (i = 0; i < WORKERS + 1; i++)
        CloseHandle(threads[i]);

    step("tear the pool down and count the retentions");
    vkapi->freeGPUExecPool(pool);
    vkapi->destroyGPUBuffer(buffer);
    vk->vkDestroyPipeline(handles.device, pipeline, NULL);
    vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
    vk->vkDestroyShaderModule(handles.device, module, NULL);
    vkapi->freeGPUShader(shader);

    step("freeCore must complete");
    vsapi->freeCore(core);

    step("done");
    printf("\nper-worker results (%d parked waiters):\n", WORKERS);
    for (i = 0; i < WORKERS; i++) {
        const char *name = workerResult[i] == gdDrained ? "gdDrained"
                         : workerResult[i] == gdDeviceLost ? "gdDeviceLost"
                         : workerResult[i] == gdIncomplete ? "gdIncomplete" : "?";
        printf("  worker %d: %-12s after %5llu ms   loss latched when it returned: %s\n",
            i, name, (unsigned long long)workerMs[i], workerSawLatched[i] ? "yes" : "no");
        if (workerResult[i] == gdDrained && workerSawLatched[i])
            bad++;
    }

    printf("\nresults:\n");
    printf("  every parked wait returned: yes (none hung)\n");
    printf("  release callbacks: %ld of %d\n", releaseCalls, RETAINED);
    for (i = 0; i < RETAINED; i++) {
        if (retainedSeen[i] != 1) {
            printf("  FAIL: retention %d released %ld times\n", i, retainedSeen[i]);
            printf("FAILED\n");
            return 1;
        }
    }
    if (releaseCalls != RETAINED) { printf("FAILED\n"); return 1; }
    printf("  teardown completed\n");

    if (bad) {
        printf("\n  NOTE: %d waiter(s) reported gdDrained with the loss already latched.\n"
               "        That is what section 2a's re-read exists to prevent, but the two\n"
               "        events can interleave between the wait returning and the flag being\n"
               "        read here, so treat it as a lead to confirm rather than a verdict.\n", bad);
    }
    printf("ALL PASS\n");
    return 0;
}
