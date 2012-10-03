/*--------------------------------------------------------------------------*/
/* Copyright 2006-2009 Joe Lowe                                             */
/*                                                                          */
/* Permission is granted to any person obtaining a copy of this Software,   */
/* to deal in the Software without restriction, including the rights to use,*/
/* copy, modify, merge, publish, distribute, sublicense, and sell copies of */
/* the Software.                                                            */
/*                                                                          */
/* The above copyright and permission notice must be left intact in all     */
/* copies or substantial portions of the Software.                          */
/*                                                                          */
/* THE SOFTWARE IS WITHOUT WARRANTY.                                        */
/*--------------------------------------------------------------------------*/
/* file name:  pfmmarshaller.h                                              */
/* created:    2006.09.12                                                   */
/*--------------------------------------------------------------------------*/
#ifndef PFMMARSHALLER_H
#define PFMMARSHALLER_H
#ifdef __cplusplus_cli
#pragma managed(push,off)
#endif

#include "pfmprefix.h"

typedef struct
{
    PFM_INT8 fileType;
    PFM_UINT8 fileFlags;
    PFM_UINT8 extraFlags;
    PFM_UINT8 reserved1[5];
    PFM_INT64 fileId;
    PFM_UINT64 fileSize;
    PFM_INT64 createTime;
    PFM_INT64 accessTime;
    PFM_INT64 writeTime;
    PFM_INT64 changeTime;
} PfmAttribs;

typedef struct
{
    PFM_INT64 openId;
    PFM_INT64 openSequence;
    PFM_INT8 accessLevel;
    PFM_UINT8 controlFlags;
    PFM_UINT8 reserved1[6];
    PfmAttribs attribs;
} PfmOpenAttribs;

typedef struct
{
    const wchar_t* name;
    size_t len;
    const char* name8;
    size_t len8;
} PfmNamePart;

typedef struct
{
    PFM_UUID mediaUuid;
    PFM_UINT64 mediaId64;
    PFM_UINT32 mediaId32;
    PFM_UINT8 mediaFlags;
    PFM_UINT8 reserved1[3];
    PFM_INT64 createTime;
} PfmMediaInfo;

enum { pfmOpFormatterUseSize = 20*sizeof(int)+20*sizeof(void*) };

#ifdef __cplusplus
struct PfmFormatterDispatch;
#else
typedef struct PfmFormatterDispatch_* PfmFormatterDispatch;
#endif

typedef struct
{
    size_t paramsSize;
    PfmFormatterDispatch* dispatch;
    union { int volumeFlags; size_t pad1[1]; };
    const char* formatterName;
    size_t dataAlign;
    size_t maxDirectMsgSize;
    PFM_HANDLE toFormatterRead;
    PFM_HANDLE fromFormatterWrite;
} PfmMarshallerServeParams;

#ifdef __cplusplus

/* Concurrent marshaller definitions. */

struct PfmFormatterSerializeOpen
{
    virtual void PFM_CCALL SerializeOpen(PFM_INT64 openId,PFM_INT64* openSequence) = 0;
};

