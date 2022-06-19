import unittest
import vapoursynth as vs

def get_pixel_value(clip, plane):
    frame = clip.get_frame(0)
    arr = frame[plane]
    return arr[0,0]

class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.core
        self.Transpose = self.core.std.Transpose
        self.BlankClip = self.core.std.BlankClip
        self.MakeFullDiff = self.core.std.MakeFullDiff
        self.MergeFullDiff = self.core.std.MergeFullDiff
		
    def test_transpose8_test(self):
        clip = self.BlankClip(format=vs.YUV420P8, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

    def test_transpose16(self):
        clip = self.BlankClip(format=vs.YUV420P16, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

    def test_transposeS(self):
        clip = self.BlankClip(format=vs.YUV444PS, color=[0, 0, 0], width=1156, height=752)
        self.Transpose(clip).get_frame(0)

    def test_makefulldiff1(self):
        clipa = self.BlankClip(format=vs.YUV420P8, color=[0, 255, 0], width=1156, height=752)
        clipb = self.BlankClip(format=vs.YUV420P8, color=[255, 0, 0], width=1156, height=752)
        diff1 = self.MakeFullDiff(clipa, clipb)
        newclipb = self.MergeFullDiff(clipb, diff1)
        self.assertEqual(get_pixel_value(newclipb, 0), get_pixel_value(clipa, 0))
        self.assertEqual(get_pixel_value(newclipb, 1), get_pixel_value(clipa, 1))

if __name__ == '__main__':
    unittest.main()
