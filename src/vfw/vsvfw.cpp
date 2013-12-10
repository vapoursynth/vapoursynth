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

// loosely based on the relevant parts of main.cpp in avisynth

#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#include <vfw.h>
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <algorithm>
#include <fstream>
#include <string>
#include <errno.h>
#include <mutex>

#include "VSScript.h"
#include "VSHelper.h"

static long refCount=0;

static int BMPSize(int height, int rowsize) {
    return height * ((rowsize+3) & ~3);
}

// {58F74CA0-BD0E-4664-A49B-8D10E6F0C131}
extern "C" const GUID CLSID_VapourSynth =
{ 0x58f74ca0, 0xbd0e, 0x4664, { 0xa4, 0x9b, 0x8d, 0x10, 0xe6, 0xf0, 0xc1, 0x31 } };

extern "C" const GUID IID_IAvisynthClipInfo   // {E6D6B708-124D-11D4-86F3-DB80AFD98778}
    = {0xe6d6b708, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78}};

struct IAvisynthClipInfo : IUnknown {
    virtual int __stdcall GetError(const char** ppszMessage) = 0;
    virtual bool __stdcall GetParity(int n) = 0;
    virtual bool __stdcall IsFieldBased() = 0;
};

class VapourSynthFile: public IAVIFile, public IPersistFile, public IClassFactory, public IAvisynthClipInfo {
    friend class VapourSynthStream;
private:
    int num_threads;
    const VSAPI *vsapi;
    VSScript *se;
    bool enable_v210;
    bool pad_scanlines;
    VSNodeRef *node;
    long m_refs;
    std::string szScriptName;
    const VSVideoInfo* vi;
    std::string error_msg;
    volatile long pending_requests;

    std::mutex cs_filter_graph;

    bool DelayInit();
    bool DelayInit2();

    void Lock();
    void Unlock();

    int ImageSize();

public:

    VapourSynthFile(const CLSID& rclsid);
    ~VapourSynthFile();

    static HRESULT Create(const CLSID& rclsid, const IID& riid, void **ppv);
    static void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg);

    //////////// IUnknown

    STDMETHODIMP QueryInterface(const IID& iid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    //////////// IClassFactory

    STDMETHODIMP CreateInstance (LPUNKNOWN pUnkOuter, REFIID riid,  void * * ppvObj) ;
    STDMETHODIMP LockServer (BOOL fLock) ;

    //////////// IPersistFile

    STDMETHODIMP GetClassID(LPCLSID lpClassID);  // IPersist

    STDMETHODIMP IsDirty();
    STDMETHODIMP Load(LPCOLESTR lpszFileName, DWORD grfMode);
    STDMETHODIMP Save(LPCOLESTR lpszFileName, BOOL fRemember);
    STDMETHODIMP SaveCompleted(LPCOLESTR lpszFileName);
    STDMETHODIMP GetCurFile(LPOLESTR *lplpszFileName);

    //////////// IAVIFile

    STDMETHODIMP CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi);       // 5
    STDMETHODIMP EndRecord();                                                   // 8
    STDMETHODIMP GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam);   // 4
    STDMETHODIMP Info(AVIFILEINFOW *psi, LONG lSize);                           // 3

    STDMETHODIMP Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName);        // ???
    STDMETHODIMP Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,         // ???
        AVISAVECALLBACK lpfnCallback);

    STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb);                    // 7
    STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer);          // 6
    STDMETHODIMP DeleteStream(DWORD fccType, LONG lParam);                      // 9

    //////////// IAvisynthClipInfo

    int __stdcall GetError(const char** ppszMessage);
    bool __stdcall GetParity(int n);
    bool __stdcall IsFieldBased();
};

///////////////////////////////////

class VapourSynthStream: public IAVIStream , public IAVIStreaming {
public:

    //////////// IUnknown

