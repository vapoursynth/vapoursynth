/*
* Copyright (c) 2014-2015 Fredrik Mellbin
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


// TODO:
// need to remember working dir on load in case something dicks around with it
// don't write frames twice in the event that a frame really is requested twice since it's a waste of time
// have some way to make sure all frames get written? add a separate function for writing frames that isn't a filter?

#include <Magick++.h>
#include <VapourSynth.h>
#include <VSHelper.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


static std::once_flag flag;

//////////////////////////////////////////
// Shared

static bool isGrayColorspace(Magick::ColorspaceType colorspace) {
    return colorspace == Magick::GRAYColorspace || colorspace == Magick::Rec601LumaColorspace || colorspace == Magick::Rec709LumaColorspace;
}

static void realInitMagick(VSCore *core, const VSAPI *vsapi) {
    std::string path;
#ifdef _WIN32
    const char *pathPtr = vsapi->getPluginPath(vsapi->getPluginById("com.vapoursynth.imwri", core));
    if (pathPtr) {
        path = pathPtr;
        for (auto &c : path)
            if (c == '/')
                c = '\\';
    }
#endif
    Magick::InitializeMagick(path.c_str());
}

static void initMagick(VSCore *core, const VSAPI *vsapi) {
    std::call_once(flag, realInitMagick, core, vsapi);
}

static std::string specialPrintf(const std::string &filename, int number) {
    std::string result;
    size_t copyPos = 0;
    size_t minWidth = 0;
    bool zeroPad = false;
    bool percentSeen = false;
    bool zeroPadSeen = false;
    bool minWidthSeen = false;

    for (size_t pos = 0; pos < filename.length(); pos++) {
        const char c = filename[pos];
        if (c == '%' && !percentSeen) {
            result += filename.substr(copyPos, pos - copyPos);
            copyPos = pos;
            percentSeen = true;
            continue;
        }
        if (percentSeen) {
            if (c == '0' && !zeroPadSeen) {
                zeroPad = true;
                zeroPadSeen = true;
                continue;
            }
            if (c >= '1' && c <= '9' && !minWidthSeen) {
                minWidth = c - '0';
                zeroPadSeen = true;
                minWidthSeen = true;
                continue;
            }
            if (c == 'd') {
                std::string num = std::to_string(number);
                if (minWidthSeen && minWidth > num.length())
                    num = std::string(minWidth - num.length(), zeroPad ? '0' : ' ') + num;
                result += num;
                copyPos = pos + 1;
            }
        }
        minWidth = 0;
        zeroPad = false;
        percentSeen = false;
        zeroPadSeen = false;
        minWidthSeen = false;
    }

    result += filename.substr(copyPos, filename.length() - copyPos);

    return result;
}

//////////////////////////////////////////
// Write

struct WriteData {
    VSNodeRef *videoNode;
    VSNodeRef *alphaNode;
    const VSVideoInfo *vi;
    std::string imgFormat;
    std::string filename;
    int firstNum;
    int quality;
    bool dither;

    WriteData() : videoNode(nullptr), alphaNode(nullptr), vi(nullptr), quality(0), dither(true) {}
};

static void VS_CC writeInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    WriteData *d = (WriteData *)* instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC writeGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    WriteData *d = (WriteData *)* instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->videoNode, frameCtx);
        if (d->alphaNode)
            vsapi->requestFrameFilter(n, d->alphaNode, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *frame = vsapi->getFrameFilter(n, d->videoNode, frameCtx);
        const VSFormat *fi = vsapi->getFrameFormat(frame);
        int width = vsapi->getFrameWidth(frame, 0);
        int height = vsapi->getFrameHeight(frame, 0);

        const VSFrameRef *alphaFrame = nullptr;
        int alphaWidth = 0;
        int alphaHeight = 0;

        if (d->alphaNode) {
            alphaFrame = vsapi->getFrameFilter(n, d->alphaNode, frameCtx);
            alphaWidth = vsapi->getFrameWidth(alphaFrame, 0);
            alphaHeight = vsapi->getFrameHeight(alphaFrame, 0);

            if (width != alphaWidth || height != alphaHeight) {
                vsapi->setFilterError("Write: Mismatched dimension of the alpha clip", frameCtx);
                vsapi->freeFrame(frame);
                vsapi->freeFrame(alphaFrame);
                return nullptr;
            }
        }

        try {
            Magick::Image image(Magick::Geometry(width, height), Magick::Color(0, 0, 0, 0));
            image.magick(d->imgFormat);
            image.quantizeDitherMethod(Magick::FloydSteinbergDitherMethod);
            image.quantizeDither(d->dither);
            image.quality(d->quality);
            if (alphaFrame)
                image.alphaChannel(Magick::ActivateAlphaChannel);

            bool isGray = fi->colorFamily == cmGray;
            if (isGray)
                image.colorSpace(Magick::GRAYColorspace);

            int strideR = vsapi->getStride(frame, 0);
            int strideG = vsapi->getStride(frame, isGray ? 0 : 1);
            int strideB = vsapi->getStride(frame, isGray ? 0 : 2);
            int strideA = 0;
            if (alphaFrame)
                strideA = vsapi->getStride(alphaFrame, 0);

            if (fi->bitsPerSample < static_cast<int>(image.depth()))
                image.depth(fi->bitsPerSample);

            Magick::Pixels pixelCache(image);

            if (fi->bytesPerSample == 2 && alphaFrame) {
                const int shiftL = 16 - fi->bitsPerSample;
                const int shiftR = fi->bitsPerSample;

                const uint16_t *r = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, 0));
                const uint16_t *g = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, isGray ? 0 : 1));
                const uint16_t *b = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, isGray ? 0 : 2));
                const uint16_t *a = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(alphaFrame, 0));

                for (int y = 0; y < height; y++) {
                    Magick::PixelPacket* pixels = pixelCache.get(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        pixels[x].red = (r[x] << shiftL) + (r[x] >> shiftR);
                        pixels[x].green = (g[x] << shiftL) + (b[x] >> shiftR);
                        pixels[x].blue = (b[x] << shiftL) + (b[x] >> shiftR);
                        pixels[x].opacity = (a[x] << shiftL) + (a[x] >> shiftR);
                    }

                    r += strideR / sizeof(uint16_t);
                    g += strideG / sizeof(uint16_t);
                    b += strideB / sizeof(uint16_t);
                    a += strideA / sizeof(uint16_t);

                    pixelCache.sync();
                }
            } else if (fi->bytesPerSample == 2) {
                const int shiftL = 16 - fi->bitsPerSample;
                const int shiftR = fi->bitsPerSample;

                const uint16_t *r = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, 0));
                const uint16_t *g = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, isGray ? 0 : 1));
                const uint16_t *b = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(frame, isGray ? 0 : 2));

                for (int y = 0; y < height; y++) {
                    Magick::PixelPacket* pixels = pixelCache.get(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        pixels[x].red = (r[x] << shiftL) + (r[x] >> shiftR);
                        pixels[x].green = (g[x] << shiftL) + (b[x] >> shiftR);
                        pixels[x].blue = (b[x] << shiftL) + (b[x] >> shiftR);
                        pixels[x].opacity = 0;
                    }

                    r += strideR / sizeof(uint16_t);
                    g += strideG / sizeof(uint16_t);
                    b += strideB / sizeof(uint16_t);

                    pixelCache.sync();
                }
            } else if (fi->bytesPerSample == 1 && alphaFrame) {
                const uint8_t *r = vsapi->getReadPtr(frame, 0);
                const uint8_t *g = vsapi->getReadPtr(frame, isGray ? 0 : 1);
                const uint8_t *b = vsapi->getReadPtr(frame, isGray ? 0 : 2);
                const uint8_t *a = vsapi->getReadPtr(alphaFrame, 0);

                for (int y = 0; y < height; y++) {
                    Magick::PixelPacket* pixels = pixelCache.get(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        pixels[x].red = (r[x] << 8) + r[x];
                        pixels[x].green = (g[x] << 8) + g[x];
                        pixels[x].blue = (b[x] << 8) + b[x];
                        pixels[x].opacity = (a[x] << 8) + a[x];
                    }

                    r += strideR;
                    g += strideG;
                    b += strideB;
                    a += strideA;

                    pixelCache.sync();
                }
            } else /*if (fi->bytesPerSample == 1)*/ {
                const uint8_t *r = vsapi->getReadPtr(frame, 0);
                const uint8_t *g = vsapi->getReadPtr(frame, isGray ? 0 : 1);
                const uint8_t *b = vsapi->getReadPtr(frame, isGray ? 0 : 2);

                for (int y = 0; y < height; y++) {
                    Magick::PixelPacket* pixels = pixelCache.get(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        pixels[x].red = (r[x] << 8) + r[x];
                        pixels[x].green = (g[x] << 8) + g[x];
                        pixels[x].blue = (b[x] << 8) + b[x];
                        pixels[x].opacity = 0;
                    }

                    r += strideR;
                    g += strideG;
                    b += strideB;

                    pixelCache.sync();
                }
            }

            image.write(specialPrintf(d->filename, n + d->firstNum));

            vsapi->freeFrame(alphaFrame);
            return frame;
        } catch (Magick::Exception &e) {
            vsapi->setFilterError((std::string("Write: ImageMagick error: ") + e.what()).c_str(), frameCtx);
            vsapi->freeFrame(frame);
            vsapi->freeFrame(alphaFrame);
            return nullptr;
        }
    }

    return nullptr;
}

