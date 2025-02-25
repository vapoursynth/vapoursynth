#ifndef AVERAGE_H
#define AVERAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void vs_average_plane_byte_luma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_byte_chroma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_luma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_chroma_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_float_c(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);

#ifdef VS_TARGET_CPU_X86
void vs_average_plane_byte_luma_sse2(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_byte_chroma_sse2(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_luma_sse2(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_chroma_sse2(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_float_sse2(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
#elif defined(VS_TARGET_CPU_ARM)
void vs_average_plane_byte_luma_neon(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_byte_chroma_neon(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_luma_neon(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_word_chroma_neon(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
void vs_average_plane_float_neon(const void *weights, const void * const *srcs, unsigned num_srcs, void *dst, const void *scale, unsigned depth, unsigned w, unsigned h, ptrdiff_t stride);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* AVERAGE_H */
