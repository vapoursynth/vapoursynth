/*
* Copyright (c) 2012-2014 Fredrik Mellbin
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
#define NOMINMAX
#include <windows.h>
#include <vfw.h>
#include <string>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <codecvt>

#include "VSScript.h"
#include "VSHelper.h"
#include "taffy.h"

static std::atomic<long> refCount(0);

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
    VSNodeRef *node;
    std::atomic<long> m_refs;
    std::string szScriptName;
    const VSVideoInfo* vi;
    std::string error_msg;
    std::atomic<long> pending_requests;

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
    std::atomic<long> m_refs;

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
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return Open(conversion.to_bytes(lpszFileName).c_str(), grfMode, lpszFileName);
}

STDMETHODIMP VapourSynthFile::Save(LPCOLESTR lpszFileName, BOOL fRemember) {
    return E_FAIL;
}

STDMETHODIMP VapourSynthFile::SaveCompleted(LPCOLESTR lpszFileName) {
    return S_OK;
}

STDMETHODIMP VapourSynthFile::GetCurFile(LPOLESTR *lplpszFileName) {
    if (lplpszFileName)
        *lplpszFileName = nullptr;
    return E_FAIL;
}

///////////////////////////////////////////////////
/////// static local

HRESULT VapourSynthFile::Create(const CLSID& rclsid, const IID& riid, void **ppv) {
    HRESULT hresult;
    VapourSynthFile* pAVIFileSynth = new(std::nothrow)VapourSynthFile(rclsid);
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
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();

    return S_OK;
}

STDMETHODIMP_(ULONG) VapourSynthFile::AddRef() {
    const int refs = ++m_refs;
    ++refCount;
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthFile::Release() {
    const int refs = --m_refs;
    --refCount;
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
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();

    return S_OK;
}

STDMETHODIMP_(ULONG) VapourSynthStream::AddRef() {
    const int refs = ++m_refs;
    ++refCount;
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthStream::Release() {
    const int refs = --m_refs;
    --refCount;
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
    *ppStream = nullptr;
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

VapourSynthFile::VapourSynthFile(const CLSID& rclsid) : num_threads(1), node(nullptr), se(nullptr), vsapi(nullptr), enable_v210(false), m_refs(0), vi(nullptr), pending_requests(0) {
    vsapi = vsscript_getVSApi();
    AddRef();
}

VapourSynthFile::~VapourSynthFile() {
    Lock();
    if (vi) {
        while (pending_requests > 0) {};
        vi = nullptr;
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
    } else if (vi->format->numPlanes == 1) {
        image_size = BMPSize(vi->height, vi->width * vi->format->bytesPerSample);
    } else {
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
            if (!node) {
                error_msg = "Couldn't get output clip, no output set?";
                goto vpyerror;
            }
            vi = vsapi->getVideoInfo(node);
            error_msg.clear();

            if (vi->width == 0 || vi->height == 0 || vi->format == nullptr || vi->numFrames == 0) {
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
            VSMap *options = vsapi->createMap();
            vsscript_getVariable(se, "enable_v210", options);
            enable_v210 = !!vsapi->propGetInt(options, "enable_v210", 0, &error);
            if (error)
                enable_v210 = false;
            vsapi->freeMap(options);

            const VSCoreInfo *info = vsapi->getCoreInfo(vsscript_getCore(se));
            num_threads = info->numThreads;

            return true;
        } else {
            error_msg = vsscript_getError(se);
            vpyerror:
            vi = nullptr;
            vsscript_freeScript(se);
            se = nullptr;
            std::string error_script = ErrorScript1;
            error_script += error_msg;
            error_script += ErrorScript2;
            int res = vsscript_evaluateScript(&se, error_script.c_str(), "vfw_error.bleh", 0);
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
    memcpy(pfi, &afi, std::min(static_cast<size_t>(lSize), sizeof(afi)));
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

    *ppStream = nullptr;

    if (!fccType) {
        if (lParam==0)
            fccType = streamtypeVIDEO;
    }

    if (lParam > 0)
        return AVIERR_NODATA;

    if (fccType == streamtypeVIDEO) {
        if ((casr = new(std::nothrow)VapourSynthStream(this, false)) == 0)
            return AVIERR_MEMORY;

        *ppStream = (IAVIStream *)casr;

    } else if (fccType == streamtypeAUDIO) {
        return AVIERR_NODATA;

        if ((casr = new(std::nothrow)VapourSynthStream(this, true)) == 0)
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

VapourSynthStream::VapourSynthStream(VapourSynthFile *parentPtr, bool isAudio) : sName("video"), m_refs(0) {
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
    memcpy(psi, &asi, std::min(static_cast<size_t>(lSize), sizeof(asi)));
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
    VapourSynthFile *vsfile = static_cast<VapourSynthFile *>(userData);
    vsfile->vsapi->freeFrame(f);
    --vsfile->pending_requests;
}

bool VapourSynthStream::ReadFrame(void* lpBuffer, int n) {
    const VSAPI *vsapi = parent->vsapi;
    const VSFrameRef *f = vsapi->getFrame(n, parent->node, nullptr, 0);
    if (!f)
        return false;

    const VSFormat *fi = vsapi->getFrameFormat(f);
    
    if (fi->id == pfYUV422P10 && parent->enable_v210) {
        taffy_param p = {};
        for (int plane = 0; plane < 3; plane++) {
            p.srcp[plane] = vsapi->getReadPtr(f, plane);
            p.src_stride[plane] = vsapi->getStride(f, plane);
            p.width[plane] = vsapi->getFrameWidth(f, plane);
            p.height[plane] = vsapi->getFrameHeight(f, plane);
        }
        p.dst_stride[0] = taffy_get_v210_stride(p.width[0]);
        p.dstp[0] = lpBuffer;
        taffy_pack_v210(&p);
    } else if ((fi->id == pfYUV420P16) || (fi->id == pfYUV422P16) || (fi->id == pfYUV420P10) || (fi->id == pfYUV422P10)) {
        taffy_param p = { 0 };
        for (int plane = 0; plane < 3; plane++) {
            p.srcp[plane] = vsapi->getReadPtr(f, plane);
            p.src_stride[plane] = vsapi->getStride(f, plane);
            p.width[plane] = vsapi->getFrameWidth(f, plane);
            p.height[plane] = vsapi->getFrameHeight(f, plane);
        }
        p.dst_stride[0] = p.width[0] * fi->bytesPerSample;
        p.dst_stride[1] = p.width[1] * 2 * fi->bytesPerSample;
        p.dstp[0] = lpBuffer;
        p.dstp[1] = (BYTE *)lpBuffer + p.dst_stride[0] * p.height[0];
        if ((fi->id == pfYUV420P10) || (fi->id == pfYUV422P10))
            taffy_pack_px10(&p);
        else
            taffy_pack_px16(&p);
    } else {
        const int stride = vsapi->getStride(f, 0);
        const int height = vsapi->getFrameHeight(f, 0);
        int row_size = vsapi->getFrameWidth(f, 0) * fi->bytesPerSample;
        if (fi->numPlanes == 1) {
            vs_bitblt(lpBuffer, (row_size + 3) & ~3, vsapi->getReadPtr(f, 0), stride, row_size, height);
        } else if (fi->numPlanes == 3) {
            int row_size23 = vsapi->getFrameWidth(f, 1) * fi->bytesPerSample;

            vs_bitblt(lpBuffer, row_size, vsapi->getReadPtr(f, 0), stride, row_size, height);

            vs_bitblt((BYTE *)lpBuffer + (row_size*height),
                row_size23, vsapi->getReadPtr(f, 2),
                vsapi->getStride(f, 2), vsapi->getFrameWidth(f, 2),
                vsapi->getFrameHeight(f, 2));

            vs_bitblt((BYTE *)lpBuffer + (row_size*height + vsapi->getFrameHeight(f, 1)*row_size23),
                row_size23, vsapi->getReadPtr(f, 1),
                vsapi->getStride(f, 1), vsapi->getFrameWidth(f, 1),
                vsapi->getFrameHeight(f, 1));
        }
    }

    vsapi->freeFrame(f);

    for (int i = n + 1; i < std::min<int>(n + parent->num_threads, parent->vi->numFrames); i++) {
        ++parent->pending_requests;
        vsapi->getFrameAsync(i, parent->node, VapourSynthFile::frameDoneCallback, static_cast<void *>(parent));
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
    *lpcbFormat = std::min<LONG>(*lpcbFormat, sizeof(bi));
    memcpy(lpFormat, &bi, static_cast<size_t>(*lpcbFormat));

    return S_OK;
}

STDMETHODIMP VapourSynthStream::Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
    LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten,
    LONG FAR *plBytesWritten) {
        return AVIERR_READONLY;
}

