//    VirtualDub - Video processing and capture application
//    Copyright (C) 1998-2007 Avery Lee
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include <vfw.h>
#include <vd2/system/debug.h>
#include <vd2/system/log.h>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <vd2/Dita/resources.h>
#include "AVIReadHandler.h"
#include "Avisynth.h"

///////////////////////////////////////////////////////////////////////////////

namespace {
    enum { kVDST_AVIReadHandler = 2 };

    enum {
        kVDM_AvisynthDetected,        // AVI: Avisynth detected. Extended error handling enabled.
        kVDM_OpenDMLIndexDetected,    // AVI: OpenDML hierarchical index detected on stream %d.
        kVDM_IndexMissing,            // AVI: Index not found or damaged -- reconstructing via file scan.
        kVDM_InvalidChunkDetected,    // AVI: Invalid chunk detected at %lld. Enabling aggressive recovery mode.
        kVDM_StreamFailure,            // AVI: Invalid block found at %lld -- disabling streaming.
        kVDM_FixingBadSampleRate,    // AVI: Stream %d has an invalid sample rate. Substituting %lu samples/sec as placeholder.
        kVDM_PaletteChanges,        // AVI: Palette changes detected.  These are not currently supported -- color palette errors may appear in the output.
        kVDM_InfoTruncated,            // AVI: The text information chunk of type '%s' at %llx was not fully read because it was too long (%u bytes).
        kVDM_NonZeroStart            // AVI: Stream %u (%s) has a start position of %lld samples (%+lld ms). VirtualDub does not currently support a non-zero start time and the stream will be interpreted as starting from zero.
    };

    void ConvertAVIStreamInfo(VDAVIStreamInfo& streamInfo, const AVISTREAMINFO& streamInfoW32) {
        streamInfo.fccType                    = streamInfoW32.fccType;
        streamInfo.fccHandler                = streamInfoW32.fccHandler;
        streamInfo.dwFlags                    = streamInfoW32.dwFlags;
        streamInfo.wPriority                = streamInfoW32.wPriority;
        streamInfo.wLanguage                = streamInfoW32.wLanguage;
        streamInfo.dwScale                    = streamInfoW32.dwScale;
        streamInfo.dwRate                    = streamInfoW32.dwRate;
        streamInfo.dwStart                    = streamInfoW32.dwStart;
        streamInfo.dwLength                    = streamInfoW32.dwLength;
        streamInfo.dwInitialFrames            = streamInfoW32.dwInitialFrames;
        streamInfo.dwSuggestedBufferSize    = streamInfoW32.dwSuggestedBufferSize;
        streamInfo.dwQuality                = streamInfoW32.dwQuality;
        streamInfo.dwSampleSize                = streamInfoW32.dwSampleSize;
        streamInfo.rcFrameLeft                = (uint16)streamInfoW32.rcFrame.left;
        streamInfo.rcFrameTop                = (uint16)streamInfoW32.rcFrame.top;
        streamInfo.rcFrameRight                = (uint16)streamInfoW32.rcFrame.right;
        streamInfo.rcFrameBottom            = (uint16)streamInfoW32.rcFrame.bottom;
    }
}

///////////////////////////////////////////////////////////////////////////////

class AVIReadTunnelStream : public IAVIReadStream {
public:
    AVIReadTunnelStream(IAVIReadHandler *, PAVISTREAM, IAvisynthClipInfo *pClipInfo);
    ~AVIReadTunnelStream();

    sint32 BeginStreaming(VDPosition lStart, VDPosition lEnd, long lRate);
    sint32 EndStreaming();
    sint32 Info(VDAVIStreamInfo *pasi);
    bool IsKeyFrame(VDPosition lFrame);
    sint32 Read(VDPosition lStart, long lSamples, void *lpBuffer, long cbBuffer, long *plBytes, long *plSamples);
    VDPosition Start();
    VDPosition End();
    VDPosition PrevKeyFrame(VDPosition lFrame);
    VDPosition NextKeyFrame(VDPosition lFrame);
    VDPosition NearestKeyFrame(VDPosition lFrame);
    sint32 FormatSize(VDPosition lFrame, long *plSize);
    sint32 ReadFormat(VDPosition lFrame, void *pFormat, long *plSize);
    bool isStreaming();
    bool isKeyframeOnly();
    bool getVBRInfo(double& bitrate_mean, double& bitrate_stddev, double& maxdev) { return false; }

    sint64        getSampleBytePosition(VDPosition sample_num) { return -1; }

