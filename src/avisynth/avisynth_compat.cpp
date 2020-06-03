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
#include "avisynth_compat.h"
#include <algorithm>
#include <cstdarg>
#include "../common/vsutf16.h"

#define NOMINMAX
#include <Windows.h>

extern const AVS_Linkage* const AVS_linkage;

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }
} // namespace


namespace AvisynthCompat {

static int VSFormatToAVSPixelType(const VSFormat *fi) {
    if (fi->id == pfCompatBGR32)
        return VideoInfo::CS_BGR32;
    else if (fi->id == pfCompatYUY2)
        return VideoInfo::CS_YUY2;
    else if (fi->id == pfYUV444P8)
        return VideoInfo::CS_YV24;
    else if (fi->id == pfYUV422P8)
        return VideoInfo::CS_YV16;
    else if (fi->id == pfYUV420P8)
        return VideoInfo::CS_YV12;
    else if (fi->id == pfYUV410P8)
        return VideoInfo::CS_YUV9;
    else if (fi->id == pfYUV411P8)
        return VideoInfo::CS_YV411;
    else if (fi->id == pfGray8)
        return VideoInfo::CS_Y8;
    else if (fi->id == pfYUV444P10)
        return VideoInfo::CS_YUV444P10;
    else if (fi->id == pfYUV422P10)
        return VideoInfo::CS_YUV422P10;
    else if (fi->id == pfYUV420P10)
        return VideoInfo::CS_YUV420P10;
    else if (fi->bitsPerSample == 10 && fi->colorFamily == cmGray && fi->sampleType == stInteger)
        return VideoInfo::CS_Y10;
    else if (fi->id == pfYUV444P12)
        return VideoInfo::CS_YUV444P12;
    else if (fi->id == pfYUV422P12)
        return VideoInfo::CS_YUV422P12;
    else if (fi->id == pfYUV420P12)
        return VideoInfo::CS_YUV420P12;
    else if (fi->bitsPerSample == 12 && fi->colorFamily == cmGray && fi->sampleType == stInteger)
        return VideoInfo::CS_Y12;
    else if (fi->id == pfYUV444P14)
        return VideoInfo::CS_YUV444P14;
    else if (fi->id == pfYUV422P14)
        return VideoInfo::CS_YUV422P14;
    else if (fi->id == pfYUV420P14)
        return VideoInfo::CS_YUV420P14;
    else if (fi->bitsPerSample == 14 && fi->colorFamily == cmGray && fi->sampleType == stInteger)
        return VideoInfo::CS_Y14;
    else if (fi->id == pfYUV444P16)
        return VideoInfo::CS_YUV444P16;
    else if (fi->id == pfYUV422P16)
        return VideoInfo::CS_YUV422P16;
    else if (fi->id == pfYUV420P16)
        return VideoInfo::CS_YUV420P16;
    else if (fi->id == pfGray16)
        return VideoInfo::CS_Y16;
    else if (fi->id == pfYUV444PS)
        return VideoInfo::CS_YUV444PS;
    else if (fi->bitsPerSample == 32 && fi->colorFamily == cmYUV && fi->sampleType == stFloat && fi->subSamplingH == 0 && fi->subSamplingW == 1)
        return VideoInfo::CS_YUV422PS;
    else if (fi->bitsPerSample == 32 && fi->colorFamily == cmYUV && fi->sampleType == stFloat && fi->subSamplingH == 1 && fi->subSamplingW == 1)
        return VideoInfo::CS_YUV420PS;
    else if (fi->id == pfGrayS)
        return VideoInfo::CS_Y32;
    else if (fi->id == pfRGB24)
        return VideoInfo::CS_RGBP;
    else if (fi->id == pfRGB30)
        return VideoInfo::CS_RGBP10;
    else if (fi->bitsPerSample == 12 && fi->colorFamily == cmRGB)
        return VideoInfo::CS_RGBP12;
    else if (fi->bitsPerSample == 14 && fi->colorFamily == cmRGB)
        return VideoInfo::CS_RGBP14;
    else if (fi->id == pfRGB48)
        return VideoInfo::CS_RGBP16;
    else if (fi->id == pfGrayS)
        return VideoInfo::CS_Y32;
    else
        return 0;
}

static const VSFormat *AVSPixelTypeToVSFormat(const VideoInfo &vi, VSCore *core, const VSAPI *vsapi) {
    if (vi.IsYUY2())
        return vsapi->getFormatPreset(pfCompatYUY2, core);
    else if (vi.IsRGB32())
        return vsapi->getFormatPreset(pfCompatBGR32, core);

    if (vi.IsPlanar()) {
        bool hasSubSampling = vi.IsYUV();
        int colorspace = vi.IsYUV() ? cmYUV : (vi.IsRGB() ? cmRGB : (vi.IsY() ? cmGray : 0));
        if (colorspace)
            return vsapi->registerFormat(colorspace, vi.BitsPerComponent() == 32 ? stFloat : stInteger, vi.BitsPerComponent(), hasSubSampling ? vi.GetPlaneWidthSubsampling(PLANAR_U) : 0, hasSubSampling ? vi.GetPlaneHeightSubsampling(PLANAR_U) : 0, core);
    }

    return nullptr;
}

const VSFrameRef *FakeAvisynth::avsToVSFrame(VideoFrame *frame) {
    const VSFrameRef *ref = nullptr;
    std::map<VideoFrame *, const VSFrameRef *>::iterator it = ownedFrames.find(frame);

    if (it != ownedFrames.end()) {
        ref = vsapi->cloneFrameRef(it->second);
    } else {
        vsapi->logMessage(mtFatal, "unreachable condition");
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
    std::map<VideoFrame *, const VSFrameRef *>::iterator it = ownedFrames.begin();

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
        return "clip";
    default:
        vsapi->logMessage(mtFatal, "Avisynth Compat: invalid argument type character, I quit");
        return "";
    }
}

VSClip::VSClip(VSNodeRef *clip, FakeAvisynth *fakeEnv, const VSAPI *vsapi)
    : clip(clip), fakeEnv(fakeEnv), vsapi(vsapi), numSlowWarnings(0) {
    const ::VSVideoInfo *srcVi = vsapi->getVideoInfo(clip);
    vi = {};
    vi.width = srcVi->width;
    vi.height = srcVi->height;

    vi.pixel_type = VSFormatToAVSPixelType(srcVi->format);
    if (!vi.pixel_type)
        vsapi->logMessage(mtFatal, "Bad colorspace");

    vi.image_type = VideoInfo::IT_BFF;
    vi.fps_numerator = int64ToIntS(srcVi->fpsNum);
    vi.fps_denominator = int64ToIntS(srcVi->fpsDen);
    vi.num_frames = srcVi->numFrames;
    vi.sample_type = SAMPLE_INT16;
}

PVideoFrame VSClip::GetFrame(int n, IScriptEnvironment *env) {
    const VSFrameRef *ref;
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
            vsapi->logMessage(mtWarning, s.c_str());
        }
        ref = vsapi->getFrame(n, clip, buf.data(), static_cast<int>(buf.size()));
    }

    if (!ref)
        vsapi->logMessage(mtFatal, ("Avisynth Compat: error while getting input frame synchronously: "_s + buf.data()).c_str());

    bool isMultiplePlanes = (vi.pixel_type & VideoInfo::CS_PLANAR) && !(vi.pixel_type & VideoInfo::CS_INTERLEAVED);

    const uint8_t *firstPlanePtr = vsapi->getReadPtr(ref, 0);

    VideoFrame *vfb = new VideoFrame(
        // the data will never be modified due to the writable protections embedded in this mess
        (BYTE *)firstPlanePtr,
        false,
        0,
        vsapi->getStride(ref, 0),
        vsapi->getFrameWidth(ref, 0) * vsapi->getFrameFormat(ref)->bytesPerSample,
        vsapi->getFrameHeight(ref, 0),
        isMultiplePlanes ? vsapi->getReadPtr(ref, 1) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getReadPtr(ref, 2) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getStride(ref, 1) : 0,
        vsapi->getFrameWidth(ref, 1) * vsapi->getFrameFormat(ref)->bytesPerSample,
        vsapi->getFrameHeight(ref, 1));
    PVideoFrame pvf(vfb);
    fakeEnv->ownedFrames.insert(std::make_pair(vfb, ref));
    return pvf;
}

