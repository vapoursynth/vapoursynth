//----------------------------------------------------------------------------
// Copyright 2008-2015 Joe Lowe
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
// file name:  avfspfm.cpp
// created:    2008.01.04
//----------------------------------------------------------------------------
// Notes:
//
// This module implements a simple virtual file system for
// use with Avisynth and the Pismo File Mount Audit Package.
// The file system mounts an Avisynth script file, presenting
// it as a volume containing virtual files that represent the
// the media stream produced by the script.
//
// The file system monitors the script file for changes. A
// few seconds after the echo script file is modified,
// Avisynth will be reinitialized and the virtual media files
// will change.
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
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <wchar.h>
#include <new>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ss.h"
#include "files.h"
#include "include/pfmapi.h"
#include "include/pfmmarshaller.h"
#include "avfspfm.h"
#include "avfs.rc"
#include "../common/vsutf16.h"
#include <VSScript4.h>

#define CCALL __cdecl

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
    int64_t fileId;
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

struct Volume final:
    AvfsLog_,
    AvfsVolume_,
    PfmFormatterDispatch
{
    PfmMarshaller* marshaller;
    wchar_t* scriptFileName;
    wchar_t* scriptFolderName;
    const wchar_t* scriptEndName;
    wchar_t* mediaName;
    bool scriptWritable;
    int64_t scriptWriteTime;
    FileNode* scriptFile;
    FileNode* logFile;
    ListState* firstList;
    FileNode* firstFile;
    int64_t lastFileId;

    // AvfsLog_
    void Print(const wchar_t* data);
    void Vprintf(const wchar_t* format,va_list args);
    void Printf(const wchar_t* format,...);
    void Line(const wchar_t* data);

    ListState* ListFind(int64_t listId);
    int/*pfmError*/ FileFindName(const PfmNamePart* nameParts,size_t namePartCount,FileNode** file);
    int/*pfmError*/ FileFindOpenId(int64_t openId,bool forModify,FileNode** file);
    void FileOpened(FileNode* file,PfmOpenAttribs* openAttribs);
    int/*pfmError*/ FileFactory(int64_t openId,int8_t createFileType,int64_t writeTime,FileNode** outFile);
    void FileTouchParent(FileNode* file,int64_t writeTime);
    int/*pfmError*/ FileOpenOrMove(const PfmNamePart* nameParts,size_t namePartCount,FileNode* sourceFile,int64_t newExistingOpenId,bool deleteSource,int notFoundError,int64_t writeTime,FileNode** outFile,bool* existed);
    void FileDelete(FileNode* file,int64_t writeTime);
    int/*pfmError*/ FileReplace(FileNode* sourceFile,FileNode* targetFile,bool deleteSource,int64_t writeTime);
    int/*pfmError*/ FileWrite(FileNode* file,size_t maxFileDataSize,uint64_t fileOffset,const void* buffer,size_t requestedSize,size_t* actualSize);
    int64_t FileParentId(FileNode* file)
        { return file?firstFile->fileId:0; }
    const wchar_t* FileEndName(FileNode* file)
        { return file?file->name:0; }
    void DeleteMediaFiles(int64_t writeTime);
    void ProcessScript(void);

    // AvfsVolume_
    const wchar_t* GetScriptFileName(void);
    const wchar_t* GetScriptEndName(void);
    const wchar_t* GetMediaName(void);
    const char* GetScriptData(void);
    size_t GetScriptDataSize(void);
    void CreateMediaFile(AvfsMediaFile_* mediaFile,const wchar_t* fileName,uint64_t fileSize);

    // PfmFormatterDispatch
    void CCALL Open(PfmMarshallerOpenOp*,void*);
    void CCALL Replace(PfmMarshallerReplaceOp*,void*);
    void CCALL Move(PfmMarshallerMoveOp*,void*);
    void CCALL MoveReplace(PfmMarshallerMoveReplaceOp*,void*);
    void CCALL Delete(PfmMarshallerDeleteOp*,void*);
    void CCALL Close(PfmMarshallerCloseOp*,void*);
    void CCALL FlushFile(PfmMarshallerFlushFileOp*,void*);
    void CCALL List(PfmMarshallerListOp*,void*);
    void CCALL ListEnd(PfmMarshallerListEndOp*,void*);
    void CCALL Read(PfmMarshallerReadOp*,void*);
    void CCALL Write(PfmMarshallerWriteOp*,void*);
    void CCALL SetSize(PfmMarshallerSetSizeOp*,void*);
    void CCALL Capacity(PfmMarshallerCapacityOp*,void*);
    void CCALL FlushMedia(PfmMarshallerFlushMediaOp*,void*);
    void CCALL Control(PfmMarshallerControlOp*,void*);
    void CCALL MediaInfo(PfmMarshallerMediaInfoOp*,void*);
    void CCALL Access(PfmMarshallerAccessOp*,void*);
    void CCALL ReadXattr(PfmMarshallerReadXattrOp*,void*);
    void CCALL WriteXattr(PfmMarshallerWriteXattrOp*,void*);

    Volume(void);
    ~Volume(void);
    int/*error*/ Init(const wchar_t* scriptFileName);
    void Destroy(void);
};

