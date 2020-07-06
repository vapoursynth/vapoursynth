/*
* Copyright (c) 2012-2020 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef VAPOURSYNTH4_H
#define VAPOURSYNTH4_H

#include <stdint.h>

#define VAPOURSYNTH_API_MAJOR 4
#define VAPOURSYNTH_API_MINOR 9000
#define VAPOURSYNTH_API_VERSION ((VAPOURSYNTH_API_MAJOR << 16) | (VAPOURSYNTH_API_MINOR))

/* Convenience for C++ users. */
#ifdef __cplusplus
#    define VS_EXTERN_C extern "C"
#    if __cplusplus >= 201103L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201103L)
#        define VS_NOEXCEPT noexcept
#    else
#        define VS_NOEXCEPT
#    endif
#else
#    define VS_EXTERN_C
#    define VS_NOEXCEPT
#endif

#if defined(_WIN32) && !defined(_WIN64)
#    define VS_CC __stdcall
#else
#    define VS_CC
#endif

/* And now for some symbol hide-and-seek... */
#if defined(_WIN32) /* Windows being special */
#    define VS_EXTERNAL_API(ret) VS_EXTERN_C __declspec(dllexport) ret VS_CC
#elif defined(__GNUC__) && __GNUC__ >= 4
#    define VS_EXTERNAL_API(ret) VS_EXTERN_C __attribute__((visibility("default"))) ret VS_CC
#else
#    define VS_EXTERNAL_API(ret) VS_EXTERN_C ret VS_CC
#endif

#if !defined(VS_CORE_EXPORTS) && defined(_WIN32)
#    define VS_API(ret) VS_EXTERN_C __declspec(dllimport) ret VS_CC
#else
#    define VS_API(ret) VS_EXTERNAL_API(ret)
#endif

typedef struct VSFrameRef VSFrameRef;
typedef struct VSNodeRef VSNodeRef;
typedef struct VSCore VSCore;
typedef struct VSPlugin VSPlugin;
typedef struct VSFuncRef VSFuncRef;
typedef struct VSMap VSMap;
typedef struct VSAPI VSAPI;
typedef struct VSFrameContext VSFrameContext;

#define VS_AUDIO_FRAME_SAMPLES 3000

typedef enum VSColorFamily {
    cfUndefined = 0,
    cfGray      = 1,
    cfRGB       = 2,
    cfYUV       = 3,
    cfYCoCg     = 4, // ?? keep this? mostly harmless and unused
    /* special for compatibility, can't be used in normal filters */
    cfCompatYUY2 = 14,
    cfCompatBGR32 = 15
} VSColorFamily;

typedef enum VSSampleType {
    stInteger = 0,
    stFloat = 1
} VSSampleType;

#define VS_MAKE_VIDEO_ID(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH) ((colorFamily << 28) | (sampleType << 24) | (bitsPerSample << 16) | (subSamplingW << 8) | (subSamplingH << 0))

