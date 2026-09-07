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

#ifndef VSVULKANFRAME_H
#define VSVULKANFRAME_H

#include "vsvulkanexec.h"
#include "VapourSynth4.h"

#include <chrono>

/* One GPU resident plane: a linear pitched device local buffer, laid out exactly like the CPU
   plane it mirrors so a matching stride uploads as a single flat copy.

   The producer pair lives here per plane rather than per frame, because plane sharing means one
   frame's planes can have different producers. Whoever writes the plane stores the timeline and
   value its submission signals; consumers wait on it device side before reading. A null
   semaphore means host produced content, ready as soon as it is handed over. */
struct VSVulkanPlane {
    VSVulkanBuffer buffer;
    ptrdiff_t stride = 0; /* bytes per row, aligned like a CPU plane would be */
    uint32_t width = 0;   /* in samples */
    uint32_t height = 0;
    /* Counted, so the plane keeps its producer's timeline alive for exactly as long as the pair
       remains something a consumer might wait on. Null means host produced. Always go through
       setPlaneProducer rather than assigning the two fields, which is what keeps the count and
       the pair in step. */
    VSVulkanTimeline *readyTimeline = nullptr;
    uint64_t readyValue = 0;

    VSVulkanPlane() = default;
    ~VSVulkanPlane() {
        if (readyTimeline)
            readyTimeline->release();
    }
    /* Copying would need a second reference and there is no reason to: planes are shared by
       counting the VSPlaneData that owns them, never by duplicating this. Which also means
       `plane = {}` no longer compiles, so clearing goes through reset(). */
    VSVulkanPlane(const VSVulkanPlane &) = delete;
    VSVulkanPlane &operator=(const VSVulkanPlane &) = delete;

    void reset() {
        if (readyTimeline)
            readyTimeline->release();
        readyTimeline = nullptr;
        readyValue = 0;
        buffer = {};
        stride = 0;
        width = 0;
        height = 0;
    }
};

/* addRef before release so republishing the same timeline onto a plane -- the ordinary case for
   a filter writing a plane twice -- cannot drop the last reference in between. */
inline void setPlaneProducer(VSVulkanPlane &plane, VSVulkanTimeline *timeline, uint64_t value) {
    /* A pair on a pool's timeline past what the pool submitted would be waited for by every
       consumer, by waitGPUFrame and by the plane's own destruction, and never reached; the
       header promises it is fatal instead (invariant I23). The pool records each value under
       its queue lock before anyone can learn it, so a value obtained legitimately -- the
       signaledValue of a submit, or a plane's published pair -- always passes. Timelines a
       filter signals itself carry no bound. */
    if (timeline && timeline->isPoolOwned() && value > timeline->lastSubmitted())
        vulkanFatal("setGPUPlaneProducer published a value on an exec pool's timeline that the pool has not submitted");
    if (timeline)
        timeline->addRef();
    if (plane.readyTimeline)
        plane.readyTimeline->release();
    plane.readyTimeline = timeline;
    plane.readyValue = value;
}

/* One linear pitched device local plane with the stride the caller decided on, which is how
   VSFrame keeps its GPU strides identical to its CPU ones. */
bool createGPUPlane(VSVulkanDevice &device, uint32_t width, uint32_t height, int bytesPerSample,
    ptrdiff_t stride, VSVulkanPlane &plane, std::string &errorMessage);

/* Host wait for one plane's producer; the common case is already signaled and returns at once. */
inline bool waitPlaneHost(VSVulkanDevice &device, const VSVulkanPlane &plane) {
    if (!plane.readyTimeline)
        return true;
    VkSemaphore semaphore = plane.readyTimeline->semaphore();
    return device.waitTimelines(&semaphore, &plane.readyValue, 1);
}

/* Moves frames across the PCIe bus. Uploads memcpy straight into the plane buffer when it
   landed in host visible device local memory (resizable BAR), otherwise through a pipelined
   staging ring on the DMA queue. Downloads mirror that but gate on host CACHED rather than
   coherent, since reading a discrete card's write combined memory over PCIe is orders of
   magnitude too slow; unified memory hands back cached plane memory that reads at memcpy
   speed. A frame's plane copies all travel in one submission, the ~0.2 ms submission floor
   dwarfing the per plane cost at common sizes.

   Slot buffers are created lazily and sized to the last two epochs of demand, so they shrink
   back once a burst of big frames is over; they are accounted like every other driver
   allocation, and releaseIdle() returns a ring's buffers when nothing has used it for a
   while under memory pressure.

   Thread safe the same way the exec pool is: slots are claimed with a CAS walk, claims are held
   only across CPU work, and slots are always claimed before exec contexts so the two rings
   cannot deadlock. The device must outlive this object, and waitIdle() must run before frames
   still in flight are destroyed. */
