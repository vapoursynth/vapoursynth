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

#include "vsvulkanframe.h"

#include <cstring>

namespace {

/* Planes are always allocated stride * height on both sides, the same guarantee VSFrame makes,
   so matching strides move as one flat copy and only mismatched ones pay the row loop. */
void copyPlane(uint8_t *dst, ptrdiff_t dstStride, const uint8_t *src, ptrdiff_t srcStride, size_t rowBytes, uint32_t height) {
    if (dstStride == srcStride) {
        memcpy(dst, src, static_cast<size_t>(srcStride) * height);
    } else {
        for (uint32_t row = 0; row < height; row++) {
            memcpy(dst, src, rowBytes);
            dst += dstStride;
            src += srcStride;
        }
    }
}

constexpr VkDeviceSize slotGranularity = 1 << 20;

} // namespace

VSVulkanTransfer::~VSVulkanTransfer() {
    if (!dev)
        return;
    std::string ignored;
    execPool.waitAll(ignored);
    for (auto &slot : staging.slots)
        dev->destroyBuffer(slot->buffer);
    for (auto &slot : readback.slots)
        dev->destroyBuffer(slot->buffer);
}

bool VSVulkanTransfer::init(VSVulkanDevice &device, uint32_t slots, std::string &errorMessage) {
    if (dev) {
        errorMessage = "VSVulkanTransfer cannot be initialized twice";
        return false;
    }
    if (slots == 0) {
        errorMessage = "A transfer needs at least one slot";
        return false;
    }

    dev = &device;
    if (!execPool.init(device, device.transferQueue(), slots, errorMessage))
        return false;

    /* Slot buffers are created lazily at first use since the frame sizes are unknown here. */
    for (uint32_t i = 0; i < slots; i++) {
        staging.slots.push_back(std::make_unique<Slot>());
        readback.slots.push_back(std::make_unique<Slot>());
    }
    return true;
}

/* Device local required, host visible preferred: on resizable BAR systems every plane lands
   writable straight from the CPU and uploads never touch staging. Pooled, since planes are
   exactly what the block allocator exists for. */
bool createGPUPlane(VSVulkanDevice &device, uint32_t width, uint32_t height, int bytesPerSample,
    ptrdiff_t stride, VSVulkanPlane &plane, std::string &errorMessage) {
    plane.reset();
    plane.width = width;
    plane.height = height;
    plane.stride = stride;
    if (!device.createBufferPooled(plane.buffer, static_cast<VkDeviceSize>(stride) * height,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, errorMessage)) {
        plane.reset();
        return false;
    }
    return true;
}

bool VSVulkanTransfer::createFrame(VSVulkanFrame &frame, const VSVideoFormat &format, int width, int height, std::string &errorMessage) {
    frame.reset();

    if (format.numPlanes < 1 || format.numPlanes > 3 || format.bytesPerSample < 1 || format.bytesPerSample > 4 ||
        width <= 0 || height <= 0) {
        errorMessage = "Invalid format or dimensions for a GPU frame";
        return false;
    }
    if ((width % (1 << format.subSamplingW)) || (height % (1 << format.subSamplingH))) {
        errorMessage = "Frame dimensions not a multiple of the subsampling";
        return false;
    }

    frame.format = format;
    frame.numPlanes = format.numPlanes;

    for (int p = 0; p < format.numPlanes; p++) {
        uint32_t pw = static_cast<uint32_t>(p ? width >> format.subSamplingW : width);
        uint32_t ph = static_cast<uint32_t>(p ? height >> format.subSamplingH : height);
        /* The same 64 byte row alignment CPU planes get, so strides usually match end to end. */
        ptrdiff_t stride = (static_cast<ptrdiff_t>(pw) * format.bytesPerSample + 63) & ~static_cast<ptrdiff_t>(63);
        if (!createGPUPlane(*dev, pw, ph, format.bytesPerSample, stride, frame.planes[p], errorMessage)) {
            destroyFrame(frame);
            return false;
        }
    }
    return true;
}

void VSVulkanTransfer::destroyFrame(VSVulkanFrame &frame) {
    for (int p = 0; p < frame.numPlanes; p++)
        dev->destroyBuffer(frame.planes[p].buffer);
    frame.reset();
}

