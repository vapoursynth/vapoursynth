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

#ifndef AVISYNTH_COMPAT_H
#define AVISYNTH_COMPAT_H

#include "avisynth_wrapper.h"
#include "VapourSynth.h"
#include "VSHelper.h"
#include <list>
#include <map>
#include <vector>
#include <string>

namespace AvisynthCompat {

struct WrappedClip;

class FakeAvisynth : public IScriptEnvironment {
    friend class VSClip;
private:
    VSCore *core;
    std::list<std::string> savedStrings;
    const VSAPI *vsapi;
    std::map<VideoFrame *, const VSFrameRef *> ownedFrames;
public:
    const VSFrameRef *avsToVSFrame(VideoFrame *frame);

    // ugly, but unfortunately the best place to put the pseudo global variables, actually they're per context
    // the locking prevents multiple accesses at once
    bool initializing;
    int uglyN;
    VSNodeRef *uglyNode;
    VSFrameContext *uglyCtx;

    FakeAvisynth(VSCore *core, const VSAPI *vsapi) : core(core), vsapi(vsapi), initializing(true), uglyN(-1), uglyNode(NULL), uglyCtx(NULL) {}
    // virtual avisynth functions
    ~FakeAvisynth();
    long __stdcall GetCPUFlags();
    char *__stdcall SaveString(const char *s, int length = -1);
    char *Sprintf(const char *fmt, ...);
    char *__stdcall VSprintf(const char *fmt, void *val);
    __declspec(noreturn) void ThrowError(const char *fmt, ...);
    void __stdcall AddFunction(const char *name, const char *params, ApplyFunc apply, void *user_data);
    bool __stdcall FunctionExists(const char *name);
    AVSValue __stdcall Invoke(const char *name, const AVSValue args, const char **arg_names);
    AVSValue __stdcall GetVar(const char *name);
    bool __stdcall SetVar(const char *name, const AVSValue &val);
    bool __stdcall SetGlobalVar(const char *name, const AVSValue &val);
    void __stdcall PushContext(int level);
    void __stdcall PopContext();
    PVideoFrame __stdcall NewVideoFrame(const VideoInfo &vi, int align = 32);
    bool __stdcall MakeWritable(PVideoFrame *pvf);
    void __stdcall BitBlt(uint8_t *dstp, int dst_pitch, const uint8_t *srcp, int src_pitch, int row_size, int height);
    void __stdcall AtExit(ShutdownFunc function, void *user_data);
    void __stdcall CheckVersion(int version);
    PVideoFrame __stdcall Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height);
    int __stdcall SetMemoryMax(int mem);
    int __stdcall SetWorkingDir(const char *newdir);
    void *__stdcall ManageCache(int key, void *data);
    bool __stdcall PlanarChromaAlignment(PlanarChromaAlignmentMode key);
    PVideoFrame __stdcall SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV);
};

class VSClip : public IClip {
private:
    VSNodeRef *clip;
    FakeAvisynth *fakeEnv;
    const VSAPI *vsapi;
    int numSlowWarnings;
    VideoInfo vi;
public:
    VSClip(VSNodeRef *clip, int64_t numAudioSamples, int nChannels, FakeAvisynth *fakeEnv, const VSAPI *vsapi)
        : clip(clip), fakeEnv(fakeEnv), vsapi(vsapi), numSlowWarnings(0) {
        const ::VSVideoInfo *srcVi = vsapi->getVideoInfo(clip);
        vi.width = srcVi->width;
        vi.height = srcVi->height;

        if (srcVi->format->id == pfYUV420P8)
            vi.pixel_type = VideoInfo::CS_YV12;
        else if (srcVi->format->id == pfCompatYUY2)
            vi.pixel_type = VideoInfo::CS_YUY2;
        else if (srcVi->format->id == pfCompatBGR32)
            vi.pixel_type = VideoInfo::CS_BGR32;

        vi.image_type = VideoInfo::IT_BFF;
        vi.fps_numerator = int64ToIntS(srcVi->fpsNum);
        vi.fps_denominator = int64ToIntS(srcVi->fpsDen);
        vi.num_frames = srcVi->numFrames;
        vi.audio_samples_per_second = 0;
        vi.sample_type = 0;
        vi.num_audio_samples = numAudioSamples;
        vi.nchannels = nChannels;
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
    bool __stdcall GetParity(int n) {
        return true;
    }
    void __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) {}
    void __stdcall SetCacheHints(int cachehints, int frame_range) {
        // intentionally ignore
    }
    const VideoInfo &__stdcall GetVideoInfo() {
        return vi;
    }
    ~VSClip() {
        vsapi->freeNode(clip);
    }
};


struct PrefetchInfo {
    int div;
    int mul;
    int from;
    int to;
    PrefetchInfo(int div, int mul, int from, int to) : div(div), mul(mul), from(from), to(to) { }
};

struct WrappedClip {
    std::string filterName;
    PrefetchInfo prefetchInfo;
    std::vector<VSNodeRef *> preFetchClips;
    PClip clip;
    FakeAvisynth *fakeEnv;
    int64_t magicalNumAudioSamplesForMVTools;
    int magicalNChannelsForMVTools;
    WrappedClip(const std::string &filterName, const PClip &clip, const std::vector<VSNodeRef *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv);
    ~WrappedClip() {
        clip = NULL;
        delete fakeEnv;
    }
};

struct AvisynthArgs {
    std::string name;
    short type;
    bool required;
    AvisynthArgs(const std::string &name, short type, bool required) : name(name), type(type), required(required) {}
};

struct WrappedFunction {
    std::string name;
    FakeAvisynth::ApplyFunc apply;
    std::vector<AvisynthArgs> parsedArgs;
    void *avsUserData;
    WrappedFunction(const std::string &name, FakeAvisynth::ApplyFunc apply, const std::vector<AvisynthArgs> &parsedArgs, void *avsUserData);
};

}

#endif // AVISYNTH_COMPAT_H
