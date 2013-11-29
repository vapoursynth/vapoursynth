#  Copyright (c) 2012-2013 Fredrik Mellbin
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
from cpython.ref cimport Py_INCREF, Py_DECREF, Py_CLEAR, PyObject
import os
import sys
import ctypes
import threading
import traceback
import gc

_using_vsscript = False
_environment_id_stack = []
_environment_id = None
_stored_outputs = {}
_cores = {}
_stored_output = {}
_core = None
_message_handler = None

GRAY  = vapoursynth.cmGray
RGB   = vapoursynth.cmRGB
YUV   = vapoursynth.cmYUV
YCOCG = vapoursynth.cmYCoCg
COMPAT= vapoursynth.cmCompat

GRAY8 = vapoursynth.pfGray8
GRAY16 = vapoursynth.pfGray16

GRAYH = vapoursynth.pfGrayH
GRAYS = vapoursynth.pfGrayS

YUV420P8 = vapoursynth.pfYUV420P8
YUV422P8 = vapoursynth.pfYUV422P8
YUV444P8 = vapoursynth.pfYUV444P8
YUV410P8 = vapoursynth.pfYUV410P8
YUV411P8 = vapoursynth.pfYUV411P8
YUV440P8 = vapoursynth.pfYUV440P8

YUV420P9 = vapoursynth.pfYUV420P9
YUV422P9 = vapoursynth.pfYUV422P9
YUV444P9 = vapoursynth.pfYUV444P9

YUV420P10 = vapoursynth.pfYUV420P10
YUV422P10 = vapoursynth.pfYUV422P10
YUV444P10 = vapoursynth.pfYUV444P10

YUV420P16 = vapoursynth.pfYUV420P16
YUV422P16 = vapoursynth.pfYUV422P16
YUV444P16 = vapoursynth.pfYUV444P16

YUV444PH = vapoursynth.pfYUV444PH
YUV444PS = vapoursynth.pfYUV444PS

RGB24 = vapoursynth.pfRGB24
RGB27 = vapoursynth.pfRGB27
RGB30 = vapoursynth.pfRGB30
RGB48 = vapoursynth.pfRGB48

RGBH = vapoursynth.pfRGBH
RGBS = vapoursynth.pfRGBS

COMPATBGR32 = vapoursynth.pfCompatBGR32
COMPATYUY2 = vapoursynth.pfCompatYUY2

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
    funcs = getVapourSynthAPI(3)
    if funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. Is the Python module and loaded core library mismatched?')
    if handler_func is None:
        _message_handler = None
        funcs.setMessageHandler(NULL, NULL)
    else:
        handler_func(vapoursynth.mtDebug, 'New message handler installed from python')
        _message_handler = handler_func
        funcs.setMessageHandler(message_handler_wrapper, NULL)
    
def clear_output(int index = 0):
    global _using_vsscript
    if _using_vsscript:
        global _stored_outputs
        global _environment_id
        if _environment_id is None:
            raise Error('Internal environment id not set. Was clear_output() called from a filter callback?')
        # fixme, should probably catch a more specific exception
        try:
            del _stored_outputs[_environment_id][index]
        except:
            pass
    else:
        global _stored_output
        try:
            del _stored_output[index]
        except:
            pass

def clear_outputs():
    global _using_vsscript
    if _using_vsscript:
        global _stored_outputs
        global _environment_id
        if _environment_id is None:
            raise Error('Internal environment id not set. Was clear_outputs() called from a filter callback?')
        _stored_outputs[_environment_id] = {}
    else:
        global _stored_output
        _stored_output = {}

def get_output(int index = 0):
    global _using_vsscript
    if _using_vsscript:
        global _stored_outputs
        global _environment_id
        if _environment_id is None:
            raise Error('Internal environment id not set. Was get_output() called from a filter callback?')
        return _stored_outputs[_environment_id][index]
    else:
        global _stored_output
        return _stored_output[index]

# fixme, make it possible for this to call functions not defined in python
cdef class Func(object):
    cdef Core core
    cdef object func
    cdef VSFuncRef *ref

    def __init__(self, object func not None, Core core not None):
        self.core = core
        self.func = func
        self.ref = core.funcs.createFunc(publicFunction, <void *>self, freeFunc)
        Py_INCREF(self)

    def __dealloc__(self):
        self.core.funcs.freeFunc(self.ref)

    def __call__(self, **kwargs):
        return self.func(**kwargs)

