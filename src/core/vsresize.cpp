/*
* Copyright (c) 2015-2020 Hoppsan G. Pig
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

#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#define ZIMGXX_NAMESPACE vszimgxx
#include <zimg++.hpp>

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "internalfilters.h"
#include "resizeshared.h"
#include "version.h"

using namespace vsh;

/* The shared tables carry the H.273/property values; everything below static_casts them
   into zimg's enums, which is only sound while the two domains agree number for number.
   Asserted entry by entry rather than trusted, range included -- VSRange under the
   current API numbers full 1 exactly as zimg does. */
static_assert(ZIMG_RANGE_LIMITED == VSC_RANGE_LIMITED && ZIMG_RANGE_FULL == VSC_RANGE_FULL);
static_assert(ZIMG_CHROMA_LEFT == VSC_CHROMA_LEFT && ZIMG_CHROMA_CENTER == VSC_CHROMA_CENTER &&
    ZIMG_CHROMA_TOP_LEFT == VSC_CHROMA_TOP_LEFT && ZIMG_CHROMA_TOP == VSC_CHROMA_TOP &&
    ZIMG_CHROMA_BOTTOM_LEFT == VSC_CHROMA_BOTTOM_LEFT && ZIMG_CHROMA_BOTTOM == VSC_CHROMA_BOTTOM);
static_assert(ZIMG_MATRIX_RGB == VSC_MATRIX_RGB && ZIMG_MATRIX_BT709 == VSC_MATRIX_BT709 &&
    ZIMG_MATRIX_UNSPECIFIED == VSC_MATRIX_UNSPECIFIED && ZIMG_MATRIX_ST170_M == VSC_MATRIX_ST170_M &&
    ZIMG_MATRIX_ST240_M == VSC_MATRIX_ST240_M && ZIMG_MATRIX_BT470_BG == VSC_MATRIX_BT470_BG &&
    ZIMG_MATRIX_FCC == VSC_MATRIX_FCC && ZIMG_MATRIX_YCGCO == VSC_MATRIX_YCGCO &&
    ZIMG_MATRIX_BT2020_NCL == VSC_MATRIX_BT2020_NCL && ZIMG_MATRIX_BT2020_CL == VSC_MATRIX_BT2020_CL &&
    ZIMG_MATRIX_CHROMATICITY_DERIVED_CL == VSC_MATRIX_CHROMATICITY_DERIVED_CL &&
    ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL == VSC_MATRIX_CHROMATICITY_DERIVED_NCL &&
    ZIMG_MATRIX_ICTCP == VSC_MATRIX_ICTCP);
static_assert(ZIMG_TRANSFER_BT709 == VSC_TRANSFER_BT709 && ZIMG_TRANSFER_UNSPECIFIED == VSC_TRANSFER_UNSPECIFIED &&
    ZIMG_TRANSFER_BT601 == VSC_TRANSFER_BT601 && ZIMG_TRANSFER_LINEAR == VSC_TRANSFER_LINEAR &&
    ZIMG_TRANSFER_BT2020_10 == VSC_TRANSFER_BT2020_10 && ZIMG_TRANSFER_BT2020_12 == VSC_TRANSFER_BT2020_12 &&
    ZIMG_TRANSFER_ST240_M == VSC_TRANSFER_ST240_M && ZIMG_TRANSFER_BT470_M == VSC_TRANSFER_BT470_M &&
    ZIMG_TRANSFER_BT470_BG == VSC_TRANSFER_BT470_BG && ZIMG_TRANSFER_LOG_100 == VSC_TRANSFER_LOG_100 &&
    ZIMG_TRANSFER_LOG_316 == VSC_TRANSFER_LOG_316 && ZIMG_TRANSFER_ST2084 == VSC_TRANSFER_ST2084 &&
    ZIMG_TRANSFER_ARIB_B67 == VSC_TRANSFER_ARIB_B67 && ZIMG_TRANSFER_ST428 == VSC_TRANSFER_ST428 &&
    ZIMG_TRANSFER_IEC_61966_2_1 == VSC_TRANSFER_IEC_61966_2_1 &&
    ZIMG_TRANSFER_IEC_61966_2_4 == VSC_TRANSFER_IEC_61966_2_4);
static_assert(ZIMG_PRIMARIES_BT709 == VSC_PRIMARIES_BT709 && ZIMG_PRIMARIES_UNSPECIFIED == VSC_PRIMARIES_UNSPECIFIED &&
    ZIMG_PRIMARIES_ST170_M == VSC_PRIMARIES_ST170_M && ZIMG_PRIMARIES_ST240_M == VSC_PRIMARIES_ST240_M &&
    ZIMG_PRIMARIES_BT470_M == VSC_PRIMARIES_BT470_M && ZIMG_PRIMARIES_BT470_BG == VSC_PRIMARIES_BT470_BG &&
    ZIMG_PRIMARIES_FILM == VSC_PRIMARIES_FILM && ZIMG_PRIMARIES_BT2020 == VSC_PRIMARIES_BT2020 &&
    ZIMG_PRIMARIES_ST428 == VSC_PRIMARIES_ST428 && ZIMG_PRIMARIES_ST431_2 == VSC_PRIMARIES_ST431_2 &&
    ZIMG_PRIMARIES_ST432_1 == VSC_PRIMARIES_ST432_1 && ZIMG_PRIMARIES_EBU3213_E == VSC_PRIMARIES_EBU3213_E);

