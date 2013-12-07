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

; The log and exp functions are an intrinsics to sse2 asm conversion
; of the code found in http://gruntthepeon.free.fr/ssemath/

%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION_RODATA

;vs_evaluate_expr_sse2(const void *exprs, const void * const * rwptrs, int numiterations, void *stack)
; m0 and m1 hold the top of the stack
; m7 is the handy zero register

%define iterationsize 2*mmsize
%define exprsize 8

%macro NEXT_EXPR 0
    add exprq, exprsize
    mov tmp1d, [exprq + 4]
%ifdef PIC
    mov tmp1q, [tmp2q + gprsize*tmp1q]
    add tmp1q, tmp2q
    jmp tmp1q
%else
    jmp [.jtable + gprsize*tmp1q]
%endif
%endmacro

%if ARCH_X86_64
    %define PTRDATA dq
%else
    %define PTRDATA dd
%endif

; labelname instruction
%macro TWO_ARG_OP 2
    .l_%1:
    sub stackq, iterationsize
    %2 m0, [stackq]
    %2 m1, [stackq+mmsize]
    NEXT_EXPR
%endmacro

%macro RTWO_ARG_OP 2
    .l_%1:
    sub stackq, iterationsize
    movaps m2, m0
    movaps m3, m1
    movaps m0, [stackq]
    movaps m1, [stackq+mmsize]
    %2 m0, m2
    %2 m1, m3
    NEXT_EXPR
%endmacro

%macro CMP_OP 2
    .l_%1:
    sub stackq, iterationsize
    movaps m2, [float_one]
    %2 m0, [stackq]
    %2 m1, [stackq + mmsize]
    andps m0, m2
    andps m1, m2
    NEXT_EXPR
%endmacro

%macro LOGIC_OP 2
    .l_%1:
    sub stackq, iterationsize
    movaps m6, [float_one]
    movaps m2, [stackq]
    movaps m3, [stackq + mmsize]
    cmpnleps m0, m7
    cmpnleps m1, m7
    cmpnleps m2, m7
    cmpnleps m3, m7
    %2 m0, m2
    %2 m1, m3
    andps m0, m6
    andps m1, m6
    NEXT_EXPR
%endmacro

%macro EXP_PS 1
    %define x %1
    %define fx m2
    %define emm0 m3
    %define etmp m4
    %define y m4
    %define mask m5
    %define z m6

    minps x, [exp_hi]
    maxps x, [exp_lo]

    movaps fx, x
    mulps fx, [cephes_LOG2EF]
    addps fx, [float_half]

    cvttps2dq emm0, fx
    cvtdq2ps etmp, emm0

    movaps mask, etmp
    cmpnleps mask, fx

    andps mask, [float_one]
    movaps fx, etmp
    subps fx, mask

    movaps etmp, fx
    mulps etmp, [cephes_exp_C1]

    movaps z, fx
    mulps z, [cephes_exp_C2]
    subps x, etmp
    subps x, z

    movaps z, x
    mulps z, z

    movaps y, [cephes_exp_p0]
    mulps y, x
    addps y, [cephes_exp_p1]
    mulps y, x
    addps y, [cephes_exp_p2]
    mulps y, x
    addps y, [cephes_exp_p3]
    mulps y, x
    addps y, [cephes_exp_p4]
    mulps y, x
    addps y, [cephes_exp_p5]
    mulps y, z
    addps y, x
    addps y, [float_one]

    cvttps2dq emm0, fx
    paddd emm0, [c7F]

    pslld emm0, 23
    mulps y, emm0
    movaps x, y

    %undef x
    %undef fx
    %undef emm0
    %undef etmp
    %undef y
    %undef mask
    %undef z
%endmacro

%macro LOG_PS 1
    %define x %1
    %define emm0 m2
    %define invalid_mask m3
    %define mask m4
    %define y m4
    %define etmp m5
    %define z m6

    xorps invalid_mask, invalid_mask
    cmpnleps invalid_mask, x

    maxps x, [min_norm_pos]

    mova emm0, x
    psrld emm0, 23

    andps x, [inv_mant_mask]
    orps x, [float_half]

    psubd emm0, [c7F]
    cvtdq2ps emm0, emm0

    addps emm0, [float_one]

    movaps mask, x
    cmpltps mask, [cephes_SQRTHF]

    movaps etmp, x
    andps etmp, mask

    subps x, [float_one]

    andps mask, [float_one]
    subps emm0, mask

    addps x, etmp

    movaps z, x
    mulps z, z

    movaps y, [cephes_log_p0]
    mulps y, x
    addps y, [cephes_log_p1]
    mulps y, x
    addps y, [cephes_log_p2]
    mulps y, x
    addps y, [cephes_log_p3]
    mulps y, x
    addps y, [cephes_log_p4]
    mulps y, x
    addps y, [cephes_log_p5]
    mulps y, x
    addps y, [cephes_log_p6]
    mulps y, x
    addps y, [cephes_log_p7]
    mulps y, x
    addps y, [cephes_log_p8]
    mulps y, x

    mulps y, z

    movaps etmp, emm0
    mulps etmp, [cephes_log_q1]
    addps y, etmp

    mulps z, [float_half]
    subps y, z

    mulps emm0, [cephes_log_q2]
    addps x, y
    addps x, emm0
    orps x, invalid_mask

    %undef x
    %undef emm0
    %undef invalid_mask
    %undef mask
    %undef y
    %undef etmp
    %undef z
