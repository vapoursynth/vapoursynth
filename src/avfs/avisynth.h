// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// Avisynth v2.6.  Copyright 2006 Klaus Post.
// Avisynth v2.6.  Copyright 2009 Ian Brabham.
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
// Linking Avisynth statically or dynamically with other modules is making
// a combined work based on Avisynth.  Thus, the terms and conditions of
// the GNU General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate
// with Avisynth solely through the interfaces defined in avisynth.h,
// regardless of the license terms of these independent modules, and to
// copy and distribute the resulting combined work under terms of your
// choice, provided that every copy of the combined work is accompanied
// by a complete copy of the source code of Avisynth (the version of
// Avisynth used to produce the combined work), being distributed under
// the terms of the GNU General Public License plus this exception.  An
// independent module is a module which is not derived from or based on
// Avisynth, such as 3rd-party filters, import and export plugins, or
// graphical user interfaces.




#ifndef __AVISYNTH_6_H__
#define __AVISYNTH_6_H__

enum { AVISYNTH_INTERFACE_VERSION = 6 };


/* Define all types necessary for interfacing with avisynth.dll
   Moved from internal.h */

// Win32 API macros, notably the types BYTE, DWORD, ULONG, etc.
#include <windef.h>


// Raster types used by VirtualDub & Avisynth
#define in64 (__int64)(unsigned short)
typedef unsigned long   Pixel;    // this will break on 64-bit machines!
typedef unsigned long   Pixel32;
typedef unsigned char   Pixel8;
typedef long            PixCoord;
typedef long            PixDim;
typedef long            PixOffset;


/* Compiler-specific crap */

// Tell MSVC to stop precompiling here
#ifdef _MSC_VER
  #pragma hdrstop
#endif

// Set up debugging macros for MS compilers; for others, step down to the
// standard <assert.h> interface
#ifdef _MSC_VER
  #include <crtdbg.h>
#else
  #define _RPT0(a,b) ((void)0)
  #define _RPT1(a,b,c) ((void)0)
  #define _RPT2(a,b,c,d) ((void)0)
  #define _RPT3(a,b,c,d,e) ((void)0)
  #define _RPT4(a,b,c,d,e,f) ((void)0)

  #define _ASSERTE(x) assert(x)
  #include <assert.h>
#endif



// I had problems with Premiere wanting 1-byte alignment for its structures,
// so I now set the Avisynth struct alignment explicitly here.
#pragma pack(push,8)

#define FRAME_ALIGN 16
// Default frame alignment is 16 bytes, to help P4, when using SSE2

// The VideoInfo struct holds global information about a clip (i.e.
// information that does not depend on the frame number).  The GetVideoInfo
// method in IClip returns this struct.

// Audio Sample information
typedef float SFLOAT;

enum {SAMPLE_INT8  = 1<<0,
      SAMPLE_INT16 = 1<<1,
      SAMPLE_INT24 = 1<<2,    // Int24 is a very stupid thing to code, but it's supported by some hardware.
      SAMPLE_INT32 = 1<<3,
      SAMPLE_FLOAT = 1<<4};

enum {
   PLANAR_Y=1<<0,
   PLANAR_U=1<<1,
   PLANAR_V=1<<2,
   PLANAR_ALIGNED=1<<3,
   PLANAR_Y_ALIGNED=PLANAR_Y|PLANAR_ALIGNED,
   PLANAR_U_ALIGNED=PLANAR_U|PLANAR_ALIGNED,
   PLANAR_V_ALIGNED=PLANAR_V|PLANAR_ALIGNED,
   PLANAR_A=1<<4,
   PLANAR_R=1<<5,
   PLANAR_G=1<<6,
   PLANAR_B=1<<7,
   PLANAR_A_ALIGNED=PLANAR_A|PLANAR_ALIGNED,
   PLANAR_R_ALIGNED=PLANAR_R|PLANAR_ALIGNED,
   PLANAR_G_ALIGNED=PLANAR_G|PLANAR_ALIGNED,
   PLANAR_B_ALIGNED=PLANAR_B|PLANAR_ALIGNED,
  };

class AvisynthError /* exception */ {
public:
  const char* const msg;
  AvisynthError(const char* _msg) : msg(_msg) {}
}; // end class AvisynthError


/* Forward references */
struct __single_inheritance VideoInfo;
class __single_inheritance VideoFrameBuffer;
class __single_inheritance VideoFrame;
class IClip;
class __single_inheritance PClip;
class __single_inheritance PVideoFrame;
class IScriptEnvironment;
class __single_inheritance AVSValue;


/*
 * Avisynth C++ plugin API code function pointers.
 *
 * In order to maintain binary compatibility with
 * future version do not change the order of the
 * existing function pointers. It will be baked
 * into all existing plugins.
 *
 * Add new function pointers to the end of the
 * structure. The linkage macros generate some
 * protection code to ensure newer plugin do not
 * call non-existing functions in an older host.
 */

struct AVS_Linkage {

