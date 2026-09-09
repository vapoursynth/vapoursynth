/* L22: randomized stress over the legal plugin-facing GPU API.

   Every probe so far encodes an interleaving someone thought of. This one does not: N threads each
   loop over random operations drawn from the documented-legal subset of the API -- acquire, retain,
   read and write frames, submit, abandon, wait on submitted values, drain, allocate and free memory,
   grow and shrink reservations (which sweeps), create and free frames and copies, and churn private
   pools -- under a small VRAM limit so the admission gate and the allocation ladder are live. The
   rules that keep every op legal are enforced by the fuzzer itself, so a fatal from the core is a
   real finding rather than misuse, and a thread that stops making progress is reported by name.

     api_fuzz [seconds] [seed] [threads] [hang-at]   defaults 30, time-based, 8, never

   With hang-at set, the main thread submits an infinite dispatch that many seconds into the
   run while the workers keep fuzzing, and the driver resets the device a few seconds later:
   I29 under random load. Every call must still return, the loss must be reported to at least
   one call, retentions must still balance after the drains, and teardown must complete.
   Refusals carrying the device-lost message are then expected and counted, not failures.
   A core log handler counts messages by level, which is also the oracle when the run is made
   with VS_VULKAN_VALIDATION=1: validation messages arrive through the core log.

   Passes when every thread finishes its run, every retention it made was released exactly once
   after the drains, and teardown completes. Print the seed on failure: it reproduces the run.
   Meant to be run under ThreadSanitizer and the lock-order build as well as plain.

   Second round adds the ownership-transferring and refcounted surface, the class the WritesPlane
   defect fell into: gpuExecUsesBuffer and gpuExecUsesMemory (the handle is consumed, on submit
   AND on abandon), timelines (create, addRef, free, publish an already-reached value, publish
   host-ready), exported memory and semaphore handles (each a new fd the caller closes -- the fd
   count is checked at the end, so a handle leaked inside the core shows), the shader cache
   (identical sources compiled concurrently share words), and waitGPUFrame.

   Third round makes the work real and crosses the threads. Submissions carry actual dispatches
   (a storage buffer handed to the context, or the plane of a frame the thread is writing), so
   completion takes time and sweeps race real timelines rather than empty batches; frames a
   thread has written are published to a ring other threads read, wait on, copy and export
   (cross-pool waits on producer values); every shared pool's newest submitted value is waited
   on from any thread through gpuExecWaitValue and the pool timeline; setMaxVRAMUse moves the
   limit and the gate budget under load; the queue lock is taken as the leaf bracket it is
   documented as; buffers are destroyed only after their submission completed; a second core
   is brought up and torn down beside the first; and retentions of 24 MB trip the gate. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe_compat.h"
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#endif

#define MAXTHREADS 16
#define SHARED_POOLS 3
#define FRAMES 6
#define MEMS 4

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *shared[SHARED_POOLS];
static VSVideoFormat fmt;
static unsigned long long deadline;
static volatile LONG stopAll = 0;

struct Frame { VSFrame *f; int shared; int written; };

struct Worker {
    int idx;
    uint64_t rng;
    VSGPUExecContext *ctx;      /* at most one context, ever (I16, I26) */
    int ctxPool;                /* index into shared[], or -1 for the private pool */
    VSGPUExecPool *priv;        /* a pool only this thread uses */
    struct Frame frames[FRAMES];
    int nframes;
    VSGPUMemory *mems[MEMS];
    int nmems;
    VSGPUMemoryReservation *res;
    uint64_t submitted[SHARED_POOLS]; /* newest value this thread submitted on each shared pool */
    VSGPUTimeline *tl;          /* one timeline of our own, or NULL */
    int tlRefs;                 /* references we hold on it: created 1, addRef +1, free -1 */
    VSGPUShader *shaders[2];
    long handedOver;            /* buffers and memory whose ownership went to a context */
    long exports;
    volatile LONG progress;
    volatile LONG retains, releases;
    long ops;
    long opsAfterLoss;
    char lastOp[40];
};
static struct Worker workers[MAXTHREADS];
static int nthreads = 8;

/* Third round state: pipelines for real dispatches, a ring of frames threads publish to each
   other, the newest submitted value per shared pool, and the queue handle for the leaf bracket. */
static const VSVulkanFunctions *vk;
static VSVulkanCoreHandles handles;
static VkQueue computeQueue = VK_NULL_HANDLE;
static VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkPipeline pipelines[2];
static VkShaderModule modules[2];
static VSGPUShader *cachedShaders[2];
static int dispatchReady = 0;
#define RING 8
static VSFrame *ring[RING];
static probe_mutex_t ringLock; /* a real mutex: see probe_compat.h on why not a spin lock */
static void ringEnter(void) { probe_mutex_lock(&ringLock); }
static void ringLeave(void) { probe_mutex_unlock(&ringLock); }
static volatile LONG newest[SHARED_POOLS];
static volatile LONG ringPublished = 0, ringUsed = 0, dispatches = 0, secondCores = 0;

/* The reset injection and what it is measured by. */
static int hangAt = 0;
static volatile LONG hangInjected = 0;
static unsigned long long hangTime = 0;
static volatile LONG lostReports = 0;
static LONG firstReportMs = -1;
static char firstReportCall[40];
static VkPipeline hangPipeline = VK_NULL_HANDLE;
static VkShaderModule hangModule = VK_NULL_HANDLE;
static VSGPUShader *hangShaderObj;
static VSGPUBuffer *hangBuffer;
static VSVulkanBufferInfo hangBinfo;
static const char *hangShaderSrc =
    "#version 450\nlayout(local_size_x = 64) in;\nlayout(std430, set = 0, binding = 0) buffer B { uint data[]; };\n"
    "void main() { uint acc = gl_GlobalInvocationID.x + 1u; while (data[0] == 0u) { acc = acc * 1664525u + 1013904223u; data[1u + (acc & 0xFFFFu)] = acc; } data[1] = acc; }\n";
