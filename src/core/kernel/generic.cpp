#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include "generic.h"

namespace {

template <class T>
T limit(T x, uint16_t maxval)
{
    return static_cast<T>(std::min(static_cast<uint16_t>(x), maxval));
}

template <>
float limit(float x, uint16_t) { return x; }

template <class T>
T xrint(float x)
{
    return static_cast<T>(std::lrint(std::min(x, static_cast<float>(std::numeric_limits<T>::max()))));
}

template <>
float xrint(float x) { return x; }

template <class T>
T *line_ptr(T *ptr, unsigned i, ptrdiff_t stride)
{
    return (T *)(((unsigned char *)ptr) + static_cast<ptrdiff_t>(i) * stride);
}


template <class T, bool Sobel>
struct PrewittSobelOp {
    typedef T Ty;

    float scale;

    explicit PrewittSobelOp(const vs_generic_params &params) : scale{ params.scale } {}

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;
        constexpr Signed two = 2;

        Signed gx, gy;

        if (Sobel) {
            gx = static_cast<Signed>(a20) + two * a21 + a22 - a00 - two * a01 - a02;
            gy = static_cast<Signed>(a02) + two * a12 + a22 - a00 - two * a10 - a20;
        } else {
            gx = static_cast<Signed>(a20) + a21 + a22 - a00 - a01 - a02;
            gy = static_cast<Signed>(a02) + a12 + a22 - a00 - a10 - a20;
        }

        float tmp = std::sqrt(static_cast<float>(gx * gx + gy * gy)) * scale;
        return xrint<T>(tmp);
    }
};

template <class T, bool Max>
struct MinMaxOp {
    typedef T Ty;

    typename std::conditional<std::is_integral<T>::value, int32_t, float>::type threshold;
    uint8_t stencil[8];

    explicit MinMaxOp(const vs_generic_params &params) :
        threshold{ std::is_integral<T>::value ? static_cast<T>(params.threshold) : static_cast<T>(params.thresholdf) },
        stencil{}
    {
        for (unsigned i = 0; i < 8; ++i) {
            stencil[i] = (params.stencil & (1U << i)) ? 0xFF : 0;
        }
    }

    static T reduce(T lhs, T rhs)
    {
        return Max ? std::max(lhs, rhs) : std::min(lhs, rhs);
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        T val = a11;
        val = reduce(val, stencil[0] ? a00 : val);
        val = reduce(val, stencil[1] ? a01 : val);
        val = reduce(val, stencil[2] ? a02 : val);
        val = reduce(val, stencil[3] ? a10 : val);
        val = reduce(val, stencil[4] ? a12 : val);
        val = reduce(val, stencil[5] ? a20 : val);
        val = reduce(val, stencil[6] ? a21 : val);
        val = reduce(val, stencil[7] ? a22 : val);

        Signed minval = std::is_integral<T>::value ? Signed{} : static_cast<Signed>(-INFINITY);
        Signed limit = Max ? static_cast<Signed>(a11) + threshold : std::max(static_cast<Signed>(a11) - threshold, minval);
        return Max ? std::min(static_cast<Signed>(val), limit) : std::max(static_cast<Signed>(val), limit);
    }
};

template <class T>
struct MedianOp {
    typedef T Ty;

    explicit MedianOp(const vs_generic_params &) {}

    static void compare_exchange(T &lhs, T &rhs)
    {
        T x = lhs;
        T y = rhs;
        lhs = std::min(x, y);
        rhs = std::max(x, y);
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        compare_exchange(a00, a01);
        compare_exchange(a02, a10);
        compare_exchange(a12, a20);
        compare_exchange(a21, a22);

        compare_exchange(a00, a02);
        compare_exchange(a01, a10);
        compare_exchange(a12, a21);
        compare_exchange(a20, a22);

        compare_exchange(a01, a02);
        compare_exchange(a20, a21);

        compare_exchange(a00, a12);
        compare_exchange(a01, a20);
        compare_exchange(a02, a21);
        compare_exchange(a10, a22);

        compare_exchange(a02, a12);
        compare_exchange(a10, a20);

        compare_exchange(a10, a12);

        compare_exchange(a10, a11);
        compare_exchange(a11, a12);
        return a11;
    }
};

template <class T, bool Inflate>
struct DeflateInflateOp {
    typedef T Ty;

    typename std::conditional<std::is_integral<T>::value, int32_t, float>::type threshold;

