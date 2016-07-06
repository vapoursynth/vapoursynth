
void FileClose(HANDLE handle);
int/*error*/ FileOpenRead(const wchar_t* fileName,HANDLE* outHandle);
int/*error*/ FileOpenWrite(const wchar_t* fileName,HANDLE* outHandle);
int/*error*/ FileRead(HANDLE fileHandle,void* buffer,size_t count,size_t* bytesRead);
int/*error*/ FileWrite(HANDLE fileHandle,const void* buffer,size_t count,size_t* bytesWritten);
int/*error*/ FileSetSize(HANDLE fileHandle,uint64_t length);
int/*error*/ FilePointer(HANDLE fileHandle,uint64_t offset,int method,uint64_t* newOffset);
uint64_t FileGetPointer(HANDLE fileHandle);
void FileSetPointer(HANDLE fileHandle,uint64_t offset);
uint64_t FileGetSize(HANDLE fileHandle);
int64_t FileGetTime(HANDLE fileHandle);
int/*error*/ FileSetTime(HANDLE fileHandle,int64_t fileTime);
int/*error*/ FileCreateSocket(int/*bool*/ bidir,int/*bool*/ async,HANDLE* outHandle1,HANDLE* outHandle2);
int/*error*/ FileCreatePipe(HANDLE* read,HANDLE* write);
int/*error*/ FileGetTime2(const wchar_t* fileName,int64_t* outWriteTime);

// inline int64_t FileCurrentTime(void)
// {
//     FILETIME time;
//     GetSystemTimeAsFileTime(&time);
//     return time;
// }

inline void FileCleanup(HANDLE* handle)
{
    if(*handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(*handle);
        *handle = INVALID_HANDLE_VALUE;
    }
}

inline void FileCleanupPair(HANDLE* handle1,HANDLE* handle2)
{
    if(*handle1 != INVALID_HANDLE_VALUE)
    {
        CloseHandle(*handle1);
        if(*handle2 == *handle1)
        {
            *handle2 = INVALID_HANDLE_VALUE;
        }
        *handle1 = INVALID_HANDLE_VALUE;
    }
    FileCleanup(handle2);
}