namespace {

using namespace std::string_literals;

template<class T>
struct EnumEntry {
    std::string_view name;
    T value;
};

template<class T, size_t N>
const T *findEnum(const EnumEntry<T> (&table)[N], std::string_view name) {
    for (const EnumEntry<T> &entry : table) {
        if (entry.name == name)
            return &entry.value;
    }
    return nullptr;
}

/* The shared tables carry int values; the identity asserts above are what make the
   static_cast at the lookup sites sound. */
template<size_t N>
const int *findEnum(const ResizeEnumEntry (&table)[N], std::string_view name) {
    return findResizeEnum(table, name);
}

constexpr EnumEntry<zimg_cpu_type_e> g_cpu_type_table[] = {
    { "none",      ZIMG_CPU_NONE },
    { "auto",      ZIMG_CPU_AUTO },
    { "auto64",    ZIMG_CPU_AUTO_64B },
#if defined(__i386) || defined(_M_IX86) || defined(_M_X64) || defined(__x86_64__)
    { "mmx",       ZIMG_CPU_X86_MMX },
    { "sse",       ZIMG_CPU_X86_SSE },
    { "sse2",      ZIMG_CPU_X86_SSE2 },
    { "sse3",      ZIMG_CPU_X86_SSE3 },
    { "ssse3",     ZIMG_CPU_X86_SSSE3 },
    { "sse41",     ZIMG_CPU_X86_SSE41 },
    { "sse42",     ZIMG_CPU_X86_SSE42 },
    { "avx",       ZIMG_CPU_X86_AVX },
    { "f16c",      ZIMG_CPU_X86_F16C },
    { "avx2",      ZIMG_CPU_X86_AVX2 },
    { "avx512f",   ZIMG_CPU_X86_AVX512F },
    { "avx512skx", ZIMG_CPU_X86_AVX512_SKX },
    { "avx512clx", ZIMG_CPU_X86_AVX512_CLX },
    { "avx512snc", ZIMG_CPU_X86_AVX512_SNC },
#endif
};

/* The colorspace vocabulary lives in resizeshared.h, shared with the compute path;
   the tables below are statements about the scalar implementation and stay here. */

constexpr EnumEntry<zimg_dither_type_e> g_dither_type_table[] = {
    { "none",            ZIMG_DITHER_NONE },
    { "ordered",         ZIMG_DITHER_ORDERED },
    { "random",          ZIMG_DITHER_RANDOM },
    { "error_diffusion", ZIMG_DITHER_ERROR_DIFFUSION },
};

constexpr EnumEntry<zimg_resample_filter_e> g_resample_filter_table[] = {
    { "point",    ZIMG_RESIZE_POINT },
    { "bilinear", ZIMG_RESIZE_BILINEAR },
    { "bicubic",  ZIMG_RESIZE_BICUBIC },
    { "spline16", ZIMG_RESIZE_SPLINE16 },
    { "spline36", ZIMG_RESIZE_SPLINE36 },
    { "spline64", ZIMG_RESIZE_SPLINE64 },
    { "lanczos",  ZIMG_RESIZE_LANCZOS },
};


template <class T, class U>
T range_check_integer(U x, const char *key) {
    if (x < std::numeric_limits<T>::min() || x > std::numeric_limits<T>::max())
        throw std::range_error{ "value for key \""s + key + "\" out of range" };
    return static_cast<T>(x);
}

template <class T>
T propGetScalar(const VSMap *map, const char *key, const VSAPI *vsapi);

template <>
int propGetScalar<int>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    auto x = vsapi->mapGetInt(map, key, 0, nullptr);
    return range_check_integer<int>(x, key);
}

template <>
unsigned propGetScalar<unsigned>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    auto x = vsapi->mapGetInt(map, key, 0, nullptr);
    return range_check_integer<unsigned>(x, key);
}

template <>
double propGetScalar<double>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    return vsapi->mapGetFloat(map, key, 0, nullptr);
}

template <>
const char *propGetScalar<const char *>(const VSMap *map, const char *key, const VSAPI *vsapi) {
    return vsapi->mapGetData(map, key, 0, nullptr);
}

template <class T>
T propGetScalarDef(const VSMap *map, const char *key, T def, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(map, key) > 0)
        return propGetScalar<T>(map, key, vsapi);
    else
        return def;
}

template <class T, class U, class Pred>
void propGetIfValid(const VSMap *map, const char *key, U *out, Pred pred, const VSAPI *vsapi) {
    if (vsapi->mapNumElements(map, key) > 0) {
        T x = propGetScalar<T>(map, key, vsapi);
        if (pred(x))
            *out = static_cast<U>(x);
    }
}


void translate_pixel_type(const VSVideoFormat *format, zimg_pixel_type_e *out, const VSAPI *vsapi) {
    if (format->sampleType == stInteger && format->bytesPerSample == 1)
        *out = ZIMG_PIXEL_BYTE;
    else if (format->sampleType == stInteger && format->bytesPerSample == 2)
        *out = ZIMG_PIXEL_WORD;
    else if (format->sampleType == stFloat && format->bytesPerSample == 2)
        *out = ZIMG_PIXEL_HALF;
    else if (format->sampleType == stFloat && format->bytesPerSample == 4)
        *out = ZIMG_PIXEL_FLOAT;
    else {
        char buffer[32];
        vsapi->getVideoFormatName(format, buffer);
        throw std::runtime_error{ "no matching pixel type for format: "s + buffer };
    }
}

void translate_color_family(VSColorFamily cf, zimg_color_family_e *out, zimg_matrix_coefficients_e *out_matrix) {
    switch (cf) {
    case cfGray:
        *out = ZIMG_COLOR_GREY;
        *out_matrix = ZIMG_MATRIX_UNSPECIFIED;
        break;
    case cfRGB:
        *out = ZIMG_COLOR_RGB;
        *out_matrix = ZIMG_MATRIX_RGB;
        break;
    case cfYUV:
        *out = ZIMG_COLOR_YUV;
        *out_matrix = ZIMG_MATRIX_UNSPECIFIED;
        break;
    default:
        throw std::runtime_error{ "unsupported color family" };
    }
}

void translate_vsformat(const VSVideoFormat *vsformat, zimg_image_format *format, const VSAPI *vsapi) {
    translate_color_family(static_cast<VSColorFamily>(vsformat->colorFamily), &format->color_family, &format->matrix_coefficients);
    translate_pixel_type(vsformat, &format->pixel_type, vsapi);
    format->depth = vsformat->bitsPerSample;

    format->subsample_w = vsformat->subSamplingW;
    format->subsample_h = vsformat->subSamplingH;
    format->pixel_range = (format->color_family == ZIMG_COLOR_RGB) ? ZIMG_RANGE_FULL : ZIMG_RANGE_LIMITED;

    format->field_parity = ZIMG_FIELD_PROGRESSIVE;
    format->chroma_location = (format->subsample_w || format->subsample_h) ? ZIMG_CHROMA_LEFT : ZIMG_CHROMA_CENTER;
}


