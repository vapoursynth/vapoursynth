//---------------------------------------------------------------------------
// Copyright 2006-2013 Joe Lowe
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
//---------------------------------------------------------------------------
// file name:  pfmformatter.h
// created:    2006.08.01
//---------------------------------------------------------------------------
#ifndef PFMFORMATTER_H
#define PFMFORMATTER_H
#include "ptfactory1.h"
#include "pfmprefix.h"
#ifdef __cplusplus
extern "C" {
#endif

#define INTERFACE_NAME PfmFormatter
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void              , Release );
   PT_INTERFACE_FUNC( int/*systemError*/, Identify, PT_FD_T statusWrite,const wchar_t* mountFileName,PT_FD_T mountFileHandle,const void* mountFileData,size_t mountFileDataSize);
   PT_INTERFACE_FUNC( int/*systemError*/, Serve   , const wchar_t* mountFileName,int mountFlags,PT_FD_T toFormatterRead,PT_FD_T fromFormatterWrite);
   PT_INTERFACE_FUN0( void              , Cancel  );
};
#undef INTERFACE_NAME

#define PfmFormatterFactory PfmFormatterFactory2
PT_EXPORT int/*systemError*/ PT_CCALL PfmFormatterFactory(PfmFormatter**);

#define INTERFACE_NAME PfmConnector
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void              , Release );
   PT_INTERFACE_FUN0( void              , Cancel  );
   PT_INTERFACE_FUNC( int/*systemError*/, Identify, PT_FD_T statusWrite,const wchar_t* mountFileName,PT_FD_T mountFileHandle,const void* mountFileData,size_t mountFileDataSize);
   PT_INTERFACE_FUNC( int/*systemError*/, Connect , PT_FD_T statusWrite,const wchar_t* mountFileName,int mountFlags,PT_FD_T* fromFormatterRead,PT_FD_T* toFormatterWrite);
};
#undef INTERFACE_NAME

#define PfmConnectorFactory PfmConnectorFactory2
PT_EXPORT int/*systemError*/ PT_CCALL PfmConnectorFactory(PfmConnector**);

#ifdef __cplusplus
}
#endif
#endif