ListState::ListState(
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

ListState::~ListState(void)
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

FileNode::FileNode(
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
    fileId = ++volume->lastFileId;
    open = false;
    fileType = inFileType;
    writeTime = inWriteTime;
    fileSize = 0;
    maxFileDataSize = 0;
    fileData = 0;
    mediaFile = 0;
}

FileNode::~FileNode(void)
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
    free(name);
    if(fileData)
    {
        free(fileData);
    }
    if(mediaFile)
    {
        mediaFile->Release();
    }
}

void Volume::Print(const wchar_t* data)
{
    marshaller->Print(data);
    std::string data8 = utf16_to_utf8(data);
    if(!data8.empty())
    {
        FileWrite(logFile,maxLogDataSize,logFile->fileSize,data8.data(),
            data8.size(),0);
    }
}

void Volume::Vprintf(const wchar_t* format,va_list args)
{
    marshaller->Vprintf(format,args);
    wchar_t* data = ssvformatalloc(format,args);
    Print(data);
    free(data);
}

void Volume::Printf(const wchar_t* format,...)
{
    va_list args;
    va_start(args,format);
    Vprintf(format,args);
}

void Volume::Line(const wchar_t* data)
{
    Printf(L"%s\r\n",data);
}

ListState* Volume::ListFind(
    int64_t listId)
{
    ListState* list = firstList;
    while(list && list->listId != listId)
    {
        list = list->nextList;
    }
    return list;
}

