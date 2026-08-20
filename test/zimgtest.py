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
