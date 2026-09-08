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
#include <thread>
#include <vector>

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
   so callers can append every plane of every frame without caring which are resident where.

   Unbounded on purpose: one distinct timeline per upstream filter instance is the rule, and
   the filters that combine clips do not cap how many they take -- StackHorizontal any number,
   Expr 26, AverageFrames 31 -- so no fixed size is safe. Dropping a wait would let a consumer
   read planes whose producing dispatch is still running, with nothing to say so. */
class VSVulkanWaitList {
public:
    /* Producer pairs arrive as counted timelines; the raw handle is what a submission needs and
       is safe to hold here, since whoever assembles a wait list holds the frames supplying it
       for at least as long as the submit call. */
    void add(VSVulkanTimeline *timeline, uint64_t value) {
        if (timeline)
            add(timeline->semaphore(), value);
    }

    void add(VkSemaphore semaphore, uint64_t value) {
        if (!semaphore)
            return;
        for (VSVulkanWait &wait : waits) {
            if (wait.semaphore == semaphore) {
                if (value > wait.value)
                    wait.value = value;
                return;
            }
        }
        waits.push_back({ semaphore, value });
    }

    const VSVulkanWait *data() const { return waits.data(); }
    uint32_t size() const { return static_cast<uint32_t>(waits.size()); }
    void clear() { waits.clear(); }

private:
    std::vector<VSVulkanWait> waits;
};

struct VSVulkanExecRetained {
    VSGPUReleaseFunc release;
    void *object;
};

struct VSGPUExecPool;
class VSVulkanExecContext;

/* The public VSGPUExecContext: a claimed recording plus what the filter declared about it.
   Waits and publications are collected during recording and applied at submit, which is
   what lets a filter state its dependencies in any order while the pool still gets one
   deduplicated wait list and one producer value.

   One lives inside each ring slot rather than being allocated per acquire, so the handle a
   filter holds stays a valid object for the life of the pool: a handle used after the submit
   or abandon that ended it names a slot nobody holds, or one held by someone else, and every
   entry point checks that before touching anything (invariant I22). The two pointers are
   bound once at pool creation; the lists belong to whoever holds the claim. */
struct VSGPUExecContext {
    VSGPUExecPool *owner = nullptr;
    VSVulkanExecContext *context = nullptr;
    VSVulkanWaitList waits;
    struct PublishTarget {
        VSFrame *frame;
        int plane;
    };
    std::vector<PublishTarget> publish;

    void reset() {
        waits.clear();
        publish.clear();
    }
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

    /* The public handle for this slot, bound once by bindHandles; see VSGPUExecContext. */
    VSGPUExecContext handle;

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint64_t pendingValue = 0;
    std::atomic<bool> claimed{false};
    /* The thread that acquired the context, for as long as it holds the claim; a sweep's
       transient claim leaves it empty. What the one-context-per-pool-per-thread rule and the
       "only the acquiring thread may retain, submit or abandon" rule are checked against. */
    std::atomic<std::thread::id> owner{};
    std::vector<VSVulkanExecRetained> retained;
    /* What the retained objects pin, added to the device's in-flight total when the
       recording is submitted rather than as each object is retained: a recording pins its
       memory either way, but only submitted work completes without help from the thread
       holding it, and the admission gate waits on that total -- counting a recording in
       progress would let a thread block on bytes only it can ever release. */
    VkDeviceSize retainedBytes = 0;
    bool retainedCounted = false;
};

