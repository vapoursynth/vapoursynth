import contextlib
import gc
import io
import multiprocessing
import pickle
import threading
import time
import traceback
import unittest
import weakref
from threading import Lock

import vapoursynth as vs


class StubPolicy(vs.EnvironmentPolicy):
    # The current environment is kept per thread, as the policy contract describes it and as
    # VSScript's policy keeps it. The module switches environments around every callback it runs
    # on a worker thread, restoring what it found there afterwards. With one environment shared
    # by all threads, a worker that entered while the main thread was still inside a use() block
    # saved that environment, and restored it after the main thread had left: the policy then
    # held a strong reference past teardown, and the collection checks failed at random.
    def __init__(self) -> None:
        self._local = threading.local()
        self._api = None

    def on_policy_registered(self, special_api):
        self._api = special_api
        self._local = threading.local()

    def on_policy_cleared(self):
        assert self._api is not None
        self._local = threading.local()

    def get_current_environment(self):
        return getattr(self._local, "current", None)

    def set_environment(self, environment):
        previous = getattr(self._local, "current", None)
        self._local.current = environment
        return previous


@contextlib.contextmanager
def _with_policy():
    pol = StubPolicy()
    vs.register_policy(pol)
    try:
        yield pol
    finally:
        pol._api.unregister_policy()


@contextlib.contextmanager
def _holding(lock, *threads):
    # For tests that hold the lock a frame request blocks on. However the block exits, the lock
    # is released and the threads that were started are joined. A failed check that left it held
    # would hang the run instead of failing it: the blocked request never completes, and neither
    # does a teardown waiting for it, nor the test process.
    lock.acquire()
    try:
        yield
    finally:
        lock.release()
        for thread in threads:
            if thread.ident is not None:
                thread.join()


test_functions = {}
counter = 0


def _wrap_with(ident):
    func = test_functions[ident]
    return func(EnvironmentTest())


# How long one test may run in its process, and how long that process then gets to exit. Every
# test takes seconds at most; the margins are for slow CI runners.
SUBPROCESS_TEST_TIMEOUT = 120
SUBPROCESS_EXIT_TIMEOUT = 30


class _RemoteTraceback(Exception):
    # The test process's traceback, attached as the cause of the exception raised here again, the
    # way concurrent.futures shows one.
    def __str__(self):
        return self.args[0]


def _run_in_subprocess(ident, conn):
    try:
        _wrap_with(ident)
        outcome = None
    except BaseException as e:
        remote = "".join(traceback.format_exception(e))
        try:
            pickle.dumps(e)
        except Exception:
            e = RuntimeError(f"{type(e).__name__}: {e}")
        outcome = (e, remote)
    conn.send(outcome)
    conn.close()


def subprocess_runner(func):
    global counter
    my_counter = counter
    counter += 1
    test_functions[my_counter] = func

    def _wrapper(self):
        # Each test runs in a fresh process, against a deadline: a hang, whether a deadlock in
        # VapourSynth or a thread left waiting for good, fails the test instead of stalling the
        # whole run. A process that misses its deadline, or does not exit after reporting, is
        # killed rather than waited for. What the test raised is raised here again, so failures,
        # errors and skips are reported as before.
        ctx = multiprocessing.get_context("spawn")
        receiver, sender = ctx.Pipe(duplex=False)
        proc = ctx.Process(target=_run_in_subprocess, args=(my_counter, sender), daemon=True)
        proc.start()
        sender.close()
        outcome = None
        finished = reported = False
        try:
            # True once the result arrives, or the process dies without sending one
            finished = receiver.poll(SUBPROCESS_TEST_TIMEOUT)
            if finished:
                try:
                    outcome = receiver.recv()
                    reported = True
                except EOFError:
                    pass
                proc.join(SUBPROCESS_EXIT_TIMEOUT)
        finally:
            lingering = proc.is_alive()
            if lingering:
                proc.kill()
            proc.join()
            receiver.close()

        if not finished:
            self.fail(f"did not finish within {SUBPROCESS_TEST_TIMEOUT} s, its process was killed")
        if not reported:
            self.fail(f"its process exited with code {proc.exitcode} before reporting a result")
        if outcome is not None:
            exc, remote = outcome
            raise exc from _RemoteTraceback(remote)
        if lingering:
            self.fail(f"passed, but its process did not exit within {SUBPROCESS_EXIT_TIMEOUT} s and was killed")

    return _wrapper


