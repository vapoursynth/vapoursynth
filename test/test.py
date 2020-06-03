import unittest
import vapoursynth as vs

def float_lut(x):
    return float(1)

def int_lut(x):
    return int(1)

class CoreTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core(threads=10)

    # core argument tests
    def test_num_threads(self):
        self.assertEqual(self.core.num_threads, 10)


### Clip-Attr tests

    def test_frames_generator(self):
        clip = self.core.std.BlankClip(length=200)
        e = -1
        for e, frame in enumerate(clip.frames()):
            self.assertIsInstance(frame, vs.VideoFrame)
        self.assertEqual(e, 199)

### Filter-Call-Tests

    def test_func1(self):
        with self.assertRaises(AttributeError):
            self.core.blah.list_functions()

    def test_arg1(self):
        self.core.std.BlankClip()

    def test_arg2(self):
        self.core.std.BlankClip(width=50)

    def test_arg3(self):
        self.core.std.BlankClip(width=[50])

    def test_arg4(self):
        with self.assertRaises(vs.Error):
            self.core.std.BlankClip(width=[50, 50])

    def test_arg5(self):
        with self.assertRaises(vs.Error):
            self.core.std.BlankClip(width=[])

    def test_arg6(self):
        self.core.std.BlankClip(_width=50)

    def test_arg7(self):
        with self.assertRaises(vs.Error):
            self.core.std.BlankClip(_width=[])

    def test_arg8(self):
        with self.assertRaises(vs.Error):
            self.core.std.BlankClip(10,10,10,10,10,10,10,10,10,10,10,10,10,10)

    def test_arg9(self):
        with self.assertRaises(vs.Error):
            self.core.std.BlankClip(width2=50)

    def test_arg10(self):
        with self.assertRaises(vs.Error):
            self.core.std.FlipVertical()

    def test_arg11(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.text.ClipInfo(clip)

    def test_arg12(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.text.ClipInfo(clip, 5)

    def test_arg13(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.text.ClipInfo(clip, alignment2=None)

    def test_arg14(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.text.ClipInfo(clip, alignment=None)

#lut argument tests
    def test_lut_arg1(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, lut=list(range(256)), function=lambda x: x)

    def test_lut_arg2(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip)

    def test_lut_arg3(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut(clip, lut=list(range(256)))

    def test_lut_arg4(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, lut=list(range(257)))

    def test_lut_arg5(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, lut=list(range(255)))

    def test_lut_arg6(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, lut=list(range(255)), function=lambda x: x)

    def test_lut_arg7(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, lut=list(range(255)), function=lambda x: x + 1)

    def test_lut_arg8(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut(clip, function=lambda x: x)

    def test_lut_arg9(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, function=lambda x: x, planes=[3])
        
    def test_lut_arg10(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, function=float_lut, floatout=False)
            
    def test_lut_arg11(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut(clip, function=float_lut, floatout=True)
        
    def test_lut_arg12(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut(clip, function=int_lut, floatout=True)
            
    def test_lut_arg13(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut(clip, function=int_lut, floatout=False)

#lut2 argument tests
    def test_lut2_arg1(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, lut=[5]*65536, function=lambda x,y: (x+y)//2)

    def test_lut2_arg2(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip)

    def test_lut2_arg3(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut2(clip, clip, lut=[5]*65536)

    def test_lut2_arg4(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, lut=[5]*65537)

    def test_lut2_arg5(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, lut=[5]*65535)

    def test_lut2_arg6(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, lut=[5]*65535, function=lambda x,y: (x+y)//2)

    def test_lut2_arg7(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, lut=[5]*65535, function=lambda x,y: x*y)

    def test_lut2_arg8(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        self.core.std.Lut2(clip, clip, function=lambda x,y: (x+y)//2)

    def test_lut2_arg9(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8, color=[69, 242, 115])
        with self.assertRaises(vs.Error):
            self.core.std.Lut2(clip, clip, function=lambda x,y: (x+y)//2, planes=[3])
            
#shuffleplanes argument tests
    def test_suffleplanes_arg1(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8)
        with self.assertRaises(vs.Error):
            self.core.std.ShufflePlanes(clip, planes=[0, 1, 2], colorfamily=vs.RGB)

    def test_suffleplanes_arg2(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8)
        self.core.std.ShufflePlanes(clip, planes=[0, 1, 2], colorfamily=vs.YCOCG)

    def test_suffleplanes_arg3(self):
        clip = self.core.std.BlankClip(format=vs.YUV420P8)
        self.core.std.ShufflePlanes(clip, planes=[1, 1, 2], colorfamily=vs.RGB)

    def test_suffleplanes_arg4(self):
        clip1 = self.core.std.BlankClip(format=vs.YUV420P8)
        clip2 = self.core.std.BlankClip(format=vs.YUV420P9)
        with self.assertRaises(vs.Error):
            self.core.std.ShufflePlanes([clip1, clip2], planes=[0, 1, 2], colorfamily=vs.YUV)

    def test_suffleplanes_arg5(self):
        clip1 = self.core.std.BlankClip(format=vs.YUV420P8)
        clip2 = self.core.std.BlankClip(format=vs.RGB24)
        with self.assertRaises(vs.Error):
            self.core.std.ShufflePlanes([clip1, clip2], planes=[2, 1, 2], colorfamily=vs.RGB)

    def test_suffleplanes_arg6(self):
        clip1 = self.core.std.BlankClip(format=vs.YUV420P8)
        clip2 = self.core.std.BlankClip(format=vs.RGB24)
        with self.assertRaises(vs.Error):
            self.core.std.ShufflePlanes([clip1, clip2, clip1], planes=[0, 1, 2], colorfamily=vs.RGB)               

#clamp tests
    def test_levels_clamp(self):
        for i in range(1024):
            levels_clip = self.core.std.BlankClip(format=vs.YUV420P10, color=[i, 0, 0]).std.Levels(min_in=504, max_in=520, gamma=1, min_out=0, max_out=1023, planes=0).std.PEMVerifier()
            levels_clip.get_frame(0)

    def test_convolution_clamp(self):
        for i in range(1024):
            levels_clip = self.core.std.BlankClip(format=vs.YUV420P10, color=[i, 0, 0]).std.Convolution([1, 1, 1, 1, 1, 1, 1, 1, 1], divisor=.1, planes=0).std.PEMVerifier()
            levels_clip.get_frame(0)

if __name__ == '__main__':
    unittest.main()
