// Avisynth v2.5.  Copyright 2002, 2005 Ben Rudiak-Gould et al.
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


/* Maintenance notes :-
 *
 *   All the code here was formally baked into the avisynth.h interface.
 *
 *   Whenever you modify any code here please keep and mark the original
 *   code with a block comment beginning with the word "Baked"
 *
 *   Be mindful with changes you do make that previous 2.5 plugins will
 *   still have the original code active. This may require other defensive
 *   or mitigating code elsewhere.
 */

#include "avisynth.h"


/**********************************************************************/

// struct VideoInfo

// useful functions of the above
bool VideoInfo::HasVideo() const { return (width!=0); }
bool VideoInfo::HasAudio() const { return (audio_samples_per_second!=0); }
bool VideoInfo::IsRGB() const { return !!(pixel_type&CS_BGR); }
bool VideoInfo::IsRGB24() const { return (pixel_type&CS_BGR24)==CS_BGR24; } // Clear out additional properties
bool VideoInfo::IsRGB32() const { return (pixel_type & CS_BGR32) == CS_BGR32 ; }
bool VideoInfo::IsYUV() const { return !!(pixel_type&CS_YUV ); }
bool VideoInfo::IsYUY2() const { return (pixel_type & CS_YUY2) == CS_YUY2; }

bool VideoInfo::IsYV24()  const { return (pixel_type & CS_PLANAR_MASK) == (CS_YV24  & CS_PLANAR_FILTER); }
bool VideoInfo::IsYV16()  const { return (pixel_type & CS_PLANAR_MASK) == (CS_YV16  & CS_PLANAR_FILTER); }
bool VideoInfo::IsYV12()  const { return (pixel_type & CS_PLANAR_MASK) == (CS_YV12  & CS_PLANAR_FILTER); }
bool VideoInfo::IsY8()    const { return (pixel_type & CS_PLANAR_MASK) == (CS_Y8    & CS_PLANAR_FILTER); }

bool VideoInfo::IsYV411() const { return (pixel_type & CS_PLANAR_MASK) == (CS_YV411 & CS_PLANAR_FILTER); }
//bool VideoInfo::IsYUV9()  const { return (pixel_type & CS_PLANAR_MASK) == (CS_YUV9  & CS_PLANAR_FILTER); }

/* Baked ********************
bool VideoInfo::IsColorSpace(int c_space) const { return ((pixel_type & c_space) == c_space); }
   Baked ********************/
bool VideoInfo::IsColorSpace(int c_space) const {
  return IsPlanar() ? ((pixel_type & CS_PLANAR_MASK) == (c_space & CS_PLANAR_FILTER)) : ((pixel_type & c_space) == c_space);
}

bool VideoInfo::Is(int property) const { return ((image_type & property)==property ); }
bool VideoInfo::IsPlanar() const { return !!(pixel_type & CS_PLANAR); }
bool VideoInfo::IsFieldBased() const { return !!(image_type & IT_FIELDBASED); }
bool VideoInfo::IsParityKnown() const { return ((image_type & IT_FIELDBASED)&&(image_type & (IT_BFF|IT_TFF))); }
bool VideoInfo::IsBFF() const { return !!(image_type & IT_BFF); }
bool VideoInfo::IsTFF() const { return !!(image_type & IT_TFF); }

/* Baked ********************
bool VideoInfo::IsVPlaneFirst() const {return ((pixel_type & CS_YV12) == CS_YV12); }  // Don't use this
int VideoInfo::BytesFromPixels(int pixels) const { return pixels * (BitsPerPixel()>>3); }   // Will not work on planar images, but will return only luma planes
int VideoInfo::RowSize() const { return BytesFromPixels(width); }  // Also only returns first plane on planar images
int VideoInfo::BMPSize() const { if (IsPlanar()) {int p = height * ((RowSize()+3) & ~3); p+=p>>1; return p;  } return height * ((RowSize()+3) & ~3); }
__int64 VideoInfo::AudioSamplesFromFrames(__int64 frames) const { return (fps_numerator && HasVideo()) ? ((__int64)(frames) * audio_samples_per_second * fps_denominator / fps_numerator) : 0; }
   Baked ********************/
__int64 VideoInfo::AudioSamplesFromFrames(int frames) const { return (fps_numerator && HasVideo()) ? ((__int64)(frames) * audio_samples_per_second * fps_denominator / fps_numerator) : 0; }
int VideoInfo::FramesFromAudioSamples(__int64 samples) const { return (fps_denominator && HasAudio()) ? (int)((samples * fps_numerator)/((__int64)fps_denominator * audio_samples_per_second)) : 0; }
__int64 VideoInfo::AudioSamplesFromBytes(__int64 bytes) const { return HasAudio() ? bytes / BytesPerAudioSample() : 0; }
__int64 VideoInfo::BytesFromAudioSamples(__int64 samples) const { return samples * BytesPerAudioSample(); }
int VideoInfo::AudioChannels() const { return HasAudio() ? nchannels : 0; }
int VideoInfo::SampleType() const{ return sample_type;}
bool VideoInfo::IsSampleType(int testtype) const{ return !!(sample_type&testtype);}
int VideoInfo::SamplesPerSecond() const { return audio_samples_per_second; }
int VideoInfo::BytesPerAudioSample() const { return nchannels*BytesPerChannelSample();}
void VideoInfo::SetFieldBased(bool isfieldbased)  { if (isfieldbased) image_type|=IT_FIELDBASED; else  image_type&=~IT_FIELDBASED; }
void VideoInfo::Set(int property)  { image_type|=property; }
void VideoInfo::Clear(int property)  { image_type&=~property; }

