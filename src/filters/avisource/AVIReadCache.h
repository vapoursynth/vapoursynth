#ifndef f_AVIREADCACHE_H
#define f_AVIREADCACHE_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <list>

class AVIStreamNode;

class VDINTERFACE IAVIReadCacheSource {
public:
    virtual long ReadData(int stream, void *buffer, sint64 position, long len);
    virtual bool Stream(AVIStreamNode *, _int64 pos) = 0;
    virtual sint64 getStreamPtr() = 0;
};

class AVIReadCache {
public:
    long cache_hit_bytes, cache_miss_bytes;
    int reads;

    AVIReadCache(int nlines, int nstream, IAVIReadCacheSource *root, AVIStreamNode *psnData);
    ~AVIReadCache();

    void ResetStatistics();
    bool WriteBegin(sint64 pos, uint32 len);
    void Write(const void *buffer, uint32 len);
    void WriteEnd();
    long Read(void *dest, sint64 chunk_pos, sint64 pos, uint32 len);

    long getMaxRead() {
        return (long)mSize;
    }

private:
    struct IndexBlockEntry {
        sint64 pos;
        uint32 start;
        uint32 len;
    };

    struct IndexBlock {
        enum { kBlocksPerIndex = 64 };

        int        mHead;
        int        mTail;
        IndexBlockEntry mBlocks[kBlocksPerIndex];
    };

    AVIStreamNode *psnData;

    vdfastvector<char> mBuffer;

    typedef std::list<IndexBlock> tIndexBlockList;
    tIndexBlockList mActiveIndices;
    tIndexBlockList mFreeIndices;

    int mSize, mFree;
    int mWritePos;
    int stream;
    IAVIReadCacheSource *source;
};


#endif
