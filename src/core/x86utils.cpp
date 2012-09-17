//  Copyright (c) 2012 Fredrik Mellbin
//
//  This file is part of VapourSynth.
//
//  VapourSynth is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as
//  published by the Free Software Foundation, either version 3 of the
//  License, or (at your option) any later version.
//
//  VapourSynth is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with VapourSynth.  If not, see <http://www.gnu.org/licenses/>.

#include <stdint.h>

#if defined(_WIN32) && !defined(_WIN64)

bool isFPUStateOk() {
    uint32_t ctlword = 0;
    uint32_t *ctlwordp = &ctlword;
    __asm mov eax, ctlwordp
    __asm fnstcw [eax]
    ctlword &= 0x0f3f;
    return ctlword == 0x023f;
}

bool isMMXStateOk() {
    char buf[28];
    __asm fnstenv buf
    unsigned short tagword = *(unsigned short *)(buf + 8);
    return tagword == 0xffff;
}

#endif