/* Baked ********************
int VideoInfo::BitsPerPixel() const {
  switch (pixel_type) {
    case CS_BGR24:
      return 24;
    case CS_BGR32:
      return 32;
    case CS_YUY2:
      return 16;
    case CS_YV12:
    case CS_I420:
      return 12;
    default:
      return 0;
  }
}
   Baked ********************/

int VideoInfo::BytesPerChannelSample() const {
  switch (sample_type) {
  case SAMPLE_INT8:
    return sizeof(unsigned char);
  case SAMPLE_INT16:
    return sizeof(signed short);
  case SAMPLE_INT24:
    return 3;
  case SAMPLE_INT32:
    return sizeof(signed int);
  case SAMPLE_FLOAT:
    return sizeof(SFLOAT);
  default:
    _ASSERTE("Sample type not recognized!");
    return 0;
  }
}

bool VideoInfo::IsVPlaneFirst() const {
  return !IsY8() && IsPlanar() && (pixel_type & (CS_VPlaneFirst | CS_UPlaneFirst)) == CS_VPlaneFirst;   // Shouldn't use this
}

int VideoInfo::BytesFromPixels(int pixels) const {
  return !IsY8() && IsPlanar() ? pixels << ((pixel_type>>CS_Shift_Sample_Bits) & 3) : pixels * (BitsPerPixel()>>3);   // For planar images, will return luma plane
}

int VideoInfo::RowSize(int plane) const {
  const int rowsize = BytesFromPixels(width);

  switch (plane) {
    case PLANAR_U: case PLANAR_V:
      return (!IsY8() && IsPlanar()) ? rowsize>>GetPlaneWidthSubsampling(plane) : 0;

    case PLANAR_U_ALIGNED: case PLANAR_V_ALIGNED:
      return (!IsY8() && IsPlanar()) ? ((rowsize>>GetPlaneWidthSubsampling(plane))+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)) : 0; // Aligned rowsize

    case PLANAR_Y_ALIGNED:
      return (rowsize+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)); // Aligned rowsize
  }
  return rowsize;
}

int VideoInfo::BMPSize() const {
  if (!IsY8() && IsPlanar()) {
    // Y plane
    const int Ybytes  = ((RowSize(PLANAR_Y)+3) & ~3) * height;
    const int UVbytes = Ybytes >> (GetPlaneWidthSubsampling(PLANAR_U)+GetPlaneHeightSubsampling(PLANAR_U));
    return Ybytes + UVbytes*2;
  }
  return height * ((RowSize()+3) & ~3);
}

int VideoInfo::GetPlaneWidthSubsampling(int plane) const {  // Subsampling in bitshifts!
  if (plane == PLANAR_Y)  // No subsampling
    return 0;
  if (IsY8())
    throw AvisynthError("Filter error: GetPlaneWidthSubsampling not available on Y8 pixel type.");
  if (plane == PLANAR_U || plane == PLANAR_V) {
    if (IsYUY2())
      return 1;
    else if (IsPlanar())
      return ((pixel_type>>CS_Shift_Sub_Width)+1) & 3;
    else
      throw AvisynthError("Filter error: GetPlaneWidthSubsampling called with unsupported pixel type.");
  }
  throw AvisynthError("Filter error: GetPlaneWidthSubsampling called with unsupported plane.");
}

int VideoInfo::GetPlaneHeightSubsampling(int plane) const {  // Subsampling in bitshifts!
  if (plane == PLANAR_Y)  // No subsampling
    return 0;
  if (IsY8())
    throw AvisynthError("Filter error: GetPlaneHeightSubsampling not available on Y8 pixel type.");
  if (plane == PLANAR_U || plane == PLANAR_V) {
    if (IsYUY2())
      return 0;
    else if (IsPlanar())
      return ((pixel_type>>CS_Shift_Sub_Height)+1) & 3;
    else
      throw AvisynthError("Filter error: GetPlaneHeightSubsampling called with unsupported pixel type.");
  }
  throw AvisynthError("Filter error: GetPlaneHeightSubsampling called with supported plane.");
}

int VideoInfo::BitsPerPixel() const {
// Lookup Interleaved, calculate PLANAR's
    switch (pixel_type) {
      case CS_BGR24:
        return 24;
      case CS_BGR32:
        return 32;
      case CS_YUY2:
        return 16;
      case CS_Y8:
        return 8;
//    case CS_Y16:
//      return 16;
//    case CS_Y32:
//      return 32;
    }
    if (IsPlanar()) {
      const int S = IsYUV() ? GetPlaneWidthSubsampling(PLANAR_U) + GetPlaneHeightSubsampling(PLANAR_U) : 0;
      return ( ((1<<S)+2) * (8<<((pixel_type>>CS_Shift_Sample_Bits) & 3)) ) >> S;
    }
    return 0;
}

