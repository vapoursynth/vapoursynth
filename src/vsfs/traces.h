//----------------------------------------------------------------------------
// Copyright 2005 Joe Lowe
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
// file name:   traces.h
// created:     2001.05.12
//----------------------------------------------------------------------------
#ifndef TRACES_H
#define TRACES_H

#include <wchar.h>
#include <stdarg.h>

   // This header is compatible with C and C++ compilers.
#if defined(__cplusplus)
#define _TREXTERN extern "C"
#else
#define _TREXTERN
#endif

   // Compiler specific calling convention override for Windows.
#if defined(_MSC_VER)
#define _TRCDECL __cdecl
#else
#define _TRCDECL
#endif

   // Opaque channel handle.
typedef struct _TRCHANNEL *TRCHANNEL;

   // Predefined handle for default channel.
#define TRDEF ((TRCHANNEL)((char*)(0)-1))
   // All functions will perform no action if passed a null handle.
#define TRNULL ((TRCHANNEL)(0))

   // Open and close handle to channel. Application code can call open and
   // close at any time. Driver code must call open and close from known
   // system thread context at APC or lower IRQL. It is not necessary to
   // check the return value from the open or dup functions.
_TREXTERN TRCHANNEL _TRCDECL tropen(const char* channelname);
_TREXTERN TRCHANNEL _TRCDECL trwopen(const wchar_t* channelname);
_TREXTERN TRCHANNEL _TRCDECL trdup(TRCHANNEL);
_TREXTERN void _TRCDECL trclose(TRCHANNEL);
    // Set channel to use when TRDEF is specified.
_TREXTERN void _TRCDECL trdefault(TRCHANNEL);
   // Attach and detach calls establish and terminate a connection with the
   // trace driver. For application code the trace import library performs an
   // automatic attach at process or dll initialization. A matching detach is
   // performed automatically at process exit or dll unload. Driver code must
   // manually call attach and detach functions from a known system thread
   // context at APC or lower IRQL. It is not necessary to check the return
   // result from the attach function. The attach function call can be issued
   // periodically to allow for dynamic attach to the trace driver if it was
   // not available at initialization. The client code is responsible to
   // serialize the detach call with all other calls.
_TREXTERN int/*bool attached*/ _TRCDECL trattach(void);
_TREXTERN void _TRCDECL trdetach(void);

   // Don't use this function. You won't like it.
_TREXTERN void _TRCDECL trcrash(void);

   // Functions for outputting formatted data to trace channel. All calls are
   // fully reentrant for application and driver code. Driver code must call
   // output functions from DISPATCH or lower IRQL.
_TREXTERN void _TRCDECL trprint(TRCHANNEL,const char* text);
_TREXTERN void _TRCDECL trwprint(TRCHANNEL,const wchar_t* text);
_TREXTERN void _TRCDECL trvprintf(TRCHANNEL,const char* format,va_list args);
_TREXTERN void _TRCDECL trprintf(TRCHANNEL,const char* format,...);
_TREXTERN void _TRCDECL trvwprintf(TRCHANNEL,const wchar_t* format,
   va_list args);
_TREXTERN void _TRCDECL trwprintf(TRCHANNEL,const wchar_t* format,...);

   // Functions to read channel data for use by trace display utilities.
   // Driver code must call read functions from known system thread context
   // at APC or lower IRQL.
_TREXTERN void _TRCDECL trlist(wchar_t* buffer,size_t bufferSize);
_TREXTERN void _TRCDECL trinfo(TRCHANNEL,size_t* startLine,size_t* endLine,
   size_t* endLineLength);
_TREXTERN void _TRCDECL trread(TRCHANNEL,size_t line,wchar_t* buffer,
   size_t maxBufferChars);

#if defined(__cplusplus)

   // Argument overloaded functions so C++ code doesn't need to use wide char
   // specific function names.
inline TRCHANNEL tropen(const wchar_t* channelName)
   { return trwopen(channelName); }
inline void trprint(TRCHANNEL channel,const wchar_t* text)
   { trwprint(channel,text); }
inline void trvprintf(TRCHANNEL channel,const wchar_t* format,va_list args)
   { trvwprintf(channel,format,args); }
inline void trprintf(TRCHANNEL channel,const wchar_t* format,...)
   { va_list args; va_start(args,format); trvwprintf(channel,format,args); }

    // Argument overloaded functions so C++ code doesn't need to specify
    // default channel.
inline void trprint(const char* text)
   { trprint(TRDEF,text); }
inline void trvprintf(const char* format,va_list args)
   { trvprintf(TRDEF,format,args); }
inline void trprintf(const char* format,...)
   { va_list args; va_start(args,format); trvprintf(TRDEF,format,args); }
inline void trprint(const wchar_t* text)
   { trwprint(TRDEF,text); }
inline void trvprintf(const wchar_t* format,va_list args)
   { trvwprintf(TRDEF,format,args); }
inline void trprintf(const wchar_t* format,...)
   { va_list args; va_start(args,format); trvwprintf(TRDEF,format,args); }

   // Bogus channel struct to allow channel->print(...) style function
   // invocation in C++ code.
struct _TRCHANNEL
{
private:
   int unused;
      // This isn't a real struct so inhibit copies and construction.
   _TRCHANNEL(void) { ; }
   _TRCHANNEL(TRCHANNEL&) { ; }
   _TRCHANNEL& operator=(_TRCHANNEL&) { return *this; }
public:
   void close(void) { trclose(this); }
   TRCHANNEL dup(void) { return trdup(this); }
   void print(const char* text) { trprint(this,text); }
   void vprintf(const char* format,va_list args)
      { trvprintf(this,format,args); }
   void printf(const char* format,va_list args)
      { trvprintf(this,format,args); }
   void printf(const char* format,...)
      { va_list args; va_start(args,format); trvprintf(this,format,args); }
   void wprint(const wchar_t* text) { trwprint(this,text); }
   void print(const wchar_t* text) { trwprint(this,text); }
   void vwprintf(const wchar_t* format,va_list args)
      { trvwprintf(this,format,args); }
   void vprintf(const wchar_t* format,va_list args)
      { trvwprintf(this,format,args); }
   void printf(const wchar_t* format,va_list args)
      { trvwprintf(this,format,args); }
   void wprintf(const wchar_t* format,...)
      { va_list args; va_start(args,format); trvwprintf(this,format,args); }
   void printf(const wchar_t* format,...)
      { va_list args; va_start(args,format); trvwprintf(this,format,args); }
};

#endif

#endif
