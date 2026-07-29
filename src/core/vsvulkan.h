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

#ifndef VSVULKAN_H
#define VSVULKAN_H

/* Every entry point is reached through a pointer in VSVulkanFunctions, so the statically linked
   prototypes are deliberately not declared. Calling one by its bare name is then a link error
   rather than something that silently works on the build machine and not elsewhere. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <string>

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
   call at runtime rather than a compile error. */
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
       needs. Support is per format, so query before relying on it for a given layout. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyMemoryToImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyImageToMemory) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CopyImageToImage) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, TransitionImageLayout) \
    \
    /* ---- Samplers ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateSampler) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroySampler) \
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
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, AllocateCommandBuffers) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, FreeCommandBuffers) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, BeginCommandBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, EndCommandBuffer) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetCommandBuffer) \
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
    \
    /* ---- Timestamp queries. Only ever called when profiling is on, but they are core, so a \
       missing one is as fatal as any other. ---- */ \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CreateQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, DestroyQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, GetQueryPoolResults) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, ResetQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdResetQueryPool) \
    FN(VS_VK_DEVICE,   VS_VK_REQUIRED, CmdWriteTimestamp2) \
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
struct VSVulkanFunctions {
#define VS_VK_DECLARE_MEMBER(level, req, name) PFN_vk##name vk##name = nullptr;
    VS_VK_FUNCTION_LIST(VS_VK_DECLARE_MEMBER)
#undef VS_VK_DECLARE_MEMBER
};

/* Owns whatever the entry points were reached through, and the entry points themselves. A device
   object will hold one of these; nothing here knows about instances or devices beyond needing the
   handles passed in, so it can be built and tested on its own. */
class VSVulkanLoader {
public:
    VSVulkanLoader() = default;
    ~VSVulkanLoader();
    VSVulkanLoader(const VSVulkanLoader &) = delete;
    VSVulkanLoader &operator=(const VSVulkanLoader &) = delete;

    /* Opens the platform loader and resolves the global entry points. */
    bool initialize(std::string &errorMessage);

    /* The same, but reusing an entry point supplied from outside. This is the hook that lets
       VapourSynth share a device with a host application that already owns a Vulkan instance,
       rather than creating a second one on the same GPU. */
    bool initialize(PFN_vkGetInstanceProcAddr getInstanceProcAddr, std::string &errorMessage);

    /* Called once the instance exists, then once the device does. Splitting them is not optional:
       instance level entry points cannot be resolved before there is an instance, and device
       level ones resolved through the instance would go through the dispatch trampoline. */
    bool loadInstance(VkInstance instance, std::string &errorMessage);
    bool loadDevice(VkDevice device, std::string &errorMessage);

    const VSVulkanFunctions &functions() const { return vk; }
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const { return getInstanceProcAddrFn; }

    /* Reports what the loader itself supports, which bounds what an instance may ask for.
       A device reports its own, possibly lower, version separately. */
    bool checkInstanceVersion(uint32_t required, uint32_t &found, std::string &errorMessage) const;

private:
    void closeLibrary();

    VSVulkanFunctions vk;
    PFN_vkGetInstanceProcAddr getInstanceProcAddrFn = nullptr;
    void *library = nullptr;     /* null when the entry point came from outside */
};

#endif
