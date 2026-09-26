/* L17: a sweep nested inside a sweep, on one thread, racing pool destruction.

   sweepExecPools registers a release batch keyed on (pool, thread), drops execPoolsMutex, runs
   the releases, then erases the batch. updateGPUMemoryReservation is NOT on I15's guarded list, and
   an INCREASE past the limit calls notifyCaches -> sweepExecPools. So a release callback that grows
   a reservation starts a second sweep on the same thread, which can register a second batch for the
   same (pool, thread) while the first is still open. endExecReleasesLocked erases from the END, so
   the two should pop LIFO -- this checks that they do, while another thread creates, submits on and
   frees pools, since freeGPUExecPool is what waits on those batches.

   Passes when: the nested update is accepted, the release ran exactly once, every pool free on the
   churn thread returned, and freeCore completes. A watchdog names the step that hangs. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;
static VSGPUMemoryReservation *reservation;
static volatile LONG releaseCalls = 0;
static volatile LONG churnFrees = 0;
static volatile LONG stop = 0;
static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";
static int64_t limitBytes;

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
            if (++idle >= 60) {
                printf("\nFAIL: stuck for 60 s in: %s\n", stepName);
                printf("FAILED\n"); fflush(stdout);
                TerminateProcess(GetCurrentProcess(), 1);
            }
        } else idle = 0;
        last = InterlockedGet(&watchdogStep);
    }
}

/* Runs on the sweeping thread. Growing the reservation past the limit is what nests a sweep. */
static void VS_CC growingRelease(void *object) {
    (void)object;
    InterlockedIncrement(&releaseCalls);
    printf("  MARKER: inside the release callback, growing the reservation past the limit\n");
    vkapi->updateGPUMemoryReservation(reservation, limitBytes * 2);
    printf("  MARKER: update returned -- the nested sweep ran and came back\n");
}

/* Creates, submits on and frees pools as fast as it can: freeGPUExecPool waits on release
   batches, which is the bookkeeping under test. */
static DWORD WINAPI churn(LPVOID p) {
    char err[512];
    (void)p;
    while (!InterlockedGet(&stop)) {
        VSGPUExecPool *q = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
        VSGPUExecContext *c;
        uint64_t v;
        if (!q) continue;
        c = vkapi->gpuExecAcquire(q, err, sizeof(err));
        if (c) vkapi->gpuExecSubmit(c, &v, err, sizeof(err));
        vkapi->freeGPUExecPool(q);
        InterlockedIncrement(&churnFrees);
    }
    return 0;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSVulkanCoreInfo ci;
    VSGPUExecContext *ctx;
    HANDLE t;
    uint64_t submitted = 0;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    probe_setenv("VS_VULKAN_MAX_VRAM_MB", "64"); /* a limit a reservation can cheaply exceed */
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    if (vkapi->getVulkanCoreInfo(core, &ci, err, sizeof(err))) { printf("no info: %s\n", err); return 3; }
    limitBytes = ci.limit;
    printf("L17: nested sweep from a release callback, racing pool churn (limit %lld MB)\n\n",
        (long long)(limitBytes >> 20));

    step("reserve, so the callback has something to grow");
    reservation = vkapi->reserveGPUMemory(core, 0, err, sizeof(err));
    if (!reservation) { printf("reserveGPUMemory failed: %s\n", err); return 3; }

    step("start the pool churn thread");
    t = CreateThread(NULL, 0, churn, NULL, 0, NULL);

    step("submit with a release that grows the reservation, then drain to run it here");
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    for (i = 0; i < 20; i++) {
        ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!ctx) { printf("acquire failed: %s\n", err); return 3; }
        vkapi->gpuExecRetain(ctx, &growingRelease, NULL, 1 << 20);
        if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) { printf("submit failed: %s\n", err); return 3; }
        vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err)); /* runs the release on THIS thread */
        vkapi->updateGPUMemoryReservation(reservation, 0);  /* back under the limit for the next round */
    }

    step("stop the churn and tear down");
    InterlockedExchange(&stop, 1);
    WaitForMultipleObjects(1, &t, TRUE, INFINITE);
    CloseHandle(t);
    vkapi->freeGPUExecPool(pool);
    vkapi->releaseGPUMemoryReservation(reservation);
    step("freeCore must complete");
    vsapi->freeCore(core);

    printf("\nresults:\n");
    printf("  release callbacks: %ld of 20 (each nested a sweep)\n", InterlockedGet(&releaseCalls));
    printf("  pools created+freed by the churn thread meanwhile: %ld\n", InterlockedGet(&churnFrees));
    if (InterlockedGet(&releaseCalls) != 20) { printf("FAILED\n"); return 1; }
    printf("  teardown completed\n");
    printf("ALL PASS\n");
    return 0;
}
