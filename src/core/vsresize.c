/*
* FIXME, add license
*/
*/

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vsresize.h"

#ifdef _WIN32
#include <Windows.h>
typedef CRITICAL_SECTION vszimg_mutex_t;

static int vszimg_mutex_init(vszimg_mutex_t *mutex) { InitializeCriticalSection(mutex); return 0; }
static int vszimg_mutex_lock(vszimg_mutex_t *mutex) { EnterCriticalSection(mutex); return 0; }
static int vszimg_mutex_unlock(vszimg_mutex_t *mutex) { LeaveCriticalSection(mutex); return 0; }
static int vszimg_mutex_destroy(vszimg_mutex_t *mutex) { DeleteCriticalSection(mutex); return 0; }
#else
#include <pthread.h>

typedef pthread_mutex_t vszimg_mutex_t;
static int vszimg_mutex_init(vszimg_mutex_t *mutex) { return pthread_mutex_init(mutex, NULL); }
static int vszimg_mutex_lock(vszimg_mutex_t *mutex) { return pthread_mutex_lock(mutex); }
static int vszimg_mutex_unlock(vszimg_mutex_t *mutex) { return pthread_mutex_unlock(mutex); }
static int vszimg_mutex_destroy(vszimg_mutex_t *mutex) { return pthread_mutex_destroy(mutex); }
#endif // _WIN32

#include <zimg.h>

#if ZIMG_API_VERSION < ZIMG_MAKE_API_VERSION(2, 0)
#error zAPI v2 or greater required
#endif

#include "VapourSynth.h"
#include "VSHelper.h"

typedef unsigned char vszimg_bool;
#define VSZIMG_TRUE  1
#define VSZIMG_FALSE 0

static unsigned g_version_info[3];
static unsigned g_api_version[2];


typedef union zimg_image_buffer_u {
    zimg_image_buffer_const c;
    zimg_image_buffer m;
} zimg_image_buffer_u;


struct string_table_entry {
    const char *str;
    int val;
};


#define ARRAY_SIZE(x) sizeof((x)) / sizeof((x)[0])

static const struct string_table_entry g_cpu_type_table[] = {
    { "none",  ZIMG_CPU_NONE },
    { "auto",  ZIMG_CPU_AUTO },
#if defined(__i386) || defined(_M_IX86) || defined(_M_X64) || defined(__x86_64__)
{ "mmx",   ZIMG_CPU_X86_MMX },
{ "sse",   ZIMG_CPU_X86_SSE },
{ "sse2",  ZIMG_CPU_X86_SSE2 },
{ "sse3",  ZIMG_CPU_X86_SSE3 },
{ "ssse3", ZIMG_CPU_X86_SSSE3 },
{ "sse41", ZIMG_CPU_X86_SSE41 },
{ "sse42", ZIMG_CPU_X86_SSE42 },
{ "avx",   ZIMG_CPU_X86_AVX },
{ "f16c",  ZIMG_CPU_X86_F16C },
{ "avx2",  ZIMG_CPU_X86_AVX2 },
#endif
};

static const struct string_table_entry g_range_table[] = {
    { "limited", ZIMG_RANGE_LIMITED },
    { "full",    ZIMG_RANGE_FULL },
};

static const struct string_table_entry g_chromaloc_table[] = {
    { "left",        ZIMG_CHROMA_LEFT },
    { "center",      ZIMG_CHROMA_CENTER },
    { "top_left",    ZIMG_CHROMA_TOP_LEFT },
    { "top",         ZIMG_CHROMA_TOP },
    { "bottom_left", ZIMG_CHROMA_BOTTOM_LEFT },
    { "bottom",      ZIMG_CHROMA_BOTTOM },
};

static const struct string_table_entry g_matrix_table[] = {
    { "rgb",     ZIMG_MATRIX_RGB },
    { "709",     ZIMG_MATRIX_709 },
    { "unspec",  ZIMG_MATRIX_UNSPECIFIED },
    { "470bg",   ZIMG_MATRIX_470BG },
    { "170m",    ZIMG_MATRIX_170M },
    { "ycgco",   ZIMG_MATRIX_YCGCO },
    { "2020ncl", ZIMG_MATRIX_2020_NCL },
    { "2020cl",  ZIMG_MATRIX_2020_CL },
};

static const struct string_table_entry g_transfer_table[] = {
    { "709",     ZIMG_TRANSFER_709 },
    { "unspec",  ZIMG_TRANSFER_UNSPECIFIED },
    { "601",     ZIMG_TRANSFER_601 },
    { "linear",  ZIMG_TRANSFER_LINEAR },
    { "2020_10", ZIMG_TRANSFER_2020_10 },
    { "2020_12", ZIMG_TRANSFER_2020_12 },
};

static const struct string_table_entry g_primaries_table[] = {
    { "709",    ZIMG_PRIMARIES_709 },
    { "unspec", ZIMG_PRIMARIES_UNSPECIFIED },
    { "170m",   ZIMG_PRIMARIES_170M },
    { "240m",   ZIMG_PRIMARIES_240M },
    { "2020",   ZIMG_PRIMARIES_2020 },
};

static const struct string_table_entry g_dither_type_table[] = {
    { "none",            ZIMG_DITHER_NONE },
    { "ordered",         ZIMG_DITHER_ORDERED },
    { "random",          ZIMG_DITHER_RANDOM },
    { "error_diffusion", ZIMG_DITHER_ERROR_DIFFUSION }
};

