/*
* Copyright (c) 2016 Fredrik Mellbin
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

#ifndef FOURCC_H
#define FOURCC_H

#include "VapourSynth4.h"

#define VS_FCC(ch4) ((((unsigned long)(ch4) & 0xFF) << 24) |     \
                  (((unsigned long)(ch4) & 0xFF00) << 8) |    \
                  (((unsigned long)(ch4) & 0xFF0000) >> 8) |  \
                  (((unsigned long)(ch4) & 0xFF000000) >> 24))

static inline bool IsSameVideoFormat(const VSVideoFormat &f, unsigned colorFamily, unsigned sampleType, unsigned bitsPerSample, unsigned subSamplingW = 0, unsigned subSamplingH = 0) noexcept {
    return f.colorFamily == colorFamily && f.sampleType == sampleType && f.bitsPerSample == bitsPerSample && f.subSamplingW == subSamplingW && f.subSamplingH == subSamplingH;
}

static inline bool GetFourCC(const VSVideoFormat &fi, int output_alt, unsigned long &fourcc) {
    bool success = true;
    fourcc = VS_FCC('UNKN');
    // FIXME, this doesn't break avisynth stuff?
    if (/*IsSameVideoFormat(fi, cfCompatBGR32, stInteger, 32) || */ IsSameVideoFormat(fi, cfRGB, stInteger, 8))
        fourcc = VS_FCC('DIB ');
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 10))
        fourcc = VS_FCC('r210');
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 16))
        fourcc = VS_FCC('b64a');
    //else if (IsSameVideoFormat(fi, cfCompatYUY2, stInteger, 16, 1, 0))
    //    fourcc = VS_FCC('YUY2');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1))
        fourcc = VS_FCC('YV12');
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 8))
        fourcc = VS_FCC('Y800');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 0, 0))
        fourcc = VS_FCC('YV24');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0))
        fourcc = VS_FCC('YV16');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 2, 0))
        fourcc = VS_FCC('Y41B');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 2, 2))
        fourcc = VS_FCC('YVU9');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1))
        fourcc = VS_FCC('P010');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1))
        fourcc = VS_FCC('P016');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0) && output_alt == 1)
        fourcc = VS_FCC('v210');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0))
        fourcc = VS_FCC('P210');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0))
        fourcc = VS_FCC('P216');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 0, 0))
        fourcc = VS_FCC('Y410');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 0, 0))
        fourcc = VS_FCC('Y416');
    else
        success = false;
    return success;
}

static inline bool GetBiCompression(const VSVideoFormat &format, int output_alt, unsigned long &compression) {
    bool success = GetFourCC(format, output_alt, compression) && (compression != VS_FCC('UNKN'));
    if (success) {
        if (compression == VS_FCC('DIB '))
            compression = 0; // same as BI_RGB but not going to include all headers just for one constant
    }
    return success;
}

static inline int BMPSizeHelper(int height, int rowsize) {
    return height * ((rowsize + 3) & ~3);
}

static inline int BMPSize(const VSVideoInfo *vi, int output_alt) {
    if (!vi)
        return 0;
    int image_size;

    if (IsSameVideoFormat(vi->format, cfYUV, stInteger, 10, 1, 0) && output_alt == 1) {
        image_size = ((16 * ((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
    } else if (IsSameVideoFormat(vi->format, cfRGB, stInteger, 8) || IsSameVideoFormat(vi->format, cfRGB, stInteger, 16) || IsSameVideoFormat(vi->format, cfYUV, stInteger, 16, 0, 0)) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample * 4);
    } else if (IsSameVideoFormat(vi->format, cfRGB, stInteger, 10)) {
        image_size = ((vi->width + 63) / 64) * 256 * vi->height;
    } else if (IsSameVideoFormat(vi->format, cfYUV, stInteger, 10, 0, 0)) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample * 2);
    } else if (vi->format.numPlanes == 1) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample);
    } else {
        image_size = (vi->width * vi->format.bytesPerSample) >> vi->format.subSamplingW;
        if (image_size) {
            image_size *= vi->height;
            image_size >>= vi->format.subSamplingH;
            image_size *= 2;
        }
        image_size += vi->width * vi->format.bytesPerSample * vi->height;
    }
    return image_size;
}

static inline int BitsPerPixel(const VSVideoFormat &format, int output_alt) {
    if (format.colorFamily == cfUndefined)
        return 0;
    int bits = format.bytesPerSample * 8;
    if ((IsSameVideoFormat(format, cfRGB, stInteger, 8) || IsSameVideoFormat(format, cfRGB, stInteger, 16) || IsSameVideoFormat(format, cfYUV, stInteger, 16, 0, 0)))
        bits *= 4;
    else if (IsSameVideoFormat(format, cfRGB, stInteger, 10))
        bits = 30;
    else if (IsSameVideoFormat(format, cfYUV, stInteger, 10, 0, 0))
        bits *= 2;
    else if (format.numPlanes == 3)
        bits += (bits * 2) >> (format.subSamplingH + format.subSamplingW);
    if (IsSameVideoFormat(format, cfYUV, stInteger, 10, 1, 0) && output_alt == 1)
        bits = 20;
    return bits;
}

static bool HasSupportedFourCC(const VSVideoFormat &fi) {
    unsigned long dummy;
    return GetFourCC(fi, 0, dummy);
}

static bool NeedsPacking(const VSVideoFormat &fi) {
    return (IsSameVideoFormat(fi, cfRGB, stInteger, 8) || IsSameVideoFormat(fi, cfRGB, stInteger, 10) || IsSameVideoFormat(fi, cfRGB, stInteger, 16) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 0, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 0, 0));
}

#endif