#  Copyright (c) 2012-2020 Fredrik Mellbin
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
cimport cython.parallel
from cython cimport view, final
from libc.stdint cimport intptr_t, uint16_t, uint32_t
from cpython.buffer cimport (PyBUF_WRITABLE, PyBUF_FORMAT, PyBUF_STRIDES,
                             PyBUF_F_CONTIGUOUS)
from cpython.ref cimport Py_INCREF, Py_DECREF
import os
import ctypes
import threading
import traceback
import gc
import sys
import inspect
import weakref
import atexit
import contextlib
from threading import local as ThreadLocal, Lock
from types import MappingProxyType
from collections import namedtuple
from collections.abc import Iterable, Mapping
from fractions import Fraction

# Ensure that the import doesn't fail
# if typing is not available on the python installation.
try:
    import typing
except ImportError as e:
    typing = None

__all__ = [
  'COMPAT',
    'COMPATBGR32', 'COMPATYUY2',
  'GRAY',
    'GRAY16', 'GRAY8', 'GRAYH', 'GRAYS', 
  'RGB',
    'RGB24', 'RGB27', 'RGB30', 'RGB48', 'RGBH', 'RGBS',
  'YCOCG', 'YUV',
    'YUV410P8', 'YUV411P8', 'YUV420P10', 'YUV420P12',
    'YUV420P14', 'YUV420P16', 'YUV420P8', 'YUV420P9',
    'YUV422P10', 'YUV422P12', 'YUV422P14', 'YUV422P16',
    'YUV422P8', 'YUV422P9', 'YUV440P8', 'YUV444P10',
    'YUV444P12', 'YUV444P14', 'YUV444P16', 'YUV444P8',
    'YUV444P9', 'YUV444PH', 'YUV444PS', 
  'NONE',
  'FLOAT', 'INTEGER',
  
  'get_output', 'get_outputs',
  'clear_output', 'clear_outputs',
  
  'core', 
]
    
__version__ = namedtuple("VapourSynthVersion", "release_major release_minor")(50, 0)
__api_version__ = namedtuple("VapourSynthAPIVersion", "api_major api_minor")(VAPOURSYNTH_API_MAJOR, VAPOURSYNTH_API_MINOR)

@final
cdef class EnvironmentData(object):
    cdef object core
    cdef dict outputs

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")


class EnvironmentPolicy(object):

    def on_policy_registered(self, special_api):
        pass

    def on_policy_cleared(self):
        pass

    def get_current_environment(self):
        raise NotImplementedError

    def set_environment(self, environment):
        raise NotImplementedError

    def is_active(self, environment):
        raise NotImplementedError


@final
cdef class StandaloneEnvironmentPolicy:
    cdef EnvironmentData _environment

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    def on_policy_registered(self, api):
        self._environment = api.create_environment()

    def on_policy_cleared(self):
        self._environment = None

    def get_current_environment(self):
        return self._environment

    def set_environment(self, environment):
        return self._environment

    def is_alive(self, environment):
        return environment is self._environment

# This flag is kept for backwards compatibility
# I suggest deleting it sometime after R51
_using_vsscript = False

# Internal holder of the current policy.
cdef object _policy = None


cdef object _message_handler = None
cdef const VSAPI *_vsapi = NULL


@final
cdef class EnvironmentPolicyAPI:
    # This must be a weak-ref to prevent a cyclic dependency that happens if the API
    # is stored within an EnvironmentPolicy-instance.
    cdef object _target_policy

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    cdef ensure_policy_matches(self):
        if _policy is not self._target_policy():
            raise ValueError("The currently activated policy does not match the bound policy. Was the environment unregistered?")

    def wrap_environment(self, environment_data):
        self.ensure_policy_matches()
        if not isinstance(environment_data, EnvironmentData):
            raise ValueError("environment_data must be an EnvironmentData instance.")
        return use_environment(<EnvironmentData>environment_data, direct=False)

    def create_environment(self):
        self.ensure_policy_matches()

        cdef EnvironmentData env = EnvironmentData.__new__(EnvironmentData)
        env.core = None
        env.outputs = {}
        
        return env

    def unregister_policy(self):
        self.ensure_policy_matches()
        clear_policy()

    def __repr__(self):
        target = self._target_policy()
        if target is None:
            return f"<EnvironmentPolicyAPI bound to <garbage collected> (unregistered)"
        elif _policy is not target:
            return f"<EnvironmentPolicyAPI bound to {target!r} (unregistered)>"
        else:
            return f"<EnvironmentPolicyAPI bound to {target!r}>"


def register_policy(policy):
    global _policy, _using_vsscript
    if _policy is not None:
        raise RuntimeError("There is already a policy registered.")
    _policy = policy

    # Expose Additional API-calls to the newly registered Environment-policy.
    cdef EnvironmentPolicyAPI _api = EnvironmentPolicyAPI.__new__(EnvironmentPolicyAPI)
    _api._target_policy = weakref.ref(_policy)
    _policy.on_policy_registered(_api)

    if not isinstance(policy, StandaloneEnvironmentPolicy):
        # Older script had to use this flag to determine if it ran in
        # Multi-VSCore-Environments.
        #
        # We will just assume that this is the case if we register a custom
        # policy.
        _using_vsscript = True


## DO NOT EXPOSE THIS FUNCTION TO PYTHON-LAND!
cdef get_policy():
    global _policy
    if _policy is None:
        standalone_policy = StandaloneEnvironmentPolicy.__new__(StandaloneEnvironmentPolicy)
        register_policy(standalone_policy)

    return _policy

def has_policy():
    return _policy is not None

cdef clear_policy():
    global _policy, _using_vsscript
    old_policy = _policy
    _policy = None
    if old_policy is not None:
        old_policy.on_policy_cleared()
    _using_vsscript = False
    return old_policy

cdef EnvironmentData _env_current():
    return get_policy().get_current_environment()


# Make sure the policy is cleared at exit.
atexit.register(lambda: clear_policy())


@final
cdef class _FastManager(object):
    cdef EnvironmentData target
    cdef EnvironmentData previous

    def __init__(self):
        raise RuntimeError("Cannot directly instantiate this class.")

    def __enter__(self):
        if self.target is not None:
            self.previous = get_policy().set_environment(self.target)
            self.target = None
        else:
            self.previous = get_policy().get_current_environment()
    
    def __exit__(self, *_):
        get_policy().set_environment(self.previous)
        self.previous = None


cdef object _tl_env_stack = ThreadLocal()
cdef class Environment(object):
    cdef readonly object env
    cdef bint use_stack

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
        env.use_stack = False
        return env

    def use(self):
        env = self.get_env()
        if env is None:
            raise RuntimeError("The environment is dead.")

        cdef _FastManager ctx = _FastManager.__new__(_FastManager)
        ctx.target = env
        ctx.previous = None
        return ctx

    cdef _get_stack(self):
        if not self.use_stack:
            raise RuntimeError("You cannot directly use the environment as a context-manager. Use Environment.use instead.")
        _tl_env_stack.stack = getattr(_tl_env_stack, "stack", [])
        return _tl_env_stack.stack

    def __enter__(self):
        if not self.alive:
            raise RuntimeError("The environment has died.")

        env = self.get_env()
        stack = self._get_stack()
        stack.append(get_policy().set_environment(env))
        if len(stack) > 1:
            import warnings
            warnings.warn("Using the environment as a context-manager is not reentrant. Expect undefined behaviour. Use Environment.use instead.", RuntimeWarning)

        return self

    def __exit__(self, *_):
        stack = self._get_stack()
        if not stack:
            import warnings
            warnings.warn("Exiting while the stack is empty. Was the frame suspended during the with-statement?", RuntimeWarning)
            return

        env = stack.pop()
        old = get_policy().set_environment(env)

        # We exited with a different environment. This is not good. Automatically revert this change.
        if old is not self.get_env():
            import warnings
            warnings.warn("The exited environment did not match the managed environment. Was the frame suspended during the with-statement?", RuntimeWarning)

    def __eq__(self, other):
        return other.env_id == self.env_id

    def __repr__(self):
        if self.single:
            return "<Environment (default)>"

        return f"<Environment {id(self.env)} ({('active' if self.active else 'alive') if self.alive else 'dead'})>"


cdef Environment use_environment(EnvironmentData env, bint direct = False):
    if id is None: raise ValueError("id may not be None.")

    cdef Environment instance = Environment.__new__(Environment)
    instance.env = weakref.ref(env)
    instance.use_stack = direct

    return instance