static const struct string_table_entry g_resample_filter_table[] = {
    { "point",    ZIMG_RESIZE_POINT },
    { "bilinear", ZIMG_RESIZE_BILINEAR },
    { "bicubic",  ZIMG_RESIZE_BICUBIC },
    { "spline16", ZIMG_RESIZE_SPLINE16 },
    { "spline36", ZIMG_RESIZE_SPLINE36 },
    { "lanczos",  ZIMG_RESIZE_LANCZOS }
};


static int table_lookup_str(const struct string_table_entry *table, size_t n, const char *str, int *out) {
    size_t i = 0;

    for (i = 0; i < n; ++i) {
        if (!strcmp(table[i].str, str)) {
            *out = table[i].val;
            return 0;
        }
    }
    return 1;
}

static int propGetUintDef(const VSAPI *vsapi, const VSMap *map, const char *key, int *out, int def) {
    int err = 0;
    int64_t x;

    if ((x = vsapi->propGetInt(map, key, 0, &err)), err)
        x = def;

    if (x < INT_MIN || x > INT_MAX) {
        return 1;
    } else {
        *out = (unsigned)x;
        return 0;
    }
}

static int propGetSintDef(const VSAPI *vsapi, const VSMap *map, const char *key, int *out, int def) {
    int err = 0;
    int64_t x;

    if ((x = vsapi->propGetInt(map, key, 0, &err)), err)
        x = def;

    if (x < 0 || x > INT_MAX) {
        return 1;
    } else {
        *out = (int)x;
        return 0;
    }
}

static double propGetFloatDef(const VSAPI *vsapi, const VSMap *map, const char *key, double def) {
    int err = 0;
    double x;

    if ((x = vsapi->propGetFloat(map, key, 0, &err)), err)
        x = def;

    return x;
}

static int tryGetEnumStr(const VSAPI *vsapi, const VSMap *map, const char *key, int *out, vszimg_bool *flag, const struct string_table_entry *table, size_t table_size) {
    int err = 0;
    const char *enum_str;

    if ((enum_str = vsapi->propGetData(map, key, 0, &err)), err) {
        *flag = VSZIMG_FALSE;
        return 0;
    }

    if (table_lookup_str(table, table_size, enum_str, out)) {
        return 1;
    } else {
        *flag = VSZIMG_TRUE;
        return 0;
    }
}

static int tryGetEnum(const VSAPI *vsapi, const VSMap *map, const char *key, int *out, vszimg_bool *flag, const struct string_table_entry *table, size_t table_size) {
    char key_str[64];
    int64_t enum_int;
    int err = 0;

    if ((enum_int = vsapi->propGetInt(map, key, 0, &err)), !err) {
        if (enum_int < INT_MIN || enum_int > INT_MAX) {
            return 1;
        } else {
            *out = (int)enum_int;
            *flag = VSZIMG_TRUE;
            return 0;
        }
    } else {
        sprintf(key_str, "%s_s", key);
        return tryGetEnumStr(vsapi, map, key_str, out, flag, table, table_size);
    }
}


static int translate_pixel_type(const VSFormat *format, zimg_pixel_type_e *out) {
    if (format->sampleType == stInteger && format->bytesPerSample == 1)
        *out = ZIMG_PIXEL_BYTE;
    else if (format->sampleType == stInteger && format->bytesPerSample == 2)
        *out = ZIMG_PIXEL_WORD;
    else if (format->sampleType == stFloat && format->bytesPerSample == 2)
        *out = ZIMG_PIXEL_HALF;
    else if (format->sampleType == stFloat && format->bytesPerSample == 4)
        *out = ZIMG_PIXEL_FLOAT;
    else
        return 1;

    return 0;
}

static int translate_color_family(VSColorFamily cf, zimg_color_family_e *out, zimg_matrix_coefficients_e *out_matrix) {
    switch (cf) {
    case cmGray:
        *out = ZIMG_COLOR_GREY;
        *out_matrix = ZIMG_MATRIX_UNSPECIFIED;
        return 0;
    case cmRGB:
        *out = ZIMG_COLOR_RGB;
        *out_matrix = ZIMG_MATRIX_RGB;
        return 0;
    case cmYUV:
        *out = ZIMG_COLOR_YUV;
        *out_matrix = ZIMG_MATRIX_UNSPECIFIED;
        return 0;
    case cmYCoCg:
        *out = ZIMG_COLOR_YUV;
        *out_matrix = ZIMG_MATRIX_YCGCO;
        return 0;
    default:
        return 1;
    }
}

static int translate_vsformat(const VSFormat *vsformat, zimg_image_format *format, char *err_msg) {
#define FAIL(msg) \
  do { \
    sprintf(err_msg, (msg)); \
    return 1; \
  } while (0)

    if (vsformat->id == pfCompatBGR32) {
        format->color_family = ZIMG_COLOR_RGB;
        format->matrix_coefficients = ZIMG_MATRIX_RGB;
        format->pixel_type = ZIMG_PIXEL_BYTE;
        format->depth = 8;
    } else if (vsformat->id == pfCompatYUY2) {
        format->color_family = ZIMG_COLOR_YUV;
        format->matrix_coefficients = ZIMG_MATRIX_UNSPECIFIED;
        format->pixel_type = ZIMG_PIXEL_BYTE;
        format->depth = 8;
    } else {
        if (translate_color_family(vsformat->colorFamily, &format->color_family, &format->matrix_coefficients))
            FAIL("incompatible color family");
        if (translate_pixel_type(vsformat, &format->pixel_type))
            FAIL("incompatible pixel type");

        format->depth = vsformat->bitsPerSample;
    }

    format->subsample_w = vsformat->subSamplingW;
    format->subsample_h = vsformat->subSamplingH;
    format->pixel_range = (format->color_family == ZIMG_COLOR_RGB) ? ZIMG_RANGE_FULL : ZIMG_RANGE_LIMITED;

    format->field_parity = ZIMG_FIELD_PROGRESSIVE;
    format->chroma_location = (format->subsample_w || format->subsample_h) ? ZIMG_CHROMA_LEFT : ZIMG_CHROMA_CENTER;

    return 0;
#undef FAIL
}

