#include <algorithm>
#include <stdint.h>
#include "average.h"

namespace {

template <class T>
void average_plane_int(const void *weights_, const void * const *srcs, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride, bool chroma)
{
	const int *weights = static_cast<const int *>(weights_);
	ptrdiff_t offset = 0;

	int32_t maxval = (1L << depth) - 1;
	int32_t bias = chroma ? (1L << (depth - 1)) : 0;
	int scale = *static_cast<const int *>(scale_);
	int round = scale / 2;

	for (unsigned i = 0; i < h; ++i) {
		T *dst = reinterpret_cast<T *>(static_cast<uint8_t *>(dst_) + offset);

		for (unsigned j = 0; j < w; ++j) {
			int32_t accum = 0;

			for (unsigned k = 0; k < num_srcs; ++k) {
				const T *src = reinterpret_cast<const T *>(static_cast<const uint8_t *>(srcs[k]) + offset);
				int32_t val = src[j];
				accum += (val - bias) * weights[k];
			}

			accum = (accum + round) / scale + bias;
			accum = std::min(std::max(accum, static_cast<int32_t>(0)), maxval);
			dst[j] = static_cast<T>(accum);
		}

		offset += stride;
	}
}

void average_plane_float(const void *weights_, const void * const *srcs, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	const float *weights = static_cast<const float *>(weights_);
	ptrdiff_t offset = 0;
	float scale = 1.0f / *static_cast<const float *>(scale_);

	for (unsigned i = 0; i < h; ++i) {
		float *dst = reinterpret_cast<float *>(static_cast<uint8_t *>(dst_) + offset);

		for (unsigned j = 0; j < w; ++j) {
			float accum = 0.0f;

			for (unsigned k = 0; k < num_srcs; ++k) {
				const float *src = reinterpret_cast<const float *>(static_cast<const uint8_t *>(srcs[k]) + offset);
				accum += src[j] * weights[k];
			}

			dst[j] = accum * scale;
		}

		offset += stride;
	}
}

} // namespace


void vs_average_plane_byte_luma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	average_plane_int<uint8_t>(weights, srcs, num_srcs, dst, scale, depth, w, h, stride, false);
}

void vs_average_plane_byte_chroma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	average_plane_int<uint8_t>(weights, srcs, num_srcs, dst, scale, depth, w, h, stride, true);
}

void vs_average_plane_word_luma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	average_plane_int<uint16_t>(weights, srcs, num_srcs, dst, scale, depth, w, h, stride, false);
}

void vs_average_plane_word_chroma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	average_plane_int<uint16_t>(weights, srcs, num_srcs, dst, scale, depth, w, h, stride, true);
}

void vs_average_plane_float_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	average_plane_float(weights, srcs, num_srcs, dst, scale, depth, w, h, stride);
}
