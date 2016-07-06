#include "assertive.h"
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <wchar.h>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "files.h"

void FileFailCriticalErrors(void)
{
   static bool initialized = false;
   if(!initialized)
   {
      initialized = true;
      SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
   }
}

void FileClose(HANDLE handle)
{
    if(handle && handle != INVALID_HANDLE_VALUE)
    {
        VERIFY(CloseHandle(handle));
    }
}

int/*error*/ FileOpenRead(
    const wchar_t* fileName,
    HANDLE* outHandle)
{
    FileFailCriticalErrors();
    int error = 0;
    HANDLE handle = CreateFileW(fileName,GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,
        FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM,0);
    if(handle == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
    }
    ASSERT(!error == (handle != INVALID_HANDLE_VALUE));
    *outHandle = handle;
    return error;
}

int/*error*/ FileOpenWrite(
    const wchar_t* fileName,
    HANDLE* outHandle)
{
    FileFailCriticalErrors();
    int error = 0;
    HANDLE handle = CreateFileW(fileName,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,0,0);
    if(handle == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
    }
    ASSERT(!error == (handle != INVALID_HANDLE_VALUE));
    *outHandle = handle;
    return error;
}

int/*error*/ FileIo(
    HANDLE fileHandle,
    char* buffer,
    size_t count,
    size_t* bytesTransferred,
    BOOL (__stdcall*ioFunc)(HANDLE,void*,DWORD,DWORD*,OVERLAPPED*))
{
    // Break I/O into multiple calls if count is bigger than system call
    // supports.
    int error = 0;
    if(!fileHandle || fileHandle == INVALID_HANDLE_VALUE)
    {
        error = ERROR_INVALID_HANDLE;
    }
    size_t remainingCount = count;
    DWORD subBytesTransferred = 0;
    unsigned subCount = 0;
    while(!error && subBytesTransferred == subCount && remainingCount)
    {
        subCount = (INT_MAX/16384)*16384;
        if(subCount > remainingCount)
        {
            subCount = static_cast<unsigned>(remainingCount);
        }
        subBytesTransferred = 0;
        if(!ioFunc(fileHandle,buffer+count-remainingCount,subCount,
           &subBytesTransferred,0))
        {
            error = GetLastError();
            ASSERT(error);
        }
        if(error)
        {
            switch(error)
            {
            case 0:
                break;
            case ERROR_BROKEN_PIPE:
            case ERROR_NO_DATA:
            case ERROR_DISK_FULL:
            case ERROR_HANDLE_EOF:
            case ERROR_HANDLE_DISK_FULL:
                ASSERT(!subBytesTransferred);
                subBytesTransferred = 0;
                error = 0;
                break;
            default:
                subBytesTransferred = 0;
                if(remainingCount < count)
                {
                    error = 0;
                }
                break;
            }
        }
        remainingCount -= subBytesTransferred;
    }
    ASSERT(!error || remainingCount == count);
    if(bytesTransferred)
    {
        *bytesTransferred = count-remainingCount;
    }
    return error;
}

int/*error*/ FileRead(
    HANDLE fileHandle,
    void* buffer,
    size_t count,
    size_t* bytesRead)
{
    return FileIo(fileHandle,static_cast<char*>(buffer),count,bytesRead,
        ReadFile);
}

int/*error*/ FileWrite(
    HANDLE fileHandle,
    const void* buffer,
    size_t count,
    size_t* bytesWritten)
{
    return FileIo(fileHandle,
        static_cast<char*>(const_cast<void*>(buffer)),count,bytesWritten,
        reinterpret_cast<BOOL (__stdcall*)(HANDLE,void*,DWORD,DWORD*,
        OVERLAPPED*)>(WriteFile));
}

int/*error*/ FilePointer(
    HANDLE fileHandle,
    UINT64 offset,
    int method,
    uint64_t* newOffset)
{
    int error = ERROR_INVALID_HANDLE;
    if(fileHandle != INVALID_HANDLE_VALUE)
    {
        LONG offsetHigh = static_cast<LONG>(offset>>32);
        UINT32 offsetLow = SetFilePointer(fileHandle,
            static_cast<UINT32>(offset),&offsetHigh,method);
        error = 0;
        if(offsetLow == UINT_MAX)
        {
            error = GetLastError();
        }
        if(newOffset)
        {
            *newOffset = (static_cast<uint64_t>(offsetHigh)<<32)+offsetLow;
        }
    }
    return error;
}

uint64_t FileGetSize(
    HANDLE fileHandle)
{
    uint64_t fileSize = 0;
    if(FilePointer(fileHandle,0,FILE_END,&fileSize) != 0)
    {
        fileSize = 0;
    }
    return fileSize;
}

