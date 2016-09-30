// vsfs.h : Avisynth Virtual File System
//
// Avisynth v2.5.  Copyright 2008 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.
#ifndef VSFS_H
#define VSFS_H


struct VapourSynther_ : public Synther_ {
    virtual void AddRef(void) = 0;
    virtual void Release(void) = 0;

    // Exception protected PVideoFrame->GetFrame()
    virtual const VSFrameRef *GetFrame(AvfsLog_* log, int n, bool *success = 0) = 0;

    // Readonly reference to VideoInfo
    virtual VideoInfoAdapter GetVideoInfo() = 0;

    // Read value of string variable from script. Returned pointer
    // is valid until next call, so copy if you need it long term.

    virtual const char* GetVarAsString(const char* varName, const char* defVal) = 0;
    virtual bool GetVarAsBool(const char* varName, bool defVal) = 0;
    virtual int GetVarAsInt(const char* varName, int defVal) = 0;

    virtual int BitsPerPixel() = 0;
    virtual int BMPSize() = 0;
    virtual uint8_t *GetPackedFrame() = 0;

    virtual const VSAPI *GetVSApi() = 0;
};

void VsfsWavMediaInit(
    AvfsLog_* log,
    VapourSynther_* avisynther,
    AvfsVolume_* volume);

void VsfsAviMediaInit(
    AvfsLog_* log,
    VapourSynther_* avs,
    AvfsVolume_* volume);


#endif
