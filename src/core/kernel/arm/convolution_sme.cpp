/*
* Copyright (c) 2012-2026 Fredrik Mellbin
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

/*
* SME2 streaming-mode convolution kernels: square NxN and vertical 1D.
*
* Formulation: a convolution over a block of TR output rows is a banded
* (Toeplitz) matrix product. For every contributing source row, one outer
* product accumulates coeff-band x pixel-row into ZA tiles:
*
*   out[i][j] = sum_s sum_k coeff[s - i][k] * src_row(s)[j + k - S]
*   ZA[i][j] += band(s)[i] (x) row(s)[j + k - S]     (one MOPA per (s, k))
*
* Integer paths pack two adjacent taps (k, k+1) into the inherent 2-way
* int16 dot product of SMOPA za32, so each MOPA covers two taps. Pixels
* widen to int16 (byte: zero-extended, always non-negative; word: biased by
* INT16_MIN, with the coefficient*bias sum subtracted on store -- the same
* trick the x86 kernels use). Sums are integer and order-independent, so
* results are bit-exact with the C reference. word 9x9/11x11 would overflow
* int32 in the biased domain, so they use the 4-way int16 SMOPA za64
* (FEAT_SME_I16I64) with int64 accumulators, again bit-exact. Float uses
* FMOPA za32 (fused, not bit-exact with C by design, like x86 AVX2+).
*
* Structure per plane: a non-streaming wrapper precomputes mirrored row
* pointers, the coefficient band vectors and the scalar mirror-edge columns,
* then enters ONE __arm_locally_streaming __arm_new("za") function for the
* whole interior. Everything vector-length-agnostic (svcnt*), so any SVL
* works. Every function that touches vectors in this TU is streaming: with
* +sme enabled the auto-vectoriser could otherwise emit streaming-only SVE in
* regular functions, which faults outside streaming mode.
*
* The vertical 1D row selection collapses to a pure function of (i + k) only
* when height > support; smaller planes take the C kernel instead.
*/

#include <arm_sme.h>
#include <cstdint>
#include <vector>
#include "../generic.h"
#include "conv_scalar.h"

namespace {

// ---- streaming helpers -------------------------------------------------------

// Immediate-operand dispatch for ZA32 tiles 0-3.
__attribute__((always_inline))
inline void mopa32_i16(unsigned t, svbool_t pn, svbool_t pm, svint16_t zn, svint16_t zm) __arm_streaming __arm_inout("za")
{
    switch (t) {
    case 0: svmopa_za32_s16_m(0, pn, pm, zn, zm); break;
    case 1: svmopa_za32_s16_m(1, pn, pm, zn, zm); break;
    case 2: svmopa_za32_s16_m(2, pn, pm, zn, zm); break;
    default: svmopa_za32_s16_m(3, pn, pm, zn, zm); break;
    }
}

__attribute__((always_inline))
inline void mopa32_f32(unsigned t, svbool_t pn, svbool_t pm, svfloat32_t zn, svfloat32_t zm) __arm_streaming __arm_inout("za")
{
    switch (t) {
    case 0: svmopa_za32_f32_m(0, pn, pm, zn, zm); break;
    case 1: svmopa_za32_f32_m(1, pn, pm, zn, zm); break;
    case 2: svmopa_za32_f32_m(2, pn, pm, zn, zm); break;
    default: svmopa_za32_f32_m(3, pn, pm, zn, zm); break;
    }
}

__attribute__((always_inline))
inline void mopa32_f16(unsigned t, svbool_t pn, svbool_t pm, svfloat16_t zn, svfloat16_t zm) __arm_streaming __arm_inout("za")
{
    switch (t) {
    case 0: svmopa_za32_f16_m(0, pn, pm, zn, zm); break;
    case 1: svmopa_za32_f16_m(1, pn, pm, zn, zm); break;
    case 2: svmopa_za32_f16_m(2, pn, pm, zn, zm); break;
    default: svmopa_za32_f16_m(3, pn, pm, zn, zm); break;
    }
}

__attribute__((always_inline))
inline void mopa64_i16(unsigned t, svbool_t pn, svbool_t pm, svint16_t zn, svint16_t zm) __arm_streaming __arm_inout("za")
{
    switch (t) {
    case 0: svmopa_za64_s16_m(0, pn, pm, zn, zm); break;
    case 1: svmopa_za64_s16_m(1, pn, pm, zn, zm); break;
    case 2: svmopa_za64_s16_m(2, pn, pm, zn, zm); break;
    default: svmopa_za64_s16_m(3, pn, pm, zn, zm); break;
    }
}

__attribute__((always_inline))
inline svint32_t read_za32_s32(unsigned t, uint32_t slice) __arm_streaming __arm_in("za")
{
    svbool_t pg = svptrue_b32();
    svint32_t z = svdup_s32(0);
    switch (t) {
    case 0: return svread_hor_za32_s32_m(z, pg, 0, slice);
    case 1: return svread_hor_za32_s32_m(z, pg, 1, slice);
    case 2: return svread_hor_za32_s32_m(z, pg, 2, slice);
    default: return svread_hor_za32_s32_m(z, pg, 3, slice);
    }
}

__attribute__((always_inline))
inline svfloat32_t read_za32_f32(unsigned t, uint32_t slice) __arm_streaming __arm_in("za")
{
    svbool_t pg = svptrue_b32();
    svfloat32_t z = svdup_f32(0.0f);
    switch (t) {
    case 0: return svread_hor_za32_f32_m(z, pg, 0, slice);
    case 1: return svread_hor_za32_f32_m(z, pg, 1, slice);
    case 2: return svread_hor_za32_f32_m(z, pg, 2, slice);
    default: return svread_hor_za32_f32_m(z, pg, 3, slice);
    }
}

__attribute__((always_inline))
inline svint64_t read_za64_s64(unsigned t, uint32_t slice) __arm_streaming __arm_in("za")
{
    svbool_t pg = svptrue_b64();
    svint64_t z = svdup_s64(0);
    switch (t) {
    case 0: return svread_hor_za64_s64_m(z, pg, 0, slice);
    case 1: return svread_hor_za64_s64_m(z, pg, 1, slice);
    case 2: return svread_hor_za64_s64_m(z, pg, 2, slice);
    default: return svread_hor_za64_s64_m(z, pg, 3, slice);
    }
}

// SME2 multi-vector ZA reads: one instruction pulls four consecutive
// horizontal slices (output rows) of a tile, cutting the store-side ZA
// readout instruction count 4x versus the slice-at-a-time reads above. This
// pays off in the vertical 1D kernels, where the readout is a large share of
// the per-block work; the square kernels see no benefit (the outer products
// dominate) and are left on the single-slice path.
__attribute__((always_inline))
inline svint32x4_t read_za32_s32_x4(unsigned t, uint32_t base) __arm_streaming __arm_in("za")
{
    switch (t) {
    case 0: return svread_hor_za32_s32_vg4(0, base);
    case 1: return svread_hor_za32_s32_vg4(1, base);
    case 2: return svread_hor_za32_s32_vg4(2, base);
    default: return svread_hor_za32_s32_vg4(3, base);
    }
}

__attribute__((always_inline))
inline svfloat32x4_t read_za32_f32_x4(unsigned t, uint32_t base) __arm_streaming __arm_in("za")
{
    switch (t) {
    case 0: return svread_hor_za32_f32_vg4(0, base);
    case 1: return svread_hor_za32_f32_vg4(1, base);
    case 2: return svread_hor_za32_f32_vg4(2, base);
    default: return svread_hor_za32_f32_vg4(3, base);
    }
}

// Pixel row loads, widened to the int16 MOPA input domain.
__attribute__((always_inline))
inline svint16_t load_px_i16(const uint8_t *p, svbool_t pg, bool word) __arm_streaming
{
    if (word)
        return svreinterpret_s16_u16(sveor_n_u16_x(pg, svld1_u16(pg, reinterpret_cast<const uint16_t *>(p)), 0x8000u));
    else
        return svld1ub_s16(pg, p);
}

// scale/bias/saturate + round + clamp + narrowing store of one int32 tile row.
__attribute__((always_inline))
inline void store_row_int(uint8_t *dst, svbool_t pgw, svint32_t acc, int32_t weight_bias,
                          float div, float bias, uint32_t satmask, int32_t maxclamp, bool word) __arm_streaming
{
    svbool_t pt = svptrue_b32();
    acc = svsub_n_s32_x(pt, acc, weight_bias);
    svfloat32_t f = svcvt_f32_s32_x(pt, acc);
    f = svmad_n_f32_x(pt, f, svdup_f32(div), bias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt, svreinterpret_u32_f32(f), satmask));
    f = svrintn_f32_x(pt, f);
    svint32_t r = svcvt_s32_f32_x(pt, f);
    r = svmax_n_s32_x(pt, r, 0);
    r = svmin_n_s32_x(pt, r, maxclamp);
    if (word)
        svst1h_s32(pgw, reinterpret_cast<int16_t *>(dst), r);
    else
        svst1b_s32(pgw, reinterpret_cast<int8_t *>(dst), r);
}

