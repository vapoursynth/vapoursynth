/*
* Copyright (c) 2012-2013 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef AVISYNTH_WRAPPER_H
#define AVISYNTH_WRAPPER_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdint.h>
#include <assert.h>

namespace AvisynthCompat {

enum { AVISYNTH_INTERFACE_VERSION = 3 };

#pragma pack(push,8)

#define FRAME_ALIGN 32

enum {SAMPLE_INT8  = 1 << 0,
      SAMPLE_INT16 = 1 << 1,
      SAMPLE_INT24 = 1 << 2,  // Int24 is a very stupid thing to code, but it's supported by some hardware.
      SAMPLE_INT32 = 1 << 3,
      SAMPLE_FLOAT = 1 << 4
     };

enum {
    PLANAR_Y = 1 << 0,
    PLANAR_U = 1 << 1,
    PLANAR_V = 1 << 2,
    PLANAR_ALIGNED = 1 << 3,
    PLANAR_Y_ALIGNED = PLANAR_Y | PLANAR_ALIGNED,
    PLANAR_U_ALIGNED = PLANAR_U | PLANAR_ALIGNED,
    PLANAR_V_ALIGNED = PLANAR_V | PLANAR_ALIGNED
};

class AvisynthError { /* exception */
public:
    const char *const msg;
    AvisynthError(const char *_msg) : msg(_msg) {}
};

struct VideoInfo {
    int width, height;    // width=0 means no video
    unsigned fps_numerator, fps_denominator;
    int num_frames;
    // This is more extensible than previous versions. More properties can be added seeminglesly.

    // Colorspace properties.
    enum {
        CS_BGR = 1 << 28,
        CS_YUV = 1 << 29,
        CS_INTERLEAVED = 1 << 30,
        CS_PLANAR = 1 << 31
    };

    // Specific colorformats
    enum { CS_UNKNOWN = 0,
           CS_BGR24 = 1 << 0 | CS_BGR | CS_INTERLEAVED,
           CS_BGR32 = 1 << 1 | CS_BGR | CS_INTERLEAVED,
           CS_YUY2  = 1 << 2 | CS_YUV | CS_INTERLEAVED,
           CS_YV12  = 1 << 3 | CS_YUV | CS_PLANAR, // y-v-u, 4:2:0 planar
           CS_I420  = 1 << 4 | CS_YUV | CS_PLANAR, // y-u-v, 4:2:0 planar
           CS_IYUV  = 1 << 4 | CS_YUV | CS_PLANAR // same as above
         };
    int pixel_type;                // changed to int as of 2.5


    int audio_samples_per_second;   // 0 means no audio
    int sample_type;                // as of 2.5
    int64_t num_audio_samples;      // changed as of 2.5
    int nchannels;                  // as of 2.5

    // Imagetype properties

    int image_type;

    enum {
        IT_BFF = 1 << 0,
        IT_TFF = 1 << 1,
        IT_FIELDBASED = 1 << 2
    };

    // useful functions of the above
    bool HasVideo() const {
        return (width != 0);
    }
    bool HasAudio() const {
        return (audio_samples_per_second != 0);
    }
    bool IsRGB() const {
        return !!(pixel_type & CS_BGR);
    }
    bool IsRGB24() const {
        return (pixel_type & CS_BGR24) == CS_BGR24;    // Clear out additional properties
    }
    bool IsRGB32() const {
        return (pixel_type & CS_BGR32) == CS_BGR32 ;
    }
    bool IsYUV() const {
        return !!(pixel_type & CS_YUV);
    }
    bool IsYUY2() const {
        return (pixel_type & CS_YUY2) == CS_YUY2;
    }
    bool IsYV12() const {
        return ((pixel_type & CS_YV12) == CS_YV12) || ((pixel_type & CS_I420) == CS_I420);
    }
    bool IsColorSpace(int c_space) const {
        return ((pixel_type & c_space) == c_space);
    }
    bool Is(int property) const {
        return ((pixel_type & property) == property);
    }
    bool IsPlanar() const {
        return !!(pixel_type & CS_PLANAR);
    }
    bool IsFieldBased() const {
        return !!(image_type & IT_FIELDBASED);
    }
    bool IsParityKnown() const {
        return ((image_type & IT_FIELDBASED) && (image_type & (IT_BFF | IT_TFF)));
    }
    bool IsBFF() const {
        return !!(image_type & IT_BFF);
    }
    bool IsTFF() const {
        return !!(image_type & IT_TFF);
    }