WrappedClip::WrappedClip(const std::string &filterName, const PClip &clip, const std::vector<VSNodeRef *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv)
    : filterName(filterName), prefetchInfo(prefetchInfo), preFetchClips(preFetchClips), clip(clip), fakeEnv(fakeEnv) {
}

static void prefetchHelper(int n, VSNodeRef *node, const PrefetchInfo &p, VSFrameContext *frameCtx, const VSAPI *vsapi) {
    n /= p.div;
    n *= p.mul;

    for (int i = n + p.from; i <= n + p.to; i++) {
        if (i < 0)
            continue;

        vsapi->requestFrameFilter(i, node, frameCtx);
    }
}

#define WARNING(fname, warning) if (name == #fname) vsapi->logMessage(mtWarning, "Avisynth Compat: "_s + #fname + " - " + #warning).c_str());
#define BROKEN(fname) if (name == #fname) vsapi->logMessage(mtWarning, ("Avisynth Compat: Invoking known broken function "_s + name).c_str());
#define OTHER(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define SOURCE(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define PREFETCHR0(fname) if (name == #fname) return PrefetchInfo(1, 1, 0, 0);
#define PREFETCHR1(fname) if (name == #fname) return PrefetchInfo(1, 1, -1, 1);
#define PREFETCHR2(fname) if (name == #fname) return PrefetchInfo(1, 1, -2, 2);
#define PREFETCHR3(fname) if (name == #fname) return PrefetchInfo(1, 1, -3, 3);
#define PREFETCH(fname, div, mul, from, to) if (name == #fname) return PrefetchInfo(div, mul, from, to);

