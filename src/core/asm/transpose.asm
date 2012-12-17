; Copyright (c) 2012 Fredrik Mellbin
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

%include "asm/x86inc.asm"

CPU Pentium4

SECTION .text

; src, srcstride, dst, dststride

%macro TRANPOSE_WORD_SHARED 0
	mova m4, m0
	mova m5, m2
	punpcklwd m4, m1
	punpcklwd m5, m3
	mova m0, m4
	lea tmp1q, [dstq + dststrideq]
	punpckldq m4, m5
	punpckhdq m0, m5
%endmacro

INIT_XMM
cglobal transpose_word, 4, 5, 6, src, srcstride, dst, dststride, tmp1
	lea tmp1q, [srcq + srcstrideq]
	movh m0, [srcq]
	movh m1, [tmp1q]
	movh m2, [srcq + 2*srcstrideq]
	movh m3, [tmp1q + 2*srcstrideq]
	TRANPOSE_WORD_SHARED
	movh [dstq], m4
	movhps [tmp1q], m4
	movh [dstq + 2*dststrideq], m0
	movhps [tmp1q + 2*dststrideq], m0
	RET

INIT_XMM
cglobal transpose_word_partial, 6, 7, 6, src, srcstride, dst, dststride, readlines, writelines, tmp1
	lea tmp1q, [srcq + srcstrideq]
	dec readlinesq
	movh m0, [srcq]
	jz .transpose_start
	dec readlinesq
	movh m1, [tmp1q]
	jz .transpose_start
	dec readlinesq
	movh m2, [srcq + 2*srcstrideq]
	jz .transpose_start
	movh m3, [tmp1q + 2*srcstrideq]
	.transpose_start:
	TRANPOSE_WORD_SHARED
	dec writelinesq
	movh [dstq], m4
	jz .transpose_end
	dec writelinesq
	movhps [tmp1q], m4
	jz .transpose_end
	dec writelinesq
	movh [dstq + 2*dststrideq], m0
	jz .transpose_end
	movhps [tmp1q + 2*dststrideq], m0
	.transpose_end:
	RET

INIT_XMM
cglobal transpose_byte, 4, 6, 8, src, srcstride, dst, dststride, tmp1
	lea tmp1q, [srcq + srcstrideq]
	movh m0, [srcq]
	movh m1, [tmp1q]
	movh m2, [srcq + 2*srcstrideq]
	movh m3, [tmp1q + 2*srcstrideq]
	lea srcq, [srcq + 4*srcstrideq]
	lea tmp1q, [tmp1q + 4*srcstrideq]
	punpcklbw m0, m1
	punpcklbw m2, m3
	mova m1, m0
	movh m4, [srcq]
	movh m5, [tmp1q]
	movh m6, [srcq + 2*srcstrideq]
	movh m7, [tmp1q + 2*srcstrideq]
	punpcklwd m0, m2
	punpckhwd m1, m2
	punpcklbw m4, m5
	punpcklbw m6, m7
	mova m5, m4
	punpcklwd m4, m6
	punpckhwd m5, m6
	mova m2, m0
	punpckldq m0, m4
	punpckhdq m2, m4
	mova m3, m1
	lea tmp1q, [dstq + dststrideq]
	punpckldq m1, m5
	punpckhdq m3, m5
	movh [dstq], m0
	movhps [tmp1q], m0
	movh [dstq + 2*dststrideq], m2
	movhps [tmp1q + 2*dststrideq], m2
	lea dstq, [dstq + 4*dststrideq]
	lea tmp1q, [tmp1q + 4*dststrideq]
	movh [dstq], m1
	movhps [tmp1q], m1
	movh [dstq + 2*dststrideq], m3
	movhps [tmp1q + 2*dststrideq], m3
	RET

INIT_XMM
cglobal transpose_byte_partial, 5, 7, 8
	mov r6, r4
	mov r4, r0
	mov r5, r2
	add r4, r1
	add r5, r3
	movq m0, [r0]
	movq m1, [r4]
	movq m2, [r0 + 2*r1]
	movq m3, [r4 + 2*r1]
	lea r0, [r0 + 4*r1]
	lea r4, [r4 + 4*r1]
	punpcklbw m0, m1
	punpcklbw m2, m3
	mova m1, m0
	movq m4, [r0]
	movq m5, [r4]
	movq m6, [r0 + 2*r1]
	movq m7, [r4 + 2*r1]
	punpcklwd m0, m2
	punpckhwd m1, m2
	punpcklbw m4, m5
	punpcklbw m6, m7
	mova m5, m4
	punpcklwd m4, m6
	punpckhwd m5, m6
	mova m2, m0
	punpckldq m0, m4
	punpckhdq m2, m4
	mova m3, m1
	punpckldq m1, m5
	punpckhdq m3, m5
	
	sub r6, 1
	movq [r2], m0
	jz .done
	sub r6, 1
	movhpd [r5], m0
	jz .done
	sub r6, 1
	movq [r2 + 2*r3], m2
	jz .done
	sub r6, 1
	movhpd [r5 + 2*r3], m2
	jz .done
	sub r6, 1
	lea r2, [r2 + 4*r3]
	lea r5, [r5 + 4*r3]
	movq [r2], m1
	jz .done
	sub r6, 1
	movhpd [r5], m1
	jz .done
	movq [r2 + 2*r3], m3
.done:
	RET
