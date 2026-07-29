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

#include <mutex>
#include <string>
#include <vector>

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

/* Features switched on at device creation. One list expanded both into the availability check
   and into the enable chain so the two cannot drift apart; the number picks which
   VkPhysicalDeviceVulkanNNFeatures struct the member lives in. A required feature missing makes
   the device unusable; an optional one is enabled when present and exposed as a capability.

   hostImageCopy is optional the hard way: promotion to core does not make a feature mandatory,
   and AMD's Windows driver reports it unsupported even at loader version 1.4.3xx, so the
   transfer path has to treat staging buffers as the baseline and this as the fast path. */
#define VS_VK_FEATURE_LIST(FT) \
    FT(12, timelineSemaphore,  VS_VK_REQUIRED) \
    FT(12, bufferDeviceAddress, VS_VK_REQUIRED) \
    FT(12, scalarBlockLayout,  VS_VK_REQUIRED) \
    FT(12, hostQueryReset,     VS_VK_REQUIRED) \
    FT(13, synchronization2,   VS_VK_REQUIRED) \
    FT(13, maintenance4,       VS_VK_REQUIRED) \
    FT(14, maintenance5,       VS_VK_REQUIRED) \
    FT(14, maintenance6,       VS_VK_REQUIRED) \
    FT(14, hostImageCopy,      VS_VK_OPTIONAL) \
    FT(14, pushDescriptor,     VS_VK_REQUIRED)

enum VSVulkanLogSeverity {
    VS_VK_LOG_INFO = 0,
    VS_VK_LOG_WARNING = 1,
    VS_VK_LOG_ERROR = 2
};

/* Deliberately a bare function pointer with a context so it can later be forwarded from the C API
   without an adapter. */
typedef void (*VSVulkanLogFn)(int severity, const char *message, void *userData);

/* What enumerateDevices() reports per physical device, mainly so a frontend can present the
   choice. The reason string is filled in when a device is unusable and says which requirement it
   failed first. */
struct VSVulkanDeviceInfo {
    std::string name;
    uint32_t apiVersion = 0;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    VkDeviceSize deviceLocalMemory = 0; /* largest device local heap, which is the VRAM size */
    bool usable = false;
    std::string reason;
};

/* Everything a host application has to hand over for VapourSynth to run on a device the host
   already created instead of opening a second one on the same GPU. The device must have been
   created with at least the features in VS_VK_FEATURE_LIST enabled; availability is verified at
   adoption but enablement cannot be queried after the fact, so that part is on the host.

   When the host keeps using the queues it shares, it must supply lockQueue/unlockQueue and take
   the same lock around its own submissions, since VkQueue is externally synchronized. */
struct VSVulkanDeviceImport {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;
    uint32_t computeQueueIndex = 0;
    uint32_t transferQueueFamily = UINT32_MAX; /* UINT32_MAX shares the compute queue */
    uint32_t transferQueueIndex = 0;
    void (*lockQueue)(void *context, uint32_t family, uint32_t index) = nullptr;
    void (*unlockQueue)(void *context, uint32_t family, uint32_t index) = nullptr;
    void *queueLockContext = nullptr;
};

/* A buffer and the memory backing it, freed with destroyBuffer. Ownership is deliberately
   explicit rather than RAII: frame pooling and cache accounting will want to manage these
   lifetimes themselves later, and a device pointer per buffer would only get in the way.
   Host visible buffers stay persistently mapped from creation to destruction. */
struct VSVulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;      /* nonzero when created with SHADER_DEVICE_ADDRESS usage */
    void *mapped = nullptr;           /* nonnull when the memory ended up host visible */
    VkDeviceSize size = 0;
    VkMemoryPropertyFlags memoryFlags = 0; /* what the chosen memory type actually provides */
};

/* An optimally tiled device local image and its memory, freed with destroyImage. Layout
   transitions are the caller's business since they belong to command recording. */
struct VSVulkanImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

/* One queue plus whatever serializes access to it. vkQueueSubmit is externally synchronized and
   filters run fmParallel, submitting from many threads at once, so taking the lock around every
   submission is mandatory, not defensive. The interface is BasicLockable on purpose: submission
   sites take a std::lock_guard on the queue itself, and whether that lands in the internal mutex
   or in a host application's callback is invisible to them. */
class VSVulkanQueue {
    friend class VSVulkanDevice;
public:
    VkQueue handle() const { return queue; }
    uint32_t familyIndex() const { return family; }
    uint32_t queueIndex() const { return index; }

    void lock() {
        if (lockFn)
            lockFn(lockContext, family, index);
        else
            mutex.lock();
    }

