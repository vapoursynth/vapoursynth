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
        dev.accountAllocation(static_cast<int64_t>(roundedSize));
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
            dev.accountAllocation(static_cast<int64_t>(roundedSize));
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
        errorMessage = "vkAllocateMemory failed for a " + std::to_string(newBlockSize >> 20) +
            " MB allocator block (VkResult " + std::to_string(res) + ")";
        return false;
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
    dev.accountAllocation(static_cast<int64_t>(roundedSize));
    blocks.push_back(std::move(fresh));
    return true;
}

void VSVulkanAllocator::free(VSVulkanDevice &dev, Block *block, VkDeviceSize offset, VkDeviceSize roundedSize) {
    std::lock_guard<std::mutex> lock(mutex);
    freeLists[{ block->typeIndex | (block->exportable ? (1ull << 32) : 0), roundedSize }].push_back({ block, offset });
    freeRegions++;
    usedBytes -= roundedSize;
    block->liveRegions--;
    dev.accountAllocation(-static_cast<int64_t>(roundedSize));
}

VkDeviceSize VSVulkanAllocator::trim(VSVulkanDevice &dev) {
    std::lock_guard<std::mutex> lock(mutex);
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
        freed += victim->size;
        it = blocks.erase(it);
    }
    return freed;
}

void VSVulkanAllocator::destroy(VSVulkanDevice &dev) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &block : blocks) {
        if (block->memory)
            dev.vk.vkFreeMemory(dev.device(), block->memory, nullptr); /* implicitly unmaps */
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

    uint32_t typeIndex = findMemoryType(req.memoryRequirements.memoryTypeBits, requiredFlags, preferredFlags);
    if (typeIndex == UINT32_MAX) {
        errorMessage = "No memory type provides the requested properties for this buffer";
        destroyBuffer(buffer);
        return false;
    }

    VSVulkanAllocator::Block *block = nullptr;
    VkDeviceSize offset = 0, roundedSize = 0;
    if (!allocator.allocate(*this, typeIndex, req.memoryRequirements.size, req.memoryRequirements.alignment,
            exportable, block, offset, roundedSize, errorMessage)) {
        destroyBuffer(buffer);
        return false;
    }

    VkBindBufferMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    bindInfo.buffer = buffer.buffer;
    bindInfo.memory = block->memory;
    bindInfo.memoryOffset = offset;
    res = vk.vkBindBufferMemory2(deviceHandle, 1, &bindInfo);
    if (res != VK_SUCCESS) {
        allocator.free(*this, block, offset, roundedSize);
        errorMessage = "vkBindBufferMemory2 failed (VkResult " + std::to_string(res) + ")";
        destroyBuffer(buffer);
        return false;
    }

    buffer.memory = block->memory;
    buffer.size = size;
    buffer.memoryFlags = memProps.memoryTypes[typeIndex].propertyFlags;
    buffer.poolBlock = block;
    buffer.poolOffset = offset;
    buffer.poolSize = roundedSize;
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
