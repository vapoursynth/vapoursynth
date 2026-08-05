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

/* The device feature baseline. Every VapourSynth device is created by the core itself:
 * Vulkan 1.4 with exactly the features below enabled, and no device extensions except the
 * platform's opaque handle export extension (VK_KHR_external_memory_win32 or _fd) when
 * available, which is never load-bearing. The required set is what plugin kernels may target
 * unconditionally, and since every entry is mandatory for a conformant Vulkan 1.4
 * implementation, the hardware gate is the version alone. Sharing frames with other Vulkan
 * devices or APIs in the same process goes through exportGPUPlane, not device sharing.
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
 *   optional, used when enabled: shaderFloat16 */

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
 * This is why a filter's timeline no longer has to outlive its consumers: release your reference
 * whenever you are done signalling -- the free callback is the natural place -- and any frame
 * still in flight keeps the semaphore alive on its own. Frames legitimately outlive the filter
 * that made them (FrameEval and ModifyFrame hand one back from a node they then drop, and the
 * cache can hold one indefinitely), which is exactly the case this counting exists for.
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

/* A frame plane's backing memory exported as an opaque handle, so CUDA and other Vulkan
 * devices in the same process can wrap the underlying allocation and read or write the plane
 * zero copy (cudaImportExternalMemory + cudaExternalMemoryGetMappedBuffer, or a second
 * device's buffer bound to imported memory). The plane lives at offset within an allocation
 * of memorySize bytes; importers import the whole allocation once and address planes by
 * offset.
 *
 * memoryId identifies the underlying allocation: it is stable for its whole lifetime and
 * never reused by this core's device, while every export call returns a NEW handle even for
 * the same allocation — so cache imports keyed by memoryId, never by handle value, and close
 * surplus handles. Handle ownership: the returned handle belongs to the caller. On Windows it
 * is an NT handle; neither Vulkan nor CUDA import takes ownership, so CloseHandle it once the
 * import exists. A POSIX fd is consumed by a successful import (both Vulkan and CUDA), and
 * must only be closed by the caller when the import failed or never happened.
 *
 * The OS reference-counts the underlying memory: an imported allocation stays valid even
 * after every VapourSynth side reference is gone, so a cached import can outlive the frames
 * (and, like frames, the core) that led to it. Synchronization is host side for now: call
 * waitGPUFrame before reading a frame through an import (a bare producer pair wait is NOT
 * enough — see there), and finish foreign writes (for example cudaStreamSynchronize) before
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
    /* The pooled allocator's internals, mirroring what its failure message reports:
       allocated above is the driver-committed block total; liveBytes is what is carved out
       and in use inside those blocks, and freeRegions counts the recycled regions banked in
       the free lists. allocated minus liveBytes is what suballocation and banking hold. */
    int64_t liveBytes;
    int64_t freeRegions;
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
    /* Device selection, only before the device is first used; -1 picks the most powerful one.
       All int returning functions here return 0 on success and fill errorMessage otherwise. */
    int (VS_CC *setVulkanDevice)(VSCore *core, int deviceIndex, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

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
    void (VS_CC *setGPUPlaneProducer)(VSFrame *frame, int plane, VSGPUTimeline *timeline, uint64_t value) VS_NOEXCEPT; /* the plane takes its own reference; NULL publishes the plane as host ready */

    /* Lists every physical device through a temporary instance, so it works before any device
       selection and needs no core. Returns the total device count, which may exceed
       maxEntries, or -1 with the error set; entries and maxEntries 0 just count. */
    int (VS_CC *enumerateVulkanDevices)(VSVulkanDeviceListEntry *entries, int maxEntries, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* The core's ready loaded dispatch table, the normal way for filters to call Vulkan; the
       handles' getInstanceProcAddr stays available for anything outside the curated set.
       Brings the device up on first call and stays valid for the core's lifetime. */
    const VSVulkanFunctions *(VS_CC *getVulkanFunctions)(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

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

    /* Exports the allocation backing a GPU frame plane as an opaque handle; see
       VSVulkanExportedMemory for the identity, ownership and synchronization rules. Only
       available when VSVulkanCoreInfo::exportHandleType is nonzero. Fails on CPU frames,
       missing planes and devices without export support. */
    int (VS_CC *exportGPUPlane)(const VSFrame *frame, int plane, VSVulkanExportedMemory *out,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Host waits every plane's producer pair AND makes the completed writes available
       outside the device's own domain, which is the part a bare producer wait does not give
       you: the spec only defines cross device visibility through external semaphores or an
       availability chain like this one, so skipping it means relying on driver behavior.
       Call once per frame before reading it through an exported handle; the cost is one
       submission round trip. Frames only read through Vulkan on the same device never need
       this — the producer pairs carry the dependency there. */
    int (VS_CC *waitGPUFrame)(const VSFrame *frame, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* Exports a timeline semaphore as an opaque handle; see VSVulkanExportedSemaphore. Use
       it on the readySemaphore of a plane you consume (created exportable by every core
       exec pool when the capability exists), or on your own timeline created with
       VkExportSemaphoreCreateInfo, to signal your producer pairs from the foreign API. When
       the export of a third party producer's semaphore fails because it was not created
       exportable, fall back to waitGPUFrame for that frame. Only available when
       VSVulkanCoreInfo::semaphoreExportHandleType is nonzero. */
    int (VS_CC *exportGPUSemaphore)(VSCore *core, VkSemaphore semaphore, VSVulkanExportedSemaphore *out,
        char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

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

    /* Creates an exec pool on one of the core's queues. The core sizes the pool's context
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

    /* Ends recording and submits, allocating the timeline value inside the queue lock so
       signals reach the queue in increasing order, then publishes the producer pairs. The
       context is consumed either way. Returns nonzero with the error set on failure. */
    int (VS_CC *gpuExecSubmit)(VSGPUExecContext *context, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    /* Gives up a recording without submitting: everything retained is released at once. */
    void (VS_CC *gpuExecAbandon)(VSGPUExecContext *context) VS_NOEXCEPT;

    /* The pool's timeline, for exportGPUSemaphore or for publishing producer pairs by
       hand on frames the pool does not know about. */
    VkSemaphore (VS_CC *gpuExecPoolSemaphore)(VSGPUExecPool *pool) VS_NOEXCEPT;

    /* Blocks until every submission made through this pool has completed. Filters do not
       need this per frame — producer pairs make consumers wait on the device instead — but
       one shot setup work, such as uploading weights or tables a filter will read for the
       rest of its life, has to know the copy landed before recording anything that reads
       it. Also releases everything those submissions were keeping alive. */
    int (VS_CC *gpuExecPoolWaitIdle)(VSGPUExecPool *pool, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;

    /* The same timeline as gpuExecPoolSemaphore, as the counted object setGPUPlaneProducer
       takes. The pool holds its own reference, so publishing it needs no reference of yours. */
    VSGPUTimeline *(VS_CC *gpuExecPoolTimeline)(VSGPUExecPool *pool) VS_NOEXCEPT;

    /* A timeline of your own, for filters recording and submitting without the core's exec
       pool. Created with an initial value of 0, exportable where the device allows it, and
       returned with one reference which is yours to release -- in the free callback, without
       waiting for consumers, since planes you published it on hold their own. Returns NULL
       with the error set. */
    VSGPUTimeline *(VS_CC *createGPUTimeline)(VSCore *core, char *errorMessage, int errorMessageSize) VS_NOEXCEPT;
    void (VS_CC *freeGPUTimeline)(VSGPUTimeline *timeline) VS_NOEXCEPT;
    /* Takes another reference, for handing the same timeline to something with its own
       lifetime. Every added reference needs a matching freeGPUTimeline. */
    void (VS_CC *addGPUTimelineRef)(VSGPUTimeline *timeline) VS_NOEXCEPT;
    /* The raw handle, to signal in your own vkQueueSubmit and to pass to exportGPUSemaphore.
       Valid for as long as you hold a reference. */
    VkSemaphore (VS_CC *getGPUTimelineSemaphore)(VSGPUTimeline *timeline) VS_NOEXCEPT;
};

#endif
