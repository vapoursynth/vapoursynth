# GPU exec pool protocol and invariants

Internal design reference for the Vulkan execution pools: `vsvulkanexec.h/.cpp`, the pool
registry, release batches and admission gate in `vsvulkan.h/.cpp`, the reclamation ladder in
`vsvulkanalloc.cpp`, the public wrappers in `vsapi.cpp` and the core's cache sweep in
`vscore.cpp`. It states what the code guarantees, which lock guards what, and the invariants
every change must keep. The public contract it describes is the one in `VSVulkan4.h`; when the
two disagree, the header is what plugins were promised and this file is what needs fixing.

Verified against the tree of 2026-09-05 (the release-batch registry with sequence numbers and
the enforced rules I15 to I25). Section 11 lists invariants that are *not* enforced yet and
what each would rule out.

## 1. Objects and ownership

| Object | Owned by | Holds |
|---|---|---|
| `VSVulkanDevice` | the core, and every frame or pool still holding a reference; may outlive the core | the pool registry, the release-batch registry, the retained-bytes total and budget, the progress timeline, the allocator |
| `VSVulkanExecPool` | a filter through `VSGPUExecPool`, or `VSVulkanTransfer` | a timeline (counted; frames it produced hold their own references), `contextCount` contexts, `nextValue`, its registration |
| `VSVulkanExecContext` | its pool | a command pool with one primary buffer, the `claimed` flag, `pendingValue`, the retention list, `retainedBytes`, `retainedCounted` |
| retention | the context it was registered on, until reaped | a release function and an object; bytes are summed on the context |
| release batch | the device registry | pool, running thread, sequence number |
| `VSGPUExecContext` | its ring slot, for the life of the pool; usable by the caller from `gpuExecAcquire` to the `gpuExecSubmit`/`gpuExecAbandon` that ends it | the slot, the deduplicated wait list, the planes to publish |

A public pool's ring has `clamp(workerThreads, 2, 8)` contexts, fixed at creation. The transfer's
pool has one context per staging slot, and retains exactly one thing: a download's source frame,
for as long as the copy reading its planes is in flight (I31).

## 2. Locks and their order

| Lock | Guards |
|---|---|
| `VSCore::vulkanDeviceLock` | bringing the device up, nothing after: the device is internally synchronized and the pressure and accounting paths reach it through the atomic `vulkanDev` without taking this |
| `VSVulkanDevice::execPoolsMutex` + `execReleaseCv` | the pool registry, the batch registry, `nextExecReleaseBatch`, creation of the progress semaphore |
| `VSVulkanExecPool::claimMutex` + `claimCv` | nothing but the rendezvous between a full ring and `releaseClaim`; the claim itself is the atomic `claimed` |
| `VSVulkanQueue` | `vkQueueSubmit2`, `nextValue`, `execProgressNext`. The only lock here a plugin can hold, through `lockVulkanQueue` |
| `VSVulkanDevice::flushMutex` | the device's one flush context -- its command pool, buffer, timeline and value -- held across the flush submission and the host wait for it |
| allocator mutex | blocks and free lists |
| `VSCore::cacheLock` | the set of nodes with caches |
| `VSNode::cacheMutex` | one node's cache and consumer list |
| `VSCore::logMutex` | the message handler list, held across the dispatch to each handler; recursive, and the only lock here that runs code the core did not write |

Order, outermost first: `vulkanDeviceLock` before `execPoolsMutex` and `logMutex` (bringing the
device up registers the transfer pool and logs the device line); `execPoolsMutex` before
`claimMutex` (a device sweep releases claims while walking the registry); `cacheLock` before
`cacheMutex` (eviction) and both before the allocator mutex (`trimAllocator` under the first, an
evicted GPU frame returning its regions under the second); `flushMutex` before the queue lock,
which the flush takes while holding it. The queue lock and the allocator mutex take nothing
themselves.