void import_frame_props(const VSMap *props, zimg_image_format *format, bool *interlaced, const VSAPI *vsapi) {
    propGetIfValid<int>(props, "_ChromaLocation", &format->chroma_location, [](int x) { return x >= 0; }, vsapi);

    if (vsapi->mapNumElements(props, "_Range") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_Range", 0, nullptr);

        if (x == VSC_RANGE_FULL)
            format->pixel_range = ZIMG_RANGE_FULL;
        else if (x == VSC_RANGE_LIMITED)
            format->pixel_range = ZIMG_RANGE_LIMITED;
        else
            throw std::runtime_error{ "bad _Range value: " + std::to_string(x) };
    }

    // Ignore UNSPECIFIED values from properties, since the user can specify them.
    propGetIfValid<int>(props, "_Matrix", &format->matrix_coefficients, [](int x) { return x != ZIMG_MATRIX_UNSPECIFIED; }, vsapi);
    propGetIfValid<int>(props, "_Transfer", &format->transfer_characteristics, [](int x) { return x != ZIMG_TRANSFER_UNSPECIFIED; }, vsapi);
    propGetIfValid<int>(props, "_Primaries", &format->color_primaries, [](int x) { return x != ZIMG_PRIMARIES_UNSPECIFIED; }, vsapi);

    bool is_interlaced = false;
    if (vsapi->mapNumElements(props, "_Field") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_Field", 0, nullptr);

        if (x == 0)
            format->field_parity = ZIMG_FIELD_BOTTOM;
        else if (x == 1)
            format->field_parity = ZIMG_FIELD_TOP;
        else
            throw std::runtime_error{ "bad _Field value: " + std::to_string(x) };
    } else if (vsapi->mapNumElements(props, "_FieldBased") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_FieldBased", 0, nullptr);

        if (x != VSC_FIELD_PROGRESSIVE && x != VSC_FIELD_BOTTOM && x != VSC_FIELD_TOP)
            throw std::runtime_error{ "bad _FieldBased value: " + std::to_string(x) };

        is_interlaced = x == 1 || x == 2;
    }

    if (is_interlaced) {
        format->active_region.top /= 2;
        format->active_region.height /= 2;
    }

    *interlaced = is_interlaced;
}

void export_frame_props(const zimg_image_format &format, VSMap *props, const VSAPI *vsapi) {
    auto set_int_if_positive = [&](const char *key, int x) {
        if (x >= 0)
            vsapi->mapSetInt(props, key, x, maReplace);
        else
            vsapi->mapDeleteKey(props, key);
    };

    if (format.color_family == ZIMG_COLOR_YUV && (format.subsample_w || format.subsample_h))
        vsapi->mapSetInt(props, "_ChromaLocation", format.chroma_location, maReplace);
    else
        vsapi->mapDeleteKey(props, "_ChromaLocation");

    if (format.pixel_range == ZIMG_RANGE_FULL)
        vsapi->mapSetInt(props, "_Range", VSC_RANGE_FULL, maReplace);
    else if (format.pixel_range == ZIMG_RANGE_LIMITED)
        vsapi->mapSetInt(props, "_Range", VSC_RANGE_LIMITED, maReplace);
    else
        vsapi->mapDeleteKey(props, "_Range");

    set_int_if_positive("_Matrix", format.matrix_coefficients);
    set_int_if_positive("_Transfer", format.transfer_characteristics);
    set_int_if_positive("_Primaries", format.color_primaries);
}

void propagate_sar(const VSMap *src_props, VSMap *dst_props, const zimg_image_format &src_format, const zimg_image_format &dst_format, const VSAPI *vsapi) {
    int64_t sar_num = 0;
    int64_t sar_den = 0;

    if (vsapi->mapNumElements(src_props, "_SARNum") > 0)
        sar_num = vsapi->mapGetInt(src_props, "_SARNum", 0, nullptr);
    if (vsapi->mapNumElements(src_props, "_SARDen") > 0)
        sar_den = vsapi->mapGetInt(src_props, "_SARDen", 0, nullptr);

    if (sar_num <= 0 || sar_den <= 0) {
        vsapi->mapDeleteKey(dst_props, "_SARNum");
        vsapi->mapDeleteKey(dst_props, "_SARDen");
    } else {
        if (!std::isnan(src_format.active_region.width) && src_format.active_region.width != src_format.width)
            muldivRational(&sar_num, &sar_den, std::llround(src_format.active_region.width * 16), static_cast<int64_t>(dst_format.width) * 16);
        else
            muldivRational(&sar_num, &sar_den, src_format.width, dst_format.width);

        if (!std::isnan(src_format.active_region.height) && src_format.active_region.height != src_format.height)
            muldivRational(&sar_num, &sar_den, static_cast<int64_t>(dst_format.height) * 16, std::llround(src_format.active_region.height * 16));
        else
            muldivRational(&sar_num, &sar_den, dst_format.height, src_format.height);

        vsapi->mapSetInt(dst_props, "_SARNum", sar_num, maReplace);
        vsapi->mapSetInt(dst_props, "_SARDen", sar_den, maReplace);
    }
}


vszimgxx::zimage_buffer import_frame_as_buffer(VSFrame *frame, const VSAPI *vsapi) {
    vszimgxx::zimage_buffer buffer;
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(frame);
    for (unsigned p = 0; p < static_cast<unsigned>(format->numPlanes); ++p) {
        buffer.plane[p].data = vsapi->getWritePtr(frame, p);
        buffer.plane[p].stride = vsapi->getStride(frame, p);
        buffer.plane[p].mask = ZIMG_BUFFER_MAX;
    }
    return buffer;
}

