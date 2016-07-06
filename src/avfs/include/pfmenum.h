//---------------------------------------------------------------------------
// Copyright 2006-2014 Joe Lowe
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
// file name:  pfmenum.h
// created:    2006.10.04
//---------------------------------------------------------------------------
#ifndef PFMENUM_H
#define PFMENUM_H
#include "pfmprefix.h"

PT_STATIC_CONST( int, pfmMountFlagReadOnly         , 0x00000001);
PT_STATIC_CONST( int, pfmMountFlagWorldRead        , 0x00000004);
PT_STATIC_CONST( int, pfmMountFlagWorldWrite       , 0x00000008);
PT_STATIC_CONST( int, pfmMountFlagUncOnly          , 0x00000010);
PT_STATIC_CONST( int, pfmMountFlagVerbose          , 0x00000020);
PT_STATIC_CONST( int, pfmMountFlagForceUnbuffered  , 0x00000080);
PT_STATIC_CONST( int, pfmMountFlagForceBuffered    , 0x00000100);
PT_STATIC_CONST( int, pfmMountFlagGroupRead        , 0x00000400);
PT_STATIC_CONST( int, pfmMountFlagGroupWrite       , 0x00000800);
PT_STATIC_CONST( int, pfmMountFlagGroupOwned       , 0x00001000);
PT_STATIC_CONST( int, pfmMountFlagWorldOwned       , 0x00002000);
PT_STATIC_CONST( int, pfmMountFlagCacheNameSpace   , 0x00004000);
PT_STATIC_CONST( int, pfmMountFlagBrowse           , 0x00010000);
PT_STATIC_CONST( int, pfmMountFlagUnmountOnRelease , 0x00020000);

PT_STATIC_CONST( int, pfmUnmountFlagBackground, 0x0001);

PT_STATIC_CONST( int, pfmVisibleProcessIdAll, -1);

PT_STATIC_CONST( int, pfmStatusFlagInitializing, 0x0001);
PT_STATIC_CONST( int, pfmStatusFlagReady       , 0x0002);
PT_STATIC_CONST( int, pfmStatusFlagDisconnected, 0x0004);
PT_STATIC_CONST( int, pfmStatusFlagClosed      , 0x0008);

PT_STATIC_CONST( int, pfmErrorSuccess       ,  0);
PT_STATIC_CONST( int, pfmErrorDisconnect    ,  1);
PT_STATIC_CONST( int, pfmErrorCancelled     ,  2);
PT_STATIC_CONST( int, pfmErrorUnsupported   ,  3);
PT_STATIC_CONST( int, pfmErrorInvalid       ,  4);
PT_STATIC_CONST( int, pfmErrorAccessDenied  ,  5);
PT_STATIC_CONST( int, pfmErrorOutOfMemory   ,  6);
PT_STATIC_CONST( int, pfmErrorFailed        ,  7);
PT_STATIC_CONST( int, pfmErrorNotFound      ,  8);
PT_STATIC_CONST( int, pfmErrorParentNotFound,  9);
PT_STATIC_CONST( int, pfmErrorExists        , 10);
PT_STATIC_CONST( int, pfmErrorNoSpace       , 11);
PT_STATIC_CONST( int, pfmErrorBadName       , 12);
PT_STATIC_CONST( int, pfmErrorNotEmpty      , 13);
PT_STATIC_CONST( int, pfmErrorEndOfData     , 14);
PT_STATIC_CONST( int, pfmErrorNotAFile      , 15);
PT_STATIC_CONST( int, pfmErrorDeleted       , 16);
PT_STATIC_CONST( int, pfmErrorCorruptData   , 17);
PT_STATIC_CONST( int, pfmErrorTimeout       , 18);
PT_STATIC_CONST( int, pfmErrorNotAFolder    , 19);

PT_STATIC_CONST( PT_INT8, pfmFileTypeNone   , 0);
PT_STATIC_CONST( PT_INT8, pfmFileTypeFile   , 1);
PT_STATIC_CONST( PT_INT8, pfmFileTypeFolder , 2);
PT_STATIC_CONST( PT_INT8, pfmFileTypeSymlink, 3);

