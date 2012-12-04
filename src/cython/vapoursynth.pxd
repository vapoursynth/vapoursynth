#
# Copyright (c) 2012 Fredrik Mellbin
#
# This file is part of VapourSynth.
#
# VapourSynth is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# VapourSynth is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with VapourSynth; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

from libc.stdint cimport uint8_t, uint32_t, int64_t, uintptr_t

cdef extern from "include/VapourSynth.h" nogil:
    ctypedef struct VSFrameRef:
        pass
    ctypedef struct VSNodeRef:
        pass
    ctypedef struct VSFuncRef:
        pass
    ctypedef struct VSCore:
        pass
    ctypedef struct VSPlugin:
        pass
    ctypedef struct VSNode:
        pass
    ctypedef struct VSMap:
        pass
    ctypedef struct VSFrameContext:
        pass

    cdef enum VSColorFamily:
        cmGray  = 1000000
        cmRGB   = 2000000
        cmYUV   = 3000000
        cmYCoCg = 4000000
        cmCompat= 9000000

    cdef enum VSSampleType:
        stInteger = 0
        stFloat   = 1

    cdef enum VSPresetFormat:
        pfNone = 0

        pfGray8 = cmGray + 10
        pfGray16
        
        pfGrayH
        pfGrayS
    
        pfYUV420P8 = cmYUV + 10
        pfYUV422P8
        pfYUV444P8
        pfYUV410P8
        pfYUV411P8
        pfYUV440P8

        pfYUV420P9
        pfYUV422P9
        pfYUV444P9

        pfYUV420P10
        pfYUV422P10
        pfYUV444P10

        pfYUV420P16
        pfYUV422P16
        pfYUV444P16
        
        pfYUV444PH
        pfYUV444PS

        pfRGB24 = cmRGB + 10
        pfRGB27
        pfRGB30
        pfRGB48
        
        pfRGBH
        pfRGBS

        pfCompatBGR32 = cmCompat + 10
        pfCompatYUY2

    cdef enum VSFilterMode:
        fmParallel = 100
        fmParallelRequestsOnly = 200
        fmSerialUnordered = 300
        fmSerial = 400

    ctypedef struct VSFormat:
        char name[32]
        int id
        int colorFamily
        int sampleType
        int bytesPerSample
        int bitsPerSample
        int subSamplingW
        int subSamplingH
        int numPlanes

    cdef enum NodeFlags:
        nfNoCache = 1

    cdef enum GetPropErrors:
        peUnset = 1
        peType  = 2
        peIndex = 4
        
    cdef enum PropAppendMode:
        paReplace = 0
        paAppend  = 1
        paTouch   = 2

    cdef struct VSCoreInfo:
        char *versionString
        int core
        int api
        int numThreads
        int64_t maxFramebufferSize
        int64_t usedFramebufferSize

    cdef struct VSVideoInfo:
        VSFormat *format
        int width
        int height
        int numFrames
        int fpsNum
        int fpsDen
        int flags

    cdef enum ActivationReason:
        arInitial = 0
        arFrameReady = 1
        arAllFramesReady = 2
        arError = -1

    ctypedef void (__stdcall *VSFrameDoneCallback)(void *userData, VSFrameRef *f, int n, VSNodeRef *node, char *errorMsg)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, VSAPI *vsapi)
    ctypedef void (__stdcall *VSPublicFunction)(VSMap *input, VSMap *out, void *userData, VSCore *core, VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterInit)(VSMap *input, VSMap *out, void **instanceData, VSNode *node, VSCore *core, VSAPI *vsapi)
    ctypedef VSFrameRef *(__stdcall *VSFilterGetFrame)(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, VSAPI *vsapi)
    ctypedef void (__stdcall *VSFreeFuncData)(void *userData)

    ctypedef struct VSAPI:
        VSCore *createCore(int threads) nogil
        void freeCore(VSCore *core) nogil

        void registerFunction(char *name, char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) nogil
        VSNodeRef *createFilter(VSMap *input, VSMap *out, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *filterData, VSCore *core) nogil
        VSMap *invoke(VSPlugin *plugin, char *name, VSMap *args) nogil
        void setError(VSMap *map, char *errorMessage) nogil
        char *getError(VSMap *map) nogil
        void setFilterError(char *errorMessage, VSFrameContext *frameCtx) nogil

        VSFormat *getFormatPreset(int id, VSCore *core) nogil
        VSFormat *registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil
 
        void getFrameAsync(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData) nogil
        VSFrameRef *getFrame(int n, VSNodeRef *node, char *errorMsg, int bufSize) nogil
        void requestFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        VSFrameRef *getFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        VSFrameRef *cloneFrameRef(VSFrameRef *f) nogil
        VSNodeRef *cloneNodeRef(VSNodeRef *node) nogil
        void freeFrame(VSFrameRef *f) nogil
        void freeNode(VSNodeRef *node) nogil
        VSFrameRef *newVideoFrame(VSFormat *format, int width, int height, VSFrameRef *propSrc, VSCore *core) nogil
        VSFrameRef *copyFrame(VSFrameRef *f, VSCore *core) nogil
        void copyFrameProps(VSFrameRef *src, VSFrameRef *dst, VSCore *core) nogil

        int getStride(VSFrameRef *f, int plane) nogil
        uint8_t *getReadPtr(VSFrameRef *f, int plane) nogil
        uint8_t *getWritePtr(VSFrameRef *f, int plane) nogil

        VSVideoInfo *getVideoInfo(VSNodeRef *node) nogil
        void setVideoInfo(VSVideoInfo *vi, VSNode *node) nogil
        VSFormat *getFrameFormat(VSFrameRef *f) nogil
        int getFrameWidth(VSFrameRef *f, int plane) nogil
        int getFrameHeight(VSFrameRef *f, int plane) nogil
        VSMap *getFramePropsRO(VSFrameRef *f) nogil
        VSMap *getFramePropsRW(VSFrameRef *f) nogil
        int propNumKeys(VSMap *map) nogil
        char *propGetKey(VSMap *map, int index) nogil
        int propNumElements(VSMap *map, char *key) nogil
        char propGetType(VSMap *map, char *key) nogil

        VSMap *newMap() nogil
        void freeMap(VSMap *map) nogil
        void clearMap(VSMap *map) nogil

        int64_t propGetInt(VSMap *map, char *key, int index, int *error) nogil
        double propGetFloat(VSMap *map, char *key, int index, int *error) nogil
        char *propGetData(VSMap *map, char *key, int index, int *error) nogil
        int propGetDataSize(VSMap *map, char *key, int index, int *error) nogil
        VSNodeRef *propGetNode(VSMap *map, char *key, int index, int *error) nogil
        VSFrameRef *propGetFrame(VSMap *map, char *key, int index, int *error) nogil

        bint propDeleteKey(VSMap *map, char *key) nogil
        bint propSetInt(VSMap *map, char *key, int64_t i, int append) nogil
        bint propSetFloat(VSMap *map, char *key, double d, int append) nogil
        bint propSetData(VSMap *map, char *key, char *data, int size, int append) nogil
        bint propSetNode(VSMap *map, char *key, VSNodeRef *node, int append) nogil
        bint propSetFrame(VSMap *map, char *key, VSFrameRef *f, int append) nogil

        VSPlugin *getPluginId(char *identifier, VSCore *core) nogil
        VSPlugin *getPluginNs(char *ns, VSCore *core) nogil
        VSMap *getPlugins(VSCore *core) nogil
        VSMap *getFunctions(VSPlugin *plugin) nogil

        VSCoreInfo *getCoreInfo(VSCore *core) nogil
        VSFuncRef *propGetFunc(VSMap *map, char *key, int index, int *error) nogil
        int propSetFunc(VSMap *map, char *key, VSFuncRef *func, int append) nogil
        void callFunc(VSFuncRef *func, VSMap *inm, VSMap *outm, VSCore *core, VSAPI *vsapi) nogil
        VSFuncRef *createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free) nogil
        void freeFunc(VSFuncRef *f) nogil

        int64_t setMaxCacheSize(int64_t bytes, VSCore *core) nogil
    VSAPI *getVapourSynthAPI(int version) nogil