def vpy_current_environment():
    import warnings
    warnings.warn("This function is deprecated and might cause unexpected behaviour. Use get_current_environment() instead.", DeprecationWarning)

    env = get_policy().get_current_environment()
    if env is None:
        raise RuntimeError("We are not running inside an environment.")

    vsscript_get_core_internal(env) # Make sure a core is defined
    return use_environment(env, direct=True)

def get_current_environment():
    env = get_policy().get_current_environment()
    if env is None:
        raise RuntimeError("We are not running inside an environment.")

    vsscript_get_core_internal(env) # Make sure a core is defined
    return use_environment(env, direct=False)

# Create an empty list whose instance will represent a not passed value.
_EMPTY = []

AlphaOutputTuple = namedtuple("AlphaOutputTuple", "clip alpha")

def _construct_parameter(signature):
    name,type,*opt = signature.split(":")

    # Handle Arrays.
    if type.endswith("[]"):
        array = True
        type = type[:-2]
    else:
        array = False

    # Handle types
    if type == "clip":
        type = vapoursynth.VideoNode
    elif type == "frame":
        type = vapoursynth.VideoFrame
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
        opt = None
    else:
        opt = inspect.Parameter.empty
        
        
    return inspect.Parameter(
        name, inspect.Parameter.POSITIONAL_OR_KEYWORD,
        default=opt, annotation=type
    )

def construct_signature(signature, injected=None):
    if typing is None:
        raise RuntimeError("At least Python 3.5 is required to use type-hinting")
    
    if isinstance(signature, vapoursynth.Function):
        signature = signature.signature

    params = list(
        _construct_parameter(param)
        for param in signature.split(";")
        if param
    )
    
    if injected:
        del params[0]
    
    return inspect.Signature(tuple(params), return_annotation=vapoursynth.VideoNode)
    

