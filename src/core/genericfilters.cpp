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
#include <cmath>
#include <cstdlib>
#include <string>
#include <array>

#include "VSHelper.h"


#ifdef VS_TARGET_OS_WINDOWS
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif


enum GenericOperations {
    GenericPrewitt,
    GenericSobel,

    GenericMinimum,
    GenericMaximum,

    GenericMedian,

    GenericDeflate,
    GenericInflate,

    GenericConvolution,

    GenericInvert,
    GenericLimiter,
    GenericLevels,
    GenericBinarize
};


enum ConvolutionTypes {
    ConvolutionSquare,
    ConvolutionHorizontal,
    ConvolutionVertical
};


struct GenericParams {
    // Used by all.
    int max_value;

    // Prewitt, Sobel, Limiter.
    int thresh_low;
    int thresh_high;

    // Prewitt, Sobel.
    int rshift;

    // Minimum, Maximum, Deflate, Inflate, Binarize.
    int th;

    // Binarize.
    int v0;
    int v1;

    // Minimum, Maximum.
    int enable[8];

    // Convolution.
    int matrix[25];
    int matrix_elements;
    float rdiv;
    float bias;
    bool saturate;

    // Levels.
    int min_in;
    int max_in;
    float gamma;
    int min_out;
    int max_out;
};


struct GenericData {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int process[3];

    GenericParams params;

    ConvolutionTypes convolution_type;

    const char *filter_name;
};


template <GenericOperations op>
static FORCE_INLINE int min_max(int a, int b) {
    if (op == GenericMinimum || op == GenericDeflate)
        return std::min(a, b);
    else if (op == GenericMaximum || op == GenericInflate)
        return std::max(a, b);
    else
        return 42; // Silence warning.
}


template <GenericOperations op>
static FORCE_INLINE int max_min(int a, int b) {
    if (op == GenericMinimum || op == GenericDeflate)
        return std::max(a, b);
    else if (op == GenericMaximum || op == GenericInflate)
        return std::min(a, b);
    else
        return 42; // Silence warning.
}


template <typename PixelType, GenericOperations op>
static FORCE_INLINE PixelType generic_3x3(
        PixelType a11, PixelType a21, PixelType a31,
        PixelType a12, PixelType a22, PixelType a32,
        PixelType a13, PixelType a23, PixelType a33, GenericParams *params) {

    if (op == GenericPrewitt || op == GenericSobel) {

        int max_value = params->max_value;
        int thresh_low = params->thresh_low;
        int thresh_high = params->thresh_high;
        int rshift = params->rshift;

        int64_t gx, gy;

        if (op == GenericPrewitt) {
            gx = a31 + a32 + a33 - a11 - a12 - a13;
            gy = a13 + a23 + a33 - a11 - a21 - a31;
        } else if (op == GenericSobel) {
            gx = a31 + 2 * a32 + a33 - a11 - 2 * a12 - a13;
            gy = a13 + 2 * a23 + a33 - a11 - 2 * a21 - a31;
        }

        int g = static_cast<int>(std::sqrt(static_cast<double>(gx * gx + gy * gy)) + 0.5);
        g = g >> rshift;

        if (g >= thresh_high)
            g = max_value;
        if (g <= thresh_low)
            g = 0;

        return g;

    } else if (op == GenericMinimum || op == GenericMaximum) {

        int th = params->th;
        int *enable = params->enable;

        int lower_or_upper_bound;

        if (op == GenericMinimum) {
            lower_or_upper_bound = 0;
            th = -th;
        } else if (op == GenericMaximum) {
            lower_or_upper_bound = params->max_value;
        }

        int min_or_max = a22;

        int limit = max_min<op>(min_or_max + th, lower_or_upper_bound);

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

        return max_min<op>(limit, min_or_max);

    } else if (op == GenericDeflate || op == GenericInflate) {

        int th = params->th;

        int lower_or_upper_bound;

        if (op == GenericDeflate) {
            lower_or_upper_bound = 0;
            th = -th;
        } else if (op == GenericInflate) {
            lower_or_upper_bound = params->max_value;
        }

        int limit = max_min<op>(a22 + th, lower_or_upper_bound);

        int sum = a11 + a21 + a31 + a12 + a32 + a13 + a23 + a33 + 4;

        return max_min<op>(min_max<op>(sum >> 3, a22), limit);

    } else if (op == GenericMedian) {

        // Extra extra lazy.
        std::array<PixelType, 9> v{ a11, a21, a31, a12, a22, a32, a13, a23, a33 };

        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());

        return v[v.size() / 2];

    } else if (op == GenericConvolution) {

        int *matrix = params->matrix;
        float rdiv = params->rdiv;
        float bias = params->bias;
        bool saturate = params->saturate;
        int max_value = params->max_value;

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
static void process_plane_3x3(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    const PixelType *above = srcp - stride;
    const PixelType *below = srcp + stride;

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
        PixelType a15, PixelType a25, PixelType a35, PixelType a45, PixelType a55, GenericParams *params) {

    int *matrix = params->matrix;
    float rdiv = params->rdiv;
    float bias = params->bias;
    bool saturate = params->saturate;
    int max_value = params->max_value;

    PixelType pixels[25] = {
        a11, a21, a31, a41, a51,
        a12, a22, a32, a42, a52,
        a13, a23, a33, a43, a53,
        a14, a24, a34, a44, a54,
        a15, a25, a35, a45, a55
    };

    int sum = 0;

    for (int i = 0; i < 25; i++)
        sum += pixels[i] * matrix[i];

    sum = static_cast<int>(sum * rdiv + bias + 0.5f);

    if (!saturate)
        sum = std::abs(sum);

    return std::min(max_value, std::max(sum, 0));
}


template <typename PixelType>
static void process_plane_convolution_horizontal(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    int *matrix = params->matrix;
    int matrix_elements = params->matrix_elements;
    float rdiv = params->rdiv;
    float bias = params->bias;
    bool saturate = params->saturate;
    int max_value = params->max_value;

    int border = matrix_elements / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < border; x++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[std::abs(x + i - border)] * matrix[i];

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x] = std::min(max_value, std::max(sum, 0));
        }

        for (int x = border; x < width - border; x++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + i - border] * matrix[i];

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x] = std::min(max_value, std::max(sum, 0));
        }

        for (int x = width - border; x < width; x++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++) {
                int idx = x + i - border;
                int diff = width - 1 - idx;
                idx = idx + std::min(diff*2, 0);
                sum += srcp[idx] * matrix[i];
            }

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x] = std::min(max_value, std::max(sum, 0));
        }

        dstp += stride;
        srcp += stride;
    }
}