// ---- square NxN, int paths, ZA32 (byte all N; word N <= 7) --------------------

__arm_locally_streaming __arm_new("za")
void sme_sq_int32_interior(const uint8_t *const *rows, ptrdiff_t elem_size, uint8_t *dst, ptrdiff_t dst_stride,
                           const int16_t *band, unsigned N, unsigned W, unsigned H,
                           int32_t weight_bias, float div, float fbias, uint32_t satmask, int32_t maxclamp)
{
    const bool word = elem_size == 2;
    const unsigned S = N / 2;
    const unsigned P = (N + 1) / 2;
    const unsigned TR = static_cast<unsigned>(svcntw());
    const unsigned Wend = W - S;
    const svbool_t pt16 = svptrue_b16();

    for (unsigned y = 0; y < H; y += TR) {
        unsigned vr = std::min(TR, H - y);
        for (unsigned cb = S; cb < Wend; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < Wend ? std::min(TR, Wend - base) : 0;
            }
            svzero_za();
            for (unsigned d = 0; d < vr + N - 1; ++d) {
                const uint8_t *row = rows[y + d];
                for (unsigned p = 0; p < P; ++p) {
                    svint16_t zn = svld1_s16(pt16, band + (static_cast<size_t>(d) * P + p) * 2 * TR);
                    unsigned k0 = 2 * p;
                    for (unsigned t = 0; t < 4 && v[t]; ++t) {
                        svbool_t pgh = svwhilelt_b16_u32(0, v[t]);
                        const uint8_t *base = row + (static_cast<size_t>(cb) + t * TR + k0 - S) * elem_size;
                        svint16_t va = load_px_i16(base, pgh, word);
                        svint16_t vb = k0 + 1 < N ? load_px_i16(base + elem_size, pgh, word) : svdup_s16(0);
                        mopa32_i16(t, pt16, pt16, zn, svzip1_s16(va, vb));
                    }
                }
            }
            for (unsigned i = 0; i < vr; ++i) {
                uint8_t *drow = dst + static_cast<ptrdiff_t>(y + i) * dst_stride;
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                    store_row_int(drow + (static_cast<size_t>(cb) + t * TR) * elem_size, pgw,
                                  read_za32_s32(t, i), weight_bias, div, fbias, satmask, maxclamp, word);
                }
            }
        }
    }
}

// ---- square NxN, word with int64 accumulation, ZA64 (N >= 9) ------------------

