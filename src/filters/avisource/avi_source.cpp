/*
* Copyright (c) 2012 Fredrik Mellbin
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

#include "VapourSynth.h"
#include "VSHelper.h"
#include "AVIReadHandler.h"

static int BMPSize(int height, int rowsize) {
    return height * ((rowsize+3) & ~3);
}

static int ImageSize(const VSVideoInfo *vi, DWORD fourcc, int bitcount = 0) {
    int image_size;

    switch (fourcc) {
    case '012v':
        image_size = ((16*((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
        break;
        // general packed
    case BI_RGB:
        image_size = BMPSize(vi->height, vi->width * bitcount / 8);
        break;
    case 'r84b':
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample * 3);
        break;
    case '2YUY':
        image_size = BMPSize(vi->height, vi->width * 2);
        break;
    case 'YERG':
    case '008Y':
    case '  8Y':
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample);
        break;
        // general planar
    default:
        image_size = (vi->width * vi->format->bytesPerSample) >> vi->format->subSamplingW;
        if (image_size) {
            image_size  *= vi->height;
            image_size >>= vi->format->subSamplingH;
            image_size  *= 2;
        }
        image_size += vi->width * vi->format->bytesPerSample * vi->height;
        image_size = (image_size + 3) & ~3;
    }
    return image_size;
}

static void unpackframe(const VSVideoInfo *vi, VSFrameRef *dst, VSFrameRef *dst_alpha, const uint8_t *srcp, int src_size, DWORD fourcc, int bitcount, bool flip, const VSAPI *vsapi) {
    bool padrows = false;
    switch (fourcc) {
    case '010P':
    case '012P':
        {
            uint16_t *y = (uint16_t *)vsapi->getWritePtr(dst, 0);
            uint16_t *u = (uint16_t *)vsapi->getWritePtr(dst, 1);
            uint16_t *v = (uint16_t *)vsapi->getWritePtr(dst, 2);

            const uint16_t *srcp16 = (const uint16_t *)srcp;
            for (int h = 0; h < vi->height; h++) {
                for (int x = 0; x < vi->width; x++)
                    y[x] = srcp16[x]>>6;
                y += vsapi->getStride(dst, 0)/2;
                srcp16 += vi->width;
            }

            int fw = vsapi->getFrameWidth(dst, 1);
            int fh = vsapi->getFrameHeight(dst, 1);
            for (int h = 0; h < fh; h++) {
                for (int x = 0; x < fw; x++) {
                    u[x] = srcp16[x*2+0]>>6;
                    v[x] = srcp16[x*2+1]>>6;
                }
                u += vsapi->getStride(dst, 1)/2;
                v += vsapi->getStride(dst, 2)/2;
                srcp16 += fw*2;
            }
        }
        break;
    case '610P':
    case '612P':
        {
            memcpy(vsapi->getWritePtr(dst, 0), srcp, vi->width*vi->format->bytesPerSample*vi->height);
            srcp += vi->width*vi->format->bytesPerSample*vi->height;

            uint16_t *u = (uint16_t *)vsapi->getWritePtr(dst, 1);
            uint16_t *v = (uint16_t *)vsapi->getWritePtr(dst, 2);
            const uint16_t *srcp16 = (const uint16_t *)srcp;
            int fw = vsapi->getFrameWidth(dst, 1);
            int fh = vsapi->getFrameHeight(dst, 1);
            for (int h = 0; h < fh; h++) {
                for (int x = 0; x < fw; x++) {
                    u[x] = srcp16[x*2+0];
                    v[x] = srcp16[x*2+1];
                }
                u += vsapi->getStride(dst, 1)/2;
                v += vsapi->getStride(dst, 2)/2;
                srcp16 += fw*2;
            }
        }
        break;
    case '012v':
        {
            int rowsize = ((16*((vi->width + 5) / 6) + 127) & ~127)/4;
            uint16_t *y = (uint16_t *)vsapi->getWritePtr(dst, 0);
            uint16_t *u = (uint16_t *)vsapi->getWritePtr(dst, 1);
            uint16_t *v = (uint16_t *)vsapi->getWritePtr(dst, 2);
            const uint32_t *srcp32 = (const uint32_t *)srcp;
            int adjwidth = vi->width/6;
            for (int h = 0; h < vi->height; h++) {
                for (int x = 0; x < adjwidth; x++) {
                    int off6 = x * 6;
                    int off3 = x * 3;
                    uint32_t dw1 = srcp32[x*4+0];
                    uint32_t dw2 = srcp32[x*4+1];
                    uint32_t dw3 = srcp32[x*4+2];
                    uint32_t dw4 = srcp32[x*4+3];

                    u[off3] = dw1 & 0x3FF;
                    v[off3] = (dw1 >> 20) & 0x3FF;
                    y[off6] = (dw1 >> 10) & 0x3FF;

                    y[off6+1] = dw2 & 0x3FF;
                    y[off6+2] = (dw2 >> 20) & 0x3FF;
                    u[off3+1] = (dw2 >> 10) & 0x3FF;

                    v[off3+1] = dw3 & 0x3FF;
                    u[off3+2] = (dw3 >> 20) & 0x3FF;
                    y[off6+3] = (dw3 >> 10) & 0x3FF;

                    y[off6+4] = dw4 & 0x3FF;
                    y[off6+5] = (dw4 >> 20) & 0x3FF;
                    v[off3+2] = (dw4 >> 10) & 0x3FF;

                }
                y += vsapi->getStride(dst, 0)/2;
                u += vsapi->getStride(dst, 1)/2;
                v += vsapi->getStride(dst, 2)/2;
                srcp32 += rowsize;
            }
        }
        break;
    case BI_RGB:
        {
            int rowsize = (vi->width*(bitcount/8) + 3) & ~3;
            uint8_t *r = vsapi->getWritePtr(dst, 0);
            uint8_t *g = vsapi->getWritePtr(dst, 1);
            uint8_t *b = vsapi->getWritePtr(dst, 2);

            if (bitcount == 24) {
                if (flip) {
                    srcp += rowsize * (vi->height - 1);
                    rowsize = -rowsize;
                }

                for (int h = 0; h < vi->height; h++) {
                    for (int x = 0; x < vi->width; x++) {
                        b[x] = srcp[x*3+0];
                        g[x] = srcp[x*3+1];
                        r[x] = srcp[x*3+2];
                    }
                    r += vsapi->getStride(dst, 0);
                    g += vsapi->getStride(dst, 1);
                    b += vsapi->getStride(dst, 2);
                    srcp += rowsize;
                }
            } else if (bitcount == 32) {
                uint8_t *a = vsapi->getWritePtr(dst_alpha, 0);
                for (int h = 0; h < vi->height; h++) {
                    for (int x = 0; x < vi->width; x++) {
                        b[x] = srcp[x*4+0];
                        g[x] = srcp[x*4+1];
                        r[x] = srcp[x*4+2];
                        a[x] = srcp[x*4+3];
                    }
                    r += vsapi->getStride(dst, 0);
                    g += vsapi->getStride(dst, 1);
                    b += vsapi->getStride(dst, 2);
                    a += vsapi->getStride(dst_alpha, 0);
                    srcp += rowsize;
                }

            }
        }
        break;
    case 'r84b':
        {
            int rowsize = ((vi->width*vi->format->bytesPerSample*3 + 3) & ~3)/2;
            uint16_t *r = (uint16_t *)vsapi->getWritePtr(dst, 0);
            uint16_t *g = (uint16_t *)vsapi->getWritePtr(dst, 1);
            uint16_t *b = (uint16_t *)vsapi->getWritePtr(dst, 2);
            const uint16_t *srcp16 = (const uint16_t *)srcp;
            for (int h = 0; h < vi->height; h++) {
                for (int x = 0; x < vi->width; x++) {
                    uint16_t tmp1 = srcp16[3*x+0];
                    uint16_t tmp2 = srcp16[3*x+1];
                    uint16_t tmp3 = srcp16[3*x+2];
                    r[x] = (tmp1 << 8) | (tmp1 >> 8);
                    g[x] = (tmp2 << 8) | (tmp2 >> 8);
                    b[x] = (tmp3 << 8) | (tmp3 >> 8);
                }
                r += vsapi->getStride(dst, 0)/2;
                g += vsapi->getStride(dst, 1)/2;
                b += vsapi->getStride(dst, 2)/2;
                srcp16 += rowsize;
            }
        }
        break;
    case '2YUY':
        {
            int rowsize = (vi->width*2+ 3) & ~3;
            uint8_t *y = vsapi->getWritePtr(dst, 0);
            uint8_t *u = vsapi->getWritePtr(dst, 1);
            uint8_t *v = vsapi->getWritePtr(dst, 2);
            for (int h = 0; h < vi->height; h++) {
                int hwidth = vi->width/2;
                for (int x = 0; x < hwidth; x++) {
                    y[2*x+0] = srcp[x*4+0];
                    u[x] = srcp[x*4+1];
                    y[2*x+1] = srcp[x*4+2];
                    v[x] = srcp[x*4+3];

                }
                y += vsapi->getStride(dst, 0);
                u += vsapi->getStride(dst, 1);
                v += vsapi->getStride(dst, 2);
                srcp += rowsize;
            }
        }
        break;
    case 'YERG':
    case '008Y':
    case '  8Y':
        padrows = true;
        // general planar
    default:
        if (!padrows && src_size) {
            int packed_size = vi->height * vi->width * vi->format->bytesPerSample;
            if (vi->format->numPlanes == 3)
                packed_size += 2*(packed_size >> (vi->format->subSamplingH + vi->format->subSamplingW));
            if (((src_size + 3) & ~3) != ((packed_size + 3) & ~3))
                padrows = true;
        }
        for (int i = 0; i < vi->format->numPlanes; i++) {
            bool switchuv = (fourcc != '024I');
            int plane = i;
            if (switchuv) {
                if (i == 1)
                    plane = 2;
                else if (i == 2)
                    plane = 1;
            }

            int rowsize = vsapi->getFrameWidth(dst, plane) * vi->format->bytesPerSample;
            if (padrows)
                rowsize = (rowsize + 3) & ~3;

            vs_bitblt(vsapi->getWritePtr(dst, plane), vsapi->getStride(dst, plane), srcp, rowsize, rowsize, vsapi->getFrameHeight(dst, plane));
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
    int numOutputs;
    BYTE* srcbuffer;
    int srcbuffer_size;
    BITMAPINFOHEADER* pbiSrc;
    BITMAPINFOHEADER biDst;
    bool ex;
    bool dropped_frame;
    bool bIsType1;
    bool bInvertFrames;
    char buf[1024];
    BYTE* decbuf;

    const VSFrameRef *last_frame;
    const VSFrameRef *last_alpha_frame;
    int last_frame_no;

    LRESULT DecompressBegin(LPBITMAPINFOHEADER lpbiSrc, LPBITMAPINFOHEADER lpbiDst);
    LRESULT DecompressFrame(int n, bool preroll, VSFrameRef *frame, VSFrameRef *alpha, VSCore *core, const VSAPI *vsapi);

    void CheckHresult(HRESULT hr, const char* msg, VSCore *core, const VSAPI *vsapi);
    bool AttemptCodecNegotiation(DWORD fccHandler, BITMAPINFOHEADER* bmih);
    void LocateVideoCodec(const char fourCC[], VSCore *core, const VSAPI *vsapi);
    bool DecompressQuery(const VSFormat *format, bool forcedType, int bitcount, const int fourccs[], int nfourcc = 1) ;
public:

    enum {
        MODE_NORMAL = 0,
        MODE_AVIFILE,
        MODE_OPENDML,
        MODE_WAV
    };

    AVISource(const char filename[], const char pixel_type[],
        const char fourCC[], int mode, VSCore *core, const VSAPI *vsapi);  // mode: 0=detect, 1=avifile, 2=opendml
    ~AVISource();
    void CleanUp();
    const VSFrameRef *GetFrame(int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

    static void VS_CC create_AVISource(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
        try {
            const int mode = int(userData);
            int err;
            const char* path = vsapi->propGetData(in, "path", 0, 0);
            const char* pixel_type = vsapi->propGetData(in, "pixel_type", 0, &err);
            if (!pixel_type)
                pixel_type = "";
            const char* fourCC = vsapi->propGetData(in, "fourcc", 0, &err);
            if (!fourCC)
                fourCC = "";

            AVISource *avs = new AVISource(path, pixel_type, fourCC, mode, core, vsapi);
            vsapi->createFilter(in, out, "AVISource", filterInit, filterGetFrame, filterFree, fmSerial, 0, (void *)avs, core);

        } catch (std::runtime_error &e) {
            vsapi->setError(out, e.what());
        }
    }

    static void VS_CC filterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
        AVISource *d = (AVISource *) * instanceData;
        vsapi->setVideoInfo(d->vi, d->numOutputs, node);
    }

    static const VSFrameRef *VS_CC filterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        AVISource *d = (AVISource *) * instanceData;

        if (activationReason == arInitial) {
            try {
                return d->GetFrame(n, frameCtx, core, vsapi);
            } catch (std::runtime_error &e) {
                vsapi->setFilterError(e.what(), frameCtx);
            }
        }
        return NULL;
    }

    static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
        AVISource *d = (AVISource *)instanceData;
        delete d;
    }
};

bool AVISource::DecompressQuery(const VSFormat *format, bool forcedType, int bitcount, const int fourccs[], int nfourcc) {
    char fcc[5];
    fcc[4] = 0;
    for (int i = 0; i < nfourcc; i++) {
        *((int *)fcc) = fourccs[i];
        biDst.biCompression = fourccs[0];
        vi[0].format = format;
        biDst.biSizeImage = ImageSize(vi, biDst.biCompression, bitcount);
        biDst.biBitCount = bitcount;

        if (ICERR_OK == ICDecompressQuery(hic, pbiSrc, &biDst)) {
            sprintf(buf, "AVISource: Opening as %s.\n", fcc);
            _RPT0(0, buf);
            return false;
        }
    }

    if (forcedType) {
        *((int *)fcc) = fourccs[0];
        sprintf(buf, "AVISource: the video decompressor couldn't produce %s output", fcc);
        throw std::runtime_error(buf);
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

LRESULT AVISource::DecompressFrame(int n, bool preroll, VSFrameRef *frame, VSFrameRef *alpha, VSCore *core, const VSAPI *vsapi) {
    _RPT2(0,"AVISource: Decompressing frame %d%s\n", n, preroll ? " (preroll)" : "");
    long bytes_read;
    if (!hic) {
        bytes_read = pbiSrc->biSizeImage;
        pvideo->Read(n, 1, decbuf, pbiSrc->biSizeImage, &bytes_read, NULL);
        dropped_frame = !bytes_read;
        unpackframe(vi, frame, alpha, decbuf, bytes_read, pbiSrc->biCompression, pbiSrc->biBitCount, bInvertFrames, vsapi);
        return ICERR_OK;
    }
    bytes_read = srcbuffer_size;
    LRESULT err = pvideo->Read(n, 1, srcbuffer, srcbuffer_size, &bytes_read, NULL);
    while (err == AVIERR_BUFFERTOOSMALL || (err == 0 && !srcbuffer)) {
        delete[] srcbuffer;
        pvideo->Read(n, 1, 0, srcbuffer_size, &bytes_read, NULL);
        srcbuffer_size = bytes_read;
        srcbuffer = new BYTE[bytes_read + 16]; // Provide 16 hidden guard bytes for HuffYUV, Xvid, etc bug
        err = pvideo->Read(n, 1, srcbuffer, srcbuffer_size, &bytes_read, NULL);
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
    DWORD ret = (!ex ? ICDecompress(hic, flags, pbiSrc, srcbuffer, &biDst, decbuf)
        : ICDecompressEx(hic, flags, pbiSrc, srcbuffer, 0, 0, vi[0].width, vi[0].height, &biDst, decbuf, 0, 0, vi[0].width, vi[0].height));

    unpackframe(vi, frame, alpha, decbuf, 0, biDst.biCompression, biDst.biBitCount, bInvertFrames, vsapi);

    if (pvideo->IsKeyFrame(n)) {
        vsapi->propSetData(vsapi->getFramePropsRW(frame), "_PictType", "I", 1, paAppend);
        if (alpha)
            vsapi->propSetData(vsapi->getFramePropsRW(alpha), "_PictType", "I", 1, paAppend);
    } else {
        vsapi->propSetData(vsapi->getFramePropsRW(frame), "_PictType", "P", 1, paAppend);
        if (alpha)
            vsapi->propSetData(vsapi->getFramePropsRW(alpha), "_PictType", "P", 1, paAppend);
    }

    return ret;
}


void AVISource::CheckHresult(HRESULT hr, const char* msg, VSCore *core, const VSAPI *vsapi) {
    if (SUCCEEDED(hr)) return;
    char buf2[1024] = {0};
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr, 0, buf, 1024, NULL))
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

    if (!hic || ICERR_OK!=ICDecompressQuery(hic, bmih, NULL)) {
        if (hic)
            ICClose(hic);

        // Pick a handler based on the biCompression field instead.

        hic = ICOpen(ICTYPE_VIDEO, bmih->biCompression, ICMODE_DECOMPRESS);

        if (!hic || ICERR_OK!=ICDecompressQuery(hic, bmih, NULL)) {
            if (hic)
                ICClose(hic);

            // This never seems to work...

            hic = ICLocate(ICTYPE_VIDEO, NULL, bmih, NULL, ICMODE_DECOMPRESS);
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
    vi[0].numFrames = asi.dwLength;

    // try the requested decoder, if specified
    if (fourCC != NULL && strlen(fourCC) == 4) {
        DWORD fcc = fourCC[0] | (fourCC[1] << 8) | (fourCC[2] << 16) | (fourCC[3] << 24);
        asi.fccHandler = pbiSrc->biCompression = fcc;
    }

    // see if we can handle the video format directly
    if (pbiSrc->biCompression == '2YUY') { // :FIXME: Handle UYVY, etc
        vi[0].format = vsapi->getFormatPreset(pfYUV422P8, core);
    } else if (pbiSrc->biCompression == '21VY') {
        vi[0].format = vsapi->getFormatPreset(pfYUV420P8, core);
    } else if (pbiSrc->biCompression == '024I') {
        vi[0].format = vsapi->getFormatPreset(pfYUV420P8, core);
    } else if (pbiSrc->biCompression == BI_RGB && pbiSrc->biBitCount == 32) {
        vi[0].format = vsapi->getFormatPreset(pfRGB24, core);
        numOutputs = 2;
        vi[1] = vi[0];
        vi[1].format = vsapi->getFormatPreset(pfGray8, core);
        if (pbiSrc->biHeight < 0) bInvertFrames = true;
    } else if (pbiSrc->biCompression == BI_RGB && pbiSrc->biBitCount == 24) {
        vi[0].format = vsapi->getFormatPreset(pfRGB24, core);
        if (pbiSrc->biHeight < 0) bInvertFrames = true;
    } else if (pbiSrc->biCompression == 'r84b') {
        vi[0].format = vsapi->getFormatPreset(pfRGB48, core);
    } else if (pbiSrc->biCompression == 'YERG') {
        vi[0].format = vsapi->getFormatPreset(pfGray8, core);
    } else if (pbiSrc->biCompression == '008Y') {
        vi[0].format = vsapi->getFormatPreset(pfGray8, core);
    } else if (pbiSrc->biCompression == '  8Y') {
        vi[0].format = vsapi->getFormatPreset(pfGray8, core);
    } else if (pbiSrc->biCompression == '42VY') {
        vi[0].format = vsapi->getFormatPreset(pfYUV444P8, core);
    } else if (pbiSrc->biCompression == '61VY') {
        vi[0].format = vsapi->getFormatPreset(pfYUV422P8, core);
    } else if (pbiSrc->biCompression == 'B14Y') {
        vi[0].format = vsapi->getFormatPreset(pfYUV411P8, core);
    } else if (pbiSrc->biCompression == '010P') {
        vi[0].format = vsapi->getFormatPreset(pfYUV420P10, core);
    } else if (pbiSrc->biCompression == '610P') {
        vi[0].format = vsapi->getFormatPreset(pfYUV420P16, core);
    } else if (pbiSrc->biCompression == '012P') {
        vi[0].format = vsapi->getFormatPreset(pfYUV422P10, core);
    } else if (pbiSrc->biCompression == '612P') {
        vi[0].format = vsapi->getFormatPreset(pfYUV422P16, core);
    } else if (pbiSrc->biCompression == '012v') {
        vi[0].format = vsapi->getFormatPreset(pfYUV422P10, core);

        // otherwise, find someone who will decompress it
    } else {
        switch(pbiSrc->biCompression) {
        case '34PM':    // Microsoft MPEG-4 V3
        case '3VID':    // "DivX Low-Motion" (4.10.0.3917)
        case '4VID':    // "DivX Fast-Motion" (4.10.0.3920)
        case '14PA':    // "AngelPotion Definitive" (4.0.00.3688)
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = '34PM';
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = '3VID';
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = '4VID';
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
            pbiSrc->biCompression = '14PA';
        default:
            if (AttemptCodecNegotiation(asi.fccHandler, pbiSrc)) return;
        }
        sprintf(buf, "AVISource: couldn't locate a decompressor for fourcc %c%c%c%c",
            asi.fccHandler, asi.fccHandler>>8, asi.fccHandler>>16, asi.fccHandler>>24);
        throw std::runtime_error(buf);
    }
}


AVISource::AVISource(const char filename[], const char pixel_type[], const char fourCC[], int mode, VSCore *core, const VSAPI *vsapi)
    : numOutputs(1), last_frame_no(-1), last_frame(NULL), last_alpha_frame(NULL), srcbuffer(NULL), srcbuffer_size(0), ex(false), pbiSrc(NULL),
    pvideo(NULL), pfile(NULL), bIsType1(false), hic(0), bInvertFrames(false), decbuf(NULL)  {
    memset(vi, 0, sizeof(vi));

    AVIFileInit();
    try {

        std::vector<wchar_t> wfilename;
        wfilename.resize(MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0));
        MultiByteToWideChar(CP_UTF8, 0, filename, -1, &wfilename[0], wfilename.size());

        if (mode == MODE_NORMAL) {
            // if it looks like an AVI file, open in OpenDML mode; otherwise AVIFile mode
            HANDLE h = CreateFile(&wfilename[0], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                sprintf(buf, "AVISource autodetect: couldn't open file '%s'\nError code: %d", filename, GetLastError());
                throw std::runtime_error(buf);
            }
            unsigned int buf[3];
            DWORD bytes_read;
            if (ReadFile(h, buf, 12, &bytes_read, NULL) && bytes_read == 12 && buf[0] == 'FFIR' && buf[2] == ' IVA')
                mode = MODE_OPENDML;
            else
                mode = MODE_AVIFILE;
            CloseHandle(h);
        }

        if (mode == MODE_AVIFILE || mode == MODE_WAV) {    // AVIFile mode
            PAVIFILE paf;
            if (FAILED(AVIFileOpen(&paf, &wfilename[0], OF_READ, 0))) {
                sprintf(buf, "AVIFileSource: couldn't open file '%s'", filename);
                throw std::runtime_error(buf);
            }

            pfile = CreateAVIReadHandler(paf);
        } else {              // OpenDML mode
            pfile = CreateAVIReadHandler(&wfilename[0]);
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
                    bool fP010  = lstrcmpiA(pixel_type, "P010")  == 0 || pixel_type[0] == 0;
                    bool fP016  = lstrcmpiA(pixel_type, "P016")  == 0 || pixel_type[0] == 0;
                    bool fP210  = lstrcmpiA(pixel_type, "P210")  == 0 || pixel_type[0] == 0;
                    bool fP216  = lstrcmpiA(pixel_type, "P216")  == 0 || pixel_type[0] == 0;
                    bool fv210  = lstrcmpiA(pixel_type, "v210")  == 0 || pixel_type[0] == 0;

                    if (!(fY8 || fYV12 || fYV16 || fYV24 || fYV411 || fYUY2 || fRGB32 || fRGB24 || fRGB48 || fP010 || fP016 || fP210 || fP216 || fv210))
                        throw std::runtime_error("AVISource: requested format must be one of YV24, YV16, YV12, YV411, YUY2, Y8, RGB32, RGB24, RGB48, P010, P016, P210, P216, v210");

                    // try to decompress to YV12, YV411, YV16, YV24, YUY2, Y8, RGB32, and RGB24 in turn
                    memset(&biDst, 0, sizeof(BITMAPINFOHEADER));
                    biDst.biSize = sizeof(BITMAPINFOHEADER);
                    biDst.biWidth = vi[0].width;
                    biDst.biHeight = vi[0].height;
                    biDst.biPlanes = 1;
                    bool bOpen = true;

                    const int fccyv24[] = {'42VY'};
                    const int fccyv16[] = {'61VY'};
                    const int fccyv12[] = {'21VY', '024I'};
                    const int fccyv411[] = {'B14Y'};
                    const int fccyuy2[] = {'2YUY'};
                    const int fccrgb[]  = {BI_RGB};
                    const int fccb48r[]  = {'r84b'};
                    const int fccy8[]   = {'008Y', '  8Y', 'YERG'};
                    const int fccp010[] = {'010P'};
                    const int fccp016[] = {'610P'};
                    const int fccp210[] = {'012P'};
                    const int fccp216[] = {'612P'};
                    const int fccv210[] = {'012v'};

                    if (fYV24 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV444P8, core), forcedType, 24, fccyv24);
                    if (fYV16 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV422P8, core), forcedType, 16, fccyv16);
                    if (fYV12 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV420P8, core), forcedType, 12, fccyv12, sizeof(fccyv12)/sizeof(fccyv12[0]));
                    if (fYV411 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV411P8, core), forcedType, 16, fccyv411);
                    if (fYUY2 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV422P8, core), forcedType, 16, fccyuy2);
                    if (fRGB32 && bOpen) {
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfRGB24, core), forcedType, 32, fccrgb);
                        if (!bOpen) {
                            numOutputs = 2;
                            vi[1] = vi[0];
                            vi[1].format = vsapi->getFormatPreset(pfGray8, core);
                        }
                    }
                    if (fRGB24 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfRGB24, core), forcedType, 24, fccrgb);
                    if (fRGB48 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfRGB48, core), forcedType, 48, fccb48r);
                    if (fY8 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfGray8, core), forcedType, 8, fccy8, sizeof(fccy8)/sizeof(fccy8[0]));
                    if (fP010 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV420P10, core), forcedType, 24, fccp010);
                    if (fP016 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV420P16, core), forcedType, 24, fccp016);
                    if (fP210 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV422P10, core), forcedType, 24, fccp210);
                    if (fP216 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV422P16, core), forcedType, 24, fccp216);
                    if (fv210 && bOpen)
                        bOpen = DecompressQuery(vsapi->getFormatPreset(pfYUV422P10, core), forcedType, 20, fccv210);

                    // No takers!
                    if (bOpen)
                        throw std::runtime_error("AviSource: Could not open video stream in any supported format.");

                    DecompressBegin(pbiSrc, &biDst);
                }
            } else {
                throw std::runtime_error("AviSource: Could not locate video stream.");
            }
        }

        // try to decompress frame 0 if not audio only.

        dropped_frame=false;

        if (mode != MODE_WAV) {
            decbuf = vs_aligned_malloc<BYTE>(hic ? biDst.biSizeImage : pbiSrc->biSizeImage, 32);
            int keyframe = pvideo->NearestKeyFrame(0);
            VSFrameRef *frame = vsapi->newVideoFrame(vi[0].format, vi[0].width, vi[0].height, NULL, core);
            VSFrameRef *alpha = NULL;
            if (numOutputs == 2)
                alpha = vsapi->newVideoFrame(vi[1].format, vi[1].width, vi[1].height, NULL, core);
            LRESULT error = DecompressFrame(keyframe, false, frame, alpha, core, vsapi);
            if (error != ICERR_OK)   // shutdown, if init not succesful.
                throw std::runtime_error("AviSource: Could not decompress frame 0");

            // Cope with dud AVI files that start with drop
            // frames, just return the first key frame
            if (dropped_frame) {
                keyframe = pvideo->NextKeyFrame(0);
                error = DecompressFrame(keyframe, false, frame, alpha, core, vsapi);
                if (error != ICERR_OK) {   // shutdown, if init not succesful.
                    sprintf(buf, "AviSource: Could not decompress first keyframe %d", keyframe);
                    throw std::runtime_error(buf);
                }
            }

            last_frame_no=0;
            last_frame=frame;
            last_alpha_frame=alpha;
        }
    }
    catch (std::runtime_error) {
        AVISource::CleanUp();
        throw;
    }
}

AVISource::~AVISource() {
    AVISource::CleanUp();
}

void AVISource::CleanUp() {
    if (hic) {
        !ex ? ICDecompressEnd(hic) : ICDecompressExEnd(hic);
        ICClose(hic);
    }
    if (pvideo) delete pvideo;
    if (pfile)
        pfile->Release();
    AVIFileExit();
    if (pbiSrc)
        free(pbiSrc);
    if (srcbuffer)
        delete[] srcbuffer;
    vs_aligned_free(decbuf);
    // fixme
    //vsapi->freeFrame(last_frame);
}

const VSFrameRef *AVISource::GetFrame(int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {

    n = min(max(n, 0), vi[0].numFrames-1);
    dropped_frame=false;
    if (n != last_frame_no) {
        // find the last keyframe
        VDPosition keyframe = pvideo->NearestKeyFrame(n);
        // maybe we don't need to go back that far
        if (last_frame_no < n && last_frame_no >= keyframe)
            keyframe = last_frame_no+1;
        if (keyframe < 0) keyframe = 0;

        bool frameok = false;
        VSFrameRef *frame = vsapi->newVideoFrame(vi[0].format, vi[0].width, vi[0].height, NULL, core);
        VSFrameRef *alpha = NULL;
        if (numOutputs == 2)
            alpha = vsapi->newVideoFrame(vi[1].format, vi[1].width, vi[1].height, NULL, core);
        bool not_found_yet;
        do {
            not_found_yet=false;
            for (VDPosition i = keyframe; i <= n; ++i) {
                LRESULT error = DecompressFrame(i, i != n, frame, alpha, core, vsapi);
                if ((!dropped_frame) && (error == ICERR_OK)) frameok = true;   // Better safe than sorry
            }
            last_frame_no = n;

            if (!last_frame && !frameok) {  // Last keyframe was not valid.
                const VDPosition key_pre=keyframe;
                keyframe = pvideo->NearestKeyFrame(keyframe-1);
                if (keyframe < 0) keyframe = 0;
                if (keyframe == key_pre) {
                    sprintf(buf, "AVISource: could not find valid keyframe for frame %d.", n);
                    throw std::runtime_error(buf);
                }

                not_found_yet=true;
            }
        } while(not_found_yet);

        if (frameok) {
            vsapi->freeFrame(last_frame);
            vsapi->freeFrame(last_alpha_frame);
            last_frame = frame;
            last_alpha_frame = alpha;
        } else {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(last_alpha_frame);
        }
    }
    int o = vsapi->getOutputIndex(frameCtx);
    if (vsapi->getOutputIndex(frameCtx) == 0)
        return vsapi->cloneFrameRef(last_frame);
    else
        return vsapi->cloneFrameRef(last_alpha_frame);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.avisource", "avisource", "VapourSynth AVISource Port", VAPOURSYNTH_API_VERSION, 1, plugin);
    const char *args = "path:data[];pixel_type:data:opt;fourcc:data:opt;";
    registerFunc("AVISource", args, AVISource::create_AVISource, (void*) AVISource::MODE_NORMAL, plugin);
    registerFunc("AVIFileSource", args, AVISource::create_AVISource, (void*) AVISource::MODE_AVIFILE, plugin);
    registerFunc("OpenDMLSource", args, AVISource::create_AVISource, (void*) AVISource::MODE_OPENDML, plugin);
}
