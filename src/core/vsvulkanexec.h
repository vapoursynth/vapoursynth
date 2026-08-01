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
#include <cassert>
#include <condition_variable>
#include <memory>

struct VSFrame;

class VSVulkanExecPool;

/* One device side dependency: a timeline and the value that must be reached. */
struct VSVulkanWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
};

/* Collects the dependencies of one submission, deduplicating on the fly since the common case
   is several planes produced by the same timeline, which collapse into one wait at the highest
   value. Null semaphores mean host produced content that needs no wait and are simply dropped,
   so callers can append every plane of every frame without caring which are resident where. */
class VSVulkanWaitList {
public:
    static constexpr uint32_t capacity = 16;

    void add(VkSemaphore semaphore, uint64_t value) {
        if (!semaphore)
            return;
        for (uint32_t i = 0; i < count; i++) {
            if (waits[i].semaphore == semaphore) {
                if (value > waits[i].value)
                    waits[i].value = value;
                return;
            }
        }
        /* A submission depends on at most a handful of distinct timelines, one per pool it
           consumes from; running out of room means the design changed and this should too —
           a wait must never be dropped quietly, so the tripwire is loud. */
        assert(count < capacity);
        if (count < capacity)
            waits[count++] = { semaphore, value };
    }

    const VSVulkanWait *data() const { return waits; }
    uint32_t size() const { return count; }

private:
    VSVulkanWait waits[capacity] = {};
    uint32_t count = 0;
};

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
    struct Retained {
        void (*release)(void *object);
        void *object;
    };

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint64_t pendingValue = 0;
    std::atomic<bool> claimed{false};
    std::vector<Retained> retained;
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
       submission later without holding the context. The wait list makes the submission wait on
       other timelines before executing, which is how work consuming frames waits for their
       producers, per plane, without the host ever blocking. */
    bool submit(VSVulkanExecContext &context, std::string &errorMessage, uint64_t *signaledValue = nullptr,
        const VSVulkanWait *waits = nullptr, uint32_t waitCount = 0);

    /* Attaches an object to the context's current recording, called between acquire and
       submit: the release callback runs once the submission this recording becomes is known
       complete, at the context's next acquire or at pool destruction. This is how work that
       reads frames keeps them alive without the host ever waiting: the filter retains its
       sources, submits, returns, and the references drop later. Bounded lag by design, one
       ring cycle at most. */
    void retain(VSVulkanExecContext &context, void (*release)(void *object), void *object);

    /* Gives up on a recording instead of submitting it. Everything retained is released at
       once since nothing will ever execute; the half recorded command buffer is reset by the
       next acquire. */
    void abandon(VSVulkanExecContext &context);

    /* Host waits, always outside every lock. */
    bool waitValue(uint64_t value, std::string &errorMessage);
    bool waitAll(std::string &errorMessage);

    /* The pool's timeline, handed to frames as their producer sync. */
    VkSemaphore semaphore() const { return timeline; }
    VSVulkanQueue *queue() const { return q; }

    /* The public exec pool handle wraps one of these plus the device reference that keeps
       the allocator reachable for a late free, mirroring VSGPUBuffer. */
    friend struct VSGPUExecPool;

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

/* The public VSGPUExecPool from VSVULKANAPI: an exec pool plus the device reference, so a
   pool freed late still has a live device to destroy its objects through. */
struct VSGPUExecPool {
    VSVulkanExecPool pool;
    VSVulkanDevice *device = nullptr;
};

/* The public VSGPUExecContext: a claimed recording plus what the filter declared about it.
   Waits and publications are collected during recording and applied at submit, which is
   what lets a filter state its dependencies in any order while the pool still gets one
   deduplicated wait list and one producer value. */
struct VSGPUExecContext {
    VSGPUExecPool *owner = nullptr;
    VSVulkanExecContext *context = nullptr;
    VSVulkanWaitList waits;
    struct PublishTarget {
        VSFrame *frame;
        int plane;
    };
    std::vector<PublishTarget> publish;
};

#endif
