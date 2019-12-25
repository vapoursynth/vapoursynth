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

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>

/*---------------------------------------------------------
---------------------------------------------------------*/

class VapourSynther final :
    public VapourSynther_ {
    int references;

    int num_threads;
    const VSAPI *vsapi;
    VSScript *se;
    bool enable_v210;
    VSNodeRef *node;

    std::wstring errText;

    const VSVideoInfo *vi;

    std::string lastStringValue;

    std::vector<uint8_t> packedFrame;

    // Frame read ahead.
    int prefetchFrames;
    std::atomic<int> pendingRequests;

    // Cache last accessed frame, to reduce interference with read-ahead.
    int lastPosition;
    const VSFrameRef *lastFrame;

    // Exception protected take a copy of the current error message
    void setError(const char *text, const wchar_t *alt = 0);

    // Retrieve the current error message
    const wchar_t* getError();

    // Exception protected refresh the environment
    int/*error*/ newEnv();

    // (Re)Open the Script File
    int/*error*/ Import(const wchar_t* szScriptName);

    // Print the VideoInfo contents to the log file.
    void reportFormat(AvfsLog_* log);

    static void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg);
public:

    int BitsPerPixel();
    int BMPSize();
    uint8_t *GetPackedFrame();
    const VSAPI *GetVSApi();

    // Exception protected GetFrame()
    const VSFrameRef *GetFrame(AvfsLog_* log, int n, bool *success = 0);

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

