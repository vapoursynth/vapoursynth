import unittest
import itertools
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

if __name__ == '__main__':
    unittest.main()