static int isLoss(const char *err) { return strstr(err, "reset") != NULL || strstr(err, "VkResult -4") != NULL || strstr(err, "DEVICE_LOST") != NULL; }
/* Runs on whichever thread heard it first; the first one names the call in the results. */
static void sawLoss(const char *call) {
    if (InterlockedIncrement(&lostReports) == 1) {
        firstReportMs = (LONG)(probe_millis() - hangTime);
        strncpy(firstReportCall, call, sizeof(firstReportCall) - 1);
    }
}
/* The core log, by level, with the first few at warning and above kept for the results. */
static volatile LONG logByType[5];
static volatile LONG logKept = 0;
static char firstLog[3][300];
static void VS_CC logHandler(int msgType, const char *msg, void *userData) {
    (void)userData;
    if (msgType >= 0 && msgType < 5) InterlockedIncrement(&logByType[msgType]);
    if (msgType >= mtWarning) {
        LONG k = InterlockedIncrement(&logKept) - 1;
        if (k < 3) strncpy(firstLog[k], msg, sizeof(firstLog[k]) - 1);
    }
}

static uint64_t next64(struct Worker *w) { /* xorshift64* */
    uint64_t x = w->rng; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; w->rng = x;
    return x * 2685821657736338717ull;
}
static unsigned pick(struct Worker *w, unsigned n) { return (unsigned)(next64(w) >> 33) % n; }
static void note(struct Worker *w, const char *op) {
    strncpy(w->lastOp, op, sizeof(w->lastOp) - 1); w->lastOp[sizeof(w->lastOp) - 1] = 0;
    InterlockedIncrement(&w->progress); w->ops++;
    if (InterlockedGet(&lostReports)) w->opsAfterLoss++;
}

/* Runs on whichever thread sweeps; counts against the worker that retained. */
static void VS_CC countRelease(void *object) { InterlockedIncrement(&((struct Worker *)object)->releases); }

static DWORD WINAPI watchdog(LPVOID p) {
    LONG last[MAXTHREADS]; int idle[MAXTHREADS]; int i;
    (void)p;
    for (i = 0; i < MAXTHREADS; i++) { last[i] = -1; idle[i] = 0; }
    for (;;) {
        Sleep(1000);
        if (InterlockedGet(&stopAll) == 2) return 0;
        for (i = 0; i < nthreads; i++) {
            LONG cur = InterlockedGet(&workers[i].progress);
            if (cur == last[i]) {
                if (++idle[i] >= 60) {
                    printf("\nFAIL: thread %d made no progress for 60 s; last op: %s (ctx %s)\n",
                        i, workers[i].lastOp, workers[i].ctx ? "held" : "none");
                    printf("FAILED\n"); fflush(stdout);
                    TerminateProcess(GetCurrentProcess(), 1);
                }
            } else idle[i] = 0;
            last[i] = cur;
        }
    }
}

static const char *shaderSrc[2] = {
    "#version 450\nlayout(local_size_x = 64) in;\nlayout(std430, set = 0, binding = 0) buffer B { uint d[]; };\n"
    "void main() { d[gl_GlobalInvocationID.x] += 1u; }\n",
    "#version 450\nlayout(local_size_x = 32) in;\nlayout(std430, set = 0, binding = 0) buffer B { float f[]; };\n"
    "void main() { f[gl_GlobalInvocationID.x] *= 0.5; }\n"
};

