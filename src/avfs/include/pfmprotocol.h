//---------------------------------------------------------------------------
// Copyright 2005-2013 Joe Lowe
//
// Permission is granted to any person obtaining a copy of this Software,
// to deal in the Software without restriction, including the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and sell copies of
// the Software.
//
// The above copyright and permission notice must be left intact in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS WITHOUT WARRANTY.
//---------------------------------------------------------------------------
// file name:  pfmprotocol.h
// created:    2005.12.07
//
// Definitions for the driver to formatter communication protocol. Only
// necessary for formatters that choose to not use the provided marshaller.
//---------------------------------------------------------------------------
#ifndef PFMPROTOCOL_H
#define PFMPROTOCOL_H
#include "pfmenum.h"
#ifdef __cplusplus
extern "C" {
#endif

// Protocol version history
//    1- 2007.12.31
//       first release
//    2- 2008.02.08
//       Added openId to capacity msg.
//       Added media info msg for label and media ID.
//    3- 2009.03.02
//       Read/write data alignment padding.
//       Media flush delay.
//    4- 2009.06.01
//       File exec flag.
//       File extra and control flags.
//    5- 2009.06.30
//       Large read/write msg size.
//    6- 2010.04.22
//       Added existingAccessLevel for Move.
//    7- 2011.03.10
//       Added access msg, for reopen and attrib query.
//       Added file color attrib, for Mac finder info.
//       Touchmap support in open attribs.
//       Volume time stamp support flags.
//       Added open attribs to flush file results.
//    8- 2013.03.21
//       Xattr support.
//       Alias file flag.
//       Client flags.
//    9- 2013.05
//       Symlink support.

PT_STATIC_CONST( PT_UINT16, fspVersion                  , 9);
PT_STATIC_CONST( PT_UINT16, fspMinServerCompatVersion   , 1);
PT_STATIC_CONST( PT_UINT16, fspMinClientCompatVersion   , 1);
PT_STATIC_CONST( PT_UINT16, fspMinDataAlignVersion      , 3);
PT_STATIC_CONST( PT_UINT16, fspMinLargeDataSizeVersion  , 5);
PT_STATIC_CONST( PT_UINT16, fspMinAccessVersion         , 7);
PT_STATIC_CONST( PT_UINT16, fspMinColorVersion          , 7);
PT_STATIC_CONST( PT_UINT16, fspMinFlushAttribsVersion   , 7);
PT_STATIC_CONST( PT_UINT16, fspMinXattrVersion          , 8);
PT_STATIC_CONST( PT_UINT16, fspMinSymlinkVersion        , 9);

#define FSP_SERVER_READY "+(fspServerReady:..:..)\n"
#define FSP_SERVER_READY_SIZE (sizeof(FSP_SERVER_READY)-sizeof(FSP_SERVER_READY[0]))
#define FSP_CLIENT_READY "+(fspClientReady:..:..)\n"
#define FSP_CLIENT_READY_SIZE (sizeof(FSP_CLIENT_READY)-sizeof(FSP_CLIENT_READY[0]))

PT_TYPE_DEFINE(FspMsgHeader)
{
   PT_UINT32LE msgSize;
   // low 20 bits is cookie
   // middle 11 bits is type/error
   // high bit set indicates result
   PT_UINT32LE msgTypeAndCookie;

#if defined(PORTABLEINT_H) && defined(__cplusplus)
   inline unsigned GetCookie(void) const { return getuint32le(&msgTypeAndCookie)&0xFFFFF; }
   inline bool IsResult(void) const { return !!(getuint32le(&msgTypeAndCookie)&0x80000000); }
   inline int GetTypeOrResult(void) const { return static_cast<int>((getuint32le(&msgTypeAndCookie)>>20)&0x7FF); }
   inline int GetType(void) const { ASSERT(!IsResult()); return GetTypeOrResult(); }
   inline int GetResult(void) const { ASSERT(IsResult()); return GetTypeOrResult(); }
   inline void SetTypeAndCookie(int msgType,unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&msgTypeAndCookie,(static_cast<unsigned>(msgType)<<20)|cookie); }
   inline void SetResultAndCookie(int msgType,unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&msgTypeAndCookie,cookie|(static_cast<unsigned>(msgType)<<20)|0x80000000); }
   inline void SetCookie(unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&msgTypeAndCookie,cookie|(getuint32le(&msgTypeAndCookie)&0xFFF00000)); }
   inline void SetTypeZeroCookie(int msgType) { setuint32le(&msgTypeAndCookie,(static_cast<unsigned>(msgType)<<20)); }
   inline void SetResultZeroCookie(int msgType) { setuint32le(&msgTypeAndCookie,(static_cast<unsigned>(msgType)<<20)|0x80000000); }
