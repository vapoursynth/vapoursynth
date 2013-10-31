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
#include <vd2/system/math.h>
#include "AVIReadIndex.h"

VDAVIReadIndex::VDAVIReadIndex() {
    Clear();
}

VDAVIReadIndex::~VDAVIReadIndex() {
    Clear();
}

void VDAVIReadIndex::Init(uint32 sampleSize) {
    mSampleSize        = sampleSize;
    Clear();
}

bool VDAVIReadIndex::IsVBR() const {
    return mbVBR;
}

sint64 VDAVIReadIndex::GetSampleCount() const {
    return mSampleCount;
}

uint32 VDAVIReadIndex::GetChunkCount() const {
    return mChunkCount;
}

sint64 VDAVIReadIndex::GetByteCount() const {
    return mByteCount;
}

bool VDAVIReadIndex::IsKey(sint64 samplePos) const {
    if ((uint64)samplePos >= (uint64)mSampleCount)
        return false;

    uint32 sectorIndex = FindSectorIndexBySample(samplePos);
    uint32 sampleOffset;
    uint32 chunkIndex;
    IndexEntry *ient = FindChunk(samplePos, sectorIndex, sampleOffset, chunkIndex);

    return (sint32)ient->mSizeAndKeyFrameFlag < 0;
}

sint64 VDAVIReadIndex::PrevKey(sint64 samplePos) const {
    if (samplePos <= 0)
        return -1;

    if (samplePos >= mSampleCount)
        return NearestKey(samplePos);

    uint32 sectorIndex = FindSectorIndexBySample(samplePos);
    uint32 sampleOffset;
    uint32 chunkIndex;
    IndexEntry *ient = FindChunk(samplePos, sectorIndex, sampleOffset, chunkIndex);

    if (chunkIndex == 0)
        return -1;

    --chunkIndex;
    for(;;) {
        ient = &mIndex[chunkIndex >> kBlockSizeBits][chunkIndex & kBlockMask];
        if ((sint32)ient->mSizeAndKeyFrameFlag < 0) {
            sectorIndex = FindSectorIndexByChunk(chunkIndex);

            return mSectors[sectorIndex].mChunkOffset + ient->mSampleOffset;
        }

        if (!chunkIndex)
            break;


        chunkIndex -= ient->mPrevKeyDistance;
    }

    return -1;
}

sint64 VDAVIReadIndex::NextKey(sint64 samplePos) const {
    if (samplePos < 0) {
        if (IsKey(0))
            return true;
        samplePos = 0;
    }

    if (samplePos >= mSampleCount)
        return -1;

    uint32 sectorIndex = FindSectorIndexBySample(samplePos);
    uint32 sampleOffset;
    uint32 chunkIndex;
    IndexEntry *ient = FindChunk(samplePos, sectorIndex, sampleOffset, chunkIndex);

    while(++chunkIndex < mChunkCount) {
        ient = &mIndex[chunkIndex >> kBlockSizeBits][chunkIndex & kBlockMask];
        if ((sint32)ient->mSizeAndKeyFrameFlag < 0) {
            sectorIndex = FindSectorIndexByChunk(chunkIndex);

            return mSectors[sectorIndex].mChunkOffset + ient->mSampleOffset;
        }
    }

    return -1;
}

sint64 VDAVIReadIndex::NearestKey(sint64 samplePos) const {
    if (samplePos < 0)
        return -1;

    if (samplePos >= mSampleCount)
        samplePos = mSampleCount - 1;

    uint32 sectorIndex = FindSectorIndexBySample(samplePos);
    uint32 sampleOffset;
    uint32 chunkIndex;
    IndexEntry *ient = FindChunk(samplePos, sectorIndex, sampleOffset, chunkIndex);

    for(;;) {
        if ((sint32)ient->mSizeAndKeyFrameFlag < 0) {
            sectorIndex = FindSectorIndexByChunk(chunkIndex);

            return mSectors[sectorIndex].mChunkOffset + ient->mSampleOffset;
        }

        if (chunkIndex <= 0)
            return -1;

        chunkIndex -= ient->mPrevKeyDistance;
        ient = &mIndex[chunkIndex >> kBlockSizeBits][chunkIndex & kBlockMask];
    }
}