static int buildPipelines(void) {
    char log[512];
    int k;
    VkDescriptorSetLayoutBinding binding = { 0 };
    VkDescriptorSetLayoutCreateInfo slInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPipelineLayoutCreateInfo plInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    slInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    slInfo.bindingCount = 1;
    slInfo.pBindings = &binding;
    if (vk->vkCreateDescriptorSetLayout(handles.device, &slInfo, NULL, &setLayout) != VK_SUCCESS) return 0;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &setLayout;
    if (vk->vkCreatePipelineLayout(handles.device, &plInfo, NULL, &pipelineLayout) != VK_SUCCESS) return 0;
    for (k = 0; k < 2; k++) {
        VkShaderModuleCreateInfo smInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkComputePipelineCreateInfo cpInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        size_t bytes = 0;
        cachedShaders[k] = vkapi->compileGPUShader(core, 0, shaderSrc[k], log, sizeof(log));
        if (!cachedShaders[k]) { printf("  (compile failed: %s)\n", log); return 0; }
        smInfo.pCode = vkapi->getGPUShaderCode(cachedShaders[k], &bytes);
        smInfo.codeSize = bytes;
        if (vk->vkCreateShaderModule(handles.device, &smInfo, NULL, &modules[k]) != VK_SUCCESS) return 0;
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = modules[k];
        cpInfo.stage.pName = "main";
        cpInfo.layout = pipelineLayout;
        if (vk->vkCreateComputePipelines(handles.device, VK_NULL_HANDLE, 1, &cpInfo, NULL, &pipelines[k]) != VK_SUCCESS) return 0;
    }
    return 1;
}
static int buildHangPipeline(void) {
    char log[512], err[256];
    VkShaderModuleCreateInfo smInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkComputePipelineCreateInfo cpInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    size_t bytes = 0;
    hangShaderObj = vkapi->compileGPUShader(core, 0, hangShaderSrc, log, sizeof(log));
    if (!hangShaderObj) { printf("  (hang shader failed: %s)\n", log); return 0; }
    smInfo.pCode = vkapi->getGPUShaderCode(hangShaderObj, &bytes);
    smInfo.codeSize = bytes;
    if (vk->vkCreateShaderModule(handles.device, &smInfo, NULL, &hangModule) != VK_SUCCESS) return 0;
    cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = hangModule;
    cpInfo.stage.pName = "main";
    cpInfo.layout = pipelineLayout;
    if (vk->vkCreateComputePipelines(handles.device, VK_NULL_HANDLE, 1, &cpInfo, NULL, &hangPipeline) != VK_SUCCESS) return 0;
    hangBuffer = vkapi->createGPUBuffer(core, 1 << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &hangBinfo, err, sizeof(err));
    if (!hangBuffer) { printf("  (hang buffer failed: %s)\n", err); return 0; }
    memset(hangBinfo.mapped, 0, 1 << 20); /* data[0] == 0 forever */
    return 1;
}
/* Main's one submission: the infinite dispatch, while the workers carry on. */
static void injectHang(void) {
    char err[256];
    VkDescriptorBufferInfo dbInfo;
    VkWriteDescriptorSet dw;
    VkCommandBuffer cmd;
    uint64_t v = 0;
    VSGPUExecContext *ctx = vkapi->gpuExecAcquire(shared[0], err, sizeof(err));
    if (!ctx) { printf("  hang: acquire refused: %s\n", err); return; }
    cmd = vkapi->gpuExecCommandBuffer(ctx);
    memset(&dw, 0, sizeof(dw));
    dbInfo.buffer = hangBinfo.buffer; dbInfo.offset = 0; dbInfo.range = VK_WHOLE_SIZE;
    dw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dw.dstBinding = 0; dw.descriptorCount = 1;
    dw.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dw.pBufferInfo = &dbInfo;
    vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hangPipeline);
    vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &dw);
    vk->vkCmdDispatch(cmd, 4096, 1, 1);
    hangTime = probe_millis();
    if (vkapi->gpuExecSubmit(ctx, &v, err, sizeof(err))) { printf("  hang: submit failed: %s\n", err); return; }
    InterlockedExchange(&hangInjected, 1);
    printf("  hang submitted %d s in as value %llu on pool 0; the driver should reset it within a few seconds\n", hangAt, (unsigned long long)v);
}
static void destroyPipelines(void) {
    int k;
    if (hangPipeline) vk->vkDestroyPipeline(handles.device, hangPipeline, NULL);
    if (hangModule) vk->vkDestroyShaderModule(handles.device, hangModule, NULL);
    if (hangShaderObj) vkapi->freeGPUShader(hangShaderObj);
    if (hangBuffer) vkapi->destroyGPUBuffer(hangBuffer);
    for (k = 0; k < 2; k++) {
        if (pipelines[k]) vk->vkDestroyPipeline(handles.device, pipelines[k], NULL);
        if (modules[k]) vk->vkDestroyShaderModule(handles.device, modules[k], NULL);
        if (cachedShaders[k]) vkapi->freeGPUShader(cachedShaders[k]);
    }
    if (pipelineLayout) vk->vkDestroyPipelineLayout(handles.device, pipelineLayout, NULL);
    if (setLayout) vk->vkDestroyDescriptorSetLayout(handles.device, setLayout, NULL);
}
/* Records shader k over the first `bytes` of `buffer`: whole workgroups only, capped, so
   nothing runs past the end and no single dispatch takes long. */
static void recordDispatch(VSGPUExecContext *ctx, unsigned k, VkBuffer buffer, VkDeviceSize bytes) {
    VkCommandBuffer cmd = vkapi->gpuExecCommandBuffer(ctx);
    VkDescriptorBufferInfo dbInfo;
    VkWriteDescriptorSet dw;
    uint32_t local = k ? 32u : 64u, groups = (uint32_t)(bytes / 4) / local;
    if (!groups) return;
    if (groups > 4096) groups = 4096;
    memset(&dw, 0, sizeof(dw));
    dbInfo.buffer = buffer; dbInfo.offset = 0; dbInfo.range = bytes;
    dw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dw.dstBinding = 0; dw.descriptorCount = 1;
    dw.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dw.pBufferInfo = &dbInfo;
    vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[k]);
    vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &dw);
    vk->vkCmdDispatch(cmd, groups, 1, 1);
    InterlockedIncrement(&dispatches);
}
/* A second core's whole GPU life beside the first: bring-up, one frame, teardown. */
static void secondCoreLife(void) {
    char err[256];
    VSVulkanCoreHandles h2;
    VSCore *c2 = vsapi->createCore(0);
    if (!c2) return;
    if (!vkapi->getVulkanHandles(c2, &h2, err, sizeof(err))) {
        VSFrame *f = vkapi->newGPUVideoFrame(&fmt, 64, 64, NULL, c2);
        if (f) vsapi->freeFrame(f);
    }
    vsapi->freeCore(c2);
    InterlockedIncrement(&secondCores);
}

/* Open descriptors, minus the validation layer's own: with VS_VULKAN_VALIDATION=1 the Khronos
   layer opens its log_filename once per instance and never closes it, so every second core and
   every enumeration leaves one behind that is the layer's to leak, not the core's. Counted
   separately and reported, not held against the run. */
