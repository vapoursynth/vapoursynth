// avfs.cpp : Avisynth Virtual File System
//
// Avisynth v2.5.  Copyright 2008 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.


#include "avfsincludes.h"

/*---------------------------------------------------------
---------------------------------------------------------*/

class VapourSynther final :
    public VapourSynther_ {
    int references = 1;

    int num_threads = 1;
    const VSAPI *vsapi;
    const VSSCRIPTAPI *vssapi;
    VSScript *se = nullptr;
    int alt_output = 0;
    VSNode *videoNode = nullptr;
    VSNode *audioNode = nullptr;

    std::wstring errText;

    const VSVideoInfo *vi = nullptr;
    const VSAudioInfo *ai = nullptr;

    std::string lastStringValue;

    std::vector<uint8_t> packedFrame;

    // Frame read ahead.
    int prefetchFrames = 0;
    std::atomic<int> pendingRequests;

    // Cache last accessed frame, to reduce interference with read-ahead.
    int lastPosition = -1;
    const VSFrame *lastFrame = nullptr;

    // Exception protected take a copy of the current error message
    void setError(const char *text, const wchar_t *alt = 0);

    // Retrieve the current error message
    const wchar_t* getError();

    void free();

    // Exception protected refresh the environment
    int/*error*/ newEnv();

    // (Re)Open the Script File
    int/*error*/ Import(const wchar_t* szScriptName);

    // Print the VideoInfo contents to the log file.
    void reportFormat(AvfsLog_* log);

    static void VS_CC frameDoneCallback(void *userData, const VSFrame *f, int n, VSNode *, const char *errorMsg);
public:

    int BitsPerPixel();
    int BMPSize();
    uint8_t *GetPackedFrame();
    const VSAPI *GetVSApi();

    bool GetAudio(AvfsLog_ *log, void *buf, __int64 start, unsigned count);

    // Exception protected GetFrame()
    const VSFrame *GetFrame(AvfsLog_* log, int n, bool *success = 0);

    // Readonly reference to VideoInfo
    VideoInfoAdapter GetVideoInfo();

    // Read value of string variable from script. Returned pointer
    // is valid until next call, so copy if you need it long term.
    const char* GetVarAsString(const char* varName, const char* defVal);
    bool GetVarAsBool(const char* varName, bool defVal);
    int GetVarAsInt(const char* varName, int defVal);

    VapourSynther(void);
    ~VapourSynther(void);
    int/*error*/ Init(AvfsLog_* log, AvfsVolume_* volume);
    void AddRef(void);
    void Release(void);
};

/*---------------------------------------------------------
---------------------------------------------------------*/

