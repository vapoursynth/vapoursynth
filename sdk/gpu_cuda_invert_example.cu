/*
* GPU filter example: bitwise invert of 8-16 bit integer clips, computed by CUDA on frames
* that live in the core's Vulkan memory. Where gpu_invert_example.c shows a Vulkan compute
* filter and gpu_planestats_example.c the reduce pattern, this one shows the third species:
* a filter whose kernels belong to another API entirely, borrowing the frames zero copy.
*
*   - the Vulkan device is matched to a CUDA device by UUID
*   - frame planes are wrapped via exportGPUPlane: one cudaImportExternalMemory per 128 MB
*     allocation, cached by memoryId, per-plane pointers by offset — the kernels then read
*     and write the exact VRAM the Vulkan buffers occupy
*   - synchronization is device side when possible: input producer pairs are imported with
*     cudaImportExternalSemaphore (cached by VkSemaphore value, which the producer pair
*     contract keeps stable for this instance's lifetime) and waited IN THE STREAM; the
*     filter's own exportable timeline is signalled from the stream and published through
*     setGPUPlaneProducer, so the graph pipelines across the API boundary with no host wait
*   - when a producer's timeline cannot be exported (a third party filter that did not opt
*     in) the filter falls back to waitGPUFrame for that frame, and when the device offers
*     no semaphore export at all it runs fully host synchronized: waitGPUFrame before the
*     kernels, cudaStreamSynchronize after, publishing no producer pair
*   - like every asynchronous producer it retains source frames until its signalled value
*     completes, swept non blockingly with vkGetSemaphoreCounterValue through the core's
*     function table
*
* This is the one species of GPU filter the core's execution pool (VSGPUExecPool, see
* gpu_invert_example.c) cannot carry: the pool records and submits Vulkan command buffers on
* the core's queue, and this filter submits nothing there — its work enters a CUDA stream.
* So the obligations the pool normally discharges are discharged by hand across the API
* boundary instead, the same set gpu_invert_raw_example.c spells out within Vulkan: waiting
* the producers of what it reads, keeping sources alive until completion, allocating timeline
* values in order, and publishing producer pairs on what it writes.
*
* REFERENCE CODE: written and reviewed against the documented contracts, but developed on a
* machine without NVIDIA hardware, so it has not been executed. The cross device import
* mechanics it relies on are covered by the Vulkan-to-Vulkan tests shipped with the core.
*
* Build (adjust paths and SM level to taste):
*   nvcc -O2 --shared -o cudainvert.dll gpu_cuda_invert_example.cu ^
*        -I<vapoursynth>/include -I%VULKAN_SDK%/Include
* On Linux replace the handle type ifdefs' win32 arms automatically via _WIN32 and link
* nothing extra; the example only needs the CUDA runtime.
*/

#define VS_USE_API_43
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <cuda_runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define MAX_IMPORTS 16
#define MAX_SEM_IMPORTS 8
#define MAX_RETAINED 64

/* dst[i] = ~src[i] over 32 bit words: exact pixel inversion for 8 and 16 bit integer
   samples, padding included, so no per-format kernels are needed. */
__global__ void invertKernel(const uint32_t *src, uint32_t *dst, size_t words) {
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < words)
        dst[i] = ~src[i];
}

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    const VSVULKANAPI *vkapi;
    VSVulkanCoreHandles h;
    const VSVulkanFunctions *vk;
    int cudaDevice;
    cudaStream_t stream;
    int semCapable; /* device side sync available and usable */

    /* Memory imports cached per underlying allocation. */
    struct { uint64_t memoryId; cudaExternalMemory_t mem; } imports[MAX_IMPORTS];
    int importCount;

    /* Semaphore imports cached per producer timeline. */
    struct { VkSemaphore sem; cudaExternalSemaphore_t cudaSem; } semImports[MAX_SEM_IMPORTS];
    int semImportCount;

    /* The filter's own timeline: created exportable on the Vulkan side, imported once into
       CUDA, signalled from the stream, published as the producer pair of every output.
       Counted, so releasing it in the free callback is enough however many outputs are still
       in flight naming it. The raw handle is cached beside it for signalling and export. */
    VSGPUTimeline *timeline;
    VkSemaphore timelineSem;
    cudaExternalSemaphore_t cudaTimeline;
    uint64_t nextValue;

    struct { const VSFrame *frame; uint64_t value; } retained[MAX_RETAINED];
    int retainedCount;

#ifdef _WIN32
    SRWLOCK lock;
#else
    pthread_mutex_t lock;
#endif
} CudaInvertData;