%endmacro

%macro XMM_CONST 2
    %1 times 4 dd %2
%endmacro

; general constants
XMM_CONST absmask, 0x7FFFFFFF
XMM_CONST float_one, 1.0
XMM_CONST float_half, 0.5
XMM_CONST store8, 255.0
XMM_CONST store16, 65535.0

; EXP_PS/LOG_PS constants
XMM_CONST c7F, 0x7F
XMM_CONST exp_hi, 88.3762626647949
XMM_CONST exp_lo, -88.3762626647949

XMM_CONST cephes_LOG2EF, 1.44269504088896341
XMM_CONST cephes_exp_C1, 0.693359375
XMM_CONST cephes_exp_C2, -2.12194440e-4

XMM_CONST cephes_exp_p0, 1.9875691500E-4
XMM_CONST cephes_exp_p1, 1.3981999507E-3
XMM_CONST cephes_exp_p2, 8.3334519073E-3
XMM_CONST cephes_exp_p3, 4.1665795894E-2
XMM_CONST cephes_exp_p4, 1.6666665459E-1
XMM_CONST cephes_exp_p5, 5.0000001201E-1

XMM_CONST min_norm_pos, 0x00800000
XMM_CONST inv_mant_mask, ~0x7f800000
XMM_CONST cephes_SQRTHF, 0.707106781186547524
XMM_CONST cephes_log_p0, 7.0376836292E-2
XMM_CONST cephes_log_p1, -1.1514610310E-1
XMM_CONST cephes_log_p2, 1.1676998740E-1
XMM_CONST cephes_log_p3, -1.2420140846E-1
XMM_CONST cephes_log_p4, +1.4249322787E-1
XMM_CONST cephes_log_p5, -1.6668057665E-1
XMM_CONST cephes_log_p6, +2.0000714765E-1
XMM_CONST cephes_log_p7, -2.4999993993E-1
XMM_CONST cephes_log_p8, +3.3333331174E-1
XMM_CONST cephes_log_q1, -2.12194440e-4
XMM_CONST cephes_log_q2, 0.693359375

SECTION .text

INIT_XMM sse2
%ifdef PIC
cglobal evaluate_expr, 5, 8, 8, exprbase, rwptrs, ptroffsets, niterations, stack, expr, tmp1, tmp2
%else
cglobal evaluate_expr, 5, 7, 8, exprbase, rwptrs, ptroffsets, niterations, stack, expr, tmp1
%endif
%ifdef PIC
    lea tmp2q, [.jtable]
%endif
    xorps m7, m7
    jmp .loopstart

    align 16
    .jtable:
%ifdef PIC
    PTRDATA .l_load8 - .jtable, .l_load16 - .jtable, .l_loadf - .jtable, .l_loadconst - .jtable, \
    .l_store8 - .jtable, .l_store16 - .jtable, .l_storef - .jtable, \
    .l_dup - .jtable, .l_swap - .jtable, \
    .l_add - .jtable, .l_sub - .jtable, .l_mul - .jtable, .l_div - .jtable, .l_max - .jtable, .l_min - .jtable, .l_sqrt - .jtable, .l_abs - .jtable, \
    .l_gt - .jtable, .l_lt - .jtable, .l_eq - .jtable, .l_le - .jtable, .l_ge - .jtable, .l_ternary - .jtable, \
    .l_and - .jtable, .l_or - .jtable, .l_xor - .jtable, .l_neg - .jtable, \
    .l_exp - .jtable, .l_log - .jtable
%else
    PTRDATA .l_load8, .l_load16, .l_loadf, .l_loadconst, \
    .l_store8, .l_store16, .l_storef, \
    .l_dup, .l_swap, \
    .l_add, .l_sub, .l_mul, .l_div, .l_max, .l_min, .l_sqrt, .l_abs, \
    .l_gt, .l_lt, .l_eq, .l_le, .l_ge, .l_ternary, \
    .l_and, .l_or, .l_xor, .l_neg, \
    .l_exp, .l_log
%endif
    .loop:
    sub niterationsd, 1
    jz .end

%if ARCH_X86_64
    movu m2, [rwptrsq]
    movu m3, [ptroffsetsq]
    movu m4, [rwptrsq + mmsize]
    movu m5, [ptroffsetsq + mmsize]
    paddq m2, m3
    paddq m4, m5
    movu [rwptrsq], m2
    movu [rwptrsq + mmsize], m4
%else
    movu m2, [rwptrsq]
    movu m3, [ptroffsetsq]
    paddd m2, m3
    movu [rwptrsq], m2
%endif

    .loopstart:
    mov exprq, exprbaseq
    mov tmp1d, [exprbaseq + 4]
%ifdef PIC
    mov tmp1q, [tmp2q + gprsize*tmp1q]
    add tmp1q, tmp2q
    jmp tmp1q