bool VSVulkanTransfer::waitPlanesHost(VSVulkanPlane *const planes[], int numPlanes, std::string &errorMessage) {
    VSVulkanWaitList list;
    for (int p = 0; p < numPlanes; p++)
        list.add(planes[p]->readyTimeline, planes[p]->readyValue);
    if (!list.size())
        return true;

    std::vector<VkSemaphore> semaphores(list.size());
    std::vector<uint64_t> values(list.size());
    for (uint32_t i = 0; i < list.size(); i++) {
        semaphores[i] = list.data()[i].semaphore;
        values[i] = list.data()[i].value;
    }
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = list.size();
    waitInfo.pSemaphores = semaphores.data();
    waitInfo.pValues = values.data();
    VkResult res = dev->vk.vkWaitSemaphores(dev->device(), &waitInfo, UINT64_MAX);
    if (res != VK_SUCCESS) {
        errorMessage = "vkWaitSemaphores failed (VkResult " + std::to_string(res) + ")";
        return false;
    }
    return true;
}

VSVulkanTransfer::Slot *VSVulkanTransfer::acquireSlot(SlotRing &ring, VkDeviceSize minSize, std::string &errorMessage) {
    Slot *slot = nullptr;
    const size_t count = ring.slots.size();

    for (size_t attempt = 0; attempt < count && !slot; attempt++) {
        Slot *candidate = ring.slots[ring.cursor.fetch_add(1, std::memory_order_relaxed) % count].get();
        bool expected = false;
        if (candidate->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
            slot = candidate;
    }
    if (!slot) {
        std::unique_lock<std::mutex> lock(ring.claimMutex);
        ring.claimCv.wait(lock, [&]() {
            for (auto &candidate : ring.slots) {
                bool expected = false;
                if (candidate->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                    slot = candidate.get();
                    return true;
                }
            }
            return false;
        });
    }

    /* The slot's previous submission must be done both before its bytes are rewritten and
       before a shrink sized buffer is replaced. */
    if (slot->value && !execPool.waitValue(slot->value, errorMessage)) {
        releaseSlot(ring, *slot);
        return nullptr;
    }

    if (slot->buffer.size < minSize) {
        dev->destroyBuffer(slot->buffer);
        VkDeviceSize rounded = (minSize + slotGranularity - 1) & ~(slotGranularity - 1);
        /* Cached host memory measured slightly faster than write combined even for upload
           staging (memcpy in AND the GPU's reads), and for readback the difference is 40x,
           so both rings prefer it. */
        if (!dev->createBuffer(slot->buffer, rounded,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT, errorMessage)) {
            releaseSlot(ring, *slot);
            return nullptr;
        }
    }
    return slot;
}

void VSVulkanTransfer::releaseSlot(SlotRing &ring, Slot &slot) {
    slot.claimed.store(false, std::memory_order_release);
    { std::lock_guard<std::mutex> lock(ring.claimMutex); }
    ring.claimCv.notify_one();
}

bool VSVulkanTransfer::uploadPlanes(VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
    const uint8_t *const srcPlanes[], const ptrdiff_t srcStrides[], std::string &errorMessage) {
    /* Host visible is what makes the plane mappable, but only coherence makes the memcpy
       visible to the device without an explicit flush, and createGPUPlane only *prefers*
       coherence -- a device offering device local host visible memory without it would
       otherwise take this path and lose the writes. */
    bool rebar = !forceStaging;
    for (int p = 0; p < numPlanes; p++)
        rebar = rebar && planes[p]->buffer.mapped &&
            (planes[p]->buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (rebar) {
        /* Straight into VRAM, no staging, no submission. The only wait is for whatever last
           wrote the planes on the GPU, and host writes are implicitly visible to any submission
           that comes after them. */
        if (!waitPlanesHost(planes, numPlanes, errorMessage))
            return false;
        for (int p = 0; p < numPlanes; p++) {
            copyPlane(static_cast<uint8_t *>(planes[p]->buffer.mapped), planes[p]->stride, srcPlanes[p], srcStrides[p],
                static_cast<size_t>(planes[p]->width) * bytesPerSample, planes[p]->height);
            setPlaneProducer(*planes[p], nullptr, 0);
        }
        return true;
    }

    VkDeviceSize total = 0;
    for (int p = 0; p < numPlanes; p++)
        total += static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;

    /* Slot before context, always, so the two rings cannot deadlock against each other. */
    Slot *slot = acquireSlot(staging, total, errorMessage);
    if (!slot)
        return false;

    VkDeviceSize offset = 0;
    for (int p = 0; p < numPlanes; p++) {
        copyPlane(static_cast<uint8_t *>(slot->buffer.mapped) + offset, planes[p]->stride, srcPlanes[p], srcStrides[p],
            static_cast<size_t>(planes[p]->width) * bytesPerSample, planes[p]->height);
        offset += static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;
    }

    VSVulkanExecContext *ctx = execPool.acquire(errorMessage);
    if (!ctx) {
        releaseSlot(staging, *slot);
        return false;
    }

    VkBufferCopy2 regions[3] = {};
    VkCopyBufferInfo2 copies[3] = {};
    offset = 0;
    for (int p = 0; p < numPlanes; p++) {
        regions[p].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        regions[p].srcOffset = offset;
        regions[p].size = static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;
        copies[p].sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copies[p].srcBuffer = slot->buffer.buffer;
        copies[p].dstBuffer = planes[p]->buffer.buffer;
        copies[p].regionCount = 1;
        copies[p].pRegions = &regions[p];
        dev->vk.vkCmdCopyBuffer2(ctx->commandBuffer(), &copies[p]);
        offset += regions[p].size;
    }

    /* The device side wait on the previous producers covers overwrite safety without blocking
       the host; fresh planes have no producers and wait on nothing. */
    VSVulkanWaitList waits;
    for (int p = 0; p < numPlanes; p++)
        waits.add(planes[p]->readyTimeline, planes[p]->readyValue);
    uint64_t value = 0;
    bool ok = execPool.submit(*ctx, errorMessage, &value, waits.data(), waits.size());
    if (ok) {
        slot->value = value;
        for (int p = 0; p < numPlanes; p++) {
            setPlaneProducer(*planes[p], execPool.timelineObject(), value);
        }
    }
    releaseSlot(staging, *slot);
    return ok;
}

bool VSVulkanTransfer::upload(VSVulkanFrame &frame, const uint8_t *const srcPlanes[3], const ptrdiff_t srcStrides[3], std::string &errorMessage) {
    VSVulkanPlane *planes[3] = { &frame.planes[0], &frame.planes[1], &frame.planes[2] };
    return uploadPlanes(planes, frame.numPlanes, frame.format.bytesPerSample, srcPlanes, srcStrides, errorMessage);
}

bool VSVulkanTransfer::downloadPlanes(const VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
    uint8_t *const dstPlanes[], const ptrdiff_t dstStrides[], std::string &errorMessage) {
    VkDeviceSize total = 0;
    for (int p = 0; p < numPlanes; p++)
        total += static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;

    Slot *slot = acquireSlot(readback, total, errorMessage);
    if (!slot)
        return false;

    VSVulkanExecContext *ctx = execPool.acquire(errorMessage);
    if (!ctx) {
        releaseSlot(readback, *slot);
        return false;
    }

    VkBufferCopy2 regions[3] = {};
    VkCopyBufferInfo2 copies[3] = {};
    VkDeviceSize offset = 0;
    for (int p = 0; p < numPlanes; p++) {
        regions[p].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        regions[p].dstOffset = offset;
        regions[p].size = static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;
        copies[p].sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copies[p].srcBuffer = planes[p]->buffer.buffer;
        copies[p].dstBuffer = slot->buffer.buffer;
        copies[p].regionCount = 1;
        copies[p].pRegions = &regions[p];
        dev->vk.vkCmdCopyBuffer2(ctx->commandBuffer(), &copies[p]);
        offset += regions[p].size;
    }

    VSVulkanWaitList waits;
    for (int p = 0; p < numPlanes; p++)
        waits.add(planes[p]->readyTimeline, planes[p]->readyValue);
    uint64_t value = 0;
    if (!execPool.submit(*ctx, errorMessage, &value, waits.data(), waits.size())) {
        releaseSlot(readback, *slot);
        return false;
    }
    slot->value = value;

    if (!execPool.waitValue(value, errorMessage)) {
        releaseSlot(readback, *slot);
        return false;
    }

    offset = 0;
    for (int p = 0; p < numPlanes; p++) {
        copyPlane(dstPlanes[p], dstStrides[p], static_cast<const uint8_t *>(slot->buffer.mapped) + offset,
            planes[p]->stride, static_cast<size_t>(planes[p]->width) * bytesPerSample, planes[p]->height);
        offset += static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;
    }

    releaseSlot(readback, *slot);
    return true;
}

bool VSVulkanTransfer::download(const VSVulkanFrame &frame, uint8_t *const dstPlanes[3], const ptrdiff_t dstStrides[3], std::string &errorMessage) {
    const VSVulkanPlane *planes[3] = { &frame.planes[0], &frame.planes[1], &frame.planes[2] };
    return downloadPlanes(planes, frame.numPlanes, frame.format.bytesPerSample, dstPlanes, dstStrides, errorMessage);
}
