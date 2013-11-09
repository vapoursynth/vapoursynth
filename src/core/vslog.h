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

#ifndef VSLOG_H
#define VSLOG_H

#include <VapourSynth.h>

#define vsDebug(msg, ...)    vsLog(__FILE__, __LINE__, mtDebug, (msg), __VA_ARGS__)
#define vsWarning(msg, ...)  vsLog(__FILE__, __LINE__, mtWarning, (msg), __VA_ARGS__)
#define vsCritical(msg, ...) vsLog(__FILE__, __LINE__, mtCritical, (msg), __VA_ARGS__)
#define vsFatal(msg, ...)    vsLog(__FILE__, __LINE__, mtFatal, (msg), __VA_ARGS__)

void vsSetMessageHandler(VSMessageHandler handler, void *userData);
void vsLog(const char *file, long line, VSMessageType type, const char *msg, ...);

#endif