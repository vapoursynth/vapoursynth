//---------------------------------------------------------------------------
// Copyright 2005-2015 Joe Lowe
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
// file name:  pfmapi.h
// created:    2005.12.02
//---------------------------------------------------------------------------
#ifndef PFMAPI_H
#define PFMAPI_H
#include "ptfactory1.h"
#include "pfmenum.h"
#ifdef __cplusplus_cli
#pragma managed(push,off)
#endif

PT_TYPE_DEFINE(PfmMountCreateParams)
{
   size_t paramsSize;
   const wchar_t* mountName;
   int mountFlags;
   int reserved1;
   wchar_t driveLetter;
   wchar_t reserved2[sizeof(void*)/sizeof(wchar_t)-1];
   const wchar_t* ownerId;
   PT_FD_T toFormatterWrite;
   PT_FD_T fromFormatterRead;
   PT_FD_T toAuthWrite;
   PT_FD_T fromAuthRead;
   PT_UINT64 blockModeOffset;
   unsigned blockModeAlign;
   unsigned reserved3[1];
};
PT_INLINE void PfmMountCreateParams_Init(PfmMountCreateParams* mcp)
{
   memset(mcp,0,sizeof(*mcp));
   mcp->paramsSize = sizeof(*mcp);
   mcp->mountFlags = pfmMountFlagCacheNameSpace;
   mcp->toAuthWrite = PT_FD_INVALID;
   mcp->fromAuthRead = PT_FD_INVALID;
}

#define INTERFACE_NAME PfmMount
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void          , AddRef             );
   PT_INTERFACE_FUN0( void          , Release            );
   PT_INTERFACE_FUN0( int/*error*/  , Refresh            );
   PT_INTERFACE_FUN0( int           , GetMountId         );
   PT_INTERFACE_FUN0( int           , GetMountFlags      );
   PT_INTERFACE_FUN0( int           , GetStatusFlags     );
   PT_INTERFACE_FUN0( int           , GetVolumeFlags     );
   PT_INTERFACE_FUN0( PT_INT64      , GetChangeInstance  );
   PT_INTERFACE_FUN0( const wchar_t*, GetMountName       );
   PT_INTERFACE_FUN0( const wchar_t*, GetMountPoint      );
   PT_INTERFACE_FUN0( const wchar_t*, GetUncName         );
   PT_INTERFACE_FUN0( const wchar_t*, GetOwnerId         );
   PT_INTERFACE_FUN0( const wchar_t*, GetOwnerName       );
   PT_INTERFACE_FUN0( const wchar_t*, GetFormatterName   );
   PT_INTERFACE_FUN0( wchar_t       , GetDriveLetter     );
   PT_INTERFACE_FUN1( int/*error*/  , WaitReady          , int timeoutMsecs);
   PT_INTERFACE_FUN1( int/*error*/  , Unmount            , int unmountFlags);
   PT_INTERFACE_FUN0( int/*error*/  , Flush              );
   PT_INTERFACE_FUN6( int/*error*/  , Control            , int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmFileMountUi
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0(void           , Start         );
   PT_INTERFACE_FUN1(void           , Complete      ,const wchar_t* errorMessage);
   PT_INTERFACE_FUN2(void           , Status        ,const wchar_t* data,int/*bool*/ endOfLine);
   PT_INTERFACE_FUN0(void           , Suspend       );
   PT_INTERFACE_FUN0(void           , Resume        );
   PT_INTERFACE_FUN2(const wchar_t* , QueryPassword ,const wchar_t* prompt,int count);
   PT_INTERFACE_FUN0(void           , ClearPassword );
};
#undef INTERFACE_NAME

PT_STATIC_CONST( int, pfmFileMountFlagConsoleUi   , 0x0001 );
PT_STATIC_CONST( int, pfmFileMountFlagInProcess   , 0x0002 );
PT_STATIC_CONST( int, pfmFileMountFlagVerbose     , 0x0004 );
PT_STATIC_CONST( int, pfmFileMountFlagReserved1   , 0x0008 );
PT_STATIC_CONST( int, pfmFileMountFlagEditOptions , 0x0020 );
PT_STATIC_CONST( int, pfmFileMountFlagMultiMount  , 0x0040 );

