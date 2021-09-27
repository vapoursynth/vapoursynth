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

#ifndef VAPOURSYNTH3_H
#define VAPOURSYNTH3_H

#include "VapourSynth4.h"

namespace vs3 {

#define VAPOURSYNTH3_API_MAJOR 3
#define VAPOURSYNTH3_API_MINOR 6
#define VAPOURSYNTH3_API_VERSION ((VAPOURSYNTH_API_MAJOR << 16) | (VAPOURSYNTH_API_MINOR))

    typedef struct VSAPI3 VSAPI3;

    static const int nfNoCache = 1;
    static const int nfIsCache = 2;
    static const int nfMakeLinear = 4;

    typedef enum VSColorFamily {
        /* all planar formats */
        cmGray = 1000000,
        cmRGB = 2000000,
        cmYUV = 3000000,
        cmYCoCg = 4000000,
        /* special for compatibility */
        cmCompat = 9000000
    } VSColorFamily;

    /* The +10 is so people won't be using the constants interchangeably "by accident" */
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

        pfYUV420P12,
        pfYUV422P12,
        pfYUV444P12,

        pfYUV420P14,
        pfYUV422P14,
        pfYUV444P14,

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
        fmFrameState = 400 /* for source filters and compatibility with other filtering architectures */
    } VSFilterMode;

    typedef struct VSVideoFormat {
        char name[32];
        int id;
        int colorFamily; /* see VSColorFamily */
        int sampleType; /* see VSSampleType */
        int bitsPerSample; /* number of significant bits */
        int bytesPerSample; /* actual storage is always in a power of 2 and the smallest possible that can fit the number of bits used per sample */

        int subSamplingW; /* log2 subsampling factor, applied to second and third plane */
        int subSamplingH;

        int numPlanes; /* implicit from colorFamily */
    } VSVideoFormat;

    typedef enum VSPropTypes {
        ptUnset = 'u',
        ptInt = 'i',
        ptFloat = 'f',
        ptData = 's',
        ptNode = 'c',
        ptFrame = 'v',
        ptFunction = 'm'
    } VSPropTypes;

    typedef enum VSPropAppendMode {
        paReplace = 0,
        paAppend = 1,
        paTouch = 2
    } VSPropAppendMode;

    typedef struct VSVideoInfo {
        const VSVideoFormat *format;
        int64_t fpsNum;
        int64_t fpsDen;
        int width;
        int height;
        int numFrames; /* api 3.2 - no longer allowed to be 0 */
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

    /* plugin function and filter typedefs */
    typedef void (VS_CC *VSPublicFunction)(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI3 *vsapi);
    typedef void (VS_CC *VSRegisterFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);
    typedef void (VS_CC *VSConfigPlugin)(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readonly, VSPlugin *plugin);
    typedef void (VS_CC *VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
    typedef void (VS_CC *VSFilterInit)(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI3 *vsapi);
    typedef const VSFrame *(VS_CC *VSFilterGetFrame)(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI3 *vsapi);
    typedef void (VS_CC *VSMessageHandler)(int msgType, const char *msg, void *userData);
    typedef void (VS_CC *VSMessageHandlerFree)(void *userData);

    /* other */
    typedef void (VS_CC *VSFrameDoneCallback)(void *userData, const VSFrame *f, int n, VSNode *, const char *errorMsg);

    struct VSAPI3 {
        VSCore *(VS_CC *createCore)(int threads) VS_NOEXCEPT;
        void (VS_CC *freeCore)(VSCore *core) VS_NOEXCEPT;
        const VSCoreInfo *(VS_CC *getCoreInfo)(VSCore *core) VS_NOEXCEPT; /* deprecated as of api 3.6, use getCoreInfo2 instead */

        const VSFrame *(VS_CC *cloneFrameRef)(const VSFrame *f) VS_NOEXCEPT;
        VSNode *(VS_CC *cloneNodeRef)(VSNode *node) VS_NOEXCEPT;
        VSFunction *(VS_CC *cloneFunctionRef)(VSFunction *f) VS_NOEXCEPT;

        void (VS_CC *freeFrame)(const VSFrame *f) VS_NOEXCEPT;
        void (VS_CC *freeNode)(VSNode *node) VS_NOEXCEPT;
        void (VS_CC *freeFunction)(VSFunction *f) VS_NOEXCEPT;

        VSFrame *(VS_CC *newVideoFrame)(const VSVideoFormat *format, int width, int height, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
        VSFrame *(VS_CC *copyFrame)(const VSFrame *f, VSCore *core) VS_NOEXCEPT;
        void (VS_CC *copyFrameProps)(const VSFrame *src, VSFrame *dst, VSCore *core) VS_NOEXCEPT;

        void (VS_CC *registerFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin) VS_NOEXCEPT;
        VSPlugin *(VS_CC *getPluginById)(const char *identifier, VSCore *core) VS_NOEXCEPT;
        VSPlugin *(VS_CC *getPluginByNs)(const char *ns, VSCore *core) VS_NOEXCEPT;
        VSMap *(VS_CC *getPlugins)(VSCore *core) VS_NOEXCEPT;
        VSMap *(VS_CC *getFunctions)(VSPlugin *plugin) VS_NOEXCEPT;
        void (VS_CC *createFilter)(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core) VS_NOEXCEPT;
        void (VS_CC *setError)(VSMap *map, const char *errorMessage) VS_NOEXCEPT; /* use to signal errors outside filter getframe functions */
        const char *(VS_CC *getError)(const VSMap *map) VS_NOEXCEPT; /* use to query errors, returns 0 if no error */
        void (VS_CC *setFilterError)(const char *errorMessage, VSFrameContext *frameCtx) VS_NOEXCEPT; /* use to signal errors in the filter getframe function */
        VSMap *(VS_CC *invoke)(VSPlugin *plugin, const char *name, const VSMap *args) VS_NOEXCEPT;

        const VSVideoFormat *(VS_CC *getFormatPreset)(int id, VSCore *core) VS_NOEXCEPT;
        const VSVideoFormat *(VS_CC *registerFormat)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core) VS_NOEXCEPT;

        const VSFrame *(VS_CC *getFrame)(int n, VSNode *node, char *errorMsg, int bufSize) VS_NOEXCEPT; /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
        void (VS_CC *getFrameAsync)(int n, VSNode *node, VSFrameDoneCallback callback, void *userData) VS_NOEXCEPT; /* do never use inside a filter's getframe function, for external applications using the core as a library or for requesting frames in a filter constructor */
        const VSFrame *(VS_CC *getFrameFilter)(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
        void (VS_CC *requestFrameFilter)(int n, VSNode *node, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
        void (VS_CC *queryCompletedFrame)(VSNode **node, int *n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */
        void (VS_CC *releaseFrameEarly)(VSNode *node, int n, VSFrameContext *frameCtx) VS_NOEXCEPT; /* only use inside a filter's getframe function */

        int (VS_CC *getStride)(const VSFrame *f, int plane) VS_NOEXCEPT;
        const uint8_t *(VS_CC *getReadPtr)(const VSFrame *f, int plane) VS_NOEXCEPT;
        uint8_t *(VS_CC *getWritePtr)(VSFrame *f, int plane) VS_NOEXCEPT;

        VSFunction *(VS_CC *createFunction)(VSPublicFunction func, void *userData, VSFreeFunctionData free, VSCore *core, const VSAPI3 *vsapi) VS_NOEXCEPT;
        void (VS_CC *callFunction)(VSFunction *func, const VSMap *in, VSMap *out, VSCore *core, const VSAPI3 *vsapi) VS_NOEXCEPT; /* core and vsapi arguments are completely ignored, they only remain to preserve ABI */

        /* property access functions */
        VSMap *(VS_CC *createMap)(void) VS_NOEXCEPT;
        void (VS_CC *freeMap)(VSMap *map) VS_NOEXCEPT;
        void (VS_CC *clearMap)(VSMap *map) VS_NOEXCEPT;

        const VSVideoInfo *(VS_CC *getVideoInfo)(VSNode *node) VS_NOEXCEPT;
        void (VS_CC *setVideoInfo)(const VSVideoInfo *vi, int numOutputs, VSNode *node) VS_NOEXCEPT;
        const VSVideoFormat *(VS_CC *getFrameFormat)(const VSFrame *f) VS_NOEXCEPT;
        int (VS_CC *getFrameWidth)(const VSFrame *f, int plane) VS_NOEXCEPT;
        int (VS_CC *getFrameHeight)(const VSFrame *f, int plane) VS_NOEXCEPT;
        const VSMap *(VS_CC *getFramePropsRO)(const VSFrame *f) VS_NOEXCEPT;
        VSMap *(VS_CC *getFramePropsRW)(VSFrame *f) VS_NOEXCEPT;

        int (VS_CC *propNumKeys)(const VSMap *map) VS_NOEXCEPT;
        const char *(VS_CC *propGetKey)(const VSMap *map, int index) VS_NOEXCEPT;
        int (VS_CC *propNumElements)(const VSMap *map, const char *key) VS_NOEXCEPT;
        char (VS_CC *propGetType)(const VSMap *map, const char *key) VS_NOEXCEPT;

        int64_t(VS_CC *propGetInt)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        double(VS_CC *propGetFloat)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        const char *(VS_CC *propGetData)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        int (VS_CC *propGetDataSize)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        VSNode *(VS_CC *propGetNode)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        const VSFrame *(VS_CC *propGetFrame)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;
        VSFunction *(VS_CC *propGetFunc)(const VSMap *map, const char *key, int index, int *error) VS_NOEXCEPT;

        int (VS_CC *propDeleteKey)(VSMap *map, const char *key) VS_NOEXCEPT;
        int (VS_CC *propSetInt)(VSMap *map, const char *key, int64_t i, int append) VS_NOEXCEPT;
        int (VS_CC *propSetFloat)(VSMap *map, const char *key, double d, int append) VS_NOEXCEPT;
        int (VS_CC *propSetData)(VSMap *map, const char *key, const char *data, int size, int append) VS_NOEXCEPT;
        int (VS_CC *propSetNode)(VSMap *map, const char *key, VSNode *node, int append) VS_NOEXCEPT;
        int (VS_CC *propSetFrame)(VSMap *map, const char *key, const VSFrame *f, int append) VS_NOEXCEPT;
        int (VS_CC *propSetFunc)(VSMap *map, const char *key, VSFunction *func, int append) VS_NOEXCEPT;

        int64_t(VS_CC *setMaxCacheSize)(int64_t bytes, VSCore *core) VS_NOEXCEPT;
        int (VS_CC *getOutputIndex)(VSFrameContext *frameCtx) VS_NOEXCEPT;
        VSFrame *(VS_CC *newVideoFrame2)(const VSVideoFormat *format, int width, int height, const VSFrame **planeSrc, const int *planes, const VSFrame *propSrc, VSCore *core) VS_NOEXCEPT;
        void (VS_CC *setMessageHandler)(VSMessageHandler handler, void *userData) VS_NOEXCEPT; /* deprecated as of api 3.6, use addMessageHandler and removeMessageHandler instead */
        int (VS_CC *setThreadCount)(int threads, VSCore *core) VS_NOEXCEPT;

        const char *(VS_CC *getPluginPath)(const VSPlugin *plugin) VS_NOEXCEPT;

        /* api 3.1 */
        const int64_t *(VS_CC *propGetIntArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;
        const double *(VS_CC *propGetFloatArray)(const VSMap *map, const char *key, int *error) VS_NOEXCEPT;

        int (VS_CC *propSetIntArray)(VSMap *map, const char *key, const int64_t *i, int size) VS_NOEXCEPT;
        int (VS_CC *propSetFloatArray)(VSMap *map, const char *key, const double *d, int size) VS_NOEXCEPT;

        /* api 3.4 */
        void (VS_CC *logMessage)(int msgType, const char *msg) VS_NOEXCEPT;

        /* api 3.6 */
        int (VS_CC *addMessageHandler)(VSMessageHandler handler, VSMessageHandlerFree free, void *userData) VS_NOEXCEPT;
        int (VS_CC *removeMessageHandler)(int id) VS_NOEXCEPT;
        void (VS_CC *getCoreInfo2)(VSCore *core, VSCoreInfo *info) VS_NOEXCEPT;
    };

}

#endif /* VAPOURSYNTH3_H */
