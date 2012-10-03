/*--------------------------------------------------------------------------*/
/* Copyright 2009 Joe Lowe                                                  */
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
/* file name:  pfmprefix.h                                                  */
/* created:    2009.07.10                                                   */
/*--------------------------------------------------------------------------*/
#ifndef PFMPREFIX_H
#define PFMPREFIX_H

/* The following type macros are intended only to enhance portability
of the PFM header files. Implementors are not expected to use the
type macros in implementation code. Implementors should use type
names that fit the compiler/platform/style of the implementation.
*/

#include <limits.h>

#define PFM_CHAR8 char
#define PFM_BOOL8 unsigned char
#define PFM_INT8 signed char
#define PFM_UINT8 unsigned char
#define PFM_INT16 short
#define PFM_UINT16 unsigned short
#define PFM_INT32 int
#define PFM_UINT32 unsigned
#if defined(INT64_C)
#define PFM_INT64 int64_t
#define PFM_UINT64 uint64_t
#elif defined(_MSC_VER)
#define PFM_INT64 __int64
#define PFM_UINT64 unsigned __int64
#elif LONG_MAX > INT_MAX
#define PFM_INT64 long
#define PFM_UINT64 unsigned long
#else
#define PFM_INT64 long long
#define PFM_UINT64 unsigned long long
#endif
#ifdef UUID_T_DEFINED
#define PFM_UUID uuid_t
#elif defined(GUID_DEFINED)
#define PFM_UUID GUID
#else
#define PFM_UUID struct { PFM_UINT32 data1; PFM_UINT16 data2; PFM_UINT16 data3; PFM_UINT8 data4[8]; }
#endif

#ifdef INT32LE
#define PFM_INT16LE INT16LE
#define PFM_UINT16LE UINT16LE
#define PFM_INT32LE INT32LE
#define PFM_UINT32LE UINT32LE
#define PFM_INT64LE INT64LE
#define PFM_UINT64LE UINT64LE
#else
#define PFM_INT16LE PFM_INT16
#define PFM_UINT16LE PFM_UINT16
#define PFM_INT32LE PFM_INT32
#define PFM_UINT32LE PFM_UINT32
#define PFM_INT64LE PFM_INT64
#define PFM_UINT64LE PFM_UINT64
#endif
#ifdef UUIDLE
#define PFM_UUIDLE UUIDLE
#elif defined(GUID_DEFINED)
#define PFM_UUIDLE GUID
#else
#define PFM_UUIDLE struct { PFM_UINT32LE d1; PFM_UINT16LE d2; PFM_UINT16LE d3; PFM_UINT8 d4[8]; }
#endif

#ifdef _WIN32
#define PFM_CCALL __cdecl
#define PFM_EXPORT __declspec(dllexport)
#ifdef _INC_WINDOWS
#define PFM_HANDLE HANDLE
#define PFM_INVALIDHANDLE INVALID_HANDLE_VALUE
#else
#define PFM_HANDLE void*
#define PFM_INVALIDHANDLE ((void*)(ptrdiff_t)(-1))
#endif
#else
#define PFM_CCALL
#define PFM_EXPORT
#define PFM_HANDLE int
#define PFM_INVALIDHANDLE -1
#endif

#define _PFM_CAT2_(a,b) a##b
#define _PFM_CAT2(a,b) _PFM_CAT2_(a,b)
#define _PFM_QUOTE_(s) #s
#define _PFM_QUOTE(s) _PFM_QUOTE_(s)

#ifndef PFM_DEDPREFIX
#define PFM_PREFIX pfm
#define PFM_PRODID PismoFileMount
#define PFM_PRODUCT Pismo File Mount
#define PFM_COMPANY Pismo Technic Inc.
#else
#define PFM_PREFIX PFM_DEDPREFIX
#ifdef PFM_DEDPRODID
#define PFM_PRODID PFM_DEDPRODID
#else
#define PFM_PRODID PFM_PREFIX
#endif
#ifdef PFM_DEDPRODUCT
#define PFM_PRODUCT PFM_DEDPRODUCT
#else
#define PFM_PRODUCT PFM_DEDPRODID
#endif
#ifdef PFM_DEDCOMPANY
#define PFM_COMPANY PFM_DEDCOMPANY
#else
#define PFM_COMPANY Pismo Technic Inc.
#endif
#endif
#define PFM_PREFIXA _PFM_QUOTE(PFM_PREFIX)
#define PFM_PREFIXW _PFM_CAT2(L,PFM_PREFIXA)
#define PFM_PRODIDA _PFM_QUOTE(PFM_PRODID)
#define PFM_PRODIDW _PFM_CAT2(L,PFM_PRODIDA)
#define PFM_PRODUCTA _PFM_QUOTE(PFM_PRODUCT)
#define PFM_PRODUCTW _PFM_CAT2(L,PFM_PRODUCTA)
#define PFM_COMPANYA _PFM_QUOTE(PFM_COMPANY)
#define PFM_COMPANYW _PFM_CAT2(L,PFM_COMPANYA)

#endif
