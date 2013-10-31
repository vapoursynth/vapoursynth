//    VirtualDub - Video processing and capture application
//    Copyright (C) 1998-2001 Avery Lee
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

#include "AVIReadHandler.h"
#include "AVIReadCache.h"
#include "AVIReadIndex.h"
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/debug.h>
#include <vd2/system/error.h>
#include <vd2/system/list.h>
#include <vd2/system/file.h>
#include <vd2/system/log.h>
#include <vd2/system/math.h>
#include <vd2/system/text.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/resources.h>
#include "Fixes.h"
#include "misc.h"

//#define VDTRACE_AVISTREAMING VDDEBUG
#define VDTRACE_AVISTREAMING (void)sizeof

#if defined(VD_COMPILER_MSVC)
    #pragma warning(disable: 4200)        // warning C4200: nonstandard extension used : zero-sized array in struct/union
#endif

extern bool VDPreferencesIsAVINonZeroStartWarningEnabled();

///////////////////////////////////////////////////////////////////////////

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
        kVDM_NonZeroStart,            // AVI: Stream %u (%s) has a start position of %lld samples (%+lld ms). VirtualDub does not currently support a non-zero start time and the stream will be interpreted as starting from zero.
        kVDM_IndexingAborted        // AVI: Indexing was aborted at byte location %llx.
    };

    bool is_palette_change(uint32 ckid) {
        return (ckid >> 16) == 'cp';
    }

    bool isValidFOURCCChar(uint8 c) {
        // FOURCCs must consist of a sequence of alphanumerics, padded at the end by spaces.
        //
        // Change: Underscores spotted out of DirectShow DV mux filter. *sigh*
        return (uint8)(c-0x30)<10 || (uint8)(c-0x41)<26 || (uint8)(c-0x61)<26 || c==0x20 || c==(uint8)'_';
    }
}

///////////////////////////////////////////////////////////////////////////

// The following comes from the OpenDML 1.0 spec for extended AVI files

namespace {
    // bIndexType codes
    //
    #define AVI_INDEX_OF_INDEXES 0x00    // when each entry in aIndex
                                        // array points to an index chunk

    #define AVI_INDEX_OF_CHUNKS 0x01    // when each entry in aIndex
                                        // array points to a chunk in the file

    #define AVI_INDEX_IS_DATA    0x80    // when each entry is aIndex is
                                        // really the data

    // bIndexSubtype codes for INDEX_OF_CHUNKS

    #define AVI_INDEX_2FIELD    0x01    // when fields within frames
                                        // are also indexed
        struct _avisuperindex_entry {
            uint64 qwOffset;        // absolute file offset, offset 0 is
                                    // unused entry??
            uint32 dwSize;            // size of index chunk at this offset
            uint32 dwDuration;        // time span in stream ticks
        };
        struct _avistdindex_entry {
            uint32 dwOffset;            // qwBaseOffset + this is absolute file offset
            uint32 dwSize;            // bit 31 is set if this is NOT a keyframe
        };
        struct _avifieldindex_entry {
            uint32    dwOffset;
            uint32    dwSize;
            uint32    dwOffsetField2;
        };

    #pragma pack(push)
    #pragma pack(2)

    typedef struct _avisuperindex_chunk {
        uint32 fcc;                    // ’ix##’
        uint32 cb;                    // size of this structure
        uint16 wLongsPerEntry;        // must be 4 (size of each entry in aIndex array)
        uint8 bIndexSubType;            // must be 0 or AVI_INDEX_2FIELD
        uint8 bIndexType;            // must be AVI_INDEX_OF_INDEXES
        uint32 nEntriesInUse;        // number of entries in aIndex array that
                                    // are used
        uint32 dwChunkId;            // ’##dc’ or ’##db’ or ’##wb’, etc
        uint32 dwReserved[3];        // must be 0
        struct _avisuperindex_entry aIndex[];
    } AVISUPERINDEX, * PAVISUPERINDEX;

    typedef struct _avistdindex_chunk {
        uint32 fcc;                    // ’ix##’
        uint32 cb;
        uint16 wLongsPerEntry;        // must be sizeof(aIndex[0])/sizeof(uint32)
        uint8 bIndexSubType;            // must be 0
        uint8 bIndexType;            // must be AVI_INDEX_OF_CHUNKS
        uint32 nEntriesInUse;        //
        uint32 dwChunkId;            // ’##dc’ or ’##db’ or ’##wb’ etc..
        uint64 qwBaseOffset;        // all dwOffsets in aIndex array are
                                    // relative to this
        uint32 dwReserved3;            // must be 0
        struct _avistdindex_entry aIndex[];
    } AVISTDINDEX, * PAVISTDINDEX;

    typedef struct _avifieldindex_chunk {
        uint32        fcc;
        uint32        cb;
        uint16        wLongsPerEntry;
        uint8        bIndexSubType;
        uint8        bIndexType;
        uint32        nEntriesInUse;
        uint32        dwChunkId;
        uint64    qwBaseOffset;
        uint32        dwReserved3;
        struct    _avifieldindex_entry aIndex[];
    } AVIFIELDINDEX, * PAVIFIELDINDEX;

    struct AVIIndexEntry {
        enum {
            kFlagKeyFrame = 0x10
        };

        uint32 ckid;
        uint32 dwFlags;
        uint32 dwChunkOffset;
        uint32 dwChunkLength;
    };

    struct VDAVIMainHeader {
        uint32    dwMicroSecPerFrame;
        uint32    dwMaxBytesPerSec;
        uint32    dwPaddingGranularity;
        uint32    dwFlags;
        uint32    dwTotalFrames;
        uint32    dwInitialFrames;
        uint32    dwStreams;
        uint32    dwSuggestedBufferSize;
        uint32    dwWidth;
        uint32    dwHeight;
        uint32    dwReserved[4];
    };

    #pragma pack(pop)

    static const uint32 kAVIStreamTypeAudio = VDMAKEFOURCC('a', 'u', 'd', 's');
    static const uint32 kAVIStreamTypeVideo = VDMAKEFOURCC('v', 'i', 'd', 's');
    static const uint32 kAVIStreamTypeDV = VDMAKEFOURCC('i', 'a', 'v', 's');

    uint32 VDAVIGetStreamFromFOURCC(uint32 fcc0) {
        uint32 fcc = VDFromLE32(fcc0);
        uint32 hi = ((fcc >>  0) - '0') & 0x1f;
        uint32 lo = ((fcc >>  8) - '0') & 0x1f;

        if (lo >= 10)
            lo -= 7;

        if (hi >= 10)
            hi -= 7;

        return (hi << 4) + lo;
    }
}

#ifdef _DEBUG
namespace {
    class Check {
    public:
        Check() {
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('0', '0', 'd', 'c')) == 0);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('0', '1', 'd', 'c')) == 1);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('0', '9', 'd', 'c')) == 9);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('0', 'a', 'd', 'c')) == 10);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('0', 'f', 'd', 'c')) == 15);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('1', '0', 'd', 'c')) == 16);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('9', 'f', 'd', 'c')) == 159);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('a', '0', 'd', 'c')) == 160);
            VDASSERT(VDAVIGetStreamFromFOURCC(VDMAKEFOURCC('f', 'f', 'd', 'c')) == 255);
        }
    } gCheck;
}
#endif


///////////////////////////////////////////////////////////////////////////

IAVIReadStream::~IAVIReadStream() {
}

///////////////////////////////////////////////////////////////////////////

class AVIStreamNode;
class AVIReadHandler;

///////////////////////////////////////////////////////////////////////////

class AVIStreamNode : public ListNode2<AVIStreamNode> {
public:
    AVIStreamHeader_fixed    hdr;
    void                    *pFormat;
    long                    lFormatLen;
    VDAVIReadIndex            mIndex;
    sint64                    bytes;
    bool                    keyframe_only;
    bool                    is_VBR;
    int                        handler_count;
    class AVIReadCache        *cache;
    int                        streaming_count;
    sint64                    stream_push_pos;
    sint64                    stream_bytes;
    int                        stream_pushes;
    sint64                    length;
    sint64                    frames;
    List2<class AVIReadStream>    listHandlers;

    AVIStreamNode();
    ~AVIStreamNode();
};

AVIStreamNode::AVIStreamNode() {
    pFormat = NULL;
    bytes = 0;
    handler_count = 0;
    streaming_count = 0;

    stream_bytes = 0;
    stream_pushes = 0;
    cache = NULL;

    is_VBR = false;
}

AVIStreamNode::~AVIStreamNode() {
    delete pFormat;
    delete cache;
}

///////////////////////////////////////////////////////////////////////////

class AVIFileDesc {
public:
    VDFile        mFile;
    VDFile        mFileUnbuffered;
    sint64        mFileSize;
};

class AVIStreamNode;

class AVIReadHandler : public IAVIReadHandler, public IAVIReadCacheSource {
public:
    AVIReadHandler(const wchar_t *);
    ~AVIReadHandler();

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

    void EnableStreaming(int stream);
    void DisableStreaming(int stream);
    void AdjustRealTime(bool fRealTime);
    void FixCacheProblems(class AVIReadStream *);
    long ReadData(int stream, void *buffer, sint64 position, long len);

public:    // IAVIReadCacheSource
    bool Stream(AVIStreamNode *, _int64 pos);
    sint64 getStreamPtr();

private:
    friend class AVIReadStream;

    bool ReadChunkHeader(uint32& type, uint32& size);
    void SelectFile(int file);

//    enum { STREAM_SIZE = 65536 };
    enum { STREAM_SIZE = 1048576 };
    enum { STREAM_RT_SIZE = 65536 };
    enum { STREAM_BLOCK_SIZE = 4096 };

    int            mRefCount;
    sint64        i64StreamPosition;
    int            streams;
    char        *streamBuffer;
    int            sbPosition;
    int            sbSize;
    long        fStreamsActive;
    int            nRealTime;
    int            nActiveStreamers;
    bool        fFakeIndex;
    int            nFiles;

