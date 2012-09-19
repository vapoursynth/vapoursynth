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
; License along with Libav; if not, write to the Free Software
; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "asm/x86inc.asm"

SECTION .text

INIT_XMM
cglobal isFPUStateOk, 0, 2, 0
	xor r1, r1
    fnstcw [esp - 4]
	mov r0w, [esp - 4]
    and r0w, 0x0f3f
    cmp r0w, 0x023f
	cmovne r0, r1
	RET

INIT_XMM
cglobal isMMXStateOk, 0, 2, 0
	xor r1, r1
    fnstenv [esp - 28]
	mov r0w, [esp - 20]
    cmp r0w, 0xffff
	cmovne r0, r1
	RET
