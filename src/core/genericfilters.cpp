/*
* Copyright (c) 2015-2020 John Smith & Fredrik Mellbin
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
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "cpufeatures.h"
#include "filtershared.h"
#include "float16_helper.h"
#include "internalfilters.h"
#include "kernel/cpulevel.h"
#include "kernel/generic.h"

#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

using namespace std::string_literals;

enum RangeArgumentHandling {
    RangeLower,
    RangeUpper,
    RangeMiddle
};

static inline void getPlanePixelRangeArgs(const VSVideoFormat &fi, const VSMap *in, const char *propName, uint16_t *ival, float *fval, RangeArgumentHandling mode, bool mask, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(in, propName) > fi.numPlanes)
        throw std::runtime_error(std::string(propName) + " has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        bool uv = (!mask &&plane > 0 && fi.colorFamily == cfYUV);
        double temp = vsapi->mapGetFloat(in, propName, plane, &err);
        if (err) {
            if (prevValid) {
                ival[plane] = ival[plane - 1];
                fval[plane] = fval[plane - 1];
            } else if (mode == RangeLower) { // bottom of pixel range
                ival[plane] = 0;
                fval[plane] = uv ? -.5f : 0;
            } else if (mode == RangeUpper) { // top of pixel range
                ival[plane] = (1 << fi.bitsPerSample) - 1;
                fval[plane] = uv ? .5f : 1.f;
            } else if (mode == RangeMiddle) { // middle of pixel range
                ival[plane] = (1 << fi.bitsPerSample) / 2;
                fval[plane] = uv ? 0.f : .5f;
            }
        } else {
            if (fi.sampleType == stInteger) {
                int64_t temp2 = static_cast<int64_t>(temp + .5);
                if ((temp2 < 0) || (temp2 > (1 << fi.bitsPerSample) - 1))
                    throw std::runtime_error(std::string(propName) + " out of range");
                ival[plane] = static_cast<uint16_t>(temp2);
            } else {
                fval[plane] = static_cast<float>(temp);
            }
            prevValid = true;
        }
    }
}

static inline void getPlaneFloatArgs(const VSVideoFormat &fi, const VSMap *in, const char *propName, float *fval, float defval, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(in, propName) > fi.numPlanes)
        throw std::runtime_error(std::string(propName) + " has more values specified than there are planes");
    bool prevValid = false;
    for (int plane = 0; plane < 3; plane++) {
        int err;
        double temp = vsapi->mapGetFloat(in, propName, plane, &err);
        if (err) {
            fval[plane] = prevValid ? fval[plane - 1] : defval;
        } else {
            fval[plane] = static_cast<float>(temp);
            prevValid = true;
        }
    }
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
    ConvolutionVertical,
    ConvolutionSeparable
};

struct GenericDataExtra {
    const VSVideoInfo *vi;
    bool process[3];

    const char *filter_name;

    // Prewitt, Sobel
    float scale;

    // Minimum, Maximum, Deflate, Inflate
    uint16_t th;
    float thf;

    // Minimum, Maximum
    uint8_t enable;

    // Convolution
    ConvolutionTypes convolution_type;
    int matrix[121];
    float matrixf[121];
    int matrix_elements;
    float rdiv;
    float bias;
    bool saturate;
    bool conv_int8;   // all coefficients fit int8 -> byte square conv may take the VNNI path
    bool conv_f16;    // all coefficients survive a round trip through half -> SME FMOPA is lossless

    int cpulevel;

    void (*func)(const void *, ptrdiff_t, void *, ptrdiff_t, const vs_generic_params *, unsigned, unsigned);
    vs_generic_params params;
};

typedef SingleNodeData<GenericDataExtra> GenericData;

template<typename T, typename OP>
static const VSFrame *VS_CC singlePixelGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    T *d = reinterpret_cast<T *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);

        if (!is8to16orFloatFormat(*fi)) {
            vsapi->setFilterError(invalidVideoFormatMessage(*fi, vsapi, d->name, false, true).c_str(), frameCtx);
            vsapi->freeFrame(src);
            return nullptr;
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

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
                    else if (fi->sampleType == stInteger && fi->bytesPerSample == 2)
                        OP::template processPlane<uint16_t>(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, opts);
                    else if (fi->bytesPerSample == 2)
                        OP::processPlaneH(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, opts);
                    else if (fi->bytesPerSample == 4)
                        OP::template processPlaneF<float>(reinterpret_cast<const float *>(srcp), reinterpret_cast<float *>(dstp), width, opts);
                    srcp += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

template<typename T>
static void templateInit(T& d, const char *name, bool allowVariableFormat, const VSMap *in, VSMap *out, const VSAPI *vsapi) {
    d->name = name;
    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    if (!is8to16orFloatFormat(d->vi->format, allowVariableFormat))
        throw std::runtime_error(invalidVideoFormatMessage(d->vi->format, vsapi, nullptr, allowVariableFormat));

    getPlanesArg(in, d->process, vsapi);
}

vs_generic_params make_generic_params(const GenericData *d, const VSVideoFormat *fi) {
    vs_generic_params params{};

    params.maxval = ((1 << fi->bitsPerSample) - 1);
    params.scale = d->scale;
    params.threshold = d->th;
    params.thresholdf = d->thf;
    params.stencil = d->enable;

    for (int i = 0; i < d->matrix_elements; ++i) {
        params.matrix[i] = d->matrix[i];
        params.matrixf[i] = d->matrixf[i];
    }
    params.matrixsize = d->matrix_elements;

    params.div = d->rdiv;
    params.bias = d->bias;
    params.saturate = d->saturate;

    return params;
}

#ifdef VS_TARGET_CPU_X86
// The byte square 7x7..11x11 VNNI kernels are bit-exact fast paths, valid only when every
// coefficient fits int8 and the CPU reports both VNNI (vpdpbusd) and VBMI (vpermb window).
static bool convByteVNNI(const GenericData *d) {
    const CPUFeatures *cpu = getCPUFeatures();
    return d->conv_int8 && cpu->avx512_vnni && cpu->avx512_vbmi;
}

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectAVX512(const VSVideoFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_avx512;
        case GenericSobel: return vs_generic_3x3_sobel_byte_avx512;
        case GenericMinimum: return vs_generic_3x3_min_byte_avx512;
        case GenericMaximum: return vs_generic_3x3_max_byte_avx512;
        case GenericMedian: return vs_generic_3x3_median_byte_avx512;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_avx512;
        case GenericInflate: return vs_generic_3x3_inflate_byte_avx512;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return convByteVNNI(d) ? vs_generic_5x5_conv_byte_avx512vnni : vs_generic_5x5_conv_byte_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return convByteVNNI(d) ? vs_generic_7x7_conv_byte_avx512vnni : vs_generic_7x7_conv_byte_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return convByteVNNI(d) ? vs_generic_9x9_conv_byte_avx512vnni : vs_generic_9x9_conv_byte_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return convByteVNNI(d) ? vs_generic_11x11_conv_byte_avx512vnni : vs_generic_11x11_conv_byte_avx512;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_avx512;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_byte_avx512;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_byte_avx512;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_avx512;
        case GenericSobel: return vs_generic_3x3_sobel_word_avx512;
        case GenericMinimum: return vs_generic_3x3_min_word_avx512;
        case GenericMaximum: return vs_generic_3x3_max_word_avx512;
        case GenericMedian: return vs_generic_3x3_median_word_avx512;
        case GenericDeflate: return vs_generic_3x3_deflate_word_avx512;
        case GenericInflate: return vs_generic_3x3_inflate_word_avx512;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_word_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_word_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_word_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_word_avx512;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_word_avx512;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_word_avx512;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_word_avx512;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_avx512;
        case GenericSobel: return vs_generic_3x3_sobel_float_avx512;
        case GenericMinimum: return vs_generic_3x3_min_float_avx512;
        case GenericMaximum: return vs_generic_3x3_max_float_avx512;
        case GenericMedian: return vs_generic_3x3_median_float_avx512;
        case GenericDeflate: return vs_generic_3x3_deflate_float_avx512;
        case GenericInflate: return vs_generic_3x3_inflate_float_avx512;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_float_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_float_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_float_avx512;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_float_avx512;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_float_avx512;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_float_avx512;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_float_avx512;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_half_avx512;
        case GenericSobel: return vs_generic_3x3_sobel_half_avx512;
        case GenericMinimum: return vs_generic_3x3_min_half_avx512;
        case GenericMaximum: return vs_generic_3x3_max_half_avx512;
        case GenericMedian: return vs_generic_3x3_median_half_avx512;
        case GenericDeflate: return vs_generic_3x3_deflate_half_avx512;
        case GenericInflate: return vs_generic_3x3_inflate_half_avx512;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_half_avx512;
            break;
        }
    }
    return nullptr;
}

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectAVX2(const VSVideoFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_byte_avx2;
        case GenericMinimum: return vs_generic_3x3_min_byte_avx2;
        case GenericMaximum: return vs_generic_3x3_max_byte_avx2;
        case GenericMedian: return vs_generic_3x3_median_byte_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_byte_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_byte_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_byte_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_byte_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_byte_avx2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_avx2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_byte_avx2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_byte_avx2;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_word_avx2;
        case GenericMinimum: return vs_generic_3x3_min_word_avx2;
        case GenericMaximum: return vs_generic_3x3_max_word_avx2;
        case GenericMedian: return vs_generic_3x3_median_word_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_word_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_word_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_word_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_word_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_word_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_word_avx2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_word_avx2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_word_avx2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_word_avx2;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_float_avx2;
        case GenericMinimum: return vs_generic_3x3_min_float_avx2;
        case GenericMaximum: return vs_generic_3x3_max_float_avx2;
        case GenericMedian: return vs_generic_3x3_median_float_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_float_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_float_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_float_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_float_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_float_avx2;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_float_avx2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_float_avx2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_float_avx2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_float_avx2;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_half_avx2;
        case GenericSobel: return vs_generic_3x3_sobel_half_avx2;
        case GenericMinimum: return vs_generic_3x3_min_half_avx2;
        case GenericMaximum: return vs_generic_3x3_max_half_avx2;
        case GenericMedian: return vs_generic_3x3_median_half_avx2;
        case GenericDeflate: return vs_generic_3x3_deflate_half_avx2;
        case GenericInflate: return vs_generic_3x3_inflate_half_avx2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_half_avx2;
            break;
        }
    }
    return nullptr;
}

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectSSE2(const VSVideoFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_byte_sse2;
        case GenericMinimum: return vs_generic_3x3_min_byte_sse2;
        case GenericMaximum: return vs_generic_3x3_max_byte_sse2;
        case GenericMedian: return vs_generic_3x3_median_byte_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_byte_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_sse2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_sse2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_byte_sse2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_byte_sse2;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_word_sse2;
        case GenericMinimum: return vs_generic_3x3_min_word_sse2;
        case GenericMaximum: return vs_generic_3x3_max_word_sse2;
        case GenericMedian: return vs_generic_3x3_median_word_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_word_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_word_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_sse2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_word_sse2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_word_sse2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_word_sse2;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_sse2;
        case GenericSobel: return vs_generic_3x3_sobel_float_sse2;
        case GenericMinimum: return vs_generic_3x3_min_float_sse2;
        case GenericMaximum: return vs_generic_3x3_max_float_sse2;
        case GenericMedian: return vs_generic_3x3_median_float_sse2;
        case GenericDeflate: return vs_generic_3x3_deflate_float_sse2;
        case GenericInflate: return vs_generic_3x3_inflate_float_sse2;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_sse2;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_float_sse2;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_float_sse2;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_float_sse2;
            break;
        }
    }
    return nullptr;
}
#endif

#ifdef VS_TARGET_CPU_ARM64
// The byte square usdot kernels are bit-exact fast paths, valid only when every
// coefficient fits int8 and the CPU reports i8mm (the ARM analog of convByteVNNI).
// They are ~2-3.5x the vmlal kernels, which rewrites the SVE and SME byte square
// policies below -- but only where they actually apply, so wide-coefficient
// convolutions keep the old (still correct) tier choices.
#ifdef VS_TARGET_ARM_I8MM
static bool convByteDot(const GenericData *d) {
    return d->conv_int8 && getCPUFeatures()->i8mm;
}
#else
static bool convByteDot(const GenericData *) { return false; }
#endif

// Only the convolution family has hand-written ARM kernels; every other op
// falls through to the (auto-vectorised) C tier.
template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectNEON(const VSVideoFormat *fi, GenericData *d) {
    if (op != GenericConvolution)
        return nullptr;

    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
#ifdef VS_TARGET_ARM_I8MM
        // usdot folds 4 taps per op with no byte->word widening; bit-exact with
        // the vmlal kernels, but only valid when every coefficient fits int8.
        if (convByteDot(d)) {
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_neon_dot;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_byte_neon_dot;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_byte_neon_dot;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_byte_neon_dot;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_byte_neon_dot;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_neon_dot;
            // Separable runs its vertical half on vmlal and its horizontal half
            // on usdot; the vertical taps run across rows, so there is no
            // scanline window for dot to fold there.
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_byte_neon_dot;
        }
#endif
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
            return vs_generic_3x3_conv_byte_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
            return vs_generic_5x5_conv_byte_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_byte_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_byte_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_byte_neon;
        else if (d->convolution_type == ConvolutionHorizontal)
            return vs_generic_1d_conv_h_byte_neon;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_byte_neon;
        else if (d->convolution_type == ConvolutionSeparable)
            return vs_generic_2d_conv_sep_byte_neon;
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
            return vs_generic_3x3_conv_word_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
            return vs_generic_5x5_conv_word_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_word_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_word_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_word_neon;
        else if (d->convolution_type == ConvolutionHorizontal)
            return vs_generic_1d_conv_h_word_neon;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_word_neon;
        else if (d->convolution_type == ConvolutionSeparable)
            return vs_generic_2d_conv_sep_word_neon;
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        // 3x3 float has no NEON kernel: its C tier autovectorises to a tighter
        // loop (fewer vector ops/px) that NEON cannot beat at a full thread pool
        // on M4 (0.89x), and the single-thread win it did hold on Neoverse was
        // not worth carrying a shape that loses on the primary target. It falls
        // through to the C tier. (See notes/.../3x3-float-diagnosis.md.)
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
            return vs_generic_5x5_conv_float_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_float_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_float_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_float_neon;
        else if (d->convolution_type == ConvolutionHorizontal)
            return vs_generic_1d_conv_h_float_neon;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_float_neon;
        else if (d->convolution_type == ConvolutionSeparable)
            return vs_generic_2d_conv_sep_float_neon;
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 2) {
#ifdef VS_TARGET_ARM_FHM
        // fmlal folds the f16->f32 convert into the MAC; the baseline half
        // kernels are conversion-bound (3x3 only TIES C). Both fmlal operands
        // are f16, so the coefficients must narrow exactly -- the SME FMOPA
        // conv_f16 rule. Covers every half convolution shape: squares, and the
        // 1D horizontal/vertical/separable scanlines.
        if (d->conv_f16 && getCPUFeatures()->fhm) {
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_half_neon_fhm;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_half_neon_fhm;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_half_neon_fhm;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_half_neon_fhm;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_half_neon_fhm;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_half_neon_fhm;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_half_neon_fhm;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_half_neon_fhm;
        }
#endif
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
            return vs_generic_3x3_conv_half_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
            return vs_generic_5x5_conv_half_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_half_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_half_neon;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_half_neon;
        else if (d->convolution_type == ConvolutionHorizontal)
            return vs_generic_1d_conv_h_half_neon;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_half_neon;
        else if (d->convolution_type == ConvolutionSeparable)
            return vs_generic_2d_conv_sep_half_neon;
    }
    return nullptr;
}

#ifdef VS_TARGET_ARM_SVE
// The SVE kernels run at 32-bit lane density, so they only beat the NEON
// kernels when vectors are wider than NEON's 128 bits (Graviton4/Neoverse V2
// runs SVE2 at 128 bits and loses everywhere; Graviton3 at 256 bits wins the
// shapes below). Within a wide-vector machine the winners measured on
// Graviton3 are: integer squares (except word 7x7), float squares >= 9x9,
// byte horizontal/separable, and word 3x3; vertical 1D and the small float
// kernels stay on NEON.
template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectSVE(const VSVideoFormat *fi, GenericData *d) {
    if (op != GenericConvolution)
        return nullptr;

    if (vs_sve_vector_length() <= 16)
        return nullptr;

    // svdot_s64 word squares. SDOT folds 4 int16 products into a 64-bit lane, so
    // its MAC density beats vmlal_s16 at any vector length -- but it accumulates
    // into 64-bit lanes, so the scale/bias/round/clamp/narrow store is amortised
    // over only svcntd() outputs (2 at a 128-bit VL, 4 at 256). That store cost is
    // fixed per output and swamps the MAC saving unless the tap count is high,
    // which is why this pays only from 7x7 up, and only on wide vectors:
    //
    //   Graviton3 (256-bit) vs NEON:  7x7 +17%, 9x9 +39%, 11x11 +57% (1 thread)
    //                                 3x3 -19%, 5x5 -14%   -> left on the old kernels
    //   Graviton4 (128-bit) vs NEON:  loses everywhere except 11x11 (parity), so
    //                                 the VL gate above still sends it to NEON.
    // 7x7 was a wash (won single-thread, lost contended) once the round-2 NEON
    // lane-coefficient word squares landed, so it is pruned -> NEON; 9x9/11x11
    // still win at every pool size.
    if (fi->sampleType == stInteger && fi->bytesPerSample == 2 && d->convolution_type == ConvolutionSquare) {
        if (d->matrix_elements == 81)
            return vs_generic_9x9_conv_word_sve_dot;
        else if (d->matrix_elements == 121)
            return vs_generic_11x11_conv_word_sve_dot;
    }

    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        // Where the usdot kernels apply they beat the lane-density SVE squares by
        // ~2x, so those step aside. SVE has its own usdot kernels (2x the outputs
        // per op at a 256-bit VL); prefer them, and otherwise fall through to the
        // NEON tier. 3x3 has no SVE usdot kernel, but the NEON dot 3x3 beats the
        // plain 256-bit SVE kernel anyway (804 vs 609 fps on Graviton3), so it
        // yields through the nullptr below like everything else.
        if (convByteDot(d) && d->convolution_type == ConvolutionSquare) {
#ifdef VS_TARGET_ARM_SVE_I8MM
            // A thread-count sweep on Graviton3 (SVE/NEON ratio, raw curves in
            // bench/logs/sve_usdot_thread_sweep_g3.log) split the shapes: 7x7 and
            // 9x9 win at every pool size (7x7 1.23-1.48x, 9x9 1.10-1.24x, only 9x9
            // dips to ~0.97 at 16 which is within noise) and take SVE unconditionally.
            // 5x5 and 11x11 won only while nearly unloaded (5x5 tie by 4t, 11x11 a
            // loss by 4t), so they were thread-gated -- and have now been pruned:
            // int8 5x5/11x11 fall through to the NEON usdot squares, which match SVE
            // at a full pool anyway. usdot has no 3x3 SVE kernel either (NEON dot
            // wins there, 804 vs 609 fps on Graviton3), so it also yields below.
            if (d->matrix_elements == 49)
                return vs_generic_7x7_conv_byte_sve_dot;
            else if (d->matrix_elements == 81)
                return vs_generic_9x9_conv_byte_sve_dot;
#endif
            return nullptr;
        }

        // wide-coefficient byte squares: SVE lane density wins through 7x7; 9x9
        // and 11x11 lost to the round-2 NEON lane-coefficient squares and are
        // pruned -> NEON. The byte separable path is pruned for the same reason
        // (the round-2 usdot NEON separable wins).
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
            return vs_generic_3x3_conv_byte_sve;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
            return vs_generic_5x5_conv_byte_sve;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_byte_sve;
        else if (d->convolution_type == ConvolutionHorizontal)
            // The NEON usdot h kernel beats the 256-bit SVE one on Graviton3
            // (388 vs 215 fps, h25 byte int8 1080p t1) -- yield to it when it
            // applies. Revisit if wider-than-256 SVE hardware ever ships.
            return convByteDot(d) ? nullptr : vs_generic_1d_conv_h_byte_sve;
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        // word 5x5 lost to the round-2 NEON word squares and is pruned; 3x3 still
        // wins. (9x9/11x11 are served by the svdot block above.)
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
            return vs_generic_3x3_conv_word_sve;
    }
    // float SVE squares (9x9/11x11) lost to the round-2 NEON lane-coefficient
    // float squares on Graviton3 (the only SVE-conv machine), so they are pruned
    // and float falls through to NEON.
    return nullptr;
}
#endif // VS_TARGET_ARM_SVE

#ifdef VS_TARGET_ARM_SME
// SME covers the compute-dense shapes where the outer-product unit pays off:
// the larger square NxN convolutions and vertical 1D.
//
// The SME unit is shared per core cluster, so with a full thread pool the
// aggregate throughput of the cheaper shapes is better on NEON even though SME
// wins them decisively single-threaded (measured on M4 Max: 5x5 byte 4.1x
// single-thread vs 0.63x at 16 threads). Those thread-contested shapes were
// only ever dispatched on tiny thread pools, which are false by default on the
// only hardware that has SME (hardware_concurrency() is 10-16 on an M4), so
// they have been pruned entirely and fall back to the NEON tier -- SME only
// keeps the shapes that win even fully contended: the >= 7x7 squares and the
// vertical 1D kernels. Byte squares here serve WIDE coefficients only; int8
// coefficients go to the NEON usdot kernels, which win outright.
template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectSME(const VSVideoFormat *fi, GenericData *d) {
    if (op != GenericConvolution)
        return nullptr;

    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        // int8 coefficients: NEON usdot wins every byte square outright (it scales
        // per core while SME is a shared per-cluster unit), so yield the squares to
        // it. The SME byte squares below exist for WIDE coefficients only. Vertical
        // 1D still takes SME for both coefficient kinds (usdot has no vertical form).
        if (convByteDot(d) && d->convolution_type == ConvolutionSquare)
            return nullptr;

        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_byte_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_byte_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_byte_sme;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_byte_sme;
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 2) {
        // Native 2-way f16 FMOPA: one outer product folds two source rows, and the
        // pixels are consumed as f16 with no widening load. It wins the >= 7x7
        // squares and the vertical kernel on a full pool; 3x3/5x5 half are pruned
        // (break-even to thread-contested) and fall back to NEON.
        //
        // FMOPA takes both operands in f16, so it is only correct to use where the
        // coefficients survive that narrowing exactly (conv_f16) -- the same rule
        // the VNNI byte path follows with conv_int8. Otherwise the coefficients
        // would be silently degraded, which no other tier does, so those clips stay
        // on NEON and its float32 coefficients. In practice the usual matrices are
        // integers or dyadic fractions and pass the gate.
        if (!d->conv_f16)
            return nullptr;

        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_half_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_half_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_half_sme;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_half_sme;
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        // float FMOPA wins the >= 7x7 squares and vertical even fully contended;
        // 3x3/5x5 float are pruned (thread-contested) and fall back to NEON.
        if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
            return vs_generic_7x7_conv_float_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
            return vs_generic_9x9_conv_float_sme;
        else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
            return vs_generic_11x11_conv_float_sme;
        else if (d->convolution_type == ConvolutionVertical)
            return vs_generic_1d_conv_v_float_sme;
    }
    // word: all SME word kernels were thread-contested and are pruned -> NEON.
    return nullptr;
}
#endif // VS_TARGET_ARM_SME
#endif // VS_TARGET_CPU_ARM64

template <GenericOperations op>
static decltype(&vs_generic_3x3_conv_byte_c) genericSelectC(const VSVideoFormat *fi, GenericData *d) {
    if (fi->sampleType == stInteger && fi->bytesPerSample == 1) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_byte_c;
        case GenericSobel: return vs_generic_3x3_sobel_byte_c;
        case GenericMinimum: return vs_generic_3x3_min_byte_c;
        case GenericMaximum: return vs_generic_3x3_max_byte_c;
        case GenericMedian: return vs_generic_3x3_median_byte_c;
        case GenericDeflate: return vs_generic_3x3_deflate_byte_c;
        case GenericInflate: return vs_generic_3x3_inflate_byte_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_byte_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_byte_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_byte_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_byte_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_byte_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_byte_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_byte_c;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_byte_c;
            break;
        }
    } else if (fi->sampleType == stInteger && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_word_c;
        case GenericSobel: return vs_generic_3x3_sobel_word_c;
        case GenericMinimum: return vs_generic_3x3_min_word_c;
        case GenericMaximum: return vs_generic_3x3_max_word_c;
        case GenericMedian: return vs_generic_3x3_median_word_c;
        case GenericDeflate: return vs_generic_3x3_deflate_word_c;
        case GenericInflate: return vs_generic_3x3_inflate_word_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_word_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_word_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_word_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_word_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_word_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_word_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_word_c;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_word_c;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 4) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_float_c;
        case GenericSobel: return vs_generic_3x3_sobel_float_c;
        case GenericMinimum: return vs_generic_3x3_min_float_c;
        case GenericMaximum: return vs_generic_3x3_max_float_c;
        case GenericMedian: return vs_generic_3x3_median_float_c;
        case GenericDeflate: return vs_generic_3x3_deflate_float_c;
        case GenericInflate: return vs_generic_3x3_inflate_float_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_float_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_float_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_float_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_float_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_float_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_float_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_float_c;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_float_c;
            break;
        }
    } else if (fi->sampleType == stFloat && fi->bytesPerSample == 2) {
        switch (op) {
        case GenericPrewitt: return vs_generic_3x3_prewitt_half_c;
        case GenericSobel: return vs_generic_3x3_sobel_half_c;
        case GenericMinimum: return vs_generic_3x3_min_half_c;
        case GenericMaximum: return vs_generic_3x3_max_half_c;
        case GenericMedian: return vs_generic_3x3_median_half_c;
        case GenericDeflate: return vs_generic_3x3_deflate_half_c;
        case GenericInflate: return vs_generic_3x3_inflate_half_c;
        case GenericConvolution:
            if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 9)
                return vs_generic_3x3_conv_half_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 25)
                return vs_generic_5x5_conv_half_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 49)
                return vs_generic_7x7_conv_half_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 81)
                return vs_generic_9x9_conv_half_c;
            else if (d->convolution_type == ConvolutionSquare && d->matrix_elements == 121)
                return vs_generic_11x11_conv_half_c;
            else if (d->convolution_type == ConvolutionHorizontal)
                return vs_generic_1d_conv_h_half_c;
            else if (d->convolution_type == ConvolutionVertical)
                return vs_generic_1d_conv_v_half_c;
            else if (d->convolution_type == ConvolutionSeparable)
                return vs_generic_2d_conv_sep_half_c;
            break;
        }
    }
    return nullptr;
}

template <GenericOperations op>
static const char *checkPlaneDims(const GenericData *d, int width, int height) {
    if (width < 4 || height < 4)
        return "Cannot process planes smaller than 4x4.";
    if constexpr (op == GenericConvolution) {
        int radius = d->matrix_elements / 2;
        if ((d->convolution_type == ConvolutionHorizontal || d->convolution_type == ConvolutionSeparable) && radius >= width)
            return "Width must be bigger than convolution radius.";
        if ((d->convolution_type == ConvolutionVertical || d->convolution_type == ConvolutionSeparable) && radius >= height)
            return "Height must be bigger than convolution radius.";
    }
    return nullptr;
}

template <GenericOperations op>
static const VSFrame *VS_CC genericGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GenericData *d = static_cast<GenericData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);

        // Variable-size clips can't be validated at creation (declared width/height is 0); check
        // each processed plane against the actual frame here.
        if (!d->vi->width || !d->vi->height) {
            for (int plane = 0; plane < fi->numPlanes; plane++) {
                if (d->process[plane]) {
                    if (const char *msg = checkPlaneDims<op>(d, vsapi->getFrameWidth(src, plane), vsapi->getFrameHeight(src, plane))) {
                        vsapi->setFilterError((d->filter_name + ": "s + msg).c_str(), frameCtx);
                        vsapi->freeFrame(src);
                        return nullptr;
                    }
                }
            }
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = {
            d->process[0] ? nullptr : src,
            d->process[1] ? nullptr : src,
            d->process[2] ? nullptr : src
        };

        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        const vs_generic_params params = d->params;

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                int width = vsapi->getFrameWidth(src, plane);
                int height = vsapi->getFrameHeight(src, plane);
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);

                d->func(srcp, src_stride, dstp, dst_stride, &params, width, height);
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static inline int64_t floatToInt64S(float f) {
    if (f > static_cast<float>(std::numeric_limits<int64_t>::max()))
        return std::numeric_limits<int64_t>::max();
    else if (f < static_cast<float>(std::numeric_limits<int64_t>::min()))
        return std::numeric_limits<int64_t>::min();
    else
        return static_cast<int64_t>(llround(f));
}

template <GenericOperations op>
static void VS_CC genericCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<GenericData> d(new GenericData(vsapi));

    d->filter_name = static_cast<const char *>(userData);

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    try {
        if (!is8to16orFloatFormat(d->vi->format))
            throw std::runtime_error(invalidVideoFormatMessage(d->vi->format, vsapi, nullptr));

        getPlanesArg(in, d->process, vsapi);

        int err;

        if (op == GenericMinimum || op == GenericMaximum || op == GenericDeflate || op == GenericInflate) {
            d->thf = static_cast<float>(vsapi->mapGetFloat(in, "threshold", 0, &err));
            if (err) {
                d->th = ((1 << d->vi->format.bitsPerSample) - 1);
                d->thf = std::numeric_limits<float>::max();
            } else {
                if (d->vi->format.sampleType == stInteger) {
                    int64_t ith = floatToInt64S(d->thf);
                    if (ith < 0 || ith > ((1 << d->vi->format.bitsPerSample) - 1))
                        throw std::runtime_error("threshold bigger than sample value.");
                    d->th = static_cast<uint16_t>(ith);
                } else {
                    if (d->thf < 0)
                        throw std::runtime_error("threshold must be a positive value.");
                }
            }
        }

        if (op == GenericMinimum || op == GenericMaximum) {
            int enable_elements = vsapi->mapNumElements(in, "coordinates");
            if (enable_elements == -1) {
                d->enable = 0xFF;
            } else if (enable_elements == 8) {
                const int64_t *enable = vsapi->mapGetIntArray(in, "coordinates", &err);
                for (int i = 0; i < 8; i++) {
                    d->enable |= enable[i] ? (1U << i) : 0U;
                }
            } else {
                throw std::runtime_error("coordinates must contain exactly 8 numbers.");
            }
        }


        if (op == GenericPrewitt || op == GenericSobel) {
            d->scale = static_cast<float>(vsapi->mapGetFloat(in, "scale", 0, &err));
            if (err)
                d->scale = 1.0f;

            if (d->scale < 0)
                throw std::runtime_error("scale must not be negative.");
        }

        if (op == GenericConvolution) {
            d->bias = static_cast<float>(vsapi->mapGetFloat(in, "bias", 0, &err));

            d->saturate = !!vsapi->mapGetInt(in, "saturate", 0, &err);
            if (err)
                d->saturate = true;

            d->matrix_elements = vsapi->mapNumElements(in, "matrix");

            const char *mode = vsapi->mapGetData(in, "mode", 0, &err);
            if (err || mode == "s"s) {
                d->convolution_type = ConvolutionSquare;

                if (d->matrix_elements != 9 && d->matrix_elements != 25 && d->matrix_elements != 49 && d->matrix_elements != 81 && d->matrix_elements != 121)
                    throw std::runtime_error("When mode starts with 's', matrix must contain exactly 9, 25, 49, 81 or 121 numbers (a 3x3, 5x5, 7x7, 9x9 or 11x11 square).");
            } else if (mode == "h"s || mode == "v"s || mode == "hv"s || mode == "vh"s) {
                if (mode == "h"s)
                    d->convolution_type = ConvolutionHorizontal;
                else if (mode == "v"s)
                    d->convolution_type = ConvolutionVertical;
                else
                    d->convolution_type = ConvolutionSeparable;

                if (d->matrix_elements < 3 || d->matrix_elements > 25)
                    throw std::runtime_error("When mode starts with 'h' or 'v', matrix must contain between 3 and 25 numbers.");

                if (d->matrix_elements % 2 == 0)
                    throw std::runtime_error("matrix must contain an odd number of numbers.");
            } else {
                throw std::runtime_error("mode must be one of 's', 'h', 'v', 'hv', 'vh'.");
            }

            float matrix_sumf = 0;
            const double *matrix = vsapi->mapGetFloatArray(in, "matrix", nullptr);
            d->conv_int8 = true;
            d->conv_f16 = true;
            for (int i = 0; i < d->matrix_elements; i++) {
                double c = matrix[i];
                if (!std::isfinite(c))
                    throw std::runtime_error("coefficients must be finite");
                if (d->vi->format.sampleType == stInteger) {
                    double r = std::round(c);
                    if (r < -1023.0 || r > 1023.0)
                        throw std::runtime_error("coefficients may only be between -1023 and 1023");
                    int ci = static_cast<int>(r);
                    d->matrix[i] = ci;
                    d->matrixf[i] = static_cast<float>(ci);
                    if (ci < -128 || ci > 127)
                        d->conv_int8 = false;
                } else {
                    d->matrixf[i] = static_cast<float>(c);
                }

                // Kernels that must take their coefficients in a narrower type may
                // only do so when the value survives the trip exactly -- same rule
                // conv_int8 applies to the VNNI path. Rounding a coefficient to buy
                // speed is not on the table.
                if (halfToFloat(floatToHalf(d->matrixf[i])) != d->matrixf[i])
                    d->conv_f16 = false;

                matrix_sumf += d->matrixf[i];
            }

            if (std::abs(matrix_sumf) < std::numeric_limits<float>::epsilon())
                matrix_sumf = 1.0;

            d->rdiv = static_cast<float>(vsapi->mapGetFloat(in, "divisor", 0, &err));
            if (d->rdiv == 0.0f)
                d->rdiv = static_cast<float>(matrix_sumf);

            d->rdiv = 1.0f / d->rdiv;
        }

        if (d->vi->width && d->vi->height) {
            for (int plane = 0; plane < d->vi->format.numPlanes; plane++)
                if (d->process[plane])
                    if (const char *msg = checkPlaneDims<op>(d.get(), planeWidth(d->vi, plane), planeHeight(d->vi, plane)))
                        throw std::runtime_error(msg);
        }

        d->cpulevel = vs_get_cpulevel(core);

        const VSVideoFormat *fi = &d->vi->format;
        d->func = nullptr;
#ifdef VS_TARGET_CPU_X86
        const CPUFeatures *cpu = getCPUFeatures();
        if (cpu->avx512 && d->cpulevel >= VS_CPU_LEVEL_AVX512)
            d->func = genericSelectAVX512<op>(fi, d.get());
        if (!d->func && cpu->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2)
            d->func = genericSelectAVX2<op>(fi, d.get());
        if (!d->func && d->cpulevel >= VS_CPU_LEVEL_SSE2)
            d->func = genericSelectSSE2<op>(fi, d.get());
#elif defined(VS_TARGET_CPU_ARM64)
        const CPUFeatures *cpu = getCPUFeatures();
        (void)cpu;
#ifdef VS_TARGET_ARM_SME
        if (cpu->sme2 && d->cpulevel >= VS_CPU_LEVEL_SME)
            d->func = genericSelectSME<op>(fi, d.get());
#endif
#ifdef VS_TARGET_ARM_SVE
        if (!d->func && cpu->sve && d->cpulevel >= VS_CPU_LEVEL_SVE)
            d->func = genericSelectSVE<op>(fi, d.get());
#endif
        if (!d->func && d->cpulevel >= VS_CPU_LEVEL_NEON)
            d->func = genericSelectNEON<op>(fi, d.get());
#endif
        if (!d->func)
            d->func = genericSelectC<op>(fi, d.get());
        if (!d->func)
            throw std::runtime_error("no kernel available for the clip format");

        d->params = make_generic_params(d.get(), fi);
    } catch (const std::runtime_error &error) {
        vsapi->mapSetError(out, (d->filter_name + ": "s + error.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->filter_name, d->vi, genericGetframe<op>, filterFree<GenericData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

///////////////////////////////


struct InvertDataExtra {
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    bool mask;
};

typedef SingleNodeData<InvertDataExtra> InvertData;

struct InvertOp {
    uint16_t max;
    bool uv;

    InvertOp(InvertData *d, const VSVideoFormat *fi, int plane) {
        uv = (!d->mask) && (fi->colorFamily == cfYUV) && (plane > 0);
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

    static FORCE_INLINE void processPlaneH(const uint16_t * VS_RESTRICT src, uint16_t * VS_RESTRICT dst, unsigned width, const InvertOp &opts) {
        if (opts.uv) {
            for (unsigned w = 0; w < width; w++)
                dst[w] = static_cast<uint16_t>(src[w] ^ 0x8000u);
        } else {
            for (unsigned w = 0; w < width; w++)
                dst[w] = floatToHalf(1.0f - halfToFloat(src[w]));
        }
    }
};

static void VS_CC invertCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<InvertData> d(new InvertData(vsapi));

    try {
        templateInit(d, userData ? "InvertMask" : "Invert", true, in, out, vsapi);
        d->mask = !!userData;
    } catch (const std::runtime_error &error) {
        vsapi->mapSetError(out, (d->name + ": "s + error.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->name, d->vi, singlePixelGetFrame<InvertData, InvertOp>, filterFree<InvertData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

/////////////////

struct LimitDataExtra {
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t max[3], min[3];
    float maxf[3], minf[3];
};

typedef SingleNodeData<LimitDataExtra> LimitData;

struct LimitOp {
    uint16_t max, min;
    float maxf, minf;

    LimitOp(LimitData *d, const VSVideoFormat *fi, int plane) {
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

    static FORCE_INLINE void processPlaneH(const uint16_t * VS_RESTRICT src, uint16_t * VS_RESTRICT dst, unsigned width, const LimitOp &opts) {
        for (unsigned w = 0; w < width; w++)
            dst[w] = floatToHalf(std::min(opts.maxf, std::max(opts.minf, halfToFloat(src[w]))));
    }
};

static void VS_CC limitCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LimitData> d(new LimitData(vsapi));

    try {
        templateInit(d, "Limiter", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "min", d->min, d->minf, RangeLower, false, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "max", d->max, d->maxf, RangeUpper, false, vsapi);
        for (int i = 0; i < 3; i++)
            if (((d->vi->format.sampleType == stInteger) && (d->min[i] > d->max[i])) || ((d->vi->format.sampleType == stFloat) && (d->minf[i] > d->maxf[i])))
                throw std::runtime_error("min bigger than max");
    } catch (const std::runtime_error &error) {
        vsapi->mapSetError(out, (d->name + ": "s + error.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->name, d->vi, singlePixelGetFrame<LimitData, LimitOp>, filterFree<LimitData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

/////////////////

struct BinarizeDataExtra {
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    uint16_t v0[3], v1[3], thr[3];
    float v0f[3], v1f[3], thrf[3];
};

typedef SingleNodeData<BinarizeDataExtra> BinarizeData;

struct BinarizeOp {
    uint16_t v0, v1, thr;
    float v0f, v1f, thrf;

    BinarizeOp(BinarizeData *d, const VSVideoFormat *fi, int plane) {
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

    static FORCE_INLINE void processPlaneH(const uint16_t * VS_RESTRICT src, uint16_t * VS_RESTRICT dst, unsigned width, const BinarizeOp &opts) {
        uint16_t v0h = floatToHalf(opts.v0f);
        uint16_t v1h = floatToHalf(opts.v1f);
        for (unsigned w = 0; w < width; w++)
            dst[w] = (halfToFloat(src[w]) < opts.thrf) ? v0h : v1h;
    }
};

static void VS_CC binarizeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<BinarizeData> d(new BinarizeData(vsapi));

    try {
        templateInit(d, userData ? "BinarizeMask" : "Binarize", false, in, out, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v0", d->v0, d->v0f, RangeLower, !!userData, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "v1", d->v1, d->v1f, RangeUpper, !!userData, vsapi);
        getPlanePixelRangeArgs(d->vi->format, in, "threshold", d->thr, d->thrf, RangeMiddle, !!userData, vsapi);
    } catch (const std::runtime_error &error) {
        vsapi->mapSetError(out, (d->name + ": "s + error.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->name, d->vi, singlePixelGetFrame<BinarizeData, BinarizeOp>, filterFree<BinarizeData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

/////////////////

struct LevelsDataExtra {
    VSNode *node;
    const VSVideoInfo *vi;
    const char *name;
    bool process[3];
    float gamma[3];
    float max_in[3], max_out[3], min_in[3], min_out[3];
    std::vector<uint8_t> lut[3];
};

typedef SingleNodeData<LevelsDataExtra> LevelsData;

template<typename T>
static const VSFrame *VS_CC levelsGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = reinterpret_cast<LevelsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src };
        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                T maxval = static_cast<T>((static_cast<int64_t>(1) << fi->bitsPerSample) - 1);
                const T * VS_RESTRICT lut = reinterpret_cast<const T *>(d->lut[plane].data());

                for (int hl = 0; hl < h; hl++) {
                    for (int x = 0; x < w; x++)
                        dstp[x] = lut[std::min(srcp[x], maxval)];

                    dstp += dst_stride / sizeof(T);
                    srcp += src_stride / sizeof(T);
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

template<typename T>
static const VSFrame *VS_CC levelsGetframeF(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = reinterpret_cast<LevelsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src };
        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                T gamma = d->gamma[plane];
                T range_in = 1.f / (d->max_in[plane] - d->min_in[plane]);
                T range_out = d->max_out[plane] - d->min_out[plane];
                T min_in = d->min_in[plane];
                T min_out = d->min_out[plane];
                T max_in = d->max_in[plane];

                if (std::abs(gamma - static_cast<T>(1.0)) < std::numeric_limits<T>::epsilon()) {
                    T range_scale = range_out / (max_in - min_in);
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = (std::max(std::min(srcp[x], max_in) - min_in, 0.f)) * range_scale + min_out;

                        dstp += dst_stride / sizeof(T);
                        srcp += src_stride / sizeof(T);
                    }
                } else {
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = std::pow((std::max(std::min(srcp[x], max_in) - min_in, 0.f)) * range_in, gamma) * range_out + min_out;

                        dstp += dst_stride / sizeof(T);
                        srcp += src_stride / sizeof(T);
                    }
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static const VSFrame *VS_CC levelsGetframeH(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = reinterpret_cast<LevelsData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src };
        VSFrame *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                const uint16_t * VS_RESTRICT srcp = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(src, plane));
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                uint16_t * VS_RESTRICT dstp = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(dst, plane));
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                float gamma = d->gamma[plane];
                float range_in = 1.f / (d->max_in[plane] - d->min_in[plane]);
                float range_out = d->max_out[plane] - d->min_out[plane];
                float min_in = d->min_in[plane];
                float min_out = d->min_out[plane];
                float max_in = d->max_in[plane];

                if (std::abs(gamma - 1.0f) < std::numeric_limits<float>::epsilon()) {
                    float range_scale = range_out / (max_in - min_in);
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = floatToHalf((std::max(std::min(halfToFloat(srcp[x]), max_in) - min_in, 0.f)) * range_scale + min_out);

                        dstp += dst_stride / sizeof(uint16_t);
                        srcp += src_stride / sizeof(uint16_t);
                    }
                } else {
                    for (int hl = 0; hl < h; hl++) {
                        for (int x = 0; x < w; x++)
                            dstp[x] = floatToHalf(std::pow((std::max(std::min(halfToFloat(srcp[x]), max_in) - min_in, 0.f)) * range_in, gamma) * range_out + min_out);

                        dstp += dst_stride / sizeof(uint16_t);
                        srcp += src_stride / sizeof(uint16_t);
                    }
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LevelsData> d(new LevelsData(vsapi));

    try {
        templateInit(d, "Levels", false, in, out, vsapi);

        float maxvalf = 1.0f;
        if (d->vi->format.sampleType == stInteger)
            maxvalf = static_cast<float>((1 << d->vi->format.bitsPerSample) - 1);

        getPlaneFloatArgs(d->vi->format, in, "min_in", d->min_in, 0.f, vsapi);
        getPlaneFloatArgs(d->vi->format, in, "min_out", d->min_out, 0.f, vsapi);
        getPlaneFloatArgs(d->vi->format, in, "max_in", d->max_in, maxvalf, vsapi);
        getPlaneFloatArgs(d->vi->format, in, "max_out", d->max_out, maxvalf, vsapi);
        getPlaneFloatArgs(d->vi->format, in, "gamma", d->gamma, 1.f, vsapi);

        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (!d->process[plane])
                continue;
            if (!std::isfinite(d->min_in[plane]) || !std::isfinite(d->max_in[plane]) || !std::isfinite(d->min_out[plane]) || !std::isfinite(d->max_out[plane]))
                throw std::runtime_error("min_in, max_in, min_out and max_out must be finite");
            if (d->max_in[plane] <= d->min_in[plane])
                throw std::runtime_error("max_in must be greater than min_in");
            if (!std::isfinite(d->gamma[plane]) || d->gamma[plane] <= 0.f)
                throw std::runtime_error("gamma must be finite and positive");
            d->gamma[plane] = 1.f / d->gamma[plane];
        }

        // Implement with a simple per-plane lut for integer formats
        if (d->vi->format.sampleType == stInteger) {
            int maxval = (1 << d->vi->format.bitsPerSample) - 1;

            for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
                if (!d->process[plane])
                    continue;

                float min_in = std::round(d->min_in[plane]);
                float min_out = std::round(d->min_out[plane]);
                float max_in = std::round(d->max_in[plane]);
                float max_out = std::round(d->max_out[plane]);
                float gamma = d->gamma[plane];

                if (std::abs(max_in - min_in) < std::numeric_limits<float>::epsilon())
                    throw std::runtime_error("max_in and min_in are too close");

                d->lut[plane].resize(d->vi->format.bytesPerSample * (1 << d->vi->format.bitsPerSample));
                if (d->vi->format.bytesPerSample == 1) {
                    for (int v = 0; v <= 255; v++)
                        d->lut[plane][v] = static_cast<uint8_t>(std::max(std::min(std::pow(std::max(std::min<float>(v, max_in) - min_in, 0.f) / (max_in - min_in), gamma) * (max_out - min_out) + min_out, 255.f), 0.f) + 0.5f);
                } else {
                    uint16_t *lptr = reinterpret_cast<uint16_t *>(d->lut[plane].data());
                    for (int v = 0; v <= maxval; v++)
                        lptr[v] = static_cast<uint16_t>(std::max(std::min(std::pow(std::max(std::min<float>(v, max_in) - min_in, 0.f) / (max_in - min_in), gamma) * (max_out - min_out) + min_out, maxvalf), 0.f) + 0.5f);
                }
            }
        }
    } catch (const std::runtime_error &error) {
        vsapi->mapSetError(out, (d->name + ": "s + error.what()).c_str());
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    if (d->vi->format.bytesPerSample == 1)
        vsapi->createVideoFilter(out, d->name, d->vi, levelsGetframe<uint8_t>, filterFree<LevelsData>, fmParallel, deps, 1, d.get(), core);
    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
        vsapi->createVideoFilter(out, d->name, d->vi, levelsGetframe<uint16_t>, filterFree<LevelsData>, fmParallel, deps, 1, d.get(), core);
    else if (d->vi->format.bytesPerSample == 2)
        vsapi->createVideoFilter(out, d->name, d->vi, levelsGetframeH, filterFree<LevelsData>, fmParallel, deps, 1, d.get(), core);
    else
        vsapi->createVideoFilter(out, d->name, d->vi, levelsGetframeF<float>, filterFree<LevelsData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////

void genericInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Minimum",
            "clip:vnode;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;",
            "clip:vnode;",
            genericCreate<GenericMinimum>, const_cast<char *>("Minimum"), plugin);

    vspapi->registerFunction("Maximum",
            "clip:vnode;"
            "planes:int[]:opt;"
            "threshold:float:opt;"
            "coordinates:int[]:opt;",
            "clip:vnode;",
            genericCreate<GenericMaximum>, const_cast<char *>("Maximum"), plugin);

    vspapi->registerFunction("Median",
            "clip:vnode;"
            "planes:int[]:opt;",
            "clip:vnode;",
            genericCreate<GenericMedian>, const_cast<char *>("Median"), plugin);

    vspapi->registerFunction("Deflate",
            "clip:vnode;"
            "planes:int[]:opt;"
            "threshold:float:opt;",
            "clip:vnode;",
            genericCreate<GenericDeflate>, const_cast<char *>("Deflate"), plugin);

    vspapi->registerFunction("Inflate",
            "clip:vnode;"
            "planes:int[]:opt;"
            "threshold:float:opt;",
            "clip:vnode;",
            genericCreate<GenericInflate>, const_cast<char *>("Inflate"), plugin);

    vspapi->registerFunction("Convolution",
            "clip:vnode;"
            "matrix:float[];"
            "bias:float:opt;"
            "divisor:float:opt;"
            "planes:int[]:opt;"
            "saturate:int:opt;"
            "mode:data:opt;",
            "clip:vnode;",
            genericCreate<GenericConvolution>, const_cast<char *>("Convolution"), plugin);

    vspapi->registerFunction("Prewitt",
            "clip:vnode;"
            "planes:int[]:opt;"
            "scale:float:opt;",
            "clip:vnode;",
            genericCreate<GenericPrewitt>, const_cast<char *>("Prewitt"), plugin);

    vspapi->registerFunction("Sobel",
            "clip:vnode;"
            "planes:int[]:opt;"
            "scale:float:opt;",
            "clip:vnode;",
            genericCreate<GenericSobel>, const_cast<char *>("Sobel"), plugin);

    vspapi->registerFunction("Invert",
        "clip:vnode;"
        "planes:int[]:opt;",
        "clip:vnode;",
        invertCreate, nullptr, plugin);

    vspapi->registerFunction("InvertMask",
        "clip:vnode;"
        "planes:int[]:opt;",
        "clip:vnode;",
        invertCreate, (void *)1, plugin);

    vspapi->registerFunction("Limiter",
        "clip:vnode;"
        "min:float[]:opt;"
        "max:float[]:opt;"
        "planes:int[]:opt;",
        "clip:vnode;",
        limitCreate, nullptr, plugin);

    vspapi->registerFunction("Binarize",
        "clip:vnode;"
        "threshold:float[]:opt;"
        "v0:float[]:opt;"
        "v1:float[]:opt;"
        "planes:int[]:opt;",
        "clip:vnode;",
        binarizeCreate, nullptr, plugin);

    vspapi->registerFunction("BinarizeMask",
        "clip:vnode;"
        "threshold:float[]:opt;"
        "v0:float[]:opt;"
        "v1:float[]:opt;"
        "planes:int[]:opt;",
        "clip:vnode;",
        binarizeCreate, (void *)1, plugin);

    vspapi->registerFunction("Levels",
        "clip:vnode;"
        "min_in:float[]:opt;"
        "max_in:float[]:opt;"
        "gamma:float[]:opt;"
        "min_out:float[]:opt;"
        "max_out:float[]:opt;"
        "planes:int[]:opt;",
        "clip:vnode;",
        levelsCreate, nullptr, plugin);
}
