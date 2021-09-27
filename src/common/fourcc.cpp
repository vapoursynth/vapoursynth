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

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>
#include "fourcc.h"
#include "p2p_api.h"
#include "VSHelper4.h"

namespace {

constexpr VSColorFamily cfPackedRGB = static_cast<VSColorFamily>(-static_cast<int>(cfRGB));
constexpr VSColorFamily cfPackedYUV = static_cast<VSColorFamily>(-static_cast<int>(cfYUV));

constexpr unsigned UPSIDE_DOWN = 1;
constexpr unsigned SWAP_UV = 2;
constexpr unsigned NV_PACKED = 4;
constexpr p2p_packing PLANAR = static_cast<p2p_packing>(-1);

struct Traits {
    unsigned colorFamily;
    unsigned sampleType;
    unsigned bitsPerSample;
    unsigned subSamplingW;
    unsigned subSamplingH;
    int alt_output_mode;

    unsigned fourcc;
    p2p_packing packing_mode;
    unsigned bytesPerLumaSampleNum;
    unsigned bytesPerLumaSampleDen = 1;
    unsigned biBitCount = 0;
    unsigned flags = 0;
    unsigned alignment = 1;
};

constexpr Traits fourcc_traits[] = {
    { cfRGB,  stInteger,  8, 0, 0, 0, VS_FCC('DIB '), p2p_argb32_le,  4, 1, 32, UPSIDE_DOWN },
    { cfRGB,  stInteger, 10, 0, 0, 0, VS_FCC('r210'), p2p_rgb30_be,   4, 1, 30, 0, 256, },
    { cfRGB,  stInteger, 16, 0, 0, 0, VS_FCC('b64a'), p2p_argb64_be,  8, 1, 64 },
    { cfYUV,  stInteger,  8, 1, 1, 0, VS_FCC('YV12'), PLANAR,         1 },
    { cfYUV,  stInteger,  8, 1, 1, 1, VS_FCC('I420'), PLANAR,         1, 1,  0, SWAP_UV },
    { cfYUV,  stInteger,  8, 1, 1, 2, VS_FCC('IYUV'), PLANAR,         1, 1,  0, SWAP_UV },
    { cfGray, stInteger,  8, 0, 0, 0, VS_FCC('Y800'), PLANAR,         1 },
    { cfYUV,  stInteger,  8, 0, 0, 0, VS_FCC('YV24'), PLANAR,         1 },
    { cfYUV,  stInteger,  8, 1, 0, 0, VS_FCC('YV16'), PLANAR,         1 },
    { cfYUV,  stInteger,  8, 2, 0, 0, VS_FCC('Y41B'), PLANAR,         1 },
    { cfYUV,  stInteger,  8, 1, 0, 1, VS_FCC('YUY2'), p2p_yuy2,       2 },
    { cfYUV,  stInteger,  8, 1, 0, 2, VS_FCC('UYVY'), p2p_uyvy,       2 },
    { cfYUV,  stInteger,  8, 2, 2, 0, VS_FCC('YVU9'), PLANAR,         1 },
    { cfYUV,  stInteger, 10, 1, 1, 0, VS_FCC('P010'), p2p_p010_le,    2, 1,  0, NV_PACKED },
    { cfYUV,  stInteger, 16, 1, 1, 0, VS_FCC('P016'), p2p_p016_le,    2, 1,  0, NV_PACKED },
    { cfYUV,  stInteger, 10, 1, 0, 0, VS_FCC('P210'), p2p_p210_le,    2, 1,  0, NV_PACKED },
    { cfYUV,  stInteger, 10, 1, 0, 1, VS_FCC('v210'), p2p_v210_le,   16, 6, 20, 0, 128 },
    { cfYUV,  stInteger, 16, 1, 0, 0, VS_FCC('P216'), p2p_p216_le,    2, 1,  0, NV_PACKED },
    { cfYUV,  stInteger, 10, 0, 0, 0, VS_FCC('Y410'), p2p_y410_le,    4, 1, 32 },
    { cfYUV,  stInteger, 16, 0, 0, 0, VS_FCC('Y416'), p2p_y416_le,    8, 1, 64 },

    // AVS compatibility formats.
    { cfPackedRGB, stInteger, 24, 0, 0, 0, VS_FCC('DIB '), PLANAR, 3, 1, 24, 0, 4 }, // AVS is already upside down!
    { cfPackedRGB, stInteger, 32, 0, 0, 0, VS_FCC('DIB '), PLANAR, 4, 1, 32, 0, 4 },
    { cfPackedYUV, stInteger, 16, 1, 0, 0, VS_FCC('YUY2'), PLANAR, 2, 1, 16 },
};

const Traits *find_traits(const VSVideoFormat &fi, int alt_output) {
    auto it = std::find_if(std::begin(fourcc_traits), std::end(fourcc_traits), [=](const Traits &traits) {
        return traits.colorFamily == fi.colorFamily &&
            traits.sampleType == fi.sampleType &&
            traits.bitsPerSample == fi.bitsPerSample &&
            traits.subSamplingW == fi.subSamplingW &&
            traits.subSamplingH == fi.subSamplingH &&
            traits.alt_output_mode == alt_output;
    });
    return it == std::end(fourcc_traits) ? nullptr : &*it;
}

} // namespace

