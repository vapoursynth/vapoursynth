/* Practical GPU stress for the paths the suites and the earlier probes never reach.
   Subcommands, each with a watchdog so a hang is reported rather than hung on:

     contention  more threads than a ring has contexts, so acquire's slow path, the claimCv
                 rendezvous and failIfHoldingForeignContext all actually run
     churn       setMaxVRAMUse churn and pressure sweeps against continuous submits
     cores       several cores brought up, used and freed concurrently, repeatedly
     outlive     GPU frames outliving the core that made them (the documented contract)
     exhaust     allocate in bounded steps until the driver refuses, to reach the ladder's
                 pressure rung and its failure rung
*/
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;

static volatile LONG stop = 0;
static volatile LONG progress = 0;
static volatile LONG failures = 0;
static volatile LONG slowPathHits = 0;

static void fail(const char *what, const char *err) {
    InterlockedIncrement(&failures);
    printf("  FAIL %s: %s\n", what, err ? err : "");
}

/* Reports a hang instead of waiting forever. Returns 1 when everything finished. */
static int joinOrReport(HANDLE *threads, int count, int seconds, const char *what) {
    int i;
    LONG last = -1, stuck = 0;
    for (i = 0; i < seconds * 2; i++) {
        DWORD w = WaitForMultipleObjects(count, threads, TRUE, 500);
        if (w != WAIT_TIMEOUT)
            return 1;
        LONG cur = InterlockedGet(&progress);
        if (cur == last) {
            if (++stuck >= 20) { /* 10 s with no thread advancing */
                printf("  FAIL %s: no progress for 10 s -> HANG (progress stuck at %ld)\n", what, cur);
                InterlockedIncrement(&failures);
                return 0;
            }
        } else {
            stuck = 0;
        }
        last = cur;
    }
    printf("  FAIL %s: still running after %d s\n", what, seconds);
    InterlockedIncrement(&failures);
    return 0;
}

/* ------------------------------------------------------------------ contention */
static VSCore *sharedCore;
static VSGPUExecPool *poolA;
static VSGPUExecPool *poolB;

static DWORD WINAPI contender(LPVOID p) {
    char err[512];
    int i;
    const int which = (int)(intptr_t)p;
    for (i = 0; i < 3000 && !InterlockedGet(&stop); i++) {
        VSGPUExecPool *pool = ((i + which) & 1) ? poolA : poolB;
        VSGPUExecContext *ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!ctx) { fail("gpuExecAcquire", err); return 1; }
        /* Hold it a moment so the ring really fills and later acquirers must wait. */
        if ((i & 7) == 0)
            Sleep(0);
        if (i & 1) {
            vkapi->gpuExecAbandon(ctx);
        } else if (vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err))) {
            fail("gpuExecSubmit", err); return 1;
        }
        InterlockedIncrement(&progress);
    }
    return 0;
}

static int runContention(void) {
    char err[512];
    HANDLE threads[32];
    int i, ok;
    printf("contention: 32 threads over two pools\n");
    sharedCore = vsapi->createCore(0);
    poolA = vkapi->createGPUExecPool(sharedCore, vqCompute, err, sizeof(err));
    poolB = vkapi->createGPUExecPool(sharedCore, vqCompute, err, sizeof(err));
    if (!poolA || !poolB) { fail("createGPUExecPool", err); return 1; }
    for (i = 0; i < 32; i++)
        threads[i] = CreateThread(NULL, 0, contender, (LPVOID)(intptr_t)i, 0, NULL);
    ok = joinOrReport(threads, 32, 180, "contention");
    if (!ok)
        return 1;
    for (i = 0; i < 32; i++)
        CloseHandle(threads[i]);
    if (vkapi->gpuExecPoolWaitIdle(poolA, err, sizeof(err))) fail("waitIdle A", err);
    if (vkapi->gpuExecPoolWaitIdle(poolB, err, sizeof(err))) fail("waitIdle B", err);
    vkapi->freeGPUExecPool(poolA);
    vkapi->freeGPUExecPool(poolB);
    vsapi->freeCore(sharedCore);
    printf("  %ld acquire/submit-or-abandon cycles\n", InterlockedGet(&progress));
    return 0;
}

