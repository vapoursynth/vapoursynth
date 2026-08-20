"""Test harness for the GPU resize path.

Modes:
  python gpuresize_test.py refcheck   -- validate resize_reference against the scalar
                                         zimg path; this calibrates the reference itself
                                         and prints the measured floors everything later
                                         is pinned against.
  python gpuresize_test.py gpu        -- differential-test the GPU path against the
                                         reference (grows with each implementation step).

Two harness rules carried from earlier GPU work: a declined GPU path silently runs the
scalar code and passes any differential vacuously, so every GPU test asserts that no
decline was logged; and nothing is concluded from a three-repetition timing run.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import vapoursynth as vs

# unittest discovery imports every file matching *test.py, and the CI runners have neither
# numpy nor a GPU. This file is an argv-driven harness rather than a unittest module, so a
# missing dependency must not fail discovery; running it by hand still requires numpy.
try:
    import numpy as np
    import resize_reference as ref
except ImportError:
    np = None
    ref = None

core = vs.core


# ---------------------------------------------------------------------------------------
# Format plumbing.

def fmt_of(vsformat):
    family = {vs.GRAY: "gray", vs.YUV: "yuv", vs.RGB: "rgb"}[vsformat.color_family]
    return ref.Fmt(family, vsformat.bits_per_sample, vsformat.sample_type == vs.FLOAT,
                   vsformat.subsampling_w, vsformat.subsampling_h)


def np_dtype(vsformat):
    if vsformat.sample_type == vs.FLOAT:
        return np.float16 if vsformat.bytes_per_sample == 2 else np.float32
    return np.uint8 if vsformat.bytes_per_sample == 1 else np.uint16


def make_clip(planes, format_id, length=1):
    """A clip whose every frame holds exactly these numpy planes."""
    h, w = planes[0].shape
    base = core.std.BlankClip(width=w, height=h, format=format_id, length=length)
    fmt = base.format
    cast = [np.ascontiguousarray(p, dtype=np_dtype(fmt)) for p in planes]

    def inject(n, f):
        fout = f.copy()
        for p in range(fout.format.num_planes):
            np.asarray(fout[p])[:] = cast[p]
        return fout

    return base.std.ModifyFrame(clips=base, selector=inject)


def get_planes(clip, n=0):
    with clip.get_frame(n) as f:
        return [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]


def peak_of(vsformat):
    return float((1 << vsformat.bits_per_sample) - 1) if vsformat.sample_type == vs.INTEGER else 1.0


# ---------------------------------------------------------------------------------------
# Content. Noise is the honest case for resampling accuracy -- full spectrum -- and the
# ramp-plus-edges pattern is where overshoot and rounding ties concentrate.

def noise_planes(fmt, w, h, seed=1):
    rng = np.random.default_rng(seed)
    planes = []
    for p in range(fmt.num_planes):
        pw = w >> (fmt.subsampling_w if p else 0)
        ph = h >> (fmt.subsampling_h if p else 0)
        if fmt.sample_type == vs.FLOAT:
            lo, hi = (-0.5, 0.5) if (fmt.color_family == vs.YUV and p) else (0.0, 1.0)
            planes.append(rng.uniform(lo, hi, (ph, pw)))
        else:
            planes.append(rng.integers(0, 1 << fmt.bits_per_sample, (ph, pw)))
    return planes


def edges_planes(fmt, w, h):
    planes = []
    for p in range(fmt.num_planes):
        pw = w >> (fmt.subsampling_w if p else 0)
        ph = h >> (fmt.subsampling_h if p else 0)
        yy, xx = np.mgrid[0:ph, 0:pw]
        ramp = (xx / max(pw - 1, 1) + yy / max(ph - 1, 1)) / 2.0
        blocks = ((xx // 17 + yy // 13) % 2).astype(np.float64)
        v = 0.7 * ramp + 0.3 * blocks
        if fmt.sample_type == vs.FLOAT:
            if fmt.color_family == vs.YUV and p:
                v = v - 0.5
            planes.append(v)
        else:
            planes.append(np.floor(v * ((1 << fmt.bits_per_sample) - 1) + 0.5))
    return planes


# ---------------------------------------------------------------------------------------
# Decline detection. The GPU path logs one mtInformation line when it hands a call back
# to the scalar code; a differential test that ran that way proves nothing.

class LogCatcher:
    def __init__(self):
        self.messages = []
        self._handle = None

    def __enter__(self):
        self._handle = core.add_log_handler(lambda lvl, msg: self.messages.append(msg))
        return self

    def __exit__(self, *exc):
        core.remove_log_handler(self._handle)

    def declines(self):
        return [m for m in self.messages if "downloaded and resized on the CPU" in m]


# ---------------------------------------------------------------------------------------
# refcheck: the reference against the scalar path.

KERNELS = {
    "Point": ("point", float("nan"), float("nan")),
    "Bilinear": ("bilinear", float("nan"), float("nan")),
    "Bicubic": ("bicubic", float("nan"), float("nan")),
    "BicubicBspline": ("bicubic", 1.0, 0.0),
    "Lanczos": ("lanczos", float("nan"), float("nan")),
    "Lanczos4": ("lanczos", 4.0, float("nan")),
    "Spline16": ("spline16", float("nan"), float("nan")),
    "Spline36": ("spline36", float("nan"), float("nan")),
    "Spline64": ("spline64", float("nan"), float("nan")),
}


def scalar_resize(clip, kernel_key, dst_w, dst_h, **kw):
    name, a, b = KERNELS[kernel_key]
    fn = getattr(core.resize, {"point": "Point", "bilinear": "Bilinear", "bicubic": "Bicubic",
                               "lanczos": "Lanczos", "spline16": "Spline16",
                               "spline36": "Spline36", "spline64": "Spline64"}[name])
    if not np.isnan(a):
        kw.setdefault("filter_param_a", a)
    if not np.isnan(b):
        kw.setdefault("filter_param_b", b)
    return fn(clip, width=dst_w, height=dst_h, **kw)


def reference_resize(planes, src_vsfmt, dst_vsfmt, dst_w, dst_h, kernel_key,
                     kernel_uv_key=None, **kw):
    name, a, b = KERNELS[kernel_key]
    kernel = ref.make_kernel(name, a, b)
    kuv = None
    if kernel_uv_key is not None:
        uname, ua, ub = KERNELS[kernel_uv_key]
        kuv = ref.make_kernel(uname, ua, ub)
    return ref.resize_clip(planes, fmt_of(src_vsfmt), fmt_of(dst_vsfmt),
                           dst_w, dst_h, kernel, kernel_uv=kuv, **kw)


def run_refcheck():
    results = []
    failures = []

    def check(label, db, floor, maxd=None, maxd_limit=None):
        ok = db >= floor and (maxd_limit is None or maxd <= maxd_limit)
        results.append((label, db, maxd, ok))
        if not ok:
            failures.append(label)

    # Float32, every kernel, up and down.
    for kernel_key in KERNELS:
        for dst_w, dst_h, tag in ((1280, 720, "up"), (320, 180, "down")):
            planes = noise_planes(core.get_video_format(vs.GRAYS), 640, 360)
            clip = make_clip(planes, vs.GRAYS)
            out = get_planes(scalar_resize(clip, kernel_key, dst_w, dst_h))
            want = reference_resize(planes, clip.format, clip.format, dst_w, dst_h, kernel_key)
            db = ref.accuracy_db(want[0], out[0], 1.0)
            check(f"GRAYS {kernel_key} {tag}", db, 110.0)

    # Geometry variations on two kernels: odd sizes, windows, fractional windows.
    geoms = [
        (1000, 556, {}, "odd"),
        (640, 360, {"src_left": 1.25, "src_top": 2.5, "src_width": 630.5, "src_height": 350.25}, "window-1:1"),
        (960, 540, {"src_left": -3.75, "src_top": 0.5, "src_width": 645.0, "src_height": 355.5}, "window-scale"),
        (479, 271, {"src_width": 639.0, "src_height": 359.0}, "window-down"),
    ]
    for kernel_key in ("Bicubic", "Lanczos4"):
        for dst_w, dst_h, window, tag in geoms:
            planes = edges_planes(core.get_video_format(vs.GRAYS), 640, 360)
            clip = make_clip(planes, vs.GRAYS)
            out = get_planes(scalar_resize(clip, kernel_key, dst_w, dst_h, **window))
            want = reference_resize(planes, clip.format, clip.format, dst_w, dst_h,
                                    kernel_key, **window)
            db = ref.accuracy_db(want[0], out[0], 1.0)
            check(f"GRAYS {kernel_key} {tag}", db, 110.0)

    # Subsampled float: chroma window math, default left siting and a stated top_left.
    for loc, tag in ((None, "left"), (2, "top_left")):
        planes = noise_planes(core.get_video_format(vs.YUV420PS), 640, 360, seed=7)
        clip = make_clip(planes, vs.YUV420PS)
        if loc is not None:
            clip = clip.std.SetFrameProps(_ChromaLocation=loc)
        out = get_planes(scalar_resize(clip, "Bicubic", 960, 540))
        want = reference_resize(planes, clip.format, clip.format, 960, 540, "Bicubic",
                                src_loc=loc if loc is not None else 0)
        for p in range(3):
            db = ref.accuracy_db(want[p], out[p], 1.0)
            check(f"YUV420PS 2x plane{p} {tag}", db, 110.0)

    # Subsampling change within the family.
    planes = noise_planes(core.get_video_format(vs.YUV444PS), 640, 360, seed=8)
    clip = make_clip(planes, vs.YUV444PS)
    out = get_planes(scalar_resize(clip, "Bicubic", 640, 360, format=vs.YUV420PS))
    want = reference_resize(planes, clip.format, core.get_video_format(vs.YUV420PS),
                            640, 360, "Bicubic")
    for p in range(3):
        db = ref.accuracy_db(want[p], out[p], 1.0)
        check(f"444PS->420PS plane{p}", db, 110.0)

    # Integer formats: these rows quantify zimg's distance from the float-chain model,
    # they do not validate the reference. zimg's integer paths run 1.14 fixed point and
    # chain two integer passes whose intermediate is quantised AND clipped to the storage
    # range, truncating kernel overshoot; the model resamples in float and clips once.
    # Measured: rms ~0.07 LSB at 8 bit with a clipped-ringing tail of a few LSB, and up
    # to ~7% of full scale on the worst ringing pixel at 16 bit. The GPU path is pinned
    # against the reference, not against these numbers.
    planes = noise_planes(core.get_video_format(vs.GRAY8), 640, 360, seed=3)
    clip = make_clip(planes, vs.GRAY8)
    out = get_planes(scalar_resize(clip, "Bicubic", 320, 180))
    want = reference_resize(planes, clip.format, clip.format, 320, 180, "Bicubic")
    check("GRAY8 down vs model", ref.accuracy_db(want[0], out[0], 255.0), 60.0,
          ref.max_diff(want[0], out[0]), 8.0)

    planes = noise_planes(core.get_video_format(vs.GRAY16), 640, 360, seed=4)
    clip = make_clip(planes, vs.GRAY16)
    out = get_planes(scalar_resize(clip, "Bicubic", 1280, 720))
    want = reference_resize(planes, clip.format, clip.format, 1280, 720, "Bicubic")
    check("GRAY16 up vs model", ref.accuracy_db(want[0], out[0], 65535.0), 48.0,
          ref.max_diff(want[0], out[0]), 8192.0)

    # The string spellings of range and chromaloc, which go through the shared tables: a
    # pure range renumbering and a stated input siting on a subsampled scale. zimg runs
    # the range affine through a float32 intermediate, so rare near-ties land 1 LSB from
    # the float64 model; a table domain mix-up would be off by thousands, which is what
    # this row is here to catch.
    planes = noise_planes(core.get_video_format(vs.GRAY16), 320, 180, seed=9)
    clip = make_clip(planes, vs.GRAY16)
    out = get_planes(scalar_resize(clip, "Bicubic", 320, 180, range_in_s="full", range_s="limited"))
    want = reference_resize(planes, clip.format, clip.format, 320, 180, "Bicubic",
                            full_in=True, full_out=False)
    check("GRAY16 full->limited", ref.accuracy_db(want[0], out[0], 65535.0), 100.0,
          ref.max_diff(want[0], out[0]), 1.0)

    planes = noise_planes(core.get_video_format(vs.YUV420PS), 640, 360, seed=10)
    clip = make_clip(planes, vs.YUV420PS)
    out = get_planes(scalar_resize(clip, "Bicubic", 960, 540, chromaloc_in_s="top_left"))
    want = reference_resize(planes, clip.format, clip.format, 960, 540, "Bicubic", src_loc=2)
    for p in range(3):
        db = ref.accuracy_db(want[p], out[p], 1.0)
        check(f"YUV420PS chromaloc_in_s plane{p}", db, 110.0)

    # Pure format conversions round half to even and should then match zimg exactly.
    planes = noise_planes(core.get_video_format(vs.GRAY8), 320, 180, seed=5)
    clip = make_clip(planes, vs.GRAY8)
    out = get_planes(scalar_resize(clip, "Bicubic", 320, 180, format=vs.GRAY16))
    want = reference_resize(planes, clip.format, core.get_video_format(vs.GRAY16),
                            320, 180, "Bicubic")
    check("GRAY8->GRAY16 pure depth", ref.accuracy_db(want[0], out[0], 65535.0), 120.0,
          ref.max_diff(want[0], out[0]), 0.0)

    planes = noise_planes(core.get_video_format(vs.GRAY16), 320, 180, seed=6)
    clip = make_clip(planes, vs.GRAY16)
    out = get_planes(scalar_resize(clip, "Bicubic", 320, 180, format=vs.GRAY8))
    want = reference_resize(planes, clip.format, core.get_video_format(vs.GRAY8),
                            320, 180, "Bicubic")
    check("GRAY16->GRAY8 pure depth", ref.accuracy_db(want[0], out[0], 255.0), 44.0,
          ref.max_diff(want[0], out[0]), 1.0)

    width = max(len(r[0]) for r in results)
    for label, db, maxd, ok in results:
        extra = "" if maxd is None else f"   maxdiff {maxd:g}"
        print(f"  {label:<{width}}  {db:8.2f} dB{extra}{'' if ok else '   FAIL'}")
    print()
    if failures:
        print(f"{len(failures)} of {len(results)} checks FAILED:")
        for f in failures:
            print("  " + f)
        return 1
    print(f"all {len(results)} checks passed")
    return 0


# ---------------------------------------------------------------------------------------
# gpu: the compute path against the reference. Every case goes through GPUUpload so the
# create hook is offered a GPU clip, and the decline log is asserted empty -- a declined
# case would compare the scalar path against the reference and prove nothing about the
# GPU.

def gpu_resize(clip, kernel_key, dst_w, dst_h, **kw):
    up = core.std.GPUUpload(clip)
    out = scalar_resize(up, kernel_key, dst_w, dst_h, **kw)
    return core.std.GPUDownload(out)


def run_gpu():
    results = []
    failures = []

    def run_case(label, planes, fmt_id, kernel_key, dst_w, dst_h, floor, maxd_limit=None,
                 dst_fmt_id=None, props=None, refargs=None, **kw):
        fmt = core.get_video_format(fmt_id)
        dst_fmt = core.get_video_format(dst_fmt_id) if dst_fmt_id is not None else fmt
        # Everything downstream sees exactly what the clip stores: the cast through the
        # storage type is applied before both paths, or a half format would measure its
        # own quantisation as reference error.
        planes = [np.ascontiguousarray(p, np_dtype(fmt)).astype(np.float64) for p in planes]
        clip = make_clip(planes, fmt_id)
        if props:
            clip = clip.std.SetFrameProps(**props)
        callkw = dict(kw)
        if dst_fmt_id is not None:
            callkw["format"] = dst_fmt_id
        with LogCatcher() as log:
            with gpu_resize(clip, kernel_key, dst_w, dst_h, **callkw).get_frame(0) as f:
                out = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
                gpu_props = dict(f.props)
            declined = log.declines()
        if declined:
            results.append((label, float("nan"), None, False))
            failures.append(label + "  [DECLINED: " + declined[0] + "]")
            return
        # The scalar path on the same call is the authority on frame properties; pixels
        # are pinned against the reference instead.
        with scalar_resize(clip, kernel_key, dst_w, dst_h, **callkw).get_frame(0) as f:
            scalar_props = dict(f.props)
        want = reference_resize(planes, fmt, dst_fmt, dst_w, dst_h, kernel_key, **(refargs or {}),
                                **{k: v for k, v in kw.items()
                                   if k.startswith("src_") and k != "src_loc"})
        peak = peak_of(dst_fmt)
        worst = min(ref.accuracy_db(want[p], out[p], peak) for p in range(len(want)))
        maxd = max(ref.max_diff(want[p], out[p]) for p in range(len(want)))
        ok = worst >= floor and (maxd_limit is None or maxd <= maxd_limit)
        if gpu_props != scalar_props:
            ok = False
            failures.append(label + f"  [PROPS: gpu {gpu_props} != scalar {scalar_props}]")
        results.append((label, worst, maxd, ok))
        if not ok and (not failures or not failures[-1].startswith(label)):
            failures.append(label)

    # Every kernel, both directions, on noise -- the honest content for accuracy.
    for kernel_key in KERNELS:
        for dst_w, dst_h, tag in ((1280, 720, "up"), (320, 180, "down")):
            planes = noise_planes(core.get_video_format(vs.GRAYS), 640, 360)
            run_case(f"GRAYS {kernel_key} {tag}", planes, vs.GRAYS, kernel_key,
                     dst_w, dst_h, GPU_FLOOR_DB)

    # Geometry: odd sizes, windows, single-axis plans, on ringing-friendly content.
    geoms = [
        (1000, 556, {}, "odd"),
        (1280, 360, {}, "h-only"),
        (640, 720, {}, "v-only"),
        (640, 360, {"src_left": 1.25, "src_top": 2.5, "src_width": 630.5, "src_height": 350.25}, "window-1:1"),
        (960, 540, {"src_left": -3.75, "src_top": 0.5, "src_width": 645.0, "src_height": 355.5}, "window-scale"),
        (479, 271, {"src_width": 639.0, "src_height": 359.0}, "window-down"),
        (640, 360, {"src_left": 0.5}, "shift-only"),
    ]
    for kernel_key in ("Bicubic", "Lanczos4"):
        for dst_w, dst_h, window, tag in geoms:
            planes = edges_planes(core.get_video_format(vs.GRAYS), 640, 360)
            run_case(f"GRAYS {kernel_key} {tag}", planes, vs.GRAYS, kernel_key,
                     dst_w, dst_h, GPU_FLOOR_DB, **window)

    # Three-plane formats through the same plan.
    planes = noise_planes(core.get_video_format(vs.YUV444PS), 640, 360, seed=11)
    run_case("YUV444PS Bicubic up", planes, vs.YUV444PS, "Bicubic", 960, 540, GPU_FLOOR_DB)
    planes = noise_planes(core.get_video_format(vs.RGBS), 640, 360, seed=12)
    run_case("RGBS Spline36 down", planes, vs.RGBS, "Spline36", 320, 180, GPU_FLOOR_DB)

    # 4K-scale dimensions: the compensated position arithmetic is what holds accuracy
    # flat as coordinates grow; a plain float32 position would lose ~25 dB here.
    planes = noise_planes(core.get_video_format(vs.GRAYS), 3840, 160, seed=13)
    run_case("GRAYS Lanczos 4K-wide down", planes, vs.GRAYS, "Lanczos", 1920, 160, GPU_FLOOR_DB)
    planes = noise_planes(core.get_video_format(vs.GRAYS), 1920, 120, seed=14)
    run_case("GRAYS Bicubic 4K-wide up", planes, vs.GRAYS, "Bicubic", 3840, 120, GPU_FLOOR_DB)

    # --- Step 3 scope: storage formats, subsampling with siting, conversions. Integer
    # outputs are quantised identically by both sides, so the only differences are ties
    # the float32 chain lands on the other side of; the maxdiff bound of one LSB is the
    # real assertion and the dB floor just catches wholesale breakage.
    def fmt_case(label, fmt_id, kernel_key, dst_w, dst_h, floor, maxd_limit=None, seed=20, **kw):
        planes = noise_planes(core.get_video_format(fmt_id), 640, 360, seed=seed)
        run_case(label, planes, fmt_id, kernel_key, dst_w, dst_h, floor, maxd_limit, **kw)

    fmt_case("GRAY8 Bicubic down", vs.GRAY8, "Bicubic", 320, 180, 60.0, 1.0, seed=21)
    fmt_case("GRAY8 Lanczos up", vs.GRAY8, "Lanczos", 1280, 720, 60.0, 1.0, seed=22)
    fmt_case("GRAY16 Lanczos4 up", vs.GRAY16, "Lanczos4", 1280, 720, 96.0, 1.0, seed=23)
    fmt_case("GRAY16 Spline36 down", vs.GRAY16, "Spline36", 320, 180, 96.0, 1.0, seed=24)
    fmt_case("GRAYH Spline36 down", vs.GRAYH, "Spline36", 320, 180, 68.0, 1e-3, seed=25)
    fmt_case("YUV420P8 Bicubic up", vs.YUV420P8, "Bicubic", 1280, 720, 60.0, 1.0, seed=26)
    fmt_case("YUV420P16 Bicubic down", vs.YUV420P16, "Bicubic", 320, 180, 96.0, 1.0, seed=27)
    fmt_case("YUV422P10 Bicubic odd", vs.YUV422P10, "Bicubic", 1000, 556, 80.0, 1.0, seed=28)
    fmt_case("YUV420PS Lanczos up", vs.YUV420PS, "Lanczos", 960, 540, GPU_FLOOR_DB, seed=29)

    # Depth, range and subsampling conversions; grey to and from YUV.
    fmt_case("GRAY8->GRAY16 down", vs.GRAY8, "Bicubic", 320, 180, 96.0, 1.0, seed=30,
             dst_fmt_id=vs.GRAY16)
    fmt_case("GRAYS->GRAY8 1:1 depth", vs.GRAYS, "Bicubic", 640, 360, 60.0, 1.0, seed=31,
             dst_fmt_id=vs.GRAY8)
    fmt_case("GRAY16->GRAYH 1:1 depth", vs.GRAY16, "Bicubic", 640, 360, 68.0, 1e-3, seed=32,
             dst_fmt_id=vs.GRAYH)
    fmt_case("444P8->420P8 subsampling", vs.YUV444P8, "Bicubic", 640, 360, 60.0, 1.0, seed=33,
             dst_fmt_id=vs.YUV420P8)
    fmt_case("420P8->444P8 subsampling", vs.YUV420P8, "Bicubic", 640, 360, 60.0, 1.0, seed=34,
             dst_fmt_id=vs.YUV444P8)
    fmt_case("420P8->GRAY8 drop", vs.YUV420P8, "Bicubic", 640, 360, 60.0, 0.0, seed=35,
             dst_fmt_id=vs.GRAY8)
    fmt_case("GRAY8->420P8 fill", vs.GRAY8, "Bicubic", 640, 360, 60.0, 0.0, seed=36,
             dst_fmt_id=vs.YUV420P8)
    fmt_case("GRAY16 full->limited scaled", vs.GRAY16, "Bicubic", 320, 180, 96.0, 1.0, seed=37,
             range_in_s="full", range_s="limited", refargs={"full_in": True, "full_out": False})
    fmt_case("420P8 limited->full 1:1", vs.YUV420P8, "Bicubic", 640, 360, 60.0, 1.0, seed=38,
             range_s="full", refargs={"full_in": False, "full_out": True})
    fmt_case("420P8 ycgco range 1:1", vs.YUV420P8, "Bicubic", 640, 360, 60.0, 1.0, seed=39,
             range_s="full", props={"_Matrix": 8},
             refargs={"full_in": False, "full_out": True, "ycgco": True})

    # Siting: the frame's word, the argument's fallback, and a stated output location --
    # including the chromaloc-only change, where luma must pass through untouched.
    fmt_case("420PS siting prop bottom_left", vs.YUV420PS, "Bicubic", 960, 540, GPU_FLOOR_DB,
             seed=40, props={"_ChromaLocation": 4}, refargs={"src_loc": 4})
    fmt_case("420PS chromaloc_in top_left", vs.YUV420PS, "Bicubic", 960, 540, GPU_FLOOR_DB,
             seed=41, chromaloc_in_s="top_left", refargs={"src_loc": 2})
    fmt_case("420P8 chromaloc-only center", vs.YUV420P8, "Bicubic", 640, 360, 60.0, 1.0,
             seed=42, chromaloc_s="center", refargs={"dst_loc": 1})
    fmt_case("420PS uv kernel spline36", vs.YUV420PS, "Bicubic", 960, 540, GPU_FLOOR_DB,
             seed=43, resample_filter_uv="spline36", refargs={"kernel_uv_key": "Spline36"})

    # --- Step 4 scope: field parity. A _Field frame is one field stored whole and only
    # moves a quarter line; a _FieldBased frame is two interleaved fields resampled
    # separately at half height.
    for field, tag in ((1, "top"), (0, "bottom")):
        planes = noise_planes(core.get_video_format(vs.GRAYS), 640, 180, seed=50 + field)
        run_case(f"GRAYS field {tag} up", planes, vs.GRAYS, "Bicubic", 960, 270,
                 GPU_FLOOR_DB, props={"_Field": field},
                 refargs={"parity": "top" if field else "bottom"})
    planes = noise_planes(core.get_video_format(vs.YUV420PS), 640, 180, seed=52)
    run_case("420PS field top scale", planes, vs.YUV420PS, "Lanczos", 960, 270,
             GPU_FLOOR_DB, props={"_Field": 1}, refargs={"parity": "top"})

    planes = noise_planes(core.get_video_format(vs.YUV420P8), 640, 360, seed=53)
    run_case("420P8 interlaced up", planes, vs.YUV420P8, "Bicubic", 1280, 720,
             60.0, 1.0, props={"_FieldBased": 2}, refargs={"interlaced": True})
    planes = noise_planes(core.get_video_format(vs.GRAYS), 640, 360, seed=54)
    run_case("GRAYS interlaced odd scale", planes, vs.GRAYS, "Spline36", 1000, 556,
             GPU_FLOOR_DB, props={"_FieldBased": 1}, refargs={"interlaced": True})
    # Output is 8 bit, so the floor is the 8-bit one: the same tie-flip noise measures
    # ~24 dB lower against the smaller peak.
    planes = noise_planes(core.get_video_format(vs.GRAY16), 640, 360, seed=55)
    run_case("GRAY16 interlaced down+depth", planes, vs.GRAY16, "Bicubic", 320, 180,
             60.0, 1.0, dst_fmt_id=vs.GRAY8, refargs={"interlaced": True},
             props={"_FieldBased": 2})

    # Bob runs through its own create path (SeparateFields underneath), so it is pinned
    # against the scalar Bob rather than the reference: both fields of the first frame,
    # properties included.
    def bob_case(label, fmt_id, floor, maxd_limit=None, seed=56, **kw):
        fmt = core.get_video_format(fmt_id)
        planes = noise_planes(fmt, 640, 360, seed=seed)
        planes = [np.ascontiguousarray(p, np_dtype(fmt)).astype(np.float64) for p in planes]
        clip = make_clip(planes, fmt_id, length=2).std.SetFrameProps(_FieldBased=2)
        with LogCatcher() as log:
            gpu = core.std.GPUDownload(core.resize.Bob(core.std.GPUUpload(clip), tff=1, **kw))
            gouts = []
            gprops = []
            for n in (0, 1):
                with gpu.get_frame(n) as f:
                    gouts.append([np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)])
                    gprops.append(dict(f.props))
            declined = log.declines()
        if declined:
            results.append((label, float("nan"), None, False))
            failures.append(label + "  [DECLINED: " + declined[0] + "]")
            return
        cpu = core.resize.Bob(clip, tff=1, **kw)
        worst = float("inf")
        maxd = 0.0
        ok = True
        for n in (0, 1):
            with cpu.get_frame(n) as f:
                want = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
                if dict(f.props) != gprops[n]:
                    ok = False
                    failures.append(label + f"  [PROPS n={n}: gpu {gprops[n]} != scalar {dict(f.props)}]")
            worst = min(worst, *[ref.accuracy_db(want[p], gouts[n][p], peak_of(fmt))
                                 for p in range(len(want))])
            maxd = max(maxd, *[ref.max_diff(want[p], gouts[n][p]) for p in range(len(want))])
        ok = ok and worst >= floor and (maxd_limit is None or maxd <= maxd_limit)
        results.append((label, worst, maxd, ok))
        if not ok and (not failures or not failures[-1].startswith(label)):
            failures.append(label)

    bob_case("Bob GRAYS vs scalar", vs.GRAYS, 110.0)
    bob_case("Bob 420P8 vs scalar", vs.YUV420P8, 60.0, 2.0, seed=57)
    # Bob composed with a colour conversion in the same call: the out-chain carries the
    # progressive grid while the colour pass runs on the field's own samples.
    bob_case("Bob + colour to RGBS", vs.YUV420P8, 100.0, seed=59,
             format=vs.RGBS, matrix_in_s="709")

    # --- Step 5 scope: colour conversions. These pin against the SCALAR path rather
    # than the float64 reference: the resample stages are already pinned against the
    # reference above, and duplicating every curve and matrix in Python would test the
    # transcription twice. approximate_gamma=0 on both sides, or the scalar side runs
    # polynomial approximations and the comparison measures those instead.
    def colour_case(label, src_fmt_id, floor, maxd_limit=None, w=640, h=360, seed=70,
                    dst_w=None, dst_h=None, props=None, **kw):
        fmt = core.get_video_format(src_fmt_id)
        planes = noise_planes(fmt, w, h, seed=seed)
        planes = [np.ascontiguousarray(p, np_dtype(fmt)).astype(np.float64) for p in planes]
        clip = make_clip(planes, src_fmt_id)
        if props:
            clip = clip.std.SetFrameProps(**props)
        kw.setdefault("approximate_gamma", 0)
        dw = dst_w if dst_w is not None else w
        dh = dst_h if dst_h is not None else h
        with LogCatcher() as log:
            gclip = core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(clip), width=dw, height=dh, **kw))
            with gclip.get_frame(0) as f:
                out = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
                gpu_props = dict(f.props)
                out_fmt = core.get_video_format(f.format.id)
            declined = log.declines()
        if declined:
            results.append((label, float("nan"), None, False))
            failures.append(label + "  [DECLINED: " + declined[0] + "]")
            return
        with core.resize.Bicubic(clip, width=dw, height=dh, **kw).get_frame(0) as f:
            want = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
            scalar_props = dict(f.props)
        peak = peak_of(out_fmt)
        worst = min(ref.accuracy_db(want[p], out[p], peak) for p in range(len(want)))
        maxd = max(ref.max_diff(want[p], out[p]) for p in range(len(want)))
        ok = worst >= floor and (maxd_limit is None or maxd <= maxd_limit)
        if gpu_props != scalar_props:
            ok = False
            failures.append(label + f"  [PROPS: gpu {gpu_props} != scalar {scalar_props}]")
        results.append((label, worst, maxd, ok))
        if not ok and (not failures or not failures[-1].startswith(label)):
            failures.append(label)

    colour_case("420P8->RGBS matrix prop", vs.YUV420P8, 100.0, props={"_Matrix": 1}, format=vs.RGBS)
    colour_case("420P8->RGB24 matrix_in", vs.YUV420P8, 40.0, 2.0, seed=71,
                matrix_in_s="709", format=vs.RGB24)
    colour_case("RGBS->420P8 matrix 709", vs.RGBS, 40.0, 2.0, seed=72,
                matrix_s="709", format=vs.YUV420P8)
    colour_case("RGB24->444P16 2020ncl full", vs.RGB24, 60.0, 64.0, seed=73,
                matrix_s="2020ncl", range_s="full", format=vs.YUV444P16)
    colour_case("yuv-to-yuv 601->709", vs.YUV420P8, 40.0, 2.0, seed=74,
                props={"_Matrix": 5}, matrix_s="709")
    colour_case("RGBS transfer 709->linear", vs.RGBS, 100.0, seed=75,
                transfer_in_s="709", transfer_s="linear")
    colour_case("RGBS transfer linear->srgb down", vs.RGBS, 100.0, seed=76, dst_w=320, dst_h=180,
                transfer_in_s="linear", transfer_s="srgb")
    colour_case("RGBS gamut 709->2020", vs.RGBS, 90.0, seed=77,
                transfer_in_s="709", primaries_in_s="709", primaries_s="2020")
    colour_case("RGBS st2084 peak 1000", vs.RGBS, 60.0, seed=78,
                transfer_in_s="709", transfer_s="st2084", nominal_luminance=1000)
    colour_case("RGBS->420PS 2020cl", vs.RGBS, 60.0, seed=79,
                matrix_s="2020cl", transfer_in_s="709", format=vs.YUV420PS)
    colour_case("RGBS->420PS chromancl", vs.RGBS, 60.0, seed=80,
                matrix_s="chromancl", primaries_in_s="2020", format=vs.YUV420PS)
    colour_case("GRAY8->RGBS fills", vs.GRAY8, 90.0, seed=81,
                matrix_in_s="709", format=vs.RGBS)
    colour_case("RGBS->GRAY8 drops", vs.RGBS, 40.0, 2.0, seed=82,
                matrix_s="709", format=vs.GRAY8)
    colour_case("420P8->RGBS windowed down", vs.YUV420P8, 90.0, seed=83, dst_w=320, dst_h=180,
                props={"_Matrix": 1}, format=vs.RGBS, src_left=1.5, src_width=637.0)
    colour_case("444PS interlaced ->RGBS", vs.YUV444PS, 90.0, seed=84,
                props={"_Matrix": 1, "_FieldBased": 2}, format=vs.RGBS)

    # --- The parity round: everything the first colour pass declined.
    colour_case("yuv transfer no matrix", vs.YUV420P8, 40.0, 2.0, seed=90,
                props={"_Matrix": 1, "_Transfer": 1}, transfer_s="linear")
    colour_case("yuv gamut no matrix", vs.YUV420PS, 90.0, seed=91,
                props={"_Matrix": 1, "_Transfer": 1, "_Primaries": 1}, primaries_s="2020")
    colour_case("frame-tagged 2020cl in", vs.YUV420PS, 60.0, seed=92,
                props={"_Matrix": 10, "_Transfer": 14}, format=vs.RGBS)
    colour_case("frame-tagged chromancl in", vs.YUV444PS, 60.0, seed=93,
                props={"_Matrix": 12, "_Primaries": 9}, format=vs.RGBS)
    colour_case("CL to ncl yuv", vs.YUV420PS, 60.0, seed=94,
                props={"_Transfer": 14}, matrix_in_s="2020cl", matrix_s="709")
    colour_case("CL to CL chromacl", vs.YUV420PS, 55.0, seed=95,
                props={"_Transfer": 14, "_Primaries": 9}, matrix_in_s="2020cl", matrix_s="chromacl")
    colour_case("ictcp pq to RGBS", vs.YUV444PS, 60.0, seed=96,
                props={"_Matrix": 14, "_Transfer": 16, "_Primaries": 9}, format=vs.RGBS)
    colour_case("RGBS to ictcp pq", vs.RGBS, 60.0, seed=97,
                matrix_s="ictcp", transfer_in_s="linear", transfer_s="st2084",
                primaries_in_s="2020", format=vs.YUV444PS)
    colour_case("ictcp hlg to RGBS", vs.YUV444PS, 45.0, seed=98,
                props={"_Matrix": 14, "_Transfer": 18, "_Primaries": 9}, format=vs.RGBS)
    colour_case("xvycc decode", vs.YUV444PS, 90.0, seed=99,
                props={"_Matrix": 1, "_Transfer": 11}, transfer_s="linear")
    colour_case("b67 luminance ootf decode", vs.RGBS, 60.0, seed=100,
                transfer_in_s="std-b67", transfer_s="linear", primaries_in_s="2020",
                primaries_s="2020")
    colour_case("b67 per-channel (no primaries)", vs.RGBS, 60.0, seed=101,
                transfer_in_s="std-b67", transfer_s="linear")
    colour_case("windowed upscale conversion", vs.YUV420P8, 90.0, seed=102,
                props={"_Matrix": 1}, format=vs.RGBS, dst_w=1280, dst_h=720,
                src_left=1.5, src_top=0.75, src_width=636.0, src_height=357.0)

    # Dither: ordered is deterministic, so it is pinned per pixel against the scalar
    # path's identical table; random uses different rotations of the same blue noise, so
    # it is pinned statistically against the UNDITHERED reference -- zero-mean noise of
    # about half an LSB on top of an otherwise matching resample.
    def dither_case(label, dither, src_fmt_id, dst_fmt_id, floor, maxd_limit, seed, **kw):
        fmt = core.get_video_format(src_fmt_id)
        planes = noise_planes(fmt, 640, 360, seed=seed)
        planes = [np.ascontiguousarray(p, np_dtype(fmt)).astype(np.float64) for p in planes]
        clip = make_clip(planes, src_fmt_id)
        with LogCatcher() as log:
            g = core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(clip),
                format=dst_fmt_id, dither_type=dither, **kw))
            with g.get_frame(0) as f:
                out = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
            declined = log.declines()
        if declined:
            results.append((label, float("nan"), None, False))
            failures.append(label + "  [DECLINED: " + declined[0] + "]")
            return
        with core.resize.Bicubic(clip, format=dst_fmt_id, dither_type=dither, **kw).get_frame(0) as f:
            want = [np.asarray(f[p]).astype(np.float64) for p in range(f.format.num_planes)]
        peak = peak_of(core.get_video_format(dst_fmt_id))
        worst = min(ref.accuracy_db(want[p], out[p], peak) for p in range(len(want)))
        maxd = max(ref.max_diff(want[p], out[p]) for p in range(len(want)))
        # The mean must stay unbiased either way.
        bias = max(abs(float(np.mean(out[p]) - np.mean(want[p]))) for p in range(len(want)))
        ok = worst >= floor and maxd <= maxd_limit and bias < 0.05
        results.append((label, worst, maxd, ok))
        if not ok:
            failures.append(label + f"  [bias {bias:.4f}]")

    dither_case("ordered dither 16->8", "ordered", vs.GRAY16, vs.GRAY8, 40.0, 1.0, 110)
    dither_case("ordered dither float->8 420", "ordered", vs.YUV420PS, vs.YUV420P8, 40.0, 1.0, 111)
    dither_case("random dither 16->8", "random", vs.GRAY16, vs.GRAY8, 40.0, 2.0, 112)
    # With a resample in play zimg dithers a separate depth node fed by its integer
    # resize -- a quantised, clipped intermediate the store-site model deliberately does
    # not have -- so the scaled case carries the same few-LSB allowance as the integer
    # resample rows in refcheck.
    dither_case("ordered dither scaled", "ordered", vs.GRAY16, vs.GRAY8, 40.0, 8.0, 114,
                width=320, height=180)

    # Variable format stays resident: the spec is resolved per frame from the frame's
    # own format and cached with its pipelines, like the scalar path's graph cache.
    pa = noise_planes(core.get_video_format(vs.GRAY8), 640, 360, seed=120)
    pa = [np.ascontiguousarray(p, np.uint8).astype(np.float64) for p in pa]
    pb = noise_planes(core.get_video_format(vs.GRAYS), 320, 180, seed=121)
    pb = [np.ascontiguousarray(p, np.float32).astype(np.float64) for p in pb]
    var = core.std.Splice([make_clip(pa, vs.GRAY8), make_clip(pb, vs.GRAYS)], mismatch=True)
    with LogCatcher() as log:
        gv = core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(var), width=480, height=270))
        gout = []
        for nf in (0, 1):
            with gv.get_frame(nf) as f:
                gout.append((np.asarray(f[0]).astype(np.float64), f.format.id))
        vdecl = log.declines()
    sv = core.resize.Bicubic(var, width=480, height=270)
    vok = not vdecl
    vworst = float("inf")
    for nf in (0, 1):
        with sv.get_frame(nf) as f:
            want = np.asarray(f[0]).astype(np.float64)
            vok = vok and f.format.id == gout[nf][1]
            vworst = min(vworst, ref.accuracy_db(want, gout[nf][0], peak_of(f.format)))
    # The 8-bit frame is measured against the scalar path's integer chain, whose
    # quantised, clipped intermediate sits tens of dB from the float model by design.
    vok = vok and vworst >= 55.0
    results.append(("variable format per-frame specs", vworst, None, vok))
    if not vok:
        failures.append("variable format per-frame specs  [declines=%r]" % vdecl)

    # The white patch pin from the previous round: a 709 -> 470M primaries conversion
    # WITHOUT white point adaptation returns off-white, and matching the scalar path's
    # no-adaptation behaviour beats being independently right. If adaptation ever turns
    # on in zimg, both paths must move together or this catches it.
    white = [np.full((64, 64), 1.0), np.full((64, 64), 1.0), np.full((64, 64), 1.0)]
    wclip = make_clip(white, vs.RGBS)
    wargs = dict(transfer_in_s="709", primaries_in_s="709", primaries_s="470m", approximate_gamma=0)
    with LogCatcher() as log:
        g = core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(wclip), **wargs))
        with g.get_frame(0) as f:
            wr = [float(np.asarray(f[p])[0, 0]) for p in range(3)]
        wd = log.declines()
    with core.resize.Bicubic(wclip, **wargs).get_frame(0) as f:
        ws = [float(np.asarray(f[p])[0, 0]) for p in range(3)]
    # Both halves matter: the GPU must match the scalar, and both must return OFF-white --
    # a white point adaptation quietly turning on in either would return ~(1,1,1).
    ok = not wd and all(abs(wr[i] - ws[i]) < 1e-3 for i in range(3)) \
        and max(abs(v - 1.0) for v in wr) > 0.01
    results.append(("white patch no adaptation", float("inf") if ok else float("nan"), None, ok))
    if not ok:
        failures.append("white patch no adaptation  [gpu %r scalar %r declined=%r]" % (wr, ws, wd))

    # An interleaved frame whose height cannot split into whole fields per plane is a
    # per-frame error, not a wrong picture.
    planes = noise_planes(core.get_video_format(vs.YUV420P8), 640, 358, seed=59)
    planes = [np.ascontiguousarray(p, np.uint8).astype(np.float64) for p in planes]
    clip = make_clip(planes, vs.YUV420P8).std.SetFrameProps(_FieldBased=2)
    try:
        core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(clip), width=1280, height=716)).get_frame(0)
        results.append(("interlaced odd height refuses", float("nan"), None, False))
        failures.append("interlaced odd height refuses  [no error raised]")
    except vs.Error as e:
        ok = "divide" in str(e)
        results.append(("interlaced odd height refuses", float("inf"), None, ok))
        if not ok:
            failures.append("interlaced odd height refuses  [wrong message: %s]" % e)

    width = max(len(r[0]) for r in results)
    for label, db, maxd, ok in results:
        extra = "" if maxd is None else f"   maxdiff {maxd:.3g}"
        print(f"  {label:<{width}}  {db:8.2f} dB{extra}{'' if ok else '   FAIL'}")
    print()
    if failures:
        print(f"{len(failures)} of {len(results)} checks FAILED:")
        for f in failures:
            print("  " + f)
        return 1
    print(f"all {len(results)} checks passed (floor {GPU_FLOOR_DB} dB)")
    return 0


# Pinned from measurement on the 6900 XT: every float32 case lands between 136 and 158 dB
# against the float64 reference once the geometry rides as hi+lo pairs, with no dependence
# on ratio or dimension. A result below this is a real change in the arithmetic, not noise.
GPU_FLOOR_DB = 130.0


# ---------------------------------------------------------------------------------------
# audit: everything outside the compute path's scope must decline CLEANLY -- one log
# line, then a correct scalar fallback -- rather than erroring or silently mishandling.

def run_audit():
    failures = []

    def declines(label, clip, expect_reason, scalar_may_error=False, **kw):
        """The GPU hook must hand the call back with the expected reason; the scalar
        fallback then either answers it or -- for calls that are invalid everywhere --
        raises its own error, which is the correct behaviour too."""
        with LogCatcher() as log:
            try:
                out = core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(clip), **kw))
                out.get_frame(0)
                err = None
            except vs.Error as e:
                err = str(e)
            d = log.declines()
        ok = len(d) == 1 and expect_reason in d[0] and (scalar_may_error or err is None)
        print(f"  {label:<48}  {'ok' if ok else 'FAIL  err=%r declines=%r' % (err, d)}")
        if not ok:
            failures.append(label)

    def errors_both(label, clip, **kw):
        """Cases the scalar path itself refuses: the GPU hook must not swallow them into
        a wrong picture; the error surfaces at create or at the first frame."""
        try:
            core.resize.Bicubic(core.std.GPUUpload(clip), **kw).get_frame(0)
            ok = False
            detail = "no error raised"
        except vs.Error as e:
            ok = True
            detail = str(e)
        print(f"  {label:<48}  {'ok' if ok else 'FAIL  ' + detail}")
        if not ok:
            failures.append(label)

    g8 = core.std.BlankClip(format=vs.GRAY8, width=640, height=360, length=1)
    gs = core.std.BlankClip(format=vs.GRAYS, width=640, height=360, length=1)
    y8 = core.std.BlankClip(format=vs.YUV420P8, width=640, height=360, length=1)
    rgbs = core.std.BlankClip(format=vs.RGBS, width=640, height=360, length=1)

    declines("error diffusion dither", g8, "error diffusion", width=320, height=180,
             dither_type="error_diffusion")
    declines("cpu_type", gs, "cpu_type", width=320, height=180, cpu_type="avx2")
    # The _in-alone spellings fall to the plain path, whose whole-argument decline is the
    # same answer the previous implementation gave.
    declines("transfer_in alone", rgbs, "colorspace conversion", transfer_in_s="709")
    declines("primaries_in alone", rgbs, "colorspace conversion", primaries_in_s="709")
    declines("gray transfer no RGB", gs, "pass through RGB", scalar_may_error=True,
             transfer_in_s="709", transfer_s="linear")
    declines("32 bit integer", core.std.BlankClip(format=core.query_video_format(
        vs.GRAY, vs.INTEGER, 32).id, width=64, height=64, length=1), "wider than 16 bit",
        scalar_may_error=True)

    # chromatic_adaptation is accepted and ignored, matching the scalar path's real
    # behaviour at zimg API 2.4; the white patch pin guards both against it turning on.
    with LogCatcher() as log:
        core.std.GPUDownload(core.resize.Bicubic(core.std.GPUUpload(rgbs),
            transfer_in_s="709", primaries_in_s="709", primaries_s="2020",
            chromatic_adaptation=1)).get_frame(0)
        d = log.declines()
    ok = not d
    print(f"  {'chromatic adaptation accepted, ignored':<48}  {'ok' if ok else 'FAIL  ' + repr(d)}")
    if not ok:
        failures.append("chromatic adaptation accepted")

    # ICtCp without a usable transfer errors on both paths rather than declining.
    errors_both("ictcp without transfer errors", y8, format=vs.RGBS, matrix_in_s="ictcp")


    # Script errors stay script errors through the decline.
    errors_both("odd size for 420", y8, width=321, height=180, format=vs.YUV420P8)
    errors_both("matrix missing to YUV", rgbs, format=vs.YUV420P8)

    print()
    if failures:
        print(f"{len(failures)} audit checks FAILED")
        return 1
    print("all audit checks passed")
    return 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "refcheck"
    if mode == "refcheck":
        return run_refcheck()
    if mode == "gpu":
        return run_gpu()
    if mode == "audit":
        return run_audit()
    if mode == "all":
        return run_refcheck() | run_gpu() | run_audit()
    print("unknown mode: " + mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
