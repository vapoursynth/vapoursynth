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

#include "../core/x86utils.h"
#include "../core/cpufeatures.h"
#include "../core/filtershared.h"
#include "../core/version.h"
#include "avisynth_compat.h"
#include <algorithm>
#include <cstdarg>
#include <limits>
#include "../common/vsutf16.h"
#include "p2p_api.h"

#include <Windows.h>

extern const AVS_Linkage* const AVS_linkage;

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace

using namespace vsh;

namespace AvisynthCompat {

static inline bool IsSameVideoFormat(const VSVideoFormat &f, unsigned colorFamily, unsigned sampleType, unsigned bitsPerSample, unsigned subSamplingW = 0, unsigned subSamplingH = 0) noexcept {
    return f.colorFamily == colorFamily && f.sampleType == sampleType && f.bitsPerSample == bitsPerSample && f.subSamplingW == subSamplingW && f.subSamplingH == subSamplingH;
}

static int VSFormatToAVSPixelType(const VSVideoFormat &fi, bool pack) {
    if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 0, 0))
        return VideoInfo::CS_YV24;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0) && pack)
        return VideoInfo::CS_YUY2;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 0))
        return VideoInfo::CS_YV16;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 1, 1))
        return VideoInfo::CS_YV12;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 2, 2))
        return VideoInfo::CS_YUV9;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 8, 2, 0))
        return VideoInfo::CS_YV411;
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 8))
        return VideoInfo::CS_Y8;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 0, 0))
        return VideoInfo::CS_YUV444P10;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 0))
        return VideoInfo::CS_YUV422P10;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 10, 1, 1))
        return VideoInfo::CS_YUV420P10;
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 10))
        return VideoInfo::CS_Y10;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 12, 0, 0))
        return VideoInfo::CS_YUV444P12;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 12, 1, 0))
        return VideoInfo::CS_YUV422P12;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 12, 1, 1))
        return VideoInfo::CS_YUV420P12;
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 12))
        return VideoInfo::CS_Y12;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 14, 0, 0))
        return VideoInfo::CS_YUV444P14;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 14, 1, 0))
        return VideoInfo::CS_YUV422P14;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 14, 1, 1))
        return VideoInfo::CS_YUV420P14;
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 14))
        return VideoInfo::CS_Y14;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 0, 0))
        return VideoInfo::CS_YUV444P16;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 0))
        return VideoInfo::CS_YUV422P16;
    else if (IsSameVideoFormat(fi, cfYUV, stInteger, 16, 1, 1))
        return VideoInfo::CS_YUV420P16;
    else if (IsSameVideoFormat(fi, cfGray, stInteger, 16))
        return VideoInfo::CS_Y16;
    else if (IsSameVideoFormat(fi, cfYUV, stFloat, 32, 0, 0))
        return VideoInfo::CS_YUV444PS;
    else if (IsSameVideoFormat(fi, cfYUV, stFloat, 32, 1, 0))
        return VideoInfo::CS_YUV422PS;
    else if (IsSameVideoFormat(fi, cfYUV, stFloat, 32, 1, 1))
        return VideoInfo::CS_YUV420PS;
    else if (IsSameVideoFormat(fi, cfGray, stFloat, 32))
        return VideoInfo::CS_Y32;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 8) && pack)
        return VideoInfo::CS_BGR32;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 8))
        return VideoInfo::CS_RGBP;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 10))
        return VideoInfo::CS_RGBP10;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 12))
        return VideoInfo::CS_RGBP12;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 14))
        return VideoInfo::CS_RGBP14;
    else if (IsSameVideoFormat(fi, cfRGB, stInteger, 16))
        return VideoInfo::CS_RGBP16;
    else
        return 0;
}

static bool AVSPixelTypeToVSFormat(VSVideoFormat &f, bool &unpack, const VideoInfo &vi, VSCore *core, const VSAPI *vsapi) {
    unpack = false;

    if (vi.IsYUY2()) {
        unpack = true;
        return vsapi->getVideoFormatByID(&f, pfGray16, core);
    } else if (vi.IsRGB32()) {
        unpack = true;
        return vsapi->getVideoFormatByID(&f, pfGray32, core);
    }

    if (vi.IsPlanar()) {
        bool hasSubSampling = vi.IsYUV();
        unsigned colorFamily = vi.IsYUV() ? cfYUV : (vi.IsRGB() ? cfRGB : (vi.IsY() ? cfGray : 0));
        return vsapi->queryVideoFormat(&f, colorFamily, vi.BitsPerComponent() == 32 ? stFloat : stInteger, vi.BitsPerComponent(), hasSubSampling ? vi.GetPlaneWidthSubsampling(PLANAR_U) : 0, hasSubSampling ? vi.GetPlaneHeightSubsampling(PLANAR_U) : 0, core);
    }

    return false;
}

//////////////////////////////////////////
// PackYUY2

typedef struct {
    VSVideoInfo vi;
} PackYUY2DataExtra;

typedef SingleNodeData<PackYUY2DataExtra> PackYUY2Data;

static const VSFrame *VS_CC packYUY2GetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PackYUY2Data *d = reinterpret_cast<PackYUY2Data *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);

        p2p_buffer_param p = {};
        p.packing = p2p_yuy2;
        p.width = d->vi.width;
        p.height = d->vi.height;
        p.dst[0] = vsapi->getWritePtr(dst, 0);
        p.dst_stride[0] = vsapi->getStride(dst, 0);

        for (int plane = 0; plane < 3; plane++) {
            p.src[plane] = vsapi->getReadPtr(src, plane);
            p.src_stride[plane] = vsapi->getStride(src, plane);
        }

        p2p_pack_frame(&p, 0);

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

static VSNode *VS_CC packYUY2Create(VSNode *node, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PackYUY2Data> d(new PackYUY2Data(vsapi));

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core) != pfYUV422P8)
        return nullptr;

    d->node = node;
    d->vi = *vi;
    vsapi->getVideoFormatByID(&d->vi.format, pfGray16, core);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    VSNode *ret = vsapi->createVideoFilter2("PackYUY2", &d->vi, packYUY2GetFrame, filterFree<PackYUY2Data>, fmParallel, deps, 1, d.get(), core);
    d.release();
    return ret;
}

//////////////////////////////////////////
// UnpackYUY2

typedef struct {
    VSVideoInfo vi;
} UnpackYUY2DataExtra;

typedef SingleNodeData<UnpackYUY2DataExtra> UnpackYUY2Data;