vszimgxx::zimage_buffer_const import_frame_as_buffer_const(const VSFrame *frame, const VSAPI *vsapi) {
    vszimgxx::zimage_buffer_const buffer;
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(frame);
    for (unsigned p = 0; p < static_cast<unsigned>(format->numPlanes); ++p) {
        buffer.plane[p].data = vsapi->getReadPtr(frame, p);
        buffer.plane[p].stride = vsapi->getStride(frame, p);
        buffer.plane[p].mask = ZIMG_BUFFER_MAX;
    }
    return buffer;
}

template <class T>
T get_field_buffer(const T &buffer, unsigned num_planes, zimg_field_parity_e parity) {
    T field = buffer;
    unsigned phase = parity == ZIMG_FIELD_BOTTOM ? 1 : 0;

    for (unsigned p = 0; p < num_planes; ++p) {
        field.data(p) = field.line_at(phase, p);
        field.stride(p) *= 2;
    }
    return field;
}


bool operator==(const zimg_image_format &a, const zimg_image_format &b) {
    bool ret = true;

    ret = ret && a.width == b.width;
    ret = ret && a.height == b.height;
    ret = ret && a.pixel_type == b.pixel_type;
    ret = ret && a.subsample_w == b.subsample_w;
    ret = ret && a.subsample_h == b.subsample_h;
    ret = ret && a.color_family == b.color_family;

    if (a.color_family != ZIMG_COLOR_GREY)
        ret = ret && a.matrix_coefficients == b.matrix_coefficients;

    ret = ret && a.transfer_characteristics == b.transfer_characteristics;
    ret = ret && a.color_primaries == b.color_primaries;

    ret = ret && a.depth == b.depth;
    ret = ret && a.pixel_range == b.pixel_range;
    ret = ret && a.field_parity == b.field_parity;

    if (a.color_family == ZIMG_COLOR_YUV && (a.subsample_w || a.subsample_h))
        ret = ret && a.chroma_location == b.chroma_location;

    // active_region varies at runtime (halved for interlaced frames), so it must be part of the key or
    // a halved and an unhalved crop can collide in the same cached graph. NaN means unset; NaN == NaN here.
    auto region_eq = [](double x, double y) { return (std::isnan(x) && std::isnan(y)) || x == y; };
    ret = ret && region_eq(a.active_region.left, b.active_region.left);
    ret = ret && region_eq(a.active_region.top, b.active_region.top);
    ret = ret && region_eq(a.active_region.width, b.active_region.width);
    ret = ret && region_eq(a.active_region.height, b.active_region.height);

    return ret;
}

bool operator!=(const zimg_image_format &a, const zimg_image_format &b) {
    return !operator==(a, b);
}

bool is_shifted(const zimg_image_format &fmt) {
    bool ret = false;
    ret = ret || (!std::isnan(fmt.active_region.left) && fmt.active_region.left != 0);
    ret = ret || (!std::isnan(fmt.active_region.top) && fmt.active_region.top != 0);
    ret = ret || (!std::isnan(fmt.active_region.width) && fmt.active_region.width != fmt.width);
    ret = ret || (!std::isnan(fmt.active_region.height) && fmt.active_region.height != fmt.height);
    return ret;
}


enum class FieldOp {
    NONE,
    DEINTERLACE,
};


struct vszimg_userdata {
    zimg_resample_filter_e filter;
    FieldOp op;

    explicit vszimg_userdata(void *encoded) :
        filter{ static_cast<zimg_resample_filter_e>(reinterpret_cast<intptr_t>(encoded) & 0x3FFF) },
        op{ static_cast<FieldOp>(reinterpret_cast<intptr_t>(encoded) >> 14) }
    {}

    vszimg_userdata(zimg_resample_filter_e filter, FieldOp op = FieldOp::NONE) : filter{ filter }, op{ op } {}

    void *encode() const { return reinterpret_cast<void *>((static_cast<intptr_t>(filter) & 0x3FFF) | (static_cast<intptr_t>(op) << 14)); }

    operator void *() const { return encode(); }
};


class vszimg {
    struct frame_params {
        std::optional<zimg_matrix_coefficients_e> matrix;
        std::optional<zimg_transfer_characteristics_e> transfer;
        std::optional<zimg_color_primaries_e> primaries;
        std::optional<zimg_pixel_range_e> range;
        std::optional<zimg_chroma_location_e> chromaloc;
    };

    struct graph_data {
        vszimgxx::FilterGraph graph;
        zimg_image_format src_format;
        zimg_image_format dst_format;

        graph_data(const zimg_image_format &src_format, const zimg_image_format &dst_format, const zimg_graph_builder_params &params) :
            graph(vszimgxx::FilterGraph::build(src_format, dst_format, &params)),
            src_format(src_format),
            dst_format(dst_format) {}
    };

    std::shared_ptr<graph_data> m_graph_data_p;
    std::shared_ptr<graph_data> m_graph_data_t;
    std::shared_ptr<graph_data> m_graph_data_b;
    std::mutex m_graph_mutex;

    VSNode *m_node = nullptr;
    VSVideoInfo m_vi{};

    vszimgxx::zfilter_graph_builder_params m_params;
    double m_src_left = NAN, m_src_top = NAN, m_src_width = NAN, m_src_height = NAN; // Propagated to zimage_format.

    frame_params m_frame_params;
    frame_params m_frame_params_in;

    FieldOp m_field_op = FieldOp::NONE;