PT_STATIC_CONST( PT_UINT8, pfmFileFlagsInvalid, 0xFF);

PT_STATIC_CONST( PT_UINT8, pfmFileFlagReadOnly, 0x01);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagHidden  , 0x02);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagSystem  , 0x04);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagExecute , 0x08);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagHasIcon , 0x10);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagArchive , 0x20);
PT_STATIC_CONST( PT_UINT8, pfmFileFlagAlias   , 0x40);

PT_STATIC_CONST( PT_UINT8, pfmExtraFlagOffline  , 0x01);
PT_STATIC_CONST( PT_UINT8, pfmExtraFlagReserved1, 0x02);

PT_STATIC_CONST( PT_UINT8, pfmColorInvalid, 0);
PT_STATIC_CONST( PT_UINT8, pfmColorDefault, 1);
PT_STATIC_CONST( PT_UINT8, pfmColorGray   , 2);
PT_STATIC_CONST( PT_UINT8, pfmColorGreen  , 3);
PT_STATIC_CONST( PT_UINT8, pfmColorPurple , 4);
PT_STATIC_CONST( PT_UINT8, pfmColorBlue   , 5);
PT_STATIC_CONST( PT_UINT8, pfmColorYellow , 6);
PT_STATIC_CONST( PT_UINT8, pfmColorRed    , 7);
PT_STATIC_CONST( PT_UINT8, pfmColorOrange , 8);

PT_STATIC_CONST( PT_INT64, pfmTimeInvalid, 0);

PT_STATIC_CONST( PT_INT8, pfmAccessLevelReadInfo , 1);
PT_STATIC_CONST( PT_INT8, pfmAccessLevelReadData , 2);
PT_STATIC_CONST( PT_INT8, pfmAccessLevelWriteInfo, 3);
PT_STATIC_CONST( PT_INT8, pfmAccessLevelDelete   , 4);
PT_STATIC_CONST( PT_INT8, pfmAccessLevelWriteData, 5);
PT_STATIC_CONST( PT_INT8, pfmAccessLevelOwner    , 6);

PT_STATIC_CONST( PT_UINT8, pfmControlFlagForceUnbuffered, 1);
PT_STATIC_CONST( PT_UINT8, pfmControlFlagForceBuffered  , 2);

PT_STATIC_CONST( int, pfmClientFlagXattr        , 0x0001);

PT_STATIC_CONST( int, pfmVolumeFlagReadOnly     , 0x0001);
PT_STATIC_CONST( int, pfmVolumeFlagCompressed   , 0x0002);
PT_STATIC_CONST( int, pfmVolumeFlagEncrypted    , 0x0004);
PT_STATIC_CONST( int, pfmVolumeFlagCaseSensitive, 0x0008);
PT_STATIC_CONST( int, pfmVolumeFlagTouchMap     , 0x0010);
PT_STATIC_CONST( int, pfmVolumeFlagNoCreateTime , 0x0100);
PT_STATIC_CONST( int, pfmVolumeFlagNoAccessTime , 0x0200);
PT_STATIC_CONST( int, pfmVolumeFlagNoWriteTime  , 0x0400);
PT_STATIC_CONST( int, pfmVolumeFlagNoChangeTime , 0x0800);
PT_STATIC_CONST( int, pfmVolumeFlagXattr        , 0x1000);
PT_STATIC_CONST( int, pfmVolumeFlagSymlinks     , 0x2000);

PT_STATIC_CONST( int, pfmFlushFlagOpen          , 0x0001);

PT_STATIC_CONST( int, pfmFastPipeFlagFastMapping, 0x0002);
PT_STATIC_CONST( int, pfmFastPipeFlagNamedDevice, 0x0004);
PT_STATIC_CONST( int, pfmFastPipeFlagAsyncClient, 0x0008);
PT_STATIC_CONST( int, pfmFastPipeFlagAsyncServer, 0x0010);

PT_STATIC_CONST( int, pfmFastPipeOpTypeRead , 1);
PT_STATIC_CONST( int, pfmFastPipeOpTypeWrite, 2);
PT_STATIC_CONST( int, pfmFastPipeOpTypeSend , 3);

#endif