static PrefetchInfo getPrefetchInfo(const std::string &name, const VSMap *in, const VSAPI *vsapi) {
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
    temp = int64ToIntS(vsapi->propGetInt(in, "Az", 0, &err));
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
    temp = int64ToIntS(vsapi->propGetInt(in, "mode", 0, &err));
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
    temp = int64ToIntS(vsapi->propGetInt(in, "tbsize", 0, &err));
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
    temp = int64ToIntS(vsapi->propGetInt(in, "mode", 0, &err));
    PREFETCH(DGBob, (temp > 0) ? 2 : 1, 1, -2, 2) // close enough?
    BROKEN(IsCombed)
    PREFETCHR0(FieldDeinterlace)
    PREFETCH(Telecide, 1, 1, -2, 10) // not good
    PREFETCH(DGTelecide, 1, 1, -2, 10) // also not good
    temp = int64ToIntS(vsapi->propGetInt(in, "cycle", 0, &err));
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
    temp = int64ToIntS(vsapi->propGetInt(in, "delta", 0, &err));
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

    // Avisynth internal
    PREFETCH(Bob, 2, 1, 0, 0)
    PREFETCH(TemporalSoften, 1, 1, -5, 5)

    // AutoAdjust
    temp = int64ToIntS(vsapi->propGetInt(in, "temporal_radius", 0, &err));
    if (err || temp < 0)
        temp = 20;
    PREFETCH(AutoAdjust, 1, 1, -temp, temp)

    // prefetch nothing by default
    return PrefetchInfo(1, 1, 0, -1);
}

static void VS_CC avisynthFilterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = (WrappedClip *) * instanceData;

    if (!clip->preFetchClips.empty())
        clip->fakeEnv->uglyNode = clip->preFetchClips.front();

    const VideoInfo &viAvs = clip->clip->GetVideoInfo();
    ::VSVideoInfo vi;
    vi.height = viAvs.height;
    vi.width = viAvs.width;
    vi.numFrames = viAvs.num_frames;
    vi.fpsNum = viAvs.fps_numerator;
    vi.fpsDen = viAvs.fps_denominator;
    vs_normalizeRational(&vi.fpsNum, &vi.fpsDen);

    vi.format = AVSPixelTypeToVSFormat(viAvs, core, vsapi);

    if (!vi.format)
        vsapi->setError(out, "Avisynth Compat: bad format!");

    vi.flags = 0;
    vsapi->setVideoInfo(&vi, 1, node);
}

