import contextlib
import gc
import multiprocessing
import unittest
import weakref
from concurrent.futures import ProcessPoolExecutor

import vapoursynth as vs


class StubPolicy(vs.EnvironmentPolicy):
    def __init__(self) -> None:
        self._current = None
        self._api = None

    def on_policy_registered(self, special_api):
        self._api = special_api
        self._current = None

    def on_policy_cleared(self):
        assert self._api is not None
        self._current = None

    def get_current_environment(self):
        return self._current

    def set_environment(self, environment):
        self._current = environment


@contextlib.contextmanager
def _with_policy():
    pol = StubPolicy()
    vs.register_policy(pol)
    try:
        yield pol
    finally:
        pol._api.unregister_policy()


test_functions = {}
counter = 0


def _wrap_with(ident):
    func = test_functions[ident]
    return func(EnvironmentTest())


def subprocess_runner(func):
    global counter
    my_counter = counter
    counter += 1
    test_functions[my_counter] = func

    def _wrapper(self):
        with ProcessPoolExecutor(max_workers=1, mp_context=multiprocessing.get_context("spawn")) as executor:
            executor.submit(_wrap_with, my_counter).result()

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
            self.assertIsNotNone(pol._api.get_core_ptr(env))

    @subprocess_runner
    def test_environment_use_unsets_environment_on_exit(self):
        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with self.assertRaises(RuntimeError):
                vs.get_current_environment()

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


if __name__ == "__main__":
    unittest.main()
