/*
* Copyright (c) 2012-2026 Fredrik Mellbin
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
#include "kernel/cpulevel.h"
#include "vscore.h"
#include "vsvulkanframe.h"

#include <memory>
#include <string>

//////////////////////////////////////////
// SetAudio/VideoCache

static void VS_CC setCache(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    int mode = vsapi->mapGetIntSaturated(in, "mode", 0, &err);
    if (!err)
        vsapi->setCacheMode(node, mode);
    int fixedsize = vsapi->mapGetIntSaturated(in, "fixedsize", 0, &err);
    if (err)
        fixedsize = -1;
    int maxsize = vsapi->mapGetIntSaturated(in, "maxsize", 0, &err);
    if (err)
        maxsize = -1;
    int maxhistory = vsapi->mapGetIntSaturated(in, "maxhistory", 0, &err);
    if (err)
        maxhistory = -1;
    vsapi->setCacheOptions(node, fixedsize, maxsize, maxhistory);
    vsapi->freeNode(node);
}

//////////////////////////////////////////
// SetMaxCpu

static void VS_CC setMaxCpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *str = vsapi->mapGetData(in, "cpu", 0, nullptr);
    int level = vs_cpulevel_from_str(str);
    level = vs_set_cpulevel(core, level);
    str = vs_cpulevel_to_str(level);
    vsapi->mapSetData(out, "cpu", str, -1, dtUtf8, maReplace);
}

//////////////////////////////////////////
// GPUUpload/GPUDownload
//
// The CPU/GPU boundary filters. Everything else about GPU residency is types and bookkeeping;
// these two are where bytes actually cross the bus, with the policies the transfer benchmark
// picked. Both run fmParallel since the shared transfer machinery is thread safe and the
// per instance state is read only after construction.

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

        const VSVulkanPlane *planes[3] = {};
        uint8_t *dstPlanes[3] = {};
        ptrdiff_t dstStrides[3] = {};
        for (int p = 0; p < fmt->numPlanes; p++) {
            planes[p] = src->getGPUPlane(p);
            dstPlanes[p] = dst->getWritePtr(p);
            dstStrides[p] = dst->getStride(p);
        }

        if (!transfer->downloadPlanes(planes, fmt->numPlanes, fmt->bytesPerSample, dstPlanes, dstStrides, err)) {
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

    if ((vsapi->getNodeResidency(d->node) == nrGPU) == toGPU) {
        vsapi->mapConsumeNode(out, "clip", d->node, maAppend);
        return;
    }

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

} // namespace

//////////////////////////////////////////
// Init

void internalFiltersInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("SetAudioCache", "clip:anode;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetVideoCache", "clip:vnode:all;mode:int:opt;fixedsize:int:opt;maxsize:int:opt;maxhistory:int:opt;", "", setCache, 0, plugin);
    vspapi->registerFunction("SetMaxCPU", "cpu:data;", "cpu:data;", setMaxCpu, 0, plugin);
    vspapi->registerFunction("GPUUpload", "clip:vnode:all;", "clip:vnode:gpu;", gpuTransferCreate, reinterpret_cast<void *>(1), plugin);
    vspapi->registerFunction("GPUDownload", "clip:vnode:all;", "clip:vnode;", gpuTransferCreate, nullptr, plugin);
}
