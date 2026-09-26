/* L21: the core's own transfers against plugin pools on the ONE queue this hardware has.

   Where there is no dedicated transfer family the transfer pool IS the compute pool and vqTransfer
   and vqCompute are the same non-recursive lock. The transfer pool's metering of retained source
   frames against the admission gate (added 2026-09-08) only takes effect here, and nothing has run it
   under contention. This drives GPUUpload/GPUDownload through the std plugin in a loop on one thread
   while others acquire and submit on 2-context plugin pools, all under a small VRAM limit so the
   gate is live.

   Passes when every thread finishes its rounds, no acquire hangs, and teardown completes. A
   plugin-pool thread starving behind transfer retentions the gate never lets drain is the finding. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"
#include "VSHelper4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

#define POOLS 3
#define ROUNDS 300
#define FRAMES 120

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSNode *roundTrip;
static volatile LONG stop = 0, frames = 0, submits = 0, gateReturnsNull = 0;
static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";

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
            if (++idle >= 120) {
                printf("\nFAIL: stuck for 120 s in: %s (frames %ld, submits %ld)\nFAILED\n", stepName,
                    InterlockedGet(&frames), InterlockedGet(&submits));
                fflush(stdout); TerminateProcess(GetCurrentProcess(), 1);
            }
        } else idle = 0;
        last = InterlockedGet(&watchdogStep);
    }
}

/* The core's own transfer traffic: upload, then download, frame after frame. */
static DWORD WINAPI transfers(LPVOID p) {
    char err[512] = { 0 };
    int n;
    (void)p;
    for (n = 0; n < FRAMES && !InterlockedGet(&stop); n++) {
        const VSFrame *f = vsapi->getFrame(n, roundTrip, err, sizeof(err));
        if (!f) { printf("  getFrame %d failed: %s\n", n, err); InterlockedExchange(&stop, 1); return 0; }
        vsapi->freeFrame(f);
        InterlockedIncrement(&frames);
        InterlockedIncrement(&watchdogStep);
    }
    return 0;
}

/* A plugin pool: acquire, retain a little, submit, wait on it -- the plugin-side rhythm. */
static DWORD WINAPI plugin(LPVOID p) {
    char err[512] = { 0 };
    VSGPUExecPool *pool = (VSGPUExecPool *)p;
    int r;
    for (r = 0; r < ROUNDS && !InterlockedGet(&stop); r++) {
        uint64_t v = 0;
        VSGPUExecContext *c = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!c) { InterlockedIncrement(&gateReturnsNull); printf("  acquire refused: %s\n", err); InterlockedExchange(&stop, 1); return 0; }
        vkapi->gpuExecRetain(c, NULL, NULL, 2 << 20);
        if (vkapi->gpuExecSubmit(c, &v, err, sizeof(err))) { printf("  submit failed: %s\n", err); InterlockedExchange(&stop, 1); return 0; }
        vkapi->gpuExecWaitValue(pool, v, err, sizeof(err));
        InterlockedIncrement(&submits);
        InterlockedIncrement(&watchdogStep);
    }
    return 0;
}

static VSNode *invokeNode(VSPlugin *plug, const char *name, VSMap *in) {
    VSMap *out = vsapi->invoke(plug, name, in);
    const char *e = vsapi->mapGetError(out);
    VSNode *n = NULL;
    int err = 0;
    if (e) printf("  %s failed: %s\n", name, e);
    else n = vsapi->mapGetNode(out, "clip", 0, &err);
    vsapi->freeMap(out);
    return n;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSPlugin *stdp;
    VSMap *in;
    VSNode *src, *up;
    VSGPUExecPool *pools[POOLS];
    HANDLE t[POOLS + 1];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    probe_setenv("VS_VULKAN_MAX_VRAM_MB", "64"); /* a live gate: budget 16 MB */
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vsapi->setThreadCount(2, core); /* 2-context rings, so a full ring is two acquires away */
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    printf("L21: transfers vs %d plugin pools on the shared queue, budget 16 MB, 2-context rings\n\n", POOLS);
    printf("  compute family/index %u/%u, transfer %u/%u %s\n", h.computeQueueFamily, h.computeQueueIndex,
        h.transferQueueFamily, h.transferQueueIndex,
        (h.computeQueueFamily == h.transferQueueFamily && h.computeQueueIndex == h.transferQueueIndex) ? "(aliased -- the case under test)" : "(dedicated -- not the case under test)");

    step("build BlankClip -> GPUUpload -> GPUDownload");
    stdp = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);
    in = vsapi->createMap();
    vsapi->mapSetInt(in, "width", 1920, maReplace); vsapi->mapSetInt(in, "height", 1080, maReplace);
    vsapi->mapSetInt(in, "format", pfRGBS, maReplace); vsapi->mapSetInt(in, "length", FRAMES, maReplace);
    src = invokeNode(stdp, "BlankClip", in); vsapi->freeMap(in);
    if (!src) return 3;
    in = vsapi->createMap(); vsapi->mapSetNode(in, "clip", src, maReplace);
    up = invokeNode(stdp, "GPUUpload", in); vsapi->freeMap(in);
    if (!up) return 3;
    in = vsapi->createMap(); vsapi->mapSetNode(in, "clip", up, maReplace);
    roundTrip = invokeNode(stdp, "GPUDownload", in); vsapi->freeMap(in);
    if (!roundTrip) return 3;

    step("create the plugin pools");
    for (i = 0; i < POOLS; i++) {
        pools[i] = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
        if (!pools[i]) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    }

    step("run transfers and plugin submits concurrently");
    t[0] = CreateThread(NULL, 0, transfers, NULL, 0, NULL);
    for (i = 0; i < POOLS; i++) t[1 + i] = CreateThread(NULL, 0, plugin, pools[i], 0, NULL);
    WaitForMultipleObjects(POOLS + 1, t, TRUE, INFINITE);
    for (i = 0; i < POOLS + 1; i++) CloseHandle(t[i]);

    step("tear down");
    for (i = 0; i < POOLS; i++) vkapi->freeGPUExecPool(pools[i]);
    vsapi->freeNode(roundTrip); vsapi->freeNode(up); vsapi->freeNode(src);
    step("freeCore must complete");
    vsapi->freeCore(core);

    printf("\nresults:\n  frames round-tripped: %ld of %d\n  plugin submits: %ld of %d\n  acquires refused: %ld\n",
        InterlockedGet(&frames), FRAMES, InterlockedGet(&submits), POOLS * ROUNDS, InterlockedGet(&gateReturnsNull));
    if (InterlockedGet(&frames) != FRAMES || InterlockedGet(&submits) != POOLS * ROUNDS) { printf("FAILED\n"); return 1; }
    printf("  teardown completed\nALL PASS\n");
    return 0;
}
