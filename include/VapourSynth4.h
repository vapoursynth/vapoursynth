/*
* Copyright (c) 2012-2021 Fredrik Mellbin
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
#include <stddef.h>

#define VS_MAKE_VERSION(major, minor) (((major) << 16) | (minor))
#define VAPOURSYNTH_API_MAJOR 4
#define VAPOURSYNTH_API_MINOR 0
#define VAPOURSYNTH_API_VERSION VS_MAKE_VERSION(VAPOURSYNTH_API_MAJOR, VAPOURSYNTH_API_MINOR)

#define VS_AUDIO_FRAME_SAMPLES 3072

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

typedef struct VSFrame VSFrame;
typedef struct VSNode VSNode;
typedef struct VSCore VSCore;
typedef struct VSPlugin VSPlugin;
typedef struct VSPluginFunction VSPluginFunction;
typedef struct VSFunction VSFunction;
typedef struct VSMap VSMap;
typedef struct VSLogHandle VSLogHandle;
typedef struct VSFrameContext VSFrameContext;
typedef struct VSPLUGINAPI VSPLUGINAPI;
typedef struct VSAPI VSAPI;

typedef enum VSColorFamily {
    cfUndefined = 0,
    cfGray      = 1,
    cfRGB       = 2,
    cfYUV       = 3
} VSColorFamily;

typedef enum VSSampleType {
    stInteger = 0,
    stFloat = 1
} VSSampleType;

#define VS_MAKE_VIDEO_ID(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH) ((colorFamily << 28) | (sampleType << 24) | (bitsPerSample << 16) | (subSamplingW << 8) | (subSamplingH << 0))

typedef enum VSPresetFormat {
    pfNone = 0,

    pfGray8 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 8, 0, 0),
    pfGray9 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 9, 0, 0),
    pfGray10 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 10, 0, 0),
    pfGray12 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 12, 0, 0),
    pfGray14 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 14, 0, 0),
    pfGray16 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 16, 0, 0),
    pfGray32 = VS_MAKE_VIDEO_ID(cfGray, stInteger, 32, 0, 0),

    pfGrayH = VS_MAKE_VIDEO_ID(cfGray, stFloat, 16, 0, 0),
    pfGrayS = VS_MAKE_VIDEO_ID(cfGray, stFloat, 32, 0, 0),

    pfYUV410P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 2, 2),
    pfYUV411P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 2, 0),
    pfYUV440P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 0, 1),

    pfYUV420P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 1, 1),
    pfYUV422P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 1, 0),
    pfYUV444P8 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 8, 0, 0),

    pfYUV420P9 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 9, 1, 1),
    pfYUV422P9 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 9, 1, 0),
    pfYUV444P9 = VS_MAKE_VIDEO_ID(cfYUV, stInteger, 9, 0, 0),

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
    pfRGB27 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 9, 0, 0),
    pfRGB30 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 10, 0, 0),
    pfRGB36 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 12, 0, 0),
    pfRGB42 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 14, 0, 0),
    pfRGB48 = VS_MAKE_VIDEO_ID(cfRGB, stInteger, 16, 0, 0),

    pfRGBH = VS_MAKE_VIDEO_ID(cfRGB, stFloat, 16, 0, 0),
    pfRGBS = VS_MAKE_VIDEO_ID(cfRGB, stFloat, 32, 0, 0),
} VSPresetFormat;

#undef VS_MAKE_VIDEO_ID


typedef enum VSFilterMode {
    fmParallel = 0, /* completely parallel execution */
    fmParallelRequests = 1, /* for filters that are serial in nature but can request one or more frames they need in advance */
    fmUnordered = 2, /* for filters that modify their internal state every request like source filters that read a file */
    fmFrameState = 3 /* DO NOT USE UNLESS ABSOLUTELY NECESSARY, for compatibility with external code that can only keep the processing state of a single frame at a time */
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
    int subSamplingH; /* log2 subsampling factor, applied to second and third plane */

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

typedef enum VSPropertyType {
    ptUnset = 0,
    ptInt = 1,
    ptFloat = 2,
    ptData = 3,
    ptFunction = 4,
    ptVideoNode = 5,
    ptAudioNode = 6,
    ptVideoFrame = 7,
    ptAudioFrame = 8
} VSPropertyType;

typedef enum VSMapPropertyError {
    peSuccess = 0,
    peUnset   = 1, /* no key exists */
    peType    = 2, /* key exists but not of a compatible type */
    peIndex   = 4, /* index out of bounds */
    peError   = 3  /* map has error state set */
} VSMapPropertyError;