#ifdef _WIN32
#define LOCK(d) AcquireSRWLockExclusive(&(d)->lock)
#define UNLOCK(d) ReleaseSRWLockExclusive(&(d)->lock)
#else
#define LOCK(d) pthread_mutex_lock(&(d)->lock)
#define UNLOCK(d) pthread_mutex_unlock(&(d)->lock)
#endif

/* Returns the device pointer for a plane, importing its backing allocation on first sight.
   Must be called with the instance lock held. */
static void *mapPlane(CudaInvertData *d, const VSFrame *frame, int plane, char *err, int errSize) {
    VSVulkanExportedMemory exp;
    int i;
    if (d->vkapi->exportGPUPlane(frame, plane, &exp, err, errSize))
        return NULL;

    cudaExternalMemory_t mem = NULL;
    for (i = 0; i < d->importCount; i++) {
        if (d->imports[i].memoryId == exp.memoryId) {
            mem = d->imports[i].mem;
            break;
        }
    }
    if (!mem) {
        cudaExternalMemoryHandleDesc hd;
        memset(&hd, 0, sizeof(hd));
#ifdef _WIN32
        hd.type = cudaExternalMemoryHandleTypeOpaqueWin32;
        hd.handle.win32.handle = (void *)exp.handle;
#else
        hd.type = cudaExternalMemoryHandleTypeOpaqueFd;
        hd.handle.fd = (int)exp.handle;
#endif
        hd.size = exp.memorySize;
        if (cudaImportExternalMemory(&mem, &hd) != cudaSuccess || d->importCount >= MAX_IMPORTS) {
            snprintf(err, errSize, "cudaImportExternalMemory failed");
#ifdef _WIN32
            CloseHandle((HANDLE)exp.handle);
#endif
            return NULL;
        }
        d->imports[d->importCount].memoryId = exp.memoryId;
        d->imports[d->importCount].mem = mem;
        d->importCount++;
#ifndef _WIN32
        exp.handle = -1; /* fd ownership moved to CUDA on success */
#endif
    }
#ifdef _WIN32
    /* NT handles are never consumed; every export call made one, so every call closes it. */
    CloseHandle((HANDLE)exp.handle);
#else
    if (exp.handle >= 0)
        close((int)exp.handle); /* surplus fd from a cache hit */
#endif

    void *ptr = NULL;
    cudaExternalMemoryBufferDesc bd;
    memset(&bd, 0, sizeof(bd));
    bd.offset = exp.offset;
    bd.size = exp.size;
    if (cudaExternalMemoryGetMappedBuffer(&ptr, mem, &bd) != cudaSuccess) {
        snprintf(err, errSize, "cudaExternalMemoryGetMappedBuffer failed");
        return NULL;
    }
    return ptr;
}

/* Imports a producer pair's timeline, cached; returns NULL when it cannot be exported,
   which the caller treats as "host synchronize this frame instead". Lock held. */
static cudaExternalSemaphore_t mapSemaphore(CudaInvertData *d, VSCore *core, VkSemaphore sem) {
    char err[256];
    int i;
    for (i = 0; i < d->semImportCount; i++) {
        if (d->semImports[i].sem == sem)
            return d->semImports[i].cudaSem;
    }
    if (d->semImportCount >= MAX_SEM_IMPORTS)
        return NULL;

    VSVulkanExportedSemaphore sexp;
    if (d->vkapi->exportGPUSemaphore(core, sem, &sexp, err, sizeof(err)))
        return NULL; /* third party producer without an exportable timeline */

    cudaExternalSemaphoreHandleDesc sd;
    memset(&sd, 0, sizeof(sd));
#ifdef _WIN32
    sd.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
    sd.handle.win32.handle = (void *)sexp.handle;
#else
    sd.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
    sd.handle.fd = (int)sexp.handle;
#endif
    cudaExternalSemaphore_t cudaSem = NULL;
    if (cudaImportExternalSemaphore(&cudaSem, &sd) != cudaSuccess) {
#ifdef _WIN32
        CloseHandle((HANDLE)sexp.handle);
#endif
        return NULL;
    }
#ifdef _WIN32
    CloseHandle((HANDLE)sexp.handle);
#endif
    d->semImports[d->semImportCount].sem = sem;
    d->semImports[d->semImportCount].cudaSem = cudaSem;
    d->semImportCount++;
    return cudaSem;
}

