#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "sse2neon.h"
#include <VSHelper4.h>
#include "../generic.h"

#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

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

FORCE_INLINE __m128 scale_bias_saturate(__m128 x, const __m128 &scale, const __m128 &bias, const __m128 &saturatemask)
{
    return _mm_and_ps(_mm_add_ps(_mm_mul_ps(x, scale), bias), saturatemask);
}

FORCE_INLINE __m128i export_u8(__m128i i32_lo, __m128i i32_hi, const __m128 &scale, const __m128 &bias, const __m128 &saturatemask)
{
    __m128 tmp;

    tmp = scale_bias_saturate(_mm_cvtepi32_ps(i32_lo), scale, bias, saturatemask);
    i32_lo = _mm_cvtps_epi32(tmp);

    tmp = scale_bias_saturate(_mm_cvtepi32_ps(i32_hi), scale, bias, saturatemask);
    i32_hi = _mm_cvtps_epi32(tmp);

    i32_lo = _mm_packs_epi32(i32_lo, i32_hi);
    i32_lo = _mm_packus_epi16(i32_lo, i32_lo);
    return i32_lo;
}

FORCE_INLINE __m128i export_u16(__m128i i32_lo, __m128i i32_hi, const __m128 &scale, const __m128 &bias, const __m128 &saturatemask, const __m128i &maxval)
{
    __m128 tmp;

    tmp = scale_bias_saturate(_mm_cvtepi32_ps(i32_lo), scale, bias, saturatemask);
    i32_lo = _mm_cvtps_epi32(tmp);
    i32_lo = _mm_add_epi32(i32_lo, _mm_set1_epi32(INT16_MIN));

    tmp = scale_bias_saturate(_mm_cvtepi32_ps(i32_hi), scale, bias, saturatemask);
    i32_hi = _mm_cvtps_epi32(tmp);
    i32_hi = _mm_add_epi32(i32_hi, _mm_set1_epi32(INT16_MIN));

    i32_lo = _mm_packs_epi32(i32_lo, i32_hi);
    i32_lo = _mm_min_epi16(i32_lo, maxval);
    i32_lo = _mm_sub_epi16(i32_lo, _mm_set1_epi16(INT16_MIN));
    return i32_lo;
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
            accum_lo = export_u8(accum_lo, accum_hi, scale, bias, saturatemask);
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
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    __m128i maxval = _mm_set1_epi16(static_cast<int16_t>(static_cast<int32_t>(params.maxval) + INT16_MIN));

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
            accum_lo = _mm_sub_epi32(accum_lo, w_bias);
            accum_hi = _mm_sub_epi32(accum_hi, w_bias);
            accum_lo = export_u16(accum_lo, accum_hi, scale, bias, saturatemask, maxval);
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
        if (Last) accum0 = scale_bias_saturate(accum0, scale, bias, saturatemask);
        _mm_store_ps(dst + j, accum0);
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_byte_pass(const void * const src[], uint8_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    const uint8_t *srcp0 = static_cast<const uint8_t *>(src[K + 0]);
    const uint8_t *srcp1 = N >= 2 ? static_cast<const uint8_t *>(src[K + 1]) : srcp0;
    const uint8_t *srcp2 = N >= 3 ? static_cast<const uint8_t *>(src[K + 2]) : srcp1;
    const uint8_t *srcp3 = N >= 4 ? static_cast<const uint8_t *>(src[K + 3]) : srcp2;
    const uint8_t *srcp4 = N >= 5 ? static_cast<const uint8_t *>(src[K + 4]) : srcp3;
    const uint8_t *srcp5 = N >= 6 ? static_cast<const uint8_t *>(src[K + 5]) : srcp4;
    const uint8_t *srcp6 = N >= 7 ? static_cast<const uint8_t *>(src[K + 6]) : srcp5;
    const uint8_t *srcp7 = N >= 8 ? static_cast<const uint8_t *>(src[K + 7]) : srcp6;
    const uint8_t *srcp8 = N >= 9 ? static_cast<const uint8_t *>(src[K + 8]) : srcp7;
    const uint8_t *srcp9 = N >= 10 ? static_cast<const uint8_t *>(src[K + 9]) : srcp8;

    __m128i w0w1 = _mm_set1_epi32((weight(1) << 16) | weight(0));
    __m128i w2w3 = _mm_set1_epi32((weight(3) << 16) | weight(2));
    __m128i w4w5 = _mm_set1_epi32((weight(5) << 16) | weight(4));
    __m128i w6w7 = _mm_set1_epi32((weight(7) << 16) | weight(6));
    __m128i w8w9 = _mm_set1_epi32((weight(9) << 16) | weight(8));

    __m128 scale = _mm_set_ps1(params.div);
    __m128 bias = _mm_set_ps1(params.bias);
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m128i accum_lo = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 0));
        __m128i accum_hi = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 4));
        __m128i x0, x1;

        if (N >= 1) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp0 + j)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp1 + j)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp2 + j)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp3 + j)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp4 + j)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp5 + j)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp6 + j)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp7 + j)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp8 + j)), _mm_setzero_si128());
            x1 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(srcp9 + j)), _mm_setzero_si128());
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w8w9));
        }

        if (Last) {
            accum_lo = export_u8(accum_lo, accum_hi, scale, bias, saturatemask);
            _mm_storel_epi64((__m128i *)(dst + j), accum_lo);
        } else {
            _mm_store_si128((__m128i *)(tmp + j + 0), accum_lo);
            _mm_store_si128((__m128i *)(tmp + j + 4), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_word_pass(const void * const src[], uint16_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n, int32_t i32bias)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    const uint16_t *srcp0 = static_cast<const uint16_t *>(src[K + 0]);
    const uint16_t *srcp1 = N >= 2 ? static_cast<const uint16_t *>(src[K + 1]) : srcp0;
    const uint16_t *srcp2 = N >= 3 ? static_cast<const uint16_t *>(src[K + 2]) : srcp1;
    const uint16_t *srcp3 = N >= 4 ? static_cast<const uint16_t *>(src[K + 3]) : srcp2;
    const uint16_t *srcp4 = N >= 5 ? static_cast<const uint16_t *>(src[K + 4]) : srcp3;
    const uint16_t *srcp5 = N >= 6 ? static_cast<const uint16_t *>(src[K + 5]) : srcp4;
    const uint16_t *srcp6 = N >= 7 ? static_cast<const uint16_t *>(src[K + 6]) : srcp5;
    const uint16_t *srcp7 = N >= 8 ? static_cast<const uint16_t *>(src[K + 7]) : srcp6;
    const uint16_t *srcp8 = N >= 9 ? static_cast<const uint16_t *>(src[K + 8]) : srcp7;
    const uint16_t *srcp9 = N >= 10 ? static_cast<const uint16_t *>(src[K + 9]) : srcp8;

    __m128i w0w1 = _mm_set1_epi32((weight(1) << 16) | weight(0));
    __m128i w2w3 = _mm_set1_epi32((weight(3) << 16) | weight(2));
    __m128i w4w5 = _mm_set1_epi32((weight(5) << 16) | weight(4));
    __m128i w6w7 = _mm_set1_epi32((weight(7) << 16) | weight(6));
    __m128i w8w9 = _mm_set1_epi32((weight(9) << 16) | weight(8));
    __m128i w_bias = _mm_set1_epi32(i32bias);

    __m128 scale = _mm_set_ps1(params.div);
    __m128 bias = _mm_set_ps1(params.bias);
    __m128 saturatemask = _mm_castsi128_ps(_mm_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    __m128i maxval = _mm_set1_epi16(static_cast<int16_t>(static_cast<int32_t>(params.maxval) + INT16_MIN));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m128i accum_lo = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 0));
        __m128i accum_hi = First ? _mm_setzero_si128() : _mm_load_si128((const __m128i *)(tmp + j + 4));
        __m128i x0, x1;

        if (N >= 1) {
            x0 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp0 + j)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp1 + j)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp2 + j)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp3 + j)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp4 + j)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp5 + j)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp6 + j)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp7 + j)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp8 + j)), _mm_set1_epi16(INT16_MIN));
            x1 = _mm_add_epi16(_mm_load_si128((const __m128i *)(srcp9 + j)), _mm_set1_epi16(INT16_MIN));
            accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(_mm_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(_mm_unpackhi_epi16(x0, x1), w8w9));
        }

        if (Last) {
            accum_lo = _mm_sub_epi32(accum_lo, w_bias);
            accum_hi = _mm_sub_epi32(accum_hi, w_bias);
            accum_lo = export_u16(accum_lo, accum_hi, scale, bias, saturatemask, maxval);
            _mm_store_si128((__m128i *)(dst + j), accum_lo);
        } else {
            _mm_store_si128((__m128i *)(tmp + j + 0), accum_lo);
            _mm_store_si128((__m128i *)(tmp + j + 4), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_v_float_pass(const void * const src[], float *dst, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> float { return k < N ? params.matrixf[K + k] : 0; };

    const float *srcp0 = static_cast<const float *>(src[K + 0]);
    const float *srcp1 = N >= 2 ? static_cast<const float *>(src[K + 1]) : srcp0;
    const float *srcp2 = N >= 3 ? static_cast<const float *>(src[K + 2]) : srcp1;
    const float *srcp3 = N >= 4 ? static_cast<const float *>(src[K + 3]) : srcp2;
    const float *srcp4 = N >= 5 ? static_cast<const float *>(src[K + 4]) : srcp3;
    const float *srcp5 = N >= 6 ? static_cast<const float *>(src[K + 5]) : srcp4;
    const float *srcp6 = N >= 7 ? static_cast<const float *>(src[K + 6]) : srcp5;
    const float *srcp7 = N >= 8 ? static_cast<const float *>(src[K + 7]) : srcp6;
    const float *srcp8 = N >= 9 ? static_cast<const float *>(src[K + 8]) : srcp7;
    const float *srcp9 = N >= 10 ? static_cast<const float *>(src[K + 9]) : srcp8;

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

        if (N >= 1) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w0, _mm_load_ps(srcp0 + j)));
        if (N >= 2) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w1, _mm_load_ps(srcp1 + j)));
        if (N >= 3) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w2, _mm_load_ps(srcp2 + j)));
        if (N >= 4) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w3, _mm_load_ps(srcp3 + j)));
        if (N >= 5) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w4, _mm_load_ps(srcp4 + j)));
        if (N >= 6) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w5, _mm_load_ps(srcp5 + j)));
        if (N >= 7) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w6, _mm_load_ps(srcp6 + j)));
        if (N >= 8) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w7, _mm_load_ps(srcp7 + j)));
        if (N >= 9) accum0 = _mm_add_ps(accum0, _mm_mul_ps(w8, _mm_load_ps(srcp8 + j)));
        if (N >= 10) accum1 = _mm_add_ps(accum1, _mm_mul_ps(w9, _mm_load_ps(srcp9 + j)));

        accum0 = _mm_add_ps(accum0, accum1);
        if (Last) accum0 = scale_bias_saturate(accum0, scale, bias, saturatemask);
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

