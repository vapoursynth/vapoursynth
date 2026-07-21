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
* FEAT_I8MM byte square NxN convolution, N in {5,7,9,11} -- a faster interior for
* the byte path, dispatched only when every coefficient fits int8 (conv_int8) and
* the CPU reports i8mm.
*
* This is the ARM analog of square_vnni_impl.h: usdot is the vpdpbusd equivalent,
* reducing 4 uint8*int8 products into an int32 lane in one op with no byte->word
* widening, and vqtbl1q_u8 plays the role of vpermb -- it builds the sliding
* window so that 32-bit lane l holds the 4 pixels output j+l needs. The
* accumulator therefore stays linear (lane l = output j+l) and the store needs no
* un-scramble.
*
* Bit-exact with sq_interior_byte: the products and the int32 sum are the same
* integers (integer addition is associative, so the regrouping into 4-tap chunks
* cannot change the result), and the scale/bias/round/clamp reuses the same
* nc_store_u8x8. |coeff| <= 127 under conv_int8, so 121 * 127 * 255 cannot
* overflow int32.
*
* Coefficients are zero-padded to a multiple of 4 taps per row, so the window can
* read up to 4G-N taps past the kernel; those pixels are multiplied by zero, but
* they are still *read*, so the SIMD interior is bounded to keep every load inside
* the row and the driver's scalar edge covers whatever is left.
*
* Compiled in its own TU with -march=...+i8mm (see meson.build). NEON baseline
* code must not be built with +i8mm, hence the separate TU rather than a target
* attribute.
*/

#include <cstdint>
#include <cstring>
#include "../generic.h"
#include "conv_scalar.h"
#include "neon_common.h"

namespace {

// Window indices: 32-bit lane l gets pixels {l, l+1, l+2, l+3} of the 8-byte load.
alignas(16) const uint8_t NC_DOT_WIN[16] = {
    0, 1, 2, 3,
    1, 2, 3, 4,
    2, 3, 4, 5,
    3, 4, 5, 6,
};

template <unsigned N>
unsigned sq_interior_byte_dot(const uint8_t *const *rows, uint8_t *dst, unsigned S, unsigned W,
                              const int32_t *cg, float32x4_t sc, float32x4_t bi, uint32x4_t sm)
{
    constexpr unsigned G = (N + 3) / 4;                 // 4-tap groups per row
    const uint8x16_t widx = vld1q_u8(NC_DOT_WIN);

    auto block = [&](unsigned jj) {
        int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0), a2 = vdupq_n_s32(0), a3 = vdupq_n_s32(0);
        for (unsigned r = 0; r < N; ++r) {
            const uint8_t *row = rows[r] + (jj - S);
            for (unsigned g = 0; g < G; ++g) {
                // The group's 4 coefficients, replicated into every 32-bit lane.
                const int8x16_t w = vreinterpretq_s8_s32(vdupq_n_s32(cg[r * G + g]));
                const uint8_t *q = row + 4 * g;
                const uint8x16_t z = vdupq_n_u8(0);
                uint8x16_t t0 = vqtbl1q_u8(vcombine_u8(vld1_u8(q + 0), vget_low_u8(z)), widx);
                uint8x16_t t1 = vqtbl1q_u8(vcombine_u8(vld1_u8(q + 4), vget_low_u8(z)), widx);
                uint8x16_t t2 = vqtbl1q_u8(vcombine_u8(vld1_u8(q + 8), vget_low_u8(z)), widx);
                uint8x16_t t3 = vqtbl1q_u8(vcombine_u8(vld1_u8(q + 12), vget_low_u8(z)), widx);
                a0 = vusdotq_s32(a0, t0, w);
                a1 = vusdotq_s32(a1, t1, w);
                a2 = vusdotq_s32(a2, t2, w);
                a3 = vusdotq_s32(a3, t3, w);
            }
        }
        nc_store_u8x8(dst + jj, a0, a1, sc, bi, sm);
        nc_store_u8x8(dst + jj + 8, a2, a3, sc, bi, sm);
    };

    const unsigned end = W > S ? W - S : 0;
    if (end < S + 16)
        return S;

    // Highest block start whose furthest load, row[(jj - S) + 4(G-1) + 12 + 7],
    // still lands inside the row.
    const long maxjj = static_cast<long>(W) + static_cast<long>(S) - 4L * static_cast<long>(G) - 16L;
    if (maxjj < static_cast<long>(S))
        return S;

    unsigned j = S;
    for (; j + 16 <= end && static_cast<long>(j) <= maxjj; j += 16)
        block(j);