    explicit DeflateInflateOp(const vs_generic_params &params) :
        threshold{ std::is_integral<T>::value ? static_cast<T>(params.threshold) : static_cast<T>(params.thresholdf) }
    {}

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, uint32_t, float>::type U;
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        U accum = static_cast<U>(a00) + a01 + a02 + a10 + a12 + a20 + a21 + a22;
        accum = std::is_integral<T>::value ? accum + 4 : accum;

        Signed val = static_cast<Signed>(accum / 8);
        val = Inflate ? std::max(val, static_cast<Signed>(a11)) : std::min(val, static_cast<Signed>(a11));

        Signed minval = std::is_integral<T>::value ? Signed{} : static_cast<Signed>(-INFINITY);
        Signed limit = Inflate ? static_cast<Signed>(a11) + threshold : std::max(static_cast<Signed>(a11) - threshold, minval);
        return Inflate ? std::min(val, limit) : std::max(val, limit);
    }
};

template <class T>
struct ConvolutionOp {
    typedef T Ty;

    std::array<typename std::conditional<std::is_integral<T>::value, int16_t, float>::type, 9> coeffs;
    float div;
    float bias;
    uint8_t saturate;

    explicit ConvolutionOp(const vs_generic_params &params) :
        coeffs{},
        div{ params.div },
        bias{ params.bias },
        saturate{ params.saturate }
    {
        for (unsigned i = 0; i < 9; ++i) {
            coeffs[i] = std::is_integral<T>::value ? static_cast<T>(params.matrix[i]) : static_cast<T>(params.matrixf[i]);
        }
    }

    T op(T a00, T a01, T a02, T a10, T a11, T a12, T a20, T a21, T a22) const
    {
        typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Signed;

        Signed accum = coeffs[0] * a00;
        accum += coeffs[1] * a01;
        accum += coeffs[2] * a02;
        accum += coeffs[3] * a10;
        accum += coeffs[4] * a11;
        accum += coeffs[5] * a12;
        accum += coeffs[6] * a20;
        accum += coeffs[7] * a21;
        accum += coeffs[8] * a22;

        float tmp = static_cast<float>(accum) * div + bias;
        tmp = saturate ? tmp : std::fabs(tmp);
        return xrint<T>(tmp);
    }
};