static const VSFrameRef *VS_CC avisynthFilterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = (WrappedClip *) * instanceData;
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
                vsapi->logMessage(mtFatal, "Avisynth Error: no frame returned");
        } catch (const AvisynthError &e) {
            vsapi->logMessage(mtFatal, ("Avisynth Error: avisynth errors in GetFrame() are unrecoverable, crashing... "_s + e.msg).c_str());
        } catch (const IScriptEnvironment::NotFound &) {
            vsapi->logMessage(mtFatal, "Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing... ");
        } catch (...) {
            vsapi->logMessage(mtFatal, "Avisynth Error: avisynth errors are unrecoverable, crashing...");
        }

        clip->fakeEnv->uglyCtx = nullptr;
    } else if (activationReason == arInitial) {
        for (VSNodeRef *c : clip->preFetchClips)
            prefetchHelper(n, c, clip->prefetchInfo, frameCtx, vsapi);
    } else if (activationReason == arError) {
        return nullptr;
    }

    // Enjoy the casting to trigger the void * operator. Please contact me if you can make it pretty.

    const VSFrameRef *ref = nullptr;

    if (frame)
        ref = clip->fakeEnv->avsToVSFrame((VideoFrame *)((void *)frame));

    return ref;
}

static void VS_CC avisynthFilterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = (WrappedClip *)instanceData;
    delete clip;
}

static bool isSupportedPF(const VSFormat *f, int interfaceVersion) {
    if (interfaceVersion == 2)
        return (f->id == pfYUV420P8) || (f->id == pfCompatYUY2) || (f->id == pfCompatBGR32);
    else
        return !!VSFormatToAVSPixelType(f);
}

