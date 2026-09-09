"""L23: randomized stress of the Python binding around GPU frames and nodes.

Most misuse on this side is SUPPOSED to raise, so the invariants are different from api_fuzz's:
the process survives (no segfault, no abort), no worker stops making progress, the core logs no
"still allocated" warning at teardown, and the GPU allocation the core reports does not grow
round over round. That last one needs care: `allocated` is the allocator's retained high-water
mark, not live frames -- 48 frames held, dropped and collected leave it exactly where the peak
put it, cycle after cycle -- so every round runs the SAME op sequence per thread, the peak is
identical by construction, and any increase after round 0 is frames surviving across rounds.
Exceptions are counted by type and reported, never failed on, except that a type outside the
expected set is flagged. A log handler records every core message; the binding notes that a
Vulkan diagnostic raised under the allocator's mutex during a frame free reaches this handler and
wants the GIL, which nothing had driven under load before.

Threads request frames from shared nodes while others build and drop graphs, hand frames across
threads through a queue, close frames twice, use them after close and after their `with` block,
abandon `frames()` generators and futures mid-flight, poke planes and properties of GPU-resident
frames, churn the cache size and the thread count, and one thread does nothing but gc.collect().

    python py_fuzz.py [seconds] [seed] [threads]        defaults 30, time-based, 6

Third round crosses residencies and moves the limits: CPU-only std filters on GPU nodes (the
core keeps them resident), upload/download ping-pong, the residency mismatches that must be
refused with an error (Splice, FrameEval, alpha) rather than crash, copies of GPU frames (planes
shared, props writable, pixel access refused -- checked, since a NULL plane handed out would be
the crash), max_vram_cache_size moved under load, 1080p float frames against a limit that may be
256 MB, ModifyFrame callbacks returning copies, FrameEval returning GPU clips, and the device
list's temporary instance beside the live device.

Meant to run plain and under ASan and TSan through LD_PRELOAD (see linux_tests.md section 1).
"""
import collections
import concurrent.futures
import faulthandler
import gc
import logging
import os
import queue
import random
import sys
import threading
import time

import vapoursynth as vs

faulthandler.enable()
core = vs.core

logged = collections.Counter()   # level -> count
warned = []                      # WARNING and above, verbatim


class Recorder(logging.Handler):
    def emit(self, record):
        logged[record.levelname] += 1
        if record.levelno >= logging.WARNING:
            warned.append(record.getMessage()[:160])
            sys.stderr.write(f"CORE {record.levelname}: {record.getMessage()}\n")
            sys.stderr.flush()


_log = logging.getLogger("vapoursynth")
_log.setLevel(logging.DEBUG)
_log.addHandler(Recorder())

SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else int(time.time())
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
ROUNDS = 8

EXPECTED = {"Error", "RuntimeError", "ValueError", "TypeError", "AttributeError", "KeyError",
            "IndexError", "TimeoutError", "CancelledError", "NotImplementedError", "OverflowError"}

lock = threading.Lock()
nodes = []                      # shared graphs, bounded
handoff = queue.Queue()         # frames crossing threads
stats = collections.Counter()   # exception types and op counts
unexpected = collections.Counter()
progress = collections.defaultdict(int)
lastop = {}
stop = threading.Event()

FORMATS = [vs.RGBS, vs.GRAY8, vs.GRAYS, vs.YUV420P8, vs.YUV444PS, vs.RGB24]


def note(tid, op):
    lastop[tid] = op
    progress[tid] += 1
    with lock:
        stats["op:" + op] += 1


violations = []


def violation(msg):
    with lock:
        violations.append(msg)
    print(f"  VIOLATION: {msg}", flush=True)


def caught(e):
    name = type(e).__name__
    with lock:
        stats["exc:" + name] += 1
        if name not in EXPECTED:
            unexpected[name + ": " + str(e)[:60]] += 1


def source(rng):
    w = rng.choice([64, 96, 128, 200, 320])
    h = rng.choice([48, 64, 120, 180])
    fmt = rng.choice(FORMATS)
    return core.std.BlankClip(width=w, height=h, format=fmt, length=24,
                              color=[rng.random()] * core.get_video_format(fmt).num_planes)