    template <class T, class Table>
    static void lookup_enum_str(const VSMap *map, const char *key, const Table &enum_table, std::optional<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            const char *enum_str = propGetScalar<const char *>(map, key, vsapi);
            if (const auto *value = findEnum(enum_table, enum_str))
                *out = static_cast<T>(*value);
            else
                throw std::runtime_error{ "bad value: "s + key };
        }
    }

    template <class T, class Map>
    static void lookup_enum(const VSMap *map, const char *key, const Map &enum_table, std::optional<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            *out = static_cast<T>(propGetScalar<int>(map, key, vsapi));
        } else {
            std::string altkey = std::string{ key } + "_s";
            lookup_enum_str(map, altkey.c_str(), enum_table, out, vsapi);
        }
    }

    template <class T, class Map>
    static bool lookup_enum_str_opt(const VSMap *map, const char *key, const Map &enum_table, T *out, const VSAPI *vsapi) {
        std::optional<T> opt;
        lookup_enum_str(map, key, enum_table, &opt, vsapi);
        if (opt.has_value())
            *out = opt.value();
        return opt.has_value();
    }

    template <class T>
    static void propagate_if_present(const std::optional<T> &in, T *out) {
        if (in.has_value())
            *out = in.value();
    }

    vszimg(const VSMap *in, void *userData, VSCore *core, const VSAPI *vsapi)
    {
        vszimg_userdata u{ userData };
        m_field_op = u.op;

        try {
            m_node = vsapi->mapGetNode(in, "clip", 0, nullptr);
            const VSVideoInfo &node_vi = *vsapi->getVideoInfo(m_node);

            m_vi = node_vi;

            m_vi.width = propGetScalarDef<unsigned>(in, "width", node_vi.width, vsapi);
            m_vi.height = propGetScalarDef<unsigned>(in, "height", node_vi.height, vsapi);

            if (m_field_op == FieldOp::DEINTERLACE)
                m_vi.height = node_vi.height * 2;

            if (int format_id = propGetScalarDef<int>(in, "format", 0, vsapi)) {
                if (!vsapi->getVideoFormatByID(&m_vi.format, format_id, core) || m_vi.format.colorFamily == cfUndefined)
                    throw std::runtime_error{ "Invalid format id." };
            } else {
                m_vi.format = node_vi.format;
            }

            lookup_enum(in, "matrix", resizeMatrixTable, &m_frame_params.matrix, vsapi);
            lookup_enum(in, "transfer", resizeTransferTable, &m_frame_params.transfer, vsapi);
            lookup_enum(in, "primaries", resizePrimariesTable, &m_frame_params.primaries, vsapi);
            lookup_enum(in, "range", resizeRangeTable, &m_frame_params.range, vsapi);
            lookup_enum(in, "chromaloc", resizeChromaLocTable, &m_frame_params.chromaloc, vsapi);

            lookup_enum(in, "matrix_in", resizeMatrixTable, &m_frame_params_in.matrix, vsapi);
            lookup_enum(in, "transfer_in", resizeTransferTable, &m_frame_params_in.transfer, vsapi);
            lookup_enum(in, "primaries_in", resizePrimariesTable, &m_frame_params_in.primaries, vsapi);
            lookup_enum(in, "range_in", resizeRangeTable, &m_frame_params_in.range, vsapi);
            lookup_enum(in, "chromaloc_in", resizeChromaLocTable, &m_frame_params_in.chromaloc, vsapi);

            m_params.cpu_type = ZIMG_CPU_AUTO_64B;
            m_params.allow_approximate_gamma = propGetScalarDef<int>(in, "approximate_gamma", 1, vsapi);
#if ZIMG_API_VERSION >= ZIMG_MAKE_API_VERSION(2, 5)
            m_params.chromatic_adaptation = propGetScalarDef<int>(in, "chromatic_adaptation", 1, vsapi);
#endif
            m_params.resample_filter = u.filter;
            m_params.filter_param_a = propGetScalarDef<double>(in, "filter_param_a", m_params.filter_param_a, vsapi);
            m_params.filter_param_b = propGetScalarDef<double>(in, "filter_param_b", m_params.filter_param_b, vsapi);

            if (lookup_enum_str_opt(in, "resample_filter_uv", g_resample_filter_table, &m_params.resample_filter_uv, vsapi)) {
                m_params.filter_param_a_uv = propGetScalarDef<double>(in, "filter_param_a_uv", m_params.filter_param_a_uv, vsapi);
                m_params.filter_param_b_uv = propGetScalarDef<double>(in, "filter_param_b_uv", m_params.filter_param_b_uv, vsapi);
            } else {
                m_params.resample_filter_uv = m_params.resample_filter;
                m_params.filter_param_a_uv = m_params.filter_param_a;
                m_params.filter_param_b_uv = m_params.filter_param_b;
            }

            lookup_enum_str_opt(in, "dither_type", g_dither_type_table, &m_params.dither_type, vsapi);
            lookup_enum_str_opt(in, "cpu_type", g_cpu_type_table, &m_params.cpu_type, vsapi);

            m_src_left = propGetScalarDef<double>(in, "src_left", NAN, vsapi);
            m_src_top = propGetScalarDef<double>(in, "src_top", NAN, vsapi);
            m_src_width = propGetScalarDef<double>(in, "src_width", NAN, vsapi);
            m_src_height = propGetScalarDef<double>(in, "src_height", NAN, vsapi);
            m_params.nominal_peak_luminance = propGetScalarDef<double>(in, "nominal_luminance", NAN, vsapi);

            // Basic compatibility check.
            if (isConstantVideoFormat(&node_vi) && isConstantVideoFormat(&m_vi)) {
                vszimgxx::zimage_format src_format, dst_format;

                src_format.width = node_vi.width;
                src_format.height = node_vi.height;
                dst_format.width = m_vi.width;
                dst_format.height = m_vi.height;

                translate_vsformat(&node_vi.format, &src_format, vsapi);
                translate_vsformat(&m_vi.format, &dst_format, vsapi);

                if ((dst_format.color_family == ZIMG_COLOR_YUV || dst_format.color_family == ZIMG_COLOR_GREY)
                    && dst_format.matrix_coefficients == ZIMG_MATRIX_UNSPECIFIED
                    && src_format.color_family != ZIMG_COLOR_YUV
                    && src_format.color_family != ZIMG_COLOR_GREY
                    && !m_frame_params.matrix.has_value()) {
                    throw std::runtime_error{ "Matrix must be specified when converting to YUV or GRAY from RGB" };
                }
            }
        } catch (...) {
            freeResources(core, vsapi);
            throw;
        }
    }

    std::shared_ptr<graph_data> get_graph_data(const zimg_image_format &src_format, const zimg_image_format &dst_format) {
        std::shared_ptr<graph_data> *data_ptr;

        if (src_format.field_parity == ZIMG_FIELD_TOP)
            data_ptr = &m_graph_data_t;
        else if (src_format.field_parity == ZIMG_FIELD_BOTTOM)
            data_ptr = &m_graph_data_b;
        else
            data_ptr = &m_graph_data_p;

        // A plain mutex rather than std::atomic<std::shared_ptr<>>: that C++20 type is still
        // unimplemented in libc++ (macOS). The lock is only held briefly on the cache-hit path,
        // and it also dedupes the (rare) graph rebuild across threads.
        std::lock_guard<std::mutex> lock(m_graph_mutex);
        if (!*data_ptr || (*data_ptr)->src_format != src_format || (*data_ptr)->dst_format != dst_format)
            *data_ptr = std::make_shared<graph_data>(src_format, dst_format, m_params);

        return *data_ptr;
    }

    void set_frame_params(const frame_params &params, zimg_image_format *format) {
        propagate_if_present(params.matrix, &format->matrix_coefficients);
        propagate_if_present(params.transfer, &format->transfer_characteristics);
        propagate_if_present(params.primaries, &format->color_primaries);
        propagate_if_present(params.range, &format->pixel_range);
        propagate_if_present(params.chromaloc, &format->chroma_location);
    }

    void set_src_colorspace(const VSMap *props, zimg_image_format *src_format, bool *interlaced, const VSAPI *vsapi) {
        // Frame properties take precedence over defaults.
        set_frame_params(m_frame_params_in, src_format);
        import_frame_props(props, src_format, interlaced, vsapi);
    }

    void set_dst_colorspace(const zimg_image_format &src_format, zimg_image_format *dst_format) {
        // Avoid copying matrix coefficients when restricted by color family.
        if (dst_format->matrix_coefficients != ZIMG_MATRIX_RGB)
            dst_format->matrix_coefficients = src_format.matrix_coefficients;

        dst_format->transfer_characteristics = src_format.transfer_characteristics;
        dst_format->color_primaries = src_format.color_primaries;

        // Avoid propagating source pixel range and chroma location if color family changes.
        if (dst_format->color_family == src_format.color_family) {
            dst_format->pixel_range = src_format.pixel_range;

            if (dst_format->color_family == ZIMG_COLOR_YUV &&
                (dst_format->subsample_w || dst_format->subsample_h) &&
                (src_format.subsample_w || src_format.subsample_h))
            {
                dst_format->chroma_location = src_format.chroma_location;
            }
        }

        dst_format->field_parity = src_format.field_parity;
        set_frame_params(m_frame_params, dst_format);
    }

    const VSFrame *real_get_frame(const VSFrame *src_frame, VSCore *core, const VSAPI *vsapi) {
        VSFrame *dst_frame = nullptr;
        vszimgxx::zimage_format src_format, dst_format;

        try {
            const VSMap *src_props = vsapi->getFramePropertiesRO(src_frame);
            const VSVideoFormat *src_vsformat = vsapi->getVideoFrameFormat(src_frame);
            const VSVideoFormat *dst_vsformat = (m_vi.format.colorFamily != cfUndefined) ? &m_vi.format : src_vsformat;

            src_format.width = vsapi->getFrameWidth(src_frame, 0);
            src_format.height = vsapi->getFrameHeight(src_frame, 0);
            dst_format.width = m_vi.width ? static_cast<unsigned>(m_vi.width) : src_format.width;
            dst_format.height = m_vi.height ? static_cast<unsigned>(m_vi.height) : src_format.height;

            src_format.active_region.left = m_src_left;
            src_format.active_region.top = m_src_top;
            src_format.active_region.width = m_src_width;
            src_format.active_region.height = m_src_height;

            translate_vsformat(src_vsformat, &src_format, vsapi);
            translate_vsformat(dst_vsformat, &dst_format, vsapi);

            bool interlaced = false;

            set_frame_params(m_frame_params_in, &src_format);
            import_frame_props(src_props, &src_format, &interlaced, vsapi);
            set_dst_colorspace(src_format, &dst_format);

            if (m_field_op == FieldOp::DEINTERLACE) {
                if (interlaced || src_format.field_parity == ZIMG_FIELD_PROGRESSIVE)
                    vsapi->logMessage(mtFatal, "expected _Field when bobbing", core);

                dst_format.height = src_format.height * 2;
                dst_format.field_parity = ZIMG_FIELD_PROGRESSIVE;
            }

            if (src_format == dst_format && isSameVideoFormat(src_vsformat, dst_vsformat) && !is_shifted(src_format)) {
                VSFrame *clone = vsapi->copyFrame(src_frame, core);
                export_frame_props(dst_format, vsapi->getFramePropertiesRW(clone), vsapi);
                return clone;
            }

            dst_frame = vsapi->newVideoFrame(dst_vsformat, dst_format.width, dst_format.height, src_frame, core);

            if (interlaced) {
                vszimgxx::zimage_format src_format_t = src_format;
                vszimgxx::zimage_format dst_format_t = dst_format;

                src_format_t.height /= 2;
                dst_format_t.height /= 2;

                src_format_t.field_parity = ZIMG_FIELD_TOP;
                dst_format_t.field_parity = ZIMG_FIELD_TOP;
                std::shared_ptr<graph_data> graph_t = get_graph_data(src_format_t, dst_format_t);

                vszimgxx::zimage_format src_format_b = src_format_t;
                vszimgxx::zimage_format dst_format_b = dst_format_t;
                src_format_b.field_parity = ZIMG_FIELD_BOTTOM;
                dst_format_b.field_parity = ZIMG_FIELD_BOTTOM;
                std::shared_ptr<graph_data> graph_b = get_graph_data(src_format_b, dst_format_b);

                std::unique_ptr<void, decltype(&vsh_aligned_free)> tmp{
                    vsh_aligned_malloc(std::max(graph_t->graph.get_tmp_size(), graph_b->graph.get_tmp_size()), 64),
                    vsh_aligned_free
                };
                if (!tmp)
                    throw std::bad_alloc{};

                auto src_buffer = import_frame_as_buffer_const(src_frame, vsapi);
                auto dst_buffer = import_frame_as_buffer(dst_frame, vsapi);

                auto src_buffer_b = get_field_buffer(src_buffer, src_vsformat->numPlanes, ZIMG_FIELD_BOTTOM);
                auto dst_buffer_b = get_field_buffer(dst_buffer, dst_vsformat->numPlanes, ZIMG_FIELD_BOTTOM);
                graph_b->graph.process(src_buffer_b, dst_buffer_b, tmp.get());

                auto src_buffer_t = get_field_buffer(src_buffer, src_vsformat->numPlanes, ZIMG_FIELD_TOP);
                auto dst_buffer_t = get_field_buffer(dst_buffer, dst_vsformat->numPlanes, ZIMG_FIELD_TOP);
                graph_t->graph.process(src_buffer_t, dst_buffer_t, tmp.get());
            } else {
                std::shared_ptr<graph_data> graph = get_graph_data(src_format, dst_format);

                std::unique_ptr<void, decltype(&vsh_aligned_free)> tmp{
                    vsh_aligned_malloc(graph->graph.get_tmp_size(), 64),
                    vsh_aligned_free
                };
                if (!tmp)
                    throw std::bad_alloc{};

                auto src_buffer = import_frame_as_buffer_const(src_frame, vsapi);
                auto dst_buffer = import_frame_as_buffer(dst_frame, vsapi);
                graph->graph.process(src_buffer, dst_buffer, tmp.get());
            }

            VSMap *dst_props = vsapi->getFramePropertiesRW(dst_frame);
            propagate_sar(src_props, dst_props, src_format, dst_format, vsapi);
            export_frame_props(dst_format, dst_props, vsapi);
        } catch (const vszimgxx::zerror &e) {
            vsapi->freeFrame(dst_frame);

            if (e.code == ZIMG_ERROR_NO_COLORSPACE_CONVERSION) {
                char buf[256];

                snprintf(buf, sizeof(buf), "Resize error %d: %s (%d/%d/%d => %d/%d/%d). May need to specify additional colorspace parameters.",
                    e.code, e.msg, src_format.matrix_coefficients, src_format.transfer_characteristics, src_format.color_primaries,
                    dst_format.matrix_coefficients, dst_format.transfer_characteristics, dst_format.color_primaries);
                throw std::runtime_error{ buf };
            } else {
                throw;
            }
        } catch (...) {
            vsapi->freeFrame(dst_frame);
            throw;
        }

        return dst_frame;
    }
