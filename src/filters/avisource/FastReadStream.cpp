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

#include <windows.h>
#include <io.h>

#include <vd2/system/error.h>
#include "FastReadStream.h"

class FastReadStreamHeader {
public:
    __int64 i64BlockNo;
    long fAccessedBits;
    long lBytes;
    long lAge;
    long lHistoryVal;
};

FastReadStream::FastReadStream(HANDLE hFile, long lBlockCount, long lBlockSize) {
    this->hFile            = hFile;
    this->iFile            = -1;

    _Init(lBlockCount, lBlockSize);
}

FastReadStream::FastReadStream(int iFile, long lBlockCount, long lBlockSize) {
    this->hFile            = INVALID_HANDLE_VALUE;
    this->iFile            = iFile;
    _Init(lBlockCount, lBlockSize);
}

void FastReadStream::_Init(long lBlockCount, long lBlockSize) {
    this->lBlockCount    = lBlockCount;
    this->lBlockSize    = (lBlockSize + 4095) & -4096;
    this->pHeaders        = new FastReadStreamHeader[this->lBlockCount];
    this->pBuffer        = VirtualAlloc(NULL, this->lBlockCount * this->lBlockSize, MEM_COMMIT, PAGE_READWRITE);

    if (!this->pHeaders || !this->pBuffer) {
        delete this->pHeaders;
        if (this->pBuffer) VirtualFree(this->pBuffer, 0, MEM_RELEASE);

        this->pHeaders = NULL;
        this->pBuffer = NULL;
    } else {
        Flush();
    }

    lHistory            = 0;
}

bool FastReadStream::Ready() {
    return pHeaders && pBuffer;
}

FastReadStream::~FastReadStream() {
    delete pHeaders;
    if (pBuffer) VirtualFree(pBuffer, 0, MEM_RELEASE);
}

///////////////////////////////////////////////////////////////////////////

#pragma function(memcpy)

long FastReadStream::Read(int stream, __int64 i64Pos, void *pDest, long lBytes) {
    long lOffset, lActual = 0, lToCopy;
    __int64 i64BlockNo;
    char *pBuffer2 = (char *)pDest;
    int iCacheBlock;

    // First block number and offset...

    i64BlockNo = i64Pos / lBlockSize;
    lOffset = (long)(i64Pos % lBlockSize);

//    _RPT3(0,"Read request: %ld bytes, pos %I64x, first block %I64d\n", lBytes, i64Pos, i64BlockNo);

    while(lBytes) {
        long lInBlock;

        lToCopy = lBlockSize - lOffset;
        if (lToCopy > lBytes) lToCopy = lBytes;

        iCacheBlock = _Commit(stream, i64BlockNo);
        lInBlock = pHeaders[iCacheBlock].lBytes - lOffset;

//        _RPT4(0,"(%ld) Reading %ld from cache block %d, offset %ld\n", stream, lToCopy, iCacheBlock, lOffset);

        if (lInBlock < lToCopy) {
            if (lInBlock > 0) {
                memcpy(pBuffer2, (char *)pBuffer + iCacheBlock * lBlockSize + lOffset, lInBlock);

                lActual += lInBlock;
            }

            break;
        } else
            memcpy(pBuffer2, (char *)pBuffer + iCacheBlock * lBlockSize + lOffset, lToCopy);

        pBuffer2 += lToCopy;
        lBytes -= lToCopy;
        lActual += lToCopy;
        ++i64BlockNo;
        lOffset = 0;
    }

    return lActual;
}

void FastReadStream::Flush() {
    for(int i=0; i<lBlockCount; i++) {
        pHeaders[i].i64BlockNo = -1;
        pHeaders[i].fAccessedBits = 0;
        pHeaders[i].lHistoryVal = 0;
    }

    lHistory = 0;
}

///////////////////////////////////////////////////////////////////////////

