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

#ifndef VSVULKANEXEC_H
#define VSVULKANEXEC_H

#include "vsvulkan.h"

#include <atomic>
#include <condition_variable>
#include <memory>

class VSVulkanExecPool;

/* One recording and submission slot: a command pool holding a single primary command buffer,
   plus the timeline value its last submission signals. A context belongs to exactly one thread
   from acquire() until submit(); after that the claim is gone but the GPU may still be working,
   which is what the pending value tracks for the next acquirer. */
class VSVulkanExecContext {
    friend class VSVulkanExecPool;

public:
    VSVulkanExecContext() = default;
    VSVulkanExecContext(const VSVulkanExecContext &) = delete;
    VSVulkanExecContext &operator=(const VSVulkanExecContext &) = delete;

    /* Valid for recording between acquire() and submit(). */
    VkCommandBuffer commandBuffer() const { return cmd; }

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint64_t pendingValue = 0;
    std::atomic<bool> claimed{false};
};

/* A fixed set of exec contexts shared by however many threads a filter instance is called on.
   This is the piece the fmParallel decision shapes: filters get concurrent getFrame calls, so
   claiming a context is a lock free ring walk, the only real lock taken is the queue's around
   vkQueueSubmit2, and every wait for the GPU happens outside all of them. With N contexts a
   filter keeps N frames in flight; more threads than contexts simply wait their turn at
   acquire(), which is the intended backpressure.

   The pool owns one timeline semaphore. Timeline signal values must reach the queue in
   increasing order, so the next value is allocated under the same queue lock as the submission
   that signals it, making the pair atomic without any additional ordering machinery.

   The device must outlive the pool. Like the rest of the Vulkan layer it is single shot: init
   once, and a failed init leaves only the destructor to run. */
class VSVulkanExecPool {
public:
    VSVulkanExecPool() = default;
    ~VSVulkanExecPool();
    VSVulkanExecPool(const VSVulkanExecPool &) = delete;
    VSVulkanExecPool &operator=(const VSVulkanExecPool &) = delete;

    bool init(VSVulkanDevice &device, VSVulkanQueue &queue, uint32_t contextCount, std::string &errorMessage);

    /* Claims a context, waits out whatever the GPU still owes it, and hands it back reset and
       recording. Returns null with the error set on device loss and the like. */
    VSVulkanExecContext *acquire(std::string &errorMessage);

    /* Ends recording and submits, signaling the next timeline value, then releases the claim.
       The signaled value is optionally returned so the caller can wait for this exact
       submission later without holding the context. */
    bool submit(VSVulkanExecContext &context, std::string &errorMessage, uint64_t *signaledValue = nullptr);

    /* Host waits, always outside every lock. */
    bool waitValue(uint64_t value, std::string &errorMessage);
    bool waitAll(std::string &errorMessage);

    VSVulkanQueue *queue() const { return q; }

private:
    void releaseClaim(VSVulkanExecContext &context);

    VSVulkanDevice *dev = nullptr;
    VSVulkanQueue *q = nullptr;
    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t nextValue = 0; /* guarded by the queue lock */
    std::vector<std::unique_ptr<VSVulkanExecContext>> contexts;
    std::atomic<uint32_t> cursor{0};
    std::mutex claimMutex;
    std::condition_variable claimCv;
};

#endif