/* ------------------------------------------------------------------ churn */
static DWORD WINAPI submitter2(LPVOID p) {
    char err[512];
    (void)p;
    while (!InterlockedGet(&stop)) {
        VSGPUExecContext *ctx = vkapi->gpuExecAcquire(poolA, err, sizeof(err));
        if (!ctx) { fail("gpuExecAcquire", err); return 1; }
        if (vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err))) { fail("gpuExecSubmit", err); return 1; }
        InterlockedIncrement(&progress);
    }
    return 0;
}

static DWORD WINAPI limitChurner(LPVOID p) {
    int i = 0;
    (void)p;
    /* Every setMaxVRAMUse moves the exec retention budget, so the admission gate's target
       changes under threads already sleeping on it. */
    while (!InterlockedGet(&stop)) {
        vkapi->setMaxVRAMUse((i++ & 1) ? (256ll << 20) : (2048ll << 20), sharedCore);
        InterlockedIncrement(&progress);
    }
    return 0;
}

static DWORD WINAPI allocChurner(LPVOID p) {
    char err[512];
    VkMemoryRequirements req;
    VSVulkanMemoryInfo info;
    (void)p;
    req.size = 32u << 20;
    req.alignment = 256;
    req.memoryTypeBits = 0xFFFFFFFFu;
    while (!InterlockedGet(&stop)) {
        VSGPUMemory *mem = vkapi->allocateGPUMemory(sharedCore, &req,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
        if (mem)
            vkapi->freeGPUMemory(mem);
        InterlockedIncrement(&progress);
    }
    return 0;
}

static int runChurn(void) {
    char err[512];
    HANDLE threads[10];
    int i;
    printf("churn: 8 submitters against a VRAM-limit churner and an allocator, 20 s\n");
    sharedCore = vsapi->createCore(0);
    poolA = vkapi->createGPUExecPool(sharedCore, vqCompute, err, sizeof(err));
    if (!poolA) { fail("createGPUExecPool", err); return 1; }
    for (i = 0; i < 8; i++)
        threads[i] = CreateThread(NULL, 0, submitter2, NULL, 0, NULL);
    threads[8] = CreateThread(NULL, 0, limitChurner, NULL, 0, NULL);
    threads[9] = CreateThread(NULL, 0, allocChurner, NULL, 0, NULL);
    Sleep(20000);
    InterlockedExchange(&stop, 1);
    if (!joinOrReport(threads, 10, 60, "churn"))
        return 1;
    for (i = 0; i < 10; i++)
        CloseHandle(threads[i]);
    vkapi->freeGPUExecPool(poolA);
    vsapi->freeCore(sharedCore);
    printf("  %ld operations\n", progress);
    return 0;
}

/* ------------------------------------------------------------------ cores */
static DWORD WINAPI coreCycler(LPVOID p) {
    char err[512];
    int i, j;
    (void)p;
    for (i = 0; i < 6 && !stop; i++) {
        VSCore *core = vsapi->createCore(0);
        VSVulkanCoreHandles h;
        VSGPUExecPool *pool;
        VSVideoFormat fmt;
        if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { fail("getVulkanHandles", err); return 1; }
        pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
        if (!pool) { fail("createGPUExecPool", err); return 1; }
        vsapi->queryVideoFormat(&fmt, cfYUV, stInteger, 8, 1, 1, core);
        for (j = 0; j < 20; j++) {
            VSGPUExecContext *ctx;
            VSFrame *frame = vkapi->newGPUVideoFrame(&fmt, 512, 512, NULL, core);
            if (frame)
                vsapi->freeFrame(frame);
            ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
            if (ctx && vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err)))
                fail("gpuExecSubmit", err);
        }
        vkapi->freeGPUExecPool(pool);
        vsapi->freeCore(core);
        InterlockedIncrement(&progress);
    }
    return 0;
}