static const VSFrame *VS_CC unpackYUY2GetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    UnpackYUY2Data *d = reinterpret_cast<UnpackYUY2Data *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);

        p2p_buffer_param p = {};
        p.packing = p2p_yuy2;
        p.width = d->vi.width;
        p.height = d->vi.height;
        p.src[0] = vsapi->getReadPtr(src, 0);
        p.src_stride[0] = vsapi->getStride(src, 0);

        for (int plane = 0; plane < 3; plane++) {
            p.dst[plane] = vsapi->getWritePtr(dst, plane);
            p.dst_stride[plane] = vsapi->getStride(dst, plane);
        }

        p2p_unpack_frame(&p, 0);

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

static VSNode *VS_CC unpackYUY2Create(VSNode *node, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<UnpackYUY2Data> d(new UnpackYUY2Data(vsapi));

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core) != pfGray16)
        return nullptr;

    d->node = node;
    d->vi = *vi;
    vsapi->getVideoFormatByID(&d->vi.format, pfYUV422P8, core);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    VSNode *ret = vsapi->createVideoFilter2("UnpackRGB32", &d->vi, unpackYUY2GetFrame, filterFree<UnpackYUY2Data>, fmParallel, deps, 1, d.get(), core);
    d.release();
    return ret;
}

//////////////////////////////////////////
// PackRGB32

typedef struct {
    VSVideoInfo vi;
} PackRGB32DataExtra;

typedef SingleNodeData<PackRGB32DataExtra> PackRGB32Data;

static const VSFrame *VS_CC packRGB32GetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PackRGB32Data *d = reinterpret_cast<PackRGB32Data *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);

        p2p_buffer_param p = {};
        p.packing = p2p_argb32;
        p.width = d->vi.width;
        p.height = d->vi.height;
        p.dst[0] = vsapi->getWritePtr(dst, 0);
        p.dst_stride[0] = vsapi->getStride(dst, 0);

        for (int plane = 0; plane < 3; plane++) {
            p.src[plane] = vsapi->getReadPtr(src, plane);
            p.src_stride[plane] = vsapi->getStride(src, plane);
        }

        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

static VSNode *VS_CC packRGB32Create(VSNode *node, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PackRGB32Data> d(new PackRGB32Data(vsapi));

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core) != pfRGB24)
        return nullptr;

    d->node = node;
    d->vi = *vi;
    vsapi->getVideoFormatByID(&d->vi.format, pfGray32, core);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    VSNode *ret = vsapi->createVideoFilter2("PackRGB32", &d->vi, packRGB32GetFrame, filterFree<PackRGB32Data>, fmParallel, deps, 1, d.get(), core);
    d.release();
    return ret;
}

//////////////////////////////////////////
// UnpackRGB32

typedef struct {
    VSVideoInfo vi;
} UnpackRGB32DataExtra;

typedef SingleNodeData<UnpackRGB32DataExtra> UnpackRGB32Data;

static const VSFrame *VS_CC unpackRGB32GetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    UnpackRGB32Data *d = reinterpret_cast<UnpackRGB32Data *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);

        p2p_buffer_param p = {};
        p.packing = p2p_argb32;
        p.width = d->vi.width;
        p.height = d->vi.height;
        p.src[0] = vsapi->getReadPtr(src, 0);
        p.src_stride[0] = vsapi->getStride(src, 0);

        for (int plane = 0; plane < 3; plane++) {
            p.dst[plane] = vsapi->getWritePtr(dst, plane);
            p.dst_stride[plane] = vsapi->getStride(dst, plane);
        }

        p2p_unpack_frame(&p, 0);

        vsapi->freeFrame(src);

        return dst;
    }

    return nullptr;
}

static VSNode *VS_CC unpackRGB32Create(VSNode *node, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<UnpackRGB32Data> d(new UnpackRGB32Data(vsapi));

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core) != pfGray32)
        return nullptr;

    d->node = node;
    d->vi = *vi;
    vsapi->getVideoFormatByID(&d->vi.format, pfRGB24, core);

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    VSNode *ret = vsapi->createVideoFilter2("UnpackRGB32", &d->vi, unpackRGB32GetFrame, filterFree<UnpackRGB32Data>, fmParallel, deps, 1, d.get(), core);
    d.release();
    return ret;
}

//////////////////////////////////////////

const VSFrame *FakeAvisynth::avsToVSFrame(VideoFrame *frame) {
    const VSFrame *ref = nullptr;
    std::map<VideoFrame *, const VSFrame *>::iterator it = ownedFrames.find(frame);

    if (it != ownedFrames.end()) {
        ref = vsapi->addFrameRef(it->second);
    } else {
        vsapi->logMessage(mtFatal, "unreachable condition", core);
        assert(false);
    }

    it = ownedFrames.begin();

    while (it != ownedFrames.end()) {
        if (it->first->refcount == 0 || it->first->refcount == 9000) {
            delete it->first;
            vsapi->freeFrame(it->second);
            it = ownedFrames.erase(it);
        } else {
            ++it;
        }
    }

    assert(ref);
    return ref;
}

FakeAvisynth::~FakeAvisynth() {
    std::map<VideoFrame *, const VSFrame *>::iterator it = ownedFrames.begin();

    while (it != ownedFrames.end()) {
        delete it->first;
        vsapi->freeFrame(it->second);
        it = ownedFrames.erase(it);
    }

    ownedFrames.clear();
}

int FakeAvisynth::GetCPUFlags() {
    const CPUFeatures *cpuf = getCPUFeatures();
    int flags = CPUF_FPU | CPUF_MMX | CPUF_INTEGER_SSE | CPUF_SSE | CPUF_SSE2; // minimum to run VS
    if (cpuf->sse3)      flags |= CPUF_SSE3;
    if (cpuf->ssse3)     flags |= CPUF_SSSE3;
    if (cpuf->sse4_1)    flags |= CPUF_SSE4_1;
    if (cpuf->sse4_2)    flags |= CPUF_SSE4_2;
    if (cpuf->avx)       flags |= CPUF_AVX;
    if (cpuf->avx2)      flags |= CPUF_AVX2;
    if (cpuf->fma3)      flags |= CPUF_FMA3;
    if (cpuf->f16c)      flags |= CPUF_F16C;
    if (cpuf->avx512_f)  flags |= CPUF_AVX512F;
    if (cpuf->avx512_bw) flags |= CPUF_AVX512BW;
    if (cpuf->avx512_dq) flags |= CPUF_AVX512DQ;
    if (cpuf->avx512_cd) flags |= CPUF_AVX512CD;
    if (cpuf->avx512_vl) flags |= CPUF_AVX512VL;
    return flags;
}