typedef enum VSMapAppendMode {
    maReplace = 0,
    maAppend  = 1
} VSMapAppendMode;

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
    int numFrames; /* the total number of audio frames needed to hold numSamples, implicit from numSamples when calling createAudioFilter */
} VSAudioInfo;

typedef enum VSActivationReason {
    arInitial = 0,
    arAllFramesReady = 1,
    arError = -1
} VSActivationReason;

typedef enum VSMessageType {
    mtDebug = 0,
    mtInformation = 1, 
    mtWarning = 2,
    mtCritical = 3,
    mtFatal = 4 /* also terminates the process, should generally not be used by normal filters */
} VSMessageType;

typedef enum VSCoreCreationFlags {
    ccfEnableGraphInspection = 1,
    ccfDisableAutoLoading = 2,
    ccfDisableLibraryUnloading = 4
} VSCoreCreationFlags;

typedef enum VSPluginConfigFlags {
    pcModifiable = 1
} VSPluginConfigFlags;

typedef enum VSDataTypeHint {
    dtUnknown = -1,
    dtBinary = 0,
    dtUtf8 = 1
} VSDataTypeHint;

typedef enum VSRequestPattern {
    rpGeneral = 0, /* General pattern */
    rpNoFrameReuse = 1, /* When requesting all output frames from the filter no frame will be requested more than once from this input clip, never requests frames beyond the end of the clip */
    rpStrictSpatial = 2 /* Always (and only) requests frame n from input clip when generating output frame n, never requests frames beyond the end of the clip */
} VSRequestPattern;

/* Core entry point */
typedef const VSAPI *(VS_CC *VSGetVapourSynthAPI)(int version);

/* Plugin, function and filter related */
typedef void (VS_CC *VSPublicFunction)(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSInitPlugin)(VSPlugin *plugin, const VSPLUGINAPI *vspapi);
typedef void (VS_CC *VSFreeFunctionData)(void *userData);
typedef const VSFrame *(VS_CC *VSFilterGetFrame)(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi);

/* Other */
typedef void (VS_CC *VSFrameDoneCallback)(void *userData, const VSFrame *f, int n, VSNode *node, const char *errorMsg);
typedef void (VS_CC *VSLogHandler)(int msgType, const char *msg, void *userData);
typedef void (VS_CC *VSLogHandlerFree)(void *userData);