static vszimg_bool image_format_eq(const zimg_image_format *a, const zimg_image_format *b) {
    vszimg_bool ret = VSZIMG_TRUE;

    ret = ret && a->width == b->width;
    ret = ret && a->height == b->height;
    ret = ret && a->pixel_type == b->pixel_type;
    ret = ret && a->subsample_w == b->subsample_w;
    ret = ret && a->subsample_h == b->subsample_h;
    ret = ret && a->color_family == b->color_family;

    if (a->color_family != ZIMG_COLOR_GREY) {
        ret = ret && a->matrix_coefficients == b->matrix_coefficients;
        ret = ret && a->transfer_characteristics == b->transfer_characteristics;
        ret = ret && a->color_primaries == b->color_primaries;
    }

    ret = ret && a->depth == b->depth;
    ret = ret && a->pixel_range == b->pixel_range;
    ret = ret && a->field_parity == b->field_parity;

    if (a->color_family == ZIMG_COLOR_YUV && (a->subsample_w || a->subsample_h))
        ret = ret && a->chroma_location == b->chroma_location;

    return ret;
}

static void format_zimg_error(char *err_msg, size_t n) {
    zimg_error_code_e err_code;
    int offset;

    err_code = zimg_get_last_error(NULL, 0);
    offset = sprintf(err_msg, "zimg %d: ", err_code);
    zimg_get_last_error(err_msg + offset, n - offset);
}

static void import_frame_as_read_buffer(const VSFrameRef *frame, zimg_image_buffer_const *buf, unsigned mask, const VSAPI *vsapi) {
    const VSFormat *format = vsapi->getFrameFormat(frame);
    int p;

    for (p = 0; p < format->numPlanes; ++p) {
        buf->plane[p].data = vsapi->getReadPtr(frame, p);
        buf->plane[p].stride = vsapi->getStride(frame, p);
        buf->plane[p].mask = mask;
    }
}

static void import_frame_as_write_buffer(VSFrameRef *frame, zimg_image_buffer *buf, unsigned mask, const VSAPI *vsapi) {
    const VSFormat *format = vsapi->getFrameFormat(frame);
    int p;

    for (p = 0; p < format->numPlanes; ++p) {
        buf->plane[p].data = vsapi->getWritePtr(frame, p);
        buf->plane[p].stride = vsapi->getStride(frame, p);
        buf->plane[p].mask = mask;
    }
}

static int import_frame_props(const VSAPI *vsapi, const VSMap *props, zimg_image_format *format, char *err_msg) {
#define FAIL(name) \
  do { \
    sprintf(err_msg, "%s: bad value", (name)); \
    return 1; \
  } while (0)
#define COPY_INT_POSITIVE(name, out, invalid) \
  do { \
    int tmp; \
    if (propGetSintDef(vsapi, props, name, &tmp, out)) \
      FAIL(name); \
    if (tmp >= 0 && tmp != invalid) \
      (out) = tmp; \
  } while (0)

    int64_t tmp_i64;
    int err = 0;

    COPY_INT_POSITIVE("_ChromaLocation", format->chroma_location, -1);

    if ((tmp_i64 = vsapi->propGetInt(props, "_ColorRange", 0, &err)), !err) {
        if (tmp_i64 == 0)
            format->pixel_range = ZIMG_RANGE_FULL;
        else if (tmp_i64 == 1)
            format->pixel_range = ZIMG_RANGE_LIMITED;
        else
            FAIL("_ColorRange");
    }

    /* Ignore UNSPECIFIED values from properties, since the user can specify them. */
    COPY_INT_POSITIVE("_Matrix", format->matrix_coefficients, ZIMG_MATRIX_UNSPECIFIED);
    COPY_INT_POSITIVE("_Transfer", format->transfer_characteristics, ZIMG_TRANSFER_UNSPECIFIED);
    COPY_INT_POSITIVE("_Primaries", format->color_primaries, ZIMG_PRIMARIES_UNSPECIFIED);

    if ((tmp_i64 = vsapi->propGetInt(props, "_FieldBased", 0, &err)), !err) {
        if (tmp_i64 != 0)
            FAIL("_FieldBased");
    }

    return 0;
#undef FAIL
#undef COPY_INT_POSITIVE
}

static void export_frame_props(const VSAPI *vsapi, const zimg_image_format *format, VSMap *props) {
#define COPY_INT_POSITIVE(name, val) \
  do { \
    if (val >= 0) \
      vsapi->propSetInt(props, (name), (val), paReplace); \
    else \
      vsapi->propDeleteKey(props, (name)); \
  } while (0)

    char version_str[64];

    sprintf(version_str, "%u.%u.%u", g_version_info[0], g_version_info[1], g_version_info[2]);
    vsapi->propSetData(props, "ZimgVersion", version_str, (int)strlen(version_str) + 1, paReplace);

    sprintf(version_str, "%u.%u", g_api_version[0], g_api_version[1]);
    vsapi->propSetData(props, "ZimgApiVersion", version_str, (int)strlen(version_str) + 1, paReplace);

    COPY_INT_POSITIVE("_ChromaLocation", format->chroma_location);

    if (format->pixel_range == ZIMG_RANGE_FULL)
        vsapi->propSetInt(props, "_ColorRange", 0, paReplace);
    else if (format->pixel_range == ZIMG_RANGE_LIMITED)
        vsapi->propSetInt(props, "_ColorRange", 1, paReplace);
    else
        vsapi->propDeleteKey(props, "_ColorRange");

    COPY_INT_POSITIVE("_Matrix", format->matrix_coefficients);
    COPY_INT_POSITIVE("_Transfer", format->transfer_characteristics);
    COPY_INT_POSITIVE("_Primaries", format->color_primaries);

    vsapi->propSetInt(props, "_FieldBased", 0, paReplace);
#undef COPY_INT_POSITIVE
}

