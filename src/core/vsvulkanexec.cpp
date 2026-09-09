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
#include "lockorder.h"

VSVulkanExecPool::~VSVulkanExecPool() {
    if (!dev)
        return;
    /* Off the device's sweep list before anything is torn down; unregistration blocks while
       a sweep is walking the pools or still running releases it detached from this one, so
       after it returns no sweep can be touching this pool and every release has run. */
    dev->unregisterExecPool(this);
    /* With the pool off the registry a claim can only belong to a thread still using it --
       recording, or inside acquire -- and nothing below could survive that: the retention
       lists would be released under the holder and the command pools destroyed under its
       recording. Waiting for the claim would not help either, since submit keeps touching
       the pool after it drops the claim. The core never destroys a node while a frame
       request on it runs, so this only ever names a bug. */
    failIfAnyContextHeld("freeGPUExecPool");
    /* Everything below retires what the GPU may still be using, so it may only run once
       completion is established. waitAll fails for two reasons and they differ here: after a
       device reset nothing is executing and all of it may go, while an allocation failure
       inside the wait establishes nothing -- running a release callback there hands a region
       back to the allocator under a live read, and destroying a command pool with a pending
       buffer is invalid usage. In that case retire nothing: the retention callbacks never run
       and the command pools are never destroyed, which leaks both outright rather than
       deferring them -- nothing downstream picks them up, and that is the point, since the
       submission they belong to may still be reading and writing. The bytes are still settled,
       or an admission gate that outlives this pool would wait forever on work nobody will
       reap. */
    std::string ignored;
    const bool completed = waitAll(ignored) || dev->deviceLost();
    for (auto &context : contexts) {
        if (!completed) {
            settleRetained(*context);
            continue;
        }
        releaseRetained(*context);
        if (context->commandPool)
            dev->vk.vkDestroyCommandPool(dev->device(), context->commandPool, nullptr);
    }
    /* Just the pool's own reference. Frames this pool produced hold theirs, so the semaphore
       outlives the pool exactly when something still needs to wait on it -- but only a frame
       does that, and a submission signalling this timeline need not have produced one: a
       scratch or readback only recording leaves nothing else holding a reference, so releasing
       here would destroy the semaphore out from under its own pending signal operation. On the
       give-up path it is leaked with everything else, which also keeps the device alive, a
       timeline holding a device reference -- exactly what that pending submission still
       needs. */
    if (timeline && completed)
        timeline->release();
}

void VSVulkanExecPool::failUnlessOwner(const VSVulkanExecContext &context, const char *what) const {
    const std::thread::id holder = context.owner.load(std::memory_order_acquire);
    if (holder == std::this_thread::get_id())
        return;
    /* An empty owner is a handle whose recording already ended: the slot is idle, pending on
       the GPU, or transiently claimed by a reaper, none of which this caller holds.

       What this cannot catch is a handle kept across the same thread's next acquire of the
       same slot: the handle lives in the slot, so the stale pointer and the fresh one are one
       object with one owner, and no field added here could differ between them. It would take
       a token the caller carries, which the ABI does not give it. Documented on
       gpuExecAcquire rather than defended; see I22. */
    std::string message = std::string(what) + (holder == std::thread::id()
        ? " called with a context handle whose recording was already ended by gpuExecSubmit or gpuExecAbandon"
        : " called by a thread that did not acquire the context");
    vulkanFatal(message.c_str());
}

void VSVulkanExecPool::bindHandles(VSGPUExecPool *owner) {
    for (auto &context : contexts) {
        context->handle.owner = owner;
        context->handle.context = context.get();
    }
}

void VSVulkanExecPool::failIfAnyContextHeld(const char *what) const {
    for (const auto &context : contexts) {
        if (context->claimed.load(std::memory_order_acquire)) {
            std::string message = std::string(what) + " called while a context of the pool is still held";
            vulkanFatal(message.c_str());
        }
    }
}

