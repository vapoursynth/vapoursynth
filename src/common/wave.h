/*
* Copyright (c) 2020 Fredrik Mellbin
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

#ifndef WAVE_H
#define WAVE_H


#if !defined(UUID_T_DEFINED) && !defined(uuid_t)
struct uuid_t {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};
#endif

static const unsigned waveMaxSampleBlockSize = 128;
static const uint64_t maxWaveFileSize = 0xFFFFFFFE;
static const uint64_t maxCompatWaveFileSize = 0x7FFFFFFE;

struct WaveHeader {
    uint32_t riffTag;
    uint32_t riffSize;
    uint32_t waveTag;
    uint32_t fmtTag;
    uint32_t fmtSize;
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    // uint16_t cbSize;
    // uint8_t fmtExtra[cbSize];
    uint32_t dataTag;
    uint32_t dataSize;
    // uint8_t data[dataSize];
};

#define MAKE_RIFF_TAG(c0,c1,c2,c3) static_cast<uint32_t>((c0)|((c1)<<8)|((c2)<<16)|((c3)<<24))

static_assert(sizeof(WaveHeader) == 44, "");
static const uint32_t waveHdrRiffTagVal = MAKE_RIFF_TAG('R', 'I', 'F', 'F');
static const uint32_t waveHdrWaveTagVal = MAKE_RIFF_TAG('W', 'A', 'V', 'E');
static const uint32_t waveHdrFmtTagVal = MAKE_RIFF_TAG('f', 'm', 't', ' ');
static const uint32_t waveHdrDataTagVal = MAKE_RIFF_TAG('d', 'a', 't', 'a');

struct Wave64Header {
    uuid_t riffUuid;
    uint64_t riffSize;
    uuid_t waveUuid;
    uuid_t fmtUuid;
    uint64_t fmtSize;
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uuid_t dataUuid;
    uint64_t dataSize;
};

static_assert(sizeof(Wave64Header) == 104, "");
static const uuid_t wave64HdrRiffUuidVal = { 0x66666972u,0x912Eu,0x11CFu,{0xA5u,0xD6u,0x28u,0xDBu,0x04u,0xC1u,0x00u,0x00u} };
static const uuid_t wave64HdrWaveUuidVal = { 0x65766177u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrFmtUuidVal = { 0x20746D66u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrDataUuidVal = { 0x61746164u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };

#endif // WAVE_H