class VSVulkanTransfer {
public:
    VSVulkanTransfer() = default;
    ~VSVulkanTransfer();
    VSVulkanTransfer(const VSVulkanTransfer &) = delete;
    VSVulkanTransfer &operator=(const VSVulkanTransfer &) = delete;

    bool init(VSVulkanDevice &device, uint32_t slots, std::string &errorMessage);

    /* Planes rather than frames: they are owned elsewhere, which is what lets VSPlaneData hold
       them while the transfer machinery stays out of the core headers. */
    bool uploadPlanes(VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
        const uint8_t *const srcPlanes[], const ptrdiff_t srcStrides[], std::string &errorMessage);
    /* The download's submission reads the planes, so unlike the upload it needs them to
       outlive the call: ownership of source passes in here, and releaseSource runs once the
       submission that named them has completed -- or at once if none was made. Its own host
       wait is not that guarantee, since a wait that fails leaves the copy queued, and a plane
       is destroyed after waiting for its own producer alone, which for a host produced plane
       is nothing at all. Still a void pointer rather than a frame, so the transfer stays out
       of the core headers; pass a null release to retain nothing. */
    bool downloadPlanes(const VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
        uint8_t *const dstPlanes[], const ptrdiff_t dstStrides[],
        VSGPUReleaseFunc releaseSource, void *source, std::string &errorMessage);

    bool waitIdle(std::string &errorMessage) { return execPool.waitAll(errorMessage); }

    /* Frees the buffers of every slot in a ring nothing has acquired for idleAfter: the
       memory pressure paths' lever on the rings, since a graph that stopped transferring --
       moved on to CPU work, or to chains that stay resident -- would otherwise hold two
       rings of frame sized buffers for the life of the core. Slots in use and slots whose
       last copy the GPU has not finished are skipped; nothing here waits. Measured from the
       ring's last acquire, so steady transfers keep their rings warm under any pressure and
       two sweeps a millisecond apart cannot declare a ring idle between frames. Returns the
       bytes freed. */
    VkDeviceSize releaseIdle(std::chrono::steady_clock::duration idleAfter);

    /* Testing hook: pretend resizable BAR is absent so the staging path runs everywhere. */
    void setForceStaging(bool force) { forceStaging = force; }

    VSVulkanExecPool &pool() { return execPool; }

private:
    struct Slot {
        VSVulkanBuffer buffer;
        uint64_t value = 0; /* timeline value of the last submission using this slot */
        std::atomic<bool> claimed{false};
    };
    struct SlotRing {
        std::vector<std::unique_ptr<Slot>> slots;
        std::atomic<uint32_t> cursor{0};
        std::mutex claimMutex;
        std::condition_variable claimCv;
        /* Demand, for sizing the slots: the largest request of the current epoch and of the
           one before, an epoch being demandEpoch acquires. A slot is sized to the larger of
           the two rather than to the request in hand, so a graph mixing frame sizes keeps
           its slots at the largest instead of reallocating per frame, while a burst of big
           frames stops dictating the size two epochs after it ends and the slots shrink
           back. Under its own lock; the claim walk stays lock free. */
        std::mutex demandMutex;
        VkDeviceSize epochMax = 0;
        VkDeviceSize previousEpochMax = 0;
        uint32_t epochAcquires = 0;
        /* steady_clock ticks of the last acquire, what releaseIdle measures idleness from. */
        std::atomic<int64_t> lastAcquire{0};
    };

    Slot *acquireSlot(SlotRing &ring, VkDeviceSize minSize, std::string &errorMessage);
    /* Records a request and returns the size a slot serving it should have. */
    VkDeviceSize noteDemand(SlotRing &ring, VkDeviceSize minSize);
    void releaseSlot(SlotRing &ring, Slot &slot);
    bool waitPlanesHost(VSVulkanPlane *const planes[], int numPlanes, std::string &errorMessage);

    VSVulkanDevice *dev = nullptr;
    VSVulkanExecPool execPool;
    SlotRing staging;
    SlotRing readback;
    bool forceStaging = false;
};

#endif