__arm_locally_streaming __arm_new("za")
void sme_sq_int64_interior(const uint8_t *const *rows, uint8_t *dst, ptrdiff_t dst_stride,
                           const int16_t *band, unsigned N, unsigned W, unsigned H,
                           int64_t weight_bias, float div, float fbias, uint32_t satmask, int32_t maxclamp)
{
    const unsigned S = N / 2;
    const unsigned Q = (N + 3) / 4;
    const unsigned TD = static_cast<unsigned>(svcntd());
    const unsigned Wend = W - S;
    const svbool_t pt16 = svptrue_b16();
    const svbool_t pt32 = svptrue_b32();
    const svbool_t pt64 = svptrue_b64();

    for (unsigned y = 0; y < H; y += TD) {
        unsigned vr = std::min(TD, H - y);
        for (unsigned cb = S; cb < Wend; cb += 4 * TD) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TD;
                v[t] = base < Wend ? std::min(TD, Wend - base) : 0;
            }
            svzero_za();
            for (unsigned d = 0; d < vr + N - 1; ++d) {
                const uint16_t *row = reinterpret_cast<const uint16_t *>(rows[y + d]);
                for (unsigned q = 0; q < Q; ++q) {
                    svint16_t zn = svld1_s16(pt16, band + (static_cast<size_t>(d) * Q + q) * 4 * TD);
                    unsigned k0 = 4 * q;
                    for (unsigned t = 0; t < 4 && v[t]; ++t) {
                        svbool_t pgh = svwhilelt_b16_u32(0, v[t]);
                        const uint16_t *base = row + cb + t * TD + k0 - S;
                        svint16_t v0 = load_px_i16(reinterpret_cast<const uint8_t *>(base), pgh, true);
                        svint16_t v1 = k0 + 1 < N ? load_px_i16(reinterpret_cast<const uint8_t *>(base + 1), pgh, true) : svdup_s16(0);
                        svint16_t v2 = k0 + 2 < N ? load_px_i16(reinterpret_cast<const uint8_t *>(base + 2), pgh, true) : svdup_s16(0);
                        svint16_t v3 = k0 + 3 < N ? load_px_i16(reinterpret_cast<const uint8_t *>(base + 3), pgh, true) : svdup_s16(0);
                        svint16_t z01 = svzip1_s16(v0, v1);
                        svint16_t z23 = svzip1_s16(v2, v3);
                        svint16_t zm = svreinterpret_s16_s32(svzip1_s32(svreinterpret_s32_s16(z01), svreinterpret_s32_s16(z23)));
                        mopa64_i16(t, pt16, pt16, zn, zm);
                    }
                }
            }
            for (unsigned i = 0; i < vr; ++i) {
                uint16_t *drow = reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i) * dst_stride);
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    svbool_t pgd = svwhilelt_b64_u32(0, v[t]);
                    svint64_t acc = svsub_n_s64_x(pt64, read_za64_s64(t, i), weight_bias);
                    // int64 -> float32 in the even float lane of each doubleword;
                    // the f32 math runs on all lanes (odd lanes are garbage) and
                    // the halfword store picks the low 16 bits of each doubleword.
                    svfloat32_t f = svcvt_f32_s64_x(pt64, acc);
                    f = svmad_n_f32_x(pt32, f, svdup_f32(div), fbias);
                    f = svreinterpret_f32_u32(svand_n_u32_x(pt32, svreinterpret_u32_f32(f), satmask));
                    f = svrintn_f32_x(pt32, f);
                    svint32_t r = svcvt_s32_f32_x(pt32, f);
                    r = svmax_n_s32_x(pt32, r, 0);
                    r = svmin_n_s32_x(pt32, r, maxclamp);
                    svst1h_s64(pgd, reinterpret_cast<int16_t *>(drow + cb + t * TD), svreinterpret_s64_s32(r));
                }
            }
        }
    }
}

// ---- half (binary16) store ----------------------------------------------------
// ZA accumulates in f32; a half sample sits in the bottom 16 bits of its 32-bit
// container, so svcvt + a narrowing halfword store writes svcntw() halves.

inline void st_f32_half(svbool_t pg, uint16_t *p, svfloat32_t f) __arm_streaming
{
    svst1h_u32(pg, p, svreinterpret_u32_f16(svcvt_f16_f32_x(pg, f)));
}

// scale/bias/saturate + store of one f32 tile row (float and half outputs).
__attribute__((always_inline))
inline void store_row_float(float *dst, svbool_t pgw, svfloat32_t f, float div, float fbias, uint32_t satmask) __arm_streaming
{
    svbool_t pt = svptrue_b32();
    f = svmad_n_f32_x(pt, f, svdup_f32(div), fbias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt, svreinterpret_u32_f32(f), satmask));
    svst1_f32(pgw, dst, f);
}

__attribute__((always_inline))
inline void store_row_half(uint16_t *dst, svbool_t pgw, svfloat32_t f, float div, float fbias, uint32_t satmask) __arm_streaming
{
    svbool_t pt = svptrue_b32();
    f = svmad_n_f32_x(pt, f, svdup_f32(div), fbias);
    f = svreinterpret_f32_u32(svand_n_u32_x(pt, svreinterpret_u32_f32(f), satmask));
    st_f32_half(pgw, dst, f);
}