static void propagate_sar(const VSAPI *vsapi, const VSMap *src_props, VSMap *dst_props, const zimg_image_format *src_format, const zimg_image_format *dst_format) {
    int64_t sar_num = 0;
    int64_t sar_den = 0;
    int err;

    if ((sar_num = vsapi->propGetInt(src_props, "_SARNum", 0, &err)), err)
        sar_num = 0;
    if ((sar_den = vsapi->propGetInt(src_props, "_SARDen", 0, &err)), err)
        sar_den = 0;

    if (sar_num <= 0 || sar_den <= 0) {
        vsapi->propDeleteKey(dst_props, "_SARNum");
        vsapi->propDeleteKey(dst_props, "_SARDen");
    } else {
        muldivRational(&sar_num, &sar_den, dst_format->width, src_format->width);
        muldivRational(&sar_num, &sar_den, src_format->height, dst_format->height);

        vsapi->propSetInt(dst_props, "_SARNum", sar_num, paReplace);
        vsapi->propSetInt(dst_props, "_SARDen", sar_den, paReplace);
    }
}


struct callback_data {
    zimg_filter_graph_callback cb;
    VSFrameRef *frame_tmp;

    zimg_image_buffer_u plane_buf;
    zimg_image_buffer_u line_buf;
    unsigned height;
};

static const void *buffer_get_line_const(const zimg_image_buffer_const *buf, unsigned p, unsigned i) {
    return (const char *)buf->plane[p].data + (ptrdiff_t)(i & buf->plane[p].mask) * buf->plane[p].stride;
}

static void *buffer_get_line(const zimg_image_buffer *buf, unsigned p, unsigned i) {
    return (char *)buf->plane[p].data + (ptrdiff_t)(i & buf->plane[p].mask) * buf->plane[p].stride;
}

static int graph_unpack_bgr32(void *user, unsigned i, unsigned left, unsigned right) {
    struct callback_data *cb = user;
    const uint8_t *bgr32 = buffer_get_line_const(&cb->plane_buf.c, 0, cb->height - i - 1);
    uint8_t *planar_r = buffer_get_line(&cb->line_buf.m, 0, i);
    uint8_t *planar_g = buffer_get_line(&cb->line_buf.m, 1, i);
    uint8_t *planar_b = buffer_get_line(&cb->line_buf.m, 2, i);
    unsigned j;

    for (j = left; j < right; ++j) {
        uint8_t r, g, b;

        b = bgr32[j * 4 + 0];
        g = bgr32[j * 4 + 1];
        r = bgr32[j * 4 + 2];

        planar_r[j] = r;
        planar_g[j] = g;
        planar_b[j] = b;
    }

    return 0;
}

static int graph_unpack_yuy2(void *user, unsigned i, unsigned left, unsigned right) {
    struct callback_data *cb = user;
    const uint8_t *yuy2 = buffer_get_line_const(&cb->plane_buf.c, 0, i);
    uint8_t *planar_y = buffer_get_line(&cb->line_buf.m, 0, i);
    uint8_t *planar_u = buffer_get_line(&cb->line_buf.m, 1, i);
    uint8_t *planar_v = buffer_get_line(&cb->line_buf.m, 2, i);
    unsigned j;

    left = left % 2 ? left - 1 : left;
    right = right % 2 ? right + 1 : right;

    for (j = left; j < right; j += 2) {
        uint8_t y0, y1, u, v;

        y0 = yuy2[j * 2 + 0];
        u = yuy2[j * 2 + 1];
        y1 = yuy2[j * 2 + 2];
        v = yuy2[j * 2 + 3];

        planar_y[j + 0] = y0;
        planar_y[j + 1] = y1;
        planar_u[j / 2] = u;
        planar_v[j / 2] = v;
    }

    return 0;
}

static int graph_pack_bgr32(void *user, unsigned i, unsigned left, unsigned right) {
    struct callback_data *cb = user;
    const uint8_t *planar_r = buffer_get_line_const(&cb->line_buf.c, 0, i);
    const uint8_t *planar_g = buffer_get_line_const(&cb->line_buf.c, 1, i);
    const uint8_t *planar_b = buffer_get_line_const(&cb->line_buf.c, 2, i);
    uint8_t *bgr32 = buffer_get_line(&cb->plane_buf.m, 0, cb->height - i - 1);
    unsigned j;

    for (j = left; j < right; ++j) {
        uint8_t r, g, b;

        r = planar_r[j];
        g = planar_g[j];
        b = planar_b[j];

        bgr32[j * 4 + 0] = b;
        bgr32[j * 4 + 1] = g;
        bgr32[j * 4 + 2] = r;
    }

    return 0;
}

