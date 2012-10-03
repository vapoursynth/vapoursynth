/*--------------------------------------------------------------------------*/
/* Copyright 2006-2008 Joe Lowe                                             */
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
/* file name:  pfmformatter.h                                               */
/* created:    2006.08.01                                                   */
/*--------------------------------------------------------------------------*/
#ifndef PFMFORMATTER_H
#define PFMFORMATTER_H

#include "pfmprefix.h"

#ifdef __cplusplus

struct PfmFormatter
{
    virtual void PFM_CCALL Release(void) = 0;
    virtual int/*systemError*/ PFM_CCALL Identify(PFM_HANDLE statusWrite,const wchar_t* mountFileName,PFM_HANDLE mountFileHandle,const void* mountFileData,size_t mountFileDataSize) = 0;
    virtual int/*systemError*/ PFM_CCALL Serve(const wchar_t* mountFileName,int mountFlags,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite) = 0;
};

extern "C" {

    /* Duplicate of above definition for C code. */
#else

typedef struct PfmFormatter_
{
    void (PFM_CCALL*Release)(struct PfmFormatter_**);
    int/*systemError*/ (PFM_CCALL*Identify)(struct PfmFormatter_**,PFM_HANDLE statusWrite,const wchar_t* mountFileName,PFM_HANDLE mountFileHandle,const void* mountFileData,size_t mountFileDataSize);
    int/*systemError*/ (PFM_CCALL*Serve)(struct PfmFormatter_**,const wchar_t* mountFileName,int mountFlags,PFM_HANDLE toFormatterRead,PFM_HANDLE fromFormatterWrite);
} *PfmFormatter;

#endif

PFM_EXPORT int/*systemError*/ PFM_CCALL PfmFormatterFactory1(PfmFormatter**);
typedef int/*systemError*/ (PFM_CCALL*PfmFormatterFactory1_t)(PfmFormatter**);
#define PFM_FORMATTER_FACTORY_EXPORT "PfmFormatterFactory1"

#ifdef __cplusplus
}
#endif

#endif