    VDPosition    TimeToPosition(VDTime timeInMicroseconds);
    VDTime        PositionToTime(VDPosition pos);

private:
    IAvisynthClipInfo *const mpAvisynthClipInfo;
    IAVIReadHandler *const parent;
    const PAVISTREAM pas;
};

///////////////////////////////////////////////////////////////////////////

AVIReadTunnelStream::AVIReadTunnelStream(IAVIReadHandler *_parent, PAVISTREAM _pas, IAvisynthClipInfo *pClipInfo)
: mpAvisynthClipInfo(pClipInfo)
, parent(_parent)
, pas(_pas)
{
    parent->AddRef();
}

AVIReadTunnelStream::~AVIReadTunnelStream() {
    pas->Release();
    parent->Release();
}

sint32 AVIReadTunnelStream::BeginStreaming(VDPosition lStart, VDPosition lEnd, long lRate) {
    LONG lStart32 = (LONG)lStart;
    LONG lEnd32 = (LONG)lEnd;

    if (lStart32 != lStart)
        lStart32 = lStart < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;
    if (lEnd32 != lEnd)
        lEnd32 = lEnd < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamBeginStreaming(pas, lStart32, lEnd32, lRate);
}

sint32 AVIReadTunnelStream::EndStreaming() {
    return AVIStreamEndStreaming(pas);
}

sint32 AVIReadTunnelStream::Info(VDAVIStreamInfo *pasi) {
    AVISTREAMINFO streamInfoW32;
    HRESULT hr = AVIStreamInfo(pas, &streamInfoW32, sizeof(streamInfoW32));

    if (FAILED(hr))
        return hr;

    ConvertAVIStreamInfo(*pasi, streamInfoW32);
    return S_OK;
}

bool AVIReadTunnelStream::IsKeyFrame(VDPosition lFrame) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return !!AVIStreamIsKeyFrame(pas, lFrame32);
}

sint32 AVIReadTunnelStream::Read(VDPosition lStart, long lSamples, void *lpBuffer, long cbBuffer, long *plBytes, long *plSamples) {
    HRESULT hr;

    {
        VDExternalCodeBracket(mpAvisynthClipInfo ? L"Avisynth" : L"An AVIFile input stream driver", __FILE__, __LINE__);
        hr = AVIStreamRead(pas, (LONG)lStart, lSamples, lpBuffer, cbBuffer, plBytes, plSamples);
    }

    if (mpAvisynthClipInfo) {
        const char *pszErr;

        if (mpAvisynthClipInfo->GetError(&pszErr))
            throw MyError("Avisynth read error:\n%s", pszErr);
    }

    return hr;
}

VDPosition AVIReadTunnelStream::Start() {
    return AVIStreamStart(pas);
}

VDPosition AVIReadTunnelStream::End() {
    return AVIStreamEnd(pas);
}

VDPosition AVIReadTunnelStream::PrevKeyFrame(VDPosition lFrame) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamPrevKeyFrame(pas, lFrame32);
}

VDPosition AVIReadTunnelStream::NextKeyFrame(VDPosition lFrame) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamNextKeyFrame(pas, lFrame32);
}

VDPosition AVIReadTunnelStream::NearestKeyFrame(VDPosition lFrame) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamNearestKeyFrame(pas, lFrame32);
}

sint32 AVIReadTunnelStream::FormatSize(VDPosition lFrame, long *plSize) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamFormatSize(pas, lFrame32, plSize);
}

sint32 AVIReadTunnelStream::ReadFormat(VDPosition lFrame, void *pFormat, long *plSize) {
    LONG lFrame32 = (LONG)lFrame;

    if (lFrame32 != lFrame)
        lFrame32 = lFrame < 0 ? (LONG)0x80000000 : (LONG)0x7FFFFFFF;

    return AVIStreamReadFormat(pas, lFrame32, pFormat, plSize);
}

bool AVIReadTunnelStream::isStreaming() {
    return false;
}

bool AVIReadTunnelStream::isKeyframeOnly() {
    return false;
}

VDPosition AVIReadTunnelStream::TimeToPosition(VDTime timeInUs) {
    AVISTREAMINFO asi;
    if (AVIStreamInfo(pas, &asi, sizeof asi))
        return 0;

    return VDRoundToInt64(timeInUs * (double)asi.dwRate / (double)asi.dwScale * (1.0 / 1000000.0));
}

