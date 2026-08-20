"""Independent double-precision reference for the GPU resize path.

Everything here is transcribed from zimg (resize/filter.cpp and graph/graphbuilder.cpp)
rather than from the C++ implementation under test, so the two can only agree by both
matching zimg's definitions. Weights are computed in float64 with no lookup table, which
is what makes this a reference rather than a second copy of the shader: the GPU path's
LUT interpolation and float32 position arithmetic show up as a measured distance from
this, not as agreement with it.

The rounding model matches the compute path's declared behaviour, not zimg's scalar
integer kernels: resample in float, one affine at the store, round once. Where an axis
resamples the rounding is half up (zimg's pack_pixel_u16); a pure format conversion
rounds half to even (zimg's lrint). zimg's own integer paths run 1.14 fixed point and so
differ from this by design; the validation entry points quantify that gap instead of
hiding it.
"""

import numpy as np
from math import ceil, floor, isnan, nan

# ---------------------------------------------------------------------------------------
# Kernels, from zimg resize/filter.cpp. support is in units of source pixels at scale 1.


def _poly3(x, c0, c1, c2, c3):
    return c0 + x * (c1 + x * (c2 + x * c3))


class Point:
    support = 0

    def __call__(self, x):
        return 1.0


class Bilinear:
    support = 1

    def __call__(self, x):
        return max(1.0 - abs(x), 0.0)


class Bicubic:
    support = 2
    DEFAULT_B = 0.0
    DEFAULT_C = 0.5

    def __init__(self, b=DEFAULT_B, c=DEFAULT_C):
        self.p0 = (6.0 - 2.0 * b) / 6.0
        self.p2 = (-18.0 + 12.0 * b + 6.0 * c) / 6.0
        self.p3 = (12.0 - 9.0 * b - 6.0 * c) / 6.0
        self.q0 = (8.0 * b + 24.0 * c) / 6.0
        self.q1 = (-12.0 * b - 48.0 * c) / 6.0
        self.q2 = (6.0 * b + 30.0 * c) / 6.0
        self.q3 = (-b - 6.0 * c) / 6.0

    def __call__(self, x):
        x = abs(x)
        if x < 1.0:
            return _poly3(x, self.p0, 0.0, self.p2, self.p3)
        if x < 2.0:
            return _poly3(x, self.q0, self.q1, self.q2, self.q3)
        return 0.0


class Spline16:
    support = 2

    def __call__(self, x):
        x = abs(x)
        if x < 1.0:
            return _poly3(x, 1.0, -1.0 / 5.0, -9.0 / 5.0, 1.0)
        if x < 2.0:
            return _poly3(x - 1.0, 0.0, -7.0 / 15.0, 4.0 / 5.0, -1.0 / 3.0)
        return 0.0


class Spline36:
    support = 3

    def __call__(self, x):
        x = abs(x)
        if x < 1.0:
            return _poly3(x, 1.0, -3.0 / 209.0, -453.0 / 209.0, 13.0 / 11.0)
        if x < 2.0:
            return _poly3(x - 1.0, 0.0, -156.0 / 209.0, 270.0 / 209.0, -6.0 / 11.0)
        if x < 3.0:
            return _poly3(x - 2.0, 0.0, 26.0 / 209.0, -45.0 / 209.0, 1.0 / 11.0)
        return 0.0


class Spline64:
    support = 4

    def __call__(self, x):
        x = abs(x)
        if x < 1.0:
            return _poly3(x, 1.0, -3.0 / 2911.0, -6387.0 / 2911.0, 49.0 / 41.0)
        if x < 2.0:
            return _poly3(x - 1.0, 0.0, -2328.0 / 2911.0, 4032.0 / 2911.0, -24.0 / 41.0)
        if x < 3.0:
            return _poly3(x - 2.0, 0.0, 582.0 / 2911.0, -1008.0 / 2911.0, 6.0 / 41.0)
        if x < 4.0:
            return _poly3(x - 3.0, 0.0, -97.0 / 2911.0, 168.0 / 2911.0, -1.0 / 41.0)
        return 0.0


class Lanczos:
    DEFAULT_TAPS = 3

    def __init__(self, taps=DEFAULT_TAPS):
        self.support = int(taps)

    @staticmethod
    def _sinc(x):
        return 1.0 if x == 0.0 else np.sin(x * np.pi) / (x * np.pi)

    def __call__(self, x):
        x = abs(x)
        return self._sinc(x) * self._sinc(x / self.support) if x < self.support else 0.0