char *FakeAvisynth::SaveString(const char *s, int length) {
    auto strIter = (length >= 0) ? savedStrings.emplace(s, length) : savedStrings.emplace(s);
    // UGLY
    // Cast away the const because nobody would actually try to write to a saved string (except possibly an avisynth plugin developer)
    return const_cast<char *>(strIter.first->c_str());
}

char *FakeAvisynth::Sprintf(const char *fmt, ...) {
    va_list val;
    va_start(val, fmt);
    char *result = VSprintf(fmt, val);
    va_end(val);
    return result;
}

char *FakeAvisynth::VSprintf(const char *fmt, void *val) {
    std::vector<char> buf;
    int size = 0, count = -1;

    while (count == -1) {
        buf.resize(buf.size() + 4096);
        count = vsnprintf(buf.data(), size - 1, fmt, (va_list)val);
    }

    char *i = SaveString(buf.data());
    return i;
}

void FakeAvisynth::ThrowError(const char *fmt, ...) {
    va_list val;
    va_start(val, fmt);
    char buf[8192];
    _vsnprintf(buf, sizeof(buf) - 1, fmt, val);
    va_end(val);
    buf[sizeof(buf)-1] = '\0';
    throw AvisynthError(SaveString(buf));
}

std::string FakeAvisynth::charToFilterArgumentString(char c) {
    switch (c) {
    case 'i':
    case 'b':
        return "int";
    case 'f':
        return "float";
    case 's':
        return "data";
    case 'c':
        return "vnode";
    default:
        vsapi->logMessage(mtFatal, "Avisynth Compat: invalid argument type character, I quit", core);
        return "";
    }
}

VSClip::VSClip(VSNode *inclip, FakeAvisynth *fakeEnv, bool pack, const VSAPI *vsapi)
    : clip(inclip), fakeEnv(fakeEnv), vsapi(vsapi), numSlowWarnings(0) {
    const VSVideoInfo *srcVi = vsapi->getVideoInfo(clip);

    if (pack) {
        if (IsSameVideoFormat(srcVi->format, cfRGB, stInteger, 8, 0, 0)) {
            clip = packRGB32Create(clip, fakeEnv->core, vsapi);
            assert(clip);
        } else if (IsSameVideoFormat(srcVi->format, cfYUV, stInteger, 8, 1, 0)) {
            clip = packYUY2Create(clip, fakeEnv->core, vsapi);
            assert(clip);
        }
    }

    vi = {};
    vi.width = srcVi->width;
    vi.height = srcVi->height;

    vi.pixel_type = VSFormatToAVSPixelType(srcVi->format, pack);
    if (!vi.pixel_type)
        vsapi->logMessage(mtFatal, "Bad colorspace", fakeEnv->core);

    vi.image_type = VideoInfo::IT_BFF;
    vi.fps_numerator = int64ToIntS(srcVi->fpsNum);
    vi.fps_denominator = int64ToIntS(srcVi->fpsDen);
    vi.num_frames = srcVi->numFrames;
    vi.sample_type = SAMPLE_INT16;
}

PVideoFrame VSClip::GetFrame(int n, IScriptEnvironment *env) {
    const VSFrame *ref;
    n = std::min(std::max(0, n), vi.num_frames - 1);

    if (fakeEnv->initializing)
        ref = vsapi->getFrame(n, clip, nullptr, 0);
    else
        ref = vsapi->getFrameFilter(n, clip, fakeEnv->uglyCtx);

    std::vector<char> buf(1024);

    if (!ref) {
        if (numSlowWarnings < 200) {
            numSlowWarnings++;
            std::string s = "Avisynth Compat: requested frame "_s + std::to_string(n) + " not prefetched, using slow method";
            vsapi->logMessage(mtWarning, s.c_str(), fakeEnv->core);
        }
        ref = vsapi->getFrame(n, clip, buf.data(), static_cast<int>(buf.size()));
    }

    if (!ref)
        vsapi->logMessage(mtFatal, ("Avisynth Compat: error while getting input frame synchronously: "_s + buf.data()).c_str(), fakeEnv->core);

    bool isMultiplePlanes = (vi.pixel_type & VideoInfo::CS_PLANAR) && !(vi.pixel_type & VideoInfo::CS_INTERLEAVED);

    const uint8_t *firstPlanePtr = vsapi->getReadPtr(ref, 0);

    VideoFrame *vfb = new VideoFrame(
        // the data will never be modified due to the writable protections embedded in this mess
        (BYTE *)firstPlanePtr,
        false,
        0,
        static_cast<int>(vsapi->getStride(ref, 0)),
        vsapi->getFrameWidth(ref, 0) * vsapi->getVideoFrameFormat(ref)->bytesPerSample,
        vsapi->getFrameHeight(ref, 0),
        isMultiplePlanes ? vsapi->getReadPtr(ref, 1) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getReadPtr(ref, 2) - firstPlanePtr : 0,
        isMultiplePlanes ? static_cast<int>(vsapi->getStride(ref, 1)) : 0,
        vsapi->getFrameWidth(ref, 1) * vsapi->getVideoFrameFormat(ref)->bytesPerSample,
        vsapi->getFrameHeight(ref, 1));
    PVideoFrame pvf(vfb);
    fakeEnv->ownedFrames.insert(std::make_pair(vfb, ref));
    return pvf;
}

WrappedClip::WrappedClip(const std::string &filterName, const PClip &clip, const std::vector<VSNode *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv)
    : filterName(filterName), prefetchInfo(prefetchInfo), preFetchClips(preFetchClips), clip(clip), fakeEnv(fakeEnv) {
}

static void prefetchHelper(int n, VSNode *node, const PrefetchInfo &p, VSFrameContext *frameCtx, const VSAPI *vsapi) {
    n /= p.div;
    n *= p.mul;

    for (int i = n + p.from; i <= n + p.to; i++) {
        if (i < 0)
            continue;

        vsapi->requestFrameFilter(i, node, frameCtx);
    }
}