    AVIFileDesc    *mpCurrentFile;
    int            mCurrentFile;

    char *        pSegmentHint;

    bool        fDisableFastIO;

    // Whenever a file is aggressively recovered, do not allow streaming.

    bool        mbFileIsDamaged;
    bool        mbPaletteChangesDetected;

    int            mTextInfoCodePage;
    int            mTextInfoCountryCode;
    int            mTextInfoLanguage;
    int            mTextInfoDialect;

    tTextInfo    mTextInfo;

    List2<AVIStreamNode>        listStreams;
    vdfastvector<AVIFileDesc *>    mFiles;

    void        _construct(const wchar_t *pszFile);
    void        _parseFile(List2<AVIStreamNode>& streams);
    bool        _parseStreamHeader(List2<AVIStreamNode>& streams, uint32 dwLengthLeft, bool& bIndexDamaged);
    bool        _parseIndexBlock(List2<AVIStreamNode>& streams, int count, sint64);
    void        _parseExtendedIndexBlock(List2<AVIStreamNode>& streams, AVIStreamNode *pasn, sint64 fpos, uint32 dwLength);
    void        _destruct();

    char *        _StreamRead(long& bytes);
};

IAVIReadHandler *CreateAVIReadHandler(const wchar_t *pszFile) {
    return new AVIReadHandler(pszFile);
}

///////////////////////////////////////////////////////////////////////////

class AVIReadStream : public IAVIReadStream, public ListNode2<AVIReadStream> {
    friend AVIReadHandler;

public:
    AVIReadStream(AVIReadHandler *, AVIStreamNode *, int);
    ~AVIReadStream();

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
    void Reinit();
    bool getVBRInfo(double& bitrate_mean, double& bitrate_stddev, double& maxdev);
    sint64        getSampleBytePosition(VDPosition sample_num);

    VDPosition    TimeToPosition(VDTime timeInMicroseconds);
    VDTime        PositionToTime(VDPosition pos);

private:
    AVIReadHandler *parent;
    AVIStreamNode *psnData;
    VDAVIReadIndex *mpIndex;
    AVIReadCache *rCache;
    sint64& length;
    sint64& frames;
    long sampsize;
    int streamno;
    bool fStreamingEnabled;
    bool fStreamingActive;
    int iStreamTrackCount;
    sint64    lStreamTrackValue;
    sint64    lStreamTrackInterval;
    bool fRealTime;
};

///////////////////////////////////////////////////////////////////////////

AVIReadStream::AVIReadStream(AVIReadHandler *parent, AVIStreamNode *psnData, int streamno)
:length(psnData->length)
,frames(psnData->frames)
{
    this->parent = parent;
    this->psnData = psnData;
    this->streamno = streamno;

    fStreamingEnabled = false;
    fStreamingActive = false;
    fRealTime = false;

    parent->AddRef();

    mpIndex = &psnData->mIndex;
    sampsize = psnData->hdr.dwSampleSize;

    // Hack to imitate Microsoft's parser.  It seems to ignore this value
    // for audio streams.

    if (psnData->hdr.fccType == kAVIStreamTypeAudio && !psnData->is_VBR) {
        sampsize = ((WAVEFORMATEX *)psnData->pFormat)->nBlockAlign;

        // wtf....
        if (!sampsize)
            sampsize = 1;

        length = mpIndex->GetSampleCount();
    }

    psnData->listHandlers.AddTail(this);
}

AVIReadStream::~AVIReadStream() {
    EndStreaming();
    Remove();
    parent->Release();
}

void AVIReadStream::Reinit() {
    length = mpIndex->GetSampleCount();
}

sint32 AVIReadStream::BeginStreaming(VDPosition lStart, VDPosition lEnd, long lRate) {
    if (fStreamingEnabled)
        return 0;

    if (lRate <= 1500) {
        parent->AdjustRealTime(true);
        fRealTime = true;
    } else
        fRealTime = false;

    if (parent->fDisableFastIO)
        return 0;

    if (!psnData->streaming_count) {
        psnData->stream_bytes = 0;
        psnData->stream_pushes = 0;
        psnData->stream_push_pos = 0;
    }
    ++psnData->streaming_count;

    fStreamingEnabled = true;
    fStreamingActive = false;
    iStreamTrackCount = 0;
    lStreamTrackValue = -1;
    lStreamTrackInterval = -1;
    return 0;
}

sint32 AVIReadStream::EndStreaming() {
    if (!fStreamingEnabled)
        return 0;

    if (fRealTime)
        parent->AdjustRealTime(false);

    if (fStreamingActive)
        parent->DisableStreaming(streamno);

    fStreamingEnabled = false;
    fStreamingActive = false;

    if (!--psnData->streaming_count) {
        delete psnData->cache;
        psnData->cache = NULL;
    }
    return 0;
}

sint32 AVIReadStream::Info(VDAVIStreamInfo *pasi) {
    VDAVIStreamInfo& asi = *pasi;

    asi.fccType                = psnData->hdr.fccType;
    asi.fccHandler            = psnData->hdr.fccHandler;
    asi.dwFlags                = psnData->hdr.dwFlags;
    asi.wPriority            = psnData->hdr.wPriority;
    asi.wLanguage            = psnData->hdr.wLanguage;
    asi.dwScale                = psnData->hdr.dwScale;
    asi.dwRate                = psnData->hdr.dwRate;
    asi.dwStart                = psnData->hdr.dwStart;
    asi.dwLength            = psnData->hdr.dwLength;
    asi.dwInitialFrames        = psnData->hdr.dwInitialFrames;
    asi.dwSuggestedBufferSize = psnData->hdr.dwSuggestedBufferSize;
    asi.dwQuality            = psnData->hdr.dwQuality;
    asi.dwSampleSize        = psnData->hdr.dwSampleSize;
    asi.rcFrameTop            = psnData->hdr.rcFrame.top;
    asi.rcFrameLeft            = psnData->hdr.rcFrame.left;
    asi.rcFrameRight        = psnData->hdr.rcFrame.right;
    asi.rcFrameBottom        = psnData->hdr.rcFrame.bottom;
    return 0;
}

bool AVIReadStream::IsKeyFrame(VDPosition lFrame) {
    return mpIndex->IsKey(lFrame);
}

