"""Support for running the CPU filter tests against the GPU paths.

The tests in this directory build ordinary CPU graphs and assert on pixel values. The GPU
variants reuse those test bodies unchanged: a mixin wraps every std.* call so that node
arguments are uploaded first and a GPU resident result is downloaded before the test reads
it, which makes each wrapped call exercise the filter's GPU path with the CPU expectations.

What the wrapper does NOT do is force everything onto the GPU:

  * a call with no node arguments (BlankClip and the other sources) runs untouched;
  * a filter that declines its GPU path by policy -- the message says to insert
    GPUDownload/GPUUpload, or that an expression has no GPU kernel -- falls back to the
    plain CPU call, since a decline is an documented answer rather than a defect;
  * a filter with no GPU path at all comes back CPU resident and is passed through.

Only unexpected errors propagate, so a real GPU defect fails the test instead of hiding in
a fallback. A test where not a single call ended up on the GPU skips itself in tearDown:
running it again would only test the CPU twice.

Only the std namespace is wrapped. Reference graphs built through resize keep validating
the GPU against zimg rather than against itself, and bound-style invocations
(clip.std.Filter()) intentionally bypass the wrapper for the same reason: everything that
is not the filter under test stays on the CPU.
"""

import os
import unittest

import vapoursynth as vs


def _probe_gpu():
    # The explicit opt-out exists for CI: a runner that grows a software Vulkan
    # implementation (lavapipe advertises 1.4 these days) would otherwise pass the probe
    # and run the whole GPU suite on llvmpipe. Named after the VS_VULKAN_* switches the
    # core itself reads.
    if os.environ.get("VS_TEST_NO_GPU"):
        return False
    try:
        vs.core.vulkan_device_info
        return True
    except Exception:
        return False


HAVE_GPU = _probe_gpu()

# The creation-time refusals that mean "this combination deliberately has no GPU path",
# phrased by the core itself. Anything else a GPU invocation raises is a finding.
DECLINE_MARKERS = ("insert GPUDownload", "insert GPUUpload", "no GPU kernel")


def _is_decline(err):
    msg = str(err)
    return any(marker in msg for marker in DECLINE_MARKERS)


class _WrappedFunction:
    """One std.* function; uploads node arguments and downloads a GPU resident result."""

    def __init__(self, owner, fn):
        self._owner = owner
        self._fn = fn

    @property
    def signature(self):
        return self._fn.signature

    @property
    def name(self):
        return self._fn.name

    def _upload(self, value, found):
        if isinstance(value, vs.VideoNode):
            found.append(True)
            return vs.core.std.GPUUpload(value) if not value.gpu_resident else value
        if isinstance(value, (list, tuple)):
            return type(value)(self._upload(v, found) for v in value)
        return value

    def __call__(self, *args, **kwargs):
        found = []
        up_args = tuple(self._upload(a, found) for a in args)
        up_kwargs = {k: self._upload(v, found) for k, v in kwargs.items()}
        if not found:
            return self._fn(*args, **kwargs)
        try:
            out = self._fn(*up_args, **up_kwargs)
        except vs.Error as e:
            if _is_decline(e):
                return self._fn(*args, **kwargs)
            raise
        if isinstance(out, vs.VideoNode) and out.gpu_resident:
            self._owner._gpu_calls += 1
            return vs.core.std.GPUDownload(out)
        return out


# Filters whose whole point is a host side Python callback touching pixel data; forcing
# their inputs onto the GPU only makes the callback fail when it reads them. They stay
# plain and their output is uploaded by whatever wrapped filter consumes it next.
PASSTHROUGH = frozenset({"ModifyFrame", "FrameEval"})


class _WrappedNamespace:
    def __init__(self, owner, plugin):
        self._owner = owner
        self._plugin = plugin

    def __getattr__(self, name):
        fn = getattr(self._plugin, name)
        if name in PASSTHROUGH:
            return fn
        return _WrappedFunction(self._owner, fn)


class _WrappedCore:
    """Forwards everything to the real core; only .std comes back wrapped."""

    def __init__(self, owner, core):
        object.__setattr__(self, "_owner", owner)
        object.__setattr__(self, "_core", core)

    def __getattr__(self, name):
        if name == "std":
            return _WrappedNamespace(self._owner, self._core.std)
        return getattr(self._core, name)

    def __setattr__(self, name, value):
        setattr(self._core, name, value)


class GPUTestMixin:
    """Mix in ahead of a CPU TestCase to run its tests through the GPU paths.

    Runs the original setUp first, then replaces self.core with the wrapping proxy and
    rewraps any std functions the original setUp bound onto the instance -- the two ways
    the tests in this directory reach their filters.
    """

    def setUp(self):
        self._gpu_calls = 0
        super().setUp()
        real = self.core
        self.core = _WrappedCore(self, real)
        for name, value in list(self.__dict__.items()):
            if isinstance(value, vs.Function):
                setattr(self, name, _WrappedFunction(self, value))

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            calls, self._gpu_calls = self._gpu_calls, 0
            if calls == 0:
                raise unittest.SkipTest("nothing in this test has a GPU path")