class Error(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return str(self.value)
        
    def __repr__(self):
        return repr(self.value)

cdef void __stdcall message_handler_wrapper(int msgType, const char *msg, void *userData) nogil:
    with gil:
        global _message_handler
        _message_handler(msgType, msg.decode('utf-8'))
        
def set_message_handler(handler_func):
    cdef const VSAPI *funcs
    global _message_handler
    funcs = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    if funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. Is the Python module and loaded core library mismatched?')
    if handler_func is None:
        _message_handler = None
        funcs.setMessageHandler(NULL, NULL)
    else:
        handler_func(vapoursynth.mtDebug, 'New message handler installed from python')
        _message_handler = handler_func
        funcs.setMessageHandler(message_handler_wrapper, NULL)
    
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
    cdef VSFuncRef *ref
    
    def __init__(self):
        raise Error('Class cannot be instantiated directly')
        
    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFunc(self.ref)
        
    def __call__(self, **kwargs):
        cdef VSMap *outm
        cdef VSMap *inm
        cdef const VSAPI *vsapi
        cdef const char *error
        vsapi = vpy_getVSApi();
        outm = self.funcs.createMap()
        inm = self.funcs.createMap()
        try:
            dictToMap(kwargs, inm, NULL, vsapi)
            self.funcs.callFunc(self.ref, inm, outm, NULL, NULL)
            error = self.funcs.getError(outm)
            if error:
                raise Error(error.decode('utf-8'))
            ret = mapToDict(outm, False, False, NULL, vsapi)
            if not isinstance(ret, dict):
                ret = {'val':ret}
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
    instance.ref = instance.funcs.createFunc(publicFunction, <void *>fdata, freeFunc, core, funcs)
    return instance
        
cdef Func createFuncRef(VSFuncRef *ref, const VSAPI *funcs):
    cdef Func instance = Func.__new__(Func)
    instance.funcs = funcs
    instance.ref = ref
    return instance

cdef class RawCallbackData(object):
    cdef const VSAPI *funcs
    cdef object callback

    cdef VideoNode node

    cdef object wrap_cb
    cdef object future
    cdef EnvironmentData env

    def __init__(self, VideoNode node, EnvironmentData env, object callback = None):
        self.node = node
        self.callback = callback

        self.future = None
        self.wrap_cb = None
        self.env = env

    def for_future(self, object future, object wrap_call=None):
        if wrap_call is None:
            wrap_call = lambda func, *args, **kwargs: func(*args, **kwargs)
        self.callback = self.handle_future
        self.future = future
        self.wrap_cb = wrap_call

    def handle_future(self, node, n, result):
        if isinstance(result, Error):
            func = self.future.set_exception
        else:
            func = self.future.set_result

        with use_environment(self.env).use():
            self.wrap_cb(func, result)

    def receive(self, n, result):
        self.callback(self.node, n, result)


cdef createRawCallbackData(const VSAPI* funcs, VideoNode videonode, object cb, object wrap_call=None):
    cbd = RawCallbackData(videonode, _env_current(), cb)
    if not callable(cb):
        cbd.for_future(cb, wrap_call)
    cbd.funcs = funcs
    return cbd

cdef class CallbackData(object):
    cdef VideoNode node
    cdef const VSAPI *funcs
    cdef object fileobj
    cdef int output
    cdef int requested
    cdef int completed
    cdef int total
    cdef int num_planes
    cdef bint y4m
    cdef dict reorder
    cdef object condition
    cdef object progress_update
    cdef str error

    def __init__(self, fileobj, requested, total, num_planes, y4m, node, progress_update):
        self.fileobj = fileobj
        self.output = 0
        self.requested = requested
        self.completed = 0
        self.total = total
        self.num_planes = num_planes
        self.y4m = y4m
        self.condition = threading.Condition()
        self.node = node
        self.progress_update = progress_update
        self.funcs = (<VideoNode>node).funcs
        self.reorder = {}


cdef class FramePtr(object):
    cdef const VSFrameRef *f
    cdef const VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFrame(self.f)

cdef FramePtr createFramePtr(const VSFrameRef *f, const VSAPI *funcs):
    cdef FramePtr instance = FramePtr.__new__(FramePtr)    
    instance.f = f
    instance.funcs = funcs
    return instance

cdef void __stdcall frameDoneCallbackRaw(void *data, const VSFrameRef *f, int n, VSNodeRef *node, const char *errormsg) nogil:
    with gil:
        d = <RawCallbackData>data
        if f == NULL:
            result = ''
            if errormsg != NULL:
                result = errormsg.decode('utf-8')
            result = Error(result)

        else:
            result = createConstVideoFrame(f, d.funcs, d.node.core.core)

        try:
            d.receive(n, result)
        except:
            import traceback
            traceback.print_exc()
        finally:
            Py_DECREF(d)


cdef void __stdcall frameDoneCallbackOutput(void *data, const VSFrameRef *f, int n, VSNodeRef *node, const char *errormsg) with gil:
    cdef VideoFrame frame_obj
    cdef VideoPlane plane
    cdef int x

    cdef CallbackData d = <CallbackData>data
    d.completed += 1

    if f == NULL:
        d.total = d.requested
        if errormsg == NULL:
            d.error = 'Failed to retrieve frame ' + str(n)
        else:
            d.error = 'Failed to retrieve frame ' + str(n) + ' with error: ' + errormsg.decode('utf-8')
        d.output += 1
    else:
        d.reorder[n] = createConstVideoFrame(f, d.funcs, d.node.core.core)

        while d.output in d.reorder:
            frame_obj = <VideoFrame>d.reorder[d.output]
            bytes_per_sample = frame_obj.format.bytes_per_sample
            try:
                if d.y4m:
                    d.fileobj.write(b'FRAME\n')
                for x in range(frame_obj.format.num_planes):
                    plane = VideoPlane.__new__(VideoPlane, frame_obj, x)
                    
                    # This is a quick fix.
                    # Calling bytes(VideoPlane) should make the buffer continuous by
                    # copying the frame to a continous buffer
                    # if the stride does not match the width*bytes_per_sample.
                    if frame_obj.get_stride(x) != plane.width*bytes_per_sample:
                        d.fileobj.write(bytes(plane))
                    else:
                        d.fileobj.write(plane)

            except BaseException as e:
                d.error = 'File write call returned an error: ' + str(e)
                d.total = d.requested

            del d.reorder[d.output]
            d.output += 1

        if d.progress_update is not None:
            try:
                d.progress_update(d.completed, d.total)
            except BaseException as e:
                d.error = 'Progress update caused an exception: ' + str(e)
                d.total = d.requested

    if d.requested < d.total:
        d.node.funcs.getFrameAsync(d.requested, d.node.node, frameDoneCallbackOutput, data)
        d.requested += 1

    d.condition.acquire()
    d.condition.notify()
    d.condition.release()


cdef object mapToDict(const VSMap *map, bint flatten, bint add_cache, VSCore *core, const VSAPI *funcs):
    cdef int numKeys = funcs.propNumKeys(map)
    retdict = {}
    cdef const char *retkey
    cdef char proptype

    for x in range(numKeys):
        retkey = funcs.propGetKey(map, x)
        proptype = funcs.propGetType(map, retkey)

        for y in range(funcs.propNumElements(map, retkey)):
            if proptype == 'i':
                newval = funcs.propGetInt(map, retkey, y, NULL)
            elif proptype == 'f':
                newval = funcs.propGetFloat(map, retkey, y, NULL)
            elif proptype == 's':
                newval = funcs.propGetData(map, retkey, y, NULL)
            elif proptype == 'c' or proptype == 'a':
                c = _get_core()
                newval = createNode(funcs.propGetNode(map, retkey, y, NULL), funcs, c)
                
                if add_cache and not (newval.flags & vapoursynth.nfNoCache):
                    if isinstance(newval, VideoNode):
                        newval = c.std.Cache(clip=newval)
                    elif isinstance(newval, AudioNode):
                        newval = c.std.AudioCache(clip=newval)

                    if isinstance(newval, dict):
                        newval = newval['dict']
            elif proptype == 'v' or proptype =='w':
                newval = createConstFrame(funcs.propGetFrame(map, retkey, y, NULL), funcs, core)
            elif proptype == 'm':
                newval = createFuncRef(funcs.propGetFunc(map, retkey, y, NULL), funcs)

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

        if isinstance(val, (str, bytes, bytearray, RawNode)):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]     

        for v in val:
            if isinstance(v, RawNode):
                if funcs.propSetNode(inm, ckey, (<RawNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, RawFrame):
                if funcs.propSetFrame(inm, ckey, (<RawFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, Func):
                if funcs.propSetFunc(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif callable(v):
                tf = createFuncPython(v, core, funcs)

                if funcs.propSetFunc(inm, ckey, tf.ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, int):
                if funcs.propSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, float):
                if funcs.propSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, str):
                s = str(v).encode('utf-8')

                if funcs.propSetData(inm, ckey, s, -1, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, (bytes, bytearray)):
                if funcs.propSetData(inm, ckey, v, <int>len(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type (' + type(v).__name__ + ')')


cdef void typedDictToMap(dict ndict, dict atypes, VSMap *inm, VSCore *core, const VSAPI *funcs) except *:
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]
        if val is None:
            continue

        if isinstance(val, (str, bytes, bytearray, VideoNode)) or not isinstance(val, Iterable):
            val = [val]

        for v in val:
            if ((atypes[key][:4] == 'clip' or atypes[key][:5] == 'vnode') and isinstance(v, VideoNode)) or (atypes[key][:5] == 'anode' and isinstance(v, AudioNode)):
                if funcs.propSetNode(inm, ckey, (<RawNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif ((atypes[key][:5] == 'frame' or atypes[key][:6] == 'vframe') and isinstance(v, VideoFrame)) or (atypes[key][:6] == 'aframe' and isinstance(v, AudioFrame)):
                if funcs.propSetFrame(inm, ckey, (<RawFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and isinstance(v, Func):
                if funcs.propSetFunc(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and callable(v):
                tf = createFuncPython(v, core, funcs)
                if funcs.propSetFunc(inm, ckey, tf.ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:3] == 'int':
                if funcs.propSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:5] == 'float':
                if funcs.propSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'data':
                if not isinstance(v, (str, bytes, bytearray)):
                    v = str(v)
                if isinstance(v, str):
                    s = v.encode('utf-8')
                else:
                    s = v
                if funcs.propSetData(inm, ckey, s, <int>len(s), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type (expected ' + atypes[key] + ' compatible type but got ' + type(v).__name__ + ')')
        if len(val) == 0:
        # set an empty key if it's an empty array
            if atypes[key][:4] == 'clip' or atypes[key][:5] == 'vnode':
                funcs.propSetEmpty(inm, ckey, 'c')
            elif atypes[key][:5] == 'anode':
                funcs.propSetEmpty(inm, ckey, 'a')     
            elif atypes[key][:5] == 'frame' or atypes[key][:6] == 'vframe':
                funcs.propSetEmpty(inm, ckey, 'v')
            elif atypes[key][:6] == 'aframe':
                funcs.propSetEmpty(inm, ckey, 'w')   
            elif atypes[key][:4] == 'func':
                funcs.propSetEmpty(inm, ckey, 'm')
            elif atypes[key][:3] == 'int':
                funcs.propSetEmpty(inm, ckey, 'i')
            elif atypes[key][:5] == 'float':
                funcs.propSetEmpty(inm, ckey, 'f')
            elif atypes[key][:4] == 'data':
                funcs.propSetEmpty(inm, ckey, 's')
            else:
                raise Error('argument ' + key + ' has an unknown type: ' + atypes[key])

cdef class VideoFormat(object):
    cdef readonly int id
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
        return core.register_format(**vals)

    def __eq__(self, other):
        if not isinstance(other, VideoFormat):
            return False
        return other.id == self.id

    def __int__(self):
        return self.id

    def __str__(self):
        return ('Video Format Descriptor\n'
               f'\tId: {self.id:d}\n'
               f'\tName: {self.name}\n'
               f'\tColor Family: {self.color_family.name}\n'
               f'\tSample Type: {self.sample_type.name.capitalize()}\n'
               f'\tBits Per Sample: {self.bits_per_sample:d}\n'
               f'\tBytes Per Sample: {self.bytes_per_sample:d}\n'
               f'\tPlanes: {self.num_planes:d}\n'
               f'\tSubsampling W: {self.subsampling_w:d}\n'
               f'\tSubsampling H: {self.subsampling_h:d}\n')

cdef VideoFormat createVideoFormat(const VSVideoFormat *f):
    cdef VideoFormat instance = VideoFormat.__new__(VideoFormat)
    instance.id = f.id
    instance.name = (<const char *>f.name).decode('utf-8')
    instance.color_family = ColorFamily(f.colorFamily)
    instance.sample_type = SampleType(f.sampleType)
    instance.bits_per_sample = f.bitsPerSample
    instance.bytes_per_sample = f.bytesPerSample
    instance.subsampling_w = f.subSamplingW
    instance.subsampling_h = f.subSamplingH
    instance.num_planes = f.numPlanes
    return instance
    
cdef class AudioFormat(object):
    cdef readonly int id
    cdef readonly str name
    cdef readonly object sample_type
    cdef readonly int bits_per_sample
    cdef readonly int bytes_per_sample
    cdef readonly int samples_per_frame
    cdef readonly int64_t channel_layout
    cdef readonly int num_channels

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def _as_dict(self):
        return {
            'sample_type': self.sample_type,
            'bits_per_sample': self.bits_per_sample,
            'samples_per_frame': self.samples_per_frame,
            'channel_layout': self.channel_layout,
            'num_channels': self.num_channels
        }

    def replace(self, **kwargs):
        core = kwargs.pop("core", None) or get_core()
        vals = self._as_dict()
        vals.update(**kwargs)
        return core.register_format(**vals)

    def __eq__(self, other):
        if not isinstance(other, AudioFormat):
            return False
        return other.id == self.id

    def __int__(self):
        return self.id

    def __str__(self):
        return ('Audio Format Descriptor\n'
               f'\tId: {self.id:d}\n'
               f'\tName: {self.name}\n'
               f'\tSample Type: {self.sample_type.name.capitalize()}\n'
               f'\tBits Per Sample: {self.bits_per_sample:d}\n'
               f'\tBytes Per Sample: {self.bytes_per_sample:d}\n'
               f'\tSamples Per Frame: {self.samples_per_frame:d}\n')

cdef AudioFormat createAudioFormat(const VSAudioFormat *f):
    cdef AudioFormat instance = AudioFormat.__new__(AudioFormat)
    instance.id = f.id
    instance.name = (<const char *>f.name).decode('utf-8')
    instance.sample_type = SampleType(f.sampleType)
    instance.bits_per_sample = f.bitsPerSample
    instance.bytes_per_sample = f.bytesPerSample
    instance.samples_per_frame = f.samplesPerFrame
    instance.channel_layout = f.channelLayout
    instance.num_channels = f.numChannels
    return instance

cdef class FrameProps(object):
    cdef const VSFrameRef *constf
    cdef VSFrameRef *f
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef bint readonly

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFrame(self.constf)

    def __contains__(self, str name):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        cdef bytes b = name.encode('utf-8')
        cdef int numelem = self.funcs.propNumElements(m, b)
        return numelem > 0

    def __getitem__(self, str name):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        cdef bytes b = name.encode('utf-8')
        cdef list ol = []
        cdef int numelem = self.funcs.propNumElements(m, b)
        cdef const int64_t *intArray
        cdef const double *floatArray
        cdef const char *data

        if numelem < 0:
            raise KeyError('No key named ' + name + ' exists')
        cdef char t = self.funcs.propGetType(m, b)
        if t == 'i':
            if numelem > 0:
                intArray = self.funcs.propGetIntArray(m, b, NULL)
                for i in range(numelem):
                    ol.append(intArray[i])
        elif t == 'f':
            if numelem > 0:
                floatArray = self.funcs.propGetFloatArray(m, b, NULL)
                for i in range(numelem):
                    ol.append(floatArray[i])
        elif t == 's':
            for i in range(numelem):
                data = self.funcs.propGetData(m, b, i, NULL)
                ol.append(data[:self.funcs.propGetDataSize(m, b, i, NULL)])
        elif t == 'c' or t == 'a':
            for i in range(numelem):
                ol.append(createNode(self.funcs.propGetNode(m, b, i, NULL), self.funcs, _get_core()))
        elif t == 'v' or t == 'w':
            for i in range(numelem):
                ol.append(createConstFrame(self.funcs.propGetFrame(m, b, i, NULL), self.funcs, self.core))
        elif t == 'm':
            for i in range(numelem):
                ol.append(createFuncRef(self.funcs.propGetFunc(m, b, i, NULL), self.funcs))

        if len(ol) == 1:
            return ol[0]
        else:
            return ol

    def __setitem__(self, str name, value):
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropsRW(self.f)
        cdef bytes b = name.encode('utf-8')
        cdef const VSAPI *funcs = self.funcs
        val = value
        if isinstance(val, (str, bytes, bytearray, VideoNode)):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]
        self.__delitem__(name)
        try:
            for v in val:
                if isinstance(v, VideoNode):
                    if funcs.propSetNode(m, b, (<VideoNode>v).node, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, VideoFrame):
                    if funcs.propSetFrame(m, b, (<VideoFrame>v).constf, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, Func):
                    if funcs.propSetFunc(m, b, (<Func>v).ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif callable(v):
                    tf = createFuncPython(v, self.core, self.funcs)
                    if funcs.propSetFunc(m, b, tf.ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, int):
                    if funcs.propSetInt(m, b, int(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, float):
                    if funcs.propSetFloat(m, b, float(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, str):
                    s = str(v).encode('utf-8')
                    if funcs.propSetData(m, b, s, -1, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, (bytes, bytearray)):
                    if funcs.propSetData(m, b, v, <int>len(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                else:
                    raise Error('Setter was passed an unsupported type (' + type(v).__name__ + ')')
        except Error:
            self.__delitem__(name)
            raise

    def __delitem__(self, str name):
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropsRW(self.f)
        cdef bytes b = name.encode('utf-8')
        self.funcs.propDeleteKey(m, b)

    def __setattr__(self, name, value):
        self[name] = value

    def __delattr__(self, name):
        del self[name]

    # Only the methods __getattr__ and keys are required for the support of
    #     >>> dict(frame.props)
    # this can be shown at Objects/dictobject.c:static int dict_merge(PyObject *, PyObject *, int)
    # in the generic code path.

    def __getattr__(self, name):
        try:
           return self[name]
        except KeyError as e:
           raise AttributeError from e

    def keys(self):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        cdef int numkeys = self.funcs.propNumKeys(m)
        result = set()
        for i in range(numkeys):
            result.add(self.funcs.propGetKey(m, i).decode('utf-8'))
        return result

    def values(self):
        return {self[key] for key in self.keys()}

    def items(self):
        return {(key, self[key]) for key in self.keys()}

    def get(self, key, default=None):
        if key in self:
            return self[key]
        return default

    def pop(self, key, default=_EMPTY):
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
        if len(self) <= 0:
            raise KeyError
        key = next(iter(self.keys()))
        return (key, self.pop(key))

    def setdefault(self, key, default=0):
        """
        Behaves like the dict.setdefault function but since setting None is not supported,
        it will default to zero.
        """
        if key not in self:
            self[key] = default
        return self[key]

    def update(self, *args, **kwargs):
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
        for _ in range(len(self)):
            self.popitem()

    def copy(self):
        """
        We can't copy VideoFrames directly, so we're just gonna return a real dictionary.
        """
        return dict(self)

    def __iter__(self):
        yield from self.keys()

    def __len__(self):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        return self.funcs.propNumKeys(m)

    def __dir__(self):
        return super(FrameProps, self).__dir__() + list(self.keys())

    def __repr__(self):
        return "<vapoursynth.FrameProps %r>" % dict(self)

cdef FrameProps createFrameProps(RawFrame f):
    cdef FrameProps instance = FrameProps.__new__(FrameProps)
# since the vsapi only returns const refs when cloning a VSFrameRef it is safe to cast away the const here
    instance.constf = f.funcs.cloneFrameRef(f.constf)
    instance.f = NULL
    instance.funcs = f.funcs
    instance.core = f.core
    instance.readonly = f.readonly
    if not instance.readonly:
        instance.f = <VSFrameRef *>instance.constf
    return instance

# Make sure the FrameProps-Object quacks like a Mapping.
Mapping.register(FrameProps)

cdef class RawFrame(object):
    cdef const VSFrameRef *constf
    cdef VSFrameRef *f
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef readonly bint readonly
    cdef readonly FrameProps props
    
    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeFrame(self.constf)


cdef class VideoFrame(RawFrame):
    cdef readonly VideoFormat format
    cdef readonly int width
    cdef readonly int height

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def copy(self):
        return createVideoFrame(self.funcs.copyFrame(self.constf, self.core), self.funcs, self.core)

    def get_read_ptr(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef const uint8_t *d = self.funcs.getReadPtr(self.constf, plane)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_read_array(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef const uint8_t *d = self.funcs.getReadPtr(self.constf, plane)
        stride = self.get_stride(plane) // self.format.bytes_per_sample
        width = self.width
        height = self.height
        if plane is not 0:
            height >>= self.format.subsampling_h
            width >>= self.format.subsampling_w
        array = None
        if self.format.sample_type == INTEGER:
            if self.format.bytes_per_sample == 1:
                array = <uint8_t[:height, :stride]> d
            elif self.format.bytes_per_sample == 2:
                array = <uint16_t[:height, :stride]> (<uint16_t*>d)
            elif self.format.bytes_per_sample == 4:
                array = <uint32_t[:height, :stride]> (<uint32_t*>d)
        elif self.format.sample_type == FLOAT:
            array = <float[:height, :stride]> (<float*>d)
        if array is not None:
            return array[:height, :width]
        return None

    def get_write_ptr(self, int plane):
        if self.readonly:
            raise Error('Cannot obtain write pointer to read only frame')
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef uint8_t *d = self.funcs.getWritePtr(self.f, plane)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_write_array(self, int plane):
        if self.readonly:
            raise Error('Cannot obtain write pointer to read only frame')
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef uint8_t *d = self.funcs.getWritePtr(self.f, plane)
        stride = self.get_stride(plane) // self.format.bytes_per_sample
        width = self.width
        height = self.height
        if plane is not 0:
            height >>= self.format.subsampling_h
            width >>= self.format.subsampling_w
        array = None
        if self.format.sample_type == INTEGER:
            if self.format.bytes_per_sample == 1:
                array = <uint8_t[:height, :stride]> d
            elif self.format.bytes_per_sample == 2:
                array = <uint16_t[:height, :stride]> (<uint16_t*>d)
            elif self.format.bytes_per_sample == 4:
                array = <uint32_t[:height, :stride]> (<uint32_t*>d)
        elif self.format.sample_type == FLOAT:
            array = <float[:height, :stride]> (<float*>d)
        if array is not None:
            return array[:height, :width]
        return None

    def get_stride(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        return self.funcs.getStride(self.constf, plane)

    def planes(self):
        cdef int x
        for x in range(self.format.num_planes):
            yield VideoPlane.__new__(VideoPlane, self, x)

    def __str__(self):
        cdef str s = 'VideoFrame\n'
        s += '\tFormat: ' + self.format.name + '\n'
        s += '\tWidth: ' + str(self.width) + '\n'
        s += '\tHeight: ' + str(self.height) + '\n'
        return s


cdef VideoFrame createConstVideoFrame(const VSFrameRef *constf, const VSAPI *funcs, VSCore *core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = constf
    instance.f = NULL
    instance.funcs = funcs
    instance.core = core
    instance.readonly = True
    instance.format = createVideoFormat(funcs.getFrameFormat(constf))
    instance.width = funcs.getFrameWidth(constf, 0)
    instance.height = funcs.getFrameHeight(constf, 0)
    instance.props = createFrameProps(instance)
    return instance


cdef VideoFrame createVideoFrame(VSFrameRef *f, const VSAPI *funcs, VSCore *core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = f
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.readonly = False
    instance.format = createVideoFormat(funcs.getFrameFormat(f))
    instance.width = funcs.getFrameWidth(f, 0)
    instance.height = funcs.getFrameHeight(f, 0)
    instance.props = createFrameProps(instance)
    return instance


cdef class VideoPlane:
    cdef VideoFrame frame
    cdef int plane
    cdef Py_ssize_t shape[2]
    cdef Py_ssize_t strides[2]
    cdef char* format

    def __cinit__(self, VideoFrame frame, int plane):
        cdef Py_ssize_t itemsize

        if not (0 <= plane < frame.format.num_planes):
            raise IndexError("specified plane index out of range")

        self.shape[1] = <Py_ssize_t> frame.width
        self.shape[0] = <Py_ssize_t> frame.height
        if plane:
            self.shape[1] >>= <Py_ssize_t> frame.format.subsampling_w
            self.shape[0] >>= <Py_ssize_t> frame.format.subsampling_h

        self.strides[1] = itemsize = <Py_ssize_t> frame.format.bytes_per_sample
        self.strides[0] = <Py_ssize_t> frame.funcs.getStride(frame.constf, plane)

        if frame.format.sample_type == INTEGER:
            if itemsize == 1:
                self.format = b'B'
            elif itemsize == 2:
                self.format = b'H'
            elif itemsize == 4:
                self.format = b'I'
        elif frame.format.sample_type == FLOAT:
            if itemsize == 2:
                self.format = b'e'
            elif itemsize == 4:
                self.format = b'f'

        self.frame = frame
        self.plane = plane

    @property
    def width(self):
        """Plane's pixel width."""
        if self.plane:
            return self.frame.width >> self.frame.format.subsampling_w
        return self.frame.width

    @property
    def height(self):
        """Plane's pixel height."""
        if self.plane:
            return self.frame.height >> self.frame.format.subsampling_h
        return self.frame.height

    def __getbuffer__(self, Py_buffer* view, int flags):
        if (flags & PyBUF_F_CONTIGUOUS) == PyBUF_F_CONTIGUOUS:
            raise BufferError("C-contiguous buffer only.")

        if self.frame.readonly:
            if flags & PyBUF_WRITABLE:
                raise BufferError("Object is not writable.")
            view.buf = (<void*> self.frame.funcs.getReadPtr(self.frame.constf, self.plane))
        else:
            view.buf = (<void*> self.frame.funcs.getWritePtr(self.frame.f, self.plane))

        if flags & PyBUF_STRIDES:
            view.shape = self.shape
            view.strides = self.strides
        else:
            view.shape = NULL
            view.strides = NULL

        if flags & PyBUF_FORMAT:
            view.format = self.format
        else:
            view.format = NULL

        view.obj = self
        view.len = self.shape[0] * self.shape[1] * self.strides[1]
        view.readonly = self.frame.readonly
        view.itemsize = self.strides[1]
        view.ndim = 2
        view.suboffsets = NULL
        view.internal = NULL


cdef class AudioFrame(RawFrame):
    cdef readonly AudioFormat format
    cdef readonly int num_samples # FIXME, not set or implemented

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def copy(self):
        return createAudioFrame(self.funcs.copyFrame(self.constf, self.core), self.funcs, self.core)

    def get_read_ptr(self, int channel):
        if channel < 0 or channel >= self.format.num_channels:
            raise IndexError('Specified channel index out of range')
        cdef const uint8_t *d = self.funcs.getReadPtr(self.constf, channel)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_write_ptr(self, int channel):
        if self.readonly:
            raise Error('Cannot obtain write pointer to read only frame')
        if channel < 0 or channel >= self.format.num_channels:
            raise IndexError('Specified channel index out of range')
        cdef uint8_t *d = self.funcs.getWritePtr(self.f, channel)
        return ctypes.c_void_p(<uintptr_t>d)

    def __str__(self):
        cdef str s = 'AudioFrame\n'
        s += '\tFormat: ' + self.format.name + '\n'
        return s


cdef AudioFrame createConstAudioFrame(const VSFrameRef *constf, const VSAPI *funcs, VSCore *core):
    cdef AudioFrame instance = AudioFrame.__new__(AudioFrame)
    instance.constf = constf
    instance.f = NULL
    instance.funcs = funcs
    instance.core = core
    instance.readonly = True
    instance.format = createAudioFormat(funcs.getAudioFrameFormat(constf))
    instance.props = createFrameProps(instance)
    return instance


cdef AudioFrame createAudioFrame(VSFrameRef *f, const VSAPI *funcs, VSCore *core):
    cdef AudioFrame instance = AudioFrame.__new__(AudioFrame)
    instance.constf = f
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.readonly = False
    instance.format = createAudioFormat(funcs.getAudioFrameFormat(f))
    instance.props = createFrameProps(instance)
    return instance


cdef class RawNode(object):
    cdef VSNodeRef *node
    cdef const VSAPI *funcs
    cdef Core core
   
    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

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
    cdef readonly int flags

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
            raise AttributeError('There is no attribute or namespace named ' + name)

    cdef ensure_valid_frame_number(self, int n):
        if n < 0:
            raise ValueError('Requesting negative frame numbers not allowed')
        if (self.num_frames > 0) and (n >= self.num_frames):
            raise ValueError('Requesting frame number is beyond the last frame')

    def get_frame(self, int n):
        cdef char errorMsg[512]
        cdef char *ep = errorMsg
        cdef const VSFrameRef *f
        self.ensure_valid_frame_number(n)

        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 500)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createConstVideoFrame(f, self.funcs, self.core.core)

    def get_frame_async_raw(self, int n, object cb, object future_wrapper=None):
        self.ensure_valid_frame_number(n)

        data = createRawCallbackData(self.funcs, self, cb, future_wrapper)
        Py_INCREF(data)
        with nogil:
            self.funcs.getFrameAsync(n, self.node, frameDoneCallbackRaw, <void *>data)

    def get_frame_async(self, int n):
        from concurrent.futures import Future
        fut = Future()
        fut.set_running_or_notify_cancel()

        try:
            self.get_frame_async_raw(n, fut)
        except Exception as e:
            fut.set_exception(e)

        return fut

    def set_output(self, int index = 0, VideoNode alpha = None):
        cdef const VSVideoFormat *aformat = NULL
        clip = self
        if alpha is not None:
            if (self.vi.width != alpha.vi.width) or (self.vi.height != alpha.vi.height):
                raise Error('Alpha clip dimensions must match the main video')
            if (self.num_frames != alpha.num_frames):
                raise Error('Alpha clip length must match the main video')
            if (self.vi.format) and (alpha.vi.format):
                if (alpha.vi.format != self.funcs.registerFormat(GRAY, self.vi.format.sampleType, self.vi.format.bitsPerSample, 0, 0, self.core.core)):
                    raise Error('Alpha clip format must match the main video')
            elif (self.vi.format) or (alpha.vi.format):
                raise Error('Format must be either known or unknown for both alpha and main clip')
            
            clip = AlphaOutputTuple(self, alpha)

        _get_output_dict("set_output")[index] = clip

    def output(self, object fileobj not None, bint y4m = False, object progress_update = None, int prefetch = 0):
        if prefetch < 1:
            prefetch = self.core.num_threads
            
        # stdout usually isn't in binary mode so let's automatically compensate for that
        if fileobj == sys.stdout:
            fileobj = sys.stdout.buffer
            
        cdef CallbackData d = CallbackData(fileobj, min(prefetch, self.num_frames), self.num_frames, self.format.num_planes, y4m, self, progress_update)

        # this is also an implicit test that the progress_update callback at least vaguely matches the requirements
        if (progress_update is not None):
            progress_update(0, d.total)

        if (self.format is None or self.format.color_family not in (YUV, GRAY)) and y4m:
            raise Error('Can only apply y4m headers to YUV and Gray format clips')

        y4mformat = ''
        numbits = ''

        if y4m:
            if self.format.color_family == GRAY:
                y4mformat = 'mono'
                if self.format.bits_per_sample > 8:
                    y4mformat = y4mformat + str(self.format.bits_per_sample)
            elif self.format.color_family == YUV:
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

        if len(y4mformat) > 0:
            y4mformat = 'C' + y4mformat + ' '

        cdef str header = 'YUV4MPEG2 ' + y4mformat + 'W' + str(self.width) + ' H' + str(self.height) + ' F' + str(self.fps_num) + ':' + str(self.fps_den) + ' Ip A0:0\n'
        if y4m:
            fileobj.write(header.encode('utf-8'))
        d.condition.acquire()

        for n in range(min(prefetch, d.total)):
            self.funcs.getFrameAsync(n, self.node, frameDoneCallbackOutput, <void *>d)

        stored_exception = None
        while d.total != d.completed:
            try:
                d.condition.wait()
            except BaseException, e:
                d.total = d.requested
                stored_exception = e
        d.condition.release()
        
        if stored_exception is not None:
            raise stored_exception

        if d.error:
            raise Error(d.error)
            
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

    def frames(self):
        for frameno in range(len(self)):
            yield self.get_frame(frameno)
            
    def __dir__(self):
        plugins = [plugin["namespace"] for plugin in self.core.get_plugins().values()]
        return super(VideoNode, self).__dir__() + plugins

    def __len__(self):
        return self.num_frames

    def __str__(self):
        cdef str s = 'VideoNode\n'

        if self.format:
            s += '\tFormat: ' + self.format.name + '\n'
        else:
            s += '\tFormat: dynamic\n'

        if not self.width or not self.height:
            s += '\tWidth: dynamic\n'
            s += '\tHeight: dynamic\n'
        else:
            s += '\tWidth: ' + str(self.width) + '\n'
            s += '\tHeight: ' + str(self.height) + '\n'

        s += '\tNum Frames: ' + str(self.num_frames) + '\n'

        s += <str>f"\tFPS: {self.fps or 'dynamic'}\n"

        if self.flags:
            s += '\tFlags:'
            if (self.flags & vapoursynth.nfNoCache):
                s += ' NoCache'
            if (self.flags & vapoursynth.nfIsCache):
                s += ' IsCache'
            if (self.flags & vapoursynth.nfMakeLinear):
                s += ' MakeLinear'
            s += '\n'
        else:
            s += '\tFlags: None\n'

        return s

cdef VideoNode createVideoNode(VSNodeRef *node, const VSAPI *funcs, Core core):
    cdef VideoNode instance = VideoNode.__new__(VideoNode)
    instance.core = core
    instance.node = node
    instance.funcs = funcs
    instance.vi = funcs.getVideoInfo(node)

    if (instance.vi.format):
        instance.format = createVideoFormat(instance.vi.format)
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
    instance.flags = instance.vi.flags
    return instance
    
cdef class AudioNode(RawNode):
    cdef const VSAudioInfo *ai
    cdef readonly AudioFormat format
    cdef readonly int sample_rate
    cdef readonly int64_t num_samples
    cdef readonly int num_frames
    cdef readonly int flags
    
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
            raise AttributeError('There is no attribute or namespace named ' + name)

    cdef ensure_valid_frame_number(self, int n):
        if n < 0:
            raise ValueError('Requesting negative frame numbers not allowed')
        if (self.num_frames > 0) and (n >= self.num_frames):
            raise ValueError('Requesting frame number is beyond the last frame')

    def get_frame(self, int n):
        cdef char errorMsg[512]
        cdef char *ep = errorMsg
        cdef const VSFrameRef *f
        self.ensure_valid_frame_number(n)

        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 500)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createConstAudioFrame(f, self.funcs, self.core.core)

    def set_output(self, int index = 0):
        _get_output_dict("set_output")[index] = self
            
    def __add__(x, y):
        if not isinstance(x, AudioNode) or not isinstance(y, AudioNode):
            return NotImplemented
        return (<AudioNode>x).core.std.AudioSplice(clips=[x, y])

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

    def frames(self):
        for frameno in range(len(self)):
            yield self.get_frame(frameno)
            
    def __dir__(self):
        plugins = [plugin["namespace"] for plugin in self.core.get_plugins().values()]
        return super(AudioNode, self).__dir__() + plugins

    def __len__(self):
        return self.num_samples

    def __str__(self):
        cdef str s = 'AudioNode\n'

        s += '\tFormat: ' + self.format.name + '\n'
        s += '\tSample Rate: ' + str(self.sample_rate) + '\n'
        s += '\tNum Samples: ' + str(self.num_samples) + '\n'
        s += '\tNum Frames: ' + str(self.num_frames) + '\n'
        
        if self.flags:
            s += '\tFlags:'
            if (self.flags & vapoursynth.nfNoCache):
                s += ' NoCache'
            if (self.flags & vapoursynth.nfIsCache):
                s += ' IsCache'
            if (self.flags & vapoursynth.nfMakeLinear):
                s += ' MakeLinear'
            s += '\n'
        else:
            s += '\tFlags: None\n'

        return s
    
cdef AudioNode createAudioNode(VSNodeRef *node, const VSAPI *funcs, Core core):
    cdef AudioNode instance = AudioNode.__new__(AudioNode)
    instance.core = core
    instance.node = node
    instance.funcs = funcs
    instance.ai = funcs.getAudioInfo(node)
    instance.format = createAudioFormat(instance.ai.format)
    instance.sample_rate = instance.ai.sampleRate
    instance.num_samples = instance.ai.numSamples
    instance.num_frames = instance.ai.numFrames
    instance.flags = instance.ai.flags
    return instance

cdef class Core(object):
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef public bint add_cache

    cdef object __weakref__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeCore(self.core)
            
    property num_threads:
        def __get__(self):
            cdef VSCoreInfo v
            self.funcs.getCoreInfo2(self.core, &v)
            return v.numThreads
        
        def __set__(self, int value):
            self.funcs.setThreadCount(value, self.core)
            
    property max_cache_size:
        def __get__(self):
            cdef VSCoreInfo v
            self.funcs.getCoreInfo2(self.core, &v)
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

    def __getattr__(self, name):
        cdef VSPlugin *plugin
        tname = name.encode('utf-8')
        cdef const char *cname = tname
        plugin = self.funcs.getPluginByNs(cname, self.core)

        if plugin:
            return createPlugin(plugin, name, self.funcs, self)
        else:
            raise AttributeError('No attribute with the name ' + name + ' exists. Did you mistype a plugin namespace?')

    def set_max_cache_size(self, int mb):
        self.max_cache_size = mb
        return self.max_cache_size

    def get_plugins(self):
        cdef VSMap *m = self.funcs.getPlugins(self.core)
        cdef VSMap *n
        cdef bytes b
        cdef dict sout = {}

        for i in range(self.funcs.propNumKeys(m)):
            a = self.funcs.propGetData(m, self.funcs.propGetKey(m, i), 0, NULL)
            a = a.decode('utf-8')
            a = a.split(';', 2)

            plugin_dict = {}
            plugin_dict['namespace'] = a[0]
            plugin_dict['identifier'] = a[1]
            plugin_dict['name'] = a[2]

            function_dict = {}

            b = a[1].encode('utf-8')
            n = self.funcs.getFunctions(self.funcs.getPluginById(b, self.core))

            for j in range(self.funcs.propNumKeys(n)):
                c = self.funcs.propGetData(n, self.funcs.propGetKey(n, j), 0, NULL)
                c = c.decode('utf-8')
                c = c.split(';', 1)
                function_dict[c[0]] = c[1]

            plugin_dict['functions'] = function_dict
            sout[plugin_dict['identifier']] = plugin_dict
            self.funcs.freeMap(n)

        self.funcs.freeMap(m)
        return sout

    def list_functions(self):
        sout = ""
        plugins = self.get_plugins()
        for plugin in sorted(plugins.keys()):
            sout += 'name: ' + plugins[plugin]['name'] + '\n'
            sout += 'namespace: ' + plugins[plugin]['namespace'] + '\n'
            sout += 'identifier: ' + plugins[plugin]['identifier'] + '\n'
            for function in sorted(plugins[plugin]['functions'].keys()):
                line = '\t' + function + '(' + plugins[plugin]['functions'][function].replace(';', '; ') + ')\n'
                sout += line.replace('; )', ')')
        return sout

    def register_format(self, int color_family, int sample_type, int bits_per_sample, int subsampling_w, int subsampling_h):
        cdef const VSVideoFormat *fmt = self.funcs.registerFormat(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h, self.core)
        if fmt == NULL:
            raise Error('Invalid format specified')
        return createVideoFormat(fmt)

    def get_format(self, int id):
        cdef const VSVideoFormat *f = self.funcs.getFormatPreset(id, self.core)

        if f == NULL:
            raise Error('Format not registered')
        else:
            return createVideoFormat(f)

    def version(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo2(self.core, &v)
        return (<const char *>v.versionString).decode('utf-8')
        
    def version_number(self):
        cdef VSCoreInfo v
        self.funcs.getCoreInfo2(self.core, &v)
        return v.core
        
    def __dir__(self):
        plugins = [plugin["namespace"] for plugin in self.get_plugins().values()]
        return super(Core, self).__dir__() + plugins

    def __str__(self):
        cdef str s = 'Core\n'
        s += self.version() + '\n'
        s += '\tNumber of Threads: ' + str(self.num_threads) + '\n'
        s += '\tAdd Cache: ' + str(self.add_cache) + '\n'
        return s

cdef object createNode(VSNodeRef *node, const VSAPI *funcs, Core core):
    if funcs.getNodeType(node) == VIDEO:
        return createVideoNode(node, funcs, core)
    else:
        return createAudioNode(node, funcs, core)

cdef object createConstFrame(VSFrameRef *f, const VSAPI *funcs, VSCore *core):
    if funcs.getFrameType(f) == VIDEO:
        return createConstVideoFrame(f, funcs, core)
    else:
        return createConstAudioFrame(f, funcs, core)

cdef Core createCore():
    cdef Core instance = Core.__new__(Core)
    instance.funcs = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    if instance.funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. System does not support SSE2 or is the Python module and loaded core library mismatched?')
    instance.core = instance.funcs.createCore(0)
    instance.add_cache = True
    return instance

def _get_core(threads = None, add_cache = None):
    env = _env_current()
    if env is None:
        raise Error('Internal environment id not set. Was get_core() called from a filter callback?')

    return vsscript_get_core_internal(env)
    
def get_core(threads = None, add_cache = None):
    import warnings
    warnings.warn("get_core() is deprecated. Use \"vapoursynth.core\" instead.", DeprecationWarning)
    
    ret_core = _get_core()
    if ret_core is not None:
        if threads is not None:
            ret_core.num_threads = threads
        if add_cache is not None:
            ret_core.add_cache = add_cache
    return ret_core
    
cdef Core vsscript_get_core_internal(EnvironmentData env):
    if env.core is None:
        env.core = createCore()
    return env.core
    
cdef class _CoreProxy(object):

    def __init__(self):
        raise Error('Class cannot be instantiated directly')
    
    @property
    def core(self):
        return _get_core()
        
    def __dir__(self):
        d = dir(self.core)
        if 'core' not in d:
            d += ['core']
            
        return d
        
    def __getattr__(self, name):
        return getattr(self.core, name)
        
    def __setattr__(self, name, value):
        setattr(self.core, name, value)
    
core = _CoreProxy.__new__(_CoreProxy)
    

cdef class Plugin(object):
    cdef Core core
    cdef VSPlugin *plugin
    cdef const VSAPI *funcs
    cdef object injected_arg
    cdef readonly str namespace
    cdef readonly str __doc__

    def __init__(self):
        raise Error('Class cannot be instantiated directly')
        

    def __getattr__(self, name):
        tname = name.encode('utf-8')
        cdef const char *cname = tname
        cdef VSMap *m = self.funcs.getFunctions(self.plugin)
        match = False

        for i in range(self.funcs.propNumKeys(m)):
            cname = self.funcs.propGetKey(m, i)
            orig_name = cname.decode('utf-8')

            if orig_name == name:
                match = True
                break

        if match:
            signature = self.funcs.propGetData(m, cname, 0, NULL).decode('utf-8')
            signature = signature.split(';', 1)
            self.funcs.freeMap(m)
            return createFunction(orig_name, signature[1], self, self.funcs)
        else:
            self.funcs.freeMap(m)
            raise AttributeError('There is no function named ' + name)

    def get_functions(self):
        cdef VSMap *n
        cdef bytes b
        cdef dict sout = {}

        n = self.funcs.getFunctions(self.plugin)

        for j in range(self.funcs.propNumKeys(n)):
            c = self.funcs.propGetData(n, self.funcs.propGetKey(n, j), 0, NULL)
            c = c.decode('utf-8')
            c = c.split(';', 1)
            sout[c[0]] = c[1];

        self.funcs.freeMap(n)
        return sout

    def list_functions(self):
        sout = ""
        functions = self.get_functions()
        for key in sorted(functions.keys()):
            sout += key + '(' + functions[key].replace(';', '; ') + ')\n'
        return sout.replace('; )', ')')

    def __dir__(self):
        attrs = []
        functions = self.get_functions()
        for key in functions:
            attrs.append(key)
        return attrs

cdef Plugin createPlugin(VSPlugin *plugin, str namespace, const VSAPI *funcs, Core core):
    cdef Plugin instance = Plugin.__new__(Plugin)
    instance.core = core
    instance.plugin = plugin
    instance.funcs = funcs
    instance.injected_arg = None
    instance.namespace = namespace

    for plugin_dict in core.get_plugins().values():
        if plugin_dict['namespace'] == namespace:
            instance.__doc__ = plugin_dict['name']
            break

    return instance

cdef class Function(object):
    cdef const VSAPI *funcs
    cdef readonly Plugin plugin
    cdef readonly str name
    cdef readonly str signature
    
    @property
    def __signature__(self):
        if typing is None:
            return None
        return construct_signature(self.signature, injected=self.plugin.injected_arg)

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

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
            if key[0] == '_':
                nkey = key[1:]
            # PEP8 tells us single_trailing_underscore_ for collisions with Python-keywords.
            elif key[-1] == "_":
                nkey = key[:-1]
            else:
                nkey = key
            ndict[nkey] = kwargs[key]

        # match up unnamed arguments to the first unused name in order
        sigs = self.signature.split(';')

        for sig in sigs:
            if sig == '':
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

        if len(ndict) > 0:
            raise Error(self.name + ': Function does not take argument(s) named ' + ', '.join(ndict.keys()))

        inm = self.funcs.createMap()

        dtomsuccess = True
        dtomexceptmsg = ''
        try:
            typedDictToMap(processed, atypes, inm, self.plugin.core.core, self.funcs)
        except Error as e:
            self.funcs.freeMap(inm)
            dtomsuccess = False
            dtomexceptmsg = str(e)    
        
        if dtomsuccess == False:
            raise Error(self.name + ': ' + dtomexceptmsg)

        tname = self.name.encode('utf-8')
        cname = tname
        with nogil:
            outm = self.funcs.invoke(self.plugin.plugin, cname, inm)
        self.funcs.freeMap(inm)
        cdef const char *err = self.funcs.getError(outm)
        cdef bytes emsg

        if err:
            emsg = err
            self.funcs.freeMap(outm)
            raise Error(emsg.decode('utf-8'))

        retdict = mapToDict(outm, True, self.plugin.core.add_cache, self.plugin.core.core, self.funcs)
        self.funcs.freeMap(outm)
        return retdict

cdef Function createFunction(str name, str signature, Plugin plugin, const VSAPI *funcs):
    cdef Function instance = Function.__new__(Function)
    instance.name = name
    instance.signature = signature
    instance.plugin = plugin
    instance.funcs = funcs
    return instance

# for python functions being executed by vs

cdef void __stdcall freeFunc(void *pobj) nogil:
    with gil:
        fobj = <FuncData>pobj
        Py_DECREF(fobj)
        fobj = None


cdef void __stdcall publicFunction(const VSMap *inm, VSMap *outm, void *userData, VSCore *core, const VSAPI *vsapi) nogil:
    with gil:
        d = <FuncData>userData
        try:
            with use_environment(d.env).use():
                m = mapToDict(inm, False, False, core, vsapi)
                ret = d(**m)
                if not isinstance(ret, dict):
                    if ret is None:
                        ret = 0
                    ret = {'val':ret}
                dictToMap(ret, outm, core, vsapi)
        except BaseException, e:
            emsg = str(e).encode('utf-8')
            vsapi.setError(outm, emsg)


@final
cdef class VSScriptEnvironmentPolicy:
    cdef dict _env_map
    cdef dict _known_environments

    cdef object _stack
    cdef object _lock
    cdef EnvironmentPolicyAPI _api

    cdef object __weakref__

    def __init__(self):
        raise RuntimeError("Cannot instantiate this class directly.")

    def on_policy_registered(self, policy_api):
        self._env_map = {}
        self._known_environments = {}
        self._stack = ThreadLocal()
        self._lock = Lock()
        self._api = policy_api

    def on_policy_cleared(self):
        with self._lock:
            self._known_environments = None
            self._env_map = None
            self._stack = None
            self._lock = None

    cdef EnvironmentData get_environment(self, id):
        return self._env_map.get(id, None)

    def get_current_environment(self):
        return getattr(self._stack, "stack", None)

    def set_environment(self, environment):
        previous = getattr(self._stack, "stack", None)
        self._stack.stack = environment
        return previous

    cdef EnvironmentData _make_environment(self, int script_id):
        env = self._api.create_environment()
        self._env_map[script_id] = env
        self._known_environments[id(env)] = env
        return env
      
    cdef has_environment(self, int script_id):
        return id in self._env_map

    cdef _free_environment(self, int script_id):
        env = self._env_map.pop(script_id, None)
        if env is not None:
            self._known_environments.pop(id(env))
        
    def is_alive(self, env):
        return id(env) in self._known_environments


cdef VSScriptEnvironmentPolicy _get_vsscript_policy():
    if not isinstance(_policy, VSScriptEnvironmentPolicy):
        raise RuntimeError("This is not a VSScript-Policy.")
    return <VSScriptEnvironmentPolicy>_policy


cdef object _vsscript_use_environment(int id):
    return use_environment(_get_vsscript_policy().get_environment(id))


cdef object _vsscript_use_or_create_environment(int id):
    cdef VSScriptEnvironmentPolicy policy = _get_vsscript_policy()
    if not policy.has_environment(id):
        policy._make_environment(id)
    return use_environment(policy.get_environment(id))


# for whole script evaluation and export
cdef public struct VPYScriptExport:
    void *pyenvdict
    void *errstr
    int id


cdef public api int vpy_createScript(VPYScriptExport *se) nogil:
    with gil:
        try:
            evaldict = {}
            Py_INCREF(evaldict)
            se.pyenvdict = <void *>evaldict

            _get_vsscript_policy()._make_environment(<int>se.id)

        except:
            errstr = 'Unspecified Python exception' + '\n\n' + traceback.format_exc()
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 1
        return 0         
    
cdef public api int vpy_evaluateScript(VPYScriptExport *se, const char *script, const char *scriptFilename, int flags) nogil:
    with gil:
        orig_path = None
        try:
            evaldict = {}
            if se.pyenvdict:
                evaldict = <dict>se.pyenvdict
            else:
                Py_INCREF(evaldict)
                se.pyenvdict = <void *>evaldict

                _get_vsscript_policy().get_environment(se.id).outputs.clear()

            fn = scriptFilename.decode('utf-8')

            # don't set a filename if NULL is passed
            if fn != '<string>':
                abspath = os.path.abspath(fn)
                evaldict['__file__'] = abspath
                if flags & 1:
                    orig_path = os.getcwd()
                    os.chdir(os.path.dirname(abspath))

            evaldict['__name__'] = "__vapoursynth__"
            
            if se.errstr:
                errstr = <bytes>se.errstr
                se.errstr = NULL
                Py_DECREF(errstr)
                errstr = None

            comp = compile(script.decode('utf-8-sig'), fn, 'exec')

            # Change the environment now.
            with _vsscript_use_or_create_environment(se.id).use():
                exec(comp) in evaldict

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
        finally:
            if orig_path is not None:
                os.chdir(orig_path)
        return 0

cdef public api int vpy_evaluateFile(VPYScriptExport *se, const char *scriptFilename, int flags) nogil:
    with gil:
        if not se.pyenvdict:
            evaldict = {}
            Py_INCREF(evaldict)
            se.pyenvdict = <void *>evaldict
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

cdef public api void vpy_freeScript(VPYScriptExport *se) nogil:
    with gil:
        vpy_clearEnvironment(se)
        if se.pyenvdict:
            evaldict = <dict>se.pyenvdict
            evaldict.clear()
            se.pyenvdict = NULL
            Py_DECREF(evaldict)
            evaldict = None

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

cdef public api char *vpy_getError(VPYScriptExport *se) nogil:
    if not se.errstr:
        return NULL
    with gil:
        errstr = <bytes>se.errstr
        return errstr

cdef public api VSNodeRef *vpy_getOutput(VPYScriptExport *se, int index) nogil:
    with gil:
        evaldict = <dict>se.pyenvdict
        node = None
        try:
            node = _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return NULL

        if isinstance(node, AlphaOutputTuple):
            node = node[0]
            
        if isinstance(node, RawNode):
            return (<RawNode>node).funcs.cloneNodeRef((<RawNode>node).node)
        else:
            return NULL
            
cdef public api VSNodeRef *vpy_getOutput2(VPYScriptExport *se, int index, VSNodeRef **alpha) nogil:
    if (alpha != NULL):
        alpha[0] = NULL
    with gil:
        evaldict = <dict>se.pyenvdict
        node = None
        try:
            node = _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return NULL
   
        if isinstance(node, AlphaOutputTuple):
            if (isinstance(node[1], VideoNode) and (alpha != NULL)):
                alpha[0] = (<VideoNode>(node[1])).funcs.cloneNodeRef((<VideoNode>(node[1])).node)
            return (<VideoNode>(node[0])).funcs.cloneNodeRef((<VideoNode>(node[0])).node)
        elif isinstance(node, RawNode):
            return (<RawNode>node).funcs.cloneNodeRef((<RawNode>node).node)
        else:
            return NULL

cdef public api int vpy_clearOutput(VPYScriptExport *se, int index) nogil:
    with gil:
        try:
            del _get_vsscript_policy().get_environment(se.id).outputs[index]
        except:
            return 1
        return 0

cdef public api VSCore *vpy_getCore(VPYScriptExport *se) nogil:
    with gil:
        try:
            core = vsscript_get_core_internal(_get_vsscript_policy().get_environment(se.id))
            if core is not None:
                return (<Core>core).core
            else:
                return NULL
        except:
            return NULL

cdef public api const VSAPI *vpy_getVSApi() nogil:
    global _vsapi
    if _vsapi == NULL:
        _vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION)
    return _vsapi
    
cdef public api const VSAPI *vpy_getVSApi2(int version) nogil:
    return getVapourSynthAPI(version)

cdef public api int vpy_getVariable(VPYScriptExport *se, const char *name, VSMap *dst) nogil:
    with gil:
        with _vsscript_use_environment(se.id).use():
            if vpy_getVSApi() == NULL:
                return 1
            evaldict = <dict>se.pyenvdict
            try:
                dname = name.decode('utf-8')
                read_var = { dname:evaldict[dname]}
                core = vsscript_get_core_internal(_get_vsscript_policy().get_environment(se.id))
                dictToMap(read_var, dst, core.core, vpy_getVSApi())
                return 0
            except:
                return 1

cdef public api int vpy_setVariable(VPYScriptExport *se, const VSMap *vars) nogil:
    with gil:
        with _vsscript_use_environment(se.id).use():
            if vpy_getVSApi() == NULL:
                return 1
            evaldict = <dict>se.pyenvdict
            try:     
                core = vsscript_get_core_internal(_get_vsscript_policy().get_environment(se.id))
                new_vars = mapToDict(vars, False, False, core.core, vpy_getVSApi())
                for key in new_vars:
                    evaldict[key] = new_vars[key]
                return 0
            except:
                return 1

cdef public api int vpy_clearVariable(VPYScriptExport *se, const char *name) nogil:
    with gil:
        evaldict = <dict>se.pyenvdict
        try:
            del evaldict[name.decode('utf-8')]
        except:
            return 1
        return 0

cdef public api void vpy_clearEnvironment(VPYScriptExport *se) nogil:
    with gil:
        evaldict = <dict>se.pyenvdict
        for key in evaldict:
            evaldict[key] = None
        evaldict.clear()
        try:
             _get_vsscript_policy().get_environment(se.id).outputs.clear()
             _get_vsscript_policy().get_environment(se.id).core = None
        except:
            pass
        gc.collect()

cdef public api int vpy_initVSScript() nogil:
    with gil:
        if vpy_getVSApi() == NULL:
            return 1
        if has_policy():
            return 1

        vsscript = VSScriptEnvironmentPolicy.__new__(VSScriptEnvironmentPolicy)
        register_policy(vsscript)
        return 0
