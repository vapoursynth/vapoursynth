/*
* Copyright (c) 2015 John Smith
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


#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <array>
#include <memory>
#include <VapourSynth.h>
#include <VSHelper.h>
#include "filtershared.h"



#ifdef VS_TARGET_OS_WINDOWS
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

#ifdef VS_TARGET_CPU_X86
#include <emmintrin.h>
#endif

static inline int64_t floatToInt64S(double f) {
    if (f > INT64_MAX)
        return INT64_MAX;
    else if (f < INT64_MIN)
        return INT64_MIN;
    else
        return (int64_t)llrint(f);
}


enum GenericOperations {
    GenericPrewitt,
    GenericSobel,

    GenericMinimum,
    GenericMaximum,

    GenericMedian,

    GenericDeflate,
    GenericInflate,

    GenericConvolution
};

enum ConvolutionTypes {
    ConvolutionSquare,
    ConvolutionHorizontal,
    ConvolutionVertical
};

struct GenericData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    bool process[3];

    ConvolutionTypes convolution_type;

    const char *filter_name;

    /////////
    // per filter stuff

    // Prewitt, Sobel.
    float thresh_low;
    float thresh_high;
    int rshift;

    // Minimum, Maximum, Deflate, Inflate.
    uint16_t th;
    float thf;

    // Minimum, Maximum.
    int pattern;
    bool enable[8];

    // Convolution.
    int matrix[25];
    float matrixf[25];
    int matrix_sum;
    int matrix_elements;
    float rdiv;
    float bias;
    bool saturate;
};

static void sharedFormatCheck(const VSFormat *fi) {
    if (fi->colorFamily == cmCompat)
        throw std::string("Cannot process compat formats.");

    if ((fi->sampleType == stInteger && fi->bitsPerSample > 16) || (fi->sampleType == stFloat && fi->bitsPerSample != 32))
        throw std::string("Only clips with 8..16 bits integer per sample or float supported.");
}

enum RangeArgumentHandling {
    RangeLower,
    RangeUpper,
    RangeMiddle
};

static void getPlanePixelRangeArgs(const VSFormat *fi, const VSMap *in, const char *propName, uint16_t *ival, float *fval, RangeArgumentHandling mode, const VSAPI *vsapi) {
    if (vsapi->propNumElements(in, propName) > fi->numPlanes)
        throw std::string(propName).append(" has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        bool uv = (plane > 0 && (fi->colorFamily == cmYUV || fi->colorFamily == cmYCoCg));
        double temp = vsapi->propGetFloat(in, propName, plane, &err);
        if (err) {
            if (prevValid) {
                ival[plane] = ival[plane - 1];
                fval[plane] = fval[plane - 1];
            } else if (mode == RangeLower) { // bottom of pixel range
                ival[plane] = 0;
                fval[plane] = uv ? -.5f : 0;
            } else if (mode == RangeUpper) { // top of pixel range
                ival[plane] = (1 << fi->bitsPerSample) - 1;
                fval[plane] = uv ? .5f : 1.f;
            } else if (mode == RangeMiddle) { // middle of pixel range
                ival[plane] = (1 << fi->bitsPerSample) / 2;
                fval[plane] = uv ? 0.f : .5f;
            }
        } else {
            if (fi->sampleType == stInteger) {
                int64_t temp2 = static_cast<int64_t>(temp + .5);
                if ((temp2 < 0) || (temp2 >(1 << fi->bitsPerSample) - 1))
                    throw std::string(propName).append(" out of range");
                ival[plane] = static_cast<uint16_t>(temp2);
            } else {
                fval[plane] = static_cast<float>(temp);
            }
            prevValid = true;
        }
    }
}

template<typename T>
static void getPlaneArgs(const VSFormat *fi, const VSMap *in, const char *propName, T *val, T def, const VSAPI *vsapi) {
    if (vsapi->propNumElements(in, propName) > fi->numPlanes)
        throw std::string(propName).append(" has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        T temp = vsapi->propGetFloat(in, propName, plane, &err);
        if (err) {
            val[plane] = prevValid ? val[plane - 1] : def;
        } else {
            val[plane] = temp;
            prevValid = true;
        }
    }
}

template<typename T>
static void VS_CC templateClipInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    T *d = (T *)* instanceData;
    vsapi->setVideoInfo(vsapi->getVideoInfo(d->node), 1, node);
}

template<typename T>
static void VS_CC templateClipFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    T *d = (T *)instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

template<typename T, typename OP>
static const VSFrameRef *VS_CC singlePixelGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    T *d = (T *)* instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);

        try {
            sharedFormatCheck(fi);
        } catch (std::string &error) {
            vsapi->setFilterError(std::string(d->name).append(": ").append(error).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                OP opts(d, fi, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
                const uint8_t * VS_RESTRICT srcp = vsapi->getReadPtr(src, plane);
                int width = vsapi->getFrameWidth(src, plane);
                int height = vsapi->getFrameHeight(src, plane);
                ptrdiff_t stride = vsapi->getStride(src, plane);

                for (int h = 0; h < height; h++) {
                    if (fi->bytesPerSample == 1)
                        OP::template processPlane<uint8_t>(srcp, dstp, width, opts);
                    else if (fi->bytesPerSample == 2)
                        OP::template processPlane<uint16_t>(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, opts);
                    else if (fi->bytesPerSample == 4)
                        OP::template processPlaneF<float>(reinterpret_cast<const float *>(srcp), reinterpret_cast<float *>(dstp), width, opts);
                    srcp += stride;
                    dstp += stride;
                }
            }
        }

        return dst;
    }

    return nullptr;
}

template<typename T>
static void templateInit(T& d, const char *name, bool allowVariableFormat, const VSMap *in, VSMap *out, const VSAPI *vsapi) {
    *d = {};

    d->name = name;
    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    if (!allowVariableFormat && !d->vi->format)
        throw std::string("Cannot process variable format.");

    if (d->vi->format)
        sharedFormatCheck(d->vi->format);

    int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        d->process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int o = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

        if (o < 0 || o >= 3)
            throw std::string("plane index out of range");

        if (d->process[o])
            throw std::string("plane specified twice");

        d->process[o] = true;
    }
}

/////////////////

#ifdef VS_TARGET_CPU_X86

alignas(sizeof(__m128i)) static const uint16_t signMask16[8] = { 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000 };

#define CONVSIGN16_IN \
__m128i convSignMask = _mm_load_si128(reinterpret_cast<const __m128i *>(signMask16)); \
t1 = _mm_xor_si128(t1, convSignMask); \
t2 = _mm_xor_si128(t2, convSignMask); \
t3 = _mm_xor_si128(t3, convSignMask); \
m1 = _mm_xor_si128(m1, convSignMask); \
m2 = _mm_xor_si128(m2, convSignMask); \
m3 = _mm_xor_si128(m3, convSignMask); \
b1 = _mm_xor_si128(b1, convSignMask); \
b2 = _mm_xor_si128(b2, convSignMask); \
b3 = _mm_xor_si128(b3, convSignMask)

#define CONVSIGN16_OUT(x) \
_mm_xor_si128((x), convSignMask)

#define ReduceAll(OP) \
t1 = OP(t1, t2); \
m1 = OP(m1, m2); \
b1 = OP(b1, b2); \
t1 = OP(t1, t3); \
m1 = OP(m1, m3); \
b1 = OP(b1, b3); \
t1 = OP(t1, m1); \
auto reduced = OP(t1, b1)

#define ReducePlus(OP) \
t2 = OP(t2, b2); \
m1 = OP(m1, m3); \
t2 = OP(t2, m2); \
auto reduced = OP(t2, m1)

#define ReduceHorizontal(OP) \
auto reduced = OP(OP(m1, m2), m3)

#define ReduceVertical(OP) \
auto reduced = OP(OP(t2, m2), b2)

struct LimitMehFlateMinOp {
    static FORCE_INLINE __m128i limit8(__m128i newval, __m128i oldval, uint16_t limit) {
        return _mm_min_epu8(_mm_max_epu8(newval, oldval), _mm_adds_epu8(oldval, _mm_set1_epi8(limit)));
    }

    static FORCE_INLINE __m128i limit16(__m128i newval, __m128i oldval, uint16_t limit, __m128i convSignMask) {
        return CONVSIGN16_OUT(_mm_min_epi16(_mm_max_epi16(CONVSIGN16_OUT(newval), CONVSIGN16_OUT(oldval)), CONVSIGN16_OUT(_mm_adds_epu16(oldval, _mm_set1_epi16(limit)))));
    }

    static FORCE_INLINE __m128 limitF(__m128 newval, __m128 oldval, float limitf) {
        return _mm_min_ps(_mm_max_ps(newval, oldval), _mm_add_ps(oldval, _mm_set_ps1(limitf)));
    }
};

struct LimitMehFlateMaxOp {
    static FORCE_INLINE __m128i limit8(__m128i newval, __m128i oldval, uint16_t limit) {
        return _mm_max_epu8(_mm_min_epu8(newval, oldval), _mm_subs_epu8(oldval, _mm_set1_epi8(limit)));
    }

    static FORCE_INLINE __m128i limit16(__m128i newval, __m128i oldval, uint16_t limit, __m128i convSignMask) {
        return CONVSIGN16_OUT(_mm_max_epi16(_mm_min_epi16(CONVSIGN16_OUT(newval), CONVSIGN16_OUT(oldval)), CONVSIGN16_OUT(_mm_subs_epu16(oldval, _mm_set1_epi16(limit)))));
    }

    static FORCE_INLINE __m128 limitF(__m128 newval, __m128 oldval, float limitf) {
        return _mm_max_ps(_mm_min_ps(newval, oldval), _mm_sub_ps(oldval, _mm_set_ps1(limitf)));
    }
};

struct LimitMinOp {
    static FORCE_INLINE __m128i limit8(__m128i newval, __m128i oldval, uint16_t limit) {
        return _mm_min_epu8(newval, _mm_adds_epu8(oldval, _mm_set1_epi8(limit)));
    }

    static FORCE_INLINE __m128i limit16(__m128i newval, __m128i oldval, uint16_t limit, __m128i convSignMask) {
        return CONVSIGN16_OUT(_mm_min_epi16(newval, CONVSIGN16_OUT(_mm_adds_epu16(CONVSIGN16_OUT(oldval), _mm_set1_epi16(limit)))));
    }

    static FORCE_INLINE __m128 limitF(__m128 newval, __m128 oldval, float limitf) {
        return _mm_min_ps(newval, _mm_add_ps(oldval, _mm_set_ps1(limitf)));
    }
};

struct LimitMaxOp {
    static FORCE_INLINE __m128i limit8(__m128i newval, __m128i oldval, uint16_t limit) {
        return _mm_max_epu8(newval, _mm_subs_epu8(oldval, _mm_set1_epi8(limit)));
    }

    static FORCE_INLINE __m128i limit16(__m128i newval, __m128i oldval, uint16_t limit, __m128i convSignMask) {
        return CONVSIGN16_OUT(_mm_max_epi16(newval, CONVSIGN16_OUT(_mm_subs_epu16(CONVSIGN16_OUT(oldval), _mm_set1_epi16(limit)))));
    }

    static FORCE_INLINE __m128 limitF(__m128 newval, __m128 oldval, float limitf) {
        return _mm_max_ps(newval, _mm_sub_ps(oldval, _mm_set_ps1(limitf)));
    }
};

#define X86_MAXMINOP(NAME, REDUCE, REDUCEOP, LIMITOP) \
struct NAME ## Op ## REDUCE { \
    struct FrameData { \
        uint16_t limit; \
        float limitf; \
        FrameData(const GenericData *d, const VSFormat *fi, int plane) { \
            limit = d->th; \
            limitf = d->thf; \
        } \
    }; \
 \
    static FORCE_INLINE __m128i process8(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) { \
        REDUCE(_mm_##REDUCEOP##_epu8); \
        return LIMITOP::limit8(reduced, m2, opts.limit); \
    } \
 \
    static FORCE_INLINE __m128i process16(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) { \
        CONVSIGN16_IN; \
        REDUCE(_mm_##REDUCEOP##_epi16); \
        return LIMITOP::limit16(reduced, m2, opts.limit, convSignMask); \
    } \
 \
    static FORCE_INLINE __m128 processF(__m128 t1, __m128 t2, __m128 t3, __m128 m1, __m128 m2, __m128 m3, __m128 b1, __m128 b2, __m128 b3, const FrameData &opts) { \
        REDUCE(_mm_##REDUCEOP##_ps); \
        return LIMITOP::limitF(reduced, m2, opts.limitf); \
    } \
};


X86_MAXMINOP(Max, ReduceAll, max, LimitMinOp)
X86_MAXMINOP(Max, ReducePlus, max, LimitMinOp)
X86_MAXMINOP(Max, ReduceHorizontal, max, LimitMinOp)
X86_MAXMINOP(Max, ReduceVertical, max, LimitMinOp)

X86_MAXMINOP(Min, ReduceAll, min, LimitMaxOp)
X86_MAXMINOP(Min, ReducePlus, min, LimitMaxOp)
X86_MAXMINOP(Min, ReduceHorizontal, min, LimitMaxOp)
X86_MAXMINOP(Min, ReduceVertical, min, LimitMaxOp)

static FORCE_INLINE __m128i _mm_packus_epi32_sse2(__m128i v1, __m128i v2) {
    __m128i ones = _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128());
    __m128i subMask32 = _mm_srli_epi32(_mm_slli_epi32(ones, 31), 16);
    __m128i addMask16 = _mm_slli_epi16(ones, 15);
    return _mm_add_epi16(_mm_packs_epi32(_mm_sub_epi32(v1, subMask32), _mm_sub_epi32(v2, subMask32)), addMask16);
}

struct Convolution3x3 {
    struct FrameData {
        float bias;
        float divisor;
        int matrix[9];
        float matrixf[9];
        bool saturate;
        int matrix_sum2;

        FrameData(const GenericData *d, const VSFormat *fi, int plane) {
            bias = d->bias;
            divisor = d->rdiv;
            saturate = d->saturate;
            matrix_sum2 = d->matrix_sum * 2;
            for (int i = 0; i < 9; i++) {
                matrix[i] = d->matrix[i];
                matrixf[i] = d->matrixf[i];
            }
        }
    };

#define CONV_REDUCE_REG8(reg1, reg2, idx1, idx2) \
    __m128i reg1 ## lo = _mm_unpacklo_epi8(reg1, _mm_setzero_si128()); \
    __m128i reg1 ## hi = _mm_unpackhi_epi8(reg1, _mm_setzero_si128()); \
    __m128i reg2 ## lo = _mm_unpacklo_epi8(reg2, _mm_setzero_si128()); \
    __m128i reg2 ## hi = _mm_unpackhi_epi8(reg2, _mm_setzero_si128()); \
    acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(_mm_unpacklo_epi16(reg1 ## lo, reg2 ## lo), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[idx1]), _mm_set1_epi16(opts.matrix[idx2])))); \
    acc2 = _mm_add_epi32(acc2, _mm_madd_epi16(_mm_unpackhi_epi16(reg1 ## lo, reg2 ## lo), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[idx1]), _mm_set1_epi16(opts.matrix[idx2])))); \
    acc3 = _mm_add_epi32(acc3, _mm_madd_epi16(_mm_unpacklo_epi16(reg1 ## hi, reg2 ## hi), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[idx1]), _mm_set1_epi16(opts.matrix[idx2])))); \
    acc4 = _mm_add_epi32(acc4, _mm_madd_epi16(_mm_unpackhi_epi16(reg1 ## hi, reg2 ## hi), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[idx1]), _mm_set1_epi16(opts.matrix[idx2]))))


    static FORCE_INLINE __m128i process8(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        __m128 absMask = _mm_castsi128_ps(!opts.saturate ? _mm_srli_epi32(_mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()), 1) : _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));

        __m128i acc1 = _mm_madd_epi16(_mm_unpacklo_epi16(_mm_unpacklo_epi8(t1, _mm_setzero_si128()), _mm_setzero_si128()), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_setzero_si128()));
        __m128i acc2 = _mm_madd_epi16(_mm_unpackhi_epi16(_mm_unpackhi_epi8(t1, _mm_setzero_si128()), _mm_setzero_si128()), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_setzero_si128()));
        __m128i acc3 = _mm_madd_epi16(_mm_unpacklo_epi16(_mm_unpacklo_epi8(t1, _mm_setzero_si128()), _mm_setzero_si128()), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_setzero_si128()));
        __m128i acc4 = _mm_madd_epi16(_mm_unpackhi_epi16(_mm_unpackhi_epi8(t1, _mm_setzero_si128()), _mm_setzero_si128()), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_setzero_si128()));

        CONV_REDUCE_REG8(t2, t3, 1, 2);
        CONV_REDUCE_REG8(m1, m2, 3, 4);
        CONV_REDUCE_REG8(m3, b1, 5, 6);
        CONV_REDUCE_REG8(b2, b3, 7, 8);

        acc1 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc1), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));
        acc2 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc2), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));
        acc3 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc3), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));
        acc4 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc4), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));

        return _mm_packus_epi16(_mm_packus_epi32_sse2(acc1, acc2), _mm_packus_epi32_sse2(acc3, acc4));
    }

    static FORCE_INLINE __m128i process16(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        __m128 absMask = _mm_castsi128_ps(!opts.saturate ? _mm_srli_epi32(_mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()), 1) : _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));
        CONVSIGN16_IN;

        __m128i acc1 = _mm_madd_epi16(_mm_unpacklo_epi16(t1, t2), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_set1_epi16(opts.matrix[1])));
        __m128i acc2 = _mm_madd_epi16(_mm_unpackhi_epi16(t1, t2), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[0]), _mm_set1_epi16(opts.matrix[1])));
        acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(_mm_unpacklo_epi16(t3, m1), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[2]), _mm_set1_epi16(opts.matrix[3]))));
        acc2 = _mm_add_epi32(acc2, _mm_madd_epi16(_mm_unpackhi_epi16(t3, m1), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[2]), _mm_set1_epi16(opts.matrix[3]))));
        acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(_mm_unpacklo_epi16(m2, m3), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[4]), _mm_set1_epi16(opts.matrix[5]))));
        acc2 = _mm_add_epi32(acc2, _mm_madd_epi16(_mm_unpackhi_epi16(m2, m3), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[4]), _mm_set1_epi16(opts.matrix[5]))));
        acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(_mm_unpacklo_epi16(b1, b2), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[6]), _mm_set1_epi16(opts.matrix[7]))));
        acc2 = _mm_add_epi32(acc2, _mm_madd_epi16(_mm_unpackhi_epi16(b1, b2), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[6]), _mm_set1_epi16(opts.matrix[7]))));
        acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(_mm_unpacklo_epi16(b3, _mm_set1_epi16(0x4000)), _mm_unpacklo_epi16(_mm_set1_epi16(opts.matrix[8]), _mm_set1_epi16(opts.matrix_sum2))));
        acc2 = _mm_add_epi32(acc2, _mm_madd_epi16(_mm_unpackhi_epi16(b3, _mm_set1_epi16(0x4000)), _mm_unpackhi_epi16(_mm_set1_epi16(opts.matrix[8]), _mm_set1_epi16(opts.matrix_sum2))));

        // fixme, convert to integer only?
        acc1 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc1), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));
        acc2 = _mm_cvtps_epi32(_mm_max_ps(_mm_and_ps(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(acc2), _mm_set_ps1(opts.divisor)), _mm_set_ps1(opts.bias)), absMask), _mm_setzero_ps()));

        return _mm_packus_epi32_sse2(acc1, acc2);
    }

    static FORCE_INLINE __m128 processF(__m128 t1, __m128 t2, __m128 t3, __m128 m1, __m128 m2, __m128 m3, __m128 b1, __m128 b2, __m128 b3, const FrameData &opts) {
        __m128 absMask = _mm_castsi128_ps(!opts.saturate ? _mm_srli_epi32(_mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()), 1) : _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));
        t1 = _mm_mul_ps(t1, _mm_set_ps1(opts.matrixf[0]));
        t2 = _mm_mul_ps(t2, _mm_set_ps1(opts.matrixf[1]));
        t3 = _mm_mul_ps(t3, _mm_set_ps1(opts.matrixf[2]));
        m1 = _mm_mul_ps(m1, _mm_set_ps1(opts.matrixf[3]));
        m2 = _mm_mul_ps(m2, _mm_set_ps1(opts.matrixf[4]));
        m3 = _mm_mul_ps(m3, _mm_set_ps1(opts.matrixf[5]));
        b1 = _mm_mul_ps(b1, _mm_set_ps1(opts.matrixf[6]));
        b2 = _mm_mul_ps(b2, _mm_set_ps1(opts.matrixf[7]));
        b3 = _mm_mul_ps(b3, _mm_set_ps1(opts.matrixf[8]));

        t1 = _mm_add_ps(t1, t2);
        m1 = _mm_add_ps(m1, m2);
        b1 = _mm_add_ps(b1, b2);
        t1 = _mm_add_ps(t1, t3);
        m1 = _mm_add_ps(m1, m3);
        b1 = _mm_add_ps(b1, b3);
        t1 = _mm_add_ps(t1, m1);
        t1 = _mm_add_ps(t1, b1);

        t1 = _mm_mul_ps(t1, _mm_set_ps1(opts.divisor));
        t1 = _mm_add_ps(t1, _mm_set_ps1(opts.bias));
        t1 = _mm_and_ps(t1, absMask);

        return t1;
    }
};

#define UNPACK_ACC(add, unpack, reg) \
    acc1 = _mm_add_epi ## add(acc1, _mm_unpacklo_epi ## unpack(reg, _mm_setzero_si128())); \
    acc2 = _mm_add_epi ## add(acc2, _mm_unpackhi_epi ## unpack(reg, _mm_setzero_si128()));

template<typename LimitOp>
struct MehFlate {
    struct FrameData {
        uint16_t limit;
        float limitf;
        FrameData(const GenericData *d, const VSFormat *fi, int plane) {
            limit = d->th;
            limitf = d->thf;
        }
    };

    static FORCE_INLINE __m128i process8(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        __m128i acc1 = _mm_setzero_si128();
        __m128i acc2 = _mm_setzero_si128();
        UNPACK_ACC(16, 8, t1);
        UNPACK_ACC(16, 8, t2);
        UNPACK_ACC(16, 8, t3);
        UNPACK_ACC(16, 8, m1);
        UNPACK_ACC(16, 8, m3);
        UNPACK_ACC(16, 8, b1);
        UNPACK_ACC(16, 8, b2);
        UNPACK_ACC(16, 8, b3);

        acc1 = _mm_srli_epi16(_mm_add_epi16(acc1, _mm_set1_epi16(4)), 3);
        acc2 = _mm_srli_epi16(_mm_add_epi16(acc2, _mm_set1_epi16(4)), 3);

        __m128i reduced = _mm_packus_epi16(acc1, acc2);
        return LimitOp::limit8(reduced, m2, opts.limit);
    }

    static FORCE_INLINE __m128i process16(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        __m128i convSignMask = _mm_load_si128(reinterpret_cast<const __m128i *>(signMask16));
        __m128i acc1 = _mm_setzero_si128();
        __m128i acc2 = _mm_setzero_si128();
        UNPACK_ACC(32, 16, t1);
        UNPACK_ACC(32, 16, t2);
        UNPACK_ACC(32, 16, t3);
        UNPACK_ACC(32, 16, m1);
        UNPACK_ACC(32, 16, m3);
        UNPACK_ACC(32, 16, b1);
        UNPACK_ACC(32, 16, b2);
        UNPACK_ACC(32, 16, b3);

        acc1 = _mm_srli_epi32(_mm_add_epi32(acc1, _mm_set1_epi32(4)), 3);
        acc2 = _mm_srli_epi32(_mm_add_epi32(acc2, _mm_set1_epi32(4)), 3);

        __m128i reduced = _mm_packus_epi32_sse2(acc1, acc2);
        return LimitOp::limit16(reduced, m2, opts.limit, convSignMask);
    }

    static FORCE_INLINE __m128 processF(__m128 t1, __m128 t2, __m128 t3, __m128 m1, __m128 m2, __m128 m3, __m128 b1, __m128 b2, __m128 b3, const FrameData &opts) {
        ReduceAll(_mm_add_ps);
        reduced = _mm_mul_ps(reduced, _mm_set_ps1(1.f/8));
        return LimitOp::limitF(reduced, m2, opts.limitf);
    }
};

static FORCE_INLINE void sort_pair8(__m128i &a1, __m128i &a2) {
    const __m128i tmp = _mm_min_epu8(a1, a2);
    a2 = _mm_max_epu8(a1, a2);
    a1 = tmp;
}

static FORCE_INLINE void sort_pair16(__m128i &a1, __m128i &a2) {
    const __m128i tmp = _mm_min_epi16(a1, a2);
    a2 = _mm_max_epi16(a1, a2);
    a1 = tmp;
}

static FORCE_INLINE void sort_pairF(__m128 &a1, __m128 &a2) {
    const __m128 tmp = _mm_min_ps(a1, a2);
    a2 = _mm_max_ps(a1, a2);
    a1 = tmp;
}

struct Median {
    struct FrameData {
        FrameData(const GenericData *d, const VSFormat *fi, int plane) {
        }
    };

    static FORCE_INLINE __m128i process8(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        sort_pair8(t1, t2);
        sort_pair8(t3, m1);
        sort_pair8(m3, b1);
        sort_pair8(b2, b3);

        sort_pair8(t1, t3);
        sort_pair8(t2, m1);
        sort_pair8(m3, b2);
        sort_pair8(b1, b3);

        sort_pair8(t2, t3);
        sort_pair8(b1, b2);

        m3 = _mm_max_epu8(t1, m3);
        b1 = _mm_max_epu8(t2, b1);
        t3 = _mm_min_epu8(t3, b2);
        m1 = _mm_min_epu8(m1, b3);

        m3 = _mm_max_epu8(t3, m3);
        m1 = _mm_min_epu8(m1, b1);

        sort_pair8(m1, m3);

        return _mm_min_epu8(_mm_max_epu8(m2, m1), m3);
    }

    static FORCE_INLINE __m128i process16(__m128i t1, __m128i t2, __m128i t3, __m128i m1, __m128i m2, __m128i m3, __m128i b1, __m128i b2, __m128i b3, const FrameData &opts) {
        CONVSIGN16_IN;

        sort_pair16(t1, t2);
        sort_pair16(t3, m1);
        sort_pair16(m3, b1);
        sort_pair16(b2, b3);

        sort_pair16(t1, t3);
        sort_pair16(t2, m1);
        sort_pair16(m3, b2);
        sort_pair16(b1, b3);

        sort_pair16(t2, t3);
        sort_pair16(b1, b2);

        m3 = _mm_max_epi16(t1, m3);    
        b1 = _mm_max_epi16(t2, b1);   
        t3 = _mm_min_epi16(t3, b2); 
        m1 = _mm_min_epi16(m1, b3);    

        m3 = _mm_max_epi16(t3, m3);   
        m1 = _mm_min_epi16(m1, b1);   

        sort_pair16(m1, m3);

        return CONVSIGN16_OUT(_mm_min_epi16(_mm_max_epi16(m2, m1), m3));
    }

    static FORCE_INLINE __m128 processF(__m128 t1, __m128 t2, __m128 t3, __m128 m1, __m128 m2, __m128 m3, __m128 b1, __m128 b2, __m128 b3, const FrameData &opts) {
        sort_pairF(t1, t2);
        sort_pairF(t3, m1);
        sort_pairF(m3, b1);
        sort_pairF(b2, b3);

        sort_pairF(t1, t3);
        sort_pairF(t2, m1);
        sort_pairF(m3, b2);
        sort_pairF(b1, b3);

        sort_pairF(t2, t3);
        sort_pairF(b1, b2);

        m3 = _mm_max_ps(t1, m3);
        b1 = _mm_max_ps(t2, b1);
        t3 = _mm_min_ps(t3, b2);
        m1 = _mm_min_ps(m1, b3);

        m3 = _mm_max_ps(t3, m3);
        m1 = _mm_min_ps(m1, b1);

        sort_pairF(m1, m3);

        return _mm_min_ps(_mm_max_ps(m2, m1), m3);
    }
};

typedef MehFlate<LimitMehFlateMaxOp> Deflate;
typedef MehFlate<LimitMehFlateMinOp> Inflate;

template<typename T, typename OP>
void filterPlane(const uint8_t * VS_RESTRICT src, uint8_t * VS_RESTRICT dst, const ptrdiff_t stride, const unsigned width, const int height, int plane, const VSFormat *fi, const GenericData *data) {
    // -2 to compensate for first and last part
    unsigned miter = ((width * sizeof(T) + sizeof(__m128i) - sizeof(T)) / sizeof(__m128i)) - 2;
    int tailelems = ((width * sizeof(T)) % sizeof(__m128i)) / sizeof(T);
    if (tailelems == 0)
        tailelems = sizeof(__m128i) / sizeof(T);
    unsigned nheight = std::max(height - 2, 0);
    const uint8_t * VS_RESTRICT srcLineStart = src;
    uint8_t * VS_RESTRICT dstLineStart = dst;
    typename OP::FrameData opts(data, fi, plane);

    if (sizeof(T) == 4) {
        alignas(sizeof(__m128)) const uint32_t ascendMask[4] = { 0, 1, 2, 3 };
        
        __m128 tailmask = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_load_si128(reinterpret_cast<const __m128i *>(ascendMask)), _mm_set1_epi32(tailelems - 1)));

        // first line
        {
            // first block
            {
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                __m128 m1 = _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(2, 1, 0, 0));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride + sizeof(T)));
                __m128 b1 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(2, 1, 0, 0));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                __m128 b1 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride - sizeof(T)));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride + sizeof(T)));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // last block
            {
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, m2));
                __m128 b1 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride - sizeof(T)));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, b2));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
            }

            srcLineStart += stride;
            src = srcLineStart;
            dstLineStart += stride;
            dst = dstLineStart;
        }

        for (unsigned h = nheight; h > 0; h--) {
            // first block
            {
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride + sizeof(T)));
                __m128 t1 = _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(2, 1, 0, 0));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                __m128 m1 = _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(2, 1, 0, 0));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride + sizeof(T)));
                __m128 b1 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(2, 1, 0, 0));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128 t1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride - sizeof(T)));
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride + sizeof(T)));
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                __m128 b1 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride - sizeof(T)));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride + sizeof(T)));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // last block
            {
                __m128 t1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride - sizeof(T)));
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, t2));
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, m2));
                __m128 b1 = _mm_loadu_ps(reinterpret_cast<const float *>(src + stride - sizeof(T)));
                __m128 b2 = _mm_load_ps(reinterpret_cast<const float *>(src + stride));
                __m128 b3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, b2));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
            }

            srcLineStart += stride;
            src = srcLineStart;
            dstLineStart += stride;
            dst = dstLineStart;
        }

        // last line
        {
            // first block
            {
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride + sizeof(T)));
                __m128 t1 = _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(2, 1, 0, 0));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                __m128 m1 = _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(2, 1, 0, 0));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128 t1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride - sizeof(T)));
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride + sizeof(T)));
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_loadu_ps(reinterpret_cast<const float *>(src + sizeof(T)));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
                src += sizeof(__m128);
                dst += sizeof(__m128);
            }

            // last block
            {
                __m128 t1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - stride - sizeof(T)));
                __m128 t2 = _mm_load_ps(reinterpret_cast<const float *>(src - stride));
                __m128 t3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, t2));
                __m128 m1 = _mm_loadu_ps(reinterpret_cast<const float *>(src - sizeof(T)));
                __m128 m2 = _mm_load_ps(reinterpret_cast<const float *>(src));
                __m128 m3 = _mm_or_ps(_mm_andnot_ps(tailmask, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(3, 3, 2, 1))), _mm_and_ps(tailmask, m2));
                _mm_store_ps(reinterpret_cast<float *>(dst), OP::processF(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
            }
        }
    } else {
        alignas(sizeof(__m128i)) const uint8_t ascendMask1[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        alignas(sizeof(__m128i)) const uint16_t ascendMask2[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

        __m128i leadmask = _mm_srli_si128(_mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()), sizeof(__m128i) - sizeof(T));
        __m128i tailmask;
        if (sizeof(T) == 1) {
            tailmask = _mm_cmpeq_epi8(_mm_load_si128(reinterpret_cast<const __m128i *>(ascendMask1)), _mm_set1_epi8(tailelems - 1));
        } else {
            tailmask = _mm_cmpeq_epi16(_mm_load_si128(reinterpret_cast<const __m128i *>(ascendMask2)), _mm_set1_epi16(tailelems - 1));
        }


        // first line
        {
            // first block
            {
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                __m128i m1 = _mm_or_si128(_mm_and_si128(m2, leadmask), _mm_slli_si128(m2, sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride + sizeof(T)));
                __m128i b1 = _mm_or_si128(_mm_and_si128(b2, leadmask), _mm_slli_si128(b2, sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride - sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride + sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // last block
            {
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(m2, sizeof(T))), _mm_and_si128(tailmask, m2));
                __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride - sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(b2, sizeof(T))), _mm_and_si128(tailmask, b2));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(m1, m2, m3, m1, m2, m3, b1, b2, b3, opts));
            }

            srcLineStart += stride;
            src = srcLineStart;
            dstLineStart += stride;
            dst = dstLineStart;
        }

        for (unsigned h = nheight; h > 0; h--) {
            // first block
            {
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride + sizeof(T)));
                __m128i t1 = _mm_or_si128(_mm_and_si128(t2, leadmask), _mm_slli_si128(t2, sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                __m128i m1 = _mm_or_si128(_mm_and_si128(m2, leadmask), _mm_slli_si128(m2, sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride + sizeof(T)));
                __m128i b1 = _mm_or_si128(_mm_and_si128(b2, leadmask), _mm_slli_si128(b2, sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128i t1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride - sizeof(T)));
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride + sizeof(T)));
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride - sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride + sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // last block
            {
                __m128i t1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride - sizeof(T)));
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(t2, sizeof(T))), _mm_and_si128(tailmask, t2));
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(m2, sizeof(T))), _mm_and_si128(tailmask, m2));
                __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + stride - sizeof(T)));
                __m128i b2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src + stride));
                __m128i b3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(b2, sizeof(T))), _mm_and_si128(tailmask, b2));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, b1, b2, b3, opts));
            }

            srcLineStart += stride;
            src = srcLineStart;
            dstLineStart += stride;
            dst = dstLineStart;
        }

        // last line
        {
            // first block
            {
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride + sizeof(T)));
                __m128i t1 = _mm_or_si128(_mm_and_si128(t2, leadmask), _mm_slli_si128(t2, sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                __m128i m1 = _mm_or_si128(_mm_and_si128(m2, leadmask), _mm_slli_si128(m2, sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // middle
            for (unsigned w = miter; w > 0; w--) {
                __m128i t1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride - sizeof(T)));
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride + sizeof(T)));
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + sizeof(T)));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
                src += sizeof(__m128i);
                dst += sizeof(__m128i);
            }

            // last block
            {
                __m128i t1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - stride - sizeof(T)));
                __m128i t2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src - stride));
                __m128i t3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(t2, sizeof(T))), _mm_and_si128(tailmask, t2));
                __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src - sizeof(T)));
                __m128i m2 = _mm_load_si128(reinterpret_cast<const __m128i *>(src));
                __m128i m3 = _mm_or_si128(_mm_andnot_si128(tailmask, _mm_srli_si128(m2, sizeof(T))), _mm_and_si128(tailmask, m2));
                _mm_store_si128(reinterpret_cast<__m128i *>(dst), (sizeof(T) == 1) ? OP::process8(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts) : OP::process16(t1, t2, t3, m1, m2, m3, m1, m2, m3, opts));
            }
        }
    }
}

#endif


struct GenericPlaneParams {
    uint16_t max_value;

    // Prewitt, Sobel.
    float thresh_low;
    float thresh_high;
    unsigned rshift;

    // Minimum, Maximum, Deflate, Inflate.
    uint16_t th;
    float thf;

    // Minimum, Maximum.
    int enable[8];

    // Convolution.
    int matrix[25];
    float matrixf[25];
    int matrix_elements;
    float rdiv;
    float bias;
    bool saturate;

    GenericPlaneParams(const GenericData *params, const VSFormat *fi, int plane) {
        max_value = ((1 << fi->bitsPerSample) - 1);

        thresh_low = params->thresh_low;
        thresh_high = params->thresh_high;
        rshift = params->rshift;

        th = params->th;
        thf = params->thf;

        matrix_elements = params->matrix_elements;
        rdiv = params->rdiv;
        bias = params->bias;
        saturate = params->saturate;

        for (int i = 0; i < 8; i++)
            enable[i] = params->enable[i];
        for (int i = 0; i < 25; i++) {
            matrix[i] = params->matrix[i];
            matrixf[i] = params->matrixf[i];
        }
    }
};



template <GenericOperations op, typename T>
static FORCE_INLINE T min_max(T a, T b) {
    if (op == GenericMinimum || op == GenericDeflate)
        return std::min(a, b);
    else if (op == GenericMaximum || op == GenericInflate)
        return std::max(a, b);
    else
        return 42; // Silence warning.
}


template <GenericOperations op, typename T>
static FORCE_INLINE T max_min(T a, T b) {
    if (op == GenericMinimum || op == GenericDeflate)
        return std::max(a, b);
    else if (op == GenericMaximum || op == GenericInflate)
        return std::min(a, b);
    else
        return 42; // Silence warning.
}

template <typename PixelType, GenericOperations op>
static FORCE_INLINE PixelType generic_3x3I(
    PixelType a11, PixelType a21, PixelType a31,
    PixelType a12, PixelType a22, PixelType a32,
    PixelType a13, PixelType a23, PixelType a33, const GenericPlaneParams &params) {

    if (op == GenericPrewitt || op == GenericSobel) {

        int max_value = params.max_value;
        float thresh_low = params.thresh_low;
        float thresh_high = params.thresh_high;

        int64_t gx, gy;

        if (op == GenericPrewitt) {
            gx = a31 + a32 + a33 - a11 - a12 - a13;
            gy = a13 + a23 + a33 - a11 - a21 - a31;
        } else if (op == GenericSobel) {
            gx = a31 + 2 * a32 + a33 - a11 - 2 * a12 - a13;
            gy = a13 + 2 * a23 + a33 - a11 - 2 * a21 - a31;
        }

        float f = std::sqrt(static_cast<float>(gx * gx + gy * gy));

        PixelType g;
        if (f >= (thresh_high * (1 << params.rshift)))
            g = max_value;
        else if (f <= (thresh_low * (1 << params.rshift)))
            g = 0;
        else
            g = a22;

        return g;

    } else if (op == GenericMinimum || op == GenericMaximum) {

        int th = params.th;
        const int *enable = params.enable;

        int lower_or_upper_bound;

        if (op == GenericMinimum) {
            lower_or_upper_bound = 0;
            th = -th;
        } else if (op == GenericMaximum) {
            lower_or_upper_bound = params.max_value;
        }

        PixelType min_or_max = a22;

        int limit = max_min<op, int>(min_or_max + th, lower_or_upper_bound);

        if (enable[0])
            min_or_max = min_max<op>(a11, min_or_max);
        if (enable[1])
            min_or_max = min_max<op>(a21, min_or_max);
        if (enable[2])
            min_or_max = min_max<op>(a31, min_or_max);
        if (enable[3])
            min_or_max = min_max<op>(a12, min_or_max);
        if (enable[4])
            min_or_max = min_max<op>(a32, min_or_max);
        if (enable[5])
            min_or_max = min_max<op>(a13, min_or_max);
        if (enable[6])
            min_or_max = min_max<op>(a23, min_or_max);
        if (enable[7])
            min_or_max = min_max<op>(a33, min_or_max);

        return max_min<op, uint16_t>(limit, min_or_max);


    } else if (op == GenericDeflate || op == GenericInflate) {

        int th = params.th;

        int lower_or_upper_bound;

        if (op == GenericDeflate) {
            lower_or_upper_bound = 0;
            th = -th;
        } else if (op == GenericInflate) {
            lower_or_upper_bound = params.max_value;
        }

        int limit = max_min<op, int>(a22 + th, lower_or_upper_bound);

        int sum = a11 + a21 + a31 + a12 + a32 + a13 + a23 + a33 + 4;

        return max_min<op>(min_max<op, int>(sum >> 3, a22), limit);
    } else if (op == GenericMedian) {

        // Extra extra lazy.
        std::array<PixelType, 9> v{ a11, a21, a31, a12, a22, a32, a13, a23, a33 };

        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());

        return v[v.size() / 2];

    } else if (op == GenericConvolution) {
        const int *matrix = params.matrix;
        float rdiv = params.rdiv;
        float bias = params.bias;
        bool saturate = params.saturate;
        int max_value = params.max_value;

        PixelType pixels[9] = {
            a11, a21, a31,
            a12, a22, a32,
            a13, a23, a33
        };

        int sum = 0;

        for (int i = 0; i < 9; i++)
            sum += pixels[i] * matrix[i];

        sum = static_cast<int>(sum * rdiv + bias + 0.5f);

        if (!saturate)
            sum = std::abs(sum);

        return std::min(max_value, std::max(sum, 0));
    }

    return 42; // Silence warning.
}

template <typename PixelType, GenericOperations op>
static FORCE_INLINE PixelType generic_3x3F(
    PixelType a11, PixelType a21, PixelType a31,
    PixelType a12, PixelType a22, PixelType a32,
    PixelType a13, PixelType a23, PixelType a33, const GenericPlaneParams &params) {

    if (op == GenericPrewitt || op == GenericSobel) {

        float thresh_low = params.thresh_low;
        float thresh_high = params.thresh_high;

        float gx, gy;

        if (op == GenericPrewitt) {
            gx = a31 + a32 + a33 - a11 - a12 - a13;
            gy = a13 + a23 + a33 - a11 - a21 - a31;
        } else if (op == GenericSobel) {
            gx = a31 + 2 * a32 + a33 - a11 - 2 * a12 - a13;
            gy = a13 + 2 * a23 + a33 - a11 - 2 * a21 - a31;
        }

        float f = std::sqrt(static_cast<float>(gx * gx + gy * gy));

        PixelType g;
        if (f >= thresh_high)
            g = 1.f; // fixme, should have some kind of max value shit? or set value?
        else if (f <= thresh_low)
            g = 0;
        else
            g = a22;

        return g;

    } else if (op == GenericMinimum || op == GenericMaximum) {

        float th = params.thf;
        const int *enable = params.enable;

        if (op == GenericMinimum) {
            th = -th;
        }

        float min_or_max = a22;

        float limit = a22 + th;

        if (enable[0])
            min_or_max = min_max<op, PixelType>(a11, min_or_max);
        if (enable[1])
            min_or_max = min_max<op, PixelType>(a21, min_or_max);
        if (enable[2])
            min_or_max = min_max<op, PixelType>(a31, min_or_max);
        if (enable[3])
            min_or_max = min_max<op, PixelType>(a12, min_or_max);
        if (enable[4])
            min_or_max = min_max<op, PixelType>(a32, min_or_max);
        if (enable[5])
            min_or_max = min_max<op, PixelType>(a13, min_or_max);
        if (enable[6])
            min_or_max = min_max<op, PixelType>(a23, min_or_max);
        if (enable[7])
            min_or_max = min_max<op, PixelType>(a33, min_or_max);

        return max_min<op>(limit, min_or_max);


    } else if (op == GenericDeflate || op == GenericInflate) {

        float th = params.thf;

        if (op == GenericDeflate) {
            th = -th;
        }

        float limit = a22 + th;

        float sum = a11 + a21 + a31 + a12 + a32 + a13 + a23 + a33;

        return max_min<op>(min_max<op, PixelType>(sum / 8.f, a22), limit);

    } else if (op == GenericMedian) {

        // Extra extra lazy.
        std::array<PixelType, 9> v{ a11, a21, a31, a12, a22, a32, a13, a23, a33 };

        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());

        return v[v.size() / 2];

    } else if (op == GenericConvolution) {

        const float *matrixf = params.matrixf;
        float rdiv = params.rdiv;
        float bias = params.bias;
        bool saturate = params.saturate;

        PixelType pixels[9] = {
            a11, a21, a31,
            a12, a22, a32,
            a13, a23, a33
        };

        float sum = 0;

        for (int i = 0; i < 9; i++)
            sum += pixels[i] * matrixf[i];

        sum = (sum * rdiv + bias);

        if (!saturate)
            sum = std::abs(sum);

        return sum;      
    }

    return 42; // Silence warning.
}

template <typename PixelType, GenericOperations op>
static FORCE_INLINE PixelType generic_3x3(
    PixelType a11, PixelType a21, PixelType a31,
    PixelType a12, PixelType a22, PixelType a32,
    PixelType a13, PixelType a23, PixelType a33, const GenericPlaneParams &params) {
    
    if (sizeof(PixelType) == 1)
        return generic_3x3I<uint8_t, op>(a11, a21, a31, a12, a22, a32, a13, a23, a33, params);
    else if (sizeof(PixelType) == 2)
        return generic_3x3I<uint16_t, op>(a11, a21, a31, a12, a22, a32, a13, a23, a33, params);
    else
        return generic_3x3F<float, op>(a11, a21, a31, a12, a22, a32, a13, a23, a33, params);
}

template <typename PixelType, GenericOperations op>
static void process_plane_3x3(uint8_t * VS_RESTRICT dstp8, const uint8_t * VS_RESTRICT srcp8, int width, int height, int stride, const GenericPlaneParams &params) {
    stride /= sizeof(PixelType);

    PixelType * VS_RESTRICT dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType * VS_RESTRICT srcp = reinterpret_cast<const PixelType *>(srcp8);

    const PixelType * VS_RESTRICT above = srcp - stride;
    const PixelType * VS_RESTRICT below = srcp + stride;

    dstp[0] = generic_3x3<PixelType, op>(
            below[1], below[0], below[1],
             srcp[1],  srcp[0],  srcp[1],
            below[1], below[0], below[1], params);

    for (int x = 1; x < width - 1; x++)
        dstp[x] = generic_3x3<PixelType, op>(
                below[x-1], below[x], below[x+1],
                 srcp[x-1],  srcp[x],  srcp[x+1],
                below[x-1], below[x], below[x+1], params);

    dstp[width-1] = generic_3x3<PixelType, op>(
            below[width-2], below[width-1], below[width-2],
             srcp[width-2],  srcp[width-1],  srcp[width-2],
            below[width-2], below[width-1], below[width-2], params);

    dstp += stride;
    srcp += stride;
    above += stride;
    below += stride;

    for (int y = 1; y < height - 1; y++) {
        dstp[0] = generic_3x3<PixelType, op>(
                above[1], above[0], above[1],
                 srcp[1],  srcp[0],  srcp[1],
                below[1], below[0], below[1], params);

        for (int x = 1; x < width - 1; x++)
            dstp[x] = generic_3x3<PixelType, op>(
                    above[x-1], above[x], above[x+1],
                     srcp[x-1],  srcp[x],  srcp[x+1],
                    below[x-1], below[x], below[x+1], params);

        dstp[width-1] = generic_3x3<PixelType, op>(
                above[width-2], above[width-1], above[width-2],
                 srcp[width-2],  srcp[width-1],  srcp[width-2],
                below[width-2], below[width-1], below[width-2], params);

        dstp += stride;
        srcp += stride;
        above += stride;
        below += stride;
    }

    dstp[0] = generic_3x3<PixelType, op>(
            above[1], above[0], above[1],
             srcp[1],  srcp[0],  srcp[1],
            above[1], above[0], above[1], params);

    for (int x = 1; x < width - 1; x++)
        dstp[x] = generic_3x3<PixelType, op>(
                above[x-1], above[x], above[x+1],
                 srcp[x-1],  srcp[x],  srcp[x+1],
                above[x-1], above[x], above[x+1], params);

    dstp[width-1] = generic_3x3<PixelType, op>(
            above[width-2], above[width-1], above[width-2],
             srcp[width-2],  srcp[width-1],  srcp[width-2],
            above[width-2], above[width-1], above[width-2], params);
}


template <typename PixelType, GenericOperations op>
static FORCE_INLINE PixelType generic_5x5(
        PixelType a11, PixelType a21, PixelType a31, PixelType a41, PixelType a51,
        PixelType a12, PixelType a22, PixelType a32, PixelType a42, PixelType a52,
        PixelType a13, PixelType a23, PixelType a33, PixelType a43, PixelType a53,
        PixelType a14, PixelType a24, PixelType a34, PixelType a44, PixelType a54,
        PixelType a15, PixelType a25, PixelType a35, PixelType a45, PixelType a55, const GenericPlaneParams &params) {

    const float *matrixf = params.matrixf;
    const int *matrix = params.matrix;
    float rdiv = params.rdiv;
    float bias = params.bias;
    bool saturate = params.saturate;
    int max_value = params.max_value;

    PixelType pixels[25] = {
        a11, a21, a31, a41, a51,
        a12, a22, a32, a42, a52,
        a13, a23, a33, a43, a53,
        a14, a24, a34, a44, a54,
        a15, a25, a35, a45, a55
    };

    if (std::numeric_limits<PixelType>::is_integer) {
        int sum = 0;

        for (int i = 0; i < 25; i++)
            sum += pixels[i] * matrix[i];

        sum = static_cast<int>(sum * rdiv + bias + 0.5f);

        if (!saturate)
            sum = std::abs(sum);

        return std::min(max_value, std::max(sum, 0));
    } else {
        float sum = 0;

        for (int i = 0; i < 25; i++)
            sum += pixels[i] * matrixf[i];

        sum = (sum * rdiv + bias);

        if (!saturate)
            sum = std::abs(sum);

        return sum;
    }
}

template <typename PixelType>
static void process_plane_convolution_horizontal(uint8_t * VS_RESTRICT dstp8, const uint8_t * VS_RESTRICT srcp8, int width, int height, int stride, const GenericPlaneParams &params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    const float *matrixf = params.matrixf;
    const int *matrix = params.matrix;
    int matrix_elements = params.matrix_elements;
    float rdiv = params.rdiv;
    float bias = params.bias;
    bool saturate = params.saturate;
    int max_value = params.max_value;

    int border = matrix_elements / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < border; x++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[std::abs(x + i - border)] * matrixf[i];

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x] = sum;
        }

        for (int x = border; x < width - border; x++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + i - border] * matrixf[i];

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x] = sum;
        }

        for (int x = width - border; x < width; x++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++) {
                int idx = x + i - border;
                int diff = width - 1 - idx;
                idx = idx + std::min(diff*2, 0);
                sum += srcp[idx] * matrixf[i];
            }

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x] = sum;
        }

        dstp += stride;
        srcp += stride;
    }
}


template <typename PixelType>
static void process_plane_convolution_vertical(uint8_t * VS_RESTRICT dstp8, const uint8_t * VS_RESTRICT srcp8, int width, int height, int stride, const GenericPlaneParams &params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    const float *matrixf = params.matrixf;
    const int *matrix = params.matrix;
    int matrix_elements = params.matrix_elements;
    float rdiv = params.rdiv;
    float bias = params.bias;
    bool saturate = params.saturate;
    int max_value = params.max_value;

    int border = matrix_elements / 2;

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < border; y++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + std::abs(y + i - border) * stride] * matrixf[i];

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x + y * stride] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x + y * stride] = sum;
        }

        for (int y = border; y < height - border; y++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + (y + i - border) * stride] * matrixf[i];

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x + y * stride] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x + y * stride] = sum;
        }

        for (int y = height - border; y < height; y++) {
            float sum = 0;

            for (int i = 0; i < matrix_elements; i++) {
                int idx = y + i - border;
                int diff = height - 1 - idx;
                idx = idx + std::min(diff*2, 0);
                sum += srcp[x + idx * stride] * matrixf[i];
            }

            sum = (sum * rdiv + bias);

            if (!saturate)
                sum = std::abs(sum);

            if (std::numeric_limits<PixelType>::is_integer)
                dstp[x + y * stride] = std::min(max_value, std::max(static_cast<int>(sum + 0.5f), 0));
            else
                dstp[x + y * stride] = sum;
        }
    }
}


template <typename PixelType, GenericOperations op>
static void process_plane_5x5(uint8_t * VS_RESTRICT dstp8, const uint8_t * VS_RESTRICT srcp8, int width, int height, int stride, const GenericPlaneParams &params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    const PixelType *above2 = srcp - stride * 2;
    const PixelType *above1 = srcp - stride;
    const PixelType *below1 = srcp + stride;
    const PixelType *below2 = srcp + stride * 2;

    dstp[0] = generic_5x5<PixelType, op>(
            below2[2], below2[1], below2[0], below2[1], below2[2],
            below1[2], below1[1], below1[0], below1[1], below1[2],
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
            below1[2], below1[1], below1[0], below1[1], below1[2],
            below2[2], below2[1], below2[0], below2[1], below2[2], params);

    dstp[1] = generic_5x5<PixelType, op>(
            below2[1], below2[0], below2[1], below2[2], below2[3],
            below1[1], below1[0], below1[1], below1[2], below1[3],
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
            below1[1], below1[0], below1[1], below1[2], below1[3],
            below2[1], below2[0], below2[1], below2[2], below2[3], params);

    for (int x = 2; x < width - 2; x++)
        dstp[x] = generic_5x5<PixelType, op>(
                below2[x-2], below2[x-1], below2[x], below2[x+1], below2[x+2],
                below1[x-2], below1[x-1], below1[x], below1[x+1], below1[x+2],
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                below1[x-2], below1[x-1], below1[x], below1[x+1], below1[x+2],
                below2[x-2], below2[x-1], below2[x], below2[x+1], below2[x+2], params);

    dstp[width-2] = generic_5x5<PixelType, op>(
            below2[width-4], below2[width-3], below2[width-2], below2[width-1], below2[width-2],
            below1[width-4], below1[width-3], below1[width-2], below1[width-1], below1[width-2],
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
            below1[width-4], below1[width-3], below1[width-2], below1[width-1], below1[width-2],
            below2[width-4], below2[width-3], below2[width-2], below2[width-1], below2[width-2], params);

    dstp[width-1] = generic_5x5<PixelType, op>(
            below2[width-3], below2[width-2], below2[width-1], below2[width-2], below2[width-3],
            below1[width-3], below1[width-2], below1[width-1], below1[width-2], below1[width-3],
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
            below1[width-3], below1[width-2], below1[width-1], below1[width-2], below1[width-3],
            below2[width-3], below2[width-2], below2[width-1], below2[width-2], below2[width-3], params);

    srcp += stride;
    dstp += stride;
    above2 += stride;
    above1 += stride;
    below1 += stride;
    below2 += stride;

    dstp[0] = generic_5x5<PixelType, op>(
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
            above1[2], above1[1], above1[0], above1[1], above1[2],
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
            below1[2], below1[1], below1[0], below1[1], below1[2],
            below2[2], below2[1], below2[0], below2[1], below2[2], params);

    dstp[1] = generic_5x5<PixelType, op>(
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
            above1[1], above1[0], above1[1], above1[2], above1[3],
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
            below1[1], below1[0], below1[1], below1[2], below1[3],
            below2[1], below2[0], below2[1], below2[2], below2[3], params);

    for (int x = 2; x < width - 2; x++)
        dstp[x] = generic_5x5<PixelType, op>(
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                above1[x-2], above1[x-1], above1[x], above1[x+1], above1[x+2],
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                below1[x-2], below1[x-1], below1[x], below1[x+1], below1[x+2],
                below2[x-2], below2[x-1], below2[x], below2[x+1], below2[x+2], params);

    dstp[width-2] = generic_5x5<PixelType, op>(
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
            above1[width-4], above1[width-3], above1[width-2], above1[width-1], above1[width-2],
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
            below1[width-4], below1[width-3], below1[width-2], below1[width-1], below1[width-2],
            below2[width-4], below2[width-3], below2[width-2], below2[width-1], below2[width-2], params);

    dstp[width-1] = generic_5x5<PixelType, op>(
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
            above1[width-3], above1[width-2], above1[width-1], above1[width-2], above1[width-3],
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
            below1[width-3], below1[width-2], below1[width-1], below1[width-2], below1[width-3],
            below2[width-3], below2[width-2], below2[width-1], below2[width-2], below2[width-3], params);

    srcp += stride;
    dstp += stride;
    above2 += stride;
    above1 += stride;
    below1 += stride;
    below2 += stride;

    for (int y = 2; y < height - 2; y++) {
        dstp[0] = generic_5x5<PixelType, op>(
                above2[2], above2[1], above2[0], above2[1], above2[2],
                above1[2], above1[1], above1[0], above1[1], above1[2],
                  srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
                below1[2], below1[1], below1[0], below1[1], below1[2],
                below2[2], below2[1], below2[0], below2[1], below2[2], params);

        dstp[1] = generic_5x5<PixelType, op>(
                above2[1], above2[0], above2[1], above2[2], above2[3],
                above1[1], above1[0], above1[1], above1[2], above1[3],
                  srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
                below1[1], below1[0], below1[1], below1[2], below1[3],
                below2[1], below2[0], below2[1], below2[2], below2[3], params);

        for (int x = 2; x < width - 2; x++)
            dstp[x] = generic_5x5<PixelType, op>(
                    above2[x-2], above2[x-1], above2[x], above2[x+1], above2[x+2],
                    above1[x-2], above1[x-1], above1[x], above1[x+1], above1[x+2],
                      srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                    below1[x-2], below1[x-1], below1[x], below1[x+1], below1[x+2],
                    below2[x-2], below2[x-1], below2[x], below2[x+1], below2[x+2], params);

        dstp[width-2] = generic_5x5<PixelType, op>(
                above2[width-4], above2[width-3], above2[width-2], above2[width-1], above2[width-2],
                above1[width-4], above1[width-3], above1[width-2], above1[width-1], above1[width-2],
                  srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
                below1[width-4], below1[width-3], below1[width-2], below1[width-1], below1[width-2],
                below2[width-4], below2[width-3], below2[width-2], below2[width-1], below2[width-2], params);

        dstp[width-1] = generic_5x5<PixelType, op>(
                above2[width-3], above2[width-2], above2[width-1], above2[width-2], above2[width-3],
                above1[width-3], above1[width-2], above1[width-1], above1[width-2], above1[width-3],
                  srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
                below1[width-3], below1[width-2], below1[width-1], below1[width-2], below1[width-3],
                below2[width-3], below2[width-2], below2[width-1], below2[width-2], below2[width-3], params);

        srcp += stride;
        dstp += stride;
        above2 += stride;
        above1 += stride;
        below1 += stride;
        below2 += stride;
    }

    dstp[0] = generic_5x5<PixelType, op>(
            above2[2], above2[1], above2[0], above2[1], above2[2],
            above1[2], above1[1], above1[0], above1[1], above1[2],
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
            below1[2], below1[1], below1[0], below1[1], below1[2],
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2], params);

    dstp[1] = generic_5x5<PixelType, op>(
            above2[1], above2[0], above2[1], above2[2], above2[3],
            above1[1], above1[0], above1[1], above1[2], above1[3],
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
            below1[1], below1[0], below1[1], below1[2], below1[3],
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3], params);

    for (int x = 2; x < width - 2; x++)
        dstp[x] = generic_5x5<PixelType, op>(
                above2[x-2], above2[x-1], above2[x], above2[x+1], above2[x+2],
                above1[x-2], above1[x-1], above1[x], above1[x+1], above1[x+2],
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                below1[x-2], below1[x-1], below1[x], below1[x+1], below1[x+2],
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2], params);

    dstp[width-2] = generic_5x5<PixelType, op>(
            above2[width-4], above2[width-3], above2[width-2], above2[width-1], above2[width-2],
            above1[width-4], above1[width-3], above1[width-2], above1[width-1], above1[width-2],
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
            below1[width-4], below1[width-3], below1[width-2], below1[width-1], below1[width-2],
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2], params);

    dstp[width-1] = generic_5x5<PixelType, op>(
            above2[width-3], above2[width-2], above2[width-1], above2[width-2], above2[width-3],
            above1[width-3], above1[width-2], above1[width-1], above1[width-2], above1[width-3],
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
            below1[width-3], below1[width-2], below1[width-1], below1[width-2], below1[width-3],
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3], params);

    srcp += stride;
    dstp += stride;
    above2 += stride;
    above1 += stride;
    below1 += stride;
    below2 += stride;

    dstp[0] = generic_5x5<PixelType, op>(
            above2[2], above2[1], above2[0], above2[1], above2[2],
            above1[2], above1[1], above1[0], above1[1], above1[2],
              srcp[2],   srcp[1],   srcp[0],   srcp[1],   srcp[2],
            above1[2], above1[1], above1[0], above1[1], above1[2],
            above2[2], above2[1], above2[0], above2[1], above2[2], params);

    dstp[1] = generic_5x5<PixelType, op>(
            above2[1], above2[0], above2[1], above2[2], above2[3],
            above1[1], above1[0], above1[1], above1[2], above1[3],
              srcp[1],   srcp[0],   srcp[1],   srcp[2],   srcp[3],
            above1[1], above1[0], above1[1], above1[2], above1[3],
            above2[1], above2[0], above2[1], above2[2], above2[3], params);

    for (int x = 2; x < width - 2; x++)
        dstp[x] = generic_5x5<PixelType, op>(
                above2[x-2], above2[x-1], above2[x], above2[x+1], above2[x+2],
                above1[x-2], above1[x-1], above1[x], above1[x+1], above1[x+2],
                  srcp[x-2],   srcp[x-1],   srcp[x],   srcp[x+1],   srcp[x+2],
                above1[x-2], above1[x-1], above1[x], above1[x+1], above1[x+2],
                above2[x-2], above2[x-1], above2[x], above2[x+1], above2[x+2], params);

    dstp[width-2] = generic_5x5<PixelType, op>(
            above2[width-4], above2[width-3], above2[width-2], above2[width-1], above2[width-2],
            above1[width-4], above1[width-3], above1[width-2], above1[width-1], above1[width-2],
              srcp[width-4],   srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],
            above1[width-4], above1[width-3], above1[width-2], above1[width-1], above1[width-2],
            above2[width-4], above2[width-3], above2[width-2], above2[width-1], above2[width-2], params);

    dstp[width-1] = generic_5x5<PixelType, op>(
            above2[width-3], above2[width-2], above2[width-1], above2[width-2], above2[width-3],
            above1[width-3], above1[width-2], above1[width-1], above1[width-2], above1[width-3],
              srcp[width-3],   srcp[width-2],   srcp[width-1],   srcp[width-2],   srcp[width-3],
            above1[width-3], above1[width-2], above1[width-1], above1[width-2], above1[width-3],
            above2[width-3], above2[width-2], above2[width-1], above2[width-2], above2[width-3], params);
}

template <GenericOperations op>
static const VSFrameRef *VS_CC genericGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GenericData *d = static_cast<GenericData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(src);

        try {
            sharedFormatCheck(fi);
            if (vsapi->getFrameWidth(src, fi->numPlanes - 1) < 4 || vsapi->getFrameHeight(src, fi->numPlanes - 1) < 4)
                throw std::string("Cannot process frames with subsampled planes smaller than 4x4.");

        } catch (std::string &error) {
            vsapi->setFilterError(std::string(d->filter_name).append(": ").append(error).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return 0;
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrameRef *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        void(*process_plane)(uint8_t * VS_RESTRICT dstp8, const uint8_t * VS_RESTRICT srcp8, int width, int height, int stride, const GenericPlaneParams &params) = nullptr;

        int bytes = fi->bytesPerSample;
        bool defaultProcess = true;

#ifdef VS_TARGET_CPU_X86
        void(*process_plane_fast)(const uint8_t * VS_RESTRICT src, uint8_t * VS_RESTRICT dst, const ptrdiff_t stride, const unsigned width, const int height, int plane, const VSFormat *fi, const GenericData *data) = nullptr;

        if (vsapi->getFrameWidth(src, fi->numPlanes - 1) < 17) {
            // don't use optimized versions when too small width
        } else if (op == GenericConvolution && d->convolution_type == ConvolutionHorizontal && d->matrix_elements == 3) {
            //rewrite to 3x3 matrix here
            d->convolution_type = ConvolutionSquare;
            d->matrix_elements = 9;
            d->matrix[3] = d->matrix[0];
            d->matrix[4] = d->matrix[1];
            d->matrix[5] = d->matrix[2];
            d->matrix[0] = 0;
            d->matrix[1] = 0;
            d->matrix[2] = 0;
            d->matrix[6] = 0;
            d->matrix[7] = 0;
            d->matrix[8] = 0;
            d->matrixf[3] = d->matrixf[0];
            d->matrixf[4] = d->matrixf[1];
            d->matrixf[5] = d->matrixf[2];
            d->matrixf[0] = 0.f;
            d->matrixf[1] = 0.f;
            d->matrixf[2] = 0.f;
            d->matrixf[6] = 0.f;
            d->matrixf[7] = 0.f;
            d->matrixf[8] = 0.f;
        } else if (op == GenericConvolution && d->convolution_type == ConvolutionVertical && d->matrix_elements == 3) {
            //rewrite to 3x3 matrix here
            d->convolution_type = ConvolutionSquare;
            d->matrix_elements = 9;
            d->matrix[7] = d->matrix[2];
            d->matrix[5] = d->matrix[1];
            d->matrix[1] = d->matrix[0];
            d->matrix[0] = 0;
            d->matrix[2] = 0;
            d->matrix[3] = 0;
            d->matrix[4] = 0;
            d->matrix[6] = 0;
            d->matrix[8] = 0;
            d->matrixf[7] = d->matrixf[2];
            d->matrixf[5] = d->matrixf[1];
            d->matrixf[1] = d->matrixf[0];
            d->matrixf[0] = 0.f;
            d->matrixf[2] = 0.f;
            d->matrixf[3] = 0.f;
            d->matrixf[4] = 0.f;
            d->matrixf[6] = 0.f;
            d->matrixf[8] = 0.f;
        }

        if (op == GenericConvolution && d->convolution_type == ConvolutionSquare && d->matrix_elements == 9) {
            if (bytes == 1)
                process_plane_fast = filterPlane<uint8_t, Convolution3x3>;
            else if (bytes == 2)
                process_plane_fast = filterPlane<uint16_t, Convolution3x3>;
            else
                process_plane_fast = filterPlane<float, Convolution3x3>;
        } else if (op == GenericInflate) {
            if (bytes == 1)
                process_plane_fast = filterPlane<uint8_t, Inflate>;
            else if (bytes == 2)
                process_plane_fast = filterPlane<uint16_t, Inflate>;
            else
                process_plane_fast = filterPlane<float, Inflate>;
        } else if (op == GenericDeflate) {
            if (bytes == 1)
                process_plane_fast = filterPlane<uint8_t, Deflate>;
            else if (bytes == 2)
                process_plane_fast = filterPlane<uint16_t, Deflate>;
            else
                process_plane_fast = filterPlane<float, Deflate>;
        } else if (op == GenericMedian) {
            if (bytes == 1)
                process_plane_fast = filterPlane<uint8_t, Median>;
            else if (bytes == 2)
                process_plane_fast = filterPlane<uint16_t, Median>;
            else
                process_plane_fast = filterPlane<float, Median>;
        } else if (op == GenericMaximum) {
            if (d->pattern == 1) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MaxOpReduceAll>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MaxOpReduceAll>;
                else
                    process_plane_fast = filterPlane<float, MaxOpReduceAll>;
            } else if (d->pattern == 2) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MaxOpReducePlus>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MaxOpReducePlus>;
                else
                    process_plane_fast = filterPlane<float, MaxOpReducePlus>;
            } else if (d->pattern == 3) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MaxOpReduceVertical>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MaxOpReduceVertical>;
                else
                    process_plane_fast = filterPlane<float, MaxOpReduceVertical>;
            } else if (d->pattern == 4) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MaxOpReduceHorizontal>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MaxOpReduceHorizontal>;
                else
                    process_plane_fast = filterPlane<float, MaxOpReduceHorizontal>;
            }
        } else if (op == GenericMinimum) {
            if (d->pattern == 1) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MinOpReduceAll>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MinOpReduceAll>;
                else
                    process_plane_fast = filterPlane<float, MinOpReduceAll>;
            } else if (d->pattern == 2) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MinOpReducePlus>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MinOpReducePlus>;
                else
                    process_plane_fast = filterPlane<float, MinOpReducePlus>;
            } else if (d->pattern == 3) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MinOpReduceVertical>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MinOpReduceVertical>;
                else
                    process_plane_fast = filterPlane<float, MinOpReduceVertical>;
            } else if (d->pattern == 4) {
                if (bytes == 1)
                    process_plane_fast = filterPlane<uint8_t, MinOpReduceHorizontal>;
                else if (bytes == 2)
                    process_plane_fast = filterPlane<uint16_t, MinOpReduceHorizontal>;
                else
                    process_plane_fast = filterPlane<float, MinOpReduceHorizontal>;
            }
        }
        
        defaultProcess = !process_plane_fast;

        if (process_plane_fast) {

            for (int plane = 0; plane < fi->numPlanes; plane++) {
                if (d->process[plane]) {
                    uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int width = vsapi->getFrameWidth(src, plane);
                    int height = vsapi->getFrameHeight(src, plane);
                    int stride = vsapi->getStride(src, plane);
                    process_plane_fast(srcp, dstp, stride, width, height, plane, fi, d);
                }
            }
        }
#endif
        if (defaultProcess) {
            if (op == GenericConvolution && d->matrix_elements == 25) {
                if (bytes == 1)
                    process_plane = process_plane_5x5<uint8_t, op>;
                else if (bytes == 2)
                    process_plane = process_plane_5x5<uint16_t, op>;
                else
                    process_plane = process_plane_5x5<float, op>;
            } else if (op == GenericConvolution && d->convolution_type == ConvolutionHorizontal) {
                if (bytes == 1)
                    process_plane = process_plane_convolution_horizontal<uint8_t>;
                else if (bytes == 2)
                    process_plane = process_plane_convolution_horizontal<uint16_t>;
                else
                    process_plane = process_plane_convolution_horizontal<float>;
            } else if (op == GenericConvolution && d->convolution_type == ConvolutionVertical) {
                if (bytes == 1)
                    process_plane = process_plane_convolution_vertical<uint8_t>;
                else if (bytes == 2)
                    process_plane = process_plane_convolution_vertical<uint16_t>;
                else
                    process_plane = process_plane_convolution_vertical<float>;
            } else if (op == GenericMinimum ||
                op == GenericMaximum ||
                op == GenericMedian ||
                op == GenericDeflate ||
                op == GenericInflate ||
                op == GenericConvolution ||
                op == GenericPrewitt ||
                op == GenericSobel) {

                if (bytes == 1)
                    process_plane = process_plane_3x3<uint8_t, op>;
                else if (bytes == 2)
                    process_plane = process_plane_3x3<uint16_t, op>;
                else
                    process_plane = process_plane_3x3<float, op>;
            }

            for (int plane = 0; plane < fi->numPlanes; plane++) {
                if (d->process[plane]) {
                    uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                    int width = vsapi->getFrameWidth(src, plane);
                    int height = vsapi->getFrameHeight(src, plane);
                    int stride = vsapi->getStride(src, plane);

                    GenericPlaneParams planeParams(d, fi, plane);
                    process_plane(dstp, srcp, width, height, stride, planeParams);
                }
            }
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

template <GenericOperations op>
static void VS_CC genericCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<GenericData> d (new GenericData);
    *d = {};

    d->filter_name = static_cast<const char *>(userData);

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    try {
        if (!d->vi->format)
            throw std::string("cannot process variable format input.");

        sharedFormatCheck(d->vi->format);

        if (d->vi->height && d->vi->width)
            if (planeWidth(d->vi, d->vi->format->numPlanes - 1) < 4 || planeHeight(d->vi, d->vi->format->numPlanes - 1) < 4)
                throw std::string("Cannot process frames with subsampled planes smaller than 4x4.");

        int m = vsapi->propNumElements(in, "planes");

        for (int i = 0; i < 3; i++)
            d->process[i] = (m <= 0);

        for (int i = 0; i < m; i++) {
            int o = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

            if (o < 0 || o >= 3)
                throw std::string("plane index out of range");

            if (d->process[o])
                throw std::string("plane specified twice");

            d->process[o] = true;
        }

        int err;

        if (op == GenericMinimum || op == GenericMaximum || op == GenericDeflate || op == GenericInflate) {
            d->thf = static_cast<float>(vsapi->propGetFloat(in, "threshold", 0, &err));
            if (err) {
                d->th = ((1 << d->vi->format->bitsPerSample) - 1);
                d->thf = std::numeric_limits<float>::max();
            } else {
                if (d->vi->format->sampleType == stInteger) {
                    int64_t ith = floatToInt64S(d->thf);
                    if (ith < 0 || ith > ((1 << d->vi->format->bitsPerSample) - 1))
                        throw std::string("threshold bigger than sample value.");
                    d->th = static_cast<uint16_t>(ith);
                } else {
                    if (d->thf < 0)
                        throw std::string("threshold must be a positive value.");
                }
            }
        }

        if (op == GenericMinimum || op == GenericMaximum) {
            d->pattern = 0;
            int enable_elements = vsapi->propNumElements(in, "coordinates");
            if (enable_elements == -1) {
                for (int i = 0; i < 8; i++)
                    d->enable[i] = true;
                d->pattern = 1;
            } else if (enable_elements == 8) {
                const int64_t *enable = vsapi->propGetIntArray(in, "coordinates", &err);
                for (int i = 0; i < 8; i++)
                    d->enable[i] = !!enable[i];

                bool allenable[] = { true, true, true, true, true, true, true, true };
                bool plusenable[] = { false, true, false, true, true, false, true, false };
                bool venable[] = { false, true, false, false, false, false, true, false };
                bool henable[] = { false, false, false, true, true, false, false, false };

                if (!memcmp(allenable, d->enable, sizeof(d->enable)))
                    d->pattern = 1;
                else if (!memcmp(plusenable, d->enable, sizeof(d->enable)))
                    d->pattern = 2;
                else if (!memcmp(venable, d->enable, sizeof(d->enable)))
                    d->pattern = 3;
                else if (!memcmp(henable, d->enable, sizeof(d->enable)))
                    d->pattern = 4;
            } else {
                throw std::string("coordinates must contain exactly 8 numbers.");
            }
        }


        if (op == GenericPrewitt || op == GenericSobel) {
            d->thresh_low = static_cast<float>(vsapi->propGetFloat(in, "min", 0, &err));
            d->thresh_high = static_cast<float>(vsapi->propGetFloat(in, "max", 0, &err));
            if (err)
                d->thresh_high = (d->vi->format->sampleType == stInteger) ? ((1 << d->vi->format->bitsPerSample) - 1) : 1.0f;

            if (d->thresh_low < 0)
                throw std::string("min must be a positive number.");

            if (d->thresh_high < 0)
                throw std::string("max must be a positive number.");

            d->rshift = int64ToIntS(vsapi->propGetInt(in, "rshift", 0, &err));

            if (d->rshift < 0)
                throw std::string("rshift must not be negative.");
        }

        if (op == GenericConvolution) {
            d->bias = static_cast<float>(vsapi->propGetFloat(in, "bias", 0, &err));

            d->saturate = !!vsapi->propGetInt(in, "saturate", 0, &err);
            if (err)
                d->saturate = true;

            d->matrix_elements = vsapi->propNumElements(in, "matrix");

            const char *mode = vsapi->propGetData(in, "mode", 0, &err);
            if (err || mode[0] == 's') {
                d->convolution_type = ConvolutionSquare;

                if (d->matrix_elements != 9 && d->matrix_elements != 25)
                    throw std::string("When mode starts with 's', matrix must contain exactly 9 or exactly 25 numbers.");
            } else if (mode[0] == 'h' || mode[0] == 'v') {
                if (mode[0] == 'h')
                    d->convolution_type = ConvolutionHorizontal;
                else
                    d->convolution_type = ConvolutionVertical;

                if (d->matrix_elements < 3 || d->matrix_elements > 17)
                    throw std::string("When mode starts with 'h' or 'v', matrix must contain between 3 and 17 numbers.");

                if (d->matrix_elements % 2 == 0)
                    throw std::string("matrix must contain an odd number of numbers.");
            } else {
                throw std::string("mode must start with 's', 'h', or 'v'.");
            }

            float matrix_sumf = 0;
            d->matrix_sum = 0;
            const double *matrix = vsapi->propGetFloatArray(in, "matrix", nullptr);
            for (int i = 0; i < d->matrix_elements; i++) {
                if (d->vi->format->sampleType == stInteger) {
                    d->matrix[i] = lround(matrix[i]);
                    d->matrixf[i] = d->matrix[i];
                    if (std::abs(d->matrix[i]) > 1023)
                        throw std::string("coefficients may only be between -1023 and 1023");
                } else {
                    d->matrix[i] = lround(matrix[i]);
                    d->matrixf[i] = static_cast<float>(matrix[i]);
                }

                matrix_sumf += d->matrixf[i];
                d->matrix_sum += d->matrix[i];
            }
            
            if (std::abs(matrix_sumf) < std::numeric_limits<float>::epsilon())
                matrix_sumf = 1.0;

            d->rdiv = static_cast<float>(vsapi->propGetFloat(in, "divisor", 0, &err));
            if (d->rdiv == 0.0f)
                d->rdiv = static_cast<float>(matrix_sumf);

            d->rdiv = 1.0f / d->rdiv;
        }
   
    } catch (std::string &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, std::string(d->filter_name).append(": ").append(error).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->filter_name, templateClipInit<GenericData>, genericGetframe<op>, templateClipFree<GenericData>, fmParallel, 0, d.get(), core);
    d.release();
}

///////////////////////////////


struct InvertData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
};

struct InvertOp {
    uint16_t max;
    bool uv;

    InvertOp(InvertData *d, const VSFormat *fi, int plane) {
        uv = ((fi->colorFamily == cmYUV) || (fi->colorFamily == cmYCoCg)) && (plane > 0);
        max = (1LL << fi->bitsPerSample) - 1;
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const InvertOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = static_cast<T>(opts.max) - std::min(src[w], static_cast<T>(opts.max));
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const InvertOp &opts) {
        if (opts.uv) {
            for (unsigned w = 0; w < width; w++)
                dst[w] = -src[w];
        } else {
            for (unsigned w = 0; w < width; w++)
                dst[w] = 1 - src[w];
        }
    }
};

static void VS_CC invertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<InvertData> d(new InvertData);

    try {
        templateInit(d, "Invert", true, in, out, vsapi);
    } catch (std::string &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, std::string(d->name).append(": ").append(error).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateClipInit<InvertData>, singlePixelGetFrame<InvertData, InvertOp>, templateClipFree<InvertData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct LimitData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t max[3], min[3];
    float maxf[3], minf[3];
};

struct LimitOp {
    uint16_t max, min;
    float maxf, minf;

    LimitOp(LimitData *d, const VSFormat *fi, int plane) {
        max = d->max[plane];
        min = d->min[plane];
        maxf = d->maxf[plane];
        minf = d->minf[plane];
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LimitOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = std::min(static_cast<T>(opts.max), std::max(static_cast<T>(opts.min), src[w]));
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LimitOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = std::min(opts.maxf, std::max(opts.minf, src[w]));
    }
};

static void VS_CC limitCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LimitData> d(new LimitData);

    try {
        templateInit(d, "Limiter", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "min", d->min, d->minf, RangeLower, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "max", d->max, d->maxf, RangeUpper, vsapi);
        for (int i = 0; i < 3; i++)
            if (((d->vi->format->sampleType == stInteger) && (d->min[i] > d->max[i])) || ((d->vi->format->sampleType == stFloat) && (d->minf[i] > d->maxf[i])))
                throw std::string("min bigger than max");
    } catch (std::string &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, std::string(d->name).append(": ").append(error).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateClipInit<LimitData>, singlePixelGetFrame<LimitData, LimitOp>, templateClipFree<LimitData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct BinarizeData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t v0[3], v1[3], thr[3];
    float v0f[3], v1f[3], thrf[3];
};

struct BinarizeOp {
    uint16_t v0, v1, thr;
    float v0f, v1f, thrf;

    BinarizeOp(BinarizeData *d, const VSFormat *fi, int plane) {
        v0 = d->v0[plane];
        v1 = d->v1[plane];
        v0f = d->v0f[plane];
        v1f = d->v1f[plane];
        thr = d->thr[plane];
        thrf = d->thrf[plane];
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const BinarizeOp &opts) {
        for (unsigned w = 0; w < width; w++)
            if (src[w] < opts.thr)
                dst[w] = static_cast<T>(opts.v0);
            else
                dst[w] = static_cast<T>(opts.v1);
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const BinarizeOp &opts) {
        for (unsigned w = 0; w < width; w++)
            if (src[w] < opts.thrf)
                dst[w] = opts.v0f;
            else
                dst[w] = opts.v1f;
    }
};

static void VS_CC binarizeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BinarizeData> d(new BinarizeData);

    try {
        templateInit(d, "Binarize", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v0", d->v0, d->v0f, RangeLower, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v1", d->v1, d->v1f, RangeUpper, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "threshold", d->thr, d->thrf, RangeMiddle, vsapi);
    } catch (std::string &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, std::string(d->name).append(": ").append(error).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateClipInit<BinarizeData>, singlePixelGetFrame<BinarizeData, BinarizeOp>, templateClipFree<BinarizeData>, fmParallel, 0, d.get(), core);
    d.release();
}

/////////////////

struct LevelsData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    double gamma[3];
    uint16_t max_in[3], max_out[3], min_in[3], min_out[3];
    float max_inf[3], max_outf[3], min_inf[3], min_outf[3];
};

struct LevelsOp {
    float gamma;
    uint16_t range_in, range_out, min_in, min_out;
    float range_inf, range_outf, min_inf, min_outf;

    LevelsOp(LevelsData *d, const VSFormat *fi, int plane) {
        gamma = static_cast<float>(1.0 / d->gamma[plane]);
        range_in = d->max_in[plane] - d->min_in[plane];
        range_inf = d->max_inf[plane] - d->min_inf[plane];
        range_out = d->max_out[plane] - d->min_out[plane];
        range_outf = d->max_outf[plane] - d->min_outf[plane];
        min_in = d->min_in[plane];
        min_inf = d->min_inf[plane];
        min_out = d->min_out[plane];
        min_outf = d->min_outf[plane];
    }

    template<typename T>
    static FORCE_INLINE void processPlane(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LevelsOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = static_cast<T>(std::pow(static_cast<float>(src[w] - opts.min_in) / (opts.range_in), opts.gamma) * (opts.range_out) + opts.min_out + 0.5f);
    }

    template<typename T>
    static FORCE_INLINE void processPlaneF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, unsigned width, const LevelsOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = std::pow(static_cast<float>(src[w] - opts.min_inf) / (opts.range_inf), opts.gamma) * (opts.range_outf) + opts.min_outf;
    }
};

static void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LevelsData> d(new LevelsData);

    try {
        templateInit(d, "Levels", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "min_in", d->min_in, d->min_inf, RangeLower, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "min_out", d->min_out, d->min_outf, RangeLower, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "max_in", d->max_in, d->max_inf, RangeUpper, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "max_out", d->max_out, d->max_outf, RangeUpper, vsapi);
        getPlaneArgs(d->vi->format, in, "gamma", d->gamma, 1., vsapi);
    } catch (std::string &error) {
        vsapi->freeNode(d->node);
        vsapi->setError(out, std::string(d->name).append(": ").append(error).c_str());
        return;
    }

    vsapi->createFilter(in, out, d->name, templateClipInit<LevelsData>, singlePixelGetFrame<LevelsData, LevelsOp>, templateClipFree<LevelsData>, fmParallel, 0, d.get(), core);
    d.release();
}

//////////////////////////

void VS_CC genericInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Minimum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMinimum>, const_cast<char *>("Minimum"), plugin);

    registerFunc("Maximum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMaximum>, const_cast<char *>("Maximum"), plugin);

    registerFunc("Median",
            "clip:clip;"
            "planes:int[]:opt;"
            , genericCreate<GenericMedian>, const_cast<char *>("Median"), plugin);

    registerFunc("Deflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            , genericCreate<GenericDeflate>, const_cast<char *>("Deflate"), plugin);

    registerFunc("Inflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            , genericCreate<GenericInflate>, const_cast<char *>("Inflate"), plugin);

    registerFunc("Convolution",
            "clip:clip;"
            "matrix:float[];"
            "bias:float:opt;"
            "divisor:float:opt;"
            "planes:int[]:opt;"
            "saturate:int:opt;"
            "mode:data:opt;"
            , genericCreate<GenericConvolution>, const_cast<char *>("Convolution"), plugin);

    registerFunc("Prewitt",
            "clip:clip;"
            "min:float:opt;"
            "max:float:opt;"
            "planes:int[]:opt;"
            "rshift:int:opt;"
            , genericCreate<GenericPrewitt>, const_cast<char *>("Prewitt"), plugin);

    registerFunc("Sobel",
            "clip:clip;"
            "min:float:opt;"
            "max:float:opt;"
            "planes:int[]:opt;"
            "rshift:int:opt;"
            , genericCreate<GenericSobel>, const_cast<char *>("Sobel"), plugin);

    registerFunc("Invert",
        "clip:clip;"
        "planes:int[]:opt;"
        , invertCreate, nullptr, plugin);

    registerFunc("Limiter",
        "clip:clip;"
        "min:float[]:opt;"
        "max:float[]:opt;"
        "planes:int[]:opt;"
        , limitCreate, nullptr, plugin);

    registerFunc("Binarize",
        "clip:clip;"
        "threshold:float[]:opt;"
        "v0:float[]:opt;"
        "v1:float[]:opt;"
        "planes:int[]:opt;"
        , binarizeCreate, nullptr, plugin);

    registerFunc("Levels",
        "clip:clip;"
        "min_in:float[]:opt;"
        "max_in:float[]:opt;"
        "gamma:float[]:opt;"
        "min_out:float[]:opt;"
        "max_out:float[]:opt;"
        "planes:int[]:opt;"
        , levelsCreate, nullptr, plugin);
}
