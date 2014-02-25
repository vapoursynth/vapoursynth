%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION_RODATA

masks_8bit ddq 0x00000000000000000000000000000000, \
               0xff000000000000000000000000000000, \
               0xffff0000000000000000000000000000, \
               0xffffff00000000000000000000000000, \
               0xffffffff000000000000000000000000, \
               0xffffffffff0000000000000000000000, \
               0xffffffffffff00000000000000000000, \
               0xffffffffffffff000000000000000000, \
               0xffffffffffffffff0000000000000000, \
               0xffffffffffffffffff00000000000000, \
               0xffffffffffffffffffff000000000000, \
               0xffffffffffffffffffffff0000000000, \
               0xffffffffffffffffffffffff00000000, \
               0xffffffffffffffffffffffffff000000, \
               0xffffffffffffffffffffffffffff0000, \
               0xffffffffffffffffffffffffffffff00

masks_16bit ddq 0x00000000000000000000000000000000, \
                0xffff0000000000000000000000000000, \
                0xffffffff000000000000000000000000, \
                0xffffffffffff00000000000000000000, \
                0xffffffffffffffff0000000000000000, \
                0xffffffffffffffffffff000000000000, \
                0xffffffffffffffffffffffff00000000, \
                0xffffffffffffffffffffffffffff0000



SECTION .text


; parameters:
;  r0: srcp1
;  r1: srcp2
;  r2: width
;  r3: height
;  r4: stride

INIT_XMM
cglobal planediff_uint8_sse2, 5, 7, 4
    ; keep the sums here
    pxor m0, m0

    ; if there are extra pixels on the right, process them, then process the rest
    mov r6, r2
    and r6, 15
    jz .modsixteen

    ; multiply r6 by 16 to get the offset for the mask
    shl r6, 4
    lea r5, [masks_8bit]
    add r5, r6
    mova m1, [r5]

    ; width / 16 * 16
    ; if this is 0, there is only the last column
    mov r5, r2
    shr r5, 4
    shl r5, 4

    push r0
    push r1
    push r3

    ; Move the pointers to the last column
    add r0, r5
    add r1, r5

.yloopcolumn:
    mova m2, [r0]
    mova m3, [r1]
    pand m2, m1
    pand m3, m1
    psadbw m2, m3
    paddq m0, m2

    add r0, r4
    add r1, r4

    sub r3, 1
    jnz .yloopcolumn

    pop r3
    pop r1
    pop r0

    cmp r5, 0
    jz .alldone

.modsixteen:
    mov r6, r2
    shr r6, 4
    shl r6, 4
.yloop:
    xor r5, r5
.xloop:
    ; do stuff
    mova m1, [r0+r5]
    mova m2, [r1+r5]

    psadbw m1, m2
    paddq m0, m1

    add r5, 16
    cmp r5, r6
    jnz .xloop

.xloopdone:
    add r0, r4
    add r1, r4
    
    sub r3, 1
    jnz .yloop

.yloopdone:

.alldone:
    mova m1, m0
    psrldq m1, 8
    paddq m0, m1

%if ARCH_X86_64
    movq rax, m0
%else
    movd eax, m0
    psrldq m0, 4
    movd edx, m0
%endif

    RET


; parameters:
;  r0: srcp1
;  r1: srcp2
;  r2: width
;  r3: height
;  r4: stride

INIT_XMM
cglobal planediff_uint16_sse2, 5, 7, 8
    ; keep the sums here
    pxor m0, m0

    ; zeroes for psadbw
    pxor m6, m6

    ; if there are extra pixels on the right, process them, then process the rest
    mov r6, r2
    and r6, 7
    jz .modeight

    ; multiply r6 by 16 to get the offset for the mask
    shl r6, 4
    lea r5, [masks_16bit]
    add r5, r6
    mova m1, [r5]

    ; width / 8 * 8
    ; if this is 0, there is only the last column
    mov r5, r2
    shr r5, 3
    shl r5, 4 ; +1 because each pixel is two bytes

    push r0
    push r1
    push r3

    ; Move the pointers to the last column
    add r0, r5
    add r1, r5

.yloopcolumn:
    mova m2, [r0]
    mova m3, [r1]
    pand m2, m1
    pand m3, m1

    mova m4, m2
    psubusw m4, m3
    psubusw m3, m2
    por m3, m4 ; absolute differences

    mova m2, m3
    psllw m2, 8
    psadbw m2, m6 ; sums of the low bytes

    psrlw m3, 8
    psadbw m3, m6 ; sums of the high bytes
    pslld m3, 8

    paddd m2, m3

    paddq m0, m2


    add r0, r4
    add r1, r4

    sub r3, 1
    jnz .yloopcolumn

    pop r3
    pop r1
    pop r0

    cmp r5, 0
    jz .alldone

.modeight:
    mov r6, r2
    shr r6, 3
    shl r6, 4

.yloop:
    xor r5, r5