One edge is absent from that order because it is forbidden rather than ordered. The queue lock is
public through `lockVulkanQueue`, and every entry point that reaches the exec registry --
allocating, `gpuExecAcquire`, `createGPUExecPool`, `freeGPUExecPool`, `gpuExecPoolWaitIdle`,
`gpuExecWaitValue`, `gpuTimelineWaitValue` -- takes `execPoolsMutex` under whatever the caller
already holds. A plugin holding the queue lock across one of them therefore closes
`queue lock -> execPoolsMutex` against the core's own `execPoolsMutex -> ... -> queue lock`, and
deadlocks with an ordinary cache sweep on a worker thread. Nothing in the core creates that edge
(I28, and `detachCompleted` reads `queuedCeiling` rather than `nextValue` precisely to keep it
from existing); `lockVulkanQueue` states the leaf rule that keeps plugins from creating it too.
`registerCache` is called
after `cacheMutex` is released, never under it. **No lock in this table is held while a release
callback runs**, and `execPoolsMutex` is never held while calling into anything a plugin wrote.

That is the whole graph rather than a summary of it, and it is measured rather than argued:
every lock acquisition in the core was instrumented to record what the acquiring thread already
held, and the result collected over the test suite, the GPU harness, the concurrency and
pressure workloads with and without forced staging, and a probe driving the entry points those
miss -- reservations, `waitGPUFrame`'s flush, `abandon`, `waitIdle`. No pair appeared in both
orders, and nothing was held across a filter's `getFrame`, a release callback, a filter free
callback or a plugin function's invoke. An edge added here should be rechecked the same way; the
instrumentation is a scoped object recording the thread's held set after each lock acquisition,
plus one check at each of those four boundaries, and is cheap to rebuild.

`vulkanDeviceLock` before `logMutex` costs nothing inside the core and is written down for what
it means outside it: `logMessage` dispatches to handlers, and the Python handler takes the GIL,
so a lock held across a log call is one a Python binding may not block on while holding the GIL.
That reversal on this very lock once froze the whole interpreter, which is why the Vulkan
bindings in `vapoursynth.pyx` release the GIL around every call that can reach device creation.

The queue lock is a leaf and has to stay one (I28), because it is the one lock in this table a
plugin holds: `lockVulkanQueue` hands it out for raw submissions. `detachCompleted` used to take
it under `execPoolsMutex` to read `nextValue`, and that single edge was enough -- a filter
holding the queue lock across any call reaching the exec registry (an allocation, an acquire,
creating or freeing a pool) deadlocked against a worker thread in `notifyCaches` ->
`sweepExecPools`, which needs no second plugin to be running. The check reads the lock free
`queuedCeiling` instead. The other half of the rule cannot be enforced from in here and is
stated in VSVulkan4.h: submit inside the bracket and call nothing else, and never hold both
queues' locks, which are one non-recursive lock where the device has no dedicated transfer
family.
Frame property maps refuse nodes and functions (I25), so freeing a frame, which happens under
`cacheLock` and inside release callbacks, never destroys a node or runs a function's free
callback.

## 2a. Device loss

A GPU reset does not have to announce itself, and the two drivers this has been measured on
announce it in two different ways: one the core can act on, and one no Vulkan call can tell
apart from success. Windows' TDR force-signals every timeline to
`UINT64_MAX` so the waiters are released, and then hands back a device whose
`vkCreateSemaphore`, `vkDeviceWaitIdle` and counter queries all answer `VK_SUCCESS`. Measured
on an AMD driver by hanging the GPU with a data-dependent infinite compute loop: the reset took
10.4 s, not the 2 s `TdrDelay`, and **nothing ever returned `VK_ERROR_DEVICE_LOST`**. So a flag
set from that result alone would never be set; the force signal is the only observable there is.

It is also unmistakable, since a pool would need 2^64 submissions to reach it. `detachCompleted`
and the admission gate therefore test for `VSVulkanDevice::resetTimelineValue` and set the
device's one-way `deviceLost` flag, and `waitTimelines` -- the single wait policy every host
wait goes through (I30) -- tests the counter that actually satisfied a successful wait, so the
call that discovers the reset is never also the one that reports work complete. Filters reach
that same policy through `gpuExecWaitValue`, which exists so that waiting on one submission does
not mean hand-rolling `vkWaitSemaphores` and re-deriving the check. From then on `acquire`, `submit`, `waitValue`, `waitAll` and `flushDeviceWrites` fail
with `deviceLostMessage`, which travels the ordinary filter error path; the sweeps stop reaping
and the gate returns at once rather than spinning on a progress counter whose `counter + 1`
wraps to zero. What a pool still retains is released by its destructor, which is safe because
after a reset nothing is reading it.