class AnObject:
    pass


class EnvironmentTest(unittest.TestCase):
    @subprocess_runner
    def test_environment_can_retrieve_api(self):
        with _with_policy() as pol:
            _version = vs.__api_version__
            self.assertIsNotNone(pol._api.get_vapoursynth_api((_version.api_major << 16) | _version.api_minor))

    @subprocess_runner
    def test_environment_can_retrieve_core_ptr(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            # the core is created on demand by this call, and the pointer must be the
            # live core's, stable across calls and usable right away
            ptr = pol._api.get_core_ptr(env)
            self.assertNotEqual(ptr.value, 0)
            self.assertEqual(ptr.value, pol._api.get_core_ptr(env).value)
            with pol._api.wrap_environment(env).use():
                vs.core.std.BlankClip(width=16, height=16, length=1).get_frame(0)

    @subprocess_runner
    def test_environment_use_unsets_environment_on_exit(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with self.assertRaises(RuntimeError):
                vs.get_current_environment()

            # comparisons and dunder probes answer per protocol, no environment needed
            self.assertNotEqual(wrapped, None)
            self.assertFalse(hasattr(vs.Local(), "__deepcopy__"))
            self.assertFalse(hasattr(vs.core, "__deepcopy__"))
            self.assertIn("core", dir(vs.core))

            with wrapped.use():
                self.assertEqual(vs.get_current_environment(), wrapped)

            with self.assertRaises(RuntimeError):
                vs.get_current_environment()

    @subprocess_runner
    def test_environment_use_restores_environment_on_exit(self):
        with _with_policy() as pol:
            env1 = pol._api.create_environment()
            wrapped1 = pol._api.wrap_environment(env1)

            env2 = pol._api.create_environment()
            wrapped2 = pol._api.wrap_environment(env2)

            with wrapped1.use():
                ce1 = vs.get_current_environment()

                with wrapped2.use():
                    self.assertNotEqual(ce1, vs.get_current_environment())

                self.assertEqual(ce1, vs.get_current_environment())

    @subprocess_runner
    def test_policy_clearing_runs_callbacks(self):
        f1_run = [False]

        def f1():
            f1_run[0] = True

        f2_run = [False]

        def f2():
            f2_run[0] = True

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                vs.register_on_destroy(f1)
                vs.register_on_destroy(f2)
                vs.unregister_on_destroy(f1)

        self.assertFalse(f1_run[0])
        self.assertTrue(f2_run[0])

        f1_run = [False]
        f2_run = [False]

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                vs.register_on_destroy(f1)

        self.assertTrue(f1_run[0])
        self.assertFalse(f2_run[0])

    @subprocess_runner
    def test_environment_destruction_runs_callbacks(self):
        f1_run = [False]

        def f1():
            f1_run[0] = True

        f2_run = [False]

        def f2():
            f2_run[0] = True

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                vs.register_on_destroy(f1)
                vs.register_on_destroy(f2)
                vs.unregister_on_destroy(f1)

            pol._api.destroy_environment(env)

            self.assertFalse(f1_run[0])
            self.assertTrue(f2_run[0])

            f1_run = [False]
            f2_run = [False]

            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                vs.register_on_destroy(f1)

            pol._api.destroy_environment(env)

            self.assertTrue(f1_run[0])
            self.assertFalse(f2_run[0])

    @subprocess_runner
    def test_environment_warns_against_resource_leaks(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()

            with self.assertWarnsRegex(RuntimeWarning, "An environment is getting collected"):
                env = None
                gc.collect()

    @subprocess_runner
    def test_locals_store_data_between_envs(self):
        local = vs.Local()

        with _with_policy() as pol:
            env1 = pol._api.create_environment()
            wrapped1 = pol._api.wrap_environment(env1)

            env2 = pol._api.create_environment()
            wrapped2 = pol._api.wrap_environment(env2)

            with wrapped1.use():
                with self.assertRaises(AttributeError):
                    local.hello

            with wrapped2.use():
                local.hello = 5

            with wrapped1.use():
                local.hello = 1

            with wrapped2.use():
                self.assertEqual(local.hello, 5)

            with wrapped1.use():
                self.assertEqual(local.hello, 1)

            with wrapped1.use():
                del local.hello

            with wrapped2.use():
                self.assertEqual(local.hello, 5)

            with wrapped1.use():
                with self.assertRaises(AttributeError):
                    local.hello

    @subprocess_runner
    def test_locals_differ_from_each_other(self):
        local1 = vs.Local()
        local2 = vs.Local()

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                local1.a = 5
                local2.a = 6

                self.assertEqual(local1.a, 5)
                self.assertEqual(local2.a, 6)

    @subprocess_runner
    def test_locals_store_data_between_envs(self):
        local = vs.Local()
        o = AnObject()

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                local.obj = o

            wr = weakref.ref(o)
            del o

            with wrapped.use():
                self.assertIsNotNone(wr())

            pol._api.destroy_environment(env)
            gc.collect()
            gc.collect()
            gc.collect()

            self.assertIsNone(wr())

    @subprocess_runner
    def test_exception_tunneling_and_no_leak(self):
        class CustomException(BaseException):
            pass

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                clip = vs.core.std.BlankClip(width=16, height=16, length=2)

                exc = CustomException("Tunnel test")
                exc_ref = weakref.ref(exc)

                def callback(n, f):
                    raise exc

                modified = clip.std.ModifyFrame(clip, callback)

                with self.assertRaises(CustomException) as ctx:
                    modified.get_frame(0)

                self.assertEqual(str(ctx.exception), "Tunnel test")

                # Clear all references that might hold the exception
                del ctx
                del callback
                del modified
                del clip
                del exc
                gc.collect()
                gc.collect()
                gc.collect()

                self.assertIsNone(exc_ref())

    @subprocess_runner
    def test_environment_invalidation_releases_nodes_and_core(self):
        def modify_func(n, f):
            return f.copy()

        hold_strong_refs = []

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                core = vs.core.core
                clip = core.std.BlankClip(width=16, height=16, length=2)
                clip = core.std.ModifyFrame(clip, clip, modify_func)
                frame = clip.get_frame(0)
                hold_strong_refs.append(core)
                hold_strong_refs.append(clip)
                hold_strong_refs.append(frame)
                blank_clip = core.std.BlankClip
                std_plugin = core.std
                plugin_iter = core.plugins()
                func_iter = std_plugin.functions()

                # Track weak references to verify garbage collection later
                core_ref = weakref.ref(core)
                clip_ref = weakref.ref(clip)
                frame_ref = weakref.ref(frame)

                self.assertIsNotNone(core_ref())
                self.assertIsNotNone(clip_ref())
                self.assertIsNotNone(frame_ref())

            # Now, destroy the environment
            pol._api.destroy_environment(env)
            del env

            # Any subsequent access to core or clip should raise vs.Error
            with self.assertRaises(vs.Error) as ctx1:
                clip.get_frame(0)
            self.assertIn("Use of invalidated VideoNode", str(ctx1.exception))

            with self.assertRaisesRegex(AttributeError, "Use of invalidated VideoNode"):
                clip.std

            # comparisons and hashing fall back to identity instead of raising
            self.assertEqual(hash(clip), hash(clip))
            self.assertIn(clip, [clip])
            self.assertNotEqual(clip, frame)

            # every accessor that reaches the core must refuse the same way, never crash
            for access in (lambda: list(core.plugins()), lambda: core.core_version,
                           lambda: clip.node_name, lambda: clip.timings, lambda: clip.dependencies,
                           lambda: next(clip.frames()),
                           lambda: blank_clip(width=16, height=16, length=1)):
                with self.assertRaises(vs.Error):
                    access()

            # representations and completion degrade instead of raising
            self.assertIn("invalidated", repr(core))
            self.assertIn("invalidated", str(core))
            self.assertIn("invalidated", repr(std_plugin))
            self.assertEqual(dir(std_plugin), [])
            self.assertIsInstance(dir(core), list)
            self.assertIsInstance(dir(clip), list)

            # listings taken before the teardown were snapshotted and still iterate
            self.assertTrue(list(plugin_iter))
            self.assertTrue(list(func_iter))

            # invalidated objects handed to a live core are refused by the argument
            # conversion instead of reaching the core as NULL
            env2 = pol._api.create_environment()
            with pol._api.wrap_environment(env2).use():
                with self.assertRaisesRegex(vs.Error, "Use of invalidated VideoNode"):
                    vs.core.std.Crop(clip, left=1)
                writable = vs.core.std.BlankClip(width=16, height=16, length=1).get_frame(0).copy()
                with self.assertRaisesRegex(RuntimeError, "already been released"):
                    writable.props["_Alpha"] = frame
            pol._api.destroy_environment(env2)

            with self.assertRaises(vs.Error) as ctx2:
                core.num_threads
            self.assertIn("Use of invalidated Core", str(ctx2.exception))

            gc.collect()
            gc.collect()
            gc.collect()

            self.assertIsNone(wrapped.env())
            self.assertIsNotNone(core_ref())
            self.assertIsNotNone(clip_ref())
            self.assertIsNotNone(frame_ref())

    @subprocess_runner
    def test_environment_waits_for_futures(self):
        lock = Lock()
        running = False

        def check_running(func):
            def wrapper(n, f):
                nonlocal running
                running = True
                r = func(n, f)
                running = False
                return r
            return wrapper

        @check_running
        def modify_func(n, f):
            with lock:
                return f

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)
            thread = threading.Thread(target=lambda: pol._api.destroy_environment(env))

            with _holding(lock, thread):
                with wrapped.use():
                    core = vs.core.core
                    clip = core.std.BlankClip(width=16, height=16, length=2)
                    clip = core.std.ModifyFrame(clip, clip, modify_func)
                    fut = clip.get_frame_async(0)

                    fut_ref = weakref.ref(fut)
                    self.assertIsNotNone(fut_ref())

                thread.start()
                thread.join(0.25)

                gc.collect()
                gc.collect()
                gc.collect()

                # Environment should still be alive because wait_futures blocks destroy_environment
                # until the pending future resolves.
                self.assertIsNotNone(wrapped.env())
                self.assertTrue(running)

            # The future should have resolved successfully.
            self.assertFalse(running)
            self.assertIsNotNone(fut_ref())
            self.assertIsNone(fut_ref().exception())
            self.assertIsInstance(fut_ref().result(), vs.VideoFrame)

            del env
            gc.collect()
            gc.collect()
            gc.collect()

            self.assertIsNone(wrapped.env())

    @subprocess_runner
    def test_environment_refuses_requests_during_destruction(self):
        lock = Lock()

        def modify_func(n, f):
            with lock:
                return f

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)
            thread = threading.Thread(target=lambda: pol._api.destroy_environment(env))

            with _holding(lock, thread):
                with wrapped.use():
                    core = vs.core.core
                    clip = core.std.BlankClip(width=16, height=16, length=2)
                    clip = core.std.ModifyFrame(clip, clip, modify_func)
                    fut = clip.get_frame_async(0)

                thread.start()
                thread.join(0.25)

                # destroy_environment is blocked waiting for the first request, and every
                # request made after it claimed the environment must be refused, keyed on
                # the environment owning the node even though none is current here; checked,
                # since the worker blocked in modify_func is inside an environment switch right
                # now. The asynchronous request goes first and with a timeout: both kinds share
                # one admission check, and a synchronous request it let through would block on
                # this lock for good instead of failing.
                self.assertIsNotNone(wrapped.env())
                self.assertIsNone(pol.get_current_environment())
                self.assertIsInstance(clip.get_frame_async(1).exception(timeout=10), vs.Error)
                with self.assertRaisesRegex(vs.Error, "being destroyed"):
                    clip.get_frame(1)

            self.assertIsInstance(fut.result(), vs.VideoFrame)

    @subprocess_runner
    def test_policy_unregistering_itself_during_registration(self):
        class SelfUnregisteringPolicy(StubPolicy):
            def on_policy_registered(self, special_api):
                super().on_policy_registered(special_api)
                special_api.unregister_policy()

        vs.register_policy(SelfUnregisteringPolicy())

        # the registration must end in the plain "no policy" state, from which the
        # standalone policy is still created on demand
        self.assertFalse(vs.has_policy())
        self.assertIsInstance(vs.core.num_threads, int)
        self.assertTrue(vs.has_policy())

    @subprocess_runner
    def test_failed_registration_leaves_no_policy(self):
        class ExplodingPolicy(StubPolicy):
            def on_policy_registered(self, special_api):
                super().on_policy_registered(special_api)
                raise RuntimeError("registration failed")

        class UnregisteringExplodingPolicy(StubPolicy):
            def on_policy_registered(self, special_api):
                super().on_policy_registered(special_api)
                special_api.unregister_policy()
                raise RuntimeError("registration failed")

        for policy_class in (ExplodingPolicy, UnregisteringExplodingPolicy):
            with self.assertRaisesRegex(RuntimeError, "registration failed"):
                vs.register_policy(policy_class())

            self.assertFalse(vs.has_policy())

            with _with_policy():
                self.assertTrue(vs.has_policy())
            self.assertFalse(vs.has_policy())

    @subprocess_runner
    def test_requests_without_current_environment_complete_and_track(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                clip = vs.core.std.BlankClip(width=16, height=16, length=2)

            # no environment is current here: the request is keyed on the node's own
            # environment, the callback simply runs without switching, and the frames
            # still belong to that environment's teardown
            fut = clip.get_frame_async(0)
            self.assertIsInstance(fut.result(timeout=10), vs.VideoFrame)
            frame = clip.get_frame(1)
            self.assertFalse(frame.closed)

            pol._api.destroy_environment(env)
            self.assertTrue(frame.closed)
            with self.assertRaises(RuntimeError):
                frame.copy()

    @subprocess_runner
    def test_current_environment_waits_for_foreign_requests(self):
        lock = Lock()

        def modify_func(n, f):
            with lock:
                return f

        with _with_policy() as pol:
            env_a = pol._api.create_environment()
            env_b = pol._api.create_environment()
            wrapped_a = pol._api.wrap_environment(env_a)
            wrapped_b = pol._api.wrap_environment(env_b)

            with wrapped_a.use():
                core = vs.core.core
                clip = core.std.BlankClip(width=16, height=16, length=2)
                clip = core.std.ModifyFrame(clip, clip, modify_func)

            thread = threading.Thread(target=lambda: pol._api.destroy_environment(env_b))
            with _holding(lock, thread):
                with wrapped_b.use():
                    fut = clip.get_frame_async(0)

                # the request was made from B on a node owned by A, so destroying B has to
                # wait for it just like destroying A would
                thread.start()
                thread.join(0.25)
                self.assertIsNotNone(wrapped_b.env())
                self.assertTrue(thread.is_alive())

            self.assertIsInstance(fut.result(), vs.VideoFrame)
            pol._api.destroy_environment(env_a)

    @subprocess_runner
    def test_failed_registration_destroys_created_environments(self):
        destroyed = [False]

        def on_destroy():
            destroyed[0] = True

        class CreatingExplodingPolicy(StubPolicy):
            def on_policy_registered(self, special_api):
                super().on_policy_registered(special_api)
                env = special_api.create_environment()
                with special_api.wrap_environment(env).use():
                    vs.register_on_destroy(on_destroy)
                raise RuntimeError("registration failed")

        with self.assertRaisesRegex(RuntimeError, "registration failed"):
            vs.register_policy(CreatingExplodingPolicy())

        self.assertTrue(destroyed[0])
        self.assertFalse(vs.has_policy())

    @subprocess_runner
    def test_invoking_without_environment_does_not_leak(self):
        messages = []

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                core = vs.core.core
                core.add_log_handler(lambda level, msg: messages.append(msg))

            # invoking with no current environment cannot hand back a node and fails,
            # but the filter instance it created must still be released with the map
            with self.assertRaisesRegex(vs.Error, "No environment is currently activated"):
                core.std.BlankClip(width=16, height=16, length=1)

            pol._api.destroy_environment(env)

        self.assertFalse([m for m in messages if "filter instance" in m], messages)

    @subprocess_runner
    def test_output_rejects_mismatched_alpha(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                clip = vs.core.std.BlankClip(width=16, height=16, length=1, format=vs.GRAY8)
                alpha = vs.core.std.BlankClip(width=16, height=16, length=1, format=vs.GRAY16)
                with self.assertRaisesRegex(ValueError, "same dimensions and bit depth"):
                    vs.core.std.ClipToProp(clip, alpha).output(io.BytesIO())

                alpha = vs.core.std.BlankClip(width=32, height=16, length=1, format=vs.GRAY8)
                with self.assertRaisesRegex(ValueError, "same dimensions and bit depth"):
                    vs.core.std.ClipToProp(clip, alpha).output(io.BytesIO())

                alpha = vs.core.std.BlankClip(width=16, height=16, length=1, format=vs.GRAY8)
                out = io.BytesIO()
                vs.core.std.ClipToProp(clip, alpha).output(out)
                self.assertEqual(len(out.getvalue()), 16 * 16 * 2)

    @subprocess_runner
    def test_frame_props_keep_embedded_nul(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            with pol._api.wrap_environment(env).use():
                frame = vs.core.std.BlankClip(width=16, height=16, length=1).get_frame(0).copy()
                frame.props["text"] = "a\0b"
                self.assertEqual(frame.props["text"], "a\0b")
            pol._api.destroy_environment(env)

    @subprocess_runner
    def test_create_video_frame_validates_dimensions(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            with pol._api.wrap_environment(env).use():
                # zero or negative sizes abort the process in the core and unaligned
                # ones yield frames no node may carry, so both are refused up front
                for width, height in ((0, 16), (16, -1), (15, 16), (16, 15)):
                    with self.assertRaises(ValueError):
                        vs.core.create_video_frame(vs.YUV420P8, width, height)
                frame = vs.core.create_video_frame(vs.YUV420P8, 16, 16)
                self.assertEqual((frame.width, frame.height), (16, 16))
            pol._api.destroy_environment(env)

    @subprocess_runner
    def test_frame_props_iteration_survives_close(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            with pol._api.wrap_environment(env).use():
                frame = vs.core.std.BlankClip(width=16, height=16, length=1).get_frame(0).copy()
                frame.props["a"] = 1
                frame.props["b"] = 2
                expected = sorted(frame.props)
                keys = iter(frame.props)
                frame.close()
                self.assertEqual(sorted(keys), expected)
                self.assertLessEqual({"a", "b"}, set(expected))
            pol._api.destroy_environment(env)

    @subprocess_runner
    def test_output_releases_frames_on_error(self):
        messages = []

        def progress(done, total):
            if done == 2:
                raise RuntimeError("stop")

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                core = vs.core.core
                core.add_log_handler(lambda level, msg: messages.append(msg))
                clip = core.std.BlankClip(width=16, height=16, length=16)
                with self.assertRaisesRegex(RuntimeError, "stop"):
                    clip.output(io.BytesIO(), progress_update=progress, prefetch=2)

            # the frames the interrupted output still had in flight were released with
            # its generator, so nothing is left allocated when the core goes away
            pol._api.destroy_environment(env)

        self.assertFalse([m for m in messages if "still allocated" in m or "filter instance" in m], messages)

    @subprocess_runner
    def test_functions_invoke_on_their_own_core(self):
        with _with_policy() as pol:
            env_a = pol._api.create_environment()
            env_b = pol._api.create_environment()
            with pol._api.wrap_environment(env_a).use():
                blank_a = vs.core.std.BlankClip

            with pol._api.wrap_environment(env_b).use():
                # a function captured in A invoked while B is current still runs on A's
                # core and hands back a node that can be used right away
                clip = blank_a(width=16, height=16, length=1)
                self.assertEqual((clip.width, clip.height), (16, 16))
                clip.get_frame(0)

            with self.assertRaisesRegex(vs.Error, "No environment is currently activated"):
                blank_a(width=16, height=16, length=1)

            pol._api.destroy_environment(env_a)
            pol._api.destroy_environment(env_b)

    @subprocess_runner
    def test_get_outputs_keeps_dict_semantics(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use():
                clip = vs.core.std.BlankClip(width=16, height=16, length=1)
                outputs = vs.get_outputs()
                keys = outputs.keys()

                self.assertEqual(outputs, {})
                clip.set_output(3)

                # live views and dict equality, what a read-only view of a dict gives
                self.assertIn(3, keys)
                self.assertEqual(outputs, {3: vs.VideoOutputTuple(clip, None, 0)})
                self.assertIs(vs.get_output(3).clip, clip)
                with self.assertRaises(TypeError):
                    outputs[3] = clip

                vs.clear_output(3)
                self.assertNotIn(3, keys)
                self.assertEqual(vs.get_outputs(), {})


class ShortWriteSink(io.RawIOBase):
    """A raw stream that takes at most seven bytes per write, as a raw file may."""

    def __init__(self):
        super().__init__()
        self.data = bytearray()

    def writable(self):
        return True

    def write(self, b):
        b = bytes(b)[:7]
        self.data += b
        return len(b)


class OutputStreamTest(unittest.TestCase):
    def test_video_output_completes_short_writes(self):
        clip = vs.core.std.BlankClip(format=vs.GRAY8, width=8, height=8, length=2, color=[7])
        sink = ShortWriteSink()
        clip.output(sink)
        self.assertEqual(bytes(sink.data), bytes([7]) * 128)

    def test_audio_output_completes_short_writes(self):
        clip = vs.core.std.BlankAudio(channels=[vs.FRONT_LEFT, vs.FRONT_RIGHT], bits=16, length=16)
        sink = ShortWriteSink()
        clip.output(sink)
        self.assertEqual(len(sink.data), 16 * 2 * 2)


class DeferredDestroyTest(unittest.TestCase):
    @subprocess_runner
    def test_destroy_from_own_frame_callback(self):
        lock = Lock()

        def modify_func(n, f):
            with lock:
                return f

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            seen_alive = []

            def dispose(f):
                # from inside the environment's own frame callback: the call returns at
                # once and hands the teardown to a helper thread
                pol._api.destroy_environment(env)
                seen_alive.append(wrapped.alive)

            with _holding(lock):
                with wrapped.use():
                    core = vs.core.core
                    clip = core.std.BlankClip(width=16, height=16, length=1)
                    clip = core.std.ModifyFrame(clip, clip, modify_func)
                    fut = clip.get_frame_async(0)
                    fut.add_done_callback(dispose)

            self.assertIsInstance(fut.result(timeout=10), vs.VideoFrame)

            for _ in range(200):
                if not wrapped.alive:
                    break
                time.sleep(0.05)
            self.assertFalse(wrapped.alive)
            self.assertEqual(seen_alive, [True])

    @subprocess_runner
    def test_destroy_caller_environment_from_frame_callback(self):
        # a node of one environment requested while another is current: the callback runs in
        # the caller's environment, and destroying that one from inside it must defer as well
        lock = Lock()

        def modify_func(n, f):
            with lock:
                return f

        with _with_policy() as pol:
            owner = pol._api.create_environment()
            caller = pol._api.create_environment()
            wrapped_caller = pol._api.wrap_environment(caller)

            with pol._api.wrap_environment(owner).use():
                core = vs.core.core
                clip = core.std.BlankClip(width=16, height=16, length=1)
                clip = core.std.ModifyFrame(clip, clip, modify_func)

            seen_alive = []

            def dispose(f):
                pol._api.destroy_environment(caller)
                seen_alive.append(wrapped_caller.alive)

            with _holding(lock):
                with wrapped_caller.use():
                    fut = clip.get_frame_async(0)
                    fut.add_done_callback(dispose)

            self.assertIsInstance(fut.result(timeout=10), vs.VideoFrame)

            for _ in range(200):
                if not wrapped_caller.alive:
                    break
                time.sleep(0.05)
            self.assertFalse(wrapped_caller.alive)
            self.assertEqual(seen_alive, [True])
            pol._api.destroy_environment(owner)


class DestroyCallbackTest(unittest.TestCase):
    @subprocess_runner
    def test_baseexception_from_a_destroy_callback_still_tears_down(self):
        # SystemExit and friends used to escape teardown, leaving the environment marked as
        # destroying with its nodes, frames and core alive and no way to finish the job
        def boom():
            raise SystemExit(7)

        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)
            with wrapped.use():
                core = vs.core.core
                clip = core.std.BlankClip(width=16, height=16, length=1)
                frame = clip.get_frame(0)
                vs.register_on_destroy(boom)

            with self.assertRaises(SystemExit):
                pol._api.destroy_environment(env)

            self.assertFalse(wrapped.alive)
            self.assertTrue(frame.closed)

    @subprocess_runner
    def test_rejected_filter_creation_does_not_strand_the_input(self):
        # resize hands its instance to the factory; a rejected VSVideoInfo used to leave the
        # instance, and the input node reference it holds, alive for the life of the process
        messages = []
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)
            with wrapped.use():
                core = vs.core.core
                core.add_log_handler(lambda level, msg: messages.append(msg))
                clip = core.std.BlankClip(format=vs.GRAY8, width=16, height=16, length=1)
                with self.assertRaises(vs.Error):
                    core.resize.Point(clip, width=0)
                del clip
            pol._api.destroy_environment(env)
            self.assertFalse(wrapped.alive)
        self.assertEqual([m for m in messages if "still exist" in m], [])


if __name__ == "__main__":
    unittest.main()
