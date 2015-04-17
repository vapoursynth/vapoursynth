/*
* Copyright (c) 2012-2015 Fredrik Mellbin
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

#ifndef VAPOURSYNTH_H
#define VAPOURSYNTH_H

#include <stdint.h>

#define VAPOURSYNTH_API_MAJOR 3
#define VAPOURSYNTH_API_MINOR 2
#define VAPOURSYNTH_API_VERSION ((VAPOURSYNTH_API_MAJOR << 16) | (VAPOURSYNTH_API_MINOR))

/* Convenience for C++ users. */
#ifdef __cplusplus
#    define VS_EXTERN_C extern "C"
#else
#    define VS_EXTERN_C
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
typedef struct VSNode VSNode;
typedef struct VSFuncRef VSFuncRef;
typedef struct VSMap VSMap;
typedef struct VSAPI VSAPI;
typedef struct VSFrameContext VSFrameContext;

typedef enum VSColorFamily {
    /* all planar formats */
    cmGray   = 1000000,
    cmRGB    = 2000000,
    cmYUV    = 3000000,
    cmYCoCg  = 4000000,
    /* special for compatibility */
    cmCompat = 9000000
} VSColorFamily;

typedef enum VSSampleType {
    stInteger = 0,
    stFloat = 1
} VSSampleType;

/* The +10 is so people won't be using the constants interchangably "by accident" */
typedef enum VSPresetFormat {
    pfNone = 0,

    pfGray8 = cmGray + 10,
    pfGray16,

    pfGrayH,
    pfGrayS,

    pfYUV420P8 = cmYUV + 10,
    pfYUV422P8,
    pfYUV444P8,
    pfYUV410P8,
    pfYUV411P8,
    pfYUV440P8,

    pfYUV420P9,
    pfYUV422P9,
    pfYUV444P9,

    pfYUV420P10,
    pfYUV422P10,
    pfYUV444P10,

    pfYUV420P16,
    pfYUV422P16,
    pfYUV444P16,

    pfYUV444PH,
    pfYUV444PS,

    pfRGB24 = cmRGB + 10,
    pfRGB27,
    pfRGB30,
    pfRGB48,

    pfRGBH,
    pfRGBS,

    /* special for compatibility, if you implement these in any filter I'll personally kill you */
    /* I'll also change their ids around to break your stuff regularly */
    pfCompatBGR32 = cmCompat + 10,
    pfCompatYUY2
} VSPresetFormat;

typedef enum VSFilterMode {
    fmParallel = 100, /* completely parallel execution */
    fmParallelRequests = 200, /* for filters that are serial in nature but can request one or more frames they need in advance */
    fmUnordered = 300, /* for filters that modify their internal state every request */
    fmSerial = 400 /* for source filters and compatibility with other filtering architectures */
} VSFilterMode;

typedef struct VSFormat {
    char name[32];
    int id;
    int colorFamily; /* see VSColorFamily */
    int sampleType; /* see VSSampleType */
    int bitsPerSample; /* number of significant bits */
    int bytesPerSample; /* actual storage is always in a power of 2 and the smallest possible that can fit the number of bits used per sample */

    int subSamplingW; /* log2 subsampling factor, applied to second and third plane */
    int subSamplingH;

    int numPlanes; /* implicit from colorFamily */
} VSFormat;

typedef enum VSNodeFlags {
    nfNoCache = 1,
    nfIsCache = 2
} VSNodeFlags;

typedef enum VSPropTypes {
    ptUnset = 'u',
    ptInt = 'i',
    ptFloat = 'f',
    ptData = 's',
    ptNode = 'c',
    ptFrame = 'v',
    ptFunction = 'm'
} VSPropTypes;

typedef enum VSGetPropErrors {
    peUnset = 1,
    peType  = 2,
    peIndex = 4
} VSGetPropErrors;

typedef enum VSPropAppendMode {
    paReplace = 0,
    paAppend  = 1,
    paTouch   = 2
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
    const VSFormat *format;
    int64_t fpsNum;
    int64_t fpsDen;
    int width;
    int height;
    int numFrames;
    int flags;
} VSVideoInfo;

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

