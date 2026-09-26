/* The third review pass added two things nothing in the tree exercises: gpuTimelineWaitValue,
   the reset-aware host wait for a filter's own timeline, and a bound check that turns a wait
   value nothing will ever signal into a named fatal instead of a thread gone forever.

   One mode per behaviour, since two of them are meant to abort the process:
     ok            a submitted value waits through both entry points and reports gdDrained
     own           a timeline of the caller's own waits, with no bound the core could check
     poolfatal     gpuExecWaitValue past the pool's ceiling must be fatal, not a hang
     timelinefatal gpuTimelineWaitValue past a pool timeline's newest value, likewise */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;

int main(int argc, char **argv) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSGPUExecContext *ctx;
    VSGPUTimeline *poolTimeline;
    uint64_t value = 0;
    const char *mode = argc > 1 ? argv[1] : "ok";
    int r;

    setvbuf(stdout, NULL, _IONBF, 0);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }

    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    poolTimeline = vkapi->gpuExecPoolTimeline(pool);

    /* An empty recording is a real submission: it signals the next value like any other. */
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    if (vkapi->gpuExecSubmit(ctx, &value, err, sizeof(err))) { printf("gpuExecSubmit failed: %s\n", err); return 3; }
    printf("submitted, signaledValue %llu\n", (unsigned long long)value);

    if (strcmp(mode, "poolfatal") == 0) {
        printf("waiting for %llu, which the pool never submitted -- expecting a fatal\n",
            (unsigned long long)(value + 1000));
        vkapi->gpuExecWaitValue(pool, value + 1000, err, sizeof(err));
        printf("FAILED: returned instead of being fatal\n");
        return 1;
    }

    if (strcmp(mode, "timelinefatal") == 0) {
        printf("waiting on the pool timeline for %llu -- expecting a fatal\n",
            (unsigned long long)(value + 1000));
        vkapi->gpuTimelineWaitValue(poolTimeline, value + 1000, err, sizeof(err));
        printf("FAILED: returned instead of being fatal\n");
        return 1;
    }

    if (strcmp(mode, "own") == 0) {
        /* A timeline the caller signals itself: value 0 is already reached, and the core must
           not invent a bound for one whose values it does not allocate. */
        VSGPUTimeline *mine = vkapi->createGPUTimeline(core, err, sizeof(err));
        if (!mine) { printf("createGPUTimeline failed: %s\n", err); return 3; }
        r = vkapi->gpuTimelineWaitValue(mine, 0, err, sizeof(err));
        printf("own timeline, value 0: %d (%s)\n", r, r == gdDrained ? "gdDrained" : err);
        vkapi->freeGPUTimeline(mine);
        if (r != gdDrained) { printf("FAILED\n"); return 1; }
        vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
        vkapi->freeGPUExecPool(pool);
        vsapi->freeCore(core);
        printf("ALL PASS\n");
        return 0;
    }

    r = vkapi->gpuExecWaitValue(pool, value, err, sizeof(err));
    printf("gpuExecWaitValue(%llu): %d (%s)\n", (unsigned long long)value, r,
        r == gdDrained ? "gdDrained" : err);
    if (r != gdDrained) { printf("FAILED\n"); return 1; }

    r = vkapi->gpuTimelineWaitValue(poolTimeline, value, err, sizeof(err));
    printf("gpuTimelineWaitValue(%llu): %d (%s)\n", (unsigned long long)value, r,
        r == gdDrained ? "gdDrained" : err);
    if (r != gdDrained) { printf("FAILED\n"); return 1; }

    /* Every value below the newest is legal too, and 0 is the value a timeline starts at. */
    r = vkapi->gpuTimelineWaitValue(poolTimeline, 0, err, sizeof(err));
    if (r != gdDrained) { printf("FAILED on value 0: %d %s\n", r, err); return 1; }
    if (!vsGPUDrainSafeToDestroy(r)) { printf("FAILED: gdDrained is not safe to destroy\n"); return 1; }

    vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    printf("ALL PASS\n");
    return 0;
}
