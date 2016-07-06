//---------------------------------------------------------------------------
// Copyright 2013 Joe Lowe
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
// file name:  ptblob.h
// created:    2013.01.16
//
// Untyped variable size data container. Generally for use with
// authentication data such as passwords and keys, to insure
// copies are not left strewn about memory. Also useful for passing
// variable size data around as a single arg, instead of
// ptr,size argument pairs.
//
// The interfaces defined here are expected to be used as privimitives
// in other interfaces, and so must indefinately maintain ABI
// compatibility. Source compatibility may change over time.
//---------------------------------------------------------------------------
#ifndef PTBLOB_H
#define PTBLOB_H
#include "ptpublic.h"

#define INTERFACE_NAME blob_i
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void       , retain  );
   PT_INTERFACE_FUN0( void       , release );
   PT_INTERFACE_FUN0( const void*, data    );
   PT_INTERFACE_FUN0( size_t     , size    );
};
#undef INTERFACE_NAME

#endif