    // Overlapping final block for the sub-vector remainder; re-storing already
    // written columns is safe because this path is bit-exact with the scalar one.
    if (j < end) {
        long c = static_cast<long>(end) - 16;
        if (c > maxjj)
            c = maxjj;
        if (c >= static_cast<long>(S) && static_cast<unsigned>(c) + 16 > j) {
            block(static_cast<unsigned>(c));
            j = static_cast<unsigned>(c) + 16;
        }
    }
    return j;
}

template <unsigned N>
void sq_plane_byte_dot(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                       const vs_generic_params &p, unsigned W, unsigned H)
{
    constexpr unsigned S = N / 2;
    constexpr unsigned G = (N + 3) / 4;

    // Pack each row's taps into 4-tap groups of int8, zero-filling the tail.
    int32_t cg[N * G];
    for (unsigned r = 0; r < N; ++r) {
        for (unsigned g = 0; g < G; ++g) {
            int8_t b[4];
            for (unsigned t = 0; t < 4; ++t) {
                const unsigned k = 4 * g + t;
                b[t] = k < N ? static_cast<int8_t>(p.matrix[r * N + k]) : 0;
            }
            std::memcpy(&cg[r * G + g], b, 4);
        }
    }

    const float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    const uint32x4_t sm = nc_satmask(p.saturate);

    for (unsigned i = 0; i < H; ++i) {
        const uint8_t *rows[N];
        for (unsigned r = 0; r < N; ++r)
            rows[r] = static_cast<const uint8_t *>(src) +
                static_cast<ptrdiff_t>(nc_mirror(static_cast<int>(i) + static_cast<int>(r) - static_cast<int>(S), static_cast<int>(H))) * ss;
        uint8_t *d = static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * ds;

        const unsigned aend = sq_interior_byte_dot<N>(rows, d, S, W, cg, sc, bi, sm);

        for (unsigned j = 0; j < S && j < W; ++j)
            d[j] = nc_sq_scalar_px<uint8_t, int32_t, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
        for (unsigned j = (aend > S ? aend : S); j < W; ++j)
            d[j] = nc_sq_scalar_px<uint8_t, int32_t, int16_t, N>(rows, j, S, W, p.matrix, p.div, p.bias, p.saturate, p.maxval);
    }
}

// ---- 3x3 byte (replicate edges, conv_plane_3x3 semantics) ----------------------
//
// The 3x3 shape was originally skipped because the square driver above uses
// MIRROR edges while 3x3 convolution replicates -- but the interior
// (sq_interior_byte_dot<3>, G=1) is edge-agnostic, so all it needs is its own
// replicate-edge driver. Same conv_int8 + i8mm gate, same bit-exactness
// argument, and 3x3 byte is the most common convolution there is.

inline uint8_t sq3_edge_px_byte(const uint8_t *const rows[3], const vs_generic_params &p,
                                unsigned a, unsigned b, unsigned c)
{
    int32_t accum = 0;
    for (unsigned r = 0; r < 3; ++r) {
        accum += p.matrix[r * 3 + 0] * static_cast<int32_t>(rows[r][a]);
        accum += p.matrix[r * 3 + 1] * static_cast<int32_t>(rows[r][b]);
        accum += p.matrix[r * 3 + 2] * static_cast<int32_t>(rows[r][c]);
    }
    float tmp = static_cast<float>(accum) * p.div + p.bias;
    tmp = p.saturate ? tmp : std::fabs(tmp);
    float cl = std::min(std::max(tmp, 0.0f), 255.0f);
    long v = std::lrint(cl);
    return static_cast<uint8_t>(std::min<long>(v, p.maxval));
}

void sq_plane_byte_dot_3x3(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                           const vs_generic_params &p, unsigned W, unsigned H)
{
    // One 4-tap group per row: {c0, c1, c2, 0}.
    int32_t cg[3];
    for (unsigned r = 0; r < 3; ++r) {
        int8_t b[4] = { static_cast<int8_t>(p.matrix[r * 3 + 0]),
                        static_cast<int8_t>(p.matrix[r * 3 + 1]),
                        static_cast<int8_t>(p.matrix[r * 3 + 2]), 0 };
        std::memcpy(&cg[r], b, 4);
    }

    const float32x4_t sc = vdupq_n_f32(p.div), bi = vdupq_n_f32(p.bias);
    const uint32x4_t sm = nc_satmask(p.saturate);

    for (unsigned i = 0; i < H; ++i) {
        unsigned above = i == 0 ? 0 : i - 1;
        unsigned below = i == H - 1 ? H - 1 : i + 1;
        const uint8_t *rows[3] = {
            static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(above) * ss,
            static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(i) * ss,
            static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(below) * ss,
        };
        uint8_t *d = static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * ds;

        const unsigned aend = sq_interior_byte_dot<3>(rows, d, 1, W, cg, sc, bi, sm);

        d[0] = sq3_edge_px_byte(rows, p, 0, 0, W > 1 ? 1 : 0);
        for (unsigned j = std::max(aend, 1u); j < W; ++j)
            d[j] = sq3_edge_px_byte(rows, p, j - 1, j, j == W - 1 ? W - 1 : j + 1);
    }
}

