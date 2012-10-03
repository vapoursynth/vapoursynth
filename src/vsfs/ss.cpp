
// Null ptr and buffer overflow safe C string primitives.

#include "assertive.h"
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <wchar.h>
#include "ss.h"

// make it work in vs2010
#define wcsdup _wcsdup


void sscpy(wchar_t* dest,size_t maxDestChars,const wchar_t* source)
{
    if(dest && maxDestChars)
    {
        wcsncpy(dest,ssfix(source),maxDestChars);
        dest[maxDestChars-1] = 0;
    }
}

wchar_t* ssalloc(size_t count)
{
    wchar_t* d = 0;
    if(count)
    {
        d = static_cast<wchar_t*>(malloc(count*sizeof(d[0])));
        if(d)
        {
            d[0] = 0;
        }
    }
    return d;
}

wchar_t* ssdup(const wchar_t* s)
{
    wchar_t* d = 0;
    if(s)
    {
        d = wcsdup(s);
    }
    return d;
}

int sscmpi(const wchar_t* s1,const wchar_t* s2)
{
    return _wcsicmp(ssfix(s1),ssfix(s2));
}

wchar_t* ssrchr(const wchar_t* s,wchar_t c)
{
    wchar_t* d = 0;
    if(s)
    {
        d = const_cast<wchar_t*>(wcsrchr(s,c));
    }
    return d;
}

char* ssconvalloc(const wchar_t* s)
{
    char* d = 0;
    size_t c;
    if(s)
    {
        c = wcstombs(0,s,0);
        if(c < INT_MAX)
        {
            d = static_cast<char*>(malloc((c+1)*sizeof(d[0])));
            if(d)
            {
                VERIFYIS(wcstombs(d,s,c+1),c);
                d[c] = 0;
            }
        }
    }
    return d;
}

wchar_t* ssconvalloc(const char* s)
{
    wchar_t* d = 0;
    size_t c;
    if(s)
    {
        c = mbstowcs(0,s,0);
        if(c < INT_MAX)
        {
            d = static_cast<wchar_t*>(malloc((c+1)*sizeof(d[0])));
            if(d)
            {
                VERIFYIS(mbstowcs(d,s,c+1),c);
                d[c] = 0;
            }
        }
    }
    return d;
}

void ssvformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,
    va_list args)
{
    if(dest && maxDestChars)
    {
        _vsnwprintf(dest,maxDestChars,format,args);
        dest[maxDestChars-1] = 0;
    }
}

void ssformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,...)
{
    va_list args;
    va_start(args,format);
    ssvformat(dest,maxDestChars,format,args);
}

wchar_t* ssvformatalloc(const wchar_t* format,va_list args)
{
    static const size_t maxStackScratchChars = 300;
    wchar_t stackScratch[maxStackScratchChars] = L"";
    wchar_t* heapScratch = 0;
    wchar_t* scratch = stackScratch;
    size_t maxScratchChars = maxStackScratchChars;
    int count;
    bool again;
    wchar_t* val = 0;

    do
    {
        again = false;
        count = _vsnwprintf(scratch,maxScratchChars,format,args);
        if(count < 0 || static_cast<unsigned>(count) >= maxScratchChars)
        {
            ssfree(heapScratch);
            maxScratchChars = maxScratchChars*2+5000;
            scratch = heapScratch = ssalloc(maxScratchChars);
            if(heapScratch)
            {
                again = true;
            }
        }
    } while(again);

    if(scratch)
    {
        val = wcsdup(scratch);
    }
    ssfree(heapScratch);
    return val;
}

wchar_t* ssformatalloc(const wchar_t* format,...)
{
    va_list args;
    va_start(args,format);
    return ssvformatalloc(format,args);
}