void VS_CC VapourSynther::frameDoneCallback(void *userData, const VSFrame *f, int n, VSNode *, const char *errorMsg) {
    VapourSynther *vsynther = static_cast<VapourSynther *>(userData);
    vsynther->vsapi->freeFrame(f);
    --vsynther->pendingRequests;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

std::string get_file_contents(const std::wstring &filename) {
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (in) {
        std::string contents;
        in.seekg(0, std::ios::end);
        if (in.tellg() > 16 * 1024 * 1014)
            return "";
        contents.resize(static_cast<unsigned>(in.tellg()));
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return(contents);
    }
    return "";
}

// (Re)Open the Script File
int/*error*/ VapourSynther::Import(const wchar_t* wszScriptName) {
    std::string scriptName = utf16_to_utf8(wszScriptName);

    if (!scriptName.empty()) {
        std::string script = get_file_contents(wszScriptName);
        if (script.empty())
            goto vpyerror;

        se = vssapi->createScript(nullptr);
        vssapi->evalSetWorkingDir(se, 1);
        vssapi->evaluateBuffer(se, script.c_str(), scriptName.c_str());

        if (!vssapi->getError(se)) {
            videoNode = vssapi->getOutputNode(se, 0);
            if (!videoNode || vsapi->getNodeType(videoNode) != mtVideo) {
                setError("Output index 0 is not set or not a video node");
                return ERROR_ACCESS_DENIED;
            }

            vi = vsapi->getVideoInfo(videoNode);

            if (!isConstantVideoFormat(vi)) {
                setError("Cannot open clips with varying dimensions or format in AVFS");
                return ERROR_ACCESS_DENIED;
            }

            if (!HasSupportedFourCC(vi->format)) {
                std::string error_msg = "AVFS module doesn't support ";
                char nameBuffer[32];
                vsapi->getVideoFormatName(&vi->format, nameBuffer);
                error_msg += nameBuffer;
                error_msg += " output";
                setError(error_msg.c_str());
                return ERROR_ACCESS_DENIED;
            }

            audioNode = vssapi->getOutputNode(se, 1);
            if (audioNode && vsapi->getNodeType(audioNode) != mtAudio) {
                setError("Output index index 1 is not an audio node");
                return ERROR_ACCESS_DENIED;
            }

            if (audioNode)
                ai = vsapi->getAudioInfo(audioNode);

            alt_output = vssapi->getAltOutputMode(se, 0);

            VSCoreInfo info;
            vsapi->getCoreInfo(vssapi->getCore(se), &info);
            num_threads = info.numThreads;

            packedFrame.clear();
            packedFrame.resize(BMPSize());

            return 0;
        } else {
            vpyerror:
            setError(vssapi->getError(se));
            return ERROR_ACCESS_DENIED;
        }
    }
    return ERROR_ACCESS_DENIED;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::reportFormat(AvfsLog_* log) {
    if (vi) {
        log->Print(L"Video stream :-\n");

        char nameBuffer[32];
        vsapi->getVideoFormatName(&vi->format, nameBuffer);

        int msLen = (int)(1000.0 * vi->numFrames * vi->fpsDen / vi->fpsNum);
        log->Printf(L"  Duration: %8d frames, %02d:%02d:%02d.%03d\n", vi->numFrames, (msLen / (60 * 60 * 1000)), (msLen / (60 * 1000)) % 60, (msLen / 1000) % 60, msLen % 1000);
        log->Printf(L"  Format: %hs\n", &nameBuffer);
        log->Printf(L"  Width:%4d pixels, Height:%4d pixels.\n", vi->width, vi->height);
        log->Printf(L"  Frames per second: %7.4f (%u/%u)\n", (double)vi->fpsNum / vi->fpsDen, vi->fpsNum, vi->fpsDen);
    } else {
        log->Print(L"No video stream.\n");
    }

    if (ai) {
        log->Print(L"Audio stream :-\n");

        char nameBuffer[32];
        vsapi->getAudioFormatName(&ai->format, nameBuffer);

        int msLen = (int)(1000.0 * ai->numSamples / ai->sampleRate);
        log->Printf(L"  Audio length: %I64u samples. %02d:%02d:%02d.%03d\n", ai->numSamples, (msLen/(60*60*1000)), (msLen/(60*1000))%60, (msLen/1000)%60, msLen%1000);
        log->Printf(L"  Format: %hs\n", &nameBuffer);
        log->Printf(L"  Audio Channels: %-8d\n", ai->format.numChannels);
        log->Printf(L"  Samples Per Second: %5d\n", ai->sampleRate);
    } else {
        log->Print(L"No audio stream.\n");
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

bool VapourSynther::GetAudio(AvfsLog_ *log, void *buf, __int64 start, unsigned count) {
    const VSAudioFormat &af = ai->format;

    uint8_t *dst = reinterpret_cast<uint8_t *>(buf);

    int startFrame = static_cast<int>(start / VS_AUDIO_FRAME_SAMPLES);
    int endFrame = static_cast<int>((start + count - 1) / VS_AUDIO_FRAME_SAMPLES);
    
    std::vector<const uint8_t *> tmp;
    tmp.resize(af.numChannels);

    size_t bytesPerOutputSample = (af.bitsPerSample + 7) / 8;

    for (int i = startFrame; i <= endFrame; i++) {
        const VSFrame *f = vsapi->getFrame(i, audioNode, nullptr, 0);
        int64_t firstFrameSample = i * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
        size_t offset = 0;
        int copyLength = VS_AUDIO_FRAME_SAMPLES;
        if (firstFrameSample < start) {
            offset = (start - firstFrameSample) * af.bytesPerSample;
            copyLength -= (start - firstFrameSample);
        }

        if (copyLength > count)
            copyLength = count;

        for (int c = 0; c < af.numChannels; c++)
            tmp[c] = vsapi->getReadPtr(f, c) + offset;

        assert(copyLength > 0);

        if (bytesPerOutputSample == 2)
            PackChannels16to16le(tmp.data(), dst, copyLength, af.numChannels);
        else if (bytesPerOutputSample == 3)
            PackChannels32to24le(tmp.data(), dst, copyLength, af.numChannels);
        else if (bytesPerOutputSample == 4)
            PackChannels32to32le(tmp.data(), dst, copyLength, af.numChannels);

        dst += copyLength * bytesPerOutputSample * af.numChannels;
        count -= copyLength;

        vsapi->freeFrame(f);
    }

    assert(count == 0);

    // FIXME, maybe do error handling
    return true;
}

// Exception protected PVideoFrame->GetFrame()
const VSFrame *VapourSynther::GetFrame(AvfsLog_* log, int n, bool *_success) {

    const VSFrame *f = nullptr;
    bool success = false;
    bool doPrefetch = false;

    if (n == lastPosition) {
        f = vsapi->addFrameRef(lastFrame);
        success = true;
    } else {
        int ndiff = n - lastPosition;
        doPrefetch = (ndiff == 1 && lastPosition >= 0); // only prefetch if seeking forward one or a few frames at a time
        lastPosition = -1;
        vsapi->freeFrame(lastFrame);
        lastFrame = nullptr;

        if (videoNode) {
            char errMsg[512];
            f = vsapi->getFrame(n, videoNode, errMsg, sizeof(errMsg));
            success = !!f;
            if (success) {
                if (NeedsPacking(vi->format, alt_output)) {
                    const uint8_t *src[3] = {};
                    ptrdiff_t src_stride[3] = {};

                    for (int plane = 0; plane < vi->format.numPlanes; plane++) {
                        src[plane] = vsapi->getReadPtr(f, plane);
                        src_stride[plane] = vsapi->getStride(f, plane);
                    }

                    PackOutputFrame(src, src_stride, packedFrame.data(), vsapi->getFrameWidth(f, 0), vsapi->getFrameHeight(f, 0), vi->format, alt_output);
                }
            } else {
                setError(errMsg);
            }
        }
        if (!success) {
            log->Line(getError());
        } else {
            lastPosition = n;
            lastFrame = vsapi->addFrameRef(f);
        }
    }

    if (_success) *_success = success;

    if (success && doPrefetch) {
        for (int i = n + 1; i < std::min(n + prefetchFrames, vi->numFrames); i++) {
            ++pendingRequests;
            vsapi->getFrameAsync(i, videoNode, VapourSynther::frameDoneCallback, static_cast<void *>(this));
        }
    }

    return f;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Readonly reference to VideoInfo
VideoInfoAdapter VapourSynther::GetVideoInfo() {
    return { vi, ai, this, alt_output };
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Read value of string variable from script. Returned pointer
// is valid until next call, so copy if you need it long term.
const char* VapourSynther::GetVarAsString(
    const char* varName, const char* defVal) {

    VSMap *map = vsapi->createMap();
    vssapi->getVariable(se, varName, map);
    int err = 0;
    const char *result = vsapi->mapGetData(map, varName, 0, &err);
    if (err)
        result = defVal;

    lastStringValue = result ? result : "";
    vsapi->freeMap(map);

    return lastStringValue.c_str();
}

/*---------------------------------------------------------
---------------------------------------------------------*/

bool VapourSynther::GetVarAsBool(const char* varName, bool defVal) {
    return !!GetVarAsInt(varName, defVal);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::GetVarAsInt(const char* varName, int defVal) {
    VSMap *map = vsapi->createMap();
    vssapi->getVariable(se, varName, map);
    int err = 0;
    int result = vsapi->mapGetIntSaturated(map, varName, 0, &err);
    if (err)
        result = defVal;
    vsapi->freeMap(map);

    return result;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Take a copy of the current error message
void VapourSynther::setError(const char *_text, const wchar_t *alt) {
    errText.clear();

    if (_text)
        errText = utf16_from_utf8(_text);
    else if (alt)
        errText = alt;
    else
        errText = L"VapourSynthError.msg corrupted.";
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Retrieve the current avisynth error message
const wchar_t* VapourSynther::getError() {
    return errText.c_str();
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::free() {
    if (ai) {
        vsapi->freeNode(audioNode);
        audioNode = nullptr;
    }
    if (vi) {
        vsapi->freeFrame(lastFrame);
        lastFrame = nullptr;
        vsapi->freeNode(videoNode);
        videoNode = nullptr;
    }
    if (vi || ai) {
        vssapi->freeScript(se);
        ai = nullptr;
        vi = nullptr;
        se = nullptr;
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/
// Exception protected refresh the IScriptEnvironment
int/*error*/ VapourSynther::newEnv() {
    free();
    return 0;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Constructor
VapourSynther::VapourSynther(void) : pendingRequests(0) {
    vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Destructor
VapourSynther::~VapourSynther(void) {
    ASSERT(!references);
    free();
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int/*error*/ VapourSynther::Init(
    AvfsLog_* log,
    AvfsVolume_* volume) {

    int error = Import(volume->GetScriptFileName());

    if (error) {
        log->Line(getError());
    } else {
        reportFormat(log);
    }

    if (!error) {
        // Initialize frame read-ahead logic.
        VSCoreInfo info;
        vsapi->getCoreInfo(vssapi->getCore(se), &info);
        prefetchFrames = GetVarAsInt("AVFS_ReadAheadFrameCount", -1);
        if (prefetchFrames < 0)
            prefetchFrames = info.numThreads;
    }

    return error;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::AddRef(void) {
    ASSERT(references);
    references++;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::Release(void) {
    ASSERT(references);
    if (!--references) {
        while (pendingRequests > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
        delete this;
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::BitsPerPixel() {
    return ::BitsPerPixel(vi->format, alt_output);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::BMPSize() {
    return ::BMPSize(vi, alt_output);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

uint8_t *VapourSynther::GetPackedFrame() {
    return packedFrame.data();
}

const VSAPI *VapourSynther::GetVSApi() {
    return vsapi;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VsfsProcessScript(
    AvfsLog_* log,
    AvfsVolume_* volume) {
    // Construct an implementation of the media interface and
    // initialize the script.
    VapourSynther* avs = new(std::nothrow) VapourSynther();
    if (avs && avs->Init(log, volume) != 0) {
        avs->Release();
        avs = 0;
    }
    if (avs) {
        AvfsWavMediaInit(log, avs, volume);
        VsfsAviMediaInit(log, avs, volume);
        avs->Release();
    }
}