  size_t Size;

/**********************************************************************/

// struct VideoInfo
  bool    (VideoInfo::*HasVideo)() const;
  bool    (VideoInfo::*HasAudio)() const;
  bool    (VideoInfo::*IsRGB)() const;
  bool    (VideoInfo::*IsRGB24)() const;
  bool    (VideoInfo::*IsRGB32)() const;
  bool    (VideoInfo::*IsYUV)() const;
  bool    (VideoInfo::*IsYUY2)() const;
  bool    (VideoInfo::*IsYV24)() const;
  bool    (VideoInfo::*IsYV16)() const;
  bool    (VideoInfo::*IsYV12)() const;
  bool    (VideoInfo::*IsYV411)() const;
  bool    (VideoInfo::*IsY8)() const;
  bool    (VideoInfo::*IsColorSpace)(int c_space) const;
  bool    (VideoInfo::*Is)(int property) const;
  bool    (VideoInfo::*IsPlanar)() const;
  bool    (VideoInfo::*IsFieldBased)() const;
  bool    (VideoInfo::*IsParityKnown)() const;
  bool    (VideoInfo::*IsBFF)() const;
  bool    (VideoInfo::*IsTFF)() const;
  bool    (VideoInfo::*IsVPlaneFirst)() const;
  int     (VideoInfo::*BytesFromPixels)(int pixels) const;
  int     (VideoInfo::*RowSize)(int plane) const;
  int     (VideoInfo::*BMPSize)() const;
  __int64 (VideoInfo::*AudioSamplesFromFrames)(int frames) const;
  int     (VideoInfo::*FramesFromAudioSamples)(__int64 samples) const;
  __int64 (VideoInfo::*AudioSamplesFromBytes)(__int64 bytes) const;
  __int64 (VideoInfo::*BytesFromAudioSamples)(__int64 samples) const;
  int     (VideoInfo::*AudioChannels)() const;
  int     (VideoInfo::*SampleType)() const;
  bool    (VideoInfo::*IsSampleType)(int testtype) const;
  int     (VideoInfo::*SamplesPerSecond)() const;
  int     (VideoInfo::*BytesPerAudioSample)() const;
  void    (VideoInfo::*SetFieldBased)(bool isfieldbased);
  void    (VideoInfo::*Set)(int property);
  void    (VideoInfo::*Clear)(int property);
  int     (VideoInfo::*GetPlaneWidthSubsampling)(int plane) const;
  int     (VideoInfo::*GetPlaneHeightSubsampling)(int plane) const;
  int     (VideoInfo::*BitsPerPixel)() const;
  int     (VideoInfo::*BytesPerChannelSample)() const;
  void    (VideoInfo::*SetFPS)(unsigned numerator, unsigned denominator);
  void    (VideoInfo::*MulDivFPS)(unsigned multiplier, unsigned divisor);
  bool    (VideoInfo::*IsSameColorspace)(const VideoInfo& vi) const;
// end struct VideoInfo

/**********************************************************************/

// class VideoFrameBuffer
  const BYTE* (VideoFrameBuffer::*VFBGetReadPtr)() const;
  BYTE*       (VideoFrameBuffer::*VFBGetWritePtr)();
  size_t      (VideoFrameBuffer::*GetDataSize)() const;
  int         (VideoFrameBuffer::*GetSequenceNumber)() const;
  int         (VideoFrameBuffer::*GetRefcount)() const;
// end class VideoFrameBuffer

/**********************************************************************/

// class VideoFrame
  int               (VideoFrame::*GetPitch)(int plane) const;
  int               (VideoFrame::*GetRowSize)(int plane) const;
  int               (VideoFrame::*GetHeight)(int plane) const;
  VideoFrameBuffer* (VideoFrame::*GetFrameBuffer)() const;
  size_t            (VideoFrame::*GetOffset)(int plane) const;
  const BYTE*       (VideoFrame::*VFGetReadPtr)(int plane) const;
  bool              (VideoFrame::*IsWritable)() const;
  BYTE*             (VideoFrame::*VFGetWritePtr)(int plane) const;
  void              (VideoFrame::*VideoFrame_DESTRUCTOR)();
// end class VideoFrame

/**********************************************************************/

// class IClip
  /* nothing */
// end class IClip

/**********************************************************************/

// class PClip
  void (PClip::*PClip_CONSTRUCTOR0)();
  void (PClip::*PClip_CONSTRUCTOR1)(const PClip& x);
  void (PClip::*PClip_CONSTRUCTOR2)(IClip* x);
  void (PClip::*PClip_OPERATOR_ASSIGN0)(IClip* x);
  void (PClip::*PClip_OPERATOR_ASSIGN1)(const PClip& x);
  void (PClip::*PClip_DESTRUCTOR)();
// end class PClip

/**********************************************************************/

// class PVideoFrame
  void (PVideoFrame::*PVideoFrame_CONSTRUCTOR0)();
  void (PVideoFrame::*PVideoFrame_CONSTRUCTOR1)(const PVideoFrame& x);
  void (PVideoFrame::*PVideoFrame_CONSTRUCTOR2)(VideoFrame* x);
  void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN0)(VideoFrame* x);
  void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN1)(const PVideoFrame& x);
  void (PVideoFrame::*PVideoFrame_DESTRUCTOR)();
// end class PVideoFrame

/**********************************************************************/

// class AVSValue
  void            (AVSValue::*AVSValue_CONSTRUCTOR0)();
  void            (AVSValue::*AVSValue_CONSTRUCTOR1)(IClip* c);
  void            (AVSValue::*AVSValue_CONSTRUCTOR2)(const PClip& c);
  void            (AVSValue::*AVSValue_CONSTRUCTOR3)(bool b);
  void            (AVSValue::*AVSValue_CONSTRUCTOR4)(int i);
  void            (AVSValue::*AVSValue_CONSTRUCTOR5)(float f);
  void            (AVSValue::*AVSValue_CONSTRUCTOR6)(double f);
  void            (AVSValue::*AVSValue_CONSTRUCTOR7)(const char* s);
  void            (AVSValue::*AVSValue_CONSTRUCTOR8)(const AVSValue* a, int size);
  void            (AVSValue::*AVSValue_CONSTRUCTOR9)(const AVSValue& v);
  void            (AVSValue::*AVSValue_DESTRUCTOR)();
  AVSValue&       (AVSValue::*AVSValue_OPERATOR_ASSIGN)(const AVSValue& v);
  const AVSValue& (AVSValue::*AVSValue_OPERATOR_INDEX)(int index) const;
  bool            (AVSValue::*Defined)() const;
  bool            (AVSValue::*IsClip)() const;
  bool            (AVSValue::*IsBool)() const;
  bool            (AVSValue::*IsInt)() const;
  bool            (AVSValue::*IsFloat)() const;
  bool            (AVSValue::*IsString)() const;
  bool            (AVSValue::*IsArray)() const;
  PClip           (AVSValue::*AsClip)() const;
  bool            (AVSValue::*AsBool1)() const;
  int             (AVSValue::*AsInt1)() const;
  const char*     (AVSValue::*AsString1)() const;
  double          (AVSValue::*AsFloat1)() const;
  bool            (AVSValue::*AsBool2)(bool def) const;
  int             (AVSValue::*AsInt2)(int def) const;
  double          (AVSValue::*AsDblDef)(double def) const;
  double          (AVSValue::*AsFloat2)(float def) const;
  const char*     (AVSValue::*AsString2)(const char* def) const;
  int             (AVSValue::*ArraySize)() const;