static void VS_CC writeFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    WriteData *d = (WriteData *)instanceData;
    vsapi->freeNode(d->videoNode);
    vsapi->freeNode(d->alphaNode);
    delete d;
}

static void VS_CC writeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<WriteData> d(new WriteData());
    int err = 0;

    initMagick(core, vsapi);

    d->firstNum = int64ToIntS(vsapi->propGetInt(in, "firstnum", 0, &err));
    if (d->firstNum < 0) {
        vsapi->setError(out, "Write: Frame number offset can't be negative");
        return;
    }

    d->quality = int64ToIntS(vsapi->propGetInt(in, "quality", 0, &err));
    if (err)
        d->quality = 75;
    if (d->quality < 0 || d->quality > 100) {
        vsapi->setError(out, "Write: Quality must be between 0 and 100");
        return;
    }

    d->videoNode = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->videoNode);
    if (!d->vi->format || (d->vi->format->colorFamily != cmRGB && d->vi->format->colorFamily != cmGray) || d->vi->format->sampleType != stInteger || d->vi->format->bitsPerSample > 16) {
        vsapi->freeNode(d->videoNode);
        vsapi->setError(out, "Write: Only constant format 8-16 bit RGB and Grayscale input supported");
        return;
    }

    d->alphaNode = vsapi->propGetNode(in, "alpha", 0, &err);
    d->imgFormat = vsapi->propGetData(in, "imgformat", 0, nullptr);
    d->filename = vsapi->propGetData(in, "filename", 0, nullptr);
    d->dither = !!vsapi->propGetInt(in, "dither", 0, &err);
    if (err)
        d->dither = true;

    d->vi = vsapi->getVideoInfo(d->videoNode);
    if (d->alphaNode) {
        const VSVideoInfo *alphaVi = vsapi->getVideoInfo(d->alphaNode);
        if (d->vi->width != alphaVi->width || d->vi->height != alphaVi->height || !alphaVi->format ||
            alphaVi->format != vsapi->registerFormat(cmGray, d->vi->format->sampleType, d->vi->format->bitsPerSample, 0, 0, core)) {
            vsapi->freeNode(d->videoNode);
            vsapi->freeNode(d->alphaNode);
            vsapi->setError(out, "Write: Alpha clip dimensions and format don't match the main clip");
            return;
        }
        
    }

    if (specialPrintf(d->filename, 0) == d->filename) {
        // No valid digit substitution in the filename so error out to warn the user
        vsapi->freeNode(d->videoNode);
        vsapi->freeNode(d->alphaNode);
        vsapi->setError(out, "Write: Filename string doesn't contain a number");
        return;
    }

    vsapi->createFilter(in, out, "Write", writeInit, writeGetFrame, writeFree, fmParallelRequests, 0, d.release(), core);
}

