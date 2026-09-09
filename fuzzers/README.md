# fuzzers

Randomized stress, as opposed to the targeted probes in `../linux_tests/`. Each draws random
operations from the documented-legal API surface on several threads at once, so the interleavings
it reaches are ones nobody designed a test for -- which is how `api_fuzz` found a use-after-free in
`gpuExecWritesPlane` on its first run. A seed reproduces any run and is printed on failure.

| file | drives | invariants |
|---|---|---|
| `api_fuzz.c` | the plugin-facing GPU C API: contexts, retentions, frames, buffers and memory handed to contexts, timelines, exports, the shader cache, reservations, private pools; since the third round also real dispatches, frames shared across threads, waits on any thread's submitted value, the VRAM limit moved under load, the queue-lock bracket, buffer lifetimes and second cores | every retention released exactly once; no leaked descriptor; watchdog on progress |
| `py_fuzz.py` | the Python binding around GPU frames: frames across threads, close/with misuse, abandoned generators and futures, GC from anywhere, cache and thread churn; since the third round also mixed-residency graphs, upload/download ping-pong, the residency mismatches that must be refused, GPU frame copies, the VRAM limit moved under load, 1080p float frames, ModifyFrame and FrameEval callbacks and the device list | process survives; no stall; no teardown leak warning; GPU allocation plateaus |

Build the C one with the probes -- `linux_tests/build_probes.sh <prefix>` puts it in
`linux_tests/bin/api_fuzz` -- and run both plain, under ASan and under ThreadSanitizer; the
preload recipes are in section 1 of `../linux_tests.md`, the write-ups are L22 and L23 there.
Two more modes are worth a run after any change to the GPU core: `api_fuzz 40 <seed> 8 20`
injects an infinite dispatch 20 s in and measures the reset under random load (one GPU reset;
run it from a TTY as section 3 describes, and reboot rather than suspend afterwards), and both
fuzzers with `VS_VULKAN_VALIDATION=1 VK_LAYER_SETTINGS_PATH=linux_tests/vk_layer_settings.txt`
put the validation layer with synchronization validation under them; the C fuzzer reports the
core log by level, which is where validation messages arrive. The layer keeps one descriptor per
instance open until exit; the fuzzer's descriptor count excludes those and names the rest.
`linux_tests/run_all.sh` runs both, and `check_lock_order.sh` runs the C one under the lock-order
instrumentation.

One measurement worth knowing before reading `py_fuzz.py`'s output: `vulkan_device_info["allocated"]`
is what the allocator retains, not live frames. It ratchets up in 128 MB blocks as new peaks are
found and trims back when `max_vram_cache_size` is lowered, which the third round does under load,
so the settled series moves both ways. A leak lifts its floor: the verdict is that the second
half's lowest settled value stays within a block of the first half's highest, and the exact check
is the core's own "still allocated" warning at teardown.

When extending `api_fuzz.c`: anything one thread hands to another that the core will touch goes
under `probe_mutex` from `../linux_tests/probe_compat.h`, not a spin lock built on `Interlocked*`.
The fuzzer is built uninstrumented and run against the instrumented core, so ThreadSanitizer sees
only the synchronization it intercepts; a spin lock of the fuzzer's own is invisible to it and
every object passed through one is reported as a race against its own allocation.