bool VSVulkanExecPool::holdsContextOwnedByThisThread() const {
    const std::thread::id me = std::this_thread::get_id();
    for (const auto &context : contexts) {
        if (context->claimed.load(std::memory_order_relaxed) && context->owner.load(std::memory_order_acquire) == me)
            return true;
    }
    return false;
}

void VSVulkanExecPool::failIfHoldingContext(const char *what) const {
    if (holdsContextOwnedByThisThread()) {
        std::string message = std::string(what) + " called by a thread already holding a context of the pool";
        vulkanFatal(message.c_str());
    }
}

void VSVulkanExecPool::retain(VSVulkanExecContext &context, VSGPUReleaseFunc release, void *object, VkDeviceSize bytes) {
    failUnlessOwner(context, "gpuExecRetain");
    context.retained.push_back({ release, object });
    /* Only a pool whose submissions signal the progress timeline meters its bytes: a gated
       thread sleeps on that timeline, and bytes only a poll could discover would turn every
       wait for them into the poll's full interval. */
    if (signalsProgress)
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
    /* Registered with the device while it runs, so a waitAll on another thread waits for
       these releases exactly as it does for a device sweep's. The caller holds the claim,
       which is what makes the list safe to look at here. */
    if (context.retained.empty()) {
        settleRetained(context);
        return;
    }
    dev->beginExecReleases(this);
    releaseRetainedNow(context);
    dev->endExecReleases(this);
}

void VSVulkanExecPool::releaseRetainedNow(VSVulkanExecContext &context) {
    settleRetained(context);
    /* Moved out first, so a release that reaches back into the pool never meets a list in
       the middle of being walked. */
    std::vector<VSVulkanExecRetained> retained = std::move(context.retained);
    context.retained.clear();
    runReleases(retained);
}

void VSVulkanExecPool::abandon(VSVulkanExecContext &context) {
    failUnlessOwner(context, "gpuExecAbandon");
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
    timeline = VSVulkanTimeline::create(*dev, errorMessage, true);
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
    /* Detaching and registering the batch happen together under the device's registry lock.
       Doing them in two steps left a window in which the retentions had left their contexts
       and no batch named them yet, so another thread's waitAll -- which sweeps, finds nothing,
       and then waits for the batches in flight -- could return promising that everything was
       released while this thread still held them. Nothing is registered when there was nothing
       to take, so an idle pool's sweep costs one lock and no bookkeeping. */
    std::vector<VSVulkanExecRetained> detached;
    if (!dev->detachExecReleases(this, detached))
        return;
    runReleases(detached);
    dev->endExecReleases(this);
}

void VSVulkanExecPool::detachCompleted(std::vector<VSVulkanExecRetained> &out) {
    /* Nothing left to reap once the device is gone, and the counter below would only say so
       again. What this pool still holds goes back in the destructor. */
    if (dev->deviceLost())
        return;
    uint64_t counter = 0;
    VkResult counterRes = dev->vk.vkGetSemaphoreCounterValue(dev->device(), timeline->semaphore(), &counter);
    if (counterRes != VK_SUCCESS) {
        if (counterRes == VK_ERROR_DEVICE_LOST)
            dev->markDeviceLost();
        return;
    }
    /* A reset before a violation: a driver that force-signals its timelines to release the
       waiters lands exactly on resetTimelineValue, which no pool can reach by submitting, so
       the two are told apart by the value rather than confused. This used to be the fatal
       below, which turned any GPU hang on the machine into a terminated process. */
    if (counter >= VSVulkanDevice::resetTimelineValue) {
        dev->markDeviceLost();
        return;
    }
    /* The timeline advances only through this pool's own submissions, so a counter past
       everything the pool ever handed to the queue can only mean something else signalled the
       semaphore -- which would make every pending submission look complete below and hand the
       GPU's inputs back while it still reads them. The ceiling is read after the counter and
       never lowered, so a submission racing in between can only raise it: no false positives.

       Deliberately queuedCeiling and not nextValue under the queue lock. This runs under
       execPoolsMutex, and the queue lock is one a plugin can hold (lockVulkanQueue), so that
       one edge was enough to deadlock a filter holding the queue lock across a core call
       against a worker thread sweeping from notifyCaches. */
    if (counter > queuedCeiling.load(std::memory_order_acquire))
        vulkanFatal("the exec pool's timeline was signalled from outside the pool: its counter is past the last value the pool submitted");
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
    for (const auto &r : detached) {
        VS_LOCK_BOUNDARY("a release callback");
        r.release(r.object);
    }
    detached.clear();
}

