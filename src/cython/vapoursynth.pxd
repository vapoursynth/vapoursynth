#  Copyright (c) 2012 Fredrik Mellbin
#
#  This file is part of VapourSynth.
#
#  VapourSynth is free software: you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as
#  published by the Free Software Foundation, either version 3 of the
#  License, or (at your option) any later version.
#
#  VapourSynth is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with VapourSynth.  If not, see <http://www.gnu.org/licenses/>.

from libc.stdint cimport uint8_t, uint32_t, int64_t

cdef extern from "../../include/VapourSynth.h":
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

        pfRGB24 = cmRGB + 10
        pfRGB27
        pfRGB30
        pfRGB48

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

    cdef struct VSVersion:
        int core
        int api
        char *versionString

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
        VSCore *createVSCore(int threads)
        void freeVSCore(VSCore *core)

        void registerFunction(char *name, char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin)
        VSNodeRef *createFilter(VSMap *input, VSMap *out, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *filterData, VSCore *core)
        VSMap *invoke(VSPlugin *plugin, char *name, VSMap *args)
        void setError(VSMap *map, char *errorMessage)
        char *getError(VSMap *map)
        void setFilterError(char *errorMessage, VSFrameContext *frameCtx)

        VSFormat *getFormatPreset(int id, VSCore *core)
        VSFormat *registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core)
 
        void getFrameAsync(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData)
        VSFrameRef *getFrame(int n, VSNodeRef *node, char *errorMsg, int bufSize)
        void requestFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx)
        VSFrameRef * getFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx)
        VSFrameRef *cloneFrameRef(VSFrameRef *f)
        VSNodeRef *cloneNodeRef(VSNodeRef *node)
        void freeFrame(VSFrameRef *f)
        void freeNode(VSNodeRef *node)
        VSFrameRef *newVideoFrame(VSFormat *format, int width, int height, VSFrameRef *propSrc, VSCore *core)
        VSFrameRef *copyFrame(VSFrameRef *f, VSCore *core)
        void copyFrameProps(VSFrameRef *src, VSFrameRef *dst, VSCore *core)

        int getStride(VSFrameRef *f, int plane)
        uint8_t *getReadPtr(VSFrameRef *f, int plane)
        uint8_t *getWritePtr(VSFrameRef *f, int plane)

        VSVideoInfo *getVideoInfo(VSNodeRef *node)
        void setVideoInfo(VSVideoInfo *vi, VSNode *node)
        VSFormat *getFrameFormat(VSFrameRef *f)
        int getFrameWidth(VSFrameRef *f, int plane)
        int getFrameHeight(VSFrameRef *f, int plane)
        VSMap *getFramePropsRO(VSFrameRef *f)
        VSMap *getFramePropsRW(VSFrameRef *f)
        int propNumKeys(VSMap *map)
        char *propGetKey(VSMap *map, int index)
        int propNumElements(VSMap *map, char *key)
        char propGetType(VSMap *map, char *key)

        VSMap *newMap()
        void freeMap(VSMap *map)
        void clearMap(VSMap *map)

        int64_t propGetInt(VSMap *map, char *key, int index, int *error)
        double propGetFloat(VSMap *map, char *key, int index, int *error)
        char *propGetData(VSMap *map, char *key, int index, int *error)
        int propGetDataSize(VSMap *map, char *key, int index, int *error)
        VSNodeRef *propGetNode(VSMap *map, char *key, int index, int *error)
        VSFrameRef *propGetFrame(VSMap *map, char *key, int index, int *error)

        bint propDeleteKey(VSMap *map, char *key)
        bint propSetInt(VSMap *map, char *key, int64_t i, bint append)
        bint propSetFloat(VSMap *map, char *key, double d, bint append)
        bint propSetData(VSMap *map, char *key, char *data, int size, bint append)
        bint propSetNode(VSMap *map, char *key, VSNodeRef *node, bint append)
        bint propSetFrame(VSMap *map, char *key, VSFrameRef *f, bint append)

        VSPlugin *getPluginId(char *identifier, VSCore *core)
        VSPlugin *getPluginNs(char *ns, VSCore *core)
        VSMap *getPlugins(VSCore *core)
        VSMap *getFunctions(VSPlugin *plugin)

        VSVersion *getVersion()
        VSFuncRef *propGetFunc(VSMap *map, char *key, int index, int *error)
        int propSetFunc(VSMap *map, char *key, VSFuncRef *func, int append)
        void callFunc(VSFuncRef *func, VSMap *inm, VSMap *outm, VSCore *core, VSAPI *vsapi)
        VSFuncRef *createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free)
        void freeFunc(VSFuncRef *f)

    VSAPI *getVapourSynthAPI(int version)
