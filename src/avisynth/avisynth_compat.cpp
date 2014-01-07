/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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

#include "avisynth_compat.h"
#include "../core/x86utils.h"
#include "../core/vslog.h"
#include <codecvt>
#include <algorithm>

namespace AvisynthCompat {

const VSFrameRef *FakeAvisynth::avsToVSFrame(VideoFrame *frame) {
    const VSFrameRef *ref = NULL;
    std::map<VideoFrame *, const VSFrameRef *>::iterator it = ownedFrames.find(frame);

    if (it != ownedFrames.end()) {
        ref = vsapi->cloneFrameRef(it->second);
    } else {
        vsFatal("unreachable condition");
        assert(false);
    }

    it = ownedFrames.begin();

    while (it != ownedFrames.end()) {
        if (it->first->GetRefCount() == 0 || it->first->GetRefCount() == 9000) {
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

long FakeAvisynth::GetCPUFlags() {
    long flags = CPUF_FPU | CPUF_MMX | CPUF_INTEGER_SSE | CPUF_SSE | CPUF_SSE2;
    return flags;
}

char *FakeAvisynth::SaveString(const char *s, int length) {
    if (length >= 0)
        savedStrings.push_back(std::string(s, length));
    else
        savedStrings.push_back(std::string(s));

    // UGLY
    // Cast away the const because nobody would actually try to write to a saved string (except possibly an avisynth plugin developer)
    return const_cast<char *>(savedStrings.back().c_str());
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
        count = vsnprintf(&buf[0], size - 1, fmt, (va_list)val);
    }

    char *i = SaveString(&buf[0]);
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

static std::string charToFilterArgumentString(char c) {
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
        vsFatal("Avisynth Compat: invalid argument type character, I quit");
        return "";
    }
}

PVideoFrame VSClip::GetFrame(int n, IScriptEnvironment *env) {
    const VSFrameRef *ref;
    n = std::min(std::max(0, n), vi.num_frames - 1);

    if (fakeEnv->initializing)
        ref = vsapi->getFrame(n, clip, 0, 0);
    else
        ref = vsapi->getFrameFilter(n, clip, fakeEnv->uglyCtx);

    std::vector<char> buf(1024);

    if (!ref) {
        if (numSlowWarnings < 200) {
            numSlowWarnings++;
            std::string s = std::string("Avisynth Compat: requested frame ") + std::to_string(n) + std::string(" not prefetched, using slow method");
            vsWarning("%s", s.c_str());
        }
        ref = vsapi->getFrame(n, clip, &buf[0], buf.size());
    }

    if (!ref)
        vsFatal("Avisynth Compat: error while getting input frame synchronously: %s", &buf[0]);

    bool isYV12 = vi.IsYV12();

    const uint8_t *firstPlanePtr = vsapi->getReadPtr(ref, 0);
    VideoFrame *vfb = new VideoFrame(
        // the data will never be modified due to the writable protections embedded in this mess
        (uint8_t *)(firstPlanePtr),
        false,
        0,
        vsapi->getStride(ref, 0),
        vsapi->getFrameWidth(ref, 0) * vsapi->getFrameFormat(ref)->bytesPerSample,
        vsapi->getFrameHeight(ref, 0),
        isYV12 ? vsapi->getReadPtr(ref, 1) - firstPlanePtr : 0,
        isYV12 ? vsapi->getReadPtr(ref, 2) - firstPlanePtr : 0,
        isYV12 ? vsapi->getStride(ref, 1) : 0);
    PVideoFrame pvf(vfb);
    fakeEnv->ownedFrames.insert(std::make_pair(vfb, ref));
    return pvf;
}

WrappedClip::WrappedClip(const std::string &filterName, const PClip &clip, const std::vector<VSNodeRef *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv)
    : filterName(filterName), prefetchInfo(prefetchInfo), preFetchClips(preFetchClips), clip(clip), fakeEnv(fakeEnv),
      magicalNumAudioSamplesForMVTools(clip->GetVideoInfo().num_audio_samples),
      magicalNChannelsForMVTools(clip->GetVideoInfo().nchannels) {
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

#define WARNING(fname, warning) if (name == #fname) vsWarning("%s", (std::string("Avisynth Compat: ") + #fname + " - " + #warning).c_str());
#define BROKEN(fname) if (name == #fname) vsWarning("%s", ("Avisynth Compat: Invoking known broken function " + name).c_str());
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
    BROKEN(IsCombed)
    PREFETCHR0(FieldDeinterlace)
    PREFETCH(Telecide, 1, 1, -1, 10) // not goot
    temp = int64ToIntS(vsapi->propGetInt(in, "cycle", 0, &err));
    PREFETCH(Decimate, temp - 1, temp, -temp, temp + 3) // probably suboptimal
    // Meowtools 2
    PREFETCHR0(mt_edge)
    PREFETCHR1(mt_motion)
    PREFETCHR0(mt_expand)
    PREFETCHR0(mt_inpand)
    PREFETCHR0(mt_inflate)
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

    // Avisynth internal
    PREFETCH(Bob, 2, 1, 0, 0)
    PREFETCH(TemporalSoften, 1, 1, -5, 5)

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

    if (viAvs.IsYV12())
        vi.format = vsapi->getFormatPreset(pfYUV420P8, core);
    else if (viAvs.IsYUY2())
        vi.format = vsapi->getFormatPreset(pfCompatYUY2, core);
    else if (viAvs.IsRGB32())
        vi.format = vsapi->getFormatPreset(pfCompatBGR32, core);
    else
        vsapi->setError(out, "Avisynth Compat: Only YV12, YUY2 and RGB32 supported");

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
                vsFatal("Avisynth Error: no frame returned");
        } catch (const AvisynthError &e) {
            vsFatal("Avisynth Error: avisynth errors in GetFrame() are unrecoverable, crashing... %s", e.msg);
        } catch (const IScriptEnvironment::NotFound &) {
            vsFatal("Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing... ");
        } catch (...) {
            vsFatal("Avisynth Error: avisynth errors are unrecoverable, crashing...");
        }

        clip->fakeEnv->uglyCtx = NULL;
    } else if (activationReason == arInitial) {
        for (VSNodeRef *c : clip->preFetchClips)
            prefetchHelper(n, c, clip->prefetchInfo, frameCtx, vsapi);
    } else if (activationReason == arError) {
        return NULL;
    }

    // Enjoy the casting to trigger the void * operator. Please contact me if you can make it pretty.

    const VSFrameRef *ref = NULL;

    if (frame) {
        ref = clip->fakeEnv->avsToVSFrame((VideoFrame *)((void *)frame));
        VSFrameRef *ref2 = vsapi->copyFrame(ref, core);
        vsapi->freeFrame(ref);
        vsapi->propSetInt(vsapi->getFramePropsRW(ref2), "MVToolsHackNumAudioSamples", clip->magicalNumAudioSamplesForMVTools, 0);
        vsapi->propSetInt(vsapi->getFramePropsRW(ref2), "MVToolsHackNChannels", clip->magicalNChannelsForMVTools, 0);
        ref = ref2;
    }

    return ref;
}

static void VS_CC avisynthFilterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    WrappedClip *clip = (WrappedClip *)instanceData;
    delete clip;
}