// ---- 1D horizontal byte -------------------------------------------------------
//
// One row of the square machinery: the taps run along the scanline and
// zero-pad to 4-tap groups exactly like a square row, so the window/usdot
// form carries over with the row loop removed. One 1D-specific improvement:
// consecutive groups overlap 3 of their 4 window loads (group g reads at
// offsets 4g..4g+12, group g+1 at 4g+4..), so the group loop rolls the loads
// forward instead of reloading -- the squares cannot do this because each
// row has its own pointer. Bit-exact with vs_generic_1d_conv_h_byte_neon for
// the same reason the squares are: integer regrouping is associative and the
// store pipeline is shared. Vertical 1D does NOT transfer: its taps run
// across rows, so there is no scanline window to build.

// conv_h_edge_px<Byte> replica (convolution_neon.cpp); keep in sync. The 1D
// kernels clamp edge taps instead of the squares' mirror.
inline uint8_t h_edge_px_byte(const uint8_t *srcp, unsigned j, unsigned width, const vs_generic_params &p)
{
    unsigned fwidth = p.matrixsize;
    unsigned support = fwidth / 2;
    unsigned dist_from_right = width - 1 - j;

    int32_t accum = 0;
    for (unsigned k = 0; k < support; ++k) {
        unsigned idx = j < support - k ? std::min(support - k - j - 1, width - 1) : j - support + k;
        accum += p.matrix[k] * static_cast<int32_t>(srcp[idx]);
    }
    for (unsigned k = support; k < fwidth; ++k) {
        unsigned idx = dist_from_right < k - support ? width - std::min(k - support - dist_from_right, width) : j - support + k;
        accum += p.matrix[k] * static_cast<int32_t>(srcp[idx]);
    }
    float tmp = static_cast<float>(accum) * p.div + p.bias;
    tmp = p.saturate ? tmp : std::fabs(tmp);
    float c = std::min(std::max(tmp, 0.0f), 255.0f);
    long v = std::lrint(c);
    return static_cast<uint8_t>(std::min<long>(v, p.maxval));
}

// Everything that depends only on the filter, not on the row. Split out so the
// plane driver can hoist it out of its row loop while the exported per-scanline
// entry (used by the separable driver, which owns its own row loop) can build
// it per call -- it is ~40 ops against a row of thousands of pixels.
struct h_dot_ctx {
    int32_t cg[7];                        // ceil(25/4) 4-tap int8 groups
    unsigned S, G, end;
    long maxjj;
    float32x4_t sc, bi;
    uint32x4_t sm;
    uint8x16_t widx;
};

inline h_dot_ctx h_dot_make(const vs_generic_params &p, unsigned W)
{
    h_dot_ctx c;
    const unsigned N = p.matrixsize;      // runtime, 3..25 odd
    c.S = N / 2;
    c.G = (N + 3) / 4;

    // Taps packed into 4-tap int8 groups, zero-filled tail.
    for (unsigned g = 0; g < c.G; ++g) {
        int8_t b[4];
        for (unsigned t = 0; t < 4; ++t) {
            const unsigned k = 4 * g + t;
            b[t] = k < N ? static_cast<int8_t>(p.matrix[k]) : 0;
        }
        std::memcpy(&c.cg[g], b, 4);
    }

    c.sc = vdupq_n_f32(p.div);
    c.bi = vdupq_n_f32(p.bias);
    c.sm = nc_satmask(p.saturate);
    c.widx = vld1q_u8(NC_DOT_WIN);

    // Same interior bounds as the squares: the furthest load of a block at jj
    // is byte (jj - S) + 4(G-1) + 12 + 7; keep it inside the row.
    c.end = W > c.S ? W - c.S : 0;
    c.maxjj = static_cast<long>(W) + static_cast<long>(c.S) - 4L * static_cast<long>(c.G) - 16L;
    return c;
}

