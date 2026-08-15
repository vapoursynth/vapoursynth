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

/* The GPU side of the API, obtained through VSAPI::getVulkanAPI. It has no version of its own and
 * grows with the core API instead, so there is nothing to negotiate and the call cannot fail. It
 * is deliberately a raw exposure: the core hands out its Vulkan handles and per plane buffers, and
 * a GPU filter brings its own pipelines, command buffers and synchronization on top of them. The
 * contract in short:
 *
 * - Resolve every entry point through getInstanceProcAddr/vkGetDeviceProcAddr from the handles;
 *   nothing is linked.
 * - vkQueueSubmit on the shared queues must happen with the matching queue lock held.
 * - Before reading a plane on the GPU, wait for its (readySemaphore, readyValue) pair; after
 *   producing one, publish your own pair through setGPUPlaneProducer, whose timeline is a
 *   counted VSGPUTimeline the plane keeps alive by itself. A null semaphore means host produced
 *   content that is ready immediately.
 * - Hold references to every frame a submission touches until that submission has completed.
 * - GPU producing functions declare "vnode:gpu" returns and create their nodes with ffGPUOutput.
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
   descriptor indexing, buffer device address, push descriptors and maintenance5/6 into core,
   which is what lets this table be almost entirely unconditional. */
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
    /* ---- Device ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyDevice) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DeviceWaitIdle) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetDeviceQueue2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, QueueSubmit2) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, QueueWaitIdle) \
    \
    /* ---- Memory ---- */ \
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
    /* ---- Samplers ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSampler) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySampler) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSamplerYcbcrConversion) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySamplerYcbcrConversion) \
    \
    /* ---- Descriptors ---- */ \
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
    /* ---- Synchronisation ---- */ \
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
    /* ---- Timestamp queries ---- */ \
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

typedef struct VSVulkanFunctions {
#define VS_VK_DECLARE_MEMBER(level, req, name) PFN_vk##name vk##name;
    VS_VK_FUNCTION_LIST(VS_VK_DECLARE_MEMBER)
#undef VS_VK_DECLARE_MEMBER
} VSVulkanFunctions;

/* The two are NOT interchangeable, and Vulkan guarantees the implication one way only: a
 * compute queue always accepts transfer commands, while a dedicated transfer queue need not
 * accept compute ones -- a discrete card's DMA family typically reports VK_QUEUE_TRANSFER_BIT
 * alone. So vqCompute takes anything, and vqTransfer must be given copies only; recording a
 * dispatch against it is invalid usage wherever a real DMA family exists, and silently fine on
 * the hardware where the two resolve to the same queue, which is what makes the mistake easy to
 * ship. When in doubt use vqCompute: the cost is losing overlap with the core's own transfers,
 * not correctness. */
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

