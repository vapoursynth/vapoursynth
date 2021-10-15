#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <emmintrin.h>
#include <VSHelper4.h>
#include "../generic.h"

namespace {

template <class T>
T *line_ptr(T *ptr, unsigned i, ptrdiff_t stride)
{
    return (T *)(((unsigned char *)ptr) + static_cast<ptrdiff_t>(i) * stride);
}

template <class T>
void flip_left(T *ptr, unsigned n)
{
    for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
        ptr[-1 - i] = ptr[i];
    }
}

template <class T>
void flip_right(T *ptr, unsigned n)
{
    for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
        ptr[i] = ptr[-1 - i];
    }
}


template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_byte_pass(const uint8_t *src, uint8_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    __m128i w0w1 = _mm_set1_epi32((weight(1) << 16) | weight(0));
    __m128i w2w3 = _mm_set1_epi32((weight(3) << 16) | weight(2));
    __m128i w4w5 = _mm_set1_epi32((weight(5) << 16) | weight(4));
    __m128i w6w7 = _mm_set1_epi32((weight(7) << 16) | weight(6));
    __m128i w8w9 = _mm_set1_epi32((weight(9) << 16) | weight(8));
    __m128i w10w11 = _mm_set1_epi32((weight(11) << 16) | weight(10));
    __m128i w12 = _mm_set1_epi32(weight(12));

    __m128 scale = _mm_set_ps1(params.div);
    __m128 bias = _mm_set_ps1(params.bias);
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m128i accum_lo = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 0));
        __m128i accum_hi = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 4));
        __m128i x0, x1;

        if (N >= 1) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 0)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 1)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 2)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 3)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 4)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 5)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 6)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 7)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 8)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 9)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w8w9));
        }

        if (N >= 11) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 10)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 11)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w10w11));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w10w11));
        }

        if (N >= 13) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + j + 12)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x0), w12));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x0), w12));
        }

        if (Last) {
            __m128 accum_lo_f, accum_hi_f;

            accum_lo_f = _mm_cvtepi32_ps(accum_lo);
            accum_lo_f = _mm_add_ps(_mm_mul_ps(accum_lo_f, scale), bias);
            accum_lo_f = _mm_and_ps(accum_lo_f, saturatemask);
            accum_lo = _mm_cvtps_epi32(accum_lo_f);

            accum_hi_f = _mm_cvtepi32_ps(accum_hi);
            accum_hi_f = _mm_add_ps(_mm_mul_ps(accum_hi_f, scale), bias);
            accum_hi_f = _mm_and_ps(accum_hi_f, saturatemask);
            accum_hi = _mm_cvtps_epi32(accum_hi_f);

            accum_lo = _mm_packs_epi32(accum_lo, accum_hi);
            accum_lo = _mm_packus_epi16(accum_lo, accum_lo);
            _mm_storel_epi64((__m128i *)(dst + j), accum_lo);
        } else {
            _mm_store_si128((__m128i *)(tmp + j + 0), accum_lo);
            _mm_store_si128((__m128i *)(tmp + j + 4), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_word_pass(const uint16_t *src, uint16_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n, int32_t i32bias)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    __m128i w0w1 = _mm_set1_epi32((weight(1) << 16) | weight(0));
    __m128i w2w3 = _mm_set1_epi32((weight(3) << 16) | weight(2));
    __m128i w4w5 = _mm_set1_epi32((weight(5) << 16) | weight(4));
    __m128i w6w7 = _mm_set1_epi32((weight(7) << 16) | weight(6));
    __m128i w8w9 = _mm_set1_epi32((weight(9) << 16) | weight(8));
    __m128i w10w11 = _mm_set1_epi32((weight(11) << 16) | weight(10));
    __m128i w12 = _mm_set1_epi32(weight(12));
    __m128i w_bias = _mm_set1_epi32(i32bias);

    __m128 scale = _mm_set_ps1(params.div);
    __m128 bias = _mm_set_ps1(params.bias);
    __m128 maxval = _mm_set_ps1(params.maxval);
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m128i accum_lo = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 0));
        __m128i accum_hi = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 4));
        __m128i x0, x1;

        if (N >= 1) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 0)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 1)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 2)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 3)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 4)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 5)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 6)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 7)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 8)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 9)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w8w9));
        }

        if (N >= 11) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 10)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 11)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w10w11));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w10w11));
        }

        if (N >= 13) {
            x0 = _mm_add_epi16(_mm_loadu_si128((const __m128i *)(src + j + 12)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x0), w12));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x0), w12));
        }

        if (Last) {
            __m128 accum_lo_f, accum_hi_f;

            accum_lo = _mm_sub_epi32(accum_lo, w_bias);
            accum_lo_f = _mm_cvtepi32_ps(accum_lo);
            accum_lo_f = _mm_add_ps(_mm_mul_ps(accum_lo_f, scale), bias);
            accum_lo_f = _mm_and_ps(accum_lo_f, saturatemask);
            accum_lo_f = _mm_min_ps(accum_lo_f, maxval);
            accum_lo = _mm_cvtps_epi32(accum_lo_f);
            accum_lo = _mm_add_epi32(accum_lo, _mm_set1_epi32(INT16_MIN));

            accum_hi = _mm_sub_epi32(accum_hi, w_bias);
            accum_hi_f = _mm_cvtepi32_ps(accum_hi);
            accum_hi_f = _mm_add_ps(_mm_mul_ps(accum_hi_f, scale), bias);
            accum_hi_f = _mm_and_ps(accum_hi_f, saturatemask);
            accum_hi_f = _mm_min_ps(accum_hi_f, maxval);
            accum_hi = _mm_cvtps_epi32(accum_hi_f);
            accum_hi = _mm_add_epi32(accum_hi, _mm_set1_epi32(INT16_MIN));

            accum_lo = _mm_packs_epi32(accum_lo, accum_hi);
            accum_lo = _mm_sub_epi16(accum_lo, _mm_set1_epi16(INT16_MIN));
            _mm_store_si128((__m128i *)(dst + j), accum_lo);
        } else {
            _mm_store_si128((__m128i *)(tmp + j + 0), accum_lo);
            _mm_store_si128((__m128i *)(tmp + j + 4), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_float_pass(const float *src, float *dst, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> float { return k < N ? params.matrixf[K + k] : 0; };

    __m128 w0 = _mm_set_ps1(weight(0));
    __m128 w1 = _mm_set_ps1(weight(1));
    __m128 w2 = _mm_set_ps1(weight(2));
    __m128 w3 = _mm_set_ps1(weight(3));
    __m128 w4 = _mm_set_ps1(weight(4));
    __m128 w5 = _mm_set_ps1(weight(5));
    __m128 w6 = _mm_set_ps1(weight(6));
    __m128 w7 = _mm_set_ps1(weight(7));
    __m128 w8 = _mm_set_ps1(weight(8));
    __m128 w9 = _mm_set_ps1(weight(9));

    __m128 scale = _mm_set_ps1(params.div);
    __m128 bias = _mm_set_ps1(params.bias);
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 4) {
        __m128 accum0 = First ? _mm_setzero_ps() : _mm_load_ps(dst + j);
        __m128 accum1 = _mm_setzero_ps();

        if (N >= 1) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w0, _mm_loadu_ps(src + j + 0)));
        if (N >= 2) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w1, _mm_loadu_ps(src + j + 1)));
        if (N >= 3) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w2, _mm_loadu_ps(src + j + 2)));
        if (N >= 4) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w3, _mm_loadu_ps(src + j + 3)));
        if (N >= 5) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w4, _mm_loadu_ps(src + j + 4)));
        if (N >= 6) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w5, _mm_loadu_ps(src + j + 5)));
        if (N >= 7) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w6, _mm_loadu_ps(src + j + 6)));
        if (N >= 8) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w7, _mm_loadu_ps(src + j + 7)));
        if (N >= 9) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w8, _mm_loadu_ps(src + j + 8)));
        if (N >= 10) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w9, _mm_loadu_ps(src + j + 9)));

        accum0 = _mm_add_ps(accum0, accum1);

        if (Last) {
            accum0 = _mm_add_ps(_mm_mul_ps(accum0, scale), bias);
            accum0 = _mm_and_ps(accum0, saturatemask);
        }

        _mm_store_ps(dst + j, accum0);
    }
}