VDTime AVIReadTunnelStream::PositionToTime(VDPosition pos) {
    AVISTREAMINFO asi;
    if (AVIStreamInfo(pas, &asi, sizeof asi))
        return 0;

    return VDRoundToInt64(pos * (double)asi.dwScale / (double)asi.dwRate * 1000000.0);
}

///////////////////////////////////////////////////////////////////////////

class AVIReadHandlerTunnelW32 : public IAVIReadHandler {
public:
    AVIReadHandlerTunnelW32(const wchar_t *);
    AVIReadHandlerTunnelW32(PAVIFILE);
    ~AVIReadHandlerTunnelW32();

    void AddRef();
    void Release();
    IAVIReadStream *GetStream(uint32 fccType, int lParam);
    void EnableFastIO(bool);
    bool isOptimizedForRealtime();
    bool isStreaming();
    bool isIndexFabricated();
    bool AppendFile(const wchar_t *pszFile);
    bool getSegmentHint(const char **ppszPath);
    void GetTextInfo(tTextInfo& textInfo);
    void GetTextInfoEncoding(int& codePage, int& countryCode, int& language, int& dialect);

private:
    IAvisynthClipInfo *mpAvisynthClipInfo;
    PAVIFILE    mpAVIFile;
    int            mRefCount;
};

IAVIReadHandler *CreateAVIReadHandler(PAVIFILE paf) {
    return new AVIReadHandlerTunnelW32(paf);
}

///////////////////////////////////////////////////////////////////////////////

AVIReadHandlerTunnelW32::AVIReadHandlerTunnelW32(PAVIFILE pAVIFile)
    : mpAvisynthClipInfo(NULL)
    , mpAVIFile(pAVIFile)
    , mRefCount(1)
{
    if (FAILED(mpAVIFile->QueryInterface(IID_IAvisynthClipInfo, (void **)&mpAvisynthClipInfo)))
        mpAvisynthClipInfo = NULL;
    else {
        const char *s;

        if (mpAvisynthClipInfo->GetError(&s)) {
            MyError e("Avisynth open failure:\n%s", s);
            mpAvisynthClipInfo->Release();
            mpAvisynthClipInfo = NULL;
            AVIFileRelease(mpAVIFile);
            mpAVIFile = NULL;
            throw e;
        }

        //VDLogAppMessage(kVDLogInfo, kVDST_AVIReadHandler, kVDM_AvisynthDetected);
    }
}

AVIReadHandlerTunnelW32::~AVIReadHandlerTunnelW32() {
    if (mpAvisynthClipInfo) {
        mpAvisynthClipInfo->Release();
        mpAvisynthClipInfo = NULL;
    }

    if (mpAVIFile) {
        AVIFileRelease(mpAVIFile);
        mpAVIFile = NULL;
    }
}

bool AVIReadHandlerTunnelW32::AppendFile(const wchar_t *pszFile) {
    return false;
}

void AVIReadHandlerTunnelW32::Release() {
    if (!--mRefCount)
        delete this;
}

void AVIReadHandlerTunnelW32::AddRef() {
    ++mRefCount;
}

IAVIReadStream *AVIReadHandlerTunnelW32::GetStream(uint32 fccType, int lParam) {
    PAVISTREAM pas;
    HRESULT hr;

    if (IsMMXState())
        throw MyInternalError("MMX state left on: %s:%d", __FILE__, __LINE__);

    hr = AVIFileGetStream(mpAVIFile, &pas, fccType, lParam);

    ClearMMXState();

    if (hr)
        return NULL;

    return new AVIReadTunnelStream(this, pas, mpAvisynthClipInfo);
}

void AVIReadHandlerTunnelW32::EnableFastIO(bool f) {
}

bool AVIReadHandlerTunnelW32::isOptimizedForRealtime() {
    return false;
}

bool AVIReadHandlerTunnelW32::isStreaming() {
    return false;
}

bool AVIReadHandlerTunnelW32::isIndexFabricated() {
    return false;
}

bool AVIReadHandlerTunnelW32::getSegmentHint(const char **ppszPath) {
    if (ppszPath)
        *ppszPath = NULL;

    return false;
}

void AVIReadHandlerTunnelW32::GetTextInfo(tTextInfo& textInfo) {
    textInfo.clear();
}

void AVIReadHandlerTunnelW32::GetTextInfoEncoding(int& codePage, int& countryCode, int& language, int& dialect) {
    codePage        = 0;
    countryCode        = 0;
    language        = 0;
    dialect            = 0;
}