void VDAVIReadIndex::FindSampleRange(VDAVIReadIndexIterator& it, sint64 samplePos, uint32 maxSamples) const {
    uint32 sectorIndex = FindSectorIndexBySample(samplePos);
    uint32 sampleOffset;
    uint32 chunkIndex;
    FindChunk(samplePos, sectorIndex, sampleOffset, chunkIndex);

    it.mSectorIndex        = sectorIndex;
    it.mSectorLimit        = mSectors[it.mSectorIndex + 1].mChunkOffset;
    it.mChunkIndex        = chunkIndex;
    it.mChunkOffset        = sampleOffset * mSampleSize;
}

void VDAVIReadIndex::GetFirstSampleRange(VDAVIReadIndexIterator& it) const {
    it.mSectorIndex        = 0;
    it.mSectorLimit        = mSectors[1].mChunkOffset;
    it.mChunkIndex        = 0;
    it.mChunkOffset        = 0;
}

bool VDAVIReadIndex::GetNextSampleRange(VDAVIReadIndexIterator& it, sint64& chunkPos, uint32& offset, uint32& byteSize) const {
    if (it.mSectorIndex >= mSectorCount)
        return false;

    const SectorEntry& sec = mSectors[it.mSectorIndex];
    const IndexEntry& ient = mIndex[it.mChunkIndex >> kBlockSizeBits][it.mChunkIndex & kBlockMask];

    chunkPos = sec.mByteOffset + ient.mByteOffset;
    offset = it.mChunkOffset;
    byteSize = (ient.mSizeAndKeyFrameFlag & 0x7FFFFFFF) - it.mChunkOffset;

    if (mbVBR && byteSize > mSampleSize) {
        it.mChunkOffset += mSampleSize;
        byteSize = mSampleSize;
        return true;
    }

    it.mChunkOffset = 0;
    if (++it.mChunkIndex >= it.mSectorLimit) {
        ++it.mSectorIndex;

        it.mSectorLimit = mSectors[it.mSectorIndex + 1].mChunkOffset;
    }

    return true;
}

void VDAVIReadIndex::Clear() {
    mbFinalized = false;
    mbVBR        = false;
    mByteCount = 0;
    mSampleCount = 0;
    mChunkCount = 0;
    mBlockOffset = 0;
    mPrevKey = 0;
    mSectors.clear();

    while(!mIndex.empty()) {
        IndexEntry *ient = mIndex.back();
        mIndex.pop_back();

        delete[] ient;
    }

    SectorEntry& sec = mSectors.push_back();
    sec.mByteOffset        = 0;
    sec.mSampleOffset    = 0;
    sec.mChunkOffset    = 0;
    sec.mbOneSamplePerChunk = true;

    mSectorCount = 1;
}

void VDAVIReadIndex::AddChunk(sint64 bytePos, uint32 sizeAndKeyFrameFlag) {
    SectorEntry *sec = &mSectors.back();

    // Note: Some (perhaps broken) AVI files have chunks out of order in the index. In
    // that case, we must force a new sector.

    if ((uint64)(bytePos - sec->mByteOffset) >= (uint64)0x100000000 || mSampleCount - sec->mChunkOffset >= 0x10000) {
        sec = &mSectors.push_back();
        sec->mByteOffset    = bytePos;
        sec->mSampleOffset    = mSampleCount;
        sec->mChunkOffset    = mChunkCount;
        sec->mbOneSamplePerChunk = true;
        ++mSectorCount;
    }

    if (mIndex.empty() || mBlockOffset >= kBlockSize) {
        IndexEntry *newBlock = new IndexEntry[kBlockSize];

        try {
            mIndex.push_back(newBlock);
        } catch(...) {
            delete[] newBlock;
            throw;
        }

        mBlockOffset = 0;
    }

    if ((sint32)sizeAndKeyFrameFlag < 0)
        mPrevKey = mChunkCount;

    IndexEntry& ient = mIndex.back()[mBlockOffset++];

    ient.mByteOffset            = (uint32)bytePos - (uint32)sec->mByteOffset;
    ient.mSampleOffset            = (uint32)mSampleCount - (uint32)sec->mSampleOffset;
    ient.mSizeAndKeyFrameFlag    = sizeAndKeyFrameFlag;
    ient.mPrevKeyDistance        = VDClampToUint16(mChunkCount - mPrevKey);
    ient.mUnused0                = 0;

    VDASSERT(ient.mByteOffset + sec->mByteOffset == bytePos);
    VDASSERT(ient.mSampleOffset + sec->mSampleOffset == mSampleCount);

    uint32 chunkSize = sizeAndKeyFrameFlag & 0x7FFFFFFF;
    if (mSampleSize) {
        uint32 roundedUpSize = chunkSize + (mSampleSize - 1);
        uint32 sampleCountInChunk = roundedUpSize / mSampleSize;

        if (chunkSize % mSampleSize)
            mbVBR = true;

        mSampleCount += sampleCountInChunk;

        if (sampleCountInChunk != 1)
            sec->mbOneSamplePerChunk = false;
    } else
        ++mSampleCount;
    ++mChunkCount;
    mByteCount += chunkSize;
}

