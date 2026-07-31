GPU Filters
===========

VapourSynth can run filters on a Vulkan GPU. GPU filters exchange frames that
live in video memory, so a chain of them processes without the frames ever
crossing the PCIe bus; transfers happen only at the boundaries between CPU and
GPU parts of the graph. This page describes the model from the user side, then
from the plugin author side.

Requirements
############

A GPU and driver with conformant Vulkan 1.4 support. That is the entire
hardware gate: everything VapourSynth requires beyond the version is mandatory
for conformant 1.4 implementations, so there is no fine print. In practice
this means reasonably recent hardware from every desktop vendor, and macOS
through MoltenVK. The Vulkan runtime is loaded dynamically; systems without it
simply have no GPU support, and nothing else changes.

Whether a specific device qualifies can be checked from Python with
``core.vulkan_devices``, which lists every device with a usable flag and, when
not usable, the first requirement it failed.

Using GPU filters
#################

GPU filters take and return a distinct node type, ``vknode``, whose frames are
GPU resident. The boundaries are explicit filters:

* std.GPUUpload_ turns a ``vnode`` into a ``vknode``
* std.GPUDownload_ turns a ``vknode`` back into a ``vnode``

.. _std.GPUUpload: functions/video/gpuupload_gpudownload.html
.. _std.GPUDownload: functions/video/gpuupload_gpudownload.html

Passing a CPU clip to a GPU filter works anyway: the core automatically
inserts the upload and logs that it did. Chains therefore compose naturally::

   blurred = core.std.GPUBoxBlur(clip, hradius=8, hpasses=2)  # upload auto-inserted
   result  = blurred.std.GPUDownload()                         # back to CPU frames

Every consecutive run of GPU filters should stay on the GPU; a round trip per
filter costs more than most filters do. ``set_output`` and ``output`` on a
``vknode`` insert the download automatically, with a log message, so scripts
and vspipe just work. Reading pixel data of a GPU resident *frame* from
Python raises an error — pass the clip through GPUDownload first; whether a
node or frame is GPU resident is exposed as the ``gpu_resident`` property.
Frame properties are always CPU side and work normally on GPU frames. (In the
C API, reading a GPU frame's planes is a fatal error instead: for compiled
plugins it is a programming error and must fail loudly, never silently
download.)

Device selection
################

One Vulkan device per core, chosen automatically the first time it is needed:
the most powerful suitable device, preferring discrete over integrated and
deciding ties by memory size. To pick explicitly, call
``core.set_vulkan_device(index)`` **before** any GPU filter is created, with
an index from ``core.vulkan_devices``. ``core.vulkan_device_info`` reports the
active device, its live memory budget and VapourSynth's current use.

VRAM and caching
################

GPU frames participate in caching exactly like CPU frames, against a separate
VRAM budget that defaults to 80% of what the driver reports as available to
the process. ``core.set_max_vram_use(bytes)`` adjusts it. Under pressure the
cache evicts GPU frames, returns the memory to the driver, and the thread pool
throttles frame requests the same way it does when host memory runs short —
workloads far larger than VRAM complete correctly, just slower.

Environment variables
#####################

``VS_VULKAN_VALIDATION``
   When set, the Khronos validation layer is enabled on core created devices
   (if installed) and its messages go to the core's log. Development tool;
   costs performance.

``VS_VULKAN_MAX_VRAM_MB``
   Overrides the default VRAM limit, mainly for exercising the pressure paths
   with small budgets. Must be set before the process starts.


Writing GPU filters
###################

Everything below is for plugin authors. The complete API reference lives in
:doc:`api/vsvulkan4.h`; this section is the narrative version. GPU filters
are ordinary VapourSynth filters — same registration, same getframe callback,
same frame request pattern — that record Vulkan compute work instead of
touching pixels with the CPU.

Start from the examples
-----------------------

The sdk dir contains two deliberately small filters that, together with the
in-tree GPUBoxBlur (src/core/vsvulkanfilters.cpp), cover the three
fundamental kernel shapes:

+---------------------------+-----------+---------------------------------------------------------------+
| Example                   | Shape     | What it demonstrates                                          |
+===========================+===========+===============================================================+
| gpu_invert_example.c      | map       | The full asynchronous pattern: producer pair waits, own       |
|                           |           | timeline, publishing producers, keeping sources alive with a  |
|                           |           | retained ring, a command buffer slot ring for frames in       |
|                           |           | flight.                                                       |
+---------------------------+-----------+---------------------------------------------------------------+
| GPUBoxBlur (in-tree)      | stencil   | Multi-pass kernels with barriers, scratch reuse, plane        |
|                           |           | sharing for unprocessed planes.                               |
+---------------------------+-----------+---------------------------------------------------------------+
| gpu_planestats_example.c  | reduce    | Scratch buffers that are not frame shaped, a compute→compute  |
|                           |           | barrier between dependent dispatches, and the one legitimate  |
|                           |           | host wait — results delivered as frame properties must exist  |
|                           |           | before returning. The wait removes every piece of async       |
|                           |           | machinery the invert example needs, which is the lesson.      |
+---------------------------+-----------+---------------------------------------------------------------+
| gpu_cuda_invert_example.cu| foreign   | The complete CUDA interop pattern: UUID device matching,      |
|                           | API       | cached memory imports, device side producer pair waits with   |
|                           |           | graceful host sync fallback, and signalling its own           |
|                           |           | exportable timeline from the stream. Reference code — it has  |
|                           |           | not run on NVIDIA hardware yet.                               |
+---------------------------+-----------+---------------------------------------------------------------+

A filter's obligations
----------------------

#. **Declare residency.** Register with ``vknode`` argument and return types,
   and create the filter with createVideoFilterEx passing ``ffGPUOutput``.
   The core verifies all three layers agree and auto-inserts transfers for
   CPU inputs.

#. **Call Vulkan through the core.** getVulkanFunctions returns the loaded
   dispatch table; getVulkanHandles supplies the raw handles and
   ``getInstanceProcAddr`` for anything outside it. Never create your own
   instance or device.

#. **Wait producers device side.** For every plane you read, make your
   submission wait its (semaphore, value) pair from getGPUPlane. Deduplicate
   per semaphore to the highest value. Never wait on the host for input
   planes.

#. **Lock the queue.** Every vkQueueSubmit happens between lockVulkanQueue
   and unlockVulkanQueue, and timeline values are allocated inside that lock
   so signals reach the queue in increasing order.

#. **Publish your producers.** After submitting, setGPUPlaneProducer on every
   plane you wrote with your timeline and the signaled value. Your timeline
   must outlive all consumers — it lives as long as the filter instance.

#. **Keep sources alive.** The GPU may still be reading a source frame long
   after your getframe returned. Hold the reference until your submission's
   value completes; the examples sweep a small ring with
   vkGetSemaphoreCounterValue, falling back to a blocking wait when full.

#. **Bound your frames in flight.** Reusing a per-stream command buffer or
   scratch buffer must wait out its previous submission; the size of that
   ring is your filter's pipelining depth. Filters run ``fmParallel`` — do
   short internal locking, never hold a lock across a GPU wait.

#. **Clean up in order.** In the free callback: wait your final timeline
   value, then destroy pipelines, pools, scratch buffers and the timeline.
   Scratch buffers have no producer pair anyone waits on — destroying them
   before the device finished using them is a bug the core cannot catch.

When to wait on the host
------------------------

Never for frame data — that is what producer pairs are for, and host waits
destroy the pipelining the whole design exists to provide. The exception is
results that must be CPU visible before your getframe returns, i.e. frame
properties: a reduction writing its result into a mapped buffer must wait for
its own submission, and in exchange needs none of the asynchronous machinery
(no retained sources, no producer publication for planes it merely passes
through). gpu_planestats_example.c is exactly this trade.

Scratch memory
--------------

createGPUBuffer/destroyGPUBuffer draw from the core's pooled allocator:
accounted against the VRAM limit, visible to admission control, recycled
through size buckets so per-frame allocate/destroy is cheap. Request
DEVICE_LOCAL for working memory; request HOST_VISIBLE|HOST_COHERENT
(preferring HOST_CACHED) for small readback buffers, which arrive
persistently mapped. Long lived constant data (weight tables and the like)
belongs in a DEVICE_LOCAL buffer filled once through a staging copy at
filter creation.

Borrowing frames from CUDA and other APIs
-----------------------------------------

A filter implemented with CUDA (or any API that can import opaque memory
handles) does not copy frames across — it wraps them. Frames stay owned by
the core's Vulkan allocator; exportGPUPlane hands out an opaque handle plus
the plane's (offset, size) within its backing allocation, and the foreign API
imports that allocation and reads or writes the very same VRAM. Since planes
are linear pitched buffers with CPU strides, the wrapped pointer behaves like
a CPU plane pointer; most existing CUDA kernels port with a pointer swap.

The pattern, per frame:

#. Export each plane you touch. Cache imports keyed by *memoryId* — one
   ``cudaImportExternalMemory`` per 128 MB allocation, then per-plane
   pointers are just base + offset. Close surplus handles per the ownership
   rules on VSVulkanExportedMemory.
