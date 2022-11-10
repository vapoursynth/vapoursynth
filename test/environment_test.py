import unittest
import contextlib
import multiprocessing
from concurrent.futures import ProcessPoolExecutor

from vapoursynth import core
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


class EnvironmentTest(unittest.TestCase):
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
    def test_environment_clearing_runs_callbacks(self):
        f1_run = [False]
        def f1():
            f1_run[0] = True

        f2_run = [False]
        def f2():
            f2_run[0] = True


        with _with_policy() as pol:
            env = pol._api.create_environment()
            wrapped = pol._api.wrap_environment(env)

            with wrapped.use() as pol:
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
