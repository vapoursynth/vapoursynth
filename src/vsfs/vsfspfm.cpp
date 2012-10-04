//
// VapourSynth modifications Copyright 2012 Fredrik Mellbin
//
//----------------------------------------------------------------------------
// Copyright 2008-2010 Joe Lowe
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
//----------------------------------------------------------------------------
// file name:  vsfspfm.cpp
// created:    2008.01.04
//----------------------------------------------------------------------------
// Notes:
//
// This module implements a simple virtual file system for
// use with Avisynth and the Pismo File Mount Audit Package.
// The file system mounts an Avisynth script file, presenting
// it as a virtual folder containing virtual files that
// represent the the media stream produced by the script.
//
// The file system has write support for a virtual "echo"
// script file. This echo script file allows the script to be
// edited and the virtual media stream reinitialized without
// having to unmount and remount the real script file.
//
// A few seconds after the echo script file is modified,
// Avisynth will be reinitialized and the virtual media files
// will change. You can force Avisynth to be reinitialized by
// touching the echo script file (modifying the time stamp).
//
// Avisynth errors should be logged to the provided log
// interface. This will write the errors to a virtual
// error.log file.
//
// The PfmMarshaller interface provided by PFM contains
// diagnostic tracing support and generates diagnostic traces
// related to file system activity. Developers can view these
// traces by installing Pismo Trace Monitor. Errors written
// to the log interface are also visible through the trace
// monitor.
//
//----------------------------------------------------------------------------
#include "assertive.h"
#include <limits.h>
#include "stdint.h"
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <wchar.h>
#include <new>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ss.h"
#include "files.h"
#include "pfmenum.h"
#include "pfmformatter.h"
#include "pfmmarshaller.h"
#include "vsfspfm.h"

#define CCALL __cdecl

struct AvfsFormatter: PfmFormatter
{
    struct Volume;

    enum {
        fileTypeAny       = 0,
        fileTypeFolder    = 1,
        fileTypeScript    = 2,
        fileTypeLog       = 3,
        fileTypeMedia     = 4,
        maxScriptDataSize = 10*1000000,
        maxLogDataSize    = 250*1000000, };

        struct ListState
        {
            Volume* volume;
            ListState** prevList;
            ListState* nextList;
            int64_t listId;
            size_t position;
            ListState(Volume* volume,int64_t listId);
            ~ListState(void);
        };

        struct FileNode
        {
            Volume* volume;
            FileNode** prevFile;
            FileNode* nextFile;
            wchar_t* name;
            int64_t openId;
            int64_t openSequence;
            bool open;
            int8_t fileType;
            int64_t writeTime;
            uint64_t fileSize;
            size_t maxFileDataSize;
            void* fileData;
            AvfsMediaFile_* mediaFile;
            FileNode(Volume* volume,int64_t openId,int8_t fileType,int64_t writeTime);
            ~FileNode(void);
        };