#. Allocate the output with newGPUVideoFrame and wrap its planes the same
   way — foreign kernels write directly into what downstream Vulkan filters
   will read.
#. Synchronize. Two options, and the second is strongly preferred:

   * **Host side**: call waitGPUFrame on each input frame before launching
     (it waits the producer pairs *and* makes the writes available outside
     the device, which a bare semaphore wait does not), and finish your work
     (``cudaStreamSynchronize``) before returning, publishing no producer
     pair. Simple, correct, and it parks a worker thread for the whole GPU
     round trip.

   * **Device side**: import each input plane's *readySemaphore* through
     exportGPUSemaphore and enqueue a wait on your stream
     (``cudaWaitExternalSemaphoresAsync`` with the pair's value), signal your
     own exportable timeline at the end
     (``cudaSignalExternalSemaphoresAsync``), publish that (semaphore, value)
     with setGPUPlaneProducer, and return immediately. Nothing blocks; the
     graph pipelines across the API boundary exactly as it does between
     Vulkan filters. Create your timeline with VkExportSemaphoreCreateInfo
     using the handle type from
     ``VSVulkanCoreInfo::semaphoreExportHandleType``, and take on the
     asynchronous obligations that come with it: retain source frames until
     your signalled value completes, and let the timeline live as long as the
     filter instance.

   Not every producer's timeline is exportable — third party filters may not
   opt in — so when exportGPUSemaphore fails on an input, fall back to
   waitGPUFrame for that frame.

Match devices by UUID: ``VSVulkanCoreInfo::deviceUUID`` equals the UUID CUDA
reports for the same GPU. Whether export is available at all is
``VSVulkanCoreInfo::exportHandleType`` (memory) and
``semaphoreExportHandleType`` (semaphores) — both require the platform's
opaque handle extensions and are absent on MoltenVK, so a CUDA-backed filter
should fail creation with a clear message when memory export is 0, and fall
back to host synchronization when only the semaphore half is missing. Cached
imports may safely outlive the frames that led to them: the OS keeps an
imported allocation alive until the importer releases it.

Performance notes
-----------------

* **Submissions have a floor** of roughly 0.2 ms. Batch a frame's planes and
  passes into one submission; per-plane submissions drown small frames in
  overhead.

* **Never move rows with per-row copy regions.** Copy engines charge per
  region; a frame's worth of row-granular VkBufferCopy2 regions costs
  milliseconds. Row interleaving, field extraction and similar reshaping
  belong in a trivial compute kernel — or better, have your kernels read and
  write strided so the reshaping disappears entirely.

* **Stay resident.** A CPU↔GPU round trip costs about 0.75 ms at 1080p and
  5.4 ms at 4K16; a resident filter pass costs a small fraction of that.
  Design filters to chain, and let unprocessed planes be shared rather than
  copied (newVideoFrame2).

* **Pin your subgroup size if your kernel depends on it.** Since Vulkan 1.3
  the subgroup size may vary per pipeline unless pinned at creation
  (subgroupSizeControl and computeFullSubgroups are always enabled). A GEMV
  style kernel that maps work to lanes breaks silently on wave64/wave32
  hardware without this.

* **Declare only the SPIR-V capabilities you use.** Capabilities like
  VulkanMemoryModel or VariablePointers can make shader compilation more
  conservative in exactly the shaders that declare them.

The feature baseline
--------------------

Kernels may unconditionally target every feature the core requires — 8/16-bit
storage and arithmetic, scalar block layout, subgroup basic/vote/arithmetic/
ballot/shuffle/rotate including extended types, integer dot product, variable
pointers, the dynamic indexing set, push descriptors, maintenance5 module-less
pipeline creation, timeline semaphores and synchronization2. The precise list
lives in the VSVulkan4.h header. Two capabilities are
optional and must be queried: ``shaderFloat16`` (half precision arithmetic)
and ``hostImageCopy``. Everything else that is optional in Vulkan is simply
absent: core devices enable no extensions, and vendor specific paths cannot
exist on them.

Porting an existing Vulkan filter
---------------------------------

Filters that already run their own Vulkan device port mechanically: delete
instance/device management and per-frame transfers, take frames as
``vknode``, move any CPU side data reshaping into small kernels, and let
scratch come from createGPUBuffer. The shading code itself usually moves
unchanged — kernels neither know nor care who created the device. Ported
filters gain resident chaining with every other GPU filter and centralized
VRAM budgeting, and lose only whatever depended on vendor extensions the
core does not enable.