struct VSPLUGINAPI {
    int (VS_CC *getAPIVersion)(void) VS_NOEXCEPT; /* returns VAPOURSYNTH_API_VERSION of the library */
    int (VS_CC *configPlugin)(const char *identifier, const char *pluginNamespace, const char *name, int pluginVersion, int apiVersion, int flags, VSPlugin *plugin) VS_NOEXCEPT; /* use the VS_MAKE_VERSION macro for pluginVersion */
    int (VS_CC *registerFunction)(const char *name, const char *args, const char *returnType, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT; /* non-zero return value on success  */
};

typedef struct VSFilterDependency {
    VSNode *source;
    int requestPattern; /* VSRequestPattern */
} VSFilterDependency;

struct VSAPI {
    /* Audio and video filter related including nodes */
    void (VS_CC *createVideoFilter)(VSMap *out, const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT; /* output nodes are appended to the clip key in the out map */
    VSNode *(VS_CC *createVideoFilter2)(const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT; /* same as createVideoFilter but returns a pointer to the VSNode directly or NULL on failure */
    void (VS_CC *createAudioFilter)(VSMap *out, const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT; /* output nodes are appended to the clip key in the out map */
    VSNode *(VS_CC *createAudioFilter2)(const char *name, const VSAudioInfo *ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency *dependencies, int numDeps, void *instanceData, VSCore *core) VS_NOEXCEPT; /* same as createAudioFilter but returns a pointer to the VSNode directly or NULL on failure */
    int (VS_CC *setLinearFilter)(VSNode *node) VS_NOEXCEPT; /* Use right after create*Filter*, sets the correct cache mode for using the cacheFrame API and returns the recommended upper number of additional frames to cache per request */
    void (VS_CC *setCacheMode)(VSNode *node, int mode) VS_NOEXCEPT; /* -1: default (auto), 0: force disable, 1: force enable, changing the cache mode also resets all options to their default */
    void (VS_CC *setCacheOptions)(VSNode *node, int fixedSize, int maxSize, int maxHistorySize) VS_NOEXCEPT; /* passing -1 means no change */

    void (VS_CC *freeNode)(VSNode *node) VS_NOEXCEPT;
    VSNode *(VS_CC *addNodeRef)(VSNode *node) VS_NOEXCEPT;
    int (VS_CC *getNodeType)(VSNode *node) VS_NOEXCEPT; /* returns VSMediaType */
    const VSVideoInfo *(VS_CC *getVideoInfo)(VSNode *node) VS_NOEXCEPT;
    const VSAudioInfo *(VS_CC *getAudioInfo)(VSNode *node) VS_NOEXCEPT;

    /* Frame related functions */
    VSFrame *(VS_CC *newVideoFrame)(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
    VSFrame *(VS_CC *newVideoFrame2)(const VSVideoFormat *format, int width, int height, const VSFrame **planeSrc, const int *planes, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT; /* same as newVideoFrame but allows the specified planes to be effectively copied from the source frames */
    VSFrame *(VS_CC *newAudioFrame)(const VSAudioFormat *format, int numSamples, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
    VSFrame *(VS_CC *newAudioFrame2)(const VSAudioFormat *format, int numSamples, const VSFrame **channelSrc, const int *channels, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT; /* same as newAudioFrame but allows the specified channels to be effectively copied from the source frames */
    void (VS_CC *freeFrame)(const VSFrame *f) VS_NOEXCEPT;
    const VSFrame *(VS_CC *addFrameRef)(const VSFrame *f) VS_NOEXCEPT;
    VSFrame *(VS_CC *copyFrame)(const VSFrame *f, VSCore *core) VS_NOEXCEPT;
    const VSMap *(VS_CC *getFramePropertiesRO)(const VSFrame *f) VS_NOEXCEPT;
    VSMap *(VS_CC *getFramePropertiesRW)(VSFrame *f) VS_NOEXCEPT;

    ptrdiff_t (VS_CC *getStride)(const VSFrame *f, int plane) VS_NOEXCEPT;
    const uint8_t *(VS_CC *getReadPtr)(const VSFrame *f, int plane) VS_NOEXCEPT;
    uint8_t *(VS_CC *getWritePtr)(VSFrame *f, int plane) VS_NOEXCEPT; /* calling this function invalidates previously gotten read pointers to the same frame */

    const VSVideoFormat *(VS_CC *getVideoFrameFormat)(const VSFrame *f) VS_NOEXCEPT;
    const VSAudioFormat *(VS_CC *getAudioFrameFormat)(const VSFrame *f) VS_NOEXCEPT;
    int (VS_CC *getFrameType)(const VSFrame *f) VS_NOEXCEPT; /* returns VSMediaType */
    int (VS_CC *getFrameWidth)(const VSFrame *f, int plane) VS_NOEXCEPT;
    int (VS_CC *getFrameHeight)(const VSFrame *f, int plane) VS_NOEXCEPT;
    int (VS_CC *getFrameLength)(const VSFrame *f) VS_NOEXCEPT; /* returns the number of samples for audio frames */

    /* General format functions  */
    int (VS_CC *getVideoFormatName)(const VSVideoFormat *format, char *buffer) VS_NOEXCEPT; /* up to 32 characters including terminating null may be written to the buffer, non-zero return value on success */
    int (VS_CC *getAudioFormatName)(const VSAudioFormat *format, char *buffer) VS_NOEXCEPT; /* up to 32 characters including terminating null may be written to the buffer, non-zero return value on success */
    int (VS_CC *queryVideoFormat)(VSVideoFormat *format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT; /* non-zero return value on success */
    int (VS_CC *queryAudioFormat)(VSAudioFormat *format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore *core) VS_NOEXCEPT; /* non-zero return value on success */
    uint32_t (VS_CC *queryVideoFormatID)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT; /* returns 0 on failure */
    int (VS_CC *getVideoFormatByID)(VSVideoFormat *format, uint32_t id, VSCore *core) VS_NOEXCEPT; /* non-zero return value on success */

    /* Frame request and filter getframe functions */
    const VSFrame *(VS_CC *getFrame)(int n, VSNode *node, char *errorMsg, int bufSize) VS_NOEXCEPT; /* only for external applications using the core as a library or for requesting frames in a filter constructor, do not use inside a filter's getframe function */
    void (VS_CC *getFrameAsync)(int n, VSNode *node, VSFrameDoneCallback callback, void *userData) VS_NOEXCEPT; /* only for external applications using the core as a library or for requesting frames in a filter constructor, do not use inside a filter's getframe function */
    const VSFrame *(VS_CC *getFrameFilter)(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
    void (VS_CC *requestFrameFilter)(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
    void (VS_CC *releaseFrameEarly)(VSNode *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function, unless this function is called a requested frame is kept in memory until the end of processing the current frame */
    void (VS_CC *cacheFrame)(const VSFrame *frame, int n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* used to store intermediate frames in cache, useful for filters where random access is slow, must call setLinearFilter on the node before using or the result is undefined  */
    void (VS_CC *setFilterError)(const char *errorMessage, VSFrameContext *frameCtx) VS_NOEXCEPT; /* used to signal errors in the filter getframe function */

    /* External functions */
    VSFunction *(VS_CC *createFunction)(VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core) VS_NOEXCEPT;
    void (VS_CC *freeFunction)(VSFunction *f) VS_NOEXCEPT;
    VSFunction *(VS_CC *addFunctionRef)(VSFunction *f) VS_NOEXCEPT;
    void (VS_CC *callFunction)(VSFunction *func, const VSMap *in, VSMap *out) VS_NOEXCEPT;

    /* Map and property access functions */
    VSMap *(VS_CC *createMap)(void) VS_NOEXCEPT;
    void (VS_CC *freeMap)(VSMap *map) VS_NOEXCEPT;
    void (VS_CC *clearMap)(VSMap *map) VS_NOEXCEPT;
    void (VS_CC *copyMap)(const VSMap *src, VSMap *dst) VS_NOEXCEPT; /* copies all values in src to dst, if a key already exists in dst it's replaced */

    void (VS_CC *mapSetError)(VSMap *map, const char *errorMessage) VS_NOEXCEPT; /* used to signal errors outside filter getframe function */
    const char *(VS_CC *mapGetError)(const VSMap *map) VS_NOEXCEPT; /* used to query errors, returns 0 if no error */

    int (VS_CC *mapNumKeys)(const VSMap *map) VS_NOEXCEPT;
    const char *(VS_CC *mapGetKey)(const VSMap *map, int index) VS_NOEXCEPT;
    int (VS_CC *mapDeleteKey)(VSMap *map, const char *key) VS_NOEXCEPT;
    int (VS_CC *mapNumElements)(const VSMap *map, const char *key) VS_NOEXCEPT; /* returns -1 if a key doesn't exist */
    int (VS_CC *mapGetType)(const VSMap *map, const char *key) VS_NOEXCEPT; /* returns VSPropertyType */
    int (VS_CC *mapSetEmpty)(VSMap *map, const char *key, int type) VS_NOEXCEPT;

    int64_t (VS_CC *mapGetInt)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapGetIntSaturated)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    const int64_t *(VS_CC *mapGetIntArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;
    int (VS_CC *mapSetInt)(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT;
    int (VS_CC *mapSetIntArray)(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT;

    double (VS_CC *mapGetFloat)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    float (VS_CC *mapGetFloatSaturated)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    const double *(VS_CC *mapGetFloatArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;
    int (VS_CC *mapSetFloat)(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT;
    int (VS_CC *mapSetFloatArray)(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT;

    const char *(VS_CC *mapGetData)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapGetDataSize)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapGetDataTypeHint)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT; /* returns VSDataTypeHint */
    int (VS_CC *mapSetData)(VSMap *map, const char *key, const char *data, int size, int type, int append) VS_NOEXCEPT;

    VSNode *(VS_CC *mapGetNode)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapSetNode)(VSMap *map, const char *key, VSNode *node, int append) VS_NOEXCEPT; /* returns 0 on success */
    int (VS_CC *mapConsumeNode)(VSMap *map, const char *key, VSNode *node, int append) VS_NOEXCEPT; /* always consumes the reference, even on error */

    const VSFrame *(VS_CC *mapGetFrame)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapSetFrame)(VSMap *map, const char *key, const VSFrame *f, int append) VS_NOEXCEPT; /* returns 0 on success */
    int (VS_CC *mapConsumeFrame)(VSMap *map, const char *key, const VSFrame *f, int append) VS_NOEXCEPT; /* always consumes the reference, even on error */

    VSFunction *(VS_CC *mapGetFunction)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
    int (VS_CC *mapSetFunction)(VSMap *map, const char *key, VSFunction *func, int append) VS_NOEXCEPT; /* returns 0 on success */
    int (VS_CC *mapConsumeFunction)(VSMap *map, const char *key, VSFunction *func, int append) VS_NOEXCEPT; /* always consumes the reference, even on error */

    /* Plugin and plugin function related */
    int (VS_CC *registerFunction)(const char *name, const char *args, const char *returnType, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT; /* non-zero return value on success  */
    VSPlugin *(VS_CC *getPluginByID)(const char *identifier, VSCore *core) VS_NOEXCEPT;
    VSPlugin *(VS_CC *getPluginByNamespace)(const char *ns, VSCore *core) VS_NOEXCEPT;
    VSPlugin *(VS_CC *getNextPlugin)(VSPlugin *plugin, VSCore *core) VS_NOEXCEPT; /* pass NULL to get the first plugin  */
    const char *(VS_CC *getPluginName)(VSPlugin *plugin) VS_NOEXCEPT;
    const char *(VS_CC *getPluginID)(VSPlugin *plugin) VS_NOEXCEPT;
    const char *(VS_CC *getPluginNamespace)(VSPlugin *plugin) VS_NOEXCEPT;
    VSPluginFunction *(VS_CC *getNextPluginFunction)(VSPluginFunction *func, VSPlugin *plugin) VS_NOEXCEPT; /* pass NULL to get the first plugin function  */
    VSPluginFunction *(VS_CC *getPluginFunctionByName)(const char *name, VSPlugin *plugin) VS_NOEXCEPT;
    const char *(VS_CC *getPluginFunctionName)(VSPluginFunction *func) VS_NOEXCEPT;
    const char *(VS_CC *getPluginFunctionArguments)(VSPluginFunction *func) VS_NOEXCEPT; /* returns an argument format string */
    const char *(VS_CC *getPluginFunctionReturnType)(VSPluginFunction *func) VS_NOEXCEPT; /* returns an argument format string */
    const char *(VS_CC *getPluginPath)(const VSPlugin *plugin) VS_NOEXCEPT; /* the full path to the loaded library file containing the plugin entry point */
    int (VS_CC *getPluginVersion)(const VSPlugin *plugin) VS_NOEXCEPT;
    VSMap *(VS_CC *invoke)(VSPlugin *plugin, const char *name, const VSMap *args) VS_NOEXCEPT; /* user must free the returned VSMap */

    /* Core and information */
    VSCore *(VS_CC *createCore)(int flags) VS_NOEXCEPT; /* flags uses the VSCoreCreationFlags enum */
    void (VS_CC *freeCore)(VSCore *core) VS_NOEXCEPT; /* only call this function after all node, frame and function references belonging to the core have been freed */
    int64_t(VS_CC *setMaxCacheSize)(int64_t bytes, VSCore *core) VS_NOEXCEPT; /* the total cache size at which vapoursynth more aggressively tries to reclaim memory, it is not a hard limit */
    int (VS_CC *setThreadCount)(int threads, VSCore *core) VS_NOEXCEPT; /* setting threads to 0 means automatic detection */
    void (VS_CC *getCoreInfo)(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT;
    int (VS_CC *getAPIVersion)(void) VS_NOEXCEPT;

    /* Message handler */
    void (VS_CC *logMessage)(int msgType, const char *msg, VSCore *core) VS_NOEXCEPT;
    VSLogHandle *(VS_CC *addLogHandler)(VSLogHandler handler, VSLogHandlerFree free, void *userData, VSCore *core) VS_NOEXCEPT; /* free and userData can be NULL, returns a handle that can be passed to removeLogHandler */
    int (VS_CC *removeLogHandler)(VSLogHandle *handle, VSCore *core) VS_NOEXCEPT; /* returns non-zero if successfully removed */
    
#ifdef VS_GRAPH_API
    /* Graph information */

    /* 
     * NOT PART OF THE STABLE API!
     * These functions only exist to retrieve internal details for debug purposes and graph visualization
     * They will only only work properly when used on a core created with ccfEnableGraphInspection and are
     * not safe to use concurrently with frame requests or other API functions
     * NOT PART OF THE STABLE API!
     */
    
    const char *(VS_CC *getNodeCreationFunctionName)(VSNode *node, int level) VS_NOEXCEPT; /* level=0 returns the name of the function that created the filter, specifying a higher level will retrieve the function above that invoked it or NULL if a non-existent level is requested */
    const VSMap *(VS_CC *getNodeCreationFunctionArguments)(VSNode *node, int level) VS_NOEXCEPT; /* level=0 returns a copy of the arguments passed to the function that created the filter, returns NULL if a non-existent level is requested */
    const char *(VS_CC *getNodeName)(VSNode *node) VS_NOEXCEPT; /* the name passed to create*Filter */
    int (VS_CC *getNodeFilterMode)(VSNode *node) VS_NOEXCEPT; /* VSFilterMode */
    int64_t (VS_CC *getNodeFilterTime)(VSNode *node) VS_NOEXCEPT; /* time spent processing frames in nanoseconds */
    const VSFilterDependency *(VS_CC *getNodeDependencies)(VSNode *node) VS_NOEXCEPT;
    int (VS_CC *getNumNodeDependencies)(VSNode *node) VS_NOEXCEPT;
#endif
};

VS_API(const VSAPI *) getVapourSynthAPI(int version) VS_NOEXCEPT;

#endif /* VAPOURSYNTH4_H */