    void unlock() {
        if (unlockFn)
            unlockFn(lockContext, family, index);
        else
            mutex.unlock();
    }

private:
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    uint32_t index = 0;
    void (*lockFn)(void *context, uint32_t family, uint32_t index) = nullptr;
    void (*unlockFn)(void *context, uint32_t family, uint32_t index) = nullptr;
    void *lockContext = nullptr;
    std::mutex mutex;
};

/* Owns (or borrows) one Vulkan device and everything needed to reach it: the entry points, the
   instance, the chosen queues and the cached properties later allocation decisions read. The
   eventual home is one of these per GPU on the core.

   Like the loader it is single shot: a failed create() or adopt() leaves the object permanently
   dead and the caller starts over with a fresh one. GPU support either comes up or it does not;
   there is nothing sensible to retry with the same object. */
class VSVulkanDevice {
private:
    /* Declared before the public reference below so vk can bind to it in the initializer list. */
    VSVulkanLoader loader;

public:
    VSVulkanDevice() : vk(loader.functions()) {}
    ~VSVulkanDevice();
    VSVulkanDevice(const VSVulkanDevice &) = delete;
    VSVulkanDevice &operator=(const VSVulkanDevice &) = delete;

    /* Every Vulkan call goes through here, as dev->vk.vkCmdDispatch(...). */
    const VSVulkanFunctions &vk;

    /* Must be set before create() for validation and driver messages to go anywhere. */
    void setLogCallback(VSVulkanLogFn callback, void *userData) {
        logFn = callback;
        logUserData = userData;
    }

    /* Opens the platform loader, creates an instance and picks a physical device: the given index
       into the enumeration order, or with -1 the first suitable discrete GPU falling back to any
       suitable device. enableValidation asks for the Khronos validation layer and a debug
       messenger routed to the log callback, degrading with a warning when the layer is not
       installed since it is a development tool that may legitimately be absent. */
    bool create(int physicalDeviceIndex, bool enableValidation, std::string &errorMessage);

    /* Runs on a host application's existing device instead. Nothing is owned afterwards: the
       destructor will not touch any of the imported handles, and the host must keep them alive
       for as long as this object exists. */
    bool adopt(const VSVulkanDeviceImport &import, std::string &errorMessage);

    /* Lists every physical device the loader can see, usable or not, for frontends that let the
       user pick. Self-contained: uses its own temporary instance. */
    static bool enumerateDevices(std::vector<VSVulkanDeviceInfo> &devices, std::string &errorMessage);

    VkInstance instance() const { return instanceHandle; }
    VkPhysicalDevice physicalDevice() const { return physicalDeviceHandle; }
    VkDevice device() const { return deviceHandle; }
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const { return loader.getInstanceProcAddr(); }
    bool isOwned() const { return owned; }

    /* Only meaningful once create() or adopt() has succeeded. */
    const VkPhysicalDeviceProperties &properties() const { return props; }
    const VkPhysicalDeviceMemoryProperties &memoryProperties() const { return memProps; }

    VSVulkanQueue &computeQueue() { return computeQ; }
    /* The same object as computeQueue() when the device has no dedicated transfer family, so
       locking stays correct without the caller caring which case it is in. */
    VSVulkanQueue &transferQueue() { return *transferPtr; }
    bool hasDedicatedTransferQueue() const { return transferPtr != &computeQ; }

    /* Whether uploads and downloads may go through vkCopyMemoryToImage and friends, still
       subject to the per format queries. On an adopted device this reports availability on the
       physical device, and actually enabling the feature there is part of the host's side of
       the bargain. */
    bool hostImageCopySupported() const { return hostImageCopyFlag; }

    /* First pass wants required plus preferred, second pass settles for required. Returns
       UINT32_MAX when nothing qualifies. */
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;

    /* Creation, backing allocation and binding in one step. Resources are shared concurrently
       between the compute and transfer families when those differ, trading a sliver of
       throughput for never having to record queue family ownership transfers. */
    bool createBuffer(VSVulkanBuffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, std::string &errorMessage);
    void destroyBuffer(VSVulkanBuffer &buffer);
    bool createImage2D(VSVulkanImage &image, VkFormat format, uint32_t width, uint32_t height,
        VkImageUsageFlags usage, std::string &errorMessage);
    void destroyImage(VSVulkanImage &image);

private:
    enum class State { Unused, Ready, Failed };

    void teardown();
    void emitLog(int severity, const std::string &message) const;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerTrampoline(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData);

    State state = State::Unused;
    bool owned = false;
    VkInstance instanceHandle = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDeviceHandle = VK_NULL_HANDLE;
    VkDevice deviceHandle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props = {};
    VkPhysicalDeviceMemoryProperties memProps = {};
    VSVulkanQueue computeQ;
    VSVulkanQueue transferQ;
    VSVulkanQueue *transferPtr = &computeQ;
    bool hostImageCopyFlag = false;
    VSVulkanLogFn logFn = nullptr;
    void *logUserData = nullptr;
};

#endif