// useful mutator
void VideoInfo::SetFPS(unsigned numerator, unsigned denominator) {
  if ((numerator == 0) || (denominator == 0)) {
    fps_numerator = 0;
    fps_denominator = 1;
  }
  else {
    unsigned x=numerator, y=denominator;
    while (y) {   // find gcd
      unsigned t = x%y; x = y; y = t;
    }
    fps_numerator = numerator/x;
    fps_denominator = denominator/x;
  }
}

// Range protected multiply-divide of FPS
void VideoInfo::MulDivFPS(unsigned multiplier, unsigned divisor) {
  unsigned __int64 numerator   = UInt32x32To64(fps_numerator,   multiplier);
  unsigned __int64 denominator = UInt32x32To64(fps_denominator, divisor);

  unsigned __int64 x=numerator, y=denominator;
  while (y) {   // find gcd
    unsigned __int64 t = x%y; x = y; y = t;
  }
  numerator   /= x; // normalize
  denominator /= x;

  unsigned __int64 temp = numerator | denominator; // Just looking top bit
  unsigned u = 0;
  while (temp & 0xffffffff80000000) { // or perhaps > 16777216*2
    temp = Int64ShrlMod32(temp, 1);
    u++;
  }
  if (u) { // Scale to fit
    const unsigned round = 1 << (u-1);
    SetFPS( (unsigned)Int64ShrlMod32(numerator   + round, u),
            (unsigned)Int64ShrlMod32(denominator + round, u) );
  }
  else {
    fps_numerator   = (unsigned)numerator;
    fps_denominator = (unsigned)denominator;
  }
}

// Test for same colorspace
bool VideoInfo::IsSameColorspace(const VideoInfo& vi) const {
  if (vi.pixel_type == pixel_type) return TRUE;
  if (IsYV12() && vi.IsYV12()) return TRUE;
  return FALSE;
}

// end struct VideoInfo

/**********************************************************************/

// class VideoFrameBuffer

const BYTE* VideoFrameBuffer::GetReadPtr() const { return data; }
/* Baked ********************
BYTE* VideoFrameBuffer::GetWritePtr() { ++sequence_number; return data; }
   Baked ********************/
BYTE* VideoFrameBuffer::GetWritePtr() { InterlockedIncrement(&sequence_number); return data; }
size_t VideoFrameBuffer::GetDataSize() const { return data_size; }
int VideoFrameBuffer::GetSequenceNumber() const { return sequence_number; }
int VideoFrameBuffer::GetRefcount() const { return refcount; }

// end class VideoFrameBuffer

/**********************************************************************/

// class VideoFrame

/* Baked ********************
void VideoFrame::AddRef() { InterlockedIncrement((long *)&refcount); }
void VideoFrame::Release() { if (refcount==1) InterlockedDecrement(&vfb->refcount); InterlockedDecrement((long *)&refcount); }

int VideoFrame::GetPitch() const { return pitch; }
int VideoFrame::GetPitch(int plane) const { switch (plane) {case PLANAR_U: case PLANAR_V: return pitchUV;} return pitch; }
int VideoFrame::GetRowSize() const { return row_size; }

int VideoFrame::GetRowSize(int plane) const {
  switch (plane) {
  case PLANAR_U: case PLANAR_V: if (pitchUV) return row_size>>1; else return 0;
  case PLANAR_U_ALIGNED: case PLANAR_V_ALIGNED:
    if (pitchUV) {
      int r = ((row_size+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)) )>>1; // Aligned rowsize
      if (r<=pitchUV)
        return r;
      return row_size>>1;
    } else return 0;
  case PLANAR_Y_ALIGNED:
    int r = (row_size+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)); // Aligned rowsize
    if (r<=pitch)
      return r;
    return row_size;
  }
  return row_size; }

int VideoFrame::GetHeight() const { return height; }
int VideoFrame::GetHeight(int plane) const {  switch (plane) {case PLANAR_U: case PLANAR_V: if (pitchUV) return height>>1; return 0;} return height; }

// generally you shouldn't use these three
VideoFrameBuffer* VideoFrame::GetFrameBuffer() const { return vfb; }
int VideoFrame::GetOffset() const { return offset; }
int VideoFrame::GetOffset(int plane) const { switch (plane) {case PLANAR_U: return offsetU;case PLANAR_V: return offsetV;default: return offset;}; }

const BYTE* VideoFrame::GetReadPtr() const { return vfb->GetReadPtr() + offset; }
const BYTE* VideoFrame::GetReadPtr(int plane) const { return vfb->GetReadPtr() + GetOffset(plane); }

bool VideoFrame::IsWritable() const { return (refcount == 1 && vfb->refcount == 1); }

BYTE* VideoFrame::GetWritePtr() const {
  if (vfb->GetRefcount()>1) {
    _ASSERT(FALSE);
    //throw AvisynthError("Internal Error - refcount was more than one!");
  }
  return IsWritable() ? (vfb->GetWritePtr() + offset) : 0;
}
   Baked ********************/