Before this the counter check called a reset a protocol violation and `vulkanFatal` terminated
the process -- and a TDR resets every context on the machine, so any application's runaway
shader killed every running core.

### The reset that is not observable at all

Measured on Linux, RADV 26.2.2 on an AMD Raphael integrated GPU, kernel 7.2.2-1-cachyos, by
submitting a finite but very long arithmetic compute shader through a real exec pool. The kernel
log records what happened: compute ring `comp_1.1.0` timed out, the ring reset failed, and the
device went through a full MODE2 reset and resumed. The core saw none of it. The submission was
accepted with signal value 1; the host wait eventually returned `VK_SUCCESS`; the counter query
returned `VK_SUCCESS` with **value 1, exactly the value the submission was going to signal**,
not the sentinel; and the checked output word still held the untouched input seed. Creating
another semaphore, `vkDeviceWaitIdle` and a later `acquire` on the same logical device all
succeeded, and **nothing anywhere returned `VK_ERROR_DEVICE_LOST`**. So `waitTimelines` returned
established completion, the sweep released the retention exactly once, and a filter would have
taken stale bytes as its result with no error on any path.

Nothing in Vulkan separates that from a real completion. The specification permits a wait to
succeed after a loss, does not require a driver to report the loss at all, and offers no reset
status query -- Vulkan replaced that idea with device loss, which is the thing this stack does
not report. The device fault extension only becomes queryable after a loss is reported, so it
does not help either. Coverage therefore has two axes and only one of them is complete:

- **Which waits are checked: all of them.** Every host wait in the core goes through
  `waitTimelines`, and a filter reaches the same policy through `gpuExecWaitValue` for a pool's
  timeline or `gpuTimelineWaitValue` for one of its own, so no wait anywhere skips the check.
  That includes a wait already in progress when another thread latches the loss: the flag is read
  once more before a successful wait reports completion. Without it every worker sitting in a wait
  at the moment of a reset returned success and handed out its frame, while the worker that
  happened to submit next was already being told the device was gone -- with several workers in
  flight that is the ordinary shape of a reset, and it was the one silent case that could be
  closed for the price of an atomic load.
- **What there is to check: two observables.** A conformant `VK_ERROR_DEVICE_LOST` from any wait,
  submit or counter query, or `resetTimelineValue` on a timeline. A reset that signals the exact
  pending values produces neither and is accepted as success.

The only thing that would close it is proof the GPU itself has to produce: every submission
writing its own value into host-visible memory as its last command, checked where the host
already waits and already has the availability barrier for it. Two parts of that need design
rather than a patch. Submissions from one pool can be in flight together and complete out of
order, so a single per-timeline word gives false alarms and the proof has to be per context or
carry max semantics. And a timeline a filter signals itself through `setGPUPlaneProducer` is one
the core cannot make write anything, so those chains would stay unverified without an API for
the filter to publish its own proof.

That is **deliberately not implemented**. Reopen it if a driver turns up that reports a reset in
some third way the core could act on, if a cheaper check appears, or if this is ever seen in a
real filter graph rather than a probe.

One thing bounding the damage is untested: whether the *next* submission on that logical device
reports device loss. The core already calls markDeviceLost on a submit returning it, so if RADV
reports it there, one frame is wrong and the rest of the graph errors out; if it never reports
it, every frame after the reset is silently wrong. The reproduction acquired a context on the
reset device and abandoned it without dispatching, so this is open. It costs one more case in
the same probe to settle, and it changes only how much is lost, not whether the first frame is.

What is at risk is integrity and nothing else. Work a reset killed really is not running, so
releasing its retentions and recycling its memory stay correct: the reproduction saw exactly one
release, no double free, no teardown deadlock and a clean destruction. The claim that fails is
only that the work happened. The other ways GPU output can be silently wrong -- a shader bug, a
missing producer pair, a missing availability barrier, a region recycled under a live dispatch --
are not affected by any of this and have their own mechanisms above.