PT_TYPE_DEFINE(PfmFileMountCreateParams)
{
   size_t paramsSize;
   const wchar_t* mountFileName;
   const wchar_t* reserved1;
   const wchar_t*const* argv/*reserved*/;
   int argc/*reserved*/;
   int mountFlags;
   int fileMountFlags;
   int reserved2;
   wchar_t driveLetter;
   wchar_t reserved3[(sizeof(int)*2)/sizeof(wchar_t)-1];
   const wchar_t* ownerId;
   const wchar_t* formatterName;
   const wchar_t* password;
   PfmFileMountUi* ui;
   void* reserved4[3];
};
PT_INLINE void PfmFileMountCreateParams_Init(PfmFileMountCreateParams* p)
{
   memset(p,0,sizeof(*p));
   p->paramsSize = sizeof(*p);
}

#define INTERFACE_NAME PfmFileMount
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void        , AddRef       );
   PT_INTERFACE_FUN0( void        , Release      );
   PT_INTERFACE_FUN2( int/*error*/, GetInterface ,const char* id,void*);
   PT_INTERFACE_FUN0( void        , Cancel       );
   PT_INTERFACE_FUN1( int/*error*/, Start        ,const PfmFileMountCreateParams* fmp);
   PT_INTERFACE_FUN2( void        , Send         ,const wchar_t* data,int/*bool*/ endOfLine);
   PT_INTERFACE_FUN2( void        , Status       ,const wchar_t* data,int/*bool*/ endOfLine);
   PT_INTERFACE_FUN0( int/*error*/, WaitReady    );
   PT_INTERFACE_FUN0( PfmMount*   , GetMount     );
   PT_INTERFACE_FUN0( void        , Detach       );
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmIterator
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void          , AddRef );
   PT_INTERFACE_FUN0( void          , Release);
   PT_INTERFACE_FUN1( int/*mountId*/, Next   , PT_INT64* changeInstance);
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmMonitor
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void        , AddRef );
   PT_INTERFACE_FUN0( void        , Release);
   PT_INTERFACE_FUN2( int/*error*/, Wait   , PT_INT64 nextChangeInstance,int timeoutMsecs);
   PT_INTERFACE_FUN0( void        , Cancel );
};
#undef INTERFACE_NAME

PT_TYPE_DEFINE(PfmFastPipeCreateParams)
{
   size_t paramsSize;
   const wchar_t* baseDeviceName;
   int fastPipeFlags;
#if UINT_MAX < SIZE_MAX
   PT_UINT8 align1[sizeof(size_t)-sizeof(int)];
#endif
   wchar_t* deviceName;
   size_t maxDeviceNameChars;
};
PT_INLINE void PfmFastPipeCreateParams_Init(PfmFastPipeCreateParams* pcp)
{
   memset(pcp,0,sizeof(*pcp));
   pcp->paramsSize = sizeof(*pcp);
}

PT_TYPE_DEFINE(PfmFastPipeOp)
{
   PT_UINT64 clientId;
   PT_UINT64 offset;
   int opType;
#if UINT_MAX < SIZE_MAX
   PT_UINT8 align1[sizeof(size_t)-sizeof(int)];
#endif
   void* input;
   size_t maxInputSize;
   void* output;
   size_t maxOutputSize;
      // implementation use
   PT_UINT64 opaque[(sizeof(void*)*4+48)/8];
};

PT_TYPE_DEFINE(PfmFastPipeSendContext)
{
   void (PT_CCALL*complete)(PfmFastPipeSendContext*,int error,size_t inputSize,size_t outputSize);
#if SIZE_MAX < 0xFFFFFFFFFFFFFFFF
   PT_UINT8 align1[8-sizeof(void(*)(void))];
#endif
      // implementation use
   PT_UINT64 opaque[(sizeof(void*)*10+40)/8];
};

#define INTERFACE_NAME PfmFastPipeServer
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void        , Release );
   PT_INTERFACE_FUN1( void        , InitOp  , PfmFastPipeOp* op);
   PT_INTERFACE_FUN1( void        , FreeOp  , PfmFastPipeOp* op);
   PT_INTERFACE_FUN1( int/*error*/, Receive , PfmFastPipeOp* op);
   PT_INTERFACE_FUN4( void        , Complete, PfmFastPipeOp* op,int error,size_t inputSize,size_t outputSize);
   PT_INTERFACE_FUN0( void        , Cancel  );
};
#undef INTERFACE_NAME

