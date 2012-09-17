cdef extern from "windows.h":
    bint WriteFile(int hFile, void *lpBuffer, int nNumberOfBytesToWrite, int *lpNumberOfBytesWritten, void *lpOverlapped)
    bint FlushFileBuffers(int hFile)