struct PfmMarshallerOpenOp
{
    virtual const PfmNamePart* PFM_CCALL NameParts(void) = 0;
    virtual size_t             PFM_CCALL NamePartCount(void) = 0;
    virtual PFM_INT8           PFM_CCALL CreateFileType(void) = 0;
    virtual PFM_UINT8          PFM_CCALL CreateFileFlags(void) = 0;
    virtual PFM_INT64          PFM_CCALL WriteTime(void) = 0;
    virtual PFM_INT64          PFM_CCALL NewCreateOpenId(void) = 0;
    virtual PFM_INT8           PFM_CCALL ExistingAccessLevel(void) = 0;
    virtual PFM_INT64          PFM_CCALL NewExistingOpenId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,bool existed,const PfmOpenAttribs* openAttribs,PFM_INT64 parentFileId,const wchar_t* endName,PfmFormatterSerializeOpen* serializeOpen) = 0;
};
struct PfmMarshallerReplaceOp
{
    virtual PFM_INT64          PFM_CCALL TargetOpenId(void) = 0;
    virtual PFM_INT64          PFM_CCALL TargetParentFileId(void) = 0;
    virtual const PfmNamePart* PFM_CCALL TargetEndName(void) = 0;
    virtual PFM_UINT8          PFM_CCALL CreateFileFlags(void) = 0;
    virtual PFM_INT64          PFM_CCALL WriteTime(void) = 0;
    virtual PFM_INT64          PFM_CCALL NewCreateOpenId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,const PfmOpenAttribs* openAttribs,PfmFormatterSerializeOpen* serializeOpen) = 0;
};
struct PfmMarshallerMoveOp
{
    virtual PFM_INT64          PFM_CCALL SourceOpenId(void) = 0;
    virtual PFM_INT64          PFM_CCALL SourceParentFileId(void) = 0;
    virtual const PfmNamePart* PFM_CCALL SourceEndName(void) = 0;
    virtual const PfmNamePart* PFM_CCALL TargetNameParts(void) = 0;
    virtual size_t             PFM_CCALL TargetNamePartCount(void) = 0;
    virtual bool               PFM_CCALL DeleteSource(void) = 0;
    virtual PFM_INT64          PFM_CCALL WriteTime(void) = 0;
    virtual PFM_INT64          PFM_CCALL NewExistingOpenId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,bool existed,const PfmOpenAttribs* openAttribs,PFM_INT64 parentFileId,const wchar_t* endName,PfmFormatterSerializeOpen* serializeOpen) = 0;
};
struct PfmMarshallerMoveReplaceOp
{
    virtual PFM_INT64          PFM_CCALL SourceOpenId(void) = 0;
    virtual PFM_INT64          PFM_CCALL SourceParentFileId(void) = 0;
    virtual const PfmNamePart* PFM_CCALL SourceEndName(void) = 0;
    virtual PFM_INT64          PFM_CCALL TargetOpenId(void) = 0;
    virtual PFM_INT64          PFM_CCALL TargetParentFileId(void) = 0;
    virtual const PfmNamePart* PFM_CCALL TargetEndName(void) = 0;
    virtual bool               PFM_CCALL DeleteSource(void) = 0;
    virtual PFM_INT64          PFM_CCALL WriteTime(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerDeleteOp
{
    virtual PFM_INT64          PFM_CCALL OpenId(void) = 0;
    virtual PFM_INT64          PFM_CCALL ParentFileId(void) = 0;
    virtual const PfmNamePart* PFM_CCALL EndName(void) = 0;
    virtual PFM_INT64          PFM_CCALL WriteTime(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerCloseOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual PFM_INT64 PFM_CCALL OpenSequence(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerFlushFileOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual PFM_UINT8 PFM_CCALL FileFlags(void) = 0;
    virtual PFM_INT64 PFM_CCALL CreateTime(void) = 0;
    virtual PFM_INT64 PFM_CCALL AccessTime(void) = 0;
    virtual PFM_INT64 PFM_CCALL WriteTime(void) = 0;
    virtual PFM_INT64 PFM_CCALL ChangeTime(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerListOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual PFM_INT64 PFM_CCALL ListId(void) = 0;
    virtual bool/*added*/ PFM_CCALL Add(const PfmAttribs* attribs,const wchar_t* endName) = 0;
    virtual bool/*added*/ PFM_CCALL Add8(const PfmAttribs* attribs,const char* endName) = 0;
    virtual void PFM_CCALL Complete(int pfmError,bool noMore) = 0;
};
struct PfmMarshallerListEndOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual PFM_INT64 PFM_CCALL ListId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerReadOp
{
    virtual PFM_INT64  PFM_CCALL OpenId(void) = 0;
    virtual PFM_UINT64 PFM_CCALL FileOffset(void) = 0;
    virtual void*      PFM_CCALL Data(void) = 0;
    virtual size_t     PFM_CCALL RequestedSize(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,size_t actualSize) = 0;
};
struct PfmMarshallerWriteOp
{
    virtual PFM_INT64   PFM_CCALL OpenId(void) = 0;
    virtual PFM_UINT64  PFM_CCALL FileOffset(void) = 0;
    virtual void*       PFM_CCALL Data(void) = 0;
    virtual size_t      PFM_CCALL RequestedSize(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,size_t actualSize) = 0;
};
struct PfmMarshallerSetSizeOp
{
    virtual PFM_INT64  PFM_CCALL OpenId(void) = 0;
    virtual PFM_UINT64 PFM_CCALL FileSize(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError) = 0;
};
struct PfmMarshallerCapacityOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,PFM_UINT64 totalCapacity,PFM_UINT64 availableCapacity) = 0;
};
struct PfmMarshallerFlushMediaOp
{
    virtual void PFM_CCALL Complete(int pfmError,int msecFlushDelay) = 0;
};
struct PfmMarshallerControlOp
{
    virtual PFM_INT64   PFM_CCALL OpenId(void) = 0;
    virtual PFM_INT8    PFM_CCALL AccessLevel(void) = 0;
    virtual int         PFM_CCALL ControlCode(void) = 0;
    virtual const void* PFM_CCALL Input(void) = 0;
    virtual size_t      PFM_CCALL InputSize(void) = 0;
    virtual void*       PFM_CCALL Output(void) = 0;
    virtual size_t      PFM_CCALL MaxOutputSize(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,size_t outputSize) = 0;
};
struct PfmMarshallerMediaInfoOp
{
    virtual PFM_INT64 PFM_CCALL OpenId(void) = 0;
    virtual void PFM_CCALL Complete(int pfmError,const PfmMediaInfo* mediaInfo,const wchar_t* mediaLabel) = 0;
};

struct PfmFormatterDispatch
{
    virtual void PFM_CCALL Open(PfmMarshallerOpenOp* openOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Replace(PfmMarshallerReplaceOp* replaceOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Move(PfmMarshallerMoveOp* moveOp,void* formatterUse) = 0;
    virtual void PFM_CCALL MoveReplace(PfmMarshallerMoveReplaceOp* moveReplaceOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Delete(PfmMarshallerDeleteOp* deleteOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Close(PfmMarshallerCloseOp* closeOp,void* formatterUse) = 0;
    virtual void PFM_CCALL FlushFile(PfmMarshallerFlushFileOp* flushFileOp,void* formatterUse) = 0;
    virtual void PFM_CCALL List(PfmMarshallerListOp* listOp,void* formatterUse) = 0;
    virtual void PFM_CCALL ListEnd(PfmMarshallerListEndOp* listEndOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Read(PfmMarshallerReadOp* readOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Write(PfmMarshallerWriteOp* writeOp,void* formatterUse) = 0;
    virtual void PFM_CCALL SetSize(PfmMarshallerSetSizeOp* setSizeOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Capacity(PfmMarshallerCapacityOp* capacityOp,void* formatterUse) = 0;
    virtual void PFM_CCALL FlushMedia(PfmMarshallerFlushMediaOp* flushMediaOp,void* formatterUse) = 0;
    virtual void PFM_CCALL Control(PfmMarshallerControlOp* controlOp,void* formatterUse) = 0;
    virtual void PFM_CCALL MediaInfo(PfmMarshallerMediaInfoOp* mediaInfoOp,void* formatterUse) = 0;
};

/* Single threaded marshaller definitions. */

struct PfmMarshallerListResult
{
    virtual bool/*added*/ PFM_CCALL Add(const PfmAttribs* attribs,const wchar_t* endName,bool* needMore) = 0;
    virtual bool/*added*/ PFM_CCALL Add8(const PfmAttribs* attribs,const char* endName,bool* needMore) = 0;
    virtual void PFM_CCALL NoMore(void) = 0;
};

struct PfmFormatterOps
{
    virtual void PFM_CCALL ReleaseName(wchar_t* name) = 0;
    virtual int/*pfmError*/ PFM_CCALL Open(const PfmNamePart* nameParts,size_t namePartCount,PFM_INT8 createFileType,PFM_UINT8 createFileFlags,PFM_INT64 writeTime,PFM_INT64 newCreateOpenId,PFM_INT8 existingAccessLevel,PFM_INT64 newExistingOpenId,bool* existed,PfmOpenAttribs* openAttribs,PFM_INT64* parentFileId,wchar_t** endName) = 0;
    virtual int/*pfmError*/ PFM_CCALL Replace(PFM_INT64 targetOpenId,PFM_INT64 targetParentFileId,const PfmNamePart* targetEndName,PFM_UINT8 createFileFlags,PFM_INT64 writeTime,PFM_INT64 newCreateOpenId,PfmOpenAttribs* openAttribs) = 0;
    virtual int/*pfmError*/ PFM_CCALL Move(PFM_INT64 sourceOpenId,PFM_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,bool deleteSource,PFM_INT64 writeTime,PFM_INT64 newExistingOpenId,bool* existed,PfmOpenAttribs* openAttribs,PFM_INT64* parentFileId,wchar_t** endName) = 0;
    virtual int/*pfmError*/ PFM_CCALL MoveReplace(PFM_INT64 sourceOpenId,PFM_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,PFM_INT64 targetOpenId,PFM_INT64 targetParentFileId,const PfmNamePart* targetEndName,bool deleteSource,PFM_INT64 writeTime) = 0;
    virtual int/*pfmError*/ PFM_CCALL Delete(PFM_INT64 openId,PFM_INT64 parentFileId,const PfmNamePart* endName,PFM_INT64 writeTime) = 0;
    virtual int/*pfmError*/ PFM_CCALL Close(PFM_INT64 openId,PFM_INT64 openSequence) = 0;
    virtual int/*pfmError*/ PFM_CCALL FlushFile(PFM_INT64 openId,PFM_UINT8 fileFlags,PFM_INT64 createTime,PFM_INT64 accessTime,PFM_INT64 writeTime,PFM_INT64 changeTime) = 0;
    virtual int/*pfmError*/ PFM_CCALL List(PFM_INT64 openId,PFM_INT64 listId,PfmMarshallerListResult* listResult) = 0;
    virtual int/*pfmError*/ PFM_CCALL ListEnd(PFM_INT64 openId,PFM_INT64 listId) = 0;
    virtual int/*pfmError*/ PFM_CCALL Read(PFM_INT64 openId,PFM_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL Write(PFM_INT64 openId,PFM_UINT64 fileOffset,const void* data,size_t requestedSize,size_t* outActualSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL SetSize(PFM_INT64 openId,PFM_UINT64 fileSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL Capacity(PFM_UINT64* totalCapacity,PFM_UINT64* availableCapacity) = 0;
    virtual int/*pfmError*/ PFM_CCALL FlushMedia(bool* mediaClean) = 0;
    virtual int/*pfmError*/ PFM_CCALL Control(PFM_INT64 openId,PFM_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL MediaInfo(PFM_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel) = 0;
};

struct PfmReadOnlyFormatterOps
{
    virtual void PFM_CCALL ReleaseName(wchar_t* name) = 0;
    virtual int/*pfmError*/ PFM_CCALL Open(const PfmNamePart* nameParts,size_t namePartCount,PFM_INT8 accessLevel,PFM_INT64 newOpenId,PfmOpenAttribs* openAttribs,wchar_t** endName) = 0;
    virtual int/*pfmError*/ PFM_CCALL Close(PFM_INT64 openId,PFM_INT64 openSequence) = 0;
    virtual int/*pfmError*/ PFM_CCALL List(PFM_INT64 openId,PFM_INT64 listId,PfmMarshallerListResult* listResult) = 0;
    virtual int/*pfmError*/ PFM_CCALL ListEnd(PFM_INT64 openId,PFM_INT64 listId) = 0;
    virtual int/*pfmError*/ PFM_CCALL Read(PFM_INT64 openId,PFM_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL Capacity(PFM_UINT64* totalCapacity) = 0;
    virtual int/*pfmError*/ PFM_CCALL FlushMedia(bool* mediaClean) = 0;
    virtual int/*pfmError*/ PFM_CCALL Control(PFM_INT64 openId,PFM_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize) = 0;
    virtual int/*pfmError*/ PFM_CCALL MediaInfo(PFM_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel) = 0;
};

/* Pfm protocol marshaller. */

struct PfmMarshaller
{
    virtual void PFM_CCALL Release(void) = 0;
    virtual void PFM_CCALL SetTrace(const wchar_t* traceChannelName) = 0;
    virtual void PFM_CCALL SetStatus(PFM_HANDLE write) = 0;
    virtual int/*pfmError*/ PFM_CCALL ConvertSystemError(int error) = 0;
    virtual int/*error*/ PFM_CCALL Identify(const char* mountFileData,size_t mountFileDataLen,const char* formatterName) = 0;
    virtual int/*error*/ PFM_CCALL GetPassword(PFM_HANDLE read,const wchar_t* prompt,const wchar_t** password) = 0;
    virtual void PFM_CCALL ClearPassword(void) = 0;
    virtual int/*error*/ PFM_CCALL ServeReadWrite(PfmFormatterOps* formatter,int volumeFlags,const char* formatterName,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite) = 0;
    virtual int/*error*/ PFM_CCALL ServeReadOnly(PfmReadOnlyFormatterOps* formatter,int volumeFlags,const char* formatterName,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite) = 0;
    virtual int/*error*/ PFM_CCALL ServeDispatch(const PfmMarshallerServeParams* params) = 0;
    /* Status print routines. */
    virtual void PFM_CCALL Line(const wchar_t* data,bool newLine) = 0;
    virtual void PFM_CCALL Print(const wchar_t* data) = 0;
    virtual void PFM_CCALL Vprintf(const wchar_t* format,va_list args) = 0;
    virtual void PFM_CCALL Printf(const wchar_t* format,...) = 0;
};


/* Duplicate of above definitions for non C++ code. */
#else

#define PFM_BOOL unsigned char

typedef struct PfmMarshallerListResult_* PfmMarshallerListResult;
struct PfmMarshallerListResult_
{
    PFM_BOOL/*added*/ (PFM_CCALL*Add)(PfmMarshallerListResult*,const PfmAttribs* attribs,const wchar_t* endName,PFM_BOOL* needMore);
    PFM_BOOL/*added*/ (PFM_CCALL*Add8)(PfmMarshallerListResult*,const PfmAttribs* attribs,const char* endName,PFM_BOOL* needMore);
    void (PFM_CCALL*NoMore)(PfmMarshallerListResult*);
};

typedef struct PfmFormatterOps_* PfmFormatterOps;
struct PfmFormatterOps_
{
    void (PFM_CCALL*ReleaseName)(PfmFormatterOps*,wchar_t* name);
    int/*pfmError*/ (PFM_CCALL*Open)(PfmFormatterOps*,const PfmNamePart* nameParts,size_t namePartCount,PFM_INT8 createFileType,PFM_UINT8 createFileFlags,PFM_INT64 writeTime,PFM_INT64 newCreateOpenId,PFM_INT8 existingAccessLevel,PFM_INT64 newExistingOpenId,PFM_BOOL* existed,PfmOpenAttribs* openAttribs,PFM_INT64* parentFileId,wchar_t** endName);
    int/*pfmError*/ (PFM_CCALL*Replace)(PfmFormatterOps*,PFM_INT64 targetOpenId,PFM_INT64 targetParentFileId,const PfmNamePart* targetEndName,PFM_UINT8 createFileFlags,PFM_INT64 writeTime,PFM_INT64 newCreateOpenId,PfmOpenAttribs* openAttribs);
    int/*pfmError*/ (PFM_CCALL*Move)(PfmFormatterOps*,PFM_INT64 sourceOpenId,PFM_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,PFM_BOOL deleteSource,PFM_INT64 writeTime,PFM_INT64 newExistingOpenId,PFM_BOOL* existed,PfmOpenAttribs* openAttribs,PFM_INT64* parentFileId,wchar_t** endName);
    int/*pfmError*/ (PFM_CCALL*MoveReplace)(PfmFormatterOps*,PFM_INT64 sourceOpenId,PFM_INT64 sourceParentFileId,const PfmNamePart* sourceEndName,PFM_INT64 targetOpenId,PFM_INT64 targetParentFileId,const PfmNamePart* targetEndName,PFM_BOOL deleteSource,PFM_INT64 writeTime);
    int/*pfmError*/ (PFM_CCALL*Delete)(PfmFormatterOps*,PFM_INT64 openId,PFM_INT64 parentFileId,const PfmNamePart* endName,PFM_INT64 writeTime);
    int/*pfmError*/ (PFM_CCALL*Close)(PfmFormatterOps*,PFM_INT64 openId,PFM_INT64 openSequence);
    int/*pfmError*/ (PFM_CCALL*FlushFile)(PfmFormatterOps*,PFM_INT64 openId,PFM_UINT8 fileFlags,PFM_INT64 createTime,PFM_INT64 accessTime,PFM_INT64 writeTime,PFM_INT64 changeTime);
    int/*pfmError*/ (PFM_CCALL*List)(PfmFormatterOps*,PFM_INT64 openId,PFM_INT64 listId,PfmMarshallerListResult* listResult);
    int/*pfmError*/ (PFM_CCALL*ListEnd)(PfmFormatterOps*,PFM_INT64 openId,PFM_INT64 listId);
    int/*pfmError*/ (PFM_CCALL*Read)(PfmFormatterOps*,PFM_INT64 openId,PFM_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
    int/*pfmError*/ (PFM_CCALL*Write)(PfmFormatterOps*,PFM_INT64 openId,PFM_UINT64 fileOffset,const void* data,size_t requestedSize,size_t* outActualSize);
    int/*pfmError*/ (PFM_CCALL*SetSize)(PfmFormatterOps*,PFM_INT64 openId,PFM_UINT64 fileSize);
    int/*pfmError*/ (PFM_CCALL*Capacity)(PfmFormatterOps*,PFM_UINT64* totalCapacity,PFM_UINT64* availableCapacity);
    int/*pfmError*/ (PFM_CCALL*FlushMedia)(PfmFormatterOps*,PFM_BOOL* mediaClean);
    int/*pfmError*/ (PFM_CCALL*Control)(PfmFormatterOps*,PFM_INT64 openId,PFM_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
    int/*pfmError*/ (PFM_CCALL*MediaInfo)(PfmFormatterOps*,PFM_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);
};

typedef struct PfmReadOnlyFormatterOps_* PfmReadOnlyFormatterOps;
struct PfmReadOnlyFormatterOps_
{
    void (PFM_CCALL*ReleaseName)(PfmReadOnlyFormatterOps*,wchar_t* name);
    int/*pfmError*/ (PFM_CCALL*Open)(PfmReadOnlyFormatterOps*,const PfmNamePart* nameParts,size_t namePartCount,PFM_INT8 accessLevel,PFM_INT64 newOpenId,PfmOpenAttribs* openAttribs,wchar_t** endName);
    int/*pfmError*/ (PFM_CCALL*Close)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PFM_INT64 openSequence);
    int/*pfmError*/ (PFM_CCALL*List)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PFM_INT64 listId,PfmMarshallerListResult* listResult);
    int/*pfmError*/ (PFM_CCALL*ListEnd)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PFM_INT64 listId);
    int/*pfmError*/ (PFM_CCALL*Read)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PFM_UINT64 fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
    int/*pfmError*/ (PFM_CCALL*Capacity)(PfmReadOnlyFormatterOps*,PFM_UINT64* totalCapacity);
    int/*pfmError*/ (PFM_CCALL*FlushMedia)(PfmReadOnlyFormatterOps*,PFM_BOOL* mediaClean);
    int/*pfmError*/ (PFM_CCALL*Control)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PFM_INT8 accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
    int/*pfmError*/ (PFM_CCALL*MediaInfo)(PfmReadOnlyFormatterOps*,PFM_INT64 openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);
};

typedef struct PfmMarshaller_* PfmMarshaller;
struct PfmMarshaller_
{
    void (PFM_CCALL*Release)(PfmMarshaller*);
    void (PFM_CCALL*SetTrace)(PfmMarshaller*,const wchar_t* traceChannelName);
    void (PFM_CCALL*SetStatus)(PfmMarshaller*,PFM_HANDLE write);
    int/*pfmError*/ (PFM_CCALL*ConvertSystemError)(PfmMarshaller*,int error);
    int/*error*/ (PFM_CCALL*Identify)(PfmMarshaller*,const char* mountFileData,size_t mountFileDataLen,const char* formatterName);
    int/*error*/ (PFM_CCALL*GetPassword)(PfmMarshaller*,const wchar_t** password);
    void (PFM_CCALL*ClearPasswords)(PfmMarshaller*);
    int/*error*/ (PFM_CCALL*ServeReadWrite)(PfmMarshaller*,PfmFormatterOps* formatter,int volumeFlags,const char* formatterName,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite);
    int/*error*/ (PFM_CCALL*ServeReadOnly)(PfmMarshaller*,PfmReadOnlyFormatterOps* formatter,int volumeFlags,const char* formatterName,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite);
    int/*error*/ (PFM_CCALL*ServeDispatch)(const PfmMarshallerServeParams* params);
    void (PFM_CCALL*Line)(PfmMarshaller*,const wchar_t* data,PFM_BOOL newLine);
    void (PFM_CCALL*Print)(PfmMarshaller*,const wchar_t* data);
    void (PFM_CCALL*Vprintf)(PfmMarshaller*,const wchar_t* format,va_list args);
    void (PFM_CCALL*Printf)(PfmMarshaller*,const wchar_t* format,...);
};

#endif


/*
Marshaller interface version history.
PfmMarshaller1- 2007.12.31
First public release.
PfmMarshaller2- 2008.02.08
Added FormatterOps::MediaInfo to return media ID and label.
PfmMarshaller3- 2009.03.02
Added fastpipe.
PfmMarshaller4- 2009.06.03
Concurrent marshaller
*/

#include "ptfactory1.h"

PTFACTORY1_DECLARE(PfmMarshaller,PFM_PRODIDW,PFM_PREFIXW L"api");
PTFACTORY1_INLINE int/*error*/ PTFACTORY1_CCALL PfmMarshallerFactory(PfmMarshaller** marshaller)
{ return PfmMarshallerGetInterface("PfmMarshaller4",marshaller); }
/* void PfmMarshallerUnload(void); */

#ifdef __cplusplus_cli
#pragma managed(pop)
#endif
#endif
