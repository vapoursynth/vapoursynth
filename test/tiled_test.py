# Tiled filters (createTiledVideoFilter): filters running together in bands of rows must give
# exactly what the same filters give producing whole frames. A tiled node only runs together with
# its consumer when nothing caches its frames, so the same graph with a second consumer on every
# intermediate node is the whole frame reference, built in the same process. Frame maps
# (createFrameMapVideoFilter: Trim, Splice and the other filters that only pick frames or set
# properties) are seen through, so the filters behind them run together at the picked frame, and
# so are plane maps (createPlaneMapVideoFilter: ShufflePlanes, SplitPlanes, CopyFrameProps).
import ctypes
import os
import random
import struct
import time
import unittest

import vapoursynth as vs

try:
    from gputestsupport import HAVE_GPU
except ImportError:
    from test.gputestsupport import HAVE_GPU

core = vs.core

FUSION_DISABLED = os.environ.get('VS_TILED_FUSION') == '0'

# Heights just above a power of two, so the rings wrap with a short last band, in every plane.
SIZES = [(96, 262), (40, 130)]
FORMATS = [vs.GRAY8, vs.YUV420P8, vs.YUV420P10, vs.YUV444P16, vs.YUV444PS, vs.YUV420PH]

CONV3 = [1, 2, 1, 2, 4, 2, 1, 2, 1]


def noise(fmt, width, height, length=3, seed=1):
    """Pseudo-random samples over each format's whole range, written row by row. Frame n has the
    properties N = n and Seed = seed, which tell where a frame came from."""
    blank = core.std.BlankClip(format=fmt, width=width, height=height, length=length)

    def fill(n, f):
        out = f.copy()
        rng = random.Random(seed * 1000 + n)
        ff = out.format
        for p in range(ff.num_planes):
            w = out.width >> (ff.subsampling_w if p else 0)
            h = out.height >> (ff.subsampling_h if p else 0)
            ptr = out.get_write_ptr(p).value
            stride = out.get_stride(p)
            for y in range(h):
                if ff.sample_type == vs.INTEGER and ff.bytes_per_sample == 1:
                    row = rng.randbytes(w)
                elif ff.sample_type == vs.INTEGER:
                    row = struct.pack(f'={w}H', *(rng.getrandbits(ff.bits_per_sample) for _ in range(w)))
                elif ff.bytes_per_sample == 2:
                    row = struct.pack(f'={w}e', *(rng.random() for _ in range(w)))
                else:
                    row = struct.pack(f'={w}f', *(rng.random() * 1.2 - 0.1 for _ in range(w)))
                ctypes.memmove(ptr + y * stride, row, len(row))
        out.props['N'] = n
        return out

    return core.std.SetFrameProps(core.std.ModifyFrame(blank, blank, fill), _Matrix=1, Source=fmt, Seed=seed)


def planes(frame):
    return [memoryview(frame[p]).tobytes() for p in range(frame.format.num_planes)]