    bool IsVPlaneFirst() const {
        return ((pixel_type & CS_YV12) == CS_YV12);    // Don't use this
    }
    int BytesFromPixels(int pixels) const {
        return pixels * (BitsPerPixel() >> 3);    // Will not work on planar images, but will return only luma planes
    }
    int RowSize() const {
        return BytesFromPixels(width);    // Also only returns first plane on planar images
    }
    int BMPSize() const {
        if (IsPlanar()) {
            int p = height * ((RowSize() + 3) & ~3);
            p += p >> 1;
            return p;
        }

        return height * ((RowSize() + 3) & ~3);
    }

    int SampleType() const {
        return sample_type;
    }
    bool IsSampleType(int testtype) const {
        return !!(sample_type & testtype);
    }

    void SetFieldBased(bool isfieldbased)  {
        if (isfieldbased) image_type |= IT_FIELDBASED;
        else  image_type &= ~IT_FIELDBASED;
    }
    void Set(int property)  {
        image_type |= property;
    }
    void Clear(int property)  {
        image_type &= ~property;
    }

    int BitsPerPixel() const {
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

    // useful mutator
    void SetFPS(unsigned numerator, unsigned denominator) {
        if ((numerator == 0) || (denominator == 0)) {
            fps_numerator = 0;
            fps_denominator = 1;
        } else {
            unsigned x = numerator, y = denominator;

            while (y) {   // find gcd
                unsigned t = x % y;
                x = y;
                y = t;
            }

            fps_numerator = numerator / x;
            fps_denominator = denominator / x;
        }
    }

    // Range protected multiply-divide of FPS
    void MulDivFPS(unsigned multiplier, unsigned divisor) {
        uint64_t numerator   = (uint64_t)fps_numerator * (uint64_t)multiplier;
        uint64_t denominator = (uint64_t)fps_denominator * (uint64_t)divisor;

        uint64_t x = numerator, y = denominator;

        while (y) {   // find gcd
            uint64_t t = x % y;
            x = y;
            y = t;
        }

        numerator   /= x; // normalize
        denominator /= x;

        uint64_t temp = numerator | denominator; // Just looking top bit
        unsigned u = 0;

        while (temp & 0xffffffff80000000ull) { // or perhaps > 16777216*2
            temp >>= 1;
            u++;
        }

        if (u) { // Scale to fit
            const unsigned round = 1 << (u - 1);
            SetFPS((unsigned)((numerator + round) >> u),
                (unsigned)((denominator + round) >> u));
        } else {
            fps_numerator   = (unsigned)numerator;
            fps_denominator = (unsigned)denominator;
        }
    }

    // Test for same colorspace
    bool IsSameColorspace(const VideoInfo &vi) const {
        if (vi.pixel_type == pixel_type) return TRUE;

        if (IsYV12() && vi.IsYV12()) return TRUE;

        return FALSE;
    }

};




// VideoFrameBuffer holds information about a memory block which is used
// for video data. Due to how the compatibility layer allocates frames
// the VideoFrame and VideoFrameBuffer refcounts are always the same so
// they are completely ignored. The sequence_number represents a concept
// that also doesn't exist because only deep copies exist.
// Finally, data_size has no use at all and it's a mystery why it's
// exposed to public filters. Fortunately no one ever calls it.

class VideoFrameBuffer {
    uint8_t *const data;
    const int data_size;
    long sequence_number;

    friend class VideoFrame;
    friend class Cache;
    friend class ScriptEnvironment;
    long refcount;

public:
    // modified constructor and destructor
    VideoFrameBuffer(uint8_t *data, bool writable) : data(data), data_size(0), sequence_number(0), refcount(writable ? 1 : 9000) {}
    ~VideoFrameBuffer() {}

