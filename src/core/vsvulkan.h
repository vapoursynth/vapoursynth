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

#include "VSVulkan4.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    /* The same, but reusing an entry point supplied from outside instead of opening the
       platform loader, which is what lets the test harness drive the whole loader against
       fake function pointers. */
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

    /* Value initialized since the public struct, being C, cannot default its members. */
    VSVulkanFunctions vk = {};
    PFN_vkGetInstanceProcAddr getInstanceProcAddrFn = nullptr;
    void *library = nullptr;     /* null when the entry point came from outside */
};

/* Features switched on at device creation. One list expanded both into the availability check
   and into the enable chain so the two cannot drift apart; the number picks which
   VkPhysicalDeviceVulkanNNFeatures struct the member lives in. A required feature missing makes
   the device unusable; an optional one is enabled when present and exposed as a capability.

   Every REQUIRED entry is mandatory for a conformant implementation of the Vulkan version
   noted beside it, so requiring them costs no hardware beyond the 1.4 gate itself. The list is
   maximal within that rule, being the baseline plugin kernels may target as published in
   VSVulkan4.h; additions after release need an API version bump plugins can gate on. It covers
   the mandatory features a compute plugin can consume, as SPIR-V capabilities or through the
   exposed API, and omits internal conveniences and graphics-pipeline state.

   The OPTIONAL entries -- shaderFloat16, shaderInt64, shaderFloat64 and the two int64 atomic
   features -- are optional in every core version, so a kernel wanting one asks the physical
   device rather than the 1.4 gate; per the documented policy, what the device reports is what
   got enabled.

   hostImageCopy is deliberately absent: it exists to avoid a staging allocation, not to move
   bulk linear data, and measured 17-26x slower than the path in use on two Metal drivers
   because the image is optimally tiled and the driver swizzles on the CPU. */