%else
    jmp [.jtable + gprsize*tmp1q]
%endif

    .l_load8:
    mov tmp1d, [exprq]
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    mov tmp1q, [rwptrsq + gprsize*tmp1q + gprsize]
    movh m0, [tmp1q]
    punpcklbw m0, m7
    mova m1, m0
    punpckhwd m0, m7
    punpcklwd m1, m7
    cvtdq2ps m0, m0
    cvtdq2ps m1, m1
    add stackq, iterationsize
    NEXT_EXPR

    .l_load16:
    mov tmp1d, [exprq]
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    mov tmp1q, [rwptrsq + gprsize*tmp1q + gprsize]
    mova m0, [tmp1q]
    mova m1, m0
    punpckhwd m0, m7
    punpcklwd m1, m7
    cvtdq2ps m0, m0
    cvtdq2ps m1, m1
    add stackq, iterationsize
    NEXT_EXPR

    .l_loadf:
    mov tmp1d, [exprq]
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    mov tmp1q, [rwptrsq + gprsize*tmp1q + gprsize]
    movaps m0, [tmp1q]
    movaps m1, [tmp1q + mmsize]
    add stackq, iterationsize
    NEXT_EXPR

    .l_loadconst:
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    movss m0, [exprq]
    shufps m0, m0, 0
    movaps m1, m0
    add stackq, iterationsize
    NEXT_EXPR

    .l_store8:
    mov tmp1q, [rwptrsq]
    movaps m6, [store8]
    maxps m0, m7
    maxps m1, m7
    minps m0, m6
    minps m1, m6
    cvtps2dq m0, m0
    cvtps2dq m1, m1
    mova m2, m0
    mova m3, m1
    psrldq m0, 6
    psrldq m1, 6
    por m0, m2
    por m1, m3
    pshuflw m0, m0, q3120
    pshuflw m1, m1, q3120
    punpcklqdq m1, m0
    packuswb m1, m7
    movh [tmp1q], m1
    sub stackq, iterationsize
    jmp .loop

    .l_store16:
    mov tmp1q, [rwptrsq]
    movaps m6, [store16]
    maxps m0, m7
    maxps m1, m7
    minps m0, m6
    minps m1, m6
    cvtps2dq m0, m0
    cvtps2dq m1, m1
    mova m2, m0
    mova m3, m1
    psrldq m0, 6
    psrldq m1, 6
    por m0, m2
    por m1, m3
    pshuflw m0, m0, q3120
    pshuflw m1, m1, q3120
    punpcklqdq m1, m0
    mova [tmp1q], m1
    sub stackq, iterationsize
    jmp .loop

    .l_storef:
    mov tmp1q, [rwptrsq]
    movaps [tmp1q], m0
    movaps [tmp1q + mmsize], m1
    sub stackq, iterationsize
    jmp .loop

    .l_dup:
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    add stackq, iterationsize
    NEXT_EXPR

    .l_swap:
    movaps m2, [stackq]
    movaps m3, [stackq + mmsize]
    movaps [stackq], m0
    movaps [stackq + mmsize], m1
    movaps m0, m2
    movaps m1, m3
    NEXT_EXPR

    TWO_ARG_OP    add, addps
    RTWO_ARG_OP    sub, subps
    TWO_ARG_OP    mul, mulps
    RTWO_ARG_OP    div, divps
    TWO_ARG_OP    max, maxps
    TWO_ARG_OP    min, minps

    .l_sqrt:
    maxps m0, m7
    maxps m1, m7
    sqrtps m0, m0
    sqrtps m1, m1
    NEXT_EXPR

    .l_abs:
    movaps m2, [absmask]
    andps m0, m2
    andps m1, m2
    NEXT_EXPR

    CMP_OP gt, cmpltps
    CMP_OP lt, cmpnleps
    CMP_OP eq, cmpeqps
    CMP_OP le, cmpnltps
    CMP_OP ge, cmpleps

    .l_ternary:
    sub stackq, iterationsize*2
    movaps m2, [stackq]
    movaps m3, [stackq + mmsize]
    xorps m6, m6
    cmpltps m6, m2
    cmpltps m7, m3
    movaps m4, [stackq + iterationsize]
    movaps m5, [stackq + mmsize + iterationsize]
    andps m4, m6
    andps m5, m7
    andnps m6, m0
    andnps m7, m1
    orps m6, m4
    orps m7, m5
    movaps m0, m6
    movaps m1, m7
    xorps m7, m7
    NEXT_EXPR

    LOGIC_OP and, andps
    LOGIC_OP or, orps
    LOGIC_OP xor, xorps

    .l_neg:
    movaps m2, [float_one]
    xorps m6, m6
    cmpltps m6, m0
    cmpltps m7, m1
    andps m0, m2
    andps m1, m2
    xorps m7, m7
    NEXT_EXPR

    .l_exp:
    EXP_PS m0
    EXP_PS m1
    NEXT_EXPR

    .l_log:
    LOG_PS m0
    LOG_PS m1
    NEXT_EXPR

    .end:
    RET
