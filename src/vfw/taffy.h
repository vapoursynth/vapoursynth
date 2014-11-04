#ifndef TAFFY_H
#define TAFFY_H

#ifdef TAFFY_LIBRARY
#   ifdef __cplusplus
#       define TAFFY_EXTERN_C extern "C"
#else
#       define TAFFY_EXTERN_C
#   endif

#   if defined(_WIN32) && !defined(_WIN64)
#       define TAFFY_CC __stdcall
#   else
#       define TAFFY_CC
#   endif

#   if defined(_WIN32)
#       define TAFFY_EXTERNAL_API(ret) TAFFY_EXTERN_C __declspec(dllexport) ret TAFFY_CC
#   elif defined(__GNUC__) && __GNUC__ >= 4
#       define TAFFY_EXTERNAL_API(ret) TAFFY_EXTERN_C __attribute__((visibility("default"))) ret TAFFY_CC
#   else
#       define TAFFY_EXTERNAL_API(ret) TAFFY_EXTERN_C ret TAFFY_CC
#   endif

#   if !defined(TAFFY_CORE_EXPORTS) && defined(_WIN32)
#       define TAFFY_API(ret) TAFFY_EXTERN_C __declspec(dllimport) ret TAFFY_CC
#   else
#       define TAFFY_API(ret) TAFFY_EXTERNAL_API(ret)
#   endif
#else
#   define TAFFY_API(ret) ret
#endif


typedef struct taffy_param {
    int width[4];
    int height[4];

    const void *srcp[4];
    void *dstp[4];

    int src_stride[4];
    int dst_stride[4];
} taffy_param;



TAFFY_API(void) taffy_pack_px10(taffy_param *args);
TAFFY_API(void) taffy_unpack_px10(taffy_param *args);


TAFFY_API(void) taffy_pack_px16(taffy_param *args);
TAFFY_API(void) taffy_unpack_px16(taffy_param *args);


TAFFY_API(int) taffy_get_v210_stride(int width);

TAFFY_API(void) taffy_pack_v210(taffy_param *args);
TAFFY_API(void) taffy_unpack_v210(taffy_param *args);


TAFFY_API(void) taffy_pack_4444_uint8(taffy_param *args);
TAFFY_API(void) taffy_unpack_4444_uint8(taffy_param *args);

TAFFY_API(void) taffy_pack_4444_uint16(taffy_param *args);
TAFFY_API(void) taffy_unpack_4444_uint16(taffy_param *args);

TAFFY_API(void) taffy_pack_4444_uint32(taffy_param *args);
TAFFY_API(void) taffy_unpack_4444_uint32(taffy_param *args);


TAFFY_API(void) taffy_pack_444_uint8(taffy_param *args);
TAFFY_API(void) taffy_unpack_444_uint8(taffy_param *args);

TAFFY_API(void) taffy_pack_444_uint16(taffy_param *args);
TAFFY_API(void) taffy_unpack_444_uint16(taffy_param *args);

TAFFY_API(void) taffy_pack_444_uint32(taffy_param *args);
TAFFY_API(void) taffy_unpack_444_uint32(taffy_param *args);


TAFFY_API(void) taffy_pack_422_uint8(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uint8(taffy_param *args);

TAFFY_API(void) taffy_pack_422_uint16(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uint16(taffy_param *args);

TAFFY_API(void) taffy_pack_422_uint32(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uint32(taffy_param *args);


TAFFY_API(void) taffy_pack_422_uyvy_uint8(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uyvy_uint8(taffy_param *args);

TAFFY_API(void) taffy_pack_422_uyvy_uint16(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uyvy_uint16(taffy_param *args);

TAFFY_API(void) taffy_pack_422_uyvy_uint32(taffy_param *args);
TAFFY_API(void) taffy_unpack_422_uyvy_uint32(taffy_param *args);


TAFFY_API(void) taffy_pack_nv_uint8(taffy_param *args);
TAFFY_API(void) taffy_unpack_nv_uint8(taffy_param *args);

TAFFY_API(void) taffy_pack_nv_uint16(taffy_param *args);
TAFFY_API(void) taffy_unpack_nv_uint16(taffy_param *args);

TAFFY_API(void) taffy_pack_nv_uint32(taffy_param *args);
TAFFY_API(void) taffy_unpack_nv_uint32(taffy_param *args);

#endif