VSVulkanExecContext *VSVulkanExecPool::acquire(std::string &errorMessage) {
    /* Two contract checks before anything else. A release callback may only free, so a
       thread running a batch of releases has no business here; and one context per pool per
       thread, since a second acquire from the same thread could only wait for a slot that,
       once every worker does the same, nobody ever releases. Both used to be hangs; now they
       name the caller and stop. */
    dev->failIfRunningReleases("gpuExecAcquire");
    failIfHoldingContext("gpuExecAcquire");
    /* Before the gate, which a reset device would otherwise spin in: its progress counter is
       force-signalled too, and counter + 1 wraps to a value already reached. */
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        return nullptr;
    }
    const std::thread::id me = std::this_thread::get_id();

    /* Admission: before this thread claims anything, the device may hold it back while the
       bytes pinned by submitted work exceed the in-flight budget. Only submitted work
       counts, so a recording this thread already holds -- on this pool or another -- never
       gates it, and the queue drains without its help either way: every device side wait
       in it names a producer that was already submitted when its consumer recorded. */
    dev->execAdmissionGate();
    /* And after it. The gate is where a parked thread hears the loss -- its own wait, or a
       latch by another thread while it slept -- and it leaves on that rather than claiming a
       slot on a device the core has written off. Without this the thread claimed and its
       submit failed instead: the same outcome one call later, but acquire is the call the
       protocol says fails, and L21 measured four contexts handed out this way. */
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        return nullptr;
    }

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
       backpressure when a filter is called on more threads than it keeps frames in flight.

       Waiting is also the only place the pools can deadlock among themselves: a thread that
       holds another pool's context and waits here, while a thread holding one of this pool's
       waits on that one, leaves two full rings neither of which can free a slot. One context
       per pool per thread was already fatal for the same reason within a ring; this is the
       same rule across rings, and nothing needs the freedom it removes, since a dependency
       between two pools is a timeline value rather than two open recordings. The fast path
       above skips the check: it never waits, and walking the registry costs a lock. */
    if (!context) {
        dev->failIfHoldingForeignContext(this, "gpuExecAcquire");
        std::unique_lock<std::mutex> lock(claimMutex);
        VS_LOCK_HELD(vsLockClaim);
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

    context->owner.store(me, std::memory_order_release);

    /* The GPU may still be chewing on this context's previous submission. Waiting it out here,
       outside every lock, is what lets the other contexts keep submitting meanwhile.

       What that submission retained is registered as a batch before the wait, not after it,
       so a waitAll elsewhere, or an allocation that failed at the driver, can wait for it: the
       GPU finishing while the acquirer is still waking is exactly the window in which that
       memory would otherwise be unreachable by anyone. Note that this is a few instructions
       later than the claim, which hid the context from every sweep the moment it was won, so a
       waitAll landing in between sees neither and reads the pool as clean. Nothing closes that
       in code; the pool ownership and setup-only rules on gpuExecPoolWaitIdle mean no wait can
       be there to see it. A never submitted context retains nothing: abandon and a failed
       submit release at once. */
    if (context->pendingValue) {
        const bool holds = !context->retained.empty();
        if (holds)
            dev->beginExecReleases(this);
        const bool done = waitValue(context->pendingValue, errorMessage);
        if (done)
            releaseRetainedNow(*context);
        if (holds)
            dev->endExecReleases(this);
        if (!done) {
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
    failUnlessOwner(context, "gpuExecSubmit");
    /* A reset device accepts submissions that never execute and whose timeline already reads
       as complete, so the frame would come out silently wrong; fail the call instead. The
       recording ends and the retentions go back exactly as they do for any failed submit. */
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        dev->vk.vkEndCommandBuffer(context.cmd);
        releaseRetained(context);
        releaseClaim(context);
        return false;
    }
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
           mandatory for the submit itself. A failed submit burns no value on either timeline,
           leaving only the lock free ceiling below one high; a later success skipping past a
           burned progress value is fine, gaps are legal on timelines and the gate only ever
           waits for counter + 1. */
        std::lock_guard<VSVulkanQueue> queueLock(*q);
        VS_LOCK_HELD(vsLockQueue);
        signalInfos[0].value = nextValue + 1;
        if (signalsProgress)
            signalInfos[1].value = dev->execProgressNext + 1;
        /* Before the submit rather than with nextValue after it, so a sweep reading a counter
           that already includes this submission cannot see a ceiling behind it; see
           queuedCeiling. */
        queuedCeiling.store(nextValue + 1, std::memory_order_release);
        res = dev->vk.vkQueueSubmit2(q->handle(), 1, &submitInfo, VK_NULL_HANDLE);
        if (res == VK_ERROR_DEVICE_LOST)
            dev->markDeviceLost();
        if (res == VK_SUCCESS) {
            nextValue++;
            context.pendingValue = nextValue;
            /* Recorded before the caller can learn the value, so a producer pair built from
               it always passes the bound setPlaneProducer checks. */
            timeline->noteSubmitted(nextValue);
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
    /* A reset device answers every wait at once, having force-signalled the timeline, so
       without this the caller would read "the work is done" off memory nothing wrote. */
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        return false;
    }
    VkSemaphore semaphore = timeline->semaphore();
    /* Through the device's wait policy, which retries an allocation failure and recognises the
       conforming half of a device loss -- a driver that does report it reports it here, and
       the rest of the core then learns it the same way as from the force signal. */
    if (!dev->waitTimelines(&semaphore, &value, 1)) {
        errorMessage = dev->deviceLost() ? VSVulkanDevice::deviceLostMessage()
            : "waiting for a GPU submission failed and could not be retried";
        return false;
    }
    return true;
}

bool VSVulkanExecPool::completedValue(uint64_t &value) const {
    return dev->vk.vkGetSemaphoreCounterValue(dev->device(), timeline->semaphore(), &value) == VK_SUCCESS;
}

bool VSVulkanExecPool::waitAll(std::string &errorMessage) {
    /* A caller holding a context of this pool would leave that recording outside the promise
       made below and, once every worker did the same, deadlock the ring; fatal instead. The
       destructor reaches this too, which makes destroying a pool with one's own context
       claimed the same error. */
    failIfHoldingContext("gpuExecPoolWaitIdle or freeGPUExecPool");
    /* Checked here rather than only in the rendezvous below, which a pool that never
       submitted skips: a release callback may not wait on any pool, submitted or not. */
    dev->failIfRunningReleases("gpuExecPoolWaitIdle");
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        return false;
    }
    uint64_t value;
    {
        std::lock_guard<VSVulkanQueue> queueLock(*q);
        VS_LOCK_HELD(vsLockQueue);
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
    /* The sweep may be what discovered the reset, and the promise this call makes -- every
       submission complete, everything released -- is not one a reset device kept. */
    if (dev->deviceLost()) {
        errorMessage = VSVulkanDevice::deviceLostMessage();
        return false;
    }
    return true;
}

void VSVulkanExecPool::releaseClaim(VSVulkanExecContext &context) {
    context.owner.store(std::thread::id(), std::memory_order_relaxed);
    context.claimed.store(false, std::memory_order_release);
    /* The empty critical section pairs the store with the predicate check in acquire(): without
       it the release could land between a waiter's failed scan and its wait, and the notify
       would hit nobody. */
    { std::lock_guard<std::mutex> lock(claimMutex); }
    VS_LOCK_HELD(vsLockClaim);
    claimCv.notify_one();
}
