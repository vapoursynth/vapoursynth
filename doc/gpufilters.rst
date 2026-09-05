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

GPU filters take and return video nodes declared ``vnode:gpu``, whose frames
are GPU resident. The boundaries are explicit filters:

* std.GPUUpload_ turns a plain ``vnode`` into a ``vnode:gpu``
* std.GPUDownload_ turns a ``vnode:gpu`` back into a plain ``vnode``

.. _std.GPUUpload: functions/video/gpuupload_gpudownload.html
.. _std.GPUDownload: functions/video/gpuupload_gpudownload.html

Passing a CPU clip to a filter that requires GPU frames works anyway: the core
automatically inserts the upload and logs that it did. Chains therefore compose
naturally::

   gpu     = clip.std.GPUUpload()                          # explicit, once
   blurred = core.std.BoxBlur(gpu, hradius=8, hpasses=2)   # runs on the device
   result  = blurred.std.GPUDownload()                     # back to CPU frames

std.BoxBlur_ shows the shape most internal filters will take as they gain GPU
support: one filter, one name, choosing its implementation from the residency
of its input. Uploading once at the head of a chain is then the only thing a
script does differently.

.. _std.BoxBlur: functions/video/boxblur.html

Every consecutive run of GPU filters should stay on the GPU; a round trip per
filter costs more than most filters do. Filters that never touch pixel data
declare ``vnode:all``: they accept CPU and GPU clips alike without any
transfer and their output residency follows their input, so none of them
interrupts a resident chain. That covers the reorder filters — Trim, Splice,
Reverse, Loop, Interleave, SelectEvery, DuplicateFrames, DeleteFrames,
FreezeFrames — the property and metadata filters — AssumeFPS, SetFrameProp,
SetFrameProps, SetFieldBased, CopyFrameProps, RemoveFrameProps, ClipToProp,
PropToClip, SetVideoCache — plane reference
shuffling — ShufflePlanes, SplitPlanes — and the deferred producers FrameEval
and ModifyFrame, whose template clip fixes the residency the returned clips
or frames must match, while their ``prop_src``/``clips`` frames may be either
residency (properties are always CPU side). Where several clips contribute
actual planes to one output (Splice, Interleave, ShufflePlanes) they must
share one residency; mixing is an error rather than a hidden transfer.
Property-only inputs like ``prop_src`` and ClipToProp's ``mclip`` are exempt.
A script may leave its outputs GPU resident: ``set_output`` stores the node as
given, and the consumer decides where the download goes — ``output`` inserts
one, and so do the VSScript entry points vspipe and most applications use,
each with a log message, so scripts just work. A clip and its alpha must share
a residency, which is the one thing ``set_output`` checks. Reading pixel data
of a GPU resident *frame* from
Python raises an error — pass the clip through GPUDownload first; whether a
node or frame is GPU resident is exposed as the ``gpu_resident`` property.
Frame properties are always CPU side and work normally on GPU frames. (In the
C API the same access returns NULL instead of raising: getReadPtr and
getWritePtr have no host address to give and never download silently, and
getFrameResidency tells that apart from a bad plane index.)

A filter with a compute path takes every call it accepts at all; none of them
quietly downloads its inputs and runs the scalar code instead, so a resident
chain stays resident and a shape a filter cannot handle is an error rather
than a silent transfer. Where several clips contribute planes they must share
one residency, as everywhere else; mixing is likewise an error.

**GPU filters require a constant format and constant dimensions.** A kernel is
compiled from the clip's format and its frames are allocated at the clip's
dimensions, so a variable clip has nothing to build from and is refused at
creation with a message naming the remedy — pass it through std.GPUDownload_
and process it on the CPU, which handles variable clips as it always has. This
applies to resize as well: the scalar resizers turn varying input into constant
output, but on a resident clip that conversion is not available.

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
VRAM budget that defaults to two thirds of what the driver reports as
available to the process — the remainder is headroom for the transient
working sets of large processing filters and for the rest of the system.
``core.max_vram_cache_size`` adjusts it, in megabytes, exactly as
``core.max_cache_size`` does for host memory. Under pressure the cache evicts
GPU frames, returns the memory to the driver, and the thread pool throttles
frame requests the same way it does when host memory runs short — workloads
far larger than VRAM complete correctly, just slower.

