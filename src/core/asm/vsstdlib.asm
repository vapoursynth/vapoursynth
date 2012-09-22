; Copyright (c) 2012 Fredrik Mellbin
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.

%include "asm/x86inc.asm"

SECTION .text

; src, srcstride, dst, dststride
INIT_XMM
cglobal transpose_word, 4, 6, 7
	mov r4, r0
	mov r5, r2
	add r4, r1
	add r5, r3
	movq m0, [r0]
	movq m1, [r4]
	movq m2, [r0 + 2*r1]
	movq m3, [r4 + 2*r1]
	mova m4, m0
	mova m5, m2
	punpcklwd m4, m1
	punpcklwd m5, m3
	mova m6, m4
	punpckldq m4, m5
	punpckhdq m6, m5
	movq [r2], m4
	movhpd [r5], m4
	movq [r2 + 2*r3], m6
	movhpd [r5 + 2*r3], m6
	RET

INIT_XMM
cglobal transpose_word_partial, 5, 7, 7
	mov r6, r4
	mov r4, r0
	mov r5, r2
	add r4, r1
	add r5, r3
	movq m0, [r0]
	movq m1, [r4]
	movq m2, [r0 + 2*r1]
	movq m3, [r4 + 2*r1]
	mova m4, m0
	mova m5, m2
	punpcklwd m4, m1
	punpcklwd m5, m3
	mova m6, m4
	punpckldq m4, m5
	punpckhdq m6, m5
	sub r6, 1
	movq [r2], m4
	jz .done
	sub r6, 1
	movhpd [r5], m4
	jz .done
	movq [r2 + 2*r3], m6
.done:
	RET

INIT_XMM
cglobal transpose_byte, 4, 6, 8
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
	
	movq [r2], m0
	movhpd [r5], m0
	movq [r2 + 2*r3], m2
	movhpd [r5 + 2*r3], m2
	lea r2, [r2 + 4*r3]
	lea r5, [r5 + 4*r3]
	movq [r2], m1
	movhpd [r5], m1
	movq [r2 + 2*r3], m3
	movhpd [r5 + 2*r3], m3
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