static void VS_CC fakeAvisynthFunctionWrapper(const VSMap *in, VSMap *out, void *userData,
        VSCore *core, const VSAPI *vsapi) {
    WrappedFunction *wf = (WrappedFunction *)userData;
    FakeAvisynth *fakeEnv = new FakeAvisynth(wf->interfaceVersion, core, vsapi);
    std::vector<AVSValue> inArgs(wf->parsedArgs.size());
    std::vector<VSNodeRef *> preFetchClips;

    for (size_t i = 0; i < inArgs.size(); i++) {
        const AvisynthArgs &parsedArg = wf->parsedArgs.at(i);

        if (vsapi->propNumElements(in, parsedArg.name.data()) > 0) {
            switch (parsedArg.type) {
            case 'i':
                inArgs[i] = int64ToIntS(vsapi->propGetInt(in, parsedArg.name.c_str(), 0, nullptr));
                break;
            case 'f':
                inArgs[i] = vsapi->propGetFloat(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 'b':
                inArgs[i] = !!vsapi->propGetInt(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 's':
                inArgs[i] = vsapi->propGetData(in, parsedArg.name.c_str(), 0, nullptr);
                break;
            case 'c':
                VSNodeRef *cr = vsapi->propGetNode(in, parsedArg.name.c_str(), 0, nullptr);
                const VSVideoInfo *vi = vsapi->getVideoInfo(cr);
                if (!isConstantFormat(vi) || !isSupportedPF(vi->format, wf->interfaceVersion)) {
                    vsapi->setError(out, "Invalid avisynth colorspace in one of the input clips");
                    vsapi->freeNode(cr);
                    delete fakeEnv;
                    return;
                }

                preFetchClips.push_back(cr);
                inArgs[i] = new VSClip(cr, fakeEnv, vsapi);
                break;
            }
        }
    }

    AVSValue inArgAVSValue(inArgs.data(), static_cast<int>(wf->parsedArgs.size()));
    AVSValue ret;

    try {
        ret = wf->apply(inArgAVSValue, wf->avsUserData, fakeEnv);
    } catch (const AvisynthError &e) {
        vsapi->setError(out, e.msg);
        return;
    } catch (const IScriptEnvironment::NotFound &) {
        vsapi->logMessage(mtFatal, "Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing... ");
    }

    fakeEnv->initializing = false;

    if (ret.IsClip()) {
        PrefetchInfo prefetchInfo = getPrefetchInfo(wf->name, in, vsapi);
        WrappedClip *filterData = new WrappedClip(wf->name, ret.AsClip(), preFetchClips, prefetchInfo, fakeEnv);
        vsapi->createFilter(
                                    in,
                                    out,
                                    wf->name.c_str(),
                                    avisynthFilterInit,
                                    avisynthFilterGetFrame,
                                    avisynthFilterFree,
                                    (preFetchClips.empty() || prefetchInfo.from > prefetchInfo.to) ? fmSerial : fmParallelRequests,
                                    0,
                                    filterData,
                                    core);
    } else if (ret.IsBool()) {
        vsapi->propSetInt(out, "val", ret.AsBool() ? 1 : 0, paReplace);
    } else if (ret.IsInt()) {
        vsapi->propSetInt(out, "val", ret.AsInt(), paReplace);
    } else if (ret.IsFloat()) {
        vsapi->propSetFloat(out, "val", ret.AsFloat(), paReplace);
    } else if (ret.IsString()) {
        vsapi->propSetData(out, "val", ret.AsString(), -1, paReplace);
    }
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
        vsapi->logMessage(mtWarning, ("Avisynth Compat: rejected adding Avisynth function " + fname + "because it is too broken").c_str());
        return;
    }

    if (fname == "FFMS2" || fname == "FFCopyrightInfringement") {
        vsapi->logMessage(mtWarning, ("Avisynth Compat: rejected adding Avisynth function " + fname + "because it calls invoke").c_str());
        return;
    }

    while (paramPos < paramLength) {
        if (params[paramPos] == '*' || params[paramPos] == '+' || params[paramPos] == '.') {
            vsapi->logMessage(mtWarning, ("Avisynth Compat: varargs not implemented so I'm just gonna skip importing " + fname).c_str());
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
    vsapi->registerFunction(fname.c_str(), newArgs.c_str(), fakeAvisynthFunctionWrapper, new WrappedFunction(fname, apply, parsedArgs, user_data, interfaceVersion), vsapi->getPluginById("com.vapoursynth.avisynth", core));
}

bool FakeAvisynth::FunctionExists(const char *name) {
    vsapi->logMessage(mtWarning, "FunctionExists not implemented");
    return false;
}

AVSValue FakeAvisynth::Invoke(const char *name, const AVSValue args, const char* const* arg_names) {
    if (!_stricmp(name, "Cache") || !_stricmp(name, "InternalCache")) {
        return args;
    }

    if (!_stricmp(name, "Crop")) {
        vsapi->logMessage(mtWarning, "Invoke not fully implemented, tried to call Crop() but I will do nothing");
        return args[0];
    }

    if (!_stricmp(name, "AudioDub")) {
        vsapi->logMessage(mtWarning, "Invoke not fully implemented, tried to call AudioDub() but I will do nothing");
        return args[0];
    }

    vsapi->logMessage(mtWarning, ("Invoke not fully implemented, tried to call: "_s + name + " but I will pretend it doesn't exist").c_str());
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
    vsapi->logMessage(mtFatal, "PushContext not implemented");
}

void FakeAvisynth::PopContext() {
    vsapi->logMessage(mtFatal, "PopContext not implemented");
}

PVideoFrame FakeAvisynth::NewVideoFrame(const VideoInfo &vi, int align) {
    VSFrameRef *ref = nullptr;
    assert(vi.width > 0);
    assert(vi.height > 0);

    // attempt to copy over the right set of properties, assuming that frame n in is also n out
    const VSFrameRef *propSrc = nullptr;

    if (uglyNode && uglyCtx)
        propSrc = vsapi->getFrameFilter(uglyN, uglyNode, uglyCtx);

    bool isMultiplePlanes = (vi.pixel_type & VideoInfo::CS_PLANAR) && !(vi.pixel_type & VideoInfo::CS_INTERLEAVED);

    const VSFormat *f = AVSPixelTypeToVSFormat(vi, core, vsapi);

    if (!f)
        vsapi->logMessage(mtFatal, "Unsupported frame format in newvideoframe (alpha and/or packed RGB not supported)");

    ref = vsapi->newVideoFrame(f, vi.width, vi.height, propSrc, core);

    if (propSrc)
        vsapi->freeFrame(propSrc);

    uint8_t *firstPlanePtr = vsapi->getWritePtr(ref, 0);
    VideoFrame *vfb = new VideoFrame(
        (BYTE *)firstPlanePtr,
        true,
        0,
        vsapi->getStride(ref, 0),
        vi.width * vsapi->getFrameFormat(ref)->bytesPerSample,
        vi.height,
        isMultiplePlanes ? vsapi->getWritePtr(ref, 1) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getWritePtr(ref, 2) - firstPlanePtr : 0,
        isMultiplePlanes ? vsapi->getStride(ref, 1) : 0,
        vsapi->getFrameWidth(ref, 1) * vsapi->getFrameFormat(ref)->bytesPerSample,
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
    VSFrameRef *ref = vsapi->copyFrame(it->second, core);
    uint8_t *firstPlanePtr = vsapi->getWritePtr(ref, 0);
    VideoFrame *newVfb = new VideoFrame(
        // the data will never be modified due to the writable protections embedded in this mess
        (BYTE *)firstPlanePtr,
        true,
        0,
        vsapi->getStride(ref, 0),
        (*pvf)->row_size,
        (*pvf)->height,
        vsapi->getWritePtr(ref, 1) - firstPlanePtr,
        vsapi->getWritePtr(ref, 2) - firstPlanePtr,
        vsapi->getStride(ref, 1),
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
    // intentionally ignored to prevent issues when multiple cores load the sample plugin in the same process
}

void FakeAvisynth::CheckVersion(int version) {
    if (version > AVISYNTH_INTERFACE_VERSION)
        ThrowError("Plugin was designed for a later version of Avisynth (%d)", version);
}

PVideoFrame FakeAvisynth::Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height) {
    if (src->row_size != new_row_size)
        vsapi->logMessage(mtFatal, "Subframe only partially implemented (row_size != new_row_size)");
    // not pretty at all, but the underlying frame has to be fished out to have any idea what the input really is
    const VSFrameRef *f = avsToVSFrame((VideoFrame *)(void *)src);
    const VSFormat *fi = vsapi->getFrameFormat(f);
    VideoInfo vi;
    vi.height = new_height;
    vi.width = vsapi->getFrameWidth(f, 0);

    if (fi->id == pfCompatYUY2)
        vi.pixel_type = VideoInfo::CS_YUY2;
    else if (fi->id == pfCompatBGR32)
        vi.pixel_type = VideoInfo::CS_BGR32;
    else
        vsapi->logMessage(mtFatal, "Bad colorspace");

    PVideoFrame dst = NewVideoFrame(vi);
    BitBlt(dst->GetWritePtr(), dst->GetPitch(), src->GetReadPtr() + rel_offset, new_pitch, new_row_size, new_height);

    return dst;
}

int FakeAvisynth::SetMemoryMax(int mem) {
    // ignore
    return 0;
}

int FakeAvisynth::SetWorkingDir(const char *newdir) {
    vsapi->logMessage(mtFatal, "SetWorkingDir not implemented");
    return 1;
}

void *FakeAvisynth::ManageCache(int key, void *data) {
    vsapi->logMessage(mtFatal, "ManageCache not implemented");
    return nullptr;
}

bool FakeAvisynth::PlanarChromaAlignment(PlanarChromaAlignmentMode key) {
    vsapi->logMessage(mtFatal, "PlanarChromaAlignment not implemented");
    return true;
}

PVideoFrame FakeAvisynth::SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV) {
    vsapi->logMessage(mtFatal, "SubframePlanar not implemented");
    if (src->row_size != new_row_size)
        vsapi->logMessage(mtFatal, "SubframePlanar only partially implemented");
    // not pretty at all, but the underlying frame has to be fished out to have any idea what the input really is
    const VSFrameRef *f = avsToVSFrame((VideoFrame *)(void *)src);
    const VSFormat *fi = vsapi->getFrameFormat(f);
    VideoInfo vi;
    vi.height = new_height;
    vi.width = vsapi->getFrameWidth(f, 0);

    vi.pixel_type = VSFormatToAVSPixelType(fi);

    if (!vi.pixel_type)
        vsapi->logMessage(mtFatal, "Bad colorspace, bad!");

    PVideoFrame dst = NewVideoFrame(vi);

    vsapi->logMessage(mtWarning, "Subframeplanar only partially implemented, report if it crashed or not (especially if not using YV12)");
    BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), src->GetReadPtr(PLANAR_Y) + rel_offset, new_pitch, new_row_size, new_height);
    BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetReadPtr(PLANAR_U) + rel_offsetU, new_pitchUV, new_row_size/2, new_height/2);
    BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetReadPtr(PLANAR_V) + rel_offsetV, new_pitchUV, new_row_size/2, new_height/2);
    return dst;
}