void VideoFrame::AddRef() { InterlockedIncrement(&refcount); }
void VideoFrame::Release() {
  VideoFrameBuffer* _vfb = vfb;

  if (!InterlockedDecrement(&refcount))
    InterlockedDecrement(&_vfb->refcount);
}

int VideoFrame::GetPitch(int plane) const { switch (plane) {case PLANAR_U: case PLANAR_V: return pitchUV;} return pitch; }

int VideoFrame::GetRowSize(int plane) const {
  switch (plane) {
  case PLANAR_U: case PLANAR_V: if (pitchUV) return row_sizeUV; else return 0;
  case PLANAR_U_ALIGNED: case PLANAR_V_ALIGNED:
    if (pitchUV) {
      const int r = (row_sizeUV+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)); // Aligned rowsize
      if (r<=pitchUV)
        return r;
      return row_sizeUV;
    }
    else return 0;
  case PLANAR_ALIGNED: case PLANAR_Y_ALIGNED:
    const int r = (row_size+FRAME_ALIGN-1)&(~(FRAME_ALIGN-1)); // Aligned rowsize
    if (r<=pitch)
      return r;
    return row_size;
  }
  return row_size; }

int VideoFrame::GetHeight(int plane) const {  switch (plane) {case PLANAR_U: case PLANAR_V: if (pitchUV) return heightUV; return 0;} return height; }

// Generally you should not be using these two
VideoFrameBuffer* VideoFrame::GetFrameBuffer() const { return vfb; }
size_t VideoFrame::GetOffset(int plane) const { switch (plane) {case PLANAR_U: return offsetU;case PLANAR_V: return offsetV;default: return offset;}; }

const BYTE* VideoFrame::GetReadPtr(int plane) const { return vfb->GetReadPtr() + GetOffset(plane); }

bool VideoFrame::IsWritable() const {
  if (refcount == 1 && vfb->refcount == 1) {
    vfb->GetWritePtr(); // Bump sequence number
    return true;
  }
  return false;
}

BYTE* VideoFrame::GetWritePtr(int plane) const {
  if (!plane || plane == PLANAR_Y) {
    if (vfb->GetRefcount()>1) {
      _ASSERT(FALSE);
//        throw AvisynthError("Internal Error - refcount was more than one!");
    }
    return (refcount == 1 && vfb->refcount == 1) ? vfb->GetWritePtr() + GetOffset(plane) : 0;
  }
  return vfb->data + GetOffset(plane);
}

/* Baked ********************
VideoFrame::~VideoFrame() { InterlockedDecrement(&vfb->refcount); }
   Baked ********************/
VideoFrame::~VideoFrame()     { DESTRUCTOR(); }
void VideoFrame::DESTRUCTOR() { delete vfb; /*is this right? called Release() before*/ }

// end class VideoFrame

/**********************************************************************/

// class IClip

/* Baked ********************
  void IClip::AddRef() { InterlockedIncrement((long *)&refcnt); }
  void IClip::Release() { InterlockedDecrement((long *)&refcnt); if (!refcnt) delete this; }
   Baked ********************/
void IClip::AddRef() { InterlockedIncrement(&refcnt); }
void IClip::Release() { if (!InterlockedDecrement(&refcnt)) delete this; }

// end class IClip

/**********************************************************************/

// class PClip

IClip* PClip::GetPointerWithAddRef() const { if (p) p->AddRef(); return p; }

void PClip::Init(IClip* x) {
  if (x) x->AddRef();
  p=x;
}

void PClip::Set(IClip* x) {
  if (x) x->AddRef();
  if (p) p->Release();
  p=x;
}

PClip::PClip()                               { CONSTRUCTOR0(); }
void PClip::CONSTRUCTOR0()                   { p = 0; }

PClip::PClip(const PClip& x)                 { CONSTRUCTOR1(x); }
void PClip::CONSTRUCTOR1(const PClip& x)     { Init(x.p); }

PClip::PClip(IClip* x)                       { CONSTRUCTOR2(x); }
void PClip::CONSTRUCTOR2(IClip* x)           { Init(x); }

void PClip::operator=(IClip* x)              { OPERATOR_ASSIGN0(x); }
void PClip::OPERATOR_ASSIGN0(IClip* x)       { Set(x); }

void PClip::operator=(const PClip& x)        { OPERATOR_ASSIGN1(x); }
void PClip::OPERATOR_ASSIGN1(const PClip& x) { Set(x.p); }

PClip::~PClip()                              { DESTRUCTOR(); }
void PClip::DESTRUCTOR()                     { if (p) p->Release(); }

// end class PClip

/**********************************************************************/

// class PVideoFrame

void PVideoFrame::Init(VideoFrame* x) {
  if (x) x->AddRef();
  p=x;
}

void PVideoFrame::Set(VideoFrame* x) {
  if (x) x->AddRef();
  if (p) p->Release();
  p=x;
}

PVideoFrame::PVideoFrame()                               { CONSTRUCTOR0(); }
void PVideoFrame::CONSTRUCTOR0()                         { p = 0; }
                                                        
