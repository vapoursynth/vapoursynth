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

#include "vsvulkanexec.h"

VSVulkanExecPool::~VSVulkanExecPool() {
    if (!dev)
        return;
    std::string ignored;
    waitAll(ignored);
    for (auto &context : contexts) {
        if (context->commandPool)
            dev->vk.vkDestroyCommandPool(dev->device(), context->commandPool, nullptr);
    }
    if (timeline)
        dev->vk.vkDestroySemaphore(dev->device(), timeline, nullptr);
}

bool VSVulkanExecPool::init(VSVulkanDevice &device, VSVulkanQueue &queue, uint32_t contextCount, std::string &errorMessage) {
    if (dev) {
        errorMessage = "VSVulkanExecPool cannot be initialized twice";
        return false;
    }
    if (contextCount == 0) {
        errorMessage = "An exec pool needs at least one context";
        return false;
    }

    dev = &device;
    q = &queue;

    VkSemaphoreTypeCreateInfo typeInfo = {};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &typeInfo;
    VkResult res = dev->vk.vkCreateSemaphore(dev->device(), &semaphoreInfo, nullptr, &timeline);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateSemaphore failed for the pool timeline (VkResult " + std::to_string(res) + ")";
        return false;
    }

    for (uint32_t i = 0; i < contextCount; i++) {
        auto context = std::make_unique<VSVulkanExecContext>();

        /* One transient pool per context: pools are externally synchronized, and per context
           ownership means resetting one never has to coordinate with any other thread. */
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = q->familyIndex();
        res = dev->vk.vkCreateCommandPool(dev->device(), &poolInfo, nullptr, &context->commandPool);
        if (res != VK_SUCCESS) {
            errorMessage = "vkCreateCommandPool failed (VkResult " + std::to_string(res) + ")";
            return false;
        }
        /* Pushed before the buffer allocation so the destructor sees the pool either way. */
        contexts.push_back(std::move(context));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = contexts.back()->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        res = dev->vk.vkAllocateCommandBuffers(dev->device(), &allocInfo, &contexts.back()->cmd);
        if (res != VK_SUCCESS) {
            errorMessage = "vkAllocateCommandBuffers failed (VkResult " + std::to_string(res) + ")";
            return false;
        }
    }

    return true;
}

