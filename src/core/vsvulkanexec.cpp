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
    /* Off the device's sweep list before anything is torn down; unregistration blocks while
       a sweep is walking the pools or still running releases it detached from this one, so
       after it returns no sweep can be touching this pool and every release has run. */
    dev->unregisterExecPool(this);
    std::string ignored;
    waitAll(ignored);
    for (auto &context : contexts) {
        releaseRetained(*context);
        if (context->commandPool)
            dev->vk.vkDestroyCommandPool(dev->device(), context->commandPool, nullptr);
    }
    /* Just the pool's own reference. Frames this pool produced hold theirs, so the semaphore
       outlives the pool exactly when something still needs to wait on it. */
    if (timeline)
        timeline->release();
}

void VSVulkanExecPool::retain(VSVulkanExecContext &context, VSGPUReleaseFunc release, void *object, VkDeviceSize bytes) {
    context.retained.push_back({ release, object });
    context.retainedBytes += bytes;
}

/* The bytes leave the device total before any release runs, not after: a release callback
   may acquire from another pool, and that pool's admission gate would otherwise wait on the
   very bytes the callback is about to free -- with nothing else left to release, forever.
   The price is a thread admitted a moment before a region is back on the free list, which
   the allocator's failure path covers by sweeping and retrying. Only a submitted recording's
   bytes ever reached the total, so an abandoned or failed one has nothing to give back. */
void VSVulkanExecPool::settleRetained(VSVulkanExecContext &context) {
    if (context.retainedCounted)
        dev->subExecRetained(context.retainedBytes);
    context.retainedBytes = 0;
    context.retainedCounted = false;
}

void VSVulkanExecPool::releaseRetained(VSVulkanExecContext &context) {
    settleRetained(context);
    /* Moved out first, so a release that reaches back into the pool never meets a list in
       the middle of being walked; registered with the device while it runs, so a waitAll
       on another thread waits for these releases exactly as it does for a device sweep's. */
    std::vector<VSVulkanExecRetained> retained = std::move(context.retained);
    context.retained.clear();
    if (retained.empty())
        return;
    dev->beginExecReleases(this);
    runReleases(retained);
    dev->endExecReleases(this);
}

void VSVulkanExecPool::abandon(VSVulkanExecContext &context) {
    releaseRetained(context);
    releaseClaim(context);
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

    /* This timeline becomes the producer pair of every frame the pool writes, so it is counted
       and may well outlive the pool; see VSVulkanTimeline. */
    timeline = VSVulkanTimeline::create(*dev, errorMessage);
    if (!timeline)
        return false;

    VkResult res;
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

    /* Compute queue pools drive the device's progress timeline; failing to bring it up only
       degrades the admission gate's sleep to its timeout, so it is not an init failure. */
    signalsProgress = (q == &device.computeQueue()) && device.ensureExecProgressSemaphore();

    dev->registerExecPool(this);
    return true;
}

void VSVulkanExecPool::sweepCompleted() {
    std::vector<VSVulkanExecRetained> detached;
    detachCompleted(detached);
    if (detached.empty())
        return;
    dev->beginExecReleases(this);
    runReleases(detached);
    dev->endExecReleases(this);
}

