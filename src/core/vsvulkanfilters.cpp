/*
* Copyright (c) 2026 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "internalfilters.h"
#include "vscore.h"
#include "vsvulkanframe.h"
#include "vsvulkanshader.h"
#include "shaders/boxblur8_spv.h"
#include "shaders/boxblur16_spv.h"
#include "shaders/boxblurs_spv.h"
#include "shaders/boxblurh_spv.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/* The CPU/GPU boundary filters. Everything else about GPU residency is types and bookkeeping;
   these two are where bytes actually cross the bus, with the policies the transfer benchmark
   picked. Both run fmParallel since the shared transfer machinery is thread safe and the
   per instance state is read only after construction. */

namespace {

struct GPUTransferData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
};

void VS_CC gpuTransferFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    GPUTransferData *d = static_cast<GPUTransferData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

const VSFrame *VS_CC gpuUploadGetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUTransferData *d = static_cast<GPUTransferData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        std::string err;
        VSVulkanTransfer *transfer = core->vulkanTransfer(err);
        if (!transfer) {
            vsapi->setFilterError(("GPUUpload: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = new VSFrame(*fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core, true);

        VSVulkanPlane *planes[3] = {};
        const uint8_t *srcPlanes[3] = {};
        ptrdiff_t srcStrides[3] = {};
        for (int p = 0; p < fmt->numPlanes; p++) {
            planes[p] = dst->getGPUPlane(p);
            srcPlanes[p] = vsapi->getReadPtr(src, p);
            srcStrides[p] = vsapi->getStride(src, p);
        }

        if (!transfer->uploadPlanes(planes, fmt->numPlanes, fmt->bytesPerSample, srcPlanes, srcStrides, err)) {
            vsapi->setFilterError(("GPUUpload: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            dst->release();
            return nullptr;
        }

        /* Downloads of planes that pass through the GPU untouched become plane sharing. */
        dst->adoptCPUOrigins(src);

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

const VSFrame *VS_CC gpuDownloadGetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUTransferData *d = static_cast<GPUTransferData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        std::string err;
        VSVulkanTransfer *transfer = core->vulkanTransfer(err);
        if (!transfer) {
            vsapi->setFilterError(("GPUDownload: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        if (!src->isGPUResident()) {
            vsapi->setFilterError("GPUDownload: source frame is not GPU resident", frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = new VSFrame(*fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        /* Planes whose upload origin survived are shared instead of copied back, and only
           whatever the GPU actually produced crosses the bus. */
        int adopted = dst->adoptOriginPlanes(src);

        const VSVulkanPlane *planes[3] = {};
        uint8_t *dstPlanes[3] = {};
        ptrdiff_t dstStrides[3] = {};
        int downloadCount = 0;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (adopted & (1 << p))
                continue;
            planes[downloadCount] = src->getGPUPlane(p);
            dstPlanes[downloadCount] = dst->getWritePtr(p);
            dstStrides[downloadCount] = dst->getStride(p);
            downloadCount++;
        }

        if (downloadCount && !transfer->downloadPlanes(planes, downloadCount, fmt->bytesPerSample, dstPlanes, dstStrides, err)) {
            vsapi->setFilterError(("GPUDownload: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            dst->release();
            return nullptr;
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC gpuTransferCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    bool toGPU = userData != nullptr;
    const char *name = toGPU ? "GPUUpload" : "GPUDownload";

    std::unique_ptr<GPUTransferData> d(new GPUTransferData());
    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = *vsapi->getVideoInfo(d->node);

    /* This is the moment lazy device bringup was specified for: the first GPU filter. */
    std::string err;
    if (!core->vulkanDevice(err)) {
        vsapi->mapSetError(out, (std::string(name) + ": " + err).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
    vsapi->createVideoFilterEx(out, name, &d->vi, toGPU ? gpuUploadGetFrame : gpuDownloadGetFrame,
        gpuTransferFree, fmParallel, toGPU ? ffGPUOutput : 0, deps, 1, d.get(), core);
    if (vsapi->mapGetError(out)) {
        vsapi->freeNode(d->node);
        return;
    }
    d.release();
}

//////////////////////////////////////////
// GPUBoxBlur, the first real GPU filter: same arguments and bit identical integer output to
// std.BoxBlur, but all planes and both directions in one node with no transposes, since the
// kernel takes its direction as a push constant.

struct GPUBoxBlurData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    bool process[3] = {};
    int hradius = 0, hpasses = 0, vradius = 0, vpasses = 0;
    VSVulkanDevice *dev = nullptr;
    VSVulkanComputePipeline pipeline; /* the width matching the format is baked in at create */
    VSVulkanExecPool execPool;
};

struct BlurPush {
    uint32_t width;
    uint32_t height;
    uint32_t srcStride; /* in elements */
    uint32_t dstStride;
    uint32_t radius;
    uint32_t rounding; /* integer variants only */
    uint32_t vertical;
    float invDiv;      /* float variants only */
};

/* Owner of a temporary pass buffer whose lifetime must reach the submission's completion, so
   it rides the exec context's retain list like the source frame does. */
struct TmpBufferHolder {
    VSVulkanDevice *dev = nullptr;
    VSVulkanBuffer buffer;
};

static void releaseFrameRef(void *object) {
    static_cast<VSFrame *>(object)->release();
}

static void releaseTmpBuffer(void *object) {
    TmpBufferHolder *holder = static_cast<TmpBufferHolder *>(object);
    holder->dev->destroyBuffer(holder->buffer);
    delete holder;
}

static void computeToComputeBarrier(VSVulkanDevice *dev, VkCommandBuffer cmd) {
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    dev->vk.vkCmdPipelineBarrier2(cmd, &dependency);
}

const VSFrame *VS_CC gpuBoxBlurGetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
        int w = vsapi->getFrameWidth(src, 0);
        int h = vsapi->getFrameHeight(src, 0);

        /* Unprocessed planes are shared straight from the source; when every plane is
           processed there is nothing to share and the plain GPU constructor applies. */
        bool shareAny = false;
        for (int p = 0; p < fmt->numPlanes; p++)
            shareAny = shareAny || !d->process[p];
        VSFrame *dst;
        if (shareAny) {
            const VSFrame *planeSrc[3] = {};
            int planes[3] = { 0, 1, 2 };
            for (int p = 0; p < fmt->numPlanes; p++)
                planeSrc[p] = d->process[p] ? nullptr : src;
            dst = new VSFrame(*fmt, w, h, planeSrc, planes, src, core);
        } else {
            dst = new VSFrame(*fmt, w, h, src, core, true);
        }

        /* The ping pong scratch buffer, sized for the largest processed plane, needed as soon
           as any plane runs more than one pass. */
        int totalPasses = (d->hradius > 0 ? d->hpasses : 0) + (d->vradius > 0 ? d->vpasses : 0);
        std::string err;
        TmpBufferHolder *tmp = nullptr;
        if (totalPasses > 1) {
            VkDeviceSize maxBytes = 0;
            for (int p = 0; p < fmt->numPlanes; p++) {
                if (d->process[p]) {
                    const VSVulkanPlane *plane = src->getGPUPlane(p);
                    VkDeviceSize bytes = static_cast<VkDeviceSize>(plane->stride) * plane->height;
                    maxBytes = bytes > maxBytes ? bytes : maxBytes;
                }
            }
            tmp = new TmpBufferHolder();
            tmp->dev = d->dev;
            if (!d->dev->createBufferPooled(tmp->buffer, maxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, err)) {
                delete tmp;
                vsapi->setFilterError(("GPUBoxBlur: " + err).c_str(), frameCtx);
                vsapi->freeFrame(src);
                dst->release();
                return nullptr;
            }
        }

        VSVulkanExecContext *ctx = d->execPool.acquire(err);
        if (!ctx) {
            if (tmp)
                releaseTmpBuffer(tmp);
            vsapi->setFilterError(("GPUBoxBlur: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            dst->release();
            return nullptr;
        }

        bool firstDispatch = true;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (!d->process[p])
                continue;
            const VSVulkanPlane *srcPlane = src->getGPUPlane(p);
            VSVulkanPlane *dstPlane = dst->getGPUPlane(p);
            uint32_t strideElems = static_cast<uint32_t>(srcPlane->stride / fmt->bytesPerSample);

            /* The pass schedule replicates the CPU filter exactly: horizontal passes first
               with rounding div-1, 0, div-1, ... then vertical ones restarting the pattern
               with their own divisor. Float formats have no rounding term, just the
               reciprocal divisor. */
            struct Pass { uint32_t radius, rounding, vertical; float invDiv; };
            std::vector<Pass> schedule;
            if (d->hradius > 0) {
                for (int i = 0; i < d->hpasses; i++)
                    schedule.push_back({ static_cast<uint32_t>(d->hradius), (i == 0 || !(i & 1)) ? 2u * d->hradius : 0u, 0,
                        1.0f / (2 * d->hradius + 1) });
            }
            if (d->vradius > 0) {
                for (int i = 0; i < d->vpasses; i++)
                    schedule.push_back({ static_cast<uint32_t>(d->vradius), (i == 0 || !(i & 1)) ? 2u * d->vradius : 0u, 1,
                        1.0f / (2 * d->vradius + 1) });
            }

            VkBuffer srcBuf = srcPlane->buffer.buffer;
            const int passes = static_cast<int>(schedule.size());
            for (int i = 0; i < passes; i++) {
                /* Alternate so the final pass always lands in the destination plane. */
                VkBuffer target = ((passes - 1 - i) % 2 == 0) ? dstPlane->buffer.buffer : tmp->buffer.buffer;
                if (!firstDispatch)
                    computeToComputeBarrier(d->dev, ctx->commandBuffer());
                firstDispatch = false;

                BlurPush push = {};
                push.width = srcPlane->width;
                push.height = srcPlane->height;
                push.srcStride = strideElems;
                push.dstStride = strideElems;
                push.radius = schedule[i].radius;
                push.rounding = schedule[i].rounding;
                push.vertical = schedule[i].vertical;
                push.invDiv = schedule[i].invDiv;
                VkBuffer bufs[2] = { srcBuf, target };
                d->pipeline.recordDispatch(*ctx, bufs, 2, &push, sizeof(push),
                    (srcPlane->width + 15) / 16, (srcPlane->height + 15) / 16, 1);
                srcBuf = target;
            }
        }

        /* The source frame's reference rides the exec context until the GPU is done reading
           it; the scratch buffer the same. This is the reader safety GPU to GPU filtering
           needs, and no host wait happens anywhere in this function. */
        const_cast<VSFrame *>(src)->add_ref();
        d->execPool.retain(*ctx, releaseFrameRef, const_cast<VSFrame *>(src));
        if (tmp)
            d->execPool.retain(*ctx, releaseTmpBuffer, tmp);

        VSVulkanWaitList waits;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (d->process[p]) {
                const VSVulkanPlane *plane = src->getGPUPlane(p);
                waits.add(plane->readySemaphore, plane->readyValue);
            }
        }
        uint64_t value = 0;
        if (!d->execPool.submit(*ctx, err, &value, waits.data(), waits.size())) {
            vsapi->setFilterError(("GPUBoxBlur: " + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            dst->release();
            return nullptr;
        }

        /* Only the planes this submission wrote get the new producer pair; shared planes keep
           their original one, which consumers wait on independently. */
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (d->process[p]) {
                VSVulkanPlane *plane = dst->getGPUPlane(p);
                plane->readySemaphore = d->execPool.semaphore();
                plane->readyValue = value;
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC gpuBoxBlurFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC gpuBoxBlurCreate(const VSMap *in, VSMap *out, void *, VSCore *core, const VSAPI *vsapi) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    try {
        int err;
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);

        if (vi->format.colorFamily == cfUndefined)
            throw std::runtime_error("clips with variable format are not supported");
        if (vi->format.bytesPerSample != 1 && vi->format.bytesPerSample != 2 && vi->format.bytesPerSample != 4)
            throw std::runtime_error("unsupported sample size");

        auto d = std::make_unique<GPUBoxBlurData>();
        d->node = nullptr;
        d->vi = *vi;

        /* Same argument handling as std.BoxBlur so scripts can switch by changing the name. */
        for (int p = 0; p < 3; p++)
            d->process[p] = vi->format.numPlanes > p;
        int numPlaneArgs = vsapi->mapNumElements(in, "planes");
        if (numPlaneArgs > 0) {
            for (int p = 0; p < 3; p++)
                d->process[p] = false;
            for (int i = 0; i < numPlaneArgs; i++) {
                int plane = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);
                if (plane < 0 || plane >= vi->format.numPlanes)
                    throw std::runtime_error("plane index out of range");
                if (d->process[plane])
                    throw std::runtime_error("plane specified twice");
                d->process[plane] = true;
            }
        }

        d->hradius = vsapi->mapGetIntSaturated(in, "hradius", 0, &err);
        if (err)
            d->hradius = 1;
        d->hpasses = vsapi->mapGetIntSaturated(in, "hpasses", 0, &err);
        if (err)
            d->hpasses = 1;
        d->vradius = vsapi->mapGetIntSaturated(in, "vradius", 0, &err);
        if (err)
            d->vradius = 1;
        d->vpasses = vsapi->mapGetIntSaturated(in, "vpasses", 0, &err);
        if (err)
            d->vpasses = 1;

        if (d->hpasses < 0 || d->vpasses < 0)
            throw std::runtime_error("number of passes can't be negative");
        if (d->hradius < 0 || d->vradius < 0)
            throw std::runtime_error("radius can't be negative");
        if (d->hradius > 30000 || d->vradius > 30000)
            throw std::runtime_error("radius must be less than 30000");
        bool hblur = (d->hradius > 0) && (d->hpasses > 0);
        bool vblur = (d->vradius > 0) && (d->vpasses > 0);
        if (!hblur && !vblur)
            throw std::runtime_error("nothing to be performed");
        if (!hblur)
            d->hradius = 0;
        if (!vblur)
            d->vradius = 0;
        if (vi->width < 4 || vi->height < 4)
            throw std::runtime_error("dimensions must be at least 4x4");

        std::string vulkanError;
        d->dev = core->vulkanDevice(vulkanError);
        if (!d->dev)
            throw std::runtime_error(vulkanError);

        const uint32_t *spirv;
        size_t spirvBytes;
        if (vi->format.sampleType == stInteger) {
            spirv = vi->format.bytesPerSample == 1 ? boxblur8Spv : boxblur16Spv;
            spirvBytes = vi->format.bytesPerSample == 1 ? sizeof(boxblur8Spv) : sizeof(boxblur16Spv);
        } else if (vi->format.bytesPerSample == 4) {
            spirv = boxblursSpv;
            spirvBytes = sizeof(boxblursSpv);
        } else {
            if (!d->dev->shaderFloat16Supported())
                throw std::runtime_error("half precision formats need the shaderFloat16 feature, which this device lacks");
            spirv = boxblurhSpv;
            spirvBytes = sizeof(boxblurhSpv);
        }
        if (!d->pipeline.init(*d->dev, spirv, spirvBytes, 2, sizeof(BlurPush), vulkanError))
            throw std::runtime_error(vulkanError);
        if (!d->execPool.init(*d->dev, d->dev->computeQueue(), 4, vulkanError))
            throw std::runtime_error(vulkanError);

        d->node = node;
        node = nullptr;
        VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
        vsapi->createVideoFilterEx(out, "GPUBoxBlur", &d->vi, gpuBoxBlurGetFrame, gpuBoxBlurFree, fmParallel, ffGPUOutput, deps, 1, d.get(), core);
        if (vsapi->mapGetError(out)) {
            vsapi->freeNode(d->node);
            return;
        }
        d.release();
    } catch (const std::exception &e) {
        vsapi->freeNode(node);
        vsapi->mapSetError(out, (std::string("GPUBoxBlur: ") + e.what()).c_str());
    }
}

} // namespace

void gpuTransferInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("GPUUpload", "clip:vnode;", "clip:vnode:gpu;", gpuTransferCreate, reinterpret_cast<void *>(1), plugin);
    vspapi->registerFunction("GPUDownload", "clip:vnode:gpu;", "clip:vnode;", gpuTransferCreate, nullptr, plugin);
    vspapi->registerFunction("GPUBoxBlur", "clip:vnode:gpu;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;", "clip:vnode:gpu;", gpuBoxBlurCreate, nullptr, plugin);
}
