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
    if (timeline)
        timeline->addRef();
    if (plane.readyTimeline)
        plane.readyTimeline->release();
    plane.readyTimeline = timeline;
    plane.readyValue = value;
}

/* A GPU resident video frame. Reader tracking is left for the filter runtime, where exec
   contexts will hold frame references until their submissions complete; until then a frame
   must not be rewritten while consumers may still be reading it. */
struct VSVulkanFrame {
    VSVideoFormat format = {};
    int numPlanes = 0;
    VSVulkanPlane planes[3];

    void reset() {
        format = {};
        numPlanes = 0;
        for (VSVulkanPlane &plane : planes)
            plane.reset();
    }
};

/* Appends every plane's producer pair; host ready planes contribute nothing and same timeline
   pairs collapse, so the common all-planes-one-producer frame adds a single wait. */
inline void addFrameWaits(VSVulkanWaitList &list, const VSVulkanFrame &frame) {
    for (int p = 0; p < frame.numPlanes; p++)
        list.add(frame.planes[p].readyTimeline, frame.planes[p].readyValue);
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
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
    waitInfo.pValues = &plane.readyValue;
    return device.vk.vkWaitSemaphores(device.device(), &waitInfo, UINT64_MAX) == VK_SUCCESS;
}

/* The common case after one submission wrote every plane. Partially produced frames set their
   plane pairs individually instead. */
inline void setFrameProduced(VSVulkanFrame &frame, VSVulkanTimeline *timeline, uint64_t value) {
    for (int p = 0; p < frame.numPlanes; p++)
        setPlaneProducer(frame.planes[p], timeline, value);
}

/* Moves frames across the PCIe bus. Uploads memcpy straight into the plane buffer when it
   landed in host visible device local memory (resizable BAR), otherwise through a pipelined
   staging ring on the DMA queue. Downloads mirror that but gate on host CACHED rather than
   coherent, since reading a discrete card's write combined memory over PCIe is orders of
   magnitude too slow; unified memory hands back cached plane memory that reads at memcpy
   speed. A frame's plane copies all travel in one submission, the ~0.2 ms submission floor
   dwarfing the per plane cost at common sizes.

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

    bool createFrame(VSVulkanFrame &frame, const VSVideoFormat &format, int width, int height, std::string &errorMessage);
    /* The frame must not be in flight; wait on its producer or waitIdle() first. */
    void destroyFrame(VSVulkanFrame &frame);

    /* The pointer forms work directly on planes owned elsewhere, which is what lets VSPlaneData
       hold the planes while the transfer machinery stays out of the core headers; the frame
       forms are thin wrappers over them. */
    bool uploadPlanes(VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
        const uint8_t *const srcPlanes[], const ptrdiff_t srcStrides[], std::string &errorMessage);
    bool downloadPlanes(const VSVulkanPlane *const planes[], int numPlanes, int bytesPerSample,
        uint8_t *const dstPlanes[], const ptrdiff_t dstStrides[], std::string &errorMessage);
    bool upload(VSVulkanFrame &frame, const uint8_t *const srcPlanes[3], const ptrdiff_t srcStrides[3], std::string &errorMessage);
    bool download(const VSVulkanFrame &frame, uint8_t *const dstPlanes[3], const ptrdiff_t dstStrides[3], std::string &errorMessage);

    bool waitIdle(std::string &errorMessage) { return execPool.waitAll(errorMessage); }

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
    };

    Slot *acquireSlot(SlotRing &ring, VkDeviceSize minSize, std::string &errorMessage);
    void releaseSlot(SlotRing &ring, Slot &slot);
    bool waitPlanesHost(VSVulkanPlane *const planes[], int numPlanes, std::string &errorMessage);

    VSVulkanDevice *dev = nullptr;
    VSVulkanExecPool execPool;
    SlotRing staging;
    SlotRing readback;
    bool forceStaging = false;
};

#endif
