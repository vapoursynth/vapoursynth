/*
* Copyright (c) 2012-2015 Fredrik Mellbin
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

#include "avisynth.h"
#include "VapourSynth.h"
#include "VSHelper.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <mutex>

namespace AvisynthCompat {

struct WrappedClip;

class FakeAvisynth : public IScriptEnvironment2 {
    friend class VSClip;
    friend class ::VideoFrame;
private:
    VSCore *core;
    std::set<std::string> savedStrings;
    const VSAPI *vsapi;
    std::map<VideoFrame *, const VSFrameRef *> ownedFrames;
    int interfaceVersion;
    std::string charToFilterArgumentString(char c);
    std::mutex registerFunctionLock;
    std::set<std::string> registeredFunctions;
public:
    const VSFrameRef *avsToVSFrame(VideoFrame *frame);

    // ugly, but unfortunately the best place to put the pseudo global variables, actually they're per context
    // the locking prevents multiple accesses at once
    bool initializing;
    int uglyN;
    VSNodeRef *uglyNode;
    VSFrameContext *uglyCtx;

    FakeAvisynth(int interfaceVersion, VSCore *core, const VSAPI *vsapi) : core(core), vsapi(vsapi), interfaceVersion(interfaceVersion), initializing(true), uglyN(-1), uglyNode(nullptr), uglyCtx(nullptr) {}
    // virtual avisynth functions
    AVSC_CC ~FakeAvisynth();

    int __stdcall GetCPUFlags();

    char* __stdcall SaveString(const char* s, int length = -1);
    char* __stdcall Sprintf(const char* fmt, ...) ;
    char* __stdcall VSprintf(const char* fmt, void* val);

    __declspec(noreturn) void __stdcall ThrowError(const char* fmt, ...);

    void __stdcall AddFunction(const char* name, const char* params, ApplyFunc apply, void* user_data);
    bool __stdcall FunctionExists(const char* name);
    AVSValue __stdcall Invoke(const char* name, const AVSValue args, const char* const* arg_names = 0);

    AVSValue __stdcall GetVar(const char* name);
    bool __stdcall SetVar(const char* name, const AVSValue& val);
    bool __stdcall SetGlobalVar(const char* name, const AVSValue& val);

    void __stdcall PushContext(int level = 0);
    void __stdcall PopContext();

    // align should be 4 or 8
    PVideoFrame __stdcall NewVideoFrame(const VideoInfo& vi, int align = FRAME_ALIGN);

    bool __stdcall MakeWritable(PVideoFrame* pvf);

    void __stdcall BitBlt(BYTE* dstp, int dst_pitch, const BYTE* srcp, int src_pitch, int row_size, int height);

    void __stdcall AtExit(ShutdownFunc function, void* user_data);

    void __stdcall CheckVersion(int version = AVISYNTH_INTERFACE_VERSION);

    PVideoFrame __stdcall Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height);

    int __stdcall SetMemoryMax(int mem);

    int __stdcall SetWorkingDir(const char * newdir);

    void* __stdcall ManageCache(int key, void* data);

    bool __stdcall PlanarChromaAlignment(PlanarChromaAlignmentMode key);

    PVideoFrame __stdcall SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size,
        int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV);

    void __stdcall DeleteScriptEnvironment();

    void __stdcall ApplyMessage(PVideoFrame* frame, const VideoInfo& vi, const char* message, int size,
        int textcolor, int halocolor, int bgcolor);

    const AVS_Linkage* const __stdcall GetAVSLinkage();

    // noThrow version of GetVar
    AVSValue __stdcall GetVarDef(const char* name, const AVSValue& def = AVSValue());

    /*IScriptEnvironment2*/

    // Generic system to ask for various properties
    size_t  __stdcall GetProperty(AvsEnvProperty prop);

    // Returns TRUE and the requested variable. If the method fails, returns FALSE and does not touch 'val'.
    bool  __stdcall GetVar(const char* name, AVSValue *val) const;

    // Return the value of the requested variable.
    // If the variable was not found or had the wrong type,
    // return the supplied default value.
    bool __stdcall GetVar(const char* name, bool def) const;
    int  __stdcall GetVar(const char* name, int def) const;
    double  __stdcall GetVar(const char* name, double def) const;
    const char*  __stdcall GetVar(const char* name, const char* def) const;

    // Plugin functions
    bool __stdcall LoadPlugin(const char* filePath, bool throwOnError, AVSValue *result);
    void __stdcall AddAutoloadDir(const char* dirPath, bool toFront);
    void __stdcall ClearAutoloadDirs();
    void __stdcall AutoloadPlugins();
    void __stdcall AddFunction(const char* name, const char* params, ApplyFunc apply, void* user_data, const char *exportVar);
    bool __stdcall InternalFunctionExists(const char* name);

    // Threading
    void __stdcall SetFilterMTMode(const char* filter, MtMode mode, bool force); // If filter is "DEFAULT_MT_MODE", sets the default MT mode
    IJobCompletion* __stdcall NewCompletion(size_t capacity);
    void __stdcall ParallelJob(ThreadWorkerFuncPtr jobFunc, void* jobData, IJobCompletion* completion);

    // This version of Invoke will return false instead of throwing NotFound().
    bool __stdcall Invoke(AVSValue *result, const char* name, const AVSValue& args, const char* const* arg_names = 0);

    // Support functions
    void* __stdcall Allocate(size_t nBytes, size_t alignment, AvsAllocType type);
    void __stdcall Free(void* ptr);

    // These lines are needed so that we can overload the older functions from IScriptEnvironment.
    using IScriptEnvironment::Invoke;
    using IScriptEnvironment::AddFunction;
    using IScriptEnvironment::GetVar;

    PVideoFrame __stdcall SubframePlanarA(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size,
        int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV, int rel_offsetA);

};

class VSClip : public IClip {
private:
    VSNodeRef *clip;
    FakeAvisynth *fakeEnv;
    const VSAPI *vsapi;
    int numSlowWarnings;
    VideoInfo vi;
public:
    VSClip(VSNodeRef *clip, FakeAvisynth *fakeEnv, const VSAPI *vsapi);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
    bool __stdcall GetParity(int n) {
        return true;
    }
    void __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) {}
    int __stdcall SetCacheHints(int cachehints, int frame_range) {
        // intentionally ignore
        return 0;
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
    WrappedClip(const std::string &filterName, const PClip &clip, const std::vector<VSNodeRef *> &preFetchClips, const PrefetchInfo &prefetchInfo, FakeAvisynth *fakeEnv);
    ~WrappedClip() {
        clip = nullptr;
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
    int interfaceVersion;
    WrappedFunction(const std::string &name, FakeAvisynth::ApplyFunc apply, const std::vector<AvisynthArgs> &parsedArgs, void *avsUserData, int interfaceVersion);
};

}

#endif // AVISYNTH_COMPAT_H
