// vsfs.cpp : VapourSynth Virtual File System
//
// VapourSynth modifications Copyright 2012-2013 Fredrik Mellbin
//
// Original code from:
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

#include "vsfsincludes.h"
#include <fstream>
#include <string>
#include <cerrno>
//#include "traces.h"

// make it work in vs2010
#define stricmp _stricmp

/*---------------------------------------------------------
---------------------------------------------------------*/

class VapourSynther:
    public VapourSynther_
{
    int references;

    //  TRCHANNEL trace;

    int num_threads;
    const VSAPI *vsapi;
    VSScript *se;
    bool enable_v210;
    bool pad_scanlines;
    VSNodeRef *node;

    wchar_t *errText;

    const VSVideoInfo *vi;

    wchar_t* lastStringValue;

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

    // Store temporary packed planes
    uint16_t *packedPlane1;
    uint16_t *packedPlane2;

    volatile long pending_requests;

    static void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg);

public:
    // VapourSynther_ interface

    void vprintf(const wchar_t* format,va_list args);
    void printf(const wchar_t* format,...);

    // Exception protected PVideoFrame->GetFrame()
    const VSFrameRef *GetFrame(AvfsLog_* log, int n, bool *success=0);

    // Readonly reference to VideoInfo
    const VSVideoInfo& GetVideoInfo();

    const VSAPI *GetVSAPI() { return vsapi; }
    bool EnableV210() { return enable_v210; }
    int ImageSize();
    const uint8_t *GetExtraPlane1() { return (const uint8_t *)packedPlane1; }
    const uint8_t *GetExtraPlane2() { return (const uint8_t *)packedPlane2; }
    VapourSynther(void);
    ~VapourSynther(void);
    int/*error*/ Init(AvfsLog_* log,AvfsVolume_* volume);
    void AddRef(void);
    void Release(void);
};

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::vprintf(const wchar_t* format,va_list args)
{
    //   trace->printf(format,args);
}

