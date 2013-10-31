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

%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION .text

INIT_XMM
cglobal isFPUStateOk, 0, 0, 0
    xor rcx, rcx
    fnstcw [rsp - 4]
    mov ax, [rsp - 4]
    and ax, 0x0f3f
    cmp ax, 0x023f
    cmovne rax, rcx
    RET

INIT_XMM
cglobal isMMXStateOk, 0, 0, 0
    xor rcx, rcx
    fnstenv [rsp - 28]
    mov ax, [rsp - 20]
    cmp ax, 0xffff
    cmovne rax, rcx
    RET

INIT_XMM
cglobal isSSEStateOk, 0, 0, 0
    xor rcx, rcx
    stmxcsr [rsp - 4]
    mov ax, [rsp - 4]
    and ax, 0x7f80
    cmp ax, 0x1f80
    cmovne rax, rcx
    RET
