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


/* Vulkan 1.4 is required rather than negotiated. It brings timeline semaphores, synchronization2,
   descriptor indexing, buffer device address, push descriptors, host image copy and maintenance5/6
   into core, which is what lets this table be almost entirely unconditional. */
#define VS_VULKAN_API_VERSION VK_API_VERSION_1_4

/* Which handle a function is resolved against. Device level entry points are deliberately fetched
   with vkGetDeviceProcAddr: resolving them through the instance yields a loader trampoline that
   has to dispatch on the first argument of every single call. */
enum VSVulkanLevel {
    VS_VK_GLOBAL,   /* resolved from a null instance, before one exists */
    VS_VK_INSTANCE,
    VS_VK_DEVICE
};

/* Optional entries are allowed to stay null and must be null checked at the call site. Everything
   else is core 1.4 and its absence means the device lied about its version, which is fatal. */
enum VSVulkanRequirement {
    VS_VK_REQUIRED,
    VS_VK_OPTIONAL
};

/* One list, expanded several ways: into the struct fields, into the load descriptor table, and
   into a single concatenated name string. Keeping them generated from one place is the only
   practical way to stop the struct and the loader drifting apart, and drift there produces a null
   call at runtime rather than a compile error.

   THE LAYOUT IS FROZEN ABI. The set was completed against core Vulkan 1.4, which stays the
   required version for years, so changes should be rare: entries may only ever be APPENDED at
   the end of the list, only together with a VSVULKAN_API_VERSION bump, and getVulkanAPI serves
   every older version from the same structs since the layout is prefix stable. Members carry
   the vk prefix so no platform header macro (windows.h defines CreateSemaphore) can rename
   them in any translation unit, whatever the include order. */