public:
    ~vszimg() {
        assert(!m_node);
    }

    void freeResources(VSCore *core, const VSAPI *vsapi) {
        vsapi->freeNode(m_node);
        m_node = nullptr;
    }

    const VSFrame *get_frame(int n, int activationReason, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        const VSFrame *ret = nullptr;
        const VSFrame *src_frame = nullptr;

        try {
            if (activationReason == arInitial) {
                vsapi->requestFrameFilter(n, m_node, frameCtx);
            } else if (activationReason == arAllFramesReady) {
                src_frame = vsapi->getFrameFilter(n, m_node, frameCtx);
                ret = real_get_frame(src_frame, core, vsapi);
            }
        } catch (const vszimgxx::zerror &e) {
            std::string errmsg = "Resize error " + std::to_string(e.code) + ": " + e.msg;
            vsapi->setFilterError(errmsg.c_str(), frameCtx);
        } catch (const std::exception &e) {
            vsapi->setFilterError(("Resize error: "s + e.what()).c_str(), frameCtx);
        }

        vsapi->freeFrame(src_frame);
        return ret;
    }

    /* Brings a GPU clip back to host memory so the scalar graph below can run on it,
       returning a copy of the arguments with only the clip replaced. */
    static void VS_CC create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
        try {
            vszimg_userdata u{ userData };
            /* Two different things, and Bob is where they part: the kernel is what
               resamples, and the name is what the node is called. */
            const char *kernelName = "";
            switch (u.filter) {
            case ZIMG_RESIZE_POINT: kernelName = "Point"; break;
            case ZIMG_RESIZE_BILINEAR: kernelName = "Bilinear"; break;
            case ZIMG_RESIZE_BICUBIC: kernelName = "Bicubic"; break;
            case ZIMG_RESIZE_SPLINE16: kernelName = "Spline16"; break;
            case ZIMG_RESIZE_SPLINE36: kernelName = "Spline36"; break;
            case ZIMG_RESIZE_SPLINE64: kernelName = "Spline64"; break;
            case ZIMG_RESIZE_LANCZOS: kernelName = "Lanczos"; break;
            }
            const char *name = u.op == FieldOp::DEINTERLACE ? "Bob" : kernelName;

            /* Warned here rather than in the constructor, which does not run when the
               compute path takes the call. */
            if (vsapi->mapNumElements(in, "prefer_props") >= 0)
                vsapi->logMessage(mtWarning, "The deprecated argument prefer_props was passed to a resizer. Ignoring argument.", core);

            /* Residency polymorphic, but only over what the compute path implements: a
               resident clip asking for anything outside it is an error naming the reason,
               not a silent trip back to host memory. Downloading instead would keep the
               call working, which is why it used to -- but it turns a chain meant to stay
               on the device into two transfers per frame, discovered only by reading a log
               line, and the fix is one GPUDownload the caller can place where it belongs. */
            const VSMap *args = in;
            VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
            const bool onGPU = vsapi->getNodeResidency(node) == nrGPU;
            vsapi->freeNode(node);

            if (onGPU) {
                std::string decline;
                if (!createGPUResize(in, out, kernelName, u.op == FieldOp::DEINTERLACE, core, vsapi, decline))
                    vsapi->mapSetError(out, ("Resize: "s + decline +
                        ", which the GPU path does not implement; insert GPUDownload to resize on the CPU").c_str());
                return;
            }

            vszimg *x = new vszimg{ args, userData, core, vsapi };
            VSFilterDependency deps[] = {{x->m_node, rpStrictSpatial}};
            vsapi->createVideoFilter(out, name, &x->m_vi, &vszimg::static_get_frame, &vszimg::free, fmParallel, deps, 1, x, core);
        } catch (const vszimgxx::zerror &e) {
            std::string errmsg = "Resize error " + std::to_string(e.code) + ": " + e.msg;
            vsapi->mapSetError(out, errmsg.c_str());
        } catch (const std::exception &e) {
            vsapi->mapSetError(out, ("Resize error: "s + e.what()).c_str());
        }
    }

    static void VS_CC free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
        vszimg *ptr = static_cast<vszimg *>(instanceData);
        ptr->freeResources(core, vsapi);
        delete ptr;
    }

    static const VSFrame * VS_CC static_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
        return static_cast<vszimg *>(instanceData)->get_frame(n, activationReason, frameData, frameCtx, core, vsapi);
    }
};