sint32 AVIReadStream::Read(VDPosition lStart, long lSamples, void *lpBuffer, long cbBuffer, long *plBytes, long *plSamples) {
    long lActual;

    if (lStart < 0 || lStart >= length || (lSamples <= 0 && lSamples != kConvenient)) {
        // umm... dummy!  can't read outside of stream!

        if (plBytes) *plBytes = 0;
        if (plSamples) *plSamples = 0;

        return 0;
    }

    // blocked or discrete?

    if (sampsize) {
        // too small to hold a sample?
        if (lpBuffer && cbBuffer < sampsize) {
            if (plBytes) *plBytes = sampsize * lSamples;
            if (plSamples) *plSamples = lSamples;

            return kBufferTooSmall;
        }

        // client too lazy to specify a size?
        bool stopOnRead = false;
        if (lSamples == kConvenient) {
            lSamples = 0x10000;
            stopOnRead = true;
        }

        // locate first sample
        VDAVIReadIndexIterator it;

        mpIndex->FindSampleRange(it, lStart, lSamples);

        sint64 chunkPos;
        uint32 chunkOffset;
        uint32 byteSize;
        bool more = mpIndex->GetNextSampleRange(it, chunkPos, chunkOffset, byteSize);

        sint64 actual_bytes = 0;

        // trim down sample count
        if (lpBuffer && lSamples > cbBuffer / sampsize)
            lSamples = cbBuffer / sampsize;

        if (lStart+lSamples > length)
            lSamples = (long)(length - lStart);

        uint64 bytecnt = lSamples * sampsize;

        // begin reading frames from this point on
        if (lpBuffer) {
            // detect streaming

            if (fStreamingEnabled) {

                // We consider the client to be streaming if we detect at least
                // 3 consecutive accesses

                if (lStart == lStreamTrackValue) {
                    ++iStreamTrackCount;

                    if (iStreamTrackCount >= 15) {

                        sint64 streamptr = parent->getStreamPtr();
                        sint64 fptrdiff = streamptr - (chunkPos - 8);

                        if (!parent->isStreaming() || streamptr<0 || (fptrdiff<4194304 && fptrdiff>-4194304)) {
                            if (!psnData->cache)
                                psnData->cache = new AVIReadCache(psnData->hdr.fccType == 'sdiv' ? 131072 : 16384, streamno, parent, psnData);
                            else
                                psnData->cache->ResetStatistics();

                            if (!fStreamingActive) {
                                fStreamingActive = true;
                                parent->EnableStreaming(streamno);
                            }

                            VDTRACE_AVISTREAMING("[a] streaming enabled\n");
                        }
                    } else {
                        VDTRACE_AVISTREAMING("[a] streaming detected\n");
                    }
                } else {
                    VDTRACE_AVISTREAMING("[a] streaming disabled\n");

                    iStreamTrackCount = 0;

                    if (fStreamingActive) {
                        fStreamingActive = false;
                        parent->DisableStreaming(streamno);
                    }
                }
            }

            while(bytecnt > 0) {
                uint32 tc = byteSize;
                if (tc > bytecnt)
                    tc = (uint32)bytecnt;

                if (psnData->cache && fStreamingActive && tc < psnData->cache->getMaxRead()) {
                    lActual = psnData->cache->Read(lpBuffer, chunkPos - 8, chunkPos + chunkOffset, tc);
                    psnData->stream_bytes += lActual;
                } else
                    lActual = parent->ReadData(streamno, lpBuffer, chunkPos + chunkOffset, tc);

                if (lActual < 0)
                    break;

                actual_bytes += lActual;

                if (lActual < tc)
                    break;

                bytecnt -= tc;
                lpBuffer = (char *)lpBuffer + tc;

                if (!more)
                    break;

                more = mpIndex->GetNextSampleRange(it, chunkPos, chunkOffset, byteSize);
            }

            if (actual_bytes < sampsize) {
                if (plBytes) *plBytes = 0;
                if (plSamples) *plSamples = 0;
                return kFileReadError;
            }

            actual_bytes -= actual_bytes % sampsize;

            if (plBytes) *plBytes = (long)actual_bytes;
            if (plSamples) *plSamples = (long)actual_bytes / sampsize;

            lStreamTrackValue = lStart + (long)actual_bytes / sampsize;

        } else {
            if (plBytes) *plBytes = (long)bytecnt;
            if (plSamples) *plSamples = lSamples;
        }

    } else {
        VDAVIReadIndexIterator it;
        mpIndex->FindSampleRange(it, lStart, lSamples);

        sint64 chunkPos;
        uint32 chunkOffset;
        uint32 byteSize;
        mpIndex->GetNextSampleRange(it, chunkPos, chunkOffset, byteSize);

        if (lpBuffer && byteSize > cbBuffer) {
            if (plBytes) *plBytes = byteSize;
            if (plSamples) *plSamples = 1;

            return kBufferTooSmall;
        }

        if (lpBuffer) {

            // detect streaming

            if (fStreamingEnabled && lStart != lStreamTrackValue) {
                if (lStreamTrackValue>=0 && lStart-lStreamTrackValue == lStreamTrackInterval) {
                    if (++iStreamTrackCount >= 15) {

                        sint64 streamptr = parent->getStreamPtr();
                        sint64 fptrdiff = streamptr - (chunkPos - 8);

                        if (!parent->isStreaming() || streamptr<0 || (fptrdiff<4194304 && fptrdiff>-4194304)) {
                            if (!psnData->cache)
                                psnData->cache = new AVIReadCache(psnData->hdr.fccType == 'sdiv' ? 131072 : 16384, streamno, parent, psnData);
                            else
                                psnData->cache->ResetStatistics();

                            if (!fStreamingActive) {
                                fStreamingActive = true;
                                parent->EnableStreaming(streamno);
                            }

                            VDTRACE_AVISTREAMING("[v] streaming activated\n");
                        }
                    } else {
                        VDTRACE_AVISTREAMING("[v] streaming detected\n");
                    }
                } else {
                    iStreamTrackCount = 0;

                    VDTRACE_AVISTREAMING("[v] streaming disabled\n");

                    if (lStreamTrackValue>=0 && lStart > lStreamTrackValue) {
                        lStreamTrackInterval = lStart - lStreamTrackValue;
                    } else
                        lStreamTrackInterval = -1;

                    if (fStreamingActive) {
                        fStreamingActive = false;
                        parent->DisableStreaming(streamno);
                    }
                }

                lStreamTrackValue = lStart;
            }

            // read data

            if (psnData->cache && fStreamingActive && byteSize < psnData->cache->getMaxRead()) {
//OutputDebugString("[v] attempting cached read\n");
                lActual = psnData->cache->Read(lpBuffer, chunkPos - 8, chunkPos + chunkOffset, byteSize);
                psnData->stream_bytes += lActual;
            } else
                lActual = parent->ReadData(streamno, lpBuffer, chunkPos + chunkOffset, byteSize);

            if (lActual != (long)byteSize) {
                if (plBytes) *plBytes = 0;
                if (plSamples) *plSamples = 0;
                return kFileReadError;
            }
        }

        if (plBytes) *plBytes = byteSize;
        if (plSamples) *plSamples = 1;
    }

    if (psnData->cache && fStreamingActive) {

        // Are we experiencing a high rate of cache misses?

        if (psnData->cache->cache_miss_bytes*2 > psnData->cache->cache_hit_bytes && psnData->cache->reads > 50) {

            // sh*t, notify the parent that we have cache misses so it can check which stream is
            // screwing up, and disable streaming on feeds that are too far off

            parent->FixCacheProblems(this);
            iStreamTrackCount = 0;
        }
    }

    return 0;
}

sint64 AVIReadStream::getSampleBytePosition(VDPosition pos) {
    if (pos < 0 || pos >= length)
        return -1;

    VDAVIReadIndexIterator it;
    mpIndex->FindSampleRange(it, pos, 1);

    sint64 chunkPos;
    uint32 chunkOffset;
    uint32 byteSize;
    mpIndex->GetNextSampleRange(it, chunkPos, chunkOffset, byteSize);

    return (chunkPos + chunkOffset) & 0x0000FFFFFFFFFFFF;
}

VDPosition AVIReadStream::Start() {
    return 0;
}

VDPosition AVIReadStream::End() {
    return length;
}

VDPosition AVIReadStream::PrevKeyFrame(VDPosition lFrame) {
    if (sampsize)
        return lFrame>0 ? lFrame-1 : -1;

    if (lFrame < 0)
        return -1;

    if (lFrame >= length)
        lFrame = length;

    return mpIndex->PrevKey(lFrame);
}

VDPosition AVIReadStream::NextKeyFrame(VDPosition lFrame) {
    if (sampsize)
        return lFrame<length ? lFrame+1 : -1;

    if (lFrame < 0)
        return 0;

    if (lFrame >= length)
        return -1;

    return mpIndex->NextKey(lFrame);
}

VDPosition AVIReadStream::NearestKeyFrame(VDPosition lFrame) {
    if (sampsize)
        return lFrame;

    if (lFrame < 0)
        return -1;

    if (lFrame >= length)
        lFrame = length - 1;

    return mpIndex->NearestKey(lFrame);
}

sint32 AVIReadStream::FormatSize(VDPosition lFrame, long *plSize) {
    *plSize = psnData->lFormatLen;
    return 0;
}

sint32 AVIReadStream::ReadFormat(VDPosition lFrame, void *pFormat, long *plSize) {
    if (!pFormat) {
        *plSize = psnData->lFormatLen;
        return 0;
    }

    if (*plSize < psnData->lFormatLen) {
        memcpy(pFormat, psnData->pFormat, *plSize);
    } else {
        memcpy(pFormat, psnData->pFormat, psnData->lFormatLen);
        *plSize = psnData->lFormatLen;
    }

    return 0;
}

bool AVIReadStream::isStreaming() {
    return psnData->cache && fStreamingActive && parent->isStreaming();
}

bool AVIReadStream::isKeyframeOnly() {
   return psnData->keyframe_only;
}

bool AVIReadStream::getVBRInfo(double& bitrate_mean, double& bitrate_stddev, double& maxdev) {
    if (!psnData->is_VBR)
        return false;

    sint64 size_accum = 0;
    double max_dev = 0;
    double size_sq_sum = 0.0;

    VDAVIReadIndexIterator it;
    mpIndex->GetFirstSampleRange(it);

    sint64 chunkPos;
    uint32 offset;
    uint32 byteSize;
    double bytes = (double)mpIndex->GetByteCount();
    double bytesPerChunk = bytes / (double)frames;
    int i = 0;
    while(mpIndex->GetNextSampleRange(it, chunkPos, offset, byteSize)) {
        double mean_center = bytesPerChunk * (i+0.5);
        double dev = fabs(mean_center - (double)(size_accum + (byteSize>>1)));

        if (dev > max_dev)
            max_dev = dev;

        size_accum += byteSize;
        size_sq_sum += (double)byteSize * byteSize;
        ++i;
    }

    // I hate probability & sadistics.
    //
    // Var(X) = E(X2) - E(X)^2
    //          = S(X2)/n - (S(x)/n)^2
    //          = (n*S(X2) - S(X)^2)/n^2
    //
    // SD(x) = sqrt(n*S(X2) - S(X)^2) / n

    double frames_per_second = (double)psnData->hdr.dwRate / (double)psnData->hdr.dwScale;
    double sum1_bits = bytes * 8.0;
    double sum2_bits = size_sq_sum * 64.0;

    bitrate_mean        = (sum1_bits / frames) * frames_per_second;
    bitrate_stddev        = sqrt(std::max<double>(0.0, frames * sum2_bits - sum1_bits * sum1_bits)) / frames * frames_per_second;
    maxdev                = max_dev * 8.0 / bitrate_mean;
    return true;
}

VDPosition AVIReadStream::TimeToPosition(VDTime timeInUs) {
    return VDRoundToInt64(timeInUs * (double)psnData->hdr.dwRate / (double)psnData->hdr.dwScale * (1.0 / 1000000.0));
}

VDTime AVIReadStream::PositionToTime(VDPosition sample) {
    return VDRoundToInt64(sample * (double)psnData->hdr.dwScale / (double)psnData->hdr.dwRate * 1000000.0);
}

///////////////////////////////////////////////////////////////////////////

AVIReadHandler::AVIReadHandler(const wchar_t *s)
    : mbFileIsDamaged(false)
    , mTextInfoCodePage(0)
    , mTextInfoCountryCode(0)
    , mTextInfoLanguage(0)
    , mTextInfoDialect(0)
{
    mRefCount = 1;
    streams=0;
    fStreamsActive = 0;
    fDisableFastIO = false;
    streamBuffer = NULL;
    nRealTime = 0;
    nActiveStreamers = 0;
    fFakeIndex = false;
    nFiles = 1;
    pSegmentHint = NULL;

    _construct(s);
}

AVIReadHandler::~AVIReadHandler() {
    _destruct();
}

void AVIReadHandler::_construct(const wchar_t *pszFile) {

    try {
        // create first link
        vdautoptr<AVIFileDesc> pDesc(new_nothrow AVIFileDesc);

        if (!pDesc)
            throw MyMemoryError();

        // open file
        pDesc->mFile.open(pszFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kSequential);
        pDesc->mFileUnbuffered.openNT(pszFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kUnbuffered);
        pDesc->mFileSize = pDesc->mFile.size();

        mpCurrentFile = pDesc;
        mCurrentFile = -1;
        mFiles.push_back(pDesc.release());

        // recursively parse file

        _parseFile(listStreams);

    } catch(...) {
        _destruct();
        throw;
    }
}