int/*pfmError*/ Volume::FileFindName(
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

int/*pfmError*/ Volume::FileFindOpenId(
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

void Volume::FileOpened(
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
        openAttribs->accessLevel = pfmAccessLevelWriteData;
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
        openAttribs->attribs.extraFlags |= pfmExtraFlagOffline;
    }
}

int/*pfmError*/ Volume::FileFactory(
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

void Volume::FileTouchParent(FileNode* file,int64_t writeTime)
{
        // Update time stamp on folder.
    firstFile->writeTime = writeTime;
}

int/*pfmError*/ Volume::FileOpenOrMove(
    const PfmNamePart* nameParts,
    size_t namePartCount,
    FileNode* sourceFile,
    int64_t newExistingOpenId,
    bool deleteSource,
    int notFoundError,
    int64_t writeTime,
    FileNode** outFile,
    bool* outExisted)
{
        // Open existing file, or move source file to
        // the non-existing file name.
    int perr;
    FileNode* file;
    bool existed = false;
    wchar_t* name;

    perr = FileFindName(nameParts,namePartCount,&file);
    if(!perr)
    {
        ASSERT(file);
        existed = true;
        // Use driver supplied open ID if this file has never
        // been opened.
        if(!file->openId)
        {
            file->openId = newExistingOpenId;
        }
    }
    else if(perr == pfmErrorNotFound)
    {
        ASSERT(namePartCount == 1);
        if(notFoundError)
        {
            perr = notFoundError;
        }
        if(sourceFile)
        {
            ASSERT(sourceFile->openId);
            // Don't support hard links, but do support restoring
            // deleted (no name) files.
            perr = pfmErrorInvalid;
            if(deleteSource || !sourceFile->name)
            {
                perr = pfmErrorOutOfMemory;
                name = ssdup(nameParts[0].name);
                if(name)
                {
                    perr = 0;
                    free(sourceFile->name);
                    sourceFile->name = name;
                    file = sourceFile;
                    FileTouchParent(file,writeTime);
                }
            }
        }
    }

    if(outFile)
    {
        *outFile = file;
    }
    if(outExisted)
    {
        *outExisted = existed;
    }
    return perr;
}

void Volume::FileDelete(FileNode* file,int64_t writeTime)
{
    if(file)
    {
        if(file->name)
        {
            free(file->name);
            file->name = 0;
            FileTouchParent(file,writeTime);
        }
        if(!file->open)
        {
            delete file;
        }
    }
}

int/*pfmError*/ Volume::FileReplace(
    FileNode* sourceFile,
    FileNode* targetFile,
    bool deleteSource,
    int64_t writeTime)
{
        // Delete target file and rename source file to target
        // files name.
    int perr = 0;

    // Only script files can be moved/deleted/modified.
    if(sourceFile->fileType != fileTypeScript ||
        targetFile->fileType != fileTypeScript)
    {
        perr = pfmErrorAccessDenied;
    }
    // Don't support hard links, but do support restoring
    // deleted (no name) files. Target can not already be
    // deleted.
    else if(!targetFile->name || (!deleteSource && sourceFile->name))
    {
        perr = pfmErrorInvalid;
    }
    if(!perr)
    {
        free(sourceFile->name);
        sourceFile->name = targetFile->name;
        targetFile->name = 0;
        FileTouchParent(sourceFile,writeTime);
    }

    return perr;
}

int/*error*/ Volume::FileWrite(
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

void Volume::DeleteMediaFiles(int64_t writeTime)
{
    // Delete all media files.
    FileNode* file;
    FileNode* next = firstFile;
    while((file = next) != 0)
    {
        next = file->nextFile;
        if(file->fileType == fileTypeMedia && file->name)
        {
            if(file->mediaFile)
            {
                file->mediaFile->Release();
                file->mediaFile = 0;
            }
            FileDelete(file,writeTime);
        }
    }
}

void Volume::ProcessScript(void)
{
        // Check to see if echo script and/or real script has been
        // modified. If real script modified then create new echo
        // script. If echo script modified then reinitialize the
        // media logic. Changes to real script file take precedence
        // over changes to echo script file.
    int error = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;
    size_t transferredSize;
    FileNode* newLogFile;
    uint64_t fileSize;
    size_t dataSize = 0;
    FileNode* file;
    int64_t writeTime;

        // Find existing echo script file.
    file = firstFile;
    while(file && (file->fileType != fileTypeScript ||
        sscmpi(file->name,scriptEndName) != 0))
    {
        file = file->nextFile;
    }

        // Check if real script file has been modified then
        // update the echo script file to match.
    if(::FileGetTime2(scriptFileName,&writeTime) == 0 &&
        writeTime != scriptWriteTime)
    {
        if(file)
        {
            FileDelete(file,writeTime);
        }
        file = new(std::nothrow) FileNode(this,0,fileTypeScript,writeTime);
        if(!file)
        {
            error = ERROR_OUTOFMEMORY;
        }
        else
        {
            file->name = ssdup(scriptEndName);
            if(!file->name)
            {
                delete file;
                file = 0;
            }
        }
        if(!error)
        {
                // If real script file can not be opened for write access
                // then do not allow the echo script file to be modified.
            scriptWritable = true;
            error = ::FileOpenWrite(scriptFileName,&handle);
            if(error)
            {
                scriptWritable = false;
                error = ::FileOpenRead(scriptFileName,&handle);
            }
        }
        if(!error)
        {
            fileSize = ::FileGetSize(handle);
            dataSize = 0;
            if(fileSize <= maxScriptDataSize)
            {
                dataSize = static_cast<size_t>(fileSize);
            }
            free(file->fileData);
            file->fileData = malloc(dataSize);
            if(file->fileData)
            {
                memset(file->fileData,0,dataSize);
                ::FileSetPointer(handle,0);
                ::FileRead(handle,file->fileData,dataSize,0);
                file->maxFileDataSize = dataSize;
                file->fileSize = dataSize;
            }
        }
        ::FileCleanup(&handle);
    }
        // If echo script file has been modified then update
        // the real script file to match.
    else if(file && file->writeTime != scriptWriteTime)
    {
        error = ::FileOpenWrite(scriptFileName,&handle);
        if(!error)
        {
            if(file->fileSize)
            {
                ::FileWrite(handle,file->fileData,
                    static_cast<size_t>(file->fileSize),
                    &transferredSize);
            }
            ::FileSetSize(handle,file->fileSize);
            ::FileSetTime(handle,file->writeTime);
            ::FileCleanup(&handle);
            ::FileGetTime2(scriptFileName,&file->writeTime);
        }
        ::FileCleanup(&handle);
        ::FileGetTime2(scriptFolderName,&writeTime);
    }
        // If the write time of the echo script file does not
        // match the real script file then it has been modified
        // and media needs to reinitialize.
    if(file && file->writeTime != scriptWriteTime)
    {
        scriptWriteTime = file->writeTime;
            // Delete the existing media files. Any that are still open
            // will stick around until closed but won't work and will
            // not have names.
        DeleteMediaFiles(writeTime);
            // New log file.
        newLogFile = new(std::nothrow) FileNode(this,0,fileTypeLog,
            scriptWriteTime);
        if(newLogFile)
        {
            newLogFile->name = ssdup(L"error.log");
            if(!newLogFile->name)
            {
                delete newLogFile;
            }
            else
            {
                FileDelete(logFile,writeTime);
                logFile = newLogFile;
            }
        }
            // No error handling here for bad scripts. User can edit
            // script to fix errors. Media logic will need to report
            // errors through error log file.
        scriptFile = file;
        static HMODULE lib = LoadLibrary(L"VSScript.dll");
        if ((!sscmpi(ssrchr(scriptFile->name, '.'), L".vpy") || !sscmpi(ssrchr(scriptFile->name, '.'), L".py")) && lib && getVSScriptAPI(VSSCRIPT_API_VERSION))
            VsfsProcessScript(this, this);
        else
            AvfsProcessScript(this, this);
        scriptFile = 0;
    }
}

const wchar_t* Volume::GetScriptFileName(void)
{
    return scriptFileName;
}

const wchar_t* Volume::GetScriptEndName(void)
{
    return scriptEndName;
}

const wchar_t* Volume::GetMediaName(void)
{
    return mediaName;
}

const char* Volume::GetScriptData()
{
    ASSERT(scriptFile);
    return static_cast<char*>(scriptFile->fileData);
}

size_t Volume::GetScriptDataSize(void)
{
    ASSERT(scriptFile);
    return static_cast<size_t>(scriptFile->fileSize);
}

void Volume::CreateMediaFile(
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

static const PfmOpenAttribs zeroOpenAttribs = {};

void CCALL Volume::Open(PfmMarshallerOpenOp* op,void* unused)
{
        // Open or create of a file or folder.
    const PfmNamePart* nameParts = op->NameParts();
    size_t namePartCount = op->NamePartCount();
    int8_t createFileType = op->CreateFileType();
    int64_t writeTime = op->WriteTime();
    int64_t newCreateOpenId = op->NewCreateOpenId();
    int64_t newExistingOpenId = op->NewExistingOpenId();
    bool existed = false;
    PfmOpenAttribs openAttribs = zeroOpenAttribs;
    FileNode* newFile;
    FileNode* file;
    int notFoundPerr;
    int perr;

        // Many editors save files using a create/delete/rename
        // sequence. Must support file creation in order to allow
        // script file to be edited.
        // Create a _new unnamed source file to be moved to the
        // specified file name if it does not exist.
    notFoundPerr = FileFactory(newCreateOpenId,createFileType,
        writeTime,&newFile);
    perr = FileOpenOrMove(nameParts,namePartCount,newFile,
        newExistingOpenId,false/*deleteSource*/,notFoundPerr,writeTime,
        &file,&existed);
    if(!perr)
    {
        FileOpened(file,&openAttribs);
    }

    op->Complete(perr,existed,&openAttribs,FileParentId(file),
        FileEndName(file),0/*linkNamePartCount*/,0/*linkData*/,
        0/*linkDataSize*/,0/*serializeOpen*/);
    if(newFile && !newFile->open)
    {
        delete newFile;
    }
}

void CCALL Volume::Replace(PfmMarshallerReplaceOp* op,void* unused)
{
        // Replace an existing file with a _new file.
    int64_t targetOpenId = op->TargetOpenId();
    int64_t writeTime = op->WriteTime();
    int64_t newCreateOpenId = op->NewCreateOpenId();
    PfmOpenAttribs openAttribs = zeroOpenAttribs;
    FileNode* newFile = 0;
    FileNode* file = 0;
    FileNode* targetFile;
    int perr;

    perr = FileFindOpenId(targetOpenId,true/*forModify*/,&targetFile);
    if(!perr)
    {
            // Create the _new unnamed file object to move to the name
            // of the target.
        perr = FileFactory(newCreateOpenId,targetFile->fileType,writeTime,
            &newFile);
    }
    if(!perr)
    {
            // Delete the target and move _new file to target name.
        perr = FileReplace(newFile,targetFile,false/*deleteSource*/,writeTime);
        if(!perr)
        {
            file = newFile;
        }
    }
    if(!perr)
    {
        FileOpened(file,&openAttribs);
    }

    op->Complete(perr,&openAttribs,0/*serializeOpen*/);
    if(newFile && !newFile->open)
    {
        delete newFile;
    }
}

void CCALL Volume::Move(PfmMarshallerMoveOp* op,void* unused)
{
        // Open an existing target file, or move a previously opened
        // file to a _new target name if the target does not exist.
    int64_t sourceOpenId = op->SourceOpenId();
    const PfmNamePart* targetNameParts = op->TargetNameParts();
    size_t targetNamePartCount = op->TargetNamePartCount();
    bool deleteSource = !!op->DeleteSource();
    int64_t writeTime = op->WriteTime();
    int64_t newExistingOpenId = op->NewExistingOpenId();
    bool existed = false;
    PfmOpenAttribs openAttribs = zeroOpenAttribs;
    FileNode* sourceFile;
    FileNode* file = 0;
    int perr;

    perr = FileFindOpenId(sourceOpenId,true/*forModify*/,&sourceFile);
    if(!perr)
    {
        perr = FileOpenOrMove(targetNameParts,targetNamePartCount,
            sourceFile,newExistingOpenId,deleteSource,pfmErrorNotFound,
            writeTime,&file,&existed);
        ASSERT(perr != pfmErrorNotFound);
    }
    if(!perr)
    {
        FileOpened(file,&openAttribs);
    }

    op->Complete(perr,existed,&openAttribs,FileParentId(file),
        FileEndName(file),0/*linkNamePartCount*/,0/*linkData*/,
        0/*linkDataSize*/,0/*serializeOpen*/);
}

void CCALL Volume::MoveReplace(PfmMarshallerMoveReplaceOp* op,void* unused)
{
        // Delete an previously opened target file and move a
        // previously opened source file to the target files name.
    int64_t sourceOpenId = op->SourceOpenId();
    int64_t targetOpenId = op->TargetOpenId();
    bool deleteSource = !!op->DeleteSource();
    int64_t writeTime = op->WriteTime();
    FileNode* sourceFile = 0;
    FileNode* targetFile;
    int perr;

    perr = FileFindOpenId(targetOpenId,true/*forModify*/,&targetFile);
    if(!perr)
    {
        perr = FileFindOpenId(sourceOpenId,true/*forModify*/,&sourceFile);
    }
    if(!perr)
    {
            // Delete the target and move _new file to target
            // name.
        perr = FileReplace(sourceFile,targetFile,deleteSource,writeTime);
    }

    op->Complete(perr);
}

void CCALL Volume::Delete(PfmMarshallerDeleteOp* op,void* unused)
{
        // Delete a previously opened file.
    int64_t openId = op->OpenId();
    int64_t writeTime = op->WriteTime();
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!perr)
    {
        FileDelete(file,writeTime);
    }

    op->Complete(perr);
}

void CCALL Volume::Close(PfmMarshallerCloseOp* op,void* unused)
{
        // If no more references to file then free associated
        // resources.
    int64_t openId = op->OpenId();
    int64_t openSequence = op->OpenSequence();
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!perr)
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

    op->Complete(perr);
}

void CCALL Volume::FlushFile(PfmMarshallerFlushFileOp* op,void* unused)
{
        // Update file attributes and commit file data to disk.
    int64_t openId = op->OpenId();
    int64_t writeTime = op->WriteTime();
    PfmOpenAttribs openAttribs = zeroOpenAttribs;
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!perr)
    {
        if(writeTime != pfmTimeInvalid)
        {
            file->writeTime = writeTime;
        }
        FileOpened(file,&openAttribs);
    }

    op->Complete(perr,&openAttribs,0/*serializeOpen*/);
}

void CCALL Volume::List(PfmMarshallerListOp* op,void* unused)
{
        // List the contents of a folder.
    int64_t openId = op->OpenId();
    int64_t listId = op->ListId();
    size_t position = 0;
    FileNode* folder;
    FileNode* file;
    ListState* list = 0;
    int perr;
    bool noMore = true;
    bool added = true;
    PfmAttribs attribs;

    perr = FileFindOpenId(openId,false/*forModify*/,&folder);
        // Only supports one folder, so just need to verify
        // the file type.
    if(!perr && folder->fileType != fileTypeFolder)
    {
        perr = pfmErrorAccessDenied;
    }
    if(!perr)
    {
        // Find the associated list state, or if first time we've
        // seen this list ID then create a _new list state.
        list = ListFind(listId);
        if(!list)
        {
            list = new(std::nothrow) ListState(this,listId);
            if(!list)
            {
                perr = pfmErrorOutOfMemory;
            }
        }
    }
    if(!perr)
    {
        // Using simple index to remember position in list.
        file = firstFile;
        while(file && position < list->position)
        {
            file = file->nextFile;
        }
        while(file && added)
        {
            attribs = {};
            attribs.fileType = pfmFileTypeFile;
            if(file->fileType == fileTypeFolder)
            {
                attribs.fileType = pfmFileTypeFolder;
            }
            attribs.fileSize = file->fileSize;
            attribs.writeTime = file->writeTime;
            if(file->fileType == fileTypeMedia)
            {
                attribs.extraFlags |= pfmExtraFlagOffline;
            }
            added = !!op->Add(&attribs,file->name);
            list->position += !!added;
            file = file->nextFile;
        }
            // Save the driver calling us back again if there are
            // no more files.
        noMore = !file;
    }

    op->Complete(perr,noMore);
}

void CCALL Volume::ListEnd(PfmMarshallerListEndOp* op,void* unused)
{
        // Clean up any resources associated with an open folder
        // list operation.
    int64_t openId = op->OpenId();
    int64_t listId = op->ListId();
    FileNode* file;
    ListState* list;
    int perr;

    perr = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!perr && file->fileType == fileTypeFolder)
    {
        list = ListFind(listId);
        if(!list)
        {
            perr = pfmErrorInvalid;
        }
        else
        {
            delete list;
        }
    }

    op->Complete(perr);
}