The other half is what a *failed* wait means, which is not one thing (I30). `vkWaitSemaphores`
returns only `VK_SUCCESS`, `VK_ERROR_DEVICE_LOST` and the two allocation failures, and the last
two establish nothing at all: releasing a retention on that basis hands a region back to the
allocator while the GPU may still be reading it, and destroying a command pool with a pending
buffer, or resetting the shared flush buffer under one, is invalid usage outright. So
`waitTimelines` retries an allocation failure a bounded number of times -- a wait needs very
little and the shortage may pass, but one that cannot allocate will not be fixed by waiting
longer -- and every caller that gives up has a safe fallback: the destructors retire nothing
and leave their objects to the device, whose reference they hold, and the flush refuses the
call rather than reusing a pending buffer.

## 3. Context life cycle

```
fresh ──acquire──▶ recording ──submit ok──▶ pending ──GPU done──▶ completed ──reap──▶ idle
   ▲                  │                        (claimed by                  (unclaimed,
   │             abandon / submit               nobody)                      list moved out)
   └───────────────failed──────────────────────────────────────────────────────┘
```

- **acquire** (`VSVulkanExecPool::acquire`): pass the admission gate, then claim a context by
  CAS, first from the ring cursor, then under `claimMutex` when the ring is full. If the
  context has a `pendingValue`, register a batch when its retention list is non-empty, wait the
  pool timeline for that value, release the retentions (`releaseRetainedNow`), end the batch.
  Then reset the command pool and begin the buffer. Every failure path drops the claim.
- **recording**: `retain` appends to the context's list and adds to `retainedBytes`; nothing
  reaches the device yet. Only the claim holder may call it.
- **submit** (`submit`): the public wrapper first moves the wait list and the publish list out
  of the slot's handle, while the claim is still held. Then end the buffer (failure releases
  retentions and claim); under the queue lock allocate the next timeline value and, for
  compute-queue pools, the next progress value, submit, and record the value on the timeline
  object (`noteSubmitted`). On success add `retainedBytes` to the device total and set
  `retainedCounted`, still under the claim; on failure release the retentions (uncounted).
  Drop the claim, then reap the pool's other completed contexts (`sweepCompleted`). The
  wrapper publishes the producer pairs from its local list afterwards.
- **abandon**: release the retentions (uncounted) and drop the claim.
- **completed**: a context nobody holds whose `pendingValue` the timeline has reached. Any
  reaper may claim it by CAS and detach its retentions.