bool AVIReadHandler::AppendFile(const wchar_t *pszFile) {
    List2<AVIStreamNode> newstreams;
    AVIStreamNode *pasn_old, *pasn_new, *pasn_old_next=NULL, *pasn_new_next=NULL;

    // open file

    vdautoptr<AVIFileDesc> pDesc(new_nothrow AVIFileDesc);

    if (!pDesc)
        throw MyMemoryError();

    pDesc->mFile.open(pszFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kSequential);
    pDesc->mFileUnbuffered.openNT(pszFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kUnbuffered);
    pDesc->mFileSize = pDesc->mFile.size();
    mFiles.push_back(pDesc.release());

    mpCurrentFile = mFiles.back();
    mCurrentFile = mFiles.size() - 1;

    try {
        _parseFile(newstreams);

        pasn_old = listStreams.AtHead();
        pasn_new = newstreams.AtHead();

        while(!!(pasn_old_next = pasn_old->NextFromHead()) & !!(pasn_new_next = pasn_new->NextFromHead())) {
            const char *szStreamType = NULL;

            switch(pasn_old->hdr.fccType) {
            case kAVIStreamTypeAudio:    szStreamType = "audio"; break;
            case kAVIStreamTypeVideo:    szStreamType = "video"; break;
            case kAVIStreamTypeDV:        szStreamType = "DV"; break;
            }

            // If it's not an audio or video stream, why do we care?

            if (szStreamType) {
                // allow ivas as a synonym for vids

                uint32 fccOld = pasn_old->hdr.fccType;
                uint32 fccNew = pasn_new->hdr.fccType;

                if (fccOld != fccNew)
                    throw MyError("Cannot append segment \"%ls\": The segment has a different set of streams.", pszFile);

                // A/B ?= C/D ==> AD ?= BC

                if ((sint64)pasn_old->hdr.dwScale * pasn_new->hdr.dwRate != (sint64)pasn_new->hdr.dwScale * pasn_old->hdr.dwRate)
                    throw MyError("Cannot append segment \"%ls\": The %s streams do not share a common sampling rate.\n"
                            "\n"
                            "First stream: %08x / %08x = %.5f samples/sec\n"
                            "Second stream: %08x / %08x = %.5f samples/sec"
                            ,pszFile
                            ,szStreamType
                            ,pasn_old->hdr.dwRate
                            ,pasn_old->hdr.dwScale
                            ,(double)pasn_old->hdr.dwRate / pasn_old->hdr.dwScale
                            ,pasn_new->hdr.dwRate
                            ,pasn_new->hdr.dwScale
                            ,(double)pasn_new->hdr.dwRate / pasn_new->hdr.dwScale
                            );

                if (pasn_old->hdr.dwSampleSize != pasn_new->hdr.dwSampleSize)
                    throw MyError("Cannot append segment \"%ls\": The %s streams have different sample sizes (%d vs %d).", pszFile, szStreamType, pasn_old->hdr.dwSampleSize, pasn_new->hdr.dwSampleSize);

                // I hate PCMWAVEFORMAT.

                uint32 oldFormatLen = pasn_old->lFormatLen;
                uint32 newFormatLen = pasn_new->lFormatLen;
                uint32 basicFormatLen = 0;

                if (pasn_old->hdr.fccType == kAVIStreamTypeAudio) {
                    const WAVEFORMATEX& wfex1 = *(const WAVEFORMATEX *)pasn_old->pFormat;
                    const WAVEFORMATEX& wfex2 = *(const WAVEFORMATEX *)pasn_new->pFormat;

                    if (wfex1.wFormatTag != wfex2.wFormatTag)
                        throw MyError("Cannot append segment \"%ls\": The audio streams use different compression formats.", pszFile);

                    if (wfex1.nSamplesPerSec != wfex2.nSamplesPerSec)
                        throw MyError("Cannot append segment \"%ls\": The audio streams use different sampling rates (%u vs. %u)", pszFile, wfex1.nSamplesPerSec, wfex2.nSamplesPerSec);

                    if (wfex1.nAvgBytesPerSec != wfex2.nAvgBytesPerSec)
                        throw MyError("Cannot append segment \"%ls\": The audio streams use different data rates (%u bytes/sec vs. %u bytes/sec)", pszFile, wfex1.nAvgBytesPerSec, wfex2.nAvgBytesPerSec);

                    if (wfex1.wFormatTag == WAVE_FORMAT_PCM
                        && wfex2.wFormatTag == WAVE_FORMAT_PCM)
                    {
                        oldFormatLen = sizeof(PCMWAVEFORMAT);
                        newFormatLen = sizeof(PCMWAVEFORMAT);
                        basicFormatLen = sizeof(PCMWAVEFORMAT);
                    } else {
                        basicFormatLen = sizeof(WAVEFORMATEX);
                    }

                } else if (pasn_new->hdr.fccType == kAVIStreamTypeVideo) {
                    basicFormatLen = sizeof(BITMAPINFOHEADER);

                    const BITMAPINFOHEADER& hdr1 = *(const BITMAPINFOHEADER *)pasn_old->pFormat;
                    const BITMAPINFOHEADER& hdr2 = *(const BITMAPINFOHEADER *)pasn_new->pFormat;

                    if (hdr1.biWidth != hdr2.biWidth || hdr1.biHeight != hdr2.biHeight)
                        throw MyError("Cannot append segment \"%ls\": The video streams are of different sizes (%dx%d vs. %dx%d)", pszFile, hdr1.biWidth, hdr1.biHeight, hdr2.biWidth, hdr2.biHeight);

                    if (hdr1.biCompression != hdr2.biCompression)
                        throw MyError("Cannot append segment \"%ls\": The video streams use different compression schemes.", pszFile);
                }

                uint32 minFormatLen = std::min<uint32>(oldFormatLen, newFormatLen);
                uint32 i = 0;

                for(; i<minFormatLen; ++i) {
                    if (((const char *)pasn_old->pFormat)[i] != ((const char *)pasn_new->pFormat)[i])
                        break;
                }

                bool fOk = (i == oldFormatLen && i == newFormatLen);

                if (!fOk)
                    throw MyError("Cannot append segment \"%ls\": The %s streams have incompatible data formats.\n\n(Mismatch detected in opaque codec data at byte %u of the format data.)", pszFile, szStreamType, i);
            }

            pasn_old = pasn_old_next;
            pasn_new = pasn_new_next;
        }

        if (pasn_old_next || pasn_new_next)
            throw MyError("Cannot append segment \"%ls\": The segment has a different number of streams.", pszFile);

    } catch(const MyError&) {
        while(pasn_new = newstreams.RemoveHead())
            delete pasn_new;

        mpCurrentFile = NULL;
        mCurrentFile = -1;
        delete mFiles.back();
        mFiles.pop_back();
        throw;
    }

    // Accept segment; begin merging process.

    pasn_old = listStreams.AtHead();

    while(pasn_old_next = pasn_old->NextFromHead()) {
        pasn_new = newstreams.RemoveHead();

        // Fix up header.

        pasn_old->hdr.dwLength    += pasn_new->hdr.dwLength;

        if (pasn_new->hdr.dwSuggestedBufferSize > pasn_old->hdr.dwSuggestedBufferSize)
            pasn_old->hdr.dwSuggestedBufferSize = pasn_new->hdr.dwSuggestedBufferSize;

        pasn_old->bytes        += pasn_new->bytes;
        pasn_old->frames    += pasn_new->frames;
        pasn_old->length    += pasn_new->length;

        // Merge indices.
        pasn_old->mIndex.Append(pasn_new->mIndex, (sint64)nFiles << 48);

        // Notify all handlers.

        AVIReadStream *pStream, *pStreamNext;

        pStream = pasn_old->listHandlers.AtHead();
        while(pStreamNext = pStream->NextFromHead()) {
            pStream->Reinit();

            pStream = pStreamNext;
        }

        // Next!

        pasn_old = pasn_old_next;
        delete pasn_new;
    }

    ++nFiles;
    return true;
}