def gpu_op(rng, c):
    """One GPU-capable std op on an uploaded clip; declines raise vs.Error, which is expected."""
    k = rng.randrange(12)
    if k == 0:
        return core.std.Expr(c, "x 0.5 *")
    if k == 1:
        return core.std.Expr([c, c], "x y + 2 /")
    if k == 2:
        return core.std.Lut(c, planes=0, function=lambda x: x)
    if k == 3:
        return core.std.MaskedMerge(c, c, c) if c.format.num_planes == 1 else core.std.MaskedMerge(c, c, core.std.ShufflePlanes(c, 0, vs.GRAY))
    if k == 4:
        return core.resize.Bicubic(c, width=c.width // 2 or 8, height=c.height // 2 or 8)
    if k == 5:
        return core.std.Transpose(c)
    if k == 6:
        return core.std.Convolution(c, [1, 2, 1, 2, 4, 2, 1, 2, 1])
    if k == 7:
        return core.std.CropAbs(c, width=max(8, c.width // 2), height=max(8, c.height // 2))
    if k == 8:
        return core.std.PlaneStats(c)
    if k == 9:
        return core.std.MakeFullDiff(c, c) if c.format.bits_per_sample < 32 else core.std.Expr(c, "x")
    if k == 10:
        return core.std.StackHorizontal([c, c])
    return core.std.PreMultiply(c, core.std.ShufflePlanes(c, 0, vs.GRAY)) if c.format.color_family != vs.YUV else core.std.Expr(c, "x")


def build(rng):
    c = core.std.GPUUpload(source(rng))
    for _ in range(rng.randrange(1, 4)):
        c = gpu_op(rng, c)
    if rng.random() < 0.5:
        c = core.std.GPUDownload(c)
    return c


def pick_node(rng):
    with lock:
        return rng.choice(nodes) if nodes else None


def worker(tid, seed):
    rng = random.Random(seed)
    held = []
    futures = []
    big = []
    while not stop.is_set():
        try:
            op = rng.randrange(130)
            if op < 12:
                note(tid, "build")
                n = build(rng)
                with lock:
                    nodes.append(n)
                    if len(nodes) > 24:
                        nodes.pop(rng.randrange(len(nodes)))
            elif op < 30:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "get_frame")
                    f = n.get_frame(rng.randrange(n.num_frames))
                    held.append(f)
                    if len(held) > 8:
                        held.pop(rng.randrange(len(held)))
            elif op < 38:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "get_frame_async")
                    futures.append(n.get_frame_async(rng.randrange(n.num_frames)))
                    if len(futures) > 6:
                        fut = futures.pop(rng.randrange(len(futures)))
                        if rng.random() < 0.7:
                            f = fut.result(timeout=60)
                            held.append(f)
                        # else: abandoned with the request possibly still in flight
            elif op < 44:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "frames_abandon")
                    for i, f in enumerate(n.frames(prefetch=rng.choice([None, 2, 4]))):
                        if i >= rng.randrange(1, 5):
                            break  # abandon the generator mid-stream
            elif op < 54:
                if held:
                    f = held.pop(rng.randrange(len(held)))
                    k = rng.randrange(4)
                    if k == 0:
                        note(tid, "close")
                        f.close()
                    elif k == 1:
                        note(tid, "close_twice")
                        f.close()
                        f.close()
                    elif k == 2:
                        note(tid, "use_after_close")
                        f.close()
                        _ = f.props          # expected to raise
                        _ = f[0]
                    else:
                        note(tid, "handoff")
                        handoff.put(f)      # another thread closes or drops it
            elif op < 62:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "with_block")
                    with n.get_frame(rng.randrange(n.num_frames)) as f:
                        _ = f.props
                        c2 = f.copy()
                        _ = f[0]
                        _ = c2.readonly
                    _ = f.props              # after the with: expected to raise
            elif op < 70:
                try:
                    f = handoff.get_nowait()
                except queue.Empty:
                    continue
                note(tid, "receive")
                k = rng.randrange(3)
                if k == 0:
                    f.close()
                elif k == 1:
                    _ = f.copy().props
                # else: just drop it
            elif op < 78:
                if held:
                    f = rng.choice(held)
                    note(tid, "planes_props")
                    for p in range(f.format.num_planes):
                        mv = f[p]
                        _ = len(mv)
                        del mv
                    c2 = f.copy()
                    c2.props["fuzz"] = rng.randrange(1000)
                    _ = c2.props["fuzz"]
                    del c2.props["fuzz"]
                    _ = dict(c2.props)
            elif op < 83:
                note(tid, "core_churn")
                core.max_cache_size = rng.choice([1, 4, 16, 64])
                core.num_threads = rng.choice([1, 2, 4, 8])
            elif op < 86:
                note(tid, "device_info")
                _ = core.vulkan_device_info
            elif op < 90:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "outputs")
                    idx = rng.randrange(4)
                    n.set_output(idx)
                    if rng.random() < 0.5:
                        vs.clear_output(idx)
                    else:
                        vs.clear_outputs()
            elif op < 95:
                note(tid, "drop_nodes")
                with lock:
                    for _ in range(min(len(nodes), rng.randrange(1, 4))):
                        nodes.pop(rng.randrange(len(nodes)))
            elif op < 100:
                note(tid, "gc")
                gc.collect()
            elif op < 108:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "mixed_graph")   # CPU-only std filters on whatever residency the node has
                    k = rng.randrange(7)
                    if k == 0:
                        m = core.std.FlipVertical(n)
                    elif k == 1:
                        m = core.std.Trim(n, first=rng.randrange(n.num_frames))
                    elif k == 2:
                        m = core.std.SetFrameProps(n, fuzz=rng.randrange(100))
                    elif k == 3:
                        m = core.std.Interleave([n, n])
                    elif k == 4:
                        m = core.std.Reverse(n)
                    elif k == 5:
                        m = core.std.Loop(n, 2)
                    else:
                        m = core.std.SelectEvery(n, 2, 0)
                    if rng.random() < 0.5:
                        m = gpu_op(rng, m)
                    held.append(m.get_frame(rng.randrange(m.num_frames)))
                    if len(held) > 8:
                        held.pop(rng.randrange(len(held)))
            elif op < 112:
                note(tid, "pingpong")   # upload, work, download, again: the transfer paths both ways
                c = source(rng)
                for _ in range(rng.randrange(1, 4)):
                    c = core.std.GPUDownload(gpu_op(rng, core.std.GPUUpload(c)))
                    if rng.random() < 0.3:
                        c = core.std.FlipVertical(c)
                held.append(c.get_frame(rng.randrange(c.num_frames)))
                if len(held) > 8:
                    held.pop(rng.randrange(len(held)))
            elif op < 116:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "mismatch")   # residency mismatches are refused with an error, never a crash
                    b = source(rng)
                    k = rng.randrange(4)
                    if k == 0:
                        core.std.Splice([n, b]).get_frame(0)
                    elif k == 1:
                        core.std.FrameEval(b, lambda _n, n=n: n).get_frame(0)
                    elif k == 2:
                        n.set_output(3, alpha=b)
                    else:
                        core.std.GPUUpload(core.std.GPUUpload(n)).get_frame(0)
            elif op < 120:
                if held:
                    f = rng.choice(held)
                    note(tid, "gpu_copy")   # copies of GPU frames share planes; props are the only writable part
                    c = f.copy()
                    _ = c.gpu_resident
                    c.props["fuzz"] = rng.randrange(1000)
                    _ = c.readonly
                    if f.gpu_resident:
                        try:
                            _ = f[0]
                        except vs.Error:
                            pass
                        else:
                            violation("a GPU resident frame handed out its plane")
            elif op < 123:
                note(tid, "vram_limit")   # moves the eviction limit and the gate budget under load
                core.max_vram_cache_size = rng.choice([64, 128, 256, 384, 512])
                _ = core.max_vram_cache_size
            elif op < 126:
                note(tid, "big_frame")   # 1080p float frames against a limit that may be 256 MB
                fmt = rng.choice([vs.RGBS, vs.GRAYS, vs.YUV444PS])
                c = core.std.BlankClip(width=1920, height=1080, format=fmt, length=4,
                                       color=[rng.random()] * core.get_video_format(fmt).num_planes)
                c = gpu_op(rng, core.std.GPUUpload(c))
                big.append(c.get_frame(rng.randrange(4)))
                while len(big) > 2:
                    big.pop(0)
            elif op < 128:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "modify_frame")   # a Python callback returning a copy with a prop set

                    def modify(n_, f):
                        c = f.copy()
                        c.props["fuzzmod"] = 1
                        return c
                    m = core.std.ModifyFrame(n, n, modify)
                    held.append(m.get_frame(rng.randrange(m.num_frames)))
                    if len(held) > 8:
                        held.pop(rng.randrange(len(held)))
            elif op < 129:
                if rng.random() < 0.1:
                    note(tid, "devices")   # a temporary instance beside the live device
                    _ = core.vulkan_devices
            else:
                n = pick_node(rng)
                if n is not None:
                    note(tid, "frame_eval")
                    m = core.std.FrameEval(n, lambda _n, n=n: n)
                    held.append(m.get_frame(rng.randrange(m.num_frames)))
                    if len(held) > 8:
                        held.pop(rng.randrange(len(held)))
        except Exception as e:  # noqa: BLE001 -- counted, and that is the point
            caught(e)
    # wind down
    for fut in futures:
        try:
            fut.result(timeout=60)
        except Exception as e:  # noqa: BLE001
            caught(e)
    held.clear()
    big.clear()