- **destruction** (`~VSVulkanExecPool`): unregister (fatal from inside a release batch; waits
  for every other thread's batch for the pool), fatal if any context is still claimed,
  `waitAll`, release what the contexts still hold, destroy the command pools, drop the
  timeline reference.

## 4. Reaping and the batch registry

Who runs releases, and from where:

| Reaper | Trigger | Reaches |
|---|---|---|
| device sweep `sweepExecPools` | `notifyCaches` (before `cacheLock`), each admission gate loop, rungs of the allocation ladder | every registered pool's completed, unclaimed contexts |
| pool sweep `sweepCompleted` | end of `submit`, `waitAll` | the pool's completed, unclaimed contexts |
| `acquire` | claiming a context with a pending submission | that context |
| `abandon`, failed `submit` | the caller | that recording's uncounted retentions |
| destructor | `freeGPUExecPool` | everything left |

`detachCompleted` reads the pool timeline once, checks the counter against `queuedCeiling`
(read afterwards and never lowered; a counter past it means the semaphore was signalled from
outside and is fatal -- except at `resetTimelineValue`, which is a GPU reset and not a bug, see
below), skips claimed contexts, wins the claim by CAS on the rest, and for each
completed one settles the bytes and moves the list out before dropping the claim. Releases run
only after that, from a local list, with no lock held.

Every batch of releases is registered on the device for as long as it runs, as `{pool, thread,
id}`, whichever reaper runs it. Both sweeps register while still holding
`execPoolsMutex`, in the same critical section that detached the retentions, so there is no
moment at which they belong to neither a context nor a batch -- which a concurrent `waitAll`
would read as "everything has been released".

`acquire` cannot register that early, and the gap is worth naming because the code once claimed
otherwise. Winning a claim hides a context from every sweep immediately, but the batch is
registered a few instructions later, so a `waitAll` landing in between finds no claimable context
and no batch and concludes the pool is clean while a completed submission's callbacks have not
run. The tail of `submit` has the same shape, the context staying claimed after the queue is
unlocked. **This is closed by contract rather than by code**: a pool belongs to one filter
instance and `gpuExecPoolWaitIdle` is a setup call, so the wait happens where the node does not
yet exist or where no frame request may run, and there is no acquire to race. Both halves are in
VSVulkan4.h; without either of them the window is reachable, which is why they are rules and not
advice. Note what is never at stake: `waitAll` waits the timeline first, so the GPU is idle
regardless -- only the host side release can be late.

The waits built on the registry:

| Wait | Waits for | Bound | Own-thread batch for the pool |
|---|---|---|---|
| `unregisterExecPool` | every other thread's batch for the pool | none | fatal |
| `waitExecReleases` (from `waitAll`) | every other thread's batch for the pool | none | fatal |
| `waitForeignExecReleases` (allocation ladder) | other threads' batches of any pool that existed when the wait began | none | excluded |

The two public promises map onto these: `freeGPUExecPool` returns only after every release the
pool registered has run; `gpuExecPoolWaitIdle` waits the timeline, reaps, then waits for
releases other threads got to first.

## 5. Accounting

`execRetainedBytes` is the sum of `retainedBytes` over contexts whose `retainedCounted` is set.
Bytes enter at a successful submit and leave when the retention is detached for release, before
any callback runs. Recordings in progress, abandoned recordings and failed submissions never
reach the total. The budget is a quarter of the VRAM limit, refreshed by `setMaxVRAMUse`; zero
disables the gate.

What each typed retention counts: a GPU-resident frame its whole size, a host frame nothing, a
pooled buffer or region its region size, a user object whatever the caller passed. A pool that
does not signal progress meters nothing (I21): its retentions are kept and released as usual
but never enter the total. The allocator's blocks are accounted separately through
`accountAllocation` into `MemoryUse`, and the transfer rings' staging buffers through the same
device callbacks by memory type: device local into the VRAM pool, system RAM (a discrete
card's host cached staging) into the host pool via `accountHostAllocation`. Slots are sized to
the last two epochs of demand, so they shrink after a burst, and `releaseIdle` frees a ring
that has been idle for a second from the pressure sweep, or of any age from the panic path.

## 6. Admission gate

`execAdmissionGate`, entered by every `acquire`, returns at once when the total is within
budget. Otherwise it loops: sample the progress counter, sweep the device, return if within
budget, wait for `counter + 1` with a 50 ms timeout, repeat. The counter is sampled *before* the
sweep so a completion between the two either gets reaped or leaves the counter behind the wait
target. Only submitted work is counted, so a thread's own recordings never gate it. Compute-queue
pools signal the progress timeline on every submission; a pool on a dedicated transfer queue
does not, and its retentions are not metered. The timeout remains for the one event that
reduces the total without a signal: a completed context an acquirer claimed first settles its
bytes on the host. A host-signalled wake-up (P10) would retire it.

## 7. Allocation ladder

`allocatePooled` retries after each rung:

1. sweep the device, wait for the releases other threads had in flight, sweep again;
2. the pressure callback, which evicts every cached GPU frame and trims idle blocks;
3. fail.

Frame creation turns a failure into `logFatal`, so a rung that gives up while another thread's
release is a microsecond from freeing the memory is a crash, not a slow frame. That is why
rung 1 waits.

## 8. Public contract (summary of `VSVulkan4.h`)

- Every acquire ends in exactly one submit or abandon, by the thread that acquired; retain,
  submit and abandon from any other thread are fatal.
- A context handle is dead after the submit or abandon that ended it; any call with it
  afterwards is fatal, with the one exception in I22.
- One context per thread, of this or any other pool, fatal otherwise: within a ring at the
  second acquire, across rings when that acquire would wait. The in-flight budget counts
  submitted work only.
- Retain only between acquire and submit.
- A release callback runs on whichever thread reaps it, possibly inside the filter's own
  `getFrame` (a gated acquire or a failed allocation sweeps), with no core lock held. It may take
  its own locks and free; acquiring, allocating GPU memory, and creating, freeing or waiting on
  a pool from inside it are fatal.
- `gpuExecPoolWaitIdle` only from a thread holding no context of the pool, fatal otherwise.
- Pools are destroyed in the filter's free callback, with no context claimed; destroying one
  while holding its own context is fatal.
- Never signal a pool's timeline from outside.
- Declare a write only on a plane the frame owns outright. `copyFrame` shares planes and GPU
  planes have no copy on write, so writing a shared one would change the other frame too.
- A producer pair on a pool's timeline names a value the pool has submitted; a larger value is
  fatal at publish. Timelines from `createGPUTimeline` carry no such bound, so the same
  discipline is the filter's: publish only a value whose signal is already in flight, never one
  the host has yet to issue. A consumer of the pair may be holding `cacheLock` -- freeing a
  frame waits out its producer and eviction frees frames -- so a signal waiting on a host thread
  can be waiting on the thread it is blocking. This is what keeps P13's deadlock out of reach
  by construction rather than by luck; the stall it also describes remains.

## 9. Invariants

| # | Invariant | Kept by |
|---|---|---|
| I1 | A context is touched by exactly one party at a time: its claim holder, or a reaper that won the CAS. | the atomic claim; sweeps skip claimed contexts |
| I2 | A retention list is appended only by the claim holder and moved out only under the claim. | construction; no code reads a list unclaimed |
| I3 | Every retention's release runs exactly once, after its submission completed or after the recording was abandoned or failed. | lists are moved out before running; `pendingValue <= counter` gates detach |
| I4 | `execRetainedBytes` equals the sum over counted contexts and never underflows. | add under the claim at submit; subtract once at settle under the claim; uncounted paths never subtract |
| I5 | Every batch of releases is registered on the device for its whole duration. In `acquire` the registration follows the claim by a few instructions rather than coinciding with it, and the same holds in the tail of `submit`; what makes that unobservable is the pool ownership and setup-only rules on `gpuExecPoolWaitIdle`, not the registry. | begin/end around every `runReleases` site; the rules in VSVulkan4.h |
| I6 | `freeGPUExecPool` and `gpuExecPoolWaitIdle` return only when no other thread holds a batch for the pool; an own batch is fatal, never silently skipped. | `unregisterExecPool`, `waitExecReleases`, `failIfInsideOwnBatch` |
| I7 | No lock from section 2 is held while a release callback runs. | sweeps detach under the lock and run after it; `notifyCaches` sweeps before `cacheLock`; the ladder and the gate hold nothing |
| I8 | The lock order in section 2. | construction; `registerCache` after `cacheMutex` is released |
| I9 | The gate waits only on bytes that submitted work will release, and never sleeps past a completion it could have reaped. | bytes counted at submit; counter sampled before the sweep |
| I10 | The ladder never declares the device full while a release another thread already detached is still running. | rung 1's wait and second sweep |
| I11 | Timeline values are allocated and submitted under the queue lock, strictly increasing per pool; `pendingValue` is the context's last submitted value or zero. | `submit` |
| I12 | A destroyed pool has no registered batch on any thread, no claimed context and empty lists, and is off the registry before anything is torn down. | destructor order |
| I13 | The transfer pool never retains, so it never registers batches and never contributes bytes. | `VSVulkanTransfer` uses no retention |
| I14 | A retained GPU frame outlives its submission; its bytes are its whole size. | `vkGPUExecReadsFrame` |
| I15 | A release callback only frees: no acquire, GPU allocation, pool creation, pool free or pool wait from a thread running a batch. | `failIfRunningReleases` in `acquire`, `allocatePooled`, `registerExecPool`, `unregisterExecPool`, `waitAll` |
| I16 | One context per pool per thread. | the owner thread recorded on the claim; `failIfHoldingContext` in `acquire` |
| I26 | A thread waiting for a context holds no context of another pool, so two full rings can never wait on each other. | `failIfHoldingForeignContext` on `acquire`'s slow path, walking the pool registry under `execPoolsMutex` |
| I27 | A producer is published only on a plane its frame owns outright, so a GPU write never reaches a frame that shares the plane. | `failIfPlaneShared` in `gpuExecWritesPlane` and `setGPUPlaneProducer`, testing `VSPlaneData::unique()` |
| I28 | The queue lock is a leaf: nothing in the core takes another lock while holding it, and nothing takes it while holding another except `flushMutex`, whose flush is the submission. | `detachCompleted` reads `queuedCeiling` rather than `nextValue`; the rule for the half a plugin owns is stated on `lockVulkanQueue` in VSVulkan4.h |
| I17 | Retain, submit and abandon only by the claim holder's thread. | `failUnlessOwner` |
| I18 | `waitAll` (idle wait and destruction) only from a thread holding no context of the pool. | `failIfHoldingContext` in `waitAll` |
| I19 | A pool is never destroyed while any thread holds one of its contexts. | `failIfAnyContextHeld` in the destructor, after unregistration, when a claim can only mean a thread still using the pool |
| I20 | The pool's timeline advances only through the pool's own submissions: its counter never exceeds what the pool handed to the queue, except at `resetTimelineValue`, which means a GPU reset (I29). | the check in `detachCompleted`, counter read first and `queuedCeiling` after it; the ceiling is stored before the submission that signals it, so it is never behind the counter and the check never fires on a correct program. A reset signalling the exact pending values leaves the counter at or below the ceiling, so this passes and section 2a's gap is what applies |
| I31 | A submission's inputs are kept alive by the submission, not by whoever waits for it: every recording that reads a frame retains it. | `gpuExecReadsFrame` for filters, and `downloadPlanes` for the transfer, which takes ownership of its source frame and releases it from the retention. Relying on the caller's own host wait instead was wrong twice over -- a failed wait leaves the copy queued, and a plane's destructor waits for its own producer alone, which is nothing at all for a host produced plane |
| I30 | Nothing is retired on a wait that did not establish completion. A retention is released, a command pool or buffer destroyed and the shared flush command buffer reset only after the wait succeeded, or after a reset, when nothing is executing. | `VSVulkanDevice::waitTimelines` is the single wait policy: it retries an allocation failure, recognises a reset, and returns true only on established completion. `~VSVulkanExecPool`, `~VSVulkanTransfer` and `~VSPlaneData` retire conditionally on it and otherwise leave their objects to the device's own destruction; `flushDeviceWrites` tracks `flushPending` and settles it before reusing the buffer |
| I29 | A GPU reset is survivable: no call spins, every retention is still released exactly once, and the core destructs. One the driver makes observable is additionally recognised and reported, so no wait claims work completed that did not; one it does not is accepted as completion, which section 2a records with the driver it was measured on. | `VK_ERROR_DEVICE_LOST` from any wait, submit or counter query, or `UINT64_MAX` on any pool or progress timeline, sets the device's one-way `deviceLost` flag, after which `acquire`, `submit`, `waitValue`, `waitAll` and `flushDeviceWrites` fail with `deviceLostMessage`, the sweeps stop reaping and the gate returns. The survivable half does not depend on recognising anything: retentions go back in `~VSVulkanExecPool` either way |
| I21 | Every metered byte belongs to a submission whose completion signals the progress timeline. | `retain` adds bytes only on a pool with `signalsProgress` |
| I22 | A context handle is usable exactly from its acquire to the submit or abandon that ends it. A later use is never a read of freed memory, and is fatal *except* once the same thread has reacquired the same slot, where it is undetectable. | the handle lives in the ring slot, bound once at creation; every public entry point runs `failUnlessOwner` first, whose empty-owner case names an ended recording; the wrapper moves the handle's lists out before `submit` drops the claim. The exception is inherent: `failUnlessOwner` compares the slot's owner thread, and after a reacquire by the same thread the stale handle is the same object with the same owner. Telling them apart needs an acquisition-specific token in the caller's hands, which the ABI does not have -- a field on the handle cannot help, since both pointers read it. Stated on `gpuExecAcquire` in VSVulkan4.h rather than defended |
| I23 | A producer pair naming a pool's timeline never carries a value the pool has not submitted. | `noteSubmitted` under the queue lock at submit, before the caller can learn the value; the check in `setPlaneProducer`; timelines from `createGPUTimeline` are not pool-owned and exempt |
| I24 | A GPU plane's buffer returns to the allocator only after its producer pair is reached, after the core is freed as before it. | `~VSPlaneData` waits unconditionally; the plane's counted timeline reference keeps the pair valid; every pool drains before `onCoreFreed`, so for a pool's timeline that wait is already satisfied by then |
| I25 | A frame's property map never holds a node or a function, so destroying a frame never destroys a node or runs plugin code. | the frame-property flag on `VSMap`, set by every frame constructor; `propSetShared` and `mapSetEmpty` fail for those types on such a map, `copyMap` into one is fatal, `SetFrameProps` rejects them at creation |

## 10. Known windows and their bounds

| # | Window | Consequence | Bound |
|---|---|---|---|
| W1 | Bytes leave the total at detach, the region returns at the callback. | a gate-admitted thread can fail at the driver | rung 1 waits for the release and retries |
| W6 | A completed context claimed by an acquirer settles its bytes on the host, with no signal to the gate. | a gated thread wakes only at the next completion or the poll | 50 ms |

W2, W4 and W5 of the earlier revision are gone with I15 and I18: a batch can no longer wait on
anything, so the ladder's wait is unbounded, and a caller cannot wait a pool idle around its
own recording. W3 is gone with I21. W6 is what the gate's poll now exists for.

## 11. Candidate invariants, not enforced

P1 to P4 of the earlier revision are now I15 to I18, P6 and P9 are I19 and I20, P5 is I21 (as
"not metered" rather than fatal, since the typed calls count bytes on their own and a transfer
pool that copies GPU frames has to read them), P11 and P12 are I22 and I23, P17 is I24 (the
skip it removed dated from timelines dying with their pools, before they were counted), and P14
is I26 -- narrowing the contract rather than defending it, since a survey of every pool user in
the tree found none holding two contexts and no reason to: the dependency that would motivate
it travels as a timeline value. P15 is I27, promoted once a probe showed the shared writable
frame it describes is two ordinary calls away rather than contrived: `copyFrame` of a GPU frame
returns a writable frame whose plane 0 is the same `VkBuffer`, and the host's safety net, the
copy on write inside `getWritePtr`, does not exist for GPU planes. Each of the rest would delete
a further class of interleavings from what must be reasoned about; P13 is a design change, the
others are cheap.

| # | Candidate | Enforce by | Eliminates |
|---|---|---|---|
| P7 | Per-pool byte identity. | keep a per-pool counted sum; assert zero at pool destruction and a zero device total at device destruction, in a self-check mode beside `VS_VULKAN_VALIDATION` | accounting drift going unnoticed until the gate stalls |
| P8 | No lock held during callbacks, and no pair of locks taken in both orders, checked rather than argued. | done once with temporary instrumentation on every lock acquisition, whose result is the order paragraph in section 2; making it standing means a scoped record on each lock plus a check in `runReleases` and at the three other plugin boundaries, under the flag `VS_VULKAN_VALIDATION` already sets | regressions of I7, and of section 2's order, by a future caller |
| P10 | Every event that reduces the byte total wakes the gate. | a second timeline that only the host signals, once per settle; the gate waits on it and the progress timeline with `VK_SEMAPHORE_WAIT_ANY_BIT` | W6, and with it the gate's 50 ms poll, which can then go entirely |
| P13 | Freeing a GPU frame never waits on the GPU. | a plane whose producer is still pending hands its buffer to a device-level deferred list keyed by the pair, reaped by the existing sweeps and at teardown, instead of the wait in `~VSPlaneData` | the eviction stall under `cacheLock` on a just-produced frame, and the deadlock where that work depends on a host signal from a thread blocked on `cacheLock` |
| P16 | ~~After device loss every wait returns an error, every retention is still released once, and destruction completes.~~ **Done for a reset the driver makes observable**: this is I29, tested by exactly the probe suggested here against a real Windows TDR. It verifies that behaviour, not the invariant: a Linux/RADV reset signalling the exact pending values is not detected at all, and section 2a records why that is left as it is. | -- | -- |

With I15 to I18 in place callbacks are pure frees and every rendezvous is unambiguous, which
is the shape a single-reaper design would enforce structurally; that later change, if wanted,
is now a refactor rather than a redesign.