PVideoFrame::PVideoFrame(const PVideoFrame& x)           { CONSTRUCTOR1(x); }
void PVideoFrame::CONSTRUCTOR1(const PVideoFrame& x)     { Init(x.p); }
                                                        
PVideoFrame::PVideoFrame(VideoFrame* x)                  { CONSTRUCTOR2(x); }
void PVideoFrame::CONSTRUCTOR2(VideoFrame* x)            { Init(x); }
                                                        
void PVideoFrame::operator=(VideoFrame* x)               { OPERATOR_ASSIGN0(x); }
void PVideoFrame::OPERATOR_ASSIGN0(VideoFrame* x)        { Set(x); }

void PVideoFrame::operator=(const PVideoFrame& x)        { OPERATOR_ASSIGN1(x); }
void PVideoFrame::OPERATOR_ASSIGN1(const PVideoFrame& x) { Set(x.p); }

PVideoFrame::~PVideoFrame()                              { DESTRUCTOR(); }
void PVideoFrame::DESTRUCTOR()                           { if (p) p->Release(); }


// end class PVideoFrame

/**********************************************************************/

// class AVSValue

AVSValue::AVSValue()                                     { CONSTRUCTOR0(); }
void AVSValue::CONSTRUCTOR0()                            { type = 'v'; }
                                                        
AVSValue::AVSValue(IClip* c)                             { CONSTRUCTOR1(c); }
void AVSValue::CONSTRUCTOR1(IClip* c)                    { type = 'c'; clip = c; if (c) c->AddRef(); }
                                                        
AVSValue::AVSValue(const PClip& c)                       { CONSTRUCTOR2(c); }
void AVSValue::CONSTRUCTOR2(const PClip& c)              { type = 'c'; clip = c.GetPointerWithAddRef(); }
                                                        
AVSValue::AVSValue(bool b)                               { CONSTRUCTOR3(b); }
void AVSValue::CONSTRUCTOR3(bool b)                      { type = 'b'; boolean = b; }
                                                        
AVSValue::AVSValue(int i)                                { CONSTRUCTOR4(i); }
void AVSValue::CONSTRUCTOR4(int i)                       { type = 'i'; integer = i; }
                                                        
AVSValue::AVSValue(float f)                              { CONSTRUCTOR5(f); }
void AVSValue::CONSTRUCTOR5(float f)                     { type = 'f'; floating_pt = f; }
                                                        
AVSValue::AVSValue(double f)                             { CONSTRUCTOR6(f); }
void AVSValue::CONSTRUCTOR6(double f)                    { type = 'f'; floating_pt = float(f); }
                                                        
AVSValue::AVSValue(const char* s)                        { CONSTRUCTOR7(s); }
void AVSValue::CONSTRUCTOR7(const char* s)               { type = 's'; string = s; }

/* Baked ********************
AVSValue::AVSValue(const AVSValue* a, int size) { type = 'a'; array = a; array_size = size; }
   Baked ********************/
AVSValue::AVSValue(const AVSValue* a, int size)          { CONSTRUCTOR8(a, size); }
void AVSValue::CONSTRUCTOR8(const AVSValue* a, int size) { type = 'a'; array = a; array_size = (short)size; }

AVSValue::AVSValue(const AVSValue& v)                    { CONSTRUCTOR9(v); }
void AVSValue::CONSTRUCTOR9(const AVSValue& v)           { Assign(&v, true); }
                                                        
AVSValue::~AVSValue()                                    { DESTRUCTOR(); }
void AVSValue::DESTRUCTOR()                              { if (IsClip() && clip) clip->Release(); }

AVSValue& AVSValue::operator=(const AVSValue& v)         { return OPERATOR_ASSIGN(v); }
AVSValue& AVSValue::OPERATOR_ASSIGN(const AVSValue& v)   { Assign(&v, false); return *this; }

// Note that we transparently allow 'int' to be treated as 'float'.
// There are no int<->bool conversions, though.

bool AVSValue::Defined() const { return type != 'v'; }
bool AVSValue::IsClip() const { return type == 'c'; }
bool AVSValue::IsBool() const { return type == 'b'; }
bool AVSValue::IsInt() const { return type == 'i'; }
//  bool IsLong() const { return (type == 'l'|| type == 'i'); }
bool AVSValue::IsFloat() const { return type == 'f' || type == 'i'; }
bool AVSValue::IsString() const { return type == 's'; }
bool AVSValue::IsArray() const { return type == 'a'; }

PClip AVSValue::AsClip() const { _ASSERTE(IsClip()); return IsClip()?clip:0; }

bool AVSValue::AsBool1() const { _ASSERTE(IsBool()); return boolean; }
bool AVSValue::AsBool() const { return AsBool1(); }

int AVSValue::AsInt1() const { _ASSERTE(IsInt()); return integer; }
int AVSValue::AsInt() const { return AsInt1(); }
//  int AsLong() const { _ASSERTE(IsLong()); return IsInt()?integer:longlong; }