void FakeAvisynth::DeleteScriptEnvironment() {
    vsapi->logMessage(mtFatal, "DeleteScriptEnvironment not implemented");
}

void FakeAvisynth::ApplyMessage(PVideoFrame* frame, const VideoInfo& vi, const char* message, int size,
    int textcolor, int halocolor, int bgcolor) {
    vsapi->logMessage(mtFatal, "ApplyMessage not implemented");
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
    vsapi->logMessage(mtFatal, "Plugin loading not implemented");
    return false;
}

void FakeAvisynth::AddAutoloadDir(const char* dirPath, bool toFront) {
    vsapi->logMessage(mtFatal, "Autoloading dirs not implemented");
}

void FakeAvisynth::ClearAutoloadDirs() {
    vsapi->logMessage(mtFatal, "Clearing autoload dirs not implemented");
}

void FakeAvisynth::AutoloadPlugins() {
    vsapi->logMessage(mtFatal, "Autoloading not implemented");
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
    vsapi->logMessage(mtFatal, "Completions not implemented");
    return nullptr;
}

void FakeAvisynth::ParallelJob(ThreadWorkerFuncPtr jobFunc, void* jobData, IJobCompletion* completion) {
    vsapi->logMessage(mtFatal, "Threadpool not implemented");
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
    return vs_aligned_malloc(nBytes, alignment);
}