#define WARNING(fname, warning) if (name == #fname) vsapi->logMessage(mtWarning, "Avisynth Compat: "_s + #fname + " - " + #warning).c_str(), core);
#define BROKEN(fname) if (name == #fname) vsapi->logMessage(mtWarning, ("Avisynth Compat: Invoking known broken function "_s + name).c_str(), core);
#define OTHER(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define SOURCE(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define PREFETCHR0(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define PREFETCHR1(fname) if (name == #fname) return PrefetchInfo(1, 1, -1, 1);
#define PREFETCHR2(fname) if (name == #fname) return PrefetchInfo(1, 1, -2, 2);
#define PREFETCHR3(fname) if (name == #fname) return PrefetchInfo(1, 1, -3, 3);
#define PREFETCH(fname, div, mul, from, to) if (name == #fname) return PrefetchInfo(div, mul, from, to);

static PrefetchInfo getPrefetchInfo(const std::string &name, const VSMap *in, VSCore *core, const VSAPI *vsapi) {
    int err;
    int temp;
    // FFMS2
    OTHER(FFIndex)
    SOURCE(FFVideoSource)
    PREFETCHR0(SWScale)
    OTHER(FFSetLogLevel)
    OTHER(FFGetLogLevel)
    OTHER(FFGetVersion)
    // TNLMeans
    temp = vsapi->mapGetIntSaturated(in, "Az", 0, &err);
    PREFETCH(TNLMeans, 1, 1, -temp, temp)
    // yadif*
    PREFETCHR1(Yadif)
    PREFETCHR0(yadifmod)
    // *EDI*
    PREFETCHR0(eedi3)
    PREFETCHR0(eedi3_rpow2)
    PREFETCHR0(nnedi2)
    PREFETCHR0(nnedi2_rpow2)
    PREFETCHR0(nnedi3)
    PREFETCHR0(nnedi3_rpow2)
    // mixed Tritical
    temp = vsapi->mapGetIntSaturated(in, "mode", 0, &err);
    switch(temp) {
    case 0:
    case -1:
    case -2:
        PREFETCHR2(TDeint); break;
    case 2:
        PREFETCHR3(TDeint); break;
    case 1:
        PREFETCH(TDeint, 2, 1, -2, 2); break;
    }
    BROKEN(ColorMatrix)
    PREFETCHR1(Cnr2)
    temp = vsapi->mapGetIntSaturated(in, "tbsize", 0, &err);
    PREFETCH(dfttest, 1, 1, -(temp / 2), temp / 2)

    // MPEG2DEC
    SOURCE(MPEG2Source)
    PREFETCHR0(LumaYV12)
    PREFETCHR0(BlindPP)
    PREFETCHR0(Deblock)

    // Meow
    SOURCE(DGSource)
    PREFETCHR0(DGDenoise)
    PREFETCHR0(DGSharpen)
    temp = vsapi->mapGetIntSaturated(in, "mode", 0, &err);
    PREFETCH(DGBob, (temp > 0) ? 2 : 1, 1, -2, 2) // close enough?
    BROKEN(IsCombed)
    PREFETCHR0(FieldDeinterlace)
    PREFETCH(Telecide, 1, 1, -2, 10) // not good
    PREFETCH(DGTelecide, 1, 1, -2, 10) // also not good
    temp = vsapi->mapGetIntSaturated(in, "cycle", 0, &err);
    PREFETCH(DGDecimate, temp - 1, temp, -(temp + 3), temp + 3) // probably suboptimal
    PREFETCH(Decimate, temp - 1, temp, -(temp + 3), temp + 3) // probably suboptimal too

    // Masktools2
    PREFETCHR0(mt_edge)
    PREFETCHR1(mt_motion)
    PREFETCHR0(mt_expand)
    PREFETCHR0(mt_inpand)
    PREFETCHR0(mt_inflate)
    PREFETCHR0(mt_lut)
    PREFETCHR0(mt_lutxy)
    PREFETCHR0(mt_lutf)
    PREFETCHR0(mt_luts)
    PREFETCHR0(mt_makediff)
    PREFETCHR0(mt_adddiff)
    PREFETCHR0(mt_average)
    PREFETCHR0(mt_clamp)
    PREFETCHR0(mt_merge)
    PREFETCHR0(mt_logic)
    PREFETCHR0(mt_hysteresis)
    PREFETCHR0(mt_invert)
    PREFETCHR0(mt_binarize)
    PREFETCHR0(mt_convolution)
    PREFETCHR0(mt_mappedblur)
    OTHER(mt_circle)
    OTHER(mt_square)
    OTHER(mt_diamond)
    OTHER(mt_losange)
    OTHER(mt_rectangle)
    OTHER(mt_ellipse)
    OTHER(mt_polish)
    // Mixed
    BROKEN(RemoveGrain)
    BROKEN(Repair)
    PREFETCHR0(VagueDenoiser)
    PREFETCHR0(UnDot)
    PREFETCHR0(SangNom)
    PREFETCHR0(gradfun2db)
    PREFETCHR0(debilinear)
    PREFETCHR0(debilinearY)
    SOURCE(dss2)
    SOURCE(DirectShowSource)
    PREFETCHR0(SCXvid)
    PREFETCHR0(ResampleHQ)
    PREFETCHR0(AssRender)
    PREFETCHR0(TextSub)
    PREFETCHR0(VobSub)
    PREFETCHR2(fft3dGPU)
    PREFETCHR2(FFT3DFilter)
    PREFETCHR1(Convolution3D)
    PREFETCHR1(deen)
    PREFETCHR0(eDeen)
    // Mvtools
    PREFETCHR0(MSuper)
    temp = vsapi->mapGetIntSaturated(in, "delta", 0, &err);
    if (temp < 1)
        temp = 1;
    PREFETCH(MAnalyse, 1, 1, -temp, temp)
    PREFETCHR3(MDegrain1)
    PREFETCHR3(MDegrain2)
    PREFETCHR3(MDegrain3)
    PREFETCHR3(MCompensate)
    PREFETCHR0(MMask)
    PREFETCHR0(MSCDetection)
    PREFETCHR0(MShow)
    PREFETCHR0(MDepan)
    PREFETCHR0(MFlow)
    PREFETCHR1(MFlowInter)
    PREFETCHR1(MFlowFps)
    PREFETCHR1(MBlockFps)
    PREFETCHR1(MFlowBlur)
    PREFETCHR1(MRecalculate)

    // aWarpShit
    PREFETCHR0(aBlur)
    PREFETCHR0(aSobel)
    PREFETCHR0(aWarp)
    PREFETCHR0(aWarp4)
    PREFETCHR0(aWarpSharp)
    PREFETCHR0(aWarpSharp2)

    // CullResize
    PREFETCHR0(CullBilinearResize)
    PREFETCHR0(CullBicubicResize)
    PREFETCHR0(CullLanczosResize)
    PREFETCHR0(CullLanczos4Resize)
    PREFETCHR0(CullBlackmanResize)
    PREFETCHR0(CullSpline16Resize)
    PREFETCHR0(CullSpline36Resize)
    PREFETCHR0(CullSpline64Resize)
    PREFETCHR0(CullGaussResize)

    // Spline resize
    PREFETCHR0(Spline100Resize)
    PREFETCHR0(Spline144Resize)

    // PVBob
    temp = int64ToIntS(vsapi->mapGetInt(in, "mode", 0, &err));
    PREFETCH(DGBob, (temp > 0) ? 2 : 1, 1, -2, 2)
    PREFETCH(PVBob, (temp > 0) ? 2 : 1, 1, -2, 2)

    // Avisynth internal
    PREFETCH(Bob, 2, 1, 0, 0)
    PREFETCH(TemporalSoften, 1, 1, -5, 5)

    // AutoAdjust
    temp = vsapi->mapGetIntSaturated(in, "temporal_radius", 0, &err);
    if (err || temp < 0)
        temp = 20;
    PREFETCH(AutoAdjust, 1, 1, -temp, temp)

    // prefetch nothing by default
    return PrefetchInfo(1, 1, 0, -1);
}