static int openFds(int *layerLog) {
#ifdef _WIN32
    if (layerLog) *layerLog = 0;
    return 0;
#else
    DIR *d = opendir("/proc/self/fd"); struct dirent *e; int n = 0, lay = 0;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char path[300], target[256]; ssize_t len;
        if (e->d_name[0] == '.') continue;
        n++;
        snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
        len = readlink(path, target, sizeof(target) - 1);
        if (len > 0) { target[len] = 0; if (strstr(target, "vk_validation")) lay++; }
    }
    closedir(d);
    if (layerLog) *layerLog = lay;
    return n - lay;
#endif
}
/* What the descriptors point at, by kind, so a leak report names its source. */
static void printFdKinds(void) {
#ifndef _WIN32
    char kinds[8][96]; int counts[8], nk = 0, i;
    DIR *d = opendir("/proc/self/fd"); struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char path[300], target[256]; ssize_t len; char *b;
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
        len = readlink(path, target, sizeof(target) - 1);
        if (len <= 0) continue;
        target[len] = 0;
        if ((b = strchr(target, '[')) != NULL) *b = 0; /* socket:[n], pipe:[n], anon_inode:[x] -> the kind */
        for (i = 0; i < nk; i++) if (!strncmp(kinds[i], target, sizeof(kinds[i]) - 1)) break;
        if (i == nk && nk < 8) { strncpy(kinds[nk], target, sizeof(kinds[nk]) - 1); kinds[nk][sizeof(kinds[nk]) - 1] = 0; counts[nk++] = 0; }
        if (i < nk) counts[i]++;
    }
    closedir(d);
    for (i = 0; i < nk; i++) printf("      %4d x %s\n", counts[i], kinds[i]);
#endif
}
static void closeHandle(intptr_t h) {
#ifndef _WIN32
    if (h >= 0) close((int)h);
#else
    (void)h;
#endif
}

static void freeFrameSlot(struct Worker *w, int i) {
    vsapi->freeFrame(w->frames[i].f);
    w->frames[i] = w->frames[--w->nframes];
}