/* The device feature baseline. The core creates every device itself: Vulkan 1.4 with exactly
 * the features below and no extensions except the platform's opaque memory and semaphore handle
 * export (VK_KHR_external_memory/_semaphore_win32 or _fd) where available, plus
 * VK_KHR_portability_subset where the device demands it. Every REQUIRED entry is mandatory for a
 * conformant 1.4 implementation. Sharing frames with another device or API goes through
 * exportGPUPlane, not device sharing.
 *
 *   required (VkPhysicalDeviceFeatures): shaderInt16, shaderImageGatherExtended,
 *     shaderStorageImageExtendedFormats, shaderUniformBufferArrayDynamicIndexing,
 *     shaderSampledImageArrayDynamicIndexing, shaderStorageBufferArrayDynamicIndexing,
 *     shaderStorageImageArrayDynamicIndexing
 *   required (Vulkan11Features): storageBuffer16BitAccess, variablePointersStorageBuffer,
 *     variablePointers, samplerYcbcrConversion
 *   required (Vulkan12Features): timelineSemaphore, hostQueryReset,
 *     uniformBufferStandardLayout, shaderSubgroupExtendedTypes, subgroupBroadcastDynamicId,
 *     bufferDeviceAddress, vulkanMemoryModel, vulkanMemoryModelDeviceScope,
 *     storageBuffer8BitAccess, shaderInt8, scalarBlockLayout
 *   required (Vulkan13Features): synchronization2, maintenance4, subgroupSizeControl,
 *     computeFullSubgroups, shaderIntegerDotProduct, shaderZeroInitializeWorkgroupMemory,
 *     inlineUniformBlock, pipelineCreationCacheControl, privateData
 *   required (Vulkan14Features): maintenance5, maintenance6, pushDescriptor,
 *     shaderSubgroupRotate, shaderSubgroupRotateClustered, shaderFloatControls2,
 *     shaderExpectAssume, pipelineRobustness
 *   optional, enabled when the device has it: shaderFloat16 (Vulkan12Features), shaderInt64 and
 *     shaderFloat64 (Features), shaderBufferInt64Atomics and shaderSharedInt64Atomics
 *     (Vulkan12Features) -- query the physical device to find out whether you got them
 *
 * One extension pair is enabled beyond the handle export pair, under the same policy the
 * optional features above follow: whenever the device offers VK_EXT_shader_atomic_float and its
 * float2 companion they are enabled, with exactly the feature bits the device reports. That
 * guarantee is the availability contract -- Vulkan cannot ask a created device what was enabled,
 * so enable-whatever-is-reported is what makes the physical device's own queries authoritative.
 * Check presence with vkEnumerateDeviceExtensionProperties and the bits with
 * vkGetPhysicalDeviceFeatures2; what they report is what is live. Resolve any extension entry
 * points you need yourself through the handles' getInstanceProcAddr -- the function table below
 * stays core 1.4 plus debug utils by design. */

/* One GPU resident plane: a linear pitched storage buffer laid out exactly like the equivalent
 * CPU plane, so getStride and the frame dimension functions apply unchanged. */
typedef struct VSVulkanPlaneInfo {
    VkBuffer buffer;
    VkDeviceSize bufferSize;    /* stride * height bytes */
    VkSemaphore readySemaphore; /* wait (semaphore, value) before reading; NULL means ready now */
    uint64_t readyValue;
} VSVulkanPlaneInfo;

/* A timeline semaphore for publishing producer pairs, reference counted and owned by the core.
 * A filter signals it and hands it to setGPUPlaneProducer; every plane it is published on takes
 * its own reference, so the semaphore lives exactly as long as something might still wait on it.
 *
 * A filter's timeline therefore need not outlive its consumers: release your reference once you
 * are done signalling -- the free callback is the natural place -- and any frame still in flight
 * keeps the semaphore alive. Frames legitimately outlive the filter that made them (FrameEval
 * and ModifyFrame return one from a node they then drop, and the cache can hold one
 * indefinitely), which is what the counting is for.
 *
 * The semaphore is created exportable wherever the device supports it, so a foreign API can wait
 * on the pairs you publish device side. */
typedef struct VSGPUTimeline VSGPUTimeline;

/* A scratch buffer from the core's pooled VRAM allocator, for filters needing memory that is
 * not a frame plane: reduction partials, lookup tables, intermediate rows. Owned through the
 * opaque handle; the info struct is everything a kernel or the host needs to use it. */
typedef struct VSGPUBuffer VSGPUBuffer;

typedef struct VSVulkanBufferInfo {
    VkBuffer buffer;
    VkDeviceAddress address;         /* nonzero when usage included SHADER_DEVICE_ADDRESS */
    void *mapped;                    /* nonnull when the memory ended up host visible; persistently mapped */
    VkDeviceSize size;               /* the requested size, unrounded */
    VkMemoryPropertyFlags memoryFlags; /* what the chosen memory type actually provides */
} VSVulkanBufferInfo;

/* A bare region of the same pooled VRAM, for resources the core has no constructor for. An
 * image is the case that motivates it: memory is never passed to vkCreateImage, it is bound
 * afterwards, so nothing is gained by wrapping image creation -- the filter creates the image
 * its own way and only the allocation comes from here. */
typedef struct VSGPUMemory VSGPUMemory;

