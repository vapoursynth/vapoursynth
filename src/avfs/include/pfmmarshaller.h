//---------------------------------------------------------------------------
// Copyright 2006-2015 Joe Lowe
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
// file name:  pfmmarshaller.h
// created:    2006.09.12
//---------------------------------------------------------------------------
#ifndef PFMMARSHALLER_H
#define PFMMARSHALLER_H
#include "ptfactory1.h"
#include "pfmenum.h"
#ifdef __cplusplus_cli
#pragma managed(push,off)
#endif

PT_TYPE_DEFINE(PfmAttribs)
{
   PT_INT8 fileType;
   PT_UINT8 fileFlags;
   PT_UINT8 extraFlags;
   PT_UINT8 color;
   PT_UINT32 resourceSize;
   PT_INT64 fileId;
   PT_UINT64 fileSize;
   PT_INT64 createTime;
   PT_INT64 accessTime;
   PT_INT64 writeTime;
   PT_INT64 changeTime;
};

PT_TYPE_DEFINE(PfmOpenAttribs)
{
   PT_INT64 openId;
   PT_INT64 openSequence;
   PT_INT8 accessLevel;
   PT_UINT8 controlFlags;
   PT_UINT8 reserved1[2];
   PT_UINT32 touch;
   PfmAttribs attribs;
};

PT_TYPE_DEFINE(PfmNamePart)
{
   const wchar_t* name;
   size_t len;
   const char* name8;
   size_t len8;
};

PT_TYPE_DEFINE(PfmMediaInfo)
{
   PT_UUID mediaUuid;
   PT_UINT64 mediaId64;
   PT_UINT32 mediaId32;
   PT_UINT8 mediaFlags;
   PT_UINT8 reserved1[3];
   PT_INT64 createTime;
};

PT_STATIC_CONST(size_t, pfmOpFormatterUseSize, 20*sizeof(int)+20*sizeof(void*));

PT_INTERFACE_DECLARE(PfmFormatterDispatch);

PT_TYPE_DEFINE(PfmMarshallerServeParams)
{
   size_t paramsSize;
   PfmFormatterDispatch* dispatch;
   int volumeFlags;
#if UINT_MAX < SIZE_MAX
   PT_UINT8 align1[sizeof(size_t)-sizeof(int)];
#endif
   const char* formatterName;
   size_t dataAlign;
   size_t maxDirectMsgSize;
   PT_FD_T toFormatterRead;
   PT_FD_T fromFormatterWrite;
};
PT_INLINE void PfmMarshallerServeParams_Init(PfmMarshallerServeParams* params)
   { memset(params,0,sizeof(*params)); params->paramsSize = sizeof(params); }

   // Concurrent marshaller definitions.

