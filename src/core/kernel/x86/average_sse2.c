#include <assert.h>
#include <stdint.h>
#include <emmintrin.h>
#include "VSHelper4.h"
#include "../average.h"

static void load_int_srcs(const uint8_t **srcs, const void * const *srcs_, unsigned num_srcs)
{
	unsigned i;

	assert(num_srcs <= 32);

	for (i = 0; i < num_srcs; ++i) {
		srcs[i] = srcs_[i];
	}
	if (num_srcs % 2)
		srcs[num_srcs] = srcs[num_srcs - 1];
}

static void load_int_weights(__m128i mm_weights[16], const int *iweights, unsigned num_weights)
{
	unsigned i;

	for (i = 0; i < (num_weights & ~1); i += 2) {
		int16_t lo = iweights[i + 0];
		int16_t hi = iweights[i + 1];
		uint32_t coeff = ((uint32_t)(uint16_t)hi) << 16 | (uint16_t)lo;
		mm_weights[i / 2] = _mm_set1_epi32(coeff);
	}
	if (num_weights % 2)
		mm_weights[num_weights / 2] = _mm_set1_epi32((uint16_t)iweights[num_weights - 1]);
}

void vs_average_plane_byte_luma_sse2(const void *weights_, const void * const *srcs_, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	const uint8_t *srcs[32];
	__m128i weights[16];
	__m128 scale = _mm_set_ps1(1.0f / *(const int *)scale_);
	ptrdiff_t offset = 0;
	unsigned i, j, k;

	load_int_srcs(srcs, srcs_, num_srcs);
	load_int_weights(weights, weights_, num_srcs);

	for (i = 0; i < h; ++i) {
		uint8_t *dst = (uint8_t *)dst_ + offset;

		for (j = 0; j < w; j += 16) {
			__m128i lolo = _mm_setzero_si128();
			__m128i lohi = _mm_setzero_si128();
			__m128i hilo = _mm_setzero_si128();
			__m128i hihi = _mm_setzero_si128();

			for (k = 0; k < num_srcs; k += 2) {
				const uint8_t *ptr1 = srcs[k + 0] + offset;
				const uint8_t *ptr2 = srcs[k + 1] + offset;

				__m128i coeffs = weights[k / 2];
				__m128i v1 = _mm_load_si128((const __m128i *)(ptr1 + j));
				__m128i v2 = _mm_load_si128((const __m128i *)(ptr2 + j));

				__m128i v1_lo = _mm_unpacklo_epi8(v1, _mm_setzero_si128());
				__m128i v1_hi = _mm_unpackhi_epi8(v1, _mm_setzero_si128());
				__m128i v2_lo = _mm_unpacklo_epi8(v2, _mm_setzero_si128());
				__m128i v2_hi = _mm_unpackhi_epi8(v2, _mm_setzero_si128());

				lolo = _mm_add_epi32(lolo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_lo, v2_lo)));
				lohi = _mm_add_epi32(lohi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_lo, v2_lo)));
				hilo = _mm_add_epi32(hilo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_hi, v2_hi)));
				hihi = _mm_add_epi32(hihi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_hi, v2_hi)));
			}

			lolo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lolo), scale));
			lohi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lohi), scale));
			hilo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hilo), scale));
			hihi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hihi), scale));

			lolo = _mm_packs_epi32(lolo, lohi);
			hilo = _mm_packs_epi32(hilo, hihi);
			lolo = _mm_packus_epi16(lolo, hilo);

			_mm_store_si128((__m128i *)(dst + j), lolo);
		}

		offset += stride;
	}
}