/* core entry point */
typedef const VSAPI *(VS_CC *VSGetVapourSynthAPI)(int version);

/* plugin function and filter typedefs */
typedef void (VS_CC *VSPublicFunction)(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSRegisterFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);
typedef void (VS_CC *VSConfigPlugin)(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readonly, VSPlugin *plugin);
typedef void (VS_CC *VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
typedef void (VS_CC *VSFreeFuncData)(void *userData);
typedef void (VS_CC *VSFilterInit)(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi);
typedef const VSFrameRef *(VS_CC *VSFilterGetFrame)(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
typedef void (VS_CC *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi);

/* other */
typedef void (VS_CC *VSFrameDoneCallback)(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg);
typedef void (VS_CC *VSMessageHandler)(int msgType, const char *msg, void *userData);

struct VSAPI {
	VSCore *(VS_CC *createCore)(int threads);
	void (VS_CC *freeCore)(VSCore *core);
	const VSCoreInfo *(VS_CC *getCoreInfo)(VSCore *core);

	const VSFrameRef *(VS_CC *cloneFrameRef)(const VSFrameRef *f);
	VSNodeRef *(VS_CC *cloneNodeRef)(VSNodeRef *node);
	VSFuncRef *(VS_CC *cloneFuncRef)(VSFuncRef *f);

	void (VS_CC *freeFrame)(const VSFrameRef *f);
	void (VS_CC *freeNode)(VSNodeRef *node);
	void (VS_CC *freeFunc)(VSFuncRef *f);

	VSFrameRef *(VS_CC *newVideoFrame)(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core);
	VSFrameRef *(VS_CC *copyFrame)(const VSFrameRef *f, VSCore *core);
	void (VS_CC *copyFrameProps)(const VSFrameRef *src, VSFrameRef *dst, VSCore *core);

	void (VS_CC *registerFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);
	VSPlugin *(VS_CC *getPluginById)(const char *identifier, VSCore *core);
	VSPlugin *(VS_CC *getPluginByNs)(const char *ns, VSCore *core);
	VSMap *(VS_CC *getPlugins)(VSCore *core);
	VSMap *(VS_CC *getFunctions)(VSPlugin *plugin);
	void (VS_CC *createFilter)(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core);
	void (VS_CC *setError)(VSMap *map, const char *errorMessage); /* use to signal errors outside filter getframe functions */
	const char *(VS_CC *getError)(const VSMap *map); /* use to query errors, returns 0 if no error */
	void (VS_CC *setFilterError)(const char *errorMessage, VSFrameContext *frameCtx); /* use to signal errors in the filter getframe function */
	VSMap *(VS_CC *invoke)(VSPlugin *plugin, const char *name, const VSMap *args);

	const VSFormat *(VS_CC *getFormatPreset)(int id, VSCore *core);
	const VSFormat *(VS_CC *registerFormat)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core);

	const VSFrameRef *(VS_CC *getFrame)(int n, VSNodeRef *node, char *errorMsg, int bufSize); /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
	void (VS_CC *getFrameAsync)(int n, VSNodeRef *node, VSFrameDoneCallback callback, void *userData); /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
	const VSFrameRef *(VS_CC *getFrameFilter)(int n, VSNodeRef *node, VSFrameContext *frameCtx); /* only use inside a filter's getframe function */
	void (VS_CC *requestFrameFilter)(int n, VSNodeRef *node, VSFrameContext *frameCtx); /* only use inside a filter's getframe function */
	void (VS_CC *queryCompletedFrame)(VSNodeRef **node, int *n, VSFrameContext *frameCtx); /* only use inside a filter's getframe function */
	void (VS_CC *releaseFrameEarly)(VSNodeRef *node, int n, VSFrameContext *frameCtx); /* only use inside a filter's getframe function */

	int (VS_CC *getStride)(const VSFrameRef *f, int plane);
	const uint8_t *(VS_CC *getReadPtr)(const VSFrameRef *f, int plane);
	uint8_t *(VS_CC *getWritePtr)(VSFrameRef *f, int plane);

	VSFuncRef *(VS_CC *createFunc)(VSPublicFunction func, void *userData, VSFreeFuncData free, VSCore *core, const VSAPI *vsapi);
	void (VS_CC *callFunc)(VSFuncRef *func, const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi); /* core and vsapi arguments are completely ignored, they only remain to preserve ABI */

    /* property access functions */
	VSMap *(VS_CC *createMap)(void);
	void (VS_CC *freeMap)(VSMap *map);
	void (VS_CC *clearMap)(VSMap *map);

	const VSVideoInfo *(VS_CC *getVideoInfo)(VSNodeRef *node);
	void (VS_CC *setVideoInfo)(const VSVideoInfo *vi, int numOutputs, VSNode *node);
	const VSFormat *(VS_CC *getFrameFormat)(const VSFrameRef *f);
	int (VS_CC *getFrameWidth)(const VSFrameRef *f, int plane);
	int (VS_CC *getFrameHeight)(const VSFrameRef *f, int plane);
	const VSMap *(VS_CC *getFramePropsRO)(const VSFrameRef *f);
	VSMap *(VS_CC *getFramePropsRW)(VSFrameRef *f);

	int (VS_CC *propNumKeys)(const VSMap *map);
	const char *(VS_CC *propGetKey)(const VSMap *map, int index);
	int (VS_CC *propNumElements)(const VSMap *map, const char *key);
	char (VS_CC *propGetType)(const VSMap *map, const char *key);

	int64_t(VS_CC *propGetInt)(const VSMap *map, const char *key, int index, int *error);
	double(VS_CC *propGetFloat)(const VSMap *map, const char *key, int index, int *error);
	const char *(VS_CC *propGetData)(const VSMap *map, const char *key, int index, int *error);
	int (VS_CC *propGetDataSize)(const VSMap *map, const char *key, int index, int *error);
	VSNodeRef *(VS_CC *propGetNode)(const VSMap *map, const char *key, int index, int *error);
	const VSFrameRef *(VS_CC *propGetFrame)(const VSMap *map, const char *key, int index, int *error);
	VSFuncRef *(VS_CC *propGetFunc)(const VSMap *map, const char *key, int index, int *error);

	int (VS_CC *propDeleteKey)(VSMap *map, const char *key);
	int (VS_CC *propSetInt)(VSMap *map, const char *key, int64_t i, int append);
	int (VS_CC *propSetFloat)(VSMap *map, const char *key, double d, int append);
	int (VS_CC *propSetData)(VSMap *map, const char *key, const char *data, int size, int append);
	int (VS_CC *propSetNode)(VSMap *map, const char *key, VSNodeRef *node, int append);
	int (VS_CC *propSetFrame)(VSMap *map, const char *key, const VSFrameRef *f, int append);
	int (VS_CC *propSetFunc)(VSMap *map, const char *key, VSFuncRef *func, int append);

	/* mixed functions added after API R3.0 */
	int64_t (VS_CC *setMaxCacheSize)(int64_t bytes, VSCore *core);
	int (VS_CC *getOutputIndex)(VSFrameContext *frameCtx);
	VSFrameRef *(VS_CC *newVideoFrame2)(const VSFormat *format, int width, int height, const VSFrameRef **planeSrc, const int *planes, const VSFrameRef *propSrc, VSCore *core);
	void (VS_CC *setMessageHandler)(VSMessageHandler handler, void *userData);
	int (VS_CC *setThreadCount)(int threads, VSCore *core);

	const char *(VS_CC *getPluginPath)(const VSPlugin *plugin);

	const int64_t *(VS_CC *propGetIntArray)(const VSMap *map, const char *key, int *error);
	const double *(VS_CC *propGetFloatArray)(const VSMap *map, const char *key, int *error);

	int (VS_CC *propSetIntArray)(VSMap *map, const char *key, const int64_t *i, int size);
	int (VS_CC *propSetFloatArray)(VSMap *map, const char *key, const double *d, int size);
};

VS_API(const VSAPI *) getVapourSynthAPI(int version);

#endif /* VAPOURSYNTH_H */
