VSVulkan4.h
===========

Table of contents
#################

Introduction_

`The GPU model`_

Enums_
   VSVulkanQueueType_

   VSGPUShaderLanguage_

Structs_
   VSVULKANAPI_

   VSVulkanFunctions_

   VSVulkanCoreHandles_

   VSVulkanPlaneInfo_

   VSGPUTimeline_

   VSGPUBuffer_

   VSVulkanBufferInfo_

   VSGPUMemory_

   VSVulkanMemoryInfo_

   VSGPUMemoryReservation_

   VSGPUShader_

   VSGPUExecPool_

   VSGPUExecContext_

   VSGPUReleaseFunc_

   VSVulkanExportedMemory_

   VSVulkanExportedSemaphore_

   VSVulkanCoreInfo_

   VSVulkanDeviceListEntry_

Functions_
   **Device selection and information**

   enumerateVulkanDevices_

   setVulkanDevice_

   getVulkanCoreInfo_

   setMaxVRAMUse_

   **Calling Vulkan on the core's device**

   getVulkanHandles_

   getVulkanFunctions_

   lockVulkanQueue_

   unlockVulkanQueue_

   **GPU resident frames**

   newGPUVideoFrame_

   getGPUPlane_

   setGPUPlaneProducer_

   **Timelines**

   createGPUTimeline_

   freeGPUTimeline_

   addGPUTimelineRef_

   getGPUTimelineSemaphore_

   **Memory from the core's pool**

   createGPUBuffer_

   destroyGPUBuffer_

   allocateGPUMemory_

   freeGPUMemory_

   reserveGPUMemory_

   updateGPUMemoryReservation_

   releaseGPUMemoryReservation_

   **Shader compilation**

   compileGPUShader_

   getGPUShaderCode_

   freeGPUShader_

   **Exec pools**

   createGPUExecPool_

   freeGPUExecPool_

   gpuExecPoolWaitIdle_

   gpuExecPoolTimeline_

   **Recording contexts**

   gpuExecAcquire_

   gpuExecCommandBuffer_

   gpuExecReadsFrame_

   gpuExecWritesPlane_

   gpuExecUsesBuffer_

   gpuExecUsesMemory_

   gpuExecRetain_

   gpuExecSubmit_

   gpuExecAbandon_

   **Sharing frames with other APIs**

   exportGPUPlane_

   exportGPUSemaphore_

   waitGPUFrame_


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

   const VSVULKANAPI *vkapi = vsapi->getVulkanAPI();

Building against this header requires the Vulkan headers; using it at runtime
does not require the Vulkan loader to be linked, since every entry point is
handed to the plugin ready to call.

For a guided introduction with complete example filters see
:doc:`../gpufilters`. The examples in the sdk dir (gpu_invert_example.c,
gpu_invert_raw_example.c, gpu_invert_driver_example.cpp
and, for the foreign API side, gpu_cuda_invert_example.cu) exercise everything
described here.


The GPU model
#############

**One device per core.** The core owns at most one Vulkan device, created
lazily the first time anything needs it, or selected explicitly up front with
setVulkanDevice_. Selection defaults to the most powerful device: suitable
discrete GPUs first, then integrated, largest device local heap deciding
ties. A Vulkan 1.4 conformant driver is a hard requirement; devices are
created with a fixed feature set (listed in the header, summarized in
:doc:`../gpufilters`) that plugins may rely on unconditionally, and no device
extensions except the platform's opaque handle export extension when
available — never load-bearing. The core always creates its device itself;
sharing frames with another Vulkan device or API in the same process goes
through exportGPUPlane_, not device sharing.

**GPU frames.** A GPU resident frame keeps its planes in VRAM as linear
pitched storage buffers with exactly the strides the equivalent CPU frame
would have, so ``getStride`` and the dimension functions apply unchanged and
uploads are flat copies. Calling ``getReadPtr``/``getWritePtr`` on a GPU
frame is a fatal error, never a silent download. Residency is part of the
type system: filters declare GPU inputs and outputs with the ``vnode:gpu``
signature form together with the ``ffGPUOutput`` filter flag, and the core
verifies at every step — creation, invoke return, and frame delivery — that
declaration and reality agree. Filters that merely rearrange frames declare
``vnode:all`` instead: they accept either residency, adapt per instance, and
their output residency follows their input.

**Synchronization is per plane and device side.** Every GPU plane carries a
producer pair: a timeline semaphore and the value whose completion means the
plane's contents are ready (VSVulkanPlaneInfo_). Consumers make their
submissions wait on the pairs of every plane they read; producers publish the
pair their submission signals through setGPUPlaneProducer_, which takes the
semaphore as a reference counted VSGPUTimeline_ so each plane keeps the
timeline it names alive by itself. A NULL semaphore means host produced
content that is ready immediately. Because frames created with
``newVideoFrame2`` share plane data, one frame's planes can have different
producers — always wait per plane, deduplicating waits on the same semaphore
to the highest value. The host never blocks in this scheme; a filter records,
submits and returns, and the graph pipelines.