void CCALL Volume::Read(PfmMarshallerReadOp* op,void* unused)
{
        // Read data from open file.
    int64_t openId = op->OpenId();
    uint64_t fileOffset = op->FileOffset();
    void* buffer = op->Data();
    size_t requestedSize = op->RequestedSize();
    size_t actualSize = 0;
    uint64_t maxSize;
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!perr)
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
            perr = pfmErrorAccessDenied;
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
            perr = pfmErrorDeleted;
            if(file->mediaFile)
            {
                perr = 0;
                if(actualSize)
                {
                    // Let media logic generate data for media files.
                    if(!file->mediaFile->ReadMedia(this,fileOffset,buffer,
                        actualSize))
                    {
                        actualSize = 0;
                        perr = pfmErrorCorruptData;
                    }
                }
            }
            break;
        }
    }

    if(perr)
    {
        actualSize = 0;
    }
    op->Complete(perr,actualSize);
}

void CCALL Volume::Write(PfmMarshallerWriteOp* op,void* unused)
{
        // Write data to open file.
    int64_t openId = op->OpenId();
    uint64_t fileOffset = op->FileOffset();
    const void* buffer = op->Data();
    size_t requestedSize = op->RequestedSize();
    size_t actualSize = 0;
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!perr)
    {
        perr = FileWrite(file,maxScriptDataSize,fileOffset,buffer,
            requestedSize,&actualSize);
    }

    op->Complete(perr,actualSize);
}

