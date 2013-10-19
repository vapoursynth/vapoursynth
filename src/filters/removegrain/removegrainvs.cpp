/*****************************************************************************

        AvsFilterRemoveGrain/Repair16
        Author: Laurent de Soras, 2012
        Modified for VapourSynth by Fredrik Mellbin 2013 

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/

#include "shared.h"

#ifdef VS_TARGET_CPU_X86
class ConvSigned
{
public:
	static __forceinline __m128i cv (__m128i a, __m128i m)
	{
		return (_mm_xor_si128 (a, m));
	}
};


class ConvUnsigned
{
public:
	static __forceinline __m128i cv (__m128i a, __m128i m)
	{
		return (a);
	}
};

#define AvsFilterRemoveGrain16_READ_PIX    \
   const int      om = stride_src - 1;     \
   const int      o0 = stride_src    ;     \
   const int      op = stride_src + 1;     \
   __m128i        a1, a2, a3, a4, c, a5, a6, a7, a8; \
   if (sizeof(T) == 1) { \
       __m128i zeroreg = _mm_setzero_si128(); \
       a1 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - op)), zeroreg), mask_sign); \
       a2 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - o0)), zeroreg), mask_sign); \
       a3 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - om)), zeroreg), mask_sign); \
       a4 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr - 1 )), zeroreg), mask_sign); \
       c  = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + 0 )), zeroreg), mask_sign); \
       a5 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + 1 )), zeroreg), mask_sign); \
       a6 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + om)), zeroreg), mask_sign); \
       a7 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + o0)), zeroreg), mask_sign); \
       a8 = ConvSign::cv (_mm_unpacklo_epi8(_mm_loadl_epi64 (reinterpret_cast<const __m128i *>(src_ptr + op)), zeroreg), mask_sign); \
   } else {                                \
       a1 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - op)), mask_sign); \
       a2 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - o0)), mask_sign); \
       a3 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - om)), mask_sign); \
       a4 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr - 1 )), mask_sign); \
       c  = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + 0 )), mask_sign); \
       a5 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + 1 )), mask_sign); \
       a6 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + om)), mask_sign); \
       a7 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + o0)), mask_sign); \
       a8 = ConvSign::cv (_mm_loadu_si128 (reinterpret_cast<const __m128i *>(src_ptr + op)), mask_sign); \
   }
#else

class ConvSigned
{
};


class ConvUnsigned
{
};
#endif


class OpRG01
{
public:
	typedef	ConvSigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int		mi = std::min (
	        std::min (std::min (a1, a2), std::min (a3, a4)),
	        std::min (std::min (a5, a6), std::min (a7, a8))
        );
        const int		ma = std::max (
	        std::max (std::max (a1, a2), std::max (a3, a4)),
	        std::max (std::max (a5, a6), std::max (a7, a8))
        );

	    return (limit (c, mi, ma));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    AvsFilterRemoveGrain16_READ_PIX

	    const __m128i	mi = _mm_min_epi16 (
		    _mm_min_epi16 (_mm_min_epi16 (a1, a2), _mm_min_epi16 (a3, a4)),
		    _mm_min_epi16 (_mm_min_epi16 (a5, a6), _mm_min_epi16 (a7, a8))
	    );
	    const __m128i	ma = _mm_max_epi16 (
		    _mm_max_epi16 (_mm_max_epi16 (a1, a2), _mm_max_epi16 (a3, a4)),
		    _mm_max_epi16 (_mm_max_epi16 (a5, a6), _mm_max_epi16 (a7, a8))
	    );

	    return (_mm_min_epi16 (_mm_max_epi16 (c, mi), ma));
    }
#endif
};

class OpRG02
{
public:
	typedef	ConvSigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        int				a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

	    std::sort (&a [0], (&a [7]) + 1);

	    return (limit (c, a [2-1], a [7-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    AvsFilterRemoveGrain16_READ_PIX

	    sort_pair (a1, a2);
	    sort_pair (a3, a4);
	    sort_pair (a5, a6);
	    sort_pair (a7, a8);

	    sort_pair (a1, a3);
	    sort_pair (a2, a4);
	    sort_pair (a5, a7);
	    sort_pair (a6, a8);

	    sort_pair (a2, a3);
	    sort_pair (a6, a7);

	    a5 = _mm_max_epi16 (a1, a5);	// sort_pair (a1, a5);
	    sort_pair (a2, a6);
	    sort_pair (a3, a7);
	    a4 = _mm_min_epi16 (a4, a8);	// sort_pair (a4, a8);

	    a3 = _mm_min_epi16 (a3, a5);	// sort_pair (a3, a5);
	    a6 = _mm_max_epi16 (a4, a6);	// sort_pair (a4, a6);

	    a2 = _mm_min_epi16 (a2, a3);	// sort_pair (a2, a3);
	    a7 = _mm_max_epi16 (a6, a7);	// sort_pair (a6, a7);

	    return (_mm_min_epi16 (_mm_max_epi16 (c, a2), a7));
    }
#endif
};

class OpRG03
{
public:
	typedef	ConvSigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
	    int				a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

	    std::sort (&a [0], (&a [7]) + 1);

	    return (limit (c, a [3-1], a [6-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    AvsFilterRemoveGrain16_READ_PIX

	    sort_pair (a1, a2);
	    sort_pair (a3, a4);
	    sort_pair (a5, a6);
	    sort_pair (a7, a8);

	    sort_pair (a1, a3);
	    sort_pair (a2, a4);
	    sort_pair (a5, a7);
	    sort_pair (a6, a8);

	    sort_pair (a2, a3);
	    sort_pair (a6, a7);

	    a5 = _mm_max_epi16 (a1, a5);	// sort_pair (a1, a5);
	    sort_pair (a2, a6);
	    sort_pair (a3, a7);
	    a4 = _mm_min_epi16 (a4, a8);	// sort_pair (a4, a8);

	    a3 = _mm_min_epi16 (a3, a5);	// sort_pair (a3, a5);
	    a6 = _mm_max_epi16 (a4, a6);	// sort_pair (a4, a6);

	    a3 = _mm_max_epi16 (a2, a3);	// sort_pair (a2, a3);
	    a6 = _mm_min_epi16 (a6, a7);	// sort_pair (a6, a7);

	    return (_mm_min_epi16 (_mm_max_epi16 (c, a3), a6));
    }
#endif
};

class OpRG04
{
public:
	typedef	ConvSigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
	    int				a [8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

	    std::sort (&a [0], (&a [7]) + 1);

	    return (limit (c, a [4-1], a [5-1]));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    // http://en.wikipedia.org/wiki/Batcher_odd%E2%80%93even_mergesort

	    AvsFilterRemoveGrain16_READ_PIX

	    sort_pair (a1, a2);
	    sort_pair (a3, a4);
	    sort_pair (a5, a6);
	    sort_pair (a7, a8);

	    sort_pair (a1, a3);
	    sort_pair (a2, a4);
	    sort_pair (a5, a7);
	    sort_pair (a6, a8);

	    sort_pair (a2, a3);
	    sort_pair (a6, a7);

	    a5 = _mm_max_epi16 (a1, a5);	// sort_pair (a1, a5);
	    a6 = _mm_max_epi16 (a2, a6);	// sort_pair (a2, a6);
	    a3 = _mm_min_epi16 (a3, a7);	// sort_pair (a3, a7);
	    a4 = _mm_min_epi16 (a4, a8);	// sort_pair (a4, a8);

	    a5 = _mm_max_epi16 (a3, a5);	// sort_pair (a3, a5);
	    a4 = _mm_min_epi16 (a4, a6);	// sort_pair (a4, a6);

											    // sort_pair (a2, a3);
	    sort_pair (a4, a5);
											    // sort_pair (a6, a7);

	    return (_mm_min_epi16 (_mm_max_epi16 (c, a4), a5));
    }
#endif
};

class OpRG11
{
public:
	typedef	ConvUnsigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
	    const int		sum = 4 * c + 2 * (a2 + a4 + a5 + a7) + a1 + a3 + a6 + a8;
	    const int		val = (sum + 8) >> 4;

	    return (val);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    return (OpRG12::rg (src_ptr, stride_src, mask_sign));
    }
#endif
};

class OpRG12
{
public:
	typedef	ConvUnsigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
	    return (OpRG11::rg (c, a1, a2, a3, a4, a5, a6, a7, a8));
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    AvsFilterRemoveGrain16_READ_PIX

	    const __m128i	bias =
		    _mm_load_si128 (reinterpret_cast <const __m128i *> (_bias));

	    const __m128i	a13  = _mm_avg_epu16 (a1, a3);
	    const __m128i	a123 = _mm_avg_epu16 (a2, a13);

	    const __m128i	a68  = _mm_avg_epu16 (a6, a8);
	    const __m128i	a678 = _mm_avg_epu16 (a7, a68);

	    const __m128i	a45  = _mm_avg_epu16 (a4, a5);
	    const __m128i	a4c5 = _mm_avg_epu16 (c, a45);

	    const __m128i	a123678  = _mm_avg_epu16 (a123, a678);
	    const __m128i	a123678b = _mm_subs_epu16 (a123678, bias);
	    const __m128i	val      = _mm_avg_epu16 (a4c5, a123678b);

	    return (val);
    }
private:
	static const __declspec (align (16)) uint16_t
						_bias [8];
#endif
};

class OpRG19
{
public:
	typedef	ConvUnsigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
	    const int		sum = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
	    const int		val = (sum + 4) >> 3;

	    return (val);
    }
#ifdef VS_TARGET_CPU_X86
    template<typename T>
	static __forceinline __m128i rg (const T *src_ptr, int stride_src, __m128i mask_sign) {
	    AvsFilterRemoveGrain16_READ_PIX

	    const __m128i	bias =
		    _mm_load_si128 (reinterpret_cast <const __m128i *> (_bias));

	    const __m128i	a13    = _mm_avg_epu16 (a1, a3);
	    const __m128i	a68    = _mm_avg_epu16 (a6, a8);
	    const __m128i	a1368  = _mm_avg_epu16 (a13, a68);
	    const __m128i	a1368b = _mm_subs_epu16 (a1368, bias);
	    const __m128i	a25    = _mm_avg_epu16 (a2, a5);
	    const __m128i	a47    = _mm_avg_epu16 (a4, a7);
	    const __m128i	a2457  = _mm_avg_epu16 (a25, a47);
	    const __m128i	val    = _mm_avg_epu16 (a1368b, a2457);

	    return (val);
    }
private:
	static const __declspec (align (16)) uint16_t
						_bias [8];
#endif
};

class OpRG20
{
public:
	typedef	ConvSigned	ConvSign;
	static __forceinline int rg (int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        const int		sum = a1 + a2 + a3 + a4 + c + a5 + a6 + a7 + a8;
	    const int		val = (sum + 4) / 9;

	    return (val);
    }
};

#ifdef VS_TARGET_CPU_X86
const __declspec (align (16)) uint16_t	OpRG12::_bias [8] =
{ 1, 1, 1, 1, 1, 1, 1, 1 };

const __declspec (align (16)) uint16_t	OpRG19::_bias [8] =
{ 1, 1, 1, 1, 1, 1, 1, 1 };

#undef AvsFilterRemoveGrain16_READ_PIX

#endif

template <class OP, class T>
class PlaneProc {
public:

static void process_subplane_cpp (const T *src_ptr, int stride_src, T *dst_ptr, int stride_dst, int width, int height)
{
	const int		y_b = 1;
	const int		y_e = height - 1;

	dst_ptr += y_b * stride_dst;
	src_ptr += y_b * stride_src;

	const int		x_e = width - 1;

	for (int y = y_b; y < y_e; ++y)
	{
		dst_ptr [0] = src_ptr [0];

		process_row_cpp (
			dst_ptr,
			src_ptr,
			stride_src,
			1,
			x_e
		);

		dst_ptr [x_e] = src_ptr [x_e];

		dst_ptr += stride_dst;
		src_ptr += stride_src;
	}
}

static void process_row_cpp (T *dst_ptr, const T *src_ptr, int stride_src, int x_beg, int x_end)
{
	const int      om = stride_src - 1;
	const int      o0 = stride_src    ;
	const int      op = stride_src + 1;

	src_ptr += x_beg;

	for (int x = x_beg; x < x_end; ++x)
	{
		const int		a1 = src_ptr [-op];
		const int		a2 = src_ptr [-o0];
		const int		a3 = src_ptr [-om];
		const int		a4 = src_ptr [-1 ];
		const int		c  = src_ptr [ 0 ];
		const int		a5 = src_ptr [ 1 ];
		const int		a6 = src_ptr [ om];
		const int		a7 = src_ptr [ o0];
		const int		a8 = src_ptr [ op];

		const int		res = OP::rg (c, a1, a2, a3, a4, a5, a6, a7, a8);

		dst_ptr [x] = res;

		++ src_ptr;
	}
}

#ifdef VS_TARGET_CPU_X86
static void process_subplane_sse2 (const T *src_ptr, int stride_src, T *dst_ptr, int stride_dst, int width, int height)
{
	const int		y_b = 1;
	const int		y_e = height - 1;

	dst_ptr += y_b * stride_dst;
	src_ptr += y_b * stride_src;

	const __m128i	mask_sign = _mm_set1_epi16 (-0x8000);

	const int		x_e =   width - 1;
	const int		w8  = ((width - 2) & -8) + 1;
	const int		w7  = x_e - w8;

	for (int y = y_b; y < y_e; ++y)
	{
		dst_ptr [0] = src_ptr [0];
		dst_ptr [0] = src_ptr [0];

		for (int x = 1; x < w8; x += 8)
		{
			__m128i			res = OP::rg (
				src_ptr + x,
				stride_src,
				mask_sign
			);

			res = OP::ConvSign::cv (res, mask_sign);
            if (sizeof(T) == 1)
                _mm_storel_epi64 (reinterpret_cast<__m128i *>(dst_ptr + x), _mm_packus_epi16 (res, res));
            else
                _mm_storeu_si128 (reinterpret_cast<__m128i *>(dst_ptr + x), res);
		}

		process_row_cpp (
			dst_ptr,
			src_ptr,
			stride_src,
			w8,
			x_e
		);

		dst_ptr [x_e] = src_ptr [x_e];

		dst_ptr += stride_dst;
		src_ptr += stride_src;
	}
}

template <class OP, class T>
static void do_process_plane_sse2 (const VSFrameRef *src_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int		w             = vsapi->getFrameWidth(src_frame, plane_id);
	const int		h             = vsapi->getFrameHeight(src_frame, plane_id);
	T *		        dst_ptr       = reinterpret_cast<T*>(vsapi->getWritePtr(dst_frame, plane_id));
	const int		stride        = vsapi->getStride(dst_frame, plane_id);

	const T*	    src_ptr       = reinterpret_cast<const T*>(vsapi->getReadPtr(src_frame, plane_id));

	// First line
	memcpy (dst_ptr, src_ptr, stride);

	// Main content
    PlaneProc<OP, T>::process_subplane_sse2(src_ptr, stride/sizeof(T), dst_ptr, stride/sizeof(T), w, h);

	// Last line
	const int		lp = (h - 1) * stride/sizeof(T);
	memcpy (dst_ptr + lp, src_ptr + lp, stride);
}

#endif

template <class OP, class T>
static void do_process_plane_cpp (const VSFrameRef *src_frame, VSFrameRef *dst_frame, int plane_id, const VSAPI *vsapi)
{
    const int		w             = vsapi->getFrameWidth(src_frame, plane_id);
	const int		h             = vsapi->getFrameHeight(src_frame, plane_id);
	T *		        dst_ptr       = reinterpret_cast<T*>(vsapi->getWritePtr(dst_frame, plane_id));
	const int		stride        = vsapi->getStride(dst_frame, plane_id);

	const T*	    src_ptr       = reinterpret_cast<const T*>(vsapi->getReadPtr(src_frame, plane_id));

	// First line
	memcpy (dst_ptr, src_ptr, stride);

	// Main content
    PlaneProc<OP, T>::process_subplane_cpp(src_ptr, stride/sizeof(T), dst_ptr, stride/sizeof(T), w, h);

	// Last line
	const int		lp = (h - 1) * stride/sizeof(T);
	memcpy (dst_ptr + lp, src_ptr + lp, stride);
}

};

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;
    int mode[3];
} RemoveGrainData;

static void VS_CC removeGrainInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC removeGrainGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src_frame = vsapi->getFrameFilter(n, d->node, frameCtx);
        int planes[3] = {0, 1, 2};
        const VSFrameRef * cp_planes[3] = { d->mode[0] ? NULL : src_frame, d->mode[1] ? NULL : src_frame, d->mode[2] ? NULL : src_frame };
        VSFrameRef *dst_frame = vsapi->newVideoFrame2(vsapi->getFrameFormat(src_frame), vsapi->getFrameWidth(src_frame, 0), vsapi->getFrameHeight(src_frame, 0), cp_planes, planes, src_frame, core);


#define PROC_ARGS_16(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint16_t>(src_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8(op) PlaneProc <op, uint16_t>::do_process_plane_cpp<op, uint8_t>(src_frame, dst_frame, i, vsapi); break;

#ifdef VS_TARGET_CPU_X86
#define PROC_ARGS_16_FAST(op) PlaneProc <op, uint16_t>::do_process_plane_sse2<op, uint16_t>(src_frame, dst_frame, i, vsapi); break;
#define PROC_ARGS_8_FAST(op) PlaneProc <op, uint8_t>::do_process_plane_sse2<op, uint8_t>(src_frame, dst_frame, i, vsapi); break;
#else
#define PROC_ARGS_16_FAST(op) PROC_ARGS_16(op)
#define PROC_ARGS_8_FAST(op) PROC_ARGS_8(op)
#endif

        if (d->vi->format->bytesPerSample == 1) {
            for (int i = 0; i < d->vi->format->numPlanes; i++) {
                switch (d->mode[i])
			    {
                    case  1: PROC_ARGS_8_FAST(OpRG01)
			        case  2: PROC_ARGS_8_FAST(OpRG02)
			        case  3: PROC_ARGS_8_FAST(OpRG03)
			        case  4: PROC_ARGS_8_FAST(OpRG04)
			        case 11: PROC_ARGS_8_FAST(OpRG11)
			        case 12: PROC_ARGS_8_FAST(OpRG12)
			        case 19: PROC_ARGS_8_FAST(OpRG19)
                    case 20: PROC_ARGS_8(OpRG20)
			        default: break;
			    }
            }
        } else {
            for (int i = 0; i < d->vi->format->numPlanes; i++) {
                switch (d->mode[i])
			    {
                    case  1: PROC_ARGS_16_FAST(OpRG01)
			        case  2: PROC_ARGS_16_FAST(OpRG02)
			        case  3: PROC_ARGS_16_FAST(OpRG03)
			        case  4: PROC_ARGS_16_FAST(OpRG04)
			        case 11: PROC_ARGS_16_FAST(OpRG11)
			        case 12: PROC_ARGS_16_FAST(OpRG12)
			        case 19: PROC_ARGS_16_FAST(OpRG19)
                    case 20: PROC_ARGS_16(OpRG20)
			        default: break;
			    }
            }
        } 

        vsapi->freeFrame(src_frame);

        return dst_frame;
    }

    return 0;
}

static void VS_CC removeGrainFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData *d = (RemoveGrainData *)instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC removeGrainCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    RemoveGrainData d;
   
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!d.vi->format) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Only constant format input supported");
        return;
    }

    if (d.vi->format->sampleType != stInteger || (d.vi->format->bytesPerSample != 1 && d.vi->format->bytesPerSample != 2)) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Only 8-16 bit int formats supported");
        return;
    }

    int n = d.vi->format->numPlanes;
    int m = vsapi->propNumElements(in, "mode");
    if (n < m) {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "RemoveGrain: Number of modes specified must be equal or fewer than the number of input planes");
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (i < m) {
            d.mode[i] = int64ToIntS(vsapi->propGetInt(in, "mode", i, NULL));
            if (d.mode[i] != 0 && d.mode[i] != 1 && d.mode[i] != 2 && d.mode[i] != 3 && d.mode[i] != 4 && d.mode[i] != 11 && d.mode[i] != 12 && d.mode[i] != 19 && d.mode[i] != 20)
            {
                vsapi->freeNode(d.node);
                vsapi->setError(out, "RemoveGrain: Invalid mode specified, only 0-4, 11-12, 19-20 allowed");
                return;
            }
        } else {
            d.mode[i] = d.mode[i - 1];
        }
    }

    RemoveGrainData *data = new RemoveGrainData;
    *data = d;

    vsapi->createFilter(in, out, "RemoveGrain", removeGrainInit, removeGrainGetFrame, removeGrainFree, fmParallel, 0, data, core);
}

