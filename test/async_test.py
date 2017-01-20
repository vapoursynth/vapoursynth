import time
import unittest
import threading
import vapoursynth as vs

from concurrent.futures import Future


class FilterTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core()
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

    def cb(self, node, n, result):
        self.cb_called = True
        self.cb_result = result
        self.cb_node = node
        self.cb_n = n

        self.condition.set()

    def mv(self, func, *args, **kwargs):
        self.mv_called = func
        return func(*args, **kwargs)

    ##########################################################################
    # Tests start here

    def test_raw_cb_slow(self):
        self.slow_filter.get_frame_async_raw(0, self.cb)
        self.condition.wait(2)

        self.assertTrue(self.cb_called)
        self.assertEqual(self.cb_node, self.slow_filter)
        self.assertEqual(self.cb_n, 0)
        self.assertIsInstance(self.cb_result, vs.VideoFrame)

    def test_raw_cb_fail(self):
        self.fail_filter.get_frame_async_raw(0, self.cb)
        self.condition.wait(2)

        self.assertTrue(self.cb_called)
        self.assertEqual(self.cb_node, self.fail_filter)
        self.assertEqual(self.cb_n, 0)
        self.assertIsInstance(self.cb_result, vs.Error)
        self.assertEqual(str(self.cb_result), "Fail")

    def test_raw_cb_fut_slow(self):
        fut = Future()
        fut.set_running_or_notify_cancel()
        self.slow_filter.get_frame_async_raw(1, fut)
        self.assertIsInstance(fut.result(2), vs.VideoFrame)

    def test_raw_cb_fut_slow_mv(self):
        fut = Future()
        fut.set_running_or_notify_cancel()
        self.slow_filter.get_frame_async_raw(1, fut, self.mv)
        self.assertIsInstance(fut.result(2), vs.VideoFrame)
        self.assertEqual(fut.set_result, self.mv_called)

    def test_raw_cb_fut_fail(self):
        fut = Future()
        fut.set_running_or_notify_cancel()
        self.fail_filter.get_frame_async_raw(1, fut)
        with self.assertRaisesRegex(vs.Error, "Fail"):
            fut.result(2)

    def test_raw_cb_fut_fail_mv(self):
        fut = Future()
        fut.set_running_or_notify_cancel()
        self.fail_filter.get_frame_async_raw(1, fut, self.mv)
        with self.assertRaisesRegex(vs.Error, "Fail"):
            fut.result(2)
        self.assertEqual(fut.set_exception, self.mv_called)

    def test_async_slow(self):
        fut = self.slow_filter.get_frame_async(1)
        self.assertIsInstance(fut.result(2), vs.VideoFrame)

    def test_async_fail(self):
        fut = self.fail_filter.get_frame_async(1)
        with self.assertRaisesRegex(vs.Error, "Fail"):
            fut.result(2)

if __name__ == '__main__':
    unittest.main()
