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
* SVE gather Lut1 for 16-bit sources.
*
* A 16-bit source indexes a 65536-entry table, which is far too large for the
* byte path's tbl/vpermb trick, so every ISA runs this scalar -- x86 included,
* where the only Lut1 SIMD kernel is the AVX-512 VBMI byte->byte one. SVE can do
* it with a real gather: widen the 16-bit pixels into 32-bit lanes and use them
* as indices, so one gather retires svcntw() lookups.
*
* Bit-exact by construction: it is the same table read, just several at a time.
*
* Whether it beats the scalar loop depends on how many lookups a gather retires,
* which is svcntw() and therefore vector-length dependent (measured, 65536-entry
* table, random indices):
*
*             word->word   word->byte
*   256-bit      +55%         +75%      (Graviton3, 8 lookups/gather)
*   128-bit       -7%         +36%      (Graviton4, 4 lookups/gather)
*
* so word->word is gated on a vector length above 128 bits and word->byte is not.
* The narrower destination wins at both because the store narrows 4:1 rather than
* 2:1, which is where the scalar loop spends its time.
*/

#include <arm_sve.h>
#include <cstdint>
#include <type_traits>

void vs_lut1_w_w_sve(const uint16_t *src, uint16_t *dst, int w, const uint16_t *lut)
{
    const unsigned n = static_cast<unsigned>(w);
    const unsigned step = static_cast<unsigned>(svcntw());
    for (unsigned i = 0; i < n; i += step) {
        svbool_t pg = svwhilelt_b32_u32(i, n);
        svuint32_t idx = svld1uh_u32(pg, src + i);
        svuint32_t v = svld1uh_gather_u32index_u32(pg, lut, idx);
        svst1h_u32(pg, dst + i, v);
    }
}

void vs_lut1_w_b_sve(const uint16_t *src, uint8_t *dst, int w, const uint8_t *lut)
{
    const unsigned n = static_cast<unsigned>(w);
    const unsigned step = static_cast<unsigned>(svcntw());
    for (unsigned i = 0; i < n; i += step) {
        svbool_t pg = svwhilelt_b32_u32(i, n);
        svuint32_t idx = svld1uh_u32(pg, src + i);
        // byte elements need no index scaling, so this is the offset form.
        svuint32_t v = svld1ub_gather_u32offset_u32(pg, lut, idx);
        svst1b_u32(pg, dst + i, v);
    }
}

/*
* SVE gather Lut2.
*
* Lut2 indexes a 2D table with a pixel from each source clip:
*
*   dst[x] = lut[(min(sy[x], my) << bitsx) + min(sx[x], mx)]
*
* which is the scalar path verbatim, one lane at a time. Unlike Lut1, whose byte
* destination packs 8 lookups into a uint64 and stores once, the Lut2 scalar path
* is a plain per-pixel loop for every destination width, so there is more headroom
* here than there was for Lut1.
*
* The combos mirror the AVX-512 ones: both sources 16-bit (ww), or one of the two
* 8-bit (wb, bw), each with a word or byte destination. Two 8-bit sources are left
* scalar -- Lut2 caps the total indexing bits at 20, so a byte/byte table is at most
* 65536 entries (128 KB) and stays resident in L2, where a gather has nothing to
* recover. x86 declines that combo for the same reason.
*
* Bit-exact by construction, and predicated on svwhilelt rather than relying on row
* padding, so the width tail needs no scalar cleanup and nothing is read out of bounds.
*/

namespace {

template<typename T>
inline svuint32_t load_widen(svbool_t pg, const T *p)
{
    if constexpr (sizeof(T) == 1)
        return svld1ub_u32(pg, p);
    else
        return svld1uh_u32(pg, p);
}

template<typename V>
inline void gather_store(svbool_t pg, V *d, const V *lut, svuint32_t idx)
{
    if constexpr (sizeof(V) == 2) {
        // 16-bit table entries are scaled by the index form.
        svst1h_u32(pg, d, svld1uh_gather_u32index_u32(pg, lut, idx));
    } else {
        // byte elements need no index scaling, so this is the offset form.
        svst1b_u32(pg, d, svld1ub_gather_u32offset_u32(pg, lut, idx));
    }
}

template<typename T, typename U, typename V>
inline void lut2_gather(const T *sx, const U *sy, V *d, int w, const V *lut, int bitsx, unsigned mx, unsigned my)
{
    const unsigned n = static_cast<unsigned>(w);
    const unsigned step = static_cast<unsigned>(svcntw());
    const svuint32_t vmx = svdup_u32(mx);
    const svuint32_t vmy = svdup_u32(my);
    const svuint32_t vsh = svdup_u32(static_cast<unsigned>(bitsx));

    for (unsigned i = 0; i < n; i += step) {
        svbool_t pg = svwhilelt_b32_u32(i, n);
        svuint32_t ix = svmin_u32_x(pg, load_widen<T>(pg, sx + i), vmx);
        svuint32_t iy = svmin_u32_x(pg, load_widen<U>(pg, sy + i), vmy);
        svuint32_t idx = svadd_u32_x(pg, svlsl_u32_x(pg, iy, vsh), ix);
        gather_store<V>(pg, d + i, lut, idx);
    }
}

} // namespace

void vs_lut2_gather_ww_w_sve(const uint16_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_ww_b_sve(const uint16_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_w_sve(const uint16_t *sx, const uint8_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_wb_b_sve(const uint16_t *sx, const uint8_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint16_t, uint8_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_w_sve(const uint8_t *sx, const uint16_t *sy, uint16_t *d, int w, const uint16_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint16_t>(sx, sy, d, w, lut, bitsx, mx, my); }
void vs_lut2_gather_bw_b_sve(const uint8_t *sx, const uint16_t *sy, uint8_t *d, int w, const uint8_t *lut, int bitsx, unsigned mx, unsigned my) { lut2_gather<uint8_t, uint16_t, uint8_t>(sx, sy, d, w, lut, bitsx, mx, my); }