template <unsigned N>
void conv_scanline_v_byte(const void * const src[], void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    uint8_t *dstp = static_cast<uint8_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);

    if (N > 19) {
        conv_scanline_v_byte_pass<10, 0, true, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<10, 10, false, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<N - 20, 20, false, true>(src, dstp, tmpp, params, n);
    } else if (N > 9) {
        conv_scanline_v_byte_pass<10, 0, true, false>(src, dstp, tmpp, params, n);
        conv_scanline_v_byte_pass<N - 10, 10, false, true>(src, dstp, tmpp, params, n);
    } else {
        conv_scanline_v_byte_pass<N, 0, true, true>(src, dstp, tmpp, params, n);
    }
}

template <unsigned N>
void conv_scanline_v_word(const void * const src[], void *dst, void *tmp, const vs_generic_params &params, unsigned n)
{
    uint16_t *dstp = static_cast<uint16_t *>(dst);
    int32_t *tmpp = static_cast<int32_t *>(tmp);
    int32_t bias = 0;

    for (unsigned k = 0; k < N; ++k) {
        bias += static_cast<int32_t>(INT16_MIN) * params.matrix[k];
    }

    if (N > 19) {
        conv_scanline_v_word_pass<10, 0, true, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<10, 10, false, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<N - 20, 20, false, true>(src, dstp, tmpp, params, n, bias);
    } else if (N > 9) {
        conv_scanline_v_word_pass<10, 0, true, false>(src, dstp, tmpp, params, n, bias);
        conv_scanline_v_word_pass<N - 10, 10, false, true>(src, dstp, tmpp, params, n, bias);
    } else {
        conv_scanline_v_word_pass<N, 0, true, true>(src, dstp, tmpp, params, n, bias);
    }
}

