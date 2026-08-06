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

#include "vsvulkan.h"

#include <cassert>

namespace {

/* Big enough that a 16 GB card tops out around 128 blocks, far from the 4096 allocation limit,
   small enough that a lightly loaded script does not reserve much it never touches. */
constexpr VkDeviceSize blockSize = 128ull << 20;
/* Region sizes round up to this, which is what makes the buckets coarse enough to actually get
   hits; the waste is under a quarter percent at plane sizes. */
constexpr VkDeviceSize regionGranularity = 4096;
/* At least this alignment for every region so any buffer requirement up to it is satisfied
   without tracking per usage alignments. */
constexpr VkDeviceSize regionAlignment = 256;

} // namespace

bool VSVulkanAllocator::allocate(VSVulkanDevice &dev, uint32_t typeIndex, VkDeviceSize size, VkDeviceSize alignment,
    bool exportable, Block *&block, VkDeviceSize &offset, VkDeviceSize &roundedSize, std::string &errorMessage) {
    roundedSize = (size + regionGranularity - 1) & ~(regionGranularity - 1);
    if (alignment < regionAlignment)
        alignment = regionAlignment;
    /* Recycled regions are handed out without realigning, which is safe because every offset
       ever carved is a multiple of regionGranularity: used starts at 0 and only grows by
       rounded sizes after an alignment step that never moves it. A future buffer usage with a
       stricter requirement would silently misbind recycled regions, hence the tripwire. */
    assert(alignment <= regionGranularity);
    const uint64_t bucketType = typeIndex | (exportable ? (1ull << 32) : 0);

    std::lock_guard<std::mutex> lock(mutex);

    auto bucket = freeLists.find({ bucketType, roundedSize });
    if (bucket != freeLists.end() && !bucket->second.empty()) {
        block = bucket->second.back().first;
        offset = bucket->second.back().second;
        bucket->second.pop_back();
        freeRegions--;
        usedBytes += roundedSize;
        block->liveRegions++;
        return true;
    }

    for (auto &candidate : blocks) {
        if (candidate->typeIndex != typeIndex || candidate->exportable != exportable)
            continue;
        VkDeviceSize aligned = (candidate->used + alignment - 1) & ~(alignment - 1);
        if (aligned + roundedSize <= candidate->size) {
            candidate->used = aligned + roundedSize;
            block = candidate.get();
            offset = aligned;
            usedBytes += roundedSize;
            candidate->liveRegions++;
            return true;
        }
    }

    /* A region bigger than a block gets a block of its own. */
    VkDeviceSize newBlockSize = roundedSize > blockSize ? roundedSize : blockSize;

    /* Every block opts into device addresses since any buffer bound to it may want one, and
       whole blocks are mapped once when the type allows it so regions never map at all.
       Exportable blocks additionally chain the export info, matched by the external memory
       info on every buffer that will bind to them. */
    VkExportMemoryAllocateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = dev.exportHandleType();
    VkMemoryAllocateFlagsInfo allocFlags = {};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.pNext = exportable ? &exportInfo : nullptr;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = newBlockSize;
    allocInfo.memoryTypeIndex = typeIndex;

    auto fresh = std::make_unique<Block>();
    fresh->size = newBlockSize;
    fresh->typeIndex = typeIndex;
    fresh->exportId = nextExportId++;
    fresh->exportable = exportable;
    VkResult res = dev.vk.vkAllocateMemory(dev.device(), &allocInfo, nullptr, &fresh->memory);
    if (res != VK_SUCCESS) {
        /* The freelists may bank fully idle blocks of other types and sizes holding exactly
           the memory this block needs; hand those back and retry once so an allocation racing
           ahead of the next cache sweep does not fail while VRAM sits reclaimable. */
        const VkDeviceSize reclaimed = trimLocked(dev);
        if (reclaimed > 0)
            res = dev.vk.vkAllocateMemory(dev.device(), &allocInfo, nullptr, &fresh->memory);
        if (res != VK_SUCCESS) {
            /* What the allocator was holding when the driver said no. Without this the message
               says only that VRAM ran out, which is equally true of a leak, of fragmentation
               and of a workload that simply wants more than the card has -- and those want
               different fixes. blockBytes is what the driver handed us; usedBytes is what is
               live inside it, and the gap between them is what suballocation is costing. */
            VkDeviceSize blockBytes = 0;
            for (const auto &b : blocks)
                blockBytes += b->size;
            errorMessage = "vkAllocateMemory failed for a " + std::to_string(newBlockSize >> 20) +
                " MB allocator block (VkResult " + std::to_string(res) + "); allocator held " +
                std::to_string(blocks.size()) + " blocks totalling " +
                std::to_string(blockBytes >> 20) + " MB, of which " +
                std::to_string(usedBytes >> 20) + " MB was live in " +
                std::to_string(freeRegions) + " free regions, and trimming reclaimed " +
                std::to_string(reclaimed >> 20) + " MB";
            return false;
        }
    }

    if (dev.memoryProperties().memoryTypes[typeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VkMemoryMapInfo mapInfo = {};
        mapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
        mapInfo.memory = fresh->memory;
        mapInfo.size = VK_WHOLE_SIZE;
        res = dev.vk.vkMapMemory2(dev.device(), &mapInfo, &fresh->mapped);
        if (res != VK_SUCCESS) {
            dev.vk.vkFreeMemory(dev.device(), fresh->memory, nullptr);
            errorMessage = "vkMapMemory2 failed for an allocator block (VkResult " + std::to_string(res) + ")";
            return false;
        }
    }

    fresh->used = roundedSize;
    fresh->liveRegions = 1;
    block = fresh.get();
    offset = 0;
    usedBytes += roundedSize;
    /* The whole block, once: the block is what the driver has committed, and that is what the
       budget is measured against. Accounting the live regions instead let the core sit just
       under its limit while the driver held ~18 percent more and hit the wall. */
    dev.accountAllocation(static_cast<int64_t>(newBlockSize));
    blocks.push_back(std::move(fresh));
    return true;
}

void VSVulkanAllocator::free(Block *block, VkDeviceSize offset, VkDeviceSize roundedSize) {
    std::lock_guard<std::mutex> lock(mutex);
    freeLists[{ block->typeIndex | (block->exportable ? (1ull << 32) : 0), roundedSize }].push_back({ block, offset });
    freeRegions++;
    usedBytes -= roundedSize;
    block->liveRegions--;
}

VkDeviceSize VSVulkanAllocator::trim(VSVulkanDevice &dev) {
    std::lock_guard<std::mutex> lock(mutex);
    return trimLocked(dev);
}

VkDeviceSize VSVulkanAllocator::trimLocked(VSVulkanDevice &dev) {
    VkDeviceSize freed = 0;
    for (auto it = blocks.begin(); it != blocks.end();) {
        if ((*it)->liveRegions != 0) {
            ++it;
            continue;
        }
        Block *victim = it->get();
        /* Its banked regions become unreachable with the block, so they leave the buckets. */
        for (auto bucket = freeLists.begin(); bucket != freeLists.end();) {
            auto &regions = bucket->second;
            for (size_t i = regions.size(); i > 0; i--) {
                if (regions[i - 1].first == victim) {
                    regions.erase(regions.begin() + (i - 1));
                    freeRegions--;
                }
            }
            bucket = regions.empty() ? freeLists.erase(bucket) : std::next(bucket);
        }
        dev.vk.vkFreeMemory(dev.device(), victim->memory, nullptr); /* implicitly unmaps */
        dev.accountAllocation(-static_cast<int64_t>(victim->size));
        freed += victim->size;
        it = blocks.erase(it);
    }
    return freed;
}

void VSVulkanAllocator::destroy(VSVulkanDevice &dev) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &block : blocks) {
        if (block->memory) {
            dev.vk.vkFreeMemory(dev.device(), block->memory, nullptr); /* implicitly unmaps */
            dev.accountAllocation(-static_cast<int64_t>(block->size));
        }
    }
    blocks.clear();
    freeLists.clear();
    usedBytes = 0;
    freeRegions = 0;
}