typedef enum VSPresetFormat {
    pfNone = 0,

    pfGray8 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 8, 0, 0),
    pfGray16 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 16, 0, 0),

    pfGrayH = VS_MAKE_VIDEO_ID(cfGray, stFloat, 16, 0, 0),
    pfGrayS = VS_MAKE_VIDEO_ID(cfGray, stFloat, 32, 0, 0),

    pfYUV410P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 2, 2),
    pfYUV411P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 2, 0),
    pfYUV440P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 0, 1),

    pfYUV420P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 1, 1),
    pfYUV422P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 1, 0),
    pfYUV444P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 0, 0),

    pfYUV420P10 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 10, 1, 1),
    pfYUV422P10 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 10, 1, 0),
    pfYUV444P10 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 10, 0, 0),

    pfYUV420P12 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 12, 1, 1),
    pfYUV422P12 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 12, 1, 0),
    pfYUV444P12 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 12, 0, 0),

    pfYUV420P14 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 14, 1, 1),
    pfYUV422P14 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 14, 1, 0),
    pfYUV444P14 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 14, 0, 0),

    pfYUV420P16 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 16, 1, 1),
    pfYUV422P16 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 16, 1, 0),
    pfYUV444P16 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 16, 0, 0),

    pfYUV444PH = VS_MAKE_VIDEO_ID(cfYUV, stFloat, 16, 0, 0),
    pfYUV444PS = VS_MAKE_VIDEO_ID(cfYUV, stFloat, 32, 0, 0),

    pfRGB24 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 8, 0, 0),
    pfRGB30 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 10, 0, 0),
    pfRGB36 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 12, 0, 0),
    pfRGB42 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 14, 0, 0),
    pfRGB48 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 16, 0, 0),

    pfRGBH = VS_MAKE_VIDEO_ID(cfRGB, stFloat, 16, 0, 0),
    pfRGBS = VS_MAKE_VIDEO_ID(cfRGB, stFloat, 32, 0, 0),

    /* special for compatibility, can only be used by some core filters */
    pfCompatBGR32 = VS_MAKE_VIDEO_ID(cfCompatBGR32, stInteger, 32, 0, 0),
    pfCompatYUY2 = VS_MAKE_VIDEO_ID(cfCompatYUY2, stInteger, 16, 1, 0)
} VSPresetFormat;

#undef VS_MAKE_VIDEO_ID


typedef enum VSFilterMode {
    fmParallel = 0, /* completely parallel execution */
    fmParallelRequests = 1, /* for filters that are serial in nature but can request one or more frames they need in advance */
    fmUnordered = 2, /* for filters that modify their internal state every request */
    fmSerial = 3 /* for source filters and compatibility with other filtering architectures */
} VSFilterMode;

typedef enum VSMediaType {
    mtVideo = 1,
    mtAudio = 2
} VSMediaType;

typedef struct VSVideoFormat {
    int colorFamily; /* see VSColorFamily */
    int sampleType; /* see VSSampleType */
    int bitsPerSample; /* number of significant bits */
    int bytesPerSample; /* actual storage is always in a power of 2 and the smallest possible that can fit the number of bits used per sample */

    int subSamplingW; /* log2 subsampling factor, applied to second and third plane */
    int subSamplingH;

    int numPlanes; /* implicit from colorFamily */
} VSVideoFormat;

typedef enum VSAudioChannels {
    acFrontLeft           = 0,
    acFrontRight          = 1,
    acFrontCenter         = 2,
    acLowFrequency        = 3,
    acBackLeft            = 4,
    acBackRight           = 5,
    acFrontLeftOFCenter   = 6,
    acFrontRightOFCenter  = 7,
    acBackCenter          = 8,
    acSideLeft            = 9,
    acSideRight           = 10,
    acTopCenter           = 11,
    acTopFrontLeft        = 12,
    acTopFrontCenter      = 13,
    acTopFrontRight       = 14,
    acTopBackLeft         = 15,
    acTopBackCenter       = 16,
    acTopBackRight        = 17,
    acStereoLeft          = 29,
    acStereoRight         = 30,
    acWideLeft            = 31,
    acWideRight           = 32,
    acSurroundDirectLeft  = 33,
    acSurroundDirectRight = 34,
    acLowFrequency2       = 35
} VSAudioChannels;

typedef struct VSAudioFormat {
    int sampleType;
    int bitsPerSample;
    int bytesPerSample; /* implicit from bitsPerSample */
    int numChannels; /* implicit from channelLayout */
    uint64_t channelLayout;
} VSAudioFormat;

// FIXME, investigate nfMakeLinear and its usefulness or convert it into a filter mode
typedef enum VSNodeFlags {
    nfNoCache    = 1,
    nfIsCache    = 2,
    nfMakeLinear = 4
} VSNodeFlags;