void VS_CC bobCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) noexcept {
    vszimg_userdata u{ userData };
    u.op = FieldOp::DEINTERLACE;

    VSPlugin *stdplugin = vsapi->getPluginByNamespace("std", core);
    VSMap *tmp_map = nullptr;
    VSMap *sep_fields = nullptr;
    int _;

    if (const char *filterName = vsapi->mapGetData(in, "filter", 0, &_)) {
        const zimg_resample_filter_e *filter = findEnum(g_resample_filter_table, filterName);

        if (!filter) {
            vsapi->mapSetError(out, "Bob: invalid filter specified");
            return;
        }
        u.filter = *filter;
    }

    tmp_map = vsapi->createMap();
    vsapi->mapConsumeNode(tmp_map, "clip", vsapi->mapGetNode(in, "clip", 0, nullptr), maReplace);
    if (vsapi->mapNumElements(in, "tff") > 0)
        vsapi->mapSetInt(tmp_map, "tff", vsapi->mapGetInt(in, "tff", 0, nullptr), maReplace);
    sep_fields = vsapi->invoke(stdplugin, "SeparateFields", tmp_map);
    if (const char *err = vsapi->mapGetError(sep_fields)) {
        vsapi->mapSetError(out, err);
        goto fail;
    }

    vsapi->copyMap(in, tmp_map);
    vsapi->mapDeleteKey(tmp_map, "filter");
    vsapi->mapDeleteKey(tmp_map, "tff");
    vsapi->mapConsumeNode(tmp_map, "clip", vsapi->mapGetNode(sep_fields, "clip", 0, nullptr), maReplace);
    vszimg::create(tmp_map, out, u, core, vsapi);
fail:
    vsapi->freeMap(tmp_map);
    vsapi->freeMap(sep_fields);
}

} // namespace


void resizeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
#define INT_OPT(x) #x ":int:opt;"
#define FLOAT_OPT(x) #x ":float:opt;"
#define DATA_OPT(x) #x ":data:opt;"
#define ENUM_OPT(x) INT_OPT(x) DATA_OPT(x ## _s)

#define COMMON_ARGS \
  INT_OPT(format) \
  ENUM_OPT(matrix) \
  ENUM_OPT(transfer) \
  ENUM_OPT(primaries) \
  ENUM_OPT(range) \
  ENUM_OPT(chromaloc) \
  ENUM_OPT(matrix_in) \
  ENUM_OPT(transfer_in) \
  ENUM_OPT(primaries_in) \
  ENUM_OPT(range_in) \
  ENUM_OPT(chromaloc_in) \
  FLOAT_OPT(filter_param_a) \
  FLOAT_OPT(filter_param_b) \
  DATA_OPT(resample_filter_uv) \
  FLOAT_OPT(filter_param_a_uv) \
  FLOAT_OPT(filter_param_b_uv) \
  DATA_OPT(dither_type) \
  DATA_OPT(cpu_type) \
  INT_OPT(prefer_props) \
  FLOAT_OPT(src_left) \
  FLOAT_OPT(src_top) \
  FLOAT_OPT(src_width) \
  FLOAT_OPT(src_height) \
  FLOAT_OPT(nominal_luminance) \
  INT_OPT(approximate_gamma) \
  INT_OPT(chromatic_adaptation)

    /* vnode:all rather than plain vnode: a GPU clip has to reach create for the compute
       path to be offered it at all. What the core used to insert automatically now
       happens inside create, and only when the compute path declines, so the CPU
       behaviour is unchanged. */
    static const char RESAMPLE_ARGS[] =
        "clip:vnode:all;"
        INT_OPT(width)
        INT_OPT(height)
        COMMON_ARGS;

    static const char RETURN_VALUE[] = "clip:vnode:all;";

    vspapi->configPlugin(VSH_RESIZE_PLUGIN_ID, "resize", "VapourSynth Resize", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Bilinear", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_BILINEAR), plugin);
    vspapi->registerFunction("Bicubic", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_BICUBIC), plugin);
    vspapi->registerFunction("Point", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_POINT), plugin);
    vspapi->registerFunction("Lanczos", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_LANCZOS), plugin);
    vspapi->registerFunction("Spline16", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE16), plugin);
    vspapi->registerFunction("Spline36", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE36), plugin);
    vspapi->registerFunction("Spline64", RESAMPLE_ARGS, RETURN_VALUE, &vszimg::create, vszimg_userdata(ZIMG_RESIZE_SPLINE64), plugin);

    /* vnode:all even though the whole of Bob may still decline: the modifier is what lets
       a resident clip reach the compute path at all. As plain vnode the core downloaded on
       Bob's own argument, ahead of the SeparateFields this builds on top of, so even the
       parts with a compute path ran on the host. A decline is now an error rather than a
       download, so the caller places the round trip themselves if they want one. */
    vspapi->registerFunction("Bob", "clip:vnode:all;filter:data:opt;tff:int:opt;" COMMON_ARGS, RETURN_VALUE, bobCreate, vszimg_userdata(ZIMG_RESIZE_BICUBIC), plugin);

#undef COMMON_ARGS
#undef INT_OPT
#undef FLOAT_OPT
#undef DATA_OPT
#undef ENUM_OPT
}
