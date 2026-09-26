/* Lock-order sweep coverage: drives the GPU entry points the test suites never reach, so the
   observed order graph is not silently missing flushMutex, the reservation lock or the slot
   rings. Correctness is not the point here; being executed is. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;
static VSGPUMemoryReservation *reservation;

#define THREADS 4
#define ROUNDS 400

static DWORD WINAPI worker(LPVOID p) {
    char err[512];
    VkMemoryRequirements req;
    VSVulkanMemoryInfo info;
    VSVideoFormat fmt;
    int i;
    (void)p;

    req.size = 4u << 20;
    req.alignment = 256;
    req.memoryTypeBits = 0xFFFFFFFFu;
    vsapi->queryVideoFormat(&fmt, cfYUV, stInteger, 8, 1, 1, core);

    for (i = 0; i < ROUNDS; i++) {
        VSGPUExecContext *ctx;
        VSGPUMemory *mem;
        VSFrame *frame;

        /* the allocation ladder, including its sweep rungs */
        mem = vkapi->allocateGPUMemory(core, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &info, err, sizeof(err));
        if (mem)
            vkapi->freeGPUMemory(mem);

        /* a GPU frame, and the availability flush that waiting on one submits */
        frame = vkapi->newGPUVideoFrame(&fmt, 640, 480, NULL, core);
        if (frame) {
            vkapi->waitGPUFrame(frame, err, sizeof(err));
            vsapi->freeFrame(frame);
        }

        /* both endings of a recording */
        ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (ctx) {
            if (i & 1)
                vkapi->gpuExecAbandon(ctx);
            else
                vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err));
        }

        /* the reservation accounting */
        vkapi->updateGPUMemoryReservation(reservation, (i & 1) ? (8 << 20) : 0);
    }
    return 0;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    HANDLE threads[THREADS];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }

    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }
    reservation = vkapi->reserveGPUMemory(core, 0, err, sizeof(err));
    if (!reservation) { printf("reserveGPUMemory failed: %s\n", err); return 3; }

    for (i = 0; i < THREADS; i++)
        threads[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    WaitForMultipleObjects(THREADS, threads, TRUE, INFINITE);
    for (i = 0; i < THREADS; i++)
        CloseHandle(threads[i]);

    /* the setup-only drain, and the destructor's own */
    vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
    vkapi->updateGPUMemoryReservation(reservation, 0);
    vkapi->releaseGPUMemoryReservation(reservation);
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    printf("coverage run done: %d threads x %d rounds\n", THREADS, ROUNDS);
    return 0;
}