// end class AVSValue

/**********************************************************************/
};

#ifdef AVISYNTH_CORE
/* Macro resolution for code inside Avisynth.dll */
# define AVS_BakedCode(arg) ;
# define AVS_LinkCall(arg)
# define AVS_LinkCallV(arg)

#else
/* Macro resolution for code inside user plugin */
# ifdef AVS_LINKAGE_DLLIMPORT
extern __declspec(dllimport) const AVS_Linkage* const AVS_linkage;
# else
extern const AVS_Linkage* AVS_linkage;
# endif

# ifndef offsetof
#  include <stddef.h>
# endif

# define AVS_BakedCode(arg) { arg ; }
# define AVS_LinkCall(arg)  !AVS_linkage || offsetof(AVS_Linkage, arg) >= AVS_linkage->Size ?     0 : (this->*(AVS_linkage->arg))
# define AVS_LinkCallV(arg) !AVS_linkage || offsetof(AVS_Linkage, arg) >= AVS_linkage->Size ? *this : (this->*(AVS_linkage->arg))

#endif

struct VideoInfo {
  int width, height;    // width=0 means no video
  unsigned fps_numerator, fps_denominator;
  int num_frames;
  // This is more extensible than previous versions.
  // More properties can be added seeminglesly.

  // Colorspace properties.
/*
7<<0  Planar Width Subsampling bits
      Use (X+1) & 3 for GetPlaneWidthSubsampling
        000 => 1        YV12, YV16
        001 => 2        YV411, YUV9
        010 => reserved
        011 => 0        YV24
        1xx => reserved

1<<3  VPlaneFirst YV12, YV16, YV24, YV411, YUV9
1<<4  UPlaneFirst I420

7<<8  Planar Height Subsampling bits
      Use ((X>>8)+1) & 3 for GetPlaneHeightSubsampling
        000 => 1        YV12
        001 => 2        YUV9
        010 => reserved
        011 => 0        YV16, YV24, YV411
        1xx => reserved

7<<16 Sample resolution bits
        000 => 8
        001 => 16
        010 => 32
        011 => reserved
        1xx => reserved

Planar match mask  1111.0000.0000.0111.0000.0111.0000.0111
Planar signature   10xx.0000.0000.00xx.0000.00xx.00xx.00xx
Planar filter mask 1111.1111.1111.1111.1111.1111.1100.1111
*/
  enum {
    CS_BGR = 1<<28,
    CS_YUV = 1<<29,
    CS_INTERLEAVED = 1<<30,
    CS_PLANAR = 1<<31,

    CS_Shift_Sub_Width   =  0,
    CS_Shift_Sub_Height  =  8,
    CS_Shift_Sample_Bits = 16,

    CS_Sub_Width_Mask    = 7 << CS_Shift_Sub_Width,
    CS_Sub_Width_1       = 3 << CS_Shift_Sub_Width, // YV24
    CS_Sub_Width_2       = 0 << CS_Shift_Sub_Width, // YV12, I420, YV16
    CS_Sub_Width_4       = 1 << CS_Shift_Sub_Width, // YUV9, YV411

    CS_VPlaneFirst       = 1 << 3, // YV12, YV16, YV24, YV411, YUV9
    CS_UPlaneFirst       = 1 << 4, // I420

    CS_Sub_Height_Mask   = 7 << CS_Shift_Sub_Height,
    CS_Sub_Height_1      = 3 << CS_Shift_Sub_Height, // YV16, YV24, YV411
    CS_Sub_Height_2      = 0 << CS_Shift_Sub_Height, // YV12, I420
    CS_Sub_Height_4      = 1 << CS_Shift_Sub_Height, // YUV9

    CS_Sample_Bits_Mask  = 7 << CS_Shift_Sample_Bits,
    CS_Sample_Bits_8     = 0 << CS_Shift_Sample_Bits,
    CS_Sample_Bits_16    = 1 << CS_Shift_Sample_Bits,
    CS_Sample_Bits_32    = 2 << CS_Shift_Sample_Bits,

    CS_PLANAR_MASK       = CS_PLANAR | CS_INTERLEAVED | CS_YUV | CS_BGR | CS_Sample_Bits_Mask
	                                                  | CS_Sub_Height_Mask | CS_Sub_Width_Mask,
    CS_PLANAR_FILTER     = ~( CS_VPlaneFirst | CS_UPlaneFirst ),

  // Specific colorformats
    CS_UNKNOWN = 0,
    CS_BGR24 = 1<<0 | CS_BGR | CS_INTERLEAVED,
    CS_BGR32 = 1<<1 | CS_BGR | CS_INTERLEAVED,
    CS_YUY2  = 1<<2 | CS_YUV | CS_INTERLEAVED,
//  CS_YV12  = 1<<3  Reserved
//  CS_I420  = 1<<4  Reserved
    CS_RAW32 = 1<<5 | CS_INTERLEAVED,

//  YV12 must be 0xA000008 2.5 Baked API will see all new planar as YV12
//  I420 must be 0xA000010

    CS_YV24  = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_VPlaneFirst | CS_Sub_Height_1 | CS_Sub_Width_1,  // YVU 4:4:4 planar
    CS_YV16  = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_VPlaneFirst | CS_Sub_Height_1 | CS_Sub_Width_2,  // YVU 4:2:2 planar
    CS_YV12  = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_VPlaneFirst | CS_Sub_Height_2 | CS_Sub_Width_2,  // YVU 4:2:0 planar
    CS_I420  = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_UPlaneFirst | CS_Sub_Height_2 | CS_Sub_Width_2,  // YUV 4:2:0 planar
    CS_IYUV  = CS_I420,
    CS_YUV9  = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_VPlaneFirst | CS_Sub_Height_4 | CS_Sub_Width_4,  // YUV 4:1:0 planar
    CS_YV411 = CS_PLANAR | CS_YUV | CS_Sample_Bits_8 | CS_VPlaneFirst | CS_Sub_Height_1 | CS_Sub_Width_4,  // YUV 4:1:1 planar