static void sweepRetained(CudaInvertData *d, const VSAPI *vsapi) {
    uint64_t completed = 0;
    int i, kept = 0;
    if (d->vk->vkGetSemaphoreCounterValue(d->h.device, d->timelineSem, &completed) != VK_SUCCESS)
        return;
    for (i = 0; i < d->retainedCount; i++) {
        if (d->retained[i].value <= completed)
            vsapi->freeFrame(d->retained[i].frame);
        else
            d->retained[kept++] = d->retained[i];
    }
    d->retainedCount = kept;
}

static const VSFrame *VS_CC cudaInvertGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    CudaInvertData *d = (CudaInvertData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return NULL;
    }
    if (activationReason != arAllFramesReady)
        return NULL;

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
    VSFrame *dst = d->vkapi->newGPUVideoFrame(fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
    char err[512] = { 0 };
    int p;

    cudaSetDevice(d->cudaDevice);

    LOCK(d);
    sweepRetained(d, vsapi);

    /* Device side sync when every input producer can be imported, host sync otherwise. */
    int hostSync = !d->semCapable;
    if (!hostSync) {
        for (p = 0; p < fmt->numPlanes && !hostSync; p++) {
            VSVulkanPlaneInfo pi;
            d->vkapi->getGPUPlane(src, p, &pi);
            if (!pi.readySemaphore)
                continue; /* host produced, ready now */
            cudaExternalSemaphore_t cs = mapSemaphore(d, core, pi.readySemaphore);
            if (!cs) {
                hostSync = 1;
                break;
            }
            cudaExternalSemaphoreWaitParams wp;
            memset(&wp, 0, sizeof(wp));
            wp.params.fence.value = pi.readyValue;
            cudaWaitExternalSemaphoresAsync(&cs, &wp, 1, d->stream);
        }
    }
    if (hostSync) {
        /* Waits the pairs AND makes the writes available outside the Vulkan device, which
           an external semaphore wait would otherwise do for us. */
        if (d->vkapi->waitGPUFrame(src, err, sizeof(err))) {
            UNLOCK(d);
            vsapi->setFilterError(err, frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return NULL;
        }
    }

    for (p = 0; p < fmt->numPlanes; p++) {
        VSVulkanPlaneInfo pi;
        d->vkapi->getGPUPlane(src, p, &pi);
        const uint32_t *sptr = (const uint32_t *)mapPlane(d, src, p, err, sizeof(err));
        uint32_t *dptr = (uint32_t *)mapPlane(d, dst, p, err, sizeof(err));
        if (!sptr || !dptr) {
            UNLOCK(d);
            vsapi->setFilterError(err, frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return NULL;
        }
        size_t words = pi.bufferSize / 4;
        invertKernel<<<(unsigned)((words + 255) / 256), 256, 0, d->stream>>>(sptr, dptr, words);
    }

    if (!hostSync) {
        uint64_t value;
        /* Values are allocated under the instance lock and signalled in stream order, which
           keeps this timeline's signals monotonic like a queue lock does for Vulkan
           filters. */
        value = ++d->nextValue;
        cudaExternalSemaphoreSignalParams sp;
        memset(&sp, 0, sizeof(sp));
        sp.params.fence.value = value;
        cudaSignalExternalSemaphoresAsync(&d->cudaTimeline, &sp, 1, d->stream);
        for (p = 0; p < fmt->numPlanes; p++)
            d->vkapi->setGPUPlaneProducer(dst, p, d->timeline, value);
        if (d->retainedCount < MAX_RETAINED) {
            d->retained[d->retainedCount].frame = src;
            d->retained[d->retainedCount].value = value;
            d->retainedCount++;
            src = NULL;
        }
    }
    UNLOCK(d);

    if (hostSync || src) {
        /* Fully synchronized fallback: nothing outlives this call, no pair published. */
        cudaStreamSynchronize(d->stream);
        if (src)
            vsapi->freeFrame(src);
    }
    return dst;
}

static void VS_CC cudaInvertFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    CudaInvertData *d = (CudaInvertData *)instanceData;
    int i;
    cudaSetDevice(d->cudaDevice);
    cudaStreamSynchronize(d->stream);
    if (d->timelineSem && d->nextValue) {
        VkSemaphoreWaitInfo wi;
        memset(&wi, 0, sizeof(wi));
        wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.semaphoreCount = 1;
        wi.pSemaphores = &d->timelineSem;
        wi.pValues = &d->nextValue;
        d->vk->vkWaitSemaphores(d->h.device, &wi, UINT64_MAX);
    }
    sweepRetained(d, vsapi);
    for (i = 0; i < d->semImportCount; i++)
        cudaDestroyExternalSemaphore(d->semImports[i].cudaSem);
    if (d->cudaTimeline)
        cudaDestroyExternalSemaphore(d->cudaTimeline);
    for (i = 0; i < d->importCount; i++)
        cudaDestroyExternalMemory(d->imports[i].mem);
    if (d->stream)
        cudaStreamDestroy(d->stream);
    /* Just this filter's reference; outputs still naming it keep the semaphore alive. */
    if (d->timeline)
        d->vkapi->freeGPUTimeline(d->timeline);
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC cudaInvertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    CudaInvertData *d = (CudaInvertData *)calloc(1, sizeof(CudaInvertData));
    char err[512] = { 0 };
    VSVulkanCoreInfo info;
    int deviceCount = 0, i;

    d->node = vsapi->mapGetNode(in, "clip", 0, NULL);
    d->vi = *vsapi->getVideoInfo(d->node);

    if (d->vi.format.colorFamily == cfUndefined || d->vi.format.sampleType != stInteger ||
        (d->vi.format.bytesPerSample != 1 && d->vi.format.bytesPerSample != 2)) {
        vsapi->mapSetError(out, "InvertCUDA: only constant format 8-16 bit integer clips are supported");
        goto fail;
    }

    d->vkapi = vsapi->getVulkanAPI();
    if (d->vkapi->getVulkanHandles(core, &d->h, err, sizeof(err)) ||
        !(d->vk = d->vkapi->getVulkanFunctions(core, err, sizeof(err)))) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

    memset(&info, 0, sizeof(info));
    d->vkapi->getVulkanCoreInfo(core, &info, err, sizeof(err));
    if (!info.exportHandleType) {
        vsapi->mapSetError(out, "InvertCUDA: memory export is not available on this device");
        goto fail;
    }

    /* The Vulkan device and the CUDA device must be the same silicon: match by UUID. */
    d->cudaDevice = -1;
    cudaGetDeviceCount(&deviceCount);
    for (i = 0; i < deviceCount; i++) {
        struct cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, i) == cudaSuccess &&
            !memcmp(prop.uuid.bytes, info.deviceUUID, 16)) {
            d->cudaDevice = i;
            break;
        }
    }
    if (d->cudaDevice < 0) {
        vsapi->mapSetError(out, "InvertCUDA: no CUDA device matches the Vulkan device UUID");
        goto fail;
    }
    cudaSetDevice(d->cudaDevice);
    if (cudaStreamCreateWithFlags(&d->stream, cudaStreamNonBlocking) != cudaSuccess) {
        vsapi->mapSetError(out, "InvertCUDA: stream creation failed");
        goto fail;
    }

    /* Device side sync needs the semaphore capability plus an exportable timeline of our
       own; without it the filter still works, host synchronized. */
    d->semCapable = 0;
    if (info.semaphoreExportHandleType) {
        VSVulkanExportedSemaphore sexp;
        /* createGPUTimeline already asks for export wherever the device supports it, which
           this branch has just established it does. */
        d->timeline = d->vkapi->createGPUTimeline(core, err, sizeof(err));
        d->timelineSem = d->vkapi->getGPUTimelineSemaphore(d->timeline);
        if (d->timeline &&
            !d->vkapi->exportGPUSemaphore(core, d->timelineSem, &sexp, err, sizeof(err))) {
            cudaExternalSemaphoreHandleDesc sd;
            memset(&sd, 0, sizeof(sd));
#ifdef _WIN32
            sd.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
            sd.handle.win32.handle = (void *)sexp.handle;
#else
            sd.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
            sd.handle.fd = (int)sexp.handle;
#endif
            if (cudaImportExternalSemaphore(&d->cudaTimeline, &sd) == cudaSuccess)
                d->semCapable = 1;
#ifdef _WIN32
            CloseHandle((HANDLE)sexp.handle);
#endif
        }
    }

#ifdef _WIN32
    InitializeSRWLock(&d->lock);
#else
    pthread_mutex_init(&d->lock, NULL);
#endif

    {
        VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
        vsapi->createVideoFilterEx(out, "InvertCUDA", &d->vi, cudaInvertGetFrame, cudaInvertFree, fmParallel, ffGPUOutput, deps, 1, d, core);
    }
    if (vsapi->mapGetError(out))
        goto fail;
    return;

fail:
    if (d->node)
        vsapi->freeNode(d->node);
    free(d);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.gpucudainvert", "cudaexample", "Out of tree CUDA interop example", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("InvertCUDA", "clip:vnode:gpu;", "clip:vnode:gpu;", cudaInvertCreate, NULL, plugin);
}