def make_kernel(name, a=nan, b=nan):
    """The NaN-is-unset rule the filter applies: omitting a parameter means the kernel's
    own default, exactly as translate_resize_filter does."""
    name = name.lower()
    if name == "point":
        return Point()
    if name == "bilinear":
        return Bilinear()
    if name == "bicubic":
        return Bicubic(Bicubic.DEFAULT_B if isnan(a) else a,
                       Bicubic.DEFAULT_C if isnan(b) else b)
    if name == "spline16":
        return Spline16()
    if name == "spline36":
        return Spline36()
    if name == "spline64":
        return Spline64()
    if name == "lanczos":
        return Lanczos(Lanczos.DEFAULT_TAPS if isnan(a) else max(int(a), 1))
    raise ValueError("unknown kernel: " + name)


# ---------------------------------------------------------------------------------------
# The resampling matrix, from compute_filter. Dense rather than sparse because reference
# clarity beats reference speed.


def round_halfup(x):
    """zimg's grid rounding: floor(x + 0.5) with positive ties nudged down one ulp so
    round(x - 1) == round(x) - 1 holds across zero."""
    return floor(x + 0.5) if x < 0 else floor(x + 0.49999999999999994)


def resample_matrix(kernel, src_dim, dst_dim, shift=0.0, width=None):
    """(dst_dim, src_dim) float64 matrix: out = m @ src along the resampled axis.

    Mirrors compute_filter exactly: kernel stretched by 1/scale on downscale, positions
    on the half-integer grid, out-of-bounds taps mirrored back into the image and the
    whole row normalised by the unmirrored tap total."""
    if width is None:
        width = float(src_dim)
    scale = dst_dim / width
    step = min(scale, 1.0)
    support = kernel.support / step
    filter_size = max(int(ceil(support)) * 2, 1)

    m = np.zeros((dst_dim, src_dim), dtype=np.float64)
    edge = np.nextafter(float(src_dim), -np.inf)
    for i in range(dst_dim):
        pos = (i + 0.5) / scale + shift
        begin_pos = round_halfup(pos - filter_size / 2.0) + 0.5
        total = 0.0
        for j in range(filter_size):
            total += kernel((begin_pos + j - pos) * step)
        for j in range(filter_size):
            xpos = begin_pos + j
            if xpos < 0.0:
                real_pos = -xpos
            elif xpos >= src_dim:
                real_pos = 2.0 * src_dim - xpos
            else:
                real_pos = xpos
            real_pos = min(max(real_pos, 0.0), edge)
            m[i][int(floor(real_pos))] += kernel((xpos - pos) * step) / total
    return m


def is_identity_window(src_dim, dst_dim, shift, width):
    """The case zimg skips an axis in: running a real kernel through it anyway is a blur,
    not a copy."""
    return src_dim == dst_dim and shift == 0.0 and width == float(src_dim)


# ---------------------------------------------------------------------------------------
# Per-plane geometry, from graphbuilder.cpp. Offsets are in units of the plane's own
# pixels; subscale is the plane's size as a fraction of luma (1/2 for 4:2:0 chroma).

CHROMA_LOC_NAMES = ("left", "center", "top_left", "top", "bottom_left", "bottom")

# _ChromaLocation value -> (horizontal, vertical) siting.
_LOC_SPLIT = {
    0: ("left", "middle"), 1: ("center", "middle"), 2: ("left", "top"),
    3: ("center", "top"), 4: ("left", "bottom"), 5: ("center", "bottom"),
}


def split_chroma_location(loc):
    return _LOC_SPLIT[int(loc)]


def chroma_offset_w(siting, subscale):
    return -0.5 + 0.5 * subscale if siting == "left" else 0.0


def chroma_offset_h(siting, subscale):
    if siting == "top":
        return -0.5 + 0.5 * subscale
    if siting == "bottom":
        return 0.5 - 0.5 * subscale
    return 0.0


def luma_parity_offset(parity):
    """A field's rows sit a quarter line off the progressive grid."""
    if parity == "top":
        return -0.25
    if parity == "bottom":
        return 0.25
    return 0.0


def chroma_parity_offset(parity, siting_offset):
    """Chroma inherits the field offset and halves its own siting offset on top."""
    if parity == "top":
        return -0.25 + siting_offset / 2
    if parity == "bottom":
        return 0.25 + siting_offset / 2
    return siting_offset


def axis_window(sub_in, sub_out, off_in, off_out, active_start, active_extent,
                dst_plane_dim, luma_dst_dim):
    """What compute_filter takes for one plane on one axis: the luma-space active region
    scaled into the plane's own units, with each side's siting offset applied to its own
    end. Luma is the same expression at subscale 1 with zero offsets."""
    src_start = active_start * sub_in - off_in
    src_extent = active_extent * sub_in
    dst_start = -off_out
    dst_extent = luma_dst_dim * sub_out
    scale = dst_extent / src_extent
    shift = src_start - dst_start / scale
    width = src_extent * (dst_plane_dim / dst_extent)
    return shift, width


