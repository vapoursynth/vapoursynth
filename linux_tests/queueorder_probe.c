/* Fresh-eyes sweep: the queue lock is PUBLIC (lockVulkanQueue) but is not a leaf in the core's
   own lock order. Every exec pool sweep takes execPoolsMutex and then, inside
   VSVulkanExecPool::detachCompleted, the queue lock -- and gpuExecSubmit sweeps on the way out.
   So a filter that holds the queue lock and makes any core call that touches the exec registry
   (an allocation, an acquire, creating or freeing a pool) closes an ABBA cycle:

     thread A: queue lock held   -> wants execPoolsMutex (allocateGPUMemory)
     thread B: execPoolsMutex held -> wants queue lock   (gpuExecSubmit's trailing sweep)

   Nothing in VSVulkan4.h says the queue lock must be a leaf; it only says the lock is
   "mandatory around every vkQueueSubmit you make on the shared queues". */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include "probe_compat.h"
#include <stdlib.h>

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;

static volatile LONG stop = 0;
static volatile LONG aLoops = 0;
static volatile LONG bLoops = 0;
static int allocFailed = 0;
static int control = 0;   /* VS_QO_CONTROL=1: same calls, never nested */

/* B: ordinary pool use. Each submit ends with a sweep: execPoolsMutex, then the queue lock. */
static DWORD WINAPI submitter(LPVOID p) {
    char err[512];
    (void)p;
    while (!InterlockedGet(&stop)) {
        VSGPUExecContext *ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
        if (!ctx)
            return 1;
        if (vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err)))
            return 1;
        InterlockedIncrement(&bLoops);
    }
    return 0;
}

/* A: the documented raw-interop bracket, with one core call inside it. */
static DWORD WINAPI queueHolder(LPVOID p) {
    char err[512];
    VkMemoryRequirements req;
    VSVulkanMemoryInfo info;
    (void)p;
    req.size = 1u << 20;
    req.alignment = 256;
    req.memoryTypeBits = 0xFFFFFFFFu;
    while (!InterlockedGet(&stop)) {
        /* The control run takes the same lock just as often and allocates just as often, but
           never holds the one across the other; if it runs clean the nesting is the cause. */
        if (control) {
            vkapi->lockVulkanQueue(core, vqCompute);
            vkapi->unlockVulkanQueue(core, vqCompute);
        }
        if (!control)
            vkapi->lockVulkanQueue(core, vqCompute);
        {
            VSGPUMemory *mem = vkapi->allocateGPUMemory(core, &req,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
            if (mem) {
                vkapi->freeGPUMemory(mem);
            } else if (!allocFailed) {
                allocFailed = 1;
                printf("allocateGPUMemory refused: %s\n", err);
            }
        }
        if (!control)
            vkapi->unlockVulkanQueue(core, vqCompute);
        InterlockedIncrement(&aLoops);
    }
    return 0;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    HANDLE threads[2];
    int i;
    LONG lastA = -1, lastB = -1, stuck = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    control = getenv("VS_QO_CONTROL") != NULL;
    printf("mode: %s\n", control ? "CONTROL (allocation outside the queue lock)"
                                 : "allocation INSIDE the queue lock bracket");
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }

    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }

    threads[0] = CreateThread(NULL, 0, submitter, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, queueHolder, NULL, 0, NULL);

    for (i = 0; i < 60; i++) {           /* 30 s of watching, in 500 ms steps */
        Sleep(500);
        LONG curA = InterlockedGet(&aLoops), curB = InterlockedGet(&bLoops);
        if (curA == lastA && curB == lastB) {
            if (++stuck >= 6) {          /* 3 s with neither thread advancing */
                printf("submits: %ld   queue-lock brackets: %ld\n", curB, curA);
                printf("RESULT: both threads stopped advancing -> DEADLOCK\n");
                printf("FAILED\n");
                TerminateProcess(GetCurrentProcess(), 1);
            }
        } else {
            stuck = 0;
        }
        lastA = curA;
        lastB = curB;
    }

    InterlockedExchange(&stop, 1);
    WaitForMultipleObjects(2, threads, TRUE, 10000);
    for (i = 0; i < 2; i++)
        CloseHandle(threads[i]);
    printf("submits: %ld   queue-lock brackets: %ld\n", InterlockedGet(&bLoops), InterlockedGet(&aLoops));
    printf("RESULT: ran 30 s without stalling\n");
    vkapi->freeGPUExecPool(pool);
    vsapi->freeCore(core);
    printf("ALL PASS\n");
    return 0;
}