template <unsigned N>
void conv_scanline_h_byte(const void *src, void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    const uint8_t *srcp = static_cast<const uint8_t *>(src);
    uint8_t *dstp = static_cast<uint8_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);

    if (N > 13) {
        conv_scanline_h_byte_pass<12, 0, true, false>(srcp, dstp, tmpp, params, n);
        conv_scanline_h_byte_pass<N - 12, 12, false, true>(srcp, dstp, tmpp, params, n);
    } else {
        conv_scanline_h_byte_pass<N, 0, true, true>(srcp, dstp, nullptr, params, n);
    }
}

template <unsigned N>
void conv_scanline_h_word(const void *src, void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    const uint16_t *srcp = static_cast<const uint16_t *>(src);
    uint16_t *dstp = static_cast<uint16_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);
    int32_t bias = 0;

    for (unsigned k = 0; k < N; ++k) {
        bias += static_cast<int32_t>(INT16_MIN) * params.matrix[k];
    }

    if (N > 13) {
        conv_scanline_h_word_pass<12, 0, true, false>(srcp, dstp, tmpp, params, n, bias);
        conv_scanline_h_word_pass<N - 12, 12, false, true>(srcp, dstp, tmpp, params, n, bias);
    } else {
        conv_scanline_h_word_pass<N, 0, true, true>(srcp, dstp, nullptr, params, n, bias);
    }
}

