#  Copyright (c) 2012-2025 Fredrik Mellbin
#
#  This file is part of VapourSynth.
#
#  VapourSynth is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2.1 of the License, or (at your option) any later version.
#
#  VapourSynth is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with VapourSynth; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
""" This is the VapourSynth module implementing the Python bindings. """

cimport vapoursynth
include 'vsconstants.pxd'
from vsscript_internal cimport VSScript
cimport cython.parallel
from cython cimport view, final
from libc.stdint cimport intptr_t, int16_t, uint16_t, int32_t, uint32_t
from cpython.buffer cimport PyBUF_SIMPLE
from cpython.buffer cimport PyBuffer_FillInfo
from cpython.buffer cimport PyBuffer_Release
from cpython.memoryview cimport PyMemoryView_FromObject
from cpython.number cimport PyIndex_Check
from cpython.number cimport PyNumber_Index
from cpython.ref cimport Py_INCREF, Py_DECREF
import os
import enum
import ctypes
import threading
import traceback
import gc
import sys
import inspect
import weakref
import atexit
import contextlib
import logging
import functools
import typing
import traceback
import warnings
from threading import local as ThreadLocal, Lock, RLock
from types import MappingProxyType
from collections import namedtuple
from collections.abc import Iterable, Mapping
from concurrent.futures import Future
from fractions import Fraction

__all__ = [
  'GRAY',
    'GRAY8', 'GRAY9', 'GRAY10', 'GRAY12', 'GRAY14', 'GRAY16', 'GRAY32', 'GRAYH', 'GRAYS',
  'RGB',
    'RGB24', 'RGB27', 'RGB30', 'RGB36', 'RGB42', 'RGB48', 'RGBH', 'RGBS',
  'YUV',
    'YUV410P8',
    'YUV411P8',
    'YUV420P8', 'YUV420P9', 'YUV420P10', 'YUV420P12', 'YUV420P14', 'YUV420P16',
    'YUV422P8', 'YUV422P9', 'YUV422P10', 'YUV422P12', 'YUV422P14', 'YUV422P16',
    'YUV440P8',
    'YUV444P8', 'YUV444P9', 'YUV444P10', 'YUV444P12', 'YUV444P14', 'YUV444P16', 'YUV420PH', 'YUV420PS', 'YUV422PH', 'YUV422PS', 'YUV444PH', 'YUV444PS',
  'NONE',

  'FLOAT', 'INTEGER',

  'RANGE_FULL', 'RANGE_LIMITED',

  'CHROMA_LEFT', 'CHROMA_CENTER', 'CHROMA_TOP_LEFT', 'CHROMA_TOP', 'CHROMA_BOTTOM_LEFT', 'CHROMA_BOTTOM',

  'FIELD_PROGRESSIVE', 'FIELD_TOP', 'FIELD_BOTTOM',

  'get_output', 'get_outputs',
  'clear_output', 'clear_outputs',

  'core',
]

class MediaType(IntEnum):
    VIDEO = mtVideo
    AUDIO = mtAudio

class ColorFamily(IntEnum):
    UNDEFINED = cfUndefined
    GRAY = cfGray
    RGB = cfRGB
    YUV = cfYUV

class SampleType(IntEnum):
    INTEGER = stInteger
    FLOAT = stFloat

class PresetVideoFormat(IntEnum):
    NONE = pfNone

    GRAY8 = pfGray8
    GRAY9 = pfGray9
    GRAY10 = pfGray10
    GRAY12 = pfGray12
    GRAY14 = pfGray14
    GRAY16 = pfGray16
    GRAY32 = pfGray32

    GRAYH = pfGrayH
    GRAYS = pfGrayS

    YUV410P8 = pfYUV410P8
    YUV411P8 = pfYUV411P8
    YUV440P8 = pfYUV440P8

    YUV420P8 = pfYUV420P8
    YUV422P8 = pfYUV422P8
    YUV444P8 = pfYUV444P8

    YUV420P9 = pfYUV420P9
    YUV422P9 = pfYUV422P9
    YUV444P9 = pfYUV444P9

    YUV420P10 = pfYUV420P10
    YUV422P10 = pfYUV422P10
    YUV444P10 = pfYUV444P10

    YUV420P12 = pfYUV420P12
    YUV422P12 = pfYUV422P12
    YUV444P12 = pfYUV444P12

    YUV420P14 = pfYUV420P14
    YUV422P14 = pfYUV422P14
    YUV444P14 = pfYUV444P14

    YUV420P16 = pfYUV420P16
    YUV422P16 = pfYUV422P16
    YUV444P16 = pfYUV444P16

    YUV420PH = pfYUV420PH
    YUV420PS = pfYUV420PS
    
    YUV422PH = pfYUV422PH
    YUV422PS = pfYUV422PS

    YUV444PH = pfYUV444PH
    YUV444PS = pfYUV444PS

    RGB24 = pfRGB24
    RGB27 = pfRGB27
    RGB30 = pfRGB30
    RGB36 = pfRGB36
    RGB42 = pfRGB42
    RGB48 = pfRGB48

    RGBH = pfRGBH
    RGBS = pfRGBS

class FilterMode(IntEnum):
    PARALLEL = fmParallel
    PARALLEL_REQUESTS = fmParallelRequests
    UNORDERED = fmUnordered
    FRAME_STATE = fmFrameState 

class AudioChannels(IntEnum):
    FRONT_LEFT = acFrontLeft
    FRONT_RIGHT = acFrontRight
    FRONT_CENTER = acFrontCenter
    LOW_FREQUENCY = acLowFrequency
    BACK_LEFT = acBackLeft
    BACK_RIGHT = acBackRight
    FRONT_LEFT_OF_CENTER = acFrontLeftOFCenter
    FRONT_RIGHT_OF_CENTER = acFrontRightOFCenter
    BACK_CENTER = acBackCenter
    SIDE_LEFT = acSideLeft
    SIDE_RIGHT = acSideRight
    TOP_CENTER = acTopCenter
    TOP_FRONT_LEFT = acTopFrontLeft
    TOP_FRONT_CENTER = acTopFrontCenter
    TOP_FRONT_RIGHT = acTopFrontRight
    TOP_BACK_LEFT = acTopBackLeft
    TOP_BACK_CENTER = acTopBackCenter
    TOP_BACK_RIGHT = acTopBackRight
    STEREO_LEFT = acStereoLeft
    STEREO_RIGHT = acStereoRight
    WIDE_LEFT = acWideLeft
    WIDE_RIGHT = acWideRight
    SURROUND_DIRECT_LEFT = acSurroundDirectLeft
    SURROUND_DIRECT_RIGHT = acSurroundDirectRight
    LOW_FREQUENCY2 = acLowFrequency2

class MessageType(IntFlag):
    MESSAGE_TYPE_DEBUG = mtDebug
    MESSAGE_TYPE_INFORMATION = mtInformation
    MESSAGE_TYPE_WARNING = mtWarning
    MESSAGE_TYPE_CRITICAL = mtCritical
    MESSAGE_TYPE_FATAL = mtFatal

class CoreCreationFlags(IntFlag):
    ENABLE_GRAPH_INSPECTION = ccfEnableGraphInspection
    DISABLE_AUTO_LOADING = ccfDisableAutoLoading
    DISABLE_LIBRARY_UNLOADING = ccfDisableLibraryUnloading

# In this file
globals().update(MediaType.__members__)
globals().update(ColorFamily.__members__)
globals().update(SampleType.__members__)
globals().update(PresetVideoFormat.__members__)
globals().update(FilterMode.__members__)
globals().update(AudioChannels.__members__)
globals().update(MessageType.__members__)
globals().update(CoreCreationFlags.__members__)

# From vsconstants.pxd
globals().update(ColorRange.__members__)
globals().update(ChromaLocation.__members__)
globals().update(FieldBased.__members__)
globals().update(MatrixCoefficients.__members__)
globals().update(TransferCharacteristics.__members__)
globals().update(ColorPrimaries.__members__)

class VapourSynthVersion(typing.NamedTuple):
    release_major: int
    release_minor: int

    def __str__(self):
        if self.release_minor:
            return f'R{self.release_major}.{self.release_minor}'
        return f'R{self.release_major}'

class VapourSynthAPIVersion(typing.NamedTuple):
    api_major: int
    api_minor: int

    def __str__(self):
        return f'R{self.api_major}.{self.api_minor}'


__version__ = VapourSynthVersion(VS_CURRENT_RELEASE, 0)
__api_version__ = VapourSynthAPIVersion(VAPOURSYNTH_API_MAJOR, VAPOURSYNTH_API_MINOR)

@final
cdef class EnvironmentData(object):
    cdef bint alive
    cdef Core core
    cdef object on_destroy
    cdef dict outputs

    cdef int coreCreationFlags
    cdef VSLogHandle* log

    cdef object env_locals

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    def __dealloc__(self):
        if self.alive:
            warnings.warn(
                "An environment is getting collected "
                "without calling EnvironmentPolicyAPI.destroy_environment(). "
                "This skips some lifecycle callbacks (e.g. register_on_destroy). "
                "This might cause libraries to misbehave.",
                RuntimeWarning
            )
        _unset_logger(self)


class EnvironmentPolicy(object):

    def on_policy_registered(self, special_api):
        pass

    def on_policy_cleared(self):
        pass

    def get_current_environment(self):
        raise NotImplementedError

    def set_environment(self, environment):
        raise NotImplementedError

    def is_alive(self, environment):
        cdef EnvironmentData env = <EnvironmentData>environment
        return env.alive


@final
cdef class StandaloneEnvironmentPolicy:
    cdef EnvironmentData _environment
    cdef object _api
    cdef object _logger
    cdef int _flags

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    def _on_log_message(self, level, msg):
        levelmap = {
            MessageType.MESSAGE_TYPE_DEBUG: logging.DEBUG,
            MessageType.MESSAGE_TYPE_INFORMATION: logging.INFO,
            MessageType.MESSAGE_TYPE_WARNING: logging.WARN,
            MessageType.MESSAGE_TYPE_CRITICAL: logging.ERROR,
            MessageType.MESSAGE_TYPE_FATAL: logging.FATAL
        }
        self._logger.log(levelmap[level], msg)

    def on_policy_registered(self, api):
        self._api = api
        self._logger = logging.getLogger("vapoursynth")
        self._environment = api.create_environment(self._flags)
        api.set_logger(self._environment, self._on_log_message)

    def on_policy_cleared(self):
        self._api.destroy_environment(self._environment)
        self._environment = None
        self._api = None

    def get_current_environment(self):
        return self._environment

    def set_environment(self, environment):
        return self._environment

    def is_alive(self, environment):
        return environment is self._environment


# Internal holder of the current policy.
cdef object _policy = None

cdef const VSAPI *_vsapi = NULL


cdef void _set_logger(EnvironmentData env, VSLogHandler handler, VSLogHandlerFree free, void *userData):
    vsscript_get_core_internal(env)
    _unset_logger(env)
    env.log = env.core.funcs.addLogHandler(handler, free, userData, env.core.core)

cdef void _unset_logger(EnvironmentData env):
    if env.log == NULL or env.core is None:
        env.log = NULL # if the core has been freed then so has the log as well
        return

    env.core.funcs.removeLogHandler(env.log, env.core.core)
    env.log = NULL


cdef void __stdcall _logCb(int msgType, const char *msg, void *userData) noexcept nogil:
    with gil:
        message = msg.decode("utf-8")
        (<object>userData)(msgType, message)

cdef void __stdcall _logFree(void* userData) noexcept nogil:
    with gil:
        Py_DECREF(<object>userData)

@final
cdef class EnvironmentPolicyAPI:
    # This must be a weak-ref to prevent a cyclic dependency that happens if the API
    # is stored within an EnvironmentPolicy-instance.
    cdef object _target_policy

    cdef object _lock
    cdef object _known_environments
    # Sadly, weakref has no WeakSet.
    # So we use a counter to fake a WeakSet.
    cdef int _known_environments_counter

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    cdef ensure_policy_matches(self):
        if _policy is not self._target_policy():
            raise ValueError("The currently activated policy does not match the bound policy. Was the environment unregistered?")

    def wrap_environment(self, environment_data):
        self.ensure_policy_matches()
        if not isinstance(environment_data, EnvironmentData):
            raise ValueError("environment_data must be an EnvironmentData instance.")
        return use_environment(<EnvironmentData>environment_data)

    def create_environment(self, int flags = 0):
        self.ensure_policy_matches()

        cdef EnvironmentData env = EnvironmentData.__new__(EnvironmentData)
        env.core = None
        env.log = NULL
        env.outputs = {}
        env.coreCreationFlags = flags
        env.on_destroy = []
        env.env_locals = weakref.WeakKeyDictionary()
        env.alive = True

        with self._lock:
            counter = self._known_environments_counter
            self._known_environments_counter += 1
            self._known_environments[counter] = env

        return env

    def set_logger(self, env, logger):
        Py_INCREF(logger)
        _set_logger(env, _logCb, _logFree, <void *>logger)

    def get_vapoursynth_api(self, int version):
        self.ensure_policy_matches()
        return ctypes.c_void_p(<uintptr_t>getVapourSynthAPI(version))

    def get_core_ptr(self, EnvironmentData environment_data):
        self.ensure_policy_matches()
        return ctypes.c_void_p(<uintptr_t>environment_data.core.core)

    def destroy_environment(self, EnvironmentData env):
        self.ensure_policy_matches()
        if not env.alive:
            return

        callbacks = env.on_destroy
        env.on_destroy = None

        with use_environment(env).use():
            for callback in callbacks:
                try:
                    callback()
                except Exception as e:
                    formatted = ''.join(traceback.format_exception(type(e), e, e.__traceback__))
                    env.core.log_message(MessageType.MESSAGE_TYPE_CRITICAL, formatted)

        _unset_logger(env)
        env.core = None
        env.log = NULL
        env.outputs = {}
        env.env_locals.clear()
        env.env_locals = None
        env.alive = False

    def unregister_policy(self):
        self.ensure_policy_matches()
        for environment in self._known_environments.values():
            self.destroy_environment(environment)
        clear_policy(delay=False)

    def __repr__(self):
        target = self._target_policy()
        if target is None:
            return f"<EnvironmentPolicyAPI bound to <garbage collected> (unregistered)>"
        elif _policy is not target:
            return f"<EnvironmentPolicyAPI bound to {target!r} (unregistered)>"
        else:
            return f"<EnvironmentPolicyAPI bound to {target!r}>"