#define VS_VK_FEATURE_LIST(FT) \
    /* 10 is the plain VkPhysicalDeviceFeatures block; everything here became mandatory in
       1.4. The 8 and 16 bit storage and arithmetic features let kernels address planes as
       their real sample type; the dynamic indexing quartet and the image features are SPIR-V
       capabilities kernels may declare. */ \
    FT(10, shaderInt16,        VS_VK_REQUIRED) \
    FT(10, shaderUniformBufferArrayDynamicIndexing, VS_VK_REQUIRED) \
    FT(10, shaderSampledImageArrayDynamicIndexing,  VS_VK_REQUIRED) \
    FT(10, shaderStorageBufferArrayDynamicIndexing, VS_VK_REQUIRED) \
    FT(10, shaderStorageImageArrayDynamicIndexing,  VS_VK_REQUIRED) \
    FT(10, shaderImageGatherExtended, VS_VK_REQUIRED) \
    FT(10, shaderStorageImageExtendedFormats, VS_VK_REQUIRED) \
    /* Mandatory in 1.4. Variable pointers because DXC-generated SPIR-V leans on them, the
       ycbcr conversion because the function table already carries the object pair. */ \
    FT(11, storageBuffer16BitAccess, VS_VK_REQUIRED) \
    FT(11, variablePointersStorageBuffer, VS_VK_REQUIRED) \
    FT(11, variablePointers,   VS_VK_REQUIRED) \
    FT(11, samplerYcbcrConversion, VS_VK_REQUIRED) \
    /* Mandatory in 1.2... */ \
    FT(12, timelineSemaphore,  VS_VK_REQUIRED) \
    FT(12, hostQueryReset,     VS_VK_REQUIRED) \
    FT(12, uniformBufferStandardLayout, VS_VK_REQUIRED) \
    FT(12, shaderSubgroupExtendedTypes, VS_VK_REQUIRED) \
    FT(12, subgroupBroadcastDynamicId, VS_VK_REQUIRED) \
    /* ...in 1.3... */ \
    FT(12, bufferDeviceAddress, VS_VK_REQUIRED) \
    FT(12, vulkanMemoryModel,  VS_VK_REQUIRED) \
    FT(12, vulkanMemoryModelDeviceScope, VS_VK_REQUIRED) \
    /* ...and in 1.4. */ \
    FT(12, storageBuffer8BitAccess,  VS_VK_REQUIRED) \
    FT(12, shaderInt8,         VS_VK_REQUIRED) \
    FT(12, scalarBlockLayout,  VS_VK_REQUIRED) \
    FT(12, shaderFloat16,      VS_VK_OPTIONAL) \
    /* 64 bit integer arithmetic is universal on desktop devices and lets ported CUDA/HIP
       kernels keep size_t shaped arguments; optional like shaderFloat16, so a plugin that
       needs it checks the physical device. Lives in the plain features block, exposed
       through the 1.1 features2 chain like the rest of this table. shaderFloat64 likewise:
       niche for video work but free to pass through, and the float64 atomic bits below are
       unusable without the Float64 capability it grants. */ \
    FT(10, shaderInt64,        VS_VK_OPTIONAL) \
    FT(10, shaderFloat64,      VS_VK_OPTIONAL) \
    /* The int64 atomic pair, promoted into the 1.2 block from VK_KHR_shader_atomic_int64 and
       optional in every core version. Enabled when reported purely for plugins targeting
       hardware they know; nothing in the tree uses 64 bit atomics, Apple hardware never
       supports them and Intel only sometimes, so portable kernels keep avoiding them. */ \
    FT(12, shaderBufferInt64Atomics, VS_VK_OPTIONAL) \
    FT(12, shaderSharedInt64Atomics, VS_VK_OPTIONAL) \
    /* Mandatory in 1.3. subgroupSizeControl/computeFullSubgroups because kernels built
       around a fixed subgroup size (one subgroup per work item is a common GPU filter
       shape) must pin it at pipeline creation or wave size variance silently breaks them. */ \
    FT(13, synchronization2,   VS_VK_REQUIRED) \
    FT(13, maintenance4,       VS_VK_REQUIRED) \
    FT(13, subgroupSizeControl, VS_VK_REQUIRED) \
    FT(13, computeFullSubgroups, VS_VK_REQUIRED) \
    FT(13, shaderIntegerDotProduct, VS_VK_REQUIRED) \
    FT(13, shaderZeroInitializeWorkgroupMemory, VS_VK_REQUIRED) \
    FT(13, inlineUniformBlock, VS_VK_REQUIRED) \
    FT(13, pipelineCreationCacheControl, VS_VK_REQUIRED) \
    FT(13, privateData,        VS_VK_REQUIRED) \
    /* Mandatory in 1.4. */ \
    FT(14, maintenance5,       VS_VK_REQUIRED) \
    FT(14, maintenance6,       VS_VK_REQUIRED) \
    FT(14, pushDescriptor,     VS_VK_REQUIRED) \
    FT(14, shaderSubgroupRotate, VS_VK_REQUIRED) \
    FT(14, shaderSubgroupRotateClustered, VS_VK_REQUIRED) \
    FT(14, shaderFloatControls2, VS_VK_REQUIRED) \
    FT(14, shaderExpectAssume, VS_VK_REQUIRED) \
    FT(14, pipelineRobustness, VS_VK_REQUIRED)

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
    uint8_t uuid[VK_UUID_SIZE] = {};
    uint8_t luid[VK_LUID_SIZE] = {};
    uint32_t nodeMask = 0;
    bool luidValid = false;
};

class VSVulkanDevice;
class VSVulkanExecPool;

struct VSVulkanAllocatorStats {
    uint64_t blockCount = 0;
    uint64_t blockBytes = 0;      /* VRAM reserved from the driver */
    uint64_t usedBytes = 0;       /* handed out to live buffers, rounding included */
    uint64_t freeRegionCount = 0; /* recycled regions waiting in the buckets */
};

