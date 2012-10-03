//
// VapourSynth modifications Copyright 2012 Fredrik Mellbin
//
//----------------------------------------------------------------------------
// Copyright 2008 Joe Lowe
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
// file name:  avfspfm.h
// created:    2008.01.07
//----------------------------------------------------------------------------
#ifndef AVFSPFM_H
#define AVFSPFM_H

static const char avfsFormatterName[] = "VSFS";
static const char avisynthFileTypeTag[] = "vapoursynth";

struct AvfsLog_
{
    virtual void Print(const wchar_t* data) = 0;
    virtual void Vprintf(const wchar_t* format,va_list args) = 0;
    virtual void Printf(const wchar_t* format,...) = 0;
    virtual void Line(const wchar_t* data) = 0;
};

struct AvfsMediaFile_
{
    virtual void AddRef(void) = 0;
    virtual void Release(void) = 0;
    virtual bool/*success*/ ReadMedia(AvfsLog_* log,uint64_t fileOffset,void* buffer,size_t requestedSize) = 0;
};

struct AvfsVolume_
{
    // Returns full file name of real script file.
    // (c:\bob\myscript.avs)
    // Real script file on disk is accessible since the
    // virtual file system is not visible in this process.
    virtual const wchar_t* GetScriptFileName(void) = 0;
    // Returns file name of script file with no directory
    // component. (myscript.avs)
    virtual const wchar_t* GetScriptEndName(void) = 0;
    // Returns base name of script file, no directory or
    // extension. (myscript)
    virtual const wchar_t* GetMediaName(void) = 0;
    // Get in memory copy of current contents of script file.
    virtual const char* GetScriptData(void) = 0;
    virtual size_t GetScriptDataSize(void) = 0;
    // Add a virtual media file.
    virtual void CreateMediaFile(
        AvfsMediaFile_* mediaFile,
        const wchar_t* endName,
        uint64_t fileSize) = 0;
};

void AvfsProcessScript(AvfsLog_* log,AvfsVolume_* volume);

#endif