static int graph_pack_yuy2(void *user, unsigned i, unsigned left, unsigned right) {
    struct callback_data *cb = user;
    const uint8_t *planar_y = buffer_get_line_const(&cb->line_buf.c, 0, i);
    const uint8_t *planar_u = buffer_get_line_const(&cb->line_buf.c, 1, i);
    const uint8_t *planar_v = buffer_get_line_const(&cb->line_buf.c, 2, i);
    uint8_t *yuy2 = buffer_get_line(&cb->plane_buf.m, 0, i);
    unsigned j;

    left = left % 2 ? left - 1 : left;
    right = right % 2 ? right + 1 : right;

    for (j = left; j < right; j += 2) {
        uint8_t y0, y1, u, v;

        y0 = planar_y[j + 0];
        y1 = planar_y[j + 1];
        u = planar_u[j / 2];
        v = planar_v[j / 2];

        yuy2[j * 2 + 0] = y0;
        yuy2[j * 2 + 1] = u;
        yuy2[j * 2 + 2] = y1;
        yuy2[j * 2 + 3] = v;
    }

    return 0;
}

static int callback_init_common(struct callback_data *data, const zimg_filter_graph *graph, vszimg_bool is_pack, const zimg_image_format *format, const VSFormat *vsformat,
    VSCore *core, const VSAPI *vsapi, char *err_msg, size_t err_msg_size) {
    zimg_image_buffer_u buf_initialized = { { ZIMG_API_VERSION } };
    unsigned count;
    unsigned mask;

    data->cb = NULL;
    data->frame_tmp = NULL;
    data->plane_buf = buf_initialized;
    data->line_buf = buf_initialized;
    data->height = format->height;

    if (vsformat->colorFamily != cmCompat)
        return 0;

    if (vsformat->id != pfCompatBGR32 && vsformat->id != pfCompatYUY2) {
        sprintf(err_msg, "unsupported compat format: %d (%s)", vsformat->id, vsformat->name);
        return 1;
    }

    if (is_pack && zimg_filter_graph_get_output_buffering(graph, &count)) {
        format_zimg_error(err_msg, err_msg_size);
        return 1;
    }
    if (!is_pack && zimg_filter_graph_get_input_buffering(graph, &count)) {
        format_zimg_error(err_msg, err_msg_size);
        return 1;
    }
    mask = zimg_select_buffer_mask(count);
    count = (mask == ZIMG_BUFFER_MAX) ? format->height : mask + 1;

    data->frame_tmp = vsapi->newVideoFrame(
        vsapi->registerFormat(cmYUV, stInteger, 8, vsformat->subSamplingW, vsformat->subSamplingH, core),
        format->width,
        count,
        NULL,
        core);
    import_frame_as_write_buffer(data->frame_tmp, &data->line_buf.m, mask, vsapi);

    return 0;
}

static int callback_init_unpack(struct callback_data *data, const zimg_filter_graph *graph, const VSFrameRef *frame, const zimg_image_format *format, const VSFormat *vsformat,
    VSCore *core, const VSAPI *vsapi, char *err_msg, size_t err_msg_size) {
    if (callback_init_common(data, graph, VSZIMG_FALSE, format, vsformat, core, vsapi, err_msg, err_msg_size))
        goto fail;

    import_frame_as_read_buffer(frame, &data->plane_buf.c, ZIMG_BUFFER_MAX, vsapi);

    if (vsformat->colorFamily != cmCompat) {
        data->line_buf = data->plane_buf;
        return 0;
    }

    data->cb = (vsformat->id == pfCompatBGR32) ? graph_unpack_bgr32 : graph_unpack_yuy2;
    return 0;
fail:
    vsapi->freeFrame(data->frame_tmp);
    return 1;
}

static int callback_init_pack(struct callback_data *data, const zimg_filter_graph *graph, VSFrameRef *frame, const zimg_image_format *format, const VSFormat *vsformat,
    VSCore *core, const VSAPI *vsapi, char *err_msg, size_t err_msg_size) {
    if (callback_init_common(data, graph, VSZIMG_TRUE, format, vsformat, core, vsapi, err_msg, err_msg_size))
        goto fail;

    import_frame_as_write_buffer(frame, &data->plane_buf.m, ZIMG_BUFFER_MAX, vsapi);

    if (vsformat->colorFamily != cmCompat) {
        data->line_buf = data->plane_buf;
        return 0;
    }

    data->cb = (vsformat->id == pfCompatBGR32) ? graph_pack_bgr32 : graph_pack_yuy2;
    return 0;
fail:
    vsapi->freeFrame(data->frame_tmp);
    return 1;
}

static void callback_destroy(struct callback_data *data, const VSAPI *vsapi) {
    if (!data)
        return;

    vsapi->freeFrame(data->frame_tmp);
}


struct vszimg_graph_data {
    zimg_filter_graph *graph;
    zimg_image_format src_format;
    zimg_image_format dst_format;
    int ref_count;
};

struct vszimg_data {
    struct vszimg_graph_data *graph_data;
    vszimg_mutex_t graph_mutex;
    vszimg_bool graph_mutex_initialized;

    VSNodeRef *node;
    VSVideoInfo vi;
    zimg_graph_builder_params params;

    zimg_matrix_coefficients_e matrix;
    zimg_transfer_characteristics_e transfer;
    zimg_color_primaries_e primaries;
    zimg_pixel_range_e range;
    zimg_chroma_location_e chromaloc;

    zimg_matrix_coefficients_e matrix_in;
    zimg_transfer_characteristics_e transfer_in;
    zimg_color_primaries_e primaries_in;
    zimg_pixel_range_e range_in;
    zimg_chroma_location_e chromaloc_in;

    vszimg_bool have_matrix;
    vszimg_bool have_transfer;
    vszimg_bool have_primaries;
    vszimg_bool have_range;
    vszimg_bool have_chromaloc;

