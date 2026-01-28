from libc.stdint cimport uint8_t, uint64_t, int64_t

cdef extern from "src/common/wave.h":
    const uint64_t maxWaveFileSize

    ctypedef struct WaveFormatExtensible:
        pass

    ctypedef struct WaveHeader:
        pass

    ctypedef struct Wave64Header:
        pass

    void PackChannels16to16le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) nogil
    void PackChannels32to32le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) nogil
    void PackChannels32to24le(const uint8_t *const *const Src, uint8_t *Dst, size_t Length, size_t Channels) nogil

    bint CreateWave64Header(Wave64Header &header, bint IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples) nogil
    bint CreateWaveHeader(WaveHeader &header, bint IsFloat, int BitsPerSample, int SampleRate, uint64_t ChannelMask, int64_t NumSamples) nogil