        struct Volume:
            AvfsLog_,
            AvfsVolume_,
            PfmFormatterOps
        {
            PfmMarshaller* marshaller;
            wchar_t* scriptFileName;
            const wchar_t* scriptEndName;
            wchar_t* mediaName;
            bool scriptWritable;
            int64_t scriptWriteTime;
            FileNode* scriptFile;
            FileNode* logFile;
            ListState* firstList;
            FileNode* firstFile;

            // AvfsLog_
            void Print(const wchar_t* data);
            void Vprintf(const wchar_t* format,va_list args);
            void Printf(const wchar_t* format,...);
            void Line(const wchar_t* data);

            ListState* ListFind(int64_t listId);
            int/*pfmError*/ FileFindName(const PfmNamePart* nameParts,size_t namePartCount,FileNode** file);
            int/*pfmError*/ FileFindOpenId(int64_t openId,bool forModify,FileNode** file);
            void FileOpened(FileNode* file,PfmOpenAttribs* openAttribs);
            int/*pfmError*/ FileCreate(int64_t openId,int8_t createFileType,int64_t writeTime,FileNode** outFile);
            int/*pfmError*/ FileOpenOrMove(const PfmNamePart* nameParts,size_t namePartCount,FileNode* sourceFile,int64_t newExistingOpenId,bool deleteSource,int notFoundError,bool* existed,PfmOpenAttribs* openAttribs,wchar_t** endName);
            int/*pfmError*/ FileReplace(FileNode* sourceFile,FileNode* targetFile,bool deleteSource,PfmOpenAttribs* openAttribs);
            int/*pfmError*/ FileWrite(FileNode* file,size_t maxFileDataSize,uint64_t fileOffset,const void* buffer,size_t requestedSize,size_t* actualSize);
            void DeleteMediaFiles(void);

            // AvfsVolume_
            const wchar_t* GetScriptFileName(void);
            const wchar_t* GetScriptEndName(void);
            const wchar_t* GetMediaName(void);
            const char* GetScriptData(void);
            size_t GetScriptDataSize(void);
            void CreateMediaFile(AvfsMediaFile_* mediaFile,const wchar_t* fileName,uint64_t fileSize);

            // PfmFormatterOps
            void CCALL ReleaseName(wchar_t* name);
            int/*pfmError*/ CCALL Open(const PfmNamePart* nameParts,size_t namePartCount,int8_t createFileType,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,int8_t existingAccessLevel,int64_t newExistingOpenId,bool* existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName);
            int/*pfmError*/ CCALL Replace(int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,PfmOpenAttribs* openAttribs);
            int/*pfmError*/ CCALL Move(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,bool deleteSource,int64_t writeTime,int64_t newExistingOpenId,bool* existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName);
            int/*pfmError*/ CCALL MoveReplace(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,bool deleteSource,int64_t writeTime);
            int/*pfmError*/ CCALL Delete(int64_t openId,int64_t parentFileId,const PfmNamePart* endName,int64_t writeTime);
            int/*pfmError*/ CCALL Close(int64_t openId,int64_t openSequence);
            int/*pfmError*/ CCALL FlushFile(int64_t openId,uint8_t fileFlags,int64_t createTime,int64_t accessTime,int64_t writeTime,int64_t changeTime);
            int/*pfmError*/ CCALL List(int64_t openId,int64_t listId,PfmMarshallerListResult* listResult);
            int/*pfmError*/ CCALL ListEnd(int64_t openId,int64_t listId);
            int/*pfmError*/ CCALL Read(int64_t openId,uint64_t fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
            int/*pfmError*/ CCALL Write(int64_t openId,uint64_t fileOffset,const void* data,size_t requestedSize,size_t* outActualSize);
            int/*pfmError*/ CCALL SetSize(int64_t openId,uint64_t fileSize);
            int/*pfmError*/ CCALL Capacity(uint64_t* totalCapacity,uint64_t* availableCapacity);
            int/*pfmError*/ CCALL FlushMedia(bool* mediaClean);
            int/*pfmError*/ CCALL Control(int64_t openId,int8_t accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
            int/*pfmError*/ CCALL MediaInfo(int64_t openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);

            Volume(void);
            ~Volume(void);
            int/*systemError*/ Init(const wchar_t* scriptFileName);
            void Destroy(void);
        };

        // PfmFormatter
        void CCALL Release(void);
        int/*systemError*/ CCALL Identify(HANDLE statusWrite,const wchar_t* mountFileName,HANDLE mountFileHandle,const void* mountFileData,size_t mountFileDataSize);
        int/*systemError*/ CCALL Serve(const wchar_t* mountFileName,int mountFlags,HANDLE read,HANDLE write);
};

AvfsFormatter avfsFormatter;

AvfsFormatter::ListState::ListState(
    Volume* inVolume,
    int64_t inListId)
{
    volume = inVolume;
    prevList = &(volume->firstList);
    while(*prevList)
    {
        prevList= &((*prevList)->nextList);
    }
    *prevList = this;
    nextList = 0;
    listId = inListId;
    position = 0;
}

AvfsFormatter::ListState::~ListState(void)
{
    if(prevList)
    {
        ASSERT(*prevList == this);
        *prevList = nextList;
        if(nextList)
        {
            ASSERT(nextList->prevList == &nextList);
            nextList->prevList = prevList;
        }
    }
}

AvfsFormatter::FileNode::FileNode(
    Volume* inVolume,
    int64_t inOpenId,
    int8_t inFileType,
    int64_t inWriteTime)
{
    volume = inVolume;
    prevFile = &(volume->firstFile);
    while(*prevFile)
    {
        prevFile= &((*prevFile)->nextFile);
    }
    *prevFile = this;
    nextFile = 0;
    name = 0;
    openId = inOpenId;
    openSequence = 0;
    open = false;
    fileType = inFileType;
    writeTime = inWriteTime;
    fileSize = 0;
    maxFileDataSize = 0;
    fileData = 0;
    mediaFile = 0;
}

AvfsFormatter::FileNode::~FileNode(void)
{
    if(prevFile)
    {
        ASSERT(*prevFile == this);
        *prevFile = nextFile;
        if(nextFile)
        {
            ASSERT(nextFile->prevFile == &nextFile);
            nextFile->prevFile = prevFile;
        }
    }
    ssfree(name);
    if(fileData)
    {
        free(fileData);
    }
    if(mediaFile)
    {
        mediaFile->Release();
    }
}

void AvfsFormatter::Volume::Print(const wchar_t* data)
{
    marshaller->Print(data);
    char* data8 = ssconvalloc(data);
    if(data8)
    {
        FileWrite(logFile,maxLogDataSize,logFile->fileSize,data8,
            sssize(data8),0);
    }
    ssfree(data8);
}

void AvfsFormatter::Volume::Vprintf(const wchar_t* format,va_list args)
{
    marshaller->Vprintf(format,args);
    wchar_t* data = ssvformatalloc(format,args);
    Print(data);
    ssfree(data);
}

void AvfsFormatter::Volume::Printf(const wchar_t* format,...)
{
    va_list args;
    va_start(args,format);
    Vprintf(format,args);
}

void AvfsFormatter::Volume::Line(const wchar_t* data)
{
    Printf(L"%s\r\n",data);
}

AvfsFormatter::ListState* AvfsFormatter::Volume::ListFind(
    int64_t listId)
{
    ListState* list = firstList;
    while(list && list->listId != listId)
    {
        list = list->nextList;
    }
    return list;
}

int/*pfmError*/ AvfsFormatter::Volume::FileFindName(
    const PfmNamePart* nameParts,
    size_t namePartCount,
    FileNode** outFile)
{
    int error = pfmErrorParentNotFound;
    FileNode* file = 0;
    // Root folder has fixed name.
    const wchar_t* name = L".";
    // Subfolders not supported, so names can only have one
    // part.
    if(namePartCount < 2)
    {
        error = pfmErrorNotFound;
        if(namePartCount == 1)
        {
            name = nameParts[0].name;
        }
        file = firstFile;
        while(file && sscmpi(file->name,name) != 0)
        {
            file = file->nextFile;
        }
        if(file)
        {
            error = 0;
        }
    }
    *outFile = file;
    return error;
}

int/*pfmError*/ AvfsFormatter::Volume::FileFindOpenId(
    int64_t openId,
    bool forModify,
    FileNode** outFile)
{
    int error = 0;
    FileNode* file = firstFile;
    while(file && (!file->open || file->openId != openId))
    {
        file = file->nextFile;
    }
    if(!file)
    {
        error = pfmErrorNotFound;
    }
    // Can only move/delete/write script files, and only if real
    // script file was writable.
    else if(forModify && (!scriptWritable ||
        file->fileType != fileTypeScript))
    {
        error = pfmErrorAccessDenied;
    }
    *outFile = file;
    return error;
}

void AvfsFormatter::Volume::FileOpened(
    FileNode* file,
    PfmOpenAttribs* openAttribs)
{
    ASSERT(file);
    file->open = true;
    openAttribs->openId = file->openId;
    openAttribs->openSequence = ++(file->openSequence);
    // HACKHACK: VirtualDub hex editor will not open read-only files.
    // openAttribs->accessLevel = pfmReadDataAccess;
    // if(file->fileType == fileTypeScript)
    {
        openAttribs->accessLevel = pfmWriteDataAccess;
    }
    openAttribs->attribs.fileType = pfmFileTypeFile;
    if(file->fileType == fileTypeFolder)
    {
        openAttribs->attribs.fileType = pfmFileTypeFolder;
    }
    openAttribs->attribs.fileSize = file->fileSize;
    openAttribs->attribs.writeTime = file->writeTime;
    if(file->fileType == fileTypeMedia)
    {
        openAttribs->attribs.extraFlags |=
            (pfmExtraFlagOffline|pfmExtraFlagNoIndex);
    }
}

int/*pfmError*/ AvfsFormatter::Volume::FileCreate(
    int64_t openId,
    int8_t createFileType,
    int64_t writeTime,
    FileNode** outFile)
{
    FileNode* file = 0;
    // Only allow files to be created.
    int error = pfmErrorAccessDenied;
    switch(createFileType)
    {
    case pfmFileTypeFile:
        error = pfmErrorAccessDenied;
        if(scriptWritable)
        {
            error = pfmErrorOutOfMemory;
            file = new(std::nothrow) FileNode(this,openId,fileTypeScript,writeTime);
            if(file)
            {
                error = 0;
            }
        }
        break;
    case pfmFileTypeNone:
        error = pfmErrorNotFound;
        break;
    }
    *outFile = file;
    return error;
}

int/*pfmError*/ AvfsFormatter::Volume::FileOpenOrMove(
    const PfmNamePart* nameParts,
    size_t namePartCount,
    FileNode* sourceFile,
    int64_t newExistingOpenId,
    bool deleteSource,
    int notFoundError,
    bool* existed,
    PfmOpenAttribs* openAttribs,
    wchar_t** endName)
{
    // Open existing file, or move source file to
    // the non-existing file name.
    FileNode* file;
    int error = FileFindName(nameParts,namePartCount,&file);
    wchar_t* name;
    if(!error)
    {
        ASSERT(file);
        *existed = true;
        // Use driver supplied open ID if this file has never
        // been opened.
        if(!file->openId)
        {
            file->openId = newExistingOpenId;
        }
    }
    else if(error == pfmErrorNotFound)
    {
        ASSERT(namePartCount == 1);
        *existed = false;
        if(notFoundError)
        {
            error = notFoundError;
        }
        if(sourceFile)
        {
            ASSERT(sourceFile->openId);
            // Don't support hard links, but do support restoring
            // deleted (no name) files.
            error = pfmErrorInvalid;
            if(deleteSource || !sourceFile->name)
            {
                error = pfmErrorOutOfMemory;
                name = ssdup(nameParts[0].name);
                if(name)
                {
                    error = 0;
                    ssfree(sourceFile->name);
                    sourceFile->name = name;
                    file = sourceFile;
                }
            }
        }
    }
    if(!error)
    {
        FileOpened(file,openAttribs);
        if(sscmpi(file->name,L".") != 0)
        {
            *endName = ssdup(file->name);
        }
    }
    return error;
}

int/*pfmError*/ AvfsFormatter::Volume::FileReplace(
    FileNode* sourceFile,
    FileNode* targetFile,
    bool deleteSource,
    PfmOpenAttribs* openAttribs)
{
    // Delete target file and rename source file to target
    // files name.
    int error = 0;
    // Only script files can be moved/deleted/modified.
    if(sourceFile->fileType != fileTypeScript ||
        targetFile->fileType != fileTypeScript)
    {
        error = pfmErrorAccessDenied;
    }
    // Don't support hard links, but do support restoring
    // deleted (no name) files. Target can not already be
    // deleted.
    else if(!targetFile->name || (!deleteSource && sourceFile->name))
    {
        error = pfmErrorInvalid;
    }
    if(!error)
    {
        ssfree(sourceFile->name);
        sourceFile->name = targetFile->name;
        targetFile->name = 0;
        if(openAttribs)
        {
            FileOpened(sourceFile,openAttribs);
        }
    }
    return error;
}

int/*error*/ AvfsFormatter::Volume::FileWrite(
    FileNode* file,
    size_t maxFileDataSize,
    uint64_t fileOffset,
    const void* buffer,
    size_t requestedSize,
    size_t* outActualSize)
{
    int error = 0;
    size_t actualSize = 0;
    size_t endOffset;
    size_t newMaxFileDataSize;
    void* newFileData;
    if(fileOffset > maxFileDataSize)
    {
        error = pfmErrorNoSpace;
    }
    else
    {
        actualSize = maxFileDataSize-
            static_cast<size_t>(fileOffset);
    }
    if(!error)
    {
        if(actualSize > requestedSize)
        {
            actualSize = requestedSize;
        }
        endOffset = static_cast<size_t>(fileOffset)+actualSize;
        if(endOffset > file->maxFileDataSize)
        {
            newMaxFileDataSize = endOffset+endOffset/4+1000;
            newFileData = malloc(newMaxFileDataSize);
            if(!newFileData)
            {
                error = pfmErrorOutOfMemory;
            }
            else
            {
                if(file->fileData)
                {
                    if(file->fileSize)
                    {
                        memcpy(newFileData,file->fileData,
                            static_cast<size_t>(file->fileSize));
                    }
                    free(file->fileData);
                }
                file->maxFileDataSize = newMaxFileDataSize;
                file->fileData = newFileData;
            }
        }
        if(!error)
        {
            memcpy(static_cast<uint8_t*>(file->fileData)+
                static_cast<size_t>(fileOffset),buffer,actualSize);
            if(endOffset > file->fileSize)
            {
                file->fileSize = endOffset;
            }
        }
    }
    if(outActualSize)
    {
        *outActualSize = actualSize;
    }
    return error;
}

void AvfsFormatter::Volume::DeleteMediaFiles(void)
{
    // Delete all media files.
    FileNode* file = firstFile;
    while(file)
    {
        if(file->fileType == fileTypeMedia)
        {
            ssfree(file->name);
            file->name = 0;
            if(file->mediaFile)
            {
                file->mediaFile->Release();
                file->mediaFile = 0;
            }
            if(file->open)
            {
                // The file is still open. It will be deleted
                // when it is finally closed.
                file = file->nextFile;
            }
            else
            {
                delete file;
                file = firstFile;
            }
        }
        else
        {
            file = file->nextFile;
        }
    }
}

const wchar_t* AvfsFormatter::Volume::GetScriptFileName(void)
{
    return scriptFileName;
}

const wchar_t* AvfsFormatter::Volume::GetScriptEndName(void)
{
    return scriptEndName;
}

const wchar_t* AvfsFormatter::Volume::GetMediaName(void)
{
    return mediaName;
}

const char* AvfsFormatter::Volume::GetScriptData()
{
    ASSERT(scriptFile);
    return static_cast<char*>(scriptFile->fileData);
}

size_t AvfsFormatter::Volume::GetScriptDataSize(void)
{
    ASSERT(scriptFile);
    return static_cast<size_t>(scriptFile->fileSize);
}

void AvfsFormatter::Volume::CreateMediaFile(
    AvfsMediaFile_* mediaFile,
    const wchar_t* endName,
    uint64_t fileSize)
{
    // Create a media file whose data will be satisfied by
    // read calls to the media object.
    FileNode* file;
    file = new(std::nothrow) FileNode(this,0,fileTypeMedia,
        scriptWriteTime);
    ASSERT(!wcschr(endName,'\\'));
    if(file)
    {
        file->name = ssdup(endName);
        file->fileSize = fileSize;
        if(!file->name)
        {
            delete file;
        }
        else if(mediaFile)
        {
            mediaFile->AddRef();
            file->mediaFile = mediaFile;
        }
    }
}

void CCALL AvfsFormatter::Volume::ReleaseName(
    wchar_t* name)
{
    ssfree(name);
}

int/*pfmError*/ CCALL AvfsFormatter::Volume::Open(
    const PfmNamePart* nameParts,
    size_t namePartCount,
    int8_t createFileType,
    uint8_t createFileFlags,
    int64_t writeTime,
    int64_t newCreateOpenId,
    int8_t existingAccessLevel,
    int64_t newExistingOpenId,
    bool* existed,
    PfmOpenAttribs* openAttribs,
    int64_t* parentFileId,
    wchar_t** endName)
{
    // Open or create of a file or folder.
    FileNode* createFile;
    // Many editors save files using a create/delete/rename
    // sequence. Must support file creation in order to allow
    // script file to be edited.
    // Create a _new unnamed source file to be moved to the
    // specified file name if it does not exist.
    int notFoundError = FileCreate(newCreateOpenId,createFileType,
        writeTime,&createFile);
    // Use common open/move logic.
    int error = FileOpenOrMove(nameParts,namePartCount,createFile,
        newExistingOpenId,false/*deleteSource*/,notFoundError,existed,
        openAttribs,endName);
    if(createFile && !createFile->open)
    {
        delete createFile;
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Replace(
    int64_t targetOpenId,
    int64_t targetParentFileId,
    const PfmNamePart* targetEndName,
    uint8_t createFileFlags,
    int64_t writeTime,
    int64_t newCreateOpenId,
    PfmOpenAttribs* openAttribs)
{
    // Replace an existing file with a _new file.
    FileNode* createFile = 0;
    FileNode* targetFile;
    int error = FileFindOpenId(targetOpenId,true/*forModify*/,&targetFile);
    // Create the _new unnamed file object to move to the name
    // of the target.
    if(!error)
    {
        error = FileCreate(newCreateOpenId,targetFile->fileType,writeTime,
            &createFile);
    }
    if(!error)
    {
        // Delete the target and move _new file to target name.
        error = FileReplace(createFile,targetFile,false/*deleteSource*/,
            openAttribs);
    }
    if(createFile && !createFile->open)
    {
        delete createFile;
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Move(
    int64_t sourceOpenId,
    int64_t sourceParentFileId,
    const PfmNamePart* sourceEndName,
    const PfmNamePart* targetNameParts,
    size_t targetNamePartCount,
    bool deleteSource,
    int64_t writeTime,
    int64_t newExistingOpenId,
    bool* existed,
    PfmOpenAttribs* openAttribs,
    int64_t* parentFileId,
    wchar_t** endName)
{
    // Open an existing target file, or move a previously opened
    // file to a _new target name if the target does not exist.
    FileNode* sourceFile = 0;
    int error = FileFindOpenId(sourceOpenId,true/*forModify*/,&sourceFile);
    if(!error)
    {
        // Use common open/move logic.
        error = FileOpenOrMove(targetNameParts,targetNamePartCount,
            sourceFile,newExistingOpenId,deleteSource,pfmErrorNotFound,
            existed,openAttribs,endName);
        ASSERT(error != pfmErrorNotFound);
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::MoveReplace(
    int64_t sourceOpenId,
    int64_t sourceParentFileId,
    const PfmNamePart* sourceEndName,
    int64_t targetOpenId,
    int64_t targetParentFileId,
    const PfmNamePart* targetEndName,
    bool deleteSource,
    int64_t writeTime)
{
    // Delete an previously opened target file and move a
    // previously opened source file to the target files name.
    FileNode* sourceFile = 0;
    FileNode* targetFile;
    int error = FileFindOpenId(targetOpenId,true/*forModify*/,&targetFile);
    if(!error)
    {
        error = FileFindOpenId(sourceOpenId,true/*forModify*/,&sourceFile);
    }
    if(!error)
    {
        // Delete the target and move _new file to target
        // name.
        error = FileReplace(sourceFile,targetFile,deleteSource,0);
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Delete(
    int64_t openId,
    int64_t parentFileId,
    const PfmNamePart* endName,
    int64_t writeTime)
{
    // Delete a previously opened file.
    FileNode* file;
    int error = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!error)
    {
        // Mark file deleted by freeing name.
        ssfree(file->name);
        file->name = 0;
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Close(
    int64_t openId,
    int64_t openSequence)
{
    // If no more references to file then free associated
    // resources.
    FileNode* file;
    int error = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!error)
    {
        // Driver avoids race conditions between open and close
        // by returning highest open sequence it had seen for
        // the file when the close request was generated. If the
        // supplied open sequence is less than the last one
        // returned then the file is still open.
        if(openSequence >= file->openSequence)
        {
            file->open = false;
            switch(file->fileType)
            {
            case fileTypeFolder:
                // Clean up any lists when folder is closed.
                while(firstList)
                {
                    delete firstList;
                }
                break;
            case fileTypeLog:
                // Use a new open ID on next open, so cached
                // data will be discarded. This is a workaround
                // for the lack of support in PFM for externally
                // modified files.
                file->openId = 0;
                break;
            }
            // If file has no name then it is deleted so
            // can now be freed.
            if(!file->name)
            {
                delete file;
            }
        }
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::FlushFile(
    int64_t openId,
    uint8_t fileFlags,
    int64_t createTime,
    int64_t accessTime,
    int64_t writeTime,
    int64_t changeTime)
{
    // Update file attributes and commit file data to disk.
    FileNode* file;
    int error = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!error)
    {
        if(writeTime != pfmTimeInvalid)
        {
            file->writeTime = writeTime;
        }
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::List(
    int64_t openId,
    int64_t listId,
    PfmMarshallerListResult* listResult)
{
    // List the contents of a folder.
    size_t position = 0;
    FileNode* file;
    ListState* list = 0;
    int error = FileFindOpenId(openId,false/*forModify*/,&file);
    bool needMore = true;
    bool added = true;
    PfmAttribs attribs;
    // AvfsFormatter only supports one folder, so just need to verify
    // the file type.
    if(!error && file->fileType != fileTypeFolder)
    {
        error = pfmErrorAccessDenied;
    }
    if(!error)
    {
        // Find the associated list state, or if first time we've
        // seen this list ID then create a _new list state.
        list = ListFind(listId);
        if(!list)
        {
            list = new(std::nothrow) ListState(this,listId);
            if(!list)
            {
                error = pfmErrorOutOfMemory;
            }
        }
    }
    if(!error)
    {
        // Using simple index to remember position in list.
        file = firstFile;
        while(file && position < list->position)
        {
            file = file->nextFile;
        }
        while(file && added && needMore)
        {
            memset(&attribs,0,sizeof(attribs));
            attribs.fileType = pfmFileTypeFile;
            if(file->fileType == fileTypeFolder)
            {
                attribs.fileType = pfmFileTypeFolder;
            }
            attribs.fileSize = file->fileSize;
            attribs.writeTime = file->writeTime;
            if(file->fileType == fileTypeMedia)
            {
                attribs.extraFlags |=
                    (pfmExtraFlagOffline|pfmExtraFlagNoIndex);
            }
            added = listResult->Add(&attribs,file->name,&needMore);
            list->position += !!added;
            file = file->nextFile;
        }
        if(!file)
        {
            // Save the driver calling us back again if there are
            // no more files.
            listResult->NoMore();
        }
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::ListEnd(
    int64_t openId,
    int64_t listId)
{
    // Clean up any resources associated with an open folder
    // list operation.
    FileNode* file;
    ListState* list;
    int error = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!error && file->fileType == fileTypeFolder)
    {
        list = ListFind(listId);
        if(!list)
        {
            error = pfmErrorInvalid;
        }
        else
        {
            delete list;
        }
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Read(
    int64_t openId,
    uint64_t fileOffset,
    void* buffer,
    size_t requestedSize,
    size_t* outActualSize)
{
    // Read data from open file.
    size_t actualSize = 0;
    uint64_t maxSize;
    FileNode* file;
    int error = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!error)
    {
        maxSize = file->fileSize;
        if(fileOffset < maxSize)
        {
            maxSize -= fileOffset;
            actualSize = requestedSize;
            if(maxSize < requestedSize)
            {
                actualSize = static_cast<size_t>(maxSize);
            }
        }
        switch(file->fileType)
        {
        default:
        case fileTypeFolder:
            error = pfmErrorAccessDenied;
            break;
        case fileTypeScript:
        case fileTypeLog:
            // Echo data for script and log files.
            if(actualSize)
            {
                ASSERT(file->fileSize <= maxScriptDataSize);
                memcpy(buffer,static_cast<uint8_t*>(file->fileData)+
                    static_cast<size_t>(fileOffset),actualSize);
            }
            break;
        case fileTypeMedia:
            // Watch for deleted media files.
            error = pfmErrorDeleted;
            if(file->mediaFile)
            {
                error = 0;
                if(actualSize)
                {
                    // Let media logic generate data for media files.
                    if(!file->mediaFile->ReadMedia(this,fileOffset,buffer,
                        actualSize))
                    {
                        actualSize = 0;
                        error = pfmErrorCorruptData;
                    }
                }
            }
            break;
        }
    }
    if(error)
    {
        actualSize = 0;
    }
    *outActualSize = actualSize;
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Write(
    int64_t openId,
    uint64_t fileOffset,
    const void* buffer,
    size_t requestedSize,
    size_t* outActualSize)
{
    // Write data to open file.
    size_t actualSize = 0;
    FileNode* file;
    int error = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!error)
    {
        error = FileWrite(file,maxScriptDataSize,fileOffset,buffer,
            requestedSize,&actualSize);
    }
    *outActualSize = actualSize;
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::SetSize(
    int64_t openId,
    uint64_t fileSize)
{
    // Extend or truncate file data.
    FileNode* file;
    int error = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!error)
    {
        if(fileSize < file->fileSize)
        {
            file->fileSize = fileSize;
        }
        else
        {
            error = FileWrite(file,maxScriptDataSize,fileSize,0,0,0);
        }
    }
    return error;
}

int/*error*/ CCALL AvfsFormatter::Volume::Capacity(
    uint64_t* outTotalCapacity,
    uint64_t* availableCapacity)
{
    // Return total and available capacity of media.
    uint64_t totalCapacity = *availableCapacity = 1000000;
    // Won't make much difference, but return capacity that
    // accounts for the size of all the virtual files.
    FileNode* file = firstFile;
    while(file)
    {
        totalCapacity += file->fileSize;
        file = file->nextFile;
    }
    *outTotalCapacity = totalCapacity;
    return 0;
}

int/*error*/ CCALL AvfsFormatter::Volume::FlushMedia(
    bool* mediaClean)
{
    // Called after ~1 sec of inactivity. Flush modified data to
    // disk.

    // Check to see if echo script has been modified and
    // if so, copy to real script file and have media logic
    // process the _new script.
    int error = 0;
    HANDLE handle;
    size_t transferredSize;
    FileNode* newLogFile;
    // Find a script file that matches the original
    // echo file name.
    scriptFile = firstFile;
    while(scriptFile && (scriptFile->fileType != fileTypeScript ||
        sscmpi(scriptFile->name,scriptEndName) != 0))
    {
        scriptFile = scriptFile->nextFile;
    }
    // If the write time of the echo script file doesn't
    // match the real script file then it has been modified
    // and media needs to reinitialize.
    if(scriptFile && scriptFile->writeTime != scriptWriteTime)
    {
        // Rewrite real script file to match the echo script
        // file.
        ASSERT(scriptFile->fileSize <= maxScriptDataSize);
        scriptWriteTime = scriptFile->writeTime;
        // If unable to open or write the real script file
        // then will return an error to driver. Driver will
        // display a cache write failure notification. Script
        // changes will be lost.
        error = marshaller->ConvertSystemError(::FileOpenWrite(
            scriptFileName,&handle));
        if(!error)
        {
            if(scriptFile->fileSize)
            {
                error = marshaller->ConvertSystemError(::FileWrite(
                    handle,scriptFile->fileData,
                    static_cast<size_t>(scriptFile->fileSize),
                    &transferredSize));
                if(!error && transferredSize != scriptFile->fileSize)
                {
                    error = pfmErrorNoSpace;
                }
            }
            if(!error)
            {
                error = marshaller->ConvertSystemError(::FileSetSize(
                    handle,scriptFile->fileSize));
            }
            ::FileSetTime(handle,scriptWriteTime);
            ::FileClose(handle);
        }
        if(!error)
        {
            // Delete the existing media files. Any that are still open
            // will stick around until closed but won't work and will
            // not have names.
            DeleteMediaFiles();

            // New log file.
            newLogFile = new(std::nothrow) FileNode(this,0,fileTypeLog,
                pfmTimeInvalid);
            if(newLogFile)
            {
                newLogFile->name = ssdup(L"error.log");
                if(!newLogFile->name)
                {
                    delete newLogFile;
                }
                else
                {
                    ssfree(logFile->name);
                    logFile->name = 0;
                    if(!logFile->open)
                    {
                        delete logFile;
                    }
                    logFile = newLogFile;
                }
            }

            // No error handling here for bad scripts. User can edit
            // script to fix errors. Media logic will need to report
            // errors through error log file.
            AvfsProcessScript(this,this);
        }
    }
    scriptFile = 0;
    *mediaClean = true;
    return 0;
}

int/*error*/ CCALL AvfsFormatter::Volume::Control(
    int64_t openId,
    int8_t accessLevel,
    int controlCode,
    const void* input,
    size_t inputSize,
    void* output,
    size_t maxOutputSize,
    size_t* outputSize)
{
    // If we needed to tunnel control codes through the file system
    // using the PFM API then this is where they would end up.
    return pfmErrorInvalid;
}

int/*error*/ CCALL AvfsFormatter::Volume::MediaInfo(
    int64_t openId,
    PfmMediaInfo* mediaInfo,
    wchar_t** mediaLabel)
{
    return 0;
}

AvfsFormatter::Volume::Volume(void)
{
    marshaller = 0;
    scriptFileName = 0;
    scriptEndName = 0;
    mediaName = 0;
    scriptWritable = false;
    scriptWriteTime = 0;
    scriptFile = 0;
    firstList = 0;
    firstFile = 0;
}

AvfsFormatter::Volume::~Volume(void)
{
    if(marshaller)
    {
        marshaller->Release();
    }
    ssfree(scriptFileName);
    ssfree(mediaName);
    while(firstList)
    {
        delete firstList;
    }
    while(firstFile)
    {
        delete firstFile;
    }
}

int/*systemError*/ AvfsFormatter::Volume::Init(
    const wchar_t* inScriptFileName)
{
    wchar_t* dot;
    FileNode* folder;
    HANDLE scriptHandle = INVALID_HANDLE_VALUE;
    uint64_t scriptFileSize;
    size_t scriptDataSize = 0;
    int error = PfmMarshallerFactory(&marshaller);
    if(!error)
    {
        marshaller->SetTrace(L"VSFS-PFM");
        scriptFileName = ssdup(inScriptFileName);
        scriptEndName = ssrchr(scriptFileName,'\\');
        if(scriptEndName)
        {
            scriptEndName ++;
        }
        else
        {
            scriptEndName = scriptFileName;
        }
        mediaName = ssdup(scriptEndName);
        dot = ssrchr(mediaName,'.');
        if(dot && dot > mediaName)
        {
            *dot = 0;
        }
        if(!scriptFileName || !mediaName)
        {
            error = ERROR_OUTOFMEMORY;
        }
    }
    if(!error)
    {
        // Create root folder file. Do this first so it will show
        // up first in listing.
        error = ERROR_OUTOFMEMORY;
        folder = new(std::nothrow) FileNode(this,0,fileTypeFolder,
            pfmTimeInvalid);
        if(folder)
        {
            folder->name = ssdup(L".");
            if(folder->name)
            {
                error = 0;
            }
        }
    }
    if(!error)
    {
        // Create initial script echo file, to allow reading/writing
        // the script while mounted.
        error = ERROR_OUTOFMEMORY;
        scriptFile = new(std::nothrow) FileNode(this,0,fileTypeScript,
            pfmTimeInvalid);
        if(scriptFile)
        {
            scriptFile->name = ssdup(scriptEndName);
            if(scriptFile->name)
            {
                error = 0;
            }
        }
        if(!error)
        {
            // If real script file can not be opened for write access
            // then do not allow the echo script file to be modified.
            scriptWritable = true;
            error = ::FileOpenWrite(scriptFileName,&scriptHandle);
            if(error)
            {
                scriptWritable = false;
                error = ::FileOpenRead(scriptFileName,&scriptHandle);
            }
        }
        if(!error)
        {
            scriptFileSize = ::FileGetSize(scriptHandle);
            scriptDataSize = static_cast<size_t>(scriptFileSize);
            if(scriptFileSize > maxScriptDataSize)
            {
                error = ERROR_HANDLE_DISK_FULL;
            }
        }
        if(!error)
        {
            // Save a copy of the script data for the volume and
            // for the echo file.
            scriptFile->fileData = malloc(scriptDataSize);
            if(!scriptFile->fileData)
            {
                error = ERROR_OUTOFMEMORY;
            }
            else
            {
                memset(scriptFile->fileData,0,scriptDataSize);
                ::FileSetPointer(scriptHandle,0);
                ::FileRead(scriptHandle,scriptFile->fileData,scriptDataSize,0);
                scriptFile->maxFileDataSize = scriptDataSize;
                scriptFile->fileSize = scriptDataSize;
            }
            ::FileGetTime(scriptHandle,&scriptWriteTime);
            scriptFile->writeTime = scriptWriteTime;
        }
    }
    if(!error)
    {
        // Create log file to report errors and log actions.
        error = ERROR_OUTOFMEMORY;
        logFile = new(std::nothrow) FileNode(this,0,fileTypeLog,
            pfmTimeInvalid);
        if(logFile)
        {
            logFile->name = ssdup(L"error.log");
            if(logFile->name)
            {
                error = 0;
            }
        }
    }
    ::FileClose(scriptHandle);
    if(!error)
    {
        // No error handling here for bad scripts. User can edit
        // script to fix errors. Media logic will need to report
        // errors through error log file.
        AvfsProcessScript(this,this);
    }
    scriptFile = 0;
    return error;
}

void AvfsFormatter::Volume::Destroy(void)
{
    DeleteMediaFiles();
}

void CCALL AvfsFormatter::Release(void)
{
}

int/*systemError*/ CCALL AvfsFormatter::Identify(
    HANDLE statusWrite,
    const wchar_t* mountFileName,
    HANDLE mountFileHandle,
    const void* mountFileData,
    size_t mountFileDataSize)
{
    PfmMarshaller* marshaller;
    int error = PfmMarshallerFactory(&marshaller);
    if(!error)
    {
        // Mount any file with an .vpy extension
        if(sscmpi(ssrchr(mountFileName,'.'),L".vpy") != 0)
        {
            // or that has "#!vpy" at start of file.
            error = marshaller->Identify(
                static_cast<const char*>(mountFileData),
                mountFileDataSize/sizeof(char),avisynthFileTypeTag);
        }
        marshaller->Release();
    }
    return error;
}

int/*systemError*/ CCALL AvfsFormatter::Serve(
    const wchar_t* mountFileName,
    int mountFlags,
    HANDLE read,
    HANDLE write)
{
    Volume volume;
    int error = volume.Init(mountFileName);
    if(!error)
    {
        volume.marshaller->ServeReadWrite(&volume,0,avfsFormatterName,read,write);
    }
    volume.Destroy();
    return error;
}

extern "C" int/*systemError*/ CCALL PfmFormatterFactory1(
    PfmFormatter** formatter)
{
    *formatter = &avfsFormatter;
    return 0;
}