int/*error*/ FileSetSize(
    HANDLE fileHandle,
    uint64_t length)
{
    int error = FilePointer(fileHandle,length,FILE_BEGIN,0);
    if(!error)
    {
        if(!SetEndOfFile(fileHandle))
        {
            error = GetLastError();
            ASSERT(error);
        }
    }
    return error;
}

uint64_t FileGetPointer(
    HANDLE fileHandle)
{
    uint64_t offset = 0;
    if(FilePointer(fileHandle,0,FILE_CURRENT,&offset) != 0)
    {
        offset = 0;
    }
    return offset;
}

void FileSetPointer(
    HANDLE fileHandle,
    uint64_t offset)
{
    FilePointer(fileHandle,offset,FILE_BEGIN,0);
}

int/*error*/ FileGetTime(
    HANDLE fileHandle,
    int64_t* outWriteTime)
{
    int error = 0;
    int64_t writeTime = 0;
    if(fileHandle == INVALID_HANDLE_VALUE)
    {
        error = ERROR_INVALID_HANDLE;
    }
    else if(!GetFileTime(fileHandle,0,0,
        reinterpret_cast<FILETIME*>(&writeTime)))
    {
        writeTime = 0;
        error = GetLastError();
        ASSERT(error);
    }
    *outWriteTime = writeTime;
    return error;
}

int/*error*/ FileSetTime(
    HANDLE fileHandle,
    int64_t writeTime)
{
    int error = 0;
    if(fileHandle == INVALID_HANDLE_VALUE)
    {
        error = ERROR_INVALID_HANDLE;
    }
    else if(!SetFileTime(fileHandle,0,0,
        reinterpret_cast<FILETIME*>(&writeTime)))
    {
        error = GetLastError();
        ASSERT(error);
    }
    return error;
}

static long fileLastPipeInstance = 0;

int/*err*/ FileCreateSocket(int/*bool*/ bidir,int/*bool*/ async,
   HANDLE* outHandle1,HANDLE* outHandle2)
{
    int err = 0;
    HANDLE handle1;
    HANDLE handle2 = INVALID_HANDLE_VALUE;
    int options1 = FILE_FLAG_FIRST_PIPE_INSTANCE;
    int options2 = 0;
    int access2 = GENERIC_WRITE;
    __int64 now;
    static const size_t maxPipeNameChars = 100;
    wchar_t pipeName[maxPipeNameChars];

    if(bidir)
    {
        options1 |= PIPE_ACCESS_DUPLEX;
        access2 |= GENERIC_READ;
    }
    else
    {
        options1 |= PIPE_ACCESS_INBOUND;
    }
    if(async)
    {
        options1 |= FILE_FLAG_OVERLAPPED;
        options2 |= FILE_FLAG_OVERLAPPED;
    }
        // CreatePipe creates handles that do not support overlapped IO and
        // are unidirectional. Use named pipe instead.
    GetSystemTimeAsFileTime(reinterpret_cast<::FILETIME*>(&now));
    wsprintfW(pipeName,L"\\\\.\\Pipe\\AnonymousPipe.%i.%p.%i.%I64i",
        GetCurrentProcessId(),&fileLastPipeInstance,
        InterlockedIncrement(&fileLastPipeInstance),now);
    handle1 = CreateNamedPipeW(pipeName,options1,PIPE_TYPE_BYTE|
        PIPE_READMODE_BYTE|PIPE_WAIT,1/*pipeCount*/,0/*outBufSize*/,
        0/*inBufSize*/,30*1000/*timeout*/,0/*security*/);
    if(!handle1)
    {
        handle1 = INVALID_HANDLE_VALUE;
    }
    if(handle1 == INVALID_HANDLE_VALUE)
    {
        err = GetLastError();
    }
    if(!err)
    {
        handle2 = CreateFileW(pipeName,access2,0/*shareMode*/,0/*security*/,
            OPEN_EXISTING,options2,0/*template*/);
        if(handle2 == INVALID_HANDLE_VALUE)
        {
            err = GetLastError();
            CloseHandle(handle1);
            handle1 = INVALID_HANDLE_VALUE;
        }
    }

    *outHandle1 = handle1;
    *outHandle2 = handle2;
    return err;
}

int/*err*/ FileCreatePipe(HANDLE* read,HANDLE* write)
{
    return FileCreateSocket(0/*bidir*/,1/*async*/,read,write);
}

int/*err*/ FileGetTime2(const wchar_t* fileName,int64_t* outWriteTime)
{
    int err = 0;
    WIN32_FILE_ATTRIBUTE_DATA info;

    memset(&info,0,sizeof(info));
    if(!GetFileAttributesExW(fileName,GetFileExInfoStandard,&info))
    {
        err = GetLastError();
    }

    if(outWriteTime)
    {
        *outWriteTime = *reinterpret_cast<int64_t*>(&info.ftLastWriteTime);
    }
    return err;
}