template <typename PixelType>
static void process_plane_convolution_vertical(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    int *matrix = params->matrix;
    int matrix_elements = params->matrix_elements;
    float rdiv = params->rdiv;
    float bias = params->bias;
    bool saturate = params->saturate;
    int max_value = params->max_value;

    int border = matrix_elements / 2;

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < border; y++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + std::abs(y + i - border) * stride] * matrix[i];

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x + y * stride] = std::min(max_value, std::max(sum, 0));
        }

        for (int y = border; y < height - border; y++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++)
                sum += srcp[x + (y + i - border) * stride] * matrix[i];

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x + y * stride] = std::min(max_value, std::max(sum, 0));
        }

        for (int y = height - border; y < height; y++) {
            int sum = 0;

            for (int i = 0; i < matrix_elements; i++) {
                int idx = y + i - border;
                int diff = height - 1 - idx;
                idx = idx + std::min(diff*2, 0);
                sum += srcp[x + idx * stride] * matrix[i];
            }

            sum = static_cast<int>(sum * rdiv + bias + 0.5f);

            if (!saturate)
                sum = std::abs(sum);

            dstp[x + y * stride] = std::min(max_value, std::max(sum, 0));
        }
    }
}


template <typename PixelType, GenericOperations op>
static void process_plane_5x5(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params) {
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


template <typename PixelType, GenericOperations op>
static void process_plane_1x1(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params) {
    stride /= sizeof(PixelType);

    PixelType *dstp = reinterpret_cast<PixelType *>(dstp8);
    const PixelType *srcp = reinterpret_cast<const PixelType *>(srcp8);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (op == GenericInvert) {

                dstp[x] = params->max_value - srcp[x];

            } else if (op == GenericLimiter) {

                dstp[x] = std::min(params->thresh_high, std::max<int>(params->thresh_low, srcp[x]));

            } else if (op == GenericLevels) {

                dstp[x] = static_cast<int>(std::pow(static_cast<float>(srcp[x] - params->min_in) / (params->max_in - params->min_in), 1.0f / params->gamma) * (params->max_out - params->min_out) + params->min_out + 0.5f);

            } else if (op == GenericBinarize) {

                if (srcp[x] < params->th)
                    dstp[x] = params->v0;
                else
                    dstp[x] = params->v1;

            }
        }

        srcp += stride;
        dstp += stride;
    }
}