static DWORD WINAPI run(LPVOID p) {
    struct Worker *w = (struct Worker *)p;
    char err[512];
    while (probe_millis() < deadline && !InterlockedGet(&stopAll)) {
        unsigned op = pick(w, 130);
        err[0] = 0;
        if (op < 18) {                                   /* acquire on a shared pool */
            if (!w->ctx) {
                int i = (int)pick(w, SHARED_POOLS);
                note(w, "acquire");
                w->ctx = vkapi->gpuExecAcquire(shared[i], err, sizeof(err));
                if (w->ctx) w->ctxPool = i;
                else if (InterlockedGet(&hangInjected) && isLoss(err)) sawLoss("acquire");
                else { printf("  thread %d: acquire refused: %s\n", w->idx, err); InterlockedExchange(&stopAll, 1); return 0; }
            }
        } else if (op < 30) {                            /* retain something on the recording */
            if (w->ctx) {
                note(w, "retain");
                vkapi->gpuExecRetain(w->ctx, &countRelease, w, pick(w, 8) == 0 ? (6 << 20) : (256 << 10));
                InterlockedIncrement(&w->retains);
            }
        } else if (op < 38) {                            /* declare a read of one of our frames */
            if (w->ctx && w->nframes) { note(w, "readsFrame"); vkapi->gpuExecReadsFrame(w->ctx, w->frames[pick(w, (unsigned)w->nframes)].f); }
        } else if (op < 44) {                            /* declare a write: only a frame we own outright, once */
            if (w->ctx && w->nframes) {
                int i = (int)pick(w, (unsigned)w->nframes);
                if (!w->frames[i].shared && !w->frames[i].written) {
                    note(w, "writesPlane");
                    vkapi->gpuExecWritesPlane(w->ctx, w->frames[i].f, 0);
                    w->frames[i].written = 1;
                }
            }
        } else if (op < 58) {                            /* submit */
            if (w->ctx) {
                uint64_t v = 0;
                note(w, "submit");
                if (vkapi->gpuExecSubmit(w->ctx, &v, err, sizeof(err))) {
                    /* A failed submit ends the recording and returns its retentions like an abandon. */
                    w->ctx = NULL;
                    if (InterlockedGet(&hangInjected) && isLoss(err)) sawLoss("submit");
                    else { printf("  thread %d: submit failed: %s\n", w->idx, err); InterlockedExchange(&stopAll, 1); return 0; }
                } else {
                    if (w->ctxPool >= 0) { w->submitted[w->ctxPool] = v; InterlockedExchange(&newest[w->ctxPool], (LONG)v); }
                    w->ctx = NULL;
                }
            }
        } else if (op < 62) {                            /* abandon */
            if (w->ctx) { note(w, "abandon"); vkapi->gpuExecAbandon(w->ctx); w->ctx = NULL; }
        } else if (op < 68) {                            /* wait on a value we submitted */
            int i = (int)pick(w, SHARED_POOLS);
            if (w->submitted[i]) { note(w, "waitValue"); if (vkapi->gpuExecWaitValue(shared[i], w->submitted[i], err, sizeof(err)) == gdDeviceLost) sawLoss("waitValue"); }
        } else if (op < 71) {                            /* drain a shared pool: only holding nothing (I18) */
            if (!w->ctx) { note(w, "waitIdle"); if (vkapi->gpuExecPoolWaitIdle(shared[pick(w, SHARED_POOLS)], err, sizeof(err)) == gdDeviceLost) sawLoss("waitIdle"); }
        } else if (op < 77) {                            /* allocate / free pooled memory */
            if (w->nmems < MEMS && pick(w, 2)) {
                VkMemoryRequirements req; VSVulkanMemoryInfo info;
                req.size = (VkDeviceSize)(64 << 10) << pick(w, 5); req.alignment = 256; req.memoryTypeBits = 0xFFFFFFFFu;
                note(w, "allocate");
                w->mems[w->nmems] = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
                if (w->mems[w->nmems]) w->nmems++;
            } else if (w->nmems) {
                note(w, "freeMemory");
                vkapi->freeGPUMemory(w->mems[--w->nmems]);
            }
        } else if (op < 82) {                            /* reservation churn: an increase past the limit sweeps */
            note(w, "reservation");
            vkapi->updateGPUMemoryReservation(w->res, pick(w, 3) ? 0 : (int64_t)(200 << 20));
        } else if (op < 90) {                            /* frames: create, copy, free */
            unsigned k = pick(w, 3);
            if (k == 0 && w->nframes < FRAMES) {
                note(w, "newFrame");
                w->frames[w->nframes].f = vkapi->newGPUVideoFrame(&fmt, 64 << pick(w, 3), 64, NULL, core);
                w->frames[w->nframes].shared = 0; w->frames[w->nframes].written = 0;
                if (w->frames[w->nframes].f) w->nframes++;
            } else if (k == 1 && w->nframes && w->nframes < FRAMES) {
                int i = (int)pick(w, (unsigned)w->nframes);
                note(w, "copyFrame");
                w->frames[w->nframes].f = vsapi->copyFrame(w->frames[i].f, core);
                w->frames[w->nframes].shared = 1; w->frames[w->nframes].written = 1;
                w->frames[i].shared = 1; /* both share the planes now: neither may be written */
                if (w->frames[w->nframes].f) w->nframes++;
            } else if (w->nframes) {
                note(w, "freeFrame");
                freeFrameSlot(w, (int)pick(w, (unsigned)w->nframes));
            }
        } else if (op < 92) {                            /* ownership transfer: a buffer or a region the context now owns */
            if (w->ctx) {
                if (pick(w, 2)) {
                    VSVulkanBufferInfo bi;
                    VSGPUBuffer *b;
                    note(w, "usesBuffer");
                    b = vkapi->createGPUBuffer(core, 256 << 10, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &bi, err, sizeof(err));
                    if (b) { vkapi->gpuExecUsesBuffer(w->ctx, b); w->handedOver++; } /* consumed: never destroyed here */
                } else {
                    VkMemoryRequirements req; VSVulkanMemoryInfo info; VSGPUMemory *m;
                    req.size = 64 << 10; req.alignment = 256; req.memoryTypeBits = 0xFFFFFFFFu;
                    note(w, "usesMemory");
                    m = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
                    if (m) { vkapi->gpuExecUsesMemory(w->ctx, m); w->handedOver++; }  /* consumed too */
                }
            }
        } else if (op < 95) {                            /* our own timeline: create, extra ref, free, publish */
            unsigned k = pick(w, 4);
            if (!w->tl && k < 2) {
                note(w, "createTimeline");
                w->tl = vkapi->createGPUTimeline(core, err, sizeof(err));
                w->tlRefs = w->tl ? 1 : 0;
            } else if (w->tl && k == 2) {
                note(w, "timelineRefs");
                vkapi->addGPUTimelineRef(w->tl); w->tlRefs++;
                vkapi->freeGPUTimeline(w->tl); w->tlRefs--;
            } else if (w->tl && k == 3 && w->nframes) {
                int i = (int)pick(w, (unsigned)w->nframes);
                if (!w->frames[i].shared && !w->frames[i].written) {
                    note(w, "publish");
                    /* value 0 is already reached on a fresh timeline; NULL publishes host-ready */
                    vkapi->setGPUPlaneProducer(w->frames[i].f, 0, pick(w, 2) ? w->tl : NULL, 0);
                    w->frames[i].written = 1;
                }
            } else if (w->tl && w->tlRefs == 1 && pick(w, 3) == 0) {
                note(w, "freeTimeline");
                vkapi->freeGPUTimeline(w->tl); w->tl = NULL; w->tlRefs = 0;
            }
        } else if (op < 97) {                            /* exported handles: a new fd each time, ours to close */
            if (w->nframes) {
                int i = (int)pick(w, (unsigned)w->nframes);
                if (pick(w, 2)) {
                    VSVulkanExportedMemory em;
                    note(w, "exportPlane");
                    if (!vkapi->exportGPUPlane(w->frames[i].f, 0, &em, err, sizeof(err))) { closeHandle(em.handle); w->exports++; }
                } else {
                    VSVulkanPlaneInfo pi; VSVulkanExportedSemaphore es;
                    note(w, "exportSemaphore");
                    if (!vkapi->getGPUPlane(w->frames[i].f, 0, &pi) && pi.readySemaphore &&
                        !vkapi->exportGPUSemaphore(core, pi.readySemaphore, &es, err, sizeof(err))) { closeHandle(es.handle); w->exports++; }
                    else if (w->tl && !vkapi->exportGPUSemaphore(core, vkapi->getGPUTimelineSemaphore(w->tl), &es, err, sizeof(err))) { closeHandle(es.handle); w->exports++; }
                }
            }
        } else if (op < 99) {                            /* the shader cache: identical sources from every thread */
            unsigned k = pick(w, 2);
            if (!w->shaders[k]) {
                char log[512];
                note(w, "compileShader");
                w->shaders[k] = vkapi->compileGPUShader(core, 0, shaderSrc[k], log, sizeof(log));
                if (w->shaders[k]) { size_t n = 0; (void)vkapi->getGPUShaderCode(w->shaders[k], &n); }
            } else {
                note(w, "freeShader");
                vkapi->freeGPUShader(w->shaders[k]); w->shaders[k] = NULL;
            }
        } else if (op < 100) {                           /* waitGPUFrame, or a private pool's whole life */
            if (w->nframes && pick(w, 2)) {
                note(w, "waitGPUFrame");
                if (vkapi->waitGPUFrame(w->frames[pick(w, (unsigned)w->nframes)].f, err, sizeof(err)) && isLoss(err)) sawLoss("waitGPUFrame");
            } else if (!w->ctx) {
                VSGPUExecContext *c; uint64_t v = 0;
                note(w, "privatePool");
                w->priv = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
                if (w->priv) {
                    c = vkapi->gpuExecAcquire(w->priv, err, sizeof(err));
                    if (c) {
                        if (w->nframes) vkapi->gpuExecReadsFrame(c, w->frames[0].f);
                        vkapi->gpuExecRetain(c, &countRelease, w, 128 << 10); InterlockedIncrement(&w->retains);
                        if (pick(w, 4)) vkapi->gpuExecSubmit(c, &v, err, sizeof(err)); else vkapi->gpuExecAbandon(c);
                    }
                    vkapi->freeGPUExecPool(w->priv); /* runs its releases */
                    w->priv = NULL;
                }
            } else { note(w, "sleep"); Sleep(pick(w, 3)); }
        } else if (op < 106) {                           /* a real dispatch over a buffer the context then owns */
            if (dispatchReady && w->ctx) {
                VSVulkanBufferInfo bi;
                VSGPUBuffer *b;
                note(w, "dispatch");
                b = vkapi->createGPUBuffer(core, 256 << 10, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &bi, err, sizeof(err));
                if (b) {
                    memset(bi.mapped, 1, 256 << 10);
                    recordDispatch(w->ctx, pick(w, 2), bi.buffer, 256 << 10);
                    vkapi->gpuExecUsesBuffer(w->ctx, b); w->handedOver++;
                }
            }
        } else if (op < 109) {                           /* write a plane for real: the declaration plus a dispatch into it */
            if (dispatchReady && w->ctx && w->nframes) {
                int i = (int)pick(w, (unsigned)w->nframes);
                VSVulkanPlaneInfo pi;
                if (!w->frames[i].shared && !w->frames[i].written && !vkapi->getGPUPlane(w->frames[i].f, 0, &pi)) {
                    note(w, "writePlaneDispatch");
                    vkapi->gpuExecWritesPlane(w->ctx, w->frames[i].f, 0);
                    recordDispatch(w->ctx, 0, pi.buffer, pi.bufferSize);
                    w->frames[i].written = 1;
                }
            }
        } else if (op < 112) {                           /* publish a written frame for the other threads; never written again */
            if (!w->ctx && w->nframes) {
                int i = (int)pick(w, (unsigned)w->nframes);
                if (w->frames[i].written) {
                    VSFrame *old, *mine = (VSFrame *)vsapi->addFrameRef(w->frames[i].f);
                    unsigned slot = pick(w, RING);
                    note(w, "shareFrame");
                    ringEnter(); old = ring[slot]; ring[slot] = mine; ringLeave();
                    if (old) vsapi->freeFrame(old);
                    w->frames[i].shared = 1;
                    InterlockedIncrement(&ringPublished);
                }
            }
        } else if (op < 118) {                           /* use another thread's frame: read, wait, copy, export, inspect */
            VSFrame *f;
            unsigned slot = pick(w, RING);
            ringEnter(); f = ring[slot]; if (f) vsapi->addFrameRef(f); ringLeave();
            if (f) {
                unsigned k = pick(w, 5);
                note(w, "useShared");
                if (k == 0) { if (w->ctx) vkapi->gpuExecReadsFrame(w->ctx, f); }
                else if (k == 1) { if (vkapi->waitGPUFrame(f, err, sizeof(err)) && isLoss(err)) sawLoss("waitGPUFrame"); }
                else if (k == 2) { VSFrame *c = vsapi->copyFrame(f, core); if (c) vsapi->freeFrame(c); }
                else if (k == 3) { VSVulkanExportedMemory em; if (!vkapi->exportGPUPlane(f, 0, &em, err, sizeof(err))) { closeHandle(em.handle); w->exports++; } }
                else { VSVulkanPlaneInfo pi; (void)vkapi->getGPUPlane(f, 0, &pi); }
                vsapi->freeFrame(f);
                InterlockedIncrement(&ringUsed);
            }
        } else if (op < 121) {                           /* wait on the newest value anyone submitted on a shared pool */
            int i = (int)pick(w, SHARED_POOLS);
            uint64_t v = (uint64_t)InterlockedGet(&newest[i]);
            if (v) {
                note(w, "waitOther");
                if (pick(w, 2)) { if (vkapi->gpuTimelineWaitValue(vkapi->gpuExecPoolTimeline(shared[i]), v, err, sizeof(err)) == gdDeviceLost) sawLoss("timelineWait"); }
                else if (vkapi->gpuExecWaitValue(shared[i], v, err, sizeof(err)) == gdDeviceLost) sawLoss("waitOther");
            }
        } else if (op < 123) {                           /* move the limit and the gate budget under everyone */
            VSVulkanCoreInfo ci;
            note(w, "vramLimit");
            vkapi->setMaxVRAMUse((int64_t)(64 << pick(w, 4)) << 20, core);
            (void)vkapi->setMaxVRAMUse(0, core);
            (void)vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err));
        } else if (op < 125) {                           /* the queue lock as the leaf it is documented as */
            if (!w->ctx && computeQueue) {
                note(w, "queueLock");
                vkapi->lockVulkanQueue(core, vqCompute);
                vk->vkQueueSubmit2(computeQueue, 0, NULL, VK_NULL_HANDLE);
                vkapi->unlockVulkanQueue(core, vqCompute);
            }
        } else if (op < 127) {                           /* a buffer's life: destroyed only after its submission completed */
            VSVulkanBufferInfo bi;
            VSGPUBuffer *b;
            note(w, "bufferLife");
            b = vkapi->createGPUBuffer(core, 128 << 10, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &bi, err, sizeof(err));
            if (b) {
                if (dispatchReady && w->ctx) {
                    uint64_t v = 0;
                    int pool = w->ctxPool;
                    memset(bi.mapped, 2, 128 << 10);
                    recordDispatch(w->ctx, 1, bi.buffer, 128 << 10);
                    if (vkapi->gpuExecSubmit(w->ctx, &v, err, sizeof(err))) {
                        w->ctx = NULL;
                        if (InterlockedGet(&hangInjected) && isLoss(err)) sawLoss("submit");
                        else { printf("  thread %d: submit failed: %s\n", w->idx, err); InterlockedExchange(&stopAll, 1); return 0; }
                    } else {
                        w->ctx = NULL;
                        if (pool >= 0) { w->submitted[pool] = v; InterlockedExchange(&newest[pool], (LONG)v); if (vkapi->gpuExecWaitValue(shared[pool], v, err, sizeof(err)) == gdDeviceLost) sawLoss("waitValue"); }
                    }
                }
                vkapi->destroyGPUBuffer(b);
            }
        } else if (op < 128) {                           /* a second core beside this one, or the device list */
            if (!w->ctx && pick(w, 64) == 0) { note(w, "secondCore"); secondCoreLife(); }
            else if (pick(w, 16) == 0) { VSVulkanDeviceListEntry e[4]; note(w, "enumerate"); (void)vkapi->enumerateVulkanDevices(e, 4, err, sizeof(err)); }
        } else if (op < 129) {                           /* a retention big enough to trip the gate on its own */
            if (w->ctx) {
                note(w, "heavyRetain");
                vkapi->gpuExecRetain(w->ctx, &countRelease, w, 24 << 20);
                InterlockedIncrement(&w->retains);
            }
        } else {                                         /* yield */
            note(w, "sleep"); Sleep(pick(w, 3));
        }
    }
    /* Wind down in an order that keeps every call legal. */
    note(w, "winddown");
    if (w->ctx) { vkapi->gpuExecAbandon(w->ctx); w->ctx = NULL; }
    while (w->nframes) freeFrameSlot(w, 0);
    if (w->tl) { while (w->tlRefs-- > 0) vkapi->freeGPUTimeline(w->tl); w->tl = NULL; }
    if (w->shaders[0]) { vkapi->freeGPUShader(w->shaders[0]); w->shaders[0] = NULL; }
    if (w->shaders[1]) { vkapi->freeGPUShader(w->shaders[1]); w->shaders[1] = NULL; }
    while (w->nmems) vkapi->freeGPUMemory(w->mems[--w->nmems]);
    vkapi->updateGPUMemoryReservation(w->res, 0);
    return 0;
}