Submissions the GPU has not executed yet pin their sources and scratch, so a
deep graph recording far ahead of the device would turn queue depth into pure
VRAM cost; the core bounds those in-flight bytes to a quarter of the VRAM
limit and briefly holds back new recordings past it, which trades memory
nothing needs for depth nothing uses.

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

The sdk dir contains four deliberately small Vulkan filters and one CUDA
filter that, together with the in-tree GPU path of std.BoxBlur
(src/core/boxblurfilter.cpp), cover the fundamental kernel shapes. The first
three are the same invert filter written at three levels of abstraction, so
reading them in order shows exactly what each layer takes over:

+---------------------------+-----------+---------------------------------------------------------------+
| Example                   | Shape     | What it demonstrates                                          |
+===========================+===========+===============================================================+
| gpu_invert_example.c      | map       | The standard shape of a GPU filter: an execution pool         |
|                           |           | created with the filter, then per frame acquire, declare      |
|                           |           | reads and writes, record one dispatch per plane, submit.      |
|                           |           | Also the runtime compilation pattern: its kernel ships as     |
|                           |           | GLSL source and compileGPUShader turns it into SPIR-V at      |
|                           |           | filter creation. Start here.                                  |
+---------------------------+-----------+---------------------------------------------------------------+
| gpu_invert_raw_example.c  | map, by   | The same filter without the pool: producer pair waits, own    |
|                           | hand      | timeline, queue locking, publishing producers, keeping        |
|                           |           | sources alive with a retained ring, a command buffer slot     |
|                           |           | ring for frames in flight. Read it to see everything the      |
|                           |           | pool discharges, or as the template for filters whose         |
|                           |           | submissions the pool cannot carry.                            |
+---------------------------+-----------+---------------------------------------------------------------+
| gpu_invert_driver\_       | map, from | The same filter again, declared through gpufilter.h rather    |
| example.cpp               | a         | than recorded: the driver owns the frame loop AND the         |
|                           | declara-  | pipeline, so what is left is one GLSL statement per sample    |
|                           | tion      | type and a callback filling the parameter block. Handles      |
|                           |           | float as well as integer, which by hand would be a second     |
|                           |           | kernel and a second pipeline. The shortest of the three,      |
|                           |           | and the shape almost every pixel filter fits.                 |
+---------------------------+-----------+---------------------------------------------------------------+
| BoxBlur GPU path          | stencil   | Multi-pass kernels with barriers, scratch reuse, plane        |
|                           |           | sharing for unprocessed planes. Compiled into the core but    |
|                           |           | written against nothing but these public headers, in its own  |
|                           |           | translation unit so that stays true, which makes              |
|                           |           | src/core/boxblurfilter.cpp readable as a plugin would be.     |
+---------------------------+-----------+---------------------------------------------------------------+
| PlaneStats GPU path       | reduce    | A filter that produces no pixels at all: every plane is left  |
|                           |           | unprocessed and the result leaves as frame properties.        |
|                           |           | Declares readbackBytes for host visible output and a          |
|                           |           | finishReadback callback that turns the mapped records into    |
|                           |           | properties, so the driver owns the host wait that reading     |
|                           |           | them requires. Also specialization constants — subgroup size  |
|                           |           | pinned with requiredSubgroupSize — in                         |
|                           |           | src/core/simplefilters.cpp.                                   |
+---------------------------+-----------+---------------------------------------------------------------+
| gpu_cuda_invert_example.cu| foreign   | The complete CUDA interop pattern: UUID device matching,      |
|                           | API       | cached memory imports, device side producer pair waits with   |
|                           |           | graceful host sync fallback, and signalling its own           |
|                           |           | exportable timeline from the stream. Its work enters a CUDA   |
|                           |           | stream, which the exec pool cannot carry, so the raw          |
|                           |           | obligations are discharged by hand across the API boundary.   |
|                           |           | Reference code — it has not run on NVIDIA hardware yet.       |
+---------------------------+-----------+---------------------------------------------------------------+