// ---- square NxN, half, native 2-way f16 FMOPA ---------------------------------
//
// The widening FMOPA reduces over adjacent element pairs:
//     ZA32[i][j] += Zn.h[2i]*Zm.h[2j] + Zn.h[2i+1]*Zm.h[2j+1]
// so one instruction folds *two* source rows into the tile instead of one, which
// halves the outer-product count versus the f32 path, and the pixels are consumed
// as f16 with no widening load and no convert.
//
// The pairing is over source rows: for pair dp, element 2j of Zm is the pixel from
// row 2dp and element 2j+1 the pixel from row 2dp+1, at the same column -- exactly
// svzip1_f16 of two ordinary row loads. The band is pre-interleaved to match
// (element 2i+p holds the coefficient for source row 2dp+p, output row i), so the
// odd element of a trailing unpaired row is simply zero and the phantom row reads
// harmlessly from its partner.
//
// PRECISION: FMOPA takes both operands in f16, so the coefficients are rounded to
// half here, where every other tier (C, NEON, x86) keeps them in float32. Products
// are still exact in f32 (an f16*f16 product fits the f32 mantissa) and ZA
// accumulates in f32, so the only loss is that coefficient rounding: ~2^-11
// relative, roughly one ulp of the half output.

std::vector<uint16_t> build_sq_band_f16(const float *m, unsigned N, unsigned TR)
{
    const unsigned band_rows = TR + N - 1;
    const unsigned npair = (band_rows + 1) / 2;
    std::vector<uint16_t> band(static_cast<size_t>(npair) * N * 2 * TR, 0);
    for (unsigned dp = 0; dp < npair; ++dp) {
        for (unsigned k = 0; k < N; ++k) {
            uint16_t *b = band.data() + (static_cast<size_t>(dp) * N + k) * 2 * TR;
            for (unsigned i = 0; i < TR; ++i) {
                for (unsigned p = 0; p < 2; ++p) {
                    const int r = static_cast<int>(2 * dp + p) - static_cast<int>(i);
                    const float c = (r >= 0 && r < static_cast<int>(N)) ? m[static_cast<unsigned>(r) * N + k] : 0.0f;
                    b[2 * i + p] = nc_float_to_half(c);
                }
            }
        }
    }
    return band;
}

__arm_locally_streaming __arm_new("za")
void sme_sq_half_fmopa_interior(const uint8_t *const *rows, uint8_t *dst, ptrdiff_t dst_stride,
                                const uint16_t *band, unsigned N, unsigned W, unsigned H,
                                float div, float fbias, uint32_t satmask)
{
    const unsigned S = N / 2;
    const unsigned TR = static_cast<unsigned>(svcntw());
    const unsigned Wend = W - S;
    const svbool_t pt32 = svptrue_b32();
    const svbool_t pt16 = svptrue_b16();

    for (unsigned y = 0; y < H; y += TR) {
        const unsigned vr = std::min(TR, H - y);
        const unsigned nrows = vr + N - 1;
        const unsigned npair = (nrows + 1) / 2;
        for (unsigned cb = S; cb < Wend; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < Wend ? std::min(TR, Wend - base) : 0;
            }
            svzero_za();
            for (unsigned dp = 0; dp < npair; ++dp) {
                const unsigned d0 = 2 * dp, d1 = d0 + 1;
                const uint16_t *r0 = reinterpret_cast<const uint16_t *>(rows[y + d0]);
                // Trailing odd row: its band coefficients are zero, so pointing at
                // the partner keeps the load in bounds and contributes nothing.
                const uint16_t *r1 = d1 < nrows ? reinterpret_cast<const uint16_t *>(rows[y + d1]) : r0;
                for (unsigned k = 0; k < N; ++k) {
                    svfloat16_t zn = svld1_f16(pt16, reinterpret_cast<const float16_t *>(band + (static_cast<size_t>(dp) * N + k) * 2 * TR));
                    for (unsigned t = 0; t < 4 && v[t]; ++t) {
                        const svbool_t pld = svwhilelt_b16_u32(0u, v[t]);
                        const svbool_t pcol = svwhilelt_b16_u32(0u, 2u * v[t]);
                        const unsigned off = cb + t * TR + k - S;
                        svfloat16_t a = svld1_f16(pld, reinterpret_cast<const float16_t *>(r0 + off));
                        svfloat16_t b = svld1_f16(pld, reinterpret_cast<const float16_t *>(r1 + off));
                        mopa32_f16(t, pt16, pcol, zn, svzip1_f16(a, b));
                    }
                }
            }
            for (unsigned i = 0; i < vr; ++i) {
                uint16_t *drow = reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i) * dst_stride);
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    const svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                    svfloat32_t f = read_za32_f32(t, i);
                    f = svmad_n_f32_x(pt32, f, svdup_f32(div), fbias);
                    f = svreinterpret_f32_u32(svand_n_u32_x(pt32, svreinterpret_u32_f32(f), satmask));
                    st_f32_half(pgw, drow + cb + t * TR, f);
                }
            }
        }
    }
}

// ---- square NxN, float, ZA32 FMOPA --------------------------------------------

__arm_locally_streaming __arm_new("za")
void sme_sq_float_interior(const uint8_t *const *rows, uint8_t *dst, ptrdiff_t dst_stride,
                           const float *band, unsigned N, unsigned W, unsigned H,
                           float div, float fbias, uint32_t satmask)
{
    const unsigned S = N / 2;
    const unsigned TR = static_cast<unsigned>(svcntw());
    const unsigned Wend = W - S;
    const svbool_t pt32 = svptrue_b32();

    for (unsigned y = 0; y < H; y += TR) {
        unsigned vr = std::min(TR, H - y);
        for (unsigned cb = S; cb < Wend; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < Wend ? std::min(TR, Wend - base) : 0;
            }
            svzero_za();
            for (unsigned d = 0; d < vr + N - 1; ++d) {
                const float *row = reinterpret_cast<const float *>(rows[y + d]);
                for (unsigned k = 0; k < N; ++k) {
                    svfloat32_t zn = svld1_f32(pt32, band + (static_cast<size_t>(d) * N + k) * TR);
                    for (unsigned t = 0; t < 4 && v[t]; ++t) {
                        svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                        svfloat32_t zm = svld1_f32(pgw, row + cb + t * TR + k - S);
                        mopa32_f32(t, pt32, pgw, zn, zm);
                    }
                }
            }
            for (unsigned i = 0; i < vr; ++i) {
                float *drow = reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i) * dst_stride);
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                    svfloat32_t f = read_za32_f32(t, i);
                    f = svmad_n_f32_x(pt32, f, svdup_f32(div), fbias);
                    f = svreinterpret_f32_u32(svand_n_u32_x(pt32, svreinterpret_u32_f32(f), satmask));
                    svst1_f32(pgw, drow + cb + t * TR, f);
                }
            }
        }
    }
}