void CCALL Volume::SetSize(PfmMarshallerSetSizeOp* op,void* unused)
{
        // Extend or truncate file data.
    int64_t openId = op->OpenId();
    uint64_t fileSize = op->FileSize();
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,true/*forModify*/,&file);
    if(!perr)
    {
        if(fileSize < file->fileSize)
        {
            file->fileSize = fileSize;
        }
        else
        {
            perr = FileWrite(file,maxScriptDataSize,fileSize,0,0,0);
        }
    }

    op->Complete(perr);
}

void CCALL Volume::Capacity(PfmMarshallerCapacityOp* op,void* unused)
{
        // Return total and available capacity of media.
    uint64_t totalCapacity;
    uint64_t availableCapacity;
    FileNode* file;

    totalCapacity = availableCapacity = 1000000;
    // Won't make much difference, but return capacity that
    // accounts for the size of all the virtual files.
    file = firstFile;
    while(file)
    {
        totalCapacity += file->fileSize;
        file = file->nextFile;
    }

    op->Complete(0/*perr*/,totalCapacity,availableCapacity);
}

void CCALL Volume::FlushMedia(PfmMarshallerFlushMediaOp* op,void* unused)
{
        // Called after 2 seconds of inactivity. Flush modified data to
        // disk.
    ProcessScript();

        // Ask to be flushed again in 2 more seconds.
    op->Complete(0/*perr*/,2000);
}