    CS_Y8    = CS_PLANAR | CS_INTERLEAVED | CS_YUV | CS_Sample_Bits_8,                                     // Y   4:0:0 planar
/*
    CS_YV48  = CS_PLANAR | CS_YUV | CS_Sample_Bits_16 | CS_VPlaneFirst | CS_Sub_Height_1 | CS_Sub_Width_1, // YUV 4:4:4 16bit samples
    CS_Y16   = CS_PLANAR | CS_INTERLEAVED | CS_YUV | CS_Sample_Bits_16,                                    // Y   4:0:0 16bit samples

    CS_YV96  = CS_PLANAR | CS_YUV | CS_Sample_Bits_32 | CS_VPlaneFirst | CS_Sub_Height_1 | CS_Sub_Width_1, // YUV 4:4:4 32bit samples
    CS_Y32   = CS_PLANAR | CS_INTERLEAVED | CS_YUV | CS_Sample_Bits_32,                                    // Y   4:0:0 32bit samples

    CS_PRGB  = CS_PLANAR | CS_RGB | CS_Sample_Bits_8,                                                      // Planar RGB
    CS_RGB48 = CS_PLANAR | CS_RGB | CS_Sample_Bits_16,                                                     // Planar RGB 16bit samples
    CS_RGB96 = CS_PLANAR | CS_RGB | CS_Sample_Bits_32,                                                     // Planar RGB 32bit samples
*/
  };

  int pixel_type;                // changed to int as of 2.5


  int audio_samples_per_second;   // 0 means no audio
  int sample_type;                // as of 2.5
  __int64 num_audio_samples;      // changed as of 2.5
  int nchannels;                  // as of 2.5

  // Imagetype properties

  int image_type;

  enum {
    IT_BFF = 1<<0,
    IT_TFF = 1<<1,
    IT_FIELDBASED = 1<<2
  };

  // Chroma placement bits 20 -> 23  ::FIXME:: Really want a Class to support this
  enum {
    CS_UNKNOWN_CHROMA_PLACEMENT = 0 << 20,
    CS_MPEG1_CHROMA_PLACEMENT   = 1 << 20,
    CS_MPEG2_CHROMA_PLACEMENT   = 2 << 20,
    CS_YUY2_CHROMA_PLACEMENT    = 3 << 20,
    CS_TOPLEFT_CHROMA_PLACEMENT = 4 << 20
  };

  // useful functions of the above
  bool HasVideo() const AVS_BakedCode( return AVS_LinkCall(HasVideo)() )
  bool HasAudio() const AVS_BakedCode( return AVS_LinkCall(HasAudio)() )
  bool IsRGB() const AVS_BakedCode( return AVS_LinkCall(IsRGB)() )
  bool IsRGB24() const AVS_BakedCode( return AVS_LinkCall(IsRGB24)() )
  bool IsRGB32() const AVS_BakedCode( return AVS_LinkCall(IsRGB32)() )
  bool IsYUV() const AVS_BakedCode( return AVS_LinkCall(IsYUV)() )
  bool IsYUY2() const AVS_BakedCode( return AVS_LinkCall(IsYUY2)() )

  bool IsYV24()  const AVS_BakedCode( return AVS_LinkCall(IsYV24)() )
  bool IsYV16()  const AVS_BakedCode( return AVS_LinkCall(IsYV16)() )
  bool IsYV12()  const AVS_BakedCode( return AVS_LinkCall(IsYV12)() )
  bool IsYV411() const AVS_BakedCode( return AVS_LinkCall(IsYV411)() )
//bool IsYUV9()  const;
  bool IsY8()    const AVS_BakedCode( return AVS_LinkCall(IsY8)() )

  bool IsColorSpace(int c_space) const AVS_BakedCode( return AVS_LinkCall(IsColorSpace)(c_space) )

  bool Is(int property) const AVS_BakedCode( return AVS_LinkCall(Is)(property) )
  bool IsPlanar() const AVS_BakedCode( return AVS_LinkCall(IsPlanar)() )
  bool IsFieldBased() const AVS_BakedCode( return AVS_LinkCall(IsFieldBased)() )
  bool IsParityKnown() const AVS_BakedCode( return AVS_LinkCall(IsParityKnown)() )
  bool IsBFF() const AVS_BakedCode( return AVS_LinkCall(IsBFF)() )
  bool IsTFF() const AVS_BakedCode( return AVS_LinkCall(IsTFF)() )

  bool IsVPlaneFirst() const AVS_BakedCode( return AVS_LinkCall(IsVPlaneFirst)() )  // Don't use this
  // Will not work on planar images, but will return only luma planes
  int BytesFromPixels(int pixels) const AVS_BakedCode( return AVS_LinkCall(BytesFromPixels)(pixels) )
  int RowSize(int plane=0) const AVS_BakedCode( return AVS_LinkCall(RowSize)(plane) )
  int BMPSize() const AVS_BakedCode( return AVS_LinkCall(BMPSize)() )

  __int64 AudioSamplesFromFrames(int frames) const AVS_BakedCode( return AVS_LinkCall(AudioSamplesFromFrames)(frames) )
  int FramesFromAudioSamples(__int64 samples) const AVS_BakedCode( return AVS_LinkCall(FramesFromAudioSamples)(samples) )
  __int64 AudioSamplesFromBytes(__int64 bytes) const AVS_BakedCode( return AVS_LinkCall(AudioSamplesFromBytes)(bytes) )
  __int64 BytesFromAudioSamples(__int64 samples) const AVS_BakedCode( return AVS_LinkCall(BytesFromAudioSamples)(samples) )
  int AudioChannels() const AVS_BakedCode( return AVS_LinkCall(AudioChannels)() )
  int SampleType() const AVS_BakedCode( return AVS_LinkCall(SampleType)() )
  bool IsSampleType(int testtype) const AVS_BakedCode( return AVS_LinkCall(IsSampleType)(testtype) )
  int SamplesPerSecond() const AVS_BakedCode( return AVS_LinkCall(SamplesPerSecond)() )
  int BytesPerAudioSample() const AVS_BakedCode( return AVS_LinkCall(BytesPerAudioSample)() )
  void SetFieldBased(bool isfieldbased) AVS_BakedCode( AVS_LinkCall(SetFieldBased)(isfieldbased) )
  void Set(int property) AVS_BakedCode( AVS_LinkCall(Set)(property) )
  void Clear(int property) AVS_BakedCode( AVS_LinkCall(Clear)(property) )
  // Subsampling in bitshifts!
  int GetPlaneWidthSubsampling(int plane) const AVS_BakedCode( return AVS_LinkCall(GetPlaneWidthSubsampling)(plane) )
  int GetPlaneHeightSubsampling(int plane) const AVS_BakedCode( return AVS_LinkCall(GetPlaneHeightSubsampling)(plane) )
  int BitsPerPixel() const AVS_BakedCode( return AVS_LinkCall(BitsPerPixel)() )