template <unsigned N>
void conv_scanline_v_float(const void * const src[], void *dst, void *, const vs_generic_params &params, unsigned n)
{
    float *dstp = static_cast<float *>(dst);

    if (N > 19) {
        conv_scanline_v_float_pass<10, 0, true, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<10, 10, false, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<N - 20, 20, false, true>(src, dstp, params, n);
    } else if (N > 9) {
        conv_scanline_v_float_pass<10, 0, true, false>(src, dstp, params, n);
        conv_scanline_v_float_pass<N - 10, 10, false, true>(src, dstp, params, n);
    } else {
        conv_scanline_v_float_pass<N, 0, true, true>(src, dstp, params, n);
    }
}

template <class T>
decltype(&conv_scanline_h_byte<0>) select_conv_scanline_h(unsigned fwidth);

template <class T>
decltype(&conv_scanline_v_byte<0>) select_conv_scanline_v(unsigned fwidth);

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
SELECT(v, byte, uint8_t)
SELECT(v, word, uint16_t)
SELECT(v, float, float)

#undef SELECT

template <class T>
void conv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned vec_end = std::max((std::max(width, 13U) - 13) & ~15U, 16U);

    // Max support = 12. Max buffering = 12 before, 32 window, 12 after.
    alignas(32) T padded[64];

    // Multi-pass threshold = 13.
    auto kernel = select_conv_scanline_h<T>(params.matrixsize);
    void *tmp = (params.matrixsize > 13 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 8) * sizeof(int32_t), 16) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = static_cast<const T *>(line_ptr(src, i, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        // Pixels 0...15.
        {
            // Center.
            std::copy_n(srcp, std::min(width, 28U), padded + 16);
            flip_left(padded + 16, 12);
            if (width < 28U)
                flip_right(padded + 16 + width, std::min(28U - width, 12U));
            kernel(padded + 16, dstp, tmp, params, 16U);
        }

        // Pixels 16...vec_end-1.
        if (vec_end > 16U) {
            kernel(srcp + 16, dstp + 16, tmp, params, vec_end - 16U);
        }

        // Pixels vec_end...N-1
        if (width > 16U) {
            std::copy_n(srcp + vec_end - 12U, width - vec_end + 12U, padded + 4U);
            flip_right(padded + 16U + (width - vec_end), 12U);
            kernel(padded + 16, dstp + vec_end, tmp, params, width - vec_end);
        }
    }

    vsh::vsh_aligned_free(tmp);
}