static void VS_CC fakeAvisynthFunctionWrapper(const VSMap *in, VSMap *out, void *userData,
        VSCore *core, const VSAPI *vsapi) {
    WrappedFunction *wf = (WrappedFunction *)userData;
    FakeAvisynth *fakeEnv = new FakeAvisynth(core, vsapi);
    std::vector<AVSValue> inArgs(wf->parsedArgs.size());
    std::vector<VSNodeRef *> preFetchClips;

    for (size_t i = 0; i < inArgs.size(); i++) {
        const AvisynthArgs &parsedArg = wf->parsedArgs.at(i);

        if (vsapi->propNumElements(in, parsedArg.name.data()) > 0) {
            switch (parsedArg.type) {
            case 'i':
                inArgs[i] = int64ToIntS(vsapi->propGetInt(in, parsedArg.name.c_str(), 0, NULL));
                break;
            case 'f':
                inArgs[i] = vsapi->propGetFloat(in, parsedArg.name.c_str(), 0, NULL);
                break;
            case 'b':
                inArgs[i] = !!vsapi->propGetInt(in, parsedArg.name.c_str(), 0, NULL);
                break;
            case 's':
                inArgs[i] = vsapi->propGetData(in, parsedArg.name.c_str(), 0, NULL);
                break;
            case 'c':
                VSNodeRef *cr = vsapi->propGetNode(in, parsedArg.name.c_str(), 0, NULL);
                const VSVideoInfo *vi = vsapi->getVideoInfo(cr);
                if (!isConstantFormat(vi) || vi->numFrames == 0 || (vi->format->id != pfYUV420P8 && vi->format->id != pfCompatYUY2 && vi->format->id != pfCompatBGR32)) {
                    vsapi->setError(out, "Invalid avisynth colorspace in one of the input clips");
                    vsapi->freeNode(cr);
                    delete fakeEnv;
                    return;
                }

                const VSFrameRef *fr = vsapi->getFrame(0, cr, 0, 0);
                int err;
                int64_t numAudioSamples = vsapi->propGetInt(vsapi->getFramePropsRO(fr), "MVToolsHackNumAudioSamples", 0, &err);
                int nChannels = int64ToIntS(vsapi->propGetInt(vsapi->getFramePropsRO(fr), "MVToolsHackNChannels", 0, &err));
                preFetchClips.push_back(cr);
                inArgs[i] = new VSClip(cr, numAudioSamples, nChannels, fakeEnv, vsapi);
                break;
            }
        }
    }

    AVSValue inArgAVSValue(inArgs.data(), wf->parsedArgs.size());
    AVSValue ret;

    try {
        ret = wf->apply(inArgAVSValue, wf->avsUserData, fakeEnv);
    } catch (AvisynthError &e) {
        vsapi->setError(out, e.msg);
        return;
    } catch (const IScriptEnvironment::NotFound &) {
        vsFatal("Avisynth Error: escaped IScriptEnvironment::NotFound exceptions are non-recoverable, crashing... ");
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
        vsapi->propSetInt(out, "val", ret.AsBool() ? 1 : 0, 0);
    } else if (ret.IsInt()) {
        vsapi->propSetInt(out, "val", ret.AsInt(), 0);
    } else if (ret.IsFloat()) {
        vsapi->propSetFloat(out, "val", ret.AsFloat(), 0);
    } else if (ret.IsString()) {
        vsapi->propSetData(out, "val", ret.AsString(), -1, 0);
    }
}