Shaders reach the pipeline two ways. Every example here takes the first: ship
readable GLSL and compile it at creation through compileGPUShader, which
caches per core by source text, so many instances of the same kernel parse
once. The alternative is to commit SPIR-V a build step produced — ``glslc -O``
output as a header, say — and hand that to Program::spirv instead, needing no
compiler at runtime. Both reach the same maintenance5 pipeline creation, so
pick by taste, or by whether you want the optimizer pass the runtime path
deliberately omits since drivers optimize anyway.

Specialize by putting a preamble in front of the kernel body, which is what
BoxBlur does to get its four sample types out of one source::

   std::string preamble = "#version 460\n#define SAMPLE_T uint16_t\n";
   auto spirv = compile(preamble + kernelBody);

Note that ``#version`` has to be the very first token of a shader, so it
belongs in the preamble and the reusable body starts at the extension list.
Each distinct preamble is simply a different cache key.

A filter's obligations
----------------------

#. **Declare residency.** Register with ``vnode:gpu`` argument and return
   types, and create the filter with createVideoFilterEx passing
   ``ffGPUOutput``. The core verifies all three layers agree and auto-inserts
   transfers for CPU inputs. A filter whose code genuinely works on both
   residencies (it never touches pixel data) declares ``vnode:all`` instead
   and passes ``ffGPUOutput`` exactly when its input is GPU resident.

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
   plane you wrote with your timeline and the signaled value. The timeline is
   a reference counted ``VSGPUTimeline`` from createGPUTimeline (or the exec
   pool's, through gpuExecPoolTimeline), and every plane you publish it on
   takes its own reference — so it outlives your filter instance by itself
   whenever a frame still names it, and you never have to arrange that.

#. **Keep sources alive.** The GPU may still be reading a source frame long
   after your getframe returned. Hold the reference until your submission's
   value completes; the raw example sweeps a small ring with
   vkGetSemaphoreCounterValue, falling back to a blocking wait when full.

#. **Bound your frames in flight.** Reusing a per-stream command buffer or
   scratch buffer must wait out its previous submission; the size of that ring
   is your filter's pipelining depth.

   ``fmParallel`` is the usual choice, and it is what makes internal locking
   worth thinking about: getframe then runs concurrently on one instance, so
   keep any lock short and never hold one across a GPU wait — that hands back
   the concurrency the mode just gave you. The other modes are legal too;
   std.BlankClip is ``fmUnordered`` whenever *keep* is set. Serializing getframe
   does not serialize the device: a filter that records, submits and returns has
   every submitted frame in flight regardless of how the calls were spaced,
   since the producer pairs carry the ordering. What a serial mode costs is
   recording concurrency, and recording is microseconds against a submission
   floor of ~0.2 ms.

   The exception is a filter that WAITS in getframe — a readback reduction.
   There ``fmParallel`` is close to mandatory, because it is what lets other
   frames flow past the wait; any serial mode queues the waits behind each
   other and pipelining collapses to one frame at a time.

#. **Clean up in order.** In the free callback: wait your final timeline
   value, then destroy pipelines, pools and scratch buffers, and release your
   reference to the timeline. Scratch buffers have no producer pair anyone
   waits on — destroying them before the device finished using them is a bug
   the core cannot catch. The wait is about your own objects, not about
   consumers: frames you produced keep the timeline alive on their own, so
   freeGPUTimeline never has to wait for anybody.

Creating the output frame
-------------------------

There are two constructors and the choice is not stylistic — it decides whether
unprocessed planes cost a copy.

**newGPUVideoFrame(format, width, height, propSrc, core)** allocates every plane
fresh in VRAM. It is the GPU counterpart of newVideoFrame and behaves the same
in every other way, *propSrc* included, since properties are CPU side
regardless of residency. The planes come back with NULL producer pairs, meaning
"ready now" — which is a lie until your kernel has run, so publish the real pair
with setGPUPlaneProducer (or let gpuExecWritesPlane do it at submit) on every
plane you write, before returning the frame::

   VSFrame *dst = vkapi->newGPUVideoFrame(&fi, w, h, src, core);
   ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
   vkapi->gpuExecReadsFrame(ctx, src);
   for (int p = 0; p < fi.numPlanes; p++)
       vkapi->gpuExecWritesPlane(ctx, dst, p);
   /* record into vkapi->gpuExecCommandBuffer(ctx) */
   vkapi->gpuExecSubmit(ctx, NULL, err, sizeof(err));

**newVideoFrame2(format, width, height, planeSrc, planes, propSrc, core)** is
the one to reach for whenever the filter leaves some planes alone — a luma-only
filter, anything honouring a *planes* argument. It takes residency from the
source planes rather than from a flag: pass GPU resident frames and the result
is GPU resident, with each shared plane carrying its producer pair across
untouched, so a consumer still waits on whoever actually wrote it. Slots left
NULL are allocated as fresh GPU planes, exactly as newGPUVideoFrame would::

   const VSFrame *planeSrc[3] = { NULL, src, src };   /* process luma, share chroma */
   const int planeIdx[3]      = { 0, 1, 2 };
   VSFrame *dst = vsapi->newVideoFrame2(&fi, w, h, planeSrc, planeIdx, src, core);
   /* plane 0 is fresh and yours to write; 1 and 2 already point at src's VRAM */

Sharing is not an optimization detail — it is the difference between touching
one plane and copying three, and at 4K the copy costs more than most kernels.
Declare only the planes you actually write with gpuExecWritesPlane; publishing a
producer pair on a shared plane would overwrite the pair of the filter that
really produced it.

Every non-NULL entry must have the same residency. One frame cannot straddle
the bus, so a mixed set returns NULL — assemble on one side first. With every
entry NULL there is nothing to infer from and you get a CPU frame, which is why
the all-fresh case wants newGPUVideoFrame instead.

Two CPU-side facilities do not carry over. copyFrame on a GPU frame copies the
properties and shares the planes as usual, but the copy on write that normally
makes those planes independent cannot run for VRAM (`the API reference
<api/vapoursynth4.h.html#copyframe>`_ explains why), so the pixels of the copy
stay read only — enough for property editing, not a route to a writable
duplicate. And getReadPtr/getWritePtr return NULL on a GPU frame rather than
downloading behind your back; getGPUPlane is the way in, and getFrameResidency
distinguishes a resident frame from a bad plane index.

In debug builds configured with ``VS_FRAME_GUARD``, the guard bands the core
puts around CPU planes are absent from GPU planes and the verification is
skipped: there is no host pointer to inspect, and a kernel writing out of bounds
is the driver's and the validation layer's jurisdiction. Run with
``VS_VULKAN_VALIDATION`` when that is what you are hunting.

The execution pool
------------------

Obligations 3 through 8 are the same plumbing in every filter, so the core
ships it: create a VSGPUExecPool with the filter and per frame do ::

   ctx = gpuExecAcquire(pool);          /* backpressure: waits out the oldest submission (7) */
   gpuExecReadsFrame(ctx, src);         /* producer waits + keeps src alive (3, 6) */
   gpuExecWritesPlane(ctx, dst, p);     /* published as producer pairs on submit (5) */
   gpuExecUsesBuffer(ctx, scratch);     /* destroyed when the submission retires;
                                           gpuExecUsesMemory for a bare region,
                                           gpuExecRetain for anything else */
   /* ... record into gpuExecCommandBuffer(ctx) ... */
   gpuExecSubmit(ctx, NULL);            /* queue lock, values in queue order (4);
                                           non-NULL receives this submission's
                                           timeline value for host readback waits */

with the pool's context ring bounding frames in flight (7) — the core sizes
it from its worker thread count, since how many recordings can be concurrent
is core knowledge, not filter knowledge — and freeGPUExecPool draining the
device in the free callback (8). gpu_invert_example.c is this pattern whole,
and every in-tree GPU filter is built on it. The context hands out its
command buffer and imposes nothing on what goes into it — indirect dispatches,
custom barriers and query pools record the same way — so the raw path
underneath, spelled out by gpu_invert_raw_example.c, remains for filters whose
*submissions* the pool cannot carry: work entering another API's queue (the
CUDA example) or producer pairs published on frames the pool never sees.