**Queues.** The core exposes a compute queue and a transfer queue (the same
queue when the device has no dedicated transfer family). ``VkQueue`` is
externally synchronized, so every submission a plugin makes must hold the
matching lock via lockVulkanQueue_/unlockVulkanQueue_. Filters that signal
their own timeline should allocate the value inside that same lock, so
signals reach the queue in increasing order.

**Recording and submitting.** Doing the above by hand is the same work in
every filter, so the core offers it as an exec pool (createGPUExecPool_): a
timeline, a command pool and a ring of command buffers, from which a thread
claims one context for the duration of one recording. The context waits the
producer pairs of the frames declared with gpuExecReadsFrame_, keeps those
frames and any scratch handed to it alive until the submission completes,
allocates its timeline value under the queue lock, and publishes the producer
pairs of the planes declared with gpuExecWritesPlane_. What goes into the
command buffer is entirely the filter's business — gpuExecCommandBuffer_
hands it over and the pool imposes nothing on it — so indirect dispatches,
custom barriers and private query pools are as usable as a plain dispatch.
Filters that would rather own their submissions can still do everything by
hand with createGPUTimeline_ and the queue locks; the two paths interoperate,
since both end in producer pairs on planes.

**Memory.** Frame planes and scratch buffers (createGPUBuffer_) come from a
pooled sub-allocator whose use is accounted against a VRAM limit
(setMaxVRAMUse_, default two thirds of the driver reported budget, clamped
further on unified memory devices where that VRAM is the host's RAM). Cache
pressure, eviction and the thread pool's admission control all see GPU memory
the same way they see host memory. Resources the API has no constructor for —
images above all — take a bare region of the same pool through
allocateGPUMemory_ and bind against it, so they are accounted like everything
else. Memory a plugin allocates through some other API entirely (a CUDA pool,
a second Vulkan device, a video session) can still be declared to the same
budget with reserveGPUMemory_, which is how a foreign consumer avoids
competing with the core's cache for the same VRAM.

**Lifetime.** GPU frames may legally outlive the core, exactly like CPU
frames; releasing one after ``freeCore`` still returns its VRAM. Everything
else is bounded by the filter instance: destroy pipelines, pools, images and
scratch buffers in the filter's free callback at the latest, and only after
the submissions using them have completed on the device. Timelines are the
exception, because they are reference counted: release yours whenever you are
done signalling, and any plane still naming it keeps it alive on its own.
That matters because frames legitimately outlive the filter that made them —
FrameEval and ModifyFrame hand back a frame from a node they then drop, and
the cache can hold one indefinitely. An exec pool takes care of the rest of
the timing on its own: what it retains is released once the submission is
known complete, and freeGPUExecPool_ drains the GPU before returning.

**The function table.** getVulkanFunctions_ returns every Vulkan entry point
a filter normally needs, loaded and device bound (VSVulkanFunctions_). The
table is frozen ABI: existing entries never move or disappear, additions only
happen together with a VAPOURSYNTH_API_VERSION bump, which leaves a plugin
built against an older one holding a still valid prefix of the same struct.
The table deliberately carries the
Vulkan 1.4 spellings only (``vkCmdPushConstants2``, ``vkCmdCopyBuffer2``,
and so on). Anything outside the curated set — a function from a host
enabled extension, for example — can be resolved through the
``getInstanceProcAddr`` in VSVulkanCoreHandles_.


Enums
#####

.. _VSVulkanQueueType:

enum VSVulkanQueueType
----------------------

   * vqCompute

     Takes any work. Use it for anything that dispatches.

   * vqTransfer

     Copies only. Vulkan guarantees the implication one way: a compute queue
     always accepts transfer commands, while a dedicated transfer queue need
     not accept compute ones, and a discrete card's DMA family typically
     reports ``VK_QUEUE_TRANSFER_BIT`` alone. Recording a dispatch against a
     pool created on this queue is therefore invalid usage wherever such a
     family exists — and silently fine where it does not, since the two then
     resolve to the same queue, which is what makes the mistake easy to ship.

     Locking through either constant stays correct either way; it is
     createGPUExecPool_ where the choice binds. When in doubt use *vqCompute*:
     the cost is losing overlap with the core's own transfers, not correctness.

.. _VSGPUShaderLanguage:

enum VSGPUShaderLanguage
------------------------

   The source language for compileGPUShader_. Only slGLSL exists today;
   unknown values fail cleanly, so adding a language later is an additive
   enum value rather than an ABI event.

   * slGLSL

     Compute stage GLSL. The core pins the accepted dialect as a platform
     property: write ``#version 460``, compiled for the Vulkan 1.4 client
     targeting SPIR-V 1.6.


Structs
#######

.. _VSVULKANAPI:

struct VSVULKANAPI
------------------

This struct is the whole GPU API. It is threadsafe and boringly stable: use
getVulkanAPI (VapourSynth4.h, API 4.3) to obtain it. It takes no arguments and
never returns NULL — the GPU API has no version of its own, growing append-only
with the core API instead, so which one you get was already settled when the
VSAPI itself was, and getAPIVersion says which that was.

Unless an entry says otherwise, a status returning function returns 0 on
success and nonzero on failure, and one returning a handle returns NULL on
failure; either way the reason lands in *errorMessage* where the function
takes one.

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

.. _VSVulkanPlaneInfo:

struct VSVulkanPlaneInfo
------------------------

One GPU resident plane, as returned by getGPUPlane_.

   * VkBuffer buffer — the plane's storage buffer

   * VkDeviceSize bufferSize — stride * height bytes

   * VkSemaphore readySemaphore — wait (semaphore, value) device side before
     reading; NULL means the contents are ready now

   * uint64_t readyValue

.. _VSGPUTimeline:

struct VSGPUTimeline
--------------------

Opaque handle to a reference counted timeline semaphore, the object
setGPUPlaneProducer_ publishes. Get one from createGPUTimeline_ or, for an
exec pool's own timeline, from gpuExecPoolTimeline_; the raw ``VkSemaphore``
behind it comes from getGPUTimelineSemaphore_.

Every plane a timeline is published on takes its own reference, so the
semaphore lives exactly as long as something might still wait on it. This is
why a filter's timeline no longer has to outlive its consumers: release your
reference whenever you are done signalling — the free callback is the natural
place — and any frame still in flight keeps it alive by itself. The semaphore
is created exportable wherever the device supports it, so a foreign API can
wait the pairs you publish device side.

.. _VSGPUBuffer:

struct VSGPUBuffer
------------------

Opaque handle to a scratch buffer from createGPUBuffer_; pass it to
destroyGPUBuffer_ when done, or hand it to gpuExecUsesBuffer_ and let the
submission retire it.

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

.. _VSGPUMemory:

struct VSGPUMemory
------------------

Opaque handle to a bare region of the same pooled VRAM, from
allocateGPUMemory_, for resources the core has no constructor for. An image
is the case that motivates it: memory is never passed to ``vkCreateImage``,
it is bound afterwards, so nothing would be gained by wrapping image creation
— the filter creates the image its own way and only the allocation comes from
the pool. Release with freeGPUMemory_, or hand it to gpuExecUsesMemory_.

.. _VSVulkanMemoryInfo:

struct VSVulkanMemoryInfo
-------------------------

Filled in by allocateGPUMemory_; what ``vkBindBufferMemory2`` and
``vkBindImageMemory2`` need.

   * VkDeviceMemory memory — the shared block; bind against it, never free it

   * VkDeviceSize offset — where the region starts in that block, and what to
     pass to the bind call; **not** zero, and not to be assumed so

   * VkDeviceSize size — what the region actually reserves, at least the
     requested size

   * void \*mapped — nonnull when host visible, already offset to the region

   * VkMemoryPropertyFlags memoryFlags — what the chosen memory type actually
     provides

.. _VSGPUMemoryReservation:

struct VSGPUMemoryReservation
-----------------------------

Opaque handle to a declaration of GPU memory allocated outside the core, from
reserveGPUMemory_, so that the byte count takes part in the core's budgeting.
It holds no memory itself, only the number.

.. _VSGPUShader:

struct VSGPUShader
------------------

Opaque handle to a runtime compiled shader holding the SPIR-V words,
returned by compileGPUShader_ and read through getGPUShaderCode_. It is
independent of everything else once returned — it stays valid after the
core that compiled it is freed — and is released with freeGPUShader_.

.. _VSGPUExecPool:

struct VSGPUExecPool
--------------------

Opaque handle to an exec pool from createGPUExecPool_: a timeline semaphore,
a command pool and a ring of command buffers on one of the core's queues.
This is the plumbing every GPU filter needs regardless of what it records —
waiting on the producers of the frames it reads, keeping those frames alive
until the GPU is done, allocating timeline values in queue order, and
publishing producer pairs on the planes it writes. Destroy it with
freeGPUExecPool_ in the filter's free callback.

.. _VSGPUExecContext:

struct VSGPUExecContext
-----------------------

Opaque handle to one of the pool's recording slots, claimed by one thread
from gpuExecAcquire_ until gpuExecSubmit_ or gpuExecAbandon_. It hands out
its command buffer and imposes nothing on what goes into it, so filters
recording indirect dispatches, custom barriers or their own query pools use
it exactly like filters recording a plain dispatch.

.. _VSGPUReleaseFunc:

typedef void (VS_CC \*VSGPUReleaseFunc)(void \*object)

   Cleanup a filter hands to an exec context with gpuExecRetain_, run once
   the submission it was recorded against has completed. It runs on whichever
   thread reaps that submission, so it must be safe to call from any thread.

.. _VSVulkanExportedMemory:

struct VSVulkanExportedMemory
-----------------------------

A frame plane's backing memory exported as an opaque handle by
exportGPUPlane_, so CUDA and other Vulkan devices in the same process can
wrap the underlying allocation and read or write the plane zero copy. The
plane lives at *offset* within an allocation of *memorySize* bytes;
importers import the whole allocation once and address planes by offset.

   * uint64_t memoryId — stable identity of the underlying allocation, never
     reused by this device; cache imports keyed by it, never by handle value,
     since every export call returns a new handle

   * VkDeviceSize memorySize

   * VkDeviceSize offset

   * VkDeviceSize size — the plane's bytes, stride * height

   * int handleType — the VkExternalMemoryHandleTypeFlagBits of the handle

   * intptr_t handle — HANDLE on Windows, file descriptor elsewhere

Handle ownership: the returned handle belongs to the caller. An NT handle is
not consumed by Vulkan or CUDA import, so CloseHandle it once the import
exists; a POSIX fd is consumed by a successful import and must only be closed
when the import failed or never happened. The OS reference counts the
underlying memory, so a live import keeps the allocation valid even after
every VapourSynth side reference — frames, and like frames the core — is
gone. Synchronization stays host side for now: call waitGPUFrame_ before
reading a frame through an import, and finish foreign writes
(cudaStreamSynchronize) before returning a frame containing them.

.. _VSVulkanExportedSemaphore:

struct VSVulkanExportedSemaphore
--------------------------------

A timeline semaphore exported as an opaque handle by exportGPUSemaphore_.
Importing a producer pair's semaphore lets a CUDA stream or another Vulkan
device wait the pair **on the device**, replacing waitGPUFrame_'s host wait
and restoring full pipelining across the API boundary. An external semaphore
wait also carries the cross device memory dependency, so the availability
flush waitGPUFrame_ performs is not needed on that path.

   * int handleType — the VkExternalSemaphoreHandleTypeFlagBits of the handle

   * intptr_t handle — HANDLE on Windows, file descriptor elsewhere

Every call returns a new handle for the same semaphore; cache imports keyed
by the VkSemaphore value from VSVulkanPlaneInfo_, scoped to your filter
instance. That key cannot go stale while you hold the frame, because a plane
holds a reference to the timeline it names (VSGPUTimeline_). Handle ownership
follows the same platform rules as VSVulkanExportedMemory_.

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

   * uint8_t deviceUUID[VK_UUID_SIZE] — matches the UUID CUDA reports for the
     same GPU, for picking the right foreign device

   * uint8_t deviceLUID[VK_LUID_SIZE], uint32_t deviceNodeMask,
     int deviceLUIDValid — the DXGI adapter identity, meaningful only when
     *deviceLUIDValid* is nonzero

   * int exportHandleType — the VkExternalMemoryHandleTypeFlagBits
     exportGPUPlane_ hands out, 0 when export is unavailable

   * int semaphoreExportHandleType — the VkExternalSemaphoreHandleTypeFlagBits
     exportGPUSemaphore_ hands out, 0 when unavailable; also what a filter
     passes to VkExportSemaphoreCreateInfo to make its own timeline
     exportable

   * int unifiedMemory — nonzero when the device's memory is the host's, which
     is the case for integrated and software devices. *deviceMemory* and
     *budget* then describe a share of system RAM rather than separate
     hardware, and *limit* is capped so that it and the host memory limit
     together leave the machine some room. A plugin sizing pools of its own
     should treat its allocations as competing with host memory here.

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

   * uint8_t deviceUUID[VK_UUID_SIZE], uint8_t deviceLUID[VK_LUID_SIZE],
     uint32_t deviceNodeMask, int deviceLUIDValid — as in VSVulkanCoreInfo_,
     available before any device selection


Functions
#########

.. _enumerateVulkanDevices:

int enumerateVulkanDevices(VSVulkanDeviceListEntry_ \*entries, int maxEntries, char \*errorMessage, int errorMessageSize)

   Lists every physical device through a temporary instance, so it works
   before any device selection and needs no core. Returns the total device
   count, which may exceed *maxEntries*, or -1 with the error set. Passing
   NULL and 0 just counts.

----------

.. _setVulkanDevice:

int setVulkanDevice(VSCore \*core, int deviceIndex, char \*errorMessage, int errorMessageSize)

   Selects the Vulkan device the core will use, by index into the
   enumerateVulkanDevices_ order, or -1 for the automatic most powerful
   choice. Must be called before the device is first used; calling it after
   is an error. Unusable devices may be selected and fail with their reason,
   so a frontend can let the user pick from the full list.

----------

.. _getVulkanCoreInfo:

int getVulkanCoreInfo(VSCore \*core, VSVulkanCoreInfo_ \*info, char \*errorMessage, int errorMessageSize)

   Reports the device name, its identity for matching against other APIs'
   device enumerations, the memory budget and the current VapourSynth VRAM
   use. Brings the device up on first call, like the first GPU filter would.

----------

.. _setMaxVRAMUse:

int64_t setMaxVRAMUse(int64_t bytes, VSCore \*core)

   Sets the VRAM limit cache pressure works against, mirroring
   setMaxCacheSize for the host pool. Values of zero or less leave the limit
   unchanged, and positive values are raised to a 256 MB floor: the allocator
   commits whole 128 MB blocks, so any limit below one block is over-limit
   from the first GPU frame on and the session spends its life in permanent
   cache pressure. Returns the limit in effect. The default is two thirds of
   the live driver budget at device creation, the rest being headroom for
   filter working sets and for the system, and never below the same floor,
   which matters when another process already holds most of the card at
   startup; on a unified memory device it is clamped further, since that VRAM
   and the host's memory are the same RAM.

----------

.. _getVulkanHandles:

int getVulkanHandles(VSCore \*core, VSVulkanCoreHandles_ \*handles, char \*errorMessage, int errorMessageSize)

   Returns the raw handles of the core's device, bringing the device up on
   first call like the first GPU filter would.

----------

.. _getVulkanFunctions:

const VSVulkanFunctions_ \*getVulkanFunctions(VSCore \*core, char \*errorMessage, int errorMessageSize)

   The core's ready loaded dispatch table, the normal way for filters to call
   Vulkan; nothing needs to be linked or loaded by hand. Brings the device up
   on first call and stays valid for the core's lifetime. Returns NULL with
   the error set when no device is available.

----------

.. _lockVulkanQueue:

void lockVulkanQueue(VSCore \*core, int queue)

   Locks one of the shared queues (VSVulkanQueueType_). Mandatory around
   every vkQueueSubmit a plugin performs, since VkQueue is externally
   synchronized and the core and other plugins submit to the same queues.
   Allocate your timeline values inside the lock so their signal order
   matches their numeric order. Filters using an exec pool never need this;
   gpuExecSubmit_ does both.

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

void setGPUPlaneProducer(VSFrame \*frame, int plane, VSGPUTimeline_ \*timeline, uint64_t value)

   Publishes the producer pair of a plane you write: consumers will make
   their submissions wait for *timeline* to reach *value* before reading.
   The plane takes its own reference on the timeline, so publishing needs no
   reference of yours to be kept afterwards, and you may release yours as
   soon as you are done signalling. Passing NULL publishes the plane as host
   ready. Only ever call this on frames you are producing.

----------

.. _createGPUTimeline:

VSGPUTimeline_ \*createGPUTimeline(VSCore \*core, char \*errorMessage, int errorMessageSize)

   A timeline of your own, for filters recording and submitting without the
   core's exec pool. Created with an initial value of 0, exportable where the
   device allows it, and returned with one reference which is yours to
   release — in the free callback, without waiting for consumers, since
   planes you published it on hold their own.

----------

.. _freeGPUTimeline:

void freeGPUTimeline(VSGPUTimeline_ \*timeline)

   Drops one reference. The semaphore is destroyed when the last one goes,
   which may be long after this call if planes still name it.

----------

.. _addGPUTimelineRef:

void addGPUTimelineRef(VSGPUTimeline_ \*timeline)

   Takes another reference, for handing the same timeline to something with
   its own lifetime. Every added reference needs a matching freeGPUTimeline_.

----------

.. _getGPUTimelineSemaphore:

VkSemaphore getGPUTimelineSemaphore(VSGPUTimeline_ \*timeline)

   The raw handle, to signal in your own vkQueueSubmit and to pass to
   exportGPUSemaphore_. Valid for as long as you hold a reference. Never
   signal an exec pool's timeline this way: the pool allocates its signal
   values under the queue lock at submit, and an external signal races them
   on a timeline whose values must only increase.

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
   free callback. gpuExecUsesBuffer_ takes that timing off your hands.

----------

.. _destroyGPUBuffer:

void destroyGPUBuffer(VSGPUBuffer_ \*buffer)

   Returns the buffer's memory to the pool. NULL is ignored.

----------

.. _allocateGPUMemory:

VSGPUMemory_ \*allocateGPUMemory(VSCore \*core, const VkMemoryRequirements \*requirements, VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, VSVulkanMemoryInfo_ \*info, char \*errorMessage, int errorMessageSize)

   A region of the pool frame planes come from, for a resource this API has
   no constructor for. Create the resource, ask Vulkan what memory it needs,
   allocate it here, and bind against the memory and offset that come back::

      vkCreateImage(device, &imageInfo, NULL, &image);
      vkGetImageMemoryRequirements2(device, &reqInfo, &req);
      VSGPUMemory *mem = allocateGPUMemory(core, &req.memoryRequirements,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
      VkBindImageMemoryInfo bind = { ..., image, info.memory, info.offset };
      vkBindImageMemory2(device, 1, &bind);

   What this buys over a private vkAllocateMemory is everything the pool
   does: the region counts against the VRAM limit, is visible to the thread
   pool's admission control, is recycled through the size buckets, and takes
   part in the reclamation the core performs under pressure. A private
   allocation is none of those things — the core cannot see it, so it can
   neither account for it nor make room for it.

   Bind at *info.offset*, not at the start of *info.memory*: the region sits
   inside a shared block, and images in particular are aligned further inside
   it, since their requirements are routinely coarser than the pool's regions
   (65536 for a small image on hardware that asks 256 for a large one). That
   is handled by reserving the distance, so any alignment is satisfied; the
   cost is a bounded overshoot that is invisible at plane sizes.

   Two resources must not bind here, and neither is detectable from what this
   is given, so both are the caller's to check: one created with external
   memory info, since these blocks are not exportable, and one whose
   VkMemoryDedicatedRequirements report *requiresDedicatedAllocation*, since
   a suballocated region is by definition not a dedicated allocation. Chain
   VkMemoryDedicatedRequirements onto the vkGet\*MemoryRequirements2 call to
   see the latter; it is false for ordinary images and buffers, and true
   mostly for external and platform specific formats. Allocate those
   yourself. (*prefersDedicatedAllocation* is only a hint and is safe to
   ignore here.)

   A device whose buffer/image granularity is coarser than the pool's regions
   is refused with a message rather than risking two resources sharing a
   granularity page.

   A freed region returns to the pool at once and may back another resource
   immediately, so freeing early aliases live data rather than faulting: free
   only once the submissions using the resource have completed, or hand the
   region to gpuExecUsesMemory_ and let it retire on its own.

----------

.. _freeGPUMemory:

void freeGPUMemory(VSGPUMemory_ \*memory)

   Returns the region to the pool. NULL is ignored. The resource bound to it
   is still yours to destroy; do that first.

----------

.. _reserveGPUMemory:

VSGPUMemoryReservation_ \*reserveGPUMemory(VSCore \*core, int64_t bytes, char \*errorMessage, int errorMessageSize)

   Declares GPU memory the core did not allocate — a CUDA pool, another
   Vulkan device with its own queues and features, a video session — into the
   same accounting the core's pool uses, so the frame cache, the thread
   pool's admission control and the unified memory brake all see it.

   The core never refuses a reservation: it does not own the memory and
   cannot veto it. What an increase buys is cooperation. When the declared
   bytes push the GPU pool past its limit, the least valuable cached GPU
   frames are evicted down to a tenth under it and fully idle allocator
   blocks are handed back to the driver before the call returns — so reserve
   or update **before** the foreign allocation, and the VRAM it is about to
   ask the driver for has actually been vacated. Shrinking and releasing just
   subtract.

   Only declare memory living on the core's device: match *deviceUUID* or
   *deviceLUID* from getVulkanCoreInfo_ against your API's device
   enumeration, and keep a second GPU's bytes out of this core's budget.
   Never declare bytes the core already accounts — anything from
   createGPUBuffer_, allocateGPUMemory_ or GPU frames — or they count twice.
   On unified memory devices this bookkeeping matters doubly, since the
   pool's bytes and the host's are the same RAM.

----------

.. _updateGPUMemoryReservation:

void updateGPUMemoryReservation(VSGPUMemoryReservation_ \*reservation, int64_t bytes)

   Republishes the reservation's total. *bytes* is an absolute total, not a
   delta: keep publishing what you hold and drift is impossible, however the
   calls interleave. Concurrent updates are safe and the last written total
   wins; negative is treated as zero. An increase runs the same eviction an
   initial reservation does.

----------

.. _releaseGPUMemoryReservation:

void releaseGPUMemoryReservation(VSGPUMemoryReservation_ \*reservation)

   Drops the whole reservation together with the handle; NULL is ignored.
   Late release — in the filter's free callback — is the intended place.

----------

.. _compileGPUShader:

VSGPUShader_ \*compileGPUShader(VSCore \*core, int language, const char \*source, char \*errorLog, int errorLogSize)

   Compiles compute shader source to SPIR-V at runtime through the statically
   embedded glslang, so plugins can ship readable kernels instead of blobs
   and need no shader toolchain at build time. Pure CPU work: no device is
   touched, and no optimizer runs — drivers optimize anyway, and whoever
   wants pre optimized modules keeps shipping ``glslc -O`` output, since both
   feed the identical pipeline creation path.

   *language* is a VSGPUShaderLanguage_; only GLSL exists today and unknown
   values fail so a future language becomes an additive value rather than an
   ABI event. The accepted dialect is pinned by the core as a platform
   property: write ``#version 460``, compiled for the Vulkan 1.4 client
   targeting SPIR-V 1.6, compute stage only. Specialize by concatenating a
   ``#define`` preamble in front of the kernel body — there is no include
   handler, which also replaces most uses of specialization constants and
   static shader variant multiplication.

   Results are cached per core by source text, so every filter instance
   compiling the same kernel shares one parse and one copy of the words;
   repeated compilation is therefore cheap enough to do per instance. Returns
   NULL with *errorLog* filled (including the compiler's messages) on
   failure.

----------

.. _getGPUShaderCode:

const uint32_t \*getGPUShaderCode(const VSGPUShader_ \*shader, size_t \*sizeInBytes)

   The compiled SPIR-V words, ready for VkShaderModuleCreateInfo (with
   maintenance5, chained straight into pipeline creation). Valid until
   freeGPUShader_; the handle is independent of everything else, including
   the core that compiled it.

----------

.. _freeGPUShader:

void freeGPUShader(VSGPUShader_ \*shader)

   Releases the shader handle. The typical lifetime is short: compile, create
   the pipeline, free — the per core cache keeps the words for the next
   instance.

----------

.. _createGPUExecPool:

VSGPUExecPool_ \*createGPUExecPool(VSCore \*core, int queue, char \*errorMessage, int errorMessageSize)

   Creates an exec pool on one of the core's queues (VSVulkanQueueType_).

   A pool on *vqTransfer* may only ever record copies, and does not drive the
   core's progress timeline, so the in-flight budget below falls back to polling
   for it. A pool anything is dispatched into belongs on *vqCompute*.

   The core sizes the pool's context ring itself, from its worker thread
   count — how many recordings can even be concurrent is core knowledge, not
   filter knowledge — and how much memory queued submissions may pin is
   bounded separately: acquiring waits out the ring's oldest submission, and
   may additionally wait on the core's device-wide in-flight budget, which
   caps the total bytes queued submissions retain across all pools. Filters
   notice nothing but an occasional slower acquire when a graph runs far
   ahead of the GPU. The pool's timeline is created exportable when the
   device can, so consumers in other APIs can wait the producer pairs it
   publishes.

   Destroy it in the filter's free callback; freeGPUExecPool_ drains the GPU
   first, so everything it still holds is released safely.

   Retained objects — frames from gpuExecReadsFrame_, scratch from
   gpuExecUsesBuffer_ and gpuExecUsesMemory_, anything from gpuExecRetain_ —
   are released once their submission is known complete: every submit on the
   pool reaps the other contexts' completed retentions (about one submission
   of lag while active), and the context's next acquire, pool destruction and
   the core's memory pressure sweeps cover the rest, so a pool gone idle does
   not park its last submissions' footprint. That reclamation is a pool-only
   property; references a filter retains privately on the raw path are
   invisible to the core and cannot be freed by pressure.

----------

.. _freeGPUExecPool:

void freeGPUExecPool(VSGPUExecPool_ \*pool)

   Drains every submission the pool made, releases everything they retained,
   and destroys the pool. NULL is ignored.

----------

.. _gpuExecPoolWaitIdle:

int gpuExecPoolWaitIdle(VSGPUExecPool_ \*pool, char \*errorMessage, int errorMessageSize)

   Blocks until every submission made through this pool has completed.
   Filters do not need this per frame — producer pairs make consumers wait on
   the device instead — but one shot setup work, such as uploading weights or
   tables a filter will read for the rest of its life, has to know the copy
   landed before recording anything that reads it. Also releases everything
   those submissions were keeping alive.

----------

.. _gpuExecPoolTimeline:

VSGPUTimeline_ \*gpuExecPoolTimeline(VSGPUExecPool_ \*pool)

   The pool's timeline as the counted object setGPUPlaneProducer_ takes, for
   publishing producer pairs by hand on frames the pool does not know about —
   the out of order producer case, where the submission that wrote a plane
   was submitted calls ago and gpuExecSubmit_'s *signaledValue* was saved for
   this moment. The pool holds its own reference, so publishing it needs no
   reference of yours; getGPUTimelineSemaphore_ gives the raw handle when
   exportGPUSemaphore_ needs one.

   Never signal that handle yourself: the pool allocates signal values under
   the queue lock at submit, and an external signal races them on a timeline
   where values must only increase.

----------

.. _gpuExecAcquire:

VSGPUExecContext_ \*gpuExecAcquire(VSGPUExecPool_ \*pool, char \*errorMessage, int errorMessageSize)

   Claims a context and begins recording; returns NULL with the error set on
   device loss. Every acquire must end in exactly one gpuExecSubmit_ or
   gpuExecAbandon_. May block briefly while the ring's oldest submission
   finishes or the in-flight retention budget frees up.

----------

.. _gpuExecCommandBuffer:

VkCommandBuffer gpuExecCommandBuffer(VSGPUExecContext_ \*context)

   The command buffer being recorded: put anything Vulkan allows into it.

----------

.. _gpuExecReadsFrame:

void gpuExecReadsFrame(VSGPUExecContext_ \*context, const VSFrame \*frame)

   Declares that this submission reads the frame: its planes' producer pairs
   become device side waits, and the frame is kept alive until the submission
   completes. Takes its own reference, so the caller still releases its own
   reference normally.

----------

.. _gpuExecWritesPlane:

void gpuExecWritesPlane(VSGPUExecContext_ \*context, VSFrame \*frame, int plane)

   Declares that this submission writes the plane: gpuExecSubmit_ publishes
   the pool's (timeline, value) on it as the producer pair.

----------

.. _gpuExecUsesBuffer:

void gpuExecUsesBuffer(VSGPUExecContext_ \*context, VSGPUBuffer_ \*buffer)

   Hands a scratch buffer to the context, which destroys it once the
   submission completes. Ownership transfers; do not destroy it yourself.

----------

.. _gpuExecUsesMemory:

void gpuExecUsesMemory(VSGPUExecContext_ \*context, VSGPUMemory_ \*memory)

   gpuExecUsesBuffer_ for a bare region: the region stays out of the pool
   until the submission being recorded has completed, and its bytes count
   against the in-flight retention budget meanwhile. Ownership transfers; do
   not free it yourself. Release follows the ordinary retention timing
   described at createGPUExecPool_, so what is guaranteed is that the region
   is never recycled early, not that it returns at a particular moment.

   The resource bound to the region is still yours to destroy, since this
   never saw it. A resource whose lifetime is the filter's — created once,
   reused every frame — is the easy case and wants no retention at all. One
   created per frame has to outlive its submission too, so either keep those
   handles and destroy them when the filter is freed, or register the whole
   set with gpuExecRetain_.

----------

.. _gpuExecRetain:

void gpuExecRetain(VSGPUExecContext_ \*context, VSGPUReleaseFunc_ release, void \*object, VkDeviceSize bytes)

   Cleanup of your own on the retention list, for what the typed calls above
   cannot name. ``release(object)`` runs once the submission being recorded
   has completed, on the schedule described at createGPUExecPool_; call it
   between acquire and submit like the others, and gpuExecAbandon_ runs it
   immediately since nothing will execute.

   This is what a per frame image wants, because the objects that have to
   outlive a submission are not only the ones holding memory: an image, every
   view recorded against it, and the region underneath all retire together,
   and one registration of a struct holding the three frees them in the right
   order — views, image, then freeGPUMemory_ — where a call per object could
   not express the ordering. Anything else a submission borrowed can ride
   along the same way.

   *bytes* is what the object pins in device memory, counted against the
   in-flight retention budget until release; pass 0 for host side
   bookkeeping, and do not count bytes the typed calls already counted for
   the same object.

   The callback runs on whichever thread reaps the submission — another
   filter's submit, or a core memory pressure sweep — so it must be safe to
   call from any thread. No pool lock is held while it runs, so it may take
   its own; it must not acquire a context from the pool that is reaping it.

----------

.. _gpuExecSubmit:

int gpuExecSubmit(VSGPUExecContext_ \*context, uint64_t \*signaledValue, char \*errorMessage, int errorMessageSize)

   Ends recording and submits, allocating the timeline value inside the queue
   lock so signals reach the queue in increasing order, then publishes the
   producer pairs. The context is consumed either way, success or failure.

   *signaledValue*, when non-NULL, receives the value this submission signals
   on the pool's timeline. Waiting for it — vkWaitSemaphores on
   ``getGPUTimelineSemaphore(gpuExecPoolTimeline(pool))`` — waits for exactly
   this submission, which is what a filter reading results back on the host
   wants: gpuExecPoolWaitIdle_ also works but waits the pool's newest
   submission, so concurrent frames serialize on each other's work. Filters
   that only produce planes never need either; the producer pairs carry the
   synchronization.

----------

.. _gpuExecAbandon:

void gpuExecAbandon(VSGPUExecContext_ \*context)

   Gives up a recording without submitting: everything retained is released
   at once, including gpuExecRetain_ callbacks, and the context returns to
   the ring. The way out of an error discovered halfway through recording.

----------

.. _exportGPUPlane:

int exportGPUPlane(const VSFrame \*frame, int plane, VSVulkanExportedMemory_ \*out, char \*errorMessage, int errorMessageSize)

   Exports the allocation backing a GPU frame plane as an opaque handle
   another API in the process can import; see VSVulkanExportedMemory_ for the
   identity, ownership and synchronization rules. Only available when
   VSVulkanCoreInfo_::exportHandleType is nonzero; fails on CPU frames and
   missing planes. This is how a CUDA filter reads and writes frames in
   place: import once per *memoryId*, map, and run kernels on the same VRAM
   the Vulkan buffers occupy.

----------

.. _exportGPUSemaphore:

int exportGPUSemaphore(VSCore \*core, VkSemaphore semaphore, VSVulkanExportedSemaphore_ \*out, char \*errorMessage, int errorMessageSize)

   Exports a timeline semaphore as an opaque handle another API can import.
   Use it on the *readySemaphore* of a plane you consume — every core exec
   pool timeline is created exportable when the capability exists — to wait
   the producer pair on the device instead of the host, and on your own
   timeline (createGPUTimeline_, or one made with VkExportSemaphoreCreateInfo
   and the handle type from VSVulkanCoreInfo_::semaphoreExportHandleType) to
   signal your producer pairs from the foreign API. Only available when
   VSVulkanCoreInfo_::semaphoreExportHandleType is nonzero.

   Third party filters are not obliged to create exportable timelines: when
   the export of some other plugin's producer semaphore fails, fall back to
   waitGPUFrame_ for that frame.

----------

.. _waitGPUFrame:

int waitGPUFrame(const VSFrame \*frame, char \*errorMessage, int errorMessageSize)

   Host waits every plane's producer pair and makes the completed writes
   available outside the device's own domain — the part a bare producer wait
   does not give you, since the spec only defines cross device visibility
   through external semaphores or an availability chain like this one. Call
   once per frame before reading it through an exported handle; costs one
   submission round trip. Frames consumed by Vulkan work on the same device
   never need this, their producer pairs carry the dependency. Foreign
   consumers that import the producer pair through exportGPUSemaphore_ do not
   need it either — a device side external wait carries the dependency.
