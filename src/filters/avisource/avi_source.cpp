/*
* Copyright (c) 2012-2017 Fredrik Mellbin
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

// This code is based on Avisynth's AVISource but most of it has
// been rewritten during the porting

#include <stdexcept>
#include "stdafx.h"

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "AVIReadHandler.h"
#include "../../core/version.h"
#include "../../common/p2p_api.h"
#include "../../common/fourcc.h"
#include "../../common/vsutf16.h"
#include <vd2/system/error.h>

using namespace vsh;

static int ImageSize(const VSVideoInfo *vi, DWORD fourcc, int bitcount = 0) {
    int image_size;

    switch (fourcc) {
    case VS_FCC('v210'):
        image_size = ((16*((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
        break;
        // general packed
    case BI_RGB:
        image_size = BMPSizeHelper(vi->height, vi->width * bitcount / 8);
        break;
    case VS_FCC('b48r'):
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample * 3);
        break;
    case VS_FCC('b64a'):
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample * 4);
        break;
    case VS_FCC('YUY2'):
        image_size = BMPSizeHelper(vi->height, vi->width * 2);
        break;
    case VS_FCC('GREY'):
    case VS_FCC('Y800'):
    case VS_FCC('Y8  '):
        image_size = BMPSizeHelper(vi->height, vi->width * vi->format.bytesPerSample);
        break;
        // general planar
    default:
        image_size = (vi->width * vi->format.bytesPerSample) >> vi->format.subSamplingW;
        if (image_size) {
            image_size  *= vi->height;
            image_size >>= vi->format.subSamplingH;
            image_size  *= 2;
        }
        image_size += vi->width * vi->format.bytesPerSample * vi->height;
        image_size = (image_size + 3) & ~3;
    }
    return image_size;
}

static void unpackframe(const VSVideoInfo *vi, VSFrame *dst, VSFrame *dst_alpha, const uint8_t *srcp, int src_size, DWORD fourcc, int bitcount, bool flip, const VSAPI *vsapi) {
    bool padrows = false;

    const VSVideoFormat *fi = vsapi->getVideoFrameFormat(dst);
    p2p_buffer_param p = {};
    p.width = vi->width;
    p.height = vi->height;
    p.src[0] = srcp;
    p.src_stride[0] = vi->width * fi->bytesPerSample;
    p.src[1] = (uint8_t *)p.src[0] + p.src_stride[0] * p.height;
    p.src_stride[1] = vi->width * fi->bytesPerSample;
    for (int plane = 0; plane < fi->numPlanes; plane++) {
        p.dst[plane] = vsapi->getWritePtr(dst, plane);
        p.dst_stride[plane] = vsapi->getStride(dst, plane);
    }
    if (dst_alpha) {
        p.dst[3] = vsapi->getWritePtr(dst_alpha, 0);
        p.dst_stride[3] = vsapi->getStride(dst_alpha, 0);
    }

    switch (fourcc) {
    case VS_FCC('P010'): p.packing = p2p_p010_le; p2p_unpack_frame(&p, 0); break;
    case VS_FCC('P210'): p.packing = p2p_p210_le; p2p_unpack_frame(&p, 0); break;
    case VS_FCC('P016'): p.packing = p2p_p016_le; p2p_unpack_frame(&p, 0); break;
    case VS_FCC('P216'): p.packing = p2p_p216_le; p2p_unpack_frame(&p, 0); break;
    case VS_FCC('Y416'): p.src_stride[0] = vi->width * fi->bytesPerSample * 4; p.packing = p2p_y416_le; p2p_unpack_frame(&p, 0); break;
    case VS_FCC('v210'):
        p.packing = p2p_v210_le;
        p.src_stride[0] = ((16 * ((vi->width + 5) / 6) + 127) & ~127);
        p2p_unpack_frame(&p, 0);
        break;
    case BI_RGB:
        if (bitcount == 24)
            p.packing = p2p_rgb24_le;
        else if (bitcount == 32)
            p.packing = p2p_argb32_le;
        p.src_stride[0] = (vi->width*(bitcount/8) + 3) & ~3;
        if (flip) {
            p.src[0] = srcp + p.src_stride[0] * (p.height - 1);
            p.src_stride[0] = -p.src_stride[0];
        }
        p2p_unpack_frame(&p, 0);
        break;
    case VS_FCC('b48r'):
    case VS_FCC('b64a'):
        if (fourcc == VS_FCC('b48r'))
            p.packing = p2p_rgb48_be;
        else if (fourcc == VS_FCC('b64a'))
            p.packing = p2p_argb64_be;
        p.src_stride[0] = ((vi->width*vi->format.bytesPerSample*(p.packing == p2p_rgb48_be ? 3 : 4) + 3) & ~3);
        if (flip) {
            p.src[0] = srcp + p.src_stride[0] * (p.height - 1);
            p.src_stride[0] = -p.src_stride[0];
        }
        p2p_unpack_frame(&p, 0);
        break;
    case VS_FCC('YUY2'):
        p.packing = p2p_yuy2;
        p.src_stride[0] = (vi->width*2+ 3) & ~3;
        p2p_unpack_frame(&p, 0);
        break;
    case VS_FCC('GREY'):
    case VS_FCC('Y800'):
    case VS_FCC('Y8  '):
        padrows = true;
        // general planar
    default:
        if (!padrows && src_size) {
            int packed_size = vi->height * vi->width * vi->format.bytesPerSample;
            if (vi->format.numPlanes == 3)
                packed_size += 2*(packed_size >> (vi->format.subSamplingH + vi->format.subSamplingW));
            if (((src_size + 3) & ~3) != ((packed_size + 3) & ~3))
                padrows = true;
        }
        for (int i = 0; i < vi->format.numPlanes; i++) {
            bool switchuv =  (fourcc != VS_FCC('I420') && fourcc != VS_FCC('Y41B'));
            int plane = i;
            if (switchuv) {
                if (i == 1)
                    plane = 2;
                else if (i == 2)
                    plane = 1;
            }

            int rowsize = vsapi->getFrameWidth(dst, plane) * vi->format.bytesPerSample;
            if (padrows)
                rowsize = (rowsize + 3) & ~3;

            vsh::bitblt(vsapi->getWritePtr(dst, plane), vsapi->getStride(dst, plane), srcp, rowsize, rowsize, vsapi->getFrameHeight(dst, plane));
            srcp += vsapi->getFrameHeight(dst, plane) * rowsize;
        }
        break;
    }
}

class AVISource {
    IAVIReadHandler *pfile;
    IAVIReadStream *pvideo;
    HIC hic;
    VSVideoInfo vi[2];
    BYTE* srcbuffer;
    int srcbuffer_size;
    BITMAPINFOHEADER* pbiSrc;
    BITMAPINFOHEADER biDst;
    bool ex;
    bool bIsType1;
    bool bInvertFrames;
    char buf[1024];
    BYTE* decbuf;
    bool output_alpha;

    VSFrame *last_frame;
    VSFrame *last_alpha_frame;
    int last_frame_no;

    LRESULT DecompressBegin(LPBITMAPINFOHEADER lpbiSrc, LPBITMAPINFOHEADER lpbiDst);
    LRESULT DecompressFrame(int n, bool preroll, bool &dropped_frame, VSFrame *frame, VSFrame *alpha, VSCore *core, const VSAPI *vsapi);

    void CheckHresult(HRESULT hr, const char* msg, VSCore *core, const VSAPI *vsapi);
    bool AttemptCodecNegotiation(DWORD fccHandler, BITMAPINFOHEADER* bmih);
    void LocateVideoCodec(const char fourCC[], VSCore *core, const VSAPI *vsapi);
    bool DecompressQuery(uint32_t format, VSCore *core, const VSAPI *vsapi, bool forcedType, int bitcount, const int fourccs[], int nfourcc = 1) ;
public:

    enum {
        MODE_NORMAL = 0,
        MODE_AVIFILE,
        MODE_OPENDML,
        MODE_WAV
    };

    AVISource(const char filename[], const char pixel_type[],
        const char fourCC[], bool output_alpha, int mode, VSCore *core, const VSAPI *vsapi);  // mode: 0=detect, 1=avifile, 2=opendml
    void CleanUp(const VSAPI *vsapi);
    const VSFrame *GetFrame(int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

    static void VS_CC create_AVISource(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
        try {
            const intptr_t mode = reinterpret_cast<intptr_t>(userData);
            int err;
            const char* path = vsapi->mapGetData(in, "path", 0, nullptr);
            const char* pixel_type = vsapi->mapGetData(in, "pixel_type", 0, &err);
            if (!pixel_type)
                pixel_type = "";
            const char* fourCC = vsapi->mapGetData(in, "fourcc", 0, &err);
            if (!fourCC)
                fourCC = "";
            bool output_alpha = !!vsapi->mapGetInt(in, "alpha", 0, &err);

            AVISource *avs = new AVISource(path, pixel_type, fourCC, output_alpha, static_cast<int>(mode), core, vsapi);
            VSNode *node = vsapi->createVideoFilter2("AVISource", avs->vi, filterGetFrame, filterFree, fmUnordered, nullptr, 0, static_cast<void *>(avs), core);
            vsapi->setLinearFilter(node);
            vsapi->mapConsumeNode(out, "clip", node, maAppend);
        } catch (std::runtime_error &e) {
            vsapi->mapSetError(out, e.what());
        }
    }

    static const VSFrame *VS_CC filterGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        AVISource *d = static_cast<AVISource *>(instanceData);

        if (activationReason == arInitial) {
            try {
                return d->GetFrame(n, frameCtx, core, vsapi);
            } catch (std::runtime_error &e) {
                vsapi->setFilterError(e.what(), frameCtx);
            }
        }
        return nullptr;
    }

    static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
        AVISource *d = static_cast<AVISource *>(instanceData);
        d->CleanUp(vsapi);
        delete d;
    }
};

bool AVISource::DecompressQuery(uint32_t format, VSCore *core, const VSAPI *vsapi, bool forcedType, int bitcount, const int fourccs[], int nfourcc) {
    char fcc[5];
    fcc[4] = 0;
    for (int i = 0; i < nfourcc; i++) {
        *reinterpret_cast<int *>(fcc) = fourccs[i];
        biDst.biCompression = fourccs[0];
        
        vsapi->getVideoFormatByID(&vi[0].format, format, core);
        biDst.biSizeImage = ImageSize(vi, biDst.biCompression, bitcount);
        biDst.biBitCount = bitcount;

        if (ICERR_OK == ICDecompressQuery(hic, pbiSrc, &biDst)) {
            sprintf(buf, "AVISource: Opening as %s.\n", fcc);
            _RPT0(0, buf);
            return false;
        }
    }

    if (forcedType) {
        *reinterpret_cast<int *>(fcc) = fourccs[0];
        throw std::runtime_error("AVISource: the video decompressor couldn't produce " + std::string(fcc) + " output");
    }
    return true;
}


LRESULT AVISource::DecompressBegin(LPBITMAPINFOHEADER lpbiSrc, LPBITMAPINFOHEADER lpbiDst) {
    if (!ex) {
        LRESULT result = ICDecompressBegin(hic, lpbiSrc, lpbiDst);
        if (result != ICERR_UNSUPPORTED)
            return result;
        else
            ex = true;
        // and fall thru
    }
    return ICDecompressExBegin(hic, 0,
        lpbiSrc, 0, 0, 0, lpbiSrc->biWidth, lpbiSrc->biHeight,
        lpbiDst, 0, 0, 0, lpbiDst->biWidth, lpbiDst->biHeight);
}

LRESULT AVISource::DecompressFrame(int n, bool preroll, bool &dropped_frame, VSFrame *frame, VSFrame *alpha, VSCore *core, const VSAPI *vsapi) {
    _RPT2(0,"AVISource: Decompressing frame %d%s\n", n, preroll ? " (preroll)" : "");
    long bytes_read;
    if (!hic) {
        bytes_read = pbiSrc->biSizeImage;
        pvideo->Read(n, 1, decbuf, pbiSrc->biSizeImage, &bytes_read, nullptr);
        dropped_frame = !bytes_read;
        unpackframe(vi, frame, alpha, decbuf, bytes_read, pbiSrc->biCompression, pbiSrc->biBitCount, bInvertFrames, vsapi);
        return ICERR_OK;
    }
    bytes_read = srcbuffer_size;
    LRESULT err = pvideo->Read(n, 1, srcbuffer, srcbuffer_size, &bytes_read, nullptr);
    while (err == AVIERR_BUFFERTOOSMALL || (err == 0 && !srcbuffer)) {
        delete[] srcbuffer;
        pvideo->Read(n, 1, 0, srcbuffer_size, &bytes_read, nullptr);
        srcbuffer_size = bytes_read;
        srcbuffer = new BYTE[bytes_read + 16]; // Provide 16 hidden guard bytes for HuffYUV, Xvid, etc bug
        err = pvideo->Read(n, 1, srcbuffer, srcbuffer_size, &bytes_read, nullptr);
    }
    dropped_frame = !bytes_read;
    if (dropped_frame) return ICERR_OK;  // If frame is 0 bytes (dropped), return instead of attempt decompressing as Vdub.

    // Fill guard bytes with 0xA5's for Xvid bug
    memset(srcbuffer + bytes_read, 0xA5, 16);
    // and a Null terminator for good measure
    srcbuffer[bytes_read + 15] = 0;

    int flags = preroll ? ICDECOMPRESS_PREROLL : 0;
    flags |= dropped_frame ? ICDECOMPRESS_NULLFRAME : 0;
    flags |= !pvideo->IsKeyFrame(n) ? ICDECOMPRESS_NOTKEYFRAME : 0;
    pbiSrc->biSizeImage = bytes_read;
    LRESULT ret = (!ex ? ICDecompress(hic, flags, pbiSrc, srcbuffer, &biDst, decbuf)
        : ICDecompressEx(hic, flags, pbiSrc, srcbuffer, 0, 0, vi[0].width, vi[0].height, &biDst, decbuf, 0, 0, vi[0].width, vi[0].height));

    if (ret != ICERR_OK)
        return ret;

    unpackframe(vi, frame, alpha, decbuf, 0, biDst.biCompression, biDst.biBitCount, bInvertFrames, vsapi);

    vsapi->mapSetData(vsapi->getFramePropertiesRW(frame), "_PictType", pvideo->IsKeyFrame(n) ? "I" : "P", 1, dtUtf8, maAppend);

    return ICERR_OK;
}


void AVISource::CheckHresult(HRESULT hr, const char* msg, VSCore *core, const VSAPI *vsapi) {
    if (SUCCEEDED(hr)) return;
    char buf2[1024] = {0};
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, 0, buf, 1024, nullptr))
        wsprintfA(buf2, "error code 0x%x", hr);
    sprintf(buf, "AVISource: %s:\n%s", msg, buf2);
    throw std::runtime_error(buf);
}


// taken from VirtualDub
bool AVISource::AttemptCodecNegotiation(DWORD fccHandler, BITMAPINFOHEADER* bmih) {

    // Try the handler specified in the file first.  In some cases, it'll
    // be wrong or missing.

    if (fccHandler)
        hic = ICOpen(ICTYPE_VIDEO, fccHandler, ICMODE_DECOMPRESS);

    if (!hic || ICERR_OK!=ICDecompressQuery(hic, bmih, nullptr)) {
        if (hic)
            ICClose(hic);

        // Pick a handler based on the biCompression field instead.

        hic = ICOpen(ICTYPE_VIDEO, bmih->biCompression, ICMODE_DECOMPRESS);

        if (!hic || ICERR_OK!=ICDecompressQuery(hic, bmih, nullptr)) {
            if (hic)
                ICClose(hic);

            // This never seems to work...

            hic = ICLocate(ICTYPE_VIDEO, 0, bmih, nullptr, ICMODE_DECOMPRESS);
        }
    }

    return !!hic;
}


void AVISource::LocateVideoCodec(const char fourCC[], VSCore *core, const VSAPI *vsapi) {
    VDAVIStreamInfo asi;
    CheckHresult(pvideo->Info(&asi), "couldn't get video info", core, vsapi);
    long size = sizeof(BITMAPINFOHEADER);

    // Read video format.  If it's a
    // type-1 DV, we're going to have to fake it.

    if (bIsType1) {
        pbiSrc = (BITMAPINFOHEADER *)malloc(size);
        if (!pbiSrc)
            throw std::runtime_error("AviSource: Could not allocate BITMAPINFOHEADER.");

        pbiSrc->biSize      = sizeof(BITMAPINFOHEADER);
        pbiSrc->biWidth     = 720;

        if (asi.dwRate > asi.dwScale*26i64)
            pbiSrc->biHeight      = 480;
        else
            pbiSrc->biHeight      = 576;

        pbiSrc->biPlanes      = 1;
        pbiSrc->biBitCount    = 24;
        pbiSrc->biCompression   = 'dsvd';
        pbiSrc->biSizeImage   = asi.dwSuggestedBufferSize;
        pbiSrc->biXPelsPerMeter = 0;
        pbiSrc->biYPelsPerMeter = 0;
        pbiSrc->biClrUsed     = 0;
        pbiSrc->biClrImportant  = 0;

    } else {
        CheckHresult(pvideo->ReadFormat(0, 0, &size), "couldn't get video format size", core, vsapi);
        pbiSrc = (LPBITMAPINFOHEADER)malloc(size);
        CheckHresult(pvideo->ReadFormat(0, pbiSrc, &size), "couldn't get video format", core, vsapi);
    }

    vi[0].width = pbiSrc->biWidth;
    vi[0].height = abs(pbiSrc->biHeight);
    vi[0].fpsNum = asi.dwRate;
    vi[0].fpsDen = asi.dwScale;
    vsh::reduceRational(&vi[0].fpsNum, &vi[0].fpsDen);
    vi[0].numFrames = asi.dwLength;

    // try the requested decoder, if specified
    if (fourCC != nullptr && strlen(fourCC) == 4) {
        DWORD fcc = fourCC[0] | (fourCC[1] << 8) | (fourCC[2] << 16) | (fourCC[3] << 24);
        asi.fccHandler = pbiSrc->biCompression = fcc;
    }

    // see if we can handle the video format directly
    if (pbiSrc->biCompression == VS_FCC('YUY2')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV422P8, core);
    } else if (pbiSrc->biCompression == VS_FCC('YV12') || pbiSrc->biCompression == VS_FCC('I420')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV420P8, core);
    } else if (pbiSrc->biCompression == BI_RGB && pbiSrc->biBitCount == 32) {
        vsapi->getVideoFormatByID(&vi[0].format, pfRGB24, core);
        vi[1] = vi[0];
        vsapi->getVideoFormatByID(&vi[1].format, pfGray8, core);
        if (pbiSrc->biHeight > 0)
            bInvertFrames = true;
    } else if (pbiSrc->biCompression == BI_RGB && pbiSrc->biBitCount == 24) {
        vsapi->getVideoFormatByID(&vi[0].format, pfRGB24, core);
        if (pbiSrc->biHeight > 0)
            bInvertFrames = true;
    } else if (pbiSrc->biCompression == VS_FCC('b48r')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfRGB48, core);
    } else if (pbiSrc->biCompression == VS_FCC('b64a')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfRGB48, core);
    } else if (pbiSrc->biCompression == VS_FCC('GREY') || pbiSrc->biCompression == VS_FCC('Y800') || pbiSrc->biCompression == VS_FCC('Y8  ')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfGray8, core);
    } else if (pbiSrc->biCompression == VS_FCC('YV24')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV444P8, core);
    } else if (pbiSrc->biCompression == VS_FCC('YV16')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV422P8, core);
    } else if (pbiSrc->biCompression == VS_FCC('Y41B')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV411P8, core);
    } else if (pbiSrc->biCompression == VS_FCC('P010')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV420P10, core);
    } else if (pbiSrc->biCompression == VS_FCC('P016')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV420P16, core);
    } else if (pbiSrc->biCompression == VS_FCC('P210')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV422P10, core);
    } else if (pbiSrc->biCompression == VS_FCC('P216')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV422P16, core);
    } else if (pbiSrc->biCompression == VS_FCC('v210')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV422P10, core);
    } else if (pbiSrc->biCompression == VS_FCC('Y416')) {
        vsapi->getVideoFormatByID(&vi[0].format, pfYUV444P16, core);

        // otherwise, find someone who will decompress it
    } else {
        switch(pbiSrc->biCompression) {
        case VS_FCC('MP43'):    // Microsoft MPEG-4 V3
        case VS_FCC('DIV3'):    // "DivX Low-Motion" (4.10.0.3917)
        case VS_FCC('DIV4'):    // "DivX Fast-Motion" (4.10.0.3920)
        case VS_FCC('AP41'):    // "AngelPotion Definitive" (4.0.00.3688)
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = VS_FCC('MP43');
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = VS_FCC('DIV3');
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = VS_FCC('DIV4');
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = VS_FCC('AP41');
        default:
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
        }
        sprintf(buf, "AVISource: couldn't locate a decompressor for fourcc %c%c%c%c",
            asi.fccHandler, asi.fccHandler>>8, asi.fccHandler>>16, asi.fccHandler>>24);
        throw std::runtime_error(buf);
    }
}


AVISource::AVISource(const char filename[], const char pixel_type[], const char fourCC[], bool output_alpha, int mode, VSCore *core, const VSAPI *vsapi)
    : output_alpha(output_alpha), last_frame_no(-1), last_frame(nullptr), last_alpha_frame(nullptr), srcbuffer(nullptr), srcbuffer_size(0), ex(false), pbiSrc(nullptr),
    pvideo(nullptr), pfile(nullptr), bIsType1(false), hic(0), bInvertFrames(false), decbuf(nullptr)  {
    vi[0] = {};
    vi[1] = {};

    AVIFileInit();

    try {
        std::wstring wfilename = utf16_from_utf8(filename);

        if (mode == MODE_NORMAL) {
            // if it looks like an AVI file, open in OpenDML mode; otherwise AVIFile mode
            HANDLE h = CreateFile(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                sprintf(buf, "AVISource autodetect: couldn't open file '%s'\nError code: %d", filename, (int)GetLastError());
                throw std::runtime_error(buf);
            }
            unsigned int buf[3];
            DWORD bytes_read;
            if (ReadFile(h, buf, 12, &bytes_read, nullptr) && bytes_read == 12 && buf[0] == 'FFIR' && buf[2] == ' IVA')
                mode = MODE_OPENDML;
            else
                mode = MODE_AVIFILE;
            CloseHandle(h);
        }

        if (mode == MODE_AVIFILE || mode == MODE_WAV) {    // AVIFile mode
            PAVIFILE paf;
            if (FAILED(AVIFileOpen(&paf, wfilename.c_str(), OF_READ, 0))) {
                sprintf(buf, "AVIFileSource: couldn't open file '%s'", filename);
                throw std::runtime_error(buf);
            }

            pfile = CreateAVIReadHandler(paf);
        } else {              // OpenDML mode
            pfile = CreateAVIReadHandler(wfilename.c_str());
        }

        if (mode != MODE_WAV) { // check for video stream
            pvideo = pfile->GetStream(streamtypeVIDEO, 0);

            if (!pvideo) { // Attempt DV type 1 video.
                pvideo = pfile->GetStream('svai', 0);
                bIsType1 = true;
            }

            if (pvideo) {
                LocateVideoCodec(fourCC, core, vsapi);
                if (hic) {
                    bool forcedType = !(pixel_type[0] == 0);
                    
                    bool fY8    = lstrcmpiA(pixel_type, "Y8"   ) == 0 || pixel_type[0] == 0;
                    bool fYV12  = lstrcmpiA(pixel_type, "YV12" ) == 0 || pixel_type[0] == 0;
                    bool fYV16  = lstrcmpiA(pixel_type, "YV16" ) == 0 || pixel_type[0] == 0;
                    bool fYV24  = lstrcmpiA(pixel_type, "YV24" ) == 0 || pixel_type[0] == 0;
                    bool fYV411 = lstrcmpiA(pixel_type, "YV411") == 0 || pixel_type[0] == 0;
                    bool fYUY2  = lstrcmpiA(pixel_type, "YUY2" ) == 0 || pixel_type[0] == 0;
                    bool fRGB32 = lstrcmpiA(pixel_type, "RGB32") == 0 || pixel_type[0] == 0;
                    bool fRGB24 = lstrcmpiA(pixel_type, "RGB24") == 0 || pixel_type[0] == 0;
                    bool fRGB48 = lstrcmpiA(pixel_type, "RGB48") == 0 || pixel_type[0] == 0;
                    bool fRGB64 = lstrcmpiA(pixel_type, "RGB64") == 0 || pixel_type[0] == 0;
                    bool fP010  = lstrcmpiA(pixel_type, "P010")  == 0 || pixel_type[0] == 0;
                    bool fP016  = lstrcmpiA(pixel_type, "P016")  == 0 || pixel_type[0] == 0;
                    bool fP210  = lstrcmpiA(pixel_type, "P210")  == 0 || pixel_type[0] == 0;
                    bool fP216  = lstrcmpiA(pixel_type, "P216")  == 0 || pixel_type[0] == 0;
                    bool fY416  = lstrcmpiA(pixel_type, "Y416")  == 0 || pixel_type[0] == 0;
                    bool fv210  = lstrcmpiA(pixel_type, "v210")  == 0 || pixel_type[0] == 0;

                    if (!(fY8 || fYV12 || fYV16 || fYV24 || fYV411 || fYUY2 || fRGB32 || fRGB24 || fRGB48 || fRGB64 || fP010 || fP016 || fP210 || fP216 || fY416 || fv210))
                        throw std::runtime_error("AVISource: requested format must be one of YV24, YV16, YV12, YV411, YUY2, Y8, RGB24, RGB32, RGB48, RGB64, P010, P016, P210, P216, Y416, v210");

                    // try to decompress to YV12, YV411, YV16, YV24, YUY2, Y8, RGB32, and RGB24 in turn
                    biDst = {};
                    biDst.biSize = sizeof(BITMAPINFOHEADER);
                    biDst.biWidth = vi[0].width;
                    biDst.biHeight = vi[0].height;
                    biDst.biPlanes = 1;
                    bool bOpen = true;
                    
                    const int fccyv24[]  = { VS_FCC('YV24') };
                    const int fccyv16[]  = { VS_FCC('YV16') };
                    const int fccyv12[]  = { VS_FCC('YV12'), VS_FCC('I420') };
                    const int fccyv411[] = { VS_FCC('Y41B') };
                    const int fccyuy2[]  = { VS_FCC('YUY2') };
                    const int fccrgb[]   = {BI_RGB};
                    const int fccb48r[]  = { VS_FCC('b48r') };
                    const int fccb64a[]  = { VS_FCC('b64a') };
                    const int fccy8[]    = { VS_FCC('Y800'), VS_FCC('Y8  '), VS_FCC('GREY') };
                    const int fccp010[]  = { VS_FCC('P010') };
                    const int fccp016[]  = { VS_FCC('P016') };
                    const int fccp210[]  = { VS_FCC('P210') };
                    const int fccp216[]  = { VS_FCC('P216') };
                    const int fccy416[]  = { VS_FCC('Y416') };
                    const int fccv210[]  = { VS_FCC('v210') };

                    if (fYV24 && bOpen)
                        bOpen = DecompressQuery(pfYUV444P8, core, vsapi, forcedType, 24, fccyv24);
                    if (fYV16 && bOpen)
                        bOpen = DecompressQuery(pfYUV422P8, core, vsapi, forcedType, 16, fccyv16);
                    if (fYV12 && bOpen)
                        bOpen = DecompressQuery(pfYUV420P8, core, vsapi, forcedType, 12, fccyv12, sizeof(fccyv12)/sizeof(fccyv12[0]));
                    if (fYV411 && bOpen)
                        bOpen = DecompressQuery(pfYUV411P8, core, vsapi, forcedType, 16, fccyv411);
                    if (fYUY2 && bOpen)
                        bOpen = DecompressQuery(pfYUV422P8, core, vsapi, forcedType, 16, fccyuy2);
                    if (fRGB32 && bOpen) {
                        bOpen = DecompressQuery(pfRGB24, core, vsapi, forcedType, 32, fccrgb);
                        if (!bOpen) {
                            vi[1] = vi[0];
                            vsapi->getVideoFormatByID(&vi[1].format, pfGray8, core);
                        }
                    }
                    if (fRGB24 && bOpen)
                        bOpen = DecompressQuery(pfRGB24, core, vsapi, forcedType, 24, fccrgb);
                    if (fRGB48 && bOpen)
                        bOpen = DecompressQuery(pfRGB48, core, vsapi, forcedType, 48, fccb48r);
                    if (fRGB64 && bOpen)
                        bOpen = DecompressQuery(pfRGB48, core, vsapi, forcedType, 64, fccb64a);
                    if (fY8 && bOpen)
                        bOpen = DecompressQuery(pfGray8, core, vsapi, forcedType, 8, fccy8, sizeof(fccy8)/sizeof(fccy8[0]));
                    if (fP010 && bOpen)
                        bOpen = DecompressQuery(pfYUV420P10, core, vsapi, forcedType, 24, fccp010);
                    if (fP016 && bOpen)
                        bOpen = DecompressQuery(pfYUV420P16, core, vsapi, forcedType, 24, fccp016);
                    if (fP210 && bOpen)
                        bOpen = DecompressQuery(pfYUV422P10, core, vsapi, forcedType, 24, fccp210);
                    if (fP216 && bOpen)
                        bOpen = DecompressQuery(pfYUV422P16, core, vsapi, forcedType, 24, fccp216);
                    if (fY416 && bOpen)
                        bOpen = DecompressQuery(pfYUV444P16, core, vsapi, forcedType, 32, fccy416);
                    if (fv210 && bOpen)
                        bOpen = DecompressQuery(pfYUV422P10, core, vsapi, forcedType, 20, fccv210);

                    // No takers!
                    if (bOpen)
                        throw std::runtime_error("AviSource: Could not open video stream in any supported format.");

                    DecompressBegin(pbiSrc, &biDst);
                    if ((biDst.biCompression == BI_RGB) && (biDst.biHeight > 0))
                        bInvertFrames = true;
                }
            } else {
                throw std::runtime_error("AviSource: Could not locate video stream.");
            }
        }

        // try to decompress frame 0 if not audio only.

        bool dropped_frame = false;

        if (mode != MODE_WAV) {
            decbuf = vsh_aligned_malloc<BYTE>(hic ? biDst.biSizeImage : pbiSrc->biSizeImage, 32);
            int keyframe = pvideo->NearestKeyFrame(0);
            VSFrame *frame = vsapi->newVideoFrame(&vi[0].format, vi[0].width, vi[0].height, nullptr, core);
            VSFrame *alpha_frame = nullptr;
            if (output_alpha)
                alpha_frame = vsapi->newVideoFrame(&vi[1].format, vi[1].width, vi[1].height, nullptr, core);
            LRESULT error = DecompressFrame(keyframe, false, dropped_frame, frame, alpha_frame, core, vsapi);
            if (error != ICERR_OK)   // shutdown, if init not succesful.
                throw std::runtime_error("AviSource: Could not decompress frame 0");

            // Cope with dud AVI files that start with drop
            // frames, just return the first key frame
            if (dropped_frame) {
                keyframe = pvideo->NextKeyFrame(0);
                error = DecompressFrame(keyframe, false, dropped_frame, frame, alpha_frame, core, vsapi);
                if (error != ICERR_OK) {   // shutdown, if init not succesful.
                    sprintf(buf, "AviSource: Could not decompress first keyframe %d", keyframe);
                    throw std::runtime_error(buf);
                }
            }

            last_frame_no=0;
            last_frame=frame;
            last_alpha_frame = alpha_frame;
        }
    } catch (std::runtime_error &) {
        AVISource::CleanUp(vsapi);
        throw;
    } catch (MyError &e) {
        AVISource::CleanUp(vsapi);
        throw std::runtime_error(e.c_str());
    }
}

void AVISource::CleanUp(const VSAPI *vsapi) {
    if (hic) {
        !ex ? ICDecompressEnd(hic) : ICDecompressExEnd(hic);
        ICClose(hic);
    }
    delete pvideo;
    if (pfile)
        pfile->Release();
    AVIFileExit();
    free(pbiSrc);
    delete[] srcbuffer;
    vsh_aligned_free(decbuf);

    vsapi->freeFrame(last_frame);
    vsapi->freeFrame(last_alpha_frame);
}

const VSFrame *AVISource::GetFrame(int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    n = min(max(n, 0), vi[0].numFrames - 1);
    bool dropped_frame = false;
    if (n != last_frame_no || !last_frame) {
        // find the last keyframe
        int keyframe = pvideo->NearestKeyFrame(n);
        // maybe we don't need to go back that far
        if (last_frame_no < n && last_frame_no >= keyframe)
            keyframe = last_frame_no + 1;
        if (keyframe < 0) keyframe = 0;

        bool frameok = false;
        VSFrame *frame = nullptr;
        VSFrame *alpha_frame = nullptr;
        bool not_found_yet;
        do {
            not_found_yet = false;
            for (VDPosition i = keyframe; i <= n; ++i) {
                if (!frame)
                    frame = vsapi->newVideoFrame(&vi[0].format, vi[0].width, vi[0].height, nullptr, core);
                if (output_alpha && !alpha_frame)
                    alpha_frame = vsapi->newVideoFrame(&vi[1].format, vi[1].width, vi[1].height, nullptr, core);
                LRESULT error = DecompressFrame(i, i != n, dropped_frame, frame, alpha_frame, core, vsapi);
                if ((!dropped_frame) && (error == ICERR_OK))
                    frameok = true;   // Better safe than sorry
                if (frameok) {
                    vsapi->freeFrame(last_frame);
                    last_frame = frame;
                    frame = nullptr;
                    vsapi->freeFrame(last_alpha_frame);
                    last_alpha_frame = alpha_frame;
                    alpha_frame = nullptr;
                    if (output_alpha)
                        vsapi->mapSetFrame(vsapi->getFramePropertiesRW(last_frame), "_Alpha", last_alpha_frame, maAppend);
                }

                if (last_frame && i != n) {
                    vsapi->cacheFrame(last_frame, i, frameCtx);
                }
            }
            last_frame_no = n;

            if (!last_frame && !frameok) {  // Last keyframe was not valid.
                const VDPosition key_pre = keyframe;
                keyframe = pvideo->NearestKeyFrame(keyframe - 1);
                if (keyframe < 0) keyframe = 0;
                if (keyframe == key_pre)
                    throw std::runtime_error("AVISource: could not find valid keyframe for frame " + std::to_string(n));

                not_found_yet = true;
            }
        } while (not_found_yet);
    }

    if (!last_frame) {
        throw std::runtime_error("AVISource: failed to decode frame " + std::to_string(n));
    }

    return vsapi->addFrameRef(last_frame);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.avisource", "avisource", "VapourSynth AVISource Port", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    const char *args = "path:data[];pixel_type:data:opt;fourcc:data:opt;alpha:int:opt;";
    const char *retArgs = "clip:vnode;";
    vspapi->registerFunction("AVISource", args, retArgs, AVISource::create_AVISource, reinterpret_cast<void *>(AVISource::MODE_NORMAL), plugin);
    vspapi->registerFunction("AVIFileSource", args, retArgs, AVISource::create_AVISource, reinterpret_cast<void *>(AVISource::MODE_AVIFILE), plugin);
    vspapi->registerFunction("OpenDMLSource", args, retArgs, AVISource::create_AVISource, reinterpret_cast<void *>(AVISource::MODE_OPENDML), plugin);
}