    const uint8_t *GetReadPtr() const {
        return data;
    }
    uint8_t *GetWritePtr() {
        ++sequence_number;
        return data;
    }
    int GetDataSize() const {
        return data_size;
    }
    int GetSequenceNumber() const {
        return sequence_number;
    }
    int GetRefcount() const {
        return refcount;
    }
};


class IClip;
class PClip;
class PVideoFrame;
class IScriptEnvironment;
class AVSValue;


// VideoFrame holds a "window" into a VideoFrameBuffer. This is shit complicated just to optimize
// crop and separatefield. We don't like complicated stuff like overriding operator new. Keep
// good old references to this class alive so that our superior destructor is called instead
// of the shitty baked code.

class VideoFrame {
    long refcount;
    VideoFrameBuffer *const vfb;
    const int offset, pitch, row_size, height, offsetU, offsetV, pitchUV;  // U&V offsets are from top of picture.

    friend class PVideoFrame;
    void AddRef() {
        InterlockedIncrement(&refcount);
    }
    void Release() {
        if (refcount == 1) InterlockedDecrement(&vfb->refcount);

        InterlockedDecrement(&refcount);
    }

    friend class FakeAvisynth;
    friend class VSClip;


// modified constructors that create the underlying fake frame
    VideoFrame(uint8_t *_vfb, bool writable, int _offset, int _pitch, int _row_size, int _height,
               int _offsetU, int _offsetV, int _pitchUV)
        : refcount(0), vfb(new VideoFrameBuffer(_vfb, writable)), offset(_offset), pitch(_pitch), row_size(_row_size),
          height(_height), offsetU(_offsetU), offsetV(_offsetV), pitchUV(_pitchUV) {
    }

// there used to be an operator new override here, but it is no more
public:
    // Addition for refcount rape
    long GetRefCount() {
        return refcount;
    }

    int GetPitch() const {
        return pitch;
    }
    int GetPitch(int plane) const {
        switch (plane) {
        case PLANAR_U:
        case PLANAR_V:
            return pitchUV;
        }

        return pitch;
    }
    int GetRowSize() const {
        return row_size;
    }
    int GetRowSize(int plane) const {
        switch (plane) {
        case PLANAR_U:
        case PLANAR_V:

            if (pitchUV) return row_size >> 1;
            else return 0;

        case PLANAR_U_ALIGNED:
        case PLANAR_V_ALIGNED:

            if (pitchUV) {
                int r = ((row_size + FRAME_ALIGN - 1) & (~(FRAME_ALIGN - 1))) >> 1; // Aligned rowsize

                if (r <= pitchUV)
                    return r;

                return row_size >> 1;
            } else return 0;

        case PLANAR_Y_ALIGNED:
            int r = (row_size + FRAME_ALIGN - 1) & (~(FRAME_ALIGN - 1)); // Aligned rowsize

            if (r <= pitch)
                return r;

            return row_size;
        }

        return row_size;
    }
    int GetHeight() const {
        return height;
    }
    int GetHeight(int plane) const {
        switch (plane) {
        case PLANAR_U:
        case PLANAR_V:

            if (pitchUV) return height >> 1;

            return 0;
        }

        return height;
    }

    // generally you shouldn't use these three
    VideoFrameBuffer *GetFrameBuffer() const {
        return vfb;
    }
    int GetOffset() const {
        return offset;
    }
    int GetOffset(int plane) const {
        switch (plane) {
        case PLANAR_U:
            return offsetU;
        case PLANAR_V:
            return offsetV;
        default:
            return offset;
        };
    }

    // in plugins use env->SubFrame()
    VideoFrame *Subframe(int rel_offset, int new_pitch, int new_row_size, int new_height) const;
    VideoFrame *Subframe(int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int pitchUV) const;


    const uint8_t *GetReadPtr() const {
        return vfb->GetReadPtr() + offset;
    }
    const uint8_t *GetReadPtr(int plane) const {
        return vfb->GetReadPtr() + GetOffset(plane);
    }