VSVulkanExecContext *VSVulkanExecPool::acquire(std::string &errorMessage) {
    VSVulkanExecContext *context = nullptr;
    const size_t count = contexts.size();

    /* Fast path: race for an unclaimed slot starting at the ring cursor. Distinct threads get
       distinct starting points, so under light contention this is one CAS and no locks. */
    for (size_t attempt = 0; attempt < count && !context; attempt++) {
        VSVulkanExecContext *candidate = contexts[cursor.fetch_add(1, std::memory_order_relaxed) % count].get();
        bool expected = false;
        if (candidate->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
            context = candidate;
    }

    /* Slow path: more threads than contexts, so wait for a release. This is the intended
       backpressure when a filter is called on more threads than it keeps frames in flight. */
    if (!context) {
        std::unique_lock<std::mutex> lock(claimMutex);
        claimCv.wait(lock, [&]() {
            for (auto &candidate : contexts) {
                bool expected = false;
                if (candidate->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                    context = candidate.get();
                    return true;
                }
            }
            return false;
        });
    }

    /* The GPU may still be chewing on this context's previous submission. Waiting it out here,
       outside every lock, is what lets the other contexts keep submitting meanwhile. */
    if (context->pendingValue) {
        if (!waitValue(context->pendingValue, errorMessage)) {
            releaseClaim(*context);
            return nullptr;
        }
    }

    VkResult res = dev->vk.vkResetCommandPool(dev->device(), context->commandPool, 0);
    if (res != VK_SUCCESS) {
        errorMessage = "vkResetCommandPool failed (VkResult " + std::to_string(res) + ")";
        releaseClaim(*context);
        return nullptr;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    res = dev->vk.vkBeginCommandBuffer(context->cmd, &beginInfo);
    if (res != VK_SUCCESS) {
        errorMessage = "vkBeginCommandBuffer failed (VkResult " + std::to_string(res) + ")";
        releaseClaim(*context);
        return nullptr;
    }

    return context;
}

bool VSVulkanExecPool::submit(VSVulkanExecContext &context, std::string &errorMessage, uint64_t *signaledValue,
    const VSVulkanWait *waits, uint32_t waitCount) {
    VkResult res = dev->vk.vkEndCommandBuffer(context.cmd);
    if (res != VK_SUCCESS) {
        errorMessage = "vkEndCommandBuffer failed (VkResult " + std::to_string(res) + ")";
        releaseClaim(context);
        return false;
    }

    /* Deduplicated here as well so raw arrays behave the same as a VSVulkanWaitList. */
    VkSemaphoreSubmitInfo waitInfos[VSVulkanWaitList::capacity];
    uint32_t waitInfoCount = 0;
    bool overflow = false;
    for (uint32_t i = 0; i < waitCount; i++) {
        if (!waits[i].semaphore)
            continue;
        uint32_t j = 0;
        for (; j < waitInfoCount; j++) {
            if (waitInfos[j].semaphore == waits[i].semaphore) {
                if (waits[i].value > waitInfos[j].value)
                    waitInfos[j].value = waits[i].value;
                break;
            }
        }
        if (j < waitInfoCount)
            continue;
        if (waitInfoCount == VSVulkanWaitList::capacity) {
            overflow = true;
            break;
        }
        waitInfos[waitInfoCount] = {};
        waitInfos[waitInfoCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfos[waitInfoCount].semaphore = waits[i].semaphore;
        waitInfos[waitInfoCount].value = waits[i].value;
        waitInfos[waitInfoCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfoCount++;
    }
    if (overflow) {
        errorMessage = "A submission cannot depend on more than " + std::to_string(VSVulkanWaitList::capacity) +
            " distinct timelines";
        releaseClaim(context);
        return false;
    }

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = context.cmd;
    VkSemaphoreSubmitInfo signalInfo = {};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = timeline;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    submitInfo.waitSemaphoreInfoCount = waitInfoCount;
    submitInfo.pWaitSemaphoreInfos = waitInfoCount ? waitInfos : nullptr;

    {
        /* Value allocation and submission stay together under the queue lock, since timeline
           signal values must reach the queue in increasing order and the lock is already
           mandatory for the submit itself. A failed submit burns no value. */
        std::lock_guard<VSVulkanQueue> queueLock(*q);
        signalInfo.value = nextValue + 1;
        res = dev->vk.vkQueueSubmit2(q->handle(), 1, &submitInfo, VK_NULL_HANDLE);
        if (res == VK_SUCCESS) {
            nextValue++;
            context.pendingValue = nextValue;
            if (signaledValue)
                *signaledValue = nextValue;
        }
    }

    releaseClaim(context);
    if (res != VK_SUCCESS) {
        errorMessage = "vkQueueSubmit2 failed (VkResult " + std::to_string(res) + ")";
        return false;
    }
    return true;
}

bool VSVulkanExecPool::waitValue(uint64_t value, std::string &errorMessage) {
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &timeline;
    waitInfo.pValues = &value;
    VkResult res = dev->vk.vkWaitSemaphores(dev->device(), &waitInfo, UINT64_MAX);
    if (res != VK_SUCCESS) {
        errorMessage = "vkWaitSemaphores failed (VkResult " + std::to_string(res) + ")";
        return false;
    }
    return true;
}

bool VSVulkanExecPool::waitAll(std::string &errorMessage) {
    uint64_t value;
    {
        std::lock_guard<VSVulkanQueue> queueLock(*q);
        value = nextValue;
    }
    return value == 0 || waitValue(value, errorMessage);
}

void VSVulkanExecPool::releaseClaim(VSVulkanExecContext &context) {
    context.claimed.store(false, std::memory_order_release);
    /* The empty critical section pairs the store with the predicate check in acquire(): without
       it the release could land between a waiter's failed scan and its wait, and the notify
       would hit nobody. */
    { std::lock_guard<std::mutex> lock(claimMutex); }
    claimCv.notify_one();
}