typedef struct VSVulkanMemoryInfo {
    VkDeviceMemory memory;   /* the shared block; bind against it, never free it */
    VkDeviceSize offset;     /* where the region starts in that block; pass to vkBind*Memory2 */
    VkDeviceSize size;       /* what the region actually reserves, at least the requested size */
    void *mapped;            /* nonnull when host visible, already offset to the region */
    VkMemoryPropertyFlags memoryFlags; /* what the chosen memory type actually provides */
} VSVulkanMemoryInfo;

/* A declaration of GPU memory allocated outside the core -- by CUDA, another Vulkan device
 * ecosystem, a video session -- so the byte count takes part in the core's budgeting; see
 * reserveGPUMemory. Holds no memory itself, only the number. */
typedef struct VSGPUMemoryReservation VSGPUMemoryReservation;

/* A frame plane's backing memory exported as an opaque handle, so CUDA or another Vulkan
 * device in the process can wrap the allocation and read or write the plane zero copy. The
 * plane lives at offset within an allocation of memorySize bytes; import the whole allocation
 * once and address planes by offset.
 *
 * memoryId is stable for the allocation's lifetime and never reused, while every export call
 * returns a NEW handle — so key cached imports by memoryId, never by handle value, and close
 * surplus handles. The handle belongs to the caller: an NT handle is not consumed by Vulkan or
 * CUDA import, so CloseHandle it once the import exists; a POSIX fd is consumed by a
 * successful import and must only be closed when the import failed.
 *
 * The OS reference-counts the memory, so a cached import may outlive the frames, and the core,
 * that led to it. Synchronization is host side: call waitGPUFrame before reading through an
 * import (a bare producer pair wait is not enough, see there) and finish foreign writes before
 * returning a frame containing them. */
typedef struct VSVulkanExportedMemory {
    uint64_t memoryId;
    VkDeviceSize memorySize;
    VkDeviceSize offset;
    VkDeviceSize size;   /* the plane's bytes, stride * height */
    int handleType;      /* the VkExternalMemoryHandleTypeFlagBits of the handle */
    intptr_t handle;     /* HANDLE on Windows, file descriptor elsewhere */
} VSVulkanExportedMemory;

/* An exec pool and one of its recording slots. The pool owns a timeline semaphore, a
   command pool and contextCount command buffers; a context is one recording, claimed by
   one thread from gpuExecAcquire until gpuExecSubmit or gpuExecAbandon.

   This is the plumbing every GPU filter needs regardless of what it records: waiting on the
   producers of the frames it reads, keeping those frames alive until the GPU is done,
   allocating timeline values in queue order, and publishing producer pairs on the planes it
   writes. Filters that record ordinary dispatches and filters that record indirect
   dispatches, custom barriers or their own query pools need it equally, so the context
   hands out its command buffer and imposes nothing on what goes into it. */
typedef struct VSGPUExecPool VSGPUExecPool;
typedef struct VSGPUExecContext VSGPUExecContext;

/* Cleanup a filter hands to an exec context, run once the submission it was recorded against
 * has completed; see gpuExecRetain. */
typedef void (VS_CC *VSGPUReleaseFunc)(void *object);

/* A runtime compiled shader as an opaque handle holding the SPIR-V words. Independent of
   everything else once returned: it stays valid after the core that compiled it is freed
   and is released with freeGPUShader. */
typedef struct VSGPUShader VSGPUShader;

typedef enum VSGPUShaderLanguage {
    slGLSL = 0 /* compute stage GLSL; the core pins the accepted dialect as a platform property: write #version 460, compiled for the Vulkan 1.4 client targeting SPIR-V 1.6 */
} VSGPUShaderLanguage;

/* A timeline semaphore exported as an opaque handle, from exportGPUSemaphore. Importing a
 * producer pair's semaphore lets a CUDA stream or another Vulkan device wait the pair on the
 * device, replacing waitGPUFrame's host wait and restoring full pipelining across the API
 * boundary; an external semaphore wait also carries the cross device memory dependency, so
 * no separate availability flush is needed on that path.
 *
 * Every call returns a NEW handle for the same semaphore. Cache imports keyed by the
 * VkSemaphore value from VSVulkanPlaneInfo, scoped to your filter instance: a plane holds a
 * reference to the timeline it names, so as long as you hold the frame the handle stays
 * live and the key cannot go stale. Handle ownership follows the same platform rules as memory
 * export (close NT handles after importing; fds are consumed by a successful import). */
