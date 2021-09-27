/*****************************************************************************
* Copyright (c) 2012-2020 Fredrik Mellbin
* --- Legal stuff ---
* This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
* and/or modify it under the terms of the Do What The Fuck You Want
* To Public License, Version 2, as published by Sam Hocevar. See
* http://sam.zoy.org/wtfpl/COPYING for more details.
*****************************************************************************/

#ifndef VSHELPER4_H
#define VSHELPER4_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#ifdef _WIN32
#include <malloc.h>
#endif
#include "VapourSynth4.h"

#define VSH_STD_PLUGIN_ID "com.vapoursynth.std"
#define VSH_RESIZE_PLUGIN_ID "com.vapoursynth.resize"
#define VSH_TEXT_PLUGIN_ID "com.vapoursynth.text"

#ifdef __cplusplus
namespace vsh {
#define VSH4_MANGLE_FUNCTION_NAME(name) name
#define VSH4_BOOLEAN_TYPE bool
#else
#define VSH4_MANGLE_FUNCTION_NAME(name) vsh_##name
#define VSH4_BOOLEAN_TYPE int
#endif

/* Visual Studio doesn't recognize inline in c mode */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline _inline
#endif

/* A kinda portable definition of the C99 restrict keyword (or its unofficial C++ equivalent) */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L /* Available in C99 */
#define VS_RESTRICT restrict
#elif defined(__cplusplus) || defined(_MSC_VER) /* Almost all relevant C++ compilers support it so just assume it works */
#define VS_RESTRICT __restrict
#else /* Not supported */
#define VS_RESTRICT
#endif

#ifdef _WIN32
#define VSH_ALIGNED_MALLOC(pptr, size, alignment) do { *(pptr) = _aligned_malloc((size), (alignment)); } while (0)
#define VSH_ALIGNED_FREE(ptr) do { _aligned_free((ptr)); } while (0)
#else
#define VSH_ALIGNED_MALLOC(pptr, size, alignment) do { if(posix_memalign((void**)(pptr), (alignment), (size))) *((void**)pptr) = NULL; } while (0)
#define VSH_ALIGNED_FREE(ptr) do { free((ptr)); } while (0)
#endif

#define VSMAX(a,b) ((a) > (b) ? (a) : (b))
#define VSMIN(a,b) ((a) > (b) ? (b) : (a))

#ifdef __cplusplus 
/* A nicer templated malloc for all the C++ users out there */
#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1900)
template<typename T = void>
#else
template<typename T>
#endif
static inline T *vsh_aligned_malloc(size_t size, size_t alignment) {
#ifdef _WIN32
    return (T *)_aligned_malloc(size, alignment);
#else
    void *tmp = NULL;
    if (posix_memalign(&tmp, alignment, size))
        tmp = 0;
    return (T *)tmp;
#endif
}

static inline void vsh_aligned_free(void *ptr) {
    VSH_ALIGNED_FREE(ptr);
}
#endif /* __cplusplus */

/* convenience function for checking if the format never changes between frames */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isConstantVideoFormat)(const VSVideoInfo *vi) {
    return vi->height > 0 && vi->width > 0 && vi->format.colorFamily != cfUndefined;
}

/* convenience function to check if two clips have the same format (unknown/changeable will be considered the same too) */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isSameVideoFormat)(const VSVideoFormat *v1, const VSVideoFormat *v2) {
    return v1->colorFamily == v2->colorFamily && v1->sampleType == v2->sampleType && v1->bitsPerSample == v2->bitsPerSample && v1->subSamplingW == v2->subSamplingW && v1->subSamplingH == v2->subSamplingH;
}

/* convenience function to check if a clip has the same format as a format id */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isSameVideoPresetFormat)(unsigned presetFormat, const VSVideoFormat *v, VSCore *core, const VSAPI *vsapi) {
    return vsapi->queryVideoFormatID(v->colorFamily, v->sampleType, v->bitsPerSample, v->subSamplingW, v->subSamplingH, core) == presetFormat;
}

/* convenience function to check for if two clips have the same format (but not framerate) while also including width and height (unknown/changeable will be considered the same too) */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isSameVideoInfo)(const VSVideoInfo *v1, const VSVideoInfo *v2) {
    return v1->height == v2->height && v1->width == v2->width && VSH4_MANGLE_FUNCTION_NAME(isSameVideoFormat)(&v1->format, &v2->format);
}