    STDMETHODIMP QueryInterface(const IID& iid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    VapourSynthStream(VapourSynthFile *parentPtr, bool isAudio);
    ~VapourSynthStream();

    //////////// IAVIStream

    STDMETHODIMP Create(LPARAM lParam1, LPARAM lParam2);
    STDMETHODIMP Delete(LONG lStart, LONG lSamples);
    STDMETHODIMP_(LONG) Info(AVISTREAMINFOW *psi, LONG lSize);
    STDMETHODIMP_(LONG) FindSample(LONG lPos, LONG lFlags);
    STDMETHODIMP Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples);
    STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb);
    STDMETHODIMP ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat);
    STDMETHODIMP SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat);
    STDMETHODIMP Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
        LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten,
        LONG FAR *plBytesWritten);
    STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer);
    STDMETHODIMP SetInfo(AVISTREAMINFOW *psi, LONG lSize);

    //////////// IAVIStreaming

    STDMETHODIMP Begin(LONG lStart, LONG lEnd, LONG lRate);
    STDMETHODIMP End();

private:
    long m_refs;

    VapourSynthFile *parent;
    std::string sName;

    //////////// internal

    bool ReadFrame(void* lpBuffer, int n);

    HRESULT Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples);
};


BOOL APIENTRY DllMain(HANDLE hModule, ULONG ulReason, LPVOID lpReserved) {
    if (ulReason == DLL_PROCESS_ATTACH) {
        // fixme, move this where threading can't be an issue
        vsscript_init();
    } else if (ulReason == DLL_PROCESS_DETACH) {
        vsscript_finalize();
    }
    return TRUE;
}

// From the Microsoft AVIFile docs.  Dense code...

STDAPI DllGetClassObject(const CLSID& rclsid, const IID& riid, void **ppv) {

    if (rclsid != CLSID_VapourSynth)
        return CLASS_E_CLASSNOTAVAILABLE;
    HRESULT hresult = VapourSynthFile::Create(rclsid, riid, ppv);
    return hresult;
}

STDAPI DllCanUnloadNow() {
    return refCount ? S_FALSE : S_OK;
}


///////////////////////////////////////////////////////////////////////////
//
//    VapourSynthFile
//
///////////////////////////////////////////////////////////////////////////
//////////// IClassFactory

STDMETHODIMP VapourSynthFile::CreateInstance (LPUNKNOWN pUnkOuter, REFIID riid,  void * * ppvObj) {
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;
    HRESULT hresult = Create(CLSID_VapourSynth, riid, ppvObj);
    return hresult;
}

STDMETHODIMP VapourSynthFile::LockServer (BOOL fLock) {
    return S_OK;
}

///////////////////////////////////////////////////
//////////// IPersistFile

STDMETHODIMP VapourSynthFile::GetClassID(LPCLSID lpClassID) {  // IPersist
    if (!lpClassID)
        return E_POINTER;
    *lpClassID = CLSID_VapourSynth;
    return S_OK;
}

STDMETHODIMP VapourSynthFile::IsDirty() {
    return S_FALSE;
}

STDMETHODIMP VapourSynthFile::Load(LPCOLESTR lpszFileName, DWORD grfMode) {
    char filename[MAX_PATH*2];
    WideCharToMultiByte(CP_UTF8, 0, lpszFileName, -1, filename, sizeof(filename), NULL, NULL);
    return Open(filename, grfMode, lpszFileName);
}

STDMETHODIMP VapourSynthFile::Save(LPCOLESTR lpszFileName, BOOL fRemember) {
    return E_FAIL;
}

STDMETHODIMP VapourSynthFile::SaveCompleted(LPCOLESTR lpszFileName) {
    return S_OK;
}

STDMETHODIMP VapourSynthFile::GetCurFile(LPOLESTR *lplpszFileName) {
    if (lplpszFileName)
        *lplpszFileName = NULL;
    return E_FAIL;
}

///////////////////////////////////////////////////
/////// static local

HRESULT VapourSynthFile::Create(const CLSID& rclsid, const IID& riid, void **ppv) {
    HRESULT hresult;
    VapourSynthFile* pAVIFileSynth = new VapourSynthFile(rclsid);
    if (!pAVIFileSynth)
        return E_OUTOFMEMORY;
    hresult = pAVIFileSynth->QueryInterface(riid, ppv);
    pAVIFileSynth->Release();
    return hresult;
}

///////////////////////////////////////////////////
//////////// IUnknown