typedef enum VSPropTypes {
    ptUnset = 0,
    ptInt = 1,
    ptFloat = 2,
    ptData = 3,
    ptFunction = 4,
    ptVideoNode = 5,
    ptAudioNode = 6,
    ptVideoFrame = 7,
    ptAudioFrame = 8,
} VSPropTypes;

typedef enum VSGetPropErrors {
    peUnset = 1,
    peType  = 2,
    peIndex = 4
} VSGetPropErrors;

typedef enum VSPropAppendMode {
    paReplace = 0,
    paAppend  = 1
} VSPropAppendMode;

typedef struct VSCoreInfo {
    const char *versionString;
    int core;
    int api;
    int numThreads;
    int64_t maxFramebufferSize;
    int64_t usedFramebufferSize;
} VSCoreInfo;

typedef struct VSVideoInfo {
    VSVideoFormat format;
    int64_t fpsNum;
    int64_t fpsDen;
    int width;
    int height;
    int numFrames;
} VSVideoInfo;

typedef struct VSAudioInfo {
    VSAudioFormat format;
    int sampleRate;
    int64_t numSamples;
    int numFrames; /* implicit from numSamples */ // FIXME, where and how does this get set? does the user set it?
} VSAudioInfo;

typedef enum VSActivationReason {
    arInitial = 0,
    arFrameReady = 1,
    arAllFramesReady = 2,
    arError = -1
} VSActivationReason;

typedef enum VSMessageType {
    mtDebug = 0,
    mtWarning = 1,
    mtCritical = 2,
    mtFatal = 3
} VSMessageType;

typedef enum VSCoreFlags {
    cfDisableAutoLoading = 1
} VSCoreFlags;

/* core entry point */
typedef const VSAPI *(VS_CC *VSGetVapourSynthAPI)(int version);