static void VS_CC genericInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    GenericData *d = static_cast<GenericData *>(*instanceData);
    vsapi->setVideoInfo(d->vi, 1, node);
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
            if (fi->colorFamily == cmCompat)
                throw std::string("Cannot process compat formats.");

            if (fi->sampleType != stInteger || fi->bitsPerSample > 16)
                throw std::string("Only clips with integer samples and 8..16 bits per sample supported.");
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

        void (*process_plane)(uint8_t *dstp8, const uint8_t *srcp8, int width, int height, int stride, GenericParams *params);

        int bits = fi->bitsPerSample;

        if (op == GenericConvolution && d->params.matrix_elements == 25)

            process_plane = bits == 8 ? process_plane_5x5<uint8_t, op> : process_plane_5x5<uint16_t, op>;

        else if (op == GenericConvolution && d->convolution_type == ConvolutionHorizontal)

            process_plane = bits == 8 ? process_plane_convolution_horizontal<uint8_t> : process_plane_convolution_horizontal<uint16_t>;

        else if (op == GenericConvolution && d->convolution_type == ConvolutionVertical)

            process_plane = bits == 8 ? process_plane_convolution_vertical<uint8_t> : process_plane_convolution_vertical<uint16_t>;

        else if (op == GenericMinimum ||
                 op == GenericMaximum ||
                 op == GenericMedian ||
                 op == GenericDeflate ||
                 op == GenericInflate ||
                 op == GenericConvolution ||
                 op == GenericPrewitt ||
                 op == GenericSobel)

            process_plane = bits == 8 ? process_plane_3x3<uint8_t, op> : process_plane_3x3<uint16_t, op>;

        else

            process_plane = bits == 8 ? process_plane_1x1<uint8_t, op> : process_plane_1x1<uint16_t, op>;


        GenericParams params = d->params;
        params.max_value = (1 << bits) - 1;

        params.thresh_low = std::min(params.thresh_low, params.max_value);
        params.thresh_high = std::min(params.thresh_high, params.max_value);
        
        for (int plane = 0; plane < fi->numPlanes; plane++) {
            if (d->process[plane]) {
                uint8_t *dstp = vsapi->getWritePtr(dst, plane);
                const uint8_t *srcp = vsapi->getReadPtr(src, plane);
                int width = vsapi->getFrameWidth(src, plane);
                int height = vsapi->getFrameHeight(src, plane);
                int stride = vsapi->getStride(src, plane);

                process_plane(dstp, srcp, width, height, stride, &params);
            }
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}


static void VS_CC genericFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    GenericData *d = static_cast<GenericData *>(instanceData);

    vsapi->freeNode(d->node);
    delete d;
}