void CCALL Volume::Control(PfmMarshallerControlOp* op,void* unused)
{
        // If we needed to tunnel control codes through the file system
        // using the PFM API then this is where they would end up.
    op->Complete(pfmErrorInvalid,0/*outputSize*/);
}

static const PfmMediaInfo zeroMediaInfo = {};

void CCALL Volume::MediaInfo(PfmMarshallerMediaInfoOp* op,void* unused)
{
    PfmMediaInfo mediaInfo = zeroMediaInfo;

    op->Complete(0,&mediaInfo,0/*mediaLabel*/);
}

void CCALL Volume::Access(PfmMarshallerAccessOp* op,void* unused)
{
    int64_t openId = op->OpenId();
    PfmOpenAttribs openAttribs = zeroOpenAttribs;
    FileNode* file;
    int perr;

    perr = FileFindOpenId(openId,false/*forModify*/,&file);
    if(!perr)
    {
        FileOpened(file,&openAttribs);
    }

    op->Complete(perr,&openAttribs,0/*serializeOpen*/);
}

void CCALL Volume::ReadXattr(PfmMarshallerReadXattrOp* op,void* unused)
{
    op->Complete(pfmErrorInvalid,0/*xattrSize*/,0/*transferred*/);
}

void CCALL Volume::WriteXattr(PfmMarshallerWriteXattrOp* op,void* unused)
{
    op->Complete(pfmErrorInvalid,0/*transferred*/);
}

