#
# Copyright (c) 2012-2025 Fredrik Mellbin
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
#cython: language_level=3

from libc.stdint cimport uint8_t, uint32_t, int64_t, uint64_t, uintptr_t
from libc.stddef cimport ptrdiff_t

cdef extern from "include/VapourSynth4.h" nogil:
    enum:
        VS_CURRENT_RELEASE
        VAPOURSYNTH_API_MAJOR
        VAPOURSYNTH_API_MINOR
        VAPOURSYNTH_API_VERSION

    ctypedef struct VSFrame:
        pass
    ctypedef struct VSNode:
        pass
    ctypedef struct VSCore:
        pass
    ctypedef struct VSPlugin:
        pass
    ctypedef struct VSPluginFunction:
        pass
    ctypedef struct VSFunction:
        pass
    ctypedef struct VSMap:
        pass
    ctypedef struct VSLogHandle:
        pass
    ctypedef struct VSFrameContext:
        pass

    enum:
        mtVideo
        mtAudio

    enum:
        cfUndefined
        cfGray
        cfRGB
        cfYUV

    enum:
        stInteger
        stFloat

    enum:
        pfNone

        pfGray8
        pfGray9
        pfGray10
        pfGray12
        pfGray14
        pfGray16
        pfGray32

        pfGrayH
        pfGrayS

        pfYUV410P8
        pfYUV411P8
        pfYUV440P8

        pfYUV420P8
        pfYUV422P8
        pfYUV444P8

        pfYUV420P9
        pfYUV422P9
        pfYUV444P9

        pfYUV420P10
        pfYUV422P10
        pfYUV444P10

        pfYUV420P12
        pfYUV422P12
        pfYUV444P12

        pfYUV420P14
        pfYUV422P14
        pfYUV444P14

        pfYUV420P16
        pfYUV422P16
        pfYUV444P16

        pfYUV420PH
        pfYUV420PS
        
        pfYUV422PH
        pfYUV422PS

        pfYUV444PH
        pfYUV444PS

        pfRGB24
        pfRGB27
        pfRGB30
        pfRGB36
        pfRGB42
        pfRGB48

        pfRGBH
        pfRGBS

    enum:
        fmParallel
        fmParallelRequests
        fmUnordered
        fmFrameState

    ctypedef struct VSVideoFormat:
        int colorFamily
        int sampleType
        int bytesPerSample
        int bitsPerSample
        int subSamplingW
        int subSamplingH
        int numPlanes

    enum:
        acFrontLeft
        acFrontRight
        acFrontCenter
        acLowFrequency
        acBackLeft
        acBackRight
        acFrontLeftOFCenter
        acFrontRightOFCenter
        acBackCenter
        acSideLeft
        acSideRight
        acTopCenter
        acTopFrontLeft
        acTopFrontCenter
        acTopFrontRight
        acTopBackLeft
        acTopBackCenter
        acTopBackRight
        acStereoLeft
        acStereoRight
        acWideLeft
        acWideRight
        acSurroundDirectLeft
        acSurroundDirectRight
        acLowFrequency2

    ctypedef struct VSAudioFormat:
        int sampleType
        int bitsPerSample
        int bytesPerSample
        int numChannels
        uint64_t channelLayout

    enum VSPropertyType:
        ptUnset
        ptInt
        ptFloat
        ptData
        ptFunction
        ptVideoNode
        ptAudioNode
        ptVideoFrame
        ptAudioFrame

    enum VSMapPropertyError:
        peSuccess
        peUnset
        peType
        peIndex
        peError

    enum VSMapAppendMode:
        maReplace
        maAppend

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
        arAllFramesReady
        arError

    enum:
        mtDebug
        mtInformation
        mtWarning
        mtCritical
        mtFatal

    enum:
        ccfEnableGraphInspection
        ccfDisableAutoLoading
        ccfDisableLibraryUnloading

    enum VSPluginConfigFlags:
        pcModifiable

    enum VSDataTypeHint:
        dtUnknown
        dtBinary
        dtUtf8

    struct VSFilterDependency:
        VSNode *source;
        int requestPattern


    ctypedef const VSAPI *(__stdcall *VSGetVapourSynthAPI)(int version)

    ctypedef void (__stdcall *VSPublicFunction)(const VSMap *input, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSInitPlugin)(VSPlugin *plugin, const VSPLUGINAPI *vspapi)

    ctypedef void (__stdcall *VSFreeFunctionData)(void *userData)
    ctypedef const VSFrame *(__stdcall *VSFilterGetFrame)(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
    ctypedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi)

    ctypedef void (__stdcall *VSFrameDoneCallback)(void *userData, const VSFrame *f, int n, VSNode *node, const char *errorMsg)
    ctypedef void (__stdcall *VSLogHandler)(int msgType, const char *msg, void *userData)
    ctypedef void (__stdcall *VSLogHandlerFree)(void *userData)

    ctypedef struct VSPLUGINAPI:
        int getAPIVersion() nogil
        bint configPlugin(const char *identifier, const char *pluginNamespace, const char *name, int pluginVersion, int apiVersion, int flags, VSPlugin *plugin) nogil
        bint registerFunction(const char *name, const char *args, const char *returnType, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) nogil

    ctypedef struct VSAPI:
        # Audio and video filter
        void createVideoFilter(VSMap *out, const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        VSNode *createVideoFilter2(const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        void createAudioFilter(VSMap *out, const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        VSNode *createAudioFilter2(const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) nogil
        int setLinearFilter(VSNode *node) nogil

        void freeNode(VSNode *node) nogil
        VSNode *addNodeRef(VSNode *node) nogil
        int getNodeType(VSNode *node) nogil
        void setCacheMode(VSNode *node, int mode) nogil
        const VSVideoInfo *getVideoInfo(VSNode *node) nogil
        const VSAudioInfo *getAudioInfo(VSNode *node) nogil

        # Frame related
        VSFrame *newVideoFrame(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) nogil
        VSFrame *newVideoFrame2(const VSVideoFormat *format, int width, int height, const VSFrame **planeSrc, const int *planes, const VSFrame *propSrc, VSCore *core) nogil
        VSFrame *newAudioFrame(const VSAudioFormat *format, int sampleRate, const VSFrame *propSrc, VSCore *core) nogil
        VSFrame *newAudioFrame2(const VSAudioFormat *format, int numSamples, const VSFrame **channelSrc, const int *channels, const VSFrame *propSrc, VSCore *core) nogil
        void freeFrame(const VSFrame *f) nogil
        const VSFrame *addFrameRef(const VSFrame *f) nogil
        VSFrame *copyFrame(const VSFrame *f, VSCore *core) nogil
        const VSMap *getFramePropertiesRO(const VSFrame *f) nogil
        VSMap *getFramePropertiesRW(VSFrame *f) nogil

        ptrdiff_t getStride(const VSFrame *f, int plane) nogil
        const uint8_t *getReadPtr(const VSFrame *f, int plane) nogil
        uint8_t *getWritePtr(VSFrame *f, int plane) nogil

        const VSVideoFormat *getVideoFrameFormat(const VSFrame *f) nogil
        const VSAudioFormat *getAudioFrameFormat(const VSFrame *f) nogil
        int getFrameType(const VSFrame *f) nogil
        int getFrameWidth(const VSFrame *f, int plane) nogil
        int getFrameHeight(const VSFrame *f, int plane) nogil
        int getFrameLength(const VSFrame *f) nogil

        # General format functions
        bint getVideoFormatName(const VSVideoFormat *format, char *buffer) nogil
        bint getAudioFormatName(const VSAudioFormat *format, char *buffer) nogil
        bint queryVideoFormat(VSVideoFormat *format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil
        bint queryAudioFormat(VSAudioFormat *format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore *core) nogil
        uint32_t queryVideoFormatID(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) nogil
        int getVideoFormatByID(VSVideoFormat *format, uint32_t id, VSCore *core) nogil

        # Frame requests
        const VSFrame *getFrame(int n, VSNode *node, char *errorMsg, int bufSize) nogil
        void getFrameAsync(int n, VSNode *node, VSFrameDoneCallback callback, void *userData) nogil
        const VSFrame *getFrameFilter(int n, VSNode *node, VSFrameContext *frameCtx) nogil
        void requestFrameFilter(int n, VSNode *node, VSFrameContext *frameCtx) nogil
        void releaseFrameEarly(VSNode *node, int n, VSFrameContext *frameCtx) nogil
        void cacheFrame(const VSFrame *frame, int n, VSFrameContext *frameCtx) nogil
        void setFilterError(const char *errorMessage, VSFrameContext *frameCtx) nogil

        # External functions
        VSFunction *createFunction(VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core) nogil
        void freeFunction(VSFunction *f) nogil
        VSFunction *addFunctionRef(VSFunction *f) nogil
        void callFunction(VSFunction *func, const VSMap *inm, VSMap *outm) nogil

        # Map and property access
        VSMap *createMap() nogil
        void freeMap(VSMap *map) nogil
        void clearMap(VSMap *map) nogil
        void copyMap(const VSMap *src, VSMap *dst) nogil

        void mapSetError(VSMap *map, const char *errorMessage) nogil
        char *mapGetError(const VSMap *map) nogil

        int mapNumKeys(const VSMap *map) nogil
        const char *mapGetKey(const VSMap *map, int index) nogil
        bint mapDeleteKey(VSMap *map, const char *key) nogil
        int mapNumElements(const VSMap *map, const char *key) nogil
        int mapGetType(const VSMap *map, const char *key) nogil
        int mapSetEmpty(VSMap *map, const char *key, int type) nogil

        int64_t mapGetInt(const VSMap *map, const char *key, int index, int *error) nogil
        int mapGetIntSaturated(const VSMap *map, const char *key, int index, int *error) nogil
        const int64_t *mapGetIntArray(const VSMap *map, const char *key, int *error) nogil
        bint mapSetInt(VSMap *map, const char *key, int64_t i, int append) nogil
        bint mapSetIntArray(VSMap *map, const char *key, const int64_t *i, int size) nogil

        double mapGetFloat(const VSMap *map, const char *key, int index, int *error) nogil
        float mapGetFloatSaturated(const VSMap *map, const char *key, int index, int *error) nogil
        const double *mapGetFloatArray(const VSMap *map, const char *key, int *error) nogil
        bint mapSetFloat(VSMap *map, const char *key, double d, int append) nogil
        bint mapSetFloatArray(VSMap *map, const char *key, const double *d, int size) nogil

        const char *mapGetData(const VSMap *map, const char *key, int index, int *error) nogil
        int mapGetDataSize(const VSMap *map, const char *key, int index, int *error) nogil
        int mapGetDataTypeHint(const VSMap *map, const char *key, int index, int *error) nogil
        bint mapSetData(VSMap *map, const char *key, const char *data, int size, int type, int append) nogil

        VSNode *mapGetNode(const VSMap *map, const char *key, int index, int *error) nogil
        bint mapSetNode(VSMap *map, const char *key, VSNode *node, int append) nogil
        bint mapConsumeNode(VSMap *map, const char *key, VSNode *node, int append) nogil

        const VSFrame *mapGetFrame(const VSMap *map, const char *key, int index, int *error) nogil
        bint mapSetFrame(VSMap *map, const char *key, const VSFrame *f, int append) nogil
        bint mapConsumeFrame(VSMap *map, const char *key, const VSFrame *f, int append) nogil

        VSFunction *mapGetFunction(const VSMap *map, const char *key, int index, int *error) nogil
        bint mapSetFunction(VSMap *map, const char *key, VSFunction *func, int append) nogil
        bint mapConsumeFunction(VSMap *map, const char *key, VSFunction *func, int append) nogil

        # Plugin and function related
        bint registerFunction(const char *name, const char *args, const char *returnType, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) nogil
        VSPlugin *getPluginByID(const char *identifier, VSCore *core) nogil
        VSPlugin *getPluginByNamespace(const char *ns, VSCore *core) nogil
        VSPlugin *getNextPlugin(VSPlugin *plugin, VSCore *core) nogil
        const char *getPluginName(VSPlugin *plugin) nogil
        const char *getPluginID(VSPlugin *plugin) nogil
        const char *getPluginNamespace(VSPlugin *plugin) nogil
        VSPluginFunction *getNextPluginFunction(VSPluginFunction *func, VSPlugin *plugin) nogil
        VSPluginFunction *getPluginFunctionByName(const char *name, VSPlugin *plugin) nogil
        const char *getPluginFunctionName(VSPluginFunction *func) nogil
        const char *getPluginFunctionArguments(VSPluginFunction *func) nogil
        const char *getPluginFunctionReturnType(VSPluginFunction *func) nogil
        const char *getPluginPath(const VSPlugin *plugin) nogil
        int *getPluginVersion(const VSPlugin *plugin) nogil;
        VSMap *invoke(VSPlugin *plugin, const char *name, const VSMap *args) nogil

        # Core and information
        VSCore *createCore(int flags) nogil
        void freeCore(VSCore *core) nogil
        int64_t setMaxCacheSize(int64_t bytes, VSCore *core) nogil
        int setThreadCount(int threads, VSCore *core) nogil
        void getCoreInfo(VSCore *core, VSCoreInfo *info) nogil
        int getAPIVersion() nogil

        # Message handler
        void logMessage(int msgType, const char *msg, VSCore *core) nogil
        VSLogHandle *addLogHandler(VSLogHandler handler, VSLogHandlerFree free, void *userData, VSCore *core) nogil
        bint removeLogHandler(VSLogHandle *handle, VSCore *core) nogil

        # Additional cache management to free memory */
        void clearNodeCache(VSNode *node) nogil
        void clearCoreCaches(VSCore *core) nogil

        # Basic node information
        const char *getNodeName(VSNode *node) nogil
        int getNodeFilterMode(VSNode *node) nogil
        int getNumNodeDependencies(VSNode *node) nogil
        const VSFilterDependency *getNodeDependency(VSNode *node, int index) nogil

        # Node timing functions
        int getCoreNodeTiming(VSCore *core) nogil
        void setCoreNodeTiming(VSCore *core, int enable) nogil
        int64_t getNodeProcessingTime(VSNode *node, int reset) nogil
        int64_t getFreedNodeProcessingTime(VSCore *core, int reset) nogil

        # Unstable API, has no set place.
        const char *getNodeCreationFunctionName(VSNode *node, int level) nogil
        const VSMap *getNodeCreationFunctionArguments(VSNode *node, int level) nogil

    const VSAPI *getVapourSynthAPI(int version) nogil