    vszimg_bool have_matrix_in;
    vszimg_bool have_transfer_in;
    vszimg_bool have_primaries_in;
    vszimg_bool have_range_in;
    vszimg_bool have_chromaloc_in;
};

static void _vszimg_default_init(struct vszimg_data *data) {
    memset(data, 0, sizeof(*data));

    data->graph_data = NULL;
    data->graph_mutex_initialized = VSZIMG_FALSE;
    data->node = NULL;
}

static void _vszimg_destroy(struct vszimg_data *data, const VSAPI *vsapi) {
    if (!data)
        return;

    if (data->graph_data)
        zimg_filter_graph_free(data->graph_data->graph);
    if (data->graph_mutex_initialized)
        vszimg_mutex_destroy(&data->graph_mutex);

    free(data->graph_data);
    vsapi->freeNode(data->node);
}

static void _vszimg_set_src_colorspace(const struct vszimg_data *data, zimg_image_format *src_format) {
    if (data->have_matrix_in)
        src_format->matrix_coefficients = data->matrix_in;
    if (data->have_transfer_in)
        src_format->transfer_characteristics = data->transfer_in;
    if (data->have_primaries_in)
        src_format->color_primaries = data->primaries_in;
    if (data->have_range_in)
        src_format->pixel_range = data->range_in;
    if (data->have_chromaloc_in)
        src_format->chroma_location = data->chromaloc_in;
}

static void _vszimg_set_dst_colorspace(const struct vszimg_data *data, const zimg_image_format *src_format, zimg_image_format *dst_format) {
    /* Avoid propagating RGB matrix coefficients. */
    if (src_format->color_family != ZIMG_COLOR_RGB && dst_format->color_family != ZIMG_COLOR_RGB)
        dst_format->matrix_coefficients = src_format->matrix_coefficients;

    dst_format->transfer_characteristics = src_format->transfer_characteristics;
    dst_format->color_primaries = src_format->color_primaries;

    /* Avoid propagating source pixel range and chroma location if color family changes. */
    if (dst_format->color_family == src_format->color_family) {
        dst_format->pixel_range = src_format->pixel_range;
        dst_format->chroma_location = src_format->chroma_location;
    }

    if (data->have_matrix)
        dst_format->matrix_coefficients = data->matrix;
    if (data->have_transfer)
        dst_format->transfer_characteristics = data->transfer;
    if (data->have_primaries)
        dst_format->color_primaries = data->primaries;
    if (data->have_range)
        dst_format->pixel_range = data->range;
    if (data->have_chromaloc)
        dst_format->chroma_location = data->chromaloc;
}

static void _vszimg_release_graph_data_unsafe(struct vszimg_graph_data *graph_data) {
    if (!graph_data)
        return;
    if (--graph_data->ref_count == 0) {
        zimg_filter_graph_free(graph_data->graph);
        free(graph_data);
    }
}

static void _vszimg_release_graph_data(struct vszimg_data *data, struct vszimg_graph_data *graph_data) {
    if (!graph_data)
        return;
    if (vszimg_mutex_lock(&data->graph_mutex))
        return;

    _vszimg_release_graph_data_unsafe(graph_data);

    vszimg_mutex_unlock(&data->graph_mutex);
}

static struct vszimg_graph_data *_vszimg_get_graph_data(struct vszimg_data *data, const zimg_image_format *src_format, const zimg_image_format *dst_format,
    char *err_msg, size_t err_msg_size) {
    struct vszimg_graph_data *graph_data = NULL;
    struct vszimg_graph_data *ret = NULL;
    vszimg_bool mutex_locked = VSZIMG_FALSE;
    vszimg_bool allocate_new;

    if (vszimg_mutex_lock(&data->graph_mutex)) {
        sprintf(err_msg, "error locking mutex");
        goto fail;
    }
    mutex_locked = VSZIMG_TRUE;

    allocate_new = !data->graph_data ||
        (!image_format_eq(&data->graph_data->src_format, src_format) ||
            !image_format_eq(&data->graph_data->dst_format, dst_format));

    if (allocate_new) {
        if (!(graph_data = malloc(sizeof(*graph_data)))) {
            sprintf(err_msg, "error allocating vszimg_graph_data");
            goto fail;
        }
        graph_data->graph = NULL;
        graph_data->ref_count = 1;

        if (!(graph_data->graph = zimg_filter_graph_build(src_format, dst_format, &data->params))) {
            format_zimg_error(err_msg, err_msg_size);
            goto fail;
        }

        graph_data->src_format = *src_format;
        graph_data->dst_format = *dst_format;

        _vszimg_release_graph_data_unsafe(data->graph_data);

        ++graph_data->ref_count;
        data->graph_data = graph_data;

        ret = graph_data;
        graph_data = NULL;
    } else {
        ++data->graph_data->ref_count;
        ret = data->graph_data;
    }
fail:
    if (mutex_locked)
        vszimg_mutex_unlock(&data->graph_mutex);

    _vszimg_release_graph_data(data, graph_data);
    return ret;
}

