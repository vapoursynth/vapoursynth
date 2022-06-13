#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <immintrin.h>
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

FORCE_INLINE __m256 scale_bias_saturate(__m256 x, const __m256 &scale, const __m256 &bias, const __m256 &saturatemask)
{
    return _mm256_and_ps(_mm256_fmadd_ps(x, scale, bias), saturatemask);
}

FORCE_INLINE __m128i export_u8(__m256i i32_lo, __m256i i32_hi, const __m256 &scale, const __m256 &bias, const __m256 &saturatemask)
{
    __m256 tmp;

    tmp = scale_bias_saturate(_mm256_cvtepi32_ps(i32_lo), scale, bias, saturatemask);
    i32_lo = _mm256_cvtps_epi32(tmp);

    tmp = scale_bias_saturate(_mm256_cvtepi32_ps(i32_hi), scale, bias, saturatemask);
    i32_hi = _mm256_cvtps_epi32(tmp);

    i32_lo = _mm256_packs_epi32(i32_lo, i32_hi);
    i32_lo = _mm256_packus_epi16(i32_lo, i32_lo);
    i32_lo = _mm256_permute4x64_epi64(i32_lo, _MM_SHUFFLE(3, 1, 2, 0));
    return _mm256_castsi256_si128(i32_lo);
}

FORCE_INLINE __m256i export_u16(__m256i i32_lo, __m256i i32_hi, const __m256 &scale, const __m256 &bias, const __m256 &saturatemask, const __m256i &maxval)
{
    __m256 tmp;

    tmp = scale_bias_saturate(_mm256_cvtepi32_ps(i32_lo), scale, bias, saturatemask);
    i32_lo = _mm256_cvtps_epi32(tmp);

    tmp = scale_bias_saturate(_mm256_cvtepi32_ps(i32_hi), scale, bias, saturatemask);
    i32_hi = _mm256_cvtps_epi32(tmp);

    i32_lo = _mm256_packus_epi32(i32_lo, i32_hi);
    i32_lo = _mm256_min_epu16(i32_lo, maxval);
    return i32_lo;
}


template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_byte_pass(const uint8_t *src, uint8_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    __m256i w0w1 = _mm256_set1_epi32((weight(1) << 16) | weight(0));
    __m256i w2w3 = _mm256_set1_epi32((weight(3) << 16) | weight(2));
    __m256i w4w5 = _mm256_set1_epi32((weight(5) << 16) | weight(4));
    __m256i w6w7 = _mm256_set1_epi32((weight(7) << 16) | weight(6));
    __m256i w8w9 = _mm256_set1_epi32((weight(9) << 16) | weight(8));
    __m256i w10w11 = _mm256_set1_epi32((weight(11) << 16) | weight(10));
    __m256i w12 = _mm256_set1_epi32(weight(12));

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 16) {
        __m256i accum_lo = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 0));
        __m256i accum_hi = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 8));
        __m256i x0, x1;

        if (N >= 1) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 0)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 1)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 2)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 3)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 4)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 5)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 6)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 7)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 8)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 9)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w8w9));
        }

        if (N >= 11) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 10)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 11)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w10w11));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w10w11));
        }

        if (N >= 13) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(src + j + 12)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x0), w12));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x0), w12));
        }

        if (Last) {
            __m128i result = export_u8(accum_lo, accum_hi, scale, bias, saturatemask);
            _mm_store_si128((__m128i *)(dst + j), result);
        } else {
            _mm256_store_si256((__m256i *)(tmp + j + 0), accum_lo);
            _mm256_store_si256((__m256i *)(tmp + j + 8), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_word_pass(const uint16_t *src, uint16_t *dst, int32_t *tmp, const vs_generic_params &params, unsigned n, int32_t i32bias)
{
    auto weight = [=](unsigned k) -> uint32_t { return k < N ? static_cast<uint16_t>(params.matrix[K + k]) : 0; };

    __m256i w0w1 = _mm256_set1_epi32((weight(1) << 16) | weight(0));
    __m256i w2w3 = _mm256_set1_epi32((weight(3) << 16) | weight(2));
    __m256i w4w5 = _mm256_set1_epi32((weight(5) << 16) | weight(4));
    __m256i w6w7 = _mm256_set1_epi32((weight(7) << 16) | weight(6));
    __m256i w8w9 = _mm256_set1_epi32((weight(9) << 16) | weight(8));
    __m256i w10w11 = _mm256_set1_epi32((weight(11) << 16) | weight(10));
    __m256i w12 = _mm256_set1_epi32(weight(12));
    __m256i w_bias = _mm256_set1_epi32(i32bias);

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    __m256i maxval = _mm256_set1_epi16(params.maxval);

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 16) {
        __m256i accum_lo = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 0));
        __m256i accum_hi = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 8));
        __m256i x0, x1;

        if (N >= 1) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 0)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 1)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 2)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 3)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 4)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 5)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 6)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 7)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 8)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 9)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w8w9));
        }

        if (N >= 11) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 10)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 11)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w10w11));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w10w11));
        }

        if (N >= 13) {
            x0 = _mm256_add_epi16(_mm256_loadu_si256((const __m256i *)(src + j + 12)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x0), w12));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x0), w12));
        }

        if (Last) {
            accum_lo = _mm256_sub_epi32(accum_lo, w_bias);
            accum_hi = _mm256_sub_epi32(accum_hi, w_bias);
            accum_lo = export_u16(accum_lo, accum_hi, scale, bias, saturatemask, maxval);
            _mm256_store_si256((__m256i *)(dst + j), accum_lo);
        } else {
            _mm256_store_si256((__m256i *)(tmp + j + 0), accum_lo);
            _mm256_store_si256((__m256i *)(tmp + j + 8), accum_hi);
        }
    }
}

