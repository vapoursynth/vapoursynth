; Copyright (c) 2013 Fredrik Mellbin
;
; This file is part of VapourSynth.
;
; VapourSynth is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; VapourSynth is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public
; License along with VapourSynth; if not, write to the Free Software
; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION .text

INIT_XMM
cglobal masked_merge_uint8_sse2, 6, 7, 6, src1, src2, mask, dst, stride, height, lineoffset
   pxor m5, m5
   pcmpeqb m4, m4
   psrlw m4, 15
   psllw m4, 1
.yloop:
   xor lineoffsetq, lineoffsetq
.xloop:
   ; load 8 pixels
   movh m0, [src1q+lineoffsetq]
   movh m1, [src2q+lineoffsetq]
   movh m2, [maskq+lineoffsetq]

   ; unpack into words
   punpcklbw m0, m5
   punpcklbw m1, m5
   punpcklbw m2, m5

   ; subtract
   psubw m1, m0

   ; prepare the mask pixels
   mova m3, m2
   pcmpgtw m3, m4
   psrlw m3, 15
   paddw m2, m3

   ; the idea below is to make sure the relevant bits end up in the high word
   ; this is done by shifting the multiplication result 8 bits to the left
   ; since m1 (srcp2-srcp1) only has 7 unused bits left we can't do all the shifting there
   ; and m2 (maskp) also only has 6 unused bits (sign bit can't be used and max value is 0x100) so shift both a little
   psllw m1, 4
   psllw m2, 4

   ; multiply
   mova m3, m2
   pmullw m2, m1
   pmulhw m3, m1

   ; round result
   psrlw m2, 15
   paddw m3, m2

   ; add srcp1
   paddw m3, m0

   ; write dstp[x]
   packuswb m3, m3
   movh [dstq+lineoffsetq], m3

   ; maybe it should decrement here instead to save a cmp
   add lineoffsetq, 8
   cmp lineoffsetq, strideq
   jnz .xloop

.xloopdone:
   add src1q, strideq
   add src2q, strideq
   add maskq, strideq
   add dstq, strideq

   sub heightq, 1
   jnz .yloop

.yloopdone:
   RET

INIT_XMM
cglobal merge_uint8_sse2, 6, 7, 8, src1, src2, mask, dst, stride, height, lineoffset
   movd m1, maskq ;mask
   pshuflw m1, m1, 0
   pshufd m1, m1, 0
   pcmpeqb m2, m2 ; always 2
   psrlw m2, 15
   psllw m2, 1

.yloop:
   xor lineoffsetq, lineoffsetq
.xloop:
   ; load 16 pixels
   mova m3, [src1q+lineoffsetq]
   mova m4, [src2q+lineoffsetq]

   pxor m0, m0

   ; unpack into words
   mova m5, m3
   mova m6, m4

   punpcklbw m3, m0
   punpcklbw m4, m0
   punpckhbw m5, m0
   punpckhbw m6, m0

   ; subtract
   psubw m4, m3
   psubw m6, m5

   psllw m4, 1
   psllw m6, 1
   
   ; multiply
   mova m0, m4
   mova m7, m6
   pmullw m0, m1
   pmullw m7, m1
   pmulhw m4, m1
   pmulhw m6, m1

   ; round result
   psrlw m0, 15
   psrlw m7, 15
   paddw m4, m0
   paddw m6, m7

   ; add srcp1
   paddw m3, m4
   paddw m5, m6

   ; write dstp[x]
   packuswb m3, m5
   mova [dstq+lineoffsetq], m3

   ; maybe it should decrement here instead to save a cmp
   add lineoffsetq, 16
   cmp lineoffsetq, strideq
   jnz .xloop

.xloopdone:
   add src1q, strideq
   add src2q, strideq
   add dstq, strideq

   sub heightq, 1
   jnz .yloop

.yloopdone:
   RET

INIT_XMM
cglobal masked_merge_uint16_sse2, 6, 7, 7, src1, src2, mask, dst, stride, height, lineoffset
   pxor m5, m5
   pcmpeqb m6, m6
   psrlw m6, 15
   psllw m6, 1
.yloop:
   xor lineoffsetq, lineoffsetq
.xloop:
   ; load 8 pixels
   mova m0, [src1q+lineoffsetq]
   mova m1, [src2q+lineoffsetq]
   mova m2, [maskq+lineoffsetq]

   mova m3, m0
   mova m4, m1

   ; subtract
   psubusw m3, m1
   psubusw m1, m0
   por m1, m3
   psraw m4, 15 ; sign mask for later use

   ; prepare the mask pixels
   mova m4, m2
   pcmpgtw m4, m6
   pand m4, m2
   psrlw m3, 15
   paddw m2, m3

   ; multiply
   mova m3, m2
   pmullw m2, m1
   pmulhw m3, m1

   ; round result
   psrlw m2, 15
   paddw m3, m2

   ; add srcp1
   paddw m3, m0

   ; write dstp[x]
   packuswb m3, m3
   movh [dstq+lineoffsetq], m3

   ; maybe it should decrement here instead to save a cmp
   add lineoffsetq, 16
   cmp lineoffsetq, strideq
   jnz .xloop

.xloopdone:
   add src1q, strideq
   add src2q, strideq
   add maskq, strideq
   add dstq, strideq

   sub heightq, 1
   jnz .yloop

.yloopdone:
   RET