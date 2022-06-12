import time
import unittest
import threading
import vapoursynth as vs

from concurrent.futures import Future


class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.core
        self.filter = self.core.std.BlankClip(length=20)
        self.slow_filter = self.filter.std.FrameEval(self.slow_filter_fe)
        self.fail_filter = self.filter.std.FrameEval(self.fail_filter_fe)

        self.condition = threading.Event()

        self.cb_called = False
        self.cb_result = None
        self.cb_node = None
        self.cb_n = None

        self.mv_called = False

    def slow_filter_fe(self, n):
        time.sleep(1)
        return self.filter

    def fail_filter_fe(self, n):
        time.sleep(1)
        raise RuntimeError("Fail")

    def cb_old(self, node, n, result):
        self.cb_called = True
        self.cb_result = result
        self.cb_node = node
        self.cb_n = n

        self.condition.set()

    def cb(self, result, error):
        self.cb_result = (result, error)
        self.cb_called = True
        self.cb_node = None
        self.cb_n = None

        self.condition.set()

    def mv(self, func, *args, **kwargs):
        self.mv_called = func
        return func(*args, **kwargs)

    ##########################################################################
    # Tests start here

    def test_raw_cb_slow(self):
        self.slow_filter.get_frame_async(0, self.cb)
        self.condition.wait(2)

        self.assertTrue(self.cb_called)
        self.assertIsInstance(self.cb_result, tuple)
        self.assertEqual(len(self.cb_result), 2)
        self.assertIsInstance(self.cb_result[0], vs.VideoFrame)
        self.assertIsNone(self.cb_result[1])

    def test_raw_cb_fail(self):
        self.fail_filter.get_frame_async(0, self.cb)
        self.condition.wait(2)

        self.assertTrue(self.cb_called)
        self.assertIsInstance(self.cb_result, tuple)
        self.assertEqual(len(self.cb_result), 2)
        self.assertIsInstance(self.cb_result[1], vs.Error)
        self.assertIsNone(self.cb_result[0])

    def test_async_slow(self):
        fut = self.slow_filter.get_frame_async(1)
        self.assertIsInstance(fut.result(2), vs.VideoFrame)

    def test_async_fail(self):
        fut = self.fail_filter.get_frame_async(1)
        with self.assertRaisesRegex(vs.Error, "Fail"):
            fut.result(2)

if __name__ == '__main__':
    unittest.main()
