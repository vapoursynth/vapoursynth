/* L18: L9's hang reached through the sweep path -- what it takes down with it.

   L9 showed that freeing a frame whose producer pair was published on a value nothing signals
   parks the freeing thread forever in ~VSPlaneData. A release callback "may only free", and
   releases run on whichever thread happens to sweep. So a filter that makes the L9 mistake AND
   drops that frame from a release callback parks the SWEEPING thread -- and the pool that
   detached the release is registered as a batch in flight until the release returns, which is
   exactly what freeGPUExecPool waits for. This measures that chain rather than inferring it.

   Two cases, control first so a hang is attributable:
     control  producer value 0 (already reached)  -> the drain returns and the pool frees
     hazard   producer value 1 (never signalled)  -> expected: the sweeper hangs in the release,
              then freeGPUExecPool hangs behind the batch, then freeCore would hang behind that

   The watchdog names the step. In the hazard case the sweeper is a helper thread, so the main
   thread can report which of ITS steps hung too. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;
static VSFrame *frame;
static volatile LONG releaseEntered = 0, releaseReturned = 0;
static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";
static int hazard = 0;

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
            if (++idle >= 20) {
                printf("\n  stuck for 20 s in: %s\n", stepName);
                printf("  release callback: entered=%ld returned=%ld\n",
                    InterlockedGet(&releaseEntered), InterlockedGet(&releaseReturned));
                if (hazard) {
                    printf("\nRESULT: the release callback never returned -- the sweeping thread is parked in\n"
                           "        ~VSPlaneData on the unsignalled producer, its pool's batch stays in flight,\n"
                           "        and the step above is what queued up behind it.\n");
                    printf("MEASURED: hang confirmed\n");
                    fflush(stdout);
                    TerminateProcess(GetCurrentProcess(), 0);
                }
                printf("\nFAIL: the CONTROL case hung\nFAILED\n");
                fflush(stdout);
                TerminateProcess(GetCurrentProcess(), 1);
            }
        } else idle = 0;
        last = InterlockedGet(&watchdogStep);
    }
}

/* The mistake under test: drop the frame from inside the release. */
static void VS_CC releaseDropsFrame(void *object) {
    (void)object;
    InterlockedIncrement(&releaseEntered);
    printf("  MARKER: release callback freeing the frame (sweeper thread)\n");
    vsapi->freeFrame(frame);
    InterlockedIncrement(&releaseReturned);
    printf("  MARKER: release callback returned\n");
}

/* The sweeper: a drain runs the releases on the draining thread. */
static DWORD WINAPI drainer(LPVOID p) {
    char err[512] = { 0 };
    (void)p;
    vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    return 0;
}

static int runCase(const char *label, uint64_t value) {
    char err[512] = { 0 };
    VSGPUTimeline *timeline;
    VSVideoFormat fmt;
    VSGPUExecContext *ctx;
    uint64_t submitted = 0;
    HANDLE t;

    printf("\n%s: producer value %llu\n", label, (unsigned long long)value);
    InterlockedExchange(&releaseEntered, 0);
    InterlockedExchange(&releaseReturned, 0);

    step("frame with a producer on a timeline of our own");
    timeline = vkapi->createGPUTimeline(core, err, sizeof(err));
    if (!timeline) { printf("createGPUTimeline failed: %s\n", err); return 1; }
    vsapi->queryVideoFormat(&fmt, cfGray, stInteger, 8, 0, 0, core);
    frame = vkapi->newGPUVideoFrame(&fmt, 256, 256, NULL, core);
    if (!frame) { printf("newGPUVideoFrame failed\n"); return 1; }
    vkapi->setGPUPlaneProducer(frame, 0, timeline, value);

    step("submit with a release that frees that frame");
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 1; }
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("acquire failed: %s\n", err); return 1; }
    vkapi->gpuExecRetain(ctx, &releaseDropsFrame, NULL, 1 << 20);
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) { printf("submit failed: %s\n", err); return 1; }

    step("drain on a helper thread, which makes it the sweeper running the release");
    t = CreateThread(NULL, 0, drainer, NULL, 0, NULL);
    Sleep(2000);

    step("freeGPUExecPool from the main thread -- waits for the sweeper's batch");
    vkapi->freeGPUExecPool(pool);
    WaitForMultipleObjects(1, &t, TRUE, INFINITE);
    CloseHandle(t);

    step("freeGPUTimeline");
    vkapi->freeGPUTimeline(timeline);
    printf("  %s completed (release entered=%ld returned=%ld)\n", label,
        InterlockedGet(&releaseEntered), InterlockedGet(&releaseReturned));
    return 0;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;

    setvbuf(stdout, NULL, _IONBF, 0);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    printf("L18: an L9 mistake made from inside a release callback\n");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    if (runCase("control (value 0, already reached)", 0)) { printf("FAILED\n"); return 1; }
    hazard = 1;
    if (runCase("hazard (value 1, never signalled)", 1)) { printf("FAILED\n"); return 1; }

    step("freeCore");
    vsapi->freeCore(core);
    printf("\nRESULT: the hazard case did NOT hang -- something bounds it; record what and where.\n");
    printf("MEASURED: no hang\n");
    return 0;
}
