#include <algorithm>
#include <cassert>
#include <cstring>
#include "p2p.h"
#include "p2p_api.h"

#ifdef P2P_USER_NAMESPACE
  #error API build must not use custom namespace
#endif

namespace {

struct packing_traits {
	enum p2p_packing packing;
	p2p_unpack_func unpack;
	p2p_pack_func pack;
	bool native_endian;
	unsigned char subsample_w;
	unsigned char subsample_h;
	bool is_nv;
	unsigned char bytes_per_sample;	// Only used to copy luma plane for NV12.
	unsigned char nv_shift; // Extra LSB to shift away for MS P010/P210, etc.
};

#define CASE(x, ...) \
	{ p2p_##x, &p2p::packed_to_planar<p2p::packed_##x>::unpack, &p2p::planar_to_packed<p2p::packed_##x>::pack, ##__VA_ARGS__}
#define CASE2(x, ...) \
	CASE(x##_be, std::is_same<p2p::native_endian_t, p2p::big_endian_t>::value, ##__VA_ARGS__), \
	CASE(x##_le, std::is_same<p2p::native_endian_t, p2p::little_endian_t>::value, ##__VA_ARGS__), \
	CASE(x, true, ##__VA_ARGS__)
const packing_traits traits_table[] = {
	CASE2(rgb24, 0, 0),
	CASE2(argb32, 0, 0),
	CASE2(ayuv, 0, 0),
	CASE2(rgb48, 0, 0),
	CASE2(argb64, 0, 0),
	CASE2(rgb30, 0, 0),
	CASE2(y410, 0, 0),
	CASE2(y416, 0, 0),
	CASE(yuy2, true, 1, 0),
	CASE(uyvy, true, 1, 0),
	CASE2(y210, 1, 0),
	CASE2(y216, 1, 0),
	CASE2(v210, 1, 0),
	CASE2(v216, 1, 0),
	// For NV12-like formats, bytes_per_pel is the number of bytes in a UV pair.
	CASE2(nv12, 1, 1, true, 1),
	CASE2(p010, 1, 1, true, 2, 6),
	CASE2(p016, 1, 1, true, 2),
	CASE2(p210, 1, 0, true, 2, 6),
	CASE2(p216, 1, 0, true, 2),
};
#undef CASE2
#undef CASE

const packing_traits &lookup_traits(enum p2p_packing packing)
{
	assert(packing >= 0);
	assert(packing < sizeof(traits_table) / sizeof(traits_table[0]));

	const packing_traits &traits = traits_table[packing];
	assert(traits.packing == packing);
	assert(traits.subsample_h == 0 || traits.is_nv);
	return traits;
}

template <class T>
T *increment_ptr(T *ptr, ptrdiff_t n)
{
	return (T *)((const unsigned char *)ptr + n);
}

void copy_plane_fast(const void *src, void *dst, ptrdiff_t src_stride, ptrdiff_t dst_stride,
                     unsigned linesize, unsigned height)
{
	for (unsigned i = 0; i < height; ++i) {
		memcpy(dst, src, linesize);

		src = increment_ptr(src, src_stride);
		dst = increment_ptr(dst, dst_stride);
	}
}

void unpack_nv16_plane(const void *src, void *dst, ptrdiff_t src_stride, ptrdiff_t dst_stride,
                       const packing_traits &traits, unsigned width, unsigned height)
{
	assert(traits.bytes_per_sample == 1);

	for (unsigned i = 0; i < height; ++i) {
		std::transform(static_cast<const uint16_t *>(src), static_cast<const uint16_t *>(src) + width, static_cast<uint16_t *>(dst), [=](uint16_t x)
		{
			x = traits.native_endian ? x : (x >> 8) | (x << 8);
			return x >> traits.nv_shift;
		});

		src = increment_ptr(src, src_stride);
		dst = increment_ptr(dst, dst_stride);
	}
}

void pack_nv16_plane(const void *src, void *dst, ptrdiff_t src_stride, ptrdiff_t dst_stride,
                     const packing_traits &traits, unsigned width, unsigned height)
{
	assert(traits.bytes_per_sample == 1);

	for (unsigned i = 0; i < height; ++i) {
		std::transform(static_cast<const uint16_t *>(src), static_cast<const uint16_t *>(src) + width, static_cast<uint16_t *>(dst), [=](uint16_t x)
		{
			x = x << traits.nv_shift;
			return traits.native_endian ? x : (x >> 8) | (x << 8);
		});

		src = increment_ptr(src, src_stride);
		dst = increment_ptr(dst, dst_stride);
	}
}

} // namespace


p2p_unpack_func p2p_select_unpack_func(enum p2p_packing packing)
{
	return lookup_traits(packing).unpack;
}

p2p_pack_func p2p_select_pack_func(enum p2p_packing packing)
{
	return lookup_traits(packing).pack;
}

void p2p_unpack_frame(const struct p2p_buffer_param *param, unsigned long flags)
{
	const packing_traits &traits = lookup_traits(param->packing);

	// Process interleaved plane.
	const void *src_p = traits.is_nv ? param->src[1] : param->src[0];
	ptrdiff_t src_stride = traits.is_nv ? param->src_stride[1] : param->src_stride[0];

	void *dst_p[4] = { param->dst[0], param->dst[1], param->dst[2], param->dst[3] };

	for (unsigned i = 0; i < (param->height >> traits.subsample_h); ++i) {
		traits.unpack(src_p, dst_p, 0, param->width);

		src_p = increment_ptr(src_p, src_stride);

		if (!traits.is_nv) {
			dst_p[0] = increment_ptr(dst_p[0], param->dst_stride[0]);
			dst_p[3] = increment_ptr(dst_p[3], param->dst_stride[3]);
		}
		dst_p[1] = increment_ptr(dst_p[1], param->dst_stride[1]);
		dst_p[2] = increment_ptr(dst_p[2], param->dst_stride[2]);
	}

	if (traits.is_nv && !(flags & P2P_SKIP_UNPACKED_PLANES) && param->src[0] && param->dst[0]) {
		if ((traits.bytes_per_sample == 1 || traits.native_endian) && !traits.nv_shift) {
			copy_plane_fast(param->src[0], param->dst[0], param->src_stride[0], param->dst_stride[0],
			                traits.bytes_per_sample * param->width, param->height);
		} else {
			unpack_nv16_plane(param->src[0], param->dst[0], param->src_stride[0], param->dst_stride[0], traits, param->width, param->height);
		}
	}
}

void p2p_pack_frame(const struct p2p_buffer_param *param, unsigned long flags)
{
	const packing_traits &traits = lookup_traits(param->packing);

	// Process interleaved plane.
	const void *src_p[4] = { param->src[0], param->src[1], param->src[2], param->src[3] };

	void *dst_p = traits.is_nv ? param->dst[1] : param->dst[0];
	ptrdiff_t dst_stride = traits.is_nv ? param->dst_stride[1] : param->dst_stride[0];

	for (unsigned i = 0; i < (param->height >> traits.subsample_h); ++i) {
		traits.pack(src_p, dst_p, 0, param->width);

		if (!traits.is_nv) {
			src_p[0] = increment_ptr(src_p[0], param->src_stride[0]);
			src_p[3] = increment_ptr(src_p[3], param->src_stride[3]);
		}
		src_p[1] = increment_ptr(src_p[1], param->src_stride[1]);
		src_p[2] = increment_ptr(src_p[2], param->src_stride[2]);

		dst_p = increment_ptr(dst_p, dst_stride);
	}

	if (traits.is_nv && !(flags & P2P_SKIP_UNPACKED_PLANES) && param->src[0] && param->dst[0]) {
		if ((traits.bytes_per_sample == 1 || traits.native_endian) && !traits.nv_shift) {
			copy_plane_fast(param->src[0], param->dst[0], param->src_stride[0], param->dst_stride[0],
			                traits.bytes_per_sample * param->width, param->height);
		} else {
			pack_nv16_plane(param->src[0], param->dst[0], param->src_stride[0], param->dst_stride[0], traits, param->width, param->height);
		}
	}
}