template <unsigned N, unsigned K, bool First, bool Last>
void conv_scanline_h_float_pass(const float *src, float *dst, const vs_generic_params &params, unsigned n)
{
    auto weight = [=](unsigned k) -> float { return k < N ? params.matrixf[K + k] : 0; };

    __m256 w0 = _mm256_set1_ps(weight(0));
    __m256 w1 = _mm256_set1_ps(weight(1));
    __m256 w2 = _mm256_set1_ps(weight(2));
    __m256 w3 = _mm256_set1_ps(weight(3));
    __m256 w4 = _mm256_set1_ps(weight(4));
    __m256 w5 = _mm256_set1_ps(weight(5));
    __m256 w6 = _mm256_set1_ps(weight(6));
    __m256 w7 = _mm256_set1_ps(weight(7));
    __m256 w8 = _mm256_set1_ps(weight(8));
    __m256 w9 = _mm256_set1_ps(weight(9));

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m256 accum0 = First ? _mm256_setzero_ps() : _mm256_load_ps(dst + j);
        __m256 accum1 = _mm256_setzero_ps();

        if (N >= 1) accum0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 0), w0, accum0);
        if (N >= 2) accum1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 1), w1, accum1);
        if (N >= 3) accum0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 2), w2, accum0);
        if (N >= 4) accum1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 3), w3, accum1);
        if (N >= 5) accum0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 4), w4, accum0);
        if (N >= 6) accum1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 5), w5, accum1);
        if (N >= 7) accum0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 6), w6, accum0);
        if (N >= 8) accum1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 7), w7, accum1);
        if (N >= 9) accum0 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 8), w8, accum0);
        if (N >= 10) accum1 = _mm256_fmadd_ps(_mm256_loadu_ps(src + j + 9), w9, accum1);

        accum0 = _mm256_add_ps(accum0, accum1);
        if (Last) accum0 = scale_bias_saturate(accum0, scale, bias, saturatemask);
        _mm256_store_ps(dst + j, accum0);
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

    __m256i w0w1 = _mm256_set1_epi32((weight(1) << 16) | weight(0));
    __m256i w2w3 = _mm256_set1_epi32((weight(3) << 16) | weight(2));
    __m256i w4w5 = _mm256_set1_epi32((weight(5) << 16) | weight(4));
    __m256i w6w7 = _mm256_set1_epi32((weight(7) << 16) | weight(6));
    __m256i w8w9 = _mm256_set1_epi32((weight(9) << 16) | weight(8));

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 16) {
        __m256i accum_lo = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 0));
        __m256i accum_hi = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 8));
        __m256i x0, x1;

        if (N >= 1) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp0 + j)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp1 + j)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp2 + j)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp3 + j)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp4 + j)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp5 + j)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp6 + j)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp7 + j)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp8 + j)));
            x1 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(srcp9 + j)));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w8w9));
        }

        if (Last) {
            __m128i result = export_u8(accum_lo, accum_hi, scale, bias, saturatemask);
            _mm_store_si128((__m128i *)(dst + j), result);
        } else {
            _mm256_store_si256((__m256i *)(tmp + j + 0), accum_lo);
            _mm256_store_si256((__m256i *)(tmp + j + 8), accum_hi);
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

    __m256i w0w1 = _mm256_set1_epi32((weight(1) << 16) | weight(0));
    __m256i w2w3 = _mm256_set1_epi32((weight(3) << 16) | weight(2));
    __m256i w4w5 = _mm256_set1_epi32((weight(5) << 16) | weight(4));
    __m256i w6w7 = _mm256_set1_epi32((weight(7) << 16) | weight(6));
    __m256i w8w9 = _mm256_set1_epi32((weight(9) << 16) | weight(8));
    __m256i w_bias = _mm256_set1_epi32(i32bias);

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));
    __m256i maxval = _mm256_set1_epi16(params.maxval);

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 16) {
        __m256i accum_lo = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 0));
        __m256i accum_hi = First ? _mm256_setzero_si256() : _mm256_load_si256((const __m256i *)(tmp + j + 8));
        __m256i x0, x1;

        if (N >= 1) {
            x0 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp0 + j)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp1 + j)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w0w1));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w0w1));
        }

        if (N >= 3) {
            x0 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp2 + j)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp3 + j)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w2w3));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w2w3));
        }

        if (N >= 5) {
            x0 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp4 + j)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp5 + j)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w4w5));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w4w5));
        }

        if (N >= 7) {
            x0 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp6 + j)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp7 + j)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w6w7));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w6w7));
        }

        if (N >= 9) {
            x0 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp8 + j)), _mm256_set1_epi16(INT16_MIN));
            x1 = _mm256_add_epi16(_mm256_load_si256((const __m256i *)(srcp9 + j)), _mm256_set1_epi16(INT16_MIN));
            accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(_mm256_unpacklo_epi16(x0, x1), w8w9));
            accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(_mm256_unpackhi_epi16(x0, x1), w8w9));
        }

        if (Last) {
            accum_lo = _mm256_sub_epi32(accum_lo, w_bias);
            accum_hi = _mm256_sub_epi32(accum_hi, w_bias);
            accum_lo = export_u16(accum_lo, accum_hi, scale, bias, saturatemask, maxval);
            _mm256_store_si256((__m256i *)(dst + j), accum_lo);
        } else {
            _mm256_store_si256((__m256i *)(tmp + j + 0), accum_lo);
            _mm256_store_si256((__m256i *)(tmp + j + 8), accum_hi);
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

    __m256 w0 = _mm256_set1_ps(weight(0));
    __m256 w1 = _mm256_set1_ps(weight(1));
    __m256 w2 = _mm256_set1_ps(weight(2));
    __m256 w3 = _mm256_set1_ps(weight(3));
    __m256 w4 = _mm256_set1_ps(weight(4));
    __m256 w5 = _mm256_set1_ps(weight(5));
    __m256 w6 = _mm256_set1_ps(weight(6));
    __m256 w7 = _mm256_set1_ps(weight(7));
    __m256 w8 = _mm256_set1_ps(weight(8));
    __m256 w9 = _mm256_set1_ps(weight(9));

    __m256 scale = _mm256_broadcast_ss(&params.div);
    __m256 bias = _mm256_broadcast_ss(&params.bias);
    __m256 saturatemask = _mm256_castsi256_ps(_mm256_set1_epi32(params.saturate ? 0xFFFFFFFF : 0x7FFFFFFF));

    src = src - static_cast<ptrdiff_t>(params.matrixsize / 2) + K;

    for (ptrdiff_t j = 0; j < static_cast<ptrdiff_t>(n); j += 8) {
        __m256 accum0 = First ? _mm256_setzero_ps() : _mm256_load_ps(dst + j);
        __m256 accum1 = _mm256_setzero_ps();

        if (N >= 1) accum0 = _mm256_fmadd_ps(_mm256_load_ps(srcp0 + j), w0, accum0);
        if (N >= 2) accum1 = _mm256_fmadd_ps(_mm256_load_ps(srcp1 + j), w1, accum1);
        if (N >= 3) accum0 = _mm256_fmadd_ps(_mm256_load_ps(srcp2 + j), w2, accum0);
        if (N >= 4) accum1 = _mm256_fmadd_ps(_mm256_load_ps(srcp3 + j), w3, accum1);
        if (N >= 5) accum0 = _mm256_fmadd_ps(_mm256_load_ps(srcp4 + j), w4, accum0);
        if (N >= 6) accum1 = _mm256_fmadd_ps(_mm256_load_ps(srcp5 + j), w5, accum1);
        if (N >= 7) accum0 = _mm256_fmadd_ps(_mm256_load_ps(srcp6 + j), w6, accum0);
        if (N >= 8) accum1 = _mm256_fmadd_ps(_mm256_load_ps(srcp7 + j), w7, accum1);
        if (N >= 9) accum0 = _mm256_fmadd_ps(_mm256_load_ps(srcp8 + j), w8, accum0);
        if (N >= 10) accum1 = _mm256_fmadd_ps(_mm256_load_ps(srcp9 + j), w9, accum1);

        accum0 = _mm256_add_ps(accum0, accum1);
        if (Last) accum0 = scale_bias_saturate(accum0, scale, bias, saturatemask);
        _mm256_store_ps(dst + j, accum0);
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
    unsigned width_aligned = (width + 15U) & ~15U;

    // Max support = 12. Max buffering = 12 before, 16 window, 12 after.
    alignas(32) T padded[48];

    // Multi-pass threshold = 13.
    auto kernel = select_conv_scanline_h<T>(params.matrixsize);
    void *tmp = (params.matrixsize > 13 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 16) * sizeof(int32_t), 32) : nullptr;

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

template <class T>
void conv_plane_v(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    // Multi-pass threshold = 9.
    auto kernel = select_conv_scanline_v<T>(params.matrixsize);
    void *tmp = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 16) * sizeof(int32_t), 32) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? i - std::min(k - support - dist_from_bottom, i) : i - support + k;
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
    T *tmp1 = vsh::vsh_aligned_malloc<T>((width + 64) * sizeof(T), 32);
    void *tmp2 = (params.matrixsize > 9 && std::is_integral<T>::value) ? vsh::vsh_aligned_malloc((width + 16) * sizeof(int32_t), 32) : nullptr;

    for (unsigned i = 0; i < height; ++i) {
        const void *srcp[25];
        void *dstp = line_ptr(dst, i, dst_stride);

        unsigned dist_from_bottom = height - 1 - i;

        for (unsigned k = 0; k < support; ++k) {
            unsigned row = i < support - k ? std::min(support - k - i, height - 1) : i - support + k;
            srcp[k] = line_ptr(src, row, src_stride);
        }
        for (unsigned k = support; k < fwidth; ++k) {
            unsigned row = dist_from_bottom < k - support ? i - std::min(k - support - dist_from_bottom, i) : i - support + k;
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


void vs_generic_1d_conv_h_byte_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_word_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_float_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_byte_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_word_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_float_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_byte_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_word_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_2d_conv_sep_float_avx2(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_x<float>(src, src_stride, dst, dst_stride, *params, width, height);
}