def register_policy(policy):
    global _policy
    if _policy is not None:
        raise RuntimeError("There is already a policy registered.")
    _policy = policy

    # Expose Additional API-calls to the newly registered Environment-policy.
    cdef EnvironmentPolicyAPI _api = EnvironmentPolicyAPI.__new__(EnvironmentPolicyAPI)
    _api._target_policy = weakref.ref(_policy)
    _api._known_environments = weakref.WeakValueDictionary()
    _api._lock = threading.Lock()
    _policy.on_policy_registered(_api)


def _try_enable_introspection(version=None):
    global _policy
    if _policy is not None:
        return False

    if version != 0:
        return False

    cdef StandaloneEnvironmentPolicy standalone_policy = StandaloneEnvironmentPolicy.__new__(StandaloneEnvironmentPolicy)
    standalone_policy._flags = ccfEnableGraphInspection;
    register_policy(standalone_policy)

    return True


## DO NOT EXPOSE THIS FUNCTION TO PYTHON-LAND!
cdef get_policy():
    global _policy
    cdef StandaloneEnvironmentPolicy standalone_policy

    if _policy is None:
        standalone_policy = StandaloneEnvironmentPolicy.__new__(StandaloneEnvironmentPolicy)
        standalone_policy._flags = 0
        register_policy(standalone_policy)

    return _policy

def has_policy():
    return _policy is not None

cdef clear_policy(delay=False):
    global _policy
    old_policy = _policy

    if not delay:
        _policy = None

    if old_policy is not None:
        old_policy.on_policy_cleared()

    if delay:
        _policy = None

    return old_policy

cdef EnvironmentData _env_current():
    return get_policy().get_current_environment()


# Make sure the policy is cleared at exit.
atexit.register(lambda: clear_policy(delay=True))


def register_on_destroy(callback):
    cdef EnvironmentData env = get_policy().get_current_environment()
    if env.on_destroy is None:
        raise ValueError("The environment is already being destroyed.")

    env.on_destroy.append(callback)

def unregister_on_destroy(callback):
    cdef EnvironmentData env = get_policy().get_current_environment()
    if env.on_destroy is None:
        raise ValueError("The environment is already being destroyed.")

    env.on_destroy.remove(callback)


@final
cdef class _FastManager(object):
    cdef EnvironmentData target
    cdef EnvironmentData previous

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    def __enter__(self):
        self.previous = get_policy().get_current_environment()
        if self.target is not None:
            get_policy().set_environment(self.target)
            self.target = None

    def __exit__(self, *_):
        policy = get_policy()
        if policy.is_alive(self.previous):
            policy.set_environment(self.previous)
        else:
            policy.set_environment(None)
        self.previous = None


cdef class Environment(object):
    cdef readonly object env

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    @property
    def alive(self):
        env = self.get_env()
        if env is None:
            return False
        return get_policy().is_alive(env)

    @property
    def single(self):
        return self.is_single()

    @classmethod
    def is_single(self):
        return not has_policy() or isinstance(_policy, StandaloneEnvironmentPolicy)

    @property
    def env_id(self):
        if self.single:
            return -1
        return id(self.env)

    cdef EnvironmentData get_env(self):
        return self.env()

    @property
    def active(self):
        env = self.get_env()
        if env is None:
            return None
        return get_policy().get_current_environment() is env

    def copy(self):
        cdef Environment env = Environment.__new__(Environment)
        env.env = self.env
        return env

    def use(self):
        env = self.get_env()
        if env is None:
            raise RuntimeError("The environment is dead.")

        cdef _FastManager ctx = _FastManager.__new__(_FastManager)
        ctx.target = env
        ctx.previous = None
        return ctx

    def __eq__(self, other):
        return other.env_id == self.env_id

    def __repr__(self):
        if self.single:
            return "<Environment (default)>"

        return f"<Environment {id(self.env)} ({('active' if self.active else 'alive') if self.alive else 'dead'})>"


cdef class Local:
    cdef object __weakref__

    def __getattr__(self, key):
        cdef EnvironmentData env = get_policy().get_current_environment()
        values = env.env_locals.setdefault(self, {})
        if key not in values:
            raise AttributeError(key)
        return values[key]

    def __setattr__(self, key, value):
        cdef EnvironmentData env = get_policy().get_current_environment()
        values = env.env_locals.setdefault(self, {})
        values[key] = value

    def __delattr__(self, key):
        cdef EnvironmentData env = get_policy().get_current_environment()
        values = env.env_locals.setdefault(self, {})
        values.pop(key)


cdef Environment use_environment(EnvironmentData env):
    if id is None: raise ValueError("id may not be None.")

    cdef Environment instance = Environment.__new__(Environment)
    instance.env = weakref.ref(env)

    return instance

def get_current_environment():
    env = get_policy().get_current_environment()
    if env is None:
        raise RuntimeError("We are not running inside an environment.")

    vsscript_get_core_internal(env) # Make sure a core is defined
    return use_environment(env)

# Create an empty list whose instance will represent a not passed value.
_EMPTY = []

VideoOutputTuple = namedtuple("VideoOutputTuple", "clip alpha alt_output")

def _construct_type(signature):
    type,*opt = signature.split(":")

    # Handle Arrays.
    if type.endswith("[]"):
        array = True
        type = type[:-2]
    else:
        array = False

    # Handle types
    if type == "vnode":
        type = vapoursynth.VideoNode
    elif type == "anode":
        type = vapoursynth.AudioNode
    elif type == "vframe":
        type = vapoursynth.VideoFrame
    elif type == "aframe":
        type = vapoursynth.AudioFrame
    elif type == "func":
        type = typing.Union[vapoursynth.Func, typing.Callable]
    elif type == "int":
        type = int
    elif type == "float":
        type = float
    elif type == "data":
        type = typing.Union[str, bytes, bytearray]
    else:
        type = typing.Any

    # Make the type a sequence.
    if array:
        type = typing.Union[type, typing.Sequence[type]]

    # Mark an optional type
    if opt:
        type = typing.Optional[type]

    return type

def _construct_parameter(signature):
    if signature == "any":
        return inspect.Parameter(
            "kwargs", inspect.Parameter.VAR_KEYWORD,
            annotation=typing.Any
        )

    name, signature = signature.split(":", 1)
    type = _construct_type(signature)

    __,*opt = signature.split(":")
    if opt:
        default_value = None
    else:
        default_value = inspect.Parameter.empty

    return inspect.Parameter(
        name, inspect.Parameter.POSITIONAL_OR_KEYWORD,
        default=default_value, annotation=type
    )

def construct_signature(signature, return_signature, injected=None, name=None):
    if isinstance(signature, vapoursynth.Function):
        signature = signature.signature

    params = list(
        _construct_parameter(param)
        for param in signature.split(";")
        if param
    )

    if injected and params:
        del params[0]

    return_annotations = list(
        _construct_parameter(rparam)
        for rparam in return_signature.split(";")
        if rparam
    )

    if not return_annotations:
        return_annotation = None
    elif len(return_annotations) == 1:
        return_annotation = return_annotations.pop().annotation
    else:
        ret_dict_name = f'_ReturnDict_{name}' if name else '_ReturnDict'
        return_annotation = typing.TypedDict(
            ret_dict_name, {ret_ann.name: ret_ann.annotation for ret_ann in return_annotations}, total=True
        )
        return_annotation.__module__ = Exception.__module__


    return inspect.Signature(tuple(params), return_annotation=return_annotation)

def _construct_repr_wrap(value):
    if isinstance(value, (enum.Enum, VideoFormat)):
        return value.name

    if isinstance(value, typing.Iterator):
        value = ', '.join(_construct_repr_wrap(v) for v in value)

    to_wrap = isinstance(value, str) and not value.startswith('<') and ' ' in value

    if to_wrap:
        return f'"{value}"'

    return value

def _construct_repr(obj, **kwargs):
    address = f'{id(obj):X}'.rjust(16, "0")

    add_data = ''

    if kwargs:
        add_data += ', '.join(
            f'{key}={_construct_repr_wrap(value)}'
            for key, value in kwargs.items()
        )
        add_data = f' {add_data}'

    return f'<{obj.__class__.__module__}.{obj.__class__.__qualname__} object at 0x{address}{add_data}>'