#endif
};
#if defined(PORTABLEINT_H)
INLINE unsigned FspMsgHeader_GetCookie(const FspMsgHeader* m) { return getuint32le(&m->msgTypeAndCookie)&0xFFFFF; }
INLINE int/*bool*/ FspMsgHeader_IsResult(const FspMsgHeader* m) { return !!(getuint32le(&m->msgTypeAndCookie)&0x80000000); }
INLINE int FspMsgHeader_GetTypeOrResult(const FspMsgHeader* m) { return SCAST(int,(getuint32le(&m->msgTypeAndCookie)>>20)&0x7FF); }
INLINE int FspMsgHeader_GetType(const FspMsgHeader* m) { ASSERT(!FspMsgHeader_IsResult(m)); return FspMsgHeader_GetTypeOrResult(m); }
INLINE int FspMsgHeader_GetResult(const FspMsgHeader* m) { ASSERT(FspMsgHeader_IsResult(m)); return FspMsgHeader_GetTypeOrResult(m); }
INLINE void FspMsgHeader_SetTypeAndCookie(FspMsgHeader* m,int msgType,unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&m->msgTypeAndCookie,(SCAST(unsigned,msgType)<<20)|cookie); }
INLINE void FspMsgHeader_SetResultAndCookie(FspMsgHeader* m,int msgType,unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&m->msgTypeAndCookie,cookie|(SCAST(unsigned,msgType)<<20)|0x80000000); }
INLINE void FspMsgHeader_SetCookie(FspMsgHeader* m,unsigned cookie) { ASSERT(cookie <= 0x000FFFFE); setuint32le(&m->msgTypeAndCookie,cookie|(getuint32le(&m->msgTypeAndCookie)&0xFFF00000)); }
INLINE void FspMsgHeader_SetTypeZeroCookie(FspMsgHeader* m,int msgType) { setuint32le(&m->msgTypeAndCookie,(SCAST(unsigned,msgType)<<20)); }
INLINE void FspMsgHeader_SetResultZeroCookie(FspMsgHeader* m,int msgType) { setuint32le(&m->msgTypeAndCookie,(SCAST(unsigned,msgType)<<20)|0x80000000); }
#endif

PT_STATIC_CONST( size_t  , fspMinMsgSize       , sizeof(FspMsgHeader)      );
PT_STATIC_CONST( size_t  , fspMaxMsgSize       , 0x0048000 /* 256kb+32kb */);
PT_STATIC_CONST( size_t  , fspMaxDirectMsgSize , 0x1000000 /* 16Mib */     );
PT_STATIC_CONST( size_t  , fspMaxDataAlign     , 0x2000    /* 8kb */       );
PT_STATIC_CONST( unsigned, fspMaxMsgCookie     , 0x000FFFFE                );
PT_STATIC_CONST( unsigned, fspMsgCookieNoResult, 0                         );

PT_STATIC_CONST( int, fspMsgTypeCancel     ,  0);
PT_STATIC_CONST( int, fspMsgTypeDisconnect ,  1);
PT_STATIC_CONST( int, fspMsgTypeVersion    ,  2);
PT_STATIC_CONST( int, fspMsgTypeOpen       ,  3);
PT_STATIC_CONST( int, fspMsgTypeReplace    ,  4);
PT_STATIC_CONST( int, fspMsgTypeMove       ,  5);
PT_STATIC_CONST( int, fspMsgTypeMoveReplace,  6);
PT_STATIC_CONST( int, fspMsgTypeDelete     ,  7);
PT_STATIC_CONST( int, fspMsgTypeClose      ,  8);
PT_STATIC_CONST( int, fspMsgTypeFlushFile  ,  9);
PT_STATIC_CONST( int, fspMsgTypeList       , 10);
PT_STATIC_CONST( int, fspMsgTypeListEnd    , 11);
PT_STATIC_CONST( int, fspMsgTypeRead       , 12);
PT_STATIC_CONST( int, fspMsgTypeWrite      , 13);
PT_STATIC_CONST( int, fspMsgTypeSetSize    , 14);
PT_STATIC_CONST( int, fspMsgTypeCapacity   , 15);
PT_STATIC_CONST( int, fspMsgTypeFlushMedia , 16);
PT_STATIC_CONST( int, fspMsgTypeControl    , 17);
PT_STATIC_CONST( int, fspMsgTypeMediaInfo  , 18);
PT_STATIC_CONST( int, fspMsgTypeAccess     , 19);
PT_STATIC_CONST( int, fspMsgTypeReadXattr  , 20);
PT_STATIC_CONST( int, fspMsgTypeWriteXattr , 21);