/* A fixed set of exec contexts shared by however many threads a filter instance is called on.
   A filter instance may be called on many threads at once, so claiming a context is a lock free
   ring walk, the only real lock is the queue's around vkQueueSubmit2, and every GPU wait happens
   outside both. N contexts means N frames in flight; more threads than contexts wait their turn
   at acquire(), which is the intended backpressure.

   The pool owns one timeline semaphore. Signal values must reach the queue in increasing order,
   so the next value is allocated under the same queue lock as the submission that signals it,
   making the pair atomic without further ordering machinery.

   The device must outlive the pool. Single shot like the rest of the Vulkan layer: init once,
   and a failed init leaves only the destructor to run. */
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

    /* Attaches an object to the context's current recording, called between acquire and submit.
       The release callback runs once that submission is known complete — whichever of the
       pool's next submit (each one sweeps the others), the context's next acquire, a pressure
       sweep or pool destruction looks first, so an active pool lags about one submission and
       only an untouched one waits for a sweep.

       bytes is what the object pins in device memory, counted against the device's in-flight
       retention budget from submit until release; pass 0 for objects that should not gate. */
    void retain(VSVulkanExecContext &context, VSGPUReleaseFunc release, void *object, VkDeviceSize bytes = 0);

    /* Gives up on a recording instead of submitting it. Everything retained is released at
       once since nothing will ever execute; the half recorded command buffer is reset by the
       next acquire. */
    void abandon(VSVulkanExecContext &context);

    /* Host waits, always outside every lock. waitAll also reaps what the waited submissions
       retained, which is the public gpuExecPoolWaitIdle contract; recordings other threads
       hold at the time are not submissions yet and are left alone. */
    bool waitValue(uint64_t value, std::string &errorMessage);
    bool waitAll(std::string &errorMessage);
    /* The timeline's counter right now, for deciding whether a submission is done without
       waiting on it. False when the driver refuses the query, which callers treat as "not
       done". */
    bool completedValue(uint64_t &value) const;
    /* The highest value this pool ever handed to the queue, read without the queue lock. No
       value a caller can legitimately wait for exceeds it, since it is published before the
       submit that signals it, which makes it the bound the public wait rejects wild values
       against; see queuedCeiling for why it is not nextValue. */
    uint64_t submittedCeiling() const { return queuedCeiling.load(std::memory_order_acquire); }

    /* Releases every retained object whose submission has completed, without waiting. Called
       from submit, so an active pool reaps itself with about one submission of lag; an idle
       one would otherwise park its last contextCount submissions' sources and scratch
       indefinitely — gigabytes in a deep graph of heavy filters — so the device also calls
       this across all registered pools from the memory pressure paths. Safe from any thread:
       a context is only touched once its claim is won, so live recordings are skipped. */
    void sweepCompleted();
    /* The two halves of sweepCompleted, for a caller that must not run release callbacks
       where it stands: detachCompleted settles the completed retentions' bytes and moves
       them into out, runReleases invokes them. The device's registry sweep detaches under
       its lock and releases after dropping it, since a release callback may create a pool
       or acquire from another one, both of which come back to that lock. Whoever runs a
       batch registers it with the device for the duration, so the waits that promise
       "everything is released" can see it. */
    void detachCompleted(std::vector<VSVulkanExecRetained> &out);
    static void runReleases(std::vector<VSVulkanExecRetained> &detached);

    /* The pool's timeline, handed to frames as their producer sync. The pool holds one
       reference; frames published from it hold their own, so the semaphore survives the pool
       whenever a frame it produced does. */
    VSVulkanTimeline *timelineObject() const { return timeline; }
    VSVulkanQueue *queue() const { return q; }

    /* Points every slot's public handle at its slot and at the public pool, once, before any
       thread can see a handle. */
    void bindHandles(VSGPUExecPool *owner);
    /* Whether the calling thread holds one of this pool's contexts. Reads only the claim and
       its owner, both atomic, so the device can ask every registered pool while holding the
       registry lock. */
    bool holdsContextOwnedByThisThread() const;
    /* Fatal unless the calling thread is the one holding the context's claim. The public
       entry points check their handle through this before touching anything, which is what
       makes a handle used after its submit or abandon a named error rather than a read of
       freed memory (invariant I22). */
    void failUnlessOwner(const VSVulkanExecContext &context, const char *what) const;

    /* The public exec pool handle wraps one of these plus the device reference that keeps
       the allocator reachable for a late free, mirroring VSGPUBuffer. */
    friend struct VSGPUExecPool;

private:
    void releaseClaim(VSVulkanExecContext &context);
    void settleRetained(VSVulkanExecContext &context);
    void releaseRetained(VSVulkanExecContext &context);
    /* releaseRetained without the batch registration, for a caller that registered earlier. */
    void releaseRetainedNow(VSVulkanExecContext &context);
    /* Fatal when the calling thread holds a claim on any context of this pool. */
    void failIfHoldingContext(const char *what) const;
    /* Fatal when any thread holds a claim on any context of this pool. */
    void failIfAnyContextHeld(const char *what) const;

    VSVulkanDevice *dev = nullptr;
    VSVulkanQueue *q = nullptr;
    VSVulkanTimeline *timeline = nullptr;
    uint64_t nextValue = 0; /* guarded by the queue lock */
    /* The highest value ever handed to the queue. Its only reader is the sanity check in
       detachCompleted, which needs a ceiling it can trust without taking the queue lock:
       reading nextValue there was the core's one execPoolsMutex -> queue lock edge, and the
       queue lock is public through lockVulkanQueue, so a filter holding it across any call
       that reaches the exec registry -- an allocation, an acquire, creating or freeing a pool
       -- closed a cycle against an ordinary cache sweep on a worker thread.

       Published before the submission that signals it, which is the whole point of keeping it
       apart from nextValue: nextValue is raised after vkQueueSubmit2 returns, so the GPU can
       have signalled the value while nextValue still names the one before it, and a lock free
       reader would then see a counter past its ceiling and call a correct program fatal. A
       failed submit leaves this one high, which only makes that check more permissive, and it
       decides no wait -- waitAll still reads nextValue under the queue lock, where a value
       that will never be signalled would hang. */
    std::atomic<uint64_t> queuedCeiling{0};
    /* Compute queue pools additionally signal the device's progress timeline on every
       submission, which is what the admission gate sleeps on. A pool on another queue cannot
       wake it, so what it retains is kept alive and released as usual but never counted:
       every metered byte belongs to a submission whose completion can wake the gate. */
    bool signalsProgress = false;
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

#endif
