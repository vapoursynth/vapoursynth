/*
* Copyright (c) 2012-2013 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// This file only exists for V3 compatibility

#ifndef VSLOG_H
#define VSLOG_H

#include "VapourSynth4.h"
#include "VapourSynth3.h"

#ifdef __cplusplus
extern "C" {
#endif

void vsSetMessageHandler3(vs3::VSMessageHandler handler, void *userData);
int vsAddMessageHandler3(vs3::VSMessageHandler handler, vs3::VSMessageHandlerFree free, void *userData);
int vsRemoveMessageHandler3(int id);
void vsLog3(vs3::VSMessageType type, const char *msg, ...);

#ifdef __cplusplus
}
#endif

#endif