# ---------------------------------------------------------------------------------------
# Sample numbering, from depth/quantize.h. Float is canonical: range 1, offset 0.


def integer_range(bits, is_float, chroma, fullrange, ycgco=False):
    if is_float:
        return 1.0
    if fullrange:
        return float((1 << bits) - 1)
    return float((224 if (chroma and not ycgco) else 219) << (bits - 8))


def integer_offset(bits, is_float, chroma, fullrange):
    if is_float:
        return 0.0
    if chroma:
        return float(1 << (bits - 1))
    return 0.0 if fullrange else float(16 << (bits - 8))


def depth_transform(src, dst, chroma, full_in, full_out, ycgco=False):
    """(scale, offset) carrying a sample from src numbering to dst numbering, composed
    the way get_scale_offset composes it. src/dst are (bits, is_float) pairs."""
    range_in = integer_range(src[0], src[1], chroma, full_in, ycgco)
    range_out = integer_range(dst[0], dst[1], chroma, full_out, ycgco)
    offset_in = integer_offset(src[0], src[1], chroma, full_in)
    offset_out = integer_offset(dst[0], dst[1], chroma, full_out)
    scale = range_out / range_in
    return scale, -offset_in * scale + offset_out


def quantize(arr, bits, is_float, round_even=False):
    """One rounding at the store. Half up when an axis resampled (pack_pixel_u16), half
    to even for a pure format conversion (lrint); saturation is not decoration, a
    windowed kernel really does overshoot."""
    if is_float:
        return arr
    peak = float((1 << bits) - 1)
    r = np.rint(arr) if round_even else np.floor(arr + 0.5)
    return np.clip(r, 0.0, peak)


# ---------------------------------------------------------------------------------------
# Whole-clip driver for the scope the plain resize covers: gray/YUV/RGB in and out of the
# same family, any subsampling, any depth, windows, siting, field parity. Colour
# conversions extend this in their own step.


class Fmt:
    """The slice of VSVideoFormat this needs, stated explicitly so the reference has no
    VapourSynth dependency."""

    def __init__(self, family, bits, is_float, sub_w=0, sub_h=0):
        self.family = family        # 'gray' | 'yuv' | 'rgb'
        self.bits = bits
        self.is_float = is_float
        self.sub_w = sub_w
        self.sub_h = sub_h

    @property
    def num_planes(self):
        return 1 if self.family == "gray" else 3

    def subsampled(self):
        return bool(self.sub_w or self.sub_h)


def _plane_axis_pass(plane, kernel, dst_dim, shift, width, vertical):
    src_dim = plane.shape[0] if vertical else plane.shape[1]
    if is_identity_window(src_dim, dst_dim, shift, width):
        return plane, False
    m = resample_matrix(kernel, src_dim, dst_dim, shift, width)
    return (m @ plane) if vertical else (plane @ m.T), True