static int runCores(void) {
    HANDLE threads[4];
    int i;
    printf("cores: 4 threads x 6 cores, each with its own device, pool and frames\n");
    for (i = 0; i < 4; i++)
        threads[i] = CreateThread(NULL, 0, coreCycler, NULL, 0, NULL);
    if (!joinOrReport(threads, 4, 300, "cores"))
        return 1;
    for (i = 0; i < 4; i++)
        CloseHandle(threads[i]);
    printf("  %ld core lifecycles\n", progress);
    return 0;
}

/* ------------------------------------------------------------------ outlive */
#define KEPT 64
static int runOutlive(void) {
    char err[512];
    VSCore *core;
    VSGPUExecPool *pool;
    VSVideoFormat fmt;
    VSFrame *kept[KEPT];
    int i;
    printf("outlive: %d GPU frames kept past freeCore, then freed\n", KEPT);
    core = vsapi->createCore(0);
    { VSVulkanCoreInfo ci; if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { fail("getVulkanCoreInfo", err); return 1; } }
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { fail("createGPUExecPool", err); return 1; }
    vsapi->queryVideoFormat(&fmt, cfYUV, stInteger, 8, 1, 1, core);

    for (i = 0; i < KEPT; i++) {
        VSGPUExecContext *ctx;
        uint64_t signaled = 0;
        kept[i] = vkapi->newGPUVideoFrame(&fmt, 640, 480, NULL, core);
        if (!kept[i]) { fail("newGPUVideoFrame", "returned NULL"); return 1; }
        /* Give the frame a real producer pair on the pool's timeline, so the plane holds a
           counted reference to a timeline whose pool is about to be destroyed. */
        ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!ctx) { fail("gpuExecAcquire", err); return 1; }
        vkapi->gpuExecWritesPlane(ctx, kept[i], 0);
        if (vkapi->gpuExecSubmit(ctx, &signaled, err, sizeof(err))) { fail("gpuExecSubmit", err); return 1; }
        InterlockedIncrement(&progress);
    }
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    printf("  core freed with %d GPU frames still alive\n", KEPT);
    /* Everything below runs with no core: the device, its allocator and the timelines must all
       still be reachable through the frames' own references. */
    for (i = 0; i < KEPT; i++) {
        if (vkapi->waitGPUFrame(kept[i], err, sizeof(err)))
            fail("waitGPUFrame after freeCore", err);
        vsapi->freeFrame(kept[i]);
    }
    printf("  frames waited and freed after the core was gone\n");
    return 0;
}