const char* AVSValue::AsString1() const { _ASSERTE(IsString()); return IsString()?string:0; }
const char* AVSValue::AsString() const { return AVSValue::AsString1(); }
/* Baked ********************
double AVSValue::AsFloat() const { _ASSERTE(IsFloat()); return IsInt()?integer:floating_pt; }
   Baked ********************/

double AVSValue::AsFloat1() const { _ASSERTE(IsFloat()); return IsInt()?integer:floating_pt; }
double AVSValue::AsFloat() const { return AsFloat1(); }


bool AVSValue::AsBool2(bool def) const { _ASSERTE(IsBool()||!Defined()); return IsBool() ? boolean : def; }
bool AVSValue::AsBool(bool def) const { return AsBool2(def); }

int AVSValue::AsInt2(int def) const { _ASSERTE(IsInt()||!Defined()); return IsInt() ? integer : def; }
int AVSValue::AsInt(int def) const { return AsInt2(def); }
/* Baked ********************
double AVSValue::AsFloat(double def) const { _ASSERTE(IsFloat()||!Defined()); return IsInt() ? integer : type=='f' ? floating_pt : def; }
   Baked ********************/
double AVSValue::AsDblDef(double def) const { _ASSERTE(IsFloat()||!Defined()); return IsInt() ? integer : type=='f' ? floating_pt : def; }
//float  AVSValue::AsFloat(double def) const { _ASSERTE(IsFloat()||!Defined()); return IsInt() ? integer : type=='f' ? floating_pt : (float)def; }

double AVSValue::AsFloat2(float def) const { _ASSERTE(IsFloat()||!Defined()); return IsInt() ? integer : type=='f' ? floating_pt : def; }
double AVSValue::AsFloat(float def) const { return AsFloat2(def); }

const char* AVSValue::AsString2(const char* def) const { _ASSERTE(IsString()||!Defined()); return IsString() ? string : def; }
const char* AVSValue::AsString(const char* def) const { return AVSValue::AsString2(def); }

int AVSValue::ArraySize() const { _ASSERTE(IsArray()); return IsArray()?array_size:1; }

const AVSValue& AVSValue::operator[](int index) const     { return OPERATOR_INDEX(index); }
const AVSValue& AVSValue::OPERATOR_INDEX(int index) const {
  _ASSERTE(IsArray() && index>=0 && index<array_size);
  return (IsArray() && index>=0 && index<array_size) ? array[index] : *this;
}

void AVSValue::Assign(const AVSValue* src, bool init) {
  if (src->IsClip() && src->clip)
    src->clip->AddRef();
  if (!init && IsClip() && clip)
    clip->Release();
  // make sure this copies the whole struct!
  array_size = src->array_size;
  type = src->type;
  string = src->string;
}

// end class AVSValue

/**********************************************************************/

