import unittest
import vapoursynth as vs

class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core()
        self.Lut = self.core.std.Lut
        self.Lut2 = self.core.std.Lut2
        self.BlankClip = self.core.std.BlankClip
        self.mask = lambda val, bits: val & ((1 << bits) - 1)
		
    def checkDifference(self, cpu, gpu):
        diff = self.core.std.PlaneStats(cpu, gpu, 0, prop="PlaneStats0")
        diff = self.core.std.PlaneStats(diff, gpu, 1, prop="PlaneStats1")
        diff = self.core.std.PlaneStats(diff, gpu, 2, prop="PlaneStats2")

        for i in range(diff.num_frames):
            frame = diff.get_frame(i)
            self.assertEqual(frame.props['PlaneStats0Diff'], 0)
            self.assertEqual(frame.props['PlaneStats1Diff'], 0)
            self.assertEqual(frame.props['PlaneStats2Diff'], 0)

    def testLUT16Bit(self):
        clip = self.BlankClip(format=vs.YUV420P16, color=[69, 242, 115])

        ret = self.Lut(clip, planes=[0, 1, 2], function=lambda x: x)

        self.checkDifference(clip, ret)

    def testLUT2_8Bit(self):
        clipx = self.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        clipy = self.BlankClip(format=vs.YUV420P8, color=[115, 103, 205])

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: x, bits=8)
        self.checkDifference(clipx, ret)

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: x, bits=10)
        comp = self.BlankClip(format=vs.YUV420P10, color=[69, 242, 115])
        self.checkDifference(comp, ret)

    def testLUT2_8Bit_10Bit(self):
        # Check 8-bit, 10-bit source.
        clipx = self.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        clipy = self.BlankClip(format=vs.YUV420P10, color=[15, 900, 442])

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 8), bits=8)
        self.checkDifference(clipx, ret)

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: x, bits=10)
        comp = self.BlankClip(format=vs.YUV420P10, color=[69, 242, 115])
        self.checkDifference(comp, ret)

        # Check 10-bit, 8-bit source.
        # Colors are 8-bit levels for 10-bit clip so that we can verify output.
        clipx = self.BlankClip(format=vs.YUV420P10, color=[15, 235, 115])
        clipy = self.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 8), bits=8)
        comp = self.BlankClip(format=vs.YUV420P8, color=[15, 235, 115])
        self.checkDifference(comp, ret)

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: x, bits=10)
        self.checkDifference(clipx, ret)

    def testLUT2_9Bit_10Bit(self):
        # Check 9-bit, 10-bit source.
        clipx = self.BlankClip(format=vs.YUV420P9, color=[384, 10, 500])
        clipy = self.BlankClip(format=vs.YUV420P10, color=[15, 600, 900])

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 9), bits=9)
        self.checkDifference(clipx, ret)

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 8), bits=8)
        comp = self.BlankClip(format=vs.YUV420P8, color=[128, 10, 244])
        self.checkDifference(comp, ret)

        # Check 10-bit, 9-bit source.
        clipx = self.BlankClip(format=vs.YUV420P10, color=[384, 10, 500])
        clipy = self.BlankClip(format=vs.YUV420P9, color=[15, 384, 511])

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 9), bits=9)
        comp = self.BlankClip(format=vs.YUV420P9, color=[384, 10, 500])
        self.checkDifference(comp, ret)

        ret = self.Lut2(clipa=clipx, clipb=clipy, planes=[0, 1, 2], function=lambda x, y: self.mask(x, 8), bits=8)
        comp = self.BlankClip(format=vs.YUV420P8, color=[128, 10, 244])
        self.checkDifference(comp, ret)

if __name__ == '__main__':
    unittest.main()