  int BytesPerChannelSample() const AVS_BakedCode( return AVS_LinkCall(BytesPerChannelSample)() )

  // useful mutator
  void SetFPS(unsigned numerator, unsigned denominator) AVS_BakedCode( AVS_LinkCall(SetFPS)(numerator, denominator) )

  // Range protected multiply-divide of FPS
  void MulDivFPS(unsigned multiplier, unsigned divisor) AVS_BakedCode( AVS_LinkCall(MulDivFPS)(multiplier, divisor) )

  // Test for same colorspace
  bool IsSameColorspace(const VideoInfo& vi) const AVS_BakedCode( return AVS_LinkCall(IsSameColorspace)(vi) )

}; // end struct VideoInfo




// VideoFrameBuffer holds information about a memory block which is used
// for video data.  For efficiency, instances of this class are not deleted
// when the refcount reaches zero; instead they're stored in a linked list
// to be reused.  The instances are deleted when the corresponding AVS
// file is closed.

class VideoFrameBuffer {
  BYTE* const data;
  const size_t data_size;
  // sequence_number is incremented every time the buffer is changed, so
  // that stale views can tell they're no longer valid.
  volatile long sequence_number;

  friend class VideoFrame;
  friend class Cache;
  friend class ScriptEnvironment;
  volatile long refcount;

protected:
  VideoFrameBuffer(size_t size);
  VideoFrameBuffer();
  ~VideoFrameBuffer();

public:
  const BYTE* GetReadPtr() const AVS_BakedCode( return AVS_LinkCall(VFBGetReadPtr)() )
  BYTE* GetWritePtr() AVS_BakedCode( return AVS_LinkCall(VFBGetWritePtr)() )
  size_t GetDataSize() const AVS_BakedCode( return AVS_LinkCall(GetDataSize)() )
  int GetSequenceNumber() const AVS_BakedCode( return AVS_LinkCall(GetSequenceNumber)() )
  int GetRefcount() const AVS_BakedCode( return AVS_LinkCall(GetRefcount)() )
}; // end class VideoFrameBuffer


// VideoFrame holds a "window" into a VideoFrameBuffer.  Operator new
// is overloaded to recycle class instances.

class VideoFrame {
  volatile long refcount;
  VideoFrameBuffer* const vfb;
  const size_t offset;
  const int pitch, row_size, height;
  const size_t offsetU, offsetV;  // U&V offsets are from top of picture.
  const int pitchUV, row_sizeUV, heightUV;

  friend class PVideoFrame;
  void AddRef();
  void Release();

  friend class ScriptEnvironment;
  friend class Cache;

  VideoFrame(VideoFrameBuffer* _vfb, size_t _offset, int _pitch, int _row_size, int _height);
  VideoFrame(VideoFrameBuffer* _vfb, size_t _offset, int _pitch, int _row_size, int _height,
             size_t _offsetU, size_t _offsetV, int _pitchUV, int _row_sizeUV, int _heightUV);

  void* operator new(size_t size);
// TESTME: OFFSET U/V may be switched to what could be expected from AVI standard!
public:
  int GetPitch(int plane=0) const AVS_BakedCode( return AVS_LinkCall(GetPitch)(plane) )
  int GetRowSize(int plane=0) const AVS_BakedCode( return AVS_LinkCall(GetRowSize)(plane) )
  int GetHeight(int plane=0) const AVS_BakedCode( return AVS_LinkCall(GetHeight)(plane) )

  // generally you shouldn't use these three
  VideoFrameBuffer* GetFrameBuffer() const AVS_BakedCode( return AVS_LinkCall(GetFrameBuffer)() )
  size_t GetOffset(int plane=0) const AVS_BakedCode( return AVS_LinkCall(GetOffset)(plane) )

  // in plugins use env->SubFrame() -- because implementation code is only available inside avisynth.dll. Doh!
  VideoFrame* Subframe(int rel_offset, int new_pitch, int new_row_size, int new_height) const;
  VideoFrame* Subframe(int rel_offset, int new_pitch, int new_row_size, int new_height,
                       int rel_offsetU, int rel_offsetV, int pitchUV) const;

  const BYTE* GetReadPtr(int plane=0) const AVS_BakedCode( return AVS_LinkCall(VFGetReadPtr)(plane) )
  bool IsWritable() const AVS_BakedCode( return AVS_LinkCall(IsWritable)() )
  BYTE* GetWritePtr(int plane=0) const AVS_BakedCode( return AVS_LinkCall(VFGetWritePtr)(plane) )

  ~VideoFrame() AVS_BakedCode( AVS_LinkCall(VideoFrame_DESTRUCTOR)() )
#ifdef AVISYNTH_CORE
public:
  void DESTRUCTOR();  /* Damn compiler won't allow taking the address of reserved constructs, make a dummy interlude */
#endif
}; // end class VideoFrame

enum {
  // Values 0 to 5 are reserved for old 2.5 plugins
  // do not use them in new plugins

  // New 2.6 explicitly defined cache hints.
  CACHE_NOTHING=10, // Do not cache video.
  CACHE_WINDOW=11, // Hard protect upto X frames within a range of X from the current frame N.
  CACHE_GENERIC=12, // LRU cache upto X frames.
  CACHE_FORCE_GENERIC=13, // LRU cache upto X frames, override any previous CACHE_WINDOW.