static const VSFrame *VS_CC avisynthFilterGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = reinterpret_cast<WrappedClip *>(instanceData);
    PVideoFrame frame;
    n = std::min(n, clip->clip->GetVideoInfo().num_frames - 1);

    if (activationReason == arAllFramesReady || (activationReason == arInitial && (clip->preFetchClips.empty() || clip->prefetchInfo.from > clip->prefetchInfo.to))) {
        // Ready the global stuff needed to make things work behind the scenes, the locking model makes this technically safe but quite ugly.
        // The frame number is needed to pass through frame attributes for filters that create a new frame to return, the context is for GetFrame().
        if (!clip->preFetchClips.empty()) {
            clip->fakeEnv->uglyN = n;
            clip->fakeEnv->uglyCtx = frameCtx;
        }

        try {
            frame = clip->clip->GetFrame(n, clip->fakeEnv);

            if (!frame)
                vsapi->logMessage(mtFatal, "Avisynth Error: no frame returned", core);
        } catch (const AvisynthError &e) {
            vsapi->logMessage(mtFatal, ("Avisynth Error: avisynth errors in GetFrame() are unrecoverable, crashing... "_s + e.msg).c_str(), core);
        } catch (const IScriptEnvironment::NotFound &) {
            vsapi->logMessage(mtFatal, "Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing...", core);
        } catch (...) {
            vsapi->logMessage(mtFatal, "Avisynth Error: avisynth errors are unrecoverable, crashing...", core);
        }

        clip->fakeEnv->uglyCtx = nullptr;
    } else if (activationReason == arInitial) {
        for (VSNode *c : clip->preFetchClips)
            prefetchHelper(n, c, clip->prefetchInfo, frameCtx, vsapi);
    } else if (activationReason == arError) {
        return nullptr;
    }

    // Enjoy the casting to trigger the void * operator. Please contact me if you can make it pretty.

    const VSFrame *ref = nullptr;

    if (frame)
        ref = clip->fakeEnv->avsToVSFrame((VideoFrame *)((void *)frame));

    return ref;
}

static void VS_CC avisynthFilterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = (WrappedClip *)instanceData;
    delete clip;
}

static bool isSupportedPF(const VSVideoFormat &f, int interfaceVersion) {
    if (interfaceVersion == 2) {
        return IsSameVideoFormat(f, cfYUV, stInteger, 8, 1, 1);
    } else {
        return !!VSFormatToAVSPixelType(f, false);
    }
}