// ---- vertical 1D, int paths, ZA32 ---------------------------------------------

__arm_locally_streaming __arm_new("za")
void sme_v_int32_plane(const uint8_t *const *rows, ptrdiff_t elem_size, uint8_t *dst, ptrdiff_t dst_stride,
                       const int16_t *band, unsigned fwidth, unsigned W, unsigned H,
                       int32_t weight_bias, float div, float fbias, uint32_t satmask, int32_t maxclamp)
{
    const bool word = elem_size == 2;
    const unsigned TR = static_cast<unsigned>(svcntw());
    const svbool_t pt16 = svptrue_b16();

    for (unsigned y = 0; y < H; y += TR) {
        unsigned vr = std::min(TR, H - y);
        unsigned nrows = vr + fwidth - 1;
        unsigned NP = (nrows + 1) / 2;
        for (unsigned cb = 0; cb < W; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < W ? std::min(TR, W - base) : 0;
            }
            svzero_za();
            for (unsigned p = 0; p < NP; ++p) {
                svint16_t zn = svld1_s16(pt16, band + static_cast<size_t>(p) * 2 * TR);
                const uint8_t *r0 = rows[y + 2 * p];
                const uint8_t *r1 = 2 * p + 1 < nrows ? rows[y + 2 * p + 1] : nullptr;
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    svbool_t pgh = svwhilelt_b16_u32(0, v[t]);
                    size_t off = (static_cast<size_t>(cb) + t * TR) * elem_size;
                    svint16_t va = load_px_i16(r0 + off, pgh, word);
                    svint16_t vb = r1 ? load_px_i16(r1 + off, pgh, word) : svdup_s16(0);
                    mopa32_i16(t, pt16, pt16, zn, svzip1_s16(va, vb));
                }
            }
            for (unsigned t = 0; t < 4 && v[t]; ++t) {
                svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                size_t coff = (static_cast<size_t>(cb) + t * TR) * elem_size;
                unsigned i = 0;
                for (; i + 4 <= vr; i += 4) {
                    svint32x4_t z = read_za32_s32_x4(t, i);
                    store_row_int(dst + static_cast<ptrdiff_t>(y + i + 0) * dst_stride + coff, pgw, svget4_s32(z, 0), weight_bias, div, fbias, satmask, maxclamp, word);
                    store_row_int(dst + static_cast<ptrdiff_t>(y + i + 1) * dst_stride + coff, pgw, svget4_s32(z, 1), weight_bias, div, fbias, satmask, maxclamp, word);
                    store_row_int(dst + static_cast<ptrdiff_t>(y + i + 2) * dst_stride + coff, pgw, svget4_s32(z, 2), weight_bias, div, fbias, satmask, maxclamp, word);
                    store_row_int(dst + static_cast<ptrdiff_t>(y + i + 3) * dst_stride + coff, pgw, svget4_s32(z, 3), weight_bias, div, fbias, satmask, maxclamp, word);
                }
                for (; i < vr; ++i)
                    store_row_int(dst + static_cast<ptrdiff_t>(y + i) * dst_stride + coff, pgw,
                                  read_za32_s32(t, i), weight_bias, div, fbias, satmask, maxclamp, word);
            }
        }
    }
}

// ---- vertical 1D, float, ZA32 FMOPA --------------------------------------------

__arm_locally_streaming __arm_new("za")
void sme_v_float_plane(const uint8_t *const *rows, uint8_t *dst, ptrdiff_t dst_stride,
                       const float *band, unsigned fwidth, unsigned W, unsigned H,
                       float div, float fbias, uint32_t satmask)
{
    const unsigned TR = static_cast<unsigned>(svcntw());
    const svbool_t pt32 = svptrue_b32();

    for (unsigned y = 0; y < H; y += TR) {
        unsigned vr = std::min(TR, H - y);
        unsigned nrows = vr + fwidth - 1;
        for (unsigned cb = 0; cb < W; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < W ? std::min(TR, W - base) : 0;
            }
            svzero_za();
            for (unsigned d = 0; d < nrows; ++d) {
                svfloat32_t zn = svld1_f32(pt32, band + static_cast<size_t>(d) * TR);
                const float *row = reinterpret_cast<const float *>(rows[y + d]);
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                    mopa32_f32(t, pt32, pgw, zn, svld1_f32(pgw, row + cb + t * TR));
                }
            }
            for (unsigned t = 0; t < 4 && v[t]; ++t) {
                svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                const unsigned col = cb + t * TR;
                unsigned i = 0;
                for (; i + 4 <= vr; i += 4) {
                    svfloat32x4_t z = read_za32_f32_x4(t, i);
                    store_row_float(reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i + 0) * dst_stride) + col, pgw, svget4_f32(z, 0), div, fbias, satmask);
                    store_row_float(reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i + 1) * dst_stride) + col, pgw, svget4_f32(z, 1), div, fbias, satmask);
                    store_row_float(reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i + 2) * dst_stride) + col, pgw, svget4_f32(z, 2), div, fbias, satmask);
                    store_row_float(reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i + 3) * dst_stride) + col, pgw, svget4_f32(z, 3), div, fbias, satmask);
                }
                for (; i < vr; ++i)
                    store_row_float(reinterpret_cast<float *>(dst + static_cast<ptrdiff_t>(y + i) * dst_stride) + col, pgw, read_za32_f32(t, i), div, fbias, satmask);
            }
        }
    }
}