STDMETHODIMP VapourSynthFile::QueryInterface(const IID& iid, void **ppv) {
    if (!ppv)
        return E_POINTER;

    if (iid == IID_IUnknown) {
        *ppv = (IUnknown *)(IAVIFile *)this;
    } else if (iid == IID_IClassFactory) {
        *ppv = (IClassFactory *)this;
    } else if (iid == IID_IPersist) {
        *ppv = (IPersist *)this;
    } else if (iid == IID_IPersistFile) {
        *ppv = (IPersistFile *)this;
    } else if (iid == IID_IAVIFile) {
        *ppv = (IAVIFile *)this;
    } else if (iid == IID_IAvisynthClipInfo) {
        *ppv = (IAvisynthClipInfo *)this;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    AddRef();

    return S_OK;
}

STDMETHODIMP_(ULONG) VapourSynthFile::AddRef() {
    const int refs = InterlockedIncrement(&m_refs);
    InterlockedIncrement(&refCount);
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthFile::Release() {
    const int refs = InterlockedDecrement(&m_refs);
    InterlockedDecrement(&refCount);
    if (!refs)
        delete this;
    return refs;
}

////////////////////////////////////////////////////////////////////////
//
//        VapourSynthStream
//
////////////////////////////////////////////////////////////////////////
//////////// IUnknown


STDMETHODIMP VapourSynthStream::QueryInterface(const IID& iid, void **ppv) {
    if (!ppv)
        return E_POINTER;

    if (iid == IID_IUnknown) {
        *ppv = (IUnknown *)(IAVIStream *)this;
    } else if (iid == IID_IAVIStream) {
        *ppv = (IAVIStream *)this;
    } else if (iid == IID_IAVIStreaming) {
        *ppv = (IAVIStreaming *)this;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    AddRef();

    return S_OK;
}

STDMETHODIMP_(ULONG) VapourSynthStream::AddRef() {
    const int refs = InterlockedIncrement(&m_refs);
    InterlockedIncrement(&refCount);
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthStream::Release() {
    const int refs = InterlockedDecrement(&m_refs);
    InterlockedDecrement(&refCount);
    if (!refs) delete this;
    return refs;
}

////////////////////////////////////////////////////////////////////////
//
//        VapourSynthFile
//
////////////////////////////////////////////////////////////////////////
//////////// IAVIFile

STDMETHODIMP VapourSynthFile::CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi) {
    *ppStream = NULL;
    return S_OK;
}

STDMETHODIMP VapourSynthFile::EndRecord() {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,
    AVISAVECALLBACK lpfnCallback) {
        return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) {
    return AVIERR_NODATA;
}

STDMETHODIMP VapourSynthFile::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::DeleteStream(DWORD fccType, LONG lParam) {
    return AVIERR_READONLY;
}


///////////////////////////////////////////////////
/////// local

VapourSynthFile::VapourSynthFile(const CLSID& rclsid) : num_threads(1), node(NULL), se(NULL), vsapi(NULL), enable_v210(false), pad_scanlines(false), m_refs(0), vi(NULL), pending_requests(0) {
    vsapi = vsscript_getVSApi();
    AddRef();
}

VapourSynthFile::~VapourSynthFile() {
    Lock();
    if (vi) {
        while (pending_requests > 0) {};
        vi = NULL;
        vsapi->freeNode(node);
        vsscript_freeScript(se);
    }
    Unlock();
}

int VapourSynthFile::ImageSize() {
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

STDMETHODIMP VapourSynthFile::Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName) {
    if (mode & (OF_CREATE|OF_WRITE))
        return E_FAIL;
    szScriptName = szFile;
    return S_OK;
}

bool VapourSynthFile::DelayInit() {
    Lock();
    bool result = DelayInit2();
    Unlock();
    return result;
}

const char *ErrorScript1 = "\
import vapoursynth as vs\n\
import sys\n\
core = vs.get_core()\n\
w = 340\n\
h = 600\n\
red = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[255, 0, 0])\n\
green = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[0, 255, 0])\n\
blue = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[0, 0, 255])\n\
stacked = core.std.StackHorizontal([red, green, blue])\n\
msg = core.text.Text(stacked, r\"\"\"";

const char *ErrorScript2 = "\"\"\")\n\
final = core.resize.Bilinear(msg, format=vs.COMPATBGR32)\n\
final.set_output()\n";

bool VapourSynthFile::DelayInit2() {
    if (!szScriptName.empty() && !vi) {
        if (!vsscript_evaluateFile(&se, szScriptName.c_str(), efSetWorkingDir)) {
            node = vsscript_getOutput(se, 0);
            if (!node)
                goto vpyerror;
            vi = vsapi->getVideoInfo(node);
            error_msg.clear();

            if (vi->width == 0 || vi->height == 0 || vi->format == NULL || vi->numFrames == 0) {
                error_msg = "Cannot open clips with varying dimensions or format in vfw";
                goto vpyerror;
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
                error_msg = "VFW module doesn't support ";
                error_msg += vi->format->name;
                error_msg += " output";
                goto vpyerror;
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

            return true;
        } else {
            error_msg = vsscript_getError(se);
            vpyerror:
            vi = NULL;
            vsscript_freeScript(se);
            se = NULL;
            std::string error_script = ErrorScript1;
            error_script += error_msg;
            error_script += ErrorScript2;
            int res = vsscript_evaluateScript(&se, error_script.c_str(), "vfw_error.bleh", 0);
            const char *et = vsscript_getError(se);
            node = vsscript_getOutput(se, 0);
            vi = vsapi->getVideoInfo(node);
            return true;
        }
    } else {
        return !!vi;
    }
}

void VapourSynthFile::Lock() {
    cs_filter_graph.lock();
}

void VapourSynthFile::Unlock() {
    cs_filter_graph.unlock();
}

///////////////////////////////////////////////////
//////////// IAVIFile

STDMETHODIMP VapourSynthFile::Info(AVIFILEINFOW *pfi, LONG lSize) {
    if (!pfi)
        return E_POINTER;

    if (!DelayInit())
        return E_FAIL;

    AVIFILEINFOW afi;
    memset(&afi, 0, sizeof(afi));

    afi.dwMaxBytesPerSec    = 0;
    afi.dwFlags                = AVIFILEINFO_HASINDEX | AVIFILEINFO_ISINTERLEAVED;
    afi.dwCaps                = AVIFILECAPS_CANREAD | AVIFILECAPS_ALLKEYFRAMES | AVIFILECAPS_NOCOMPRESSION;

    afi.dwStreams                = 1;
    afi.dwSuggestedBufferSize    = 0;
    afi.dwWidth                    = vi->width;
    afi.dwHeight                = vi->height;
    afi.dwEditCount                = 0;

    afi.dwRate                    = int64ToIntS(vi->fpsNum ? vi->fpsNum : 1);
    afi.dwScale                    = int64ToIntS(vi->fpsDen ? vi->fpsDen : 30);
    afi.dwLength                = vi->numFrames;

    wcscpy(afi.szFileType, L"VapourSynth");

    // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(afi)
    memset(pfi, 0, lSize);
    memcpy(pfi, &afi, min(size_t(lSize), sizeof(afi)));
    return S_OK;
}

static inline char BePrintable(int ch) {
    ch &= 0xff;
    return isprint(ch) ? ch : '.';
}


STDMETHODIMP VapourSynthFile::GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam) {
    VapourSynthStream *casr;
    char fcc[5];

    fcc[0] = BePrintable(fccType      );
    fcc[1] = BePrintable(fccType >>  8);
    fcc[2] = BePrintable(fccType >> 16);
    fcc[3] = BePrintable(fccType >> 24);
    fcc[4] = 0;

    if (!DelayInit())
        return E_FAIL;

    *ppStream = NULL;

    if (!fccType) {
        if (lParam==0)
            fccType = streamtypeVIDEO;
    }

    if (lParam > 0)
        return AVIERR_NODATA;

    if (fccType == streamtypeVIDEO) {
        if ((casr = new VapourSynthStream(this, false)) == 0)
            return AVIERR_MEMORY;

        *ppStream = (IAVIStream *)casr;

    } else if (fccType == streamtypeAUDIO) {
        return AVIERR_NODATA;

        if ((casr = new VapourSynthStream(this, true)) == 0)
            return AVIERR_MEMORY;
        *ppStream = (IAVIStream *)casr;
    } else
        return AVIERR_NODATA;

    return S_OK;
}


////////////////////////////////////////////////////////////////////////
//////////// IAvisynthClipInfo

int __stdcall VapourSynthFile::GetError(const char** ppszMessage) {
    if (!DelayInit() && error_msg.empty())
        error_msg = "VapourSynth: script open failed!";

    if (ppszMessage)
        *ppszMessage = error_msg.c_str();
    return !error_msg.empty();
}

bool __stdcall VapourSynthFile::GetParity(int n) {
    if (!DelayInit())
        return false;
    return false;
}

bool __stdcall VapourSynthFile::IsFieldBased() {
    if (!DelayInit())
        return false;
    return false;
}

////////////////////////////////////////////////////////////////////////
//
//        VapourSynthStream
//
////////////////////////////////////////////////////////////////////////
//////////// IAVIStreaming

STDMETHODIMP VapourSynthStream::Begin(LONG lStart, LONG lEnd, LONG lRate) {
    return S_OK;
}

STDMETHODIMP VapourSynthStream::End() {
    return S_OK;
}

//////////// IAVIStream

STDMETHODIMP VapourSynthStream::Create(LPARAM lParam1, LPARAM lParam2) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::Delete(LONG lStart, LONG lSamples) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) {
    return AVIERR_NODATA;
}

STDMETHODIMP VapourSynthStream::SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::SetInfo(AVISTREAMINFOW *psi, LONG lSize) {
    return AVIERR_READONLY;
}

////////////////////////////////////////////////////////////////////////
//////////// local

VapourSynthStream::VapourSynthStream(VapourSynthFile *parentPtr, bool isAudio) {
    sName = "video";
    m_refs = 0;
    AddRef();
    parent = parentPtr;
    parent->AddRef();
}

VapourSynthStream::~VapourSynthStream() {
    if (parent)
        parent->Release();
}

////////////////////////////////////////////////////////////////////////
//////////// IAVIStream

STDMETHODIMP_(LONG) VapourSynthStream::Info(AVISTREAMINFOW *psi, LONG lSize) {
    if (!psi)
        return E_POINTER;

    AVISTREAMINFOW asi;

    const VSVideoInfo* const vi = parent->vi;

    memset(&asi, 0, sizeof(asi));
    asi.fccType = streamtypeVIDEO;
    asi.dwQuality = DWORD(-1);

    const int image_size = parent->ImageSize();
    asi.fccHandler = 'UNKN';
    if (vi->format->id == pfCompatBGR32)
        asi.fccHandler = ' BID';
    else if (vi->format->id == pfCompatYUY2)
        asi.fccHandler = '2YUY';
    else if (vi->format->id == pfYUV420P8)
        asi.fccHandler = '21VY';
    else if (vi->format->id == pfGray8)
        asi.fccHandler = '008Y';
    else if (vi->format->id == pfYUV444P8)
        asi.fccHandler = '42VY';
    else if (vi->format->id == pfYUV422P8)
        asi.fccHandler = '61VY';
    else if (vi->format->id == pfYUV411P8)
        asi.fccHandler = 'B14Y';
    else if (vi->format->id == pfYUV410P8)
        asi.fccHandler = '9UVY';
    else if (vi->format->id == pfYUV420P10)
        asi.fccHandler = '010P';
    else if (vi->format->id == pfYUV420P16)
        asi.fccHandler = '610P';
    else if (vi->format->id == pfYUV422P10 && parent->enable_v210)
        asi.fccHandler = '012v';
    else if (vi->format->id == pfYUV422P10)
        asi.fccHandler = '012P';
    else if (vi->format->id == pfYUV422P16)
        asi.fccHandler = '612P';
    else
        return E_FAIL;

    asi.dwScale = int64ToIntS(vi->fpsDen ? vi->fpsDen : 1);
    asi.dwRate = int64ToIntS(vi->fpsNum ? vi->fpsNum : 30);
    asi.dwLength = vi->numFrames;
    asi.rcFrame.right = vi->width;
    asi.rcFrame.bottom = vi->height;
    asi.dwSampleSize = image_size;
    asi.dwSuggestedBufferSize = image_size;
    wcscpy(asi.szName, L"VapourSynth video #1");

    // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(asi)
    memset(psi, 0, lSize);
    memcpy(psi, &asi, min(size_t(lSize), sizeof(asi)));
    return S_OK;
}

STDMETHODIMP_(LONG) VapourSynthStream::FindSample(LONG lPos, LONG lFlags) {
    if (lFlags & FIND_FORMAT)
        return -1;

    if (lFlags & FIND_FROM_START)
        return 0;

    return lPos;
}


////////////////////////////////////////////////////////////////////////
//////////// local

void VS_CC VapourSynthFile::frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
    VapourSynthFile *vsfile = (VapourSynthFile *)userData;
    vsfile->vsapi->freeFrame(f);
    InterlockedDecrement(&vsfile->pending_requests);
}

bool VapourSynthStream::ReadFrame(void* lpBuffer, int n) {
    const VSAPI *vsapi = parent->vsapi;
    const VSFrameRef *f = vsapi->getFrame(n, parent->node, 0, 0);
    if (!f)
        return false;

    const VSFormat *fi = vsapi->getFrameFormat(f);
    const int pitch    = vsapi->getStride(f, 0);
    const int row_size = vsapi->getFrameWidth(f, 0) * fi->bytesPerSample;
    const int height   = vsapi->getFrameHeight(f, 0);

    int out_pitch;
    int out_pitchUV;

    bool semi_packed_p10 = (fi->id == pfYUV420P10) || (fi->id == pfYUV422P10);
    bool semi_packed_p16 = (fi->id == pfYUV420P16) || (fi->id == pfYUV422P16);

    // BMP scanlines are dword-aligned
    if (fi->numPlanes == 1) {
        out_pitch = (row_size+3) & ~3;
        out_pitchUV = (vsapi->getFrameWidth(f, 1) * fi->bytesPerSample+3) & ~3;
    }
    // Planar scanlines are packed
    else {
        out_pitch = row_size;
        out_pitchUV = vsapi->getFrameWidth(f, 1) * fi->bytesPerSample;
    }

    if (fi->id == pfYUV422P10 && parent->enable_v210) {
        int width = vsapi->getFrameWidth(f, 0);
        int pstride_y = vsapi->getStride(f, 0)/2;
        int pstride_uv = vsapi->getStride(f, 1)/2;
        const uint16_t *yptr = (const uint16_t *)vsapi->getReadPtr(f, 0);
        const uint16_t *uptr = (const uint16_t *)vsapi->getReadPtr(f, 1);
        const uint16_t *vptr = (const uint16_t *)vsapi->getReadPtr(f, 2);
        uint32_t *outbuf = (uint32_t *)lpBuffer;
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
        uint16_t *outbuf = (uint16_t *)lpBuffer;
        const uint16_t *yptr = (const uint16_t *)vsapi->getReadPtr(f, 0);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < pwidth; x++) {
                outbuf[x] = yptr[x] << 6;
            }
            outbuf += out_pitch/2;
            yptr += pstride;
        }
    } else {
        vs_bitblt(lpBuffer, out_pitch, vsapi->getReadPtr(f, 0), pitch, row_size, height);
    }

    if (fi->id == pfYUV422P10 && parent->enable_v210) {
         // intentionally empty
    } else if (semi_packed_p10 || semi_packed_p16) {
        int pheight = vsapi->getFrameHeight(f, 1);
        int pwidth = vsapi->getFrameWidth(f, 1);
        int pstride = vsapi->getStride(f, 1) / 2;
        BYTE *outadj = (BYTE*)lpBuffer + out_pitch*height;
        uint16_t *outbuf = (uint16_t *)outadj;
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
    } else if (fi->numPlanes == 3) {
        vs_bitblt((BYTE *)lpBuffer + (out_pitch*height),
            out_pitchUV,               vsapi->getReadPtr(f, 2),
            vsapi->getStride(f, 2), vsapi->getFrameWidth(f, 2),
            vsapi->getFrameHeight(f, 2) );

        vs_bitblt((BYTE *)lpBuffer + (out_pitch*height + vsapi->getFrameHeight(f, 1)*out_pitchUV),
            out_pitchUV,               vsapi->getReadPtr(f, 1),
            vsapi->getStride(f, 1), vsapi->getFrameWidth(f, 1),
            vsapi->getFrameHeight(f, 1) );
    }

    vsapi->freeFrame(f);

    for (int i = n + 1; i < std::min<int>(n + parent->num_threads, parent->vi->numFrames); i++) {
        InterlockedIncrement(&parent->pending_requests);
        vsapi->getFrameAsync(i, parent->node, VapourSynthFile::frameDoneCallback, (void *)parent);
    }

    return true;
}

