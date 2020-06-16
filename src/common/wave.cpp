#include "wave.h"
#include "p2p.h"
#include <cstring>
#include <cassert>

static const uint8_t waveFormatExtensible[2] = { 0xFE, 0xFF };
static const uint8_t ksDataformatSubtypePCM[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
static const uint8_t ksDataformatSubtypeIEEEFloat[16] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

static_assert(sizeof(WaveHeader) == 68, "");
static_assert(offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wValidBitsPerSample) == 22, "");
static const uint8_t waveHdrRiffTagVal[4] = { 'R', 'I', 'F', 'F' };
static const uint8_t waveHdrWaveTagVal[4] = { 'W', 'A', 'V', 'E' };
static const uint8_t waveHdrFmtTagVal[4] = { 'f', 'm', 't', ' ' };
static const uint8_t waveHdrDataTagVal[4] = { 'd', 'a', 't', 'a' };

static_assert(sizeof(Wave64Header) == 128, "");
static_assert(offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, wValidBitsPerSample) == 22, "");
static const uint8_t wave64HdrRiffUuidVal[16] = { 0x72, 0x69, 0x66, 0x66, 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 };
static const uint8_t wave64HdrWaveUuidVal[16] = { 0x77, 0x61, 0x76, 0x65, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };
static const uint8_t wave64HdrFmtUuidVal[16] = { 0x66, 0x6D, 0x74, 0x20, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };
static const uint8_t wave64HdrDataUuidVal[16] = { 0x64, 0x61, 0x74, 0x61, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

void PackChannels32to24(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const int32_t *const *const S = reinterpret_cast<const int32_t *const *const>(Src);
    p2p::detail::uint24 *D = reinterpret_cast<p2p::detail::uint24 *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[c] = p2p::detail::uint24(S[c][i] >> 8);
        D += Channels;
    }
}

Wave64Header CreateWave64Header(bool IsFloat, int BitsPerSample, int SampleRate, int NumChannels, uint64_t channelMask, int64_t NumSamples) {
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;
    uint64_t dataSize = NumChannels * static_cast<uint64_t>(bytesPerOutputSample) * NumSamples;

    Wave64Header header = {};
    memcpy(&header.riffUuid, wave64HdrRiffUuidVal, sizeof(wave64HdrRiffUuidVal));
    header.riffSize = sizeof(header) + dataSize;
    memcpy(&header.waveUuid, wave64HdrWaveUuidVal, sizeof(wave64HdrWaveUuidVal));
    memcpy(&header.fmtUuid, wave64HdrFmtUuidVal, sizeof(wave64HdrFmtUuidVal));
    header.fmtSize = offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, fmtUuid);
    memcpy(&header.wFormatTag, waveFormatExtensible, sizeof(waveFormatExtensible));
    header.nChannels = NumChannels;
    header.nSamplesPerSec = SampleRate;
    header.nBlockAlign = static_cast<uint16_t>(NumChannels * bytesPerOutputSample);
    header.nAvgBytesPerSec = static_cast<uint32_t>(NumChannels * bytesPerOutputSample * SampleRate);
    header.wBitsPerSample = static_cast<uint16_t>(bytesPerOutputSample * 8);
    header.cbSize = offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, wValidBitsPerSample);
    header.wValidBitsPerSample = BitsPerSample;
    header.dwChannelMask = static_cast<uint32_t>(channelMask);
    memcpy(&header.SubFormat, IsFloat ? ksDataformatSubtypeIEEEFloat : ksDataformatSubtypePCM, sizeof(ksDataformatSubtypePCM));
    memcpy(&header.dataUuid, wave64HdrDataUuidVal, sizeof(wave64HdrDataUuidVal));
    header.dataSize = dataSize + sizeof(header.dataUuid) + sizeof(header.dataSize);
    return header;
}

WaveHeader CreateWaveHeader(bool IsFloat, int BitsPerSample, int SampleRate, int NumChannels, uint64_t channelMask, int64_t NumSamples, bool &valid) {
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;
    uint64_t dataSize = NumChannels * static_cast<uint64_t>(bytesPerOutputSample) * NumSamples;

    WaveHeader header = {};
    memcpy(&header.riffTag, waveHdrRiffTagVal, sizeof(waveHdrRiffTagVal));
    header.riffSize = static_cast<uint32_t>(sizeof(header) - sizeof(header.riffTag) - sizeof(header.riffSize) + dataSize);
    memcpy(&header.waveTag, waveHdrWaveTagVal, sizeof(waveHdrWaveTagVal));
    memcpy(&header.fmtTag, waveHdrFmtTagVal, sizeof(waveHdrFmtTagVal));
    header.fmtSize = offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wFormatTag);
    memcpy(&header.wFormatTag, waveFormatExtensible, sizeof(waveFormatExtensible));
    header.nChannels = NumChannels;
    header.nSamplesPerSec = SampleRate;
    header.nBlockAlign = static_cast<uint16_t>(NumChannels * bytesPerOutputSample);
    header.nAvgBytesPerSec = static_cast<uint32_t>(NumChannels * bytesPerOutputSample * SampleRate);
    header.wBitsPerSample = static_cast<uint16_t>(bytesPerOutputSample * 8);
    header.cbSize = offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wValidBitsPerSample);
    header.wValidBitsPerSample = BitsPerSample;
    header.dwChannelMask = static_cast<uint32_t>(channelMask);
    memcpy(&header.SubFormat, IsFloat ? ksDataformatSubtypeIEEEFloat : ksDataformatSubtypePCM, sizeof(ksDataformatSubtypePCM));
    memcpy(&header.dataTag, waveHdrDataTagVal, sizeof(waveHdrDataTagVal));
    header.dataSize = unsigned(dataSize);
    valid = (sizeof(header) + dataSize <= maxWaveFileSize);
    return header;
}