void vs_average_plane_byte_chroma_sse2(const void *weights_, const void * const *srcs_, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	const uint8_t *srcs[32];
	__m128i weights[16];
	__m128 scale = _mm_set_ps1(1.0f / *(const int *)scale_);
	__m128i bias_i16 = _mm_set1_epi16(128);
	__m128i bias_i8 = _mm_set1_epi8(128);
	ptrdiff_t offset = 0;
	unsigned i, j, k;

	load_int_srcs(srcs, srcs_, num_srcs);
	load_int_weights(weights, weights_, num_srcs);

	for (i = 0; i < h; ++i) {
		uint8_t *dst = (uint8_t *)dst_ + offset;

		for (j = 0; j < w; j += 16) {
			__m128i lolo = _mm_setzero_si128();
			__m128i lohi = _mm_setzero_si128();
			__m128i hilo = _mm_setzero_si128();
			__m128i hihi = _mm_setzero_si128();

			for (k = 0; k < num_srcs; k += 2) {
				const uint8_t *ptr1 = srcs[k + 0] + offset;
				const uint8_t *ptr2 = srcs[k + 1] + offset;

				__m128i coeffs = weights[k / 2];
				__m128i v1 = _mm_load_si128((const __m128i *)(ptr1 + j));
				__m128i v2 = _mm_load_si128((const __m128i *)(ptr2 + j));

				__m128i v1_lo = _mm_sub_epi16(_mm_unpacklo_epi8(v1, _mm_setzero_si128()), bias_i16);
				__m128i v1_hi = _mm_sub_epi16(_mm_unpackhi_epi8(v1, _mm_setzero_si128()), bias_i16);
				__m128i v2_lo = _mm_sub_epi16(_mm_unpacklo_epi8(v2, _mm_setzero_si128()), bias_i16);
				__m128i v2_hi = _mm_sub_epi16(_mm_unpackhi_epi8(v2, _mm_setzero_si128()), bias_i16);

				lolo = _mm_add_epi32(lolo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_lo, v2_lo)));
				lohi = _mm_add_epi32(lohi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_lo, v2_lo)));
				hilo = _mm_add_epi32(hilo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1_hi, v2_hi)));
				hihi = _mm_add_epi32(hihi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1_hi, v2_hi)));
			}

			lolo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lolo), scale));
			lohi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lohi), scale));
			hilo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hilo), scale));
			hihi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hihi), scale));

			lolo = _mm_packs_epi32(lolo, lohi);
			hilo = _mm_packs_epi32(hilo, hihi);
			lolo = _mm_packs_epi16(lolo, hilo);
			lolo = _mm_add_epi8(lolo, bias_i8);

			_mm_store_si128((__m128i *)(dst + j), lolo);
		}

		offset += stride;
	}
}

void vs_average_plane_word_luma_sse2(const void *weights_, const void * const *srcs_, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	const uint8_t *srcs[32];
	__m128i weights[16];
	__m128 scale = _mm_set_ps1(1.0f / *(const int *)scale_);
	__m128i maxval = _mm_add_epi16(_mm_set1_epi16((1U << depth) - 1), _mm_set1_epi16(INT16_MIN));
	__m128i accum_bias = _mm_setzero_si128();
	ptrdiff_t offset = 0;
	unsigned i, j, k;

	load_int_srcs(srcs, srcs_, num_srcs);
	load_int_weights(weights, weights_, num_srcs);

	/* sum(weights * int16_min) */
	for (unsigned i = 0; i < num_srcs; i += 2) {
		accum_bias = _mm_add_epi32(accum_bias, _mm_madd_epi16(_mm_set1_epi16(INT16_MIN), weights[i / 2]));
	}

	for (i = 0; i < h; ++i) {
		uint16_t *dst = (uint16_t *)((uint8_t *)dst_ + offset);

		for (j = 0; j < w; j += 8) {
			__m128i lo = _mm_setzero_si128();
			__m128i hi = _mm_setzero_si128();

			for (k = 0; k < num_srcs; k += 2) {
				const uint16_t *ptr1 = (const uint16_t *)(srcs[k + 0] + offset);
				const uint16_t *ptr2 = (const uint16_t *)(srcs[k + 1] + offset);

				__m128i coeffs = weights[k / 2];
				__m128i v1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(ptr1 + j)), _mm_set1_epi16(INT16_MIN));
				__m128i v2 = _mm_add_epi16(_mm_load_si128((const __m128i *)(ptr2 + j)), _mm_set1_epi16(INT16_MIN));

				lo = _mm_add_epi32(lo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1, v2)));
				hi = _mm_add_epi32(hi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1, v2)));
			}
			lo = _mm_sub_epi32(lo, accum_bias);
			hi = _mm_sub_epi32(hi, accum_bias);

			lo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lo), scale));
			hi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hi), scale));

			lo = _mm_add_epi32(lo, _mm_set1_epi32(INT16_MIN));
			hi = _mm_add_epi32(hi, _mm_set1_epi32(INT16_MIN));
			lo = _mm_packs_epi32(lo, hi);

			lo = _mm_min_epi16(lo, maxval);
			lo = _mm_sub_epi16(lo, _mm_set1_epi16(INT16_MIN));

			_mm_store_si128((__m128i *)(dst + j), lo);
		}

		offset += stride;
	}
}

