/*
* Copyright (c) 2014-2019 Fredrik Mellbin
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
#include <VapourSynth4.h>
#include <VSHelper4.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../../common/vsutf16.h"
#include "../../core/filtershared.h"
#else
#include <unistd.h>
#endif
#include "../../core/version.h"

// Handle both with and without hdri
#if MAGICKCORE_HDRI_ENABLE
#define IMWRI_NAMESPACE "imwri"
#define IMWRI_PLUGIN_NAME "VapourSynth ImageMagick 7 HDRI Writer/Reader"
#define IMWRI_ID "com.vapoursynth.imwri"
#else
#error ImageMagick must be compiled with HDRI enabled
#endif

#if defined(MAGICKCORE_LCMS_DELEGATE)
#define IMWRI_HAS_LCMS2
#endif

// Because proper namespace handling is too hard for ImageMagick shitvelopers
using MagickCore::Quantum;

//////////////////////////////////////////
// Shared

static void initMagick(VSCore *core, const VSAPI *vsapi) {
    std::string path;
#ifdef _WIN32
    const char *pathPtr = vsapi->getPluginPath(vsapi->getPluginByID(IMWRI_ID, core));
    if (pathPtr) {
        path = pathPtr;
        for (auto &c : path)
            if (c == '/')
                c = '\\';
    }
#endif
    Magick::InitializeMagick(path.c_str());
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

static bool isAbsolute(const std::string &path) {
#ifdef _WIN32
    return path.size() > 1 && ((path[0] == '/' && path[1] == '/') || (path[0] == '\\' && path[1] == '\\') || path[1] == ':');
#else
    return path.size() && path[0] == '/';
#endif
}

static bool fileExists(const std::string &filename) {
#ifdef _WIN32
    FILE * f = _wfopen(utf16_from_utf8(filename).c_str(), L"rb");
#else
    FILE * f = fopen(filename.c_str(), "rb");
#endif
    if (f)
        fclose(f);
    return !!f;
}

static void getWorkingDir(std::string &path) {
#ifdef _WIN32
    DWORD size = GetCurrentDirectoryW(0, nullptr);
    std::vector<wchar_t> buffer(size);
    GetCurrentDirectoryW(size, buffer.data());
    path = utf16_to_utf8(buffer.data()) + '\\';
#else
    char *buffer = getcwd(nullptr, 0);

    if (buffer) {
        if (buffer[0] != '(') {
            path = buffer;
            path += '/';
        }
        free(buffer);
    }
#endif
}

//////////////////////////////////////////
// Write

struct WriteData {
    VSNode *videoNode;
    VSNode *alphaNode;
    const VSVideoInfo *vi;
    std::string imgFormat;
    std::string filename;
    std::string workingDir;
    int firstNum;
    int quality;
    MagickCore::CompressionType compressType;
    bool dither;
    bool overwrite;

    WriteData() : videoNode(nullptr), alphaNode(nullptr), vi(nullptr), quality(0), compressType(MagickCore::UndefinedCompression), dither(true) {}
};

template<typename T>
static void writeImageHelper(const VSFrame *frame, const VSFrame *alphaFrame, bool isGray, Magick::Image &image, int width, int height, int bitsPerSample, const VSAPI *vsapi) {
    unsigned prepeat = (MAGICKCORE_QUANTUM_DEPTH - 1) / bitsPerSample;
    unsigned pleftover = MAGICKCORE_QUANTUM_DEPTH - (bitsPerSample * prepeat);
    unsigned shiftFactor = bitsPerSample - pleftover;
    unsigned scaleFactor = 0;
    for (unsigned i = 0; i < prepeat; i++) {
        scaleFactor <<= bitsPerSample;
        scaleFactor += 1;
    }
    scaleFactor <<= pleftover;

    Magick::Pixels pixelCache(image);

    const T * VS_RESTRICT r = reinterpret_cast<const T *>(vsapi->getReadPtr(frame, 0));
    const T * VS_RESTRICT g = reinterpret_cast<const T *>(vsapi->getReadPtr(frame, isGray ? 0 : 1));
    const T * VS_RESTRICT b = reinterpret_cast<const T *>(vsapi->getReadPtr(frame, isGray ? 0 : 2));
    ptrdiff_t strideR = vsapi->getStride(frame, 0);
    ptrdiff_t strideG = vsapi->getStride(frame, isGray ? 0 : 1);
    ptrdiff_t strideB = vsapi->getStride(frame, isGray ? 0 : 2);
    ssize_t rOff = pixelCache.offset(MagickCore::RedPixelChannel);
    ssize_t gOff = pixelCache.offset(MagickCore::GreenPixelChannel);
    ssize_t bOff = pixelCache.offset(MagickCore::BluePixelChannel);
    size_t channels = image.channels();

    if (alphaFrame) {
        ssize_t aOff = pixelCache.offset(MagickCore::AlphaPixelChannel);
        ptrdiff_t strideA = vsapi->getStride(alphaFrame, 0);
        const T * VS_RESTRICT a = reinterpret_cast<const T *>(vsapi->getReadPtr(alphaFrame, 0));

        for (int y = 0; y < height; y++) {
            MagickCore::Quantum *pixels = pixelCache.get(0, y, width, 1);
            for (int x = 0; x < width; x++) {
                pixels[x * channels + rOff] = r[x] * scaleFactor + (r[x] >> shiftFactor);
                pixels[x * channels + gOff] = g[x] * scaleFactor + (g[x] >> shiftFactor);
                pixels[x * channels + bOff] = b[x] * scaleFactor + (b[x] >> shiftFactor);
                pixels[x * channels + aOff] = a[x] * scaleFactor + (a[x] >> shiftFactor);
            }

            r += strideR / sizeof(T);
            g += strideG / sizeof(T);
            b += strideB / sizeof(T);
            a += strideA / sizeof(T);

            pixelCache.sync();
        }
    } else {
        for (int y = 0; y < height; y++) {
            MagickCore::Quantum *pixels = pixelCache.get(0, y, width, 1);
            for (int x = 0; x < width; x++) {
                pixels[x * channels + rOff] = r[x] * scaleFactor + (r[x] >> shiftFactor);
                pixels[x * channels + gOff] = g[x] * scaleFactor + (g[x] >> shiftFactor);
                pixels[x * channels + bOff] = b[x] * scaleFactor + (b[x] >> shiftFactor);
            }

            r += strideR / sizeof(T);
            g += strideG / sizeof(T);
            b += strideB / sizeof(T);

            pixelCache.sync();
        }
    }
}

static const VSFrame *VS_CC writeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    WriteData *d = static_cast<WriteData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->videoNode, frameCtx);
        if (d->alphaNode)
            vsapi->requestFrameFilter(n, d->alphaNode, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *frame = vsapi->getFrameFilter(n, d->videoNode, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(frame);
        int width = vsapi->getFrameWidth(frame, 0);
        int height = vsapi->getFrameHeight(frame, 0);

        const VSFrame *alphaFrame = nullptr;
        int alphaWidth = 0;
        int alphaHeight = 0;

        std::string filename = specialPrintf(d->filename, n + d->firstNum);
        if (!isAbsolute(filename))
            filename = d->workingDir + filename;

        if (!d->overwrite && fileExists(filename))
            return frame;

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
            image.modulusDepth(fi->bitsPerSample);
            if (d->compressType != MagickCore::UndefinedCompression)
                image.compressType(d->compressType);
            image.quantizeDitherMethod(Magick::FloydSteinbergDitherMethod);
            image.quantizeDither(d->dither);
            image.quality(d->quality);
            image.alphaChannel(alphaFrame ? Magick::ActivateAlphaChannel : Magick::RemoveAlphaChannel);

            bool isGray = fi->colorFamily == cfGray;
            if (isGray)
                image.colorSpace(Magick::GRAYColorspace);

            if (fi->bytesPerSample == 4 && fi->sampleType == stFloat) {
                image.attribute("quantum:format", "floating-point");
                Magick::Pixels pixelCache(image);
                const Quantum scaleFactor = QuantumRange;

                const float * VS_RESTRICT r = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 0));
                const float * VS_RESTRICT g = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, isGray ? 0 : 1));
                const float * VS_RESTRICT b = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, isGray ? 0 : 2));
               
                ptrdiff_t strideR = vsapi->getStride(frame, 0);
                ptrdiff_t strideG = vsapi->getStride(frame, isGray ? 0 : 1);
                ptrdiff_t strideB = vsapi->getStride(frame, isGray ? 0 : 2);
                    
                ssize_t rOff = pixelCache.offset(MagickCore::RedPixelChannel);
                ssize_t gOff = pixelCache.offset(MagickCore::GreenPixelChannel);
                ssize_t bOff = pixelCache.offset(MagickCore::BluePixelChannel);
                size_t channels = image.channels();

                if (alphaFrame) {
                    const float * VS_RESTRICT a = reinterpret_cast<const float *>(vsapi->getReadPtr(alphaFrame, 0));
                    ptrdiff_t strideA = vsapi->getStride(alphaFrame, 0);
                    ssize_t aOff = pixelCache.offset(MagickCore::AlphaPixelChannel);
            
                    for (int y = 0; y < height; y++) {
                        MagickCore::Quantum* pixels = pixelCache.get(0, y, width, 1);
                        for (int x = 0; x < width; x++) {
                            pixels[x * channels + rOff] = r[x] * scaleFactor;
                            pixels[x * channels + gOff] = g[x] * scaleFactor;
                            pixels[x * channels + bOff] = b[x] * scaleFactor;
                            pixels[x * channels + aOff] = a[x] * scaleFactor;
                        }

                        r += strideR / sizeof(float);
                        g += strideG / sizeof(float);
                        b += strideB / sizeof(float);
                        a += strideA / sizeof(float);

                        pixelCache.sync();
                    }
                } else {
                    const float *r = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 0));
                    const float *g = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, isGray ? 0 : 1));
                    const float *b = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, isGray ? 0 : 2));

                    for (int y = 0; y < height; y++) {
                        MagickCore::Quantum* pixels = pixelCache.get(0, y, width, 1);
                        for (int x = 0; x < width; x++) {
                            pixels[x * channels + rOff] = r[x] * scaleFactor;
                            pixels[x * channels + gOff] = g[x] * scaleFactor;
                            pixels[x * channels + bOff] = b[x] * scaleFactor;
                        }

                        r += strideR / sizeof(float);
                        g += strideG / sizeof(float);
                        b += strideB / sizeof(float);

                        pixelCache.sync();
                    }
                }
            } else if (fi->bytesPerSample == 4) {
                writeImageHelper<uint32_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            } else if (fi->bytesPerSample == 2) {
                writeImageHelper<uint16_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            } else if (fi->bytesPerSample == 1) {
                writeImageHelper<uint8_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            }

            image.strip();

            image.write(filename);

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
    WriteData *d = static_cast<WriteData *>(instanceData);
    vsapi->freeNode(d->videoNode);
    vsapi->freeNode(d->alphaNode);
    delete d;
}

static void VS_CC writeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<WriteData> d(new WriteData());
    int err = 0;

    initMagick(core, vsapi);

    d->firstNum = vsapi->mapGetIntSaturated(in, "firstnum", 0, &err);
    if (d->firstNum < 0) {
        vsapi->mapSetError(out, "Write: Frame number offset can't be negative");
        return;
    }

    d->quality = vsapi->mapGetIntSaturated(in, "quality", 0, &err);
    if (err)
        d->quality = 75;
    if (d->quality < 0 || d->quality > 100) {
        vsapi->mapSetError(out, "Write: Quality must be between 0 and 100");
        return;
    }

    const char *compressType = vsapi->mapGetData(in, "compression_type", 0, &err);
    if (!err) {
        std::string s = compressType;
        std::transform(s.begin(), s.end(), s.begin(), toupper);
        if (s == "" || s == "UNDEFINED")
            d->compressType = MagickCore::UndefinedCompression;
        else if (s == "NONE")
            d->compressType = MagickCore::NoCompression;
        else if (s == "BZIP")
            d->compressType = MagickCore::BZipCompression;
        else if (s == "DXT1")
            d->compressType = MagickCore::DXT1Compression;
        else if (s == "DXT3")
            d->compressType = MagickCore::DXT3Compression;
        else if (s == "DXT5")
            d->compressType = MagickCore::DXT5Compression;
        else if (s == "FAX")
            d->compressType = MagickCore::FaxCompression;
        else if (s == "GROUP4")
            d->compressType = MagickCore::Group4Compression;
        else if (s == "JPEG")
            d->compressType = MagickCore::JPEGCompression;
        else if (s == "JPEG2000")
            d->compressType = MagickCore::JPEG2000Compression;
        else if (s == "LOSSLESSJPEG")
            d->compressType = MagickCore::LosslessJPEGCompression;
        else if (s == "LZW")
            d->compressType = MagickCore::LZWCompression;
        else if (s == "RLE")
            d->compressType = MagickCore::RLECompression;
        else if (s == "ZIP")
            d->compressType = MagickCore::ZipCompression;
        else if (s == "ZIPS")
            d->compressType = MagickCore::ZipSCompression;
        else if (s == "PIZ")
            d->compressType = MagickCore::PizCompression;
        else if (s == "PXR24")
            d->compressType = MagickCore::Pxr24Compression;
        else if (s == "B44")
            d->compressType = MagickCore::B44Compression;
        else if (s == "B44A")
            d->compressType = MagickCore::B44ACompression;
        else if (s == "LZMA")
            d->compressType = MagickCore::LZMACompression;
        else if (s == "JBIG1")
            d->compressType = MagickCore::JBIG1Compression;
        else if (s == "JBIG2")
            d->compressType = MagickCore::JBIG2Compression;
        else {
            vsapi->mapSetError(out, "Write: Unrecognized compression type");
            return;
        }
    }

    d->videoNode = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->videoNode);
    if ((d->vi->format.colorFamily != cfRGB && d->vi->format.colorFamily != cfGray)
        || (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 32))
    {
        vsapi->freeNode(d->videoNode);
        vsapi->mapSetError(out, "Write: Only constant format 8-32 bit integer or float RGB and Grayscale input supported");
        return;
    }

    d->alphaNode = vsapi->mapGetNode(in, "alpha", 0, &err);
    d->imgFormat = vsapi->mapGetData(in, "imgformat", 0, nullptr);
    d->filename = vsapi->mapGetData(in, "filename", 0, nullptr);
    d->dither = !!vsapi->mapGetInt(in, "dither", 0, &err);
    if (err)
        d->dither = true;
    d->overwrite = !!vsapi->mapGetInt(in, "overwrite", 0, &err);

    d->vi = vsapi->getVideoInfo(d->videoNode);
    if (d->alphaNode) {
        const VSVideoInfo *alphaVi = vsapi->getVideoInfo(d->alphaNode);
        VSVideoFormat alphaFormat;
        vsapi->queryVideoFormat(&alphaFormat, cfGray, d->vi->format.sampleType, d->vi->format.bitsPerSample, 0, 0, core);

        if (d->vi->width != alphaVi->width || d->vi->height != alphaVi->height || alphaVi->format.colorFamily == cfUndefined ||
            !vsh::isSameVideoFormat(&alphaVi->format, &alphaFormat)) {
            vsapi->freeNode(d->videoNode);
            vsapi->freeNode(d->alphaNode);
            vsapi->mapSetError(out, "Write: Alpha clip dimensions and format don't match the main clip");
            return;
        }
        
    }

    if (!d->overwrite && specialPrintf(d->filename, 0) == d->filename) {
        // No valid digit substitution in the filename so error out to warn the user
        vsapi->freeNode(d->videoNode);
        vsapi->freeNode(d->alphaNode);
        vsapi->mapSetError(out, "Write: Filename string doesn't contain a number");
        return;
    }

    getWorkingDir(d->workingDir);

    VSFilterDependency deps[] = {{ d->videoNode, rpStrictSpatial }, { d->alphaNode, rpStrictSpatial }};
    vsapi->createVideoFilter(out, "Write", d->vi, writeGetFrame, writeFree, fmParallelRequests, deps, d->alphaNode ? 2 : 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Read

struct ReadData {
    VSVideoInfo vi[2];
    std::vector<std::string> filenames;
    std::string workingDir;
    int firstNum;
    bool alpha;
    bool mismatch;
    bool fileListMode;
    bool floatOutput;
    int cachedFrameNum;
    bool cachedAlpha;
    bool embedICC;
    const VSFrameRef *cachedFrame;

    ReadData() : fileListMode(true) {};
};

template<typename T>
static void readImageHelper(VSFrame *frame, VSFrame *alphaFrame, bool isGray, Magick::Image &image, int width, int height, int bitsPerSample, const VSAPI *vsapi) {
    float outScale = ((1 << bitsPerSample) - 1) / static_cast<float>((1 << MAGICKCORE_QUANTUM_DEPTH) - 1);
    size_t channels = image.channels();
    Magick::Pixels pixelCache(image);

    T *r = reinterpret_cast<T *>(vsapi->getWritePtr(frame, 0));
    T *g = reinterpret_cast<T *>(vsapi->getWritePtr(frame, isGray ? 0 : 1));
    T *b = reinterpret_cast<T *>(vsapi->getWritePtr(frame, isGray ? 0 : 2));

    ptrdiff_t strideR = vsapi->getStride(frame, 0);
    ptrdiff_t strideG = vsapi->getStride(frame, isGray ? 0 : 1);
    ptrdiff_t strideB = vsapi->getStride(frame, isGray ? 0 : 2);

    ssize_t rOff = pixelCache.offset(MagickCore::RedPixelChannel);
    ssize_t gOff = pixelCache.offset(MagickCore::GreenPixelChannel);
    ssize_t bOff = pixelCache.offset(MagickCore::BluePixelChannel);
    ssize_t aOff = pixelCache.offset(MagickCore::AlphaPixelChannel);

    if (alphaFrame && aOff >= 0) {
        T *a = reinterpret_cast<T *>(vsapi->getWritePtr(alphaFrame, 0));
        ptrdiff_t strideA = vsapi->getStride(alphaFrame, 0);

        for (int y = 0; y < height; y++) {
            const Magick::Quantum *pixels = pixelCache.getConst(0, y, width, 1);
            for (int x = 0; x < width; x++) {
                r[x] = (unsigned)(pixels[x * channels + rOff] * outScale + .5f);
                g[x] = (unsigned)(pixels[x * channels + gOff] * outScale + .5f);
                b[x] = (unsigned)(pixels[x * channels + bOff] * outScale + .5f);
                a[x] = (unsigned)(pixels[x * channels + aOff] * outScale + .5f);
            }

            r += strideR / sizeof(T);
            g += strideG / sizeof(T);
            b += strideB / sizeof(T);
            a += strideA / sizeof(T);
        }
    } else {
        for (int y = 0; y < height; y++) {
            const Magick::Quantum *pixels = pixelCache.getConst(0, y, width, 1);
            for (int x = 0; x < width; x++) {
                r[x] = (unsigned)(pixels[x * channels + rOff] * outScale + .5f);
                g[x] = (unsigned)(pixels[x * channels + gOff] * outScale + .5f);
                b[x] = (unsigned)(pixels[x * channels + bOff] * outScale + .5f);
            }

            r += strideR / sizeof(T);
            g += strideG / sizeof(T);
            b += strideB / sizeof(T);
        }

        if (alphaFrame) {
            T *a = reinterpret_cast<T *>(vsapi->getWritePtr(alphaFrame, 0));
            ptrdiff_t strideA = vsapi->getStride(alphaFrame, 0);
            memset(a, 0, strideA  * height);
        }
    }
}

static void readSampleTypeDepth(const ReadData *d, const Magick::Image &image, VSSampleType &st, int &depth) {
        st = stInteger;
        depth = static_cast<int>(image.depth());
        if (depth == 32)
                st = stFloat;

        if (d->floatOutput || image.attribute("quantum:format") == "floating-point") {
                depth = 32;
                st = stFloat;
        }

        // VapourSynth does not support <8-bit integer types.
        if (depth < 8)
                depth = 8;
}

static std::string getVideoFormatName(const VSVideoFormat &f, const VSAPI *vsapi) {
    char name[32];
    if (vsapi->getVideoFormatName(&f, name))
        return name;
    else
        return "";
}

static const VSFrame *VS_CC readGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ReadData *d = static_cast<ReadData *>(instanceData);

    if (activationReason == arInitial) {
        VSFrame *frame = nullptr;
        VSFrame *alphaFrame = nullptr;
        
        try {
            std::string filename = d->fileListMode ? d->filenames[n] : specialPrintf(d->filenames[0], n + d->firstNum);
            if (!isAbsolute(filename))
                filename = d->workingDir + filename;

            Magick::Image image(filename);
            VSColorFamily cf = cfRGB;
            if (image.colorSpace() == Magick::GRAYColorspace)
                cf = cfGray;

            int width = static_cast<int>(image.columns());
            int height = static_cast<int>(image.rows());
            size_t channels = image.channels();

            VSSampleType st;
            int depth;
            readSampleTypeDepth(d, image, st, depth);

            if (d->vi[0].format.colorFamily != cfUndefined && (cf != d->vi[0].format.colorFamily || depth != d->vi[0].format.bitsPerSample)) {
                VSVideoFormat tmp;
                vsapi->queryVideoFormat(&tmp, cf, st, depth, 0, 0, core);

                std::string err = "Read: Format mismatch for frame " + std::to_string(n) + ", is ";
                err += getVideoFormatName(tmp, vsapi) + std::string(" but should be ") + getVideoFormatName(d->vi[0].format, vsapi);
                vsapi->setFilterError(err.c_str(), frameCtx);
                return nullptr;
            }

            if (d->vi[0].width && (width != d->vi[0].width || height != d->vi[0].height)) {
                std::string err = "Read: Size mismatch for frame " + std::to_string(n) + ", is " + std::to_string(width) + "x" + std::to_string(height) + " but should be " + std::to_string(d->vi[0].width) + "x" + std::to_string(d->vi[0].height);
                vsapi->setFilterError(err.c_str(), frameCtx);
                return nullptr;
            }

            VSVideoFormat fformat;
            vsapi->queryVideoFormat(&fformat, cf, stInteger, depth, 0, 0, core);
            frame = vsapi->newVideoFrame(&fformat, width, height, nullptr, core);

            if (d->alpha) {
                VSVideoFormat aformat;
                vsapi->queryVideoFormat(&aformat, cfGray, stInteger, depth, 0, 0, core);
                alphaFrame = vsapi->newVideoFrame(&aformat, width, height, nullptr, core);
            }

            const VSVideoFormat *fi = vsapi->getVideoFrameFormat(frame);
 
            bool isGray = fi->colorFamily == cfGray;                
     
            if (fi->bytesPerSample == 4 && fi->sampleType == stFloat) {
                const Quantum scaleFactor = QuantumRange;
                Magick::Pixels pixelCache(image);

                float *r = reinterpret_cast<float *>(vsapi->getWritePtr(frame, 0));
                float *g = reinterpret_cast<float *>(vsapi->getWritePtr(frame, isGray ? 0 : 1));
                float *b = reinterpret_cast<float *>(vsapi->getWritePtr(frame, isGray ? 0 : 2));

                ptrdiff_t strideR = vsapi->getStride(frame, 0);
                ptrdiff_t strideG = vsapi->getStride(frame, isGray ? 0 : 1);
                ptrdiff_t strideB = vsapi->getStride(frame, isGray ? 0 : 2);

                ssize_t rOff = pixelCache.offset(MagickCore::RedPixelChannel);
                ssize_t gOff = pixelCache.offset(MagickCore::GreenPixelChannel);
                ssize_t bOff = pixelCache.offset(MagickCore::BluePixelChannel);

                if (alphaFrame) {
                    float *a = reinterpret_cast<float *>(vsapi->getWritePtr(alphaFrame, 0));
                    ptrdiff_t strideA = vsapi->getStride(alphaFrame, 0);
                    ssize_t aOff = pixelCache.offset(MagickCore::AlphaPixelChannel);

                    if (aOff >= 0) {
                        for (int y = 0; y < height; y++) {
                            const MagickCore::Quantum* pixels = pixelCache.getConst(0, y, width, 1);
                            for (int x = 0; x < width; x++) {
                                r[x] = pixels[x * channels + rOff] / scaleFactor;
                                g[x] = pixels[x * channels + gOff] / scaleFactor;
                                b[x] = pixels[x * channels + bOff] / scaleFactor;
                                a[x] = pixels[x * channels + aOff] / scaleFactor;
                            }

                            r += strideR / sizeof(float);
                            g += strideG / sizeof(float);
                            b += strideB / sizeof(float);
                            a += strideA / sizeof(float);
                        }
                    } else {
                        memset(a, 0, strideA  * height);
                    }
                } else {
                    for (int y = 0; y < height; y++) {
                        const MagickCore::Quantum* pixels = pixelCache.getConst(0, y, width, 1);
                        for (int x = 0; x < width; x++) {
                            r[x] = pixels[x * channels + rOff] / scaleFactor;
                            g[x] = pixels[x * channels + gOff] / scaleFactor;
                            b[x] = pixels[x * channels + bOff] / scaleFactor;
                        }

                        r += strideR / sizeof(float);
                        g += strideG / sizeof(float);
                        b += strideB / sizeof(float);
                    }
                }
            } else if (fi->bytesPerSample == 4) {
                readImageHelper<uint32_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            } else if (fi->bytesPerSample == 2) {
                readImageHelper<uint16_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            } else if (fi->bytesPerSample == 1) {
                readImageHelper<uint8_t>(frame, alphaFrame, isGray, image, width, height, fi->bitsPerSample, vsapi);
            }
#if defined(IMWRI_HAS_LCMS2)
            if (d->embedICC) {
                const MagickCore::StringInfo *icc_profile = MagickCore::GetImageProfile(image.constImage(), "icc");
                if (icc_profile) {
                    vsapi->propSetData(vsapi->getFramePropsRW(frame), "_ICCProfile", reinterpret_cast<const char *>(icc_profile->datum), icc_profile->length, paReplace);
                }
            }
#endif
        } catch (Magick::Exception &e) {
            vsapi->setFilterError((std::string("Read: ImageMagick error: ") + e.what()).c_str(), frameCtx);
            vsapi->freeFrame(frame);
            vsapi->freeFrame(alphaFrame);
            return nullptr;
        }

        if (alphaFrame)
            vsapi->mapConsumeFrame(vsapi->getFramePropertiesRW(frame), "_Alpha", alphaFrame, maAppend);
        return frame;
    }

    return nullptr;
}

static void VS_CC readFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ReadData *d = static_cast<ReadData *>(instanceData);
    delete d;
}

static void VS_CC readCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ReadData> d(new ReadData());
    int err = 0;

    initMagick(core, vsapi);

    d->firstNum = vsapi->mapGetIntSaturated(in, "firstnum", 0, &err);
    if (d->firstNum < 0) {
        vsapi->mapSetError(out, "Read: Frame number offset can't be negative");
        return;
    }

    d->alpha = !!vsapi->mapGetInt(in, "alpha", 0, &err);
    d->mismatch = !!vsapi->mapGetInt(in, "mismatch", 0, &err);
    d->floatOutput = !!vsapi->mapGetInt(in, "float_output", 0, &err);
#if defined(IMWRI_HAS_LCMS2)
    d->embedICC = !!vsapi->propGetInt(in, "embed_icc", 0, &err);
#else
    d->embedICC = false;
#endif
    int numElem = vsapi->mapNumElements(in, "filename");
    d->filenames.resize(numElem);
    for (int i = 0; i < numElem; i++)
        d->filenames[i] = vsapi->mapGetData(in, "filename", i, nullptr);
    
    d->vi[0] = {{}, 30, 1, 0, 0, static_cast<int>(d->filenames.size())};
    // See if it's a single filename with number substitution and check how many files exist
    if (d->vi[0].numFrames == 1 && specialPrintf(d->filenames[0], 0) != d->filenames[0]) {
        d->fileListMode = false;

        for (int i = d->firstNum; i < INT_MAX; i++) {
            if (!fileExists(specialPrintf(d->filenames[0], i))) {
                d->vi[0].numFrames = i - d->firstNum;
                break;
            }
        }

        if (d->vi[0].numFrames == 0) {
            vsapi->mapSetError(out, "Read: No files matching the given pattern exist");
            return;
        }
    }

    try {
        Magick::Image image(d->fileListMode ? d->filenames[0] : specialPrintf(d->filenames[0], d->firstNum));

        VSSampleType st;
        int depth;
        readSampleTypeDepth(d.get(), image, st, depth);

        if (!d->mismatch || d->vi[0].numFrames == 1) {
            d->vi[0].height = static_cast<int>(image.rows());
            d->vi[0].width = static_cast<int>(image.columns());
            VSColorFamily cf = cfRGB;
            if (image.colorSpace() == Magick::GRAYColorspace)
                cf = cfGray;

            vsapi->queryVideoFormat(&d->vi[0].format, cf, st, depth, 0, 0, core);
        }

        if (d->alpha) {
            d->vi[1] = d->vi[0];
            if (d->vi[0].format.colorFamily != cfUndefined)
                vsapi->queryVideoFormat(&d->vi[1].format, cfGray, st, depth, 0, 0, core);
        }
    } catch (Magick::Exception &e) {
        vsapi->mapSetError(out, (std::string("Read: Failed to read image properties: ") + e.what()).c_str());
        return;
    }

    getWorkingDir(d->workingDir);

    vsapi->createVideoFilter(out, "Read", d->vi, readGetFrame, readFree, fmUnordered, nullptr, 0, d.get(), core);
    d.release();
}


//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(IMWRI_ID, IMWRI_NAMESPACE, IMWRI_PLUGIN_NAME, VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Write", "clip:clip;imgformat:data;filename:data;firstnum:int:opt;quality:int:opt;dither:int:opt;compression_type:data:opt;overwrite:int:opt;alpha:clip:opt;", "clip:vnode;", writeCreate, nullptr, plugin);
    vspapi->registerFunction("Read", "filename:data[];firstnum:int:opt;mismatch:int:opt;alpha:int:opt;float_output:int:opt;embed_icc:int:opt;", "clip:vnode;", readCreate, nullptr, plugin);
}