PT_TYPE_DEFINE(FspFileAttribs)
{
   PT_INT8 fileType;
   PT_UINT8 fileFlags;
   PT_UINT8 extraFlags;
   PT_UINT8 color;
   PT_UINT32LE resourceSize;
   PT_INT64LE fileId;
   PT_UINT64LE fileSize;
   PT_INT64LE createTime;
   PT_INT64LE accessTime;
   PT_INT64LE writeTime;
   PT_INT64LE changeTime;
};

PT_TYPE_DEFINE(FspFileOpenAttribs)
{
   PT_INT64LE openId;
   PT_INT64LE openSequence;
   PT_INT8 accessLevel;
   PT_UINT8 controlFlags;
   PT_UINT8 reserved1[2];
   PT_UINT32LE touch;
   FspFileAttribs attribs;
};

PT_TYPE_DEFINE(FspMsgDisconnect)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgDisconnectResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgVersion)
{
   FspMsgHeader header;
   PT_UINT16LE clientVersion;
   PT_UINT16LE clientFlags;
   PT_UINT32LE reserved[1];
};

PT_TYPE_DEFINE(FspMsgVersionResult)
{
   FspMsgHeader header;
   PT_UINT16LE serverVersion;
   PT_UINT16LE minClientCompatVersion;
   PT_INT32LE volumeFlags;
   PT_CHAR8 formatterName[24];
   PT_UINT32LE dataAlign;
   PT_UINT32LE maxDirectMsgSize;
   PT_UINT32LE reserved1[4];
};

PT_TYPE_DEFINE(FspMsgOpen)
{
   FspMsgHeader header;
   PT_INT8 existingAccessLevel;
   PT_INT8 createFileType;
   PT_UINT8 createFileFlags;
   PT_UINT8 reserved1[5];
   PT_INT64LE writeTime;
   PT_INT64LE newExistingOpenId;
   PT_INT64LE newCreateOpenId;
   PT_INT64LE reservedForParentOpenId;
   PT_CHAR8 name[8]; // multisz
};

PT_TYPE_DEFINE(FspMsgOpenResult)
{
   FspMsgHeader header;
   PT_BOOL8 existed;
   PT_UINT8 reserved1[3];
   PT_UINT32LE endNameOffset;
   FspFileOpenAttribs openAttribs;
   PT_INT64LE parentFileId;
   PT_UINT32LE linkNamePartCount;
   PT_UINT32LE linkDataOffset;
   PT_UINT32LE linkDataSize;
   PT_CHAR8 endName[4 /*strlen(endName)+1*/];
   // PT_CHAR8 linkData[linkDataSize];
};
PT_STATIC_CONST(unsigned,FspMsgOpenResult_minSize,offsetof(FspMsgOpenResult,linkNamePartCount));
PT_STATIC_CONST(unsigned,FspMsgOpenResult_minEndNameOffset,offsetof(FspMsgOpenResult,linkNamePartCount));

PT_TYPE_DEFINE(FspMsgReplace)
{
   FspMsgHeader header;
   PT_INT64LE targetOpenId;
   PT_INT64LE targetParentFileId;
   PT_UINT8 createFileFlags;
   PT_UINT8 reserved1[7];
   PT_INT64LE writeTime;
   PT_INT64LE newCreateOpenId;
   PT_CHAR8 targetEndName[8];
};

PT_TYPE_DEFINE(FspMsgReplaceResult)
{
   FspMsgHeader header;
   FspFileOpenAttribs openAttribs;
};