void vs_average_plane_word_chroma_sse2(const void *weights_, const void * const *srcs_, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	const uint8_t *srcs[32];
	__m128i weights[16];
	__m128 scale = _mm_set_ps1(1.0f / *(const int *)scale_);
	__m128i bias = _mm_set1_epi16(1U << (depth - 1));
	__m128i minval = _mm_sub_epi16(_mm_setzero_si128(), bias);
	__m128i maxval = _mm_sub_epi16(_mm_set1_epi16((1U << depth) - 1), bias);
	ptrdiff_t offset = 0;
	unsigned i, j, k;

	load_int_srcs(srcs, srcs_, num_srcs);
	load_int_weights(weights, weights_, num_srcs);

	for (i = 0; i < h; ++i) {
		uint16_t *dst = (uint16_t *)((uint8_t *)dst_ + offset);

		for (j = 0; j < w; j += 8) {
			__m128i lo = _mm_setzero_si128();
			__m128i hi = _mm_setzero_si128();

			for (k = 0; k < num_srcs; k += 2) {
				const uint16_t *ptr1 = (const uint16_t *)(srcs[k + 0] + offset);
				const uint16_t *ptr2 = (const uint16_t *)(srcs[k + 1] + offset);

				__m128i coeffs = weights[k / 2];
				__m128i v1 = _mm_sub_epi16(_mm_load_si128((const __m128i *)(ptr1 + j)), bias);
				__m128i v2 = _mm_sub_epi16(_mm_load_si128((const __m128i *)(ptr2 + j)), bias);

				lo = _mm_add_epi32(lo, _mm_madd_epi16(coeffs, _mm_unpacklo_epi16(v1, v2)));
				hi = _mm_add_epi32(hi, _mm_madd_epi16(coeffs, _mm_unpackhi_epi16(v1, v2)));
			}

			lo = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(lo), scale));
			hi = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(hi), scale));
			lo = _mm_packs_epi32(lo, hi);

			lo = _mm_max_epi16(lo, minval);
			lo = _mm_min_epi16(lo, maxval);
			lo = _mm_add_epi16(lo, bias);

			_mm_store_si128((__m128i *)(dst + j), lo);
		}

		offset += stride;
	}
}

void vs_average_plane_float_sse2(const void *weights_, const void * const *srcs, unsigned num_srcs, void *dst_, const void *scale_, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride)
{
	__m128 weights[32];
	__m128 scale = _mm_set_ps1(1.0f / *(const float *)scale_);
	ptrdiff_t offset = 0;
	unsigned i, j, k;

	assert(num_srcs <= 32);

	for (i = 0; i < num_srcs; ++i) {
		weights[i] = _mm_set_ps1(((const float *)weights_)[i]);
	}

	for (i = 0; i < h; ++i) {
		float *dst = (float *)((uint8_t *)dst_ + offset);

		for (j = 0; j < w; j += 4) {
			__m128 accum = _mm_setzero_ps();

			for (k = 0; k < num_srcs; ++k) {
				const float *ptr = (const float *)((const uint8_t *)srcs[k] + offset);
				__m128 val = _mm_load_ps(ptr + j);
				accum = _mm_add_ps(accum, _mm_mul_ps(val, weights[k]));
			}

			accum = _mm_mul_ps(accum, scale);
			_mm_store_ps(dst + j, accum);
		}

		offset += stride;
	}
}