void FakeAvisynth::Free(void* ptr) {
    vs_aligned_free(ptr);
}

PVideoFrame FakeAvisynth::SubframePlanarA(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size,
    int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV, int rel_offsetA) {
    vsapi->logMessage(mtFatal, "SubframePlanarA not implemented");
    return PVideoFrame();
}

static void VS_CC avsLoadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    const char *rawPath = vsapi->propGetData(in, "path", 0, nullptr);
    std::wstring wPath = utf16_from_utf8(rawPath);

    HMODULE plugin = LoadLibraryW(wPath.c_str());

    typedef const char *(__stdcall *AvisynthPluginInit2Func)(IScriptEnvironment *env);
    typedef const char *(__stdcall *AvisynthPluginInit3Func)(IScriptEnvironment *env, const AVS_Linkage *const vectors);

    if (!plugin) {
        vsapi->setError(out, "Avisynth Loader: failed to load module");
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
        vsapi->setError(out, "Avisynth Loader: no entry point found");
        FreeLibrary(plugin);
        return;
    }

    if (avisynthPluginInit3) {
        FakeAvisynth *avs = new FakeAvisynth(3, core, vsapi);
        avisynthPluginInit3(avs, AVS_linkage);
        delete avs;
    } else {
#ifdef _WIN64
        vsapi->setError(out, "Avisynth Loader: 2.5 plugins can't be loaded on x64");
        return;
#else
        FakeAvisynth *avs = new FakeAvisynth(2, core, vsapi);
        avisynthPluginInit2(avs);
        delete avs;
#endif
    }

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        vsapi->logMessage(mtFatal, ("Bad SSE state detected after loading "_s + rawPath).c_str());
#endif
}

WrappedFunction::WrappedFunction(const std::string &name, FakeAvisynth::ApplyFunc apply, const std::vector<AvisynthArgs> &parsedArgs, void *avsUserData, int interfaceVersion) :
    name(name), apply(apply), parsedArgs(parsedArgs), avsUserData(avsUserData), interfaceVersion(interfaceVersion) {
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.avisynth", "avs", "VapourSynth Avisynth Compatibility", VAPOURSYNTH_API_VERSION, 0, plugin);
    registerFunc("LoadPlugin", "path:data;", &avsLoadPlugin, plugin, plugin);
}

}
