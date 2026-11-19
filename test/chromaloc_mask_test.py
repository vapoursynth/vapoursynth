import ctypes
import math
import random
import struct
import unittest
from fractions import Fraction

import vapoursynth as vs


ALL_LOCATIONS = tuple(vs.ChromaLocation)


def plane_bytes(frame, p):
    # Used for equality comparison
    return bytes(frame[p])


class ChromaLocationMaskTest(unittest.TestCase):
    """Tests for chromaloc-aware mask resampling in
    std.MaskedMerge (first_plane) and std.PreMultiply (alpha).
    """

    def setUp(self):
        self.core = vs.core
        self.core.num_threads = 1

    def create_gray(self, value, w=4, h=4):
        return self.core.std.BlankClip(
            format=vs.GRAY8,
            width=w,
            height=h,
            color=value,
            length=1
        )

    def create_checkerboard(self):
        # 8x8 GRAY8 checkerboard of 4x4 cells making chroma downsample
        # sensitive to both horizontal and vertical chroma siting shift.
        top = self.core.std.StackHorizontal([self.create_gray(20), self.create_gray(235)])
        bottom = self.core.std.StackHorizontal([self.create_gray(235), self.create_gray(20)])
        return self.core.std.StackVertical([top, bottom])

    def create_yuv(self, y, u, v, length=1):
        return self.core.std.BlankClip(
            format=vs.YUV420P8, width=8, height=8, color=(y, u, v), length=length)

    def vary_chromaloc(self, base):
        # Splice base into 6-frame clip whose frames carry _ChromaLocation 0-5
        parts = [base.std.SetFrameProps(_ChromaLocation=i) for i in ALL_LOCATIONS]
        return self.core.std.Splice(parts)

    def repeat(self, clip, n=6):
        return self.core.std.Splice([clip] * n)

    def resize_reference_chroma(self, single_plane, loc, field_based=False):
        # Copy the plane as 4:4:4, then let resize site the chroma to 4:2:0 at
        # `loc`, optionally interlaced. Matches what internal mask resample
        # must reproduce.
        yuv444 = self.core.std.ShufflePlanes(
            (single_plane, single_plane, single_plane),
            (0, 0, 0),
            colorfamily=vs.YUV
        )
        if field_based:
            yuv444 = yuv444.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
        ref = yuv444.resize.Bilinear(format=vs.YUV420P8, chromaloc=loc)
        return plane_bytes(ref.get_frame(0), 1)

    def maskedmerge_reveal_chroma_mask(self, mask, n=0, length=1, field_based=False, chromaloc=None, **kwargs):
        # MaskedMerge(black -> white) returns per-plane resampled mask itself.
        black = self.create_yuv(0, 0, 0, length)
        white = self.create_yuv(255, 255, 255, length)
        if chromaloc is not None:
            black = black.std.SetFrameProps(_ChromaLocation=chromaloc)
            white = white.std.SetFrameProps(_ChromaLocation=chromaloc)
        if field_based:
            # Mask and clips must match in interlacing
            black = black.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
            white = white.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
            mask = mask.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
        out = self.core.std.MaskedMerge(black, white, mask, first_plane=True, **kwargs)
        return plane_bytes(out.get_frame(n), 1)

    def test_maskedmerge_matches_chromaloc_reference(self):
        # Resampled mask must match location-aware downsample of every siting.
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc),
                    self.resize_reference_chroma(mask, loc))

    def test_maskedmerge_center_equals_unshifted_downsample(self):
        # Old chroma-location-unaware behavior was exactly CHROMA_CENTER siting
        # Simulated here as a reference.
        mask = self.create_checkerboard()
        naive = plane_bytes(mask.resize.Bilinear(width=4, height=4).get_frame(0), 0)
        self.assertEqual(
            self.maskedmerge_reveal_chroma_mask(mask, chromaloc=vs.CHROMA_CENTER),
            naive
        )

    def test_maskedmerge_per_frame_dispatch(self):
        # Resampling must employ frame-specific _ChromaLocation compensation.
        # clipa and clipb carry matching per-frame locations so they stay
        # compatible while the dispatch varies frame to frame.
        mask = self.create_checkerboard()
        black = self.vary_chromaloc(self.create_yuv(0, 0, 0))
        white = self.vary_chromaloc(self.create_yuv(255, 255, 255))
        mask6 = self.repeat(mask)
        out = self.core.std.MaskedMerge(black, white, mask6, first_plane=True)
        results = []
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                result = plane_bytes(out.get_frame(loc), 1)
                self.assertEqual(result, self.resize_reference_chroma(mask, loc))
                results.append(result)
        # Each siting should have a distinct luma alignment
        self.assertEqual(len(set(results)), len(ALL_LOCATIONS))

    def test_maskedmerge_non_subsampled_ignores_chromaloc(self):
        # Chroma siting is irrelevant on non-subsampled clips, so clipa/clipb
        # _ChromaLocation mismatch shouldn't error nor change the result.
        a = self.core.std.BlankClip(format=vs.YUV444P8, width=8, height=8, color=(60, 70, 80))
        b = self.core.std.BlankClip(format=vs.YUV444P8, width=8, height=8, color=(200, 150, 90))
        mask = self.core.std.ShufflePlanes(
            (self.create_checkerboard(), self.create_checkerboard(), self.create_checkerboard()),
            (0, 0, 0),
            colorfamily=vs.YUV
        )
        base = self.core.std.MaskedMerge(a, b, mask, first_plane=True)
        tweaked = self.core.std.MaskedMerge(
            a.std.SetFrameProps(_ChromaLocation=vs.CHROMA_BOTTOM),
            b,
            mask,
            first_plane=True,
        )
        for p in range(3):
            with self.subTest(plane=p):
                self.assertEqual(
                    plane_bytes(base.get_frame(0), p),
                    plane_bytes(tweaked.get_frame(0), p)
                )

    def test_maskedmerge_interlaced_matches_chromaloc_reference(self):
        # For interlaced content, the mask resample must match resize's native
        # interlaced chroma siting for each location (just like progressive)
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc, field_based=True),
                    self.resize_reference_chroma(mask, loc, field_based=True)
                )

    def test_maskedmerge_interlaced_differs_from_progressive(self):
        # Interlacing must actually change the resample, not be ignored.
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertNotEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc, field_based=True),
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc)
                )

    def test_maskedmerge_chromaloc_compatibility(self):
        # Subsampled clipa/clipb merge requires matching chroma locations
        mask = self.create_checkerboard()
        off = self.create_yuv(0, 0, 0)
        on = self.create_yuv(255, 255, 255)

        def merged(a_loc, b_loc):
            a = off if a_loc is None else off.std.SetFrameProps(_ChromaLocation=a_loc)
            b = on if b_loc is None else on.std.SetFrameProps(_ChromaLocation=b_loc)
            return self.core.std.MaskedMerge(a, b, mask, first_plane=True)

        # (clipa loc, clipb loc, should_error)
        cases = (
            (vs.CHROMA_LEFT, vs.CHROMA_CENTER, True),   # explicit mismatch
            (None, vs.CHROMA_CENTER, True),             # fallback left vs explicit center
            (None, None, False),                        # both guess left
            (vs.CHROMA_LEFT, None, False),              # explicit left vs fallback left
        )
        for a_loc, b_loc, should_error in cases:
            with self.subTest(a=a_loc, b=b_loc):
                out = merged(a_loc, b_loc)
                if should_error:
                    with self.assertRaises(vs.Error):
                        out.get_frame(0)
                else:
                    out.get_frame(0)  # must not raise

    def premultiply_chroma(self, clip, alpha, chromaloc=None, **kwargs):
        if chromaloc is not None:
            clip = clip.std.SetFrameProps(_ChromaLocation=chromaloc)
        out = self.core.std.PreMultiply(clip, alpha, **kwargs)
        return plane_bytes(out.get_frame(0), 1)

    def premultiply_reference_chroma(self, alpha, loc, field_based=False):
        # With the clip's chroma fixed at 255 and chroma neutral at 128, the
        # premultiply of the resampled alpha (ra) reduces to
        # out = (127 * ra + 127) // 255 + 128
        # Resampled alpha comes from the independent resize reference.
        ra = self.resize_reference_chroma(alpha, loc, field_based=field_based)
        return bytes((127 * v + 127) // 255 + 128 for v in ra)

    def test_premultiply_matches_reference(self):
        alpha = self.create_checkerboard()
        clip = self.create_yuv(128, 255, 255)
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.premultiply_chroma(clip, alpha, chromaloc=loc),
                    self.premultiply_reference_chroma(alpha, loc)
                )

    def test_premultiply_per_frame_dispatch(self):
        alpha = self.repeat(self.create_checkerboard())
        clip = self.vary_chromaloc(self.create_yuv(128, 255, 255))
        out = self.core.std.PreMultiply(clip, alpha)
        single = self.create_checkerboard()
        results = []
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                result = plane_bytes(out.get_frame(loc), 1)
                self.assertEqual(result, self.premultiply_reference_chroma(single, loc))
                results.append(result)
        # Each siting should have a distinct luma alignment
        self.assertEqual(len(set(results)), len(ALL_LOCATIONS))

    def test_premultiply_interlaced_matches_reference(self):
        # The alpha resample for interlaced content should match resize's
        # native interlaced chroma siting.
        alpha = self.create_checkerboard()
        clip = self.create_yuv(128, 255, 255)
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.premultiply_chroma(
                        clip.std.SetFrameProps(_FieldBased=vs.FIELD_TOP),
                        alpha.std.SetFrameProps(_FieldBased=vs.FIELD_TOP),
                        chromaloc=loc
                    ),
                    self.premultiply_reference_chroma(alpha, loc, field_based=True)
                )



