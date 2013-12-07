; Copyright (c) 2012-2013 Fredrik Mellbin
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

; A fancy pdf with a detailed description of how a transpose in MMX works can be found at:
; http://software.intel.com/sites/default/files/m/c/c/a/MMX_App_Transpose_Matrix.pdf

%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION .text

; src, srcstride, dst, dststride

%macro TRANPOSE_WORD_SHARED 0
    lea tmp1q, [srcq + srcstrideq]
    movh m0, [srcq]
    movh m1, [tmp1q]
    movh m2, [srcq + 2*srcstrideq]
    movh m3, [tmp1q + 2*srcstrideq]
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
    TRANPOSE_WORD_SHARED
    movh [dstq], m4
    movhps [tmp1q], m4
    movh [dstq + 2*dststrideq], m0
    movhps [tmp1q + 2*dststrideq], m0
    RET

INIT_XMM
cglobal transpose_word_partial, 5, 6, 6, src, srcstride, dst, dststride, writelines, tmp1
    TRANPOSE_WORD_SHARED
    sub writelinesd, 1
    movh [dstq], m4
    jz .transpose_end
    sub writelinesd, 1
    movhps [tmp1q], m4
    jz .transpose_end
    sub writelinesd, 1
    movh [dstq + 2*dststrideq], m0
    jz .transpose_end
    movhps [tmp1q + 2*dststrideq], m0
    .transpose_end:
    RET

%macro TRANPOSE_BYTE_SHARED 0
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
%endmacro

INIT_XMM
cglobal transpose_byte, 4, 5, 8, src, srcstride, dst, dststride, tmp1
    TRANPOSE_BYTE_SHARED
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
cglobal transpose_byte_partial, 5, 6, 8, src, srcstride, dst, dststride, writelines, tmp1
    TRANPOSE_BYTE_SHARED
    sub writelinesd, 1
    movh [dstq], m0
    jz .write_done
    sub writelinesd, 1
    movhps [tmp1q], m0
    jz .write_done
    sub writelinesd, 1
    movh [dstq + 2*dststrideq], m2
    jz .write_done
    sub writelinesd, 1
    movhps [tmp1q + 2*dststrideq], m2
    jz .write_done
    sub writelinesd, 1
    lea dstq, [dstq + 4*dststrideq]
    lea tmp1q, [tmp1q + 4*dststrideq]
    movh [dstq], m1
    jz .write_done
    sub writelinesd, 1
    movhps [tmp1q], m1
    jz .write_done
    movh [dstq + 2*dststrideq], m3
    .write_done:
    RET