void AVIReadHandler::_parseFile(List2<AVIStreamNode>& streamlist) {
    uint32 fccType;
    uint32 dwLength;
    bool index_found = false;
    bool fAcceptIndexOnly = true;
    bool hyperindexed = false;
    bool bScanRequired = false;
    AVIStreamNode *pasn, *pasn_next;
    VDAVIMainHeader avihdr;
    bool bMainAVIHeaderFound = false;

    sint64    i64ChunkMoviPos = 0;
    uint32    dwChunkMoviLength = 0;

    if (!ReadChunkHeader(fccType, dwLength))
        throw MyError("Invalid AVI file: File is less than 8 bytes");

    if (fccType != FOURCC_RIFF)
        throw MyError("Invalid AVI file: Not a RIFF file");

    // If the RIFF header is <4 bytes, assume it was an improperly closed
    // file.

    mpCurrentFile->mFile.read(&fccType, 4);

    if (fccType != ' IVA')
        throw MyError("Invalid AVI file: RIFF type is not 'AVI'");

    // Aggressive mode recovery does extensive validation and searching to attempt to
    // recover an AVI.  It is significantly slower, however.  We place the flag here
    // so we can set it if something dreadfully wrong is with the AVI.

    bool bAggressive = false;

    // begin parsing chunks
    mbPaletteChangesDetected = false;

    sint64    infoChunkEnd = 0;
    sint64    fileSize = mpCurrentFile->mFile.size();

    while(ReadChunkHeader(fccType, dwLength)) {

//        _RPT4(0,"%08I64x %08I64x Chunk '%-4s', length %08lx\n", _posFile()+dwLengthLeft, _posFile(), &fccType, dwLength);

        if (!isValidFOURCC(fccType))
            break;

        if (fccType == FOURCC_LIST) {
             mpCurrentFile->mFile.read(&fccType, 4);

            // If we find a LIST/movi chunk with zero size, jump straight to reindexing
            // (unclosed AVIFile output).

            if (!dwLength && fccType == 'ivom') {
                i64ChunkMoviPos =  mpCurrentFile->mFile.tell();
                dwChunkMoviLength = 0xFFFFFFF0;
                goto terminate_scan;
            }

            // Some idiot Premiere plugin is writing AVI files with an invalid
            // size field in the LIST/hdrl chunk.

            if (dwLength<4 && fccType != VDMAKEFOURCC('h', 'd', 'r', 'l'))
                throw MyError("Invalid AVI file: LIST chunk <4 bytes");

            if (dwLength < 4)
                dwLength = 0;
            else
                dwLength -= 4;

            switch(fccType) {
            case 'ivom':

                if (dwLength < 8) {
                    i64ChunkMoviPos =  mpCurrentFile->mFile.tell();
                    dwChunkMoviLength = 0xFFFFFFF0;
                    dwLength = 0;
                } else {
                    i64ChunkMoviPos =  mpCurrentFile->mFile.tell();
                    dwChunkMoviLength = dwLength;
                }

                if (fAcceptIndexOnly)
                    goto terminate_scan;

                break;
            case ' cer':                // silently enter grouping blocks
            case VDMAKEFOURCC('h', 'd', 'r', 'l'):        // silently enter header blocks
                dwLength = 0;
                break;
            case VDMAKEFOURCC('s', 't', 'r', 'l'):
                if (!_parseStreamHeader(streamlist, dwLength, bScanRequired))
                    fAcceptIndexOnly = false;
                else {
                    int s = streams;
                    //VDLogAppMessage(kVDLogInfo, kVDST_AVIReadHandler, kVDM_OpenDMLIndexDetected, 1, &s);
                    hyperindexed = true;
                }

                ++streams;
                dwLength = 0;
                break;
            case 'OFNI':
                infoChunkEnd = mpCurrentFile->mFile.tell() + dwLength;
                dwLength = 0;
                break;
            }
        } else {
            // Check if the chunk extends outside of the boundaries, and bail if so. We don't do
            // this for LIST chunks because those are often done incorrectly and we don't actually
            // maintain a nesting stack anyway.
            if (mpCurrentFile->mFile.tell() + dwLength > fileSize)
                break;

            switch(fccType) {
            case VDMAKEFOURCC('i', 'd', 'x', '1'):
                if (!hyperindexed) {
                    index_found = _parseIndexBlock(streamlist, dwLength/16, i64ChunkMoviPos);
                    dwLength &= 15;
                }
                break;

            case VDMAKEFOURCC('J', 'U', 'N', 'K'):
                break;

            case 'mges':            // VirtualDub segment hint block
                delete pSegmentHint;
                if (!(pSegmentHint = new char[dwLength]))
                    throw MyMemoryError();

                mpCurrentFile->mFile.read(pSegmentHint, dwLength);

                if (dwLength&1)
                    mpCurrentFile->mFile.skip(1);

                dwLength = 0;
                break;

            case 'lrdh':
                if (!bMainAVIHeaderFound) {
                    uint32 tc = std::min<uint32>(dwLength, sizeof avihdr);
                    memset(&avihdr, 0, sizeof avihdr);
                    mpCurrentFile->mFile.read(&avihdr, tc);
                    dwLength -= tc;
                    bMainAVIHeaderFound = true;
                }
                break;

            case 'TESC':        // CSET (character set)
                if (dwLength >= 8) {
                    struct {
                        uint16    wCodePage;
                        uint16    wCountryCode;
                        uint16    wLanguageCode;
                        uint16    wDialect;
                    } csetData;

                    mpCurrentFile->mFile.read(&csetData, 8);
                    dwLength -= 8;

                    mTextInfoCodePage        = csetData.wCodePage;
                    mTextInfoCountryCode    = csetData.wCountryCode;
                    mTextInfoLanguage        = csetData.wLanguageCode;
                    mTextInfoDialect        = csetData.wDialect;
                }
                break;

            default:
                if (infoChunkEnd && (mpCurrentFile->mFile.tell() < infoChunkEnd)) {
                    switch(fccType) {
                    case 'LRAI':
                    case 'TRAI':
                    case 'SMCI':
                    case 'TMCI':
                    case 'POCI':
                    case 'DRCI':
                    case 'PRCI':
                    case 'MIDI':
                    case 'IPDI':
                    case 'GNEI':
                    case 'RNGI':
                    case 'YEKI':
                    case 'TGLI':
                    case 'DEMI':
                    case 'MANI':
                    case 'TLPI':
                    case 'DRPI':
                    case 'JBSI':
                    case 'TFSI':
                    case 'PHSI':
                    case 'CRSI':
                    case 'FRSI':
                    case 'HCTI':
                        uint32 tc = dwLength;
                        if (tc > 4096) {
                            char fccstr[5]={0};
                            *(uint32 *)fccstr = fccType;
                            const char *fccptr = fccstr;
                            sint64 pos = mpCurrentFile->mFile.tell();
                            unsigned len = dwLength;

                            //VDLogAppMessage(kVDLogWarning, kVDST_AVIReadHandler, kVDM_InfoTruncated, 3, &fccptr, &pos, &len);
                            tc = 4096;
                        }

                        vdblock<char> data((tc + 1) & ~1);

                        dwLength -= tc;
                        mpCurrentFile->mFile.read(data.data(), data.size());

                        int len = tc;

                        while(len > 0 && !data[len-1])
                            --len;

                        if (len > 0)
                            mTextInfo.push_back(tTextInfo::value_type(fccType, VDStringA(data.data(), len)));

                        break;
                    }
                }
                break;
            }
        }

        if (dwLength) {
            if (!mpCurrentFile->mFile.skipNT(dwLength + (dwLength&1)))
                break;
        }
    }

    if (i64ChunkMoviPos == 0)
        throw MyError("Invalid AVI file: The main 'movi' block is missing.");

terminate_scan:

    if (!hyperindexed && !index_found)
        bScanRequired = true;

    if (bScanRequired) {
        //VDLogAppMessage(kVDLogWarning, kVDST_AVIReadHandler, kVDM_IndexMissing);

        // It's possible that we were in the middle of reading an index when an error
        // occurred, so we need to clear all of the indices for all streams.

        pasn = streamlist.AtHead();

        while(pasn_next = pasn->NextFromHead()) {
            pasn->mIndex.Clear();
            pasn = pasn_next;
        }

        // obtain length of file and limit scanning if so
        uint32 dwLengthLeft = dwChunkMoviLength;

        long short_length = (long)((dwChunkMoviLength + 1023i64) >> 10);
        long long_length = (long)((fileSize - i64ChunkMoviPos + 1023i64) >> 10);

        // fix short length if the movi length happens to be beyond the end of the file
        if (short_length > long_length)
            short_length = long_length;

        long length = (hyperindexed || bAggressive) ? long_length : short_length;

        fFakeIndex = true;

        mpCurrentFile->mFile.seek(i64ChunkMoviPos);

        // For standard AVI files, stop as soon as the movi chunk is exhausted or
        // the end of the AVI file is hit.  For OpenDML files, continue as long as
        // valid chunks are found.

        bool bStopWhenLengthExhausted = !hyperindexed && !bAggressive;

        try {
            for(;;) {
                // Exit if we are out of movi chunk -- except for OpenDML files.

                if (!bStopWhenLengthExhausted && dwLengthLeft < 8)
                    break;

                // Validate the FOURCC itself but avoid validating the size, since it
                // may be too large for the last LIST/movi.
                if (!ReadChunkHeader(fccType, dwLength))
                    break;

                bool bValid = isValidFOURCC(fccType) && ((mpCurrentFile->mFile.tell() + dwLength) <= fileSize);

                // In aggressive mode, verify that a valid FOURCC follows this chunk.

                if (bValid && bAggressive) {
                    sint64 current_pos = mpCurrentFile->mFile.tell();
                    sint64 rounded_length = (dwLength+1i64) & ~1i64;

                    if (current_pos + dwLength > fileSize)
                        bValid = false;
                    else if (current_pos + rounded_length <= fileSize-8) {
                        uint32 fccNext;
                        uint32 dwLengthNext;

                        mpCurrentFile->mFile.seek(current_pos + rounded_length);
                        if (!ReadChunkHeader(fccNext, dwLengthNext))
                            break;

                        bValid &= isValidFOURCC(fccNext) && ((mpCurrentFile->mFile.tell() + dwLengthNext) <= fileSize);

                        mpCurrentFile->mFile.seek(current_pos);
                    }
                }

                if (!bValid) {
                    // Notify the user that recovering this file requires stronger measures.

                    if (!bAggressive) {
                        sint64 bad_pos = mpCurrentFile->mFile.tell();
                        //VDLogAppMessage(kVDLogWarning, kVDST_AVIReadHandler, kVDM_InvalidChunkDetected, 1, &bad_pos);

                        bAggressive = true;
                        bStopWhenLengthExhausted = false;
                    }

                    // Backup by up to seven bytes and continue.
                    //
                    // We are looking for a contiguous sequence of four valid bytes starting at position 1.

                    union {
                        uint32 i[2];
                        unsigned char b[8];
                    } conv = { { VDToLE32(fccType), VDToLE32(dwLength) } };

                    uint32 validBits = 0xF00;
                    for(int i=1; i<8; ++i) {
                        if (isValidFOURCCChar(conv.b[i]))
                            validBits |= (1 << i);
                    }

                    validBits &= (validBits >> 1);
                    validBits &= (validBits >> 2);

                    int invalidBytes = VDFindLowestSetBit(validBits);

                    mpCurrentFile->mFile.skip(invalidBytes-8);
                    continue;
                }

                dwLengthLeft -= 8+(dwLength + (dwLength&1));

                // Skip over the chunk.  Don't skip over RIFF chunks of type AVIX, or
                // LIST chunks of type 'movi'.

                if (dwLength) {
                    if (fccType == 'FFIR' || fccType == 'TSIL') {
                        uint32 fccType2;

                        if (4 != mpCurrentFile->mFile.readData(&fccType2, 4))
                            break;

                        if (fccType2 != 'XIVA' && fccType2 != 'ivom' && fccType2 != ' cer') {
                            if (!mpCurrentFile->mFile.skipNT(dwLength + (dwLength&1) - 4))
                                break;
                        }
                    } else {
                        if (!mpCurrentFile->mFile.skipNT(dwLength + (dwLength&1)))
                            break;
                    }
                }

                if (mpCurrentFile->mFile.tell() > fileSize)
                    break;


                // TODO: This isn't necessarily correct for OpenDML, for which the MS parser accepts
                // non-sequential IDs (according to alexnoe)
                if (isxdigit(fccType&0xff) && isxdigit((fccType>>8)&0xff)) {
                    if (is_palette_change(fccType)) {
                        mbPaletteChangesDetected = true;
                    } else {
                        int stream = VDAVIGetStreamFromFOURCC(fccType);

                        if (stream >=0 && stream < streams) {

                            pasn = streamlist.AtHead();

                            while((pasn_next = pasn->NextFromHead()) && stream--)
                                pasn = pasn_next;

                            if (pasn && pasn_next) {

                                // Set the keyframe flag for the first sample in the stream, or
                                // if this is known to be a keyframe-only stream.  Do not set the
                                // keyframe flag if the frame has zero bytes (drop frame).
                                uint32 sizeAndKey = dwLength;

                                if ((!pasn->bytes || pasn->keyframe_only) && dwLength>0)
                                    sizeAndKey |= 0x80000000;

                                pasn->mIndex.AddChunk(mpCurrentFile->mFile.tell()-(dwLength + (dwLength&1)), sizeAndKey);
                                pasn->bytes += dwLength;
                            }
                        }
                    }
                }
            }
        } catch(const MyUserAbortError&) {
            sint64 pos = mpCurrentFile->mFile.tell();
            //VDLogAppMessage(kVDLogInfo, kVDST_AVIReadHandler, kVDM_IndexingAborted, 1, &pos);
            throw;
        }
    }

    if (mbPaletteChangesDetected) {
        //VDLogAppMessage(kVDLogWarning, kVDST_AVIReadHandler, kVDM_PaletteChanges);
    }

    mbFileIsDamaged |= bAggressive;

    // glue together indices

    pasn = streamlist.AtHead();

    int nStream = 0;
    while(pasn_next = pasn->NextFromHead()) {
        pasn->mIndex.Finalize();
        pasn->is_VBR = pasn->mIndex.IsVBR();

        pasn->frames = pasn->mIndex.GetChunkCount();

        // Attempt to fix invalid dwRate/dwScale fractions (can result from unclosed
        // AVI files being written by DirectShow).

        if (pasn->hdr.dwRate==0 || pasn->hdr.dwScale == 0) {
            // If we're dealing with a video stream, try the frame rate in the AVI header.
            // If we're dealing with an audio stream, try the frame rate in the audio
            // format.
            // Otherwise, just use... uh, 15.

            if (pasn->hdr.fccType == kAVIStreamTypeVideo) {
                if (bMainAVIHeaderFound) {
                    pasn->hdr.dwRate    = avihdr.dwMicroSecPerFrame;        // This can be zero, in which case the default '15' will kick in below.
                    pasn->hdr.dwScale    = 1000000;
                }
            } else if (pasn->hdr.fccType == kAVIStreamTypeAudio) {
                const WAVEFORMATEX *pwfex = (const WAVEFORMATEX *)pasn->pFormat;

                pasn->hdr.dwRate    = pwfex->nAvgBytesPerSec;
                pasn->hdr.dwScale    = pwfex->nBlockAlign;
            }

            if (pasn->hdr.dwRate==0 || pasn->hdr.dwScale == 0) {
                pasn->hdr.dwRate = 15;
                pasn->hdr.dwScale = 1;
            }

            const int badstream = nStream;
            const double newrate = pasn->hdr.dwRate / (double)pasn->hdr.dwScale;
            //VDLogAppMessage(kVDLogWarning, kVDST_AVIReadHandler, kVDM_FixingBadSampleRate, 2, &badstream, &newrate);
        }

        // Check and warn on non-zero dwStart values.
        if (pasn->hdr.dwStart) {
            const char *badstreamtype = "unknown";
            switch(pasn->hdr.fccType) {
            case kAVIStreamTypeAudio:    badstreamtype = "audio"; break;
            case kAVIStreamTypeVideo:    badstreamtype = "video"; break;
            case kAVIStreamTypeDV:        badstreamtype = "DV"; break;
            }

            unsigned badstream = nStream;
            uint32 offsetInSamples = pasn->hdr.dwStart;
            sint64 offsetInTime = VDMulDiv64(pasn->hdr.dwStart, pasn->hdr.dwScale * VD64(1000), pasn->hdr.dwRate);

            //VDLogAppMessage(
            //    kVDLogInfo,
            //    kVDST_AVIReadHandler, kVDM_NonZeroStart, 4, &badstream, &badstreamtype, &offsetInSamples, &offsetInTime);
        }

        pasn->length = pasn->mIndex.GetSampleCount();

        pasn = pasn_next;
        ++nStream;
    }

//    throw MyError("Parse complete.  Aborting.");
}