/* plugin function and filter typedefs */
typedef void (VS_CC *VSPublicFunction)(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSRegisterFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);
typedef void (VS_CC *VSConfigPlugin)(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readonly, VSPlugin *plugin);
typedef void (VS_CC *VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
typedef void (VS_CC *VSFreeFuncData)(void *userData);
typedef const VSFrameRef *(VS_CC *VSFilterGetFrame)(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi);

/* other */
typedef void (VS_CC *VSFrameDoneCallback)(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg);
typedef void (VS_CC *VSMessageHandler)(int msgType, const char *msg, void *userData);
typedef void (VS_CC *VSMessageHandlerFree)(void *userData);

struct VSAPI {
    VSCore *(VS_CC *createCore)(int threads, int flags) VS_NOEXCEPT;
    void (VS_CC *freeCore)(VSCore *core) VS_NOEXCEPT;

    const VSFrameRef *(VS_CC *cloneFrameRef)(const VSFrameRef *f) VS_NOEXCEPT;
    VSNodeRef *(VS_CC *cloneNodeRef)(VSNodeRef *node) VS_NOEXCEPT;
    VSFuncRef *(VS_CC *cloneFuncRef)(VSFuncRef *f) VS_NOEXCEPT;

    void (VS_CC *freeFrame)(const VSFrameRef *f) VS_NOEXCEPT;
    void (VS_CC *freeNode)(VSNodeRef *node) VS_NOEXCEPT;
    void (VS_CC *freeFunc)(VSFuncRef *f) VS_NOEXCEPT;

    VSFrameRef *(VS_CC *newVideoFrame)(const VSVideoFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT;
    VSFrameRef *(VS_CC *copyFrame)(const VSFrameRef *f, VSCore *core) VS_NOEXCEPT;
    void (VS_CC *copyFrameProps)(const VSFrameRef *src, VSFrameRef *dst, VSCore *core) VS_NOEXCEPT;

    void (VS_CC *registerFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT;
    VSPlugin *(VS_CC *getPluginById)(const char *identifier, VSCore *core) VS_NOEXCEPT;
    VSPlugin *(VS_CC *getPluginByNs)(const char *ns, VSCore *core) VS_NOEXCEPT;
    VSMap *(VS_CC *getPlugins)(VSCore *core) VS_NOEXCEPT;
    VSMap *(VS_CC *getFunctions)(VSPlugin *plugin) VS_NOEXCEPT; //// FIXME, replace with a function to get the number of functions and a VSPluginFunction struct/opaque object, 
    void (VS_CC *setError)(VSMap *map, const char *errorMessage) VS_NOEXCEPT; /* use to signal errors outside filter getframe functions */
    const char *(VS_CC *getError)(const VSMap *map) VS_NOEXCEPT; /* use to query errors, returns 0 if no error */
    void (VS_CC *setFilterError)(const char *errorMessage, VSFrameContext *frameCtx) VS_NOEXCEPT; /* use to signal errors in the filter getframe function */
    VSMap *(VS_CC *invoke)(VSPlugin *plugin, const char *name, const VSMap *args) VS_NOEXCEPT;

    const VSFrameRef *(VS_CC *getFrame)(int n, VSNodeRef *node, char *errorMsg, int bufSize) VS_NOEXCEPT; /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
    void (VS_CC *getFrameAsync)(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData) VS_NOEXCEPT; /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
    const VSFrameRef *(VS_CC *getFrameFilter)(int n, VSNodeRef *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
    void (VS_CC *requestFrameFilter)(int n, VSNodeRef *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
    void (VS_CC *queryCompletedFrame)(VSNodeRef **node, int *n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
    void (VS_CC *releaseFrameEarly)(VSNodeRef *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */

    ptrdiff_t (VS_CC *getStride)(const VSFrameRef *f, int plane) VS_NOEXCEPT;
    const uint8_t *(VS_CC *getReadPtr)(const VSFrameRef *f, int plane) VS_NOEXCEPT;
    uint8_t *(VS_CC *getWritePtr)(VSFrameRef *f, int plane) VS_NOEXCEPT;

    VSFuncRef *(VS_CC *createFunc)(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core) VS_NOEXCEPT;
    void (VS_CC *callFunc)(VSFuncRef *func, const VSMap *in, VSMap *out) VS_NOEXCEPT;

    /* property access functions */
    VSMap *(VS_CC *createMap)(void) VS_NOEXCEPT;
    void (VS_CC *freeMap)(VSMap *map) VS_NOEXCEPT;
    void (VS_CC *clearMap)(VSMap *map) VS_NOEXCEPT;

    const VSVideoInfo *(VS_CC *getVideoInfo)(VSNodeRef *node) VS_NOEXCEPT;
    const VSVideoFormat *(VS_CC *getVideoFrameFormat)(const VSFrameRef *f) VS_NOEXCEPT;
    int (VS_CC *getFrameWidth)(const VSFrameRef *f, int plane) VS_NOEXCEPT;
    int (VS_CC *getFrameHeight)(const VSFrameRef *f, int plane) VS_NOEXCEPT;
    const VSMap *(VS_CC *getFramePropsRO)(const VSFrameRef *f) VS_NOEXCEPT;
    VSMap *(VS_CC *getFramePropsRW)(VSFrameRef *f) VS_NOEXCEPT;

    int (VS_CC *propNumKeys)(const VSMap *map) VS_NOEXCEPT;
    const char *(VS_CC *propGetKey)(const VSMap *map, int index) VS_NOEXCEPT;
    int (VS_CC *propNumElements)(const VSMap *map, const char *key) VS_NOEXCEPT;
    int (VS_CC *propGetType)(const VSMap *map, const char *key) VS_NOEXCEPT;

    int64_t(VS_CC *propGetInt)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    double(VS_CC *propGetFloat)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    const char *(VS_CC *propGetData)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *propGetDataSize)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    VSNodeRef *(VS_CC *propGetNode)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    const VSFrameRef *(VS_CC *propGetFrame)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    VSFuncRef *(VS_CC *propGetFunc)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;

    int (VS_CC *propDeleteKey)(VSMap *map, const char *key) VS_NOEXCEPT;
    int (VS_CC *propSetInt)(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT;
    int (VS_CC *propSetFloat)(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT;
    int (VS_CC *propSetData)(VSMap *map, const char *key, const char *data, int size, int append) VS_NOEXCEPT;
    int (VS_CC *propSetNode)(VSMap *map, const char *key, VSNodeRef *node, int append) VS_NOEXCEPT;
    int (VS_CC *propSetFrame)(VSMap *map, const char *key, const VSFrameRef *f, int append) VS_NOEXCEPT;
    int (VS_CC *propSetFunc)(VSMap *map, const char *key, VSFuncRef *func, int append) VS_NOEXCEPT;

    int64_t (VS_CC *setMaxCacheSize)(int64_t bytes, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *getOutputIndex)(VSFrameContext *frameCtx) VS_NOEXCEPT;
    VSFrameRef *(VS_CC *newVideoFrame2)(const VSVideoFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *setThreadCount)(int threads, VSCore *core) VS_NOEXCEPT;

    const char *(VS_CC *getPluginPath)(const VSPlugin *plugin) VS_NOEXCEPT;

    const int64_t *(VS_CC *propGetIntArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;
    const double *(VS_CC *propGetFloatArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;

    int (VS_CC *propSetIntArray)(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT;
    int (VS_CC *propSetFloatArray)(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT;

    void (VS_CC *logMessage)(int msgType, const char *msg) VS_NOEXCEPT;
    int (VS_CC *addMessageHandler)(VSMessageHandler handler, VSMessageHandlerFree free, void *userData) VS_NOEXCEPT;
    int (VS_CC *removeMessageHandler)(int id) VS_NOEXCEPT;
    void (VS_CC *getCoreInfo)(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT;

    int (VS_CC *propSetEmpty)(VSMap *map, const char *key, int type) VS_NOEXCEPT;
    void (VS_CC *createVideoFilter)(VSMap *out, const char *name, const VSVideoInfo *vi, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT;
    void (VS_CC *createAudioFilter)(VSMap *out, const char *name, const VSAudioInfo *ai, int numOutputs, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT;
    VSFrameRef *(VS_CC *newAudioFrame)(const VSAudioFormat *format, int numSamples, const VSFrameRef *propSrc, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *queryAudioFormat)(VSAudioFormat *format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *queryVideoFormat)(VSVideoFormat *format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT;
    uint32_t (VS_CC *queryVideoFormatID)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT;
    int (VS_CC *queryVideoFormatByID)(VSVideoFormat *format, uint32_t id, VSCore *core) VS_NOEXCEPT;
    // fixme, return success or failure in get*FormatName?
    void (VS_CC *getAudioFormatName)(const VSAudioFormat *format, char *buffer) VS_NOEXCEPT; /* up to 32 characters including terminating null may be written to the buffer */
    void (VS_CC *getVideoFormatName)(const VSVideoFormat *format, char *buffer) VS_NOEXCEPT; /* up to 32 characters including terminating null may be written to the buffer */
    const VSAudioInfo *(VS_CC *getAudioInfo)(VSNodeRef *node) VS_NOEXCEPT;
    const VSAudioFormat *(VS_CC *getAudioFrameFormat)(const VSFrameRef *f) VS_NOEXCEPT;

    int (VS_CC *getNodeType)(VSNodeRef *node) VS_NOEXCEPT;
    int (VS_CC *getNodeFlags)(VSNodeRef *node) VS_NOEXCEPT;
    int (VS_CC *getFrameType)(const VSFrameRef *f) VS_NOEXCEPT;
    int (VS_CC *getFrameLength)(const VSFrameRef *f) VS_NOEXCEPT;
    int (VS_CC *getApiVersion)(void) VS_NOEXCEPT;
};

VS_API(const VSAPI *) getVapourSynthAPI(int version) VS_NOEXCEPT;

#endif /* VAPOURSYNTH4_H */
