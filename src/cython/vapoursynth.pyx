#//  Copyright (c) 2012 Fredrik Mellbin
#//
#//  This file is part of VapourSynth.
#//
#//  VapourSynth is free software; you can redistribute it and/or
#//  modify it under the terms of the GNU Lesser General Public
#//  License as published by the Free Software Foundation; either
#//  version 2.1 of the License, or (at your option) any later version.
#//
#//  VapourSynth is distributed in the hope that it will be useful,
#//  but WITHOUT ANY WARRANTY; without even the implied warranty of
#//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#//  Lesser General Public License for more details.
#//
#//  You should have received a copy of the GNU Lesser General Public
#//  License along with Libav; if not, write to the Free Software
#//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
"""
This is the VapourSynth module implementing the Python bindings.
"""

#ifdef _WIN32
cimport windows
#else
cimport posix.unistd
#endif
cimport vapoursynth
cimport cython.parallel
from cpython.ref cimport Py_INCREF, Py_DECREF
import ctypes
#ifdef _WIN32
import msvcrt
#endif
import threading

GRAY  = vapoursynth.cmGray
RGB   = vapoursynth.cmRGB
YUV   = vapoursynth.cmYUV
YCOCG = vapoursynth.cmYCoCg
COMPAT= vapoursynth.cmCompat

GRAY8 = vapoursynth.pfGray8
GRAY16 = vapoursynth.pfGray16

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

RGB24 = vapoursynth.pfRGB24
RGB27 = vapoursynth.pfRGB27
RGB30 = vapoursynth.pfRGB30
RGB48 = vapoursynth.pfRGB48

COMPATBGR32 = vapoursynth.pfCompatBGR32
COMPATYUY2 = vapoursynth.pfCompatYUY2

