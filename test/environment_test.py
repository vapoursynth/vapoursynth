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


def _wrap_with(func):
    func(EnvironmentTest())


def subprocess_runner(func):
    def _wrapper(self):
        with ProcessPoolExecutor(max_workers=1, mp_context=multiprocessing.get_context("spawn")) as executor:
            executor.submit(_wrap_with, func).result()
    return _wrapper

def t_environment_use_unsets_environment_on_exit(self):
    with _with_policy() as pol:
        env = pol._api.create_environment()
        wrapped = pol._api.wrap_environment(env)

        with self.assertRaises(RuntimeError):
            vs.get_current_environment()

        with wrapped.use():
            print(vs.get_current_environment())

        with self.assertRaises(RuntimeError):
            print(vs.get_current_environment())


def t_environment_use_restores_environment_on_exit(self):
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


class EnvironmentTest(unittest.TestCase):

    test_environment_use_unsets_environment_on_exit = subprocess_runner(t_environment_use_unsets_environment_on_exit)
    test_environment_use_restores_environment_on_exit = subprocess_runner(t_environment_use_restores_environment_on_exit)

