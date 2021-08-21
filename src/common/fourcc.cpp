/*
* Copyright (c) 2016-2021 Fredrik Mellbin
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

#include "fourcc.h"
#include "p2p_api.h"
#include "VSHelper4.h"

static bool IsSameVideoFormat(const VSVideoFormat &f, unsigned colorFamily, unsigned sampleType, unsigned bitsPerSample, unsigned subSamplingW = 0, unsigned subSamplingH = 0) noexcept {
    return f.colorFamily == colorFamily && f.sampleType == sampleType && f.bitsPerSample == bitsPerSample && f.subSamplingW == subSamplingW && f.subSamplingH == subSamplingH;
}

bool GetFourCC(const VSVideoFormat &fi, int alt_output, unsigned long &fourcc) {
    bool success = true;
    fourcc = VS_FCC('UNKN');
    if (IsSameVideoFormat(fi, cfRGB, stInteger, 8))
        fourcc = VS_FCC('DIB ');
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 10))
        fourcc = VS_FCC('r210');
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 16))
        fourcc = VS_FCC('b64a');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1) && alt_output == 1)
        fourcc = VS_FCC('I420');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1) && alt_output == 2)
        fourcc = VS_FCC('IYUV');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1))
        fourcc = VS_FCC('YV12');
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 8))
        fourcc = VS_FCC('Y800');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 0, 0))
        fourcc = VS_FCC('YV24');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && alt_output == 1)
        fourcc = VS_FCC('YUY2');
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && alt_output == 2)
        fourcc = VS_FCC('UYVY');
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
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0) && alt_output == 1)
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

bool GetBiCompression(const VSVideoFormat &format, int alt_output, unsigned long &compression) {
    bool success = GetFourCC(format, alt_output, compression) && (compression != VS_FCC('UNKN'));
    if (success) {
        if (compression == VS_FCC('DIB '))
            compression = 0; // same as BI_RGB but not going to include all headers just for one constant
    }
    return success;
}

static int BMPSizeHelper(int height, int rowsize) {
    return height * ((rowsize + 3) & ~3);
}

int BMPSize(const VSVideoInfo *vi, int alt_output) {
    if (!vi)
        return 0;
    int image_size;

    if (IsSameVideoFormat(vi->format, cfYUV, stInteger, 10, 1, 0) && alt_output == 1) {
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

int BitsPerPixel(const VSVideoFormat &format, int alt_output) {
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
    if (IsSameVideoFormat(format, cfYUV, stInteger, 10, 1, 0) && alt_output == 1)
        bits = 20;
    return bits;
}

bool HasSupportedFourCC(const VSVideoFormat &fi) {
    unsigned long dummy;
    return GetFourCC(fi, 0, dummy);
}

bool NeedsPacking(const VSVideoFormat &fi, int alt_output) {
    return (IsSameVideoFormat(fi, cfRGB, stInteger, 8) || IsSameVideoFormat(fi, cfRGB, stInteger, 10) || IsSameVideoFormat(fi, cfRGB, stInteger, 16) || (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && (alt_output == 1 || alt_output == 2)) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 0, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 0, 0));
}

// Returns false for YVU plane order and true for YUV whn doing planar output
bool NeedsUVSwap(const VSVideoFormat &fi, int alt_output) {
    return (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 2, 0)) || (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1) && (alt_output == 1 || alt_output == 2));
}

void PackOutputFrame(const uint8_t *src[3], const ptrdiff_t src_stride[3], uint8_t *dst, int width, int height, const VSVideoFormat &fi, int alt_output) {
    p2p_buffer_param p = {};
    p.width = width;
    p.height = height;
    p.dst[0] = dst;
    // Used by most so default to this
    p.dst_stride[0] = p.width * 4 * fi.bytesPerSample;

    for (int plane = 0; plane < fi.numPlanes; plane++) {
        p.src[plane] = src[plane];
        p.src_stride[plane] = src_stride[plane];
    }

    if (IsSameVideoFormat(fi, cfRGB, stInteger, 8)) {
        p.packing = p2p_argb32_le;
        for (int plane = 0; plane < 3; plane++) {
            p.src[plane] = src[plane] + src_stride[plane] * (height - 1);
            p.src_stride[plane] = -src_stride[plane];
        }
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfRGB, stInteger, 10)) {
        p.packing = p2p_rgb30_be;
        p.dst_stride[0] = ((p.width + 63) / 64) * 256;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfRGB, stInteger, 16)) {
        p.packing = p2p_argb64_be;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && alt_output == 1) {
        p.packing = p2p_yuy2;
        p.dst_stride[0] = p.width * 2 * fi.bytesPerSample;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && alt_output == 2) {
        p.packing = p2p_uyvy;
        p.dst_stride[0] = p.width * 2 * fi.bytesPerSample;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 0, 0)) {
        p.packing = p2p_y410_le;
        p.dst_stride[0] = p.width * 2 * fi.bytesPerSample;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 0, 0)) {
        p.packing = p2p_y416_le;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0) && alt_output == 1) {
        p.packing = p2p_v210_le;
        p.dst_stride[0] = ((16 * ((p.width + 5) / 6) + 127) & ~127);
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1) || IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0)) {
        if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1))
            p.packing = p2p_p016_le;
        else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0))
            p.packing = p2p_p216_le;
        else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1))
            p.packing = p2p_p010_le;
        else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0))
            p.packing = p2p_p210_le;
        p.dst_stride[0] = p.width * fi.bytesPerSample;
        p.dst_stride[1] = p.dst_stride[0];
        p.dst[1] = dst + p.dst_stride[0] * p.height;
        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
    } else {
        int row_size = width * fi.bytesPerSample;
        if (fi.numPlanes == 1) {
            vsh::bitblt(dst, (row_size + 3) & ~3, src[0], src_stride[0], row_size, height);
        } else if (fi.numPlanes == 3) {
            int row_size23 = (width >> fi.subSamplingW) * fi.bytesPerSample;

            bool switchPlanes = NeedsUVSwap(fi, alt_output);
            int plane2 = (switchPlanes ? 1 : 2);
            int plane3 = (switchPlanes ? 2 : 1);

            vsh::bitblt(dst, row_size, src[0], src_stride[0], row_size, height);

            vsh::bitblt(dst + (row_size * height),
                row_size23, src[plane2],
                src_stride[plane2], width >> fi.subSamplingW,
                height >> fi.subSamplingH);

            vsh::bitblt(dst + (row_size * height + (height >> fi.subSamplingH) * row_size23),
                row_size23, src[plane3],
                src_stride[plane3], width >> fi.subSamplingW,
                height >> fi.subSamplingH);
        }
    }
}
