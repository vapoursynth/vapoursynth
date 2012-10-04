
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
int/*error*/ FileGetTime(HANDLE fileHandle,int64_t* writeTime);
int/*error*/ FileSetTime(HANDLE fileHandle,int64_t fileTime);
