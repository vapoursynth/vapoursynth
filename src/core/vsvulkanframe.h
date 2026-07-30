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
   plane it mirrors so a matching stride uploads as a single flat copy. Shaped to slot in as an
   alternative VSPlaneData payload later, which is why the frame below is little more than an
   array of these.

   The producer pair lives here, per plane and not per frame, because plane sharing between
   frames means one frame's planes can have different producers. Whoever writes the plane on
   the GPU stores the timeline and value its submission signals; consumers wait on it device
   side before reading. A null semaphore means host produced content, ready as soon as it is
   handed over. */
struct VSVulkanPlane {
    VSVulkanBuffer buffer;
    ptrdiff_t stride = 0; /* bytes per row, aligned like a CPU plane would be */
    uint32_t width = 0;   /* in samples */
    uint32_t height = 0;
    VkSemaphore readySemaphore = VK_NULL_HANDLE;
    uint64_t readyValue = 0;
};

/* A GPU resident video frame. Reader tracking is left for the filter runtime, where exec
   contexts will hold frame references until their submissions complete; until then a frame
   must not be rewritten while consumers may still be reading it. */
struct VSVulkanFrame {
    VSVideoFormat format = {};
    int numPlanes = 0;
    VSVulkanPlane planes[3];
};

/* Appends every plane's producer pair; host ready planes contribute nothing and same timeline
   pairs collapse, so the common all-planes-one-producer frame adds a single wait. */
inline void addFrameWaits(VSVulkanWaitList &list, const VSVulkanFrame &frame) {
    for (int p = 0; p < frame.numPlanes; p++)
        list.add(frame.planes[p].readySemaphore, frame.planes[p].readyValue);
}

/* The common case after one submission wrote every plane. Partially produced frames set their
   plane pairs individually instead. */
inline void setFrameProduced(VSVulkanFrame &frame, VkSemaphore semaphore, uint64_t value) {
    for (int p = 0; p < frame.numPlanes; p++) {
        frame.planes[p].readySemaphore = semaphore;
        frame.planes[p].readyValue = value;
    }
}

/* Owns the machinery moving frames across the PCIe bus with the policies the transfer benchmark
   picked: uploads memcpy straight into the plane buffer when it landed in host visible device
   local memory (resizable BAR), otherwise they go through a pipelined staging ring on the DMA
   queue; downloads always run a DMA copy into host cached staging and memcpy out, because CPU
   reads from VRAM are two to three orders of magnitude too slow to ever be the answer. All plane
   copies of a frame travel in one submission since the ~0.2 ms per submission floor dwarfs the
   per plane cost at common sizes.

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

    Slot *acquireSlot(SlotRing &ring, VkDeviceSize minSize, bool hostCached, std::string &errorMessage);
    void releaseSlot(SlotRing &ring, Slot &slot);
    bool waitFrameHost(const VSVulkanFrame &frame, std::string &errorMessage);

    VSVulkanDevice *dev = nullptr;
    VSVulkanExecPool execPool;
    SlotRing staging;
    SlotRing readback;
    bool forceStaging = false;
};

#endif