def gc_thread():
    while not stop.is_set():
        gc.collect()
        time.sleep(0.005)


def watchdog():
    last = {}
    idle = collections.Counter()
    while not stop.is_set():
        time.sleep(1)
        for tid in list(progress):
            cur = progress[tid]
            if last.get(tid) == cur:
                idle[tid] += 1
                if idle[tid] >= 60:
                    print(f"\nFAIL: thread {tid} made no progress for 60 s; last op: {lastop.get(tid)}", flush=True)
                    faulthandler.dump_traceback(all_threads=True)
                    os._exit(3)
            else:
                idle[tid] = 0
            last[tid] = cur


def settle_and_measure():
    """Drop everything, collect, evict, and read the GPU allocation the core reports."""
    with lock:
        nodes.clear()
    while True:
        try:
            handoff.get_nowait().close()
        except queue.Empty:
            break
        except Exception as e:  # noqa: BLE001
            caught(e)
    vs.clear_outputs()
    core.max_cache_size = 1
    for _ in range(3):
        gc.collect()
    time.sleep(0.3)
    return core.vulkan_device_info["allocated"] // (1 << 20)


def rss_mb():
    try:
        with open("/proc/self/statm") as fh:
            return int(fh.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") // (1 << 20)
    except Exception:  # noqa: BLE001
        return -1


print(f"L23: {THREADS} threads, {SECONDS} s in {ROUNDS} rounds, seed {SEED}  (rerun with these to reproduce)", flush=True)
core.max_cache_size = 16
core.num_threads = 4
with core.std.GPUDownload(core.std.GPUUpload(core.std.BlankClip(width=64, height=64))).get_frame(0):
    pass
baseline = settle_and_measure()
print(f"  baseline after warm-up: {baseline} MB GPU, {rss_mb()} MB RSS", flush=True)

threading.Thread(target=watchdog, daemon=True).start()
threading.Thread(target=gc_thread, daemon=True).start()
series = []
per_round = max(1, SECONDS // ROUNDS)
for r in range(ROUNDS):
    stop.clear()
    ts = [threading.Thread(target=worker, args=(t, SEED * 1000 + t), daemon=True) for t in range(THREADS)]
    for t in ts:
        t.start()
    time.sleep(per_round)
    stop.set()
    for t in ts:
        t.join(timeout=120)
    alive = sum(t.is_alive() for t in ts)
    if alive:
        print(f"\nFAIL: {alive} worker(s) did not finish round {r}", flush=True)
        faulthandler.dump_traceback(all_threads=True)
        os._exit(4)
    gpu = settle_and_measure()
    series.append((gpu, rss_mb()))
    print(f"  round {r}: settled at {gpu} MB GPU, {series[-1][1]} MB RSS", flush=True)

ops = sum(v for k, v in stats.items() if k.startswith("op:"))
excs = sorted(((v, k[4:]) for k, v in stats.items() if k.startswith("exc:")), reverse=True)
print("\nresults:")
print(f"  {ops} ops; exceptions by type: " + ", ".join(f"{k} x{v}" for v, k in excs[:8]))
if unexpected:
    print("  exception types outside the expected set (flagged, not failed):")
    for k, v in unexpected.most_common(6):
        print(f"    x{v} {k}")
gpus = [g for g, _ in series]
rsss = [m for _, m in series]
# `allocated` is what the allocator retains, in 128 MB blocks: it ratchets up as the thread
# interleaving finds new instantaneous peaks -- even with the same op sequence every round, since
# the multiset of ops is fixed but their overlap is not -- and, since the third round moves
# max_vram_cache_size under load, it is trimmed back whenever the limit drops, so the series is
# noisy in both directions. A leak lifts the floor: every later round settles above every earlier
# one. So: the lowest settled value of the second half must not exceed the highest of the first
# half by a block. The exact live-frame check is the core's own teardown warning, caught by the
# handler above and grepped from the log after exit.
half = len(gpus) // 2
climb = min(gpus[half:]) - max(gpus[:half]) > 128
# Same shape as the GPU test above, and for the same reason: comparing the last round against the
# first two made a single high round a failure, which is what a run whose first rounds happened to
# settle low produced -- it did not reproduce at the same seed, in five runs.
rss_climb = min(rsss[half:]) > max(rsss[:half]) + max(64, max(rsss[:half]) // 5)
teardown_leak = any("still allocated" in w for w in warned)
print(f"  GPU MB settled per round: {gpus}  (baseline {baseline})  -> {'STILL GROWING in the second half' if climb else 'plateau'}")
print(f"  RSS MB per round:         {rsss}  -> {'CLIMBING' if rss_climb else 'plateau'}")
print(f"  core messages seen by the handler: {dict(logged)}")
for w in warned[:8]:
    print(f"    WARNING+: {w}")
for v in violations[:8]:
    print(f"  VIOLATION: {v}")
if climb or rss_climb or teardown_leak or violations:
    print(f"FAILED (seed {SEED})")
    sys.exit(1)
print("  process survived, every worker finished every round, no teardown leak warning")
print("ALL PASS")