  CACHE_GET_POLICY=30, // Get the current policy.
  CACHE_GET_WINDOW=31, // Get the current window h_span.
  CACHE_GET_RANGE=32, // Get the current generic frame range.

  CACHE_AUDIO=50, // Explicitly cache audio, X byte cache.
  CACHE_AUDIO_NOTHING=51, // Explicitly do not cache audio.
  CACHE_AUDIO_NONE=52, // Audio cache off (auto mode), X byte intial cache.
  CACHE_AUDIO_AUTO=53, // Audio cache on (auto mode), X byte intial cache.

  CACHE_GET_AUDIO_POLICY=70, // Get the current audio policy.
  CACHE_GET_AUDIO_SIZE=71, // Get the current audio cache size.

  CACHE_PREFETCH_FRAME=100, // Queue request to prefetch frame N.
  CACHE_PREFETCH_GO=101, // Action video prefetches.

  CACHE_PREFETCH_AUDIO_BEGIN=120, // Begin queue request transaction to prefetch audio (take critical section).
  CACHE_PREFETCH_AUDIO_STARTLO=121, // Set low 32 bits of start.
  CACHE_PREFETCH_AUDIO_STARTHI=122, // Set high 32 bits of start.
  CACHE_PREFETCH_AUDIO_COUNT=123, // Set low 32 bits of length.
  CACHE_PREFETCH_AUDIO_COMMIT=124, // Enqueue request transaction to prefetch audio (release critical section).
  CACHE_PREFETCH_AUDIO_GO=125, // Action audio prefetches.

  CACHE_GETCHILD_CACHE_MODE=200, // Cache ask Child for desired video cache mode.
  CACHE_GETCHILD_CACHE_SIZE=201, // Cache ask Child for desired video cache size.
  CACHE_GETCHILD_AUDIO_MODE=202, // Cache ask Child for desired audio cache mode.
  CACHE_GETCHILD_AUDIO_SIZE=203, // Cache ask Child for desired audio cache size.

  CACHE_GETCHILD_COST=220, // Cache ask Child for estimated processing cost.
    CACHE_COST_ZERO=221, // Child response of zero cost (ptr arithmetic only).
    CACHE_COST_UNIT=222, // Child response of unit cost (less than or equal 1 full frame blit).
    CACHE_COST_LOW=223, // Child response of light cost. (Fast)
    CACHE_COST_MED=224, // Child response of medium cost. (Real time)
    CACHE_COST_HI=225, // Child response of heavy cost. (Slow)

  CACHE_GETCHILD_THREAD_MODE=240, // Cache ask Child for thread safetyness.
    CACHE_THREAD_UNSAFE=241, // Only 1 thread allowed for all instances. 2.5 filters default!
    CACHE_THREAD_CLASS=242, // Only 1 thread allowed for each instance. 2.6 filters default!
    CACHE_THREAD_SAFE=243, //  Allow all threads in any instance.
    CACHE_THREAD_OWN=244, // Safe but limit to 1 thread, internally threaded.

  CACHE_GETCHILD_ACCESS_COST=260, // Cache ask Child for preferred access pattern.
    CACHE_ACCESS_RAND=261, // Filter is access order agnostic.
    CACHE_ACCESS_SEQ0=262, // Filter prefers sequential access (low cost)
    CACHE_ACCESS_SEQ1=263, // Filter needs sequential access (high cost)

};

// Base class for all filters.
class IClip {
  friend class PClip;
  friend class AVSValue;
  volatile long refcnt;
  void AddRef();
  void Release();
public:
  IClip() : refcnt(0) {}
  virtual int __stdcall GetVersion() { return AVISYNTH_INTERFACE_VERSION; }
  virtual PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) = 0;
  virtual bool __stdcall GetParity(int n) = 0;  // return field parity if field_based, else parity of first field in frame
  virtual void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) = 0;  // start and count are in samples
  /* Need to check GetVersion first, pre v5 will return random crap from EAX reg. */
  virtual int __stdcall SetCacheHints(int cachehints,int frame_range) = 0 ;  // We do not pass cache requests upwards, only to the next filter.
  virtual const VideoInfo& __stdcall GetVideoInfo() = 0;
  virtual __stdcall ~IClip() {}
}; // end class IClip


// smart pointer to IClip
class PClip {

  IClip* p;

  IClip* GetPointerWithAddRef() const;
  friend class AVSValue;
  friend class VideoFrame;

  void Init(IClip* x);
  void Set(IClip* x);

public:
  PClip() AVS_BakedCode( AVS_LinkCall(PClip_CONSTRUCTOR0)() )
  PClip(const PClip& x) AVS_BakedCode( AVS_LinkCall(PClip_CONSTRUCTOR1)(x) )
  PClip(IClip* x) AVS_BakedCode( AVS_LinkCall(PClip_CONSTRUCTOR2)(x) )
  void operator=(IClip* x) AVS_BakedCode( AVS_LinkCall(PClip_OPERATOR_ASSIGN0)(x) )
  void operator=(const PClip& x) AVS_BakedCode( AVS_LinkCall(PClip_OPERATOR_ASSIGN1)(x) )

  IClip* operator->() const { return p; }

  // useful in conditional expressions
  operator void*() const { return p; }
  bool operator!() const { return !p; }

  ~PClip() AVS_BakedCode( AVS_LinkCall(PClip_DESTRUCTOR)() )
#ifdef AVISYNTH_CORE
public:
  void CONSTRUCTOR0();  /* Damn compiler won't allow taking the address of reserved constructs, make a dummy interlude */
  void CONSTRUCTOR1(const PClip& x);
  void CONSTRUCTOR2(IClip* x);
  void OPERATOR_ASSIGN0(IClip* x);
  void OPERATOR_ASSIGN1(const PClip& x);
  void DESTRUCTOR();
#endif
}; // end class PClip


// smart pointer to VideoFrame
class PVideoFrame {

  VideoFrame* p;

