#include "assertive.h"
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void AssertHandler(const char* file,int line,const char* expr)
{
    static const size_t maxScratchChars = 300;
    char scratch[maxScratchChars] = "";
    _snprintf(scratch,maxScratchChars,"ASSERT: %s:%i \"%s\"\n",file,line,
        expr);
    scratch[maxScratchChars-1] = 0;
    MessageBoxA(0,scratch,"AVFS ASSERT",MB_OK);
    TRAP;
    TerminateProcess(GetCurrentProcess(),static_cast<unsigned>(-1));
}
