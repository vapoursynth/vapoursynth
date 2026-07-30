VSVulkan4.h
===========

Table of contents
#################

Introduction_

`The GPU model`_

Macros_
   VSVULKAN_API_VERSION_

Enums_
   VSVulkanQueueType_

Structs_
   VSVULKANAPI_

   VSVulkanFunctions_

   VSVulkanCoreHandles_

   VSVulkanHostImport_

   VSVulkanPlaneInfo_

   VSGPUBuffer_

   VSVulkanBufferInfo_

   VSVulkanCoreInfo_

   VSVulkanDeviceListEntry_

Functions_
   setVulkanDevice_

   setVulkanDeviceFromHost_

   getVulkanHandles_

   getVulkanCoreInfo_

   setMaxVRAMUse_

   lockVulkanQueue_

   unlockVulkanQueue_

   newGPUVideoFrame_

   getGPUPlane_

   setGPUPlaneProducer_

   enumerateVulkanDevices_

   getVulkanFunctions_

   createGPUBuffer_

   destroyGPUBuffer_


Introduction
############

This is the only public header that pulls in Vulkan types (it includes
``vulkan/vulkan_core.h``); everything a plugin needs to run compute work on
the core's Vulkan device is reached through it. The functions live in the
VSVULKANAPI_ struct, obtained with getVulkanAPI (added in API 4.3, so define
``VS_USE_API_43`` or ``VS_USE_LATEST_API`` before including VapourSynth4.h)::

   #define VS_USE_API_43
   #include <VapourSynth4.h>
   #include <VSVulkan4.h>

   const VSVULKANAPI *vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);

Building against this header requires the Vulkan headers; using it at runtime
does not require the Vulkan loader to be linked, since every entry point is
handed to the plugin ready to call.

For a guided introduction with complete example filters see
:doc:`../gpufilters`. The examples in the sdk dir (gpu_invert_example.c and
gpu_planestats_example.c) exercise everything described here.


The GPU model
#############

**One device per core.** The core owns at most one Vulkan device, created
lazily the first time anything needs it, or explicitly up front with
setVulkanDevice_ or setVulkanDeviceFromHost_. Selection defaults to the most
powerful device: suitable discrete GPUs first, then integrated, largest
device local heap deciding ties. A Vulkan 1.4 conformant driver is a hard
requirement; devices are created with **zero extensions** and a fixed feature
set (listed at VSVulkanHostImport_) that plugins may rely on unconditionally.

**GPU frames.** A GPU resident frame keeps its planes in VRAM as linear
pitched storage buffers with exactly the strides the equivalent CPU frame
would have, so ``getStride`` and the dimension functions apply unchanged and
uploads are flat copies. Calling ``getReadPtr``/``getWritePtr`` on a GPU
frame is a fatal error, never a silent download. Residency is part of the
type system: filters declare GPU inputs and outputs with the ``vknode``
signature type together with the ``ffGPUOutput`` filter flag, and the core
verifies at every step — creation, invoke return, and frame delivery — that
declaration and reality agree.

**Synchronization is per plane and device side.** Every GPU plane carries a
producer pair: a timeline semaphore and the value whose completion means the
plane's contents are ready (VSVulkanPlaneInfo_). Consumers make their
submissions wait on the pairs of every plane they read; producers publish the
pair their submission signals through setGPUPlaneProducer_. A NULL semaphore
means host produced content that is ready immediately. Because frames created
with ``newVideoFrame2`` share plane data, one frame's planes can have
different producers — always wait per plane, deduplicating waits on the same
semaphore to the highest value. The host never blocks in this scheme; a
filter records, submits and returns, and the graph pipelines.

**Queues.** The core exposes a compute queue and a transfer queue (the same
queue when the device has no dedicated transfer family). ``VkQueue`` is
externally synchronized, so every submission a plugin makes must hold the
matching lock via lockVulkanQueue_/unlockVulkanQueue_. Filters that signal
their own timeline should allocate the value inside that same lock, so
signals reach the queue in increasing order.