//////////////////////////////////////////
// Read

struct ReadData {
    VSVideoInfo vi[2];
    std::vector<std::string> filenames;
    int firstNum;
    bool alpha;
    bool mismatch;
    bool fileListMode;
    int cachedFrameNum;
    bool cachedAlpha;
    const VSFrameRef *cachedFrame;

    ReadData() : fileListMode(true), cachedFrameNum(-1), cachedAlpha(false), cachedFrame(nullptr) {};
};

static void VS_CC readInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ReadData *d = (ReadData *)* instanceData;
    vsapi->setVideoInfo(d->vi, d->alpha ? 2 : 1, node);
}

static const VSFrameRef *VS_CC readGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ReadData *d = (ReadData *)* instanceData;

    if (activationReason == arInitial) {

        int index = vsapi->getOutputIndex(frameCtx);
        if (d->alpha && d->cachedFrameNum == n) {
            if ((index == 0 && !d->cachedAlpha) || (index == 1 && d->cachedAlpha)) {
                const VSFrameRef *frame = d->cachedFrame;
                d->cachedFrame = nullptr;
                d->cachedFrameNum = -1;
                return frame;
            }
        }

        VSFrameRef *frame = nullptr;
        VSFrameRef *alphaFrame = nullptr;
        
        try {
            Magick::Image image(d->fileListMode ? d->filenames[n] : specialPrintf(d->filenames[0], n + d->firstNum));
            VSColorFamily cf = cmRGB;
            if (isGrayColorspace(image.colorSpace()))
                cf = cmGray;

            int width = static_cast<int>(image.columns());
            int height = static_cast<int>(image.rows());
            int depth = std::min(std::max<int>(image.depth(), 8), 16);

            if (d->vi[0].format && (cf != d->vi[0].format->colorFamily || depth != d->vi[0].format->bitsPerSample)) {
                std::string err = "Read: Format mismatch for frame " + std::to_string(n) + ", is ";
                err += vsapi->registerFormat(cf, stInteger, depth, 0, 0, core)->name + std::string(" but should be ") + d->vi[0].format->name;
                vsapi->setFilterError(err.c_str(), frameCtx);
                return nullptr;
            }

            if (d->vi[0].width && (width != d->vi[0].width || height != d->vi[0].height)) {
                std::string err = "Read: Size mismatch for frame " + std::to_string(n) + ", is " + std::to_string(width) + "x" + std::to_string(height) + " but should be " + std::to_string(d->vi[0].width) + "x" + std::to_string(d->vi[0].height);
                vsapi->setFilterError(err.c_str(), frameCtx);
                return nullptr;
            }

            frame = vsapi->newVideoFrame(d->vi[0].format ? d->vi[0].format : vsapi->registerFormat(cf, stInteger, depth, 0, 0, core), width, height, nullptr, core);
            if (d->alpha)
                alphaFrame = vsapi->newVideoFrame(d->vi[1].format ? d->vi[1].format : vsapi->registerFormat(cmGray, stInteger, depth, 0, 0, core), width, height, nullptr, core);
            const VSFormat *fi = vsapi->getFrameFormat(frame);
 
            bool isGray = fi->colorFamily == cmGray;

            int strideR = vsapi->getStride(frame, 0);
            int strideG = vsapi->getStride(frame, isGray ? 0 : 1);
            int strideB = vsapi->getStride(frame, isGray ? 0 : 2);
            int strideA = 0;
            if (alphaFrame)
                strideA = vsapi->getStride(alphaFrame, 0);

            Magick::Pixels pixelCache(image);

            if (fi->bytesPerSample == 2 && alphaFrame) {
                const int shiftR = 16 - fi->bitsPerSample;

                uint16_t *r = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, 0));
                uint16_t *g = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, isGray ? 0 : 1));
                uint16_t *b = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, isGray ? 0 : 2));
                uint16_t *a = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(alphaFrame, 0));

                for (int y = 0; y < height; y++) {
                    const Magick::PixelPacket* pixels = pixelCache.getConst(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        r[x] = pixels[x].red >> shiftR;
                        g[x] = pixels[x].green >> shiftR;
                        b[x] = pixels[x].blue >> shiftR;
                        a[x] = pixels[x].opacity >> shiftR;
                    }

                    r += strideR / sizeof(uint16_t);
                    g += strideG / sizeof(uint16_t);
                    b += strideB / sizeof(uint16_t);
                    a += strideA / sizeof(uint16_t);
                }
            } else if (fi->bytesPerSample == 2) {
                const int shiftR = 16 - fi->bitsPerSample;

                uint16_t *r = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, 0));
                uint16_t *g = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, isGray ? 0 : 1));
                uint16_t *b = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(frame, isGray ? 0 : 2));

                for (int y = 0; y < height; y++) {
                    const Magick::PixelPacket* pixels = pixelCache.getConst(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        r[x] = pixels[x].red >> shiftR;
                        g[x] = pixels[x].green >> shiftR;
                        b[x] = pixels[x].blue >> shiftR;
                    }

                    r += strideR / sizeof(uint16_t);
                    g += strideG / sizeof(uint16_t);
                    b += strideB / sizeof(uint16_t);
                }
            } else if (fi->bytesPerSample == 1 && alphaFrame) {
                uint8_t *r = vsapi->getWritePtr(frame, 0);
                uint8_t *g = vsapi->getWritePtr(frame, isGray ? 0 : 1);
                uint8_t *b = vsapi->getWritePtr(frame, isGray ? 0 : 2);
                uint8_t *a = vsapi->getWritePtr(alphaFrame, 0);

                for (int y = 0; y < height; y++) {
                    const Magick::PixelPacket* pixels = pixelCache.getConst(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        r[x] = pixels[x].red >> 8;
                        g[x] = pixels[x].green >> 8;
                        b[x] = pixels[x].blue >> 8;
                        a[x] = pixels[x].opacity >> 8;
                    }

                    r += strideR;
                    g += strideG;
                    b += strideB;
                    a += strideA;
                }
            } else /*if (fi->bytesPerSample == 1)*/ {
                uint8_t *r = vsapi->getWritePtr(frame, 0);
                uint8_t *g = vsapi->getWritePtr(frame, isGray ? 0 : 1);
                uint8_t *b = vsapi->getWritePtr(frame, isGray ? 0 : 2);

                for (int y = 0; y < height; y++) {
                    const Magick::PixelPacket* pixels = pixelCache.getConst(0, y, width, 1);
                    for (int x = 0; x < width; x++) {
                        r[x] = pixels[x].red >> 8;
                        g[x] = pixels[x].green >> 8;
                        b[x] = pixels[x].blue >> 8;
                    }

                    r += strideR;
                    g += strideG;
                    b += strideB;
                }
            }
        } catch (Magick::Exception &e) {
            vsapi->setFilterError((std::string("Read: ImageMagick error: ") + e.what()).c_str(), frameCtx);
            vsapi->freeFrame(frame);
            vsapi->freeFrame(alphaFrame);
            return nullptr;
        }

        if (d->alpha) {
            d->cachedFrameNum = n;
            vsapi->freeFrame(d->cachedFrame);
            if (index == 0) {
                d->cachedAlpha = true;
                d->cachedFrame = alphaFrame;
                return frame;
            } else /* if (index == 1) */ {
                d->cachedAlpha = false;
                d->cachedFrame = frame;
                return alphaFrame;
            }
        } else {
            return frame;
        }
    }

    return nullptr;
}

