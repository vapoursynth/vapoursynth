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
#include <array>

typedef std::array<uint8_t, 16> wave_uuid_t;
typedef std::array<uint8_t, 4> wave_tag_t;
typedef std::array<uint8_t, 2> wave_fmt_tag_t;

static constexpr uint64_t maxWaveFileSize = 0xFFFFFFFE;
static constexpr uint64_t maxCompatWaveFileSize = 0x7FFFFFFE;

struct WaveFormatExtensible {
    wave_fmt_tag_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
    uint16_t wValidBitsPerSample;
    uint32_t dwChannelMask;
    wave_uuid_t SubFormat;
};

struct WaveHeader {
    wave_tag_t riffTag;
    uint32_t riffSize;
    wave_tag_t waveTag;
    wave_tag_t fmtTag;
    uint32_t fmtSize;
    WaveFormatExtensible wfx;
    wave_tag_t dataTag;
    uint32_t dataSize;
};

struct Wave64Header {
    wave_uuid_t riffUuid;
    uint64_t riffSize;
    wave_uuid_t waveUuid;
    wave_uuid_t fmtUuid;
    uint64_t fmtSize;
    WaveFormatExtensible wfx;
    wave_uuid_t dataUuid;
    uint64_t dataSize;
};

void PackChannels16to16le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);
void PackChannels32to32le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);
void PackChannels32to24le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);

bool CreateWaveFormatExtensible(WaveFormatExtensible &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask);
bool CreateWave64Header(Wave64Header &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples);
bool CreateWaveHeader(WaveHeader &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples);

#endif // WAVE_H