/* L11: I15 -- "A release callback only frees." Every entry point that could reach the exec
   registry from inside one must refuse fatally rather than deadlock.

   The guard is VSVulkanDevice::failIfRunningReleases, which calls vulkanFatal, so a refusal ENDS
   THE PROCESS. Each case therefore needs its own run: pass the case index as argv[1].

   The code guards seven entry points; I15 in vsvulkanexec_protocol.md names five (acquire, GPU
   allocation, pool creation, pool free, pool wait). gpuExecWaitValue and gpuTimelineWaitValue are
   guarded too -- the latter is not a pool wait at all, so the invariant's wording is narrower than
   what is enforced.

   A case passes when the marker line prints and the process dies before printing NOT REFUSED. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;
static VSGPUExecPool *spare;
static VSGPUTimeline *timeline;
static int theCase;
static int tag;

static const char *caseName(int i) {
    switch (i) {
    case 0: return "gpuExecAcquire";
    case 1: return "allocateGPUMemory";
    case 2: return "createGPUExecPool";
    case 3: return "freeGPUExecPool";
    case 4: return "gpuExecPoolWaitIdle";
    case 5: return "gpuExecWaitValue";
    case 6: return "gpuTimelineWaitValue";
    default: return "?";
    }
}
#define NCASES 7

/* Runs on the thread draining the pool, which is what makes it a release batch. */
static void VS_CC reentrantRelease(void *object) {
    char err[512] = { 0 };
    VkMemoryRequirements req;
    VSVulkanMemoryInfo info;
    (void)object;

    printf("  MARKER: inside the release callback, calling %s\n", caseName(theCase));
    fflush(stdout);

    switch (theCase) {
    case 0: vkapi->gpuExecAcquire(pool, err, sizeof(err)); break;
    case 1:
        req.size = 1 << 16; req.alignment = 256; req.memoryTypeBits = 0xFFFFFFFFu;
        vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &info, err, sizeof(err));
        break;
    case 2: vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err)); break;
    case 3: vkapi->freeGPUExecPool(spare); break;
    case 4: vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err)); break;
    case 5: vkapi->gpuExecWaitValue(pool, 1, err, sizeof(err)); break;
    case 6: vkapi->gpuTimelineWaitValue(timeline, 0, err, sizeof(err)); break;
    default: break;
    }

    printf("  NOT REFUSED: %s returned from inside a release callback\n", caseName(theCase));
    fflush(stdout);
}

int main(int argc, char **argv) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSGPUExecContext *ctx;
    uint64_t submitted = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        int i;
        printf("usage: %s <case>   where case is one of:\n", argv[0]);
        for (i = 0; i < NCASES; i++)
            printf("  %d  %s\n", i, caseName(i));
        printf("\nEach case ends the process by design, so run them one at a time.\n");
        return 2;
    }
    theCase = atoi(argv[1]);
    if (theCase < 0 || theCase >= NCASES) { printf("no such case\n"); return 2; }

    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }

    printf("L11 case %d: %s from inside a release callback\n", theCase, caseName(theCase));

    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    spare = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!spare) { printf("spare createGPUExecPool failed: %s\n", err); return 3; }
    timeline = vkapi->createGPUTimeline(core, err, sizeof(err));
    if (!timeline) { printf("createGPUTimeline failed: %s\n", err); return 3; }

    /* An empty submission is enough: the retention's release runs when it completes. */
    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }
    vkapi->gpuExecRetain(ctx, &reentrantRelease, &tag, 1 << 20);
    if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) {
        printf("gpuExecSubmit failed: %s\n", err); return 3;
    }

    printf("  draining, which runs the release callback on this thread\n");
    vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));

    /* Only reached when the guard did not fire. */
    printf("\nRESULT: %s was NOT refused from a release callback -- I15 is not enforced there\n",
        caseName(theCase));
    printf("FAILED\n");
    vkapi->freeGPUTimeline(timeline);
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    return 1;
}