typedef struct VSVulkanExportedSemaphore {
    int handleType;      /* the VkExternalSemaphoreHandleTypeFlagBits of the handle */
    intptr_t handle;     /* HANDLE on Windows, file descriptor elsewhere */
} VSVulkanExportedSemaphore;

typedef struct VSVulkanCoreInfo {
    char deviceName[256];
    int64_t deviceMemory; /* largest device local heap */
    int64_t budget;       /* what the driver says this process may reasonably use right now */
    int64_t allocated;    /* current VapourSynth VRAM use */
    int64_t limit;        /* the eviction limit, settable through setMaxVRAMUse */
    /* Identity for matching against other APIs' device enumerations (CUDA exposes the same
       UUID; the LUID pairs with DXGI adapters and is only meaningful when luidValid). */
    uint8_t deviceUUID[VK_UUID_SIZE];
    uint8_t deviceLUID[VK_LUID_SIZE];
    uint32_t deviceNodeMask;
    int deviceLUIDValid;
    int exportHandleType; /* VkExternalMemoryHandleTypeFlagBits exportGPUPlane hands out, 0 when export is unavailable */
    int semaphoreExportHandleType; /* VkExternalSemaphoreHandleTypeFlagBits exportGPUSemaphore hands out, 0 when unavailable */
    /* Nonzero when the device's memory is the host's, which is the case for integrated and
       software devices. deviceMemory and budget then describe a share of system RAM rather
       than separate hardware, and limit is capped so it and the host memory limit together
       leave the machine some room; a plugin sizing its own pools should do likewise. */
    int unifiedMemory;
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
    uint8_t deviceUUID[VK_UUID_SIZE];
    uint8_t deviceLUID[VK_LUID_SIZE];
    uint32_t deviceNodeMask;
    int deviceLUIDValid;
} VSVulkanDeviceListEntry;

struct VSVULKANAPI {
    /* Unless an entry says otherwise, a status returning function returns 0 on success and
       nonzero on failure, and one returning a handle returns NULL on failure; either way the
       reason lands in errorMessage where the function takes one. */

    /* ---- Device selection and information ---- */