template <class T>
void conv_plane_v(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    // Multi-pass threshold = 9.
    auto kernel = select_conv_scanline_v<T>(params.matrixsize);
    void *tmp = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 8) * sizeof(int32_t), 16) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }

        kernel(srcp, dstp, tmp, params, width);
    }

    vsh::vsh_aligned_free(tmp);
}

template <class T>
void conv_plane_x(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    // Multi-pass threshold = 9 (v), 13 (h).
    auto kernel_v = select_conv_scanline_v<T>(params.matrixsize);
    auto kernel_h = select_conv_scanline_h<T>(params.matrixsize);
    T *tmp1 = vsh::vsh_aligned_malloc<T>((width + 64) * sizeof(T), 16);
    void *tmp2 = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 8) * sizeof(int32_t), 16) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i - 1, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? height - std::min(k - support - dist_from_bottom, i) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }

        kernel_v(srcp, tmp1 + 32, tmp2, params, width);
        flip_left(tmp1 + 32, 12);
        flip_right(tmp1 + 32 + width, 12);
        kernel_h(tmp1 + 32, dstp, tmp2, params, width);
    }

    vsh::vsh_aligned_free(tmp2);
    vsh::vsh_aligned_free(tmp1);
}

} // namespace


void vs_generic_1d_conv_h_byte_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_word_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_float_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_byte_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_word_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_float_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_byte_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_word_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_float_neon(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<float>(src, src_stride, dst, dst_stride, *params, width, height);
}