inline void h_dot_scanline(const h_dot_ctx &c, const uint8_t *srcp, uint8_t *d, unsigned W, const vs_generic_params &p)
{
    const uint8x8_t zlo = vdup_n_u8(0);
    const unsigned S = c.S, G = c.G, end = c.end;
    const long maxjj = c.maxjj;

    auto block = [&](unsigned jj) {
        const uint8_t *q = srcp + (jj - S);
        int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0), a2 = vdupq_n_s32(0), a3 = vdupq_n_s32(0);
        uint8x8_t v0 = vld1_u8(q), v1 = vld1_u8(q + 4), v2 = vld1_u8(q + 8);
        for (unsigned g = 0; g < G; ++g) {
            uint8x8_t v3 = vld1_u8(q + 4 * g + 12);
            const int8x16_t w = vreinterpretq_s8_s32(vdupq_n_s32(c.cg[g]));
            a0 = vusdotq_s32(a0, vqtbl1q_u8(vcombine_u8(v0, zlo), c.widx), w);
            a1 = vusdotq_s32(a1, vqtbl1q_u8(vcombine_u8(v1, zlo), c.widx), w);
            a2 = vusdotq_s32(a2, vqtbl1q_u8(vcombine_u8(v2, zlo), c.widx), w);
            a3 = vusdotq_s32(a3, vqtbl1q_u8(vcombine_u8(v3, zlo), c.widx), w);
            v0 = v1; v1 = v2; v2 = v3;
        }
        nc_store_u8x8(d + jj, a0, a1, c.sc, c.bi, c.sm);
        nc_store_u8x8(d + jj + 8, a2, a3, c.sc, c.bi, c.sm);
    };

    unsigned j = S;
    if (end >= S + 16 && maxjj >= static_cast<long>(S)) {
        for (; j + 16 <= end && static_cast<long>(j) <= maxjj; j += 16)
            block(j);
        // Overlapping final block; safe because this path is bit-exact.
        if (j < end) {
            long cc = static_cast<long>(end) - 16;
            if (cc > maxjj)
                cc = maxjj;
            if (cc >= static_cast<long>(S) && static_cast<unsigned>(cc) + 16 > j) {
                block(static_cast<unsigned>(cc));
                j = static_cast<unsigned>(cc) + 16;
            }
        }
    }

    for (unsigned jj = 0; jj < S && jj < W; ++jj)
        d[jj] = h_edge_px_byte(srcp, jj, W, p);
    for (unsigned jj = std::max(j, S); jj < W; ++jj)
        d[jj] = h_edge_px_byte(srcp, jj, W, p);
}

void h_plane_byte_dot(const void *src, ptrdiff_t ss, void *dst, ptrdiff_t ds,
                      const vs_generic_params &p, unsigned W, unsigned H)
{
    const h_dot_ctx c = h_dot_make(p, W);

    for (unsigned i = 0; i < H; ++i) {
        const uint8_t *srcp = static_cast<const uint8_t *>(src) + static_cast<ptrdiff_t>(i) * ss;
        uint8_t *d = static_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(i) * ds;
        h_dot_scanline(c, srcp, d, W, p);
    }
}

} // namespace

void vs_generic_1d_conv_h_byte_neon_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    h_plane_byte_dot(src, src_stride, dst, dst_stride, *params, width, height);
}

// One scanline, for the separable driver's horizontal half: that driver owns
// the row loop (it interleaves a vertical pass through a tmp scanline), and it
// lives in a TU built without i8mm, so it cannot inline this.
void vs_generic_1d_conv_h_byte_scanline_neon_dot(const void *srcp, void *dstp, const struct vs_generic_params *params, unsigned width)
{
    const h_dot_ctx c = h_dot_make(*params, width);
    h_dot_scanline(c, static_cast<const uint8_t *>(srcp), static_cast<uint8_t *>(dstp), width, *params);
}

void vs_generic_3x3_conv_byte_neon_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height)
{
    sq_plane_byte_dot_3x3(src, src_stride, dst, dst_stride, *params, width, height);
}

#define VS_SQUARE_DOT_ENTRY(SZ, N) \
    void vs_generic_##SZ##_conv_byte_neon_dot(const void *src, ptrdiff_t src_stride, void *dst, ptrdiff_t dst_stride, const struct vs_generic_params *params, unsigned width, unsigned height) \
    { sq_plane_byte_dot<N>(src, src_stride, dst, dst_stride, *params, width, height); }

VS_SQUARE_DOT_ENTRY(5x5, 5)
VS_SQUARE_DOT_ENTRY(7x7, 7)
VS_SQUARE_DOT_ENTRY(9x9, 9)
VS_SQUARE_DOT_ENTRY(11x11, 11)