bool AVIReadHandler::_parseStreamHeader(List2<AVIStreamNode>& streamlist, uint32 dwLengthLeft, bool& bIndexDamaged) {
    vdautoptr<AVIStreamNode> pasn(new_nothrow AVIStreamNode());
    uint32 fccType;
    uint32 dwLength;
    bool hyperindexed = false;

    if (!pasn)
        throw MyMemoryError();

    sint64 extendedIndexPos = -1;

    while(dwLengthLeft >= 8 && ReadChunkHeader(fccType, dwLength)) {

        dwLengthLeft -= 8;

        if (dwLength > dwLengthLeft)
            throw MyError("Invalid AVI file: chunk size extends outside of parent");

        dwLengthLeft -= (dwLength + (dwLength&1));

        switch(fccType) {

        case VDMAKEFOURCC('s', 't', 'r', 'h'):
            memset(&pasn->hdr, 0, sizeof pasn->hdr);

            if (dwLength < sizeof pasn->hdr) {
                mpCurrentFile->mFile.read(&pasn->hdr, dwLength);
                if (dwLength & 1)
                    mpCurrentFile->mFile.skip(1);
            } else {
                mpCurrentFile->mFile.read(&pasn->hdr, sizeof pasn->hdr);
                mpCurrentFile->mFile.skip(dwLength+(dwLength&1) - sizeof pasn->hdr);
            }
            dwLength = 0;

            pasn->keyframe_only = false;

            // Clear sample size for video streams!
            if (pasn->hdr.fccType == kAVIStreamTypeVideo)
                pasn->hdr.dwSampleSize=0;

            break;

        case VDMAKEFOURCC('s', 't', 'r', 'f'):
            if (!(pasn->pFormat = new char[pasn->lFormatLen = dwLength]))
                throw MyMemoryError();

            mpCurrentFile->mFile.read(pasn->pFormat, dwLength);

            if (pasn->hdr.fccType == kAVIStreamTypeDV) {
                pasn->keyframe_only = true;
            } else if (pasn->hdr.fccType == kAVIStreamTypeVideo) {
                switch(((BITMAPINFOHEADER *)pasn->pFormat)->biCompression) {
                    case NULL:
                    case ' WAR':
                    case ' BID':
                    case '1bmd':
                    case 'gpjm':
                    case 'GPJM':
                    case 'YUYV':
                    case '2YUY':
                    case 'YVYU':
                    case 'UYVY':
                    case '21VY':
                    case '024I':
                    case 'P14Y':
                    case 'vuyc':
                    case 'UYFH':
                    case '02tb':
                    case 'dsvd':
                        pasn->keyframe_only = true;
                        break;
                }
            }

            if (dwLength & 1)
                mpCurrentFile->mFile.skip(1);
            dwLength = 0;
            break;

        case 'xdni':            // OpenDML extended index
            extendedIndexPos = mpCurrentFile->mFile.tell();
            break;

        case VDMAKEFOURCC('J', 'U', 'N', 'K'):    // JUNK
            break;
        }

        if (dwLength) {
            if (!mpCurrentFile->mFile.skipNT(dwLength + (dwLength&1)))
                break;
        }
    }

    if (dwLengthLeft)
        mpCurrentFile->mFile.skipNT(dwLengthLeft);

    uint32 sampsize = pasn->hdr.dwSampleSize;
    if (pasn->hdr.fccType == kAVIStreamTypeAudio) {
        sampsize = ((const WAVEFORMATEX *)pasn->pFormat)->nBlockAlign;

        if (!sampsize)
            sampsize = 1;
    }

    pasn->mIndex.Init(sampsize);

    if (extendedIndexPos >= 0) {
        try {
            _parseExtendedIndexBlock(streamlist, pasn, extendedIndexPos, dwLength);
        } catch(const MyError&) {
            bIndexDamaged = true;
        }

        hyperindexed = true;
    }

    streamlist.AddTail(pasn.release());

    return hyperindexed;
}