static bool IsSameVideoFormat(const VSVideoFormat &f, unsigned colorFamily, unsigned sampleType, unsigned bitsPerSample, unsigned subSamplingW = 0, unsigned subSamplingH = 0) noexcept {
    return f.colorFamily == colorFamily && f.sampleType == sampleType && f.bitsPerSample == bitsPerSample && f.subSamplingW == subSamplingW && f.subSamplingH == subSamplingH;
}

bool GetFourCC(const VSVideoFormat &fi, int alt_output, unsigned long &fourcc) {
    const Traits *traits = find_traits(fi, alt_output);
    fourcc = traits ? traits->fourcc : VS_FCC('UNKN');
    return !!traits;
}

bool GetBiCompression(const VSVideoFormat &format, int alt_output, unsigned long &compression) {
    bool success = GetFourCC(format, alt_output, compression);
    if (success && compression == VS_FCC('DIB '))
        compression = 0; // BI_RGB
    return success;
}

static int RowSizeAligned(int rowsize, int alignment) {
    return rowsize % alignment ? rowsize + (alignment - rowsize % alignment) : rowsize;
}

static int RowSizePlanar(int width, int bytesPerSample, const Traits *traits) {
    int alignment = traits ? traits->alignment : 1;
    return RowSizeAligned(width * bytesPerSample, alignment);
}

static int RowSizeInterleaved(int width, const Traits *traits) {
    assert(traits);
    int rowsize = (width * traits->bytesPerLumaSampleNum + (traits->bytesPerLumaSampleDen - 1)) / traits->bytesPerLumaSampleDen;
    return RowSizeAligned(rowsize, traits->alignment);
}

int BMPSize(const VSVideoInfo *vi, int alt_output) {
    if (!vi)
        return 0;

    const Traits *traits = find_traits(vi->format, alt_output);
    if (!traits || traits->packing_mode == PLANAR) {
        int lumarowsize = RowSizePlanar(vi->width, vi->format.bytesPerSample, traits);
        int lumasize = lumarowsize * vi->height;

        if (vi->format.colorFamily == cfGray || vi->format.colorFamily == cfPackedRGB || vi->format.colorFamily == cfPackedYUV)
            return lumasize;

        assert(vi->format.numPlanes == 3);
        assert(vi->width % (1 << vi->format.subSamplingW) == 0);
        assert(vi->height % (1 << vi->format.subSamplingH) == 0);

        int chromarowsize = RowSizePlanar(vi->width >> vi->format.subSamplingW, vi->format.bytesPerSample, traits);
        int chromasize = chromarowsize * (vi->height >> vi->format.subSamplingH);

        return lumasize + chromasize * 2;
    } else if (traits->flags & NV_PACKED) {
        assert(vi->format.numPlanes == 3);
        assert(vi->width % (1 << vi->format.subSamplingW) == 0);
        assert(vi->height % (1 << vi->format.subSamplingH) == 0);

        int lumarowsize = RowSizePlanar(vi->width, vi->format.bytesPerSample, traits);
        int lumasize = lumarowsize * vi->height;

        int chromarowsize = RowSizePlanar((vi->width >> vi->format.subSamplingW) * 2, vi->format.bytesPerSample, traits);
        int chromasize = chromarowsize * (vi->height >> vi->format.subSamplingH);

        return lumasize + chromasize;
    } else {
        return RowSizeInterleaved(vi->width, traits) * vi->height;
    }
}