class TiledTest(unittest.TestCase):
    def setUp(self):
        self.timing = core.timings.enabled
        self.threads = core.num_threads
        core.timings.enabled = True
        self.pins = []

    def tearDown(self):
        core.timings.enabled = self.timing
        core.num_threads = self.threads
        self.pins.clear()

    def pin(self, clip):
        """Gives clip a second consumer, which keeps a cache on it and so makes it produce whole
        frames of its own instead of running together with its consumer."""
        self.pins.append(core.std.Trim(clip, length=1))
        return clip

    def assertSameAsWholeFrames(self, build, frames=3):
        """build(keep) makes a graph, passing every intermediate clip through keep. frames None
        compares every frame."""
        tiled = build(lambda clip: clip)
        whole = build(self.pin)
        self.assertGreater(len(self.pins), 0)
        self.assertEqual(tiled.num_frames, whole.num_frames)
        for n in range(tiled.num_frames if frames is None else frames):
            a = tiled.get_frame(n)
            b = whole.get_frame(n)
            for p, (pa, pb) in enumerate(zip(planes(a), planes(b))):
                self.assertEqual(pa, pb, f'frame {n} plane {p}')
            self.assertEqual(dict(a.props), dict(b.props), f'frame {n} properties')
        self.pins.clear()
        return tiled

    def for_each_format(self, check):
        for fmt in FORMATS:
            for width, height in SIZES:
                with self.subTest(format=core.get_video_format(fmt).name, size=(width, height)):
                    check(noise(fmt, width, height), core.get_video_format(fmt))

    def with_and_without_spare_threads(self, check):
        """A filter runs several inputs together with itself only while the pool has no thread to
        spare, and otherwise requests them as frames to have them computed at the same time. One
        frame is requested at a time here, so the thread count alone decides which it is."""
        for threads in (1, self.threads):
            core.num_threads = threads
            with self.subTest(threads=threads):
                check()

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_unread_input_is_never_computed(self):
        # Where running together shows from the outside: an input none of whose rows are read is
        # skipped when it runs together with its consumer, and computed in full when it is
        # requested as a frame, which is what the pin forces.
        src = noise(vs.YUV420P8, 64, 66)
        unread_b = core.std.Invert(src)
        unread_a = core.std.Invert(src)
        pinned = self.pin(core.std.Invert(src))
        outputs = [
            core.std.Merge(src, unread_b, weight=0),  # every plane is clipa's
            core.std.Merge(unread_a, src, weight=1),  # every plane is clipb's
            core.std.Merge(src, pinned, weight=0),
        ]
        for out in outputs:
            for n in range(3):
                self.assertEqual(planes(out.get_frame(n)), planes(src.get_frame(n)))
        self.assertEqual([unread_b.timings, unread_a.timings], [0, 0])
        self.assertGreater(pinned.timings, 0)

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_time_is_credited_to_the_filter_that_spent_it(self):
        core.num_threads = 1
        src = core.std.BlankClip(format=vs.YUV420P8, width=1280, height=720, length=4, keep=True)
        a = core.std.Invert(src)
        b = core.std.Convolution(a, matrix=CONV3)
        c = core.std.Median(b)
        d = core.std.Invert(c)
        d.get_frame(0)
        for node in (src, a, b, c, d):
            node.timings = 0
        start = time.perf_counter_ns()
        for n in range(1, 4):
            d.get_frame(n)
        elapsed = time.perf_counter_ns() - start
        times = [node.timings for node in (a, b, c, d)]
        for t in times:
            self.assertGreater(t, 0)
        # on one thread every nanosecond belongs to one filter: the last one, in whose requests
        # the others run, is not charged for their time as well
        self.assertLessEqual(sum(times), elapsed)

    def test_pointwise(self):
        def check(src, f):
            integer = f.sample_type == vs.INTEGER

            def build(keep):
                c = keep(core.std.Invert(src))
                c = keep(core.std.Limiter(c, min=16 if integer else 0.1, max=200 if integer else 0.8))
                c = keep(core.std.Binarize(c, threshold=100 if integer else 0.4, planes=[0]))
                c = keep(core.std.Levels(c, gamma=1.7))
                return core.std.InvertMask(c)
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_halo_filters(self):
        def check(src, f):
            def build(keep):
                c = keep(core.std.Convolution(src, matrix=CONV3))
                c = keep(core.std.Median(c))
                c = keep(core.std.Minimum(c, coordinates=[0, 1, 0, 1, 1, 0, 1, 0]))
                c = keep(core.std.Sobel(c))
                c = keep(core.std.Invert(c))
                c = keep(core.std.Deflate(c))
                c = keep(core.std.Inflate(c))
                c = keep(core.std.Prewitt(c))
                return core.std.Maximum(c)
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_planes_passed_through(self):
        def check(src, f):
            def build(keep):
                c = keep(core.std.Invert(src, planes=[0]))
                if f.num_planes > 1:
                    c = keep(core.std.Convolution(c, matrix=CONV3, planes=[1]))
                    c = keep(core.std.Invert(c, planes=[2]))
                c = keep(core.std.Median(c, planes=[0]))
                return core.std.Invert(c, planes=[0])
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_properties(self):
        src = noise(vs.YUV420P8, 64, 66)

        def build(keep):
            c = keep(core.std.Invert(src))
            c = keep(core.std.SetFrameProps(c, A=1, Text='abc'))
            c = keep(core.std.Invert(c))
            c = keep(core.std.SetFrameProp(c, prop='B', intval=[2, 3]))
            c = keep(core.std.RemoveFrameProps(c, props=['Source', '_Mat*']))
            c = keep(core.std.SetFieldBased(c, value=1))
            return core.std.Convolution(c, matrix=CONV3)
        out = self.assertSameAsWholeFrames(build)
        props = out.get_frame(0).props
        text = props['Text']
        text = text.decode() if isinstance(text, bytes) else text
        self.assertEqual((props['A'], text, list(props['B']), props['_FieldBased']), (1, 'abc', [2, 3], 1))
        self.assertNotIn('Source', props)
        self.assertNotIn('_Matrix', props)

    def test_multiple_inputs(self):
        def check(src, f):
            integer = f.sample_type == vs.INTEGER

            def build(keep):
                a = keep(core.std.Invert(src))
                b = keep(core.std.Limiter(src, min=30 if integer else 0.1))
                m = keep(core.std.Merge(a, b, weight=[0.3, 0.0] if f.num_planes > 1 else 0.3))
                d = keep(core.std.MakeDiff(m, keep(core.std.Convolution(src, matrix=CONV3))))
                d = keep(core.std.MergeDiff(keep(core.std.Invert(d)), keep(core.std.Median(src)), planes=[0]))
                return core.std.Expr([d, keep(core.std.Invert(src)), src], 'x y + z - abs 2 /')
            self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))
        self.for_each_format(check)

    def test_planes_from_the_second_clip(self):
        def check(src, f):
            def build(keep):
                a = keep(core.std.Median(keep(core.std.Invert(src))))
                b = keep(core.std.Convolution(src, matrix=CONV3))
                weights = [1.0, 0.25, 0.0] if f.num_planes > 1 else 1.0
                m = keep(core.std.Merge(a, b, weight=weights))
                return core.std.Invert(m)
            self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))
            # weight 1 is clipb's plane, unchanged
            b = core.std.Convolution(src, matrix=CONV3)
            merged = core.std.Merge(core.std.Invert(src), b, weight=[1.0, 0.25, 0.0] if f.num_planes > 1 else 1.0)
            self.assertEqual(planes(merged.get_frame(0))[0], planes(b.get_frame(0))[0])
        self.for_each_format(check)

    def test_second_clip_with_fewer_planes(self):
        src = noise(vs.YUV444P8, 64, 66)
        table = [(x ^ y) for y in range(256) for x in range(256)]

        def build(keep):
            gray = keep(core.std.Invert(keep(core.std.ShufflePlanes(src, 0, vs.GRAY))))
            c = keep(core.std.Lut2(keep(core.std.Median(src)), gray, planes=[0], lut=table))
            return core.std.Invert(c)
        self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))

    def test_luts(self):
        def check(src, f):
            if f.sample_type != vs.INTEGER or f.bits_per_sample > 10:
                return
            size = 1 << f.bits_per_sample

            def build(keep):
                c = keep(core.std.Lut(keep(core.std.Invert(src)), lut=[(x * 3 + 7) % size for x in range(size)]))
                c = keep(core.std.Lut(c, planes=[0], lut=[size - 1 - x // 2 for x in range(size)]))
                if size == 256:  # a 10+10 bit Lut2 table has a million entries
                    c = keep(core.std.Lut2(c, keep(core.std.Median(src)), lut=[(x + y) % size for y in range(size) for x in range(size)]))
                return core.std.Lut(keep(core.std.Invert(c)), bits=16, lut=[(x * 77) & 0xFFFF for x in range(size)])
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_expr_format_change(self):
        def check(src, f):
            dst = core.query_video_format(f.color_family, vs.FLOAT, 32, f.subsampling_w, f.subsampling_h)

            def build(keep):
                c = keep(core.std.Expr(keep(core.std.Invert(src)), 'x 3 *', format=dst.id))
                c = keep(core.std.Convolution(c, matrix=CONV3))
                if f.num_planes > 1:
                    c = keep(core.std.Expr(c, ['x 1 +', '']))
                return core.std.Invert(c)
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_shorter_input_repeats_its_last_frame(self):
        src = noise(vs.YUV420P8, 64, 66, length=5)

        def build(keep):
            short = keep(core.std.Invert(src[:2]))
            return core.std.Invert(keep(core.std.Merge(keep(core.std.Invert(src)), short, weight=0.6)))
        self.assertSameAsWholeFrames(build, frames=5)

    def test_long_chain(self):
        # longer than one tree may be, so it is cut into several
        src = noise(vs.YUV420P8, 64, 66, length=2)

        def build(keep):
            c = src
            for i in range(70):
                c = keep(core.std.Invert(c) if i % 3 else core.std.Median(c))
            return c
        self.assertSameAsWholeFrames(build, frames=2)

    def test_frame_maps(self):
        def check(src, f):
            other = noise(f.id, src.width, src.height, length=5, seed=2)

            def build(keep):
                a = keep(core.std.Trim(keep(core.std.Invert(src)), first=1))
                b = keep(core.std.Reverse(keep(core.std.Convolution(other, matrix=CONV3))))
                c = keep(core.std.Merge(a, keep(core.std.Trim(b, length=2)), weight=0.3))
                d = keep(core.std.Interleave([c, keep(core.std.Trim(keep(core.std.Median(other)), first=3))]))
                d = keep(core.std.SelectEvery(keep(core.std.Invert(d)), cycle=2, offsets=[1, 0]))
                e = keep(core.std.Splice([keep(core.std.AssumeFPS(keep(core.std.Invert(other)), fpsnum=50)), d]))
                e = keep(core.std.DeleteFrames(keep(core.std.DuplicateFrames(e, frames=[2])), frames=[0]))
                return core.std.Invert(keep(core.std.SetFrameProp(e, prop='P', intval=[1])))
            self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build, frames=None))
        self.for_each_format(check)

    def test_frame_maps_pick_the_right_frames(self):
        # Each frame map between two tiled filters: frame n is the source frame the map picks,
        # unchanged, with its properties and the duration the map gives it.
        src = noise(vs.GRAY8, 32, 34, length=7)
        other = noise(vs.GRAY8, 32, 34, length=4, seed=2)

        def a():
            return core.std.Invert(src)

        def b():
            return core.std.Invert(other)

        cases = [
            ('Trim', core.std.Trim(a(), first=2, last=5), [(src, n) for n in (2, 3, 4, 5)], None),
            ('Reverse', core.std.Reverse(a()), [(src, n) for n in (6, 5, 4, 3, 2, 1, 0)], None),
            ('Loop', core.std.Loop(core.std.Trim(a(), length=3), times=3), [(src, n) for n in (0, 1, 2) * 3], None),
            ('SelectEvery', core.std.SelectEvery(a(), cycle=3, offsets=[2, 0]), [(src, n) for n in (2, 0, 5, 3, 6)], (1, 16)),
            ('Interleave', core.std.Interleave([a(), b()]),
             [(src, 0), (other, 0), (src, 1), (other, 1), (src, 2), (other, 2), (src, 3), (other, 3), (src, 4), (other, 3), (src, 5), (other, 3), (src, 6)], (1, 48)),
            ('Splice', core.std.Splice([core.std.Trim(a(), length=2), b(), core.std.Trim(a(), first=5)]),
             [(src, 0), (src, 1)] + [(other, n) for n in range(4)] + [(src, 5), (src, 6)], None),
            ('DuplicateFrames', core.std.DuplicateFrames(a(), frames=[4, 1]), [(src, n) for n in (0, 1, 1, 2, 3, 4, 4, 5, 6)], None),
            ('DeleteFrames', core.std.DeleteFrames(a(), frames=[3, 0]), [(src, n) for n in (1, 2, 4, 5, 6)], None),
            ('FreezeFrames', core.std.FreezeFrames(a(), first=[1], last=[3], replacement=[6]), [(src, n) for n in (0, 6, 6, 6, 4, 5, 6)], None),
            ('AssumeFPS', core.std.AssumeFPS(a(), fpsnum=10), [(src, n) for n in range(7)], (1, 10)),
            ('SetFieldBased', core.std.SetFieldBased(a(), value=2), [(src, n) for n in range(7)], None),
        ]
        for name, mapped, expected, duration in cases:
            with self.subTest(name):
                out = core.std.Invert(mapped)
                self.assertEqual(out.num_frames, len(expected))
                for n, (clip, m) in enumerate(expected):
                    got = out.get_frame(n)
                    want = clip.get_frame(m)
                    self.assertEqual(planes(got), planes(want), f'frame {n}')
                    self.assertEqual((got.props['Seed'], got.props['N']), (want.props['Seed'], m), f'frame {n}')
                    if duration:
                        self.assertEqual((got.props['_DurationNum'], got.props['_DurationDen']), duration, f'frame {n}')
                    else:
                        self.assertEqual((got.props['_DurationNum'], got.props['_DurationDen']), (1, 24), f'frame {n}')

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_frame_maps_are_seen_through(self):
        # test_unread_input_is_never_computed with frame maps between: the filters behind them
        # run together with the consumer, which never reads them, so they are never computed
        src = noise(vs.YUV420P8, 64, 66, length=6)
        unread = [core.std.Invert(src) for _ in range(4)]
        pinned = self.pin(core.std.Invert(src))
        maps = [
            core.std.Trim(unread[0], first=2),
            core.std.SetFrameProp(core.std.Reverse(core.std.AssumeFPS(unread[1], fpsnum=5)), prop='X', intval=[1]),
            core.std.Splice([core.std.Trim(unread[2], length=3), core.std.Trim(unread[3], first=3)]),
            core.std.Trim(pinned, first=1),
        ]
        for mapped in maps:
            out = core.std.Merge(core.std.Trim(src, length=mapped.num_frames), mapped, weight=0)
            for n in range(out.num_frames):
                self.assertEqual(planes(out.get_frame(n)), planes(src.get_frame(n)))
        self.assertEqual([clip.timings for clip in unread], [0, 0, 0, 0])
        self.assertGreater(pinned.timings, 0)

    @unittest.skipUnless(HAVE_GPU, 'no usable Vulkan device')
    def test_frame_maps_keep_gpu_frames_on_the_gpu(self):
        src = noise(vs.YUV420P8, 64, 66, length=5)
        mapped = core.std.SetFrameProp(core.std.Reverse(core.std.Trim(core.std.GPUUpload(src), first=1)), prop='X', intval=[3])
        self.assertTrue(mapped.gpu_resident)
        out = core.std.GPUDownload(mapped)
        for n in range(4):
            got = out.get_frame(n)
            self.assertEqual(planes(got), planes(src.get_frame(4 - n)))
            self.assertEqual((got.props['N'], got.props['X']), (4 - n, 3))

    def test_masked_merge(self):
        def check(src, f):
            other = noise(f.id, src.width, src.height, seed=2)
            mask = noise(f.id, src.width, src.height, seed=3)

            def build(keep):
                c = keep(core.std.MaskedMerge(keep(core.std.Invert(src)), keep(core.std.Median(other)), keep(core.std.Maximum(mask))))
                c = keep(core.std.MaskedMerge(c, src, keep(core.std.Invert(mask)), premultiplied=True, planes=[0]))
                if f.num_planes > 1:
                    # the first mask plane for every plane, resampled for subsampled chroma, and a
                    # one plane alpha the same way
                    c = keep(core.std.MaskedMerge(c, other, keep(core.std.Deflate(mask)), first_plane=True))
                    c = keep(core.std.PreMultiply(c, keep(core.std.ShufflePlanes(keep(core.std.Invert(mask)), 0, vs.GRAY))))
                return core.std.Invert(c)
            self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))
        self.for_each_format(check)

    def test_separable_convolutions(self):
        def check(src, f):
            def build(keep):
                c = keep(core.std.Convolution(keep(core.std.Invert(src)), matrix=[1, 2, 3, 2, 1], mode='h'))
                c = keep(core.std.Convolution(c, matrix=[1, 4, 6, 4, 1, 2, 3], mode='v'))
                c = keep(core.std.Convolution(c, matrix=[1] * 13, mode='hv'))
                c = keep(core.std.Convolution(c, matrix=list(range(1, 26)), mode='v', planes=[0]))
                return core.std.Convolution(c, matrix=[1, -2, 4, -2, 1], mode='hv', bias=3, saturate=False)
            self.assertSameAsWholeFrames(build)
        self.for_each_format(check)

    def test_plane_maps(self):
        def check(src, f):
            other = noise(f.id, src.width, src.height, seed=2)

            def build(keep):
                y = keep(core.std.Convolution(keep(core.std.ShufflePlanes(keep(core.std.Invert(src)), 0, vs.GRAY)), matrix=CONV3))
                c = keep(core.std.CopyFrameProps(y, keep(core.std.Invert(other)), props=['Seed']))
                if f.num_planes == 1:
                    return core.std.Invert(c)
                c = keep(core.std.ShufflePlanes([c, keep(core.std.Median(other))], [0, 1, 2], f.color_family))
                c = keep(core.std.ShufflePlanes(c, [0, 2, 1], f.color_family))
                parts = [keep(core.std.Invert(keep(p))) for p in core.std.SplitPlanes(c)]
                return core.std.Invert(keep(core.std.CopyFrameProps(core.std.ShufflePlanes(parts, [0, 0, 0], f.color_family), src)))
            self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))
        self.for_each_format(check)

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_plane_maps_and_new_tiled_filters_are_seen_through(self):
        # test_unread_input_is_never_computed for plane maps, and for MaskedMerge and the one
        # dimensional convolutions, which run tiled: what is behind them is never computed
        src = noise(vs.YUV444P8, 64, 66, length=3)
        unread = [core.std.Invert(src) for _ in range(6)]
        outputs = [
            core.std.ShufflePlanes(unread[0], [0, 2, 1], vs.YUV),
            core.std.ShufflePlanes([core.std.ShufflePlanes(unread[1], 0, vs.GRAY), src], [0, 1, 2], vs.YUV),
            core.std.MaskedMerge(unread[2], src, src),
            core.std.Convolution(unread[3], matrix=[1, 2, 1], mode='hv'),
            core.std.Convolution(unread[4], matrix=[1, 2, 3, 2, 1], mode='v'),
        ]
        for out in outputs:
            merged = core.std.Merge(src, out, weight=0)
            for n in range(3):
                self.assertEqual(planes(merged.get_frame(n)), planes(src.get_frame(n)))
        # CopyFrameProps only takes properties from prop_src, so no pixel of it is computed
        props_only = core.std.Invert(core.std.CopyFrameProps(src, core.std.SetFrameProp(unread[5], prop='Z', intval=[7])))
        for n in range(3):
            self.assertEqual(props_only.get_frame(n).props['Z'], 7)
        self.assertEqual([clip.timings for clip in unread], [0] * 6)
        # a one plane mask or alpha resampled for subsampled chroma keeps MaskedMerge and PreMultiply
        # tiled as well
        src420 = noise(vs.YUV420P8, 64, 66, length=3)
        gray = core.std.ShufflePlanes(src420, 0, vs.GRAY)
        unread420 = [core.std.Invert(src420) for _ in range(2)]
        for out in (core.std.MaskedMerge(unread420[0], src420, gray), core.std.PreMultiply(unread420[1], gray)):
            merged = core.std.Merge(src420, out, weight=0)
            for n in range(3):
                self.assertEqual(planes(merged.get_frame(n)), planes(src420.get_frame(n)))
        self.assertEqual([clip.timings for clip in unread420], [0, 0])

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_nothing_behind_an_unread_input_is_computed(self):
        # One level further down than test_unread_input_is_never_computed: the filter read only for
        # its properties, or not at all, reads two inputs of its own, which a filter that is read
        # requests as frames to have them computed at the same time while the pool has threads to
        # spare. Nothing of them is read either, so they are never computed, spare threads or not.
        src = noise(vs.YUV420P8, 64, 66)
        inverted = [planes(core.std.Invert(src).get_frame(n)) for n in range(3)]
        unchanged = [planes(src.get_frame(n)) for n in range(3)]

        def check():
            unread = [core.std.Invert(src) for _ in range(4)]
            outputs = [
                (core.std.Invert(core.std.CopyFrameProps(src, core.std.Merge(unread[0], unread[1]))), inverted),
                (core.std.Merge(src, core.std.Merge(unread[2], unread[3]), weight=0), unchanged),
            ]
            for out, expected in outputs:
                for n in range(3):
                    self.assertEqual(planes(out.get_frame(n)), expected[n])
            self.assertEqual([clip.timings for clip in unread], [0] * 4)
        self.with_and_without_spare_threads(check)

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_long_properties_only_chains_leave_room_for_what_is_read(self):
        # A tree takes a bounded number of nodes, 64, and CopyFrameProps plans prop_src, which is
        # only read for its properties, ahead of clip. While both shared the bound, a prop_src chain
        # of 61 left no room for clip's unread input, which then became a frame computed in full.
        # Nodes only read for their properties have a bound of their own; the lengths bracket it.
        src = noise(vs.YUV420P8, 64, 66, length=1)
        expected = planes(core.std.Invert(src).get_frame(0))
        for length in range(40, 63):
            with self.subTest(length=length):
                prop_src = src
                for _ in range(length):
                    prop_src = core.std.Invert(prop_src)
                unread = core.std.Invert(src)
                out = core.std.Invert(core.std.CopyFrameProps(core.std.Merge(src, unread, weight=0), prop_src))
                self.assertEqual(planes(out.get_frame(0)), expected)
                self.assertEqual(unread.timings, 0)

    @unittest.skipUnless(HAVE_GPU, 'no usable Vulkan device')
    def test_plane_maps_keep_gpu_frames_on_the_gpu(self):
        src = noise(vs.YUV444P8, 64, 66, length=2)
        swapped = core.std.ShufflePlanes(core.std.GPUUpload(src), [0, 2, 1], vs.YUV)
        self.assertTrue(swapped.gpu_resident)
        # properties can come from a clip on the other side
        mixed = core.std.CopyFrameProps(swapped, core.std.SetFrameProp(src, prop='Z', intval=[5]))
        self.assertTrue(mixed.gpu_resident)
        out = core.std.GPUDownload(mixed)
        for n in range(2):
            got = out.get_frame(n)
            want = planes(src.get_frame(n))
            self.assertEqual(planes(got), [want[0], want[2], want[1]])
            self.assertEqual(got.props['Z'], 5)

    def test_variable_format_keeps_the_frame_path(self):
        clip = core.std.Splice([noise(vs.YUV420P8, 64, 66, length=1), noise(vs.GRAY8, 32, 48, length=1)], mismatch=True)
        out = core.std.Invert(core.std.Invert(clip))
        for n in range(2):
            self.assertEqual(planes(out.get_frame(n)), planes(clip.get_frame(n)))

    def test_reweave(self):
        # Fields woven from tiled filters behind frame maps into a tiled filter: reinterlacing a
        # double rate clip, the double rate pattern with a clipb one frame shorter, and the field
        # order taken from clipa's frames. Heights are mod 4 here, which 4:2:0 needs to weave,
        # and still just above a power of two in every plane.
        for fmt in FORMATS:
            for width, height in ((96, 260), (40, 132)):
                with self.subTest(format=core.get_video_format(fmt).name, size=(width, height)):
                    src = noise(fmt, width, height, length=6)

                    def reinterlaced(keep):
                        bob = keep(core.std.Invert(src))
                        return core.std.Invert(keep(core.std.Reweave(keep(bob[::2]), keep(bob[1::2]), tff=True)))

                    def doubled(keep):
                        later = keep(keep(core.std.Invert(src))[1:])
                        return core.std.Invert(keep(core.std.Reweave(keep(core.std.Median(src)), later, tff=False)))

                    def ordered(keep):
                        bff = keep(core.std.SetFieldBased(keep(core.std.Convolution(src, matrix=CONV3)), 1))
                        return core.std.Invert(keep(core.std.Reweave(bff, keep(core.std.Invert(src)))))

                    for build in (reinterlaced, doubled, ordered):
                        self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build, frames=None))

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_reweave_runs_together_with_its_consumer(self):
        # test_unread_input_is_never_computed for Reweave: a woven clip nothing reads, running
        # together with its consumer, only runs its beginFrame for the properties, so the filters
        # behind it are never computed. The same clip requested as a frame, which the pin makes it,
        # computes them.
        src = noise(vs.YUV420P8, 64, 68, length=4)
        fields = [core.std.Invert(src) for _ in range(4)]
        woven = core.std.Reweave(fields[0][::2], fields[1][1::2], tff=True)
        pinned = self.pin(core.std.Reweave(fields[2][::2], fields[3][1::2], tff=True))
        for clip in (woven, pinned):
            out = core.std.Merge(src[::2], clip, weight=0)
            for n in range(out.num_frames):
                self.assertEqual(planes(out.get_frame(n)), planes(src.get_frame(2 * n)))
        self.assertEqual([fields[0].timings, fields[1].timings], [0, 0])
        self.assertGreater(min(fields[2].timings, fields[3].timings), 0)

    def test_shared_nodes(self):
        # Nodes several filters of one tree read, which run together with all of them and are
        # computed once: a diamond, one inside another, readers at different depths (the shared
        # node's ring has to hold what the reader furthest behind needs while the one furthest
        # ahead has it produced), sharing through a frame map, a filter reading a clip twice, and a
        # node one reader reads rows of and another only properties.
        def check(src, f):
            def diamond(keep):
                d = keep(core.std.MakeDiff(src, keep(core.std.Invert(src))))
                return core.std.Expr([d, keep(core.std.Minimum(keep(core.std.Minimum(d)))), keep(core.std.Maximum(d))], 'x y + z - abs')

            def nested(keep):
                x = keep(core.std.Invert(src))
                y = keep(core.std.Merge(keep(core.std.Median(x)), x, 0.3))
                return core.std.Merge(keep(core.std.Convolution(y, matrix=CONV3)), y, 0.6)

            def depths(keep):
                x = keep(core.std.Invert(src))
                deep = x
                for _ in range(4):
                    deep = keep(core.std.Convolution(deep, matrix=CONV3))
                return core.std.Expr([x, deep, keep(core.std.Maximum(x))], 'x y + z - abs')

            def through_map(keep):
                x = keep(core.std.Invert(src))
                return core.std.Merge(keep(core.std.SetFrameProp(x, prop='S', intval=[1])), keep(core.std.Median(x)), 0.5)

            def twice(keep):
                x = keep(core.std.Median(src))
                return core.std.Merge(x, x, 0.5)

            def rows_and_properties(keep):
                x = keep(core.std.SetFrameProp(keep(core.std.Invert(src)), prop='X', intval=[7]))
                return core.std.Merge(keep(core.std.CopyFrameProps(keep(core.std.Median(src)), x)), keep(core.std.Minimum(x)))

            for build in (diamond, nested, depths, through_map, twice, rows_and_properties):
                with self.subTest(graph=build.__name__):
                    self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build))
        self.for_each_format(check)

    def test_shared_node_rings_hold_what_every_reader_needs(self):
        # Wide planes make the bands short and long chains of vertical 3-tap convolutions put the
        # readers of a shared node far apart, so that rings a few rows short of what they need wrap
        # into rows still read instead of hiding in the rounding to a power of two. One reader near
        # the root and one far ahead: the ring holds what the first needs while the other has the
        # node produced. Both far ahead: in the first band each starts from row 0, while the one
        # pulled first has the node produced well past it.
        src = noise(vs.GRAY8, 4096, 96, length=2)

        def chain(clip, keep, matrix):
            for _ in range(12):
                clip = keep(core.std.Convolution(clip, matrix=matrix, mode='v'))
            return clip

        def near_and_far(keep):
            x = keep(core.std.Invert(src))
            return core.std.Expr([chain(x, keep, [1, 2, 1]), keep(core.std.Median(x)), x], 'x y + z - abs')

        def both_far(keep):
            x = keep(core.std.Invert(src))
            return core.std.Merge(chain(x, keep, [1, 2, 1]), chain(x, keep, [1, 1, 1]))

        # with spare threads the chains become frames of their own and nothing is shared, so the
        # one thread case is the one that tests the rings
        for build in (near_and_far, both_far):
            with self.subTest(graph=build.__name__):
                self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build, frames=2))

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_shared_node_nothing_reads_is_never_computed(self):
        # test_unread_input_is_never_computed for a node two filters of the tree read: shared, it is
        # planned for its properties alone and never computed, where a whole frame for its cache, as
        # it was before nodes could be shared, is computed in full.
        src = noise(vs.YUV420P8, 64, 66)
        shared = core.std.Invert(src)
        out = core.std.Merge(src, core.std.Merge(core.std.Minimum(shared), core.std.Maximum(shared)), weight=0)
        for n in range(3):
            self.assertEqual(planes(out.get_frame(n)), planes(src.get_frame(n)))
        self.assertEqual(shared.timings, 0)

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_nodes_read_outside_or_at_other_frames_are_not_shared(self):
        # The same, except that a consumer outside the tree reads the node as well, or the tree reads
        # it at two frames: it then produces whole frames, which its cache keeps for every consumer.
        src = noise(vs.YUV420P8, 64, 66, length=4)
        outside = self.pin(core.std.Invert(src))
        frames = core.std.Invert(src)
        cases = [
            (outside, core.std.Merge(core.std.Minimum(outside), core.std.Maximum(outside))),
            (frames, core.std.Merge(core.std.Minimum(frames[:3]), core.std.Maximum(frames[1:]))),
        ]
        for node, unread in cases:
            out = core.std.Merge(src[:unread.num_frames], unread, weight=0)
            for n in range(out.num_frames):
                self.assertEqual(planes(out.get_frame(n)), planes(src.get_frame(n)))
            self.assertGreater(node.timings, 0)

    def test_plane_in_several_output_planes(self):
        # Plane maps can make one plane of a node part of several planes of the output: given
        # twice, or given once and read again by a filter that gives another plane.
        shuffles = [
            lambda x: core.std.ShufflePlanes(x, [0, 0, 0], vs.YUV),
            lambda x: core.std.ShufflePlanes([x, core.std.Invert(x), core.std.Maximum(x)], [0, 0, 0], vs.YUV),
            lambda x: core.std.MakeDiff(x, core.std.ShufflePlanes(x, [1, 0, 2], vs.YUV)),
        ]
        for i, shuffle in enumerate(shuffles):
            def build(keep, shuffle=shuffle):
                x = keep(core.std.Convolution(noise(vs.YUV444P8, 64, 66), matrix=CONV3))
                return core.std.Invert(keep(shuffle(x)))
            with self.subTest(shuffle=i):
                self.with_and_without_spare_threads(lambda: self.assertSameAsWholeFrames(build, frames=2))

    @unittest.skipIf(FUSION_DISABLED, 'VS_TILED_FUSION=0')
    def test_plane_in_several_output_planes_is_computed_once(self):
        # The planes of the output are produced one after the other, so a node running together
        # with them would compute such a plane once for each. It is requested as a frame instead,
        # which shows in a whole frame being made: the chroma nothing reads, which the node passes
        # through from `chroma`, is computed as well, where a node run together never asks for it.
        core.num_threads = 1
        shuffles = [
            lambda x: core.std.ShufflePlanes(x, [0, 0, 0], vs.YUV),
            lambda x: core.std.ShufflePlanes([x, core.std.Invert(x), core.std.Maximum(x)], [0, 0, 0], vs.YUV),
        ]
        for i, shuffle in enumerate(shuffles):
            with self.subTest(shuffle=i):
                src = noise(vs.YUV444P8, 64, 66)
                chroma = core.std.Invert(src)
                node = core.std.Invert(core.std.Merge(src, chroma, weight=[0, 1]), planes=[0])
                out = core.std.Invert(shuffle(node))
                for n in range(2):
                    out.get_frame(n)
                self.assertGreater(chroma.timings, 0)
                # the same node with its plane in one output plane runs together with it
                src = noise(vs.YUV444P8, 64, 66)
                chroma = core.std.Invert(src)
                node = core.std.Invert(core.std.Merge(src, chroma, weight=[0, 1]), planes=[0])
                out = core.std.Invert(core.std.ShufflePlanes(node, 0, vs.GRAY))
                for n in range(2):
                    out.get_frame(n)
                self.assertEqual(chroma.timings, 0)


if __name__ == '__main__':
    unittest.main()