**Memory.** Frame planes and scratch buffers (createGPUBuffer_) come from a
pooled sub-allocator whose use is accounted against a VRAM limit
(setMaxVRAMUse_, default 80% of the driver reported budget). Cache pressure,
eviction and the thread pool's admission control all see GPU memory the same
way they see host memory.

**Lifetime.** GPU frames may legally outlive the core, exactly like CPU
frames; releasing one after ``freeCore`` still returns its VRAM. Everything
else is bounded by the filter instance: destroy pipelines, pools, semaphores
and scratch buffers in the filter's free callback at the latest, and only
after the submissions using them have completed on the device. A timeline
published as a producer pair must outlive every possible consumer, which in
practice means it lives exactly as long as the filter instance.

**The function table.** getVulkanFunctions_ returns every Vulkan entry point
a filter normally needs, loaded and device bound (VSVulkanFunctions_). The
table is frozen ABI: existing entries never move or disappear, additions only
happen together with a VSVULKAN_API_VERSION_ bump, and getVulkanAPI serves
every version up to the current one from the same structs, so any older
version's layout remains a valid prefix. The table deliberately carries the
Vulkan 1.4 spellings only (``vkCmdPushConstants2``, ``vkCmdCopyBuffer2``,
and so on). Anything outside the curated set — a function from a host
enabled extension, for example — can be resolved through the
``getInstanceProcAddr`` in VSVulkanCoreHandles_.


Macros
######

VSVULKAN_API_VERSION
--------------------

The version of the GPU API and the VSVulkanFunctions_ table, currently 1.
Pass it to getVulkanAPI. Both grow append-only, so a plugin built against an
older version keeps working against every newer core.


Enums
#####

.. _VSVulkanQueueType:

enum VSVulkanQueueType
----------------------

   * vqCompute

   * vqTransfer

     The same underlying queue as vqCompute when the device has no dedicated
     transfer queue family, so locking through this constant stays correct
     either way.


Structs
#######

.. _VSVULKANAPI:

struct VSVULKANAPI
------------------

This struct is the whole GPU API. It is threadsafe and boringly stable: use
getVulkanAPI (VapourSynth4.h, API 4.3) to obtain it, together with the
VSVULKAN_API_VERSION_ you were compiled against. Returns NULL if the
requested version is newer than the core supports.

.. _VSVulkanFunctions:

struct VSVulkanFunctions
------------------------

The core's ready loaded Vulkan dispatch table: one member per entry point,
named exactly like the Vulkan function. Call everything through it::

   d->vk->vkCmdDispatch(cmd, x, y, 1);

The member list is generated from VS_VK_FUNCTION_LIST in this header, which
also documents each entry's level (global, instance or device). See
`The GPU model`_ for the freeze policy.

.. _VSVulkanCoreHandles:

struct VSVulkanCoreHandles
--------------------------

Everything needed to run your own Vulkan work on the core's device: the
instance, physical device and device handles, ``getInstanceProcAddr`` for
resolving entry points outside the curated table, and the compute and
transfer queue family/index pairs. The transfer values equal the compute
values when there is no dedicated transfer queue. Valid for the core's
lifetime.

.. _VSVulkanHostImport:

struct VSVulkanHostImport
-------------------------

A host application handing VapourSynth its existing device instead of
letting the core create one, through setVulkanDeviceFromHost_. The device
must be Vulkan 1.4 with the required feature set enabled; availability is
verified at adoption, but Vulkan offers no way to query enablement after
device creation, so enabling them is the host's responsibility. The full
required list, grouped by feature struct, is documented in the header at
this struct; every entry is mandatory for a conformant Vulkan 1.4
implementation, so any device passing the version gate can comply.

