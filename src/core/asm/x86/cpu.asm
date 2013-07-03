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

cglobal cpuid_wrapper, 2, 2, 0
	push r0
	push r1
	push rbx
	mov rax, 1
	mov rcx, 0
	cpuid
	pop rbx
	pop rax
	mov [rax], edx
	pop rax
	mov [rax], ecx
	RET

