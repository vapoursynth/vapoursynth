import unittest
import vapoursynth as vs

class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core()

    def checkDifference(self, cpu, gpu):
        diff = self.core.std.PlaneDifference([cpu, gpu], 0, prop="PlaneDifference0")
        diff = self.core.std.PlaneDifference([diff, gpu], 1, prop="PlaneDifference1")
        diff = self.core.std.PlaneDifference([diff, gpu], 2, prop="PlaneDifference2")

        for i in range(diff.num_frames):
            frame = diff.get_frame(i)
            self.assertEqual(frame.props.PlaneDifference0[0], 0)
            self.assertEqual(frame.props.PlaneDifference1[0], 0)
            self.assertEqual(frame.props.PlaneDifference2[0], 0)

    def testLUT16Bit(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P16, color=[69, 242, 115])

        lut = []
        for x in range(2 ** clip.format.bits_per_sample):
            lut.append(x)

        ret = self.core.std.Lut(clip, lut, [0, 1, 2])

        self.checkDifference(clip, ret)

    def testLUT2_8Bit(self):
        clipx = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        clipy = self.core.std.BlankClip(format=vs.YUV420P8, color=[115, 103, 205])

        lut = []
        for y in range(2 ** clipy.format.bits_per_sample):
            for x in range(2 ** clipx.format.bits_per_sample):
                lut.append(x)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=8)
        self.checkDifference(clipx, ret)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=10)
        comp = self.core.std.BlankClip(format=vs.YUV420P10, color=[69, 242, 115])
        self.checkDifference(comp, ret)

    def testLUT2_8Bit_10Bit(self):
        # Check 8-bit, 10-bit source.
        clipx = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        clipy = self.core.std.BlankClip(format=vs.YUV420P10, color=[15, 900, 442])

        lut = []
        for y in range(2 ** clipy.format.bits_per_sample):
            for x in range(2 ** clipx.format.bits_per_sample):
                lut.append(x)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=8)
        self.checkDifference(clipx, ret)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=10)
        comp = self.core.std.BlankClip(format=vs.YUV420P10, color=[69, 242, 115])
        self.checkDifference(comp, ret)

        # Check 10-bit, 8-bit source.
        # Colors are 8-bit levels for 10-bit clip so that we can verify output.
        clipx = self.core.std.BlankClip(format=vs.YUV420P10, color=[15, 235, 115])
        clipy = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])

        lut = []
        for y in range(2 ** clipy.format.bits_per_sample):
            for x in range(2 ** clipx.format.bits_per_sample):
                lut.append(x)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=8)
        comp = self.core.std.BlankClip(format=vs.YUV420P8, color=[15, 235, 115])
        self.checkDifference(comp, ret)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=10)
        self.checkDifference(clipx, ret)

    def testLUT2_9Bit_10Bit(self):
        # Check 9-bit, 10-bit source.
        clipx = self.core.std.BlankClip(format=vs.YUV420P9, color=[384, 10, 500])
        clipy = self.core.std.BlankClip(format=vs.YUV420P10, color=[15, 600, 900])

        lut = []
        for y in range(2 ** clipy.format.bits_per_sample):
            for x in range(2 ** clipx.format.bits_per_sample):
                lut.append(x)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=9)
        self.checkDifference(clipx, ret)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=8)
        comp = self.core.std.BlankClip(format=vs.YUV420P8, color=[128, 10, 244])
        self.checkDifference(comp, ret)

        # Check 10-bit, 9-bit source.
        clipx = self.core.std.BlankClip(format=vs.YUV420P10, color=[384, 10, 500])
        clipy = self.core.std.BlankClip(format=vs.YUV420P9, color=[15, 384, 511])

        lut = []
        for y in range(2 ** clipy.format.bits_per_sample):
            for x in range(2 ** clipx.format.bits_per_sample):
                lut.append(x)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=9)
        comp = self.core.std.BlankClip(format=vs.YUV420P9, color=[384, 10, 500])
        self.checkDifference(comp, ret)

        ret = self.core.std.Lut2(clips=[clipx, clipy], lut=lut, planes=[0, 1, 2], bits=8)
        comp = self.core.std.BlankClip(format=vs.YUV420P8, color=[128, 10, 244])
        self.checkDifference(comp, ret)

if __name__ == '__main__':
    unittest.main()