/* convenience function to check for if two clips have the same format while also including samplerate (unknown/changeable will be considered the same too) */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isSameAudioFormat)(const VSAudioFormat *a1, const VSAudioFormat *a2) {
    return a1->bitsPerSample == a2->bitsPerSample && a1->sampleType == a2->sampleType && a1->channelLayout == a2->channelLayout;
}

/* convenience function to check for if two clips have the same format while also including samplerate (unknown/changeable will be considered the same too) */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(isSameAudioInfo)(const VSAudioInfo *a1, const VSAudioInfo *a2) {
    return a1->sampleRate == a2->sampleRate && VSH4_MANGLE_FUNCTION_NAME(isSameAudioFormat)(&a1->format, &a2->format);
}

/* multiplies and divides a rational number, such as a frame duration, in place and reduces the result */
static inline void VSH4_MANGLE_FUNCTION_NAME(muldivRational)(int64_t *num, int64_t *den, int64_t mul, int64_t div) {
    /* do nothing if the rational number is invalid */
    if (!*den)
        return;

    /* nobody wants to accidentally divide by zero */
    assert(div);

    int64_t a, b;
    *num *= mul;
    *den *= div;
    a = *num;
    b = *den;
    while (b != 0) {
        int64_t t = a;
        a = b;
        b = t % b;
    }
    if (a < 0)
        a = -a;
    *num /= a;
    *den /= a;
}

/* reduces a rational number */
static inline void VSH4_MANGLE_FUNCTION_NAME(reduceRational)(int64_t *num, int64_t *den) {
    VSH4_MANGLE_FUNCTION_NAME(muldivRational)(num, den, 1, 1);
}

/* add two rational numbers and reduces the result */
static inline void VSH4_MANGLE_FUNCTION_NAME(addRational)(int64_t *num, int64_t *den, int64_t addnum, int64_t addden) {
    /* do nothing if the rational number is invalid */
    if (!*den)
        return;

    /* nobody wants to accidentally add an invalid rational number */
    assert(addden);

    if (*den == addden) {
        *num += addnum;
    } else {
        int64_t temp = addden;
        addnum *= *den;
        addden *= *den;
        *num *= temp;
        *den *= temp;

        *num += addnum;

        VSH4_MANGLE_FUNCTION_NAME(reduceRational)(num, den);
    }
}

/* converts an int64 to int with saturation, useful to silence warnings when reading int properties among other things */
static inline int VSH4_MANGLE_FUNCTION_NAME(int64ToIntS)(int64_t i) {
    if (i > INT_MAX)
        return INT_MAX;
    else if (i < INT_MIN)
        return INT_MIN;
    else return (int)i;
}

/* converts a double to float with saturation, useful to silence warnings when reading float properties among other things */
static inline float VSH4_MANGLE_FUNCTION_NAME(doubleToFloatS)(double d) {
    if (!isfinite(d))
        return (float)d;
    else if (d > FLT_MAX)
        return FLT_MAX;
    else if (d < -FLT_MAX)
        return -FLT_MAX;
    else
        return (float)d;
}

static inline void VSH4_MANGLE_FUNCTION_NAME(bitblt)(void *dstp, ptrdiff_t dst_stride, const void *srcp, ptrdiff_t src_stride, size_t row_size, size_t height) {
    if (height) {
        if (src_stride == dst_stride && src_stride == (ptrdiff_t)row_size) {
            memcpy(dstp, srcp, row_size * height);
        } else {
            const uint8_t *srcp8 = (const uint8_t *)srcp;
            uint8_t *dstp8 = (uint8_t *)dstp;
            size_t i;
            for (i = 0; i < height; i++) {
                memcpy(dstp8, srcp8, row_size);
                srcp8 += src_stride;
                dstp8 += dst_stride;
            }
        }
    }
}

/* check if the frame dimensions are valid for a given format */
/* returns non-zero for valid width and height */
static inline VSH4_BOOLEAN_TYPE VSH4_MANGLE_FUNCTION_NAME(areValidDimensions)(const VSVideoFormat *fi, int width, int height) {
    return !(width % (1 << fi->subSamplingW) || height % (1 << fi->subSamplingH));
}

/* Visual Studio doesn't recognize inline in c mode */
#if defined(_MSC_VER) && !defined(__cplusplus)
#undef inline
#endif

#ifdef __cplusplus
}
#endif

#endif