static void VS_CC fakeAvisynthFunctionWrapper(const VSMap *in, VSMap *out, void *userData,
        VSCore *core, const VSAPI *vsapi) {
    WrappedFunction *wf = (WrappedFunction *)userData;
    std::unique_ptr<FakeAvisynth> fakeEnv(new FakeAvisynth(wf->interfaceVersion, core, vsapi));
    std::vector<AVSValue> inArgs(wf->parsedArgs.size());
    std::vector<VSNode *> preFetchClips;

    int err;
    bool pack = !!vsapi->mapGetInt(in, "compatpack", 0, &err);

    for (size_t i = 0; i < inArgs.size(); i++) {
        const AvisynthArgs &parsedArg = wf->parsedArgs.at(i);

        if (vsapi->mapNumElements(in, parsedArg.name.data()) > 0) {
            switch (parsedArg.type) {
            case 'i':
                inArgs[i] = vsapi->mapGetIntSaturated(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 'f':
                inArgs[i] = vsapi->mapGetFloat(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 'b':
                inArgs[i] = !!vsapi->mapGetInt(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 's':
                inArgs[i] = vsapi->mapGetData(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 'c':
                VSNode *cr = vsapi->mapGetNode(in, parsedArg.name.c_str(), 0, nullptr);
                const VSVideoInfo *vi = vsapi->getVideoInfo(cr);
                if (!isConstantVideoFormat(vi) || !isSupportedPF(vi->format, wf->interfaceVersion)) {
                    vsapi->mapSetError(out, "Invalid avisynth colorspace in one of the input clips");
                    vsapi->freeNode(cr);
                    return;
                }

                VSClip *tmpclip = new VSClip(cr, fakeEnv.get(), pack, vsapi);
                preFetchClips.push_back(tmpclip->GetVSNode());
                inArgs[i] = tmpclip;
                break;
            }
        }
    }

    AVSValue inArgAVSValue(inArgs.data(), static_cast<int>(wf->parsedArgs.size()));
    AVSValue ret;

    try {
        ret = wf->apply(inArgAVSValue, wf->avsUserData, fakeEnv.get());
    } catch (const AvisynthError &e) {
        vsapi->mapSetError(out, e.msg);
        return;
    } catch (const IScriptEnvironment::NotFound &) {
        vsapi->logMessage(mtFatal, "Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing... ", core);
    }

    fakeEnv->initializing = false;

    if (ret.IsClip()) {
        PClip clip = ret.AsClip();

        PrefetchInfo prefetchInfo = getPrefetchInfo(wf->name, in, core, vsapi);
        std::unique_ptr<WrappedClip> filterData(new WrappedClip(wf->name, clip, preFetchClips, prefetchInfo, fakeEnv.get()));

        if (!filterData->preFetchClips.empty())
            filterData->fakeEnv->uglyNode = filterData->preFetchClips.front();

        const VideoInfo &viAvs = filterData->clip->GetVideoInfo();
        VSVideoInfo vi;
        vi.height = viAvs.height;
        vi.width = viAvs.width;
        vi.numFrames = viAvs.num_frames;
        vi.fpsNum = viAvs.fps_numerator;
        vi.fpsDen = viAvs.fps_denominator;
        reduceRational(&vi.fpsNum, &vi.fpsDen);

        bool unpack;
        if (!AVSPixelTypeToVSFormat(vi.format, unpack, viAvs, core, vsapi)) {
            vsapi->mapSetError(out, "Avisynth Compat: bad format!");
            return;
        }

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < preFetchClips.size(); i++)
            deps.push_back({preFetchClips[i], rpGeneral});

        VSNode *node = vsapi->createVideoFilter2(
                                    wf->name.c_str(),
                                    &vi,
                                    avisynthFilterGetFrame,
                                    avisynthFilterFree,
                                    (preFetchClips.empty() || prefetchInfo.from > prefetchInfo.to) ? fmFrameState : fmParallelRequests,
                                    deps.data(),
                                    preFetchClips.size(),
                                    filterData.release(),
                                    core);

        if (unpack) {
            const VideoInfo &vi = clip->GetVideoInfo();
            if (vi.IsRGB32()) {
                node = unpackRGB32Create(node, core, vsapi);
            } else if (vi.IsYUY2()) {
                node = unpackYUY2Create(node, core, vsapi);
            }
        }

        vsapi->mapConsumeNode(out, "clip", node, maReplace);
    } else if (ret.IsBool()) {
        vsapi->mapSetInt(out, "val", ret.AsBool() ? 1 : 0, maReplace);
    } else if (ret.IsInt()) {
        vsapi->mapSetInt(out, "val", ret.AsInt(), maReplace);
    } else if (ret.IsFloat()) {
        vsapi->mapSetFloat(out, "val", ret.AsFloat(), maReplace);
    } else if (ret.IsString()) {
        vsapi->mapSetData(out, "val", ret.AsString(), -1, dtUtf8, maReplace);
    }

    fakeEnv.release();
}

void FakeAvisynth::AddFunction(const char *name, const char *params, ApplyFunc apply, void *user_data) {
    size_t paramLength = strlen(params);
    size_t paramPos = 0;
    int argNum = 1;
    int numArgs = 0;
    std::vector<AvisynthArgs> parsedArgs;
    std::string newArgs;
    std::string fname(name);

    if (fname == "RemoveGrain" || fname == "Repair" || fname == "ColorMatrix" || fname == "IsCombed") {
        vsapi->logMessage(mtWarning, ("Avisynth Compat: rejected adding Avisynth function " + fname + "because it is too broken").c_str(), core);
        return;
    }

    if (fname == "FFMS2" || fname == "FFCopyrightInfringement") {
        vsapi->logMessage(mtWarning, ("Avisynth Compat: rejected adding Avisynth function " + fname + "because it calls invoke").c_str(), core);
        return;
    }

    while (paramPos < paramLength) {
        if (params[paramPos] == '*' || params[paramPos] == '+' || params[paramPos] == '.') {
            vsapi->logMessage(mtWarning, ("Avisynth Compat: varargs not implemented so I'm just gonna skip importing " + fname).c_str(), core);
            return;
        }

        if (params[paramPos] == '[') { // named argument start
            std::string argName(params);
            size_t nameStart = ++paramPos;

            while (paramPos < paramLength) {
                if (params[paramPos++] == ']') {
                    argName = argName.substr(nameStart, paramPos - nameStart - 1);
                    break;
                }
            }

            newArgs += argName + ":" + charToFilterArgumentString(params[paramPos]) + ":opt;";
            parsedArgs.push_back(AvisynthArgs(argName, params[paramPos++], false));
        } else {
            newArgs += params[paramPos] + std::to_string(argNum) + ":" + charToFilterArgumentString(params[paramPos]) + ";";
            parsedArgs.push_back(AvisynthArgs((params[paramPos] + std::to_string(argNum)), params[paramPos], true));
            paramPos++;
            argNum++;
        }

        numArgs++;
    }

    newArgs += "compatpack:int:opt;";

    std::lock_guard<std::mutex> lock(registerFunctionLock);

    if (registeredFunctions.count(fname)) {
        for (size_t i = 2; i < SIZE_MAX; i++) {
            std::string numberedName = fname + "_" + std::to_string(i);
            if (!registeredFunctions.count(numberedName)) {
                fname = numberedName;
                break;
            }
        }
    }

    registeredFunctions.insert(fname);
    // Simply assume a single video node is returned
    vsapi->registerFunction(fname.c_str(), newArgs.c_str(), "clip:vnode;", fakeAvisynthFunctionWrapper, new WrappedFunction(fname, apply, parsedArgs, user_data, interfaceVersion), vsapi->getPluginByID("com.vapoursynth.avisynth", core));
}

bool FakeAvisynth::FunctionExists(const char *name) {
    vsapi->logMessage(mtWarning, "FunctionExists not implemented", core);
    return false;
}

AVSValue FakeAvisynth::Invoke(const char *name, const AVSValue args, const char* const* arg_names) {
    if (!_stricmp(name, "Cache") || !_stricmp(name, "InternalCache")) {
        return args;
    }

    if (!_stricmp(name, "Crop")) {
        vsapi->logMessage(mtWarning, "Invoke not fully implemented, tried to call Crop() but I will do nothing", core);
        return args[0];
    }

    if (!_stricmp(name, "AudioDub")) {
        vsapi->logMessage(mtWarning, "Invoke not fully implemented, tried to call AudioDub() but I will do nothing", core);
        return args[0];
    }

    vsapi->logMessage(mtWarning, ("Invoke not fully implemented, tried to call: "_s + name + " but I will pretend it doesn't exist").c_str(), core);
    throw IScriptEnvironment::NotFound();
    return AVSValue();
}

AVSValue FakeAvisynth::GetVar(const char *name) {
    return AVSValue();
}

bool FakeAvisynth::SetVar(const char *name, const AVSValue &val) {
    return true;
}

bool FakeAvisynth::SetGlobalVar(const char *name, const AVSValue &val) {
    return true;
}

void FakeAvisynth::PushContext(int level) {
    vsapi->logMessage(mtFatal, "PushContext not implemented", core);
}

void FakeAvisynth::PopContext() {
    vsapi->logMessage(mtFatal, "PopContext not implemented", core);
}

PVideoFrame FakeAvisynth::NewVideoFrame(const VideoInfo &vi, int align) {
    VSFrame *ref = nullptr;
    assert(vi.width > 0);
    assert(vi.height > 0);

    // attempt to copy over the right set of properties, assuming that frame n in is also n out
    const VSFrame *propSrc = nullptr;

    if (uglyNode && uglyCtx)
        propSrc = vsapi->getFrameFilter(uglyN, uglyNode, uglyCtx);

    bool isMultiplePlanes = (vi.pixel_type & VideoInfo::CS_PLANAR) && !(vi.pixel_type & VideoInfo::CS_INTERLEAVED);

    VSVideoFormat f;

    bool unpack;
    if (!AVSPixelTypeToVSFormat(f, unpack, vi, core, vsapi))
        vsapi->logMessage(mtFatal, "Unsupported frame format in newvideoframe (alpha and/or packed RGB not supported)", core);

    ref = vsapi->newVideoFrame(&f, vi.width, vi.height, propSrc, core);

    if (propSrc)
        vsapi->freeFrame(propSrc);

    uint8_t *firstPlanePtr = vsapi->getWritePtr(ref, 0);
    VideoFrame *vfb = new VideoFrame(
        (BYTE *)firstPlanePtr,
        true,
        0,
        static_cast<int>(vsapi->getStride(ref, 0)),
        vi.width * f.bytesPerSample,
        vi.height,
        isMultiplePlanes ? vsapi->getWritePtr(ref, 1) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getWritePtr(ref, 2) - firstPlanePtr : 0,
        isMultiplePlanes ? static_cast<int>(vsapi->getStride(ref, 1)) : 0,
        vsapi->getFrameWidth(ref, 1) * f.bytesPerSample,
        vsapi->getFrameHeight(ref, 1));

    PVideoFrame pvf(vfb);
    ownedFrames.insert(std::make_pair(vfb, ref));
    return pvf;
}

bool FakeAvisynth::MakeWritable(PVideoFrame *pvf) {
    // Find the backing frame, copy it, wrap the new frame into a avisynth PVideoFrame
    VideoFrame *vfb = (VideoFrame *)(void *)(*pvf);
    auto it = ownedFrames.find(vfb);
    assert(it != ownedFrames.end());
    VSFrame *ref = vsapi->copyFrame(it->second, core);
    uint8_t *firstPlanePtr = vsapi->getWritePtr(ref, 0);
    VideoFrame *newVfb = new VideoFrame(
        // the data will never be modified due to the writable protections embedded in this mess
        (BYTE *)firstPlanePtr,
        true,
        0,
        static_cast<int>(vsapi->getStride(ref, 0)),
        (*pvf)->row_size,
        (*pvf)->height,
        vsapi->getWritePtr(ref, 1) - firstPlanePtr,
        vsapi->getWritePtr(ref, 2) - firstPlanePtr,
        static_cast<int>(vsapi->getStride(ref, 1)),
        (*pvf)->row_sizeUV,
        (*pvf)->heightUV);
    *pvf = PVideoFrame(newVfb);
    ownedFrames.insert(std::make_pair(newVfb, ref));
    return true;
}

void FakeAvisynth::BitBlt(BYTE* dstp, int dst_pitch, const BYTE* srcp, int src_pitch, int row_size, int height) {
    if (src_pitch == dst_pitch && dst_pitch == row_size) {
        memcpy(dstp, srcp, row_size * height);
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dstp, srcp, row_size);
            dstp += dst_pitch;
            srcp += src_pitch;
        }
    }
}

void FakeAvisynth::AtExit(ShutdownFunc function, void *user_data) {
    // intentionally ignored to prevent issues when multiple cores load the same plugin in the same process
}

void FakeAvisynth::CheckVersion(int version) {
    if (version > AVISYNTH_INTERFACE_VERSION)
        ThrowError("Plugin was designed for a later version of Avisynth (%d)", version);
}

PVideoFrame FakeAvisynth::Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height) {
    vsapi->logMessage(mtFatal, "Subframe not implemented", core);
    if (src->row_size != new_row_size)
        vsapi->logMessage(mtFatal, "Subframe only partially implemented (row_size != new_row_size)", core);
    // not pretty at all, but the underlying frame has to be fished out to have any idea what the input really is
    const VSFrame *f = avsToVSFrame((VideoFrame *)(void *)src);
    const VSVideoFormat *fi = vsapi->getVideoFrameFormat(f);
    VideoInfo vi;
    vi.height = new_height;
    vi.width = vsapi->getFrameWidth(f, 0);

    PVideoFrame dst = NewVideoFrame(vi);
    BitBlt(dst->GetWritePtr(), dst->GetPitch(), src->GetReadPtr() + rel_offset, new_pitch, new_row_size, new_height);

    return dst;
}

