/*
* Copyright (c) 2026 Fredrik Mellbin
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

#ifndef VSPIPE_EBML_H
#define VSPIPE_EBML_H

#include <cstdint>
#include <string>
#include <vector>

/* Generic EBML encoding, with no knowledge of Matroska. Everything is appended to a caller owned
   byte vector so that a complete element can be sized once it is built, which is what lets the
   whole muxer work without ever seeking in the output. */

typedef std::vector<uint8_t> EbmlBuffer;

/* Element ids are stored with their length marker already included, so 0xA3 is one byte and
   0x1F43B675 is four, and they are written out unchanged. */
void ebmlPutId(EbmlBuffer &dst, uint32_t id);

/* Data sizes use the same variable length integer as ids, except the value is written in the
   smallest form that fits. */
void ebmlPutSize(EbmlBuffer &dst, uint64_t size);

/* Complete elements. Integers are written in the shortest big endian form that keeps their value,
   which is what every other muxer emits and what keeps the header small. */
void ebmlUInt(EbmlBuffer &dst, uint32_t id, uint64_t value);
void ebmlFloat(EbmlBuffer &dst, uint32_t id, double value);
void ebmlString(EbmlBuffer &dst, uint32_t id, const std::string &value);
void ebmlBinary(EbmlBuffer &dst, uint32_t id, const uint8_t *data, size_t size);

/* Wraps an already built payload in its id and size. The payload is consumed from a separate
   buffer rather than written in place, since a master element cannot be sized until its children
   are complete. */
void ebmlMaster(EbmlBuffer &dst, uint32_t id, const EbmlBuffer &payload);

/* Opens a master element whose length is not yet known. Used for Segment and Cluster. */
void ebmlMasterUnknownSize(EbmlBuffer &dst, uint32_t id);

#endif