std::vector<uint16_t> build_v_band_f16(const float *m, unsigned fwidth, unsigned TR)
{
    const unsigned band_rows = TR + fwidth - 1;
    const unsigned npair = (band_rows + 1) / 2;
    std::vector<uint16_t> band(static_cast<size_t>(npair) * 2 * TR, 0);
    for (unsigned dp = 0; dp < npair; ++dp) {
        uint16_t *b = band.data() + static_cast<size_t>(dp) * 2 * TR;
        for (unsigned i = 0; i < TR; ++i) {
            for (unsigned p = 0; p < 2; ++p) {
                const int r = static_cast<int>(2 * dp + p) - static_cast<int>(i);
                const float c = (r >= 0 && r < static_cast<int>(fwidth)) ? m[r] : 0.0f;
                b[2 * i + p] = nc_float_to_half(c);
            }
        }
    }
    return band;
}

// Vertical 1D, native f16 FMOPA: same row-pairing as the square kernel.
__arm_locally_streaming __arm_new("za")
void sme_v_half_fmopa_plane(const uint8_t *const *rows, uint8_t *dst, ptrdiff_t dst_stride,
                            const uint16_t *band, unsigned fwidth, unsigned W, unsigned H,
                            float div, float fbias, uint32_t satmask)
{
    const unsigned TR = static_cast<unsigned>(svcntw());
    const svbool_t pt16 = svptrue_b16();

    for (unsigned y = 0; y < H; y += TR) {
        const unsigned vr = std::min(TR, H - y);
        const unsigned nrows = vr + fwidth - 1;
        const unsigned npair = (nrows + 1) / 2;
        for (unsigned cb = 0; cb < W; cb += 4 * TR) {
            unsigned v[4];
            for (unsigned t = 0; t < 4; ++t) {
                unsigned base = cb + t * TR;
                v[t] = base < W ? std::min(TR, W - base) : 0;
            }
            svzero_za();
            for (unsigned dp = 0; dp < npair; ++dp) {
                const unsigned d0 = 2 * dp, d1 = d0 + 1;
                const uint16_t *r0 = reinterpret_cast<const uint16_t *>(rows[y + d0]);
                const uint16_t *r1 = d1 < nrows ? reinterpret_cast<const uint16_t *>(rows[y + d1]) : r0;
                svfloat16_t zn = svld1_f16(pt16, reinterpret_cast<const float16_t *>(band + static_cast<size_t>(dp) * 2 * TR));
                for (unsigned t = 0; t < 4 && v[t]; ++t) {
                    const svbool_t pld = svwhilelt_b16_u32(0u, v[t]);
                    const svbool_t pcol = svwhilelt_b16_u32(0u, 2u * v[t]);
                    const unsigned off = cb + t * TR;
                    svfloat16_t a = svld1_f16(pld, reinterpret_cast<const float16_t *>(r0 + off));
                    svfloat16_t b = svld1_f16(pld, reinterpret_cast<const float16_t *>(r1 + off));
                    mopa32_f16(t, pt16, pcol, zn, svzip1_f16(a, b));
                }
            }
            for (unsigned t = 0; t < 4 && v[t]; ++t) {
                const svbool_t pgw = svwhilelt_b32_u32(0, v[t]);
                const unsigned col = cb + t * TR;
                unsigned i = 0;
                for (; i + 4 <= vr; i += 4) {
                    svfloat32x4_t z = read_za32_f32_x4(t, i);
                    store_row_half(reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i + 0) * dst_stride) + col, pgw, svget4_f32(z, 0), div, fbias, satmask);
                    store_row_half(reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i + 1) * dst_stride) + col, pgw, svget4_f32(z, 1), div, fbias, satmask);
                    store_row_half(reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i + 2) * dst_stride) + col, pgw, svget4_f32(z, 2), div, fbias, satmask);
                    store_row_half(reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i + 3) * dst_stride) + col, pgw, svget4_f32(z, 3), div, fbias, satmask);
                }
                for (; i < vr; ++i)
                    store_row_half(reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(y + i) * dst_stride) + col, pgw, read_za32_f32(t, i), div, fbias, satmask);
            }
        }
    }
}


// ---- non-streaming wrappers ----------------------------------------------------

// Square coefficient bands. band[d][p] is a 2*TR int16 vector whose pair 2i
// holds coeff[(d - i)*N + 2p] and pair 2i+1 coeff[(d - i)*N + 2p + 1], zero
// outside the valid band. d = (src row) - (block top) + S.
std::vector<int16_t> build_sq_band_i16(const int16_t *m, unsigned N, unsigned TR, unsigned KW)
{
    unsigned P = (N + KW - 1) / KW;
    std::vector<int16_t> band(static_cast<size_t>(TR + N - 1) * P * KW * TR, 0);
    for (unsigned d = 0; d < TR + N - 1; ++d) {
        for (unsigned p = 0; p < P; ++p) {
            int16_t *b = band.data() + (static_cast<size_t>(d) * P + p) * KW * TR;
            for (unsigned i = 0; i < TR; ++i) {
                int r = static_cast<int>(d) - static_cast<int>(i);
                if (r < 0 || r >= static_cast<int>(N))
                    continue;
                for (unsigned t = 0; t < KW; ++t) {
                    unsigned k = p * KW + t;
                    if (k < N)
                        b[KW * i + t] = m[static_cast<unsigned>(r) * N + k];
                }
            }
        }
    }
    return band;
}