VSVulkanAllocatorStats VSVulkanAllocator::stats() const {
    std::lock_guard<std::mutex> lock(mutex);
    VSVulkanAllocatorStats out;
    out.blockCount = blocks.size();
    for (const auto &block : blocks)
        out.blockBytes += block->size;
    out.usedBytes = usedBytes;
    out.freeRegionCount = freeRegions;
    return out;
}

/* Regions are whole regionGranularity pages -- offsets are multiples of it and sizes round up
   to it -- so two resources can never land on the same buffer/image granularity page as long
   as the device asks for no more than that. Every driver seen reports far less (1 on the cards
   tested here), and one that did not would need per region type tracking, so the public memory
   entry point refuses on such a device rather than pretending. */
bool VSVulkanDevice::poolAllowsMixedResourceTypes() const {
    return props.limits.bufferImageGranularity <= regionGranularity;
}

/* The allocation half shared by pooled buffers and by the public memory entry point: choose a
   memory type, take a region, and when the driver says no, climb the reclamation ladder before
   giving up. The allocator's own trim already ran inside allocate, so what is left to reclaim
   is what other subsystems hold -- exec pool retentions whose submissions completed but whose
   context was never acquired again (a graph mid teardown parks its whole in flight footprint
   that way), and cached GPU frames, which the pressure callback has the core evict. Both free
   regions and empty out blocks, so the retry can be satisfied from the free lists or by the
   trim allocate runs internally when a fresh block still fails. */
