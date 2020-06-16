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
    uint16_t cbSize;
    uint16_t wValidBitsPerSample;
    uint32_t dwChannelMask;
    uuid_t  SubFormat;
    uint32_t dataTag;
    uint32_t dataSize;
};

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
    uint16_t cbSize;
    uint16_t wValidBitsPerSample;
    uint32_t dwChannelMask;
    uuid_t  SubFormat;
    uuid_t dataUuid;
    uint64_t dataSize;
};

template<typename T>
static void PackChannels(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const T *const *const S = reinterpret_cast<const T *const *const>(Src);
    T *D = reinterpret_cast<T *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[c] = S[c][i];
        D += Channels;
    }
}

void PackChannels32to24(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels);
Wave64Header CreateWave64Header(bool IsFloat, int BitsPerSample, int SampleRate, int NumChannels, uint64_t channelMask, int64_t NumSamples);
WaveHeader CreateWaveHeader(bool IsFloat, int BitsPerSample, int SampleRate, int NumChannels, uint64_t channelMask, int64_t NumSamples, bool &valid);

#endif // WAVE_H