Volume::Volume(void)
{
    marshaller = 0;
    scriptFileName = 0;
    scriptFolderName = 0;
    scriptEndName = 0;
    mediaName = 0;
    scriptWritable = false;
    scriptFile = 0;
    scriptWriteTime = 0;
    logFile = 0;
    firstList = 0;
    firstFile = 0;
    lastFileId = 2;
}

Volume::~Volume(void)
{
    if(marshaller)
    {
        marshaller->Release();
    }
    free(scriptFileName);
    free(mediaName);
    while(firstList)
    {
        delete firstList;
    }
    while(firstFile)
    {
        delete firstFile;
    }
}

int/*error*/ Volume::Init(
    const wchar_t* inScriptFileName)
{
    wchar_t* dot;
    FileNode* folder;
    int error = PfmMarshallerFactory(&marshaller);

    if(!error)
    {
        marshaller->SetTrace(L"AVFS-PFM");
        scriptFileName = ssdup(inScriptFileName);
        scriptEndName = ssrchr(scriptFileName,'\\');
        if(scriptEndName)
        {
            scriptEndName ++;
            scriptFolderName = ssdupn(scriptFileName,scriptEndName-scriptFileName);
        }
        else
        {
            scriptEndName = scriptFileName;
            scriptFolderName = ssdup(L".\\");
        }
        mediaName = ssdup(scriptEndName);
        dot = ssrchr(mediaName,'.');
        if(dot && dot > mediaName)
        {
            *dot = 0;
        }
        if(!scriptFileName || !scriptFolderName || !mediaName)
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
        ProcessScript();
    }

    return error;
}