The pool buys one more thing the raw path cannot have: participation in
memory pressure. What a context retains for a completed submission — source
frames, gpuExecUsesBuffer scratch — is reaped by every subsequent submit on
the pool (about one submission of lag while active), and what an idle pool
still holds is released by the core's periodic pressure sweeps and by the
allocation-failure escalation, so no pool parks its last submissions'
footprint while the rest of the graph fights for VRAM. A raw filter's
retained references are its own; the core cannot release what it does not
hold, so nothing can reclaim them until the filter's next call sweeps or the
instance dies. Raw filters should therefore sweep their ring on every call,
keep it as shallow as their real pipelining depth, and avoid retaining large
per-submission scratch — or put the scratch in the frame-shaped world the
core can see.

One level further up
--------------------

Obligations 1 to 8 and the recording itself are the same again in every filter
whose shape is "one dispatch per plane over frame planes and a few parameters",
which is most of them. The core factors that out into a declaration driver,
src/core/gpufilter.h, and every in-tree GPU filter except resize is written
against it: the filter supplies one GLSL statement per sample type and a
callback filling the parameter block, and the driver owns the frame loop and
the pipeline. gpu_invert_driver_example.cpp is the invert filter in that form,
next to the same filter written both other ways.

It is INTERNAL, not part of the installed API — inline code over VSVULKANAPI
with no ABI commitment, free to change shape between releases. Copy it beside
your source and build against your copy, the way VSHelper4.h is used, so a core
update cannot change what your plugin compiles. It needs C++20.

