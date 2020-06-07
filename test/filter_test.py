import unittest
import vapoursynth as vs

class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.core
        self.Transpose = self.core.std.Transpose
        self.BlankClip = self.core.std.BlankClip
		
    def test_transpose8_test(self):
        clip = self.BlankClip(format=vs.YUV420P8, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

    def test_transpose16(self):
        clip = self.BlankClip(format=vs.YUV420P16, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

    def test_transposeS(self):
        clip = self.BlankClip(format=vs.YUV444PS, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

if __name__ == '__main__':
    unittest.main()