    bool IsWritable() const {
        return (refcount == 1 && vfb->refcount == 1);
    }

    uint8_t *GetWritePtr() const {
        if (vfb->GetRefcount() > 1) {
            assert(false);
        }

        return IsWritable() ? (vfb->GetWritePtr() + offset) : 0;
    }

    uint8_t *GetWritePtr(int plane) const {
        if (plane == PLANAR_Y) {
            if (vfb->GetRefcount() > 1) {
                assert(false);
            }

            return IsWritable() ? vfb->GetWritePtr() + GetOffset(plane) : 0;
        }

        return vfb->data + GetOffset(plane);
    }

    // modified for wrapping trickery, we must make sure our destructor is called
    // instead of the baked one
    ~VideoFrame() {
        delete vfb;
    }
};

// Base class for all filters.
class IClip {
    friend class PClip;
    friend class AVSValue;
    int refcnt;
    void AddRef() {
        InterlockedIncrement((long *)&refcnt);
    }
    void Release() {
        InterlockedDecrement((long *)&refcnt);

        if (!refcnt) delete this;
    }
public:
    IClip() : refcnt(0) {}

    virtual int __stdcall GetVersion() {
        return AVISYNTH_INTERFACE_VERSION;
    }

    virtual PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) = 0;
    virtual bool __stdcall GetParity(int n) = 0;  // return field parity if field_based, else parity of first field in frame
    virtual void __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) = 0;  // start and count are in samples
    virtual void __stdcall SetCacheHints(int cachehints, int frame_range) = 0 ; // We do not pass cache requests upwards, only to the next filter.
    virtual const VideoInfo &__stdcall GetVideoInfo() = 0;
    virtual ~IClip() {}
};

// smart pointer to IClip
class PClip {

    IClip *p;

    IClip *GetPointerWithAddRef() const {
        if (p) p->AddRef();

        return p;
    }
    friend class AVSValue;
    friend class VideoFrame;

    void Init(IClip *x) {
        if (x) x->AddRef();

        p = x;
    }
    void Set(IClip *x) {
        if (x) x->AddRef();

        if (p) p->Release();

        p = x;
    }

public:
    PClip() {
        p = 0;
    }
    PClip(const PClip &x) {
        Init(x.p);
    }
    PClip(IClip *x) {
        Init(x);
    }
    void operator=(IClip *x) {
        Set(x);
    }
    void operator=(const PClip &x) {
        Set(x.p);
    }

    IClip *operator->() const {
        return p;
    }

    // useful in conditional expressions
    operator void*() const {
        return p;
    }
    bool operator!() const {
        return !p;
    }

    ~PClip() {
        if (p) p->Release();
    }
};


// smart pointer to VideoFrame
class PVideoFrame {

    VideoFrame *p;

    void Init(VideoFrame *x) {
        if (x) x->AddRef();

        p = x;
    }
    void Set(VideoFrame *x) {
        if (x) x->AddRef();

        if (p) p->Release();

        p = x;
    }

public:
    PVideoFrame() {
        p = 0;
    }
    PVideoFrame(const PVideoFrame &x) {
        Init(x.p);
    }
    PVideoFrame(VideoFrame *x) {
        Init(x);
    }
    void operator=(VideoFrame *x) {
        Set(x);
    }
    void operator=(const PVideoFrame &x) {
        Set(x.p);
    }

    VideoFrame *operator->() const {
        return p;
    }

    // for conditional expressions
    operator void*() const {
        return p;
    }
    bool operator!() const {
        return !p;
    }

    ~PVideoFrame() {
        if (p) p->Release();
    }
};


class AVSValue {
public:

