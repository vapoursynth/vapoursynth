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

#include <algorithm>
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

/* Acquires per demand epoch, per ring. A burst's size is kept for one to two epochs after it
   ends, 64 to 128 transfers: a couple of seconds of video, long enough that a graph
   alternating between sizes never pays a reallocation per frame, short enough that a brief
   high resolution segment does not pin frame sized staging for the rest of the run. */
constexpr uint32_t demandEpoch = 64;

} // namespace

VSVulkanTransfer::~VSVulkanTransfer() {
    if (!dev)
        return;
    /* The slot buffers are the source and destination of copies that may still be running;
       the same rule as the exec pool's destructor applies, and for the same reason. Leaving
       them costs the rings' staging until the device goes, which beats recycling memory a
       copy is reading. */
    std::string ignored;
    if (!execPool.waitAll(ignored) && !dev->deviceLost())
        return;
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
    if (!dev->waitTimelines(semaphores.data(), values.data(), list.size())) {
        errorMessage = dev->deviceLost() ? VSVulkanDevice::deviceLostMessage()
            : "waiting for a plane's producer failed and could not be retried";
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

    /* Sized to recent demand rather than to this request alone, and resized in both
       directions: too small for the request, or larger than the last two epochs of requests
       needed, which is how a burst of big frames stops pinning staging once it is over. */
    const VkDeviceSize wanted = noteDemand(ring, minSize);
    if (slot->buffer.size < minSize || slot->buffer.size > wanted) {
        dev->destroyBuffer(slot->buffer);
        /* Cached host memory measured slightly faster than write combined even for upload
           staging (memcpy in AND the GPU's reads), and for readback the difference is 40x,
           so both rings prefer it. */
        if (!dev->createBuffer(slot->buffer, wanted,
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

VkDeviceSize VSVulkanTransfer::noteDemand(SlotRing &ring, VkDeviceSize minSize) {
    ring.lastAcquire.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_release);
    std::lock_guard<std::mutex> lock(ring.demandMutex);
    if (++ring.epochAcquires > demandEpoch) {
        ring.previousEpochMax = ring.epochMax;
        ring.epochMax = 0;
        ring.epochAcquires = 1;
    }
    ring.epochMax = std::max(ring.epochMax, minSize);
    const VkDeviceSize demand = std::max(ring.epochMax, ring.previousEpochMax);
    return (demand + slotGranularity - 1) & ~(slotGranularity - 1);
}

VkDeviceSize VSVulkanTransfer::releaseIdle(std::chrono::steady_clock::duration idleAfter) {
    if (!dev)
        return 0;
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    VkDeviceSize freed = 0;
    for (SlotRing *ring : { &staging, &readback }) {
        if (now - ring->lastAcquire.load(std::memory_order_acquire) < idleAfter.count())
            continue;
        /* Sampled before any claim: a submission the counter had reached was complete before
           the walk, and one it had not is left alone however the walk interleaves with it.
           Values are monotonic, so a copy submitted during the walk is always past the
           sample. */
        uint64_t completed = 0;
        const bool haveCounter = execPool.completedValue(completed);
        for (auto &slot : ring->slots) {
            bool expected = false;
            if (!slot->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
                continue;
            if (slot->buffer.size && (!slot->value || (haveCounter && completed >= slot->value))) {
                freed += slot->buffer.size;
                dev->destroyBuffer(slot->buffer);
                slot->value = 0;
            }
            releaseSlot(*ring, *slot);
        }
        /* Whatever demand the freed buffers served is over; the next acquire sizes its slot to
           what it actually needs. */
        std::lock_guard<std::mutex> lock(ring->demandMutex);
        ring->epochMax = 0;
        ring->previousEpochMax = 0;
        ring->epochAcquires = 0;
    }
    return freed;
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

bool VSVulkanTransfer::downloadPlanes(const VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
    uint8_t *const dstPlanes[], const ptrdiff_t dstStrides[],
    VSGPUReleaseFunc releaseSource, void *source, std::string &errorMessage) {
    /* Ownership of source is taken here; see the declaration. Once it has been retained on a
       context the pool answers for it on every path -- a failed submit releases it at once, a
       successful one at the next sweep -- so only the returns ahead of that retain have to let
       it go, which is what this is for. */
    auto releaseUnused = [&]() {
        if (releaseSource)
            releaseSource(source);
    };
    /* Read straight out of the plane when its memory is host CACHED, mirroring how the upload
       side gates on host coherent. Cached is the condition that matters: reads then run at
       memcpy speed, measuring 5.8x faster on two Metal drivers than a DMA into host cached
       staging plus a memcpy out of it, and within noise of a host to host copy. Unified memory
       hands back plane memory that is device local, host visible, coherent and cached; a
       discrete card's is either not host visible or host visible and write combined, where
       reads over PCIe are orders of magnitude too slow, so those keep the staging path. */
    bool direct = !forceStaging;
    for (int p = 0; p < numPlanes; p++)
        direct = direct && planes[p]->buffer.mapped &&
            (planes[p]->buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
            (planes[p]->buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    if (direct) {
        /* The producers have to be waited AND their writes made available to the host domain.
           Completing a submission does not by itself put device writes in the host's reach --
           the same reason flushDeviceWrites exists for foreign readers -- so this submits the
           availability barrier the spec asks for instead of the plane copy. That keeps the
           submission count identical to the staging path while dropping the DMA copy of every
           plane and the staging buffer with it. */
        VSVulkanExecContext *ctx = execPool.acquire(errorMessage);
        if (!ctx) {
            releaseUnused();
            return false;
        }

        VkMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        dev->vk.vkCmdPipelineBarrier2(ctx->commandBuffer(), &dep);

        /* Retained before the submit, so the batch that waits these planes' producers cannot
           outlive the frame holding them however this call ends. Zero bytes: the frame's memory
           is already in the pool total, and metering it again would gate admission on it
           twice. */
        if (releaseSource)
            execPool.retain(*ctx, releaseSource, source, 0);

        VSVulkanWaitList waits;
        for (int p = 0; p < numPlanes; p++)
            waits.add(planes[p]->readyTimeline, planes[p]->readyValue);
        uint64_t value = 0;
        if (!execPool.submit(*ctx, errorMessage, &value, waits.data(), waits.size()))
            return false;
        if (!execPool.waitValue(value, errorMessage))
            return false;

        for (int p = 0; p < numPlanes; p++) {
            copyPlane(dstPlanes[p], dstStrides[p], static_cast<const uint8_t *>(planes[p]->buffer.mapped),
                planes[p]->stride, static_cast<size_t>(planes[p]->width) * bytesPerSample, planes[p]->height);
        }
        return true;
    }

    VkDeviceSize total = 0;
    for (int p = 0; p < numPlanes; p++)
        total += static_cast<VkDeviceSize>(planes[p]->stride) * planes[p]->height;

    Slot *slot = acquireSlot(readback, total, errorMessage);
    if (!slot) {
        releaseUnused();
        return false;
    }

    VSVulkanExecContext *ctx = execPool.acquire(errorMessage);
    if (!ctx) {
        releaseSlot(readback, *slot);
        releaseUnused();
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

    /* The host reads the staging bytes below, and the copies completing does not by itself
       put them in its reach; this is the same availability operation the direct path above
       submits in place of a copy, with the transfer rather than a dispatch as its source. */
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    dev->vk.vkCmdPipelineBarrier2(ctx->commandBuffer(), &dep);

    /* As on the direct path: the copy reads these planes, so the frame holding them is
       retained on the submission rather than on this call returning. */
    if (releaseSource)
        execPool.retain(*ctx, releaseSource, source, 0);

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
