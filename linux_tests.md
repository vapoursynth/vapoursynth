# Linux test plan

What has to be run on Linux, why it cannot be run on the Windows development machine, and how to
run it. Ordered by value: section 3 is the list of open questions, and L1 is the one worth doing
first.

Nothing here is a substitute for the Windows suites. It is the coverage that machine structurally
cannot reach.

## 0. What Linux covers that the Windows machine cannot

The development machine was queried directly for the facts below, so these are not assumptions:

| Property | Windows dev machine | Why it matters |
|---|---|---|
| GPU | AMD Radeon RX 6900 XT, discrete | one vendor, one driver stack |
| Unified memory | no | the whole unified-memory branch never executes |
| Dedicated transfer queue | **yes** (compute family 1, transfer family 2) | the transfer pool is not the compute pool, so a large branch never executes |
| Sanitizers | not practical with MSVC/clang-cl here | TSan and ASan are Linux-first |
| Reset behaviour | Windows TDR, force-signals timelines to `UINT64_MAX` | one recovery style out of at least two |

That last row is the reason this document exists at all. See section 2a of
`src/core/vsvulkanexec_protocol.md`: a Linux/RADV reset was measured signalling the *exact pending
values* instead, which no Vulkan call distinguishes from success. The Windows machine cannot
reproduce that behaviour, so anything that depends on it has to be settled here.

Four code paths have, as far as anyone knows, **never executed on the development machine**:

- The transfer pool metering its retained source frames. `VSVulkanExecPool::init` sets
  `signalsProgress` only when the pool's queue is the compute queue, and `transferQueue()` aliases
  the compute queue only where there is no dedicated transfer family. On a discrete card with a
  transfer family, the transfer pool meters nothing.
- The direct (non-staging) download in `VSVulkanTransfer::downloadPlanes`, which needs plane memory
  that is host visible, coherent **and cached**. A discrete card gives write-combined at best.
- The unified-memory limit arithmetic in the allocator, where the VRAM pool shares the host limit.
- Anything conditioned on `hasDedicatedTransferQueue()` being false.

## 1. Builds

The Linux build is meson. A throwaway prefix keeps each configuration independent and avoids
touching a system install.

```sh
# plain
meson setup build --prefix="$PWD/inst" --buildtype=release
ninja -C build install

# UndefinedBehaviorSanitizer
meson setup build-ubsan --prefix="$PWD/inst-ubsan" --buildtype=debugoptimized \
    -Db_sanitize=undefined -Db_lundef=false
ninja -C build-ubsan install

# AddressSanitizer, with the leak checker it enables by default
meson setup build-asan --prefix="$PWD/inst-asan" --buildtype=debugoptimized \
    -Db_sanitize=address -Db_lundef=false
ninja -C build-asan install

# ThreadSanitizer
meson setup build-tsan --prefix="$PWD/inst-tsan" --buildtype=debugoptimized \
    -Db_sanitize=thread -Db_lundef=false
ninja -C build-tsan install
```

`-Db_lundef=false` is required: the sanitizer runtime is resolved at load time, and without it the
shared library link fails on undefined sanitizer symbols.

Running the Python suites against a prefix depends on the local layout, roughly:

```sh
export LD_LIBRARY_PATH="$PWD/inst/lib:$LD_LIBRARY_PATH"
export PYTHONPATH="$PWD/inst/lib/python3.13/site-packages:$PYTHONPATH"
python3 -m unittest discover -s test -p "*_test.py"
```

### The sanitizer-through-Python trap

The system `python3` is not built with sanitizers, so loading an instrumented `vapoursynth`
extension into it means the sanitizer runtime arrives by `dlopen`. ASan and UBSan tolerate that if
their runtime is preloaded first:

```sh
LD_PRELOAD=$(clang -print-file-name=libclang_rt.asan-x86_64.so) python3 -m unittest ...
```

**TSan does not.** It has to control the process address space from the start and cannot be brought
in by `dlopen`; the usual symptom is a SEGV at a small address during initialisation followed by a
nested sanitizer failure, which is exactly what the NVIDIA run reported. So:

> Run TSan against a **native probe binary**, not through the Python interpreter.

The probes under `scratchpad` in the session history (`gpu_stress.c`, `lo_coverage.c`,
`queueorder_probe.c`, `timelinewait_probe.c`) are plain C linking the real core and are the right
shape for this. Build them into the TSan tree and run them directly.

## 2. Standing suites and expected results

Run these first on the plain build; anything failing here makes the rest meaningless.

| Command | Expected |
|---|---|
| `python3 -m unittest discover -s test -p "*_test.py"` | 230 tests, OK with 3 expected failures |
| `python3 -m unittest discover -s test -p "test*.py"` | 51 tests, OK |
| `cd test && python3 gpuresize_test.py all` | `all audit checks passed` |

A warning about a plugin using API 3 is normal on a machine with old plugins installed and is not a
failure.

Environment switches the GPU code reads:

| Variable | Effect |
|---|---|
| `VS_VULKAN_VALIDATION=1` | installs the Khronos validation layer; its messages go to stderr, not the core log (since 2026-09-25) |
| `VS_VULKAN_FORCE_STAGING=1` | forces the staging transfer paths even where a direct one is available |
| `VS_VULKAN_HOST_STAGING=1` | keeps the upload staging ring in host memory where it would otherwise live in resizable BAR memory |
| `VS_VULKAN_NO_HOST_FRAMES=1` | only with `host_backed_download_frames.patch` applied: GPUDownload allocates ordinary frames instead of host backed ones, so downloads go through the readback ring; forced staging implies it |
| `VS_VULKAN_SINGLE_COMPUTE_QUEUE=1` | on a device without a transfer family, keeps the core's transfers and `vqTransfer` on the compute queue instead of the compute family's second queue; the device line says which |
| `VS_VULKAN_NO_TRANSFER_QUEUE=1` | ignores the transfer family, so the layout of a device without one (transfers on the compute family's second queue) can be exercised and measured on any device |
| `VS_VULKAN_MAX_VRAM_MB=<n>` | overrides the VRAM limit, for exercising the pressure paths |

## 3. Open questions, in priority order

### L1. Does the next submission on a reset device report device loss?

**Why.** This decides how bad finding F8 is. Today a reset that signals the exact pending values is
accepted as completion, so the frames in flight when it happened are silently wrong. If the *next*
submission returns `VK_ERROR_DEVICE_LOST`, the core already latches the loss on that result and
every later frame becomes a visible error, so the damage is one wave of in-flight work and the run
fails loudly. If it never reports, every frame after the reset is silently wrong and F8 is
materially worse than currently documented.

There is a specific reason to expect it does report: the kernel marks the context whose job caused
the reset as guilty and fails its later submissions, which the driver is expected to surface as
device loss. Nobody has checked. The existing reproduction acquired a context and abandoned it
without ever dispatching, so it never asked the question.

**Procedure.** Reproduce the reset exactly as before (the long finite arithmetic shader through a
real exec pool, on the AMD integrated GPU), confirm the output mismatch, and then, on the same
core and the same pool:

1. `gpuExecAcquire` a context. Record whether it returns non-NULL.
2. `gpuExecSubmit` it with nothing recorded. Record the returned code and the error string.
3. Call `gpuExecPoolWaitIdle` and record whether it returns `gdDeviceLost`, which is the observable
   for the core having latched the loss.
4. Repeat step 1 to 3 once more, in case the first submission is the one that gets the error and
   the state only settles afterwards.

**What the answers mean.**

| Result | Meaning |
|---|---|
| The submit returns device lost | Blast radius is the in-flight wave. Record it in section 2a and stop. |
| The submit succeeds and the drain says drained | Every later frame is silently wrong. Reopen F8; this is the "seen in a real graph" trigger in all but name. |
| The submit fails with something else | New information about how this stack reports. Record the exact code. |

**Also worth capturing in the same run:** whether a *second* core created afterwards in the same
process works, since the report established that a fresh process and device recover.

### L2. The unified-memory and shared-queue paths

**Why.** The four code paths listed in section 0 have never run. Two of them are recent: the
transfer pool's metering of retained source frames against the admission gate was added on
2026-09-08 and only takes effect where the transfer pool is the compute pool, which is exactly this
hardware. Code that has never executed is not tested code.

**Procedure.** On the AMD **integrated** GPU specifically, not the discrete NVIDIA one:

1. Confirm the device really has no dedicated transfer family, by reading the compute and transfer
   queue family and index out of `getVulkanHandles` and checking they are equal, and that
   `getVulkanCoreInfo` reports unified memory.
2. Run all three standing suites from section 2.
3. Run them again with `VS_VULKAN_FORCE_STAGING=1` and confirm identical results. This is the
   comparison that matters: forced staging takes the path the Windows machine always takes, so a
   difference between the two runs isolates the never-executed direct path.
4. Run with a small `VS_VULKAN_MAX_VRAM_MB` to push the allocation ladder and the eviction paths,
   where the unified limit arithmetic lives.
5. Run the download-leak and trim-recovery checks, and confirm no "still allocated in GPU
   framebuffers" warning at core teardown.

**What to look for.** Wrong pixels between the two staging modes; a stall or a timeout under the
small VRAM limit, which would point at the admission gate now metering frames it did not before; and
any accounting warning at teardown.

### L3. ThreadSanitizer on the core

**Why.** The only whole-graph race checking done so far was reading, plus one-off lock-order
instrumentation on Windows that has since been removed. TSan is the tool that would find what
reading missed.

**Known state.** The AMD run reported a race whose two stacks both land inside
`libvulkan_radeon.so`, in what looks like a driver-internal cache guarded by inline atomics that
TSan cannot see. That is a plausible false positive and was left unsuppressed and failing rather
than being explained away.

**Procedure.**

1. **Start with the GPU out of the picture.** Run TSan against the CPU-only workloads first: no
   Vulkan driver is loaded, so every frame in a report is instrumented code and there is no
   ambiguity. This is the highest signal-to-noise configuration available and it has not been done.
2. Then the GPU probes, with a suppression file for the driver, so that a real core finding is not
   buried:

   ```
   # tsan.supp
   race:libvulkan_radeon.so
   race:libnvidia-glcore.so
   race:libnvidia-eglcore.so
   deadlock:libvulkan_radeon.so
   ```

   ```sh
   TSAN_OPTIONS="suppressions=$PWD/tsan.supp:second_deadlock_stack=1:history_size=7:handle_segv=0" ./gpu_stress
   ```

3. Judge each surviving report by whether **any** frame is in VapourSynth code. Both stacks inside
   the driver is not a core finding. One stack in the core and one in the driver usually means the
   core called the driver from two threads, which the driver is required to allow, so check the
   Vulkan external-synchronization rules for the specific call before treating it as ours.

**Do not add suppressions to make a run green.** A suppressed driver race stays a known unknown;
settling it needs a symbolized or instrumented driver build, which is a separate exercise.

### L4. The NVIDIA TSan initialisation failure

**Why.** It is currently recorded as a failure with no cause. It is most likely the `dlopen`
problem described in section 1 rather than anything about the core, and leaving it unexplained
makes the whole TSan result unreadable.

**Procedure.** Re-run it as a native probe binary rather than through Python, per section 1. If it
still crashes, get a backtrace with module mappings:

```sh
gdb --batch -ex run -ex bt -ex "info sharedlibrary" --args ./gpu_stress
```

The question to answer is only which module is at fault. If it is the sanitizer or the loader, say
so and stop; it is not a core defect.

### L5. AddressSanitizer and the leak checker, first run

**Why.** Never run at all. It is the tool for use-after-free and for the frame and buffer lifetime
rules the GPU core is built on, and it costs one build.

**Procedure.** Run the standing suites and the GPU probes under the ASan build. Expect clean.

**Known deliberate exceptions.** The core leaks on purpose in a few places, all of them conditioned
on a wait that failed to establish completion: the exec pool destructor keeps its timeline, the
transfer destructor keeps slot buffers and retained frames, and `~VSPlaneData` keeps a plane, its
buffer and its device reference together. None of those paths is reached in a normal run, so LSan
should report nothing. If it does, that is a finding: it means a wait failed somewhere that was not
expected to fail.

### L6. Lock order, cross-checked

**Why.** The lock graph in section 2 of the protocol document was measured once, on Windows, with
temporary instrumentation that no longer exists. TSan detects lock-order inversions for pthread
mutexes for free, so this is a cross-check at no extra cost.

**Procedure.** Run the TSan configuration from L3 across the widest workload set available and treat
any `lock-order-inversion` report as a claim against the documented order. The documented edges are
in section 2 of `src/core/vsvulkanexec_protocol.md`; a report naming a pair not listed there, or
listing one in the opposite order, is a genuine finding.

### L7. Keep the reset reproduction as the acceptance case

The long-shader reset case is the only real-hardware test of the reset path that exists for this
driver. It should be re-run whenever the wait policy, the sweep or the device-loss handling changes,
and its result recorded either way. It is not expected to pass in the sense of detecting the reset;
what it establishes is that the core survives one, releases every retention exactly once, and
destructs cleanly.

### L8. Optional: NVIDIA reset behaviour

The NVIDIA card has never been reset. Knowing whether it force-signals to the sentinel, reports
device loss properly, or does the RADV thing would turn a sample of one driver-per-behaviour into
something worth generalising from. It is optional because deliberately hanging the display GPU is
disruptive and the answer changes no code today.

### L9. An unsignalled producer pair (`unsignalled_producer`)

**Why.** `setGPUPlaneProducer` states the rule -- "Never a value you still have to signal yourself
from the host" -- and `gpuTimelineWaitValue` says a value an exec pool has not submitted is fatal
"while a timeline you signal yourself has no bound the core could check". I24 has `~VSPlaneData`
wait unconditionally. So a plugin that publishes a value it forgets to signal has an unbounded wait
inside frame destruction, documented but never measured.

**MEASURED 2026-09-08, hang confirmed.** Control (value 0, already reached on a fresh timeline)
returns from `freeFrame` in 0 ms. Hazard (value 1, never signalled) parks in `freeFrame` forever;
the 30 s watchdog killed it. Nothing reported it and nothing timed out.

The hang is the *safe* behaviour -- proceeding would hand a buffer back while the GPU may still read
it -- so this is not an argument for a timeout that frees anyway. What it argues for is diagnosis:
after some seconds in `~VSPlaneData`, a log line naming the cause would turn a silent hang into a
message. Note the reachable-in-production shape: cache eviction frees frames, so the thread this
parks can be an ordinary core worker rather than the plugin's own.

### L10. A reset with workers already parked in waits (`reset_multithread`)

**Why.** Section 2a calls this "the ordinary shape of a reset" and describes the fix written for it:
"a wait already in progress when another thread latches the loss: the flag is read once more before
a successful wait reports completion". Both other reset probes are single threaded, so that re-read
had never executed.

**MEASURED 2026-09-08.** Six workers parked in `gpuExecWaitValue` on a hanging submission, with a
latcher submitting in a loop from the start so the windows could overlap. **All six returned
`gdDrained` at ~2033 ms with the loss not yet latched**, and the latcher's submit reported the loss
only afterwards. Every wait came back, no thread hung, 8 of 8 retentions released exactly once,
clean teardown.

The interesting half is why the ordering is hard to produce on this driver rather than merely
unlucky: the reset signals the exact pending value, so every parked waiter's condition is satisfied
at the instant the device dies, while the loss only becomes discoverable to a *submit* afterwards.
The re-read cannot help that case because there is nothing latched yet to re-read. It is the same
blast radius L1 measured, now with numbers: six of six in-flight waiters took stale output. Not a
new defect -- a sharper measurement of section 2a's known gap.

### L11. Re-entering the core from a release callback (`release_reentry`)

**Why.** I15 -- "A release callback only frees" -- is what makes the core's unbounded waits safe to
leave unbounded, and nothing tested it. The guard is `failIfRunningReleases`, which calls
`vulkanFatal`, so a refusal ends the process: each case needs its own run, which is why this probe
takes a case index rather than looping.

**MEASURED 2026-09-08: all seven refuse.** Every case printed its marker from inside the callback,
died with `SIGABRT` before printing `NOT REFUSED`, and carried the right message, e.g.
`VapourSynth encountered a fatal error: gpuExecAcquire called from a release callback, which may
only free`.

```sh
for c in 0 1 2 3 4 5 6; do ./bin/release_reentry $c; done   # each ends the process by design
```

| case | entry point | result |
|---|---|---|
| 0 | `gpuExecAcquire` | refused |
| 1 | `allocateGPUMemory` | refused |
| 2 | `createGPUExecPool` | refused |
| 3 | `freeGPUExecPool` | refused |
| 4 | `gpuExecPoolWaitIdle` | refused |
| 5 | `gpuExecWaitValue` | refused |
| 6 | `gpuTimelineWaitValue` | refused |

Deliberately **not** in `run_all.sh`: seven intentional aborts would read as a catastrophe in a
standing pass.

It also found a documentation drift. I15 named five enforcement sites; the code guards seven, and
`gpuTimelineWaitValue` is not a pool wait under any reading. The invariant row now says seven.

### L12. The fatal guards on the plugin-facing exec API (`invariant_guards`)

**Why.** I16, I18, I22 and I27 are each enforced by a fatal check and none had any coverage. Same
shape as L11: `vulkanFatal` ends the process, so one case per run.

**MEASURED 2026-09-08: all six refuse**, each with the message its invariant implies.

| case | invariant | entry point | result |
|---|---|---|---|
| 0 | I27 | `setGPUPlaneProducer` on a plane shared by `copyFrame` | refused |
| 1 | I27 | `gpuExecWritesPlane` on a plane shared by `copyFrame` | refused |
| 2 | I22 | `gpuExecRetain` on a handle already ended by submit | refused |
| 3 | I22 | `gpuExecSubmit` on a handle already ended by abandon | refused |
| 4 | I16 | second `gpuExecAcquire` on one pool from one thread | refused |
| 5 | I18 | `gpuExecPoolWaitIdle` while holding a context of that pool | refused |
| 9 | I25 | `copyMap` of a map holding a node into a frame's property map | refused (`mapSetNode` on the frame map fails with an error first; the copy is the fatal) |

I27's message is the one worth reading, because it names the fix rather than the rule:
"a GPU plane has no copy on write, so the write would land in the other frame too. Take a new frame
with the properties of the old one instead of copying it."

Like L11, deliberately kept out of `run_all.sh`: six intentional aborts do not belong in a standing
pass. What these buy is a regression net -- each guard is one `if` away from being silently lost,
and the failure mode without it is a corrupted frame or a deadlock rather than an error.

### L13. The lock graph as a standing gate (`check_lock_order.sh`)

**Why.** Section 2 of `../src/core/vsvulkanexec_protocol.md` is the authority on the core being
deadlock free, and it is measured rather than argued. The measurement is only worth what its last
run is worth, so it is wired up as something that can be re-run rather than re-derived.

```sh
meson setup build-lockorder --prefix="$PWD/inst-lockorder" --buildtype=debugoptimized \
    --force-fallback-for=zimg -Dcpp_args=-DVS_LOCK_ORDER_CHECK -Dc_args=-DVS_LOCK_ORDER_CHECK
ninja -C build-lockorder install
./build_probes.sh "$PWD/../inst-lockorder"
./check_lock_order.sh "$PWD/../inst-lockorder"
```

It drives both suites, with and without forced staging, and the five non-destructive probes, merges
every edge reported, and **fails** on an edge not in `lock_order_expected.txt`, on any pair seen in
both orders, on anything held at one of the four plugin boundaries, and on any workload timing out.
An expected edge a run does not reach is a coverage note rather than a failure.

Adding a line to `lock_order_expected.txt` is the point at which someone has to say why a new edge
is safe. That is the whole purpose: the graph changes silently otherwise.

Three properties it was checked for, because a gate that cannot fail is worse than none:

  * against the instrumented build it reports the seven documented edges and passes;
  * against an ordinary build it **refuses to run** rather than passing vacuously -- with no
    instrumentation there are no reports, hence no unexpected edges, hence a meaningless green;
  * with an edge removed from the expected list it fails and names it.

`LOCK_ORDER_TESTS` narrows the unittest pattern, which is how the third check is done quickly.

**CI**: `.github/workflows/lockorder.yml`, on demand and weekly rather than per push -- it needs its
own build and the question changes on the timescale of months. A GitHub runner has no GPU, so it
reaches only the cache and allocator edges; the GPU half of the graph needs a machine with a Vulkan
device. The gate prints which expected edges a run did not reach, so a green there is labelled as
the partial result it is.

### L14. The GPU resize path loses ~27 dB on windowed and odd geometries -- OPEN

**Status: an open finding, not a threshold to relax.** `gpuresize_test.py gpu` fails 17 of 103
checks on the Renoir iGPU. `GPU_FLOOR_DB = 130.0` was pinned on a 6900 XT where "every float32 case
lands between 136 and 158 dB", so the obvious reading is that the floor is too tight for other
hardware. The evidence says otherwise.

The failures split by GEOMETRY CLASS, not by ratio, size or device noise:

| kernel case | dB | |
|---|---|---|
| up, down, h-only, v-only, **shift-only**, 4K-wide | 148-152 | pass |
| **odd** destination | 121 | FAIL |
| **window-1:1 / window-scale / window-down** | 121 / 126 / 123 | FAIL |

A shift on its own costs nothing. A window, or an odd destination, costs ~27 dB. The rest of the
failures are the same shape: chroma siting, chromaloc, field and interlaced -- every one a
fractional or non-trivial source offset.

Three things rule out the easy explanations:

  * **The reference is not at fault.** `refcheck` validates it against the SCALAR zimg path on those
    exact eight cases and gets 149-151 dB. So the reference agrees with zimg precisely where the GPU
    does not.
  * **`precise` is not being dropped.** Compiling a test shader through the core's own glslang and
    parsing the SPIR-V shows the `NoContraction` decorations present. The qualifiers
    `gpuresize.cpp:217` calls "load bearing" do reach the binary.
  * **The low halves are not missing.** `planAxis` computes `invScaleLo`/`shiftLo` for every
    non-identity axis, windows included, and the PASSING 1.5x case has a non-zero `invScaleLo` too.

~25 dB is exactly the penalty `gpuresize.cpp:212` documents for losing the compensated position
arithmetic. Same code, same host constants, same reference produce 136-158 dB on the 6900 XT and
121-126 dB here, so what differs is the shader compiler -- AMD's Windows backend against RADV/ACO --
on a calculation whose whole point is that its rounding must not be reassociated. That would make it
a latent correctness bug the 6900 XT masks rather than device variance.

**To settle it**, cheapest first:

  1. Run `gpuresize_test.py gpu` on the 6900 XT again and confirm the odd/window cases still sit at
     136+. That is one command on a machine that already exists, and it decides whether this is
     hardware-specific at all.
  2. If it is, a probe that reads the shader's `d` back for a failing geometry and compares it
     against the same computation in double on the host. That localises the loss to a specific
     operation rather than inferring it from output dB.

Until then `run_all.sh` reports the gpu section red on this hardware, which is correct: the floor is
doing the job its comment claims -- "a result below this is a real change in the arithmetic, not
noise."

### L16. A log handler that touches the core during device bring-up (`log_reentry.py`) -- DEADLOCK

**Why.** `vulkanDevice()` takes `vulkanDeviceLock`, a non-recursive `std::mutex`, as its first line,
and section 2 records `vulkanDeviceLock -> logMutex`: bring-up logs the device line while holding it.
The Python binding releases the GIL around device calls, which handles the cross-thread case its
comment describes. This is the same-thread case: the message reaches the Python handler ON the
thread holding the lock, and a handler that decorates records with `core.vulkan_device_info`
re-enters `vulkanDevice()` there.

**MEASURED 2026-09-08: deadlock.** Control shows bring-up logs exactly one message, at INFO, on the
bring-up thread: `Vulkan device: AMD Radeon Graphics (RADV RENOIR), VRAM limit 1922 MB of ...`.
With the handler reading `vulkan_device_info`, the process hangs (timeout, exit 124).

Exposure: the message is INFO and the `vapoursynth` logger defaults to WARNING, so nothing happens
until someone lowers the level -- which is exactly what a person debugging does. A handler that
inspects the core is an ordinary thing to write.

Where: `createVulkanDeviceLocked` publishes the device (`vulkanDev = dev.release()`, vscore.cpp:1540)
and then logs the line at :1549, still inside the function, whose two callers (`vulkanDevice`,
`setVulkanDevice`) hold `vulkanDeviceLock` across the whole call. The fix is to hand the line back
and log it after the guard's scope ends: publish, unlock, log. That also removes the one place a
core lock is held across code the core did not write -- section 2 says no such place exists -- and
retires the `vulkanDeviceLock -> logMutex` edge from `lock_order_expected.txt`.

**APPLIED 2026-09-08.** `createVulkanDeviceLocked` now hands the line back through an out-parameter
and both callers log it after their guard's scope; the failure warning in `vulkanDevice` moved out
with it. Verified: `log_reentry.py reenter` completes (exit 0, the handler ran once during bring-up
and returned) where it timed out before; both suites pass; the edge is removed from the expected
list, so `check_lock_order.sh` is what notices if it ever comes back.

`log_reentry_all.py` extends the check to every public way of bringing the device up, each in a
fresh process with the re-entrant handler installed: `vulkan_device_info`, `vulkan_devices`, a
`GPUUpload -> GPUDownload` frame request, and `set_vulkan_device` followed by first use. All four
complete (`vulkan_devices` enumerates without creating, so no message reaches the handler there).

**Validation-on residue closed 2026-09-25.** What `create` reported itself still reached the
handlers under the lock whenever validation was requested. `create` now hands its own two warnings
back to be logged after the guard, and the debug messenger writes to stderr, since the core installs
no log callback. Measured on Windows with `VS_VULKAN_VALIDATION=1` and `VK_LAYER_PATH` pointing at
an empty directory, so the missing-layer warning fires during bring-up: `log_reentry.py reenter`
failed fast (0xC0000409, the recursive lock) before the change and completes after it. The
lock-order build recorded `vulkanDeviceLock -> logMutex` before and does not after.

### L17. A sweep nested inside a sweep (`nested_sweep`) -- passes

`updateGPUMemoryReservation` is not on I15's guarded list, and an increase past the limit calls
`notifyCaches -> sweepExecPools`. A release callback that grows a reservation therefore nests a
sweep on the sweeping thread, registering a second batch for the same `(pool, thread)`.
`endExecReleasesLocked` erases from the end, so the two pop LIFO. Measured with a second thread
creating, submitting on and freeing pools throughout (that free is what waits on the batches):
20 of 20 nested sweeps returned, 5 pools freed under them, clean teardown.

### L18. L9's hang reached through the sweep path (`release_frees_unsignalled`) -- HANG CONFIRMED

A release "may only free", and releases run on whichever thread sweeps. A filter that publishes an
unsignalled producer (L9) AND drops that frame from a release parks the SWEEPING thread; the pool
that detached the release stays registered as a batch in flight, which is what `freeGPUExecPool`
waits on. Measured: control (value 0) completes; hazard (value 1) parks the sweeper in the release
(`entered=1 returned=0`) and `freeGPUExecPool` on the main thread hangs behind it, so `freeCore`
would too. That is L9's blast radius: one filter's mistake takes pool destruction down with it.

### L12 additions: the guards that close the circular-wait shapes

Cases 6-8 of `invariant_guards`: I23 (`setGPUPlaneProducer` with a value the pool has not
submitted), `gpuExecWaitValue` past the pool's ceiling, `gpuTimelineWaitValue` past a pool
timeline's ceiling. All three refuse fatally with the right message. Together they are why the
"hold a context on P, wait on a value only a thread blocked on P would submit" cycle cannot be
built: a value becomes waitable only once submitted, and submitted work completes on its own.
Note `gpuExecWaitValue` does NOT object to the caller holding a context; the ceiling is the guard.

### L19 / L20. Resets against the gate and against pool destruction -- built, not yet run

`gate_reset`: four workers parked in the admission gate behind a hanging submission that retains
more than the whole budget, then the driver resets it. The gate has explicit reset handling that
has never executed. Passes when every gated acquire returns, returns NULL, and the retentions are
released once.

`free_during_reset`: `freeGPUExecPool` draining a hanging submission while another thread forces
sweeps through reservation churn, then the reset lands mid-`waitAll`. Exercises the destructor's
"completed via deviceLost" branch under concurrency.

**L20 MEASURED 2026-09-08: passes.** `freeGPUExecPool` returned through the reset with 1,878 sweeps
forced against it meanwhile, 8 of 8 retentions released exactly once, clean teardown; the kernel
logged a `comp_1.0.1` ring reset that recovered.

**L19 MEASURED 2026-09-09: passes.** Its first run gated nobody (it retained a fixed 64 MB against an
env limit the core floors to 256 MB, so the budget was 64 MB) and misreported healthy acquires; it
now sizes the retention from `VSVulkanCoreInfo::limit`, records the release count at the moment
each acquire returned, and refuses a verdict unless every worker was still parked at 1.5 s. The
measurement: all four woke at 2057 ms with a context and all 8 releases already run -- the reset
force-completed the hang, the sweep released its bytes inside the gate, and the gate opened on its
ordinary exit with the loss not yet latched, which is correct. The next submit reported
`VK_ERROR_DEVICE_LOST`, the acquire after it returned NULL, `gpuExecPoolWaitIdle` `gdDeviceLost`.

### L21: a reset reported while workers are still parked in the gate (`gate_latch`)

L19 cannot reach the gate's deviceLost exit on this driver: the reset completes the hang, so the
gate opens on bytes before anything latches. `gate_latch` holds it shut through the reset with a
submission the reset cannot complete -- a consumer queued behind the hang that waits on the GPU
for a producer value nothing signals (the L9 hazard) -- then makes the submit section 2a says
reports, on a second pool, with the workers still parked. A host `vkSignalSemaphore` of the
missing value is the release valve, so teardown never depends on the answer.

**MEASURED 2026-09-09 -- two core corrections, both verified.** The submit after the reset was
*accepted* (the queue held a wait-before-signal, and this stack defers submissions behind one in
userspace, so `vkQueueSubmit2` had nothing to report); the workers stayed parked for the full 10 s
with nothing reporting the reset; when the valve let the deferred work through, the loss arrived
as `VK_ERROR_DEVICE_LOST` from the gate's own wait -- and all four workers were handed contexts
with all 8 retentions still unreleased, then a fresh acquire got one too. Two gaps against I29 and
section 2: the gate's wait returned on any error without latching, and `acquire` tested the flag
only before the gate. Fixed (`vsvulkan.cpp` gate wait, `vsvulkanexec.cpp` acquire); rerun: all
four refused within 50 ms of the latch with the device-lost message, fresh acquire NULL, both
pools `gdDeviceLost`, 8/8 releases, clean teardown, ALL PASS. The parked-with-nothing-to-report
state needs a value nobody signalled, which in-contract use never queues; it is a producer bug's
symptom, not a core exit that is missing. Section 2a records the measurement.

All three reset the GPU. After running them, reboot rather than suspend -- see the note in section 3.

### L21. The core's transfers against plugin pools on the one queue (`transfer_contention`) -- passes

Where there is no dedicated transfer family the transfer pool is the compute pool and the two queue
locks are one lock, and the transfer pool's metering of retained source frames against the admission
gate only takes effect on that hardware. Measured under a live 16 MB gate with 2-context rings: one
thread round-tripping 120 frames through `GPUUpload -> GPUDownload` while three plugin pools each
acquire, retain, submit and wait 300 times. 120 of 120 frames, 900 of 900 submits, no acquire
refused, clean teardown. The never-executed metering path holds under contention.

### L22. Randomized legal-API stress (`api_fuzz`)

**Why.** Every other probe encodes an interleaving someone thought of. This one draws random
operations from the documented-legal subset of the plugin-facing API on N threads at once --
acquire, retain, read and write frames, submit, abandon, wait on submitted values, drain, allocate
and free memory, grow and shrink reservations (which forces sweeps), create, copy and free frames,
churn private pools -- under a 64 MB VRAM limit so the admission gate and allocation ladder are live,
with 4-context rings so a full ring is common. The fuzzer enforces the legality rules itself
(one context per thread, writes only to planes owned outright and only once, drains only when
holding nothing, waits only on values it submitted), so a fatal from the core is a finding rather
than misuse. A watchdog names a thread that stops making progress and its last operation.

```sh
./bin/api_fuzz [seconds] [seed] [threads]      # source in fuzzers/api_fuzz.c, built by build_probes.sh; defaults 30, time-based, 8
```

Passes when every retention was released exactly once after the drains and teardown completes.
Meant to run plain, under ThreadSanitizer (`LD_PRELOAD` of libtsan with `tsan.supp`, against
`inst-tsan`), and under the lock-order build, since each sees a different class of defect.

The second round widened it to the ownership-transferring and refcounted surface, the class the
WritesPlane defect fell into: `gpuExecUsesBuffer` and `gpuExecUsesMemory` (the handle is consumed,
on submit and on abandon alike), timelines (create, `addGPUTimelineRef`, free, publish an
already-reached value, publish host-ready), `exportGPUPlane` and `exportGPUSemaphore` (each a new
descriptor the caller closes; the process's open-fd count is compared before and after, so a handle
leaked inside the core shows), the shader cache (two sources compiled and freed from every thread
at once, so identical compiles collide), and `waitGPUFrame`.

Second-round results, seed 5, 30 s x 8 threads under each build: plain, ASan with leaks on, TSan
and the lock-order instrumentation all `ALL PASS`, about 1.5M ops each. Per run roughly 24-25k
buffers and regions handed to contexts and then submitted or abandoned at random, with every
retention released exactly once; 43-46k exported memory and semaphore handles with the open-fd
count identical before and after; zero ASan errors beyond Mesa's per-thread 256 bytes; zero
ThreadSanitizer reports of any kind, so the concurrent identical shader compiles do not race; and
under the lock-order build every edge reached was expected -- the new ops added
`flushMutex -> queueLock` to what the fuzzer covers, through `waitGPUFrame`'s availability flush.
No new defect this round.

Third pass 2026-09-09, seed 11, 45 s x 8 threads, after the gate and acquire change with every
build rebuilt: about 2.5 M ops per build, retains equal to releases, descriptors unchanged, ALL
PASS under plain, ASan with leaks on (the same 256 bytes in 2 allocations from an already unloaded
driver module, no VapourSynth frame in the stack), TSan (no report) and the lock-order build (the
four expected edges, no inversion, nothing held across a boundary).

Third round of the fuzzer itself (2026-09-09) makes the work real and crosses the threads:
submissions carry actual dispatches over a storage buffer the context then owns or over the
plane of a frame the thread declared a write to; frames a thread has written are published to a
ring the other threads read, wait on, copy and export (cross-pool waits on producer values);
every shared pool's newest submitted value is waited on from any thread through both
`gpuExecWaitValue` and the pool timeline; `setMaxVRAMUse` moves the limit and the gate budget
under load; `lockVulkanQueue` is taken as the leaf bracket it is documented as; buffers are
destroyed only after their submission completed; a second core is brought up and torn down beside
the first; and 24 MB retentions trip the gate on their own. Seed 17, 60 s x 8 threads: about 4.9 M
ops plain with 242 K real dispatches, 66 K frames published, 359 K uses of another thread's frame
and 433 second cores; ALL PASS under plain, ASan, TSan and the lock-order build (same four edges).
The one TSan run that reported was the fuzzer's fault: its frame ring sat under a spin lock built
from the probe kit's `Interlocked*` helpers, which are uninstrumented atomics in an uninstrumented
binary, so TSan saw a frame allocated on one thread and ref'd on another with no happens-before
("as if synchronized via sleep"). The ring now sits under `probe_mutex`, a pthread mutex TSan
intercepts whoever calls it, and the run is clean.

**A reset injected under random load (2026-09-09, `api_fuzz 40 29 8 20`).** The fourth argument,
`hang-at`, makes the main thread submit an infinite dispatch that many seconds in while the eight
workers keep fuzzing; the driver reset it 2 s later. The loss was first reported 2,047 ms after the
hang, through `waitGPUFrame`, then 845,033 more times across acquire, submit and every wait; 1.85 M
ops ran after it and no call hung; 685 second cores came up, most of them on the reset device's
successor; every retention was released exactly once after the drains, descriptors unchanged,
teardown completed. I29 under random load, with the gate and acquire changes from L21 in place.
Refusals carrying the device-lost message are expected in that mode and counted; a run without
`hang-at` that sees one fails, and a run with it that never sees one fails too.

**The same under ThreadSanitizer (2026-09-09, seed 41).** ALL PASS with zero reports: reset 2,028 ms
after the hang, first reported through `submit`, 320,639 reports, 700 K ops after the loss, 280
second cores, retains equal to releases. One thing is open from it. The first attempt died within
its first 20 s -- before the hang -- with a SEGV reading address 0x18 in an unnamed module; TSan's
own handler could not unwind it ("nested bug in the same thread") and exited 66, so there is no
stack and no core. It came straight after the plain reset run, seven resets into the boot. Six
further 20 s TSan runs of the same op stream without the hang (seeds 41 x2, 43, 47, 53, 59) and the
full gdb-supervised rerun did not reproduce it. Unresolved, observed once. The TSan recipe in
section 1 now sets `handle_segv=0`, so a recurrence is left to the kernel, which names the module
in its `segfault at ... in <lib>` line, and to systemd-coredump.

### L24. What that SEGV was: the Vulkan loader unloading drivers under itself

Chased 2026-09-09 and root-caused. ThreadSanitizer disables ASLR, so the faulting program counter
from the crash above is mappable: `0x7ffff60b9743` lands in **libxcb.so.1's text**, a library
nothing in VapourSynth calls -- the Vulkan loader pulls it in behind the AMD ICD. Running
`api_fuzz` under TSan with the suppression file removed says why: **802 data races in ten seconds**,
every one of them the dynamic linker freeing a driver library (`_dl_close_worker` -> `free`) on one
thread while another reads or reallocates that memory, all inside `libvulkan.so.1`. One side takes
the loader's own global mutex and the other takes nothing:

    Write by thread T15:  free <- _dl_close_worker <- libvulkan.so.1 <- createBareInstance   (no mutex)
    Prev. write by T9:    malloc <- _dl_close_worker <- libvulkan.so.1 <- enumerateDevices   (mutexes: write M0)
    Mutex M0 created at:  pthread_mutex_init <- libvulkan.so.1 <- VSVulkanLoader::initialize

**That reading was wrong, and the rest of this section is kept only because the correction is the
point.** See L25: the reports are a ThreadSanitizer blind spot in glibc's dynamic linker, not a
defect in the Vulkan loader. Nothing here is a VapourSynth memory bug either. Loader 1.4.341.0,
Mesa 26.0.8.

**What is true regardless of the diagnosis:** the kit's `tsan.supp` line `race:libvulkan.so` was
matching all 802 of these, so every "TSan clean" in this document before this section is clean
*modulo that suppression*, and nothing said how much it was swallowing. That is worth knowing even
though what it swallowed turned out to be noise -- the number is the thing to look at, not the
absence of reports.

**The core now serializes what it does itself.** One process-wide recursive mutex in `vsvulkan.cpp`
covers every call that can make the loader load or unload a driver: `vkCreateInstance` and the
extension and layer queries around it (that is where the loader preloads the ICDs it later drops),
`vkEnumerateInstanceVersion`, `vkEnumeratePhysicalDevices`, `vkCreateDevice`, `vkDestroyDevice`,
`vkDestroyInstance`, and our own `dlopen`/`dlclose` of the loader. Recursive because the extension
query is both an entry point of its own and part of instance creation. It is registered in the
lock-order instrumentation as a leaf, `vulkanDeviceLock -> vulkanLoaderMutex`, and the checker now
ignores the self-edge a recursive acquisition would otherwise record.

Measured on the same seed, ten seconds, eight threads: **802 -> 391 -> 253 -> 0** race occurrences
as the guards went in, and 0 with the suppression file too -- nothing matches it. Thirty seconds
under TSan with no suppressions: 0. Both suites pass, ASan and the lock-order gate are clean, the
validation layer stays silent, and the suite time is unchanged (22.1 s, 22.8 s, 23.5 s across the
session's runs). It cannot protect a host that drives Vulkan on another thread of its own, and the
defect belongs upstream; it removes what VapourSynth does on its own.

Reproduce the reports on a build without the serialization with `api_fuzz 10 888 8` under TSan and
no `suppressions=` in `TSAN_OPTIONS`, then count `Matched N suppressions` with the file back in.

### L25. The same race with nothing of ours in it, and what makes it go away locally

`loader_icd_race.c` touches only the loader: N threads loop over the global extension query, the
instance version query, `vkCreateInstance`, `vkEnumeratePhysicalDevices` and `vkDestroyInstance`,
through a `dlopen`ed `libvulkan.so.1`. No VapourSynth, so it says whether a mitigation works
without the core's serialization in the picture, and it is what an upstream report can be built
from. Ten seconds, eight threads, under TSan with **no** suppression file (2026-09-09, loader
1.4.341.0, eight ICD manifests installed of which one is real hardware):

| run | reports | cycles |
|---|---|---|
| default | 2 | 440 |
| `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1` | **0** | 581 |
| `VK_LOADER_DRIVERS_SELECT=*radeon*` (one driver) | 548 | 1105 |
| both | **0** | 1371 |
| `VK_LOADER_LAYERS_DISABLE=*` | 1 | 444 |
| one instance held alive for the run | **0** | 547 |

Two things make the reports stop: telling the loader not to unload libraries removes the `dlclose`
they are about, and holding one instance for the process lifetime keeps the driver reference count
off zero so nothing is unloaded either. Restricting the loader to a single driver does not stop
them -- it makes the window smaller but the throughput higher, so the same run reports more.
Disabling layers does nothing.

### The correction: it is the dynamic linker, and ThreadSanitizer cannot see its lock

Checked against upstream 2026-09-09. `Vulkan-Loader` main is 1.4.362 against the installed
1.4.341.0, and its preload and unload paths both take `loader_preload_icd_lock` around the global
list. Nothing there matches what the reports claimed, so the reports were re-examined instead.

`rtld_dlrace.c` settles it: eight threads doing nothing but `dlopen` and `dlclose` over the same
eight ICD libraries -- **no Vulkan call anywhere** -- produce the same reports, with the same
`_dl_close_worker` and rtld-malloc frames, 242 occurrences in ten seconds. And glibc holds
`GL(dl_load_lock)` across `_dl_close_worker` (`elf/dl-close.c`, taken in `_dl_close` and released
after the worker returns), which is an `__rtld_lock_*`, not a pthread mutex ThreadSanitizer
intercepts. Two threads in the linker therefore always look unsynchronized to it.

Filtering the loader probe's output on `race:_dl_close_worker` and `race:_dl_open_worker` accounts
for **every one** of its reports -- 3,069 occurrences, nothing left over. So:

- The 802 reports in L24 are ThreadSanitizer false positives in glibc's dynamic linker.
- The Vulkan loader is not implicated, at 1.4.341 or at 1.4.362, and there is nothing to report
  upstream.
- The mutex L24 added serialized calls that did not need serializing: it made the reports go away
  by adding a happens-before edge TSan *can* see. Reverted.
- The SEGV that started this is unexplained again. It happened once, under ThreadSanitizer, and
  never in the fifty-odd TSan runs since or in any run without it; a fault inside TSan's own
  handling of a library being unloaded underneath it is the likeliest remaining explanation.

`tsan.supp` was rewritten to match: `race:_dl_close_worker` and `race:_dl_open_worker` name the
linker, `race:libvulkan_*.so` names the ICDs, and the broad `race:libvulkan.so` is **gone**, so a
real race in the loader would now be visible. On a 20 s `api_fuzz` run after the revert that is
1,627 linker matches, 4 ICD matches (an Apple driver's one-time init racing itself across two
instances -- it is installed here with no hardware to match) and zero reports left over.

The mitigation table above still stands as a **performance** result, which is what it is good for:

- **`VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1`** stops the loader unloading libraries only to
  load them again, which speeds bring-up up and, incidentally, silences the linker reports.
- **`VK_LOADER_DRIVERS_SELECT=*radeon*`** is not a mitigation but is worth setting anyway on a
  machine like this one, where the loader opens eight ICD manifests for one real GPU. With both
  set, `api_fuzz` brings up 132 cores in twelve seconds against 69 by default, and runs 2.2x the
  operations. Both suites pass with them set.
- **A long-lived instance** would do the same thing structurally: hold one bare instance for as
  long as any core has a device, and the drivers stay loaded. Measured, not implemented.

**Under the validation layer with synchronization validation (2026-09-09).** Both fuzzers with
`VS_VULKAN_VALIDATION=1` and section 5's settings file, which has `validate_sync` and submit-time
sync validation on: `api_fuzz` 2.2 M ops with 111 K real dispatches and 214 second cores, `py_fuzz`
21 K ops -- zero validation messages through the core log, and the layer's own
`/tmp/vk_validation.log` empty. The fuzzer now installs a core log handler and reports the counts by
level, which is the oracle for this mode. The one thing that failed was its descriptor count: the
layer opens its `log_filename` once per instance and never closes it, so every second core and
every enumeration left one descriptor behind, 214 of them, all pointing at the log. The count now
excludes the layer's log and names what any remaining leaked descriptors point at.

**First run, 2026-09-08: a use-after-free in the core.** Under TSan (seed 1) the very first run
reported `heap-use-after-free` in `VSFrame::getGPUPlane` from `gpuExecSubmit`, the frame freed by
`freeFrame` on the same thread; plain (seed 2) it segfaults. Cause: `gpuExecWritesPlane` stored a
raw `VSFrame *` and took no reference, while `gpuExecSubmit` dereferences every declared write
AFTER the submission to publish the producer pair. `gpuExecReadsFrame` takes "the context's own
reference, so the caller's lifetime stays its own business" and says so; its neighbour did neither.
So a caller that dropped a frame between declaring the write and submitting -- an error path,
typically -- got memory corruption instead of the named fatal every other misuse in this API gets.

**FIXED**: `gpuExecWritesPlane` now takes a reference, released once the pair is published, on a
failed submit, and on abandon; the header says so. The fuzzer is unchanged: freeing a frame you have
declared a write on is legal now, as it always was for reads.

Verified: seed 2 plain went from segfault to `ALL PASS` (4.19M ops, 615,834 retains = 615,834
releases); the 230-suite and the fuzzer both run clean under ASan with leak detection on, which is
what would expose a double release or a leaked reference from the fix (the only leak is Mesa's
per-thread 256 bytes from L5); both plain suites pass; seed 1 under TSan, the run that reported the
use-after-free, is clean of it (1.69M ops, 0 races -- the one remaining report was the fuzzer's own
unjoined watchdog thread, since joined). Seed 3 under the lock-order instrumentation: 2.44M ops,
`ALL PASS`, and every edge it reached was already in the expected list -- no inversion, nothing held
across a boundary -- so 30 s of random interleavings added no edge the designed workloads had missed.

### L23. Randomized stress of the Python binding (`py_fuzz.py`)

**Why.** Everything above drives the C API. The Python side has its own lifetime rules -- frames
outliving nodes and threads, `close()` and `with`, generators and futures abandoned mid-flight,
garbage collection at arbitrary points from arbitrary threads -- and a different failure model:
most misuse is supposed to raise. So the invariants are that the process survives, no worker
stalls, the core logs no "still allocated in GPU framebuffers" warning at teardown, and the GPU
allocation the core reports does not grow round over round. Exceptions are counted, not failed on,
except that a type outside the expected set is flagged.

Six threads request frames from shared GPU graphs (`GPUUpload` -> random std/resize ops ->
optionally `GPUDownload`) while others build and drop graphs, hand frames across threads through a
queue, close frames twice, use them after close and after their `with` block, abandon `frames()`
generators and `get_frame_async` futures, poke planes and properties of GPU-resident frames, churn
`max_cache_size` and `num_threads`, set and clear outputs, and one thread does nothing but
`gc.collect()`. A recording log handler at DEBUG sits under all of it, which is the path the
binding's own comment describes: a Vulkan diagnostic raised under the allocator's mutex during a
frame free reaches that handler and wants the GIL.

```sh
python fuzzers/py_fuzz.py [seconds] [seed] [threads]      # defaults 30, time-based, 6; ASan/TSan via LD_PRELOAD
```

**The first run's verdict was wrong, and the lesson is worth the space.** It reported the GPU
allocation CLIMBING across rounds under all three builds (640 -> 768 -> 896 MB) while the process,
ASan, TSan and the core's teardown warning were all clean. A direct experiment settled it:
`vulkan_device_info["allocated"]` is the allocator's retained high-water mark, not live frames --
48 frames held, dropped, collected and evicted leave it at exactly 1280 MB for four identical
cycles, where a leak would add ~1.1 GB each. Blocks are 128 MB and go back to the driver only under
GPU pressure. Rounds with different seeds kept finding new peaks. Identical op sequences per round
were not enough either: the multiset of ops is fixed but their overlap across six threads is not,
so the mark still ratchets up a block at a time before it stops. The gauge that holds is the shape
of the series, not its level: no more than one block of growth across the second half of eight
rounds (a ratchet plateaus, a leak keeps going), with the exact live-frame check being the core's
own teardown warning, which the handler echoes to stderr so a grep of the log after exit sees it.

**Results, seed 7, 40 s in 8 rounds x 6 threads.** Plain: 117,956 ops, settled GPU series
[768, 768, 896, 896, 896, 896, 896, 1024] MB -- one late block, within the rule -- RSS flat at
~180 MB. ASan: 82,773 ops, 0 errors, plateau at 896. TSan: 35,006 ops, zero reports of any kind,
plateau at 768. In every leg the process survived, every worker finished every round, the only
exception types were `Error` (GPU declines and format mismatches) and `RuntimeError` (use after
close, use after `with`), the recording handler saw only the device line, and the core logged no
warning at teardown. No defect on the Python side.

**Third pass 2026-09-09, seed 13, 40 s in 8 rounds x 6 threads.** Plain: 136,517 ops, settled GPU
series [768, 896, 896, 896, 896, 1024, 1024, 1024] MB, a plateau, RSS 166-180 MB. ASan: 87,224 ops,
[640, 768, 768, 768, 896, 896, 896, 896]. TSan: 41,907 ops, [640, 640, then 768 for six rounds].
No sanitizer report, no teardown warning, no core warning, every worker finished every round.

**Third round of the fuzzer itself (2026-09-09)** crosses residencies and moves the limits:
CPU-only std filters on GPU nodes (the core keeps them resident), upload/download ping-pong, the
residency mismatches that must be refused with an error rather than crash (Splice, FrameEval,
alpha), copies of GPU frames (planes shared, props writable, pixel access refused -- checked as a
violation, since a NULL plane handed out would be the crash), `max_vram_cache_size` moved under
load, 1080p float frames against a limit that may be 256 MB, ModifyFrame callbacks returning
copies, FrameEval returning GPU clips, and `vulkan_devices`' temporary instance beside the live
device. Seed 19, 40 s in 8 rounds x 6 threads: 57,872 ops plain, no violation, no teardown
warning, ALL PASS under plain, ASan and TSan. The leak criterion changed with it: moving the
limit trims what the allocator retains, so the settled series is no longer monotone and the old
"last round against the middle" test flagged a run whose teardown check was clean; a leak lifts
the floor, so the lowest settled value of the second half may not exceed the highest of the first
half by a block.

## 4. What each sanitizer is for

| Tool | Finds | Notes |
|---|---|---|
| UBSan | signed overflow, misaligned access, invalid enum and shift values, null dereference | cheapest, already passed on both GPUs, keep as a standing gate |
| ASan | use-after-free, heap overflow, double free; LSan on top for leaks | needs the preload trick under an uninstrumented Python |
| TSan | data races and lock-order inversions | cannot be `dlopen`ed, so use native probes; uninstrumented drivers produce false positives |
| Validation layer | Vulkan API misuse, synchronization errors, object lifetime | orthogonal to the sanitizers and much cheaper, run it alongside |

Useful option strings:

```sh
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:detect_stack_use_after_return=1
export TSAN_OPTIONS=second_deadlock_stack=1:history_size=7:handle_segv=0:suppressions=$PWD/tsan.supp
```

## 5. Validation layer

Independent of the sanitizers and worth running with every GPU workload. Set `VS_VULKAN_VALIDATION=1`
and point `VK_LAYER_SETTINGS_PATH` at a settings file:

```
khronos_validation.validate_core = true
khronos_validation.check_command_buffer = true
khronos_validation.check_object_in_use = true
khronos_validation.check_query = true
khronos_validation.check_shaders = true
khronos_validation.thread_safety = true
khronos_validation.validate_sync = true
khronos_validation.syncval_submit_time_validation = true
khronos_validation.validate_best_practices = false
khronos_validation.report_flags = error,warn
khronos_validation.enable_message_limit = false
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.log_filename = /tmp/vk_validation.log
```

Synchronization validation and thread safety are the two that matter here and both are on above.
The expected result across every GPU workload is an empty log; that is what the Windows runs
produce.

GPU-assisted validation and shader printf are deliberately not enabled: they are slow enough to
change the timing the concurrency paths depend on.

## 6. Recording results

- Anything that changes what the core knows about resets goes into section 2a of
  `src/core/vsvulkanexec_protocol.md`, next to the two measurements already there. That section is
  the record, not a summary of one.
- A confirmed race or lifetime bug goes in as an ordinary finding and gets a fix; the invariant
  table at the end of the protocol document is where a new rule is written down.
- A driver-side false positive gets recorded as such, with the build ID of the driver it was seen
  on, so the next person does not re-derive it.
- Keep the raw logs. Every claim in section 2a is backed by one, and that is why it was possible to
  correct the invariants rather than argue about them.