void VapourSynther::printf(const wchar_t* format,...)
{
    va_list args;
    va_start(args,format);
    //   trace->vprintf(format,args);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

std::string get_file_contents(const char *filename)
{
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (in)
  {
    std::string contents;
    in.seekg(0, std::ios::end);
    if (in.tellg() > 16 * 1024 * 1014)
        return "";
    contents.resize(in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    return(contents);
  }
  return "";;
}

// (Re)Open the Script File
int/*error*/ VapourSynther::Import(const wchar_t* wszScriptName)
{
    char szScriptName[MAX_PATH*2];
    WideCharToMultiByte(CP_UTF8, 0, wszScriptName, -1, szScriptName, sizeof(szScriptName), NULL, NULL);
    if(*szScriptName)
    {

        std::string script = get_file_contents(szScriptName);
        if (script.empty())
            goto vpyerror;

        if (!vsscript_evaluateScript(&se, script.c_str(), szScriptName, efSetWorkingDir)) {

            node = vsscript_getOutput(se, 0);
            if (!node)
                goto vpyerror;
            vi = vsapi->getVideoInfo(node);

            if (vi->width == 0 || vi->height == 0 || vi->format == NULL || vi->numFrames == 0) {
                setError("Cannot open clips with varying dimensions or format in VSFS");
                return ERROR_ACCESS_DENIED;
            }

            int id = vi->format->id;
            if (id != pfCompatBGR32
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
                && id != pfYUV422P16) {
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

            if (vi->format->id == pfYUV422P10 && enable_v210) {
                packedPlane1 = new uint16_t[ImageSize()];
            } else if (vi->format->id == pfYUV420P16 || vi->format->id == pfYUV422P16) {
                packedPlane2 = new uint16_t[vi->format->subSamplingH == 1 ? (vi->width*vi->height)/2 : vi->width*vi->height];
            } else if (vi->format->id == pfYUV420P10 || vi->format->id == pfYUV422P10) {
                packedPlane1 = new uint16_t[vi->width*vi->height];
                packedPlane2 = new uint16_t[vi->format->subSamplingH == 1 ? (vi->width*vi->height)/2 : vi->width*vi->height];
            }

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

void VapourSynther::reportFormat(AvfsLog_* log)
{
    log->Print(L"Video stream :-\n");

    int msLen = (int)(1000.0 * vi->numFrames * vi->fpsDen / vi->fpsNum);
    log->Printf(L"  Duration: %8d frames, %02d:%02d:%02d.%03d\n", vi->numFrames,
        (msLen/(60*60*1000)), (msLen/(60*1000))%60 ,(msLen/1000)%60, msLen%1000);
    log->Printf(L"  ColorSpace: %hs\n", vi->format->name);

    log->Printf(L"  Width:%4d pixels, Height:%4d pixels.\n", vi->width, vi->height);

    log->Printf(L"  Frames per second: %7.4f (%u/%u)\n", (double)vi->fpsNum/vi->fpsDen,
        (unsigned)vi->fpsNum, (unsigned)vi->fpsDen);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VS_CC VapourSynther::frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
    VapourSynther *vsfile = (VapourSynther *)userData;
    vsfile->vsapi->freeFrame(f);
    InterlockedDecrement(&vsfile->pending_requests);
}

const VSFrameRef *VapourSynther::GetFrame(AvfsLog_* log, int n, bool *_success) {

    const VSFrameRef *f = 0;
    bool success = false;

    if (n == lastPosition) {
        f = lastFrame;
        success = true;
    }
    else {
        lastPosition = -1;
        vsapi->freeFrame(lastFrame);
        lastFrame = 0;
        char errMsg[512];

        if (vi) {
            f = vsapi->getFrame(n, node, errMsg, 500);
            success = !!f;
            if (!f) {
                setError(errMsg);
            } else {
                const VSFormat *fi = vsapi->getFrameFormat(f);
                const int row_size = vsapi->getFrameWidth(f, 0) * fi->bytesPerSample;

                int out_pitch;
                int out_pitchUV;

                bool semi_packed_p10 = (vi->format->id == pfYUV420P10) || (vi->format->id == pfYUV422P10);
                bool semi_packed_p16 = (vi->format->id == pfYUV420P16) || (vi->format->id == pfYUV422P16);

                // BMP scanlines are dword-aligned
                if (vi->format->numPlanes == 1) {
                    out_pitch = (row_size+3) & ~3;
                    out_pitchUV = (vsapi->getFrameWidth(f, 1) * fi->bytesPerSample+3) & ~3;
                }
                // Planar scanlines are packed
                else {
                    out_pitch = row_size;
                    out_pitchUV = vsapi->getFrameWidth(f, 1) * fi->bytesPerSample;
                }

                const int height = vsapi->getFrameHeight(f, 0);
                if (vi->format->id == pfYUV422P10 && enable_v210) {
                    int width = vsapi->getFrameWidth(f, 0);
                    int pstride_y = vsapi->getStride(f, 0)/2;
                    int pstride_uv = vsapi->getStride(f, 1)/2;
                    const uint16_t *yptr = (const uint16_t *)vsapi->getReadPtr(f, 0);
                    const uint16_t *uptr = (const uint16_t *)vsapi->getReadPtr(f, 1);
                    const uint16_t *vptr = (const uint16_t *)vsapi->getReadPtr(f, 2);
                    uint32_t *outbuf = (uint32_t *)packedPlane1;
                    out_pitch = ((16*((width + 5) / 6) + 127) & ~127)/4;
                    for (int y = 0; y < height; y++) {
                        const uint16_t *yline = yptr;
                        const uint16_t *uline = uptr;
                        const uint16_t *vline = vptr;
                        uint32_t *out_line = outbuf;
                        for (int x = 0; x < width + 5; x += 6) {
                            out_line[0] = (uline[0] | (yline[0] << 10) | (vline[0] << 20));
                            out_line[1] = (yline[1] | (uline[1] << 10) | (yline[2] << 20));
                            out_line[2] = (vline[1] | (yline[3] << 10) | (uline[2] << 20));
                            out_line[3] = (yline[4] | (vline[2] << 10) | (yline[5] << 20));
                            out_line += 4;
                            yline += 6;
                            uline += 3;
                            vline += 3;
                        }
                        outbuf += out_pitch;
                        yptr += pstride_y;
                        uptr += pstride_uv;
                        vptr += pstride_uv;
                    }
                } else if (semi_packed_p10) {
                    int pwidth = vsapi->getFrameWidth(f, 0);
                    int pstride = vsapi->getStride(f, 0) / 2;
                    uint16_t *outbuf = packedPlane1;
                    const uint16_t *yptr = (const uint16_t *)vsapi->getReadPtr(f, 0);

                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < pwidth; x++) {
                            outbuf[x] = yptr[x] << 6;
                        }
                        outbuf += out_pitch/2;
                        yptr += pstride;
                    }
                }

                if (vi->format->id == pfYUV422P10 && enable_v210) {
                    // intentionally empty
                } else if (semi_packed_p10 || semi_packed_p16) {
                    int pheight = vsapi->getFrameHeight(f, 1);
                    int pwidth = vsapi->getFrameWidth(f, 1);
                    int pstride = vsapi->getStride(f, 1) / 2;
                    uint16_t *outbuf = (uint16_t *)packedPlane2;
                    const uint16_t *uptr = (const uint16_t *)vsapi->getReadPtr(f, 1);
                    const uint16_t *vptr = (const uint16_t *)vsapi->getReadPtr(f, 2);

                    if (semi_packed_p16) {
                        for (int y = 0; y < pheight; y++) {
                            for (int x = 0; x < pwidth; x++) {
                                outbuf[2*x] = uptr[x];
                                outbuf[2*x + 1] = vptr[x];
                            }
                            outbuf += out_pitchUV;
                            uptr += pstride;
                            vptr += pstride;
                        }
                    } else {
                        for (int y = 0; y < pheight; y++) {
                            for (int x = 0; x < pwidth; x++) {
                                outbuf[2*x] = uptr[x] << 6;
                                outbuf[2*x + 1] = vptr[x] << 6;
                            }
                            outbuf += out_pitchUV;
                            uptr += pstride;
                            vptr += pstride;
                        }
                    }
                }
            }
            if (!success) {
                log->Line(getError());
            }
            else {
                lastPosition = n;
                lastFrame = f;
            }

            for (int i = n + 1; i < std::min<int>(n + num_threads, vi->numFrames); i++) {
                InterlockedIncrement(&pending_requests);
                vsapi->getFrameAsync(i, node, frameDoneCallback, (void *)this);
            }
        }
    }

    if (_success)
        *_success = success;

    return f;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Readonly reference to VideoInfo
const VSVideoInfo& VapourSynther::GetVideoInfo() {
    return *vi;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Take a copy of the current error message
void VapourSynther::setError(const char *_text, const wchar_t *alt) {

    if (errText)
        free(errText);

    const int maxLength = 2046;
    char text[maxLength+2];
    int i = 0;

    // Exception protected copy error message,
    // reformat \r and \n for windows text output.
    __try {
        // We are accessing a string pointer that can easily point to an invalid
        // memory address or fail to have the expected null string terminator.
        // Do not call any routines inside the __try { } block.
        for (i=0; i<maxLength; i++) {
            char ch = *_text++;
            while (ch == '\r') ch = *_text++;
            text[i] = ch;
            if (ch == '\0') break;
            if (ch == '\n') {
                text[i++] = '\r';
                text[i]   = '\n';
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        text[i] = '\0';
        //    trace->printf("setError: Trap accessing 0x%08X current contents :-\n%s\n",
        //                  _text, text);
        if (alt) {
            errText = ssdup(alt);
        }
        else {
            errText = ssdup(L"Error message corrupted.");
        }
        return;
    }
    text[maxLength+1] = '\0';
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
int/*error*/ VapourSynther::newEnv()
{
    if (vi) {
        vsapi->freeNode(node);
        node = NULL;
        vsscript_freeScript(se);
        se = NULL;
        delete [] packedPlane1;
        delete [] packedPlane2;
    }

    return 0;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Constructor
VapourSynther::VapourSynther(void) :
references(1),

    num_threads(1),
    se(NULL),
    enable_v210(false),
    pad_scanlines(false),
    node(NULL),

    //  trace(tropen(L"AVFS")),
    vi(0),
    errText(0),
    lastStringValue(0),
    lastPosition(-1),
    lastFrame(0),
    packedPlane1(0),
    packedPlane2(0),
    pending_requests(0)
{
    vsapi = vsscript_getVSApi();
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Destructor
VapourSynther::~VapourSynther(void)
{
    ASSERT(!references);
    while (pending_requests > 0);
    delete [] packedPlane1;
    delete [] packedPlane2;
    if (se) {
        if (vsapi)
            vsapi->freeFrame(lastFrame);
        vsscript_freeScript(se);
        se = NULL;
    }
    ssfree(lastStringValue);
    ssfree(errText);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int/*error*/ VapourSynther::Init(
    AvfsLog_* log,
    AvfsVolume_* volume)
{
    int error = Import(volume->GetScriptFileName());

    if (error) {
        log->Line(getError());
    }
    else {
        reportFormat(log);
    }
    return error;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::AddRef(void)
{
    ASSERT(references);
    references ++;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void VapourSynther::Release(void)
{
    ASSERT(references);
    if (!--references)
        delete this;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void AvfsProcessScript(
    AvfsLog_* log,
    AvfsVolume_* volume)
{
    // Construct an implementation of the media interface and
    // initialize the script.
    VapourSynther* avs = new(std::nothrow) VapourSynther();
    if (avs && avs->Init(log,volume) != 0)
    {
        avs->Release();
        avs = 0;
    }
    if (avs)
    {
        AvfsAviMediaInit(log,avs,volume);
        avs->Release();
    }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

//fixme, move helpers to a shared file?
static int BMPSize(int height, int rowsize) {
    return height * ((rowsize+3) & ~3);
}

int VapourSynther::ImageSize() {
    if (!vi)
        return 0;
    int image_size;

    if (vi->format->id == pfYUV422P10 && enable_v210) {
        image_size = ((16*((vi->width + 5) / 6) + 127) & ~127);
        image_size *= vi->height;
    } else if (vi->format->numPlanes == 1 || pad_scanlines) {
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample);
        if (vi->format->numPlanes == 3)
            image_size += 2 * BMPSize(vi->height >> vi->format->subSamplingH, (vi->width >> vi->format->subSamplingW) * vi->format->bytesPerSample);
    } else { // Packed size
        image_size = (vi->width * vi->format->bytesPerSample) >> vi->format->subSamplingW;
        if (image_size) {
            image_size  *= vi->height;
            image_size >>= vi->format->subSamplingH;
            image_size  *= 2;
        }
        image_size += vi->width * vi->format->bytesPerSample * vi->height;
    }
    return image_size;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

BOOL APIENTRY DllMain(HANDLE hModule, ULONG ulReason, LPVOID lpReserved) {
    if (ulReason == DLL_PROCESS_ATTACH) {
        // fixme, move this where threading can't be an issue
        vsscript_init();
    } else if (ulReason == DLL_PROCESS_DETACH) {
        vsscript_finalize();
    }
    return TRUE;
}