PT_TYPE_DEFINE(FspMsgMove)
{
   FspMsgHeader header;
   PT_INT64LE sourceOpenId;
   PT_INT64LE sourceParentFileId;
   PT_BOOL8 deleteSource;
   PT_INT8 existingAccessLevel;
   PT_UINT8 reserved1[6];
   PT_INT64LE writeTime;
   PT_INT64LE newExistingOpenId;
   PT_INT64LE reservedForTargetParentOpenId;
   PT_CHAR8 sourceEndNameAndTargetName[8];
   // PT_CHAR8 sourceEndName[strlen(sourceEndName)+1];
   // PT_CHAR8 targetName[]; // multisz
};

typedef FspMsgOpenResult FspMsgMoveResult;
PT_STATIC_CONST(unsigned,FspMsgMoveResult_minSize,FspMsgOpenResult_minSize);
PT_STATIC_CONST(unsigned,FspMsgMoveResult_minEndNameOffset,FspMsgOpenResult_minEndNameOffset);

PT_TYPE_DEFINE(FspMsgMoveReplace)
{
   FspMsgHeader header;
   PT_INT64LE sourceOpenId;
   PT_INT64LE sourceParentFileId;
   PT_INT64LE targetOpenId;
   PT_INT64LE targetParentFileId;
   PT_BOOL8 deleteSource;
   PT_UINT8 reserved1[7];
   PT_INT64LE writeTime;
   // PT_CHAR8 sourceEndName[strlen(sourceEndName)+1];
   // PT_CHAR8 targetEndName[strlen(targetEndName)+1];
   PT_CHAR8 sourceEndNameAndTargetEndName[8];
};

PT_TYPE_DEFINE(FspMsgMoveReplaceResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgDelete)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT64LE parentFileId;
   PT_INT64LE writeTime;
   PT_CHAR8 endName[8];
};

PT_TYPE_DEFINE(FspMsgDeleteResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgClose)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT64LE openSequence;
};

PT_TYPE_DEFINE(FspMsgCloseResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgFlushFile)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT8 fileFlags;
   PT_UINT8 color;
   PT_UINT8 flushFlags;
   PT_UINT8 reserved1[5];
   PT_INT64LE createTime;
   PT_INT64LE accessTime;
   PT_INT64LE writeTime;
   PT_INT64LE changeTime;
   PT_UINT32LE linkDataOffset;
   PT_UINT32LE linkDataSize;
   // PT_UINT8 linkData[linkDataSize];
};

PT_TYPE_DEFINE(FspMsgFlushFileResult)
{
   FspMsgHeader header;
   FspFileOpenAttribs openAttribs;
};

PT_TYPE_DEFINE(FspMsgList)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT64LE listId;
   PT_UINT32LE maxResultCount;
   PT_UINT32LE maxResultSize;
};

PT_TYPE_DEFINE(FspMsgListResult)
{
   FspMsgHeader header;
   PT_UINT32LE count;
   PT_BOOL8 noMore;
   PT_UINT8 reserved1[3];
   FspFileAttribs attribs[1/*count*/];
   // PT_CHAR8 names[]; // multisz
};

PT_TYPE_DEFINE(FspMsgListEnd)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT64LE listId;
};

PT_TYPE_DEFINE(FspMsgListEndResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgRead)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT64LE fileOffset;
   PT_UINT32LE readSize;
   PT_UINT32LE resultDataPadSize;
};

PT_TYPE_DEFINE(FspMsgReadResult)
{
   FspMsgHeader header;
   PT_UINT32LE readSize;
   PT_UINT32LE dataPadSize; // = msg->resultDataPadSize
   // PT_UINT8 dataPad[dataPadSize];
   // PT_UINT8 data[readSize];
   // PT_UINT8 alignPad[header->msgSize-offsetof(alignPad)];
};

PT_TYPE_DEFINE(FspMsgWrite)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT64LE fileOffset;
   PT_UINT32LE writeSize;
   PT_UINT32LE dataPadSize;
   // PT_UINT8 dataPad[dataPadSize];
   // PT_UINT8 data[writeSize];
   // PT_UINT8 alignPad[header->msgSize-offsetof(alignPad)];
};

PT_TYPE_DEFINE(FspMsgWriteResult)
{
   FspMsgHeader header;
   PT_UINT32LE writeSize;
   PT_UINT8 reserved1[4];
};

PT_TYPE_DEFINE(FspMsgSetSize)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT64LE fileSize;
};

PT_TYPE_DEFINE(FspMsgSetSizeResult)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgCapacity)
{
   FspMsgHeader header;
   PT_INT64LE openId;
};