bool AVIReadHandler::_parseIndexBlock(List2<AVIStreamNode>& streamlist, int count, sint64 movi_offset) {
    enum { kIndicesPerLoop = 4096};
    AVIIndexEntry avie[kIndicesPerLoop];        // Note: 64K
    AVIStreamNode *pasn, *pasn_next;
    bool absolute_addr = true;
    bool first_chunk = true;

    // Some AVI files have relative addresses in their AVI index chunks, and some
    // absolute.  They're supposed to be relative to the 'movi' chunk; all versions
    // of VirtualDub using fast write routines prior to build 4936 generate absolute
    // addresses (oops). AVIFile and ActiveMovie are both ambivalent.  I guess we'd
    // better be as well.

    while(count > 0) {
        int tc = count;
        int i;

        if (tc > kIndicesPerLoop)
            tc = kIndicesPerLoop;
        count -= tc;

        if (tc*sizeof(AVIIndexEntry) != (size_t)mpCurrentFile->mFile.readData(avie, tc*sizeof(AVIIndexEntry))) {
            pasn = streamlist.AtHead();

            while(pasn_next = pasn->NextFromHead()) {
                pasn->mIndex.Clear();
                pasn->bytes = 0;

                pasn = pasn_next;
            }
            return false;
        }

        if (first_chunk) {
            first_chunk = false;

            // If the chunk offset is prior to the 'movi' chunk, then we know that we have to
            // use relative addressing.
            uint32 chunk_offset = avie[0].dwChunkOffset;
            if (chunk_offset < movi_offset)
                absolute_addr = false;
            else {
                // Okay, both relative and absolute are potentially good. We try the lower one
                // (absolute) and see if the chunk at that location matches in FOURCC (we do
                // NOT check the length as some interleavers play tricks with that). If not,
                // then we use relative positioning, which is what should be used anyway.

                sint64 savepos = mpCurrentFile->mFile.tell();

                mpCurrentFile->mFile.seek(chunk_offset);

                uint32 fcc;
                mpCurrentFile->mFile.read(&fcc, sizeof fcc);

                if (fcc != avie[0].ckid)
                    absolute_addr = false;

                mpCurrentFile->mFile.seek(savepos);
            }
        }

        for(i=0; i<tc; i++) {
            const AVIIndexEntry& idxent = avie[i];
            int stream = VDAVIGetStreamFromFOURCC(idxent.ckid);

            if (is_palette_change(idxent.ckid)) {
                mbPaletteChangesDetected = true;
                continue;
            }

            pasn = streamlist.AtHead();

            while((pasn_next = (AVIStreamNode *)pasn->NextFromHead()) && stream--)
                pasn = pasn_next;

            if (pasn && pasn_next) {
                // It's fairly important that we not add entries with zero bytes as
                // key frames, as these have been seen in the wild. AVIFile silently
                // kills the key frame flag for these.
                sint64 bytePos = (sint64)idxent.dwChunkOffset + 8;
                if (!absolute_addr)
                    bytePos += movi_offset - 4;

                uint32 sizeAndKey = idxent.dwChunkLength;
                if ((idxent.dwFlags & AVIIndexEntry::kFlagKeyFrame) && idxent.dwChunkLength > 0)
                    sizeAndKey |= 0x80000000;

                pasn->mIndex.AddChunk(bytePos, sizeAndKey);

                pasn->bytes += idxent.dwChunkLength;
            }
        }
    }

    return true;

}

void AVIReadHandler::_parseExtendedIndexBlock(List2<AVIStreamNode>& streamlist, AVIStreamNode *pasn, sint64 fpos, uint32 dwLength) {
#pragma warning(push)
#pragma warning(disable: 4815)        // warning C4815: '$S1' : zero-sized array in stack object will have no elements (unless the object is an aggregate that has been aggregate initialized)
    union {
        AVISUPERINDEX idxsuper;
        AVISTDINDEX idxstd;
    };
#pragma warning(pop)

    int entries, tp;
    int i;
    sint64 i64FPSave = mpCurrentFile->mFile.tell();

    if (fpos>=0)
        mpCurrentFile->mFile.seek(fpos);

    try {
        mpCurrentFile->mFile.read((char *)&idxsuper + 8, sizeof(AVISUPERINDEX) - 8);

        switch(idxsuper.bIndexType) {
        case AVI_INDEX_OF_INDEXES:
            {
                // sanity check

                if (idxsuper.wLongsPerEntry != 4)
                    throw MyError("Invalid superindex block in stream");

                // Compute buffer size -- the smaller of the number needed or 64K (16K dwords).
                uint32 maxcount = 65536 / sizeof(_avisuperindex_entry);

                entries = idxsuper.nEntriesInUse;
                if (maxcount > entries)
                    maxcount = entries;

                vdblock<_avisuperindex_entry> buf(maxcount);
                _avisuperindex_entry *heap = buf.data();

                while(entries > 0) {
                    tp = maxcount;
                    if (tp > maxcount)
                        tp = maxcount;

                    mpCurrentFile->mFile.read(heap, tp * sizeof(heap[0]));

                    for(i=0; i<tp; i++)
                        _parseExtendedIndexBlock(streamlist, pasn, heap[i].qwOffset+8, heap[i].dwSize-8);

                    entries -= tp;
                }
            }
            break;

        case AVI_INDEX_OF_CHUNKS:

    //        if (idxstd.bIndexSubType != 0)
    //            throw MyError("Frame indexes not supported");

            entries = idxstd.nEntriesInUse;

            // In theory, if bIndexSubType==AVI_INDEX_2FIELD it's supposed to have
            // wLongsPerEntry=3, and bIndexSubType==0 gives wLongsPerEntry=2.
            // Matrox's MPEG-2 stuff generates bIndexSubType=16 and wLongsPerEntry=6.
            // *sigh*
            //
            // For wLongsPerEntry==2 and ==3, dwOffset is at 0 and dwLength at 1;
            // for wLongsPerEntry==6, dwOffset is at 2 and all are keyframes.

            if (entries) {
                if (idxstd.wLongsPerEntry!=2 && idxstd.wLongsPerEntry!=3 && idxstd.wLongsPerEntry!=6)
                    throw MyError("Invalid OpenDML index block in stream (wLongsPerEntry=%d)", idxstd.wLongsPerEntry);

                // Compute buffer size -- the smaller of the number needed or 64K (16K dwords).
                uint32 maxcount = 16384 / idxstd.wLongsPerEntry;

                if (maxcount > entries)
                    maxcount = entries;

                vdblock<uint32> buf(maxcount * idxstd.wLongsPerEntry);
                uint32 *heap = buf.data();

                while(entries > 0) {
                    tp = maxcount;
                    if (tp > entries)
                        tp = entries;

                    mpCurrentFile->mFile.read(heap, tp*idxstd.wLongsPerEntry*sizeof(uint32));

                    if (idxstd.wLongsPerEntry == 6)
                        for(i=0; i<tp; i++) {
                            uint32 dwOffset = heap[i*idxstd.wLongsPerEntry + 0];
                            uint32 dwSize = heap[i*idxstd.wLongsPerEntry + 2];

                            pasn->mIndex.AddChunk(idxstd.qwBaseOffset+dwOffset, dwSize | 0x80000000);

                            pasn->bytes += dwSize;
                        }
                    else
                        for(i=0; i<tp; i++) {
                            uint32 dwOffset = heap[i*idxstd.wLongsPerEntry + 0];
                            uint32 dwSize = heap[i*idxstd.wLongsPerEntry + 1];

                            pasn->mIndex.AddChunk(idxstd.qwBaseOffset+dwOffset, dwSize ^ 0x80000000);

                            pasn->bytes += dwSize & 0x7FFFFFFF;
                        }

                    entries -= tp;
                }
            }

            break;

        default:
            throw MyError("Unknown hyperindex type");
        }

        mpCurrentFile->mFile.seek(i64FPSave);
    } catch(const MyError&) {
        mpCurrentFile->mFile.seekNT(i64FPSave);
        throw;
    }
}

void AVIReadHandler::_destruct() {
    AVIStreamNode *pasn;

    while(pasn = listStreams.RemoveTail())
        delete pasn;

    delete streamBuffer;

    while(!mFiles.empty()) {
        AVIFileDesc *desc = mFiles.back();
        mFiles.pop_back();
        delete desc;
    }

    delete pSegmentHint;
}

///////////////////////////////////////////////////////////////////////////

void AVIReadHandler::Release() {
    if (!--mRefCount)
        delete this;
}

void AVIReadHandler::AddRef() {
    ++mRefCount;
}

IAVIReadStream *AVIReadHandler::GetStream(uint32 fccType, int lParam) {
    AVIStreamNode *pasn, *pasn_next;
    int streamno = 0;

    pasn = listStreams.AtHead();

    while(pasn_next = pasn->NextFromHead()) {
        if (pasn->hdr.fccType == fccType && !lParam--)
            break;

        pasn = pasn_next;
        ++streamno;
    }

    if (pasn_next) {
        return new AVIReadStream(this, pasn, streamno);
    }

    return NULL;
}

void AVIReadHandler::EnableFastIO(bool f) {
    fDisableFastIO = !f;
}

bool AVIReadHandler::isOptimizedForRealtime() {
    return nRealTime!=0;
}

bool AVIReadHandler::isStreaming() {
    return nActiveStreamers!=0 && !mbFileIsDamaged;
}

bool AVIReadHandler::isIndexFabricated() {
    return fFakeIndex;
}

bool AVIReadHandler::getSegmentHint(const char **ppszPath) {
    if (!pSegmentHint) {
        if (ppszPath)
            *ppszPath = NULL;

        return false;
    }

    if (ppszPath)
        *ppszPath = pSegmentHint+1;

    return !!pSegmentHint[0];
}

void AVIReadHandler::GetTextInfo(tTextInfo& textInfo) {
    textInfo = mTextInfo;
}

void AVIReadHandler::GetTextInfoEncoding(int& codePage, int& countryCode, int& language, int& dialect) {
    codePage        = mTextInfoCodePage;
    countryCode        = mTextInfoCountryCode;
    language        = mTextInfoLanguage;
    dialect            = mTextInfoDialect;
}

