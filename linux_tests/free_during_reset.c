/* L20: a reset landing while freeGPUExecPool is draining, with sweeps racing it.

   ~VSVulkanExecPool unregisters, then waitAll()s; a reset mid-wait makes waitAll fail and
   deviceLost() true, which is the "completed" branch that runs every release and destroys the
   command pools. p16 reaches that path with nothing else happening. Here a second thread grows and
   shrinks a reservation the whole time, which forces sweepExecPools on a pool that has just been
   unregistered -- the interleaving unregisterExecPool's comment says is safe and nothing has run.

   Passes when the free returns, all 8 retentions are released exactly once, the sweeping thread
   never faults, and freeCore completes. */
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

static VSGPUMemoryReservation *reservation;
static volatile LONG stop = 0, sweeps = 0;
static int64_t limitBytes;
static DWORD WINAPI sweeper(LPVOID p) {
    (void)p;
    while (!InterlockedGet(&stop)) {
        vkapi->updateGPUMemoryReservation(reservation, limitBytes * 2); /* past the limit: notifyCaches -> sweep */
        vkapi->updateGPUMemoryReservation(reservation, 0);
        InterlockedIncrement(&sweeps);
        Sleep(1);
    }
    return 0;
}
static DWORD WINAPI freer(LPVOID p) { (void)p; vkapi->freeGPUExecPool(pool); return 0; }
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
    uint64_t submitted = 0;
    int i;
    VSVulkanCoreInfo ci;

    setvbuf(stdout, NULL, _IONBF, 0);
    probe_setenv("VS_VULKAN_MAX_VRAM_MB", "64");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    printf("L20: a reset while freeGPUExecPool drains, with a sweep storm racing it\n     (the display will freeze for a second while the driver resets)\n\n");
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &handles, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!vk) { printf("no functions: %s\n", err); return 3; }
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { printf("no info: %s\n", err); return 3; }
    limitBytes = ci.limit;

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

    step("create the exec pool and a reservation for the sweeper to churn");
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    reservation = vkapi->reserveGPUMemory(core, 0, err, sizeof(err));
    if (!reservation) { printf("reserveGPUMemory failed: %s\n", err); return 3; }

    step("submit the hanging dispatch with 8 retentions");
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    {
        VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
        vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descWrite);
        vk->vkCmdDispatch(cmd, 4096, 1, 1);
    }
    for (i = 0; i < RETAINED; i++) { retainedTag[i] = i; vkapi->gpuExecRetain(ctx, &countingRelease, &retainedTag[i], 1 << 20); }
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) { printf("gpuExecSubmit failed: %s\n", err); return 3; }

    step("start the sweep storm, then free the pool on another thread while the hang is in flight");
    {
        HANDLE ts = CreateThread(NULL, 0, sweeper, NULL, 0, NULL);
        HANDLE tf;
        Sleep(200);
        tf = CreateThread(NULL, 0, freer, NULL, 0, NULL);
        step("wait for the driver to reset the hang and for freeGPUExecPool to come back");
        WaitForMultipleObjects(1, &tf, TRUE, INFINITE);
        CloseHandle(tf);
        InterlockedExchange(&stop, 1);
        WaitForMultipleObjects(1, &ts, TRUE, INFINITE);
        CloseHandle(ts);
    }
    printf("     freeGPUExecPool returned; sweeps forced meanwhile: %ld\n", InterlockedGet(&sweeps));

    step("tear down");
    vkapi->releaseGPUMemoryReservation(reservation);
    vkapi->destroyGPUBuffer(buffer);
    vk->vkDestroyPipeline(handles.device, pipeline, NULL);
    vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
    vk->vkDestroyShaderModule(handles.device, module, NULL);
    vkapi->freeGPUShader(shader);
    step("freeCore must complete");
    vsapi->freeCore(core);

    printf("\nresults:\n  freeGPUExecPool returned through the reset: yes\n");
    printf("  release callbacks: %ld of %d\n", InterlockedGet(&releaseCalls), RETAINED);
    for (i = 0; i < RETAINED; i++)
        if (retainedSeen[i] != 1) { printf("  FAIL: retention %d released %ld times\nFAILED\n", i, retainedSeen[i]); return 1; }
    if (InterlockedGet(&releaseCalls) != RETAINED) { printf("FAILED\n"); return 1; }
    printf("  teardown completed\nALL PASS\n");
    return 0;
}