static const VSFrameRef *_vszimg_get_frame(struct vszimg_data *data, const VSFrameRef *src_frame, VSCore *core, const VSAPI *vsapi, char *err_msg, size_t err_msg_size) {
    struct vszimg_graph_data *graph_data = NULL;
    VSFrameRef *dst_frame = NULL;
    VSFrameRef *ret = NULL;
    void *tmp = NULL;

    struct callback_data unpack_cb_data = { 0 };
    struct callback_data pack_cb_data = { 0 };

    zimg_image_format src_format;
    zimg_image_format dst_format;
    const VSFormat *src_vsformat;
    const VSFormat *dst_vsformat;

    const VSMap *src_props;
    VSMap *dst_props;
    size_t tmp_size;

    zimg_image_format_default(&src_format, ZIMG_API_VERSION);
    zimg_image_format_default(&dst_format, ZIMG_API_VERSION);

    src_props = vsapi->getFramePropsRO(src_frame);
    src_vsformat = vsapi->getFrameFormat(src_frame);
    dst_vsformat = data->vi.format ? data->vi.format : src_vsformat;

    src_format.width = vsapi->getFrameWidth(src_frame, 0);
    src_format.height = vsapi->getFrameHeight(src_frame, 0);

    dst_format.width = data->vi.width ? (unsigned)data->vi.width : src_format.width;
    dst_format.height = data->vi.height ? (unsigned)data->vi.height : src_format.height;

    if (translate_vsformat(src_vsformat, &src_format, err_msg))
        goto fail;
    if (translate_vsformat(dst_vsformat, &dst_format, err_msg))
        goto fail;

    _vszimg_set_src_colorspace(data, &src_format);

    if (import_frame_props(vsapi, src_props, &src_format, err_msg))
        goto fail;

    _vszimg_set_dst_colorspace(data, &src_format, &dst_format);

    if (!(graph_data = _vszimg_get_graph_data(data, &src_format, &dst_format, err_msg, err_msg_size)))
        goto fail;

    dst_frame = vsapi->newVideoFrame(dst_vsformat, dst_format.width, dst_format.height, src_frame, core);
    dst_props = vsapi->getFramePropsRW(dst_frame);

    if (callback_init_unpack(&unpack_cb_data, graph_data->graph, src_frame, &src_format, src_vsformat, core, vsapi, err_msg, err_msg_size))
        goto fail;
    if (callback_init_pack(&pack_cb_data, graph_data->graph, dst_frame, &dst_format, dst_vsformat, core, vsapi, err_msg, err_msg_size))
        goto fail;

    if (zimg_filter_graph_get_tmp_size(graph_data->graph, &tmp_size)) {
        format_zimg_error(err_msg, err_msg_size);
        goto fail;
    }

    VS_ALIGNED_MALLOC(&tmp, tmp_size, 64);
    if (!tmp) {
        sprintf(err_msg, "error allocating temporary buffer");
        goto fail;
    }

    if (zimg_filter_graph_process(graph_data->graph,
        &unpack_cb_data.line_buf.c,
        &pack_cb_data.line_buf.m,
        tmp,
        unpack_cb_data.cb,
        &unpack_cb_data,
        pack_cb_data.cb,
        &pack_cb_data)) {
        format_zimg_error(err_msg, err_msg_size);
        goto fail;
    }

    propagate_sar(vsapi, src_props, dst_props, &src_format, &dst_format);
    export_frame_props(vsapi, &dst_format, dst_props);

    ret = dst_frame;
    dst_frame = NULL;
fail:
    _vszimg_release_graph_data(data, graph_data);
    vsapi->freeFrame(dst_frame);
    VS_ALIGNED_FREE(tmp);
    callback_destroy(&unpack_cb_data, vsapi);
    callback_destroy(&pack_cb_data, vsapi);
    return ret;
}

static void VS_CC vszimg_init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    struct vszimg_data *data = *instanceData;
    vsapi->setVideoInfo(&data->vi, 1, node);
}

static const VSFrameRef * VS_CC vszimg_get_frame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    struct vszimg_data *data = *instanceData;
    const VSFrameRef *src_frame = NULL;
    const VSFrameRef *ret = NULL;
    char err_msg[1024];
    int err_flag = 1;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, data->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        src_frame = vsapi->getFrameFilter(n, data->node, frameCtx);
        if (!(ret = _vszimg_get_frame(data, src_frame, core, vsapi, err_msg, sizeof(err_msg))))
            goto fail;
    }

    err_flag = 0;
fail:
    if (err_flag)
        vsapi->setFilterError(err_msg, frameCtx);

    vsapi->freeFrame(src_frame);
    return ret;
}

static void VS_CC vszimg_free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    struct vszimg_data *data = instanceData;

    _vszimg_destroy(data, vsapi);
    free(data);
}

static void VS_CC vszimg_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    struct vszimg_data *data = NULL;

    const VSVideoInfo *node_vi;
    const VSFormat *node_fmt;
    int format_id;

    char err_msg[64];

#define FAIL_BAD_VALUE(name) \
  do { \
    sprintf(err_msg, "%s: bad value", (name)); \
    goto fail; \
  } while (0)

#define TRY_GET_ENUM(name, out, flag, table) \
  do { \
    int enum_tmp; \
    if (tryGetEnum(vsapi, in, (name), &enum_tmp, &(flag), (table), ARRAY_SIZE((table)))) \
      FAIL_BAD_VALUE(name); \
    if ((flag)) \
      (out) = enum_tmp; \
  } while (0)