#define INTERFACE_NAME PfmFormatterSerializeOpen
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN2( void, SerializeOpen, PT_INT64 openId,PT_INT64* openSequence);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerOpenOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( const PfmNamePart*, NameParts          );
   PT_INTERFACE_FUN0( size_t            , NamePartCount      );
   PT_INTERFACE_FUN0( PT_INT8           , CreateFileType     );
   PT_INTERFACE_FUN0( PT_UINT8          , CreateFileFlags    );
   PT_INTERFACE_FUN0( PT_INT64          , WriteTime          );
   PT_INTERFACE_FUN0( PT_INT64          , NewCreateOpenId    );
   PT_INTERFACE_FUN0( PT_INT8           , ExistingAccessLevel);
   PT_INTERFACE_FUN0( PT_INT64          , NewExistingOpenId  );
   PT_INTERFACE_FUN9( void              , Complete           ,
      int pfmError,
      PT_BOOL8 existed,
      const PfmOpenAttribs* openAttribs,
      PT_INT64 parentFileId,
      const wchar_t* endName,
      size_t linkNamePartCount,
      const void* linkData,
      size_t linkDataSize,
      PfmFormatterSerializeOpen* serializeOpen);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerReplaceOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , TargetOpenId      );
   PT_INTERFACE_FUN0( PT_INT64          , TargetParentFileId);
   PT_INTERFACE_FUN0( const PfmNamePart*, TargetEndName     );
   PT_INTERFACE_FUN0( PT_UINT8          , CreateFileFlags   );
   PT_INTERFACE_FUN0( PT_INT64          , WriteTime         );
   PT_INTERFACE_FUN0( PT_INT64          , NewCreateOpenId   );
   PT_INTERFACE_FUN3( void              , Complete          ,
      int pfmError,
      const PfmOpenAttribs* openAttribs,
      PfmFormatterSerializeOpen* serializeOpen);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerMoveOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , SourceOpenId       );
   PT_INTERFACE_FUN0( PT_INT64          , SourceParentFileId );
   PT_INTERFACE_FUN0( const PfmNamePart*, SourceEndName      );
   PT_INTERFACE_FUN0( const PfmNamePart*, TargetNameParts    );
   PT_INTERFACE_FUN0( size_t            , TargetNamePartCount);
   PT_INTERFACE_FUN0( PT_BOOL8          , DeleteSource       );
   PT_INTERFACE_FUN0( PT_INT64          , WriteTime          );
   PT_INTERFACE_FUN0( PT_INT8           , ExistingAccessLevel);
   PT_INTERFACE_FUN0( PT_INT64          , NewExistingOpenId  );
   PT_INTERFACE_FUN9( void              , Complete           ,
      int pfmError,
      PT_BOOL8 existed,
      const PfmOpenAttribs* openAttribs,
      PT_INT64 parentFileId,
      const wchar_t* endName,
      size_t linkNamePartCount,
      const void* linkData,
      size_t linkDataSize,
      PfmFormatterSerializeOpen* serializeOpen);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerMoveReplaceOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , SourceOpenId      );
   PT_INTERFACE_FUN0( PT_INT64          , SourceParentFileId);
   PT_INTERFACE_FUN0( const PfmNamePart*, SourceEndName     );
   PT_INTERFACE_FUN0( PT_INT64          , TargetOpenId      );
   PT_INTERFACE_FUN0( PT_INT64          , TargetParentFileId);
   PT_INTERFACE_FUN0( const PfmNamePart*, TargetEndName     );
   PT_INTERFACE_FUN0( PT_BOOL8          , DeleteSource      );
   PT_INTERFACE_FUN0( PT_INT64          , WriteTime         );
   PT_INTERFACE_FUN1( void              , Complete          ,int pfmError);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerDeleteOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , OpenId      );
   PT_INTERFACE_FUN0( PT_INT64          , ParentFileId);
   PT_INTERFACE_FUN0( const PfmNamePart*, EndName     );
   PT_INTERFACE_FUN0( PT_INT64          , WriteTime   );
   PT_INTERFACE_FUN1( void              , Complete    , int pfmError);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerCloseOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64, OpenId      );
   PT_INTERFACE_FUN0( PT_INT64, OpenSequence);
   PT_INTERFACE_FUN1( void    , Complete    , int pfmError);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerFlushFileOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64   , OpenId      );
   PT_INTERFACE_FUN0( PT_UINT8   , FlushFlags  );
   PT_INTERFACE_FUN0( PT_UINT8   , FileFlags   );
   PT_INTERFACE_FUN0( PT_UINT8   , Color       );
   PT_INTERFACE_FUN0( PT_INT64   , CreateTime  );
   PT_INTERFACE_FUN0( PT_INT64   , AccessTime  );
   PT_INTERFACE_FUN0( PT_INT64   , WriteTime   );
   PT_INTERFACE_FUN0( PT_INT64   , ChangeTime  );
   PT_INTERFACE_FUN3( void       , Complete    ,
      int pfmError,
      const PfmOpenAttribs* openAttribs,
      PfmFormatterSerializeOpen* serializeOpen);
   PT_INTERFACE_FUN0( const void*, LinkData    );
   PT_INTERFACE_FUN0( PT_UINT32  , LinkDataSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerListOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64         , OpenId  );
   PT_INTERFACE_FUN0( PT_INT64         , ListId  );
   PT_INTERFACE_FUN2( PT_BOOL8/*added*/, Add     , const PfmAttribs* attribs,const wchar_t* endName);
   PT_INTERFACE_FUN2( PT_BOOL8/*added*/, Add8    , const PfmAttribs* attribs,const char* endName);
   PT_INTERFACE_FUN2( void             , Complete, int pfmError,PT_BOOL8 noMore);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerListEndOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64, OpenId  );
   PT_INTERFACE_FUN0( PT_INT64, ListId  );
   PT_INTERFACE_FUN1( void    , Complete, int pfmError);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerReadOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64 , OpenId       );
   PT_INTERFACE_FUN0( PT_UINT64, FileOffset   );
   PT_INTERFACE_FUN0( void*    , Data         );
   PT_INTERFACE_FUN0( size_t   , RequestedSize);
   PT_INTERFACE_FUN2( void     , Complete     , int pfmError,size_t actualSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerWriteOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64 , OpenId       );
   PT_INTERFACE_FUN0( PT_UINT64, FileOffset   );
   PT_INTERFACE_FUN0( void*    , Data         );
   PT_INTERFACE_FUN0( size_t   , RequestedSize);
   PT_INTERFACE_FUN2( void     , Complete     , int pfmError,size_t actualSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerSetSizeOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64 , OpenId  );
   PT_INTERFACE_FUN0( PT_UINT64, FileSize);
   PT_INTERFACE_FUN1( void     , Complete, int pfmError);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerCapacityOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64, OpenId  );
   PT_INTERFACE_FUN3( void    , Complete, int pfmError,PT_UINT64 totalCapacity,PT_UINT64 availableCapacity);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerFlushMediaOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN2( void, Complete, int pfmError,int msecFlushDelay);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerControlOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64   , OpenId       );
   PT_INTERFACE_FUN0( PT_INT8    , AccessLevel  );
   PT_INTERFACE_FUN0( int        , ControlCode  );
   PT_INTERFACE_FUN0( const void*, Input        );
   PT_INTERFACE_FUN0( size_t     , InputSize    );
   PT_INTERFACE_FUN0( void*      , Output       );
   PT_INTERFACE_FUN0( size_t     , MaxOutputSize);
   PT_INTERFACE_FUN2( void       , Complete     , int pfmError,size_t outputSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerMediaInfoOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64, OpenId  );
   PT_INTERFACE_FUN3( void    , Complete, int pfmError,const PfmMediaInfo* mediaInfo,const wchar_t* mediaLabel);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerAccessOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64, OpenId     );
   PT_INTERFACE_FUN0( PT_INT8 , AccessLevel);
   PT_INTERFACE_FUN3( void    , Complete   , int pfmError,const PfmOpenAttribs* openAttribs,PfmFormatterSerializeOpen* serializeOpen);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerReadXattrOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , OpenId       );
   PT_INTERFACE_FUN0( const PfmNamePart*, Name         );
   PT_INTERFACE_FUN0( PT_UINT32         , Offset       );
   PT_INTERFACE_FUN0( void*             , Data         );
   PT_INTERFACE_FUN0( size_t            , RequestedSize);
   PT_INTERFACE_FUN3( void              , Complete     , int pfmError,PT_UINT32 xattrSize,size_t transferredSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerWriteXattrOp
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( PT_INT64          , OpenId       );
   PT_INTERFACE_FUN0( const PfmNamePart*, Name         );
   PT_INTERFACE_FUN0( PT_UINT32         , XattrSize    );
   PT_INTERFACE_FUN0( PT_UINT32         , Offset       );
   PT_INTERFACE_FUN0( const void*       , Data         );
   PT_INTERFACE_FUN0( size_t            , RequestedSize);
   PT_INTERFACE_FUN2( void              , Complete     , int pfmError,size_t transferredSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmFormatterDispatch
PT_INTERFACE_DEFINE2
{
   PT_INTERFACE_FUN2( void, Open       , PfmMarshallerOpenOp* openOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Replace    , PfmMarshallerReplaceOp* replaceOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Move       , PfmMarshallerMoveOp* moveOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, MoveReplace, PfmMarshallerMoveReplaceOp* moveReplaceOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Delete     , PfmMarshallerDeleteOp* deleteOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Close      , PfmMarshallerCloseOp* closeOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, FlushFile  , PfmMarshallerFlushFileOp* flushFileOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, List       , PfmMarshallerListOp* listOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, ListEnd    , PfmMarshallerListEndOp* listEndOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Read       , PfmMarshallerReadOp* readOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Write      , PfmMarshallerWriteOp* writeOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, SetSize    , PfmMarshallerSetSizeOp* setSizeOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Capacity   , PfmMarshallerCapacityOp* capacityOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, FlushMedia , PfmMarshallerFlushMediaOp* flushMediaOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Control    , PfmMarshallerControlOp* controlOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, MediaInfo  , PfmMarshallerMediaInfoOp* mediaInfoOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, Access     , PfmMarshallerAccessOp* accessOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, ReadXattr  , PfmMarshallerReadXattrOp* readXattrOp,void* formatterUse);
   PT_INTERFACE_FUN2( void, WriteXattr , PfmMarshallerWriteXattrOp* writeXattrOp,void* formatterUse);
};
#undef INTERFACE_NAME

   // Single threaded marshaller definitions.

#define INTERFACE_NAME PfmMarshallerListResult
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN3( PT_BOOL8/*added*/, Add   ,const PfmAttribs* attribs,const wchar_t* endName,PT_BOOL8* needMore);
   PT_INTERFACE_FUN3( PT_BOOL8/*added*/, Add8  ,const PfmAttribs* attribs,const char* endName,PT_BOOL8* needMore);
   PT_INTERFACE_FUN0( void             , NoMore);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmFormatterOps
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN1( void           , ReleaseName, wchar_t* name);
   PT_INTERFACE_FU12( int/*pfmError*/, Open       , const PfmNamePart* nameParts,size_t namePartCount,PT_INT8 createFileType,PT_UINT8 createFileFlags,PT_INT64 writeTime,PT_INT64 newCreateOpenId,PT_INT8 existingAccessLevel,PT_INT64 newExistingOpenId,PT_BOOL8* existed,PfmOpenAttribs* openAttribs,PT_INT64* parentFileId,wchar_t** endName);
   PT_INTERFACE_FUN7( int/*pfmError*/, Replace    , PT_INT64 targetOpenId,PT_INT64 targetParentFileId,const PfmNamePart* targetEndName,PT_UINT8 createFileFlags,PT_INT64 writeTime,PT_INT64 newCreateOpenId,PfmOpenAttribs* openAttribs);
   PT_INTERFACE_FU13( int/*pfmError*/, Move       , PT_INT64 sourceOpenId,PT_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,PT_BOOL8 deleteSource,PT_INT64 writeTime,PT_INT8 existingAccessLevel,PT_INT64 newExistingOpenId,PT_BOOL8* existed,PfmOpenAttribs* openAttribs,PT_INT64* parentFileId,wchar_t** endName);
   PT_INTERFACE_FUN8( int/*pfmError*/, MoveReplace, PT_INT64 sourceOpenId,PT_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,PT_INT64 targetOpenId,PT_INT64 targetParentFileId,const PfmNamePart* targetEndName,PT_BOOL8 deleteSource,PT_INT64 writeTime);
   PT_INTERFACE_FUN4( int/*pfmError*/, Delete     , PT_INT64 openId,PT_INT64 parentFileId,const PfmNamePart* endName,PT_INT64 writeTime);
   PT_INTERFACE_FUN2( int/*pfmError*/, Close      , PT_INT64 openId,PT_INT64 openSequence);
   PT_INTERFACE_FUN9( int/*pfmError*/, FlushFile  , PT_INT64 openId,PT_UINT8 flushFlags,PT_UINT8 fileFlags,PT_UINT8 color,PT_INT64 createTime,PT_INT64 accessTime,PT_INT64 writeTime,PT_INT64 changeTime,PfmOpenAttribs* openAttribs);
   PT_INTERFACE_FUN3( int/*pfmError*/, List       , PT_INT64 openId,PT_INT64 listId,PfmMarshallerListResult* listResult);
   PT_INTERFACE_FUN2( int/*pfmError*/, ListEnd    , PT_INT64 openId,PT_INT64 listId);
   PT_INTERFACE_FUN5( int/*pfmError*/, Read       , PT_INT64 openId,PT_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
   PT_INTERFACE_FUN5( int/*pfmError*/, Write      , PT_INT64 openId,PT_UINT64 fileOffset,const void* data,size_t requestedSize,size_t* outActualSize);
   PT_INTERFACE_FUN2( int/*pfmError*/, SetSize    , PT_INT64 openId,PT_UINT64 fileSize);
   PT_INTERFACE_FUN2( int/*pfmError*/, Capacity   , PT_UINT64* totalCapacity,PT_UINT64* availableCapacity);
   PT_INTERFACE_FUN1( int/*pfmError*/, FlushMedia , PT_BOOL8* mediaClean);
   PT_INTERFACE_FUN8( int/*pfmError*/, Control    , PT_INT64 openId,PT_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
   PT_INTERFACE_FUN3( int/*pfmError*/, MediaInfo  , PT_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);
   PT_INTERFACE_FUN3( int/*pfmError*/, Access     , PT_INT64 openId,PT_INT8 accessLevel,PfmOpenAttribs* openAttribs);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmReadOnlyFormatterOps
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN1( void           , ReleaseName, wchar_t* name);
   PT_INTERFACE_FUN6( int/*pfmError*/, Open       , const PfmNamePart* nameParts,size_t namePartCount,PT_INT8 accessLevel,PT_INT64 newOpenId,PfmOpenAttribs* openAttribs,wchar_t** endName);
   PT_INTERFACE_FUN2( int/*pfmError*/, Close      , PT_INT64 openId,PT_INT64 openSequence);
   PT_INTERFACE_FUN3( int/*pfmError*/, List       , PT_INT64 openId,PT_INT64 listId,PfmMarshallerListResult* listResult);
   PT_INTERFACE_FUN2( int/*pfmError*/, ListEnd    , PT_INT64 openId,PT_INT64 listId);
   PT_INTERFACE_FUN5( int/*pfmError*/, Read       , PT_INT64 openId,PT_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
   PT_INTERFACE_FUN1( int/*pfmError*/, Capacity   , PT_UINT64* totalCapacity);
   PT_INTERFACE_FUN1( int/*pfmError*/, FlushMedia , PT_BOOL8* mediaClean);
   PT_INTERFACE_FUN8( int/*pfmError*/, Control    , PT_INT64 openId,PT_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
   PT_INTERFACE_FUN3( int/*pfmError*/, MediaInfo  , PT_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);
   PT_INTERFACE_FUN3( int/*pfmError*/, Access     , PT_INT64 openId,PT_INT8 accessLevel,PfmOpenAttribs* openAttribs);
};
#undef INTERFACE_NAME

   // Pfm protocol marshaller.

#define INTERFACE_NAME PfmMarshaller
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void           , Release           );
   PT_INTERFACE_FUN1( void           , SetTrace          , const wchar_t* traceChannelName);
   PT_INTERFACE_FUN1( void           , SetStatus         , PT_FD_T write);
   PT_INTERFACE_FUN1( int/*pfmError*/, ConvertSystemError, int error);
   PT_INTERFACE_FUN3( int/*error*/   , Identify          , const char* mountFileData,size_t mountFileDataLen,const char* formatterName);
   PT_INTERFACE_FUN3( int/*error*/   , GetPassword       , PT_FD_T read,const wchar_t* prompt,const wchar_t** password);
   PT_INTERFACE_FUN0( void           , ClearPassword     );
   PT_INTERFACE_FUN5( int/*error*/   , ServeReadWrite    , PfmFormatterOps* formatter,int volumeFlags,const char* formatterName,PT_FD_T toFormatterRead,PT_FD_T fromFormatterWrite);
   PT_INTERFACE_FUN5( int/*error*/   , ServeReadOnly     , PfmReadOnlyFormatterOps* formatter,int volumeFlags,const char* formatterName,PT_FD_T toFormatterRead,PT_FD_T fromFormatterWrite);
   PT_INTERFACE_FUN1( int/*error*/   , ServeDispatch     , const PfmMarshallerServeParams* params);
      // Status print routines.
   PT_INTERFACE_FUN2( void           , Line              , const wchar_t* data,PT_BOOL8 newLine);
   PT_INTERFACE_FUN1( void           , Print             , const wchar_t* data);
   PT_INTERFACE_FUN2( void           , Vprintf           , const wchar_t* format,va_list args);
   PT_INTERFACE_FUN2( void           , Printf            , const wchar_t* format,...);
      //
   PT_INTERFACE_FUN0( void           , ServeCancel       );
   PT_INTERFACE_FUN0( int            , GetClientVersion  );
   PT_INTERFACE_FUN0( int            , GetClientFlags    );
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMarshallerPreMount
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN1( int/*error*/, PreMount, PfmFormatterDispatch* dispatch);
};
#undef INTERFACE_NAME

#ifdef PFMMARSHALLER_STATIC

PT_EXTERNC int/*error*/ PT_CCALL PfmMarshallerFactory(PfmMarshaller** marshaller);

PT_EXTERNC void PT_CCALL PfmMarshallerSetPreMount(PfmMarshaller* marshaller,PfmMarshallerPreMount* preMount);

#endif

   // Marshaller interface version history.
   //    PfmMarshaller1- 2007.12.31
   //       First public release.
   //    PfmMarshaller2- 2008.02.08
   //       Added FormatterOps::MediaInfo to return media ID and label.
   //    PfmMarshaller3- 2009.03.02
   //       Added fastpipe.
   //    PfmMarshaller4- 2009.06.03
   //       Concurrent marshaller
   //    PfmMarshaller5- 2010.04.22
   //       Move::ExistingAccessLevel
   //    PfmMarshaller6- 2011.01.27
   //       Access operation.
   //       File color attribute.
   //    PfmMarshaller7- 2013.03.11
   //       Xattr support.
   //       Mac alias flag.
   //       Client flags.
   //    PfmMarshaller8- 2013.05
   //       Symlink support.

#ifndef PFMMARSHALLER_STATIC

PTFACTORY1_DECLARE(PfmMarshaller,PFM_PRODIDW,PFM_APIIDW);
PT_INLINE int/*error*/ PT_CCALL PfmMarshallerFactory(PfmMarshaller** marshaller)
   { return PfmMarshallerGetInterface("PfmMarshaller8",marshaller); }
// void PfmMarshallerUnload(void);

#endif

#ifdef __cplusplus_cli
#pragma managed(pop)
#endif
#endif