  void Init(VideoFrame* x);
  void Set(VideoFrame* x);

public:
  PVideoFrame() AVS_BakedCode( AVS_LinkCall(PVideoFrame_CONSTRUCTOR0)() )
  PVideoFrame(const PVideoFrame& x) AVS_BakedCode( AVS_LinkCall(PVideoFrame_CONSTRUCTOR1)(x) )
  PVideoFrame(VideoFrame* x) AVS_BakedCode( AVS_LinkCall(PVideoFrame_CONSTRUCTOR2)(x) )
  void operator=(VideoFrame* x) AVS_BakedCode( AVS_LinkCall(PVideoFrame_OPERATOR_ASSIGN0)(x) )
  void operator=(const PVideoFrame& x) AVS_BakedCode( AVS_LinkCall(PVideoFrame_OPERATOR_ASSIGN1)(x) )

  VideoFrame* operator->() const { return p; }

  // for conditional expressions
  operator void*() const { return p; }
  bool operator!() const { return !p; }

  ~PVideoFrame() AVS_BakedCode( AVS_LinkCall(PVideoFrame_DESTRUCTOR)() )
#ifdef AVISYNTH_CORE
public:
  void CONSTRUCTOR0();  /* Damn compiler won't allow taking the address of reserved constructs, make a dummy interlude */
  void CONSTRUCTOR1(const PVideoFrame& x);
  void CONSTRUCTOR2(VideoFrame* x);
  void OPERATOR_ASSIGN0(VideoFrame* x);
  void OPERATOR_ASSIGN1(const PVideoFrame& x);
  void DESTRUCTOR();
#endif
}; // end class PVideoFrame


class AVSValue {
public:

  AVSValue() AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR0)() )
  AVSValue(IClip* c) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR1)(c) )
  AVSValue(const PClip& c) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR2)(c) )
  AVSValue(bool b) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR3)(b) )
  AVSValue(int i) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR4)(i) )
//  AVSValue(__int64 l);
  AVSValue(float f) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR5)(f) )
  AVSValue(double f) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR6)(f) )
  AVSValue(const char* s) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR7)(s) )
  AVSValue(const AVSValue* a, int size) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR8)(a, size) )
  AVSValue(const AVSValue& v) AVS_BakedCode( AVS_LinkCall(AVSValue_CONSTRUCTOR9)(v) )

  ~AVSValue() AVS_BakedCode( AVS_LinkCall(AVSValue_DESTRUCTOR)() )
  AVSValue& operator=(const AVSValue& v) AVS_BakedCode( return AVS_LinkCallV(AVSValue_OPERATOR_ASSIGN)(v) )

  // Note that we transparently allow 'int' to be treated as 'float'.
  // There are no int<->bool conversions, though.

  bool Defined() const AVS_BakedCode( return AVS_LinkCall(Defined)() )
  bool IsClip() const AVS_BakedCode( return AVS_LinkCall(IsClip)() )
  bool IsBool() const AVS_BakedCode( return AVS_LinkCall(IsBool)() )
  bool IsInt() const AVS_BakedCode( return AVS_LinkCall(IsInt)() )
//  bool IsLong() const;
  bool IsFloat() const AVS_BakedCode( return AVS_LinkCall(IsFloat)() )
  bool IsString() const AVS_BakedCode( return AVS_LinkCall(IsString)() )
  bool IsArray() const AVS_BakedCode( return AVS_LinkCall(IsArray)() )

  PClip AsClip() const AVS_BakedCode( return AVS_LinkCall(AsClip)() )
  bool AsBool() const AVS_BakedCode( return AVS_LinkCall(AsBool1)() )
  int AsInt() const AVS_BakedCode( return AVS_LinkCall(AsInt1)() )
//  int AsLong() const;
  const char* AsString() const AVS_BakedCode( return AVS_LinkCall(AsString1)() )
  double AsFloat() const AVS_BakedCode( return AVS_LinkCall(AsFloat1)() )

  bool AsBool(bool def) const AVS_BakedCode( return AVS_LinkCall(AsBool2)(def) )
  int AsInt(int def) const AVS_BakedCode( return AVS_LinkCall(AsInt2)(def) )
  double AsDblDef(double def) const AVS_BakedCode( return AVS_LinkCall(AsDblDef)(def) ) // Value is still a float
//float AsFloat(double def) const; // def demoted to a float
  double AsFloat(float def) const AVS_BakedCode( return AVS_LinkCall(AsFloat2)(def) )
  const char* AsString(const char* def) const AVS_BakedCode( return AVS_LinkCall(AsString2)(def) )

  int ArraySize() const AVS_BakedCode( return AVS_LinkCall(ArraySize)() )

  const AVSValue& operator[](int index) const AVS_BakedCode( return AVS_LinkCallV(AVSValue_OPERATOR_INDEX)(index) )

private:
#ifdef AVISYNTH64
  union {
    IClip* clip;
    bool boolean;
    __int64 integer;
    double floating_pt;
    const char* string;
    const AVSValue* array;
  };
#endif
  short type;  // 'a'rray, 'c'lip, 'b'ool, 'i'nt, 'f'loat, 's'tring, 'v'oid, or 'l'ong
  short array_size;
#ifndef AVISYNTH64
  union {
    IClip* clip;
    bool boolean;
    int integer;
    float floating_pt;
    const char* string;
    const AVSValue* array;
//    __int64 longlong;
  };
#endif

  void Assign(const AVSValue* src, bool init);
#ifdef AVISYNTH_CORE
public:
  void            CONSTRUCTOR0();  /* Damn compiler won't allow taking the address of reserved constructs, make a dummy interlude */
  void            CONSTRUCTOR1(IClip* c);
  void            CONSTRUCTOR2(const PClip& c);
  void            CONSTRUCTOR3(bool b);
  void            CONSTRUCTOR4(int i);
  void            CONSTRUCTOR5(float f);
  void            CONSTRUCTOR6(double f);
  void            CONSTRUCTOR7(const char* s);
  void            CONSTRUCTOR8(const AVSValue* a, int size);
  void            CONSTRUCTOR9(const AVSValue& v);
  void            DESTRUCTOR();
  AVSValue&       OPERATOR_ASSIGN(const AVSValue& v);
  const AVSValue& OPERATOR_INDEX(int index) const;

  bool            AsBool1() const;
  int             AsInt1() const;
  const char*     AsString1() const;
  double          AsFloat1() const;

  bool            AsBool2(bool def) const;
  int             AsInt2(int def) const;
  double          AsFloat2(float def) const;
  const char*     AsString2(const char* def) const;