/* Sub allocates plane memory out of big blocks: one vkAllocateMemory per plane would hit
   maxMemoryAllocationCount, only 4096, after a few hundred cached frames. Blocks are carved
   with a bump pointer and freed regions go into free lists bucketed by exact rounded size --
   video workloads reuse the same handful of plane sizes, so exact reuse nearly always hits and
   no coalescing is needed. A block returns to the driver only once every region in it is free,
   in trim() or destroy(); MemoryUse accounts blockBytes, the driver's budget being spent on
   blocks rather than the regions carved from them.

   Only buffers live here, which is what makes ignoring bufferImageGranularity legal; images
   keep their dedicated allocations. */
class VSVulkanAllocator {
public:
    struct Block {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        VkDeviceSize used = 0;
        void *mapped = nullptr; /* whole block, when the type is host visible */
        uint32_t typeIndex = 0;
        uint32_t liveRegions = 0;
        /* Monotonic per device and never reused, so importers on the other side of an
           exported handle can cache one import per allocation and trust the identity across
           block free/allocate cycles. Assigned whether or not the block is exportable. */
        uint64_t exportId = 0;
        bool exportable = false;
    };

    /* Exportable blocks and plain blocks never mix, matching the bind rule that couples
       VkExternalMemoryBufferCreateInfo on the buffer with VkExportMemoryAllocateInfo on the
       memory in both directions; the free lists segregate on the same flag. */
    bool allocate(VSVulkanDevice &dev, uint32_t typeIndex, VkDeviceSize size, VkDeviceSize alignment,
        bool exportable, Block *&block, VkDeviceSize &offset, VkDeviceSize &roundedSize, std::string &errorMessage);
    /* No device needed: returning a region only moves it into a bucket, and the memory it
       came from goes back to the driver in trim() or destroy() instead. */
    void free(Block *block, VkDeviceSize offset, VkDeviceSize roundedSize);
    /* Hands every block with no live regions back to the driver, called under memory pressure
       after cache eviction so the reclaimed VRAM is real for the rest of the system rather
       than banked in the free lists forever. Returns the bytes given back. */
    VkDeviceSize trim(VSVulkanDevice &dev);
    /* Frees every block; all buffers carved from them must already be gone. */
    void destroy(VSVulkanDevice &dev);
    VSVulkanAllocatorStats stats() const;

private:
    VkDeviceSize trimLocked(VSVulkanDevice &dev);

    std::vector<std::unique_ptr<Block>> blocks;
    /* (memory type + exportable bit, rounded size) -> reusable regions */
    std::map<std::pair<uint64_t, VkDeviceSize>, std::vector<std::pair<Block *, VkDeviceSize>>> freeLists;
    uint64_t usedBytes = 0;
    uint64_t freeRegions = 0;
    uint64_t nextExportId = 1;
    mutable std::mutex mutex;
};

/* One region taken from the allocator. offset/size are the region itself and are what gives
   it back; usableOffset is where the resource actually binds, which differs only when the
   resource wanted an alignment coarser than a region -- see VSVulkanDevice::allocatePooled. */
struct VSVulkanPooledRegion {
    VSVulkanAllocator::Block *block = nullptr;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    VkDeviceSize usableOffset = 0;
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
    /* Set when the memory came from the device's block allocator instead of its own
       vkAllocateMemory; destroyBuffer routes on it. */
    VSVulkanAllocator::Block *poolBlock = nullptr;
    VkDeviceSize poolOffset = 0;
    VkDeviceSize poolSize = 0;
    /* Where the buffer is actually bound inside the block, which is poolOffset unless the
       resource wanted an alignment coarser than a region. Anything naming the buffer's
       bytes in its allocation -- the memory export path -- must use this, not poolOffset. */
    VkDeviceSize poolBindOffset = 0;
};

/* The public handle from VSVULKANAPI::createGPUBuffer: a pooled buffer plus the device
   reference that keeps the allocator reachable for however late the destroy comes. */
struct VSGPUBuffer {
    VSVulkanBuffer buffer;
    VSVulkanDevice *device = nullptr;
};

/* The public bare region handle from VSVULKANAPI::allocateGPUMemory: what the allocator needs
   back to return the region, plus the device reference keeping it reachable. */