int FakeAvisynth::SetMemoryMax(int mem) {
    // ignore
    return 0;
}

int FakeAvisynth::SetWorkingDir(const char *newdir) {
    vsapi->logMessage(mtFatal, "SetWorkingDir not implemented", core);
    return 1;
}

void *FakeAvisynth::ManageCache(int key, void *data) {
    vsapi->logMessage(mtFatal, "ManageCache not implemented", core);
    return nullptr;
}

bool FakeAvisynth::PlanarChromaAlignment(PlanarChromaAlignmentMode key) {
    vsapi->logMessage(mtFatal, "PlanarChromaAlignment not implemented", core);
    return true;
}

PVideoFrame FakeAvisynth::SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV) {
    vsapi->logMessage(mtFatal, "SubframePlanar not implemented", core);
    return nullptr;
}

void FakeAvisynth::DeleteScriptEnvironment() {
    vsapi->logMessage(mtFatal, "DeleteScriptEnvironment not implemented", core);
}

void FakeAvisynth::ApplyMessage(PVideoFrame* frame, const VideoInfo& vi, const char* message, int size,
    int textcolor, int halocolor, int bgcolor) {
    vsapi->logMessage(mtFatal, "ApplyMessage not implemented", core);
}

const AVS_Linkage* const FakeAvisynth::GetAVSLinkage() {
    return AVS_linkage;
}

AVSValue FakeAvisynth::GetVarDef(const char* name, const AVSValue& def) {
    return def;
}

