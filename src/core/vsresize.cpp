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
#include <stdexcept>
#include <string>
#include <unordered_map>

#define ZIMGXX_NAMESPACE vszimgxx
#include <zimg++.hpp>

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "internalfilters.h"
#include "version.h"

using namespace vsh;

namespace {

std::string operator""_s(const char *str, size_t len) { return{ str, len }; }

const std::unordered_map<std::string, zimg_cpu_type_e> g_cpu_type_table{
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

const std::unordered_map<std::string, zimg_pixel_range_e> g_range_table{
    { "limited", ZIMG_RANGE_LIMITED },
    { "full",    ZIMG_RANGE_FULL },
};

const std::unordered_map<std::string, zimg_chroma_location_e> g_chromaloc_table{
    { "left",        ZIMG_CHROMA_LEFT },
    { "center",      ZIMG_CHROMA_CENTER },
    { "top_left",    ZIMG_CHROMA_TOP_LEFT },
    { "top",         ZIMG_CHROMA_TOP },
    { "bottom_left", ZIMG_CHROMA_BOTTOM_LEFT },
    { "bottom",      ZIMG_CHROMA_BOTTOM },
};

const std::unordered_map<std::string, zimg_matrix_coefficients_e> g_matrix_table{
    { "rgb",         ZIMG_MATRIX_RGB },
    { "709",         ZIMG_MATRIX_709 },
    { "unspec",      ZIMG_MATRIX_UNSPECIFIED },
    { "170m",        ZIMG_MATRIX_170M },
    { "240m",        ZIMG_MATRIX_240M },
    { "470bg",       ZIMG_MATRIX_470BG },
    { "fcc",         ZIMG_MATRIX_FCC },
    { "ycgco",       ZIMG_MATRIX_YCGCO },
    { "2020ncl",     ZIMG_MATRIX_2020_NCL },
    { "2020cl",      ZIMG_MATRIX_2020_CL },
    { "chromacl",    ZIMG_MATRIX_CHROMATICITY_DERIVED_CL },
    { "chromancl",   ZIMG_MATRIX_CHROMATICITY_DERIVED_NCL },
    { "ictcp",       ZIMG_MATRIX_ICTCP },
};

const std::unordered_map<std::string, zimg_transfer_characteristics_e> g_transfer_table{
    { "709",     ZIMG_TRANSFER_709 },
    { "unspec",  ZIMG_TRANSFER_UNSPECIFIED },
    { "601",     ZIMG_TRANSFER_601 },
    { "linear",  ZIMG_TRANSFER_LINEAR },
    { "2020_10", ZIMG_TRANSFER_2020_10 },
    { "2020_12", ZIMG_TRANSFER_2020_12 },
    { "240m",    ZIMG_TRANSFER_240M },
    { "470m",    ZIMG_TRANSFER_470_M },
    { "470bg",   ZIMG_TRANSFER_470_BG },
    { "log100",  ZIMG_TRANSFER_LOG_100 },
    { "log316",  ZIMG_TRANSFER_LOG_316 },
    { "st2084",  ZIMG_TRANSFER_ST2084 },
    { "std-b67", ZIMG_TRANSFER_ARIB_B67 },
    { "srgb",    ZIMG_TRANSFER_IEC_61966_2_1 },
    { "xvycc",   ZIMG_TRANSFER_IEC_61966_2_4 },
};

const std::unordered_map<std::string, zimg_color_primaries_e> g_primaries_table{
    { "709",       ZIMG_PRIMARIES_709 },
    { "unspec",    ZIMG_PRIMARIES_UNSPECIFIED },
    { "170m",      ZIMG_PRIMARIES_170M },
    { "240m",      ZIMG_PRIMARIES_240M },
    { "470m",      ZIMG_PRIMARIES_470_M },
    { "470bg",     ZIMG_PRIMARIES_470_BG },
    { "film",      ZIMG_PRIMARIES_FILM },
    { "2020",      ZIMG_PRIMARIES_2020 },
    { "st428",     ZIMG_PRIMARIES_ST428 },
    { "xyz",       ZIMG_PRIMARIES_ST428 },
    { "st431-2",   ZIMG_PRIMARIES_ST431_2 },
    { "st432-1",   ZIMG_PRIMARIES_ST432_1 },
    { "ebu3213-e", ZIMG_PRIMARIES_EBU3213_E },
};

const std::unordered_map<std::string, zimg_dither_type_e> g_dither_type_table{
    { "none",            ZIMG_DITHER_NONE },
    { "ordered",         ZIMG_DITHER_ORDERED },
    { "random",          ZIMG_DITHER_RANDOM },
    { "error_diffusion", ZIMG_DITHER_ERROR_DIFFUSION },
};

const std::unordered_map<std::string, zimg_resample_filter_e> g_resample_filter_table{
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
        throw std::range_error{ "value for key \""_s + key + "\" out of range" };
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
        throw std::runtime_error{ "no matching pixel type for format: "_s + buffer };
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

    if (vsapi->mapNumElements(props, "_ColorRange") > 0) {
        int64_t x = vsapi->mapGetInt(props, "_ColorRange", 0, nullptr);

        if (x == 0)
            format->pixel_range = ZIMG_RANGE_FULL;
        else if (x == 1)
            format->pixel_range = ZIMG_RANGE_LIMITED;
        else
            throw std::runtime_error{ "bad _ColorRange value: " + std::to_string(x) };
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
        vsapi->mapSetInt(props, "_ColorRange", 0, maReplace);
    else if (format.pixel_range == ZIMG_RANGE_LIMITED)
        vsapi->mapSetInt(props, "_ColorRange", 1, maReplace);
    else
        vsapi->mapDeleteKey(props, "_ColorRange");

    set_int_if_positive("_Matrix", format.matrix_coefficients);
    set_int_if_positive("_Transfer", format.transfer_characteristics);
    set_int_if_positive("_Primaries", format.color_primaries);
}

void propagate_sar(const VSMap *src_props, VSMap *dst_props, const zimg_image_format &src_format, const zimg_image_format &dst_format, const VSAPI *vsapi) {
    int64_t sar_num = 0;
    int64_t sar_den = 0;

    if (vsapi->mapNumElements(src_props, "_SARNum") > 0)
        sar_num = vsapi->mapGetInt(src_props, "_SARNum", 0, nullptr);
    if (vsapi->mapNumElements(dst_props, "_SARDen") > 0)
        sar_den = vsapi->mapGetInt(dst_props, "_SARDen", 0, nullptr);

    if (sar_num <= 0 || sar_den <= 0) {
        vsapi->mapDeleteKey(dst_props, "_SARNum");
        vsapi->mapDeleteKey(dst_props, "_SARDen");
    } else {
        muldivRational(&sar_num, &sar_den, src_format.width, dst_format.width);
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


void VS_CC vszimg_free(void *instanceData, VSCore *core, const VSAPI *vsapi);
const VSFrame * VS_CC vszimg_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

class vszimg {
    template <class T>
    class optional_of {
        T m_value;
        bool m_is_present;
    public:
        optional_of() : m_value{}, m_is_present{ false } {}

        optional_of(const T &x) : m_value{ x }, m_is_present{ true } {}

        optional_of &operator=(const T &v) {
            m_value = v;
            m_is_present = true;
            return *this;
        }

        const T &get() const {
            assert(is_present());
            return m_value;
        }

        bool is_present() const { return m_is_present; }
    };

    struct frame_params {
        optional_of<zimg_matrix_coefficients_e> matrix;
        optional_of<zimg_transfer_characteristics_e> transfer;
        optional_of<zimg_color_primaries_e> primaries;
        optional_of<zimg_pixel_range_e> range;
        optional_of<zimg_chroma_location_e> chromaloc;
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

    VSNode *m_node;
    VSVideoInfo m_vi;
    bool m_prefer_props; // If true, frame properties have precedence over filter arguments.
    double m_src_left, m_src_top, m_src_width, m_src_height;
    vszimgxx::zfilter_graph_builder_params m_params;

    frame_params m_frame_params;
    frame_params m_frame_params_in;

    template <class T, class Map>
    static void lookup_enum_str(const VSMap *map, const char *key, const Map &enum_table, optional_of<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            const char *enum_str = propGetScalar<const char *>(map, key, vsapi);
            auto it = enum_table.find(enum_str);
            if (it != enum_table.end())
                *out = it->second;
            else
                throw std::runtime_error{ "bad value: "_s + key };
        }
    }

    template <class T, class Map>
    static void lookup_enum(const VSMap *map, const char *key, const Map &enum_table, optional_of<T> *out, const VSAPI *vsapi) {
        if (vsapi->mapNumElements(map, key) > 0) {
            *out = static_cast<T>(propGetScalar<int>(map, key, vsapi));
        } else {
            std::string altkey = std::string{ key } + "_s";
            lookup_enum_str(map, altkey.c_str(), enum_table, out, vsapi);
        }
    }

    template <class T, class Map>
    static bool lookup_enum_str_opt(const VSMap *map, const char *key, const Map &enum_table, T *out, const VSAPI *vsapi) {
        optional_of<T> opt;
        lookup_enum_str(map, key, enum_table, &opt, vsapi);
        if (opt.is_present())
            *out = opt.get();
        return opt.is_present();
    }

    template <class T>
    static void propagate_if_present(const optional_of<T> &in, T *out) {
        if (in.is_present())
            *out = in.get();
    }

    vszimg(const VSMap *in, void *userData, VSCore *core, const VSAPI *vsapi) :
        m_node{ nullptr },
        m_vi(),
        m_prefer_props(false),
        m_src_left(),
        m_src_top(),
        m_src_width(),
        m_src_height()
    {
        try {
            m_node = vsapi->mapGetNode(in, "clip", 0, nullptr);
            const VSVideoInfo &node_vi = *vsapi->getVideoInfo(m_node);

            m_vi = node_vi;

            m_vi.width = propGetScalarDef<unsigned>(in, "width", node_vi.width, vsapi);
            m_vi.height = propGetScalarDef<unsigned>(in, "height", node_vi.height, vsapi);

            int format_id = propGetScalarDef<int>(in, "format", 0, vsapi);
            if (format_id == 0) {
                m_vi.format = node_vi.format;
            } else {
                vsapi->getVideoFormatByID(&m_vi.format, format_id, core);
            }

            lookup_enum(in, "matrix", g_matrix_table, &m_frame_params.matrix, vsapi);
            lookup_enum(in, "transfer", g_transfer_table, &m_frame_params.transfer, vsapi);
            lookup_enum(in, "primaries", g_primaries_table, &m_frame_params.primaries, vsapi);
            lookup_enum(in, "range", g_range_table, &m_frame_params.range, vsapi);
            lookup_enum(in, "chromaloc", g_chromaloc_table, &m_frame_params.chromaloc, vsapi);

            lookup_enum(in, "matrix_in", g_matrix_table, &m_frame_params_in.matrix, vsapi);
            lookup_enum(in, "transfer_in", g_transfer_table, &m_frame_params_in.transfer, vsapi);
            lookup_enum(in, "primaries_in", g_primaries_table, &m_frame_params_in.primaries, vsapi);
            lookup_enum(in, "range_in", g_range_table, &m_frame_params_in.range, vsapi);
            lookup_enum(in, "chromaloc_in", g_chromaloc_table, &m_frame_params_in.chromaloc, vsapi);

            m_params.cpu_type = ZIMG_CPU_AUTO_64B;
            m_params.allow_approximate_gamma = 1;
            m_params.resample_filter = static_cast<zimg_resample_filter_e>(reinterpret_cast<intptr_t>(userData));
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
            m_prefer_props = !!propGetScalarDef<int>(in, "prefer_props", 0, vsapi);

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
                    && !m_frame_params.matrix.is_present()) {
                    throw std::runtime_error{ "Matrix must be specified when converting to YUV or GRAY from RGB" };
                }
            }
        } catch (...) {
            freeFunc(core, vsapi);
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

        std::shared_ptr<graph_data> data = std::atomic_load(data_ptr);
        if (!data || data->src_format != src_format || data->dst_format != dst_format) {
            data = std::make_shared<graph_data>(src_format, dst_format, m_params);
            std::atomic_store(data_ptr, data);
        }

        return data;
    }

    void set_src_colorspace(zimg_image_format *src_format) {
        propagate_if_present(m_frame_params_in.matrix, &src_format->matrix_coefficients);
        propagate_if_present(m_frame_params_in.transfer, &src_format->transfer_characteristics);
        propagate_if_present(m_frame_params_in.primaries, &src_format->color_primaries);
        propagate_if_present(m_frame_params_in.range, &src_format->pixel_range);
        propagate_if_present(m_frame_params_in.chromaloc, &src_format->chroma_location);
    }

    void set_dst_colorspace(const zimg_image_format &src_format, zimg_image_format *dst_format) {
        // Avoid copying matrix coefficients when restricted by color family.
        if (dst_format->matrix_coefficients != ZIMG_MATRIX_RGB && dst_format->matrix_coefficients != ZIMG_MATRIX_YCGCO)
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

        propagate_if_present(m_frame_params.matrix, &dst_format->matrix_coefficients);
        propagate_if_present(m_frame_params.transfer, &dst_format->transfer_characteristics);
        propagate_if_present(m_frame_params.primaries, &dst_format->color_primaries);
        propagate_if_present(m_frame_params.range, &dst_format->pixel_range);
        propagate_if_present(m_frame_params.chromaloc, &dst_format->chroma_location);
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

            if (m_prefer_props) {
                set_src_colorspace(&src_format);
                import_frame_props(src_props, &src_format, &interlaced, vsapi);
            } else {
                import_frame_props(src_props, &src_format, &interlaced, vsapi);
                set_src_colorspace(&src_format);
            }

            set_dst_colorspace(src_format, &dst_format);

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

    void freeFunc(VSCore *core, const VSAPI *vsapi) {
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
            vsapi->setFilterError(("Resize error: "_s + e.what()).c_str(), frameCtx);
        }

        vsapi->freeFrame(src_frame);
        return ret;
    }

    static void create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
        try {
            vszimg *x = new vszimg{ in, userData, core, vsapi };
            const char *resizeType = "UnknownResize";
            switch (reinterpret_cast<uintptr_t>(userData)) {
                case ZIMG_RESIZE_POINT:
                    resizeType = "Point";
                    break;
                case ZIMG_RESIZE_BILINEAR:
                    resizeType = "Bilinear";
                    break;
                case ZIMG_RESIZE_BICUBIC:
                    resizeType = "Bicubic";
                    break;
                case ZIMG_RESIZE_SPLINE16:
                    resizeType = "Spline16";
                    break;
                case ZIMG_RESIZE_SPLINE36:
                    resizeType = "Spline36";
                    break;
                case ZIMG_RESIZE_SPLINE64:
                    resizeType = "Spline64";
                    break;
                case ZIMG_RESIZE_LANCZOS:
                    resizeType = "Lanczos";
                    break;
            }
            VSFilterDependency deps[] = {{x->m_node, rpStrictSpatial}};
            vsapi->createVideoFilter(out, resizeType, &x->m_vi, &vszimg_get_frame, vszimg_free, fmParallel, deps, 1, x, core);
        } catch (const vszimgxx::zerror &e) {
            std::string errmsg = "Resize error " + std::to_string(e.code) + ": " + e.msg;
            vsapi->mapSetError(out, errmsg.c_str());
        } catch (const std::exception &e) {
            vsapi->mapSetError(out, ("Resize error: "_s + e.what()).c_str());
        }
    }
};

void VS_CC vszimg_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    vszimg::create(in, out, userData, core, vsapi);
}

void VS_CC vszimg_free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    vszimg *x = static_cast<vszimg *>(instanceData);
    x->freeFunc(core, vsapi);
    delete x;
}

const VSFrame * VS_CC vszimg_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    return static_cast<vszimg *>(instanceData)->get_frame(n, activationReason, frameData, frameCtx, core, vsapi);
}

} // namespace


void resizeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
#define INT_OPT(x) #x ":int:opt;"
#define FLOAT_OPT(x) #x ":float:opt;"
#define DATA_OPT(x) #x ":data:opt;"
#define ENUM_OPT(x) INT_OPT(x) DATA_OPT(x ## _s)
    static const char FORMAT_DEFINITION[] =
        "clip:vnode;"
        INT_OPT(width)
        INT_OPT(height)
        INT_OPT(format)
        ENUM_OPT(matrix)
        ENUM_OPT(transfer)
        ENUM_OPT(primaries)
        ENUM_OPT(range)
        ENUM_OPT(chromaloc)
        ENUM_OPT(matrix_in)
        ENUM_OPT(transfer_in)
        ENUM_OPT(primaries_in)
        ENUM_OPT(range_in)
        ENUM_OPT(chromaloc_in)
        FLOAT_OPT(filter_param_a)
        FLOAT_OPT(filter_param_b)
        DATA_OPT(resample_filter_uv)
        FLOAT_OPT(filter_param_a_uv)
        FLOAT_OPT(filter_param_b_uv)
        DATA_OPT(dither_type)
        DATA_OPT(cpu_type)
        INT_OPT(prefer_props)
        FLOAT_OPT(src_left)
        FLOAT_OPT(src_top)
        FLOAT_OPT(src_width)
        FLOAT_OPT(src_height)
        FLOAT_OPT(nominal_luminance);
#undef INT_OPT
#undef FLOAT_OPT
#undef DATA_OPT
#undef ENUM_OPT

    static const char RETURN_FORMAT_DEFINITION[] = "clip:vnode;";

    vspapi->configPlugin(VSH_RESIZE_PLUGIN_ID, "resize", "VapourSynth Resize", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Bilinear", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_BILINEAR, plugin);
    vspapi->registerFunction("Bicubic", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_BICUBIC, plugin);
    vspapi->registerFunction("Point", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_POINT, plugin);
    vspapi->registerFunction("Lanczos", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_LANCZOS, plugin);
    vspapi->registerFunction("Spline16", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_SPLINE16, plugin);
    vspapi->registerFunction("Spline36", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_SPLINE36, plugin);
    vspapi->registerFunction("Spline64", FORMAT_DEFINITION, RETURN_FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_SPLINE64, plugin);
}