When the host keeps submitting to the shared queues itself it must supply
the *lockQueue*/*unlockQueue* callbacks and take the same lock around its
own submissions. Setting *transferQueueFamily* to UINT32_MAX shares the
compute queue.

The imported handles must stay valid for the core's lifetime, extended by
any GPU resident frames still referenced after the core is freed: releasing
such a frame returns VRAM through the imported device. Freeing every GPU
frame before destroying the host device is the safe order.

.. _VSVulkanPlaneInfo:

struct VSVulkanPlaneInfo
------------------------

One GPU resident plane, as returned by getGPUPlane_.

   * VkBuffer buffer — the plane's storage buffer

   * VkDeviceSize bufferSize — stride * height bytes

   * VkSemaphore readySemaphore — wait (semaphore, value) device side before
     reading; NULL means the contents are ready now

   * uint64_t readyValue

.. _VSGPUBuffer:

struct VSGPUBuffer
------------------

Opaque handle to a scratch buffer from createGPUBuffer_; pass it to
destroyGPUBuffer_ when done.

.. _VSVulkanBufferInfo:

struct VSVulkanBufferInfo
-------------------------

Filled in by createGPUBuffer_; everything a kernel or the host needs to use
the buffer.

   * VkBuffer buffer

   * VkDeviceAddress address — nonzero when *usage* included
     SHADER_DEVICE_ADDRESS

   * void \*mapped — nonnull when the memory ended up host visible;
     persistently mapped

   * VkDeviceSize size — the requested size, unrounded

   * VkMemoryPropertyFlags memoryFlags — what the chosen memory type actually
     provides

.. _VSVulkanCoreInfo:

struct VSVulkanCoreInfo
-----------------------

Filled in by getVulkanCoreInfo_.

   * char deviceName[256]

   * int64_t deviceMemory — largest device local heap

   * int64_t budget — what the driver says this process may reasonably use
     right now

   * int64_t allocated — current VapourSynth VRAM use

   * int64_t limit — the eviction limit, settable through setMaxVRAMUse_

.. _VSVulkanDeviceListEntry:

struct VSVulkanDeviceListEntry
------------------------------

One entry per physical device from enumerateVulkanDevices_. The position in
the enumeration is exactly the index setVulkanDevice_ takes.

   * char deviceName[256]

   * uint32_t apiVersion

   * int deviceType — a VkPhysicalDeviceType value

   * int64_t deviceMemory — largest device local heap

   * int usable — nonzero when the device passes the Vulkan 1.4 and required
     feature gate

   * char unusableReason[256] — why it does not, for display


Functions
#########

.. _setVulkanDevice:

int setVulkanDevice(VSCore \*core, int deviceIndex, char \*errorMessage, int errorMessageSize)

   Selects the Vulkan device the core will use, by index into the
   enumerateVulkanDevices_ order, or -1 for the automatic most powerful
   choice. Must be called before the device is first used; calling it after
   is an error. Unusable devices may be selected and fail with their reason,
   so a frontend can let the user pick from the full list.

   All int returning functions in this struct return 0 on success and fill
   *errorMessage* otherwise.

----------

.. _setVulkanDeviceFromHost:

int setVulkanDeviceFromHost(VSCore \*core, const VSVulkanHostImport_ \*import, char \*errorMessage, int errorMessageSize)

   Runs VapourSynth on a device the host application already created instead
   of opening a second one on the same GPU. Same call-before-first-use rule
   as setVulkanDevice_. See VSVulkanHostImport_ for the contract.

----------

.. _getVulkanHandles:

int getVulkanHandles(VSCore \*core, VSVulkanCoreHandles_ \*handles, char \*errorMessage, int errorMessageSize)

   Returns the raw handles of the core's device, bringing the device up on
   first call like the first GPU filter would.

----------

.. _getVulkanCoreInfo:

int getVulkanCoreInfo(VSCore \*core, VSVulkanCoreInfo_ \*info, char \*errorMessage, int errorMessageSize)

   Reports the device name, memory budget and current VapourSynth VRAM use.

----------

.. _setMaxVRAMUse:

int64_t setMaxVRAMUse(int64_t bytes, VSCore \*core)

   Sets the VRAM limit cache pressure works against, mirroring
   setMaxCacheSize for the host pool. Values of zero or less leave the limit
   unchanged. Returns the limit in effect. The default is 80% of the live
   driver budget at device creation.

----------

.. _lockVulkanQueue:

void lockVulkanQueue(VSCore \*core, int queue)

   Locks one of the shared queues (VSVulkanQueueType_). Mandatory around
   every vkQueueSubmit a plugin performs, since VkQueue is externally
   synchronized and the core, other plugins and possibly a host application
   submit to the same queues. Allocate your timeline values inside the lock
   so their signal order matches their numeric order.

----------

.. _unlockVulkanQueue:

void unlockVulkanQueue(VSCore \*core, int queue)

   Releases the queue lock.

----------

.. _newGPUVideoFrame:

VSFrame \*newGPUVideoFrame(const VSVideoFormat \*format, int width, int height, const VSFrame \*propSrc, VSCore \*core)

   Creates a GPU resident frame; identical semantics to newVideoFrame
   otherwise. The planes are freshly allocated with NULL producer pairs, so
   publish yours with setGPUPlaneProducer_ once the writing submission is
   made. To share planes from source frames use newVideoFrame2, which
   propagates GPU residency (and the producer pairs) from the shared planes.

----------

.. _getGPUPlane:

int getGPUPlane(const VSFrame \*frame, int plane, VSVulkanPlaneInfo_ \*info)

   Returns a plane's buffer and producer pair. Nonzero when the frame is not
   GPU resident or the plane does not exist.

----------

.. _setGPUPlaneProducer:

void setGPUPlaneProducer(VSFrame \*frame, int plane, VkSemaphore semaphore, uint64_t value)

   Publishes the producer pair of a plane you write: consumers will make
   their submissions wait for *semaphore* to reach *value* before reading.
   The semaphore must outlive every possible consumer, in practice the
   filter instance. Only ever call this on frames you are producing.

----------

.. _enumerateVulkanDevices:

int enumerateVulkanDevices(VSVulkanDeviceListEntry_ \*entries, int maxEntries, char \*errorMessage, int errorMessageSize)

   Lists every physical device through a temporary instance, so it works
   before any device selection and needs no core. Returns the total device
   count, which may exceed *maxEntries*, or -1 with the error set. Passing
   NULL and 0 just counts.

----------

.. _getVulkanFunctions:

const VSVulkanFunctions_ \*getVulkanFunctions(VSCore \*core, char \*errorMessage, int errorMessageSize)

   The core's ready loaded dispatch table, the normal way for filters to call
   Vulkan; nothing needs to be linked or loaded by hand. Brings the device up
   on first call and stays valid for the core's lifetime. Returns NULL with
   the error set when no device is available.

----------

.. _createGPUBuffer:

VSGPUBuffer_ \*createGPUBuffer(VSCore \*core, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, VSVulkanBufferInfo_ \*info, char \*errorMessage, int errorMessageSize)

   Scratch memory through the same sub-allocator frame planes use, so it is
   counted against the VRAM limit, predicted by the thread pool's admission
   control and recycled through size buckets — allocating and destroying per
   frame is cheap by design. *requiredFlags* must be satisfied by the chosen
   memory type; *preferredFlags* steer the choice when several qualify. Host
   visible requests come back persistently mapped. Returns NULL with the
   error set on failure.

   Lifetime is the caller's, with one rule: unlike frames, buffers have no
   producer pair anyone waits on, so destroy only after the submissions using
   the buffer have completed on the device, and at the latest in the filter's
   free callback.

----------

.. _destroyGPUBuffer:

void destroyGPUBuffer(VSGPUBuffer_ \*buffer)

   Returns the buffer's memory to the pool. NULL is ignored.
