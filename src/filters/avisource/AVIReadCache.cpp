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
#include "AVIReadCache.h"

//#define VDTRACE_AVIREADCACHE VDDEBUG
#define VDTRACE_AVIREADCACHE (void)sizeof

AVIReadCache::AVIReadCache(int nlines, int nstream, IAVIReadCacheSource *root, AVIStreamNode *psnData)
    : mBuffer(nlines * 16)
    , mSize(nlines*16)
    , mFree(nlines*16)
    , mWritePos(0)
{
    this->psnData    = psnData;
    stream        = nstream;
    source        = root;
    ResetStatistics();
}

AVIReadCache::~AVIReadCache() {
}

void AVIReadCache::ResetStatistics() {
    reads        = 0;
    cache_hit_bytes    = cache_miss_bytes = 0;
}

bool AVIReadCache::WriteBegin(sint64 pos, uint32 len) {
    int needed;

    // delete lines as necessary to make room

    needed = (len+7) & ~7;

    if (needed > mSize)
        return false;

    while(mFree < needed) {
        VDASSERT(!mActiveIndices.empty());

        IndexBlock& idxblock = mActiveIndices.front();

        VDASSERT(idxblock.mHead != idxblock.mTail);

        for(;;) {
            mFree += (idxblock.mBlocks[idxblock.mHead++].len + 7) & ~7;

            if (idxblock.mHead == idxblock.mTail)
                break;

            if (mFree >= needed)
                goto have_space;
        }

        mFreeIndices.splice(mFreeIndices.begin(), mActiveIndices, mActiveIndices.begin());
    }

have_space:

    // write in header
    if (mActiveIndices.empty() || mActiveIndices.back().mTail >= IndexBlock::kBlocksPerIndex) {
        if (mFreeIndices.empty()) {
            mActiveIndices.push_back(IndexBlock());
        } else
            mActiveIndices.splice(mActiveIndices.end(), mFreeIndices, mFreeIndices.begin());
        mActiveIndices.back().mHead = 0;
        mActiveIndices.back().mTail = 0;
    }

    IndexBlock& writeblock = mActiveIndices.back();
    IndexBlockEntry& writeent = writeblock.mBlocks[writeblock.mTail++];

    writeent.pos    = pos;
    writeent.len    = len;
    writeent.start    = mWritePos;

    mFree -= (len + 7) & ~7;

    return true;
}

#pragma function(memcpy)

void AVIReadCache::Write(const void *src, uint32 len) {
    // copy in data
    if (mWritePos + len > mSize) {        // split write
        uint32 fraction = mSize - mWritePos;

        memcpy(&mBuffer[mWritePos], src, fraction);
        memcpy(&mBuffer.front(), (const char *)src + fraction, len - fraction);
        mWritePos = len - fraction;
    } else {                            // single write
        memcpy(&mBuffer[mWritePos], src, len);

        mWritePos += len;
        if (mWritePos >= mSize)
            mWritePos = 0;
    }
}

void AVIReadCache::WriteEnd() {
    mWritePos = (mWritePos + 7) & ~7;
    if (mWritePos >= mSize)
        mWritePos = 0;
}

long AVIReadCache::Read(void *dest, sint64 chunk_pos, sint64 pos, uint32 len) {
    ++reads;

    do {
        // scan buffer looking for a range that contains data

        for(tIndexBlockList::reverse_iterator it(mActiveIndices.rbegin()), itEnd(mActiveIndices.rend()); it!=itEnd; ++it) {
            const IndexBlock& ib = *it;

            for(int i = ib.mHead; i < ib.mTail; ++i) {
                const IndexBlockEntry& ibe = ib.mBlocks[i];

                if (ibe.pos == pos) {
                    if (len > ibe.len)
                        len = ibe.len;

                    cache_hit_bytes += len;

                    while (cache_hit_bytes > 16777216) {
                        cache_miss_bytes >>= 1;
                        cache_hit_bytes >>= 1;
                    }

                    if (ibe.start + len >= mSize) {            // split read
                        uint32 fraction = mSize - ibe.start;
                        memcpy(dest, &mBuffer[ibe.start], fraction);
                        memcpy((char *)dest + fraction, &mBuffer.front(), len - fraction);
                    } else {                                // single read
                        memcpy(dest, &mBuffer[ibe.start], len);
                    }

                    VDTRACE_AVIREADCACHE("AVIReadCache: cache hit\n");
                    return (long)len;
                }
            }
        }

        if (source->getStreamPtr() > chunk_pos)
            break;

    } while(source->Stream(psnData, chunk_pos));

    VDTRACE_AVIREADCACHE("AVIReadCache: cache miss\n");

    cache_miss_bytes += len;

    while (cache_miss_bytes > 16777216) {
        cache_miss_bytes >>= 1;
        cache_hit_bytes >>= 1;
    }

    return source->ReadData(stream, dest, pos, len);
}