int BitsPerPixel(const VSVideoFormat &format, int alt_output) {
    const Traits *traits = find_traits(format, alt_output);
    if (!traits)
        return 0;

    if (traits->biBitCount)
        return traits->biBitCount;

    // Generic algorithm for biBitCount of YUV.
    int bits = format.bytesPerSample * 8;
    bits += (bits * 2) >> (format.subSamplingW + format.subSamplingH);
    return bits;
}

bool HasSupportedFourCC(const VSVideoFormat &fi) {
    unsigned long dummy;
    return GetFourCC(fi, 0, dummy);
}

bool NeedsPacking(const VSVideoFormat &fi, int alt_output) {
    const Traits *traits = find_traits(fi, alt_output);
    return traits && ((traits->packing_mode != PLANAR) || (traits->flags & UPSIDE_DOWN));
}

// Returns false for YVU plane order and true for YUV when doing planar output
bool NeedsUVSwap(const VSVideoFormat &fi, int alt_output) {
    const Traits *traits = find_traits(fi, alt_output);
    return traits && (traits->flags & SWAP_UV);
}

void PackOutputFrame(const uint8_t *src[3], const ptrdiff_t src_stride[3], uint8_t *dst, int width, int height, const VSVideoFormat &fi, int alt_output) {
    const Traits *traits = find_traits(fi, alt_output);

    if (!traits || traits->packing_mode == PLANAR) {
        // Generic plane copy.
        int planeOrder[] = { 0, 2, 1 };
        if (traits && (traits->flags & SWAP_UV))
            std::swap(planeOrder[1], planeOrder[2]);

        for (int p = 0; p < fi.numPlanes; ++p) {
            int inputPlane = planeOrder[p];

            int subSamplingW = (inputPlane == 1 || inputPlane == 2) ? fi.subSamplingW : 0;
            int subSamplingH = (inputPlane == 1 || inputPlane == 2) ? fi.subSamplingH : 0;
            int row_size = RowSizePlanar((width >> subSamplingW), fi.bytesPerSample, traits);

            uint8_t *dst_ptr = dst;
            int dst_stride = row_size;

            if (traits && (traits->flags & UPSIDE_DOWN)) {
                dst_ptr += row_size * ((height >> subSamplingH) - 1);
                dst_stride = -row_size;
            }

            vsh::bitblt(dst_ptr, dst_stride, src[inputPlane], src_stride[inputPlane], (width >> subSamplingW) * fi.bytesPerSample, height >> subSamplingH);
            dst += row_size * (height >> subSamplingH);
        }
    } else {
        p2p_buffer_param p2p_params = {};
        p2p_params.width = width;
        p2p_params.height = height;
        p2p_params.packing = traits->packing_mode;

        for (int p = 0; p < fi.numPlanes; ++p) {
            p2p_params.src[p] = src[p];
            p2p_params.src_stride[p] = src_stride[p];
        }

        p2p_params.dst[0] = dst;

        assert(!(traits->flags & SWAP_UV)); // SWAP_UV is only for planar.

        if (traits->flags & NV_PACKED) {
            p2p_params.dst_stride[0] = RowSizePlanar(width, fi.bytesPerSample, traits);
            p2p_params.dst[1] = dst + p2p_params.dst_stride[0] * height;
            p2p_params.dst_stride[1] = RowSizePlanar((width >> fi.subSamplingW) * 2, fi.bytesPerSample, traits);
        } else {
            p2p_params.dst_stride[0] = RowSizeInterleaved(width, traits);
        }

        if (traits->flags & UPSIDE_DOWN) {
            for (int p = 0; p < 4; ++p) {
                if (!p2p_params.dst[p])
                    continue;

                int heightP = p == 1 || p == 2 ? (height >> fi.subSamplingH) : height;
                assert(p2p_params.dst_stride[p] > 0); // Stride is initially equal to RowSize, which is positive.

                uint8_t *ptr = static_cast<uint8_t *>(p2p_params.dst[p]);
                p2p_params.dst[p] = ptr + (heightP - 1) * p2p_params.dst_stride[p];
                p2p_params.dst_stride[p] = -p2p_params.dst_stride[p];
            }
        }

        p2p_pack_frame(&p2p_params, P2P_ALPHA_SET_ONE);
    }
}
