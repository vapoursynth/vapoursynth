import itertools
import unittest

import vapoursynth as vs

core = vs.core

colorfamilies = (vs.GRAY, vs.YUV, vs.RGB)
intbitdepths = (8, 9, 10, 11, 12, 13, 14, 15, 16)
floatbitdepths = (16, 32)
yuvss = (0, 1, 2)


class ZimgTest(unittest.TestCase):
    def _build_format_ids(self):
        for cfs in colorfamilies:
            for bps in intbitdepths:
                if cfs == vs.YUV:
                    for wss in yuvss:
                        for hss in yuvss:
                            yield core.query_video_format(cfs, vs.INTEGER, bps, wss, hss).id
                else:
                    yield core.query_video_format(cfs, vs.INTEGER, bps, 0, 0).id

        for cfs in colorfamilies:
            for bps in floatbitdepths:
                if cfs == vs.YUV:
                    for wss in yuvss:
                        for hss in yuvss:
                            yield core.query_video_format(cfs, vs.FLOAT, bps, wss, hss).id
                else:
                    yield core.query_video_format(cfs, vs.FLOAT, bps, 0, 0).id

    def test_blank_clip_with_format(self):
        formatids = list(self._build_format_ids())
        for informat, outformat in itertools.product(formatids, formatids):
            try:
                clip = core.std.BlankClip(format=informat)
                if clip.format.color_family in (vs.YUV, vs.GRAY):
                    clip = core.resize.Bicubic(clip, format=outformat, matrix_in_s="709")
                elif core.get_video_format(outformat).color_family in (vs.YUV, vs.GRAY):
                    clip = core.resize.Bicubic(clip, format=outformat, matrix_s="709")
                else:
                    clip = core.resize.Bicubic(clip, format=outformat)
                clip.get_frame(0)
            except vs.Error as e:
                raise RuntimeError(f"Failed to convert from {informat} to {outformat}") from e

    # Where the clip lives; the GPU class runs the same Bob tests on the GPU path.
    def upload(self, clip):
        return clip

    def download(self, clip):
        return clip

    def bob_props(self, **kwargs):
        clip = core.std.BlankClip(format=vs.YUV420P8, width=720, height=480, length=2)
        clip = core.std.SetFrameProps(clip, _SARNum=32, _SARDen=27, _FieldBased=vs.FIELD_TOP)
        bob = self.download(core.resize.Bob(self.upload(clip), **kwargs))
        return [dict(bob.get_frame(n).props) for n in range(2)]

    # Bob makes frames of the fields SeparateFields gives it, which keep their frame's
    # _SARNum/_SARDen, so a frame of the source's size has the source's pixel aspect.
    def test_bob_keeps_the_pixel_aspect(self):
        for props in self.bob_props():
            self.assertEqual((props["_SARNum"], props["_SARDen"]), (32, 27))

    def test_bob_window_scales_the_pixel_aspect_against_the_frame(self):
        # src_height counts field rows: 200 of the field's 240 are 400 of the frame's 480
        for props in self.bob_props(src_top=10, src_height=200):
            self.assertEqual((props["_SARNum"], props["_SARDen"]), (64, 45))

    def test_bob_output_is_progressive(self):
        # a _Field left on the output makes a later resize sample each frame as one field
        for props in self.bob_props():
            self.assertNotIn("_Field", props)
            self.assertEqual(props["_FieldBased"], vs.FIELD_PROGRESSIVE)


try:
    from gputestsupport import HAVE_GPU, DECLINE_MARKERS
except ImportError:
    from test.gputestsupport import HAVE_GPU, DECLINE_MARKERS


@unittest.skipUnless(HAVE_GPU, "no usable Vulkan device")
class ZimgGPUTest(ZimgTest):
    """The same conversion sweep with a GPU resident input, so every combination the
    compute path accepts runs its kernels and every one it does not is a documented
    decline (the message says to insert GPUDownload) rather than a failure. The sweep
    asserts something actually ran on the GPU, so a build where the GPU path silently
    stopped accepting everything cannot pass by declining its way through."""

    def upload(self, clip):
        return core.std.GPUUpload(clip)

    def download(self, clip):
        self.assertTrue(clip.gpu_resident)
        return core.std.GPUDownload(clip)

    def test_blank_clip_with_format(self):
        formatids = list(self._build_format_ids())
        converted = 0
        declined = 0
        for informat, outformat in itertools.product(formatids, formatids):
            try:
                clip = core.std.GPUUpload(core.std.BlankClip(format=informat))
                if clip.format.color_family in (vs.YUV, vs.GRAY):
                    clip = core.resize.Bicubic(clip, format=outformat, matrix_in_s="709")
                elif core.get_video_format(outformat).color_family in (vs.YUV, vs.GRAY):
                    clip = core.resize.Bicubic(clip, format=outformat, matrix_s="709")
                else:
                    clip = core.resize.Bicubic(clip, format=outformat)
                if clip.gpu_resident:
                    clip = core.std.GPUDownload(clip)
                clip.get_frame(0)
                converted += 1
            except vs.Error as e:
                if any(marker in str(e) for marker in DECLINE_MARKERS):
                    declined += 1
                    continue
                raise RuntimeError(f"GPU conversion failed from {informat} to {outformat}") from e
        self.assertGreater(converted, 0,
                           f"every combination declined the GPU path ({declined} declines)")


if __name__ == "__main__":
    unittest.main()