template <class Traits>
void filter_plane_3x3(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename Traits::Ty T;

    Traits traits{ params };
    uint16_t maxval = params.maxval;

    for (unsigned i = 0; i < height; ++i) {
        unsigned above_idx = i == 0 ? std::min(1U, height - 1) : i - 1;
        unsigned below_idx = i == height - 1 ? height - std::min(2U, height) : i + 1;

        const T *srcp0 = static_cast<const T *>(line_ptr(src, above_idx, src_stride));
        const T *srcp1 = static_cast<const T *>(line_ptr(src, i, src_stride));
        const T *srcp2 = static_cast<const T *>(line_ptr(src, below_idx, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        {
            unsigned a = width > 1 ? 1 : 0;
            unsigned b = 0;
            unsigned c = width > 1 ? 1 : 0;

            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[0] = limit(x, maxval);
        }

        for (unsigned i = 1; i < width - 1; ++i) {
            unsigned a = i - 1;
            unsigned b = i;
            unsigned c = i + 1;

            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[i] = limit(x, maxval);
        }

        if (width > 1) {
            unsigned a = width - 2;
            unsigned b = width - 1;
            unsigned c = width - 2;
            T x = traits.op(srcp0[a], srcp0[b], srcp0[c], srcp1[a], srcp1[b], srcp1[c], srcp2[a], srcp2[b], srcp2[c]);
            dstp[width - 1] = limit(x, maxval);
        }
    }
}

template <class T>
void conv_plane_5x5(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    for (unsigned i = 0; i < height; ++i) {
        unsigned dist_from_bottom = height - 1 - i;

        unsigned above2_idx = i < 2 ? std::min(2 - i, height - 1) : i - 2;
        unsigned above1_idx = i < 1 ? std::min(1 - i, height - 1) : i - 1;
        unsigned below1_idx = dist_from_bottom < 1 ? i - std::min(1 - dist_from_bottom, i) : i + 1;
        unsigned below2_idx = dist_from_bottom < 2 ? i - std::min(2 - dist_from_bottom, i) : i + 2;

        const T *srcp0 = static_cast<const T *>(line_ptr(src, above2_idx, src_stride));
        const T *srcp1 = static_cast<const T *>(line_ptr(src, above1_idx, src_stride));
        const T *srcp2 = static_cast<const T *>(line_ptr(src, i, src_stride));
        const T *srcp3 = static_cast<const T *>(line_ptr(src, below1_idx, src_stride));
        const T *srcp4 = static_cast<const T *>(line_ptr(src, below2_idx, src_stride));
        T *dst_p = static_cast<T *>(line_ptr(dst, i, dst_stride));

        for (unsigned j = 0; j < std::min(width, 2U); ++j) {
            unsigned dist_from_right = width - 1 - i;
            unsigned idx[5];

            idx[0] = j < 2 ? std::min(2 - j, width - 1) : j - 2;
            idx[1] = j < 1 ? std::min(1 - j, width - 1) : j - 1;
            idx[2] = j;
            idx[3] = dist_from_right < 1 ? j - std::min(1 - dist_from_right, j) : j + 1;
            idx[4] = dist_from_right < 2 ? j - std::min(2 - dist_from_right, j) : j + 2;

            Accum accum = 0;

            for (unsigned k = 0; k < 5; ++k) {
                accum += coeffs[5 * 0 + k] * static_cast<Accum>(srcp0[idx[k]]);
                accum += coeffs[5 * 1 + k] * static_cast<Accum>(srcp1[idx[k]]);
                accum += coeffs[5 * 2 + k] * static_cast<Accum>(srcp2[idx[k]]);
                accum += coeffs[5 * 3 + k] * static_cast<Accum>(srcp3[idx[k]]);
                accum += coeffs[5 * 4 + k] * static_cast<Accum>(srcp4[idx[k]]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dst_p[j] = limit(xrint<T>(tmp), maxval);
        }

        for (unsigned j = 2; j < width - std::min(width, 2U); ++j) {
            Accum accum = 0;

            for (unsigned k = 0; k < 5; ++k) {
                accum += coeffs[5 * 0 + k] * static_cast<Accum>(srcp0[j - 2 + k]);
                accum += coeffs[5 * 1 + k] * static_cast<Accum>(srcp1[j - 2 + k]);
                accum += coeffs[5 * 2 + k] * static_cast<Accum>(srcp2[j - 2 + k]);
                accum += coeffs[5 * 3 + k] * static_cast<Accum>(srcp3[j - 2 + k]);
                accum += coeffs[5 * 4 + k] * static_cast<Accum>(srcp4[j - 2 + k]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dst_p[j] = limit(xrint<T>(tmp), maxval);
        }

        for (unsigned j = std::max(2U, width - std::min(width, 2U)); j < width; ++j) {
            unsigned dist_from_right = width - 1 - i;
            unsigned idx[5];

            idx[0] = j < 2 ? std::min(2 - j, width - 1) : j - 2;
            idx[1] = j < 1 ? std::min(1 - j, width - 1) : j - 1;
            idx[2] = j;
            idx[3] = dist_from_right < 1 ? j - std::min(1 - dist_from_right, j) : j + 1;
            idx[4] = dist_from_right < 2 ? j - std::min(2 - dist_from_right, j) : j + 2;

            Accum accum = 0;

            for (unsigned k = 0; k < 5; ++k) {
                accum += coeffs[5 * 0 + k] * static_cast<Accum>(srcp0[idx[k]]);
                accum += coeffs[5 * 1 + k] * static_cast<Accum>(srcp1[idx[k]]);
                accum += coeffs[5 * 2 + k] * static_cast<Accum>(srcp2[idx[k]]);
                accum += coeffs[5 * 3 + k] * static_cast<Accum>(srcp3[idx[k]]);
                accum += coeffs[5 * 4 + k] * static_cast<Accum>(srcp4[idx[k]]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dst_p[j] = limit(xrint<T>(tmp), maxval);
        }
    }
}

template <class T>
void conv_plane_h(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    for (unsigned i = 0; i < height; ++i) {
        const T *srcp = static_cast<const T * >(line_ptr(src, i, src_stride));
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        for (unsigned j = 0; j < std::min(width, support); ++j) {
            unsigned dist_from_right = width - 1 - i;

            Accum accum = 0;

            for (unsigned k = 0; k < support; ++k) {
                unsigned idx = j < support - k ? std::min(support - k - j, width - 1) : j - support + k;
                accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
            }
            for (unsigned k = support; k < fwidth; ++k) {
                unsigned idx = dist_from_right < k - support ? j - std::min(k - support - dist_from_right, j) : j - support + k;
                accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);
        }

        for (unsigned j = support; j < width - std::min(width, support); ++j) {
            Accum accum = 0;

            for (unsigned k = 0; k < fwidth; ++k) {
                accum += coeffs[k] * static_cast<Accum>(srcp[j - support + k]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);

        }

        for (unsigned j = std::max(support, width - std::min(width, support)); j < width; ++j) {
            unsigned dist_from_right = width - 1 - i;

            Accum accum = 0;

            for (unsigned k = 0; k < support; ++k) {
                unsigned idx = j < support - k ? std::min(support - k - j, width - 1) : j - support + k;
                accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
            }
            for (unsigned k = support; k < fwidth; ++k) {
                unsigned idx = dist_from_right < k - support ? j - std::min(k - support - dist_from_right, j) : j - support + k;
                accum += coeffs[k] * static_cast<Accum>(srcp[idx]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);
        }
    }
}

template <class T>
void conv_plane_v(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params &params, unsigned width, unsigned height)
{
    typedef typename std::conditional<std::is_integral<T>::value, int32_t, float>::type Accum;
    typedef typename std::conditional<std::is_integral<T>::value, int16_t, float>::type Weight;

    const Weight *coeffs = std::is_integral<T>::value ? (const Weight *)params.matrix : (const Weight *)params.matrixf;
    unsigned fwidth = params.matrixsize;
    unsigned support = fwidth / 2;

    uint16_t maxval = params.maxval;
    float div = params.div;
    float bias = params.bias;
    bool saturate = params.saturate;

    for (unsigned i = 0; i < std::min(height, support); ++i) {
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        unsigned dist_from_bottom = height - 1 - i;
        unsigned idx[25];

        for (unsigned k = 0; k < support; ++k) {
            idx[k] = i < support - k ? std::min(support - k - i, height - 1) : i - support + k;
        }
        for (unsigned k = support; k < fwidth; ++k) {
            idx[k] = dist_from_bottom < k - support ? i - std::min(k - support - dist_from_bottom, i) : i - support + k;
        }

        for (unsigned j = 0; j < width; ++j) {
            Accum accum = 0;

            for (unsigned k = 0; k < fwidth; ++k) {
                accum += coeffs[k] * static_cast<Accum>(static_cast<const T *>(line_ptr(src, idx[k], src_stride))[j]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);
        }
    }
    for (unsigned i = support; i < height - std::min(height, support); ++i) {
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        for (unsigned j = 0; j < width; ++j) {
            Accum accum = 0;

            for (unsigned k = 0; k < fwidth; ++k) {
                accum += coeffs[k] * static_cast<Accum>(static_cast<const T *>(line_ptr(src, i - support + k, src_stride))[j]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);
        }
    }
    for (unsigned i = std::max(support, height - std::min(height, support)); i < height; ++i) {
        T *dstp = static_cast<T *>(line_ptr(dst, i, dst_stride));

        unsigned dist_from_bottom = height - 1 - i;
        unsigned idx[25];

        for (unsigned k = 0; k < support; ++k) {
            idx[k] = i < support - k ? std::min(support - k - i, height - 1) : i - support + k;
        }
        for (unsigned k = support; k < fwidth; ++k) {
            idx[k] = dist_from_bottom < k - support ? i - std::min(k - support - dist_from_bottom, i) : i - support + k;
        }

        for (unsigned j = 0; j < width; ++j) {
            Accum accum = 0;

            for (unsigned k = 0; k < fwidth; ++k) {
                accum += coeffs[k] * static_cast<Accum>(static_cast<const T *>(line_ptr(src, idx[k], src_stride))[j]);
            }

            float tmp = static_cast<float>(accum) * div + bias;
            tmp = saturate ? tmp : std::fabs(tmp);
            dstp[j] = limit(xrint<T>(tmp), maxval);
        }
    }
}

} // namespace


void vs_generic_3x3_prewitt_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_prewitt_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_sobel_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<PrewittSobelOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_min_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_max_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MinMaxOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<uint8_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<uint16_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_median_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<MedianOp<float>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint8_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint16_t, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_deflate_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<float, false>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint8_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<uint16_t, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_inflate_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<DeflateInflateOp<float, true>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<uint8_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<uint16_t>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_3x3_conv_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const vs_generic_params *params, unsigned width, unsigned height)
{
    filter_plane_3x3<ConvolutionOp<float>>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_5x5_conv_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_5x5<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_5x5_conv_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_5x5<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_5x5_conv_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_5x5<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_h_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_h<float>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_byte_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint8_t>(src, src_stride, dst, dst_stride, *params, width, height);
}
void vs_generic_1d_conv_v_word_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<uint16_t>(src, src_stride, dst, dst_stride, *params, width, height);
}

void vs_generic_1d_conv_v_float_c(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    conv_plane_v<float>(src, src_stride, dst, dst_stride, *params, width, height);
}