/* ------------------------------------------------------------------ exhaust */
#define MAX_CHUNKS 512
static int runExhaust(void) {
    char err[512];
    VSCore *core;
    VSGPUMemory *chunks[MAX_CHUNKS];
    VkMemoryRequirements req;
    VSVulkanMemoryInfo info;
    VSVulkanCoreInfo ci;
    int held = 0, i;
    int maxChunks = MAX_CHUNKS;
    const int64_t chunkMB = 64;
    core = vsapi->createCore(0);
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { fail("getVulkanCoreInfo", err); return 1; }
    printf("  budget %lld MB, limit %lld MB, unified %d\n",
        (long long)(ci.budget >> 20), (long long)(ci.limit >> 20), probe_unified_memory(&ci));

    /* On a discrete card DEVICE_LOCAL is dedicated VRAM, so the 32 GB ceiling is above any real
       card and the driver refuses long before it matters. On unified memory DEVICE_LOCAL IS
       system RAM, and walking to the ceiling takes the machine down with the compositor rather
       than reaching the allocator's failure rung. Measured: this froze a 15 GB swapless Renoir
       laptop hard, last log line kwin_wayland_drm "main thread was hanging".
       So cap against what the host can actually spare. */
    {
        int64_t availMB = probe_avail_mem_mb();
        if (availMB > 0) {
            int64_t capMB = availMB / 2;
            int cap = (int)(capMB / chunkMB);
            if (cap < 1)
                cap = 1;
            if (cap < maxChunks) {
                maxChunks = cap;
                printf("  capping at %lld MB (half of %lld MB available to the host)\n",
                    (long long)(chunkMB * maxChunks), (long long)availMB);
            }
        }
    }
    printf("exhaust: 64 MB chunks until the driver refuses, capped at %lld MB\n",
        (long long)(chunkMB * maxChunks));

    req.size = (VkDeviceSize)chunkMB << 20;
    req.alignment = 256;
    req.memoryTypeBits = 0xFFFFFFFFu;
    for (i = 0; i < maxChunks; i++) {
        chunks[i] = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &info, err, sizeof(err));
        if (!chunks[i])
            break;
        held++;
    }
    if (held == maxChunks)
        printf("  the driver never refused: %lld MB committed, so the ladder's pressure and\n"
               "  failure rungs stay unreached within the cap on this hardware\n",
               (long long)(chunkMB * held));
    else
        printf("  refused after %d chunks (%lld MB); the ladder ran its rungs and returned\n"
               "  cleanly: \"%s\"\n", held, (long long)(chunkMB * held), err);
    for (i = 0; i < held; i++)
        vkapi->freeGPUMemory(chunks[i]);
    /* And the device still works afterwards. */
    {
        VSGPUMemory *again = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0, &info, err, sizeof(err));
        if (!again)
            fail("allocation after recovery", err);
        else
            vkapi->freeGPUMemory(again);
    }
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) fail("getVulkanCoreInfo", err);
    printf("  after release: allocated %lld MB\n", (long long)(ci.allocated >> 20));
    vsapi->freeCore(core);
    return 0;
}

int main(int argc, char **argv) {
    const char *what = (argc > 1) ? argv[1] : "all";
    int all = !strcmp(what, "all");
    char err[512];
    VSVulkanCoreHandles h;
    VSVulkanCoreInfo ci;
    VSCore *probe;
    int unified = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    vkapi = vsapi->getVulkanAPI();
    probe = vsapi->createCore(0);
    if (vkapi->getVulkanHandles(probe, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    if (!vkapi->getVulkanCoreInfo(probe, &ci, err, sizeof(err)))
        unified = probe_unified_memory(&ci);
    vsapi->freeCore(probe);

    if (all || !strcmp(what, "contention")) { progress = 0; stop = 0; runContention(); }
    if (all || !strcmp(what, "churn"))      { progress = 0; stop = 0; runChurn(); }
    if (all || !strcmp(what, "cores"))      { progress = 0; stop = 0; runCores(); }
    if (all || !strcmp(what, "outlive"))    { progress = 0; stop = 0; runOutlive(); }
    /* Exhaust is opt-in on unified memory. There DEVICE_LOCAL is system RAM, so filling it
       starves the compositor rather than reaching the allocator's failure rung, and it froze a
       15 GB swapless Renoir laptop outright. runExhaust caps itself against MemAvailable now,
       but "allocate until something refuses" is still the wrong default on a machine whose
       desktop shares the pool. Name it explicitly to run it anyway. */
    if (!strcmp(what, "exhaust")) {
        progress = 0; stop = 0; runExhaust();
    } else if (all) {
        if (unified)
            printf("exhaust: skipped, this device has unified memory -- run"
                   " `gpu_stress exhaust` explicitly to force it\n");
        else {
            progress = 0; stop = 0; runExhaust();
        }
    }

    (void)slowPathHits;
    if (failures) {
        printf("FAILED (%ld)\n", failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
