import unittest
import vapoursynth as vs

class CoreTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.core
        self.core.num_threads = 1

#blank clip argument tests
    def test_frame_props1(self):
        clip = self.core.std.BlankClip(format=vs.YUV444P8, color=[69, 242, 115], width=120, height=121)

        frame = clip.get_frame(0).copy()
        frame.props['_ColorRange'] = 0  
        self.assertEqual(frame.props.get('_Range'), 1)
        frame.props['_ColorRange'] = 1  
        self.assertEqual(frame.props.get('_Range'), 0)
        frame.props['_ColorRange'] = 2 
        self.assertEqual(frame.props.get('_Range'), 2)
        frame.props['_ColorRange'] = -1  
        self.assertEqual(frame.props.get('_Range'), -1)

    def test_frame_props2(self):
        clip = self.core.std.BlankClip(format=vs.YUV444P8, color=[69, 242, 115], width=120, height=121)

        frame = clip.get_frame(0).copy()
        frame.props['_Range'] = 0  
        self.assertEqual(frame.props.get('_ColorRange'), vs.RANGE_LIMITED)
        frame.props['_Range'] = 1  
        self.assertEqual(frame.props.get('_ColorRange'), vs.RANGE_FULL)
        frame.props['_Range'] = 2 
        self.assertEqual(frame.props.get('_ColorRange'), 2)
        frame.props['_Range'] = -1  
        self.assertEqual(frame.props.get('_ColorRange'), -1)
        
    def test_frame_props3(self):
        clip = self.core.std.BlankClip(format=vs.YUV444P8, color=[69, 242, 115], width=120, height=121)
        clip.std.SetFrameProp('_ColorRange', 1)
        clip.std.SetFrameProp('_Range', 1)
        
    def test_frame_props4(self):
        clip = self.core.std.BlankClip(format=vs.YUV444P8, color=[69, 242, 115], width=120, height=121)
        clip.std.SetFrameProps(_ColorRange=1)
        clip.std.SetFrameProps(_Range=0)


if __name__ == '__main__':
    unittest.main()