class MaskResampleTest(unittest.TestCase):
    """The tiled MaskedMerge and PreMultiply resample a mask the size of the luma plane for
    subsampled chroma themselves: zimg's bilinear at the subsampling factor, sited for the frame's
    _ChromaLocation and for the mask frame's fields, summed exactly and rounded once. Checked here
    against a plain reference of that arithmetic on random masks, small enough that most samples
    sit near an edge, and against resize itself, which rounds between its two passes and so may
    differ by one.
    """

    FORMATS = (vs.YUV420P8, vs.YUV420P16, vs.YUV422P8, vs.YUV440P8, vs.YUV411P8, vs.YUV410P8, vs.YUV420P10, vs.YUV420PS)
    # The mask frame's properties: a frame, one field of each parity, and two interlaced orders.
    ROWS = ({}, {'_Field': 1}, {'_Field': 0}, {'_FieldBased': vs.FIELD_TOP}, {'_FieldBased': vs.FIELD_BOTTOM})

    def setUp(self):
        self.core = vs.core
        self.core.num_threads = 1

    def random_gray(self, fmt, width, height, seed, props=None):
        ff = self.core.get_video_format(fmt)
        gray = self.core.query_video_format(vs.GRAY, ff.sample_type, ff.bits_per_sample)
        blank = self.core.std.BlankClip(format=gray.id, width=width, height=height, length=1)

        def fill(n, f):
            out = f.copy()
            rng = random.Random(seed)
            ptr = out.get_write_ptr(0).value
            stride = out.get_stride(0)
            for y in range(height):
                if ff.sample_type == vs.FLOAT:
                    row = struct.pack(f"={width}{'e' if ff.bytes_per_sample == 2 else 'f'}", *(rng.random() for _ in range(width)))
                elif ff.bytes_per_sample == 1:
                    row = rng.randbytes(width)
                else:
                    row = struct.pack(f'={width}H', *(rng.getrandbits(ff.bits_per_sample) for _ in range(width)))
                ctypes.memmove(ptr + y * stride, row, len(row))
            return out

        clip = self.core.std.ModifyFrame(blank, blank, fill)
        return clip.std.SetFrameProps(**props) if props else clip

    @staticmethod
    def axis(factor, shift):
        """The bilinear filter zimg resamples with over factor samples per output sample, its centre
        moved by shift samples: the taps of output sample i as (offset from factor * i, integer
        weight), and the sum of the weights."""
        pos = Fraction(factor, 2) + shift
        first = math.floor(pos - factor + Fraction(1, 2))
        taps = [(first + j, max(1 - abs(first + j + Fraction(1, 2) - pos) / factor, 0)) for j in range(2 * factor)]
        total = sum(w for _, w in taps)
        scale = math.lcm(*((w / total).denominator for _, w in taps))
        return [(offset, int(w / total * scale)) for offset, w in taps if w], scale

    @staticmethod
    def mirror(i, n):
        i = -i - 1 if i < 0 else 2 * n - 1 - i if i >= n else i
        return min(max(i, 0), n - 1)

    def reference(self, mask, fmt, loc, props):
        """The chroma plane of fmt the mask resamples to, each sample its exact weighted sum rounded
        half up, as a list of rows."""
        f = self.core.get_video_format(fmt)
        sw, sh = 1 << f.subsampling_w, 1 << f.subsampling_h
        m = memoryview(mask.get_frame(0)[0]).tolist()
        height, width = len(m), len(m[0])
        left = loc in (vs.CHROMA_LEFT, vs.CHROMA_TOP_LEFT, vs.CHROMA_BOTTOM_LEFT)
        src_left = -Fraction(sw - 1, 2) if left else Fraction(0)
        src_top = {vs.CHROMA_TOP_LEFT: -Fraction(sh - 1, 2), vs.CHROMA_TOP: -Fraction(sh - 1, 2),
                   vs.CHROMA_BOTTOM_LEFT: Fraction(sh - 1, 2), vs.CHROMA_BOTTOM: Fraction(sh - 1, 2)}.get(loc, Fraction(0))
        field = Fraction(1, 4) * (1 - sh)
        h_taps, h_scale = self.axis(sw, src_left)
        v_frame = self.axis(sh, src_top)
        v_fields = (self.axis(sh, src_top / 2 + field), self.axis(sh, src_top / 2 - field))
        maxval = (1 << f.bits_per_sample) - 1 if f.sample_type == vs.INTEGER else None

        out = []
        for y in range(height // sh):
            if '_Field' in props:
                (v_taps, v_scale), rows, vy, row_of = v_fields[1 - props['_Field']], height, y, lambda r: r
            elif props.get('_FieldBased', 0):
                parity = y & 1
                (v_taps, v_scale), rows, vy = v_fields[parity], height // 2, y >> 1
                row_of = lambda r, parity=parity: 2 * r + parity
            else:
                (v_taps, v_scale), rows, vy, row_of = v_frame, height, y, lambda r: r
            row = []
            for x in range(width // sw):
                acc = 0
                for vo, vw in v_taps:
                    src = m[row_of(self.mirror(sh * vy + vo, rows))]
                    acc += vw * sum(hw * src[self.mirror(sw * x + ho, width)] for ho, hw in h_taps)
                scale = v_scale * h_scale
                row.append(min((acc + scale // 2) // scale, maxval) if maxval is not None else acc / scale)
            out.append(row)
        return out

    def merged_chroma(self, mask, fmt, loc):
        """MaskedMerge from 0 to the largest value gives the resampled mask in the chroma planes."""
        f = self.core.get_video_format(fmt)
        top = 1.0 if f.sample_type == vs.FLOAT else (1 << f.bits_per_sample) - 1
        black = self.core.std.BlankClip(mask, format=fmt, color=[0, 0, 0]).std.SetFrameProps(_ChromaLocation=loc)
        white = self.core.std.BlankClip(mask, format=fmt, color=[top] * 3).std.SetFrameProps(_ChromaLocation=loc)
        out = self.core.std.MaskedMerge(black, white, mask, first_plane=True).get_frame(0)
        return [memoryview(out[p]).tolist() for p in (1, 2)]

    def assertResampled(self, got, expected, tolerance):
        """tolerance None for integer samples, which are exact."""
        if tolerance is None:
            self.assertEqual(got, expected)
        else:
            self.assertEqual([len(row) for row in got], [len(row) for row in expected])
            for gr, er in zip(got, expected):
                for g, e in zip(gr, er):
                    self.assertAlmostEqual(g, e, delta=tolerance)

    def test_matches_exact_arithmetic(self):
        for fmt in self.FORMATS:
            name = self.core.get_video_format(fmt).name
            tolerance = 1e-5 if self.core.get_video_format(fmt).sample_type == vs.FLOAT else None
            # every siting and arrangement of rows where the factors differ most, a sample elsewhere
            full = fmt in (vs.YUV420P8, vs.YUV410P8, vs.YUV420P16)
            for loc in ALL_LOCATIONS if full else (vs.CHROMA_LEFT, vs.CHROMA_TOP):
                for props in self.ROWS if full else self.ROWS[::2]:
                    with self.subTest(format=name, location=loc.name, rows=props):
                        mask = self.random_gray(fmt, 32, 16, seed=int(loc) * 7 + len(props), props=props)
                        expected = self.reference(mask, fmt, loc, props)
                        u, v = self.merged_chroma(mask, fmt, loc)
                        self.assertResampled(u, expected, tolerance)
                        self.assertResampled(v, expected, tolerance)

    def test_half_and_wide_sums(self):
        # Half samples are summed in float and stored as half again, which rounds to one of the
        # two halves around the exact sum: below 1 they are at most 2^-11 apart. 16 bit samples
        # at 16x16 subsampling have sums past 32 bits, which take the 64 bit path.
        wide = self.core.query_video_format(vs.YUV, vs.INTEGER, 16, 4, 4).id
        for fmt, width, height, tolerance in ((vs.YUV420PH, 32, 16, 2.0 ** -11), (wide, 64, 64, None)):
            for loc in ALL_LOCATIONS:
                for props in self.ROWS:
                    with self.subTest(format=self.core.get_video_format(fmt).name, location=loc.name, rows=props):
                        mask = self.random_gray(fmt, width, height, seed=int(loc) * 5 + len(props), props=props)
                        expected = self.reference(mask, fmt, loc, props)
                        u, v = self.merged_chroma(mask, fmt, loc)
                        self.assertResampled(u, expected, tolerance)
                        self.assertResampled(v, expected, tolerance)

    def test_within_one_of_resize(self):
        # resize sites the chroma of the mask copied to 4:4:4, which is the contract the resample
        # follows; its two passes round in between, so a sample may be off by one, never more.
        # resize sites no fields past 2x vertical subsampling, where the resample keeps siting
        # them as it does at 2x.
        for fmt in (vs.YUV420P8, vs.YUV410P8, vs.YUV411P8, vs.YUV440P8, vs.YUV420P16):
            for loc in ALL_LOCATIONS:
                for props in self.ROWS:
                    if props and self.core.get_video_format(fmt).subsampling_h > 1:
                        continue
                    with self.subTest(format=self.core.get_video_format(fmt).name, location=loc.name, rows=props):
                        mask = self.random_gray(fmt, 32, 16, seed=3 + int(loc), props=props)
                        yuv444 = self.core.std.ShufflePlanes((mask, mask, mask), (0, 0, 0), colorfamily=vs.YUV)
                        ref = memoryview(yuv444.resize.Bilinear(format=fmt, chromaloc=loc).get_frame(0)[1]).tolist()
                        got = self.merged_chroma(mask, fmt, loc)[0]
                        worst = max(abs(g - r) for gr, rr in zip(got, ref) for g, r in zip(gr, rr))
                        self.assertLessEqual(worst, 1)

    def test_rows_wider_than_a_chunk(self):
        # the resample works through a row in chunks of at most 2048 mask samples
        for fmt in (vs.YUV420P8, vs.YUV410P8):
            with self.subTest(format=self.core.get_video_format(fmt).name):
                mask = self.random_gray(fmt, 4200, 4, seed=11)
                self.assertEqual(self.merged_chroma(mask, fmt, vs.CHROMA_LEFT)[0], self.reference(mask, fmt, vs.CHROMA_LEFT, {}))

    def test_premultiply(self):
        # with chroma at 255 and neutral at 128, the premultiplied chroma is
        # (127 * a + 127) // 255 + 128 of the resampled alpha a
        clip = self.core.std.BlankClip(format=vs.YUV420P8, width=32, height=16, color=(128, 255, 255), length=1)
        for loc in ALL_LOCATIONS:
            for props in self.ROWS:
                with self.subTest(location=loc.name, rows=props):
                    alpha = self.random_gray(vs.YUV420P8, 32, 16, seed=5, props=props)
                    out = self.core.std.PreMultiply(clip.std.SetFrameProps(_ChromaLocation=loc), alpha).get_frame(0)
                    expected = [[(127 * a + 127) // 255 + 128 for a in row] for row in self.reference(alpha, vs.YUV420P8, loc, props)]
                    self.assertEqual(memoryview(out[1]).tolist(), expected)

    def test_bad_mask_rows(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, width=32, height=16, length=1)
        cases = (
            (self.random_gray(vs.YUV420P8, 32, 16, seed=1, props={'_Field': 2}), 'bad _Field value in the mask: 2'),
            (self.random_gray(vs.YUV420P8, 32, 16, seed=1, props={'_FieldBased': 3}), 'bad _FieldBased value in the mask: 3'),
        )
        for mask, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(vs.Error, message):
                    self.core.std.MaskedMerge(clip, clip, mask, first_plane=True).get_frame(0)
        # the two fields of an interlaced 4:2:0 mask need an even number of chroma rows
        odd = self.core.std.BlankClip(format=vs.YUV420P8, width=32, height=18, length=1)
        mask = self.random_gray(vs.YUV420P8, 32, 18, seed=1, props={'_FieldBased': vs.FIELD_TOP})
        with self.assertRaisesRegex(vs.Error, 'needs an even number of chroma rows'):
            self.core.std.MaskedMerge(odd, odd, mask, first_plane=True).get_frame(0)
        with self.assertRaisesRegex(vs.Error, 'the alpha is interlaced'):
            self.core.std.PreMultiply(odd, mask).get_frame(0)

    def test_fields_without_vertical_subsampling(self):
        # without vertical subsampling a field's chroma rows are the frame's own, so a mask made of
        # fields reads like a progressive one, at any height
        for fmt in (vs.YUV422P8, vs.YUV411P8):
            for height in (16, 17):
                for props in ({'_FieldBased': vs.FIELD_TOP}, {'_Field': 0}):
                    with self.subTest(format=self.core.get_video_format(fmt).name, height=height, rows=props):
                        progressive = self.random_gray(fmt, 32, height, seed=13)
                        fields = self.random_gray(fmt, 32, height, seed=13, props=props)
                        self.assertEqual(self.merged_chroma(fields, fmt, vs.CHROMA_TOP_LEFT),
                                         self.merged_chroma(progressive, fmt, vs.CHROMA_TOP_LEFT))
                        clip = self.core.std.BlankClip(format=fmt, width=32, height=height, color=(128, 255, 255), length=1)
                        premultiplied = [memoryview(self.core.std.PreMultiply(clip, alpha).get_frame(0)[1]).tolist() for alpha in (fields, progressive)]
                        self.assertEqual(premultiplied[0], premultiplied[1])


try:
    from gputestsupport import GPUTestMixin, HAVE_GPU
except ImportError:
    from test.gputestsupport import GPUTestMixin, HAVE_GPU


@unittest.skipUnless(HAVE_GPU, "no usable Vulkan device")
class ChromaLocationMaskTestGPU(GPUTestMixin, ChromaLocationMaskTest):
    """MaskedMerge/PreMultiply on the GPU against the same CPU-built references.

    Only the filter under test moves: the reference graphs are built through resize and
    bound-style calls, which the wrapper leaves on the CPU on purpose, so the GPU mask
    resample keeps being validated against zimg rather than against itself.

    The interlaced cases are expected failures, and that is a statement about the core,
    not the tests: the GPU mask resample picks from progressive-only coefficient tables
    and ignores _FieldBased, where the CPU resample sites interlaced chroma per field, as
    resize does. Progressive output is byte identical between the two paths; interlaced
    is not. When the kernel learns fields these flip to unexpected successes and demand
    the decorators be removed.
    """

    @unittest.expectedFailure
    def test_maskedmerge_interlaced_differs_from_progressive(self):
        super().test_maskedmerge_interlaced_differs_from_progressive()

    @unittest.expectedFailure
    def test_maskedmerge_interlaced_matches_chromaloc_reference(self):
        super().test_maskedmerge_interlaced_matches_chromaloc_reference()

    @unittest.expectedFailure
    def test_premultiply_interlaced_matches_reference(self):
        super().test_premultiply_interlaced_matches_reference()


if __name__ == "__main__":
    unittest.main()