.xloop:
    ; do stuff
    mova m2, [r0+r5]
    mova m3, [r1+r5]

    mova m4, m2
    psubusw m4, m3
    psubusw m3, m2
    por m3, m4 ; absolute differences

    mova m2, m3
    psllw m2, 8
    psadbw m2, m6 ; sums of the low bytes

    psrlw m3, 8
    psadbw m3, m6 ; sums of the high bytes
    pslld m3, 8

    paddd m2, m3

    paddq m0, m2


    add r5, 16
    cmp r5, r6
    jnz .xloop

.xloopdone:
    add r0, r4
    add r1, r4
    
    sub r3, 1
    jnz .yloop

.yloopdone:

.alldone:
    mova m1, m0
    psrldq m1, 8
    paddq m0, m1

%if ARCH_X86_64
    movq rax, m0
%else
    movd eax, m0
    psrldq m0, 4
    movd edx, m0
%endif

    RET


; parameters:
;  r0: srcp
;  r1: width
;  r2: height
;  r3: stride

INIT_XMM
cglobal planeavg_uint8_sse2, 4, 7, 8
    ; keep the sums here
    pxor m0, m0

    ; zeroes
    pxor m7, m7

    ; if there are extra pixels on the right, process them, then process the rest
    mov r6, r1
    and r6, 15
    jz .modsixteen

    ; multiply r6 by 16 to get the offset for the mask
    shl r6, 4
    lea r5, [masks_8bit]
    add r5, r6
    mova m1, [r5]

    ; width / 16 * 16
    ; if this is 0, there is only the last column
    mov r5, r1
    shr r5, 4
    shl r5, 4

    push r0
    push r2

    ; Move the pointer to the last column
    add r0, r5

.yloopcolumn:
    mova m2, [r0]
    pand m2, m1
    psadbw m2, m7
    paddq m0, m2

    add r0, r3

    sub r2, 1
    jnz .yloopcolumn

    pop r2
    pop r0

    cmp r5, 0
    jz .alldone

.modsixteen:
    mov r6, r1
    shr r6, 4
    shl r6, 4
.yloop:
    xor r5, r5
.xloop:
    ; do stuff
    mova m1, [r0+r5]

    psadbw m1, m7
    paddq m0, m1

    add r5, 16
    cmp r5, r6
    jnz .xloop

.xloopdone:
    add r0, r3
    add r1, r3
    
    sub r2, 1
    jnz .yloop

.yloopdone:

.alldone:
    mova m1, m0
    psrldq m1, 8
    paddq m0, m1

%if ARCH_X86_64
    movq rax, m0
%else
    movd eax, m0
    psrldq m0, 4
    movd edx, m0
%endif

    RET


; parameters:
;  r0: srcp
;  r1: width
;  r2: height
;  r3: stride

INIT_XMM
cglobal planeavg_uint16_sse2, 4, 7, 8
    ; keep the sums here
    pxor m0, m0

    ; zeroes for psadbw
    pxor m6, m6

    ; if there are extra pixels on the right, process them, then process the rest
    mov r6, r1
    and r6, 7
    jz .modeight

    ; multiply r6 by 16 to get the offset for the mask
    shl r6, 4
    lea r5, [masks_16bit]
    add r5, r6
    mova m1, [r5]

    ; width / 8 * 8
    ; if this is 0, there is only the last column
    mov r5, r1
    shr r5, 3
    shl r5, 4 ; +1 because each pixel is two bytes

    push r0
    push r2

    ; Move the pointer to the last column
    add r0, r5

.yloopcolumn:
    mova m2, [r0]
    pand m2, m1

    mova m3, m2
    psllw m3, 8
    psadbw m3, m6 ; sums of the low bytes

    psrlw m2, 8
    psadbw m2, m6 ; sums of the high bytes
    pslld m2, 8

    paddd m2, m3

    paddq m0, m2
    

    add r0, r3

    sub r2, 1
    jnz .yloopcolumn

    pop r2
    pop r0

    cmp r5, 0
    jz .alldone

.modeight:
    mov r6, r1
    shr r6, 3
    shl r6, 4

.yloop:
    xor r5, r5
.xloop:
    ; do stuff
    mova m2, [r0+r5]

    mova m3, m2
    psllw m3, 8
    psadbw m3, m6 ; sums of the low bytes

    psrlw m2, 8
    psadbw m2, m6 ; sums of the high bytes
    pslld m2, 8

    paddd m2, m3

    paddq m0, m2


    add r5, 16
    cmp r5, r6
    jnz .xloop

.xloopdone:
    add r0, r3
    
    sub r2, 1
    jnz .yloop

.yloopdone:

.alldone:
    mova m1, m0
    psrldq m1, 8
    paddq m0, m1

%if ARCH_X86_64
    movq rax, m0
%else
    movd eax, m0
    psrldq m0, 4
    movd edx, m0
%endif

    RET
