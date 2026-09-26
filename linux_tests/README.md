# linux_tests

Everything the plan in [`../linux_tests.md`](../linux_tests.md) needs, in one directory. Copy the
directory to the Linux machine along with the source tree and work from here.

## What is here

| File | Used by |
|---|---|
| `build_probes.sh` | builds every probe against an install prefix, with optional sanitizer flags |
| `run_all.sh` | the standing pass from section 2, plus the probes that do not reset the GPU |
| `probe_compat.h` | maps the Win32 threading calls the probes were written against onto pthreads |
| `reset_next_submit.c` | **L1**, the open question: does the next submission report device loss? |
| `p16_deviceloss.c` | **L7**, the reset acceptance case: survives, releases once, destructs |
| `gpu_stress.c` | contention, core churn, frames outliving their core, allocator exhaustion |
| `lo_coverage.c` | drives the entry points the suites never reach, for lock-order coverage |
| `queueorder_probe.c` | allocation inside the public queue-lock bracket, the I28 leaf rule |
| `timelinewait_probe.c` | the two wait helpers, including the out-of-range fatals |
| `freecore_probe.cpp` | the GPU accounting warning at core teardown, per transfer mode |
| `tsan.supp` | driver suppressions for **L3** |
| `../fuzzers/` | the randomized stress: `api_fuzz.c` (C API, built here into `bin/`) and `py_fuzz.py` (Python binding); L22 and L23 |
| `vk_layer_settings.txt` | validation layer configuration, section 5 |

## Quick start

```sh
cd ..
meson setup build --prefix="$PWD/inst" --buildtype=release
ninja -C build install
cd linux_tests
./build_probes.sh "$PWD/../inst"
./run_all.sh "$PWD/../inst"
```

Then the same again with `VS_VULKAN_FORCE_STAGING=1`, which is half of test L2.

Sanitizer builds pass their flags through to both scripts:

```sh
./build_probes.sh "$PWD/../inst-tsan" -fsanitize=thread -g
```

## The one to run first

`reset_next_submit` answers the question that decides how bad finding F8 is. It is not in
`run_all.sh` because it takes the display down while the driver recovers.

Start small to confirm the machine and the checking work, then climb until the result stops
matching:

```sh
LD_LIBRARY_PATH=../inst/lib ./bin/reset_next_submit 1000000
```

```sh
LD_LIBRARY_PATH=../inst/lib ./bin/reset_next_submit 1000000000
```

It computes a CPU reference for the same arithmetic, runs it on the GPU, waits through the core,
and compares. A dispatch that never executed leaves the seed value behind, so a reset shows up as
a wrong number rather than as a guess from a timeout. It then acquires and submits twice more and
prints which call, if any, reports the loss. The verdict it prints maps onto the table in section
L1 of the plan.

The probe emits the host-visibility barrier the core's own download path emits, so an unchanged
result word has only one explanation. That matters: without the barrier a stale read would be
ambiguous, and this probe exists to remove exactly that ambiguity.

## Caveats worth knowing before you start

**The POSIX build is unverified.** These sources were written and compiled on Windows, and there
is no Linux toolchain on that machine. `probe_compat.h` is small and mechanical, but the first
run of `build_probes.sh` is the first time it has ever been compiled. Expect to fix something.

**ThreadSanitizer will not work through Python.** It cannot be brought in by dynamic loading, so
an instrumented extension inside an uninstrumented interpreter fails during startup, usually as a
segfault at a small address followed by a nested sanitizer error. That is what the earlier NVIDIA
run reported, and it is almost certainly not a core defect. Run TSan against the probe binaries
here instead. ASan and UBSan tolerate the interpreter if their runtime is preloaded.

**`gpu_stress exhaust` does not run by default on unified memory.** On a discrete card
`DEVICE_LOCAL` is dedicated VRAM and "allocate until the driver refuses" stops at the card. On an
APU it is system RAM, so the same loop starves the compositor instead of reaching the allocator's
failure rung -- it hard-froze a 15 GB swapless Renoir laptop, last log line
`kwin_wayland_drm: The main thread was hanging temporarily!`. `gpu_stress all` now skips it there
and says so; `gpu_stress exhaust` still runs it, and it caps itself at half of `MemAvailable`.
Audit any other allocation bound in this directory against unified memory before running it on one.

**The probes share sources with the Windows machine.** They build there through the same
`probe_compat.h`, so keep this directory as the single home for them rather than editing a copy.

## Where results go

Anything learned about resets belongs in section 2a of `../src/core/vsvulkanexec_protocol.md`,
next to the two measurements already recorded there, with the raw log kept. That section is the
record rather than a summary of one, which is what made it possible to correct the invariants
instead of arguing about them.
