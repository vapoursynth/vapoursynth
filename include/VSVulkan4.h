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

/* The GPU side of the API, obtained through VSAPI::getVulkanAPI and versioned independently of
 * the core API since it is expected to evolve faster. It is deliberately a raw exposure: the
 * core hands out its Vulkan handles and per plane buffers, and a GPU filter brings its own
 * pipelines, command buffers and synchronization on top of them. The contract in short:
 *
 * - Resolve every entry point through getInstanceProcAddr/vkGetDeviceProcAddr from the handles;
 *   nothing is linked.
 * - vkQueueSubmit on the shared queues must happen with the matching queue lock held.
 * - Before reading a plane on the GPU, wait for its (readySemaphore, readyValue) pair; after
 *   producing one, publish your own pair through setGPUPlaneProducer. A null semaphore means
 *   host produced content that is ready immediately.
 * - Hold references to every frame a submission touches until that submission has completed.
 * - GPU producing functions declare vknode returns and create their nodes with ffGPUOutput.
 */

#ifndef VSVULKAN4_H
#define VSVULKAN4_H

#include "VapourSynth4.h"

#ifndef VULKAN_CORE_H_
/* Only types are needed; including Vulkan yourself first, with or without prototypes, is fine. */
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

#define VSVULKAN_API_VERSION 1

typedef enum VSVulkanQueueType {
    vqCompute = 0,
    vqTransfer = 1 /* the same underlying queue as vqCompute when no dedicated transfer queue exists */
} VSVulkanQueueType;

/* Everything needed to run your own Vulkan work on the core's device. */
typedef struct VSVulkanCoreHandles {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    uint32_t computeQueueFamily;
    uint32_t computeQueueIndex;
    uint32_t transferQueueFamily; /* equal to the compute values when there is no dedicated transfer queue */
    uint32_t transferQueueIndex;
} VSVulkanCoreHandles;

/* A host application handing VapourSynth its existing device instead of letting the core
 * create one. The device must be Vulkan 1.4 with the following features enabled; availability
 * is verified at adoption but enablement cannot be, so that part is the host's responsibility.
 *
 *   required: shaderInt16, storageBuffer16BitAccess, storageBuffer8BitAccess, shaderInt8,
 *             timelineSemaphore, bufferDeviceAddress, scalarBlockLayout, hostQueryReset,
 *             synchronization2, maintenance4, maintenance5, maintenance6, pushDescriptor
 *   optional, used when enabled: hostImageCopy, shaderFloat16
 *
 * A core created device enables exactly this set and nothing else; adoption failures name the
 * first missing feature. When the host keeps submitting to the shared queues itself it must
 * supply the lock callbacks and take the same lock around its own submissions. */
typedef struct VSVulkanHostImport {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t computeQueueFamily;
    uint32_t computeQueueIndex;
    uint32_t transferQueueFamily; /* UINT32_MAX shares the compute queue */
    uint32_t transferQueueIndex;
    void (*lockQueue)(void *context, uint32_t family, uint32_t index);   /* both may be NULL when VapourSynth is the only submitter */
    void (*unlockQueue)(void *context, uint32_t family, uint32_t index);
    void *queueLockContext;
} VSVulkanHostImport;

/* One GPU resident plane: a linear pitched storage buffer laid out exactly like the equivalent
 * CPU plane, so getStride and the frame dimension functions apply unchanged. */
typedef struct VSVulkanPlaneInfo {
    VkBuffer buffer;
    VkDeviceSize bufferSize;    /* stride * height bytes */
    VkSemaphore readySemaphore; /* wait (semaphore, value) before reading; NULL means ready now */
    uint64_t readyValue;
} VSVulkanPlaneInfo;

typedef struct VSVulkanCoreInfo {
    char deviceName[256];
    int64_t deviceMemory; /* largest device local heap */
    int64_t budget;       /* what the driver says this process may reasonably use right now */
    int64_t allocated;    /* current VapourSynth VRAM use */
    int64_t limit;        /* the eviction limit, settable through setMaxVRAMUse */
} VSVulkanCoreInfo;

/* One entry per physical device; the position in the enumeration is exactly the index
 * setVulkanDevice takes. Unusable devices are listed too and may be selected, which fails
 * with their reason, so a frontend can present everything and explain refusals. */
typedef struct VSVulkanDeviceListEntry {
    char deviceName[256];
    uint32_t apiVersion;
    int deviceType;       /* VkPhysicalDeviceType */
    int64_t deviceMemory; /* largest device local heap */
    int usable;           /* passes the Vulkan 1.4 and required feature gate */
    char unusableReason[256];
} VSVulkanDeviceListEntry;

struct VSVULKANAPI {
    /* Device selection, only before the device is first used; -1 picks the most powerful one.
       All int returning functions here return 0 on success and fill errorMessage otherwise. */
    int (VS_CC *setVulkanDevice)(VSCore *core, int deviceIndex, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    int (VS_CC *setVulkanDeviceFromHost)(VSCore *core, const VSVulkanHostImport *import, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Brings the device up on first call, like the first GPU filter would. */
    int (VS_CC *getVulkanHandles)(VSCore *core, VSVulkanCoreHandles *handles, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    int (VS_CC *getVulkanCoreInfo)(VSCore *core, VSVulkanCoreInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    int64_t (VS_CC *setMaxVRAMUse)(int64_t bytes, VSCore *core) VS_NOEXCEPT; /* mirrors setMaxCacheSize for the VRAM pool */

    /* Mandatory around every vkQueueSubmit you make on the shared queues. */
    void (VS_CC *lockVulkanQueue)(VSCore *core, int queue) VS_NOEXCEPT;
    void (VS_CC *unlockVulkanQueue)(VSCore *core, int queue) VS_NOEXCEPT;

    /* GPU resident frames for filter output; identical semantics to newVideoFrame otherwise. */
    VSFrame *(VS_CC *newGPUVideoFrame)(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *getGPUPlane)(const VSFrame *frame, int plane, VSVulkanPlaneInfo *info) VS_NOEXCEPT; /* nonzero when the frame is not GPU resident or the plane does not exist */
    void (VS_CC *setGPUPlaneProducer)(VSFrame *frame, int plane, VkSemaphore semaphore, uint64_t value) VS_NOEXCEPT; /* the semaphore must outlive every possible consumer, in practice the filter instance */

    /* Lists every physical device through a temporary instance, so it works before any device
       selection and needs no core. Returns the total device count, which may exceed
       maxEntries, or -1 with the error set; entries and maxEntries 0 just count. */
    int (VS_CC *enumerateVulkanDevices)(VSVulkanDeviceListEntry *entries, int maxEntries, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
};

#endif
