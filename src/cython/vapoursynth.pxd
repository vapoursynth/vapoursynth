#
# Copyright (c) 2012-2013 Fredrik Mellbin
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

    cdef enum VSNodeFlags:
        nfNoCache = 1

    cdef enum VSGetPropErrors:
        peUnset = 1
        peType  = 2
        peIndex = 4

    cdef enum VSPropAppendMode:
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

    cdef enum VSActivationReason:
        arInitial = 0
        arFrameReady = 1
        arAllFramesReady = 2
        arError = -1
        
    cdef enum VSMessageType:
        mtDebug = 0,
        mtWarning = 1,
        mtCritical = 2,
        mtFatal = 3

    ctypedef void (__stdcall *VSFrameDoneCallback)(void *userData, const VSFrameRef *f, int n, VSNodeRef *node, const char *errorMsg)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSPublicFunction)(const VSMap *input, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterInit)(VSMap *input, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
    ctypedef const VSFrameRef *(__stdcall *VSFilterGetFrame)(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSFreeFuncData)(void *userData)
    ctypedef void (__stdcall *VSMessageHandler)(int msgType, const char *msg, void *userData)

    ctypedef struct VSAPI:
        VSCore *createCore(int threads) nogil
        void freeCore(VSCore *core) nogil
        void registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) nogil
        void createFilter(VSMap *input, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        VSMap *invoke(VSPlugin *plugin, const char *name, const VSMap *args) nogil
        void setError(VSMap *map, const char *errorMessage) nogil
        char *getError(const VSMap *map) nogil
        void setFilterError(const char *errorMessage, VSFrameContext *frameCtx) nogil

        VSFormat *getFormatPreset(int id, VSCore *core) nogil
        VSFormat *registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil

        void getFrameAsync(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData) nogil
        const VSFrameRef *getFrame(int n, VSNodeRef *node, char *errorMsg, int bufSize) nogil
        void requestFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        const VSFrameRef *getFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        const VSFrameRef *cloneFrameRef(VSFrameRef *f) nogil
        VSNodeRef *cloneNodeRef(VSNodeRef *node) nogil
        void freeFrame(const VSFrameRef *f) nogil
        void freeNode(VSNodeRef *node) nogil
        VSFrameRef *newVideoFrame(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) nogil
        VSFrameRef *copyFrame(const VSFrameRef *f, VSCore *core) nogil
        void copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) nogil

        int getStride(const VSFrameRef *f, int plane) nogil
        const uint8_t *getReadPtr(const VSFrameRef *f, int plane) nogil
        uint8_t *getWritePtr(VSFrameRef *f, int plane) nogil

        const VSVideoInfo *getVideoInfo(VSNodeRef *node) nogil
        void setVideoInfo(const VSVideoInfo *vi, VSNode *node) nogil
        const VSFormat *getFrameFormat(const VSFrameRef *f) nogil
        int getFrameWidth(const VSFrameRef *f, int plane) nogil
        int getFrameHeight(const VSFrameRef *f, int plane) nogil
        const VSMap *getFramePropsRO(const VSFrameRef *f) nogil
        VSMap *getFramePropsRW(VSFrameRef *f) nogil
        int propNumKeys(const VSMap *map) nogil
        const char *propGetKey(const VSMap *map, int index) nogil
        int propNumElements(const VSMap *map, const char *key) nogil
        char propGetType(const VSMap *map, const char *key) nogil

        VSMap *createMap() nogil
        void freeMap(VSMap *map) nogil
        void clearMap(VSMap *map) nogil

        int64_t propGetInt(const VSMap *map, const char *key, int index, int *error) nogil
        double propGetFloat(const VSMap *map, const char *key, int index, int *error) nogil
        const char *propGetData(const VSMap *map, const char *key, int index, int *error) nogil
        int propGetDataSize(const VSMap *map, const char *key, int index, int *error) nogil
        VSNodeRef *propGetNode(const VSMap *map, const char *key, int index, int *error) nogil
        const VSFrameRef *propGetFrame(const VSMap *map, const char *key, int index, int *error) nogil

        bint propDeleteKey(VSMap *map, const char *key) nogil
        bint propSetInt(VSMap *map, const char *key, int64_t i, int append) nogil
        bint propSetFloat(VSMap *map, const char *key, double d, int append) nogil
        bint propSetData(VSMap *map, const char *key, const char *data, int size, int append) nogil
        bint propSetNode(VSMap *map, const char *key, VSNodeRef *node, int append) nogil
        bint propSetFrame(VSMap *map, const char *key, const VSFrameRef *f, int append) nogil

        VSPlugin *getPluginById(const char *identifier, VSCore *core) nogil
        VSPlugin *getPluginByNs(const char *ns, VSCore *core) nogil
        VSMap *getPlugins(VSCore *core) nogil
        VSMap *getFunctions(VSPlugin *plugin) nogil

        const VSCoreInfo *getCoreInfo(VSCore *core) nogil
        VSFuncRef *propGetFunc(const VSMap *map, const char *key, int index, int *error) nogil
        bint propSetFunc(VSMap *map, const char *key, VSFuncRef *func, int append) nogil
        void callFunc(VSFuncRef *func, const VSMap *inm, VSMap *outm, VSCore *core, const VSAPI *vsapi) nogil
        VSFuncRef *createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free) nogil
        void freeFunc(VSFuncRef *f) nogil

        int64_t setMaxCacheSize(int64_t bytes, VSCore *core) nogil

        void setMessageHandler(VSMessageHandler handler, void *userData) nogil
    const VSAPI *getVapourSynthAPI(int version) nogil
