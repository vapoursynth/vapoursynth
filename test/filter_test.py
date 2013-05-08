import unittest
import vapoursynth as vs

class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.Core()

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

if __name__ == '__main__':
    unittest.main()