class Error(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return str(self.value)

    def __repr__(self):
        return repr(self.value)

cdef _get_output_dict(funcname="this function"):
    cdef EnvironmentData env = _env_current()
    if env is None:
        raise Error('Internal environment id not set. %s called from a filter callback?'%funcname)
    return env.outputs

def clear_output(int index = 0):
    cdef dict outputs = _get_output_dict("clear_output")
    try:
        del outputs[index]
    except KeyError:
        pass

def clear_outputs():
    cdef dict outputs = _get_output_dict("clear_outputs")
    outputs.clear()

def get_outputs():
    cdef dict outputs = _get_output_dict("get_outputs")
    return MappingProxyType(outputs)

def get_output(int index = 0):
    return _get_output_dict("get_output")[index]

cdef class FuncData(object):
    cdef object func
    cdef VSCore *core
    cdef EnvironmentData env

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __call__(self, **kwargs):
        return self.func(**kwargs)

cdef FuncData createFuncData(object func, VSCore *core, EnvironmentData env):
    cdef FuncData instance = FuncData.__new__(FuncData)
    instance.func = func
    instance.core = core
    instance.env = env
    return instance

cdef class Func(object):
    cdef const VSAPI *funcs
    cdef VSFunction *ref

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFunction(self.ref)

    def __call__(self, **kwargs):
        cdef VSMap *outm
        cdef VSMap *inm
        cdef const VSAPI *vsapi
        cdef const char *error
        vsapi = getVSAPIInternal()
        outm = self.funcs.createMap()
        inm = self.funcs.createMap()
        try:
            dictToMap(kwargs, inm, NULL, vsapi)
            self.funcs.callFunction(self.ref, inm, outm)
            error = self.funcs.mapGetError(outm)
            if error:
                raise Error(error.decode('utf-8'))
            return mapToDict(outm, True)
        finally:
            vsapi.freeMap(outm)
            vsapi.freeMap(inm)

cdef Func createFuncPython(object func, VSCore *core, const VSAPI *funcs):
    cdef Func instance = Func.__new__(Func)
    instance.funcs = funcs

    cdef EnvironmentData env = _env_current()
    if env is None:
        raise Error('Internal environment id not set. Did the environment die?')
    fdata = createFuncData(func, core, env)

    Py_INCREF(fdata)
    instance.ref = instance.funcs.createFunction(publicFunction, <void *>fdata, freeFunc, core)
    return instance

cdef Func createFuncRef(VSFunction *ref, const VSAPI *funcs):
    cdef Func instance = Func.__new__(Func)
    instance.funcs = funcs
    instance.ref = ref
    return instance


cdef class CallbackData(object):
    cdef const VSAPI *funcs
    cdef object callback

    cdef RawNode node

    cdef EnvironmentData env

    def __init__(self, object node, EnvironmentData env, object callback):
        # Keeps the node alive during the call.
        self.node = node

        self.callback = callback
        self.env = env


cdef createCallbackData(const VSAPI* funcs, RawNode node, object cb):
    cbd = CallbackData(node, _env_current(), cb)
    cbd.funcs = funcs
    return cbd


cdef class FramePtr(object):
    cdef const VSFrame *f
    cdef const VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFrame(self.f)

cdef FramePtr createFramePtr(const VSFrame *f, const VSAPI *funcs):
    cdef FramePtr instance = FramePtr.__new__(FramePtr)
    instance.f = f
    instance.funcs = funcs
    return instance


cdef void __stdcall frameDoneCallback(void *data, const VSFrame *f, int n, VSNode *node, const char *errormsg) noexcept nogil:
    with gil:
        result = error = None
        d = <CallbackData>data

        try:
            if f == NULL:
                error = 'Internal error - no error message.'
                if errormsg != NULL:
                    error = errormsg.decode('utf-8')
                error = Error(error)
            else:
                result = createConstFrame(f, d.funcs, d.node.core.core)

            try:
                with use_environment(d.env).use():
                    d.callback(result, error)
            except:
                traceback.print_exc()
        finally:
            Py_DECREF(d)

cdef object mapToDict(const VSMap *map, bint flatten):
    cdef const VSAPI *funcs = getVSAPIInternal()
    cdef int numKeys = funcs.mapNumKeys(map)
    retdict = {}
    cdef const char *retkey
    cdef int proptype

    for x in range(numKeys):
        retkey = funcs.mapGetKey(map, x)
        proptype = funcs.mapGetType(map, retkey)

        for y in range(funcs.mapNumElements(map, retkey)):
            if proptype == ptInt:
                newval = funcs.mapGetInt(map, retkey, y, NULL)
            elif proptype == ptFloat:
                newval = funcs.mapGetFloat(map, retkey, y, NULL)
            elif proptype == ptData:
                newval = funcs.mapGetData(map, retkey, y, NULL)[:funcs.mapGetDataSize(map, retkey, y, NULL)]
                if funcs.mapGetDataTypeHint(map, retkey, y, NULL) == dtUtf8:
                    newval = newval.decode('utf-8')
            elif proptype == ptVideoNode or proptype == ptAudioNode:
                newval = createNode(funcs.mapGetNode(map, retkey, y, NULL), funcs, _get_core())
            elif proptype == ptVideoFrame or proptype == ptAudioFrame:
                newval = createConstFrame(funcs.mapGetFrame(map, retkey, y, NULL), funcs, _get_core().core)
            elif proptype == ptFunction:
                newval = createFuncRef(funcs.mapGetFunction(map, retkey, y, NULL), funcs)

            if y == 0:
                vval = newval
            elif y == 1:
                vval = [vval, newval]
            else:
                vval.append(newval)
        retdict[retkey.decode('utf-8')] = vval

    if not flatten:
        return retdict
    elif len(retdict) == 0:
        return None
    elif len(retdict) == 1:
        a, b = retdict.popitem()
        return b
    else:
        return retdict

cdef void dictToMap(dict ndict, VSMap *inm, VSCore *core, const VSAPI *funcs) except *:
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]

        if isinstance(val, (str, bytes, bytearray, RawNode, RawFrame, enum.Flag)):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]

        for v in val:
            if isinstance(v, (int, enum.Flag)):
                if funcs.mapSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, float):
                if funcs.mapSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, str):
                s = v.encode('utf-8')
                if funcs.mapSetData(inm, ckey, s, <int>len(s), dtUtf8, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, (bytes, bytearray)):
                if funcs.mapSetData(inm, ckey, v, <int>len(v), dtBinary, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, RawNode):
                if funcs.mapSetNode(inm, ckey, (<RawNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, RawFrame):
                if funcs.mapSetFrame(inm, ckey, (<RawFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, Func):
                if funcs.mapSetFunction(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif callable(v):
                tf = createFuncPython(v, core, funcs)

                if funcs.mapSetFunction(inm, ckey, (<Func>tf).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)

            else:
                raise Error('argument ' + key + ' was passed an unsupported type (' + type(v).__name__ + ')')


cdef void typedDictToMap(dict ndict, dict atypes, VSMap *inm, VSCore *core, const VSAPI *funcs) except *:
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]
        if val is None:
            continue

        if isinstance(val, (str, bytes, bytearray, enum.Flag, RawNode, RawFrame, enum.Flag)) or not isinstance(val, Iterable):
            val = [val]

        for v in val:
            if (atypes[key][:5] == 'vnode' and isinstance(v, VideoNode)) or (atypes[key][:5] == 'anode' and isinstance(v, AudioNode)):
                if funcs.mapSetNode(inm, ckey, (<RawNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif ((atypes[key][:6] == 'vframe') and isinstance(v, VideoFrame)) or (atypes[key][:6] == 'aframe' and isinstance(v, AudioFrame)):
                if funcs.mapSetFrame(inm, ckey, (<RawFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and isinstance(v, Func):
                if funcs.mapSetFunction(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and callable(v):
                tf = createFuncPython(v, core, funcs)
                if funcs.mapSetFunction(inm, ckey, tf.ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:3] == 'int':
                if funcs.mapSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:5] == 'float':
                if funcs.mapSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'data':
                if not isinstance(v, (str, bytes, bytearray)):
                    v = str(v)
                if isinstance(v, str):
                    s = v.encode('utf-8')
                else:
                    s = v
                if funcs.mapSetData(inm, ckey, s, <int>len(s), dtUtf8 if isinstance(v, str) else dtBinary, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type (expected ' + atypes[key] + ' compatible type but got ' + type(v).__name__ + ')')
        if len(val) == 0:
        # set an empty key if it's an empty array
            if atypes[key][:5] == 'vnode':
                funcs.mapSetEmpty(inm, ckey, ptVideoNode)
            elif atypes[key][:5] == 'anode':
                funcs.mapSetEmpty(inm, ckey, ptAudioNode)
            elif atypes[key][:6] == 'vframe':
                funcs.mapSetEmpty(inm, ckey, ptVideoFrame)
            elif atypes[key][:6] == 'aframe':
                funcs.mapSetEmpty(inm, ckey, ptAudioFrame)
            elif atypes[key][:4] == 'func':
                funcs.mapSetEmpty(inm, ckey, ptFunction)
            elif atypes[key][:3] == 'int':
                funcs.mapSetEmpty(inm, ckey, ptInt)
            elif atypes[key][:5] == 'float':
                funcs.mapSetEmpty(inm, ckey, ptFloat)
            elif atypes[key][:4] == 'data':
                funcs.mapSetEmpty(inm, ckey, ptData)
            else:
                raise Error('argument ' + key + ' has an unknown type: ' + atypes[key])

cdef class VideoFormat(object):
    cdef readonly uint32_t id
    cdef readonly str name
    cdef readonly object color_family
    cdef readonly object sample_type
    cdef readonly int bits_per_sample
    cdef readonly int bytes_per_sample
    cdef readonly int subsampling_w
    cdef readonly int subsampling_h
    cdef readonly int num_planes

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def _as_dict(self):
        return {
            'color_family': self.color_family,
            'sample_type': self.sample_type,
            'bits_per_sample': self.bits_per_sample,
            'subsampling_w': self.subsampling_w,
            'subsampling_h': self.subsampling_h
        }

    def replace(self, **kwargs):
        core = kwargs.pop("core", None) or _get_core()
        vals = self._as_dict()
        vals.update(**kwargs)
        return core.query_video_format(**vals)

    def __eq__(self, other):
        if not isinstance(other, VideoFormat):
            return False
        return other.id == self.id

    def __int__(self):
        return self.id

    def __repr__(self):
        return _construct_repr(
            self, id=self.id, name=self.name,
            color_family=self.color_family,
            sample_type=self.sample_type,
            bits_per_sample=self.bits_per_sample,
            bytes_per_sample=self.bytes_per_sample,
            num_planes=self.num_planes,
            subsampling_w=self.subsampling_w,
            subsampling_h=self.subsampling_h,
        )

    def __str__(self):
        return (
            'VideoFormat\n'
            f'\tID: {self.id:d}\n'
            f'\tName: {self.name}\n'
            f'\tColor Family: {self.color_family.name}\n'
            f'\tSample Type: {self.sample_type.name}\n'
            f'\tBits Per Sample: {self.bits_per_sample:d}\n'
            f'\tBytes Per Sample: {self.bytes_per_sample:d}\n'
            f'\tNum Planes: {self.num_planes:d}\n'
            f'\tSubsampling W: {f"{1 << self.subsampling_w}x" if self.subsampling_w else "None"}\n'
            f'\tSubsampling H: {f"{1 << self.subsampling_h}x" if self.subsampling_h else "None"}\n'
        )

cdef VideoFormat createVideoFormat(const VSVideoFormat *f, const VSAPI *funcs, VSCore *core):
    cdef VideoFormat instance = VideoFormat.__new__(VideoFormat)
    cdef char nameBuffer[32]
    funcs.getVideoFormatName(f, nameBuffer)
    instance.name = nameBuffer.decode('utf-8')
    instance.color_family = ColorFamily(f.colorFamily)
    instance.sample_type = SampleType(f.sampleType)
    instance.bits_per_sample = f.bitsPerSample
    instance.bytes_per_sample = f.bytesPerSample
    instance.subsampling_w = f.subSamplingW
    instance.subsampling_h = f.subSamplingH
    instance.num_planes = f.numPlanes
    instance.id = funcs.queryVideoFormatID(instance.color_family, instance.sample_type, instance.bits_per_sample, instance.subsampling_w, instance.subsampling_h, core)
    return instance

cdef class FrameProps(object):
    cdef RawFrame frame
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef bint readonly

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __contains__(self, str name):
        self.frame._ensure_open()
        cdef const VSMap *m = self.funcs.getFramePropertiesRO(self.frame.constf)
        cdef bytes b = name.encode('utf-8')
        cdef int numelem = self.funcs.mapNumElements(m, b)
        return numelem > 0

    def __getitem__(self, str name):
        self.frame._ensure_open()
        cdef const VSMap *m = self.funcs.getFramePropertiesRO(self.frame.constf)
        cdef bytes b = name.encode('utf-8')
        cdef list ol = []
        cdef int numelem = self.funcs.mapNumElements(m, b)
        cdef const int64_t *intArray
        cdef const double *floatArray
        cdef const char *data

        if numelem < 0:
            raise KeyError('No key named ' + name + ' exists')
        cdef int t = self.funcs.mapGetType(m, b)
        if t == ptInt:
            if numelem > 0:
                intArray = self.funcs.mapGetIntArray(m, b, NULL)
                for i in range(numelem):
                    ol.append(intArray[i])
        elif t == ptFloat:
            if numelem > 0:
                floatArray = self.funcs.mapGetFloatArray(m, b, NULL)
                for i in range(numelem):
                    ol.append(floatArray[i])
        elif t == ptData:
            for i in range(numelem):
                data = self.funcs.mapGetData(m, b, i, NULL)
                aval = data[:self.funcs.mapGetDataSize(m, b, i, NULL)]
                if self.funcs.mapGetDataTypeHint(m, b, i, NULL) == dtUtf8:
                    aval = aval.decode('utf-8')
                ol.append(aval)
        elif t == ptVideoNode or t == ptAudioNode:
            for i in range(numelem):
                ol.append(createNode(self.funcs.mapGetNode(m, b, i, NULL), self.funcs, _get_core()))
        elif t == ptVideoFrame or t == ptAudioFrame:
            for i in range(numelem):
                ol.append(createConstFrame(self.funcs.mapGetFrame(m, b, i, NULL), self.funcs, self.core))
        elif t == ptFunction:
            for i in range(numelem):
                ol.append(createFuncRef(self.funcs.mapGetFunction(m, b, i, NULL), self.funcs))

        if len(ol) == 1:
            return ol[0]
        else:
            return ol

    def __setitem__(self, str name, value):
        self.frame._ensure_open()
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropertiesRW(self.frame.f)
        cdef bytes b = name.encode('utf-8')
        cdef const VSAPI *funcs = self.funcs
        val = value
        if isinstance(val, (str, bytes, bytearray, RawNode, RawFrame, enum.Flag)):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]
        self.__delitem__(name)
        try:
            for v in val:
                if isinstance(v, RawNode):
                    if funcs.mapSetNode(m, b, (<RawNode>v).node, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, RawFrame):
                    if funcs.mapSetFrame(m, b, (<RawFrame>v).constf, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, Func):
                    if funcs.mapSetFunction(m, b, (<Func>v).ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif callable(v):
                    tf = createFuncPython(v, self.core, self.funcs)
                    if funcs.mapSetFunction(m, b, tf.ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, (int, enum.Flag)):
                    if funcs.mapSetInt(m, b, int(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, float):
                    if funcs.mapSetFloat(m, b, float(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, str):
                    if funcs.mapSetData(m, b, v.encode('utf-8'), -1, dtUtf8, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, (bytes, bytearray)):
                    if funcs.mapSetData(m, b, v, <int>len(v), dtBinary, 1) != 0:
                        raise Error('Not all values are of the same type')
                else:
                    raise Error('Setter was passed an unsupported type (' + type(v).__name__ + ')')
        except Error:
            self.__delitem__(name)
            raise

    def __delitem__(self, str name):
        self.frame._ensure_open()
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropertiesRW(self.frame.f)
        cdef bytes b = name.encode('utf-8')
        self.funcs.mapDeleteKey(m, b)

    def __setattr__(self, name, value):
        self.frame._ensure_open()
        self[name] = value

    def __delattr__(self, name):
        self.frame._ensure_open()
        del self[name]

    # Only the methods __getattr__ and keys are required for the support of
    #     >>> dict(frame.props)
    # this can be shown at Objects/dictobject.c:static int dict_merge(PyObject *, PyObject *, int)
    # in the generic code path.

    def __getattr__(self, name):
        self.frame._ensure_open()
        try:
           return self[name]
        except KeyError as e:
           raise AttributeError from e

    def keys(self):
        self.frame._ensure_open()
        cdef const VSMap *m = self.funcs.getFramePropertiesRO(self.frame.constf)
        cdef int numkeys = self.funcs.mapNumKeys(m)
        result = set()
        for i in range(numkeys):
            result.add(self.funcs.mapGetKey(m, i).decode('utf-8'))
        return result

    def values(self):
        self.frame._ensure_open()
        return {self[key] for key in self.keys()}

    def items(self):
        self.frame._ensure_open()
        return {(key, self[key]) for key in self.keys()}

    def get(self, key, default=None):
        self.frame._ensure_open()
        if key in self:
            return self[key]
        return default

    def pop(self, key, default=_EMPTY):
        self.frame._ensure_open()
        if key in self:
            value = self[key]
            del self[key]
            return value

        # The is-operator is required to ensure that
        # we have actually passed the _EMPTY list instead any other list with length zero.
        if default is _EMPTY:
            raise KeyError

        return default

    def popitem(self):
        self.frame._ensure_open()
        if len(self) <= 0:
            raise KeyError
        key = next(iter(self.keys()))
        return (key, self.pop(key))

    def setdefault(self, key, default=0):
        """
        Behaves like the dict.setdefault function but since setting None is not supported,
        it will default to zero.
        """
        self.frame._ensure_open()
        if key not in self:
            self[key] = default
        return self[key]

    def update(self, *args, **kwargs):
        self.frame._ensure_open()
        # This code converts the positional argument into a dict which we then can update
        # with the kwargs.
        if 0 < len(args) < 2:
            args = args[0]
            if not isinstance(args, dict):
                args = dict(args)
        elif len(args) > 1:
            raise TypeError("update takes 1 positional argument but %d was given" % len(args))
        else:
            args = {}

        args.update(kwargs)

        for k, v in args.items():
            self[k] = v

    def clear(self):
        self.frame._ensure_open()
        for _ in range(len(self)):
            self.popitem()

    def copy(self):
        """
        We can't copy VideoFrames directly, so we're just gonna return a real dictionary.
        """
        self.frame._ensure_open()
        return dict(self)

    def __iter__(self):
        self.frame._ensure_open()
        yield from self.keys()

    def __len__(self):
        self.frame._ensure_open()
        cdef const VSMap *m = self.funcs.getFramePropertiesRO(self.frame.constf)
        return self.funcs.mapNumKeys(m)

    def __dir__(self):
        self.frame._ensure_open()
        return super(FrameProps, self).__dir__() + list(self.keys())

    def __repr__(self):
        if self.frame.closed:
            return "<vapoursynth.FrameProps on closed frame>"
        return "<vapoursynth.FrameProps %r>" % dict(self)

cdef FrameProps createFrameProps(RawFrame f):
    cdef FrameProps instance = FrameProps.__new__(FrameProps)
    instance.frame = f
    instance.funcs = f.funcs
    instance.core = f.core
    instance.readonly = f.readonly
    return instance

# Make sure the FrameProps-Object quacks like a Mapping.
Mapping.register(FrameProps)


class ChannelLayout(int):
    def __contains__(self, layout):
        return bool(self & (1 << layout))

    def __iter__(self):
        for v in AudioChannels:
            if ((1 << v) & self):
                yield v

    def __len__(self):
        return self.bit_count()

    def __repr__(self):
        return _construct_repr(
            self, num_channels=len(self), layout=iter(self)
        )

    def __str__(self):
        layout = ', '.join([c.name for c in self])

        return (
            'ChannelLayout\n'
            f'\tNum channels: {len(self):d}\n'
            f'\tLayout: {layout}\n'
        )

cdef class RawFrame(object):
    cdef const VSFrame *constf
    cdef VSFrame *f
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef unsigned flags

    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFrame(self.constf)

    @property
    def closed(self):
        return self.constf == NULL

    cdef _ensure_open(self):
        if self.constf == NULL:
            raise RuntimeError("The Frame has already been released.")

    def close(self):
        if self.closed:
            return

        if self.funcs:
            self.funcs.freeFrame(self.constf)
        self.constf = NULL

    def __enter__(self):
        return self

    def __exit__(self, exc=None, val=None, tb=None):
        self.close()

    @property
    def props(self):
        self._ensure_open()
        return createFrameProps(self)

    @props.setter
    def props(self, new_props):
        p = self.props
        p.clear()
        p.update(**new_props)

    def get_write_ptr(self, int plane):
        self._ensure_open()
        if self.f == NULL:
            raise Error('Can only obtain write pointer for writable frames')
        cdef const uint8_t *d = self.funcs.getWritePtr(self.f, plane)
        if d == NULL:
            raise IndexError('Specified plane index out of range')
        return ctypes.c_void_p(<uintptr_t>d)

    def get_read_ptr(self, int plane):
        self._ensure_open()
        cdef const uint8_t *d = self.funcs.getReadPtr(self.constf, plane)
        if d == NULL:
            raise IndexError('Specified plane index out of range')
        return ctypes.c_void_p(<uintptr_t>d)

    def get_stride(self, int plane):
        self._ensure_open()
        cdef ptrdiff_t stride = self.funcs.getStride(self.constf, plane)
        if stride == 0:
            raise IndexError('Specified plane index out of range')
        return stride

    @property
    def readonly(self):
        return not self.flags & 1


cdef class VideoFrame(RawFrame):
    cdef readonly VideoFormat format
    cdef readonly int width
    cdef readonly int height

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def copy(self):
        self._ensure_open()
        return createVideoFrame(self.funcs.copyFrame(self.constf, self.core), self.funcs, self.core)

    def readchunks(self):
        self._ensure_open()

        lib = self.funcs
        frame = <VSFrame*> self.constf
        format = lib.getVideoFrameFormat(frame)

        # reuse the same _2dview for each plane
        view = _video.allocinfo(format)
        view.base.obj = self
        view.base.readonly = not self.flags & 1

        for plane in range(format.numPlanes):
            if lib.getStride(frame, plane) == lib.getFrameWidth(frame, plane) * format.bytesPerSample:
                _video.fillinfo(&view.base, frame, plane, &self.flags, lib)
                yield PyMemoryView_FromObject(view)
            else:
                for row in range(lib.getFrameHeight(frame, plane)):
                    _video.filllineinfo(&view.base, frame, plane, row, &self.flags, lib)
                    yield PyMemoryView_FromObject(view)

    def __getitem__(self, index):
        self._ensure_open()
        if PyIndex_Check(index):
            index = PyNumber_Index(index)
        else:
            raise TypeError("frame indices must be integers, not %s"
                            % (type(index).__name__,))

        lib = self.funcs
        frame = <VSFrame*> self.constf
        format = lib.getVideoFrameFormat(frame)

        if index < 0:
            index += format.numPlanes
        if not 0 <= index < format.numPlanes:
            raise IndexError("index out of range")

        data = _video.allocinfo(format)
        data.base.obj = createFramePtr(self.funcs.addFrameRef(self.constf), self.funcs)
        data.base.readonly = not self.flags & 1

        _video.fillinfo(&data.base, frame, index, &self.flags, lib)

        return PyMemoryView_FromObject(data)

    def __len__(self):
        self._ensure_open()
        lib = self.funcs
        return lib.getVideoFrameFormat(self.constf).numPlanes

    def __repr__(self):
        d = 'dynamic'
        width, height = (self.width, self.height) if self.width and self.height else (d, d)

        return _construct_repr(
            self, format=self.format or d, width=width, height=height, readonly=self.readonly,
        )

    def __str__(self):
        d = 'dynamic'
        format = self.format.name if self.format else d
        width, height = (self.width, self.height) if self.width and self.height else (d, d)

        return (
            'VideoFrame\n'
            f'\tFormat: {format}\n'
            f'\tWidth: {width}\n'
            f'\tHeight: {height}\n'
            f'\tReadonly: {str(self.readonly)}\n'
        )


cdef VideoFrame createConstVideoFrame(const VSFrame *constf, const VSAPI *funcs, VSCore *core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = constf
    instance.f = NULL
    instance.funcs = funcs
    instance.core = core
    instance.flags = 0
    instance.format = createVideoFormat(funcs.getVideoFrameFormat(constf), funcs, core)
    instance.width = funcs.getFrameWidth(constf, 0)
    instance.height = funcs.getFrameHeight(constf, 0)
    return instance


cdef VideoFrame createVideoFrame(VSFrame *f, const VSAPI *funcs, VSCore *core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = f
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.flags = -1
    instance.format = createVideoFormat(funcs.getVideoFrameFormat(f), funcs, core)
    instance.width = funcs.getFrameWidth(f, 0)
    instance.height = funcs.getFrameHeight(f, 0)
    return instance


@cython.final
@cython.internal
cdef class _frame:

    @staticmethod
    cdef void* getdata(VSFrame* frame, int index, unsigned* flags, const VSAPI* lib) noexcept nogil:
        cdef:
            unsigned mask

        if lib.getFrameType(frame) == mtVideo:
            mask = 1 << index+1
        else:
            mask = ~1  # there's only one plane in audio frames
        if flags[0] & mask:  # trigger copy-on-write
            flags[0] &= ~mask  # only do so once, see GH-724
            return <void*> lib.getWritePtr(frame, index)
        else:
            return <void*> lib.getReadPtr(frame, index)


@cython.final
@cython.internal
@cython.freelist(16)
cdef class _2dview:
    cdef:
        Py_buffer base
        ssize_t smalltable[4]  # shape, strides

    def __cinit__(self):
        # need Py_buffer.obj to be non-NULL
        PyBuffer_FillInfo(&self.base, None, NULL, 0, True, PyBUF_SIMPLE)

        self.base.ndim = 2
        self.base.shape = &self.smalltable[0]
        self.base.strides = &self.smalltable[2]

    def __dealloc__(self):
        PyBuffer_Release(&self.base)  # not handled by Cython

    def __getbuffer__(self, Py_buffer* view, int flags):
        # provide full info right away,
        # PEP-3118 compliance is left to memoryview
        view.obj = self.base.obj  # XXX: _2dview instances are temporary
                                  #      (requires Python>=3.3)
        view.buf = self.base.buf
        view.len = self.base.len
        view.readonly = self.base.readonly
        view.itemsize = self.base.itemsize
        view.format = self.base.format
        view.ndim = self.base.ndim
        view.shape = self.base.shape
        view.strides = self.base.strides
        view.suboffsets = self.base.suboffsets
        view.internal = self.base.internal


@cython.final
@cython.internal
cdef class _video:

    @staticmethod
    cdef _2dview allocinfo(const VSVideoFormat* format):
        cdef:
            _2dview self

        self = _2dview.__new__(_2dview)
        self.base.itemsize = format.bytesPerSample
        self.base.strides[1] = format.bytesPerSample

        if format.sampleType == stInteger:
            if format.bytesPerSample == 1:
                self.base.format = 'B'
            elif format.bytesPerSample == 2:
                self.base.format = 'H'
            elif format.bytesPerSample == 4:
                self.base.format = 'I'
        elif format.sampleType == stFloat:
            if format.bytesPerSample == 2:
                self.base.format = 'e'
            elif format.bytesPerSample == 4:
                self.base.format = 'f'

        return self

    @staticmethod
    cdef void fillinfo(Py_buffer* view, VSFrame* frame, int plane, unsigned* flags, const VSAPI* lib) nogil:
        view.shape[1] = lib.getFrameWidth(frame, plane)
        view.shape[0] = lib.getFrameHeight(frame, plane)
        view.strides[0] = lib.getStride(frame, plane)
        view.len = view.shape[0] * view.shape[1] * view.itemsize
        view.buf = _frame.getdata(frame, plane, flags, lib)
        
    @staticmethod
    cdef void filllineinfo(Py_buffer* view, VSFrame* frame, int plane, int line, unsigned* flags, const VSAPI* lib) nogil:
        view.shape[1] = lib.getFrameWidth(frame, plane)
        view.shape[0] = 1
        view.strides[0] = lib.getStride(frame, plane)
        view.len = view.shape[0] * view.shape[1] * view.itemsize
        view.buf = <void *>(<uint8_t *>_frame.getdata(frame, plane, flags, lib) + lib.getStride(frame, plane) * line)

cdef class AudioFrame(RawFrame):
    cdef readonly object sample_type
    cdef readonly int bits_per_sample
    cdef readonly int bytes_per_sample
    cdef readonly int64_t channel_layout
    cdef readonly int num_channels

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def copy(self):
        self._ensure_open()
        return createAudioFrame(self.funcs.copyFrame(self.constf, self.core), self.funcs, self.core)

    @property
    def channels(self):
        return ChannelLayout(self.channel_layout)

    def __getitem__(self, index):
        self._ensure_open()
        if PyIndex_Check(index):
            index = PyNumber_Index(index)

            lib = self.funcs
            frame = <VSFrame*> self.constf
            format = lib.getAudioFrameFormat(frame)

            if index < 0:
                index += format.numChannels
            if not 0 <= index < format.numChannels:
                raise IndexError("index out of range")

            data = _audio.allocinfo(format)
            data.base.obj = createFramePtr(self.funcs.addFrameRef(self.constf), self.funcs)
            data.base.readonly = not self.flags & 1

            _audio.fillinfo(&data.base, frame, index, &self.flags, lib)

            return PyMemoryView_FromObject(data)
        else:
            raise TypeError("frame indices must be integers, not %s"
                            % (type(index).__name__,))

    def __len__(self):
        self._ensure_open()
        lib = self.funcs
        return lib.getAudioFrameFormat(self.constf).numChannels

    def __repr__(self):
        return _construct_repr(
            self, sample_type=self.sample_type,
            bits_per_sample=self.bits_per_sample,
            bytes_per_sample=self.bytes_per_sample,
            num_channels=self.num_channels, channels=iter(self.channels),
            readonly=self.readonly
        )

    def __str__(self):
        channels = ', '.join([c.name for c in self.channels])

        return (
            'AudioFrame\n'
            f'\tSample Type: {self.sample_type.name}\n'
            f'\tBits Per Sample: {self.bits_per_sample:d}\n'
            f'\tBytes Per Sample: {self.bytes_per_sample:d}\n'
            f'\tNum Channels: {self.num_channels:d}\n'
            f'\tChannels: {channels}\n'
            f'\tReadonly: {str(self.readonly)}\n'
        )


cdef AudioFrame createConstAudioFrame(const VSFrame *constf, const VSAPI *funcs, VSCore *core):
    cdef AudioFrame instance = AudioFrame.__new__(AudioFrame)
    instance.constf = constf
    instance.f = NULL
    instance.funcs = funcs
    instance.core = core
    instance.flags = 0
    cdef const VSAudioFormat *format = funcs.getAudioFrameFormat(constf)
    instance.sample_type = SampleType(format.sampleType);
    instance.bits_per_sample = format.bitsPerSample
    instance.bytes_per_sample = format.bytesPerSample
    instance.channel_layout = format.channelLayout
    instance.num_channels = format.numChannels
    return instance


cdef AudioFrame createAudioFrame(VSFrame *f, const VSAPI *funcs, VSCore *core):
    cdef AudioFrame instance = AudioFrame.__new__(AudioFrame)
    instance.constf = f
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.flags = -1
    cdef const VSAudioFormat *format = funcs.getAudioFrameFormat(f)
    instance.sample_type = SampleType(format.sampleType);
    instance.bits_per_sample = format.bitsPerSample
    instance.bytes_per_sample = format.bytesPerSample
    instance.channel_layout = format.channelLayout
    instance.num_channels = format.numChannels
    return instance


@cython.final
@cython.internal
@cython.freelist(16)
cdef class _1dview_contig:
    cdef:
        Py_buffer base
        ssize_t smalltable[1]  # shape

    def __cinit__(self):
        # need Py_buffer.obj to be non-NULL
        PyBuffer_FillInfo(&self.base, None, NULL, 0, True, PyBUF_SIMPLE)

        self.base.ndim = 1
        self.base.shape = &self.smalltable[0]
        self.base.strides = &self.base.itemsize

    def __dealloc__(self):
        PyBuffer_Release(&self.base)  # not handled by Cython

    def __getbuffer__(self, Py_buffer* view, int flags):
        view.obj = self.base.obj
        view.buf = self.base.buf
        view.len = self.base.len
        view.readonly = self.base.readonly
        view.itemsize = self.base.itemsize
        view.format = self.base.format
        view.ndim = self.base.ndim
        view.shape = self.base.shape
        view.strides = self.base.strides
        view.suboffsets = self.base.suboffsets
        view.internal = self.base.internal


@cython.final
@cython.internal
cdef class _audio:

    @staticmethod
    cdef _1dview_contig allocinfo(const VSAudioFormat* format):
        cdef:
            _1dview_contig self

        self = _1dview_contig.__new__(_1dview_contig)
        self.base.itemsize = format.bytesPerSample

        if format.sampleType == stInteger:
            if format.bytesPerSample == 2:
                self.base.format = 'H'
            elif format.bytesPerSample == 4:
                self.base.format = 'I'
        elif format.sampleType == stFloat:
            if format.bytesPerSample == 4:
                self.base.format = 'f'

        return self

    @staticmethod
    cdef void fillinfo(Py_buffer* view, VSFrame* frame, int channel, unsigned* flags, const VSAPI* lib) nogil:
        view.shape[0] = lib.getFrameLength(frame)
        view.len = view.shape[0] * view.itemsize
        view.buf = _frame.getdata(frame, channel, flags, lib)

cdef _get_handle_future():
    fut = Future()
    fut.set_running_or_notify_cancel()

    def _handle_future(result, exception):
        if exception is not None:
            fut.set_exception(exception)
        else:
            fut.set_result(result)

    _handle_future.fut = fut

    return _handle_future

cdef class RawNode(object):
    cdef VSNode *node
    cdef const VSAPI *funcs
    cdef Core core

    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    cdef ensure_valid_frame_number(self, int n):
        raise NotImplementedError("Needs to be implemented by subclass.")

    def get_frame_async(self, int n, object cb = None):
        if cb is None:
            _handle_future = _get_handle_future()

            try:
                self.get_frame_async(n, _handle_future)
            except Exception as e:
                _handle_future.fut.set_exception(e)

            return _handle_future.fut

        cdef CallbackData data = createCallbackData(self.funcs, self, cb)
        Py_INCREF(data)
        with nogil:
            self.funcs.getFrameAsync(n, self.node, frameDoneCallback, <void *>data)


    def frames(self, prefetch=None, backlog=None, close=False):
        if prefetch is None or prefetch <= 0:
            prefetch = self.core.num_threads

        if backlog is None or backlog < 0:
            backlog = prefetch * 3
        elif backlog < prefetch:
            backlog = prefetch

        def _get_enum_fut():
            get_frame_async = self.get_frame_async

            for n in range(self.num_frames):
                handle_future = _get_handle_future()
                get_frame_async(n, handle_future)

                yield n, handle_future.fut

            return None

        enum_fut = _get_enum_fut().__next__

        finished = False
        running = 0
        lock = RLock()
        reorder = {}
        curr_frames = 0

        def _request_next():
            nonlocal finished, running, curr_frames

            if finished:
                return

            with lock:
                try:
                    idx, fut = enum_fut()
                except StopIteration:
                    finished = True
                    return

                reorder[idx] = fut
                running += 1
                curr_frames += 1

                fut.add_done_callback(_finished)

        def _finished(f):
            nonlocal finished, running

            with lock:
                running -= 1

                if finished:
                    return

                if f.exception() is not None:
                    finished = True
                    return

                _refill()

        def _refill():
            if finished:
                return

            with lock:
                # Two rules: 1. Don't exceed the concurrency barrier.
                #            2. Don't exceed unused-frames-backlog
                while (not finished) and (running < prefetch) and curr_frames < backlog:
                    _request_next()
        _refill()

        sidx = 0

        try:
            while (not finished) or (curr_frames > 0) or running > 0:
                if sidx not in reorder:
                    # Spin. Reorder being empty should never happen.
                    continue

                # Get next requested frame
                fut = reorder[sidx]

                result = fut.result()

                del reorder[sidx]

                curr_frames -= 1
                _refill()

                sidx += 1

                try:
                    yield result

                finally:
                    if close:
                        result.close()
        finally:
            finished = True

            for fut in reorder.copy().values():
                if fut.exception() is not None:
                    continue

                fut.result().close()

            gc.collect()

    def clear_cache(self):
        self.funcs.clearNodeCache(self.node)

    # Inspect API
    cdef bint _inspectable(self):
        if self.funcs.getAPIVersion() != VAPOURSYNTH_API_VERSION:
            return False
        return bool(self.core.flags & ccfEnableGraphInspection)

    def is_inspectable(self, version=None):
        if version != 0:
            return False
        return self._inspectable()

    @property
    def node_name(self):
        return self.funcs.getNodeName(self.node).decode("utf-8")

    property timings:
        def __get__(self):
            return self.funcs.getNodeProcessingTime(self.node, False)
        
        def __set__(self, bint value):
            if value != 0:
                raise ValueError('You can only set timings to 0, to reset its counter!')

            self.funcs.getNodeProcessingTime(self.node, True)

    @property
    def mode(self):
        return FilterMode(self.funcs.getNodeFilterMode(self.node))

    @property
    def dependencies(self):
        return tuple(
            createNode(self.funcs.addNodeRef(self.funcs.getNodeDependency(self.node, idx).source), self.funcs, self.core)
            for idx in range(self.funcs.getNumNodeDependencies(self.node))
        )

    @property
    def _name(self):
        if not self._inspectable():
            raise Error("This node is not inspectable.")

        return self.funcs.getNodeCreationFunctionName(self.node, 0).decode("utf-8")

    @property
    def _inputs(self):
        if not self._inspectable():
            raise Error("This node is not inspectable.")

        return mapToDict(self.funcs.getNodeCreationFunctionArguments(self.node, 0), False)

    def __eq__(self, other):
        if other is self:
            return True
        if not self._inspectable():
            # This makes __eq__ only check for being the same,
            # when introspection is enabled.
            return False

        if not isinstance(other, RawNode):
            return False
        else:
            return self.node == (<RawNode>other).node

    def __hash__(self):
        if not self._inspectable():
            # Since nodes are immutable, this is valid:
            return hash(id(self))
        else:
            return hash(int(<uintptr_t>self.node))

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeNode(self.node)


cdef class VideoNode(RawNode):
    cdef const VSVideoInfo *vi
    cdef readonly VideoFormat format
    cdef readonly int width
    cdef readonly int height
    cdef readonly int num_frames
    cdef readonly int64_t fps_num
    cdef readonly int64_t fps_den
    cdef readonly object fps

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __getattr__(self, name):
        try:
            obj = self.core.__getattr__(name)
            if isinstance(obj, Plugin):
                (<Plugin>obj).injected_arg = self
            return obj
        except AttributeError:
            raise AttributeError(f'There is no attribute or namespace named {name}. Did you mistype a plugin namespace or forget to install a plugin?') from None

    cdef ensure_valid_frame_number(self, int n):
        if n < 0:
            raise ValueError('Requesting negative frame numbers not allowed')
        if (self.num_frames > 0) and (n >= self.num_frames):
            raise ValueError('Requesting frame number is beyond the last frame')

    def get_frame(self, int n):
        cdef char errorMsg[4096]
        cdef char *ep = errorMsg
        cdef const VSFrame *f
        self.ensure_valid_frame_number(n)

        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 4096)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createConstVideoFrame(f, self.funcs, self.core.core)

    def set_output(self, int index = 0, VideoNode alpha = None, int alt_output = 0):
        if alpha is not None:
            if (self.vi.width != alpha.vi.width) or (self.vi.height != alpha.vi.height):
                raise Error('Alpha clip dimensions must match the main video')
            if (self.num_frames != alpha.num_frames):
                raise Error('Alpha clip length must match the main video')
            if (self.vi.format.colorFamily != cfUndefined) and (alpha.vi.format.colorFamily != cfUndefined):
                if (alpha.vi.format.colorFamily != cfGray) or (alpha.vi.format.sampleType != self.vi.format.sampleType) or (alpha.vi.format.bitsPerSample != self.vi.format.bitsPerSample):
                    raise Error('Alpha clip format must match the main video')
            elif (self.vi.format.colorFamily != cfUndefined) or (alpha.vi.format.colorFamily != cfUndefined):
                raise Error('Format must be either known or unknown for both alpha and main clip')

        _get_output_dict("set_output")[index] = VideoOutputTuple(self, alpha, alt_output)

    def output(self, object fileobj not None, bint y4m = False, object progress_update = None, int prefetch = 0, int backlog = -1):
        if (fileobj is sys.stdout or fileobj is sys.stderr):
            # If you are embedded in a vsscript-application, don't allow outputting to stdout/stderr.
            # This is the responsibility of the application, which does know better where to output it.
            if not isinstance(get_policy(), StandaloneEnvironmentPolicy):
                raise ValueError("In this context, use set_output() instead.")

            if hasattr(fileobj, "buffer"):
                fileobj = fileobj.buffer

        if progress_update is not None:
            progress_update(0, len(self))

        if y4m:
            if self.format.color_family == cfGray:
                y4mformat = 'mono'
                if self.format.bits_per_sample > 8:
                    y4mformat = y4mformat + str(self.format.bits_per_sample)
            elif self.format.color_family == cfYUV:
                if self.format.subsampling_w == 1 and self.format.subsampling_h == 1:
                    y4mformat = '420'
                elif self.format.subsampling_w == 1 and self.format.subsampling_h == 0:
                    y4mformat = '422'
                elif self.format.subsampling_w == 0 and self.format.subsampling_h == 0:
                    y4mformat = '444'
                elif self.format.subsampling_w == 2 and self.format.subsampling_h == 2:
                    y4mformat = '410'
                elif self.format.subsampling_w == 2 and self.format.subsampling_h == 0:
                    y4mformat = '411'
                elif self.format.subsampling_w == 0 and self.format.subsampling_h == 1:
                    y4mformat = '440'
                if self.format.bits_per_sample > 8:
                    y4mformat = y4mformat + 'p' + str(self.format.bits_per_sample)
            else:
                raise ValueError("Can only use GRAY and YUV for V4M-Streams")

            if len(y4mformat) > 0:
                y4mformat = 'C' + y4mformat + ' '

            data = 'YUV4MPEG2 {y4mformat}W{width} H{height} F{fps_num}:{fps_den} Ip A0:0 XLENGTH={length}\n'.format(
                y4mformat=y4mformat,
                width=self.width,
                height=self.height,
                fps_num=self.fps_num,
                fps_den=self.fps_den,
                length=len(self)
            )
            fileobj.write(data.encode("ascii"))

        write = fileobj.write
        readchunks = VideoFrame.readchunks

        for idx, frame in enumerate(self.frames(prefetch, backlog, close=True)):
            if y4m:
                fileobj.write(b"FRAME\n")

            for chunk in readchunks(frame):
                write(chunk)

            if progress_update is not None:
                progress_update(idx+1, len(self))

        if hasattr(fileobj, "flush"):
            fileobj.flush()

    def __add__(x, y):
        if not isinstance(x, VideoNode) or not isinstance(y, VideoNode):
            return NotImplemented
        return (<VideoNode>x).core.std.Splice(clips=[x, y])

    def __mul__(a, b):
        if isinstance(a, VideoNode):
            node = a
            val = b
        else:
            node = b
            val = a

        if not isinstance(val, int):
            raise TypeError('Clips may only be repeated by integer factors')
        if val <= 0:
            raise ValueError('Loop count must be one or bigger')
        return (<VideoNode>node).core.std.Loop(clip=node, times=val)

    def __getitem__(self, val):
        if isinstance(val, slice):
            if val.step is not None and val.step == 0:
                raise ValueError('Slice step cannot be zero')

            indices = val.indices(self.num_frames)

            step = indices[2]

            if step > 0:
                start = indices[0]
                stop = indices[1]
            else:
                start = indices[1]
                stop = indices[0]

            ret = self

            if step > 0 and stop is not None:
                stop -= 1
            if step < 0 and start is not None:
                start += 1

            if start is not None and stop is not None:
                ret = self.core.std.Trim(clip=ret, first=start, last=stop)
            elif start is not None:
                ret = self.core.std.Trim(clip=ret, first=start)
            elif stop is not None:
                ret = self.core.std.Trim(clip=ret, last=stop)

            if step < 0:
                ret = self.core.std.Reverse(clip=ret)

            if abs(step) != 1:
                ret = self.core.std.SelectEvery(clip=ret, cycle=abs(step), offsets=[0])

            return ret
        elif isinstance(val, int):
            if val < 0:
                n = self.num_frames + val
            else:
                n = val
            if n < 0 or (self.num_frames > 0 and n >= self.num_frames):
                raise IndexError('List index out of bounds')
            return self.core.std.Trim(clip=self, first=n, length=1)
        else:
            raise TypeError("index must be int or slice")

    def __dir__(self):
        plugins = []
        for plugin in self.core.plugins():
            if (<Plugin>plugin).is_video_injectable():
                plugins.append(plugin.namespace)
        return super(VideoNode, self).__dir__() + plugins

    def __len__(self):
        return self.num_frames

    def __repr__(self):
        d = 'dynamic'
        width, height = (self.width, self.height) if self.width and self.height else (d, d)

        return _construct_repr(
            self, format=self.format or d, width=width, height=height, num_frames=self.num_frames, fps=self.fps or d
        )

    def __str__(self):
        d = 'dynamic'
        format = self.format.name if self.format else d
        width, height = (self.width, self.height) if self.width and self.height else (d, d)

        return (
            'VideoNode\n'
            f'\tFormat: {format}\n'
            f'\tWidth: {width}\n'
            f'\tHeight: {height}\n'
            f'\tNum Frames: {self.num_frames:d}\n'
            f'\tFPS: {self.fps or d}\n'
        )

cdef VideoNode createVideoNode(VSNode *node, const VSAPI *funcs, Core core):
    cdef VideoNode instance = VideoNode.__new__(VideoNode)
    instance.core = core
    instance.node = node
    instance.funcs = funcs
    instance.vi = funcs.getVideoInfo(node)

    if (instance.vi.format.colorFamily != cfUndefined):
        instance.format = createVideoFormat(&instance.vi.format, funcs, core.core)
    else:
        instance.format = None

    instance.width = instance.vi.width
    instance.height = instance.vi.height
    instance.num_frames = instance.vi.numFrames
    instance.fps_num = <int64_t>instance.vi.fpsNum
    instance.fps_den = <int64_t>instance.vi.fpsDen
    if instance.vi.fpsDen:
        instance.fps = Fraction(
            <int64_t> instance.vi.fpsNum, <int64_t> instance.vi.fpsDen)
    else:
        instance.fps = Fraction(0, 1)

    return instance

cdef class AudioNode(RawNode):
    cdef const VSAudioInfo *ai
    cdef readonly object sample_type
    cdef readonly int bits_per_sample
    cdef readonly int bytes_per_sample
    cdef readonly uint64_t channel_layout
    cdef readonly int num_channels
    cdef readonly int sample_rate
    cdef readonly int64_t num_samples
    cdef readonly int num_frames

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __getattr__(self, name):
        err = False
        try:
            obj = self.core.__getattr__(name)
            if isinstance(obj, Plugin):
                (<Plugin>obj).injected_arg = self
            return obj
        except AttributeError:
            err = True
        if err:
            raise AttributeError(f'There is no attribute or namespace named {name}. Did you mistype a plugin namespace or forget to install a plugin?')

    cdef ensure_valid_frame_number(self, int n):
        if n < 0:
            raise ValueError('Requesting negative frame numbers not allowed')
        if (self.num_frames > 0) and (n >= self.num_frames):
            raise ValueError('Requesting frame number is beyond the last frame')

    def get_frame(self, int n):
        cdef char errorMsg[4096]
        cdef char *ep = errorMsg
        cdef const VSFrame *f
        self.ensure_valid_frame_number(n)

        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 4096)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createConstAudioFrame(f, self.funcs, self.core.core)

    def set_output(self, int index = 0):
        _get_output_dict("set_output")[index] = self

    @property
    def channels(self):
        return ChannelLayout(self.channel_layout)

    def __add__(x, y):
        if not isinstance(x, AudioNode) or not isinstance(y, AudioNode):
            return NotImplemented
        return (<AudioNode>x).core.std.AudioSplice(clips=[x, y])

    def __mul__(a, b):
        if isinstance(a, AudioNode):
            node = a
            val = b
        else:
            node = b
            val = a

        if not isinstance(val, int):
            raise TypeError('Clips may only be repeated by integer factors')
        if val <= 0:
            raise ValueError('Loop count must be one or bigger')
        return (<AudioNode>node).core.std.AudioLoop(clip=node, times=val)

    def __getitem__(self, val):
        if isinstance(val, slice):
            if val.step is not None and val.step == 0:
                raise ValueError('Slice step cannot be zero')
            if val.step is not None and abs(val.step) <> 1:
                raise ValueError('Slice step must be 1')

            indices = val.indices(self.num_samples)

            step = indices[2]

            if step > 0:
                start = indices[0]
                stop = indices[1]
            else:
                start = indices[1]
                stop = indices[0]

            ret = self

            if step > 0 and stop is not None:
                stop -= 1
            if step < 0 and start is not None:
                start += 1

            if start is not None and stop is not None:
                ret = self.core.std.AudioTrim(clip=ret, first=start, last=stop)
            elif start is not None:
                ret = self.core.std.AudioTrim(clip=ret, first=start)
            elif stop is not None:
                ret = self.core.std.AudioTrim(clip=ret, last=stop)

            if step < 0:
                ret = self.core.std.AudioReverse(clip=ret)

            return ret
        elif isinstance(val, int):
            if val < 0:
                n = self.num_samples + val
            else:
                n = val
            if n < 0 or (self.num_samples > 0 and n >= self.num_samples):
                raise IndexError('List index out of bounds')
            return self.core.std.AudioTrim(clip=self, first=n, length=1)
        else:
            raise TypeError("index must be int or slice")

    def __dir__(self):
        plugins = []
        for plugin in self.core.plugins():
            if (<Plugin>plugin).is_audio_injectable():
                plugins.append(plugin.namespace)
        return super(AudioNode, self).__dir__() + plugins

    def __len__(self):
        return self.num_samples

    def __repr__(self):
        return _construct_repr(
            self, sample_type=self.sample_type,
            bits_per_sample=self.bits_per_sample,
            bytes_per_sample=self.bytes_per_sample,
            num_channels=self.num_channels,
            channels=iter(self.channels), sample_rate=self.sample_rate,
            num_samples=self.num_samples

        )

    def __str__(self):
        channels = ', '.join([c.name for c in self.channels])

        return (
            'AudioNode\n'
            f'\tSample Type: {self.sample_type.name}\n'
            f'\tBits Per Sample: {self.bits_per_sample:d}\n'
            f'\tBytes Per Sample: {self.bytes_per_sample:d}\n'
            f'\tNum Channels: {self.num_channels:d}\n'
            f'\tChannels: {channels}\n'
            f'\tSample Rate: {self.sample_rate:d}\n'
            f'\tNum Samples: {self.num_samples:d}\n'
        )

cdef AudioNode createAudioNode(VSNode *node, const VSAPI *funcs, Core core):
    cdef AudioNode instance = AudioNode.__new__(AudioNode)
    instance.core = core
    instance.node = node
    instance.funcs = funcs
    instance.ai = funcs.getAudioInfo(node)
    instance.sample_rate = instance.ai.sampleRate
    instance.num_samples = instance.ai.numSamples
    instance.num_frames = instance.ai.numFrames
    instance.sample_type = SampleType(instance.ai.format.sampleType);
    instance.bits_per_sample = instance.ai.format.bitsPerSample
    instance.bytes_per_sample = instance.ai.format.bytesPerSample
    instance.channel_layout = instance.ai.format.channelLayout
    instance.num_channels = instance.ai.format.numChannels
    return instance

cdef class LogHandle(object):
    cdef VSLogHandle *handle
    cdef object handler_func

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

cdef LogHandle createLogHandle(object handler_func):
    cdef LogHandle instance = LogHandle.__new__(LogHandle)
    instance.handler_func = handler_func
    instance.handle = NULL
    return instance

cdef void __stdcall log_handler_wrapper(int msgType, const char *msg, void *userData) noexcept nogil:
    with gil:
        (<LogHandle>userData).handler_func(msgType, msg.decode('utf-8'))

cdef void __stdcall log_handler_free(void *userData) noexcept nogil:
    with gil:
        Py_DECREF(<LogHandle>userData)


cdef class CoreTimings(object):
    cdef Core core

    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    property enabled:
        def __get__(self):
            return bool(self.core.funcs.getCoreNodeTiming(self.core.core))

        def __set__(self, bint enabled):
            self.core.funcs.setCoreNodeTiming(self.core.core, enabled)

    property freed_nodes:
        def __get__(self):
            return self.core.funcs.getFreedNodeProcessingTime(self.core.core, False)
        
        def __set__(self, bint value):
            if value != 0:
                raise ValueError('You can only set freed_nodes to 0, to reset its counter!')

            self.core.funcs.getFreedNodeProcessingTime(self.core.core, True)

    def __repr__(self):
        return _construct_repr(
            self, enabled=self.enabled, freed_nodes=self.freed_nodes
        )

    def __str__(self):
        return (
            'CoreTimings\n'
            f'\tEnabled: {self.enabled}\n'
            f'\tFreed Nodes Time: {self.freed_nodes}\n'
        )

cdef CoreTimings createCoreTimings(Core core):
    cdef CoreTimings instance = CoreTimings.__new__(CoreTimings)
    instance.core = core

    return instance


cdef class Core(object):
    cdef int creationFlags
    cdef VSCore *core
    cdef const VSAPI *funcs

    cdef readonly object timings

    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeCore(self.core)

    property num_threads:
        def __get__(self):
            cdef VSCoreInfo v
            self.funcs.getCoreInfo(self.core, &v)
            return v.numThreads

        def __set__(self, int value):
            self.funcs.setThreadCount(value, self.core)

    property max_cache_size:
        def __get__(self):
            cdef VSCoreInfo v
            self.funcs.getCoreInfo(self.core, &v)
            cdef int64_t current_size = <int64_t>v.maxFramebufferSize
            current_size = current_size + 1024 * 1024 - 1
            current_size = current_size // <int64_t>(1024 * 1024)
            return current_size

        def __set__(self, int mb):
            if mb <= 0:
                raise ValueError('Maximum cache size must be a positive number')
            cdef int64_t new_size = mb
            new_size = new_size * 1024 * 1024
            self.funcs.setMaxCacheSize(new_size, self.core)

    @property
    def used_cache_size(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo(self.core, &v)
        return v.usedFramebufferSize

    @property
    def flags(self):
        return self.creationFlags

    def __getattr__(self, name):
        cdef VSPlugin *plugin
        tname = name.encode('utf-8')
        cdef const char *cname = tname
        plugin = self.funcs.getPluginByNamespace(cname, self.core)

        if plugin:
            return createPlugin(plugin, self.funcs, self)
        else:
            raise AttributeError('No attribute with the name ' + name + ' exists. Did you mistype a plugin namespace or forget to install a plugin?')

    def plugins(self):
        cdef VSPlugin *plugin = self.funcs.getNextPlugin(NULL, self.core)
        while plugin:
            tmp = createPlugin(plugin, self.funcs, self)
            plugin = self.funcs.getNextPlugin(plugin, self.core)
            yield tmp

    def query_video_format(self, int color_family, int sample_type, int bits_per_sample, int subsampling_w = 0, int subsampling_h = 0):
        cdef VSVideoFormat fmt
        if not self.funcs.queryVideoFormat(&fmt, color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h, self.core):
            raise Error('Invalid format specified')
        return createVideoFormat(&fmt, self.funcs, self.core)

    def get_video_format(self, uint32_t id):
        cdef VSVideoFormat fmt
        if not self.funcs.getVideoFormatByID(&fmt, id, self.core):
            raise Error('Invalid format id specified')
        else:
            return createVideoFormat(&fmt, self.funcs, self.core)

    def create_video_frame(self, object format, int width, int height):
        cdef VSVideoFormat fmt
        if not self.funcs.getVideoFormatByID(&fmt, int(format), self.core):
            raise Error('Invalid format id specified')

        cdef VSFrame* ref = self.funcs.newVideoFrame(&fmt, width, height, NULL, self.core)
        return createVideoFrame(ref, self.funcs, self.core)

    def log_message(self, int message_type, str message):
        self.funcs.logMessage(message_type, message.encode('utf-8'), self.core)

    def add_log_handler(self, handler_func):
        handler_func(mtDebug, 'New message handler installed from python')
        cdef LogHandle lh = createLogHandle(handler_func)
        Py_INCREF(lh)
        lh.handle = self.funcs.addLogHandler(log_handler_wrapper, log_handler_free, <void *>lh, self.core)
        return lh

    def remove_log_handler(self, LogHandle handle):
        return self.funcs.removeLogHandler(handle.handle, self.core)

    def clear_cache(self):
        self.funcs.clearCoreCaches(self.core)

    @property
    def core_version(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo(self.core, &v)

        return VapourSynthVersion(v.core, 0)

    @property
    def api_version(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo(self.core, &v)

        api_major = (v.api >> 16)
        api_minor = v.api - (api_major << 16)

        return VapourSynthAPIVersion(api_major, api_minor)

    def version(self):
        warnings.warn('core.version() is deprecated, use str(core)!', DeprecationWarning)
        return str(self)

    def version_number(self):
        warnings.warn('core.version_number() is deprecated, use core.core_version.release_major!', DeprecationWarning)
        return self.core_version.release_major

    def __dir__(self):
        plugins = []
        for plugin in self.plugins():
            plugins.append(plugin.namespace)
        return super(Core, self).__dir__() + plugins

    def __repr__(self):
        return _construct_repr(
            self, core_version=self.core_version, api_version=self.api_version,
            num_threads=self.num_threads, max_cache_size=self.max_cache_size,
            used_cache_size=self.used_cache_size
        )

    def __str__(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo(self.core, &v)

        versionString = (<const char *>v.versionString).decode('utf-8')

        version_lines = [x.strip() for x in versionString.splitlines()]
        copyright = '\n'.join(version_lines[:2])
        version = '\n\t'.join(version_lines[2:])

        return (
            f'{copyright}\n'
            f'\t{version}\n'
            f'\tNumber of Threads: {self.num_threads:d}\n'
            f'\tMax Cache Size: {self.max_cache_size:d}\n'
        )

cdef object createNode(VSNode *node, const VSAPI *funcs, Core core):
    if funcs.getNodeType(node) == mtVideo:
        return createVideoNode(node, funcs, core)
    else:
        return createAudioNode(node, funcs, core)

cdef object createConstFrame(const VSFrame *f, const VSAPI *funcs, VSCore *core):
    if funcs.getFrameType(f) == mtVideo:
        return createConstVideoFrame(f, funcs, core)
    else:
        return createConstAudioFrame(f, funcs, core)

cdef Core createCore(EnvironmentData env):
    cdef Core instance = Core.__new__(Core)
    instance.funcs = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    if instance.funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. Is the Python module and loaded core library mismatched?')
    instance.core = instance.funcs.createCore(env.coreCreationFlags)
    instance.timings = createCoreTimings(instance)
    instance.creationFlags = env.coreCreationFlags
    if instance.core_version.release_major <> VS_CURRENT_RELEASE:
        instance.log_message(mtWarning, f'Version mismatch: The VapourSynth Python module version is R{__version__.release_major:d} but the VapourSynth core library is R{instance.core_version.release_major:d}. This usually indicates a broken install.')
    return instance

cdef Core createCore2(VSCore *core):
    cdef Core instance = Core.__new__(Core)
    cdef VSMap *min
    cdef VSMap *mout
    cdef VSPlugin *plugin
    cdef VSNode *node
    instance.funcs = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    if instance.funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. Is the Python module and loaded core library mismatched?')
    instance.core = core
    instance.timings = createCoreTimings(instance)
    if instance.core_version.release_major <> VS_CURRENT_RELEASE:
        instance.log_message(mtWarning, f'Version mismatch: The VapourSynth Python module version is R{__version__.release_major:d} but the VapourSynth core library is R{instance.core_version.release_major:d}. This usually indicates a broken install.')
    plugin = instance.funcs.getPluginByID('com.vapoursynth.std', core)
    min = instance.funcs.createMap()
    mout = instance.funcs.invoke(plugin, "BlankClip", min)
    instance.funcs.freeMap(min)
    node = instance.funcs.mapGetNode(mout, "clip", 0, NULL)
    instance.funcs.freeMap(mout)
    if instance.funcs.getNodeCreationFunctionName(node, 0) <> NULL:
        instance.creationFlags = ccfEnableGraphInspection
    instance.funcs.freeNode(node)
    return instance

cdef Core _get_core(threads = None):
    env = _env_current()
    if env is None:
        raise Error('No environment is currently activated. Please activate an environment. (Hint: get_current_environment().use() allows you to temporary select an environment of your choice.)')

    return vsscript_get_core_internal(env)

cdef Core vsscript_get_core_internal(EnvironmentData env):
    if env.core is None:
        env.core = createCore(env)
    return env.core

cdef class _CoreProxy(object):

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    @property
    def core(self):
        return _get_core()

    @property
    def core_version(self):
        return __version__

    @property
    def api_version(self):
        return __api_version__

    def version(self):
        warnings.warn('core.version() is deprecated, use str(core)!', DeprecationWarning)
        return str(self)

    def version_number(self):
        warnings.warn('core.version_number() is deprecated, use core.core_version.release_major!', DeprecationWarning)
        return self.core_version.release_major

    def __dir__(self):
        d = dir(self.core)
        if 'core' not in d:
            d += ['core']

        return d

    def __getattr__(self, name):
        return getattr(self.core, name)

    def __setattr__(self, name, value):
        setattr(self.core, name, value)

    def __repr__(self):
        if _env_current() is None:
            core_repr = None
        else:
            core_repr = repr(self.core)

        return _construct_repr(self, core=core_repr)

    def __str__(self):
        if _env_current() is None:
            return (
                'Uninitialized Environment:\n'
                f'\tCore {__version__:s}\n'
                f'\tAPI {__api_version__:s}\n'
                '\tOptions: Unknown\n'
                '\tNumber of Threads: Unknown\n'
                '\tMax Cache Size: Unknown\n'
            )

        return str(self.core)

core = _CoreProxy.__new__(_CoreProxy)

PluginVersion = namedtuple("PluginVersion", "major minor")


cdef class Plugin(object):
    cdef Core core
    cdef VSPlugin *plugin
    cdef const VSAPI *funcs
    cdef object injected_arg
    cdef readonly str identifier
    cdef readonly str namespace
    cdef readonly str name

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __getattr__(self, name):
        tname = name.encode('utf-8')
        cdef const char *cname = tname
        cdef VSPluginFunction *func = self.funcs.getPluginFunctionByName(cname, self.plugin)

        if func:
            return createFunction(func, self, self.funcs)
        else:
            raise AttributeError('There is no function named ' + name)

    def functions(self):
        cdef VSPluginFunction *func = self.funcs.getNextPluginFunction(NULL, self.plugin)
        while func:
            tmp = createFunction(func, self, self.funcs)
            func = self.funcs.getNextPluginFunction(func, self.plugin)
            yield tmp

    @property
    def version(self):
        ver = <int>self.funcs.getPluginVersion(self.plugin)

        ver_major = (ver >> 16)
        ver_minor = (ver_major > -1) and (ver - (ver_major << 16)) or 0

        return PluginVersion(ver_major, ver_minor)
        
    @property
    def plugin_path(self):
        cdef const char *ppath = self.funcs.getPluginPath(self.plugin)
        if ppath:
            return ppath.decode('utf-8')
        else:
            return None

    def __dir__(self):
        attrs = []
        if isinstance(self.injected_arg, VideoNode):
            for func in self.functions():
                if (<Function>func).is_video_injectable():
                    attrs.append(func.name)
        elif isinstance(self.injected_arg, AudioNode):
            for func in self.functions():
                if (<Function>func).is_audio_injectable():
                    attrs.append(func.name)
        else:
            for func in self.functions():
                attrs.append(func.name)
        return attrs

    cdef is_video_injectable(self):
        for func in self.functions():
            if (<Function>func).is_video_injectable():
                return True
        return False

    cdef is_audio_injectable(self):
        for func in self.functions():
            if (<Function>func).is_audio_injectable():
                return True
        return False

    def __repr__(self):
        return _construct_repr(
            self, version=self.version,
            bound=(Core if self.injected_arg is None else type(self.injected_arg)).__name__
        )

cdef Plugin createPlugin(VSPlugin *plugin, const VSAPI *funcs, Core core):
    cdef Plugin instance = Plugin.__new__(Plugin)
    instance.core = core
    instance.plugin = plugin
    instance.funcs = funcs
    instance.injected_arg = None
    instance.identifier = funcs.getPluginID(plugin).decode('utf-8')
    instance.namespace = funcs.getPluginNamespace(plugin).decode('utf-8')
    instance.name = funcs.getPluginName(plugin).decode('utf-8')
    return instance

cdef class Function(object):
    cdef const VSAPI *funcs
    cdef const VSPluginFunction *func
    cdef readonly Plugin plugin
    cdef readonly str name
    cdef readonly str signature
    cdef readonly str return_signature

    @property
    def __signature__(self):
        return construct_signature(
            self.signature, self.return_signature,
            name=f'{self.plugin.namespace}_{self.name}',
            injected=self.plugin.injected_arg
        )

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    cdef is_video_injectable(self):
        first_arg_i = self.signature.find(':')
        return first_arg_i > 0 and self.signature.find(':vnode') == first_arg_i

    cdef is_audio_injectable(self):
        first_arg_i = self.signature.find(':')
        return first_arg_i > 0 and self.signature.find(':anode') == first_arg_i

    def __call__(self, *args, **kwargs):
        cdef VSMap *inm
        cdef VSMap *outm
        cdef char *cname
        arglist = list(args)
        if self.plugin.injected_arg is not None:
            arglist.insert(0, self.plugin.injected_arg)
        ndict = {}
        processed = {}
        atypes = {}
        # remove _ from all args
        for key in kwargs:
            # PEP8 tells us single_trailing_underscore_ for collisions with Python-keywords.
            if key[-1] == "_":
                nkey = key[:-1]
            else:
                nkey = key
            ndict[nkey] = kwargs[key]

        # match up unnamed arguments to the first unused name in order
        sigs = self.signature.split(';')
        any = False

        for sig in sigs:
            if sig == 'any':
                any = True
                continue
            elif sig == '':
                continue
            parts = sig.split(':')
            # store away the types for later use
            key = parts[0]
            atypes[key] = parts[1]

            # the name has already been specified
            if key in ndict:
                processed[key] = ndict[key]
                del ndict[key]
            else:
            # fill in with the first unnamed arg until they run out
                if len(arglist) > 0:
                    processed[key] = arglist[0]
                    del arglist[0]

        if len(arglist) > 0:
            raise Error(self.name + ': Too many unnamed arguments specified')

        if (len(ndict) > 0) and not any:
            raise Error(self.name + ': Function does not take argument(s) named ' + ', '.join(ndict.keys()))

        inm = self.funcs.createMap()

        dtomsuccess = True
        dtomexceptmsg = ''
        try:
            typedDictToMap(processed, atypes, inm, self.plugin.core.core, self.funcs)
            if any:
                dictToMap(ndict, inm, self.plugin.core.core, self.funcs)
        except Error as e:
            self.funcs.freeMap(inm)
            dtomsuccess = False
            dtomexceptmsg = str(e)
        except Exception:
            self.funcs.freeMap(inm)
            raise

        if dtomsuccess == False:
            raise Error(self.name + ': ' + dtomexceptmsg)

        tname = self.name.encode('utf-8')
        cname = tname
        with nogil:
            outm = self.funcs.invoke(self.plugin.plugin, cname, inm)
        self.funcs.freeMap(inm)
        cdef const char *err = self.funcs.mapGetError(outm)
        cdef bytes emsg

        if err:
            emsg = err
            self.funcs.freeMap(outm)
            raise Error(emsg.decode('utf-8'))

        retdict = mapToDict(outm, True)
        self.funcs.freeMap(outm)
        return retdict

    def __repr__(self):
        sig = self.__signature__

        parameters = ", ".join(map(str, sig.parameters.values()))
        signature = f'({parameters}) -> {inspect.formatannotation(sig.return_annotation)}'
        signature = signature.replace('vapoursynth.', '')

        return _construct_repr(
            self, bound=(Core if self.plugin.injected_arg is None else type(self.plugin.injected_arg)).__name__,
            signature=signature
        )


cdef Function createFunction(VSPluginFunction *func, Plugin plugin, const VSAPI *funcs):
    cdef Function instance = Function.__new__(Function)
    instance.name = funcs.getPluginFunctionName(func).decode('utf-8')
    instance.signature = funcs.getPluginFunctionArguments(func).decode('utf-8')
    instance.return_signature = funcs.getPluginFunctionReturnType(func).decode('utf-8')
    instance.plugin = plugin
    instance.funcs = funcs
    instance.func = func
    return instance



# for python functions being executed by vs

_warnings_showwarning = None
def _showwarning(message, category, filename, lineno, file=None, line=None):
    """
    Implementation of showwarnings which redirects to vapoursynth core logging.

    Note: This is apparently how python-logging does this.
    """
    if file is not None:
        if _warnings_showwarning is not None:
            _warnings_showwarning(message, category, filename, lineno, file, line)
    else:
        env = _env_current()
        if env is None:
            _warnings_showwarning(message, category, filename, lineno, file, line)
            return

        s = warnings.formatwarning(message, category, filename, lineno, line)
        core = vsscript_get_core_internal(env)
        core.log_message(mtWarning, s)

class PythonVSScriptLoggingBridge(logging.Handler):

    def __init__(self, parent, level=logging.NOTSET):
        super().__init__(level)
        self._parent = parent

    def emit(self, record):
        env = _env_current()
        if env is None:
            self.parent.handle(record)
            return
        core = vsscript_get_core_internal(env)

        message = self.format(record)

        if record.levelno < logging.INFO:
            mt = MessageType.MESSAGE_TYPE_DEBUG
        elif record.levelno < logging.WARN:
            mt = MessageType.MESSAGE_TYPE_INFORMATION
        elif record.levelno < logging.ERROR:
            mt = MessageType.MESSAGE_TYPE_WARNING
        elif record.levelno < logging.FATAL:
            mt = MessageType.MESSAGE_TYPE_CRITICAL
        else:
            mt = MessageType.MESSAGE_TYPE_CRITICAL
            message = "Fatal: " + message

        core.log_message(mt, message)

cdef void __stdcall freeFunc(void *pobj) noexcept nogil:
    with gil:
        fobj = <FuncData>pobj
        Py_DECREF(fobj)
        fobj = None


cdef void __stdcall publicFunction(const VSMap *inm, VSMap *outm, void *userData, VSCore *core, const VSAPI *vsapi) noexcept nogil:
    with gil:
        d = <FuncData>userData
        try:
            with use_environment(d.env).use():
                m = mapToDict(inm, False)
                ret = d(**m)
                if not isinstance(ret, dict):
                    if ret is None:
                        ret = 0
                    ret = {'val':ret}
                dictToMap(ret, outm, core, vsapi)
        except BaseException as e:
            emsg = b'\n' + ''.join(traceback.format_exception(type(e), e, e.__traceback__)).encode('utf-8')
            vsapi.mapSetError(outm, emsg)


@final
cdef class VSScriptEnvironmentPolicy:
    cdef dict _env_map

    cdef object _stack
    cdef object _lock
    cdef EnvironmentPolicyAPI _api

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot instantiate this class directly.")

    def on_policy_registered(self, policy_api):
        global _warnings_showwarning

        self._stack = ThreadLocal()
        self._api = policy_api
        self._env_map = {}

        # Redirect warnings to the parent application.
        _warnings_showwarning = warnings.showwarning
        warnings.showwarning = _showwarning
        warnings.filterwarnings("always", module="__vapoursynth__")
        warnings.filterwarnings("always", module="vapoursynth")

        # Redirect logging to the parent application.
        logging.basicConfig(level=logging.NOTSET, format="%(message)s", handlers=[
            PythonVSScriptLoggingBridge(logging.StreamHandler(sys.stderr)),
        ])

    def on_policy_cleared(self):
        global _warnings_showwarning

        self._env_map = None
        self._stack = None

        # Reset the warnings from the parent application
        warnings.showwarning = _warnings_showwarning
        _warnings_showwarning = None
        warnings.resetwarnings()

        # Reset the logging to only use sys.stderr
        for handler in logging.root.handlers[:]:
            logging.root.removeHandler(handler)
        logging.basicConfig(level=logging.WARN, format="%(message)s", handlers=[logging.StreamHandler(sys.stderr)])

        # Restore sys.stderr and sys.stdout
        sys.stderr = sys.__stderr__
        sys.stdout = sys.__stdout__

    cdef EnvironmentData get_environment(self, id):
        return self._env_map.get(id, None)

    def get_current_environment(self):
        return getattr(self._stack, "stack", None)

    def set_environment(self, environment):
        if not self.is_alive(environment):
            environment = None

        previous = getattr(self._stack, "stack", None)
        self._stack.stack = environment
        return previous

    cdef EnvironmentData _make_environment(self, int script_id, VSScript *se):
        cdef EnvironmentData env = self._api.create_environment()

        if se and se.core:
            env.core = createCore2(se.core)
            se.core = NULL # unset the core to indicate it's been used

        self._env_map[script_id] = env
        return env

    cdef has_environment(self, int script_id):
        return script_id in self._env_map

    cdef _free_environment(self, int script_id):
        env = self._env_map.pop(script_id, None)
        if env is not None:
            sys.stdout.flush()
            sys.stderr.flush()
            self._api.destroy_environment(env)

    def is_alive(self, EnvironmentData environment):
        return environment.alive


cdef VSScriptEnvironmentPolicy _get_vsscript_policy():
    if not isinstance(_policy, VSScriptEnvironmentPolicy):
        raise RuntimeError("This is not a VSScript-Policy.")
    return <VSScriptEnvironmentPolicy>_policy


cdef object _vsscript_use_environment(int id):
    return use_environment(_get_vsscript_policy().get_environment(id))


cdef object _vsscript_use_or_create_environment2(int id, VSScript *se):
    cdef VSScriptEnvironmentPolicy policy = _get_vsscript_policy()
    if not policy.has_environment(id):
        policy._make_environment(id, se)
    return use_environment(policy.get_environment(id))


cdef object _vsscript_use_or_create_environment(int id):
    return _vsscript_use_or_create_environment2(id, NULL)


@contextlib.contextmanager
def __chdir(filename, flags):
    if ((flags&1) == 0) or (filename is None) or (filename.startswith("<") and filename.endswith(">")):
        return (yield)

    origpath = os.getcwd()
    newpath = os.path.dirname(os.path.abspath(filename))

    try:
        os.chdir(newpath)
        yield
    finally:
        os.chdir(origpath)


cdef void _vpy_replace_pyenvdict(VSScript *se, dict pyenvdict):
    if se.pyenvdict:
        Py_DECREF(<dict>se.pyenvdict)
        se.pyenvdict = NULL

    if pyenvdict is not None:
        Py_INCREF(pyenvdict)
        se.pyenvdict = <void*>pyenvdict


cdef int _vpy_evaluate(VSScript *se, bytes script, str filename):
    try:
        pyenvdict = {}
        if se.pyenvdict:
            pyenvdict = <dict>se.pyenvdict
        else:
            _vpy_replace_pyenvdict(se, pyenvdict)

        pyenvdict["__name__"] = "__vapoursynth__"
        code = compile(script, filename=filename, dont_inherit=True, mode="exec")

        if filename is None or (filename.startswith("<") and filename.endswith(">")):
            filename = "<string>"
            pyenvdict.pop("__file__", None)
        else:
            pyenvdict["__file__"] = filename

        if se.errstr:
            errstr = <bytes>se.errstr
            se.errstr = NULL
            Py_DECREF(errstr)
            errstr = None

        with _vsscript_use_or_create_environment2(se.id, se).use():
            exec(code, pyenvdict, pyenvdict)

    except SystemExit, e:
        se.exitCode = e.code
        errstr = 'Python exit with code ' + str(e.code) + '\n'
        errstr = errstr.encode('utf-8')
        Py_INCREF(errstr)
        se.errstr = <void *>errstr
        return 3
    except BaseException, e:
        errstr = 'Python exception: ' + str(e) + '\n\n' + traceback.format_exc()
        errstr = errstr.encode('utf-8')
        Py_INCREF(errstr)
        se.errstr = <void *>errstr
        return 2
    except:
        errstr = 'Unspecified Python exception' + '\n\n' + traceback.format_exc()
        errstr = errstr.encode('utf-8')
        Py_INCREF(errstr)
        se.errstr = <void *>errstr
        return 1


cdef public api int vpy4_createScript(VSScript *se) nogil:
    with gil:
        try:
            _vpy_replace_pyenvdict(se, {})
            _get_vsscript_policy()._make_environment(<int>se.id, se)

        except:
            errstr = 'Unspecified Python exception' + '\n\n' + traceback.format_exc()
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 1
        return 0

cdef public api int vpy_evaluateScript(VSScript *se, const char *script, const char *scriptFilename, int flags) nogil:
    with gil:
        fn = scriptFilename.decode('utf-8')
        with __chdir(fn, flags):
            return _vpy_evaluate(se, script, fn)
        return 0

cdef public api int vpy_evaluateFile(VSScript *se, const char *scriptFilename, int flags) nogil:
    with gil:
        if not se.pyenvdict:
            pyenvdict = {}
            Py_INCREF(pyenvdict)
            se.pyenvdict = <void *>pyenvdict
            _get_vsscript_policy().get_environment(se.id).outputs.clear()
        try:
            with open(scriptFilename.decode('utf-8'), 'rb') as f:
                script = f.read(1024*1024*16)
            return vpy_evaluateScript(se, script, scriptFilename, flags)
        except BaseException, e:
            errstr = 'File reading exception:\n' + str(e)
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 2
        except:
            errstr = 'Unspecified file reading exception'
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 1

cdef public api int vpy4_evaluateBuffer(VSScript *se, const char *buffer, const char *scriptFilename) nogil:
    with gil:
        try:
            if not se.pyenvdict:
                _vpy_replace_pyenvdict(se, {})
            pyenvdict = <dict>se.pyenvdict

            if buffer == NULL:
                raise RuntimeError("NULL buffer passed.")

            fn = None
            if scriptFilename:
                fn = scriptFilename.decode('utf-8')

            if se.setCWD:
                with __chdir(fn, 1):
                    return _vpy_evaluate(se, buffer, fn)
            else:
                return _vpy_evaluate(se, buffer, fn)

        except BaseException, e:
            errstr = 'File reading exception:\n' + str(e)
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 2

cdef public api int vpy4_evaluateFile(VSScript *se, const char *scriptFilename) nogil:
    with gil:
        try:
            if scriptFilename == NULL:
                raise RuntimeError("NULL scriptFilename passed.")

            with open(scriptFilename.decode('utf-8'), 'rb') as f:
                script = f.read(1024*1024*16)
            return vpy4_evaluateBuffer(se, script, scriptFilename)
        except BaseException, e:
            errstr = 'File reading exception:\n' + str(e)
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 2
        except:
            errstr = 'Unspecified file reading exception'
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 1

cdef public api void vpy4_freeScript(VSScript *se) noexcept nogil:
    with gil:
        vpy_clearEnvironment(se)
        if se.pyenvdict:
            pyenvdict = <dict>se.pyenvdict
            se.pyenvdict = NULL
            Py_DECREF(pyenvdict)
            pyenvdict = None

        if se.errstr:
            errstr = <bytes>se.errstr
            se.errstr = NULL
            Py_DECREF(errstr)
            errstr = None

        try:
            _get_vsscript_policy()._free_environment(se.id)
        except:
            pass

        gc.collect()

cdef public api const char *vpy4_getError(VSScript *se) nogil:
    if not se.errstr:
        return NULL
    with gil:
        errstr = <bytes>se.errstr
        return errstr

cdef public api VSNode *vpy4_getOutput(VSScript *se, int index) nogil:
    with gil:
        pyenvdict = <dict>se.pyenvdict
        node = None
        try:
            node = _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return NULL

        if isinstance(node, VideoOutputTuple):
            node = node[0]

        if isinstance(node, RawNode):
            return (<RawNode>node).funcs.addNodeRef((<RawNode>node).node)
        else:
            return NULL

cdef public api VSNode *vpy4_getAlphaOutput(VSScript *se, int index) nogil:
    with gil:
        pyenvdict = <dict>se.pyenvdict
        node = None
        try:
            node = _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return NULL

        if isinstance(node, VideoOutputTuple):
            node = node[1]
            if isinstance(node, RawNode):
                return (<RawNode>node).funcs.addNodeRef((<RawNode>node).node)
        return NULL

cdef public api int vpy4_getAltOutputMode(VSScript *se, int index) nogil:
    with gil:
        pyenvdict = <dict>se.pyenvdict
        output = None
        try:
            output = _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return 0

        if isinstance(output, VideoOutputTuple):
            return output[2]
        return 0
        
cdef public api int vpy4_getAvailableOutputNodes(VSScript *se, int size, int *dst) nogil:
    cdef int dstidx = 0
    with gil:
        pyenvdict = <dict>se.pyenvdict
        nodes = None
        try:
            nodes = _get_vsscript_policy().get_environment(se.id).outputs
        except:
            return 0
    
        if size > 0:
            for key in nodes:
                dst[dstidx] = key
                dstidx = dstidx + 1
                if size - dstidx <= 0:
                    break
        
        return len(nodes)

cdef public api int vpy_clearOutput(VSScript *se, int index) nogil:
    with gil:
        try:
            del _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return 1
        return 0

cdef public api VSCore *vpy4_getCore(VSScript *se) nogil:
    with gil:
        try:
            core = vsscript_get_core_internal(_get_vsscript_policy().get_environment(se.id))
            if core is not None:
                return (<Core>core).core
            else:
                return NULL
        except:
            return NULL

cdef public api const VSAPI *vpy4_getVSAPI(int version) nogil:
    return getVapourSynthAPI(version)

cdef const VSAPI *getVSAPIInternal() nogil:
    global _vsapi
    if _vsapi == NULL:
        _vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    return _vsapi

cdef public api int vpy4_getVariable(VSScript *se, const char *name, VSMap *dst) nogil:
    with gil:
        with _vsscript_use_environment(se.id).use():
            pyenvdict = <dict>se.pyenvdict
            try:
                dname = name.decode('utf-8')
                read_var = { dname:pyenvdict[dname]}
                core = vsscript_get_core_internal(_get_vsscript_policy().get_environment(se.id))
                dictToMap(read_var, dst, core.core, core.funcs)
                return 0
            except:
                return 1

cdef public api int vpy4_setVariables(VSScript *se, const VSMap *vars) nogil:
    with gil:
        with _vsscript_use_environment(se.id).use():
            pyenvdict = <dict>se.pyenvdict
            try:
                pyenvdict.update(mapToDict(vars, False))
                return 0
            except:
                return 1

cdef public api int vpy_clearVariable(VSScript *se, const char *name) nogil:
    with gil:
        pyenvdict = <dict>se.pyenvdict
        try:
            del pyenvdict[name.decode('utf-8')]
        except:
            return 1
        return 0

cdef public api void vpy_clearEnvironment(VSScript *se) noexcept nogil:
    with gil:
        pyenvdict = <dict>se.pyenvdict
        for key in pyenvdict:
            pyenvdict[key] = None
        pyenvdict.clear()
        try:
            # Environment is lazily created at the time of exec'ing a script,
            # if the process errors out before that (e.g. fails compiling),
            # the environment might be None.
            env = _get_vsscript_policy().get_environment(se.id)
            if env is not None:
                env.outputs.clear()
                env.core = None
        except:
            pass
        gc.collect()

cdef public api int vpy4_initVSScript() nogil:
    with gil:
        if getVSAPIInternal() == NULL:
            return 1
        if has_policy():
            return 1

        vsscript = VSScriptEnvironmentPolicy.__new__(VSScriptEnvironmentPolicy)
        register_policy(vsscript)
        return 0