#endif
}; // end class AVSValue

#undef AVS_LinkCallV
#undef AVS_LinkCall
#undef AVS_BakedCode

// instantiable null filter
class GenericVideoFilter : public IClip {
protected:
  PClip child;
  VideoInfo vi;
public:
  GenericVideoFilter(PClip _child) : child(_child) { vi = child->GetVideoInfo(); }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) { return child->GetFrame(n, env); }
  void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) { child->GetAudio(buf, start, count, env); }
  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) { return child->GetParity(n); }
  int __stdcall SetCacheHints(int cachehints,int frame_range) { return 0; } ;  // We do not pass cache requests upwards, only to the next filter.
};




// For GetCPUFlags.  These are backwards-compatible with those in VirtualDub.
enum {
                    /* oldest CPU to support extension */
  CPUF_FORCE        =   0x01,   //  N/A
  CPUF_FPU          =   0x02,   //  386/486DX
  CPUF_MMX          =   0x04,   //  P55C, K6, PII
  CPUF_INTEGER_SSE  =   0x08,   //  PIII, Athlon
  CPUF_SSE          =   0x10,   //  PIII, Athlon XP/MP
  CPUF_SSE2         =   0x20,   //  PIV, K8
  CPUF_3DNOW        =   0x40,   //  K6-2
  CPUF_3DNOW_EXT    =   0x80,   //  Athlon
  CPUF_X86_64       =   0xA0,   //  Hammer (note: equiv. to 3DNow + SSE2, which
                                //          only Hammer will have anyway)
  CPUF_SSE3         =  0x100,   //  PIV+, K8 Venice
  CPUF_SSSE3        =  0x200,   //  Core 2
  CPUF_SSE4         =  0x400,   //  Penryn, Wolfdale, Yorkfield
  CPUF_SSE4_1       =  0x400,
//CPUF_AVX          =  0x800,   //  Sandy Bridge, Bulldozer
  CPUF_SSE4_2       = 0x1000,   //  Nehalem
//CPUF_AVX2         = 0x2000,   //  Haswell
//CPUF_AVX512       = 0x4000,   //  Knights Landing
};



class IScriptEnvironment {
public:
  virtual __stdcall ~IScriptEnvironment() {}

  virtual /*static*/ long __stdcall GetCPUFlags() = 0;

  virtual char* __stdcall SaveString(const char* s, int length = -1) = 0;
  virtual char* __stdcall Sprintf(const char* fmt, ...) = 0;
  // note: val is really a va_list; I hope everyone typedefs va_list to a pointer
  virtual char* __stdcall VSprintf(const char* fmt, void* val) = 0;

  __declspec(noreturn) virtual void __stdcall ThrowError(const char* fmt, ...) = 0;

  class NotFound /*exception*/ {};  // thrown by Invoke and GetVar

  typedef AVSValue (__cdecl *ApplyFunc)(AVSValue args, void* user_data, IScriptEnvironment* env);

  virtual void __stdcall AddFunction(const char* name, const char* params, ApplyFunc apply, void* user_data) = 0;
  virtual bool __stdcall FunctionExists(const char* name) = 0;
  virtual AVSValue __stdcall Invoke(const char* name, const AVSValue args, const char* const* arg_names=0) = 0;

  virtual AVSValue __stdcall GetVar(const char* name) = 0;
  virtual bool __stdcall SetVar(const char* name, const AVSValue& val) = 0;
  virtual bool __stdcall SetGlobalVar(const char* name, const AVSValue& val) = 0;

  virtual void __stdcall PushContext(int level=0) = 0;
  virtual void __stdcall PopContext() = 0;

  virtual PVideoFrame __stdcall NewVideoFrame(const VideoInfo& vi, int align=FRAME_ALIGN) = 0;

  virtual bool __stdcall MakeWritable(PVideoFrame* pvf) = 0;

  virtual void __stdcall BitBlt(BYTE* dstp, int dst_pitch, const BYTE* srcp, int src_pitch, int row_size, int height) = 0;

  typedef void (__cdecl *ShutdownFunc)(void* user_data, IScriptEnvironment* env);
  virtual void __stdcall AtExit(ShutdownFunc function, void* user_data) = 0;

  virtual void __stdcall CheckVersion(int version = AVISYNTH_INTERFACE_VERSION) = 0;

  virtual PVideoFrame __stdcall Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height) = 0;

  virtual int __stdcall SetMemoryMax(int mem) = 0;

  virtual int __stdcall SetWorkingDir(const char * newdir) = 0;

  virtual void* __stdcall ManageCache(int key, void* data) = 0;

  enum PlanarChromaAlignmentMode {
            PlanarChromaAlignmentOff,
            PlanarChromaAlignmentOn,
            PlanarChromaAlignmentTest };

  virtual bool __stdcall PlanarChromaAlignment(PlanarChromaAlignmentMode key) = 0;

  virtual PVideoFrame __stdcall SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size,
                                               int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV) = 0;

  virtual void __stdcall DeleteScriptEnvironment() = 0;

  virtual void _stdcall ApplyMessage(PVideoFrame* frame, const VideoInfo& vi, const char* message, int size,
                                     int textcolor, int halocolor, int bgcolor) = 0;

  virtual const AVS_Linkage* const __stdcall GetAVSLinkage() = 0;

  // noThrow version of GetVar
  virtual AVSValue __stdcall GetVarDef(const char* name, const AVSValue& def=AVSValue()) = 0;

}; // end class IScriptEnvironment


// avisynth.dll exports this; it's a way to use it as a library, without
// writing an AVS script or without going through AVIFile.
IScriptEnvironment* __stdcall CreateScriptEnvironment(int version = AVISYNTH_INTERFACE_VERSION);


#pragma pack(pop)

#endif //__AVISYNTH_6_H__
