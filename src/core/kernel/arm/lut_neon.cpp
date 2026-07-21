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
* NEON byte -> byte Lut scanline. The whole 256-entry table lives in sixteen
* vector registers; each block of 16 pixels resolves with four vqtbl4q
* lookups (each covers a 64-entry window, returning 0 out of range) OR-ed
* together. Like the scalar fallback, the row is processed in chunks that may
* extend a few bytes past `w`; frame rows are padded, so this is safe.
*/

#include <arm_neon.h>
#include <cstdint>

void vs_lut1_b_b_neon(const uint8_t *src, uint8_t *dst, int w, const uint8_t *lut)
{
    uint8x16x4_t t0 = vld1q_u8_x4(lut);
    uint8x16x4_t t1 = vld1q_u8_x4(lut + 64);
    uint8x16x4_t t2 = vld1q_u8_x4(lut + 128);
    uint8x16x4_t t3 = vld1q_u8_x4(lut + 192);
    uint8x16_t c64 = vdupq_n_u8(64);

    auto lookup16 = [&](uint8x16_t idx) -> uint8x16_t {
        uint8x16_t r = vqtbl4q_u8(t0, idx);
        idx = vsubq_u8(idx, c64);
        r = vorrq_u8(r, vqtbl4q_u8(t1, idx));
        idx = vsubq_u8(idx, c64);
        r = vorrq_u8(r, vqtbl4q_u8(t2, idx));
        idx = vsubq_u8(idx, c64);
        return vorrq_u8(r, vqtbl4q_u8(t3, idx));
    };

    int x = 0;
    for (; x + 16 <= w; x += 16)
        vst1q_u8(dst + x, lookup16(vld1q_u8(src + x)));
    // Tail: 8-pixel chunk with the same <= 7 byte overrun as the scalar path.
    for (; x < w; x += 8) {
        uint8x8_t idx8 = vld1_u8(src + x);
        uint8x16_t r = lookup16(vcombine_u8(idx8, idx8));
        vst1_u8(dst + x, vget_low_u8(r));
    }
}