void VS_CC VapourSynther::frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
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

        if (!vsscript_evaluateScript(&se, script.c_str(), scriptName.c_str(), efSetWorkingDir)) {

            node = vsscript_getOutput(se, 0);
            if (!node)
                goto vpyerror;
            vi = vsapi->getVideoInfo(node);

            if (vi->width == 0 || vi->height == 0 || vi->format == nullptr || vi->numFrames == 0) {
                setError("Cannot open clips with varying dimensions or format in AVFS");
                return ERROR_ACCESS_DENIED;
            }

            if (!HasSupportedFourCC(vi->format->id)) {
                std::string error_msg = "AVFS module doesn't support ";
                error_msg += vi->format->name;
                error_msg += " output";
                setError(error_msg.c_str());
                return ERROR_ACCESS_DENIED;
            }

            // set the special options hidden in global variables
            int error;
            int64_t val;
            VSMap *options = vsapi->createMap();
            vsscript_getVariable(se, "enable_v210", options);
            val = vsapi->propGetInt(options, "enable_v210", 0, &error);
            if (!error)
                enable_v210 = !!val && (vi->format->id == pfYUV422P10);
            else
                enable_v210 = false;
            vsapi->freeMap(options);

            VSCoreInfo info;
            vsapi->getCoreInfo2(vsscript_getCore(se), &info);
            num_threads = info.numThreads;

            packedFrame.clear();
            packedFrame.resize(BMPSize());

            return 0;
        } else {
        	vpyerror:
            setError(vsscript_getError(se));
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

        int msLen = (int)(1000.0 * vi->numFrames * vi->fpsDen / vi->fpsNum);
        log->Printf(L"  Duration: %8d frames, %02d:%02d:%02d.%03d\n", vi->numFrames,
            (msLen / (60 * 60 * 1000)), (msLen / (60 * 1000)) % 60, (msLen / 1000) % 60, msLen % 1000);
        const char* c_space = vi->format->name;

        log->Printf(L"  ColorSpace: %hs\n", c_space);

        log->Printf(L"  Width:%4d pixels, Height:%4d pixels.\n", vi->width, vi->height);

        log->Printf(L"  Frames per second: %7.4f (%u/%u)\n", (double)vi->fpsNum / vi->fpsDen,
            vi->fpsNum, vi->fpsDen);

        // fixme, inspect first frame to derive this?
        /*
        log->Printf(L"  FieldBased (Separated) Video: %hs\n", vi.IsFieldBased() ? "Yes" : "No");

        try {
            log->Printf(L"  Parity: %hs field first.\n", (clip->GetParity(0) ? "Top" : "Bottom"));
        } catch (...) {}

        const char* s_parity;
        if (vi.IsTFF() && vi.IsBFF())
            s_parity = "Invalid";
        else if (vi.IsTFF())
            s_parity = "Assumed Top Field First";
        else if (vi.IsBFF())
            s_parity = "Assumed Bottom Field First";
        else
            s_parity = "Unspecified";
        log->Printf(L"  Field order: %hs\n", s_parity);
        */
    } else
        log->Print(L"No video stream.\n");

    log->Print(L"No audio stream.\n");
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Exception protected PVideoFrame->GetFrame()
const VSFrameRef *VapourSynther::GetFrame(AvfsLog_* log, int n, bool *_success) {

    const VSFrameRef *f = nullptr;
    bool success = false;
    bool doPrefetch = false;

    if (n == lastPosition) {
        f = vsapi->cloneFrameRef(lastFrame);
        success = true;
    } else {
        int ndiff = n - lastPosition;
        doPrefetch = (ndiff == 1 && lastPosition >= 0); // only prefetch if seeking forward one or a few frames at a time
        lastPosition = -1;
        vsapi->freeFrame(lastFrame);
        lastFrame = nullptr;

        if (node) {
            char errMsg[512];
            f = vsapi->getFrame(n, node, errMsg, sizeof(errMsg));
            success = !!f;
            if (success) {
                if (NeedsPacking(vi->format->id)) {
                    const VSFormat *fi = vsapi->getFrameFormat(f);

                    p2p_buffer_param p = {};
                    p.width = vsapi->getFrameWidth(f, 0);
                    p.height = vsapi->getFrameHeight(f, 0);
                    p.dst[0] = packedFrame.data();
                    // Used by most
                    p.dst_stride[0] = p.width * 4 * fi->bytesPerSample;

                    for (int plane = 0; plane < fi->numPlanes; plane++) {
                        p.src[plane] = vsapi->getReadPtr(f, plane);
                        p.src_stride[plane] = vsapi->getStride(f, plane);
                    }

                    if (fi->id == pfRGB24) {
                        p.packing = p2p_argb32_le;
                        for (int plane = 0; plane < 3; plane++) {
                            p.src[plane] = vsapi->getReadPtr(f, plane) + vsapi->getStride(f, plane) * (vsapi->getFrameHeight(f, plane) - 1);
                            p.src_stride[plane] = -vsapi->getStride(f, plane);
                        }
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if (fi->id == pfRGB30) {
                        p.packing = p2p_rgb30_be;
                        p.dst_stride[0] = ((p.width + 63) / 64) * 256;
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if (fi->id == pfRGB48) {
                        p.packing = p2p_argb64_be;
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if (fi->id == pfYUV444P10) {
                        p.packing = p2p_y410_le;
                        p.dst_stride[0] = p.width * 2 * fi->bytesPerSample;
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if (fi->id == pfYUV444P16) {
                        p.packing = p2p_y416_le;
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if (fi->id == pfYUV422P10 && enable_v210) {
                        p.packing = p2p_v210_le;
                        p.dst_stride[0] = ((16 * ((p.width + 5) / 6) + 127) & ~127);
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else if ((fi->id == pfYUV420P16) || (fi->id == pfYUV422P16) || (fi->id == pfYUV420P10) || (fi->id == pfYUV422P10)) {
                        switch (fi->id) {
                        case pfYUV420P10: p.packing = p2p_p010_le; break;
                        case pfYUV422P10: p.packing = p2p_p210_le; break;
                        case pfYUV420P16: p.packing = p2p_p016_le; break;
                        case pfYUV422P16: p.packing = p2p_p216_le; break;
                        }
                        p.dst_stride[0] = p.width * fi->bytesPerSample;
                        p.dst_stride[1] = p.width * fi->bytesPerSample;
                        p.dst[1] = (uint8_t *)packedFrame.data() + p.dst_stride[0] * p.height;
                        p2p_pack_frame(&p, P2P_ALPHA_SET_ONE);
                    } else {
                        const int stride = vsapi->getStride(f, 0);
                        const int height = vsapi->getFrameHeight(f, 0);
                        int row_size = vsapi->getFrameWidth(f, 0) * fi->bytesPerSample;
                        if (fi->numPlanes == 1) {
                            vs_bitblt(packedFrame.data(), (row_size + 3) & ~3, vsapi->getReadPtr(f, 0), stride, row_size, height);
                        } else if (fi->numPlanes == 3) {
                            int row_size23 = vsapi->getFrameWidth(f, 1) * fi->bytesPerSample;

                            vs_bitblt(packedFrame.data(), row_size, vsapi->getReadPtr(f, 0), stride, row_size, height);

                            vs_bitblt((uint8_t *)packedFrame.data() + (row_size*height),
                                row_size23, vsapi->getReadPtr(f, 2),
                                vsapi->getStride(f, 2), vsapi->getFrameWidth(f, 2),
                                vsapi->getFrameHeight(f, 2));

                            vs_bitblt((uint8_t *)packedFrame.data() + (row_size*height + vsapi->getFrameHeight(f, 1)*row_size23),
                                row_size23, vsapi->getReadPtr(f, 1),
                                vsapi->getStride(f, 1), vsapi->getFrameWidth(f, 1),
                                vsapi->getFrameHeight(f, 1));
                        }
                    }

                }
            } else {
                setError(errMsg);
            }
        }
        if (!success) {
            log->Line(getError());
        } else {
            lastPosition = n;
            lastFrame = vsapi->cloneFrameRef(f);
        }
    }

    if (_success) *_success = success;

    if (success && doPrefetch) {
        for (int i = n + 1; i < std::min(n + prefetchFrames, vi->numFrames); i++) {
            ++pendingRequests;
            vsapi->getFrameAsync(i, node, VapourSynther::frameDoneCallback, static_cast<void *>(this));
        }
    }

    return f;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Readonly reference to VideoInfo
VideoInfoAdapter VapourSynther::GetVideoInfo() {

    return { vi, this, enable_v210 };

}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Read value of string variable from script. Returned pointer
// is valid until next call, so copy if you need it long term.
const char* VapourSynther::GetVarAsString(
    const char* varName, const char* defVal) {

    VSMap *map = vsapi->createMap();
    vsscript_getVariable(se, varName, map);
    int err = 0;
    const char *result = vsapi->propGetData(map, varName, 0, &err);
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
    vsscript_getVariable(se, varName, map);
    int err = 0;
    int result = int64ToIntS(vsapi->propGetInt(map, varName, 0, &err));
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
// Exception protected refresh the IScriptEnvironment
int/*error*/ VapourSynther::newEnv() {
    if (vi) {
        vsapi->freeNode(node);
        node = nullptr;
        vsscript_freeScript(se);
        se = nullptr;
    }

    return 0;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Constructor
VapourSynther::VapourSynther(void) :
    references(1),
    se(nullptr),
    node(nullptr),
    prefetchFrames(0),
    pendingRequests(0),
    lastPosition(-1),
    lastFrame(nullptr),
    vi(nullptr) {
    vsapi = vsscript_getVSApi();
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Destructor
VapourSynther::~VapourSynther(void) {
    ASSERT(!references);

    vsapi->freeFrame(lastFrame);
    lastFrame = nullptr;

    vsapi->freeNode(node);
    node = nullptr;

    if (se) {
        vsscript_freeScript(se);
        se = nullptr;
    }
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
        vsapi->getCoreInfo2(vsscript_getCore(se), &info);
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
    return ::BitsPerPixel(vi, enable_v210);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::BMPSize() {
    return ::BMPSize(vi, enable_v210);
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
        VsfsWavMediaInit(log, avs, volume);
        VsfsAviMediaInit(log, avs, volume);
        avs->Release();
    }
}