template <unsigned N>
void conv_scanline_h_float(const void *src, void *dst, void *, const vs_generic_params &params, unsigned n)
{
    const float *srcp = static_cast<const float *>(src);
    float *dstp = static_cast<float *>(dst);

    if (N > 19) {
        conv_scanline_h_float_pass<10, 0, true, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<10, 10, false, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<N - 20, 20, false, true>(srcp, dstp, params, n);
    } else if (N > 9) {
        conv_scanline_h_float_pass<10, 0, true, false>(srcp, dstp, params, n);
        conv_scanline_h_float_pass<N - 10, 10, false, true>(srcp, dstp, params, n);
    } else {
        conv_scanline_h_float_pass<N, 0, true, true>(srcp, dstp, params, n);
    }
}

template <class T>
decltype(&conv_scanline_h_byte<0>) select_conv_scanline_h(unsigned fwidth);

#define SELECT(dir, type, T) \
template <> \
decltype(&conv_scanline_##dir##_##type<0>) select_conv_scanline_##dir<T>(unsigned fwidth) \
{ \
  switch (fwidth) { \
  case 3: return conv_scanline_##dir##_##type<3>; \
  case 5: return conv_scanline_##dir##_##type<5>; \
  case 7: return conv_scanline_##dir##_##type<7>; \
  case 9: return conv_scanline_##dir##_##type<9>; \
  case 11: return conv_scanline_##dir##_##type<11>; \
  case 13: return conv_scanline_##dir##_##type<13>; \
  case 15: return conv_scanline_##dir##_##type<15>; \
  case 17: return conv_scanline_##dir##_##type<17>; \
  case 19: return conv_scanline_##dir##_##type<19>; \
  case 21: return conv_scanline_##dir##_##type<21>; \
  case 23: return conv_scanline_##dir##_##type<23>; \
  case 25: return conv_scanline_##dir##_##type<25>; \
  default: return nullptr; \
  } \
}

SELECT(h, byte, uint8_t)
SELECT(h, word, uint16_t)
SELECT(h, float, float)

#undef SELECT

template <class T>
void conv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned width_aligned = (width + 15U) & ~15U;
    ptrdiff_t support = params.matrixsize / 2;

    // Max support = 12. Max buffering = 12 before, 16 window, 12 after.
    alignas(16) T padded[48];

    // Multi-pass threshold = 13.
    auto kernel = select_conv_scanline_h<T>(params.matrixsize);
    void *tmp = (params.matrixsize > 13 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc(width * sizeof(int32_t), 16) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = static_cast<const T *>(line_ptr(src, i, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        // Pixels 0...15.
        {
            // Center.
            std::copy_n(srcp, std::min(width_aligned, 32U), padded + 16);
            flip_left(padded + 16, 12);
            if (width < 28U)
                flip_right(padded + 16 + width, std::min(28U - width, 12U));
            kernel(padded + 16, dstp, tmp, params, 16U);
        }

        // Pixels 16...N-16-1.
        if (width_aligned >= 32U) {
            kernel(srcp + 16, dstp + 16, tmp, params, width_aligned - 32);
        }

        // Pixels N-16...N-1
        if (width_aligned >= 32U) {
            std::copy_n(srcp + width_aligned - 32, 32, padded);
            flip_right(padded + 16 + (width - (width_aligned - 16)), 12U);
            kernel(padded + 16, dstp + width_aligned - 16, tmp, params, width - (width_aligned - 16));
        }
    }

    vsh::vsh_aligned_free(tmp);
}

} // namespace


void vs_generic_1d_conv_h_byte_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_word_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_float_sse2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<float>(src, src_stride, dst, dst_stride, *params, width, height);
}