A filter outside the shape it models — indirect dispatch, its own descriptor
layout, a dispatch count that varies per frame — drops back to VSVULKANAPI and
looks like the two examples above. The two compose: such a filter still takes
its exec pool from the same API, so nothing in this section stops applying.

When to wait on the host
------------------------

Never for frame data — that is what producer pairs are for, and host waits
destroy the pipelining the whole design exists to provide. The exception is
results that must be CPU visible before your getframe returns, i.e. frame
properties: a reduction writing its result into a mapped buffer must wait for
its own submission before returning — gpuExecPoolWaitIdle is the sanctioned
form — and in exchange publishes nothing, since it produced no plane. The
PlaneStats GPU path is exactly this trade, and shows the easier way to take
it: declare readbackBytes and a finishReadback callback and the driver
performs the wait for you, leaving only the arithmetic that turns the mapped
records into frame properties.

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

Anything that is not a buffer takes a bare region of the same pool instead.
Memory is never passed to ``vkCreateImage`` — it is bound afterwards — so there
is nothing to gain from wrapping image creation: create the image your own way,
ask Vulkan what it needs, and allocate that with allocateGPUMemory, binding at
the *offset* that comes back rather than at the start of the block::

   vkGetImageMemoryRequirements2(device, &reqInfo, &req);
   VSGPUMemory *mem = allocateGPUMemory(core, &req.memoryRequirements,
       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &info, err, sizeof(err));
   VkBindImageMemoryInfo bind = { ..., image, info.memory, info.offset };
   vkBindImageMemory2(device, 1, &bind);