cdef Plugin createFunc(VSFuncRef *ref, Core core):
    cdef Func instance = Func.__new__(Func)
    instance.core = core
    instance.func = None
    instance.ref = ref
    return instance

cdef object mapToDict(const VSMap *map, bint flatten, bint add_cache, Core core, const VSAPI *funcs):
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
            elif proptype =='c':
                newval = createVideoNode(funcs.propGetNode(map, retkey, y, NULL), funcs, core)

                if add_cache and not newval.flags:
                    newval = core.std.Cache(clip=newval)

                    if isinstance(newval, dict):
                        newval = newval['dict']
            elif proptype =='v':
                newval = createConstVideoFrame(funcs.propGetFrame(map, retkey, y, NULL), funcs, core)
            elif proptype =='m':
                newval = createFunc(funcs.propGetFunc(map, retkey, y, NULL), core)

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

cdef void dictToMap(dict ndict, VSMap *inm, Core core, const VSAPI *funcs) except *:
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]

        if isinstance(val, str) or isinstance(val, bytes) or isinstance(val, bytearray) or isinstance(val, VideoNode):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]     

        for v in val:
            if isinstance(v, VideoNode):
                if funcs.propSetNode(inm, ckey, (<VideoNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, VideoFrame):
                if funcs.propSetFrame(inm, ckey, (<VideoFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, Func):
                if funcs.propSetFunc(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif callable(v):
                tf = Func(v, core)

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
            elif isinstance(v, bytes) or isinstance(v, bytearray):
                if funcs.propSetData(inm, ckey, v, len(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type')


cdef void typedDictToMap(dict ndict, dict atypes, VSMap *inm, Core core, const VSAPI *funcs) except *:
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]
        if val is None:
            continue

        if isinstance(val, str) or isinstance(val, bytes) or isinstance(val, bytearray) or isinstance(val, VideoNode):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val]

        for v in val:
            if atypes[key][:4] == 'clip' and isinstance(v, VideoNode):
                if funcs.propSetNode(inm, ckey, (<VideoNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:5] == 'frame' and isinstance(v, VideoFrame):
                if funcs.propSetFrame(inm, ckey, (<VideoFrame>v).constf, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and isinstance(v, Func):
                if funcs.propSetFunc(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'func' and callable(v):
                tf = Func(v, core)
                if funcs.propSetFunc(inm, ckey, tf.ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:3] == 'int' and (type(v) == int or type(v) == long or type(v) == bool):
                if funcs.propSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:5] == 'float' and (type(v) == int or type(v) == long or type(v) == float):
                if funcs.propSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif atypes[key][:4] == 'data' and (isinstance(v, str) or isinstance(v, bytes) or isinstance(v, bytearray)):
                if isinstance(v, str):
                    s = str(v).encode('utf-8')
                else:
                    s = v
                if funcs.propSetData(inm, ckey, s, len(s), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type')
        if len(val) == 0:
        # set an empty key if it's an empty array
            if atypes[key][:4] == 'clip':
                funcs.propSetNode(inm, ckey, NULL, 2)
            elif atypes[key][:5] == 'frame':
                funcs.propSetFrame(inm, ckey, NULL, 2)
            elif atypes[key][:4] == 'func':
                funcs.propSetFunc(inm, ckey, NULL, 2)
            elif atypes[key][:3] == 'int':
                funcs.propSetInt(inm, ckey, 0, 2)
            elif atypes[key][:5] == 'float':
                funcs.propSetFloat(inm, ckey, 0, 2)
            elif atypes[key][:4] == 'data':
                funcs.propSetData(inm, ckey, NULL, 0, 2)
            else:
                raise Error('argument ' + key + ' has an unknown type: ' + atypes[key])

cdef class Format(object):
    cdef readonly int id
    cdef readonly str name
    cdef readonly int color_family
    cdef readonly int sample_type
    cdef readonly int bits_per_sample
    cdef readonly int bytes_per_sample
    cdef readonly int subsampling_w
    cdef readonly int subsampling_h
    cdef readonly int num_planes

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __str__(self):
        cdef dict color_stuff = dict({GRAY:'Gray', RGB:'RGB', YUV:'YUV', YCOCG:'YCoCg', COMPAT:'Compat'})
        cdef str s = ''
        s += 'Format Descriptor\n'
        s += '\tId: ' + str(self.id) + '\n'
        s += '\tName: ' + self.name + '\n'
        s += '\tColor Family: ' + color_stuff[self.color_family] + '\n'

        if self.sample_type == stInteger:
            s += '\tSample Type: Integral\n'
        else:
            s += '\tSample Type: Float\n'

        s += '\tBits Per Sample: ' + str(self.bits_per_sample) + '\n'
        s += '\tBytes Per Sample: ' + str(self.bytes_per_sample) + '\n'
        s += '\tPlanes: ' + str(self.num_planes) + '\n'
        s += '\tSubsampling W: ' + str(self.subsampling_w) + '\n'
        s += '\tSubsampling H: ' + str(self.subsampling_h) + '\n'
        return s

cdef Format createFormat(const VSFormat *f):
    cdef Format instance = Format.__new__(Format)
    instance.id = f.id
    instance.name = f.name.decode('utf-8')
    instance.color_family = f.colorFamily
    instance.sample_type = f.sampleType
    instance.bits_per_sample = f.bitsPerSample
    instance.bytes_per_sample = f.bytesPerSample
    instance.subsampling_w = f.subSamplingW
    instance.subsampling_h = f.subSamplingH
    instance.num_planes = f.numPlanes
    return instance

cdef class VideoProps(object):
    cdef const VSFrameRef *constf
    cdef VSFrameRef *f
    cdef Core core
    cdef const VSAPI *funcs
    cdef bint readonly

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeFrame(self.constf)

    def __getattr__(self, name):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        cdef bytes b = name.encode('utf-8')
        cdef list ol = []
        cdef int numelem = self.funcs.propNumElements(m, b)
        if numelem < 0:
            raise KeyError('No key named ' + name + ' exists')
        cdef char t = self.funcs.propGetType(m, b)
        if t == 'i':
            for i in range(numelem):
                ol.append(self.funcs.propGetInt(m, b, i, NULL))
        elif t == 'f':
            for i in range(numelem):
                ol.append(self.funcs.propGetFloat(m, b, i, NULL))
        elif t == 's':
            for i in range(numelem):
                ol.append(self.funcs.propGetData(m, b, i, NULL))
        elif t == 'c':
            for i in range(numelem):
                ol.append(createVideoNode(self.funcs.propGetNode(m, b, i, NULL), self.funcs, self.core))
        elif t == 'v':
            for i in range(numelem):
                ol.append(createConstVideoFrame(self.funcs.propGetFrame(m, b, i, NULL), self.funcs, self.core))
        elif t == 'm':
            for i in range(numelem):
                ol.append(createFunc(self.funcs.propGetFunc(m, b, i, NULL), self.core))

        if len(ol) == 1:
            return ol[0]
        else:
            return ol

    def __setattr__(self, name, value):
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropsRW(self.f)
        cdef bytes b = name.encode('utf-8')
        cdef const VSAPI *funcs = self.funcs
        val = value
        if isinstance(val, str) or isinstance(val, bytes) or isinstance(val, bytearray) or isinstance(val, VideoNode):
            val = [val]
        else:
            try:
                iter(val)
            except:
                val = [val] 
        self.__delattr__(name)
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
                    tf = Func(v, self.core)
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
                elif isinstance(v, bytes) or isinstance(v, bytearray):
                    if funcs.propSetData(m, b, v, len(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                else:
                    raise Error('Setter was passed an unsupported type')
        except Error:
            self.__delattr__(name)
            raise

    def __delattr__(self, name):
        if self.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.funcs.getFramePropsRW(self.f)
        cdef bytes b = name.encode('utf-8')
        self.funcs.propDeleteKey(m, b)

    def __dir__(self):
        cdef const VSMap *m = self.funcs.getFramePropsRO(self.constf)
        cdef int numkeys = self.funcs.propNumKeys(m)
        attrs = []
        for i in range(numkeys):
            attrs.append(self.funcs.propGetKey(m, i).decode('utf-8'))
        return attrs

cdef VideoProps createVideoProps(VideoFrame f):
    cdef VideoProps instance = VideoProps.__new__(VideoProps)
# since the vsapi only returns const refs when cloning a VSFrameRef it is safe to cast away the const here
    instance.constf = f.funcs.cloneFrameRef(f.constf)
    instance.f = NULL
    instance.funcs = f.funcs
    instance.core = f.core
    instance.readonly = f.readonly
    if not instance.readonly:
        instance.f = <VSFrameRef *>instance.constf
    return instance

cdef class VideoFrame(object):
    cdef const VSFrameRef *constf
    cdef VSFrameRef *f
    cdef Core core
    cdef const VSAPI *funcs
    cdef readonly Format format
    cdef readonly int width
    cdef readonly int height
    cdef readonly bint readonly
    cdef readonly VideoProps props

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeFrame(self.constf)

    def copy(self):
        return createVideoFrame(self.funcs.copyFrame(self.constf, self.core.core), self.funcs, self.core)

    def get_read_ptr(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef const uint8_t *d = self.funcs.getReadPtr(self.constf, plane)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_write_ptr(self, int plane):
        if self.readonly:
            raise Error('Cannot obtain write pointer to read only frame')
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef uint8_t *d = self.funcs.getWritePtr(self.f, plane)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_stride(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        return self.funcs.getStride(self.constf, plane)

    def __str__(self):
        cdef str s = 'VideoFrame\n'
        s += '\tFormat: ' + self.format.name + '\n'
        s += '\tWidth: ' + str(self.width) + '\n'
        s += '\tHeight: ' + str(self.height) + '\n'
        return s

cdef VideoFrame createConstVideoFrame(const VSFrameRef *constf, const VSAPI *funcs, Core core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = constf
    instance.f = NULL
    instance.funcs = funcs
    instance.core = core
    instance.readonly = True
    instance.format = createFormat(funcs.getFrameFormat(constf))
    instance.width = funcs.getFrameWidth(constf, 0)
    instance.height = funcs.getFrameHeight(constf, 0)
    instance.props = createVideoProps(instance)
    return instance

cdef VideoFrame createVideoFrame(VSFrameRef *f, const VSAPI *funcs, Core core):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)
    instance.constf = f
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.readonly = False
    instance.format = createFormat(funcs.getFrameFormat(f))
    instance.width = funcs.getFrameWidth(f, 0)
    instance.height = funcs.getFrameHeight(f, 0)
    instance.props = createVideoProps(instance)
    return instance

cdef class VideoNode(object):
    cdef VSNodeRef *node
    cdef const VSAPI *funcs
    cdef Core core
    cdef const VSVideoInfo *vi
    cdef readonly Format format
    cdef readonly int width
    cdef readonly int height
    cdef readonly int num_frames
    cdef readonly int64_t fps_num
    cdef readonly int64_t fps_den
    cdef readonly int flags

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeNode(self.node)

    def get_frame(self, int n):
        cdef char errorMsg[512]
        cdef char *ep = errorMsg
        cdef const VSFrameRef *f
        if n < 0:
            raise ValueError('Requesting negative frame numbers not allowed')
        if (self.num_frames > 0) and (n >= self.num_frames):
            raise ValueError('Requesting frame number is beyond the last frame')
        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 500)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createConstVideoFrame(f, self.funcs, self.core)

    def set_output(self, int index = 0):
        global _using_vsscript
        if _using_vsscript:
            global _stored_outputs
            global _environment_id
            if _environment_id is None:
                raise Error('Internal environment id not set. Was set_output() called from a filter callback?')
            _stored_outputs[_environment_id][index] = self
        else:
            global _stored_output
            _stored_output[index] = self


    def __add__(self, other):
        if not isinstance(other, VideoNode):
            raise TypeError('Only clips can be spliced')
        return (<VideoNode>self).core.std.Splice(clips=[self, other])

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
            if val.step is not None and val.step < 0 and self.num_frames == 0:
                raise ValueError('Negative step cannot be used with infinite/unknown length clips')
            if ((val.start is not None and val.start < 0) or (val.stop is not None and val.stop < 0)) and self.num_frames == 0:
                raise ValueError('Negative indices cannot be used with infinite/unknown length clips')
            # this is just a big number that no one's likely to use, hence the -68
            max_int = 2**31-68
            if self.num_frames == 0:
                indices = val.indices(max_int)
            else:
                indices = val.indices(self.num_frames)

            if indices[0] == max_int:
                indices[0] = None
            if indices[1] == max_int:
                indices[1] = None

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
            if val < 0 and self.num_frames == 0:
                raise IndexError('Negative index cannot be used with infinite/unknown length clips')
            elif val < 0:
                n = self.num_frames - val
            else:
                n = val
            if n < 0 or (self.num_frames > 0 and n >= self.num_frames):
                raise IndexError('List index out of bounds')
            return self.core.std.Trim(clip=self, first=n, length=1)
        else:
            raise TypeError("index must be int or slice")

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

        if not self.num_frames:
            s += '\tNum Frames: unknown\n'
        else:
            s += '\tNum Frames: ' + str(self.num_frames) + '\n'

        if not self.fps_num or not self.fps_den:
            s += '\tFPS Num: dynamic\n'
            s += '\tFPS Den: dynamic\n'
        else:
            s += '\tFPS Num: ' + str(self.fps_num) + '\n'
            s += '\tFPS Den: ' + str(self.fps_den) + '\n'

        if self.flags == vapoursynth.nfNoCache:
            s += '\tFlags: No Cache\n'
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
        instance.format = createFormat(instance.vi.format)
    else:
        instance.format = None

    instance.width = instance.vi.width
    instance.height = instance.vi.height
    instance.num_frames = instance.vi.numFrames
    instance.fps_num = <int64_t>instance.vi.fpsNum
    instance.fps_den = <int64_t>instance.vi.fpsDen
    instance.flags = instance.vi.flags
    return instance

cdef class Core(object):
    cdef VSCore *core
    cdef const VSAPI *funcs
    cdef readonly int num_threads
    cdef readonly bint add_cache
    cdef readonly bint accept_lowercase
    cdef readonly bint r21_arg_compat

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeCore(self.core)

    def __getattr__(self, name):
        cdef VSPlugin *plugin
        tname = name.encode('utf-8')
        cdef const char *cname = tname
        plugin = self.funcs.getPluginByNs(cname, self.core)

        if plugin:
            return createPlugin(plugin, self.funcs, self)
        else:
            raise Error('No attribute with the name ' + name + ' exists. Did you mistype a plugin namespace?')

    def set_max_cache_size(self, int mb):
        if mb <= 0:
            raise ValueError('Maximum cache size must be a positive number')
        cdef int64_t new_size = mb
        new_size = new_size * 1024 * 1024
        return self.funcs.setMaxCacheSize(new_size, self.core)

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
        return createFormat(self.funcs.registerFormat(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h, self.core))

    def get_format(self, int id):
        cdef const VSFormat *f = self.funcs.getFormatPreset(id, self.core)

        if f == NULL:
            raise Error('Internal error')
        else:
            return createFormat(f)

    def version(self):
        cdef const VSCoreInfo *v = self.funcs.getCoreInfo(self.core)
        return v.versionString.decode('utf-8')
        
    def version_number(self):
        cdef const VSCoreInfo *v = self.funcs.getCoreInfo(self.core)
        return v.core

    def __str__(self):
        cdef str s = 'Core\n'
        s += self.version() + '\n'
        s += '\tNumber of Threads: ' + str(self.num_threads) + '\n'
        s += '\tAdd Caches: ' + str(self.add_cache) + '\n'
        s += '\tAccept Lowercase: ' + str(self.accept_lowercase) + '\n'
        return s

cdef Core createCore(int threads = 0, bint add_cache = True, bint accept_lowercase = False, bint r21_arg_compat = True):
    cdef Core instance = Core.__new__(Core)
    instance.funcs = getVapourSynthAPI(3)
    if instance.funcs == NULL:
        raise Error('Failed to obtain VapourSynth API pointer. System does not support SSE2 or is the Python module and loaded core library mismatched?')
    instance.core = instance.funcs.createCore(threads)
    instance.add_cache = add_cache
    instance.accept_lowercase = accept_lowercase
    instance.r21_arg_compat = r21_arg_compat
    cdef const VSCoreInfo *info = instance.funcs.getCoreInfo(instance.core)
    instance.num_threads = info.numThreads
    return instance

def get_core(int threads = 0, bint add_cache = True, bint accept_lowercase = False, bint r21_arg_compat = True):
    global _using_vsscript
    if _using_vsscript:
        global _cores
        global _environment_id
        if _environment_id is None:
            raise Error('Internal environment id not set. Was get_core() called from a filter callback?')

        if not _environment_id in _cores:
            _cores[_environment_id] = createCore(threads, add_cache, accept_lowercase, r21_arg_compat)
        return _cores[_environment_id]
    else:
        global _core
        if _core is None:
            _core = createCore(threads, add_cache, accept_lowercase, r21_arg_compat)
        return _core

cdef get_core_internal(int _environment_id, int threads = 0, bint add_cache = True, bint accept_lowercase = False):
    global _cores
    if not _environment_id in _cores:
        _cores[_environment_id] = createCore(threads, add_cache, accept_lowercase)
    return _cores[_environment_id]

cdef class Plugin(object):
    cdef Core core
    cdef VSPlugin *plugin
    cdef const VSAPI *funcs

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
            lc_name = orig_name.lower()

            if orig_name == name:
                match = True
                break

            if (lc_name == name) and self.core.accept_lowercase:
                match = True
                break

        if match:
            signature = self.funcs.propGetData(m, cname, 0, NULL).decode('utf-8')
            signature = signature.split(';', 1)
            self.funcs.freeMap(m)
            return createFunction(orig_name, signature[1], self, self.funcs)
        else:
            self.funcs.freeMap(m)
            raise Error('There is no function named ' + name)

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

cdef Plugin createPlugin(VSPlugin *plugin, const VSAPI *funcs, Core core):
    cdef Plugin instance = Plugin.__new__(Plugin)
    instance.core = core
    instance.plugin = plugin
    instance.funcs = funcs
    return instance

cdef class Function(object):
    cdef Plugin plugin
    cdef str name
    cdef str signature
    cdef const VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __call__(self, *args, **kwargs):
        cdef VSMap *inm
        cdef VSMap *outm
        cdef char *cname
        arglist = list(args)
        ndict = {}
        processed = {}
        atypes = {}
        # remove _ from all args
        for key in kwargs:
            if key[0] == '_':
                nkey = key[1:]
            else:
                nkey = key
            ndict[nkey] = kwargs[key]
            
        # handle Merge, MaskedMerge, Lut2 r21 compat
        if self.plugin.core.r21_arg_compat and ((self.name == 'Merge') or (self.name == 'MaskedMerge') or (self.name == 'Lut2')):
            try:
                if 'clips' in ndict:
                    ndict['clipa'] = ndict['clips'][0]
                    ndict['clipb'] = ndict['clips'][1]
                    del ndict['clips']
                else:
                    ndict['clipa'] = arglist[0][0]
                    ndict['clipb'] = arglist[0][1]
                    del arglist[0]
            except:
                pass

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
            typedDictToMap(processed, atypes, inm, self.plugin.core, self.funcs)
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

        retdict = mapToDict(outm, True, self.plugin.core.add_cache, self.plugin.core, self.funcs)
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
        fobj = <object>pobj
        Py_DECREF(fobj)

cdef void __stdcall publicFunction(const VSMap *inm, VSMap *outm, void *userData, VSCore *core, const VSAPI *vsapi) nogil:
    with gil:
        d = <Func>userData

        try:
            m = mapToDict(inm, False, False, d.core, vsapi)
            ret = d(**m)
            if not isinstance(ret, dict):
                ret = {'val':ret}
            dictToMap(ret, outm, d.core, vsapi)
        except BaseException, e:
            emsg = str(e).encode('utf-8')
            vsapi.setError(outm, emsg)

# for whole script evaluation and export
cdef public struct VPYScriptExport:
    void *pyenvdict
    void *errstr
    int id

cdef public api int vpy_evaluateScript(VPYScriptExport *se, const char *script, const char *scriptFilename, int flags) nogil:
    with gil:
        global _environment_id
        global _environment_id_stack
        _environment_id_stack.append(_environment_id)
        _environment_id = se.id
        orig_path = None
        try:
            evaldict = {}
            if se.pyenvdict:
                evaldict = <dict>se.pyenvdict
            else:
                Py_INCREF(evaldict)
                se.pyenvdict = <void *>evaldict
                global _stored_outputs
                _stored_outputs[se.id] = {}

            Py_INCREF(evaldict)

            fn = scriptFilename.decode('utf-8')

            # don't set a filename if NULL is passed
            if fn != '<string>':
                abspath = os.path.abspath(fn)
                evaldict['__file__'] = abspath
                if flags & 1:
                    orig_path = os.getcwd()
                    os.chdir(os.path.dirname(abspath))

            if se.errstr:
                errstr = <bytes>se.errstr
                se.errstr = NULL
                Py_DECREF(errstr)
                errstr = None

            comp = compile(script.decode('utf-8'), fn, 'exec')
            exec(comp) in evaldict

        except BaseException, e:
            errstr = 'Python exception: ' + str(e) + '\n' + traceback.format_exc()
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 2
        except:
            errstr = 'Unspecified Python exception' + '\n' + traceback.format_exc()
            errstr = errstr.encode('utf-8')
            Py_INCREF(errstr)
            se.errstr = <void *>errstr
            return 1
        finally:
            _environment_id = _environment_id_stack.pop()
            if orig_path is not None:
                os.chdir(orig_path)
        return 0

cdef public api int vpy_evaluateFile(VPYScriptExport *se, const char *scriptFilename, int flags) nogil:
    with gil:
        if not se.pyenvdict:
            evaldict = {}
            Py_INCREF(evaldict)
            se.pyenvdict = <void *>evaldict
            global _stored_outputs
            _stored_outputs[se.id] = {}

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
        evaldict = <dict>se.pyenvdict
        se.pyenvdict = NULL
        Py_DECREF(evaldict)
        evaldict = None

        if se.errstr:
            errstr = <bytes>se.errstr
            se.errstr = NULL
            Py_DECREF(errstr)
            errstr = None

        try:
            global _cores
            del _cores[se.id]
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
            global _stored_outputs
            node = _stored_outputs[se.id][index]
        except:
            return NULL

        if isinstance(node, VideoNode):
            return (<VideoNode>node).funcs.cloneNodeRef((<VideoNode>node).node)
        else:
            return NULL

cdef public api void vpy_clearOutput(VPYScriptExport *se, int index) nogil:
    with gil:
        try:
            global _stored_outputs
            del _stored_outputs[se.id][index]
        except:
            pass

cdef public api VSCore *vpy_getCore(VPYScriptExport *se) nogil:
    with gil:
        try:
            core = get_core_internal(se.id)
            return (<Core>core).core
        except:
            return NULL

cdef public api const VSAPI *vpy_getVSApi() nogil:
    return getVapourSynthAPI(3)

cdef public api int vpy_getVariable(VPYScriptExport *se, const char *name, VSMap *dst) nogil:
    with gil:
        evaldict = <dict>se.pyenvdict
        core = get_core_internal(se.id)
        try:
            dname = name.decode('utf-8')
            read_var = { dname:evaldict[dname]}
            dictToMap(read_var, dst, core, (<Core>core).funcs)
            return 0
        except:
            return 1

cdef public api void vpy_setVariable(VPYScriptExport *se, const VSMap *vars) nogil:
    with gil:
        evaldict = <dict>se.pyenvdict
        core = get_core_internal(se.id)
        new_vars = mapToDict(vars, False, False, core, (<Core>core).funcs)
        for key in new_vars:
            evaldict[key] = new_vars[key]

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
        try:
            global _stored_outputs
            del _stored_outputs[se.id]
        except:
            pass
        gc.collect()

cdef public api void vpy_initVSScript() nogil:
    with gil:
        global _using_vsscript
        _using_vsscript = True