    AVSValue() {
        type = 'v';
    }
    AVSValue(IClip *c) {
        type = 'c';
        clip = c;

        if (c) c->AddRef();
    }
    AVSValue(const PClip &c) {
        type = 'c';
        clip = c.GetPointerWithAddRef();
    }
    AVSValue(bool b) {
        type = 'b';
        boolean = b;
    }
    AVSValue(int i) {
        type = 'i';
        integer = i;
    }
    AVSValue(float f) {
        type = 'f';
        floating_pt = f;
    }
    AVSValue(double f) {
        type = 'f';
        floating_pt = float(f);
    }
    AVSValue(const char *s) {
        type = 's';
        string = s;
    }
    AVSValue(const AVSValue *a, int size) {
        type = 'a';
        array = a;
        array_size = size;
    }
    AVSValue(const AVSValue &v) {
        Assign(&v, true);
    }

    ~AVSValue() {
        if (IsClip() && clip) clip->Release();
    }
    AVSValue &operator=(const AVSValue &v) {
        Assign(&v, false);
        return *this;
    }

    // Note that we transparently allow 'int' to be treated as 'float'.
    // There are no int<->bool conversions, though.

    bool Defined() const {
        return type != 'v';
    }
    bool IsClip() const {
        return type == 'c';
    }
    bool IsBool() const {
        return type == 'b';
    }
    bool IsInt() const {
        return type == 'i';
    }
    bool IsFloat() const {
        return type == 'f' || type == 'i';
    }
    bool IsString() const {
        return type == 's';
    }
    bool IsArray() const {
        return type == 'a';
    }

    PClip AsClip() const {
        assert(IsClip());
        return IsClip() ? clip : 0;
    }
    bool AsBool() const {
        assert(IsBool());
        return boolean;
    }
    int AsInt() const {
        assert(IsInt());
        return integer;
    }
    const char *AsString() const {
        assert(IsString());
        return IsString() ? string : 0;
    }
    double AsFloat() const {
        assert(IsFloat());
        return IsInt() ? integer : floating_pt;
    }

    bool AsBool(bool def) const {
        assert(IsBool() || !Defined());
        return IsBool() ? boolean : def;
    }
    int AsInt(int def) const {
        assert(IsInt() || !Defined());
        return IsInt() ? integer : def;
    }
    double AsFloat(double def) const {
        assert(IsFloat() || !Defined());
        return IsInt() ? integer : type == 'f' ? floating_pt : def;
    }
    const char *AsString(const char *def) const {
        assert(IsString() || !Defined());
        return IsString() ? string : def;
    }

    int ArraySize() const {
        assert(IsArray());
        return IsArray() ? array_size : 1;
    }

    const AVSValue &operator[](int index) const {
        assert(IsArray() && index >= 0 && index < array_size);
        return (IsArray() && index >= 0 && index < array_size) ? array[index] : *this;
    }

private:

    short type;  // 'a'rray, 'c'lip, 'b'ool, 'i'nt, 'f'loat, 's'tring, 'v'oid, or 'l'ong
    short array_size;
    union {
        IClip *clip;
        bool boolean;
        int integer;
        float floating_pt;
        const char *string;
        const AVSValue *array;
    };

    void Assign(const AVSValue *src, bool init) {
        if (src->IsClip() && src->clip)
            src->clip->AddRef();

        if (!init && IsClip() && clip)
            clip->Release();

        // this is less of an affront to humanity and appears to generate correct code too
        type = src->type;
        array_size = src->array_size;
        array = src->array;
        // make sure this copies the whole struct!
        // in the words of the avisynth.h in the 64bit port, wtf?!
        //((int32_t*)this)[0] = ((int32_t*)src)[0];
        //((int32_t*)this)[1] = ((int32_t*)src)[1];
    }
};

// instantiable null filter
class GenericVideoFilter : public IClip {
protected:
    PClip child;
    VideoInfo vi;
public:
    GenericVideoFilter(PClip _child) : child(_child) {
        vi = child->GetVideoInfo();
    }
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) {
        return child->GetFrame(n, env);
    }
    void __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) {
        child->GetAudio(buf, start, count, env);
    }
    const VideoInfo &__stdcall GetVideoInfo() {
        return vi;
    }
    bool __stdcall GetParity(int n) {
        return child->GetParity(n);
    }
    void __stdcall SetCacheHints(int cachehints, int frame_range) { } ;  // We do not pass cache requests upwards, only to the next filter.
};

