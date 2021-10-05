/*
* Copyright (c) 2012-2020 Fredrik Mellbin
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
#include <windows.h>
#include <vfw.h>
#include <aviriff.h>
#include <string>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>

#include "VSScript4.h"
#include "VSHelper4.h"
#include "../common/fourcc.h"
#include "../common/wave.h"
#include "../common/vsutf16.h"

using namespace vsh;

static std::atomic<long> refCount(0);

static const GUID CLSID_VapourSynth
    = { 0x58f74ca0, 0xbd0e, 0x4664, { 0xa4, 0x9b, 0x8d, 0x10, 0xe6, 0xf0, 0xc1, 0x31 } };

static const GUID IID_IAvisynthClipInfo
    = { 0xe6d6b708, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78} };

struct IAvisynthClipInfo : IUnknown {
    virtual int __stdcall GetError(const char** ppszMessage) = 0;
    virtual bool __stdcall GetParity(int n) = 0;
    virtual bool __stdcall IsFieldBased() = 0;
};

class VapourSynthFile final : public IAVIFile, public IPersistFile, public IClassFactory, public IAvisynthClipInfo {
    friend class VapourSynthStream;
private:
    int num_threads = 1;
    const VSAPI *vsapi = nullptr;
    const VSSCRIPTAPI *vssapi = nullptr;
    VSScript *se = nullptr;
    int alt_output = 0;
    VSNode *videoNode = nullptr;
    VSNode *audioNode = nullptr;
    std::atomic<long> m_refs;
    std::string szScriptName;
    const VSVideoInfo *vi = nullptr;
    const VSAudioInfo* ai = nullptr;
    std::string error_msg;
    std::atomic<long> pending_requests;

    std::mutex cs_filter_graph;

    bool DelayInit();
    bool DelayInit2();

    void Lock();
    void Unlock();
public:

    VapourSynthFile(const CLSID& rclsid);
    ~VapourSynthFile();

    static HRESULT Create(const CLSID& rclsid, const IID& riid, void **ppv);
    static void VS_CC frameDoneCallback(void *userData, const VSFrame *f, int n, VSNode *, const char *errorMsg);

    //////////// IUnknown

    STDMETHODIMP QueryInterface(const IID& iid, void **ppv) noexcept;
    STDMETHODIMP_(ULONG) AddRef() noexcept;
    STDMETHODIMP_(ULONG) Release() noexcept;

    //////////// IClassFactory

    STDMETHODIMP CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void * * ppvObj);
    STDMETHODIMP LockServer(BOOL fLock);

    //////////// IPersistFile

    STDMETHODIMP GetClassID(LPCLSID lpClassID);  // IPersist

    STDMETHODIMP IsDirty();
    STDMETHODIMP Load(LPCOLESTR lpszFileName, DWORD grfMode);
    STDMETHODIMP Save(LPCOLESTR lpszFileName, BOOL fRemember);
    STDMETHODIMP SaveCompleted(LPCOLESTR lpszFileName);
    STDMETHODIMP GetCurFile(LPOLESTR *lplpszFileName);

    //////////// IAVIFile

    STDMETHODIMP CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi) noexcept;
    STDMETHODIMP EndRecord() noexcept;
    STDMETHODIMP GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam) noexcept;
    STDMETHODIMP Info(AVIFILEINFOW *psi, LONG lSize) noexcept;

    STDMETHODIMP Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName);
    STDMETHODIMP Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,
        AVISAVECALLBACK lpfnCallback);

    STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) noexcept;
    STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) noexcept;
    STDMETHODIMP DeleteStream(DWORD fccType, LONG lParam) noexcept;

    //////////// IAvisynthClipInfo

    int __stdcall GetError(const char** ppszMessage);
    bool __stdcall GetParity(int n);
    bool __stdcall IsFieldBased();
};

///////////////////////////////////

class VapourSynthStream final : public IAVIStream, public IAVIStreaming {
public:

    //////////// IUnknown

    STDMETHODIMP QueryInterface(const IID& iid, void **ppv) noexcept;
    STDMETHODIMP_(ULONG) AddRef() noexcept;
    STDMETHODIMP_(ULONG) Release() noexcept;

    VapourSynthStream(VapourSynthFile *parentPtr, bool isAudio);
    ~VapourSynthStream();

    //////////// IAVIStream

    STDMETHODIMP Create(LPARAM lParam1, LPARAM lParam2) noexcept;
    STDMETHODIMP Delete(LONG lStart, LONG lSamples) noexcept;
    STDMETHODIMP Info(AVISTREAMINFOW *psi, LONG lSize) noexcept;
    STDMETHODIMP_(LONG) FindSample(LONG lPos, LONG lFlags) noexcept;
    STDMETHODIMP Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) noexcept;
    STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) noexcept;
    STDMETHODIMP ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat) noexcept;
    STDMETHODIMP SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat) noexcept;
    STDMETHODIMP Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
        LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten,
        LONG FAR *plBytesWritten) noexcept;
    STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) noexcept;
    STDMETHODIMP SetInfo(AVISTREAMINFOW *psi, LONG lSize) noexcept;

    //////////// IAVIStreaming

    STDMETHODIMP Begin(LONG lStart, LONG lEnd, LONG lRate) noexcept;
    STDMETHODIMP End() noexcept;

private:
    std::atomic<long> m_refs;

    VapourSynthFile *parent;
    std::string sName;
    bool fAudio = false;

    //////////// internal

    bool ReadFrame(void* lpBuffer, int n);

    HRESULT Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples);
};

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

STDMETHODIMP VapourSynthFile::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void * * ppvObj) {
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;
    HRESULT hresult = Create(CLSID_VapourSynth, riid, ppvObj);
    return hresult;
}

STDMETHODIMP VapourSynthFile::LockServer(BOOL fLock) {
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
    return Open(utf16_to_utf8(lpszFileName).c_str(), grfMode, lpszFileName);
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

STDMETHODIMP VapourSynthFile::QueryInterface(const IID& iid, void **ppv) noexcept {
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

STDMETHODIMP_(ULONG) VapourSynthFile::AddRef() noexcept {
    const int refs = ++m_refs;
    ++refCount;
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthFile::Release() noexcept {
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


STDMETHODIMP VapourSynthStream::QueryInterface(const IID& iid, void **ppv) noexcept {
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

STDMETHODIMP_(ULONG) VapourSynthStream::AddRef() noexcept {
    const int refs = ++m_refs;
    ++refCount;
    return refs;
}

STDMETHODIMP_(ULONG) VapourSynthStream::Release() noexcept {
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

STDMETHODIMP VapourSynthFile::CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi) noexcept {
    *ppStream = nullptr;
    return S_OK;
}

STDMETHODIMP VapourSynthFile::EndRecord() noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,
    AVISAVECALLBACK lpfnCallback) {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) noexcept {
    return AVIERR_NODATA;
}

STDMETHODIMP VapourSynthFile::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthFile::DeleteStream(DWORD fccType, LONG lParam) noexcept {
    return AVIERR_READONLY;
}


///////////////////////////////////////////////////
/////// local

VapourSynthFile::VapourSynthFile(const CLSID& rclsid) : m_refs(0), pending_requests(0) {
    vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    assert(vssapi);
    vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
    AddRef();
}

VapourSynthFile::~VapourSynthFile() {
    Lock();
    if (vi) {
        while (pending_requests > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
        vsapi->freeNode(videoNode);
        videoNode = nullptr;
    }
    if (ai) {
        vsapi->freeNode(audioNode);
        audioNode = nullptr;
    }
    if (vi || ai) {
        vi = nullptr;
        ai = nullptr;
        vssapi->freeScript(se);
        se = nullptr;
    }
    Unlock();
}

STDMETHODIMP VapourSynthFile::Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName) {
    if (mode & (OF_CREATE | OF_WRITE))
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

static const char *ErrorScript1 = "\
import vapoursynth as vs\n\
import sys\n\
core = vs.core\n\
w = 340\n\
h = 600\n\
red = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[255, 0, 0])\n\
green = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[0, 255, 0])\n\
blue = core.std.BlankClip(width=w, height=h, format=vs.RGB24, color=[0, 0, 255])\n\
stacked = core.std.StackHorizontal([red, green, blue])\n\
msg = core.text.Text(stacked, r\"\"\"";

static const char *ErrorScript2 = "\"\"\")\n\
msg.set_output()\n";

bool VapourSynthFile::DelayInit2() {
    if (!szScriptName.empty() && !vi) {
        se = vssapi->createScript(nullptr);
        vssapi->evalSetWorkingDir(se, 1);
        vssapi->evaluateFile(se, szScriptName.c_str());

        if (!vssapi->getError(se)) {
            error_msg.clear();

            VSCoreInfo info;
            vsapi->getCoreInfo(vssapi->getCore(se), &info);
            num_threads = info.numThreads;

            ////////// video

            videoNode = vssapi->getOutputNode(se, 0);
            if (!videoNode) {
                error_msg = "Couldn't get output clip, no output set?";
                goto vpyerror;
            }

            if (vsapi->getNodeType(videoNode) != mtVideo) {
                error_msg = "Output index 0 is not video";
                goto vpyerror;
            }

            vi = vsapi->getVideoInfo(videoNode);

            if (!isConstantVideoFormat(vi)) {
                error_msg = "Cannot open clips with varying dimensions or format in vfw";
                goto vpyerror;
            }

            if (!HasSupportedFourCC(vi->format)) {
                error_msg = "VFW module doesn't support ";
                char nameBuffer[32];
                vsapi->getVideoFormatName(&vi->format, nameBuffer);
                error_msg += nameBuffer;
                error_msg += " output";
                goto vpyerror;
            }

            vsapi->setCacheMode(videoNode, 1);
            vsapi->setCacheOptions(videoNode, -1, num_threads * 2, -1);

            alt_output = vssapi->getAltOutputMode(se, 0);

            ////////// audio

            audioNode = vssapi->getOutputNode(se, 1);

            if (audioNode) {
                if (vsapi->getNodeType(audioNode) != mtAudio) {
                    error_msg = "Output index 1 is not audio";
                    goto vpyerror;
                }

                ai = vsapi->getAudioInfo(audioNode);

                if (ai->numSamples > std::numeric_limits<DWORD>::max()) {
                    error_msg = "Audio has more samples than can be represented in VFW structures";
                    goto vpyerror;
                }

                vsapi->setCacheMode(audioNode, 1);
                vsapi->setCacheOptions(audioNode, -1, num_threads * 2, -1);
            }

            return true;
        } else {
            error_msg = vssapi->getError(se);
        vpyerror:
            vsapi->freeNode(videoNode);
            vsapi->freeNode(audioNode);
            videoNode = nullptr;
            audioNode = nullptr;
            vi = nullptr;
            ai = nullptr;
            vssapi->freeScript(se);
            se = nullptr;
            std::string error_script = ErrorScript1;
            error_script += error_msg;
            error_script += ErrorScript2;
            se = vssapi->createScript(nullptr);
            vssapi->evaluateBuffer(se, error_script.c_str(), "vfw_error.message");
            videoNode = vssapi->getOutputNode(se, 0);
            vi = vsapi->getVideoInfo(videoNode);
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

STDMETHODIMP VapourSynthFile::Info(AVIFILEINFOW *pfi, LONG lSize) noexcept {
    if (!pfi)
        return E_POINTER;

    if (!DelayInit())
        return E_FAIL;

    AVIFILEINFOW afi = {};

    afi.dwMaxBytesPerSec = 0;
    afi.dwFlags = AVIFILEINFO_HASINDEX | AVIFILEINFO_ISINTERLEAVED;
    afi.dwCaps = AVIFILECAPS_CANREAD | AVIFILECAPS_ALLKEYFRAMES | AVIFILECAPS_NOCOMPRESSION;

    afi.dwStreams = 1;
    if (audioNode)
        afi.dwStreams++;
    afi.dwSuggestedBufferSize = 0;
    afi.dwWidth = vi->width;
    afi.dwHeight = vi->height;
    afi.dwEditCount = 0;

    afi.dwRate = int64ToIntS(vi->fpsNum ? vi->fpsNum : 1);
    afi.dwScale = int64ToIntS(vi->fpsDen ? vi->fpsDen : 30);
    afi.dwLength = vi->numFrames;

    wcscpy(afi.szFileType, L"VapourSynth");

    // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(afi)
    memset(pfi, 0, lSize);
    memcpy(pfi, &afi, std::min(static_cast<size_t>(lSize), sizeof(afi)));
    return S_OK;
}

STDMETHODIMP VapourSynthFile::GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam) noexcept {
    VapourSynthStream *casr;

    if (!DelayInit())
        return E_FAIL;

    *ppStream = nullptr;

    if (!fccType) {
        if (lParam == 0 && videoNode) {
            fccType = streamtypeVIDEO;
        } else {
            if ((lParam == 1 && videoNode) || (lParam == 0 && audioNode)) {
                lParam = 0;
                fccType = streamtypeAUDIO;
            }
        }
    }

    if (lParam > 0)
        return AVIERR_NODATA;

    if (fccType == streamtypeVIDEO) {
        if ((casr = new(std::nothrow)VapourSynthStream(this, false)) == 0)
            return AVIERR_MEMORY;
        *ppStream = (IAVIStream *)casr;

    } else if (fccType == streamtypeAUDIO && ai) {
        if ((casr = new(std::nothrow)VapourSynthStream(this, true)) == 0)
            return AVIERR_MEMORY;
        *ppStream = (IAVIStream *)casr;
    } else {
        return AVIERR_NODATA;
    }

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

STDMETHODIMP VapourSynthStream::Begin(LONG lStart, LONG lEnd, LONG lRate) noexcept {
    return S_OK;
}

STDMETHODIMP VapourSynthStream::End() noexcept {
    return S_OK;
}

//////////// IAVIStream

STDMETHODIMP VapourSynthStream::Create(LPARAM lParam1, LPARAM lParam2) noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::Delete(LONG lStart, LONG lSamples) noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) noexcept {
    return AVIERR_NODATA;
}

STDMETHODIMP VapourSynthStream::SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat) noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) noexcept {
    return AVIERR_READONLY;
}

STDMETHODIMP VapourSynthStream::SetInfo(AVISTREAMINFOW *psi, LONG lSize) noexcept {
    return AVIERR_READONLY;
}

////////////////////////////////////////////////////////////////////////
//////////// local

VapourSynthStream::VapourSynthStream(VapourSynthFile *parentPtr, bool isAudio) : m_refs(0), sName(isAudio ? "audio" : "video"), fAudio(isAudio) {
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

STDMETHODIMP VapourSynthStream::Info(AVISTREAMINFOW *psi, LONG lSize) noexcept {
    if (!psi)
        return E_POINTER;

    AVISTREAMINFOW asi = {};
    asi.dwQuality = DWORD(-1);

    if (fAudio) {
        const VSAudioInfo* const ai = parent->ai;
        size_t bytesPerOutputSample = (ai->format.bitsPerSample + 7) / 8;
        asi.fccType = streamtypeAUDIO;
        asi.dwScale = static_cast<DWORD>(bytesPerOutputSample);
        asi.dwRate = static_cast<DWORD>(ai->sampleRate * bytesPerOutputSample);
        asi.dwLength = static_cast<DWORD>(ai->numSamples);
        asi.dwSampleSize = static_cast<DWORD>(bytesPerOutputSample);
        wcscpy(asi.szName, L"VapourSynth Audio #1");
    } else {
        const VSVideoInfo* const vi = parent->vi;
        int image_size = BMPSize(vi, parent->alt_output);

        if (!GetFourCC(vi->format, parent->alt_output, asi.fccHandler))
            return E_FAIL;

        asi.fccType = streamtypeVIDEO;
        asi.dwScale = int64ToIntS(vi->fpsDen ? vi->fpsDen : 1);
        asi.dwRate = int64ToIntS(vi->fpsNum ? vi->fpsNum : 30);
        asi.dwLength = vi->numFrames;
        asi.rcFrame.right = vi->width;
        asi.rcFrame.bottom = vi->height;
        asi.dwSampleSize = image_size;
        asi.dwSuggestedBufferSize = image_size;
        wcscpy(asi.szName, L"VapourSynth Video #1");
    }

    // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(asi)
    memset(psi, 0, lSize);
    memcpy(psi, &asi, std::min(static_cast<size_t>(lSize), sizeof(asi)));
    return S_OK;
}

STDMETHODIMP_(LONG) VapourSynthStream::FindSample(LONG lPos, LONG lFlags) noexcept {
    if (lFlags & FIND_FORMAT)
        return -1;

    if (lFlags & FIND_FROM_START)
        return 0;

    return lPos;
}


////////////////////////////////////////////////////////////////////////
//////////// local

void VS_CC VapourSynthFile::frameDoneCallback(void *userData, const VSFrame *f, int n, VSNode *, const char *errorMsg) {
    VapourSynthFile *vsfile = static_cast<VapourSynthFile *>(userData);
    vsfile->vsapi->freeFrame(f);
    --vsfile->pending_requests;
}

bool VapourSynthStream::ReadFrame(void* lpBuffer, int n) {
    const VSAPI *vsapi = parent->vsapi;
    const VSSCRIPTAPI *vssapi = parent->vssapi;
    std::vector<char> errMsg(32 * 1024);
    const VSFrame *f = vsapi->getFrame(n, parent->videoNode, errMsg.data(), static_cast<int>(errMsg.size()));
    VSScript *errSe = nullptr;
    if (!f) {
        std::string matrix;
        if (parent->vi->format.colorFamily == cfYUV || parent->vi->format.colorFamily == cfGray)
            matrix = ", matrix_s=\"709\"";

        char nameBuffer[32];
        vsapi->getVideoFormatName(&parent->vi->format, nameBuffer);

        std::string frameErrorScript = "import vapoursynth as vs\nimport sys\ncore = vs.core\n";
        frameErrorScript += "err_script_formatid = " + std::string(nameBuffer) + "\n";
        frameErrorScript += "err_script_width = " + std::to_string(parent->vi->width) + "\n";
        frameErrorScript += "err_script_height = " + std::to_string(parent->vi->height) + "\n";
        frameErrorScript += "err_script_background = core.std.BlankClip(width=err_script_width, height=err_script_height, format=vs.RGB24)\n";
        frameErrorScript += "err_script_clip = core.text.Text(err_script_background, r\"\"\"";
        frameErrorScript += errMsg.data();
        frameErrorScript += "\"\"\")\n";
        frameErrorScript += "err_script_clip = core.resize.Bilinear(err_script_clip, format=err_script_formatid" + matrix + ")\n";
        frameErrorScript += "err_script_clip.set_output()\n";

        errSe = vssapi->createScript(nullptr);
        vssapi->evaluateBuffer(errSe, frameErrorScript.c_str(), "vfw_error.message");
        VSNode *node = vssapi->getOutputNode(errSe, 0);
        f = vsapi->getFrame(0, node, nullptr, 0);
        vsapi->freeNode(node);

        if (!f) {
            vssapi->freeScript(errSe);
            return false;
        }
    }

    const uint8_t *src[3] = {};
    ptrdiff_t src_stride[3] = {};

    for (int plane = 0; plane < parent->vi->format.numPlanes; plane++) {
        src[plane] = vsapi->getReadPtr(f, plane);
        src_stride[plane] = vsapi->getStride(f, plane);
    }

    PackOutputFrame(src, src_stride, reinterpret_cast<uint8_t *>(lpBuffer), vsapi->getFrameWidth(f, 0), vsapi->getFrameHeight(f, 0), parent->vi->format, parent->alt_output);

    vsapi->freeFrame(f);
    vssapi->freeScript(errSe);

    if (!errSe) {
        for (int i = n + 1; i < std::min<int>(n + parent->num_threads, parent->vi->numFrames); i++) {
            ++parent->pending_requests;
            vsapi->getFrameAsync(i, parent->videoNode, VapourSynthFile::frameDoneCallback, static_cast<void *>(parent));
        }
    }

    return !errSe;
}

////////////////////////////////////////////////////////////////////////
//////////// IAVIStream

STDMETHODIMP VapourSynthStream::Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) noexcept {
    parent->Lock();
    HRESULT result = Read2(lStart, lSamples, lpBuffer, cbBuffer, plBytes, plSamples);
    parent->Unlock();
    return result;
}

HRESULT VapourSynthStream::Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) {
    const VSVideoInfo *vi = parent->vi;
    if (fAudio) {
        const VSAudioInfo *ai = parent->ai;
        if (lSamples == AVISTREAMREAD_CONVENIENT)
            lSamples = ai->sampleRate;

        if (static_cast<int64_t>(lStart) + lSamples > ai->numSamples)
            lSamples = std::max<long>(static_cast<long>(ai->numSamples - lStart), 0);

        size_t bytesPerOutputSample = (ai->format.bitsPerSample + 7) / 8;

        LONG bytes = static_cast<LONG>(lSamples * bytesPerOutputSample * ai->format.numChannels);
        if (lpBuffer && bytes > cbBuffer) {
            lSamples = static_cast<LONG>(cbBuffer / (bytesPerOutputSample * ai->format.numChannels));
            bytes = static_cast<LONG>(lSamples * bytesPerOutputSample * ai->format.numChannels);
        }
        if (plBytes)
            *plBytes = bytes;
        if (plSamples)
            *plSamples = lSamples;
        if (!lpBuffer || !lSamples)
            return S_OK;

        const VSAudioFormat &af = ai->format;

        int startFrame = lStart / VS_AUDIO_FRAME_SAMPLES;
        int endFrame = (lStart + lSamples - 1) / VS_AUDIO_FRAME_SAMPLES;

        std::vector<const uint8_t *> tmp;
        tmp.resize(ai->format.numChannels);

        const VSAPI *vsapi = parent->vsapi;
        size_t dstPos = 0;

        for (int i = startFrame; i <= endFrame; i++) {
            const VSFrame *f = vsapi->getFrame(i, parent->audioNode, nullptr, 0);
            int64_t firstFrameSample = i * static_cast<int64_t>(VS_AUDIO_FRAME_SAMPLES);
            size_t offset = 0;
            size_t copyLength = VS_AUDIO_FRAME_SAMPLES;
            if (firstFrameSample < lStart) {
                offset = (lStart - firstFrameSample) * bytesPerOutputSample;
                copyLength -= (lStart - firstFrameSample);
            }
            if (lSamples < copyLength)
                copyLength = lSamples;

            for (int c = 0; c < ai->format.numChannels; c++)
                tmp[c] = vsapi->getReadPtr(f, c) + offset;

            if (bytesPerOutputSample == 2)
                PackChannels16to16le(tmp.data(), reinterpret_cast<uint8_t *>(lpBuffer) + dstPos, copyLength, af.numChannels);
            else if (bytesPerOutputSample == 3)
                PackChannels32to24le(tmp.data(), reinterpret_cast<uint8_t *>(lpBuffer) + dstPos, copyLength, af.numChannels);
            else if (bytesPerOutputSample == 4)
                PackChannels32to32le(tmp.data(), reinterpret_cast<uint8_t *>(lpBuffer) + dstPos, copyLength, af.numChannels);

            dstPos += copyLength * af.numChannels * bytesPerOutputSample;

            vsapi->freeFrame(f);
        }

        return S_OK;
    } else {
        if (lStart >= vi->numFrames) {
            if (plSamples)
                *plSamples = 0;
            if (plBytes)
                *plBytes = 0;
            return S_OK;
        }

        int image_size = BMPSize(vi, parent->alt_output);
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
}

STDMETHODIMP VapourSynthStream::ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat) noexcept {
    if (!lpcbFormat)
        return E_POINTER;

    if (!lpFormat) {
        *lpcbFormat = fAudio ? sizeof(WaveFormatExtensible)  : sizeof(BITMAPINFOHEADER);
        return S_OK;
    }

    memset(lpFormat, 0, *lpcbFormat);

    if (fAudio) {
        const VSAudioInfo *const ai = parent->ai;
        WaveFormatExtensible wfxt;
        if (!CreateWaveFormatExtensible(wfxt, ai->format.sampleType == stFloat, ai->format.bitsPerSample, ai->sampleRate, ai->format.channelLayout))
            return E_FAIL;
        *lpcbFormat = std::min<LONG>(*lpcbFormat, sizeof(wfxt));
        memcpy(lpFormat, &wfxt, size_t(*lpcbFormat));
    } else {
        const VSVideoInfo *const vi = parent->vi;
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(bi);
        bi.biWidth = vi->width;
        bi.biHeight = vi->height;
        bi.biPlanes = 1;
        bi.biBitCount = BitsPerPixel(vi->format, parent->alt_output);
        if (!GetBiCompression(vi->format, parent->alt_output, bi.biCompression))
            return E_FAIL;

        bi.biSizeImage = BMPSize(vi, parent->alt_output);
        *lpcbFormat = std::min<LONG>(*lpcbFormat, sizeof(bi));
        memcpy(lpFormat, &bi, static_cast<size_t>(*lpcbFormat));
    }

    return S_OK;
}

STDMETHODIMP VapourSynthStream::Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
    LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten,
    LONG FAR *plBytesWritten) noexcept {
    return AVIERR_READONLY;
}