static const AVS_Linkage avs_linkage = {    // struct AVS_Linkage {

  sizeof(AVS_Linkage),                      //   size_t Size;

/***************************************************************************************************************/
// struct VideoInfo
  &VideoInfo::HasVideo,                     //   bool    (VideoInfo::*HasVideo)() const;
  &VideoInfo::HasAudio,                     //   bool    (VideoInfo::*HasAudio)() const;
  &VideoInfo::IsRGB,                        //   bool    (VideoInfo::*IsRGB)() const;
  &VideoInfo::IsRGB24,                      //   bool    (VideoInfo::*IsRGB24)() const;
  &VideoInfo::IsRGB32,                      //   bool    (VideoInfo::*IsRGB32)() const;
  &VideoInfo::IsYUV,                        //   bool    (VideoInfo::*IsYUV)() const;
  &VideoInfo::IsYUY2,                       //   bool    (VideoInfo::*IsYUY2)() const;
  &VideoInfo::IsYV24,                       //   bool    (VideoInfo::*IsYV24)()  const;
  &VideoInfo::IsYV16,                       //   bool    (VideoInfo::*IsYV16)()  const;
  &VideoInfo::IsYV12,                       //   bool    (VideoInfo::*IsYV12)()  const;
  &VideoInfo::IsYV411,                      //   bool    (VideoInfo::*IsYV411)() const;
  &VideoInfo::IsY8,                         //   bool    (VideoInfo::*IsY8)()    const;
  &VideoInfo::IsColorSpace,                 //   bool    (VideoInfo::*IsColorSpace)(int c_space) const;
  &VideoInfo::Is,                           //   bool    (VideoInfo::*Is)(int property) const;
  &VideoInfo::IsPlanar,                     //   bool    (VideoInfo::*IsPlanar)() const;
  &VideoInfo::IsFieldBased,                 //   bool    (VideoInfo::*IsFieldBased)() const;
  &VideoInfo::IsParityKnown,                //   bool    (VideoInfo::*IsParityKnown)() const;
  &VideoInfo::IsBFF,                        //   bool    (VideoInfo::*IsBFF)() const;
  &VideoInfo::IsTFF,                        //   bool    (VideoInfo::*IsTFF)() const;
  &VideoInfo::IsVPlaneFirst,                //   bool    (VideoInfo::*IsVPlaneFirst)() const;
  &VideoInfo::BytesFromPixels,              //   int     (VideoInfo::*BytesFromPixels)(int pixels) const;
  &VideoInfo::RowSize,                      //   int     (VideoInfo::*RowSize)(int plane) const;
  &VideoInfo::BMPSize,                      //   int     (VideoInfo::*BMPSize)() const;
  &VideoInfo::AudioSamplesFromFrames,       //   __int64 (VideoInfo::*AudioSamplesFromFrames)(int frames) const;
  &VideoInfo::FramesFromAudioSamples,       //   int     (VideoInfo::*FramesFromAudioSamples)(__int64 samples) const;
  &VideoInfo::AudioSamplesFromBytes,        //   __int64 (VideoInfo::*AudioSamplesFromBytes)(__int64 bytes) const;
  &VideoInfo::BytesFromAudioSamples,        //   __int64 (VideoInfo::*BytesFromAudioSamples)(__int64 samples) const;
  &VideoInfo::AudioChannels,                //   int     (VideoInfo::*AudioChannels)() const;
  &VideoInfo::SampleType,                   //   int     (VideoInfo::*SampleType)() const;
  &VideoInfo::IsSampleType,                 //   bool    (VideoInfo::*IsSampleType)(int testtype) const;
  &VideoInfo::SamplesPerSecond,             //   int     (VideoInfo::*SamplesPerSecond)() const;
  &VideoInfo::BytesPerAudioSample,          //   int     (VideoInfo::*BytesPerAudioSample)() const;
  &VideoInfo::SetFieldBased,                //   void    (VideoInfo::*SetFieldBased)(bool isfieldbased);
  &VideoInfo::Set,                          //   void    (VideoInfo::*Set)(int property);
  &VideoInfo::Clear,                        //   void    (VideoInfo::*Clear)(int property);
  &VideoInfo::GetPlaneWidthSubsampling,     //   int     (VideoInfo::*GetPlaneWidthSubsampling)(int plane) const;
  &VideoInfo::GetPlaneHeightSubsampling,    //   int     (VideoInfo::*GetPlaneHeightSubsampling)(int plane) const;
  &VideoInfo::BitsPerPixel,                 //   int     (VideoInfo::*BitsPerPixel)() const;
  &VideoInfo::BytesPerChannelSample,        //   int     (VideoInfo::*BytesPerChannelSample)() const;
  &VideoInfo::SetFPS,                       //   void    (VideoInfo::*SetFPS)(unsigned numerator, unsigned denominator)
  &VideoInfo::MulDivFPS,                    //   void    (VideoInfo::*MulDivFPS)(unsigned multiplier, unsigned divisor)
  &VideoInfo::IsSameColorspace,             //   bool    (VideoInfo::*IsSameColorspace)(const VideoInfo& vi) const;
// end struct VideoInfo
/***************************************************************************************************************/
// class VideoFrameBuffer
  &VideoFrameBuffer::GetReadPtr,            //   const BYTE* (VideoFrameBuffer::*VFBGetReadPtr)() const;
  &VideoFrameBuffer::GetWritePtr,           //   BYTE*       (VideoFrameBuffer::*VFBGetWritePtr)();
  &VideoFrameBuffer::GetDataSize,           //   size_t      (VideoFrameBuffer::*GetDataSize)() const;
  &VideoFrameBuffer::GetSequenceNumber,     //   int         (VideoFrameBuffer::*GetSequenceNumber)() const;
  &VideoFrameBuffer::GetRefcount,           //   int         (VideoFrameBuffer::*GetRefcount)() const;
// end class VideoFrameBuffer
/***************************************************************************************************************/
// class VideoFrame
  &VideoFrame::GetPitch,                    //   int               (VideoFrame::*GetPitch)(int plane) const;
  &VideoFrame::GetRowSize,                  //   int               (VideoFrame::*GetRowSize)(int plane) const;
  &VideoFrame::GetHeight,                   //   int               (VideoFrame::*GetHeight)(int plane) const;
  &VideoFrame::GetFrameBuffer,              //   VideoFrameBuffer* (VideoFrame::*GetFrameBuffer)() const;
  &VideoFrame::GetOffset,                   //   size_t            (VideoFrame::*GetOffset)(int plane) const;
  &VideoFrame::GetReadPtr,                  //   const BYTE*       (VideoFrame::*VFGetReadPtr)(int plane) const;
  &VideoFrame::IsWritable,                  //   bool              (VideoFrame::*IsWritable)() const;
  &VideoFrame::GetWritePtr,                 //   BYTE*             (VideoFrame::*VFGetWritePtr)(int plane) const;
  &VideoFrame::DESTRUCTOR,                  //   void              (VideoFrame::*VideoFrame_DESTRUCTOR)();
// end class VideoFrame
/***************************************************************************************************************/
// class IClip
                                            //   /* nothing */
// end class IClip
/***************************************************************************************************************/
// class PClip
  &PClip::CONSTRUCTOR0,                     //   void (PClip::*PClip_CONSTRUCTOR0)();
  &PClip::CONSTRUCTOR1,                     //   void (PClip::*PClip_CONSTRUCTOR1)(const PClip& x);
  &PClip::CONSTRUCTOR2,                     //   void (PClip::*PClip_CONSTRUCTOR2)(IClip* x);
  &PClip::OPERATOR_ASSIGN0,                 //   void (PClip::*PClip_OPERATOR_ASSIGN0)(IClip* x);
  &PClip::OPERATOR_ASSIGN1,                 //   void (PClip::*PClip_OPERATOR_ASSIGN1)(const PClip& x);
  &PClip::DESTRUCTOR,                       //   void (PClip::*PClip_DESTRUCTOR)();
// end class PClip
/***************************************************************************************************************/
// class PVideoFrame
  &PVideoFrame::CONSTRUCTOR0,               //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR0)();
  &PVideoFrame::CONSTRUCTOR1,               //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR1)(const PVideoFrame& x);
  &PVideoFrame::CONSTRUCTOR2,               //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR2)(VideoFrame* x);
  &PVideoFrame::OPERATOR_ASSIGN0,           //   void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN0(VideoFrame* x);
  &PVideoFrame::OPERATOR_ASSIGN1,           //   void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN1(const PVideoFrame& x);
  &PVideoFrame::DESTRUCTOR,                 //   void (PVideoFrame::*PVideoFrame_DESTRUCTOR)();
// end class PVideoFrame
/***************************************************************************************************************/
// class AVSValue
  &AVSValue::CONSTRUCTOR0,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR0)();
  &AVSValue::CONSTRUCTOR1,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR1)(IClip* c);
  &AVSValue::CONSTRUCTOR2,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR2)(const PClip& c);
  &AVSValue::CONSTRUCTOR3,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR3)(bool b);
  &AVSValue::CONSTRUCTOR4,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR4)(int i);
  &AVSValue::CONSTRUCTOR5,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR5)(float f);
  &AVSValue::CONSTRUCTOR6,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR6)(double f);
  &AVSValue::CONSTRUCTOR7,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR7)(const char* s);
  &AVSValue::CONSTRUCTOR8,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR8)(const AVSValue* a, int
  &AVSValue::CONSTRUCTOR9,                  //   void            (AVSValue::*AVSValue_CONSTRUCTOR9)(const AVSValue& v);
  &AVSValue::DESTRUCTOR,                    //   void            (AVSValue::*AVSValue_DESTRUCTOR)();
  &AVSValue::OPERATOR_ASSIGN,               //   AVSValue&       (AVSValue::*AVSValue_OPERATOR_ASSIGN)(const AVSValue& v);
  &AVSValue::OPERATOR_INDEX,                //   const AVSValue& (AVSValue::*AVSValue_OPERATOR_INDEX)(int index) const;
  &AVSValue::Defined,                       //   bool            (AVSValue::*Defined)() const;
  &AVSValue::IsClip,                        //   bool            (AVSValue::*IsClip)() const;
  &AVSValue::IsBool,                        //   bool            (AVSValue::*IsBool)() const;
  &AVSValue::IsInt,                         //   bool            (AVSValue::*IsInt)() const;
  &AVSValue::IsFloat,                       //   bool            (AVSValue::*IsFloat)() const;
  &AVSValue::IsString,                      //   bool            (AVSValue::*IsString)() const;
  &AVSValue::IsArray,                       //   bool            (AVSValue::*IsArray)() const;
  &AVSValue::AsClip,                        //   PClip           (AVSValue::*AsClip)() const;
  &AVSValue::AsBool1,                       //   bool            (AVSValue::*AsBool1)() const;
  &AVSValue::AsInt1,                        //   int             (AVSValue::*AsInt1)() const;
  &AVSValue::AsString1,                     //   const char*     (AVSValue::*AsString1)() const;
  &AVSValue::AsFloat1,                      //   double          (AVSValue::*AsFloat1)() const;
  &AVSValue::AsBool2,                       //   bool            (AVSValue::*AsBool2)(bool def) const;
  &AVSValue::AsInt2,                        //   int             (AVSValue::*AsInt2)(int def) const;
  &AVSValue::AsDblDef,                      //   double          (AVSValue::*AsDblDef)(double def) const;
  &AVSValue::AsFloat2,                      //   double          (AVSValue::*AsFloat2)(float def) const;
  &AVSValue::AsString2,                     //   const char*     (AVSValue::*AsString2)(const char* def) const;
  &AVSValue::ArraySize,                     //   int             (AVSValue::*ArraySize)() const;
// end class AVSValue
};                                          // }

/* extern __declspec(dllexport) */ const AVS_Linkage* const AVS_linkage = &avs_linkage;


/**********************************************************************/
// in UserPlugin.cpp
#if 0

#include "avisynth.h"

/* New 2.6 requirment!!! */
// Declare and initialise server pointers static storage.
const AVS_Linkage *AVS_linkage = 0;

/* New 2.6 requirment!!! */
// DLL entry point called from LoadPlugin() to setup a user plugin.
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {

  /* New 2.6 requirment!!! */
  // Save the server pointers.
  AVS_linkage = vectors;

  // Add the name of our function
  env->AddFunction("Plugin", "c", Create_Plugin, 0);

  // Return plugin text identifier.
  return "Plugin";
}

#endif
/**********************************************************************/