struct VSGPUMemory {
    VSVulkanPooledRegion region;
    VSVulkanDevice *device = nullptr;
};

/* The public handle from VSVULKANAPI::reserveGPUMemory: the published byte total and the
   references keeping the accounting reachable. The core pointer is touched only on increases,
   which a live filter performs while the core is necessarily alive; a late release goes through
   the device's accounting callback, which outlives the core by design. */
struct VSGPUMemoryReservation {
    std::atomic<int64_t> bytes{ 0 };
    VSVulkanDevice *device = nullptr;
    VSCore *core = nullptr;
};

/* One queue plus the mutex that serializes access to it. vkQueueSubmit is externally
   synchronized and filters run fmParallel, submitting from many threads at once, so taking
   the lock around every submission is mandatory, not defensive. The interface is
   BasicLockable on purpose: submission sites take a std::lock_guard on the queue itself. */
class VSVulkanQueue {
    friend class VSVulkanDevice;
public:
    VkQueue handle() const { return queue; }
    uint32_t familyIndex() const { return family; }
    uint32_t queueIndex() const { return index; }

    void lock() {
        mutex.lock();
    }

    void unlock() {
        mutex.unlock();
    }

private:
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    uint32_t index = 0;
    std::mutex mutex;
};

/* Owns one Vulkan device and everything needed to reach it: the entry points, the instance,
   the chosen queues and the cached properties later allocation decisions read.

   Like the loader it is single shot: a failed create() leaves the object permanently dead
   and the caller starts over with a fresh one. GPU support either comes up or it does not;
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

    /* Must be set before create() for validation and driver messages to go anywhere. The
       pair is atomics so onCoreFreed can retract it while driver threads may still emit:
       userData is written first and read last, so any reader that observes a function also
       observes the userData that belongs to it. */
    void setLogCallback(VSVulkanLogFn callback, void *userData) {
        logUserData.store(userData);
        logFn.store(callback);
    }

    /* Opens the platform loader, creates an instance and picks a physical device: the given index
       into the enumeration order, or with -1 the first suitable discrete GPU falling back to any
       suitable device. enableValidation asks for the Khronos validation layer and a debug
       messenger routed to the log callback, degrading with a warning when the layer is not
       installed since it is a development tool that may legitimately be absent. */
    bool create(int physicalDeviceIndex, bool enableValidation, std::string &errorMessage);

    /* Lists every physical device the loader can see, usable or not, for frontends that let the
       user pick. Self-contained: uses its own temporary instance. */
    static bool enumerateDevices(std::vector<VSVulkanDeviceInfo> &devices, std::string &errorMessage);

    VkInstance instance() const { return instanceHandle; }
    VkPhysicalDevice physicalDevice() const { return physicalDeviceHandle; }
    VkDevice device() const { return deviceHandle; }
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const { return loader.getInstanceProcAddr(); }

    /* GPU frames may legally outlive the core, the same contract CPU frames have through
       MemoryUse: the core holds one reference and every GPU resident plane holds another, so
       the device and its allocator stay up until the last surviving plane returns its buffer.
       The counting is opt-in — direct ownership (stack objects in the test harnesses, the
       unique_ptr during core setup) keeps working because nothing calls release() on those. */
    void addRef() {
        refs.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }
    /* Called from the core destructor only, after every pool and the transfer machinery are
       gone, which is what makes the flag a promise: all submissions have completed, and the
       timeline semaphores surviving planes would wait on died with their pools, so plane
       destruction must skip the producer wait instead of touching dead handles. Log routing
       points into the core and is dropped here; teardown messages after this go nowhere. */
    void onCoreFreed() {
        coreFreedFlag.store(true, std::memory_order_release);
        /* Function first, then its context: a reader that still saw the function cannot have
           seen the context nulled yet, so the pair it calls with is always consistent. */
        logFn.store(nullptr);
        logUserData.store(nullptr);
        release();
    }
    bool coreFreed() const {
        return coreFreedFlag.load(std::memory_order_acquire);
    }

    /* Only meaningful once create() has succeeded. */
    const VkPhysicalDeviceProperties &properties() const { return props; }
    const VkPhysicalDeviceMemoryProperties &memoryProperties() const { return memProps; }

    VSVulkanQueue &computeQueue() { return computeQ; }
    /* The same object as computeQueue() when the device has no dedicated transfer family, so
       locking stays correct without the caller caring which case it is in. */
    VSVulkanQueue &transferQueue() { return *transferPtr; }
    bool hasDedicatedTransferQueue() const { return transferPtr != &computeQ; }

    /* Whether kernels may declare float16_t, needed for half precision plane formats. */
    bool shaderFloat16Supported() const { return shaderFloat16Flag; }

    /* Whether the device's memory is the host's memory. Integrated and software devices
       carve their heaps out of system RAM, so a VRAM limit and the host memory limit are
       two claims on the same bytes and cannot be sized independently. Taken from the device
       type rather than from the heaps: a discrete card with resizable BAR also reports a
       host visible device local heap, which would make any heap based test say yes. */
    bool unifiedMemory() const { return unifiedMemoryFlag; }

    /* The opaque handle type pooled memory can be exported as (OPAQUE_WIN32 or OPAQUE_FD),
       or 0 when the platform extension is absent or export of our buffer shape is not
       possible. When nonzero, pooled buffers that REQUIRE device local memory (every frame
       plane) are created exportable and land in exportable blocks; host visible pools stay
       plain, because external memory info restricts a buffer's compatible memory types and
       drivers may not offer exportable host visible ones at all. */
    VkExternalMemoryHandleTypeFlagBits exportHandleType() const { return exportType; }

    /* Wins a new handle to the memory's underlying allocation. Every call returns a fresh
       handle to the same memory; callers dedup by Block::exportId, never by handle value.
       Ownership rules differ per platform and are documented at the public API. */
    bool exportMemory(VkDeviceMemory memory, intptr_t &handle, std::string &errorMessage);

    /* The opaque handle type timeline semaphores can be exported as, or 0. When nonzero
       every exec pool timeline is created exportable, so the producer pairs of core produced
       frames can be imported by CUDA and other Vulkan devices for device side waits; plugin
       timelines opt in themselves at creation. Kept separate from memory export since driver
       support for the two differs. */
    VkExternalSemaphoreHandleTypeFlagBits semaphoreExportHandleType() const { return semaphoreExportType; }

    /* Same fresh-handle-per-call semantics as exportMemory. The semaphore must have been
       created exportable or the driver rejects the call. */
    bool exportSemaphore(VkSemaphore semaphore, intptr_t &handle, std::string &errorMessage);

    /* Makes writes available outside the device's own domain: one tiny submission that
       device-waits the given timeline pairs, then executes an ALL_COMMANDS/MEMORY_WRITE to
       HOST/HOST_READ barrier, host waited. The spec does not grant cross device availability
       implicitly, so this is what keeps foreign reads of exported memory defined rather than
       driver-behaviour. The waits must belong to THIS submission: an availability operation
       covers only writes in its first synchronization scope, so producer work on other queues
       has to be chained in by them. Serialized internally, and costs a submission round trip.
       AMD happened not to need it in testing; it exists for the drivers that will. */
    bool flushDeviceWrites(const VkSemaphore *waitSemaphores, const uint64_t *waitValues, uint32_t waitCount,
        std::string &errorMessage);

    /* Identity for CUDA and friends, from VkPhysicalDeviceIDProperties. */
    const uint8_t *deviceUUID() const { return uuid; }
    const uint8_t *deviceLUID() const { return luid; }
    uint32_t deviceNodeMask() const { return nodeMask; }
    bool deviceLUIDValid() const { return luidValid; }

    /* First pass wants required plus preferred, second pass settles for required. Returns
       UINT32_MAX when nothing qualifies. */
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;

    /* Creation, backing allocation and binding in one step. Resources are shared concurrently
       between the compute and transfer families when those differ, trading a sliver of
       throughput for never having to record queue family ownership transfers.

       The plain form gives the buffer its own vkAllocateMemory, right for the few big long
       lived staging buffers; the pooled form sub allocates from the block allocator and is
       what every frame plane uses. destroyBuffer handles both. */
    bool createBuffer(VSVulkanBuffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, std::string &errorMessage);
    bool createBufferPooled(VSVulkanBuffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, std::string &errorMessage);
    void destroyBuffer(VSVulkanBuffer &buffer);

    /* A pooled region with no resource wrapped around it: what createBufferPooled uses
       underneath and what allocateGPUMemory hands out. Non-exportable on the public path, the
       caller's resource carrying no external memory info; poolAllowsMixedResourceTypes says
       whether images may share these blocks with buffers at all. */
    bool allocatePooled(const VkMemoryRequirements &req, VkMemoryPropertyFlags requiredFlags,
        VkMemoryPropertyFlags preferredFlags, bool exportable, VSVulkanPooledRegion &region,
        std::string &errorMessage);
    void freePooled(const VSVulkanPooledRegion &region);
    bool poolAllowsMixedResourceTypes() const;

    VSVulkanAllocatorStats allocatorStats() const { return allocator.stats(); }

    VkDeviceSize trimAllocator() { return allocator.trim(*this); }

    /* Block grants and returns are reported here with signed byte deltas, which is how the
       core's MemoryUse sees VRAM without this layer depending on it. Blocks rather than the
       regions carved out of them because the budget is measured against what the driver has
       committed. Set before any pooled allocation happens. */
    typedef void (*VSVulkanAccountFn)(int64_t delta, void *userData);
    void setAllocationCallback(VSVulkanAccountFn callback, void *userData) {
        accountFn = callback;
        accountUserData = userData;
    }

    void accountAllocation(int64_t delta) const {
        if (accountFn)
            accountFn(delta, accountUserData);
    }

    /* Exec pools register so the memory pressure paths can reclaim their completed but
       unswept retentions — the scratch and source frames of submissions that finished but
       whose context was never acquired again. See VSVulkanExecPool::sweepCompleted. */
    void registerExecPool(VSVulkanExecPool *pool);
    void unregisterExecPool(VSVulkanExecPool *pool);
    void sweepExecPools();

    /* The in-flight retention budget. Per pool contextCount caps multiply across a graph's
       nodes while the GPU executes one submission at a time, so nothing else bounds how much
       queued work pins. acquire() blocks while the total exceeds the budget, sweeping and
       sleeping on the progress timeline every compute submission signals. Zero disables the
       gate; the core sets a quarter of the VRAM limit. */
    void setExecRetainedBudget(uint64_t bytes) { execRetainedBudget.store(bytes, std::memory_order_relaxed); }
    void addExecRetained(uint64_t bytes) { execRetainedBytes.fetch_add(bytes, std::memory_order_relaxed); }
    void subExecRetained(uint64_t bytes) { execRetainedBytes.fetch_sub(bytes, std::memory_order_relaxed); }
    void execAdmissionGate();
    bool ensureExecProgressSemaphore();
    VkSemaphore execProgressSemaphore() const { return execProgressSem; }

    friend class VSVulkanExecPool;

    /* Called when a pooled allocation fails at the driver, before the retry: the core hooks
       this to evict cached GPU frames, which the allocator cannot reach on its own. Cleared
       by the core before teardown, since the device may outlive it. */
    typedef void (*VSVulkanPressureFn)(void *userData);
    void setPressureCallback(VSVulkanPressureFn callback, void *userData) {
        pressureFn = callback;
        pressureUserData = userData;
    }

    /* How much device local memory this process can reasonably use right now. Uses the
       driver's live budget when VK_EXT_memory_budget is present, which subtracts what other
       processes already hold, and falls back to the raw heap size otherwise. */
    VkDeviceSize memoryBudget() const;

