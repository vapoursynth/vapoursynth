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

   VSGPUShaderLanguage_

Structs_
   VSVULKANAPI_

   VSVulkanFunctions_

   VSVulkanCoreHandles_

   VSVulkanPlaneInfo_

   VSGPUBuffer_

   VSVulkanBufferInfo_

   VSVulkanExportedMemory_

   VSVulkanExportedSemaphore_

   VSVulkanCoreInfo_

   VSVulkanDeviceListEntry_

Functions_
   setVulkanDevice_

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

   exportGPUPlane_

   waitGPUFrame_

   exportGPUSemaphore_

   compileGPUShader_

   getGPUShaderCode_

   freeGPUShader_


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

.. _VSGPUShader:

struct VSGPUShader
------------------

   Opaque handle to a runtime compiled shader holding the SPIR-V words,
   returned by compileGPUShader_ and read through getGPUShaderCode_. It is
   independent of everything else once returned — it stays valid after the
   core that compiled it is freed — and is released with freeGPUShader_.


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
instance, since the producer pair contract guarantees those semaphores
outlive every consumer. Handle ownership follows the same platform rules as
VSVulkanExportedMemory_.

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
   synchronized and the core and other plugins submit to the same queues.
   Allocate your timeline values inside the lock so their signal order
   matches their numeric order.

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

----------

.. _exportGPUSemaphore:

int exportGPUSemaphore(VSCore \*core, VkSemaphore semaphore, VSVulkanExportedSemaphore_ \*out, char \*errorMessage, int errorMessageSize)

   Exports a timeline semaphore as an opaque handle another API can import.
   Use it on the *readySemaphore* of a plane you consume — every core exec
   pool timeline is created exportable when the capability exists — to wait
   the producer pair on the device instead of the host, and on your own
   timeline (created with VkExportSemaphoreCreateInfo, handle type from
   VSVulkanCoreInfo_::semaphoreExportHandleType) to signal your producer
   pairs from the foreign API. Only available when
   VSVulkanCoreInfo_::semaphoreExportHandleType is nonzero.

   Third party filters are not obliged to create exportable timelines: when
   the export of some other plugin's producer semaphore fails, fall back to
   waitGPUFrame_ for that frame.

----------

.. _compileGPUShader:

VSGPUShader \*compileGPUShader(VSCore \*core, int language, const char \*source, char \*errorLog, int errorLogSize)

   Compiles compute shader source to SPIR-V at runtime through the statically
   embedded glslang, so plugins can ship readable kernels instead of blobs
   and need no shader toolchain at build time. Pure CPU work: no device is
   touched, and no optimizer runs — drivers optimize anyway, and whoever
   wants pre optimized modules keeps shipping ``glslc -O`` output, since both
   feed the identical pipeline creation path.

   *language* is a VSGPUShaderLanguage; only GLSL exists today and unknown
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

const uint32_t \*getGPUShaderCode(const VSGPUShader \*shader, size_t \*sizeInBytes)

   The compiled SPIR-V words, ready for VkShaderModuleCreateInfo (with
   maintenance5, chained straight into pipeline creation). Valid until
   freeGPUShader_; the handle is independent of everything else, including
   the core that compiled it.

----------

.. _freeGPUShader:

void freeGPUShader(VSGPUShader \*shader)

   Releases the shader handle. The typical lifetime is short: compile, create
   the pipeline, free — the per core cache keeps the words for the next
   instance.
