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
//#include "traces.h"

/*---------------------------------------------------------
---------------------------------------------------------*/

class VapourSynther :
    public VapourSynther_ {
    int references;

    //  TRCHANNEL trace;

    // Function pointer
    //ICreateScriptEnvironment CreateScriptEnvironment;

    // VSScript.dll
    // fixme
    //HMODULE hlib;

    int num_threads;
    const VSAPI *vsapi;
    VSScript *se;
    bool enable_v210;
    bool pad_scanlines;
    VSNodeRef *node;

    wchar_t *errText;

    const VSVideoInfo *vi;

    char* lastStringValue;

    std::vector<uint8_t> packedFrame;

    // Frame read ahead.
    HANDLE fraThread;
    CRITICAL_SECTION fraMutex;
    HANDLE fraResumeEvent;
    HANDLE fraSuspendedEvent;
    int fraPosition;
    int fraEndPosition;
    int fraSuspendCount;
    enum { fraDefaultFrameCount = 0 };
    enum { fraMaxFrameCount = 100 };
    int fraFrameCount;
    enum { fraMaxResumeDelay = 1000 };
    enum { fraDefaultResumeDelay = 10 };
    int fraResumeDelay;

    // Cache last accessed frame, to reduce interference with read-ahead.
    int lastPosition;
    const VSFrameRef *lastFrame;

    // Exception protected take a copy of the current error message
    void setError(const char *text, const wchar_t *alt = 0);

    // Retrieve the current avisynth error message
    const wchar_t* getError();

    // Exception protected refresh the IScriptEnvironment
    int/*error*/ newEnv();

    // (Re)Open the Script File
    int/*error*/ Import(const wchar_t* szScriptName);

    // Print the VideoInfo contents to the log file.
    void reportFormat(AvfsLog_* log);

    // Thread to read frames in background to better utilize multi core
    // systems.
    void FraThreadMain();
    static DWORD __stdcall FraThreadMainThunk(void* param);
public:

    int BitsPerPixel();
    int ImageSize();
    bool UsePacking();
    uint8_t *GetPackedFrame();
    const VSAPI *GetVSApi();

    // Avisynther_ interface

    void vprintf(const wchar_t* format, va_list args);
    void printf(const wchar_t* format, ...);

    // Exception protected PVideoFrame->GetFrame()
    const VSFrameRef *GetFrame(AvfsLog_* log, int n, bool *success = 0);

    // Readonly reference to VideoInfo
    VideoInfoAdapter GetVideoInfo();

    // Read value of string variable from script. Returned pointer
    // is valid until next call, so copy if you need it long term.
    const char* GetVarAsString(const char* varName, const char* defVal);
    bool GetVarAsBool(const char* varName, bool defVal);
    int GetVarAsInt(const char* varName, int defVal);

    // Suspend/resume frame read ahead as necessary for
    // performance optimization.
    void FraSuspend();
    void FraResume();

    VapourSynther(void);
    ~VapourSynther(void);
    int/*error*/ Init(AvfsLog_* log, AvfsVolume_* volume);
    void AddRef(void);
    void Release(void);
};

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::vprintf(const wchar_t* format, va_list args) {
    //   trace->printf(format,args);
}

void VapourSynther::printf(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    //   trace->vprintf(format,args);
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
    return "";;
}

