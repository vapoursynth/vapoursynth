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

#include "VapourSynth.h"

#define VS_FCC(ch4) ((((unsigned long)(ch4) & 0xFF) << 24) |     \
                  (((unsigned long)(ch4) & 0xFF00) << 8) |    \
                  (((unsigned long)(ch4) & 0xFF0000) >> 8) |  \
                  (((unsigned long)(ch4) & 0xFF000000) >> 24))

static inline bool GetFourCC(int formatid, int output_alt, unsigned long &fourcc) {
    bool success = true;
    fourcc = VS_FCC('UNKN');
    if (formatid == pfCompatBGR32 || formatid == pfRGB24)
        fourcc = VS_FCC('DIB ');
    else if (formatid == pfRGB30)
        fourcc = VS_FCC('r210');
    else if (formatid == pfRGB48)
        fourcc = VS_FCC('b64a');
    else if (formatid == pfCompatYUY2)
        fourcc = VS_FCC('YUY2');
    else if (formatid == pfYUV420P8)
        fourcc = VS_FCC('YV12');
    else if (formatid == pfGray8)
        fourcc = VS_FCC('Y800');
    else if (formatid == pfYUV444P8)
        fourcc = VS_FCC('YV24');
    else if (formatid == pfYUV422P8)
        fourcc = VS_FCC('YV16');
    else if (formatid == pfYUV411P8)
        fourcc = VS_FCC('Y41B');
    else if (formatid == pfYUV410P8)
        fourcc = VS_FCC('YVU9');
    else if (formatid == pfYUV420P10)
        fourcc = VS_FCC('P010');
    else if (formatid == pfYUV420P16)
        fourcc = VS_FCC('P016');
    else if (formatid == pfYUV422P10 && output_alt == 1)
        fourcc = VS_FCC('v210');
    else if (formatid == pfYUV422P10)
        fourcc = VS_FCC('P210');
    else if (formatid == pfYUV422P16)
        fourcc = VS_FCC('P216');
    else if (formatid == pfYUV444P10)
        fourcc = VS_FCC('Y410');
    else if (formatid == pfYUV444P16)
        fourcc = VS_FCC('Y416');
    else
        success = false;
    return success;
}

static inline bool GetBiCompression(int formatid, int output_alt, unsigned long &compression) {
    bool success = GetFourCC(formatid, output_alt, compression) && (compression != VS_FCC('UNKN'));
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

    if (vi->format->id == pfYUV422P10 && output_alt == 1) {
        image_size = ((16 * ((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
    } else if (vi->format->id == pfRGB24 || vi->format->id == pfRGB48 || vi->format->id == pfYUV444P16) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format->bytesPerSample * 4);
    } else if (vi->format->id == pfRGB30) {
        image_size = ((vi->width + 63) / 64) * 256 * vi->height;
    } else if (vi->format->id == pfYUV444P10) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format->bytesPerSample * 2);
    } else if (vi->format->numPlanes == 1) {
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format->bytesPerSample);
    } else {
        image_size = (vi->width * vi->format->bytesPerSample) >> vi->format->subSamplingW;
        if (image_size) {
            image_size *= vi->height;
            image_size >>= vi->format->subSamplingH;
            image_size *= 2;
        }
        image_size += vi->width * vi->format->bytesPerSample * vi->height;
    }
    return image_size;
}

static inline int BitsPerPixel(const VSVideoInfo *vi, int output_alt) {
    if (!vi)
        return 0;
    int bits = vi->format->bytesPerSample * 8;
    if (vi->format->id == pfRGB24 || vi->format->id == pfRGB48 || vi->format->id == pfYUV444P16)
        bits *= 4;
    else if (vi->format->id == pfRGB30)
        bits = 30;
    else if (vi->format->id == pfYUV444P10)
        bits *= 2;
    else if (vi->format->numPlanes == 3)
        bits += (bits * 2) >> (vi->format->subSamplingH + vi->format->subSamplingW);
    if (vi->format->id == pfYUV422P10 && output_alt == 1)
        bits = 20;
    return bits;
}

static bool HasSupportedFourCC(int id) {
    return (id == pfCompatBGR32
        || id == pfRGB24
        || id == pfRGB30
        || id == pfRGB48
        || id == pfCompatYUY2
        || id == pfYUV420P8
        || id == pfGray8
        || id == pfYUV444P8
        || id == pfYUV422P8
        || id == pfYUV411P8
        || id == pfYUV410P8
        || id == pfYUV420P10
        || id == pfYUV420P16
        || id == pfYUV422P10
        || id == pfYUV422P16
        || id == pfYUV444P10
        || id == pfYUV444P16);
}

static bool NeedsPacking(int id) {
    return (id == pfRGB24 || id == pfRGB30 || id == pfRGB48 || id == pfYUV420P10 || id == pfYUV420P16 || id == pfYUV422P10 || id == pfYUV422P16 || id == pfYUV444P10 || id == pfYUV444P16);
}

#endif