private:
    enum class State { Unused, Ready, Failed };

    void teardown();
    void emitLog(int severity, const std::string &message) const;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerTrampoline(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData);

    State state = State::Unused;
    std::atomic<long> refs{1};
    std::atomic<bool> coreFreedFlag{false};
    VkInstance instanceHandle = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDeviceHandle = VK_NULL_HANDLE;
    VkDevice deviceHandle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props = {};
    VkPhysicalDeviceMemoryProperties memProps = {};
    VSVulkanQueue computeQ;
    VSVulkanQueue transferQ;
    VSVulkanQueue *transferPtr = &computeQ;
    VSVulkanAllocator allocator;
    bool shaderFloat16Flag = false;
    bool unifiedMemoryFlag = false;
    bool memoryBudgetFlag = false;
    VkExternalMemoryHandleTypeFlagBits exportType = static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
    VkExternalSemaphoreHandleTypeFlagBits semaphoreExportType = static_cast<VkExternalSemaphoreHandleTypeFlagBits>(0);
    /* PFN_vkGetMemoryWin32HandleKHR/PFN_vkGetMemoryFdKHR and the semaphore equivalents;
       typed at the single use site so this header stays free of the platform Vulkan
       headers. */
    PFN_vkVoidFunction exportMemoryFn = nullptr;
    PFN_vkVoidFunction exportSemaphoreFn = nullptr;
    std::mutex flushMutex;
    VkCommandPool flushPool = VK_NULL_HANDLE;
    VkCommandBuffer flushCmd = VK_NULL_HANDLE;
    VkSemaphore flushTimeline = VK_NULL_HANDLE;
    uint64_t flushValue = 0;
    uint8_t uuid[VK_UUID_SIZE] = {};
    uint8_t luid[VK_LUID_SIZE] = {};
    uint32_t nodeMask = 0;
    bool luidValid = false;
    std::atomic<VSVulkanLogFn> logFn{nullptr};
    std::atomic<void *> logUserData{nullptr};
    VSVulkanAccountFn accountFn = nullptr;
    void *accountUserData = nullptr;
    VSVulkanPressureFn pressureFn = nullptr;
    void *pressureUserData = nullptr;
    /* Held for the whole of a sweep, which is what makes unregistration in the pool
       destructor a safe rendezvous: after unregister returns no sweep can see the pool. */
    std::mutex execPoolsMutex;
    std::vector<VSVulkanExecPool *> execPools;
    std::atomic<uint64_t> execRetainedBytes{0};
    std::atomic<uint64_t> execRetainedBudget{0};
    /* Signaled once per compute queue submission; the counter is guarded by the compute
       queue's lock exactly like every pool's own nextValue. Created lazily by the first
       compute pool, under execPoolsMutex. */
    VkSemaphore execProgressSem = VK_NULL_HANDLE;
    uint64_t execProgressNext = 0;
};

/* The counted timeline behind VSGPUTimeline, whose contract VSVulkan4.h states: every plane
   naming it holds a reference, so the last one to let go destroys the semaphore and a producer
   pair is never dangling.

   The device reference is held for the same reason VSPlaneData holds one: the last release may
   come after the core is gone, and the semaphore still needs a live device to be destroyed
   through. */
struct VSVulkanTimeline {
public:
    /* Exportable whenever the device can, since a producer pair a foreign API can wait on
       device side beats one that forces a host stall. Returns null with the error set. */
    static VSVulkanTimeline *create(VSVulkanDevice &device, std::string &errorMessage);

    VkSemaphore semaphore() const { return sem; }

    void addRef() {
        refs.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }

private:
    VSVulkanTimeline(VSVulkanDevice &device, VkSemaphore semaphore) : dev(&device), sem(semaphore) {
        dev->addRef();
    }
    ~VSVulkanTimeline();
    VSVulkanTimeline(const VSVulkanTimeline &) = delete;
    VSVulkanTimeline &operator=(const VSVulkanTimeline &) = delete;

    VSVulkanDevice *dev;
    VkSemaphore sem;
    std::atomic<long> refs{1};
};

#endif