int wmain(int argc,const wchar_t*const* argv)
{
    int error = 0;
    int argi;
    const wchar_t* arg;
    bool argProcessed;
    bool showHelp = false;
    PfmMountCreateParams mcp;
    bool diagnosticMode = false;
    bool serve = false;
    PfmFastPipeCreateParams pcp;
    PfmApi* pfm = 0;
    PfmMount* mount = 0;
    Volume* volume = 0;
    FILE* fstatus = stdout;
    HANDLE status = GetStdHandle(STD_OUTPUT_HANDLE);
    PfmMarshallerServeParams msp;

    PfmMountCreateParams_Init(&mcp);
    PfmMarshallerServeParams_Init(&msp);
    msp.toFormatterRead = INVALID_HANDLE_VALUE;
    msp.fromFormatterWrite = INVALID_HANDLE_VALUE;

    argi = 1;
    while(!error && argi < argc)
    {
        arg = argv[argi++];
        argProcessed = false;
        if((arg[0] == '-' || arg[0] == '/') && sslen(arg) == 2)
        {
            argProcessed = true;
            switch(tolower(arg[1]))
            {
            case 'h':
            case '?':
                showHelp = true;
                break;
            case 'd':
                diagnosticMode = true;
                break;
            case 's':
                serve = true;
                fstatus = stderr;
                status = GetStdHandle(STD_ERROR_HANDLE);
                break;
            default:
                fprintf(fstatus,"WARNING: Ignoring switch \"%ls\".\n",arg);
            }
        }
        if(!argProcessed)
        {
            if(mcp.mountName)
            {
                fprintf(fstatus,"WARNING: Ignoring arg \"%ls\".\n",arg);
            }
            else
            {
                mcp.mountName = arg;
            }
        }
    }

    if(argc == 1)
    {
        error = -2;
        fprintf(fstatus,
            VER_DESCRIPTION "\n"
            "Version R" XSTR(VAPOURSYNTH_CORE_VERSION) "\n"
            VER_COPYRIGHT "\n"
            "For help run: avfs -h\n");
    }
    else if(showHelp)
    {
        error = -2;
        fprintf(fstatus,
           "syntax: avfs [<switch> ...] <script file>\n"
           "switches:\n"
           "  -d  Print diagnostic info to stdout.\n"
           "  -s  Serve to stdin/stdout.\n");
    }


    size_t fnlen = sslen(mcp.mountName);
    if(!error && !fnlen)
    {
        error = -1;
        fprintf(fstatus,"ERROR: Must provide name of script to mount\n");
    }

    if (!error && (fnlen < 4 || (sscmpi(mcp.mountName + fnlen - 4, L".avs") && sscmpi(mcp.mountName + fnlen - 4, L".vpy")))) {
        error = -1;
        fprintf(fstatus, "ERROR: Only files with the extensions .vpy and .avs can be mounted\n");
    }

    if(!error)
    {
            // The API factory will fail if PFM is not installed.
        error = PfmApiFactory(&pfm);
        if(error)
        {
            fprintf(fstatus,"ERROR: Unable to open PFM interface. "
                "PFM probably not installed.\n");
        }
    }

    if(serve)
    {
            // Serve volume to stdin/stdout. With a little help from
            // other utilities, this can be used for remote mounting
            // a script.
        msp.toFormatterRead = GetStdHandle(STD_INPUT_HANDLE);
        msp.fromFormatterWrite = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    else
    {
        if(!error)
        {
                // For communication between driver and formatter, use
                // fastpipe if available.
            PfmFastPipeCreateParams_Init(&pcp);
            pcp.fastPipeFlags = pfmFastPipeFlagAsyncClient|
                pfmFastPipeFlagAsyncServer;
            error = pfm->FastPipeCreate(&pcp,&mcp.toFormatterWrite,
                &msp.toFormatterRead);
            if(!error)
            {
                    // FastPipe is bi-directional.
                msp.fromFormatterWrite = msp.toFormatterRead;
                mcp.fromFormatterRead = mcp.toFormatterWrite;
            }
            else
            {
                    // If no fastpipe then use a pair of pipes.
                error = FileCreatePipe(&msp.toFormatterRead,&mcp.toFormatterWrite);
                if(!error)
                {
                    error = FileCreatePipe(&mcp.fromFormatterRead,
                        &msp.fromFormatterWrite);
                }
            }
            if(error)
            {
                fprintf(fstatus,"ERROR: %i Unable to create pipes.\n",error);
            }
        }
        if(!error)
        {
            mcp.mountFlags |= pfmMountFlagWorldRead|pfmMountFlagWorldWrite|
                pfmMountFlagUnmountOnRelease;
            error = pfm->MountCreate(&mcp,&mount);
            if(error)
            {
                fprintf(fstatus,"ERROR: %i Unable to create mount.\n",error);
            }
        }

            // Close client end handles now. Client has duplicated
            // what it needs. If this is not done then the marshaller
            // will not automatically exit if the client unexpectedly
            // disconnects.
        FileCleanupPair(&mcp.toFormatterWrite,&mcp.fromFormatterRead);
    }

    if(!error)
    {
        volume = new(std::nothrow) Volume;
        if(!volume)
        {
            error = ERROR_OUTOFMEMORY;
        }
        else
        {
            error = volume->Init(mcp.mountName);
        }
        if(error)
        {
            fprintf(fstatus,"ERROR: %i Initializing volume.\n",error);
        }
    }
    if(!error)
    {
        if(diagnosticMode)
        {
            volume->marshaller->SetStatus(status);
        }
        msp.dispatch = volume;
        msp.formatterName = "avfs";
        msp.dataAlign = 0x40;
        msp.maxDirectMsgSize = 0x100000;
        msp.volumeFlags = pfmVolumeFlagNoCreateTime;
        msp.volumeFlags = pfmVolumeFlagNoAccessTime;
        msp.volumeFlags = pfmVolumeFlagNoChangeTime;
        fwprintf(fstatus, L"Mount point: %s\n", mount->GetMountPoint());
        fprintf(fstatus,"Press CTRL+C to exit.\n");
        error = volume->marshaller->ServeDispatch(&msp);
        if(error)
        {
            fprintf(fstatus,"ERROR: %i Serving volume.\n",error);
        }
    }

    FileCleanupPair(&msp.toFormatterRead,&msp.fromFormatterWrite);
    delete volume;
    if(pfm)
    {
        pfm->Release();
    }
    if(mount)
    {
        mount->Release();
    }
    if(error == -2)
    {
        error = 0;
    }
    PfmApiUnload();
    return error;
}