PT_TYPE_DEFINE(FspMsgCapacityResult)
{
   FspMsgHeader header;
   PT_UINT64LE totalCapacity;
   PT_UINT64LE availableCapacity;
};

PT_TYPE_DEFINE(FspMsgFlushMedia)
{
   FspMsgHeader header;
};

PT_TYPE_DEFINE(FspMsgFlushMediaResult)
{
   FspMsgHeader header;
   PT_BOOL8 mediaClean;
   PT_UINT8 reserved1[3];
   PT_INT32LE msecFlushDelay;
};

PT_TYPE_DEFINE(FspMsgControl)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT8 accessLevel;
   PT_UINT8 reserved1[3];
   PT_INT32LE controlCode;
   PT_UINT32LE inputSize;
   PT_UINT32LE maxOutputSize;
   // PT_UINT8 input[inputSize];
   // PT_UINT8 alignPad[header->msgSize-offsetof(alignPad)];
};

PT_TYPE_DEFINE(FspMsgControlResult)
{
   FspMsgHeader header;
   PT_UINT32LE outputSize;
   PT_UINT8 reserved1[4];
   // PT_UINT8 output[outputSize];
   // PT_UINT8 alignPad[header->msgSize-offsetof(alignPad)];
};

PT_TYPE_DEFINE(FspMsgMediaInfo)
{
   FspMsgHeader header;
   PT_INT64LE openId;
};

PT_TYPE_DEFINE(FspMsgMediaInfoResult)
{
   FspMsgHeader header;
   PT_INT64LE createTime;
   PT_UUIDLE mediaUuid;
   PT_UINT64LE mediaId64;
   PT_UINT32LE mediaId32;
   PT_UINT8 mediaFlags;
   PT_UINT8 reserved1[11];
   PT_CHAR8 mediaLabel[8];
};

PT_TYPE_DEFINE(FspMsgAccess)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_INT8 accessLevel;
   PT_UINT8 reserved1[7];
};

PT_TYPE_DEFINE(FspMsgAccessResult)
{
   FspMsgHeader header;
   FspFileOpenAttribs openAttribs;
};

PT_TYPE_DEFINE(FspMsgReadXattr)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT32LE offset;
   PT_UINT32LE readSize;
   // PT_CHAR8 name[header->msgSize-offsetof(name)];
};

PT_TYPE_DEFINE(FspMsgReadXattrResult)
{
   FspMsgHeader header;
   PT_UINT32LE xattrSize;
   PT_UINT32LE readSize;
   // PT_UINT8 data[header->msgSize-offsetof(data)];
};

PT_TYPE_DEFINE(FspMsgWriteXattr)
{
   FspMsgHeader header;
   PT_INT64LE openId;
   PT_UINT32LE xattrSize;
   PT_UINT32LE offset;
   PT_UINT32LE writeSize;
   PT_UINT32LE nameSize;
   // PT_CHAR8 name[nameSize];
   // PT_UINT8 data[header->msgSize-offsetof(data)];
};

PT_TYPE_DEFINE(FspMsgWriteXattrResult)
{
   FspMsgHeader header;
   PT_UINT32LE writeSize;
   PT_UINT8 reserved1[4];
};

   // File id hash function based on Bob Jenkins lookup3.c,
   // for use in converting file id to touch map index.
#ifdef BITOPS_H
#define _PFM_ROL(v,c) bits_rol(v,c)
#else
#define _PFM_ROL(v,c) (((v)<<(c))|((v)>>(32U-(c))))
#endif
PT_INLINE unsigned fsp_touch_map_index(PT_INT64 file_id,size_t map_mask)
{
   unsigned a = (unsigned)file_id;
   unsigned b = (unsigned)((PT_UINT64)file_id>>32);
   unsigned c = 0;
   c ^= b; c -= _PFM_ROL(b,14);
   a ^= c; a -= _PFM_ROL(c,11);
   b ^= a; b -= _PFM_ROL(a,25);
   c ^= b; c -= _PFM_ROL(b,16);
   a ^= c; a -= _PFM_ROL(c,4);
   b ^= a; b -= _PFM_ROL(a,14);
   c ^= b; c -= _PFM_ROL(b,24);
   return c&(unsigned)map_mask;
}

#ifdef __cplusplus
}
#endif
#endif