///////////////////////////////////////////////////////////////////////////

void AVIReadHandler::EnableStreaming(int stream) {
    if (!fStreamsActive) {
        if (!(streamBuffer = new char[STREAM_SIZE]))
            throw MyMemoryError();

        i64StreamPosition = -1;
        sbPosition = sbSize = 0;
    }

    fStreamsActive |= (1<<stream);
    ++nActiveStreamers;
}

void AVIReadHandler::DisableStreaming(int stream) {
    fStreamsActive &= ~(1<<stream);

    if (!fStreamsActive) {
        delete streamBuffer;
        streamBuffer = NULL;
    }
    --nActiveStreamers;
}

void AVIReadHandler::AdjustRealTime(bool fInc) {
    if (fInc)
        ++nRealTime;
    else
        --nRealTime;
}

char *AVIReadHandler::_StreamRead(long& bytes) {
    if (mCurrentFile<0 || mCurrentFile != (int)(i64StreamPosition>>48))
        SelectFile((int)(i64StreamPosition>>48));

    if (sbPosition >= sbSize) {
        if (!mpCurrentFile->mFileUnbuffered.isOpen() || nRealTime || (((i64StreamPosition&0x0000FFFFFFFFFFFFi64)+sbSize) & -STREAM_BLOCK_SIZE)+STREAM_SIZE > mpCurrentFile->mFileSize) {
            i64StreamPosition += sbSize;
            sbPosition = 0;
            mpCurrentFile->mFile.seek(i64StreamPosition & 0x0000FFFFFFFFFFFFi64);

            sbSize = mpCurrentFile->mFile.readData(streamBuffer, STREAM_RT_SIZE);

            if (sbSize < 0) {
                sbSize = 0;
                throw MyWin32Error("Failure streaming AVI file: %%s.",GetLastError());
            }
        } else {
            i64StreamPosition += sbSize;
            sbPosition = (int)i64StreamPosition & (STREAM_BLOCK_SIZE-1);
            i64StreamPosition &= -STREAM_BLOCK_SIZE;
            mpCurrentFile->mFileUnbuffered.seek(i64StreamPosition & 0x0000FFFFFFFFFFFFi64);

            sbSize = mpCurrentFile->mFileUnbuffered.readData(streamBuffer, STREAM_SIZE);
            uint32 error = GetLastError();

            if (sbSize < 0) {
                sbSize = 0;
                throw MyWin32Error("Failure streaming AVI file: %%s.", error);
            }
        }
    }

    if (sbPosition >= sbSize)
        return NULL;

    if (bytes > sbSize - sbPosition)
        bytes = sbSize - sbPosition;

    sbPosition += bytes;

    return streamBuffer + sbPosition - bytes;
}

bool AVIReadHandler::Stream(AVIStreamNode *pusher, sint64 pos) {

    // Do not stream aggressively recovered files.

    if (mbFileIsDamaged)
        return false;

    bool read_something = false;

    if (!streamBuffer)
        return false;

    if (i64StreamPosition == -1) {
        i64StreamPosition = pos;
        sbPosition = 0;
    }

    if (pos < i64StreamPosition+sbPosition)
        return false;

    // >4Mb past current position!?

    if (pos > i64StreamPosition+sbPosition+4194304) {
//        OutputDebugString("Resetting streaming position!\n");
        i64StreamPosition = pos;
        sbSize = sbPosition = 0;
    }

/*    if (pusher->hdr.fccType == 'sdiv')
        OutputDebugString("pushed by video\n");
    else
        OutputDebugString("pushed by audio\n");*/

    ++pusher->stream_pushes;
    pusher->stream_push_pos = pos;

    while(pos >= i64StreamPosition+sbPosition) {
        long actual, left;
        char *src;
        uint32 hdr[2];
        int stream;

        // read next header

        left = 8;
        while(left > 0) {
            actual = left;
            src = _StreamRead(actual);

            if (!src)
                return read_something;

            memcpy((char *)hdr + (8-left), src, actual);
            left -= actual;
        }

        stream = VDAVIGetStreamFromFOURCC(hdr[0]);

        if (isxdigit(hdr[0]&0xff) && isxdigit((hdr[0]>>8)&0xff) && stream<32 &&
            ((1L<<stream) & fStreamsActive)) {

//            _RPT3(0,"\tstream: reading chunk at %I64x, length %6ld, stream %ld\n", i64StreamPosition+sbPosition-8, hdr[1], stream);

            AVIStreamNode *pasn, *pasn_next;
            int streamno = 0;

            pasn = listStreams.AtHead();

            while(pasn_next = pasn->NextFromHead()) {
                if (streamno == stream) {
                    unsigned chunk_size = hdr[1] + (hdr[1]&1);

                    if (chunk_size >= 0x7ffffff0) {
                        // Uh oh... assume the file has been damaged.  Disable streaming.
                        sint64 bad_pos = i64StreamPosition+sbPosition-8;

                        //VDLogAppMessage(kVDLogInfo, kVDST_AVIReadHandler, kVDM_StreamFailure, 1, &bad_pos);
                        mbFileIsDamaged = true;
                        i64StreamPosition = -1;
                        sbPosition = sbSize = 0;
                        return false;
                    }

                    long left = chunk_size;
                    bool fWrite = pasn->cache->WriteBegin(i64StreamPosition + sbPosition, left);
                    char *dst;

                    while(left > 0) {
                        actual = left;

                        dst = _StreamRead(actual);

                        if (!dst) {
                            if (fWrite)
                                pasn->cache->WriteEnd();
                            return read_something;
                        }

                        if (fWrite)
                            pasn->cache->Write(dst, actual);

                        left -= actual;
                    }

                    if (fWrite)
                        pasn->cache->WriteEnd();

                    read_something = true;

                    break;
                }

                pasn = pasn_next;
                ++streamno;
            }
        } else {

            if (hdr[0] != FOURCC_LIST && hdr[0] != FOURCC_RIFF) {
                long actual;

                // skip chunk

                unsigned chunk_size = hdr[1] + (hdr[1] & 1);

                if (chunk_size >= 0x7ffffff0) {
                    mbFileIsDamaged = true;
                    i64StreamPosition = -1;
                    sbPosition = sbSize = 0;
                    return false;
                }

                // Determine if the chunk is overly large.  If the chunk is too large, don't
                // stream through it.

                if (chunk_size > 262144) {
                    // Force resynchronization on next read.
                    i64StreamPosition += chunk_size;
                    sbPosition = sbSize = 0;
                    return read_something;
                }

                left = chunk_size;

                while(left > 0) {
                    actual = left;

                    if (!_StreamRead(actual))
                        return read_something;

                    left -= actual;
                }
            } else {
                left = 4;

                while(left > 0) {
                    actual = left;

                    if (!_StreamRead(actual))
                        return read_something;

                    left -= actual;
                }
            }

        }
    }

    return true;
}

sint64 AVIReadHandler::getStreamPtr() {
    return i64StreamPosition + sbPosition;
}

void AVIReadHandler::FixCacheProblems(AVIReadStream *arse) {
    AVIStreamNode *pasn, *pasn_next;

    // The simplest fix is simply to disable caching on the stream that's
    // cache-missing.  However, this is a bad idea if the problem is a low-bandwidth
    // audio stream that's outrunning a high-bandwidth video stream behind it.
    // Check the streaming leader, and if the streaming leader is comparatively
    // low bandwidth and running at least 512K ahead of the cache-missing stream,
    // disable its cache.

    AVIStreamNode *stream_leader = NULL;
    int stream_leader_no;
    int streamno=0;

    pasn = listStreams.AtHead();

    while(pasn_next = pasn->NextFromHead()) {
        if (pasn->cache)
            if (!stream_leader || pasn->stream_pushes > stream_leader->stream_pushes) {
                stream_leader = pasn;
                stream_leader_no = streamno;
            }

        pasn = pasn_next;
        ++streamno;
    }

    if (stream_leader && stream_leader->stream_bytes*2 < arse->psnData->stream_bytes
        && stream_leader->stream_push_pos >= arse->psnData->stream_push_pos+524288) {

        VDTRACE_AVISTREAMING("caching disabled on fast puny leader\n");

        delete stream_leader->cache;
        stream_leader->cache = NULL;

        DisableStreaming(stream_leader_no);

        i64StreamPosition = -1;
        sbPosition = sbSize = 0;
    } else {

        VDTRACE_AVISTREAMING("disabling caching at request of client\n");

        arse->EndStreaming();

        if (arse->psnData == stream_leader) {
            i64StreamPosition = -1;
            sbPosition = sbSize = 0;
        }
    }

    // Reset cache statistics on all caches.

    pasn = listStreams.AtHead();

    while(pasn_next = pasn->NextFromHead()) {
        if (pasn->cache)
            pasn->cache->ResetStatistics();

        pasn = pasn_next;
    }
}

long AVIReadHandler::ReadData(int stream, void *buffer, sint64 position, long len) {
    if (mCurrentFile<0 || mCurrentFile != (int)(position>>48))
        SelectFile((int)(position>>48));

//    _RPT3(0,"Reading from file %d, position %I64x, size %d\n", nCurrentFile, position, len);

    if (!mpCurrentFile->mFile.seekNT(position & 0x0000FFFFFFFFFFFFi64))
        return -1;
    return mpCurrentFile->mFile.readData(buffer, len);
}

void AVIReadHandler::SelectFile(int file) {
    mCurrentFile = file;
    mpCurrentFile = mFiles[file];
}

bool AVIReadHandler::ReadChunkHeader(uint32& type, uint32& size) {
    uint32 buf[2];

    if (mpCurrentFile->mFile.readData(buf, 8) < 8)
        return false;

    type = VDFromLE32(buf[0]);
    size = VDFromLE32(buf[1]);
    return true;
}