// (Re)Open the Script File
int/*error*/ VapourSynther::Import(const wchar_t* wszScriptName) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    std::string scriptName = conversion.to_bytes(wszScriptName);

    if(!scriptName.empty())
    {
        std::string script = get_file_contents(wszScriptName);
        if (script.empty())
            goto vpyerror;

        if (!vsscript_evaluateScript(&se, script.c_str(), scriptName.c_str(), efSetWorkingDir)) {

            node = vsscript_getOutput(se, 0);
            if (!node)
                goto vpyerror;
            vi = vsapi->getVideoInfo(node);

            if (vi->width == 0 || vi->height == 0 || vi->format == nullptr || vi->numFrames == 0) {
                setError("Cannot open clips with varying dimensions or format in VSFS");
                return ERROR_ACCESS_DENIED;
            }

            int id = vi->format->id;
            if (id != pfCompatBGR32
                && id != pfRGB24
                && id != pfRGB48
                && id != pfCompatYUY2
                && id != pfYUV420P8
                && id != pfGray8
                && id != pfYUV444P8
                && id != pfYUV422P8
                && id != pfYUV411P8
                && id != pfYUV410P8
                && id != pfYUV420P10
                && id != pfYUV420P16
                && id != pfYUV422P10
                && id != pfYUV422P16
                && id != pfYUV444P16) {
                    std::string error_msg = "VSFS module doesn't support ";
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
                enable_v210 = !!val;
            else
                enable_v210 = false;
            vsscript_getVariable(se, "pad_scanlines", options);
            val = vsapi->propGetInt(options, "pad_scanlines", 0, &error);
            if (!error)
                pad_scanlines = !!val;
            else
                pad_scanlines = false;
            vsapi->freeMap(options);

            const VSCoreInfo *info = vsapi->getCoreInfo(vsscript_getCore(se));
            num_threads = info->numThreads;

            packedFrame.resize(ImageSize());

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
void VapourSynther::FraThreadMain() {
    int position;

    EnterCriticalSection(&fraMutex);
    // Destructor logic sets max suspend count to signal
    // thread to exit.
    while (fraSuspendCount != INT_MAX) {

        if (fraSuspendCount > 0 || fraPosition == fraEndPosition) {
            ResetEvent(fraResumeEvent);
            // Signal any waiting thread that the ra thread is
            // suspended, so OK to use env.
            SetEvent(fraSuspendedEvent);
            LeaveCriticalSection(&fraMutex);
            // Wait until more ra is necessary.
            WaitForSingleObject(fraResumeEvent, INFINITE);
            // Delay resuming read ahead a bit, to avoid slowing
            // down sequential reads.
            Sleep(fraResumeDelay);
            EnterCriticalSection(&fraMutex);
        } else {
            ResetEvent(fraSuspendedEvent);
            position = fraPosition;
            fraPosition++;
            LeaveCriticalSection(&fraMutex);

            // Read the next frame and release it. Might be better
            // to hold the reference, but the MRU caching in avisynth
            // is enough for reasonable read ahead depths.
            //      trace->printf(L"   ra %i\n",position);

            vsapi->freeFrame(vsapi->getFrame(position, node, nullptr, 0));

            EnterCriticalSection(&fraMutex);
        }
    }
    LeaveCriticalSection(&fraMutex);
}

DWORD __stdcall VapourSynther::FraThreadMainThunk(void* param) {
    static_cast<VapourSynther*>(param)->FraThreadMain();
    return 0;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::FraSuspend() {
    if (fraThread) {
        EnterCriticalSection(&fraMutex);
        fraSuspendCount++;
        LeaveCriticalSection(&fraMutex);
        WaitForSingleObject(fraSuspendedEvent, INFINITE);
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::FraResume() {
    if (fraThread) {
        EnterCriticalSection(&fraMutex);
        ASSERT(fraSuspendCount);
        fraSuspendCount--;
        if (fraSuspendCount == 0 && fraEndPosition > fraPosition) {
            SetEvent(fraResumeEvent);
        }
        LeaveCriticalSection(&fraMutex);
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Exception protected PVideoFrame->GetFrame()
const VSFrameRef *VapourSynther::GetFrame(AvfsLog_* log, int n, bool *_success) {

    const VSFrameRef *f = nullptr;
    bool success = false;

    if (n == lastPosition) {
        f = vsapi->cloneFrameRef(lastFrame);
        success = true;
    } else {

        FraSuspend();

        lastPosition = -1;
        lastFrame = 0;

        if (node) {
            char errMsg[512];
            f = vsapi->getFrame(n, node, errMsg, sizeof(errMsg));
            success = !!f;
            if (success) {
                if (UsePacking()) {
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
                        p2p_pack_frame(&p, 0);
                    } else if (fi->id == pfRGB48) {
                        p.packing = p2p_argb64_be;
                        p2p_pack_frame(&p, 0);
                    } else if (fi->id == pfYUV444P16) {
                        p.packing = p2p_y416_le;
                        p2p_pack_frame(&p, 0);
                    } else if (fi->id == pfYUV422P10 && enable_v210) {
                        p.packing = p2p_v210_le;
                        p.dst_stride[0] = ((16 * ((p.width + 5) / 6) + 127) & ~127);
                        p2p_pack_frame(&p, 0);
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
                        p2p_pack_frame(&p, 0);
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
            vsapi->freeFrame(lastFrame);
            lastFrame = vsapi->cloneFrameRef(f);
            if (fraThread) {
                // Have read ahead thread continue reading subsequent
                // frames to allow better multi-core utilization.
                if (n > fraEndPosition || n + fraFrameCount * 2 < fraPosition) {
                    fraPosition = n + 1;
                }
                fraEndPosition = n + 1 + fraFrameCount;
            }
        }

        FraResume();
    }

    if (_success) *_success = success;

    return f;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Readonly reference to VideoInfo
VideoInfoAdapter VapourSynther::GetVideoInfo() {

    return { vi, this, (enable_v210 && vi->format->id == pfYUV422P10) };

}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Read value of string variable from script. Returned pointer
// is valid until next call, so copy if you need it long term.
const char* VapourSynther::GetVarAsString(
    const char* varName, const char* defVal) {
    FraSuspend();

    free(lastStringValue);
    lastStringValue = 0;

    VSMap *map = vsapi->createMap();
    vsscript_getVariable(se, varName, map);
    int err = 0;
    const char *result = vsapi->propGetData(map, varName, 0, &err);
    if (err)
        result = defVal;
    vsapi->freeMap(map);


    lastStringValue = strdup(result);

    FraResume();
    return lastStringValue;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

bool VapourSynther::GetVarAsBool(const char* varName, bool defVal) {
    return !!GetVarAsInt(varName, defVal);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::GetVarAsInt(const char* varName, int defVal) {
    FraSuspend();

    VSMap *map = vsapi->createMap();
    vsscript_getVariable(se, varName, map);
    int err = 0;
    int result = vsapi->propGetInt(map, varName, 0, &err);
    if (err)
        result = defVal;
    vsapi->freeMap(map);

    FraResume();
    return result;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Take a copy of the current error message
void VapourSynther::setError(const char *_text, const wchar_t *alt) {

    if (errText)
        free(errText);

    const int maxLength = 2046;
    char text[maxLength + 2] = "";
    int i = 0;

    // Exception protected copy error message,
    // reformat \r and \n for windows text output.
    __try {
        // We are accessing a string pointer that can easily point to an invalid
        // memory address or fail to have the expected null string terminator.
        // Do not call any routines inside the __try { } block.
        for (i = 0; i<maxLength; i++) {
            char ch = *_text++;
            while (ch == '\r') ch = *_text++;
            text[i] = ch;
            if (ch == '\0') break;
            if (ch == '\n') {
                text[i++] = '\r';
                text[i] = '\n';
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text[i] = '\0';
        //    trace->printf("setError: Trap accessing 0x%08X current contents :-\n%s\n",
        //	              _text, text);
        if (alt) {
            errText = ssdup(alt);
        } else {
            errText = ssdup(L"AvisynthError.msg corrupted.");
        }
        return;
    }
    text[maxLength + 1] = '\0';
    errText = ssconvalloc(text);
    //  trace->printf("%s\n",text);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Retrieve the current avisynth error message
const wchar_t* VapourSynther::getError() {

    return errText;

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
    //  trace(tropen(L"AVFS")),
    // fixme? hlib(nullptr),
    se(nullptr),
    node(nullptr),
    errText(nullptr),
    lastStringValue(nullptr),
    fraThread(nullptr),
    fraSuspendCount(0),
    fraPosition(0),
    fraEndPosition(0),
    fraFrameCount(0),
    fraResumeDelay(0),
    lastPosition(-1),
    lastFrame(nullptr),
    vi(nullptr) {
    vsapi = vsscript_getVSApi();
    InitializeCriticalSection(&fraMutex);
    fraResumeEvent = CreateEvent(0, 1, 0, 0);
    fraSuspendedEvent = CreateEvent(0, 1, 0, 0);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Destructor
VapourSynther::~VapourSynther(void) {
    ASSERT(!references);

    if (fraThread) {
        VERIFY(CloseHandle(fraThread));
    }
    if (fraResumeEvent) {
        VERIFY(CloseHandle(fraResumeEvent));
    }
    if (fraSuspendedEvent) {
        VERIFY(CloseHandle(fraSuspendedEvent));
    }
    DeleteCriticalSection(&fraMutex);

    free(lastStringValue);

    if (node) {
        lastFrame = 0;
        vsapi->freeNode(node);
        node = nullptr;
    }

    if (se) {
        vsscript_freeScript(se);
        se = nullptr;
    }

    /* fixme, probably shouldn't free this because of delayed loading
    if (hlib) {
        ASSERT(FreeLibrary(hlib));
        hlib = nullptr;
    }
    */

    free(errText);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int/*error*/ VapourSynther::Init(
    AvfsLog_* log,
    AvfsVolume_* volume) {
    // fixme? Load VSScript.dll
    //hlib = LoadLibrary(L"VSScript.dll");

    int error = Import(volume->GetScriptFileName());
    if (error) {
        log->Line(getError());
    } else {
        reportFormat(log);
    }

    if (!error) {
        // Initialize frame read-ahead logic.
        fraFrameCount = GetVarAsInt("AVFS_ReadAheadFrameCount",
            fraDefaultFrameCount);
        if (fraFrameCount < 0) {
            fraFrameCount = 0;
        }
        if (fraFrameCount > fraMaxFrameCount) {
            fraFrameCount = fraMaxFrameCount;
        }
        fraResumeDelay = GetVarAsInt("AVFS_ReadAheadDelayMsecs",
            fraDefaultResumeDelay);
        if (fraResumeDelay > fraMaxResumeDelay) {
            fraResumeDelay = fraMaxResumeDelay;
        }
        if (fraFrameCount && fraResumeEvent && fraSuspendedEvent) {
            ResetEvent(fraResumeEvent);
            SetEvent(fraSuspendedEvent);
            DWORD unusedThreadId;
            fraThread = CreateThread(0, 0, FraThreadMainThunk, this, 0, &unusedThreadId);
        }
    }

    if (error) {
        log->Line(getError());
    } else {
        reportFormat(log);
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
        if (fraThread) {
            // Kill the read-ahead thread before entering
            // destructor, to make sure it is not partially
            // torn down while thread running.
            FraSuspend();
            EnterCriticalSection(&fraMutex);
            fraSuspendCount = INT_MAX;
            SetEvent(fraResumeEvent);
            LeaveCriticalSection(&fraMutex);
            WaitForSingleObject(fraThread, INFINITE);
        }
        delete this;
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::BitsPerPixel() {
    int bits = vi->format->bytesPerSample * 8;
    if (vi->format->id == pfRGB24 || vi->format->id == pfRGB48 || vi->format->id == pfYUV444P16)
        bits *= 4;
    else if (vi->format->numPlanes == 3)
        bits += (bits * 2) >> (vi->format->subSamplingH + vi->format->subSamplingW);
    if (enable_v210 && vi->format->id == pfYUV422P10)
        bits = 20;
    return bits;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

static int BMPSize(int height, int rowsize) {
    return height * ((rowsize + 3) & ~3);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int VapourSynther::ImageSize() {
    if (!vi)
        return 0;
    int image_size;

    if (vi->format->id == pfYUV422P10 && enable_v210) {
        image_size = ((16 * ((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
    } else if (vi->format->id == pfRGB24 || vi->format->id == pfRGB48 || vi->format->id == pfYUV444P16) {
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample * 4);
    } else if (vi->format->numPlanes == 1) {
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample);
    } else {
        image_size = (vi->width * vi->format->bytesPerSample) >> vi->format->subSamplingW;
        if (image_size) {
            image_size *= vi->height;
            image_size >>= vi->format->subSamplingH;
            image_size *= 2;
        }
        image_size += vi->width * vi->format->bytesPerSample * vi->height;
    }
    return image_size;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

bool VapourSynther::UsePacking() {
    return (vi->format->id == pfRGB24 || vi->format->id == pfRGB48 || vi->format->id == pfYUV420P10 || vi->format->id == pfYUV420P16 || vi->format->id == pfYUV422P10 || vi->format->id == pfYUV422P16 || vi->format->id == pfYUV444P16);
}

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