#define TRY_GET_ENUM_STR(name, out, table) \
  do { \
    int enum_tmp; \
    vszimg_bool flag; \
    if (tryGetEnumStr(vsapi, in, (name), &enum_tmp, &flag, (table), ARRAY_SIZE((table)))) \
      FAIL_BAD_VALUE(name); \
    if ((flag)) \
      (out) = enum_tmp; \
  } while (0)

    if (!(data = malloc(sizeof(*data)))) {
        sprintf(err_msg, "error allocating vszimg_data");
        goto fail;
    }

    _vszimg_default_init(data);

    if (vszimg_mutex_init(&data->graph_mutex)) {
        sprintf(err_msg, "error initializing mutex");
        goto fail;
    }
    data->graph_mutex_initialized = VSZIMG_TRUE;

    data->node = vsapi->propGetNode(in, "clip", 0, NULL);
    node_vi = vsapi->getVideoInfo(data->node);
    node_fmt = node_vi->format;

    data->vi = *node_vi;

    zimg_graph_builder_params_default(&data->params, ZIMG_API_VERSION);

    if (propGetUintDef(vsapi, in, "width", &data->vi.width, node_vi->width))
        FAIL_BAD_VALUE("width");
    if (propGetUintDef(vsapi, in, "height", &data->vi.height, node_vi->height))
        FAIL_BAD_VALUE("height");

    if (propGetSintDef(vsapi, in, "format", &format_id, pfNone))
        FAIL_BAD_VALUE("format");
    data->vi.format = (format_id == pfNone) ? node_fmt : vsapi->getFormatPreset(format_id, core);

    TRY_GET_ENUM("matrix", data->matrix, data->have_matrix, g_matrix_table);
    TRY_GET_ENUM("transfer", data->transfer, data->have_transfer, g_transfer_table);
    TRY_GET_ENUM("primaries", data->primaries, data->have_primaries, g_primaries_table);
    TRY_GET_ENUM("range", data->range, data->have_range, g_range_table);
    TRY_GET_ENUM("chromaloc", data->chromaloc, data->have_chromaloc, g_chromaloc_table);

    TRY_GET_ENUM("matrix_in", data->matrix_in, data->have_matrix_in, g_matrix_table);
    TRY_GET_ENUM("transfer_in", data->transfer_in, data->have_transfer_in, g_transfer_table);
    TRY_GET_ENUM("primaries_in", data->primaries_in, data->have_primaries_in, g_primaries_table);
    TRY_GET_ENUM("range_in", data->range_in, data->have_range_in, g_range_table);
    TRY_GET_ENUM("chromaloc_in", data->chromaloc_in, data->have_chromaloc_in, g_chromaloc_table);

    data->params.resample_filter = (intptr_t)userData;
    data->params.filter_param_a = propGetFloatDef(vsapi, in, "filter_param_a", data->params.filter_param_a);
    data->params.filter_param_b = propGetFloatDef(vsapi, in, "filter_param_b", data->params.filter_param_b);

    TRY_GET_ENUM_STR("resample_filter_uv", data->params.resample_filter_uv, g_resample_filter_table);
    data->params.filter_param_a_uv = propGetFloatDef(vsapi, in, "filter_param_a_uv", data->params.filter_param_a_uv);
    data->params.filter_param_b_uv = propGetFloatDef(vsapi, in, "filter_param_b_uv", data->params.filter_param_b_uv);

    if (vsapi->propNumElements(in, "resample_filter_uv") <= 0)
        data->params.resample_filter_uv = data->params.resample_filter;

    TRY_GET_ENUM_STR("dither_type", data->params.dither_type, g_dither_type_table);
    TRY_GET_ENUM_STR("cpu_type", data->params.cpu_type, g_cpu_type_table);

#undef FAIL_BAD_VALUE
#undef TRY_GET_ENUM
#undef TRY_GET_ENUM_STR

    /* Basic compatibility check. */
    if (isConstantFormat(node_vi) && isConstantFormat(&data->vi)) {
        zimg_image_format src_format;
        zimg_image_format dst_format;

        zimg_image_format_default(&src_format, ZIMG_API_VERSION);
        zimg_image_format_default(&dst_format, ZIMG_API_VERSION);

        src_format.width = node_vi->width;
        src_format.height = node_vi->height;

        dst_format.width = data->vi.width;
        dst_format.height = data->vi.height;

        if (translate_vsformat(node_vi->format, &src_format, err_msg))
            goto fail;
        if (translate_vsformat(data->vi.format, &dst_format, err_msg))
            goto fail;
    }

    vsapi->createFilter(in, out, "format", vszimg_init, vszimg_get_frame, vszimg_free, fmParallel, 0, data, core);
    return;
fail:
    vsapi->setError(out, err_msg);
    _vszimg_destroy(data, vsapi);
    free(data);
}

void VS_CC resizeInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
#define INT_OPT(x) #x":int:opt;"
#define FLOAT_OPT(x) #x":float:opt;"
#define DATA_OPT(x) #x":data:opt;"
#define ENUM_OPT(x) INT_OPT(x)DATA_OPT(x ## _s)
    static const char FORMAT_DEFINITION[] =
        "clip:clip;"
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
        //DATA_OPT(resample_filter)
        FLOAT_OPT(filter_param_a)
        FLOAT_OPT(filter_param_b)
        DATA_OPT(resample_filter_uv)
        FLOAT_OPT(filter_param_a_uv)
        FLOAT_OPT(filter_param_b_uv)
        DATA_OPT(dither_type)
        DATA_OPT(cpu_type);
#undef INT_OPT
#undef FLOAT_OPT
#undef DATA_OPT
#undef ENUM_OPT

    zimg_get_version_info(&g_version_info[0], &g_version_info[1], &g_version_info[2]);
    zimg_get_api_version(&g_api_version[0], &g_api_version[1]);

    configFunc("com.vapoursynth.resize", "resize", "VapourSynth Resize", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Bilinear", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_BILINEAR, plugin);
    registerFunc("Bicubic", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_BICUBIC, plugin);
    registerFunc("Point", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_POINT, plugin);
    registerFunc("Lanczos", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_LANCZOS, plugin);
    registerFunc("Spline16", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_SPLINE16, plugin);
    registerFunc("Spline36", FORMAT_DEFINITION, vszimg_create, (void *)ZIMG_RESIZE_SPLINE36, plugin);
}