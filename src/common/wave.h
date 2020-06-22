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

#include <cstdint>
#include <cstddef>

#if !defined(UUID_T_DEFINED) && !defined(uuid_t)
struct uuid_t {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};
#endif

static const uint64_t maxWaveFileSize = 0xFFFFFFFE;
static const uint64_t maxCompatWaveFileSize = 0x7FFFFFFE;

struct WaveFormatExtensible {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
    uint16_t wValidBitsPerSample;
    uint32_t dwChannelMask;
    uuid_t  SubFormat;
};

struct WaveHeader {
    uint32_t riffTag;
    uint32_t riffSize;
    uint32_t waveTag;
    uint32_t fmtTag;
    uint32_t fmtSize;
    WaveFormatExtensible wfx;
    uint32_t dataTag;
    uint32_t dataSize;
};

struct Wave64Header {
    uuid_t riffUuid;
    uint64_t riffSize;
    uuid_t waveUuid;
    uuid_t fmtUuid;
    uint64_t fmtSize;
    WaveFormatExtensible wfx;
    uuid_t dataUuid;
    uint64_t dataSize;
};

void PackChannels16to16le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);
void PackChannels32to32le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);
void PackChannels32to24le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);

bool CreateWaveFormatExtensible(WaveFormatExtensible &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask);
bool CreateWave64Header(Wave64Header &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples);
bool CreateWaveHeader(WaveHeader &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples);

#endif // WAVE_H