def resize_clip(planes, src_fmt, dst_fmt, dst_w, dst_h, kernel, kernel_uv=None,
                src_left=0.0, src_top=0.0, src_width=None, src_height=None,
                src_loc=0, dst_loc=None, full_in=None, full_out=None, ycgco=False,
                parity="progressive", interlaced=False, dst_parity=None):
    """The reference for one resize node. planes are float64 arrays in SOURCE sample
    numbering; the result is quantized to dst_fmt. Same-family (or gray<->yuv) only.

    src_loc/dst_loc are resolved _ChromaLocation values; dst_loc None carries the
    source's when both sides are subsampled YUV and takes the format default otherwise.
    parity is the SOURCE frame's ('progressive', 'top', 'bottom'); dst_parity overrides
    the output grid (a bob writes 'progressive' from a field source). interlaced=True
    means the frame holds two interleaved fields, resampled separately at half height.
    """
    if src_width is None:
        src_width = float(planes[0].shape[1])
    if src_height is None:
        src_height = float(planes[0].shape[0])
    kernel_uv = kernel_uv or kernel
    if full_in is None:
        full_in = src_fmt.family == "rgb"
    if full_out is None:
        full_out = full_in
    if dst_parity is None:
        dst_parity = parity

    src_w = planes[0].shape[1]
    src_h = planes[0].shape[0]

    src_sw, src_sh = split_chroma_location(src_loc)
    if dst_loc is not None:
        dst_sw, dst_sh = split_chroma_location(dst_loc)
    elif dst_fmt.family == "yuv" and dst_fmt.subsampled() and \
            (src_fmt.subsampled() and src_fmt.family == "yuv"):
        dst_sw, dst_sh = src_sw, src_sh
    else:
        dst_sw, dst_sh = ("left", "middle") if dst_fmt.subsampled() else ("center", "middle")

    if interlaced:
        top = resize_clip([p[0::2] for p in planes], src_fmt, dst_fmt, dst_w, dst_h // 2,
                          kernel, kernel_uv, src_left, src_top / 2,
                          src_width, src_height / 2, src_loc, dst_loc,
                          full_in, full_out, ycgco, parity="top")
        bottom = resize_clip([p[1::2] for p in planes], src_fmt, dst_fmt, dst_w, dst_h // 2,
                             kernel, kernel_uv, src_left, src_top / 2,
                             src_width, src_height / 2, src_loc, dst_loc,
                             full_in, full_out, ycgco, parity="bottom")
        out = []
        for t, b in zip(top, bottom):
            o = np.empty((t.shape[0] * 2, t.shape[1]), dtype=np.float64)
            o[0::2] = t
            o[1::2] = b
            out.append(o)
        return out

    out_planes = []
    plane_resampled = []
    for p in range(dst_fmt.num_planes):
        chroma = dst_fmt.family == "yuv" and p != 0
        cls = 0 if p == 0 else 1
        k = kernel if cls == 0 else kernel_uv

        if src_fmt.family == "gray" and chroma:
            # Chroma that does not exist yet: neutral at the destination's numbering.
            pw = dst_w >> dst_fmt.sub_w
            ph = dst_h >> dst_fmt.sub_h
            out_planes.append(np.full((ph, pw), integer_offset(
                dst_fmt.bits, dst_fmt.is_float, True, full_out), dtype=np.float64))
            plane_resampled.append(False)
            continue

        plane = planes[p].astype(np.float64)
        sub_in_w = 1.0 if cls == 0 else 1.0 / (1 << src_fmt.sub_w)
        sub_out_w = 1.0 if cls == 0 else 1.0 / (1 << dst_fmt.sub_w)
        sub_in_h = 1.0 if cls == 0 else 1.0 / (1 << src_fmt.sub_h)
        sub_out_h = 1.0 if cls == 0 else 1.0 / (1 << dst_fmt.sub_h)
        dst_pw = dst_w if cls == 0 else dst_w >> dst_fmt.sub_w
        dst_ph = dst_h if cls == 0 else dst_h >> dst_fmt.sub_h

        off_in_w = 0.0 if cls == 0 else chroma_offset_w(src_sw, sub_in_w)
        off_out_w = 0.0 if cls == 0 else chroma_offset_w(dst_sw, sub_out_w)
        shift_w, width_w = axis_window(sub_in_w, sub_out_w, off_in_w, off_out_w,
                                       src_left, src_width, dst_pw, dst_w)

        if cls == 0:
            off_in_h = luma_parity_offset(parity)
            off_out_h = luma_parity_offset(dst_parity)
        else:
            off_in_h = chroma_parity_offset(parity, chroma_offset_h(src_sh, sub_in_h))
            off_out_h = chroma_parity_offset(dst_parity, chroma_offset_h(dst_sh, sub_out_h))
        shift_h, width_h = axis_window(sub_in_h, sub_out_h, off_in_h, off_out_h,
                                       src_top, src_height, dst_ph, dst_h)

        plane, ran_h = _plane_axis_pass(plane, k, dst_pw, shift_w, width_w, False)
        plane, ran_v = _plane_axis_pass(plane, k, dst_ph, shift_h, width_h, True)
        plane_resampled.append(ran_h or ran_v)

        scale, offset = depth_transform((src_fmt.bits, src_fmt.is_float),
                                        (dst_fmt.bits, dst_fmt.is_float),
                                        chroma, full_in, full_out, ycgco)
        if scale != 1.0 or offset != 0.0:
            plane = plane * scale + offset
        out_planes.append(plane)

    if src_fmt.family == "yuv" and dst_fmt.family == "gray":
        out_planes = out_planes[:1]
        plane_resampled = plane_resampled[:1]

    # Per plane, not per call: a chromaloc-only change resamples chroma while luma is a
    # pure format conversion, and each plane rounds by what happened to it.
    return [quantize(p, dst_fmt.bits, dst_fmt.is_float, round_even=not r)
            for p, r in zip(out_planes, plane_resampled)]


# ---------------------------------------------------------------------------------------
# Measurement.


def accuracy_db(ref, out, peak):
    """Full-scale accuracy: -20*log10(rms error / peak). Content independent enough to
    pin as a floor, unlike signal-relative SNR which rewards loud test patterns."""
    err = np.asarray(out, dtype=np.float64) - np.asarray(ref, dtype=np.float64)
    rms = np.sqrt(np.mean(err * err))
    if rms == 0.0:
        return float("inf")
    return -20.0 * np.log10(rms / peak)


def max_diff(ref, out):
    return float(np.max(np.abs(np.asarray(out, np.float64) - np.asarray(ref, np.float64))))
