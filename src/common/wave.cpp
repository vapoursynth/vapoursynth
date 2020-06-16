#include "wave.h"
#include "p2p.h"

// FIXME, definitely only works right on little endian

#define MAKE_RIFF_TAG(c0,c1,c2,c3) static_cast<uint32_t>((c0)|((c1)<<8)|((c2)<<16)|((c3)<<24))

static const uint16_t waveFormatExtensible = 0xFFFE;
static const uuid_t ksDataformatSubtypePCM = { 0x00000001u,0x0000u,0x0010u,{0x80u,0x00u,0x00u,0xAAu,0x00u,0x38u,0x9Bu,0x71u} };
static const uuid_t ksDataformatSubtypeIEEEFloat = { 0x00000003u,0x0000u,0x0010u,{0x80u,0x00u,0x00u,0xAAu,0x00u,0x38u,0x9Bu,0x71u} };

static_assert(sizeof(WaveHeader) == 68, "");
static_assert(offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wValidBitsPerSample) == 22, "");
static const uint32_t waveHdrRiffTagVal = MAKE_RIFF_TAG('R', 'I', 'F', 'F');
static const uint32_t waveHdrWaveTagVal = MAKE_RIFF_TAG('W', 'A', 'V', 'E');
static const uint32_t waveHdrFmtTagVal = MAKE_RIFF_TAG('f', 'm', 't', ' ');
static const uint32_t waveHdrDataTagVal = MAKE_RIFF_TAG('d', 'a', 't', 'a');

static_assert(sizeof(Wave64Header) == 128, "");
static_assert(offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, wValidBitsPerSample) == 22, "");
static const uuid_t wave64HdrRiffUuidVal = { 0x66666972u,0x912Eu,0x11CFu,{0xA5u,0xD6u,0x28u,0xDBu,0x04u,0xC1u,0x00u,0x00u} };
static const uuid_t wave64HdrWaveUuidVal = { 0x65766177u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrFmtUuidVal = { 0x20746D66u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrDataUuidVal = { 0x61746164u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };

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
    header.riffUuid = wave64HdrRiffUuidVal;
    header.riffSize = sizeof(header) + dataSize;
    header.waveUuid = wave64HdrWaveUuidVal;
    header.fmtUuid = wave64HdrFmtUuidVal;
    header.fmtSize = offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, fmtUuid);
    header.wFormatTag = waveFormatExtensible;
    header.nChannels = NumChannels;
    header.nSamplesPerSec = SampleRate;
    header.nBlockAlign = static_cast<uint16_t>(NumChannels * bytesPerOutputSample);
    header.nAvgBytesPerSec = static_cast<uint32_t>(NumChannels * bytesPerOutputSample * SampleRate);
    header.wBitsPerSample = static_cast<uint16_t>(bytesPerOutputSample * 8);
    header.cbSize = offsetof(Wave64Header, dataUuid) - offsetof(Wave64Header, wValidBitsPerSample);
    header.wValidBitsPerSample = BitsPerSample;
    header.dwChannelMask = static_cast<uint32_t>(channelMask);
    header.SubFormat = IsFloat ? ksDataformatSubtypeIEEEFloat : ksDataformatSubtypePCM;
    header.dataUuid = wave64HdrDataUuidVal;
    header.dataSize = dataSize + sizeof(header.dataUuid) + sizeof(header.dataSize);
    return header;
}

WaveHeader CreateWaveHeader(bool IsFloat, int BitsPerSample, int SampleRate, int NumChannels, uint64_t channelMask, int64_t NumSamples, bool &valid) {
    size_t bytesPerOutputSample = (BitsPerSample + 7) / 8;
    uint64_t dataSize = NumChannels * static_cast<uint64_t>(bytesPerOutputSample) * NumSamples;

    WaveHeader header = {};
    header.riffTag = waveHdrRiffTagVal;
    header.riffSize = static_cast<uint32_t>(sizeof(header) - sizeof(header.riffTag) - sizeof(header.riffSize) + dataSize);
    header.waveTag = waveHdrWaveTagVal;
    header.fmtTag = waveHdrFmtTagVal;
    header.fmtSize = offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wFormatTag);
    header.wFormatTag = waveFormatExtensible;
    header.nChannels = NumChannels;
    header.nSamplesPerSec = SampleRate;
    header.nBlockAlign = static_cast<uint16_t>(NumChannels * bytesPerOutputSample);
    header.nAvgBytesPerSec = static_cast<uint32_t>(NumChannels * bytesPerOutputSample * SampleRate);
    header.wBitsPerSample = static_cast<uint16_t>(bytesPerOutputSample * 8);
    header.cbSize = offsetof(WaveHeader, dataTag) - offsetof(WaveHeader, wValidBitsPerSample);
    header.wValidBitsPerSample = BitsPerSample;
    header.dwChannelMask = static_cast<uint32_t>(channelMask);
    header.SubFormat = IsFloat ? ksDataformatSubtypeIEEEFloat : ksDataformatSubtypePCM;
    header.dataTag = waveHdrDataTagVal;
    header.dataSize = unsigned(dataSize);
    valid = (sizeof(header) + dataSize <= maxWaveFileSize);
    return header;
}