The offset matters because images are routinely aligned more coarsely than the
pool's regions, which is handled by reserving the distance. Two cases must not
bind here and neither is detectable from the requirements alone, so both are
yours to check: a resource created with external memory info (these blocks are
not exportable) and one whose ``VkMemoryDedicatedRequirements`` report
*requiresDedicatedAllocation*. Allocate those with vkAllocateMemory yourself.

Retention has a typed call per kind and a general one. gpuExecUsesBuffer hands
over a VSGPUBuffer, gpuExecUsesMemory a bare region, and gpuExecRetain takes a
callback plus a ``void *`` for everything else — which is what a per frame image
actually needs, since the objects that must outlive a submission are not only
the ones holding memory. An image, every view recorded against it and the region
underneath all retire together, and one registration of a struct holding the
three frees them in the right order (views, image, then freeGPUMemory) where a
call per object could not express the ordering. The *bytes* argument is what the
object pins in device memory, counted against the in-flight retention budget
from submit until release; pass 0 for host side bookkeeping. Retentions are released on the
schedule described above — the pool's next submit, the context's next acquire,
a pressure sweep or pool destruction — so the guarantee is that nothing is
recycled early, not that it comes back at a particular moment.

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
     Vulkan filters. Create your timeline with createGPUTimeline, which asks
     for export wherever ``VSVulkanCoreInfo::semaphoreExportHandleType`` says
     the device allows it, and take on the one asynchronous obligation that
     remains: retain source frames until your signalled value completes. The
     timeline itself needs no arranging — the frames you published it on keep
     it alive past your filter.

   Not every producer's timeline is exportable — third party filters may not
   opt in — so when exportGPUSemaphore fails on an input, fall back to
   waitGPUFrame for that frame.

**Declare memory you allocate yourself.** A CUDA pool, a second Vulkan device
or a video session allocates VRAM the core cannot see, and what it cannot see
it cannot account for — so the frame cache keeps filling the card while your
allocations compete with it for the same memory. reserveGPUMemory takes that
number into the same budget, and updateGPUMemoryReservation republishes it as
an absolute total (not a delta, so drift is impossible however the calls
interleave). The core never refuses a reservation — it does not own the memory
and cannot veto it. What an increase buys is cooperation: when the declared
bytes push the pool past its limit, cached GPU frames are evicted and idle
allocator blocks handed back to the driver *before the call returns*, so
reserve or update **before** the allocation and the VRAM it is about to ask for
has actually been vacated. Two rules: only declare memory on the core's own
device (match by UUID, below), and never declare bytes the core already
accounts — anything from createGPUBuffer, allocateGPUMemory or GPU frames —
or they count twice. Release the reservation in the filter's free callback.

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
lives in the VSVulkan4.h header. Two capabilities are optional and must be
queried on the physical device: ``shaderFloat16`` (half precision arithmetic)
and ``shaderInt64`` (64-bit integer arithmetic). The core enables either one
when the device has it, but neither is promised and neither is reported back,
so a kernel that wants one asks for itself. Everything else that is optional in
Vulkan is simply absent: the only device extensions a core device enables are
the platform's opaque handle export pair and, where the device demands it,
VK_KHR_portability_subset — none of them reachable from a kernel, so vendor
specific paths cannot exist on them.

Porting an existing Vulkan filter
---------------------------------

Filters that already run their own Vulkan device port mechanically: delete
instance/device management and per-frame transfers, take clips as
``vnode:gpu``, move any CPU side data reshaping into small kernels, and let
scratch come from createGPUBuffer. The shading code itself usually moves
unchanged — kernels neither know nor care who created the device. Ported
filters gain resident chaining with every other GPU filter and centralized
VRAM budgeting, and lose only whatever depended on vendor extensions the
core does not enable.