#define INTERFACE_NAME PfmApi
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void        , AddRef                   );
   PT_INTERFACE_FUN0( void        , Release                  );
   PT_INTERFACE_FUN0( const char* , Version                  );
   PT_INTERFACE_FUN0( int/*error*/, SysStart                 );
   PT_INTERFACE_FUN2( int/*error*/, MountCreate              , const PfmMountCreateParams* params,PfmMount** mount);
   PT_INTERFACE_FUN2( int/*error*/, MountNameOpen            , const wchar_t* mountName,PfmMount** mount);
   PT_INTERFACE_FUN2( int/*error*/, MountPointOpen           , const wchar_t* mountPoint,PfmMount** mount);
   PT_INTERFACE_FUN2( int/*error*/, MountIdOpen              , int mountId,PfmMount** mount);
   PT_INTERFACE_FUN3( int/*error*/, MountIterate             , PT_INT64 startChangeInstance,PT_INT64* nextChangeInstance,PfmIterator** iterator);
   PT_INTERFACE_FUN1( int/*error*/, Monitor                  , PfmMonitor** monitor);
   PT_INTERFACE_FUN1( int/*error*/, FileMountCreate          , PfmFileMount** mount);
   PT_INTERFACE_FUN3( int/*error*/, FastPipeCreate           , const PfmFastPipeCreateParams* params,PT_FD_T* clientHandle,PT_FD_T* serverHandle);
   PT_INTERFACE_FUN1( int/*error*/, FastPipeCancel           , PT_FD_T clientFd);
   PT_INTERFACE_FUN1( int/*error*/, FastPipeEnableFastMapping, PT_FD_T clientFd);
   PT_INTERFACE_FUN2( int/*error*/, FastPipeServerFactory    , PT_FD_T serverFd,PfmFastPipeServer**);
   PT_INTERFACE_FUN2( int/*error*/, FastPipeClientContext    , PT_FD_T clientFd,int* clientContext);
   PT_INTERFACE_FUN8( void        , FastPipeSend             , PT_FD_T clientFd,int clientContext,PT_UINT64 offset,void* input,size_t maxInputSize,void* output,size_t maxOutputSize,PfmFastPipeSendContext*);
};
#undef INTERFACE_NAME

   // PfmApi interface version history.
   //    PfmApi1 - 2007.12.31
   //       First public release.
   //    PfmApi2 - 2008.01.25
   //       Auth handles now optional when creating mounts.
   //       WaitReady() added to PfmMount.
   //    PfmApi3 - 2008.02.12
   //       New driveletter parameter in PfmApi::Create.
   //       GetDriveLetter() added to PfmMount.
   //    PfmApi4 - 2008.11.04
   //       Improved handling of process visible mounts (SxS load error fix).
   //    PfmApi5 - 2009.02.26
   //       Fastpipe support.
   //    PfmApi6 - 2010.01.27
   //       Fastpipe and marshaller cancel.
   //    PfmApi7 - 2012.04.12
   //       Version and build installation check.
   //       FileMountCreate() added.
   //       Non-privileged user visible drive letter support.
   //    PfmApi8 - 2012.11.19
   //       Mount.GetMountPoint() added.
   //    PfmApi9 - 2013.12.30
   //       Unmount-on-release support added.
   //    PfmApi10 - 2014.10.
   //       Dropped virtual mount point support.
   //       Dropped visible process support.
   //       Dropped arbitrary mount point support.
   //       Dropped alerter support.
   //       New device and file versioned name convention.
PTFACTORY1_DECLARE(PfmApi,PFM_PRODIDW,PFM_APIIDW);
PT_INLINE int/*error*/ PfmApiFactory(PfmApi** pfmApi)
   { return PfmApiGetInterface("PfmApi10",pfmApi); }
// void PfmApiUnload(void);

PT_STATIC_CONST( int, pfmInstInstalled   , 0);
PT_STATIC_CONST( int, pfmInstOldBuild    , 1);
PT_STATIC_CONST( int, pfmInstOldVersion  , 2);
PT_STATIC_CONST( int, pfmInstNotInstalled, 3);
PT_INLINE int PfmInstallCheck(void)
{
   int res = pfmInstNotInstalled;
   PfmApi* api;
   if(PfmApiFactory(&api) == 0)
   {
      res = pfmInstOldBuild;
      if(strcmp(PT_VCAL0(api,Version),PFM_BUILDTAGA) >= 0)
         res = pfmInstInstalled;
   }
   else if(PfmApiGetInterface("PfmApi1",&api) == 0)
      res = pfmInstOldVersion;
   if(api) PT_VCAL0(api,Release);
   if(res != pfmInstInstalled) PfmApiUnload();
   return res;
}

#ifdef __cplusplus_cli
#pragma managed(pop)
#endif
#endif