// For GetCPUFlags.  These are backwards-compatible with those in VirtualDub.
enum {
    /* slowest CPU to support extension */
    CPUF_FORCE        =  0x01,   //  N/A
    CPUF_FPU          =  0x02,   //  386/486DX
    CPUF_MMX          =  0x04,   //  P55C, K6, PII
    CPUF_INTEGER_SSE  =  0x08,   //  PIII, Athlon
    CPUF_SSE          =  0x10,   //  PIII, Athlon XP/MP
    CPUF_SSE2         =  0x20,   //  PIV, Hammer
    CPUF_3DNOW        =  0x40,   //  K6-2
    CPUF_3DNOW_EXT    =  0x80,   //  Athlon
    CPUF_X86_64       =  0xA0,   //  Hammer (note: equiv. to 3DNow + SSE2, which
    //          only Hammer will have anyway)
    CPUF_SSE3         = 0x100   //  PIV+, Hammer
};

class IScriptEnvironment {
public:
    virtual ~IScriptEnvironment() {}

    virtual /*static*/ long __stdcall GetCPUFlags() = 0;

    virtual char *__stdcall SaveString(const char *s, int length = -1) = 0;
    virtual char *Sprintf(const char *fmt, ...) = 0;
    // note: val is really a va_list; I hope everyone typedefs va_list to a pointer
    virtual char *__stdcall VSprintf(const char *fmt, void *val) = 0;

    __declspec(noreturn) virtual void ThrowError(const char *fmt, ...) = 0;

    class NotFound /*exception*/ {};  // thrown by Invoke and GetVar

    typedef AVSValue(__cdecl *ApplyFunc)(AVSValue args, void *user_data, IScriptEnvironment *env);

    virtual void __stdcall AddFunction(const char *name, const char *params, ApplyFunc apply, void *user_data) = 0;
    virtual bool __stdcall FunctionExists(const char *name) = 0;
    virtual AVSValue __stdcall Invoke(const char *name, const AVSValue args, const char **arg_names = 0) = 0;

    virtual AVSValue __stdcall GetVar(const char *name) = 0;
    virtual bool __stdcall SetVar(const char *name, const AVSValue &val) = 0;
    virtual bool __stdcall SetGlobalVar(const char *name, const AVSValue &val) = 0;

    virtual void __stdcall PushContext(int level = 0) = 0;
    virtual void __stdcall PopContext() = 0;

    // align should be 4 or 8
    virtual PVideoFrame __stdcall NewVideoFrame(const VideoInfo &vi, int align = FRAME_ALIGN) = 0;

    virtual bool __stdcall MakeWritable(PVideoFrame *pvf) = 0;

    virtual /*static*/ void __stdcall BitBlt(uint8_t *dstp, int dst_pitch, const uint8_t *srcp, int src_pitch, int row_size, int height) = 0;

    typedef void (__cdecl *ShutdownFunc)(void *user_data, IScriptEnvironment *env);
    virtual void __stdcall AtExit(ShutdownFunc function, void *user_data) = 0;

    virtual void __stdcall CheckVersion(int version = AVISYNTH_INTERFACE_VERSION) = 0;

    virtual PVideoFrame __stdcall Subframe(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height) = 0;

    virtual int __stdcall SetMemoryMax(int mem) = 0;

    virtual int __stdcall SetWorkingDir(const char *newdir) = 0;

    virtual void *__stdcall ManageCache(int key, void *data) = 0;

    enum PlanarChromaAlignmentMode {
        PlanarChromaAlignmentOff,
        PlanarChromaAlignmentOn,
        PlanarChromaAlignmentTest
    };

    virtual bool __stdcall PlanarChromaAlignment(PlanarChromaAlignmentMode key) = 0;

    virtual PVideoFrame __stdcall SubframePlanar(PVideoFrame src, int rel_offset, int new_pitch, int new_row_size, int new_height, int rel_offsetU, int rel_offsetV, int new_pitchUV) = 0;
};

}

#pragma pack(pop)

#endif //AVISYNTH_WRAPPER_H