////////////////////////////////////////////////////////////////////////
//////////// IAVIStream

STDMETHODIMP VapourSynthStream::Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) {
    parent->Lock();
    HRESULT result = Read2(lStart, lSamples, lpBuffer, cbBuffer, plBytes, plSamples);
    parent->Unlock();
    return result;
}

HRESULT VapourSynthStream::Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) {
    if (lStart >= parent->vi->numFrames) {
        if (plSamples)
            *plSamples = 0;
        if (plBytes)
            *plBytes = 0;
        return S_OK;
    }

    int image_size = parent->ImageSize();
    if (plSamples)
        *plSamples = 1;
    if (plBytes)
        *plBytes = image_size;

    if (!lpBuffer) {
        return S_OK;
    } else if (cbBuffer < image_size) {
        return AVIERR_BUFFERTOOSMALL;
    }

    if (!ReadFrame(lpBuffer, lStart))
        return E_FAIL;
    return S_OK;
}

STDMETHODIMP VapourSynthStream::ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat) {
    if (!lpcbFormat)
        return E_POINTER;

    if (!lpFormat) {
        *lpcbFormat = sizeof(BITMAPINFOHEADER);
        return S_OK;
    }

    memset(lpFormat, 0, *lpcbFormat);

    const VSVideoInfo* const vi = parent->vi;

    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = vi->width;
    bi.biHeight = vi->height;
    bi.biPlanes = 1;
    bi.biBitCount = vi->format->bytesPerSample * 8;
    if (vi->format->numPlanes == 3)
        bi.biBitCount +=  (bi.biBitCount * 2) >> (vi->format->subSamplingH + vi->format->subSamplingW);
    if (parent->enable_v210 && vi->format->id == pfYUV422P10)
        bi.biBitCount = 20;
    if (vi->format->id == pfCompatBGR32)
        bi.biCompression = BI_RGB;
    else if (vi->format->id == pfCompatYUY2)
        bi.biCompression = '2YUY';
    else if (vi->format->id == pfYUV420P8)
        bi.biCompression = '21VY';
    else if (vi->format->id == pfGray8)
        bi.biCompression = '008Y';
    else if (vi->format->id == pfYUV444P8)
        bi.biCompression = '42VY';
    else if (vi->format->id == pfYUV422P8)
        bi.biCompression = '61VY';
    else if (vi->format->id == pfYUV411P8)
        bi.biCompression = 'B14Y';
    else if (vi->format->id == pfYUV410P8)
        bi.biCompression = '9UVY';
    else if (vi->format->id == pfYUV420P10)
        bi.biCompression = '010P';
    else if (vi->format->id == pfYUV420P16)
        bi.biCompression = '610P';
    else if (vi->format->id == pfYUV422P10 && parent->enable_v210)
        bi.biCompression = '012v';
    else if (vi->format->id == pfYUV422P10)
        bi.biCompression = '012P';
    else if (vi->format->id == pfYUV422P16)
        bi.biCompression = '612P';
    else
        return E_FAIL;

    bi.biSizeImage = parent->ImageSize();
    *lpcbFormat = min(*lpcbFormat, sizeof(bi));
    memcpy(lpFormat, &bi, size_t(*lpcbFormat));

    return S_OK;
}

STDMETHODIMP VapourSynthStream::Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
    LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten,
    LONG FAR *plBytesWritten) {
        return AVIERR_READONLY;
}

