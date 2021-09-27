/*
* Copyright (c) 2016-2021 Fredrik Mellbin
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

#ifndef FOURCC_H
#define FOURCC_H

#include "VapourSynth4.h"

#define VS_FCC(ch4) ((((unsigned long)(ch4) & 0xFF) << 24) |     \
                  (((unsigned long)(ch4) & 0xFF00) << 8) |    \
                  (((unsigned long)(ch4) & 0xFF0000) >> 8) |  \
                  (((unsigned long)(ch4) & 0xFF000000) >> 24))

bool GetFourCC(const VSVideoFormat &fi, int alt_output, unsigned long &fourcc);
bool GetBiCompression(const VSVideoFormat &format, int alt_output, unsigned long &compression);
int BMPSize(const VSVideoInfo *vi, int alt_output);
int BitsPerPixel(const VSVideoFormat &format, int alt_output);
bool HasSupportedFourCC(const VSVideoFormat &fi);
bool NeedsPacking(const VSVideoFormat &fi, int alt_output);
bool NeedsUVSwap(const VSVideoFormat &fi, int alt_output); // Returns false for YVU plane order and true for YUV when doing planar output
void PackOutputFrame(const uint8_t *src[3], const ptrdiff_t src_stride[3], uint8_t *dst, int width, int height, const VSVideoFormat &fi, int alt_output);

#endif