void VDAVIReadIndex::Append(const VDAVIReadIndex& src, sint64 bytePosOffset) {
    if (mbFinalized) {
        mSectors.pop_back();
        mSectors.pop_back();
        mbFinalized = false;
    }

    const SectorEntry *sec = &src.mSectors[0];
    uint32 next = sec[1].mChunkOffset;
    for(uint32 i=0; i<src.mChunkCount; ++i) {
        if (i >= next) {
            ++sec;
            next = sec[1].mChunkOffset;
        }

        const IndexEntry& ient = src.mIndex[i >> kBlockSizeBits][i & kBlockMask];

        sint64 bytePos = sec->mByteOffset + bytePosOffset + ient.mByteOffset;
        AddChunk(bytePos, ient.mSizeAndKeyFrameFlag);
    }

    Finalize();
}

void VDAVIReadIndex::Finalize() {
    if (mbFinalized)
        return;

    SectorEntry endSector;
    endSector.mByteOffset    = 0x3FFFFFFFFFFFFFFF;
    endSector.mChunkOffset    = mChunkCount;
    endSector.mSampleOffset    = mSampleCount;
    mSectors.push_back(endSector);
    mSectors.push_back(endSector);

    mbFinalized = true;
}

uint32 VDAVIReadIndex::FindSectorIndexByChunk(uint32 chunk) const {
    uint32 lo = 0;
    uint32 hi = mSectorCount - 1;

    while(lo < hi) {
        uint32 mid = (lo + hi + 1) >> 1;
        const SectorEntry& midSec = mSectors[mid];

        if (midSec.mChunkOffset <= chunk)
            lo = mid;
        else
            hi = mid - 1;
    }

    return lo;
}

uint32 VDAVIReadIndex::FindSectorIndexBySample(sint64 sample) const {
    uint32 lo = 0;
    uint32 hi = mSectorCount - 1;

    while(lo < hi) {
        uint32 mid = (lo + hi + 1) >> 1;
        const SectorEntry& midSec = mSectors[mid];

        if (midSec.mSampleOffset <= sample)
            lo = mid;
        else
            hi = mid - 1;
    }

    return lo;
}

VDAVIReadIndex::IndexEntry *VDAVIReadIndex::FindChunk(sint64 sample, uint32 sectorIndex, uint32& sampleOffsetOut, uint32& index) const {
    const SectorEntry& sec1 = mSectors[sectorIndex];
    const SectorEntry& sec2 = mSectors[sectorIndex + 1];

    uint32 lo = sec1.mChunkOffset;
    uint32 hi = sec2.mChunkOffset - 1;

    uint32 sampleOffset = (uint32)sample - (uint32)sec1.mSampleOffset;

    if (sec1.mbOneSamplePerChunk) {
        index = sec1.mChunkOffset + sampleOffset;
        sampleOffsetOut = 0;
        return &mIndex[index >> kBlockSizeBits][index & kBlockMask];
    }

    IndexEntry *ient = NULL;
    uint32 mid;
    while(lo < hi) {
        mid = (lo + hi + 1) >> 1;
        ient = &mIndex[mid >> kBlockSizeBits][mid & kBlockMask];

        if (ient->mSampleOffset <= sampleOffset)
            lo = mid;
        else
            hi = mid - 1;
    }

    ient = &mIndex[lo >> kBlockSizeBits][lo & kBlockMask];
    sampleOffsetOut = sampleOffset - ient->mSampleOffset;
    index = lo;
    return ient;
}
