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

#include "wave.h"
#include <cstring>
#include <bitset>

#ifdef _WIN32
#define WAVE_LITTLE_ENDIAN
#elif defined(__BYTE_ORDER__)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WAVE_BIG_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define WAVE_LITTLE_ENDIAN
#endif
#endif

#ifdef WAVE_LITTLE_ENDIAN
#define WAVE_SWAP16_LE(x) (x)
#define WAVE_SWAP32_LE(x) (x)
#define WAVE_SWAP64_LE(x) (x)
#endif // WAVE_LITTLE_ENDIAN

#ifdef WAVE_BIG_ENDIAN
#define WAVE_SWAP16_LE(x) __builtin_bswap16(x)
#define WAVE_SWAP32_LE(x) __builtin_bswap32(x)
#define WAVE_SWAP64_LE(x) __builtin_bswap64(x)
#endif // WAVE_BIG_ENDIAN

void PackChannels16to16le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const uint16_t *const *const S = reinterpret_cast<const uint16_t *const *>(Src);
    uint16_t *D = reinterpret_cast<uint16_t *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[c] = WAVE_SWAP16_LE(S[c][i]);
        D += Channels;
    }
}

void PackChannels32to32le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const uint32_t *const *const S = reinterpret_cast<const uint32_t *const *>(Src);
    uint32_t *D = reinterpret_cast<uint32_t *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[c] = WAVE_SWAP32_LE(S[c][i]);
        D += Channels;
    }
}

void PackChannels32to24le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++) {
#ifdef WAVE_LITTLE_ENDIAN
            memcpy(Dst + c * 3, Src[c] + i * 4 + 1, 3);
#else
            Dst[c * 3 + 0] = Src[c][i * 4 + 2];
            Dst[c * 3 + 1] = Src[c][i * 4 + 1];
            Dst[c * 3 + 2] = Src[c][i * 4 + 0];
#endif
        }
        Dst += Channels * 3;
    }
}

static_assert(sizeof(WaveFormatExtensible) - offsetof(WaveFormatExtensible, wValidBitsPerSample) == 22, "");
static const uint8_t waveFormatExtensible[2] = { 0xFE, 0xFF };
static const uint8_t ksDataformatSubtypePCM[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
static const uint8_t ksDataformatSubtypeIEEEFloat[16] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

static_assert(sizeof(WaveHeader) == 68, "");
static const uint8_t waveHdrRiffTagVal[4] = { 'R', 'I', 'F', 'F' };
static const uint8_t waveHdrWaveTagVal[4] = { 'W', 'A', 'V', 'E' };
static const uint8_t waveHdrFmtTagVal[4] = { 'f', 'm', 't', ' ' };
static const uint8_t waveHdrDataTagVal[4] = { 'd', 'a', 't', 'a' };

static_assert(sizeof(Wave64Header) == 128, "");
static const uint8_t wave64HdrRiffUuidVal[16] = { 0x72, 0x69, 0x66, 0x66, 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 };
static const uint8_t wave64HdrWaveUuidVal[16] = { 0x77, 0x61, 0x76, 0x65, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };
static const uint8_t wave64HdrFmtUuidVal[16] = { 0x66, 0x6D, 0x74, 0x20, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };
static const uint8_t wave64HdrDataUuidVal[16] = { 0x64, 0x61, 0x74, 0x61, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };



bool CreateWaveFormatExtensible(WaveFormatExtensible &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask) {
    header = {};
    if (ChannelMask != static_cast<uint32_t>(ChannelMask))
        return false;
    std::bitset<64> tmp(ChannelMask);
    size_t NumChannels = tmp.count();
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;

    memcpy(&header.wFormatTag, waveFormatExtensible, sizeof(waveFormatExtensible));
    header.nChannels = WAVE_SWAP16_LE(static_cast<uint16_t>(NumChannels));
    header.nSamplesPerSec = WAVE_SWAP32_LE(SampleRate);
    header.nBlockAlign = WAVE_SWAP16_LE(static_cast<uint16_t>(NumChannels * bytesPerOutputSample));
    header.nAvgBytesPerSec = WAVE_SWAP32_LE(static_cast<uint32_t>(NumChannels * bytesPerOutputSample * SampleRate));
    header.wBitsPerSample = WAVE_SWAP16_LE(static_cast<uint16_t>(bytesPerOutputSample * 8));
    header.cbSize = WAVE_SWAP16_LE(sizeof(WaveFormatExtensible) - offsetof(WaveFormatExtensible, wValidBitsPerSample));
    header.wValidBitsPerSample = WAVE_SWAP16_LE(BitsPerSample);
    header.dwChannelMask = WAVE_SWAP32_LE(static_cast<uint32_t>(ChannelMask));
    memcpy(&header.SubFormat, IsFloat ? ksDataformatSubtypeIEEEFloat : ksDataformatSubtypePCM, sizeof(ksDataformatSubtypePCM));
    return true;
}

bool CreateWave64Header(Wave64Header &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples) {
    header = {};
    std::bitset<64> tmp(ChannelMask);
    size_t NumChannels = tmp.count();
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;
    uint64_t dataSize = NumChannels * static_cast<uint64_t>(bytesPerOutputSample) * NumSamples;

    if (!CreateWaveFormatExtensible(header.wfx, IsFloat, BitsPerSample, SampleRate, ChannelMask))
        return false;

    memcpy(&header.riffUuid, wave64HdrRiffUuidVal, sizeof(wave64HdrRiffUuidVal));
    header.riffSize = WAVE_SWAP64_LE(sizeof(header) + dataSize);
    memcpy(&header.waveUuid, wave64HdrWaveUuidVal, sizeof(wave64HdrWaveUuidVal));
    memcpy(&header.fmtUuid, wave64HdrFmtUuidVal, sizeof(wave64HdrFmtUuidVal));
    header.fmtSize = WAVE_SWAP64_LE(sizeof(WaveFormatExtensible) + sizeof(header.fmtUuid) + sizeof(header.fmtSize));
    memcpy(&header.dataUuid, wave64HdrDataUuidVal, sizeof(wave64HdrDataUuidVal));
    header.dataSize = WAVE_SWAP64_LE(dataSize + sizeof(header.dataUuid) + sizeof(header.dataSize));
    return true;
}

bool CreateWaveHeader(WaveHeader &header, bool IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples) {
    header = {};
    std::bitset<64> tmp(ChannelMask);
    size_t NumChannels = tmp.count();
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;
    uint64_t dataSize = NumChannels * static_cast<uint64_t>(bytesPerOutputSample) * NumSamples;

    if (sizeof(header) + dataSize > maxWaveFileSize)
        return false;

    if (!CreateWaveFormatExtensible(header.wfx, IsFloat, BitsPerSample, SampleRate, ChannelMask))
        return false;

    memcpy(&header.riffTag, waveHdrRiffTagVal, sizeof(waveHdrRiffTagVal));
    header.riffSize = WAVE_SWAP32_LE(static_cast<uint32_t>(sizeof(header) - sizeof(header.riffTag) - sizeof(header.riffSize) + dataSize));
    memcpy(&header.waveTag, waveHdrWaveTagVal, sizeof(waveHdrWaveTagVal));
    memcpy(&header.fmtTag, waveHdrFmtTagVal, sizeof(waveHdrFmtTagVal));
    header.fmtSize = WAVE_SWAP32_LE(sizeof(WaveFormatExtensible));
    memcpy(&header.dataTag, waveHdrDataTagVal, sizeof(waveHdrDataTagVal));
    header.dataSize = WAVE_SWAP32_LE(static_cast<uint32_t>(dataSize));
    return true;
}