size_t  FakeAvisynth::GetProperty(AvsEnvProperty prop) {
    switch (prop) {
    case AEP_FILTERCHAIN_THREADS:
        return 1;
    case AEP_PHYSICAL_CPUS:
        return 1;
    case AEP_LOGICAL_CPUS:
        return 1;
    case AEP_THREAD_ID:
        return 0;
    case AEP_THREADPOOL_THREADS:
        return 1;
    case AEP_VERSION:
        return 0;
    default:
        this->ThrowError("Invalid property request.");
        return std::numeric_limits<size_t>::max();
    }

    assert(0);
}

bool FakeAvisynth::GetVar(const char* name, AVSValue *val) const {
    return val;
}

bool FakeAvisynth::GetVar(const char* name, bool def) const {
    return def;
}

int FakeAvisynth::GetVar(const char* name, int def) const {
    return def;
}

double FakeAvisynth::GetVar(const char* name, double def) const {
    return def;
}

const char* FakeAvisynth::GetVar(const char* name, const char* def) const {
    return def;
}

bool FakeAvisynth::LoadPlugin(const char* filePath, bool throwOnError, AVSValue *result) {
    vsapi->logMessage(mtFatal, "Plugin loading not implemented", core);
    return false;
}

void FakeAvisynth::AddAutoloadDir(const char* dirPath, bool toFront) {
    vsapi->logMessage(mtFatal, "Autoloading dirs not implemented", core);
}

void FakeAvisynth::ClearAutoloadDirs() {
    vsapi->logMessage(mtFatal, "Clearing autoload dirs not implemented", core);
}

void FakeAvisynth::AutoloadPlugins() {
    vsapi->logMessage(mtFatal, "Autoloading not implemented", core);
}

void FakeAvisynth::AddFunction(const char* name, const char* params, ApplyFunc apply, void* user_data, const char *exportVar) {
    AddFunction(name, params, apply, user_data);
}

bool FakeAvisynth::InternalFunctionExists(const char* name) {
    return false;
}

void FakeAvisynth::SetFilterMTMode(const char* filter, MtMode mode, bool force) {
    // do nothing
}

IJobCompletion* FakeAvisynth::NewCompletion(size_t capacity) {
    vsapi->logMessage(mtFatal, "Completions not implemented", core);
    return nullptr;
}

void FakeAvisynth::ParallelJob(ThreadWorkerFuncPtr jobFunc, void* jobData, IJobCompletion* completion) {
    vsapi->logMessage(mtFatal, "Threadpool not implemented", core);
}

// This version of Invoke will return false instead of throwing NotFound().
bool FakeAvisynth::Invoke(AVSValue *result, const char* name, const AVSValue& args, const char* const* arg_names) {
    try {
        *result = Invoke(name, args, arg_names);
        return true;
    } catch (NotFound &) {
        return false;
    }
}

// Support functions
void* FakeAvisynth::Allocate(size_t nBytes, size_t alignment, AvsAllocType type) {
    return vsh_aligned_malloc(nBytes, alignment);
}

void FakeAvisynth::Free(void* ptr) {
    vsh_aligned_free(ptr);
}

PVideoFrame FakeAvisynth::SubframePlanarA(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size,
    int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV, int rel_offsetA) {
    vsapi->logMessage(mtFatal, "SubframePlanarA not implemented", core);
    return PVideoFrame();
}

static void VS_CC avsLoadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::string rawPath = vsapi->mapGetData(in, "path", 0, nullptr);
    std::wstring wPath = utf16_from_utf8(rawPath);

    HMODULE plugin = LoadLibraryW(wPath.c_str());

    typedef const char *(__stdcall *AvisynthPluginInit2Func)(IScriptEnvironment *env);
    typedef const char *(__stdcall *AvisynthPluginInit3Func)(IScriptEnvironment *env, const AVS_Linkage *const vectors);

    if (!plugin) {
        DWORD lastError = GetLastError();

        if (lastError == 126)
            vsapi->mapSetError(out, ("Failed to load " + rawPath + ". GetLastError() returned " + std::to_string(lastError) + ". The file you tried to load or one of its dependencies is probably missing.").c_str());
        else
            vsapi->mapSetError(out, ("Failed to load " + rawPath + ". GetLastError() returned " + std::to_string(lastError) + ".").c_str());

        return;
    }

    AvisynthPluginInit2Func avisynthPluginInit2 = nullptr;
    AvisynthPluginInit3Func avisynthPluginInit3 = (AvisynthPluginInit3Func)GetProcAddress(plugin, "AvisynthPluginInit3");

    if (!avisynthPluginInit3)
        avisynthPluginInit3 = (AvisynthPluginInit3Func)GetProcAddress(plugin, "_AvisynthPluginInit3@8");

    if (!avisynthPluginInit3) {
        avisynthPluginInit2 = (AvisynthPluginInit2Func)GetProcAddress(plugin, "AvisynthPluginInit2");

        if (!avisynthPluginInit2)
            avisynthPluginInit2 = (AvisynthPluginInit2Func)GetProcAddress(plugin, "_AvisynthPluginInit2@4");
    }

    if (!avisynthPluginInit3 && !avisynthPluginInit2) {
        vsapi->mapSetError(out, "Avisynth Loader: no entry point found");
        FreeLibrary(plugin);
        return;
    }

    if (avisynthPluginInit3) {
        FakeAvisynth *avs = new FakeAvisynth(3, core, vsapi);
        avisynthPluginInit3(avs, AVS_linkage);
        delete avs;
    } else {
#ifdef _WIN64
        vsapi->mapSetError(out, "Avisynth Loader: 2.5 plugins can't be loaded on x64");
        return;
#else
        FakeAvisynth *avs = new FakeAvisynth(2, core, vsapi);
        avisynthPluginInit2(avs);
        delete avs;
#endif
    }

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        vsapi->logMessage(mtFatal, ("Bad SSE state detected after loading "_s + rawPath).c_str(), core);
#endif
}

WrappedFunction::WrappedFunction(const std::string &name, FakeAvisynth::ApplyFunc apply, const std::vector<AvisynthArgs> &parsedArgs, void *avsUserData, int interfaceVersion) :
    name(name), apply(apply), parsedArgs(parsedArgs), avsUserData(avsUserData), interfaceVersion(interfaceVersion) {
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.avisynth", "avs", "VapourSynth Avisynth Compatibility", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, pcModifiable, plugin);
    vspapi->registerFunction("LoadPlugin", "path:data;", "", avsLoadPlugin, plugin, plugin);
}

}