void FakeAvisynth::AddFunction(const char *name, const char *params, ApplyFunc apply, void *user_data) {
    size_t paramLength = strlen(params);
    size_t paramPos = 0;
    int argNum = 1;
    int numArgs = 0;
    std::vector<AvisynthArgs> parsedArgs;
    std::string newArgs;
    std::string name2(name);

    if (name2 == "RemoveGrain" || name2 == "Repair" || name2 == "ColorMatrix" || name2 == "IsCombed") {
        vsWarning("Avisynth Compat: rejected adding Avisynth function %s because it is too broken", name);
        return;
    }

    while (paramPos < paramLength) {
        if (params[paramPos] == '*' || params[paramPos] == '+' || params[paramPos] == '.') {
            vsWarning("%s", (std::string("Avisynth Compat: varargs not implemented so I'm just gonna skip importing ") + name).c_str());
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

    vsapi->registerFunction(name, newArgs.c_str(), fakeAvisynthFunctionWrapper, new WrappedFunction(name, apply, parsedArgs, user_data), vsapi->getPluginById("com.vapoursynth.avisynth", core));
}

bool FakeAvisynth::FunctionExists(const char *name) {
    vsFatal("FunctionExists not implemented");
    return false;
}

AVSValue FakeAvisynth::Invoke(const char *name, const AVSValue args, const char **arg_names) {
    if (!_stricmp(name, "Cache") || !_stricmp(name, "InternalCache")) {
        return args;
    }

    if (!_stricmp(name, "Crop")) {
        vsWarning("Invoke not fully implemented, tried to call Crop() but I will do nothing");
        return args[0];
    }

    vsWarning("Invoke not fully implemented, tried to call: %s but I will pretend it doesn't exist", name);
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
    vsFatal("PushContext not implemented");
}

void FakeAvisynth::PopContext() {
    vsFatal("PopContext not implemented");
}

PVideoFrame FakeAvisynth::NewVideoFrame(const VideoInfo &vi, int align) {
    VSFrameRef *ref = NULL;
    assert(vi.width > 0);
    assert(vi.height > 0);

    // attempt to copy over the right set of properties, assuming that frame n in is also n out
    const VSFrameRef *propSrc = NULL;

    if (uglyNode && uglyCtx)
        propSrc = vsapi->getFrameFilter(uglyN, uglyNode, uglyCtx);

    bool isYV12 = vi.IsYV12();

    if (isYV12) {
        ref = vsapi->newVideoFrame(vsapi->getFormatPreset(pfYUV420P8, core), vi.width, vi.height, propSrc, core);
    } else if (vi.IsYUY2()) {
        ref = vsapi->newVideoFrame(vsapi->getFormatPreset(pfCompatYUY2, core), vi.width, vi.height, propSrc, core);
    } else if (vi.IsRGB32()) {
        ref = vsapi->newVideoFrame(vsapi->getFormatPreset(pfCompatBGR32, core), vi.width, vi.height, propSrc, core);
    } else {
        vsFatal("Only YV12, YUY2 and RGB32 supported");
    }


    if (propSrc)
        vsapi->freeFrame(propSrc);

    uint8_t *firstPlanePtr = vsapi->getWritePtr(ref, 0);
    VideoFrame *vfb = new VideoFrame(
        (uint8_t *)firstPlanePtr,
        true,
        0,
        vsapi->getStride(ref, 0),
        vi.width * vsapi->getFrameFormat(ref)->bytesPerSample,
        vi.height,
        isYV12 ? vsapi->getWritePtr(ref, 1) - firstPlanePtr : 0,
        isYV12 ? vsapi->getWritePtr(ref, 2) - firstPlanePtr : 0,
        isYV12 ? vsapi->getStride(ref, 1) : 0);
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
        (uint8_t *)firstPlanePtr,
        true,
        0,
        vsapi->getStride(ref, 0),
        (*pvf)->row_size,
        (*pvf)->height,
        vsapi->getWritePtr(ref, 1) - firstPlanePtr,
        vsapi->getWritePtr(ref, 2) - firstPlanePtr,
        vsapi->getStride(ref, 1));
    *pvf = PVideoFrame(newVfb);
    ownedFrames.insert(std::make_pair(newVfb, ref));
    return true;
}

void FakeAvisynth::BitBlt(uint8_t *dstp, int dst_pitch, const uint8_t *srcp, int src_pitch, int row_size, int height) {
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
        vsFatal("Subframe only partially implemented");
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
        vsFatal("Bad colorspace");

    PVideoFrame dst = NewVideoFrame(vi);
    BitBlt(dst->GetWritePtr(), dst->GetPitch(), src->GetReadPtr() + rel_offset, new_pitch, new_row_size, new_height);

    return dst;
}

int FakeAvisynth::SetMemoryMax(int mem) {
    // ignore
    return 0;
}

int FakeAvisynth::SetWorkingDir(const char *newdir) {
    vsFatal("SetWorkingDir not implemented");
    return 1;
}

void *FakeAvisynth::ManageCache(int key, void *data) {
    vsFatal("ManageCache not implemented");
    return NULL;
}

bool FakeAvisynth::PlanarChromaAlignment(PlanarChromaAlignmentMode key) {
    vsFatal("PlanarChromaAlignment not implemented");
    return true;
}

PVideoFrame FakeAvisynth::SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV) {
    if (src->row_size != new_row_size)
        vsFatal("SubframePlanar only partially implemented");
    // not pretty at all, but the underlying frame has to be fished out to have any idea what the input really is
    const VSFrameRef *f = avsToVSFrame((VideoFrame *)(void *)src);
    const VSFormat *fi = vsapi->getFrameFormat(f);
    VideoInfo vi;
    vi.height = new_height;
    vi.width = vsapi->getFrameWidth(f, 0);

    if (fi->id == pfYUV420P8)
        vi.pixel_type = VideoInfo::CS_YV12;
    else
        vsFatal("Bad colorspace");

    PVideoFrame dst = NewVideoFrame(vi);

    BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), src->GetReadPtr(PLANAR_Y) + rel_offset, new_pitch, new_row_size, new_height);
    BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetReadPtr(PLANAR_U) + rel_offsetU, new_pitchUV, new_row_size/2, new_height/2);
    BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetReadPtr(PLANAR_V) + rel_offsetV, new_pitchUV, new_row_size/2, new_height/2);
    return dst;
}