static void VS_CC readFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ReadData *d = (ReadData *)instanceData;
    delete d;
}

static void VS_CC readCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ReadData> d(new ReadData());
    int err = 0;

    initMagick(core, vsapi);

    d->firstNum = int64ToIntS(vsapi->propGetInt(in, "firstnum", 0, &err));
    if (d->firstNum < 0) {
        vsapi->setError(out, "Read: Frame number offset can't be negative");
        return;
    }

    d->alpha = !!vsapi->propGetInt(in, "alpha", 0, &err);
    d->mismatch = !!vsapi->propGetInt(in, "mismatch", 0, &err);
    int numElem = vsapi->propNumElements(in, "filename");
    d->filenames.resize(numElem);
    for (int i = 0; i < numElem; i++)
        d->filenames[i] = vsapi->propGetData(in, "filename", i, nullptr);
    
    d->vi[0] = { nullptr, 30, 1, 0, 0, static_cast<int>(d->filenames.size()), 0 };
    // See if it's a single filename with number substitution and check how many files exist
    if (d->vi[0].numFrames == 1 && specialPrintf(d->filenames[0], 0) != d->filenames[0]) {
        d->fileListMode = false;

        for (int i = d->firstNum; i < INT_MAX; i++) {
#ifdef _WIN32
            std::string printedStr(specialPrintf(d->filenames[0], i));
            int numChars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, printedStr.c_str(), static_cast<int>(printedStr.length()), nullptr, 0);
            std::vector<wchar_t> charBuf(numChars + 1);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, printedStr.c_str(), static_cast<int>(printedStr.length()), charBuf.data(), static_cast<int>(charBuf.size()));
            FILE * f = _wfopen(charBuf.data(), L"rb");
