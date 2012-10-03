/*--------------------------------------------------------------------------*/
/* Copyright 2006-2009 Joe Lowe                                             */
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
/* file name:  pfmenum.h                                                    */
/* created:    2006.10.04                                                   */
/*--------------------------------------------------------------------------*/
#ifndef PFMENUM_H
#define PFMENUM_H

enum {
   pfmMountFlagReadOnly          = 0x0001,
   pfmMountFlagSystemVisible     = 0x0002,
   pfmMountFlagWorldRead         = 0x0004,
   pfmMountFlagWorldWrite        = 0x0008,
   pfmMountFlagUncOnly           = 0x0010,
   pfmMountFlagVerbose           = 0x0020,
   pfmMountFlagFolder            = 0x0040,
   pfmMountFlagForceUnbuffered   = 0x0080,
   pfmMountFlagForceBuffered     = 0x0100,
   pfmMountFlagDesktop           = 0x0200
   };

enum {
   pfmUnmountFlagAsync           = 0x0001,
   pfmUnmountFlagForce           = 0x0002 };

enum {
   pfmVisibleProcessIdAll        = -1 };

enum {
   pfmStatusFlagInitializing         = 0x0001,
   pfmStatusFlagReady                = 0x0002,
   pfmStatusFlagUnexpectedDisconnect = 0x0004,
   pfmStatusFlagClosed               = 0x0008 };

enum {
   pfmErrorSuccess               =  0,
   pfmErrorDisconnect            =  1,
   pfmErrorCancelled             =  2,
   pfmErrorUnsupported           =  3,
   pfmErrorInvalid               =  4,
   pfmErrorAccessDenied          =  5,
   pfmErrorOutOfMemory           =  6,
   pfmErrorFailed                =  7,
   pfmErrorNotFound              =  8,
   pfmErrorParentNotFound        =  9,
   pfmErrorExists                = 10,
   pfmErrorNoSpace               = 11,
   pfmErrorBadName               = 12,
   pfmErrorNotEmpty              = 13,
   pfmErrorEndOfData             = 14,
   pfmErrorNotAFile              = 15,
   pfmErrorDeleted               = 16,
   pfmErrorCorruptData           = 17 };

enum {
   pfmFileTypeNone               = 0,
   pfmFileTypeFile               = 1,
   pfmFileTypeFolder             = 2 };

enum {
   pfmFileFlagsInvalid           = 0xFF };

enum {
   pfmFileFlagReadOnly           = 0x01,
   pfmFileFlagHidden             = 0x02,
   pfmFileFlagSystem             = 0x04,
   pfmFileFlagExecute            = 0x08,
   pfmFileFlagArchive            = 0x20 };

enum {
   pfmExtraFlagOffline           = 0x01,
   pfmExtraFlagNoIndex           = 0x02 };

enum {
   pfmTimeInvalid                = 0 };

enum {
   pfmReadInfoAccess             = 1,
   pfmReadDataAccess             = 2,
   pfmWriteInfoAccess            = 3,
   pfmDeleteAccess               = 4,
   pfmWriteDataAccess            = 5,
   pfmOwnerAccess                = 6 };

enum {
   pfmControlFlagForceUnbuffered = 1,
   pfmControlFlagForceBuffered   = 2 };

enum {
   pfmVolumeFlagReadOnly         = 0x01,
   pfmVolumeFlagCompressed       = 0x02,
   pfmVolumeFlagEncrypted        = 0x04,
   pfmVolumeFlagCaseSensitive    = 0x08 };

enum {
   pfmLockFileFlagForWrite       = 0x01 };

enum {
   pfmFastPipeFlagFastMapping = 0x0002,
   pfmFastPipeFlagNamedDevice = 0x0004,
   pfmFastPipeFlagAsyncClient = 0x0008,
   pfmFastPipeFlagAsyncServer = 0x0010 };

enum {
   pfmFastPipeOpTypeRead      = 1,
   pfmFastPipeOpTypeWrite     = 2,
   pfmFastPipeOpTypeSend      = 3 };

#endif