template <GenericOperations op>
static void VS_CC genericCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    GenericData d;
    GenericData *data;

    d.filter_name = static_cast<const char *>(userData);

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    try {
        if (d.vi->format && d.vi->format->colorFamily == cmCompat)
            throw std::string("Cannot process compat formats.");

        if (d.vi->format && (d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample > 16))
            throw std::string("Only clips with integer samples and 8..16 bits per sample supported.");

        int m = vsapi->propNumElements(in, "planes");

        for (int i = 0; i < 3; i++)
            d.process[i] = (m <= 0);

        for (int i = 0; i < m; i++) {
            int o = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

            if (o < 0 || o >= 3)
                throw std::string("plane index out of range");

            if (d.process[o])
                throw std::string("plane specified twice");

            d.process[o] = 1;
        }


        int err;

        if (op == GenericMinimum || op == GenericMaximum || op == GenericDeflate || op == GenericInflate) {
            d.params.th = int64ToIntS(vsapi->propGetInt(in, "threshold", 0, &err));
            if (err)
                d.params.th = 65535;

            if (d.params.th < 0 || d.params.th > 65535)
                throw std::string("threshold must be between 0 and 65535.");
        }


        if (op == GenericMinimum || op == GenericMaximum) {
            int enable_elements = vsapi->propNumElements(in, "coordinates");
            if (enable_elements == -1) {
                for (int i = 0; i < 8; i++)
                    d.params.enable[i] = 1;
            } else if (enable_elements == 8) {
                const int64_t *enable = vsapi->propGetIntArray(in, "coordinates", &err);
                for (int i = 0; i < 8; i++)
                    d.params.enable[i] = !!enable[i];
            } else {
                throw std::string("coordinates must contain exactly 8 numbers.");
            }
        }


        if (op == GenericPrewitt || op == GenericSobel || op == GenericLimiter) {
            d.params.thresh_low = int64ToIntS(vsapi->propGetInt(in, "min", 0, &err));

            d.params.thresh_high = int64ToIntS(vsapi->propGetInt(in, "max", 0, &err));
            if (err)
                d.params.thresh_high = 65535;

            if (d.params.thresh_low < 0 || d.params.thresh_low > 65535)
                throw std::string("min must be between 0 and 65535.");

            if (d.params.thresh_high < 0 || d.params.thresh_high > 65535)
                throw std::string("max must be between 0 and 65535.");
        }


        if (op == GenericPrewitt || op == GenericSobel) {
            d.params.rshift = int64ToIntS(vsapi->propGetInt(in, "rshift", 0, &err));

            if (d.params.rshift < 0)
                throw std::string("rshift must not be negative.");
        }


        if (op == GenericConvolution) {
            d.params.bias = static_cast<float>(vsapi->propGetFloat(in, "bias", 0, &err));

            d.params.saturate = !!vsapi->propGetInt(in, "saturate", 0, &err);
            if (err)
                d.params.saturate = true;

            d.params.matrix_elements = vsapi->propNumElements(in, "matrix");

            const char *mode = vsapi->propGetData(in, "mode", 0, &err);
            if (err || mode[0] == 's') {
                d.convolution_type = ConvolutionSquare;

                if (d.params.matrix_elements != 9 && d.params.matrix_elements != 25)
                    throw std::string("When mode starts with 's', matrix must contain exactly 9 or exactly 25 numbers.");
            } else if (mode[0] == 'h' || mode[0] == 'v') {
                if (mode[0] == 'h')
                    d.convolution_type = ConvolutionHorizontal;
                else
                    d.convolution_type = ConvolutionVertical;

                if (d.params.matrix_elements < 3 || d.params.matrix_elements > 17)
                    throw std::string("When mode starts with 'h' or 'v', matrix must contain between 3 and 17 numbers.");

                if (d.params.matrix_elements % 2 == 0)
                    throw std::string("matrix must contain an odd number of numbers.");
            } else {
                throw std::string("mode must start with 's', 'h', or 'v'.");
            }

            int64_t matrix_sum = 0;
            const int64_t *matrix = vsapi->propGetIntArray(in, "matrix", nullptr);
            for (int i = 0; i < d.params.matrix_elements; i++) {
                // Supporting coefficients outside this range would probably require int64_t accumulator.
                if (matrix[i] < -1024 || matrix[i] > 1023)
                    throw std::string("The numbers in matrix must be between -1024 and 1023.");

                d.params.matrix[i] = int64ToIntS(matrix[i]);
                matrix_sum += matrix[i];
            }

            if (matrix_sum == 0)
                matrix_sum = 1;

            d.params.rdiv = static_cast<float>(vsapi->propGetFloat(in, "divisor", 0, &err));
            if (d.params.rdiv == 0.0f)
                d.params.rdiv = static_cast<float>(matrix_sum);

            d.params.rdiv = 1.0f / d.params.rdiv;
        }


        if (op == GenericBinarize) {
            if (!d.vi->format)
                throw std::string("Can only process clips with constant format."); // Constant bit depth, really.

            d.params.th = int64ToIntS(vsapi->propGetInt(in, "threshold", 0, &err));
            if (err)
                d.params.th = 1 << (d.vi->format->bitsPerSample - 1);

            int max_value = (1 << d.vi->format->bitsPerSample) - 1;

            d.params.v0 = int64ToIntS(vsapi->propGetInt(in, "v0", 0, &err));

            d.params.v1 = int64ToIntS(vsapi->propGetInt(in, "v1", 0, &err));
            if (err)
                d.params.v1 = max_value;

            std::string tmp = " must be between 0 and " + std::to_string(max_value) + ".";

            if (d.params.th < 0 || d.params.th > max_value)
                throw "threshold" + tmp;

            if (d.params.v0 < 0 || d.params.v0 > max_value)
                throw "v0" + tmp;

            if (d.params.v1 < 0 || d.params.v1 > max_value)
                throw "v1" + tmp;
        }


        if (op == GenericLevels) {
            if (!d.vi->format)
                throw std::string("Can only process clips with constant format."); // Constant bit depth, really.

            int max_value = (1 << d.vi->format->bitsPerSample) - 1;

            d.params.min_in = int64ToIntS(vsapi->propGetInt(in, "min_in", 0, &err));
            
            d.params.max_in = int64ToIntS(vsapi->propGetInt(in, "max_in", 0, &err));
            if (err)
                d.params.max_in = max_value;

            d.params.min_out = int64ToIntS(vsapi->propGetInt(in, "min_out", 0, &err));

            d.params.max_out = int64ToIntS(vsapi->propGetInt(in, "max_out", 0, &err));
            if (err)
                d.params.max_out = max_value;

            d.params.gamma = static_cast<float>(vsapi->propGetFloat(in, "gamma", 0, &err));
            if (err)
                d.params.gamma = 1.0f;

            std::string tmp = " must be between 0 and " + std::to_string(max_value) + ".";

            if (d.params.min_in < 0 || d.params.min_in > max_value)
                throw "min_in" + tmp;

            if (d.params.max_in < 0 || d.params.max_in > max_value)
                throw "max_in" + tmp;

            if (d.params.min_out < 0 || d.params.min_out > max_value)
                throw "min_out" + tmp;

            if (d.params.max_out < 0 || d.params.max_out > max_value)
                throw "max_out" + tmp;

            if (d.params.gamma <= 0.0f)
                throw std::string("gamma must be greater than 0.");
        }
    } catch (std::string &error) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, std::string(d.filter_name).append(": ").append(error).c_str());
        return;
    }

    
    data = new GenericData(d);

    vsapi->createFilter(in, out, d.filter_name, genericInit, genericGetframe<op>, genericFree, fmParallel, 0, data, core);
}


