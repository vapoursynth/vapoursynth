#
# Copyright (c) 2012-2019 Fredrik Mellbin
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

from libc.stdint cimport uint8_t, uint32_t, int64_t, uint64_t, uintptr_t
from libc.stddef cimport ptrdiff_t

cdef extern from "include/VapourSynth4.h" nogil:
    enum:
        VAPOURSYNTH_API_MAJOR
        VAPOURSYNTH_API_MINOR
        VAPOURSYNTH_API_VERSION

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
    ctypedef struct VSMap:
        pass
    ctypedef struct VSFrameContext:
        pass
        
    cpdef enum MediaType "VSMediaType":
        VIDEO "mtVideo"
        AUDIO "mtAudio"

    cpdef enum ColorFamily "VSColorFamily":
        UNDEFINED "cfUndefined"
        GRAY "cfGray"
        RGB "cfRGB"
        YUV "cfYUV"
        YCOCG "cfYCoCg"
        cfCOMPATBGR32 "cfCompatBGR32"
        cfCOMPATYUY2 "cfCompatYUY2"

    cpdef enum SampleType "VSSampleType":
        INTEGER "stInteger"
        FLOAT "stFloat"

    cpdef enum PresetFormat "VSPresetFormat":
        NONE "pfNone"

        GRAY8 "pfGray8"
        GRAY16 "pfGray16"

        GRAYH "pfGrayH"
        GRAYS "pfGrayS"

        YUV420P8 "pfYUV420P8"
        YUV422P8 "pfYUV422P8"
        YUV444P8 "pfYUV444P8"
        YUV410P8 "pfYUV410P8"
        YUV411P8 "pfYUV411P8"
        YUV440P8 "pfYUV440P8"

        YUV420P9 "pfYUV420P9"
        YUV422P9 "pfYUV422P9"
        YUV444P9 "pfYUV444P9"

        YUV420P10 "pfYUV420P10"
        YUV422P10 "pfYUV422P10"
        YUV444P10 "pfYUV444P10"
        
        YUV420P12 "pfYUV420P12"
        YUV422P12 "pfYUV422P12"
        YUV444P12 "pfYUV444P12"
        
        YUV420P14 "pfYUV420P14"
        YUV422P14 "pfYUV422P14"
        YUV444P14 "pfYUV444P14"
        
        YUV420P16 "pfYUV420P16"
        YUV422P16 "pfYUV422P16"
        YUV444P16 "pfYUV444P16"

        YUV444PH "pfYUV444PH"
        YUV444PS "pfYUV444PS"

        RGB24 "pfRGB24"
        RGB27 "pfRGB27"
        RGB30 "pfRGB30"
        RGB48 "pfRGB48"

        RGBH "pfRGBH"
        RGBS "pfRGBS"

        COMPATBGR32 "pfCompatBGR32"
        COMPATYUY2 "pfCompatYUY2"

    enum VSFilterMode:
        fmParallel
        fmParallelRequestsOnly
        fmUnordered
        fmSerial

    ctypedef struct VSVideoFormat:
        int colorFamily
        int sampleType
        int bytesPerSample
        int bitsPerSample
        int subSamplingW
        int subSamplingH
        int numPlanes
        
    cpdef enum AudioChannels "VSAudioChannels":
        FRONT_LEFT "acFrontLeft"
        FRONT_RIGHT "acFrontRight"
        FRONT_CENTER "acFrontCenter"
        LOW_FREQUENCY "acLowFrequency"
        BACK_LEFT "acBackLeft"
        BACK_RIGHT "acBackRight"
        FRONT_LEFT_OF_CENTER "acFrontLeftOFCenter"
        FRONT_RIGHT_OF_CENTER "acFrontRightOFCenter"
        BACK_CENTER "acBackCenter"
        SIDE_LEFT "acSideLeft"
        SIDE_RIGHT "acSideRight"
        TOP_CENTER "acTopCenter"
        TOP_FRONT_LEFT "acTopFrontLeft"
        TOP_FRONT_CENTER "acTopFrontCenter"
        TOP_FRONT_RIGHT "acTopFrontRight"
        TOP_BACK_LEFT "acTopBackLeft"
        TOP_BACK_CENTER "acTopBackCenter"
        TOP_BACK_RIGHT "acTopBackRight"     
        STEREO_LEFT "acStereoLeft"
        STEREO_RIGHT "acStereoRight"
        WIDE_LEFT "acWideLeft"   
        WIDE_RIGHT "acWideRight"
        SURROUND_DIRECT_LEFT "acSurroundDirectLeft"   
        SURROUND_DIRECT_RIGHT "acSurroundDirectRight"
        LOW_FREQUENCY2 "acLowFrequency2"
        
    ctypedef struct VSAudioFormat:
        int sampleType
        int bitsPerSample
        int bytesPerSample
        int numChannels
        uint64_t channelLayout

    enum VSNodeFlags:
        nfNoCache
        nfIsCache
        nfMakeLinear

    enum VSPropTypes:
        ptUnset
        ptInt
        ptFloat
        ptData
        ptFunction
        ptVideoNode
        ptAudioNode
        ptVideoFrame
        ptAudioFrame

    enum VSGetPropErrors:
        peUnset
        peType
        peIndex

    enum VSPropAppendMode:
        paReplace
        paAppend

    struct VSCoreInfo:
        char *versionString
        int core
        int api
        int numThreads
        int64_t maxFramebufferSize
        int64_t usedFramebufferSize

    struct VSVideoInfo:
        VSVideoFormat format
        int64_t fpsNum
        int64_t fpsDen
        int width
        int height
        int numFrames
        
    struct VSAudioInfo:
        VSAudioFormat format
        int sampleRate
        int64_t numSamples
        int numFrames

    enum VSActivationReason:
        arInitial
        arFrameReady
        arAllFramesReady
        arError

    enum VSMessageType:
        mtDebug
        mtWarning
        mtCritical
        mtFatal
        
    enum VSCoreFlags:
        cfDisableAutoLoading


    ctypedef const VSAPI *(__stdcall *VSGetVapourSynthAPI)(int version)

    ctypedef void (__stdcall *VSPublicFunction)(const VSMap *input, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) 
    ctypedef void (__stdcall *VSRegisterFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin)  
    ctypedef void (__stdcall *VSConfigPlugin)(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readonly, VSPlugin *plugin)  
    ctypedef void (__stdcall *VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)  
    ctypedef void (__stdcall *VSFreeFuncData)(void *userData)
    ctypedef const VSFrameRef *(__stdcall *VSFilterGetFrame)(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi)
   
    ctypedef void (__stdcall *VSFrameDoneCallback)(void *userData, const VSFrameRef *f, int n, VSNodeRef *node, const char *errorMsg)
    ctypedef void (__stdcall *VSMessageHandler)(int msgType, const char *msg, void *userData)
    ctypedef void (__stdcall *VSMessageHandlerFree)(void *userData)


    ctypedef struct VSAPI:
        VSCore *createCore(int threads, int flags) nogil
        void freeCore(VSCore *core) nogil
        
        const VSFrameRef *cloneFrameRef(VSFrameRef *f) nogil
        VSNodeRef *cloneNodeRef(VSNodeRef *node) nogil
        VSFuncRef *cloneFuncRef(VSFuncRef *f) nogil
        
        void freeFrame(const VSFrameRef *f) nogil
        void freeNode(VSNodeRef *node) nogil
        void freeFunc(VSFuncRef *f) nogil
        
        VSFrameRef *newVideoFrame(const VSVideoFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) nogil
        VSFrameRef *copyFrame(const VSFrameRef *f, VSCore *core) nogil
        void copyFrameProps(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) nogil
    
        void registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) nogil
        VSPlugin *getPluginById(const char *identifier, VSCore *core) nogil
        VSPlugin *getPluginByNs(const char *ns, VSCore *core) nogil
        VSMap *getPlugins(VSCore *core) nogil
        VSMap *getFunctions(VSPlugin *plugin) nogil
        void setError(VSMap *map, const char *errorMessage) nogil
        char *getError(const VSMap *map) nogil
        void setFilterError(const char *errorMessage, VSFrameContext *frameCtx) nogil
        VSMap *invoke(VSPlugin *plugin, const char *name, const VSMap *args) nogil

        const VSFrameRef *getFrame(int n, VSNodeRef *node, char *errorMsg, int bufSize) nogil
        void getFrameAsync(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData) nogil
        const VSFrameRef *getFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        void requestFrameFilter(int n, VSNodeRef *node, VSFrameContext *frameCtx) nogil
        void queryCompletedFrame(VSNodeRef **node, int *n, VSFrameContext *frameCtx) nogil
        void releaseFrameEarly(VSNodeRef *node, int n, VSFrameContext *frameCtx) nogil

        ptrdiff_t getStride(const VSFrameRef *f, int plane) nogil
        const uint8_t *getReadPtr(const VSFrameRef *f, int plane) nogil
        uint8_t *getWritePtr(VSFrameRef *f, int plane) nogil

        VSFuncRef *createFunc(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core) nogil
        void callFunc(VSFuncRef *func, const VSMap *inm, VSMap *outm) nogil

        VSMap *createMap() nogil
        void freeMap(VSMap *map) nogil
        void clearMap(VSMap *map) nogil
        
        const VSVideoInfo *getVideoInfo(VSNodeRef *node) nogil
        const VSVideoFormat *getVideoFrameFormat(const VSFrameRef *f) nogil
        int getFrameWidth(const VSFrameRef *f, int plane) nogil
        int getFrameHeight(const VSFrameRef *f, int plane) nogil
        const VSMap *getFramePropsRO(const VSFrameRef *f) nogil
        VSMap *getFramePropsRW(VSFrameRef *f) nogil
        
        int propNumKeys(const VSMap *map) nogil
        const char *propGetKey(const VSMap *map, int index) nogil
        int propNumElements(const VSMap *map, const char *key) nogil
        int propGetType(const VSMap *map, const char *key) nogil

        int64_t propGetInt(const VSMap *map, const char *key, int index, int *error) nogil
        double propGetFloat(const VSMap *map, const char *key, int index, int *error) nogil
        const char *propGetData(const VSMap *map, const char *key, int index, int *error) nogil
        int propGetDataSize(const VSMap *map, const char *key, int index, int *error) nogil
        VSNodeRef *propGetNode(const VSMap *map, const char *key, int index, int *error) nogil
        const VSFrameRef *propGetFrame(const VSMap *map, const char *key, int index, int *error) nogil
        VSFuncRef *propGetFunc(const VSMap *map, const char *key, int index, int *error) nogil

        bint propDeleteKey(VSMap *map, const char *key) nogil
        bint propSetInt(VSMap *map, const char *key, int64_t i, int append) nogil
        bint propSetFloat(VSMap *map, const char *key, double d, int append) nogil
        bint propSetData(VSMap *map, const char *key, const char *data, int size, int append) nogil
        bint propSetNode(VSMap *map, const char *key, VSNodeRef *node, int append) nogil
        bint propSetFrame(VSMap *map, const char *key, const VSFrameRef *f, int append) nogil
        bint propSetFunc(VSMap *map, const char *key, VSFuncRef *func, int append) nogil

        int64_t setMaxCacheSize(int64_t bytes, VSCore *core) nogil
        int getOutputIndex(VSFrameContext *frameCtx) nogil
        VSFrameRef *newVideoFrame2(const VSVideoFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) nogil
        int setThreadCount(int threads, VSCore *core) nogil

        const char *getPluginPath(const VSPlugin *plugin) nogil

        const int64_t *propGetIntArray(const VSMap *map, const char *key, int *error) nogil
        const double *propGetFloatArray(const VSMap *map, const char *key, int *error) nogil

        int propSetIntArray(VSMap *map, const char *key, const int64_t *i, int size) nogil
        int propSetFloatArray(VSMap *map, const char *key, const double *d, int size) nogil
        
        void logMessage(int msgType, const char *msg) nogil
        int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void *userData) nogil
        int removeMessageHandler(int id) nogil
        void getCoreInfo(VSCore *core, VSCoreInfo *info) nogil

        int propSetEmpty(VSMap *map, const char *key, int type) nogil
        void createVideoFilter(const VSMap *input, VSMap *out, const char *name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        void createAudioFilter(const VSMap *input, VSMap *out, const char *name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        VSFrameRef *newAudioFrame(const VSAudioFormat *format, int sampleRate, const VSFrameRef *propSrc, VSCore *core) nogil
        int queryAudioFormat(VSAudioFormat *format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore *core) nogil
        int queryVideoFormat(VSVideoFormat *format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil
        uint32_t queryVideoFormatID(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil
        int queryVideoFormatByID(VSVideoFormat *format, uint32_t id, VSCore *core) nogil
        void getAudioFormatName(const VSAudioFormat *format, char *buffer) nogil
        void getVideoFormatName(const VSVideoFormat *format, char *buffer) nogil
        const VSAudioInfo *getAudioInfo(VSNodeRef *node) nogil
        const VSAudioFormat *getAudioFrameFormat(const VSFrameRef *f) nogil
     
        int getNodeType(VSNodeRef *node) nogil
        int getNodeFlags(VSNodeRef *node) nogil
        int getFrameType(const VSFrameRef *f) nogil
        int getFrameLength(const VSFrameRef *f) nogil
        int getApiVersion() nogil
        
    const VSAPI *getVapourSynthAPI(int version) nogil