int FastReadStream::_PickVictim(int stream) {
    int i;
    long fStreamEncounteredBits=0, fStreamNotLoneBits=0;
    int iOurLowest=-1, iGlobalLowest=-1, iPreferred=-1;
    long fStreamMask = 1L<<stream;

    // Look for an unused block.

    for(i=0; i<lBlockCount; i++)
        if (pHeaders[i].i64BlockNo == -1)
            return i;

    // Compile a list of streams with lone blocks.  These can't be replaced.
    // Look for our lone block.

    for(i=0; i<lBlockCount; i++) {
        // Encountered bits -> NotLone bits

        fStreamNotLoneBits |= fStreamEncounteredBits & pHeaders[i].fAccessedBits;
        fStreamEncounteredBits |= pHeaders[i].fAccessedBits;
    }

    // Look at the histories, and choose a few candidates.

    for(i=0; i<lBlockCount; i++) {
        long lThisHistory = lHistory - pHeaders[i].lHistoryVal;

        if (lThisHistory<0) lThisHistory = 0x7FFFFFFF;

        pHeaders[i].lAge = lThisHistory;

        // Our oldest block

        if (pHeaders[i].fAccessedBits & fStreamMask)
            if (iOurLowest<0 || lThisHistory > pHeaders[iOurLowest].lAge)
                iOurLowest = i;

        // Global oldest block

        if (iGlobalLowest<0 || lThisHistory > pHeaders[iGlobalLowest].lAge)
            iGlobalLowest = i;

        // Preferred lowest block

        if (pHeaders[i].fAccessedBits & fStreamMask
            && !(pHeaders[i].fAccessedBits & ~fStreamNotLoneBits))
            if (iPreferred<0 || lThisHistory > pHeaders[iPreferred].lAge)
                iPreferred = i;
    }

    return iPreferred>=0 ? iPreferred : iOurLowest>=0 ? iOurLowest : iGlobalLowest;
}

int FastReadStream::_Commit(int stream, __int64 i64BlockNo) {
    int iCacheBlock;
    int i;

    // Already have the block?

    for(i=0; i<lBlockCount; i++)
        if (pHeaders[i].i64BlockNo == i64BlockNo) {
            pHeaders[i].fAccessedBits |= 1L<<stream;
//            _RPT1(0,"Commit(%I64d): cache hit\n", i64BlockNo);
            return i;
        }

    // Pick a replacement candidate.

    iCacheBlock = _PickVictim(stream);

    // Replace it.

    try {
        ++lHistory;

//        _RPT2(0,"Commit(%I64d): cache miss (stream %d)\n", i64BlockNo, stream);
        if (iFile >= 0) {
            int iActual;

            if (-1 == _lseeki64(iFile, i64BlockNo * lBlockSize, SEEK_SET))
                throw MyError("FastRead seek error: %s.", strerror(errno));

            iActual = _read(iFile, (char *)pBuffer + iCacheBlock * lBlockSize, lBlockSize);

            if (iActual < 0)
                throw MyError("FastRead read error: %s.", strerror(errno));

            pHeaders[iCacheBlock].lBytes = iActual;

        } else {
            LONG lLow = (LONG)i64BlockNo*lBlockSize;
            LONG lHigh = (LONG)((i64BlockNo*lBlockSize) >> 32);
            DWORD err, dwActual;

            if (0xFFFFFFFF == SetFilePointer(hFile, lLow, &lHigh, FILE_BEGIN))
                if ((err = GetLastError()) != NO_ERROR)
                    throw MyWin32Error("FastRead seek error: %%s", GetLastError());

            if (!ReadFile(hFile, (char *)pBuffer + iCacheBlock * lBlockSize, lBlockSize, &dwActual, NULL))
                throw MyWin32Error("FastRead read error: %%s", GetLastError());

            pHeaders[iCacheBlock].lBytes = dwActual;
        }
        pHeaders[iCacheBlock].i64BlockNo = i64BlockNo;
        pHeaders[iCacheBlock].fAccessedBits = 1L<<stream;
        pHeaders[iCacheBlock].lHistoryVal = lHistory;
    } catch(...) {
        pHeaders[iCacheBlock].i64BlockNo = -1;
        pHeaders[iCacheBlock].fAccessedBits = 0;
    }

    return iCacheBlock;
}