void VS_CC genericInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    //configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Minimum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:int:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMinimum>, const_cast<char *>("Minimum"), plugin);

    registerFunc("Maximum",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:int:opt;"
            "coordinates:int[]:opt;"
            , genericCreate<GenericMaximum>, const_cast<char *>("Maximum"), plugin);

    registerFunc("Median",
            "clip:clip;"
            "planes:int[]:opt;"
            , genericCreate<GenericMedian>, const_cast<char *>("Median"), plugin);

    registerFunc("Deflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:int:opt;"
            , genericCreate<GenericDeflate>, const_cast<char *>("Deflate"), plugin);

    registerFunc("Inflate",
            "clip:clip;"
            "planes:int[]:opt;"
            "threshold:int:opt;"
            , genericCreate<GenericInflate>, const_cast<char *>("Inflate"), plugin);

    registerFunc("Convolution",
            "clip:clip;"
            "matrix:int[];"
            "bias:float:opt;"
            "divisor:float:opt;"
            "planes:int[]:opt;"
            "saturate:int:opt;"
            "mode:data:opt;"
            , genericCreate<GenericConvolution>, const_cast<char *>("Convolution"), plugin);

    registerFunc("Prewitt",
            "clip:clip;"
            "min:int:opt;"
            "max:int:opt;"
            "planes:int[]:opt;"
            "rshift:int:opt;"
            , genericCreate<GenericPrewitt>, const_cast<char *>("Prewitt"), plugin);

    registerFunc("Sobel",
            "clip:clip;"
            "min:int:opt;"
            "max:int:opt;"
            "planes:int[]:opt;"
            "rshift:int:opt;"
            , genericCreate<GenericSobel>, const_cast<char *>("Sobel"), plugin);

    registerFunc("Invert",
            "clip:clip;"
            "planes:int[]:opt;"
            , genericCreate<GenericInvert>, const_cast<char *>("Invert"), plugin);

    registerFunc("Limiter",
            "clip:clip;"
            "min:int:opt;"
            "max:int:opt;"
            "planes:int[]:opt;"
            , genericCreate<GenericLimiter>, const_cast<char *>("Limiter"), plugin);

    registerFunc("Levels",
            "clip:clip;"
            "min_in:int:opt;"
            "max_in:int:opt;"
            "gamma:float:opt;"
            "min_out:int:opt;"
            "max_out:int:opt;"
            "planes:int[]:opt;"
            , genericCreate<GenericLevels>, const_cast<char *>("Levels"), plugin);

    registerFunc("Binarize",
            "clip:clip;"
            "threshold:int:opt;"
            "v0:int:opt;"
            "v1:int:opt;"
            "planes:int[]:opt;"
            , genericCreate<GenericBinarize>, const_cast<char *>("Binarize"), plugin);
}