void VSVulkanExecPool::detachCompleted(std::vector<VSVulkanExecRetained> &out) {
    uint64_t counter = 0;
    if (dev->vk.vkGetSemaphoreCounterValue(dev->device(), timeline->semaphore(), &counter) != VK_SUCCESS)
        return;
    for (auto &context : contexts) {
        /* The claim is the only thing that may be looked at unclaimed. Skipping on an
           empty retention list would be cheaper, but retain() pushes onto that vector
           from the thread holding the context between acquire and submit, so reading it
           before the claim is won is a data race; moving an empty list is a no-op anyway,
           so the claim costs a CAS and nothing else. */
        if (context->claimed.load(std::memory_order_relaxed))
            continue;
        bool expected = false;
        if (!context->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
            continue;
        /* Everything retained belongs to the context's last submission, so one value check
           covers the lot; an unsubmitted context can hold nothing here since abandon and
           acquire both clear before the claim drops. */
        if (context->pendingValue && context->pendingValue <= counter) {
            settleRetained(*context);
            out.insert(out.end(), context->retained.begin(), context->retained.end());
            context->retained.clear();
        }
        releaseClaim(*context);
    }
}

void VSVulkanExecPool::runReleases(std::vector<VSVulkanExecRetained> &detached) {
    for (const auto &r : detached)
        r.release(r.object);
    detached.clear();
}

VSVulkanExecContext *VSVulkanExecPool::acquire(std::string &errorMessage) {
    /* Admission: before this thread claims anything, the device may hold it back while the
       bytes pinned by submitted work exceed the in-flight budget. Only submitted work
       counts, so a recording this thread already holds -- on this pool or another -- never
       gates it, and the queue drains without its help either way: every device side wait
       in it names a producer that was already submitted when its consumer recorded. */
    dev->execAdmissionGate();

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

    /* The previous submission is done, so everything it kept alive can go now. */
    releaseRetained(*context);

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
        releaseRetained(context);
        releaseClaim(context);
        return false;
    }

    /* Deduplicated here as well so raw arrays behave the same as a VSVulkanWaitList, and
       unbounded for the same reason it is: dropping a wait is a race, not a diagnostic. */
    std::vector<VkSemaphoreSubmitInfo> waitInfos;
    waitInfos.reserve(waitCount);
    for (uint32_t i = 0; i < waitCount; i++) {
        if (!waits[i].semaphore)
            continue;
        bool merged = false;
        for (VkSemaphoreSubmitInfo &existing : waitInfos) {
            if (existing.semaphore == waits[i].semaphore) {
                if (waits[i].value > existing.value)
                    existing.value = waits[i].value;
                merged = true;
                break;
            }
        }
        if (merged)
            continue;
        VkSemaphoreSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        info.semaphore = waits[i].semaphore;
        info.value = waits[i].value;
        info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(info);
    }
    const uint32_t waitInfoCount = static_cast<uint32_t>(waitInfos.size());

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = context.cmd;
    /* The pool's own timeline, plus the device's progress timeline on the compute queue —
       the admission gate sleeps on the latter, so every completion can wake it. */
    VkSemaphoreSubmitInfo signalInfos[2] = {};
    signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfos[0].semaphore = timeline->semaphore();
    signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfos[1].semaphore = dev->execProgressSemaphore();
    signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = signalsProgress ? 2 : 1;
    submitInfo.pSignalSemaphoreInfos = signalInfos;
    submitInfo.waitSemaphoreInfoCount = waitInfoCount;
    submitInfo.pWaitSemaphoreInfos = waitInfoCount ? waitInfos.data() : nullptr;

    {
        /* Value allocation and submission stay together under the queue lock, since timeline
           signal values must reach the queue in increasing order and the lock is already
           mandatory for the submit itself. A failed submit burns no value on either
           timeline; a later success skipping past a burned progress value is fine, gaps are
           legal on timelines and the gate only ever waits for counter + 1. */
        std::lock_guard<VSVulkanQueue> queueLock(*q);
        signalInfos[0].value = nextValue + 1;
        if (signalsProgress)
            signalInfos[1].value = dev->execProgressNext + 1;
        res = dev->vk.vkQueueSubmit2(q->handle(), 1, &submitInfo, VK_NULL_HANDLE);
        if (res == VK_SUCCESS) {
            nextValue++;
            context.pendingValue = nextValue;
            if (signalsProgress)
                dev->execProgressNext++;
            if (signaledValue)
                *signaledValue = nextValue;
        }
    }

    /* The retention enters the in-flight total now that it is really in flight; the claim
       is still held, so no sweep can release it before it is counted. A failed submission
       executes nothing, so nothing this recording pinned is in flight: released here rather
       than left to a sweep, since a context whose first submission failed keeps pendingValue
       at 0, which the sweeps skip, and its retentions would stay parked until the pool dies. */
    if (res == VK_SUCCESS) {
        dev->addExecRetained(context.retainedBytes);
        context.retainedCounted = true;
    } else {
        releaseRetained(context);
    }

    releaseClaim(context);
    /* Every submission also reaps what the pool's other contexts finished in the meantime,
       so an active pool's parked footprint is what is genuinely in flight, not a whole ring
       cycle of it per context. One counter read and a CAS walk against the ~0.2 ms the
       submission itself costs; idle pools are the pressure sweeps' job. */
    sweepCompleted();
    if (res != VK_SUCCESS) {
        errorMessage = "vkQueueSubmit2 failed (VkResult " + std::to_string(res) + ")";
        return false;
    }
    return true;
}

bool VSVulkanExecPool::waitValue(uint64_t value, std::string &errorMessage) {
    VkSemaphore semaphore = timeline->semaphore();
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
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
    if (value == 0)
        return true;
    if (!waitValue(value, errorMessage))
        return false;
    /* Waited is not yet released: without this the setup upload a filter waits for at
       create would park its staging buffer until the pool's first frame submit, and an
       idle pool would keep everything its last submissions read until a pressure sweep.
       Another thread may have got to some of it first -- a device sweep, another waitAll,
       an acquire or submit reaping what it found -- and still be running those releases,
       so that is waited for too before the promise that everything is released holds. */
    sweepCompleted();
    dev->waitExecReleases(this);
    return true;
}

void VSVulkanExecPool::releaseClaim(VSVulkanExecContext &context) {
    context.claimed.store(false, std::memory_order_release);
    /* The empty critical section pairs the store with the predicate check in acquire(): without
       it the release could land between a waiter's failed scan and its wait, and the notify
       would hit nobody. */
    { std::lock_guard<std::mutex> lock(claimMutex); }
    claimCv.notify_one();
}