std::vector<float> build_sq_band_f32(const float *m, unsigned N, unsigned TR)
{
    std::vector<float> band(static_cast<size_t>(TR + N - 1) * N * TR, 0.0f);
    for (unsigned d = 0; d < TR + N - 1; ++d) {
        for (unsigned k = 0; k < N; ++k) {
            float *b = band.data() + (static_cast<size_t>(d) * N + k) * TR;
            for (unsigned i = 0; i < TR; ++i) {
                int r = static_cast<int>(d) - static_cast<int>(i);
                if (r >= 0 && r < static_cast<int>(N))
                    b[i] = m[static_cast<unsigned>(r) * N + k];
            }
        }
    }
    return band;
}

// Vertical 1D bands: independent of the block row because the row window is a
// pure function of (i + k). Pair 2i of band[p] holds coeff[2p - i], pair
// 2i+1 holds coeff[2p + 1 - i].
std::vector<int16_t> build_v_band_i16(const int16_t *c, unsigned fwidth, unsigned TR)
{
    unsigned NP = (TR + fwidth) / 2;
    std::vector<int16_t> band(static_cast<size_t>(NP) * 2 * TR, 0);
    for (unsigned p = 0; p < NP; ++p) {
        int16_t *b = band.data() + static_cast<size_t>(p) * 2 * TR;
        for (unsigned i = 0; i < TR; ++i) {
            int r0 = 2 * static_cast<int>(p) - static_cast<int>(i);
            int r1 = r0 + 1;
            if (r0 >= 0 && r0 < static_cast<int>(fwidth))
                b[2 * i] = c[r0];
            if (r1 >= 0 && r1 < static_cast<int>(fwidth))
                b[2 * i + 1] = c[r1];
        }
    }
    return band;
}

std::vector<float> build_v_band_f32(const float *c, unsigned fwidth, unsigned TR)
{
    std::vector<float> band(static_cast<size_t>(TR + fwidth - 1) * TR, 0.0f);
    for (unsigned d = 0; d < TR + fwidth - 1; ++d) {
        float *b = band.data() + static_cast<size_t>(d) * TR;
        for (unsigned i = 0; i < TR; ++i) {
            int r = static_cast<int>(d) - static_cast<int>(i);
            if (r >= 0 && r < static_cast<int>(fwidth))
                b[i] = c[static_cast<unsigned>(r)];
        }
    }
    return band;
}

// Mirrored row pointer table covering every (block row + band offset) access:
// rows[t] = plane row mirror(t - S) for t in [0, H + 2*S).
template <class T>
std::vector<const uint8_t *> build_mirror_rows(const void *src, ptrdiff_t src_stride, unsigned H, unsigned S)
{
    std::vector<const uint8_t *> rows(H + 2 * S);
    for (unsigned t = 0; t < H + 2 * S; ++t)
        rows[t] = static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(t) - static_cast<int>(S), static_cast<int>(H))) * src_stride;
    return rows;
}

enum class SmeType { Byte, Word, Float, Half };