#define VS_VK_FUNCTION_LIST(FN) \
    /* ---- Global ---- */ \
    FN(VS_VK_GLOBAL,   VS_VK_REQUIRED, EnumerateInstanceVersion) \
    FN(VS_VK_GLOBAL,   VS_VK_REQUIRED, EnumerateInstanceExtensionProperties) \
    FN(VS_VK_GLOBAL,   VS_VK_REQUIRED, EnumerateInstanceLayerProperties) \
    FN(VS_VK_GLOBAL,   VS_VK_REQUIRED, CreateInstance) \
    \
    /* ---- Instance ---- */ \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, DestroyInstance) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, EnumeratePhysicalDevices) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceProperties2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceFeatures2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceMemoryProperties2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceQueueFamilyProperties2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceFormatProperties2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetPhysicalDeviceImageFormatProperties2) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, EnumerateDeviceExtensionProperties) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, CreateDevice) \
    FN(VS_VK_INSTANCE, VS_VK_REQUIRED, GetDeviceProcAddr) \
    /* Validation and tooling, present only when the layer or extension is enabled. */ \
    FN(VS_VK_INSTANCE, VS_VK_OPTIONAL, CreateDebugUtilsMessengerEXT) \
    FN(VS_VK_INSTANCE, VS_VK_OPTIONAL, DestroyDebugUtilsMessengerEXT) \
    \
    /* ---- Device: lifetime and queues ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyDevice) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DeviceWaitIdle) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetDeviceQueue2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, QueueSubmit2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, QueueWaitIdle) \
    \
    /* ---- Memory. MapMemory2/UnmapMemory2 are the 1.4 core spellings. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, AllocateMemory) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, FreeMemory) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, MapMemory2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, UnmapMemory2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, FlushMappedMemoryRanges) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, InvalidateMappedMemoryRanges) \
    \
    /* ---- Buffers ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetBufferMemoryRequirements2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, BindBufferMemory2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetBufferDeviceAddress) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetDeviceBufferMemoryRequirements) \
    \
    /* ---- Images ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetImageMemoryRequirements2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, BindImageMemory2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateImageView) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyImageView) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetImageSubresourceLayout2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetDeviceImageMemoryRequirements) \
    \
    /* ---- Host image copy, core in 1.4. Uploads and downloads without a staging buffer, a \
       command buffer or a queue submission, which is most of what a transfer path otherwise \
       needs. Core commands always resolve, so these stay required, but calling them is gated \
       on the optional hostImageCopy feature (see VS_VK_FEATURE_LIST) plus per format support, \
       so query both before relying on it. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyMemoryToImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyImageToMemory) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyImageToImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, TransitionImageLayout) \
    \
    /* ---- Samplers ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSampler) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySampler) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSamplerYcbcrConversion) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySamplerYcbcrConversion) \
    \
    /* ---- Descriptors. Push descriptors are core in 1.4 and remove the need to pool and \
       allocate sets for the common case of a few bindings per dispatch. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateDescriptorSetLayout) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyDescriptorSetLayout) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateDescriptorPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyDescriptorPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetDescriptorPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, AllocateDescriptorSets) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, FreeDescriptorSets) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, UpdateDescriptorSets) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateDescriptorUpdateTemplate) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyDescriptorUpdateTemplate) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, UpdateDescriptorSetWithTemplate) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetDescriptorSetLayoutSupport) \
    \
    /* ---- Pipelines ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreatePipelineLayout) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyPipelineLayout) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateComputePipelines) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyPipeline) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreatePipelineCache) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyPipelineCache) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetPipelineCacheData) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, MergePipelineCaches) \
    /* maintenance5 allows chaining the SPIR-V straight into pipeline creation, so these two are \
       only needed if a module is deliberately kept around and reused. */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateShaderModule) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyShaderModule) \
    \
    /* ---- Command pools and buffers ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateCommandPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyCommandPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetCommandPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, TrimCommandPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, AllocateCommandBuffers) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, FreeCommandBuffers) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, BeginCommandBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, EndCommandBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetCommandBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdExecuteCommands) \
    \
    /* ---- Command recording. The 2 suffixed forms are the 1.4 core spellings. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdBindPipeline) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdBindDescriptorSets2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdPushConstants2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdPushDescriptorSet) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdPushDescriptorSetWithTemplate) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdDispatch) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdDispatchBase) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdDispatchIndirect) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdPipelineBarrier2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdCopyBuffer2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdCopyImage2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdCopyBufferToImage2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdCopyImageToBuffer2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdBlitImage2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdFillBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdUpdateBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdClearColorImage) \
    \
    /* ---- Synchronisation. The timeline semaphore calls are what let one frame's work wait on \
       the previous filter without the host ever blocking. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSemaphore) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySemaphore) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, WaitSemaphores) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, SignalSemaphore) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetSemaphoreCounterValue) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateFence) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyFence) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetFences) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, WaitForFences) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetFenceStatus) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateEvent) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyEvent) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdSetEvent2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdResetEvent2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdWaitEvents2) \
    \
    /* ---- Timestamp queries. Only ever called when profiling is on, but they are core, so a \
       missing one is as fatal as any other. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetQueryPoolResults) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdResetQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdWriteTimestamp2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdCopyQueryPoolResults) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdBeginQuery) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdEndQuery) \
    \
    /* ---- Debug labels, present only alongside the debug utils extension ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_OPTIONAL, SetDebugUtilsObjectNameEXT) \
    FN(VS_VK_DEVICE,   VS_VK_OPTIONAL, CmdBeginDebugUtilsLabelEXT) \
    FN(VS_VK_DEVICE,   VS_VK_OPTIONAL, CmdEndDebugUtilsLabelEXT)

/* Members keep the vk prefix so they read exactly as the entry point they hold, and so that none
   of them can collide with a macro from a platform header. windows.h defines CreateSemaphore as a
   macro selecting the A or W variant, which would otherwise rename this struct's member in some
   translation units and not others depending on include order. ffmpeg lives with that by always
   including windows.h first; naming the members vkCreateSemaphore and so on removes the problem
   rather than sequencing around it. */
typedef struct VSVulkanFunctions {
#define VS_VK_DECLARE_MEMBER(level, req, name) PFN_vk##name vk##name;
    VS_VK_FUNCTION_LIST(VS_VK_DECLARE_MEMBER)
#undef VS_VK_DECLARE_MEMBER
} VSVulkanFunctions;
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
 * supply the lock callbacks and take the same lock around its own submissions.
 *
 * The imported handles must stay valid for the core's lifetime, extended by any GPU resident
 * frames still referenced after the core is freed: like CPU frames, GPU frames may outlive
 * the core, and releasing one returns VRAM through the imported device. Freeing every GPU
 * frame before destroying the host device is the safe order. */
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

    /* The core's ready loaded dispatch table, the normal way for filters to call Vulkan; the
       handles' getInstanceProcAddr stays available for anything outside the curated set.
       Brings the device up on first call and stays valid for the core's lifetime. */
    const VSVulkanFunctions *(VS_CC *getVulkanFunctions)(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
};

#endif