static void VS_CC avsLoadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FakeAvisynth *avs = new FakeAvisynth(core, vsapi);
    const char *rawPath = vsapi->propGetData(in, "path", 0, NULL);
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    std::wstring wPath = conversion.from_bytes(rawPath);

    HMODULE plugin = LoadLibraryW(wPath.c_str());
    typedef const char*(__stdcall * AvisynthPluginInitFunc)(IScriptEnvironment * env);

    if (!plugin) {
        vsapi->setError(out, "Avisynth Loader: failed to load module");
        return;
    }

    AvisynthPluginInitFunc avisynthPluginInit = (AvisynthPluginInitFunc)GetProcAddress(plugin, "AvisynthPluginInit2");

    if (!avisynthPluginInit)
        avisynthPluginInit = (AvisynthPluginInitFunc)GetProcAddress(plugin, "_AvisynthPluginInit2@4");

    if (!avisynthPluginInit) {
        vsapi->setError(out, "Avisynth Loader: no entry point found");
        return;
    }

    avisynthPluginInit(avs);

// This stuff really only works properly on windows, feel free to investigate what the linux ABI thinks about it
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected after loading %s", rawPath);
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after loading %s", rawPath);
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after loading %s", rawPath);
#endif

    delete avs; // the environment is just temporary to add the functions,
    // a new one will be created for each filter instance
}

WrappedFunction::WrappedFunction(const std::string &name, FakeAvisynth::ApplyFunc apply, const std::vector<AvisynthArgs> &parsedArgs, void *avsUserData) :
    name(name), apply(apply), parsedArgs(parsedArgs), avsUserData(avsUserData) {
}

extern "C" void VS_CC avsWrapperInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.avisynth", "avs", "VapourSynth Avisynth Compatibility", VAPOURSYNTH_API_VERSION, 0, plugin);
    registerFunc("LoadPlugin", "path:data;", &avsLoadPlugin, plugin, plugin);
}

}
