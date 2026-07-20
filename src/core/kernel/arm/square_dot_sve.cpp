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
* SVE + FEAT_I8MM byte square NxN convolution, N in {5,7,9,11}.
*
* The VLA analog of square_dot_neon.cpp: svusdot folds 4 uint8*int8 products per
* 32-bit lane, and svtbl builds the sliding window so lane l holds the 4 pixels
* output j+l needs. Where NEON does 4 outputs per (load, tbl, usdot), SVE does
* svcntw() of them -- 8 at a 256-bit VL -- so this only makes sense above 128-bit
* vectors, and the dispatcher gates it on VL the same way the other SVE kernels
* are gated.
*
* Bit-exact with the C/NEON byte path: identical integer products and sum, and
* the store reproduces nc_store_u8x8 exactly. That store's int32 -> saturated
* int16 -> saturated uint8 chain is just a clamp to [0, 255], which SVE does with
* svmax/svmin plus the truncating svst1b (the value already fits a byte).
*
* Own TU compiled with +sve+i8mm: a CPU can have SVE without I8MM (A64FX), so the
* other SVE kernels must not be built with i8mm enabled.
*/

#include <arm_sve.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../generic.h"
#include "conv_scalar.h"

namespace {

// acc -> scale/bias/saturate/round/clamp -> byte, matching nc_store_u8x8.
inline void sv_store_u8(uint8_t *dst, svbool_t pg, svint32_t acc,
                        svfloat32_t sc, svfloat32_t bi, svuint32_t smask)
{
    svfloat32_t t = svmad_f32_x(pg, svcvt_f32_s32_x(pg, acc), sc, bi);
    t = svreinterpret_f32_u32(svand_u32_x(pg, svreinterpret_u32_f32(t), smask));
    svint32_t r = svcvt_s32_f32_x(pg, svrintn_f32_x(pg, t));
    r = svmax_s32_x(pg, r, svdup_n_s32(0));
    r = svmin_s32_x(pg, r, svdup_n_s32(255));
    svst1b_s32(pg, reinterpret_cast<int8_t *>(dst), r);
}

template <unsigned N>
unsigned sq_interior_byte_dot_sve(const uint8_t *const *rows, uint8_t *dst, unsigned S, unsigned W,
                                  const int32_t *coeff_groups, svfloat32_t sc, svfloat32_t bi, svuint32_t smask)
{
    constexpr unsigned G = (N + 3) / 4;
    const unsigned nl = static_cast<unsigned>(svcntw());   // outputs per vector

    // Window indices: byte i of the table result belongs to 32-bit lane i/4 and
    // tap i%4, so it must come from pixel (i/4 + i%4) of the load.
    std::vector<uint8_t> idxbuf(static_cast<size_t>(svcntb()));
    for (size_t i = 0; i < idxbuf.size(); ++i)
        idxbuf[i] = static_cast<uint8_t>(i / 4 + i % 4);
    const svuint8_t vidx = svld1_u8(svptrue_b8(), idxbuf.data());

    const svbool_t pg32 = svptrue_b32();
    // Only nl+3 bytes of each row window are ever indexed; predicating the load
    // to exactly that keeps the kernel from touching memory past the row.
    const svbool_t pgld = svwhilelt_b8_u32(0u, nl + 3u);

    const unsigned end = W > S ? W - S : 0;
    if (end < S + nl)
        return S;

    const long maxjj = static_cast<long>(W) + static_cast<long>(S) - 4L * static_cast<long>(G) - static_cast<long>(nl);
    if (maxjj < static_cast<long>(S))
        return S;

    auto block = [&](unsigned jj) {
        svint32_t acc = svdup_n_s32(0);
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *row = rows[r] + (jj - S);
            for (unsigned g = 0; g < G; ++g) {
                const svuint8_t v = svld1_u8(pgld, row + 4 * g);
                const svuint8_t t = svtbl_u8(v, vidx);
                const svint8_t w = svreinterpret_s8_s32(svdup_n_s32(coeff_groups[r * G + g]));
                acc = svusdot_s32(acc, t, w);
            }
        }
        sv_store_u8(dst + jj, pg32, acc, sc, bi, smask);
    };

    unsigned j = S;
    for (; j + nl <= end && static_cast<long>(j) <= maxjj; j += nl)
        block(j);

    if (j < end) {
        long c = static_cast<long>(end) - static_cast<long>(nl);
        if (c > maxjj)
            c = maxjj;
        if (c >= static_cast<long>(S) && static_cast<unsigned>(c) + nl > j) {
            block(static_cast<unsigned>(c));
            j = static_cast<unsigned>(c) + nl;
        }
    }
    return j;
}

template <unsigned N>
void sq_plane_byte_dot_sve(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride,
                           const vs_generic_params &p, unsigned W, unsigned H)
{
    constexpr unsigned S = N / 2;
    constexpr unsigned G = (N + 3) / 4;

    int32_t coeff_groups[N * G];
    for (unsigned r = 0; r < N; ++r) {
        for (unsigned g = 0; g < G; ++g) {
            int8_t b[4];
            for (unsigned t = 0; t < 4; ++t) {
                const unsigned k = 4 * g + t;
                b[t] = k < N ? static_cast<int8_t>(p.matrix[r * N + k]) : 0;
            }
            std::memcpy(&coeff_groups[r * G + g], b, 4);
        }
    }

    const svfloat32_t sc = svdup_n_f32(p.div), bi = svdup_n_f32(p.bias);
    const svuint32_t smask = svdup_n_u32(p.saturate ? 0xFFFFFFFFu : 0x7FFFFFFFu);

    for (unsigned i = 0; i < H; ++i) {
        const uint8_t *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = static_cast<const uint8_t *>(src) +
                static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * src_stride;
        uint8_t *d = static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * dst_stride;

        const unsigned aend = sq_interior_byte_dot_sve<N>(rows, d, S, W, coeff_groups, sc, bi, smask);

        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = nc_sq_scalar_px<uint8_t, int32_t, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = nc_sq_scalar_px<uint8_t, int32_t, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    }
}

} // namespace

#define VS_SQUARE_DOT_SVE_ENTRY(SZ, N) \
    void vs_generic_##SZ##_conv_byte_sve_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane_byte_dot_sve<N>(src, src_stride, dst, dst_stride, *params, width, height); }

// 5x5 and 11x11 were thread-gated (won only on tiny pools) and are pruned;
// int8 5x5/11x11 fall back to the NEON usdot squares. 7x7/9x9 win at every
// pool size and stay.
VS_SQUARE_DOT_SVE_ENTRY(7x7, 7)
VS_SQUARE_DOT_SVE_ENTRY(9x9, 9)
