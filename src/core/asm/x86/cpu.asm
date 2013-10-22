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

%include "asm/x86/x86inc.asm"

CPU Pentium4

SECTION .text

cglobal cpuid_wrapper, 5, 5, 0, level, peax, pebx, pecx, pedx
	push rbx
	push peaxq
    push pebxq
    push pecxq
    push pedxq
	mov rax, levelq
	xor rcx, rcx
	cpuid
	pop rax
	mov [rax], edx
	pop rax
	mov [rax], ecx
	pop rax
	mov [rax], ebx
	pop rax
	mov [rax], eax
	pop rbx
	RET