    /* Lists every physical device through a temporary instance, so it works before any device
       selection and needs no core. Returns the total device count, which may exceed
       maxEntries, or -1 with the error set; entries and maxEntries 0 just count. */
    int (VS_CC *enumerateVulkanDevices)(VSVulkanDeviceListEntry *entries, int maxEntries, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Picks which of those this core runs on, only before the device is first used; -1 picks
       the most powerful one. */
    int (VS_CC *setVulkanDevice)(VSCore *core, int deviceIndex, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* What the core ended up with and where its VRAM budget stands. Brings the device up on
       first call, like the first GPU filter would. */
    int (VS_CC *getVulkanCoreInfo)(VSCore *core, VSVulkanCoreInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    int64_t (VS_CC *setMaxVRAMUse)(int64_t bytes, VSCore *core) VS_NOEXCEPT; /* mirrors setMaxCacheSize for the VRAM pool */

    /* ---- Calling Vulkan on the core's device ---- */

    /* The instance, physical device, device and queue indices. Brings the device up on first
       call, like the first GPU filter would. */
    int (VS_CC *getVulkanHandles)(VSCore *core, VSVulkanCoreHandles *handles, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* The core's ready loaded dispatch table, the normal way for filters to call Vulkan; the
       handles' getInstanceProcAddr stays available for anything outside the curated set.
       Brings the device up on first call and stays valid for the core's lifetime. */
    const VSVulkanFunctions *(VS_CC *getVulkanFunctions)(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Mandatory around every vkQueueSubmit you make on the shared queues. */
    void (VS_CC *lockVulkanQueue)(VSCore *core, int queue) VS_NOEXCEPT;
    void (VS_CC *unlockVulkanQueue)(VSCore *core, int queue) VS_NOEXCEPT;

    /* ---- GPU resident frames ---- */

    /* GPU resident frames for filter output; identical semantics to newVideoFrame otherwise. Note that newVideoFrame2 also supports GPU resident frames */
    VSFrame *(VS_CC *newGPUVideoFrame)(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *getGPUPlane)(const VSFrame *frame, int plane, VSVulkanPlaneInfo *info) VS_NOEXCEPT; /* nonzero when the frame is not GPU resident or the plane does not exist */
    void (VS_CC *setGPUPlaneProducer)(VSFrame *frame, int plane, VSGPUTimeline *timeline, uint64_t value) VS_NOEXCEPT; /* the plane takes its own reference; NULL publishes the plane as host ready */

    /* ---- Timelines, the semaphores producer pairs are published on ---- */

    /* A timeline of your own, for filters recording and submitting without the core's exec
       pool. Created with an initial value of 0, exportable where the device allows it, and
       returned with one reference which is yours to release -- in the free callback, without
       waiting for consumers, since planes you published it on hold their own. Returns NULL
       on error. */
    VSGPUTimeline *(VS_CC *createGPUTimeline)(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *freeGPUTimeline)(VSGPUTimeline *timeline) VS_NOEXCEPT;
    /* Takes another reference, for handing the same timeline to something with its own
       lifetime. Every added reference needs a matching freeGPUTimeline. */
    void (VS_CC *addGPUTimelineRef)(VSGPUTimeline *timeline) VS_NOEXCEPT;
    /* The raw handle, to signal in your own vkQueueSubmit and to pass to exportGPUSemaphore.
       Valid for as long as you hold a reference. */
    VkSemaphore (VS_CC *getGPUTimelineSemaphore)(VSGPUTimeline *timeline) VS_NOEXCEPT;

    /* ---- Memory from the core's pool ---- */

    /* Scratch memory through the same sub allocator frame planes use, so it is counted
       against the VRAM limit, predicted by the thread pool's admission control and recycled
       through the size buckets — allocate/destroy per frame is cheap by design. Host visible
       requests come back persistently mapped. Returns NULL with the error set on failure.

       Lifetime is the caller's, with one rule: unlike frames, buffers have no producer pair
       anyone waits on, so destroy only after the submissions using the buffer have completed
       on the device, and at the latest in the filter's free callback. */
    VSGPUBuffer *(VS_CC *createGPUBuffer)(VSCore *core, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags,
        VSVulkanBufferInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *destroyGPUBuffer)(VSGPUBuffer *buffer) VS_NOEXCEPT;

    /* A region of the pool frame planes come from, for a resource this API has no constructor
       for. Create the resource, ask Vulkan what memory it needs, allocate it here, and bind
       against the memory and offset that come back:

           vkCreateImage(device, &imageInfo, NULL, &image);
           vkGetImageMemoryRequirements2(device, &reqInfo, &req);
           VSGPUMemory *mem = allocateGPUMemory(core, &req.memoryRequirements,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
           VkBindImageMemoryInfo bind = { ..., image, info.memory, info.offset };
           vkBindImageMemory2(device, 1, &bind);

       Over a private vkAllocateMemory this buys everything the pool does: the region counts
       against the VRAM limit, is visible to admission control, is recycled through the size
       buckets and takes part in reclamation under pressure. The core cannot see a private
       allocation, so it can neither account for it nor make room for it.

       Bind at info.offset, not at the start of info.memory: the region sits inside a shared
       block, and images are aligned further inside it since their requirements are routinely
       coarser than the pool's regions (65536 for a small image where a large one asks 256).
       The distance is reserved, so any alignment is satisfied at a bounded overshoot.

       Two resources must not bind here and neither is detectable from the requirements, so
       both are the caller's to check: one created with external memory info, since these
       blocks are not exportable, and one whose VkMemoryDedicatedRequirements report
       requiresDedicatedAllocation, since a suballocated region is by definition not dedicated.
       Chain VkMemoryDedicatedRequirements onto the vkGet*MemoryRequirements2 call to see the
       latter; it is false for ordinary images and buffers and true mostly for external and
       platform specific formats. (prefersDedicatedAllocation is a hint, safe to ignore here.)

       A device whose buffer/image granularity is coarser than the pool's regions is refused
       rather than risking two resources sharing a granularity page.

       A freed region returns to the pool at once and may back another resource immediately, so
       freeing early aliases live data rather than faulting: free only once the submissions
       using the resource have completed, or hand it to gpuExecUsesMemory.

       Returns NULL with the error set. */
    VSGPUMemory *(VS_CC *allocateGPUMemory)(VSCore *core, const VkMemoryRequirements *requirements,
        VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags,
        VSVulkanMemoryInfo *info, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *freeGPUMemory)(VSGPUMemory *memory) VS_NOEXCEPT;

    /* Declares GPU memory the core did not allocate -- a CUDA pool, another Vulkan device, a
       video session -- into the same accounting, so the frame cache, admission control and
       the unified-memory brake all see it. The core cannot veto memory it does not own, so a
       reservation is never refused; what an increase buys is cooperation. When the declared
       bytes push the pool past its limit, cached GPU frames are evicted to a tenth under it
       and idle allocator blocks handed back to the driver before the call returns -- so
       reserve or update BEFORE the foreign allocation and the VRAM has actually been vacated.
       Shrinking and releasing just subtract.

       bytes is an absolute total, not a delta, so publishing what you hold makes drift
       impossible however the calls interleave. Concurrent updates are safe and the last total
       wins; negative counts as zero. release drops the reservation with the handle, ignores
       NULL, and belongs in the filter free callback.

       Only declare memory on the core's device -- match deviceUUID or deviceLUID from
       getVulkanCoreInfo -- and never declare bytes the core already accounts (createGPUBuffer,
       allocateGPUMemory, GPU frames) or they count twice. On unified memory this matters
       doubly, the pool's bytes and the host's being the same RAM. Returns NULL on error. */
    VSGPUMemoryReservation *(VS_CC *reserveGPUMemory)(VSCore *core, int64_t bytes,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *updateGPUMemoryReservation)(VSGPUMemoryReservation *reservation, int64_t bytes) VS_NOEXCEPT;
    void (VS_CC *releaseGPUMemoryReservation)(VSGPUMemoryReservation *reservation) VS_NOEXCEPT;

    /* ---- Shader compilation ---- */

    /* Runtime shader compilation, so plugins can ship readable kernel source instead of
       SPIR-V blobs. Pure CPU work through the statically embedded glslang: no device is
       touched and no optimizer runs, so precompiled -O blobs remain the alternative for
       whoever wants them and both feed the same pipeline creation path. Results are cached
       per core by source text — compiling the same kernel from many filter instances
       parses once and every handle shares the cached words, which also makes repeated
       compilation cheap enough to do per instance. Specialize by concatenating a #define
       preamble in front of the kernel body; there is no include handler. Compute stage
       only. Returns NULL with the log filled on failure, including for languages this core
       does not know. */
    VSGPUShader *(VS_CC *compileGPUShader)(VSCore *core, int language, const char *source,
        char *errorLog, int errorLogSize) VS_NOEXCEPT; /* VSGPUShaderLanguage */
    const uint32_t *(VS_CC *getGPUShaderCode)(const VSGPUShader *shader, size_t *sizeInBytes) VS_NOEXCEPT;
    void (VS_CC *freeGPUShader)(VSGPUShader *shader) VS_NOEXCEPT;

    /* ---- Exec pools ---- */

    /* Creates an exec pool on one of the core's queues. A pool on vqTransfer may only ever
       record copies, per VSVulkanQueueType, and does not drive the core's progress timeline, so
       the in-flight budget below falls back to polling for it; a pool anything is dispatched
       into belongs on vqCompute.

       The core sizes the pool's context
       ring itself, from its worker thread count — how many recordings can even be
       concurrent is core knowledge, not filter knowledge, and how much memory queued
       submissions may pin is bounded separately: acquiring waits out the ring's oldest
       submission, and may additionally wait on the core's device-wide in-flight budget,
       which caps the total bytes queued submissions retain (a quarter of the VRAM limit)
       across all pools. Filters notice nothing but an occasional slower acquire when a
       graph runs far ahead of the GPU. The pool's timeline is created exportable when the
       device can, so consumers in other APIs can wait the producer pairs it publishes.
       Destroy it in the filter's free callback; freeGPUExecPool drains the GPU first, so
       everything it still holds is released safely.

       Retained objects (read frames, scratch handed over with gpuExecUsesBuffer) are
       released once their submission is known complete: every submit on the pool reaps the
       other contexts' completed retentions (about one submission of lag while active), and
       the context's next acquire, pool destruction and the core's memory pressure sweeps
       cover the rest — so a pool gone idle does not park its last submissions' footprint.
       That reclamation is a pool-only property; references a filter retains privately on
       the raw path are invisible to the core and cannot be freed by pressure. */
    VSGPUExecPool *(VS_CC *createGPUExecPool)(VSCore *core, int queue /* VSVulkanQueueType */,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *freeGPUExecPool)(VSGPUExecPool *pool) VS_NOEXCEPT;

    /* Blocks until every submission made through this pool has completed. Filters do not
       need this per frame — producer pairs make consumers wait on the device instead — but
       one shot setup work, such as uploading weights or tables a filter will read for the
       rest of its life, has to know the copy landed before recording anything that reads
       it. Also releases everything those submissions were keeping alive. */
    int (VS_CC *gpuExecPoolWaitIdle)(VSGPUExecPool *pool, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* The pool's timeline as the counted object setGPUPlaneProducer takes, for publishing
       producer pairs by hand on frames the pool does not know about -- the out of order
       producer case, where the submission that wrote a plane was submitted calls ago and
       gpuExecSubmit's signaledValue was saved for this moment. The pool holds its own
       reference, so publishing it needs no reference of yours; getGPUTimelineSemaphore
       gives the raw handle when exportGPUSemaphore needs one. NEVER signal that handle
       yourself: the pool allocates signal values under the queue lock at submit, and an
       external signal races them on a timeline where values must only increase. */
    VSGPUTimeline *(VS_CC *gpuExecPoolTimeline)(VSGPUExecPool *pool) VS_NOEXCEPT;

    /* ---- Recording contexts ---- */

    /* Claims a context and begins recording; returns NULL with the error set on device
       loss. Every acquire must end in exactly one submit or abandon. */
    VSGPUExecContext *(VS_CC *gpuExecAcquire)(VSGPUExecPool *pool, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    /* The command buffer being recorded: put anything Vulkan allows into it. */
    VkCommandBuffer (VS_CC *gpuExecCommandBuffer)(VSGPUExecContext *context) VS_NOEXCEPT;

    /* Declares that this submission reads the frame: its planes' producer pairs become
       device side waits, and the frame is kept alive until the submission completes. Takes
       its own reference, so the caller still releases its own reference normally. */
    void (VS_CC *gpuExecReadsFrame)(VSGPUExecContext *context, const VSFrame *frame) VS_NOEXCEPT;
    /* Declares that this submission writes the plane: gpuExecSubmit publishes the pool's
       (timeline, value) on it as the producer pair. */
    void (VS_CC *gpuExecWritesPlane)(VSGPUExecContext *context, VSFrame *frame, int plane) VS_NOEXCEPT;
    /* Hands a scratch buffer to the context, which destroys it once the submission
       completes. Ownership transfers; do not destroy it yourself. */
    void (VS_CC *gpuExecUsesBuffer)(VSGPUExecContext *context, VSGPUBuffer *buffer) VS_NOEXCEPT;

    /* gpuExecUsesBuffer for a bare region: the region stays out of the pool until the
       submission being recorded has completed, and its bytes count against the in-flight
       retention budget meanwhile. Ownership transfers; do not free it yourself. Release
       follows the ordinary retention timing described at createGPUExecPool -- reaped by the
       next submit on the pool, and by acquire, destruction or a pressure sweep otherwise --
       so what is guaranteed is that the region is never recycled early, not that it returns
       at a particular moment.

       The resource bound to the region is still yours to destroy, since this never saw it. A
       resource whose lifetime is the filter's -- created once, reused every frame -- is the
       easy case and wants no retention at all. One created per frame has to outlive its
       submission too, so keep those handles and destroy them when the filter is freed rather
       than trying to guess when the submission retired. */
    void (VS_CC *gpuExecUsesMemory)(VSGPUExecContext *context, VSGPUMemory *memory) VS_NOEXCEPT;

    /* Cleanup of your own on the retention list, for what the typed calls above cannot name.
       release(object) runs once the submission completes, on the schedule described at
       createGPUExecPool; call it between acquire and submit, and abandon runs it immediately.

       A per frame image is the case: the image, every view recorded against it and the region
       underneath must retire together, and one registration of a struct holding all three
       frees them in the right order -- views, image, then freeGPUMemory -- where a call per
       object could not express the ordering.

       bytes is what the object pins in device memory, counted against the in-flight retention
       budget until release; pass 0 for host side bookkeeping, and do not double count bytes a
       typed call already counted for the same object.

       The callback runs on whichever thread reaps the submission, so it must be threadsafe.
       No pool lock is held, so it may take its own, but it must not acquire a context from the
       pool that is reaping it. */
    void (VS_CC *gpuExecRetain)(VSGPUExecContext *context, VSGPUReleaseFunc release,
        void *object, VkDeviceSize bytes) VS_NOEXCEPT;

    /* Ends recording and submits, allocating the timeline value inside the queue lock so
       signals reach the queue in increasing order, then publishes the producer pairs. The
       context is consumed either way. Returns nonzero with the error set on failure.

       signaledValue, when non-NULL, receives the value this submission signals on the
       pool's timeline. Waiting for it — vkWaitSemaphores on
       getGPUTimelineSemaphore(gpuExecPoolTimeline(pool)) — waits for exactly this
       submission, which is what a filter reading results back on the host wants:
       gpuExecPoolWaitIdle also works but waits the pool's newest submission, so
       concurrent frames serialize on each other's work. Filters that only produce
       planes never need either; the producer pairs carry the synchronization. */
    int (VS_CC *gpuExecSubmit)(VSGPUExecContext *context, uint64_t *signaledValue, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    /* Gives up a recording without submitting: everything retained is released at once. */
    void (VS_CC *gpuExecAbandon)(VSGPUExecContext *context) VS_NOEXCEPT;

    /* ---- Sharing frames with other APIs ---- */

    /* Exports the allocation backing a GPU frame plane as an opaque handle; see
       VSVulkanExportedMemory for the identity, ownership and synchronization rules. Only
       available when VSVulkanCoreInfo::exportHandleType is nonzero. Fails on CPU frames,
       missing planes and devices without export support. */
    int (VS_CC *exportGPUPlane)(const VSFrame *frame, int plane, VSVulkanExportedMemory *out,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Exports a timeline semaphore as an opaque handle; see VSVulkanExportedSemaphore. Use
       it on the readySemaphore of a plane you consume (created exportable by every core
       exec pool when the capability exists), or on your own timeline created with
       VkExportSemaphoreCreateInfo, to signal your producer pairs from the foreign API. When
       the export of a third party producer's semaphore fails because it was not created
       exportable, fall back to waitGPUFrame for that frame. Only available when
       VSVulkanCoreInfo::semaphoreExportHandleType is nonzero. */
    int (VS_CC *exportGPUSemaphore)(VSCore *core, VkSemaphore semaphore, VSVulkanExportedSemaphore *out,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Host waits every plane's producer pair AND makes the completed writes available
       outside the device's own domain, which is the part a bare producer wait does not give
       you: the spec only defines cross device visibility through external semaphores or an
       availability chain like this one, so skipping it means relying on driver behavior.
       Call once per frame before reading it through an exported handle; the cost is one
       submission round trip. Frames only read through Vulkan on the same device never need
       this — the producer pairs carry the dependency there. */
    int (VS_CC *waitGPUFrame)(const VSFrame *frame, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
};

#endif
