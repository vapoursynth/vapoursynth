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
* License along with Libav; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef AVISYNTH_COMPAT_H
#define AVISYNTH_COMPAT_H

#include <functional>
#include "avisynth_wrapper.h"
#include "../../include/VapourSynth.h"

namespace AvisynthCompat {

struct WrappedClip;

class FakeAvisynth : public IScriptEnvironment {
    friend class VSClip;
private:
    VSCore *core;
    QList<QByteArray> savedStrings;
    const VSAPI *vsapi;
    QMap<VideoFrame *, const VSFrameRef *> ownedFrames;
public:
    const VSFrameRef *avsToVSFrame(VideoFrame *frame);

    // ugly, but unfortunately the best place to put the pseudo global variables, actually they're per context
    // the locking prevents multiple accesses at once
    bool initializing;
    int uglyN;
    const VSNodeRef *uglyNode;
    VSFrameContext *uglyCtx;

    FakeAvisynth(VSCore *core, const VSAPI *vsapi) : core(core), vsapi(vsapi), uglyN(-1), uglyNode(NULL), uglyCtx(NULL), initializing(true) {}
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
    PVideoFrame __stdcall NewVideoFrame(const VideoInfo &vi, int align);
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
    const VSNodeRef *clip;
    FakeAvisynth *fakeEnv;
    const VSAPI *vsapi;
    VideoInfo vi;
public:
    VSClip(const VSNodeRef *clip, int64_t numAudioSamples, int nChannels, FakeAvisynth *fakeEnv, const VSAPI *vsapi)
        : clip(clip), fakeEnv(fakeEnv), vsapi(vsapi) {
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
        vi.fps_numerator = srcVi->fpsNum;
        vi.fps_denominator = srcVi->fpsDen;
        vi.num_frames = srcVi->numFrames;
        vi.audio_samples_per_second = 0;
        vi.sample_type = 0;
        vi.num_audio_samples = numAudioSamples;
        vi.nchannels = nChannels;
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
    bool __stdcall GetParity(int n) {
        return true;    //fixme?
    }
    void __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) {}
    void __stdcall SetCacheHints(int cachehints, int frame_range) {
        /*ignore*/
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
    PrefetchInfo prefetchInfo;
    QList<const VSNodeRef *> preFetchClips;
    PClip clip;
    FakeAvisynth *fakeEnv;
    int64_t magicalNumAudioSamplesForMVTools;
    int magicalNChannelsForMVTools;
    WrappedClip(const PClip &clip, const QList<const VSNodeRef *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv);
    ~WrappedClip() {
        clip = NULL;
        delete fakeEnv;
    }
};

struct AvisynthArgs {
    QByteArray name;
    short type;
    bool required;
    AvisynthArgs(const QByteArray &name, short type, bool required) : name(name), type(type), required(required) { }
};

struct WrappedFunction {
    QByteArray name;
    FakeAvisynth::ApplyFunc apply;
    QList<AvisynthArgs> parsedArgs;
    void *avsUserData;
    WrappedFunction(const QByteArray &name, FakeAvisynth::ApplyFunc apply, const QList<AvisynthArgs> &parsedArgs, void *avsUserData);
};

}

#endif // AVISYNTH_COMPAT_H
