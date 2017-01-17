import weakref
import unittest
import vapoursynth as vs


class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core()

    def test_weakref_core(self):
        ref = weakref.ref(self.core)
        self.assert_(ref() is self.core)

    def test_weakref_node(self):
        video = self.core.std.BlankClip()
        ref = weakref.ref(video)
        self.assert_(ref() is video)

    def test_weakref_frame(self):
        video = self.core.std.BlankClip()
        frame = video.get_frame(0)
        ref = weakref.ref(frame)
        self.assert_(ref() is frame)

if __name__ == '__main__':
    unittest.main()