template <SmeType TY>
typename std::conditional<TY == SmeType::Byte, uint8_t, typename std::conditional<TY == SmeType::Float, float, uint16_t>::type>::type
sme_sq_edge_px(const typename std::conditional<TY == SmeType::Byte, uint8_t, typename std::conditional<TY == SmeType::Float, float, uint16_t>::type>::type *const *r,
               unsigned j, unsigned N, unsigned W, const vs_generic_params &p)
{
    if constexpr (TY == SmeType::Byte) {
        return nc_sq_scalar_px_rt<uint8_t, int32_t, int16_t>(r, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    } else if constexpr (TY == SmeType::Word) {
        if (N > 5)
            return nc_sq_scalar_px_rt<uint16_t, int64_t, int16_t>(r, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
        else
            return nc_sq_scalar_px_rt<uint16_t, int32_t, int16_t>(r, j, N, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    } else if constexpr (TY == SmeType::Half) {
        return nc_sq_scalar_px_half_rt(r, j, N, W, p.matrixf, p.div, p.bias, p.saturate);
    } else {
        return nc_sq_scalar_px_rt<float, float, float>(r, j, N, W, p.matrixf, p.div, p.bias, p.saturate, p.maxval);
    }
}

template <SmeType TY>
void sme_sq_plane(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                  const vs_generic_params &p, unsigned W, unsigned H, unsigned N)
{
    typedef typename std::conditional<TY == SmeType::Byte, uint8_t, typename std::conditional<TY == SmeType::Float, float, uint16_t>::type>::type T;
    const unsigned S = N / 2;
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;

    std::vector<const uint8_t *> rows = build_mirror_rows<T>(src, src_stride, H, S);

    // Interior columns.
    if (W > 2 * S) {
        if constexpr (TY == SmeType::Float) {
            unsigned TR = static_cast<unsigned>(svcntsw());
            std::vector<float> band = build_sq_band_f32(p.matrixf, N, TR);
            sme_sq_float_interior(rows.data(), static_cast<uint8_t *>(dst), dst_stride, band.data(), N, W, H, p.div, p.bias, satmask);
        } else if constexpr (TY == SmeType::Half) {
            unsigned TR = static_cast<unsigned>(svcntsw());
            std::vector<uint16_t> band = build_sq_band_f16(p.matrixf, N, TR);
            sme_sq_half_fmopa_interior(rows.data(), static_cast<uint8_t *>(dst), dst_stride, band.data(), N, W, H, p.div, p.bias, satmask);
        } else if (TY == SmeType::Word && N > 7) {
            unsigned TD = static_cast<unsigned>(svcntsd());
            std::vector<int16_t> band = build_sq_band_i16(p.matrix, N, TD, 4);
            int64_t weight_bias = 0;
            for (unsigned i = 0; i < N * N; ++i) weight_bias += static_cast<int64_t>(INT16_MIN) * p.matrix[i];
            sme_sq_int64_interior(rows.data(), static_cast<uint8_t *>(dst), dst_stride, band.data(), N, W, H,
                                  weight_bias, p.div, p.bias, satmask, p.maxval);
        } else {
            unsigned TR = static_cast<unsigned>(svcntsw());
            std::vector<int16_t> band = build_sq_band_i16(p.matrix, N, TR, 2);
            int32_t weight_bias = 0;
            if (TY == SmeType::Word)
                for (unsigned i = 0; i < N * N; ++i) weight_bias += static_cast<int32_t>(INT16_MIN) * p.matrix[i];
            sme_sq_int32_interior(rows.data(), sizeof(T), static_cast<uint8_t *>(dst), dst_stride, band.data(), N, W, H,
                                  weight_bias, p.div, p.bias, satmask, p.maxval);
        }
    }

    // Mirror edge columns, scalar, bit-exact with the C reference.
    const unsigned edge = std::min(W, S);
    for (unsigned i = 0; i < H; ++i) {
        const T *const *r = reinterpret_cast<const T *const *>(rows.data() + i);
        T *d = reinterpret_cast<T *>(static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride);
        for (unsigned j = 0; j < edge; ++j)
            d[j] = sme_sq_edge_px<TY>(r, j, N, W, p);
        for (unsigned j = std::max(S, W > edge ? W - edge : 0); j < W; ++j)
            d[j] = sme_sq_edge_px<TY>(r, j, N, W, p);
    }
}

// Vertical 1D. For H > support the C row selection reduces to
// mirror(i + k - support), which is what the banded formulation needs; the
// tiny-plane case falls back to the C kernel.
template <SmeType TY>
void sme_v_plane(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                 const vs_generic_params &p, unsigned W, unsigned H)
{
    typedef typename std::conditional<TY == SmeType::Byte, uint8_t, typename std::conditional<TY == SmeType::Float, float, uint16_t>::type>::type T;
    const unsigned fwidth = p.matrixsize;
    const unsigned support = fwidth / 2;
    const uint32_t satmask = p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu;

    if (H <= support || W == 0) {
        if constexpr (TY == SmeType::Byte)
            vs_generic_1d_conv_v_byte_c(src, src_stride, dst, dst_stride, &p, W, H);
        else if constexpr (TY == SmeType::Word)
            vs_generic_1d_conv_v_word_c(src, src_stride, dst, dst_stride, &p, W, H);
        else if constexpr (TY == SmeType::Half)
            vs_generic_1d_conv_v_half_c(src, src_stride, dst, dst_stride, &p, W, H);
        else
            vs_generic_1d_conv_v_float_c(src, src_stride, dst, dst_stride, &p, W, H);
        return;
    }

    std::vector<const uint8_t *> rows = build_mirror_rows<T>(src, src_stride, H, support);
    unsigned TR = static_cast<unsigned>(svcntsw());

    if constexpr (TY == SmeType::Float) {
        std::vector<float> band = build_v_band_f32(p.matrixf, fwidth, TR);
        sme_v_float_plane(rows.data(), static_cast<uint8_t *>(dst), dst_stride, band.data(), fwidth, W, H, p.div, p.bias, satmask);
    } else if constexpr (TY == SmeType::Half) {
        std::vector<uint16_t> band = build_v_band_f16(p.matrixf, fwidth, TR);
        sme_v_half_fmopa_plane(rows.data(), static_cast<uint8_t *>(dst), dst_stride, band.data(), fwidth, W, H, p.div, p.bias, satmask);
    } else {
        std::vector<int16_t> band = build_v_band_i16(p.matrix, fwidth, TR);
        int32_t weight_bias = 0;
        if (TY == SmeType::Word)
            for (unsigned i = 0; i < fwidth; ++i) weight_bias += static_cast<int32_t>(INT16_MIN) * p.matrix[i];
        sme_v_int32_plane(rows.data(), sizeof(T), static_cast<uint8_t *>(dst), dst_stride, band.data(), fwidth, W, H,
                          weight_bias, p.div, p.bias, satmask, p.maxval);
    }
}

} // namespace

#define VS_SME_SQUARE_ENTRY(SZ, N, TY, TN) \
    void vs_generic_##SZ##_conv_##TN##_sme(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sme_sq_plane<SmeType::TY>(src, src_stride, dst, dst_stride, *params, width, height, N); }

// Only the shapes that win even fully contended are dispatched (see
// genericSelectSME); the smaller/thread-contested squares and all word squares
// were pruned and fall back to NEON, so their entries are gone. Byte squares
// serve WIDE coefficients only (int8 goes to NEON usdot).
VS_SME_SQUARE_ENTRY(7x7,   7,  Byte,  byte)
VS_SME_SQUARE_ENTRY(9x9,   9,  Byte,  byte)
VS_SME_SQUARE_ENTRY(11x11, 11, Byte,  byte)
VS_SME_SQUARE_ENTRY(7x7,   7,  Half,  half)
VS_SME_SQUARE_ENTRY(9x9,   9,  Half,  half)
VS_SME_SQUARE_ENTRY(11x11, 11, Half,  half)
VS_SME_SQUARE_ENTRY(7x7,   7,  Float, float)
VS_SME_SQUARE_ENTRY(9x9,   9,  Float, float)
VS_SME_SQUARE_ENTRY(11x11, 11, Float, float)

#define VS_SME_V_ENTRY(TY, TN) \
    void vs_generic_1d_conv_v_##TN##_sme(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sme_v_plane<SmeType::TY>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SME_V_ENTRY(Byte, byte)
VS_SME_V_ENTRY(Float, float)
VS_SME_V_ENTRY(Half, half)