#else
            FILE * f = fopen(specialPrintf(d->filenames[0], i).c_str(), "rb");
#endif
            if (f) {
                fclose(f);
            } else {
                d->vi[0].numFrames = i - d->firstNum;
                break;
            }
        }

        if (d->vi[0].numFrames == 0) {
            vsapi->setError(out, "Read: No files matching the given pattern exist");
            return;
        }
    }

    try {
        Magick::Image image;
        image.ping(d->fileListMode ? d->filenames[0] : specialPrintf(d->filenames[0], d->firstNum));
        int depth = std::min(std::max(static_cast<int>(image.depth()), 8), 16);

        if (!d->mismatch || d->vi[0].numFrames == 1) {
            d->vi[0].height = static_cast<int>(image.rows());
            d->vi[0].width = static_cast<int>(image.columns());
            VSColorFamily cf = cmRGB;
            if (isGrayColorspace(image.colorSpace()))
                cf = cmGray;
            d->vi[0].format = vsapi->registerFormat(cf, stInteger, depth, 0, 0, core);
        }

        if (d->alpha) {
            d->vi[1] = d->vi[0];
            if (d->vi[0].format)
                d->vi[1].format = vsapi->registerFormat(cmGray, stInteger, depth, 0, 0, core);
        }
    } catch (Magick::Exception &e) {
        const char *p = e.what();
        vsapi->setError(out, (std::string("Read: Failed to read image properties: ") + e.what()).c_str());
        return;
    }

    vsapi->createFilter(in, out, "Read", readInit, readGetFrame, readFree, fmUnordered, 0, d.release(), core);
}


//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.imwri", "imwri", "VapourSynth ImageMagick Writer/Reader", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Write", "clip:clip;imgformat:data;filename:data;firstnum:int:opt;quality:int:opt;dither:int:opt;alpha:clip:opt;", writeCreate, nullptr, plugin);
    registerFunc("Read", "filename:data[];firstnum:int:opt;mismatch:int:opt;alpha:int:opt;", readCreate, nullptr, plugin);
}