class Error(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return repr(self.value)

cdef class Link(object):
    def __init__(self, val, prop):
        self.val = val
        self.prop = prop

#// fixme, make it possible for this to call functions not defined in python
cdef class Func(object):
    cdef Core core
    cdef object func
    cdef VSFuncRef *ref

    def __init__(self, object func not None, Core core not None):
        self.core = core
        self.func = func
        self.ref = core.funcs.createFunc(publicFunction, <void *>self, freeFunc)
        Py_INCREF(self)

    def __deinit__(self):
        self.core.funcs.freeFunc(self.ref)

    def __call__(self, **kwargs):
        return self.func(**kwargs)

cdef Plugin createFunc(VSFuncRef *ref, Core core):
    cdef Func instance = Func.__new__(Func)
    instance.core = core
    instance.func = None
    instance.ref = ref
    return instance

cdef class CallbackData(object):
    cdef VideoNode node
    cdef VSAPI *funcs
    cdef int handle
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

    def __init__(self, handle, requested, total, num_planes, y4m, node, progress_update):
        self.handle = handle
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
    cdef VSFrameRef *f
    cdef VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeFrame(self.f)

cdef FramePtr createFramePtr(vapoursynth.VSFrameRef *f, vapoursynth.VSAPI *funcs):
    cdef FramePtr instance = FramePtr.__new__(FramePtr)    
    instance.f = f
    instance.funcs = funcs
    return instance

cdef void __stdcall callback(void *data, VSFrameRef *f, int n, VSNodeRef *node, char *errormsg) nogil:
    cdef int pitch
    cdef uint8_t *readptr
    cdef VSFormat *fi
    cdef int row_size
    cdef int height
    cdef char err[512]
    cdef char *header = 'FRAME\n'
    cdef int dummy = 0
    cdef int p
    cdef int y

    with gil:
        d = <CallbackData>data
        d.completed = d.completed + 1
        
        if f == NULL:
            d.total = d.requested
            if errormsg == NULL:
                d.error = 'Failed to retrieve frame ' + str(n)
            else:
                d.error = 'Failed to retrieve frame ' + str(n) + ' with error: ' + errormsg.decode('utf-8')
            d.output = d.output + 1

        else:
            d.reorder[n] = createFramePtr(f, d.funcs)

            while d.output in d.reorder:
                frame_obj = <FramePtr>d.reorder[d.output]
                if d.y4m:
#ifdef _WIN32
                    windows.WriteFile(d.handle, header, 6, &dummy, NULL)
#else
                    posix.unistd.write(d.handle, header, 6)
#endif
                
                p = 0
                fi = d.funcs.getFrameFormat(frame_obj.f)

                while p < d.num_planes:
                    pitch = d.funcs.getStride(frame_obj.f, p)
                    readptr = d.funcs.getReadPtr(frame_obj.f, p)
                    row_size = d.funcs.getFrameWidth(frame_obj.f, p) * fi.bytesPerSample
                    height = d.funcs.getFrameHeight(frame_obj.f, p)
                    y = 0

                    while y < height:
#ifdef _WIN32
                        if not windows.WriteFile(d.handle, readptr, row_size, &dummy, NULL):
                            d.error = 'WriteFile() call returned false'
#else
                        if posix.unistd.write(d.handle, readptr, row_size) < 0:
                            d.error = 'write() call returned error'
#endif
                            d.total = d.requested


                        readptr += pitch
                        y = y + 1

                    p = p + 1

                del d.reorder[d.output]
                d.output = d.output + 1
#ifdef _WIN32
                windows.FlushFileBuffers(d.handle)
#endif

            if (d.progress_update is not None):
                d.progress_update(d.completed, d.total)

        if d.requested < d.total:
            d.node.funcs.getFrameAsync(d.requested, d.node.node, callback, data)
            d.requested = d.requested + 1

        if d.total == d.completed:
            d.condition.acquire()
            d.condition.notify()
            d.condition.release()

cdef object mapToDict(VSMap *map, bint flatten, bint add_cache, Core core, VSAPI *funcs):
    cdef int numKeys = funcs.propNumKeys(map)
    retdict = {}
    cdef char *retkey
    cdef char proptype
    cdef VSMap *tempmap

    for x in range(numKeys):
        retkey = funcs.propGetKey(map, x)
        proptype = funcs.propGetType(map, retkey)

        for y in range(funcs.propNumElements(map, retkey)):
            if proptype == 'i':
                newval = funcs.propGetInt(map, retkey, 0, NULL)
            elif proptype == 'f':
                newval = funcs.propGetFloat(map, retkey, 0, NULL)
            elif proptype == 's':
                newval = funcs.propGetData(map, retkey, 0, NULL)
            elif proptype =='c':
                newval = createVideoNode(funcs.propGetNode(map, retkey, 0, NULL), funcs, core)

                if add_cache and not newval.flags:
                    newval = core.std.Cache(clip=newval)

                    if type(newval) == dict:
                        newval = newval['dict']
            elif proptype =='v':
                newval = createVideoFrame(funcs.propGetFrame(map, retkey, 0, NULL), funcs, core)
            elif proptype =='m':
                newval = createFunc(funcs.propGetFunc(map, retkey, 0, NULL), core)

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

cdef void dictToMap(dict ndict, VSMap *inm, Core core, VSAPI *funcs):
    for key in ndict:
        ckey = key.encode('utf-8')
        val = ndict[key]

        if not isinstance(val, list):
            val = [val]

        for v in val:
            if isinstance(v, VideoNode):
                if funcs.propSetNode(inm, ckey, (<VideoNode>v).node, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, VideoFrame):
                if funcs.propSetFrame(inm, ckey, (<VideoFrame>v).f, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif isinstance(v, Func):
                if funcs.propSetFunc(inm, ckey, (<Func>v).ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif callable(v):
                tf = Func(v, core)

                if funcs.propSetFunc(inm, ckey, tf.ref, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif type(v) == int or type(v) == long or type(v) == bool:
                if funcs.propSetInt(inm, ckey, int(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif type(v) == float:
                if funcs.propSetFloat(inm, ckey, float(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif type(v) == str:
                s = str(v).encode('utf-8')

                if funcs.propSetData(inm, ckey, s, -1, 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            elif type(v) == bytes:
                if funcs.propSetData(inm, ckey, v, len(v), 1) != 0:
                    raise Error('not all values are of the same type in ' + key)
            else:
                raise Error('argument ' + key + ' was passed an unsupported type')


cdef class Format(object):
    cdef vapoursynth.VSFormat *f
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

cdef Format createFormat(vapoursynth.VSFormat *f):
    cdef Format instance = Format.__new__(Format)
    instance.f = f
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
    cdef VideoFrame f
    
    def __init__(self):
        raise Error('Class cannot be instantiated directly')
    
    def __getattr__(self, name):
        cdef VSMap *m = self.f.funcs.getFramePropsRO(self.f.f)
        cdef bytes b = name.encode('utf-8')
        cdef list ol = []
        cdef int numelem = self.f.funcs.propNumElements(m, b)
        if numelem < 0:
            raise KeyError('No key named ' + name + ' exists')
        cdef char t = self.f.funcs.propGetType(m, b)
        if t == 'i':
            for i in range(numelem):
                ol.append(self.f.funcs.propGetInt(m, b, i, NULL))
        elif t == 'f':
            for i in range(numelem):
                ol.append(self.f.funcs.propGetFloat(m, b, i, NULL))
        elif t == 's':
            for i in range(numelem):
                ol.append(self.f.funcs.propGetData(m, b, i, NULL))
        elif t == 'c':
            for i in range(numelem):
                ol.append(createVideoNode(self.f.funcs.propGetNode(m, b, i, NULL), self.f.funcs, self.f.core))
        elif t == 'v':
            for i in range(numelem):
                ol.append(createVideoFrame(self.f.funcs.propGetFrame(m, b, i, NULL), self.f.funcs, self.f.core))
        elif t == 'm':
            for i in range(numelem):
                ol.append(createFunc(self.f.funcs.propGetFunc(m, b, i, NULL), self.f.core))
        return ol
        
    def __setattr__(self, name, value):
        if self.f.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.f.funcs.getFramePropsRW(self.f.f)
        cdef bytes b = name.encode('utf-8')
        cdef VSAPI *funcs = self.f.funcs
        val = value
        if not isinstance(val, list):
            val = [val]
        self.__delattr__(name)
        try:
            for v in val:
                if isinstance(v, VideoNode):
                    if funcs.propSetNode(m, b, (<VideoNode>v).node, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, VideoFrame):
                    if funcs.propSetFrame(m, b, (<VideoFrame>v).f, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif isinstance(v, Func):
                    if funcs.propSetFunc(m, b, (<Func>v).ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif callable(v):
                    tf = Func(v, self.f.core)
                    if funcs.propSetFunc(m, b, tf.ref, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif type(v) == int or type(v) == long or type(v) == bool:
                    if funcs.propSetInt(m, b, int(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif type(v) == float:
                    if funcs.propSetFloat(m, b, float(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                elif type(v) == str:
                    s = str(v).encode('utf-8')
                    if funcs.propSetData(m, b, s, -1, 1) != 0:
                        raise Error('Not all values are of the same type')
                elif type(v) == bytes:
                    if funcs.propSetData(m, b, v, len(v), 1) != 0:
                        raise Error('Not all values are of the same type')
                else:
                    raise Error('Setter was passed an unsupported type')
        except Error:
            self.__delattr__(name)
            raise
        
    def __delattr__(self, name):
        if self.f.readonly:
            raise Error('Cannot delete properties of a read only object')
        cdef VSMap *m = self.f.funcs.getFramePropsRW(self.f.f)
        cdef bytes b = name.encode('utf-8')
        self.f.funcs.propDeleteKey(m, b)
        
cdef VideoProps createVideoProps(VideoFrame f):
    cdef VideoProps instance = VideoProps.__new__(VideoProps)
    instance.f = f
    return instance

cdef class VideoFrame(object):
    cdef VSFrameRef *f
    cdef Core core
    cdef vapoursynth.VSAPI *funcs
    cdef readonly Format format
    cdef readonly int width
    cdef readonly int height
    cdef readonly bint readonly
    cdef readonly VideoProps props

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeFrame(self.f)
        
    def copy(self):
        return createVideoFrame(self.funcs.copyFrame(self.f, self.core.core), self.funcs, self.core, False)

    def get_read_ptr(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef uint8_t *d = self.funcs.getReadPtr(self.f, plane)
        return ctypes.c_void_p(<uintptr_t>d)
        
    def get_write_ptr(self, int plane):
        if self.readonly:
            raise Error('Cannot obtain write poitner to read only frame')
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        cdef uint8_t *d = self.funcs.getReadPtr(self.f, plane)
        return ctypes.c_void_p(<uintptr_t>d)

    def get_stride(self, int plane):
        if plane < 0 or plane >= self.format.num_planes:
            raise IndexError('Specified plane index out of range')
        return self.funcs.getStride(self.f, plane)

    def __str__(self):
        cdef str s = 'VideoFrame\n'
        s += '\tFormat: ' + self.format.name + '\n'
        s += '\tWidth: ' + str(self.width) + '\n'
        s += '\tHeight: ' + str(self.height) + '\n'
        return s

cdef VideoFrame createVideoFrame(vapoursynth.VSFrameRef *f, vapoursynth.VSAPI *funcs, Core core, bint readonly = True):
    cdef VideoFrame instance = VideoFrame.__new__(VideoFrame)    
    instance.f = f
    instance.funcs = funcs
    instance.core = core
    instance.readonly = readonly
    instance.format = createFormat(funcs.getFrameFormat(f))
    instance.width = funcs.getFrameWidth(f, 0)
    instance.height = funcs.getFrameHeight(f, 0)
    instance.props = createVideoProps(instance)
    return instance

cdef class VideoNode(object):
    cdef vapoursynth.VSNodeRef *node
    cdef vapoursynth.VSAPI *funcs    
    cdef Core core
    cdef vapoursynth.VSVideoInfo *vi
    cdef readonly Format format
    cdef readonly int width
    cdef readonly int height
    cdef readonly int num_frames
    cdef readonly int fps_num
    cdef readonly int fps_den
    cdef readonly int flags

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __dealloc__(self):
        self.funcs.freeNode(self.node)

    def get_frame(self, int n):
        cdef char errorMsg[512]
        cdef char *ep = errorMsg
        cdef VSFrameRef *f
        with nogil:
            f = self.funcs.getFrame(n, self.node, errorMsg, 500)
        if f == NULL:
            if (errorMsg[0]):
                raise Error(ep.decode('utf-8'))
            else:
                raise Error('Internal error - no error given')
        else:
            return createVideoFrame(f, self.funcs, self.core) 

    def output(self, object fileobj not None, bint y4m = False, int prefetch = -1, object progress_update = None):
        if prefetch < 1:
            prefetch = self.core.num_threads
#ifdef _WIN32
        cdef CallbackData d = CallbackData(msvcrt.get_osfhandle(fileobj.fileno()), min(prefetch, self.num_frames), self.num_frames, self.format.num_planes, y4m, self, progress_update)
#else
        cdef CallbackData d = CallbackData(fileobj.fileno(), min(prefetch, self.num_frames), self.num_frames, self.format.num_planes, y4m, self, progress_update)
#endif
        if d.total <= 0:
            raise Error('Cannot output unknown length clip')

        #// this is also an implicit test that the progress_update callback at least vaguely matches the requirements
        if (progress_update is not None):
            progress_update(0, d.total)

        if (self.format is None or (self.format.color_family != YUV and self.format.color_family != GRAY)) and y4m:
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
        cdef bytes b = header.encode('utf-8')
        cdef int dummy = 0
        if y4m:
#ifdef _WIN32
            windows.WriteFile(d.handle, <char*>b, len(b), &dummy, NULL)
#else
            posix.unistd.write(d.handle, <char*>b, len(b))
#endif
        d.condition.acquire()

        for n in range(min(prefetch, d.total)):
            self.funcs.getFrameAsync(n, self.node, callback, <void *>d)

        d.condition.wait()
        d.condition.release()

        if d.error:
            raise Error(d.error)

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
            #// this is just a big number that no one's likely to use, hence the -68
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
                n = self.num_frames + val
            else:
                n = val
            if n < 0:
                raise IndexError('List index out of bounds')
            return self.core.std.Trim(clip=self, first=n, length=1)
        else:
            raise TypeError("index must be int or slice")

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

cdef VideoNode createVideoNode(vapoursynth.VSNodeRef *node, vapoursynth.VSAPI *funcs, Core core):
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
    instance.fps_num = instance.vi.fpsNum
    instance.fps_den = instance.vi.fpsDen
    instance.flags = instance.vi.flags
    return instance

cdef class Core(object):
    cdef vapoursynth.VSCore *core
    cdef vapoursynth.VSAPI *funcs
    cdef int num_threads
    cdef bint add_cache
    cdef bint accept_lowercase

    def __cinit__(self, int threads = 0, bint add_cache = True, bint accept_lowercase = False):
        self.funcs = vapoursynth.getVapourSynthAPI(2)
        self.num_threads = threads
        if self.funcs == NULL:
            raise Error('Failed to obtain VapourSynth API pointer. Is the Python module and loaded core library mismatched?')
        self.core = self.funcs.createCore(&self.num_threads)
        self.add_cache = add_cache
        self.accept_lowercase = accept_lowercase

    def __dealloc__(self):
        if self.funcs:
            self.funcs.freeCore(self.core)

    def __getattr__(self, name):
        cdef vapoursynth.VSPlugin *plugin
        tname = name.encode('utf-8')
        cdef char *cname = tname
        plugin = self.funcs.getPluginNs(cname, self.core)

        if plugin:
            return createPlugin(plugin, self.funcs, self)
        else:
            raise Error('No attribute with the name ' + name + ' exists. Did you mistype a plugin namespace?')

    def list_functions(self):
        cdef VSMap *m = self.funcs.getPlugins(self.core)
        cdef VSMap *n
        cdef bytes b
        cdef str sout = ''

        for i in range(self.funcs.propNumKeys(m)):
            a = self.funcs.propGetData(m, self.funcs.propGetKey(m, i), 0, NULL)
            a = a.decode('utf-8')
            a = a.split(';', 2)
            sout += a[2] + '\n'
            sout += '\tnamespace:\t' + a[0] + '\n'
            sout += '\tidentifier:\t' + a[1] + '\n'
            b = a[1].encode('utf-8')
            n = self.funcs.getFunctions(self.funcs.getPluginId(b, self.core))

            for j in range(self.funcs.propNumKeys(n)):
                c = self.funcs.propGetData(n, self.funcs.propGetKey(n, j), 0, NULL)
                c = c.decode('utf-8')
                c = c.split(';', 1)
                c[1] = c[1].replace(';', '; ', c[1].count(';')-1)
                sout += '\t\t' + c[0] + '(' + c[1] +')\n'

            self.funcs.freeMap(n)

        self.funcs.freeMap(m)
        return sout

    def register_format(self, int color_family, int sample_type, int bits_per_sample, int subsampling_w, int subsampling_h):
        return createFormat(self.funcs.registerFormat(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h, self.core))

    def get_format(self, int id):
        cdef VSFormat *f = self.funcs.getFormatPreset(id, self.core)

        if f == NULL:
            raise Error('Internal error')
        else:
            return createFormat(f)

    def version(self):
        cdef VSVersion *v = self.funcs.getVersion()
        return v.versionString.decode('utf-8')

    def __str__(self):
        cdef str s = 'Core\n'
        s += self.version() + '\n'
        s += '\tNumber of Threads: ' + str(self.num_threads) + '\n'  
        s += '\tAdd Caches: ' + str(self.add_cache) + '\n'
        s += '\tAccept Lowercase: ' + str(self.accept_lowercase) + '\n'
        return s

cdef class Plugin(object):
    cdef Core core
    cdef VSPlugin *plugin
    cdef vapoursynth.VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __getattr__(self, name):
        tname = name.encode('utf-8')
        cdef char *cname = tname
        cdef VSMap *m = self.funcs.getFunctions(self.plugin)
        lc_match = False
        cs_match = False

        for i in range(self.funcs.propNumKeys(m)):
            cname = self.funcs.propGetKey(m, i)
            orig_name = cname.decode('utf-8')
            lc_name = orig_name.lower()

            if orig_name == name:
                cs_match = True
                break

            if lc_name == name:
                lc_match = True
                break

        if cs_match or (lc_match and self.core.accept_lowercase):
            signature = self.funcs.propGetData(m, cname, 0, NULL).decode('utf-8').split(';', 1)
            self.funcs.freeMap(m)
            return createFunction(orig_name, signature[1], self, self.funcs)
        else:
            self.funcs.freeMap(m)
            raise Error('There is no function named ' + name)

    def list_functions(self):
        cdef VSMap *n
        cdef bytes b
        cdef str sout = ''

        n = self.funcs.getFunctions(self.plugin)

        for j in range(self.funcs.propNumKeys(n)):
            c = self.funcs.propGetData(n, self.funcs.propGetKey(n, j), 0, NULL)
            c = c.decode('utf-8')
            c = c.split(';', 1)
            c[1] = c[1].replace(';', '; ', c[1].count(';')-1)
            sout += '\t\t' + c[0] + '(' + c[1] +')\n'

        self.funcs.freeMap(n)
        return sout

cdef Plugin createPlugin(vapoursynth.VSPlugin *plugin, vapoursynth.VSAPI *funcs, Core core):
    cdef Plugin instance = Plugin.__new__(Plugin)    
    instance.core = core
    instance.plugin = plugin
    instance.funcs = funcs
    return instance

cdef class Function(object):
    cdef Plugin plugin
    cdef str name
    cdef str signature
    cdef vapoursynth.VSAPI *funcs

    def __init__(self):
        raise Error('Class cannot be instantiated directly')

    def __call__(self, *args, **kwargs):
        cdef VSMap *inm
        cdef VSMap *outm
        cdef char *cname
        ndict = {}
        
        sigs = self.signature.split(';')
        csig = 0
        numsig = len(sigs)
        
        #// naively insert named arguments
        for key in kwargs:
            nkey = key
            if key[0] == '_':
                nkey = key[1:]
            if isinstance(kwargs[key], Link):
                ndict[nkey + '_prop'] = kwargs[key].prop
                ndict[nkey] = kwargs[key].val
            else:
                ndict[nkey] = kwargs[key]

        #// match up unnamed arguments to the first unused name in order
        for arg in args:
            key = sigs[csig].split(':', 1)
            key = key[0]

            while key in ndict:
                csig = csig + 1

                if csig >= numsig:
                    raise Error('There are more unnamed arguments given than unspecified arguments to match')

                key = sigs[csig].split(':', 1)
                key = key[0]
            if isinstance(arg, Link):
                ndict[key + '_prop'] = arg.prop
                ndict[key] = arg.val
            else:
                ndict[key] = arg

        inm = self.funcs.newMap()

        try:
            dictToMap(ndict, inm, self.plugin.core, self.funcs)
        except Error as e:
            self.funcs.freeMap(inm)
            raise Error(self.name + ': ' + str(e))

        tname = self.name.encode('utf-8')
        cname = tname
        outm = self.funcs.invoke(self.plugin.plugin, cname, inm)
        self.funcs.freeMap(inm)
        cdef char *err = self.funcs.getError(outm)
        cdef bytes emsg

        if err:
            emsg = err
            self.funcs.freeMap(outm)
            raise Error(emsg.decode('utf-8'))

        retdict = mapToDict(outm, True, self.plugin.core.add_cache, self.plugin.core, self.funcs)
        self.funcs.freeMap(outm)
        return retdict

cdef Function createFunction(str name, str signature, Plugin plugin, vapoursynth.VSAPI *funcs):
    cdef Function instance = Function.__new__(Function)    
    instance.name = name
    instance.signature = signature
    instance.plugin = plugin
    instance.funcs = funcs
    return instance

#// for python functions being executed by vs

cdef void __stdcall freeFunc(void *pobj) nogil:
    with gil:
        Py_DECREF(<Func>pobj)

cdef void __stdcall publicFunction(VSMap *inm, VSMap *outm, void *userData, VSCore *core, VSAPI *vsapi) nogil:
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

#// for whole script evaluation and export

cdef public struct VPYScriptExport:
    void *pynode
    VSNodeRef *node
    void *errstr
    char *error
    VSAPI *vsapi
    int num_threads
    int enable_v210
    
cdef public api int __stdcall vpy_evaluate_text(char *utf8text, char *fn, VPYScriptExport *extp) nogil:
    extp.node = NULL
    extp.pynode = NULL
    extp.errstr = NULL
    extp.error = NULL
    extp.vsapi = NULL
    extp.num_threads = 0
    extp.enable_v210 = 0

    with gil:
        try:
            evaldict = {}
            comp = compile(utf8text.decode('utf-8'), fn.decode('utf-8'), 'exec')
            exec(comp) in evaldict
            node = evaldict['last']
            try:
                extp.enable_v210 = evaldict['enable_v210']
            except:
                pass
            if isinstance(node, VideoNode):
                Py_INCREF(node)
                extp.pynode = <void *>node
                extp.node = (<VideoNode>node).node
                extp.vsapi = (<VideoNode>node).funcs
                extp.num_threads = (<VideoNode>node).core.num_threads
            else:
                extp.error = 'No clip returned in last variable'
                return 3
        except BaseException, e:
            estr = 'Python exception: ' + str(e)
            estr = estr.encode('utf-8')
            Py_INCREF(estr)
            extp.errstr = <void *>estr
            extp.error = estr
            return 2
        except:
            extp.error = 'Unspecified Python exception'
            return 1
        return 0

cdef public api int __stdcall vpy_evaluate_file(char *fn, VPYScriptExport *extp) nogil:
    extp.node = NULL
    extp.pynode = NULL
    extp.errstr = NULL
    extp.error = NULL
    extp.vsapi = NULL
    extp.num_threads = 0
    extp.enable_v210 = 0

    with gil:
        try:
            script = open(fn.decode('utf-8')).read()
        except BaseException, e:
            estr = 'Python exception: ' + str(e)
            estr = estr.encode('utf-8')
            Py_INCREF(estr)
            extp.errstr = <void *>estr
            extp.error = estr
            return 2
        except:
            extp.error = 'Unspecified Python exception'
            return 1
        encscript = script.encode('utf-8')
        return vpy_evaluate_text(encscript, fn, extp)

cdef public api int __stdcall vpy_free_script(VPYScriptExport *extp) nogil:
    with gil:
        if extp.pynode:
            node = <VideoNode>extp.pynode
            Py_DECREF(node)
        if extp.errstr:
            errstr = <bytes>extp.errstr
            Py_DECREF(errstr)   
