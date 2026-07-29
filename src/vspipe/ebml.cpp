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

#include "ebml.h"

#include <cassert>
#include <cstring>

void ebmlPutId(EbmlBuffer &dst, uint32_t id) {
    /* The length is implied by the position of the highest set bit, which is exactly how the id
       was written down in the spec, so the only thing to do is drop the leading zero bytes. */
    int length = 4;
    if (id <= 0xFF)
        length = 1;
    else if (id <= 0xFFFF)
        length = 2;
    else if (id <= 0xFFFFFF)
        length = 3;

    for (int i = length - 1; i >= 0; i--)
        dst.push_back(static_cast<uint8_t>(id >> (i * 8)));
}

int ebmlSizeLength(uint64_t size) {
    /* Each added byte contributes seven usable bits, and the all ones value of every width is
       reserved to mean unknown, so a value that would fill the width completely needs one more. */
    for (int length = 1; length <= 8; length++) {
        uint64_t limit = (static_cast<uint64_t>(1) << (length * 7)) - 1;
        if (size < limit)
            return length;
    }
    return 8;
}

void ebmlPutSize(EbmlBuffer &dst, uint64_t size) {
    int length = ebmlSizeLength(size);
    /* The marker bit says how many bytes the whole field occupies and sits in the top byte. */
    dst.push_back(static_cast<uint8_t>((size >> ((length - 1) * 8)) | (0x80 >> (length - 1))));
    for (int i = length - 2; i >= 0; i--)
        dst.push_back(static_cast<uint8_t>(size >> (i * 8)));
}

void ebmlPutUnknownSize(EbmlBuffer &dst) {
    /* One byte of all ones. Wider forms exist but readers handle the short one and it keeps the
       header compact. */
    dst.push_back(0xFF);
}

static int uintLength(uint64_t value) {
    int length = 1;
    while (length < 8 && (value >> (length * 8)))
        length++;
    return length;
}

void ebmlUInt(EbmlBuffer &dst, uint32_t id, uint64_t value) {
    int length = uintLength(value);
    ebmlPutId(dst, id);
    ebmlPutSize(dst, static_cast<uint64_t>(length));
    for (int i = length - 1; i >= 0; i--)
        dst.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void ebmlSInt(EbmlBuffer &dst, uint32_t id, int64_t value) {
    /* Signed values are two's complement, so the shortest form is the one that still keeps the
       sign bit of the leading byte correct. */
    int length = 1;
    while (length < 8) {
        int64_t limit = static_cast<int64_t>(1) << (length * 8 - 1);
        if (value >= -limit && value < limit)
            break;
        length++;
    }

    ebmlPutId(dst, id);
    ebmlPutSize(dst, static_cast<uint64_t>(length));
    for (int i = length - 1; i >= 0; i--)
        dst.push_back(static_cast<uint8_t>(static_cast<uint64_t>(value) >> (i * 8)));
}

void ebmlFloat(EbmlBuffer &dst, uint32_t id, double value) {
    /* Always written as a double. The four byte form is legal but there is no reason to lose
       precision on the handful of floats a header contains. */
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    memcpy(&bits, &value, sizeof(bits));

    ebmlPutId(dst, id);
    ebmlPutSize(dst, 8);
    for (int i = 7; i >= 0; i--)
        dst.push_back(static_cast<uint8_t>(bits >> (i * 8)));
}

void ebmlString(EbmlBuffer &dst, uint32_t id, const std::string &value) {
    ebmlPutId(dst, id);
    ebmlPutSize(dst, value.size());
    dst.insert(dst.end(), value.begin(), value.end());
}

void ebmlBinary(EbmlBuffer &dst, uint32_t id, const uint8_t *data, size_t size) {
    ebmlPutId(dst, id);
    ebmlPutSize(dst, size);
    dst.insert(dst.end(), data, data + size);
}

void ebmlMaster(EbmlBuffer &dst, uint32_t id, const EbmlBuffer &payload) {
    ebmlPutId(dst, id);
    ebmlPutSize(dst, payload.size());
    dst.insert(dst.end(), payload.begin(), payload.end());
}

void ebmlMasterUnknownSize(EbmlBuffer &dst, uint32_t id) {
    ebmlPutId(dst, id);
    ebmlPutUnknownSize(dst);
}