bool VSVulkanDevice::allocatePooled(const VkMemoryRequirements &req, VkMemoryPropertyFlags requiredFlags,
    VkMemoryPropertyFlags preferredFlags, bool exportable, VSVulkanPooledRegion &region,
    std::string &errorMessage) {
    uint32_t typeIndex = findMemoryType(req.memoryTypeBits, requiredFlags, preferredFlags);
    if (typeIndex == UINT32_MAX) {
        errorMessage = "No memory type provides the requested properties for this allocation";
        return false;
    }

    /* Regions are carved and recycled on regionGranularity boundaries, which every buffer is
       happy with, but images routinely are not: this card wants 65536 for a 256x128 R32 image
       and 256 for the same format at 1920x1080, so a coarser requirement is a normal case and
       not an error to report. Reserving the extra distance to the next boundary and binding
       inside the region satisfies it without disturbing the invariant the recycling depends
       on -- the region still starts and ends where the buckets expect. The overshoot is at
       most alignment - regionGranularity, since the region already starts on a granularity
       boundary; that is invisible on a plane sized image and worst on a tiny one. */
    VkDeviceSize request = req.size;
    VkDeviceSize carveAlignment = req.alignment;
    if (req.alignment > regionGranularity) {
        request += req.alignment - regionGranularity;
        carveAlignment = regionGranularity;
    }

    if (!allocator.allocate(*this, typeIndex, request, carveAlignment, exportable, region.block,
            region.offset, region.size, errorMessage)) {
        sweepExecPools();
        if (pressureFn)
            pressureFn(pressureUserData);
        errorMessage.clear();
        if (!allocator.allocate(*this, typeIndex, request, carveAlignment, exportable, region.block,
                region.offset, region.size, errorMessage))
            return false;
    }

    region.usableOffset = (region.offset + req.alignment - 1) & ~(req.alignment - 1);
    assert(region.usableOffset + req.size <= region.offset + region.size);
    return true;
}

void VSVulkanDevice::freePooled(const VSVulkanPooledRegion &region) {
    allocator.free(region.block, region.offset, region.size);
}

bool VSVulkanDevice::createBufferPooled(VSVulkanBuffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, std::string &errorMessage) {
    buffer = {};

    uint32_t families[2] = { computeQ.family, transferQ.family };
    /* Device local pools are the exportable ones; the external info here and the export info
       on their blocks must appear together or binding is invalid, and it restricts the
       compatible memory types, which is why host visible pools stay out of it. */
    const bool exportable = exportType && (requiredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkExternalMemoryBufferCreateInfo externalInfo = {};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = exportType;
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = exportable ? &externalInfo : nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    if (hasDedicatedTransferQueue()) {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = 2;
        bufferInfo.pQueueFamilyIndices = families;
    } else {
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult res = vk.vkCreateBuffer(deviceHandle, &bufferInfo, nullptr, &buffer.buffer);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateBuffer failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    VkBufferMemoryRequirementsInfo2 reqInfo = {};
    reqInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    reqInfo.buffer = buffer.buffer;
    VkMemoryRequirements2 req = {};
    req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    vk.vkGetBufferMemoryRequirements2(deviceHandle, &reqInfo, &req);

    VSVulkanPooledRegion region;
    if (!allocatePooled(req.memoryRequirements, requiredFlags, preferredFlags, exportable, region, errorMessage)) {
        destroyBuffer(buffer);
        return false;
    }
    VSVulkanAllocator::Block *block = region.block;
    const VkDeviceSize offset = region.usableOffset;

    VkBindBufferMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    bindInfo.buffer = buffer.buffer;
    bindInfo.memory = block->memory;
    bindInfo.memoryOffset = offset;
    res = vk.vkBindBufferMemory2(deviceHandle, 1, &bindInfo);
    if (res != VK_SUCCESS) {
        freePooled(region);
        errorMessage = "vkBindBufferMemory2 failed (VkResult " + std::to_string(res) + ")";
        destroyBuffer(buffer);
        return false;
    }

    buffer.memory = block->memory;
    buffer.size = size;
    buffer.memoryFlags = memProps.memoryTypes[block->typeIndex].propertyFlags;
    /* The region as carved, which is what gives it back; the buffer itself sits at the
       usable offset inside it. */
    buffer.poolBlock = block;
    buffer.poolOffset = region.offset;
    buffer.poolSize = region.size;
    if (block->mapped)
        buffer.mapped = static_cast<uint8_t *>(block->mapped) + offset;

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.buffer;
        buffer.address = vk.vkGetBufferDeviceAddress(deviceHandle, &addressInfo);
    }

    return true;
}