int main(int argc, char **argv) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    HANDLE t[MAXTHREADS];
    HANDLE dog;
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    uint64_t seed = argc > 2 ? strtoull(argv[2], NULL, 10) : (uint64_t)probe_millis();
    long totalOps = 0, totalRetains = 0, totalReleases = 0, totalHanded = 0, totalExports = 0;
    int fdsBefore = -1, fdsAfter = -1, layerBefore = 0, layerAfter = 0;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 3) nthreads = atoi(argv[3]);
    if (argc > 4) hangAt = atoi(argv[4]);
    if (nthreads < 1) nthreads = 1;
    if (nthreads > MAXTHREADS) nthreads = MAXTHREADS;
    probe_setenv("VS_VULKAN_MAX_VRAM_MB", "64"); /* gate budget 16 MB: retentions and reservations trip it */
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vsapi->addLogHandler(logHandler, NULL, NULL, core);
    vsapi->setThreadCount(4, core); /* 4-context rings: full often, never trivially */
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vsapi->queryVideoFormat(&fmt, cfGray, stInteger, 8, 0, 0, core);
    handles = h;
    vk = vkapi->getVulkanFunctions(core, err, sizeof(err));
    dispatchReady = vk && buildPipelines();
    if (!dispatchReady) printf("  (no real dispatches this run)\n");
    if (hangAt && !(dispatchReady && buildHangPipeline())) { printf("  cannot build the hang; not injecting\n"); hangAt = 0; }
    if (vk) {
        VkDeviceQueueInfo2 qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2 };
        qi.queueFamilyIndex = h.computeQueueFamily;
        qi.queueIndex = h.computeQueueIndex;
        vk->vkGetDeviceQueue2(h.device, &qi, &computeQueue);
    }
    printf("L22: %d threads, %d s, seed %llu%s%d s  (rerun with these to reproduce)\n\n", nthreads, seconds, (unsigned long long)seed,
        hangAt ? ", hang injected at " : ", no hang, ", hangAt);

    probe_mutex_init(&ringLock);
    for (i = 0; i < SHARED_POOLS; i++) {
        shared[i] = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
        if (!shared[i]) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    }
    for (i = 0; i < nthreads; i++) {
        memset(&workers[i], 0, sizeof(workers[i]));
        workers[i].idx = i;
        workers[i].rng = seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(i + 1));
        workers[i].ctxPool = -1;
        workers[i].res = vkapi->reserveGPUMemory(core, 0, err, sizeof(err));
        if (!workers[i].res) { printf("reserveGPUMemory failed: %s\n", err); return 3; }
    }
    /* fd baseline AFTER one warm-up export, so the driver's lazily opened descriptors are in it. */
    {
        VSFrame *warm = vkapi->newGPUVideoFrame(&fmt, 64, 64, NULL, core);
        VSVulkanExportedMemory em;
        if (warm) {
            if (!vkapi->exportGPUPlane(warm, 0, &em, err, sizeof(err))) closeHandle(em.handle);
            else printf("  (export unavailable here: %s)\n", err);
            vsapi->freeFrame(warm);
        }
    }
    /* And after one second core and one enumeration, for the same reason. */
    secondCoreLife();
    { VSVulkanDeviceListEntry e[4]; (void)vkapi->enumerateVulkanDevices(e, 4, err, sizeof(err)); }
    InterlockedExchange(&secondCores, 0);
    fdsBefore = openFds(&layerBefore);
    deadline = probe_millis() + (unsigned long long)seconds * 1000ull;
    dog = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    for (i = 0; i < nthreads; i++) t[i] = CreateThread(NULL, 0, run, &workers[i], 0, NULL);
    if (hangAt > 0 && WaitForMultipleObjects((unsigned)nthreads, t, TRUE, (unsigned)hangAt * 1000u) == WAIT_TIMEOUT)
        injectHang();
    WaitForMultipleObjects((unsigned)nthreads, t, TRUE, INFINITE);
    for (i = 0; i < nthreads; i++) CloseHandle(t[i]);

    /* Drains make every remaining release run before the counts are compared. */
    for (i = 0; i < SHARED_POOLS; i++) if (vkapi->gpuExecPoolWaitIdle(shared[i], err, sizeof(err)) == gdDeviceLost) sawLoss("drain");
    for (i = 0; i < RING; i++) if (ring[i]) { vsapi->freeFrame(ring[i]); ring[i] = NULL; }
    for (i = 0; i < nthreads; i++) vkapi->releaseGPUMemoryReservation(workers[i].res);
    for (i = 0; i < SHARED_POOLS; i++) vkapi->freeGPUExecPool(shared[i]);
    destroyPipelines();
    fdsAfter = openFds(&layerAfter);
    InterlockedExchange(&stopAll, 2);
    /* The watchdog returns on that flag; joined so it is not the one "thread leak" TSan reports. */
    WaitForMultipleObjects(1, &dog, TRUE, INFINITE);
    CloseHandle(dog);
    vsapi->freeCore(core);

    printf("results:\n");
    for (i = 0; i < nthreads; i++) {
        totalOps += workers[i].ops; totalRetains += workers[i].retains; totalReleases += workers[i].releases;
        totalHanded += workers[i].handedOver; totalExports += workers[i].exports;
        printf("  thread %d: %6ld ops, retains %ld, releases %ld%s\n", i, workers[i].ops,
            (long)workers[i].retains, (long)workers[i].releases,
            workers[i].retains == workers[i].releases ? "" : "   <-- MISMATCH");
    }
    printf("  total: %ld ops, %ld retains, %ld releases, %ld handles handed to contexts, %ld exports\n",
        totalOps, totalRetains, totalReleases, totalHanded, totalExports);
    printf("  third round: %ld real dispatches, %ld frames published, %ld uses of another thread's frame, %ld second cores\n",
        (long)InterlockedGet(&dispatches), (long)InterlockedGet(&ringPublished), (long)InterlockedGet(&ringUsed), (long)InterlockedGet(&secondCores));
    printf("  open fds: %d before, %d after%s\n", fdsBefore, fdsAfter,
        (fdsBefore >= 0 && fdsAfter > fdsBefore + 2) ? "   <-- LEAKED DESCRIPTORS" : "");
    if (layerAfter) printf("     (plus the validation layer's log file: %d before, %d after, the layer's per-instance descriptor, excluded)\n", layerBefore, layerAfter);
    if (fdsBefore >= 0 && fdsAfter > fdsBefore + 2) printFdKinds();
    printf("  core log: %ld debug, %ld info, %ld warning, %ld critical\n", (long)logByType[0], (long)logByType[1], (long)logByType[2], (long)logByType[3]);
    for (i = 0; i < 3 && i < logKept; i++) printf("    %s%s\n", firstLog[i], strlen(firstLog[i]) >= 299 ? "..." : "");
    if (hangAt) {
        long after = 0;
        for (i = 0; i < nthreads; i++) after += workers[i].opsAfterLoss;
        printf("  reset: hang %s; loss reported %ld times, first %s ms after the hang via %s; %ld ops ran after the loss; %ld second cores\n",
            InterlockedGet(&hangInjected) ? "injected" : "NOT injected", (long)InterlockedGet(&lostReports),
            firstReportMs >= 0 ? "" : "never", firstReportMs >= 0 ? firstReportCall : "-", after, (long)InterlockedGet(&secondCores));
        if (firstReportMs >= 0) printf("         (first report at +%ld ms)\n", (long)firstReportMs);
    }
    if (InterlockedGet(&stopAll) == 1 || totalRetains != totalReleases || (fdsBefore >= 0 && fdsAfter > fdsBefore + 2)) {
        printf("FAILED (seed %llu)\n", (unsigned long long)seed); return 1;
    }
    if (!hangAt && InterlockedGet(&lostReports)) { printf("  FAIL: the device was lost without a hang being injected\nFAILED (seed %llu)\n", (unsigned long long)seed); return 1; }
    if (hangAt && InterlockedGet(&hangInjected) && !InterlockedGet(&lostReports)) { printf("  FAIL: the reset was never reported to any call\nFAILED (seed %llu)\n", (unsigned long long)seed); return 1; }
    printf("  teardown completed\nALL PASS\n");
    return 0;
}
