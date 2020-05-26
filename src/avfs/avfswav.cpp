// avfswav.cpp : Avisynth Virtual File System
//
// Avisynth v2.5.  Copyright 2008 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.


#include "avfsincludes.h"

#if !defined(UUID_T_DEFINED) && !defined(uuid_t)
struct uuid_t {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};
#endif

static const unsigned waveMaxSampleBlockSize = 128;
static const uint64_t maxWaveFileSize       = 0xFFFFFFFE;
static const uint64_t maxCompatWaveFileSize = 0x7FFFFFFE;

struct waveHdr {
  uint32_t riffTag;
  uint32_t riffSize; // = sizeof(waveHdr)-offsetofend(riffSize)+dataSize
  uint32_t waveTag;
  uint32_t fmtTag;
  uint32_t fmtSize; // = offsetof(dataTag)-offsetofend(fmtSize)
  uint16_t wFormatTag;         // sample type
  uint16_t nChannels;          // number of channels (i.e. mono, stereo...)
  uint32_t nSamplesPerSec;     // sample rate
  uint32_t nAvgBytesPerSec;    // for buffer estimation
  uint16_t nBlockAlign;        // block size of data
  uint16_t wBitsPerSample;     // number of bits per sample of mono data
  // uint16_t cbSize;
  // uint8_t fmtExtra[cbSize];
  uint32_t dataTag;
  uint32_t dataSize;
  // uint8_t data[dataSize];
};                             /* Data Samples */
static_assert(sizeof(waveHdr) == 44, "");
static const uint32_t waveHdrRiffTagVal = MAKETAGUINT32('R','I','F','F');
static const uint32_t waveHdrWaveTagVal = MAKETAGUINT32('W','A','V','E');
static const uint32_t waveHdrFmtTagVal  = MAKETAGUINT32('f','m','t',' ');
static const uint32_t waveHdrDataTagVal = MAKETAGUINT32('d','a','t','a');

struct wave64Hdr {
  uuid_t riffUuid;
  uint64_t riffSize; // = sizeof(wave64Hdr)+dataSize
  uuid_t waveUuid;
  uuid_t fmtUuid;
  uint64_t fmtSize; // = offsetof(dataUuid)-offsetof(fmtUuid)
  uint16_t wFormatTag;         // sample type
  uint16_t nChannels;          // number of channels (i.e. mono, stereo...)
  uint32_t nSamplesPerSec;     // sample rate
  uint32_t nAvgBytesPerSec;    // for buffer estimation
  uint16_t nBlockAlign;        // block size of data
  uint16_t wBitsPerSample;     // number of bits per sample of mono data
  // uint16_t cbSize;
  // uint8_t fmtExtra[cbSize];
  uuid_t dataUuid;
  uint64_t dataSize;
  // uint8_t data[dataSize];
};
static_assert(sizeof(wave64Hdr) == 104, "");
static const uuid_t wave64HdrRiffUuidVal = { 0x66666972u,0x912Eu,0x11CFu,{0xA5u,0xD6u,0x28u,0xDBu,0x04u,0xC1u,0x00u,0x00u} };
static const uuid_t wave64HdrWaveUuidVal = { 0x65766177u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrFmtUuidVal  = { 0x20746D66u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };
static const uuid_t wave64HdrDataUuidVal = { 0x61746164u,0xACF3u,0x11D3u,{0x8Cu,0xD1u,0x00u,0xC0u,0x4Fu,0x8Eu,0xDBu,0x8Au} };

struct AvfsWavFile final:
   AvfsMediaFile_
{
  int references;
  Avisynther_* avs;
  uint64_t startSample;
  uint64_t sampleCount;
  union {
    waveHdr wave;
    wave64Hdr wave64;
  } hdr;
  uint16_t sampleBlockSize;
  size_t hdrSize;
  uint64_t dataSize;

  AvfsWavFile(
    Avisynther_* avs,
    uint16_t sampleType,
    uint16_t sampleBlockSize,
    uint64_t sampleCount,
    uint64_t startSample,
    uint64_t maxFileSize,
    bool forceWave64);
  ~AvfsWavFile(void);
  void AddRef(void);
  void Release(void);
  bool/*success*/ ReadMedia(
    AvfsLog_* log,
    uint64_t fileOffset,
    void* buffer,
    size_t requestedSize);
};

AvfsWavFile::AvfsWavFile(
  Avisynther_* inAvs,
  uint16_t sampleType,
  uint16_t inSampleBlockSize,
  uint64_t totalSampleCount,
  uint64_t inStartSample,
  uint64_t maxFileSize,
  bool forceWave64)
{
  references = 1;
  avs = inAvs;
  avs->AddRef();
  sampleBlockSize = inSampleBlockSize;
  startSample = inStartSample;
  sampleCount = totalSampleCount-startSample;
  uint16_t channelCount = uint16_t(avs->GetVideoInfo().AudioChannels());
  uint16_t sampleSize = uint16_t(avs->GetVideoInfo().BytesPerChannelSample());
  unsigned samplesPerSec = avs->GetVideoInfo().SamplesPerSecond();
  uint16_t sampleBitCount = uint16_t(sampleSize*8);
  unsigned bytesPerSec = samplesPerSec*sampleBlockSize;
  uint64_t maxSampleCount = (maxFileSize-sizeof(hdr.wave))/sampleBlockSize;
  bool wave64 = forceWave64;
  if (maxFileSize > maxWaveFileSize || wave64) {
     maxSampleCount = (maxFileSize-sizeof(hdr.wave64))/sampleBlockSize;
  }
  if (sampleCount > maxSampleCount) {
     sampleCount = maxSampleCount;
  }
  dataSize = sampleCount*sampleBlockSize;
  if(sizeof(hdr.wave)+dataSize > maxWaveFileSize) {
    wave64 = true;
  }
  memset(&hdr,0,sizeof(hdr));

  // Initialize file header.
  if (wave64) {
    // Use wave64 header for files larger than 2GB.
    hdrSize = sizeof(hdr.wave64);
    hdr.wave64.riffUuid = wave64HdrRiffUuidVal;
    hdr.wave64.riffSize = hdrSize+dataSize;
    hdr.wave64.waveUuid = wave64HdrWaveUuidVal;
    hdr.wave64.fmtUuid = wave64HdrFmtUuidVal;
    hdr.wave64.fmtSize = offsetof(wave64Hdr,dataUuid)-offsetof(wave64Hdr,fmtUuid);
    hdr.wave64.wFormatTag = sampleType;
    hdr.wave64.nChannels = channelCount;
    hdr.wave64.nSamplesPerSec = samplesPerSec;
    hdr.wave64.nBlockAlign = sampleBlockSize;
    hdr.wave64.nAvgBytesPerSec = bytesPerSec;
    hdr.wave64.wBitsPerSample = sampleBitCount;
    hdr.wave64.dataUuid = wave64HdrDataUuidVal;
    hdr.wave64.dataSize = dataSize + sizeof(hdr.wave64.dataUuid) + sizeof(hdr.wave64.dataSize);;
  }
  else {
    // Use normal wave header for files less than 2GB.
    hdrSize = sizeof(hdr.wave);
    hdr.wave.riffTag = waveHdrRiffTagVal;
    hdr.wave.riffSize = unsigned(hdrSize-offsetofend(waveHdr,riffSize)+dataSize);
    hdr.wave.waveTag = waveHdrWaveTagVal;
    hdr.wave.fmtTag = waveHdrFmtTagVal;
    hdr.wave.fmtSize = offsetof(waveHdr,dataTag)-offsetofend(waveHdr,fmtSize);
    hdr.wave.wFormatTag = sampleType;
    hdr.wave.nChannels = channelCount;
    hdr.wave.nSamplesPerSec = samplesPerSec;
    hdr.wave.nBlockAlign = sampleBlockSize;
    hdr.wave.nAvgBytesPerSec = bytesPerSec;
    hdr.wave.wBitsPerSample = sampleBitCount;
    hdr.wave.dataTag = waveHdrDataTagVal;
    hdr.wave.dataSize = unsigned(dataSize);
  }
}

AvfsWavFile::~AvfsWavFile(void)
{
  ASSERT(!references);
  avs->Release();
}

void AvfsWavFile::AddRef(void)
{
  ASSERT(references);
  references ++;
}

void AvfsWavFile::Release(void)
{
  ASSERT(references);
  if(!--references)
  {
    delete this;
  }
}

bool/*success*/ AvfsWavFile::ReadMedia(
  AvfsLog_* log,
  uint64_t inFileOffset,
  void* inBuffer,
  size_t inRequestedSize)
{
  // Avfspfm.cpp logic makes some guarantees.
  ASSERT(inRequestedSize);
  ASSERT(inFileOffset < hdrSize+dataSize);
  ASSERT(inFileOffset+inRequestedSize <= hdrSize+dataSize);

  bool success = true;
  uint64_t offset = inFileOffset;
  size_t remainingSize = inRequestedSize;
  uint8_t* buffer = static_cast<uint8_t*>(inBuffer);
  uint8_t temp[waveMaxSampleBlockSize];
  size_t extraOffset;
  size_t partSize;

  // Copy any header data
  if (remainingSize && offset < hdrSize) {
    partSize = remainingSize;
    if (offset+partSize > hdrSize) {
      partSize = hdrSize-size_t(offset);
    }
    memcpy(buffer, reinterpret_cast<uint8_t*>(&hdr)+size_t(offset), partSize);
    buffer += partSize;
    offset += partSize;
    remainingSize -= partSize;
  }
  offset -= hdrSize;

  // Copy any data samples

  // deal with ragged front
  extraOffset = size_t(offset%sampleBlockSize);
  if (remainingSize && extraOffset) {
    partSize = remainingSize;
    if (extraOffset+partSize > sampleBlockSize) {
      partSize = sampleBlockSize-extraOffset;
    }
    success = success && avs->GetAudio(log, temp, offset/sampleBlockSize, 1);
    memcpy(buffer, temp+extraOffset, partSize);
    buffer += partSize;
    offset += partSize;
    remainingSize -= partSize;
  }

  // do sample aligned bulk of transfer
  if (remainingSize > sampleBlockSize) {
    ASSERT(offset%sampleBlockSize == 0);
    partSize = remainingSize-remainingSize%sampleBlockSize;
    success = success && avs->GetAudio(log, buffer, offset/sampleBlockSize,
      int(partSize/sampleBlockSize));
    buffer += partSize;
    offset += partSize;
    remainingSize -= partSize;
  }

  // deal with ragged tail
  if (remainingSize) {
    ASSERT(remainingSize < sampleBlockSize);
    success = success && avs->GetAudio(log, temp, offset/sampleBlockSize, 1);
    memcpy(buffer, temp, remainingSize);
  }

  return success;
}

void AvfsWavMediaInit(
  AvfsLog_* log,
  Avisynther_* avs,
  AvfsVolume_* volume)
{
  ASSERT(log && avs && volume);
  AvfsWavFile* file;
  uint16_t sampleType = 0;
  uint16_t sampleBlockSize = uint16_t(avs->GetVideoInfo().BytesPerAudioSample());
  uint64_t sampleCount = uint64_t(avs->GetVideoInfo().num_audio_samples);
  uint64_t position = 0;
  uint64_t endPosition;
  unsigned fileNumber = 0;
  static const size_t maxFileNameChars = 300;
  wchar_t fileName[maxFileNameChars];

  switch(avs->GetVideoInfo().SampleType())
  {
  case avs::SAMPLE_INT8:
  case avs::SAMPLE_INT16:
  case avs::SAMPLE_INT24:
  case avs::SAMPLE_INT32:
    sampleType = WAVE_FORMAT_PCM;
    break;
  case avs::SAMPLE_FLOAT:
    sampleType = WAVE_FORMAT_IEEE_FLOAT;
    break;
  }

  if (!avs->GetVideoInfo().HasAudio()) {
    log->Printf(L"AvfsWavMediaInit: Clip has no audio.\r\n");
  }
  else if (sampleBlockSize > waveMaxSampleBlockSize || sampleType == 0) {
    log->Printf(
      L"AvfsWavMediaInit: Unsupported BytesPerAudioSample(%u)"
      L"or SampleType(%i).\n",sampleBlockSize,avs->GetVideoInfo().SampleType());
  }
  else
  {
    // Create single wave file containing up to 4GB of audio data.
    file = new(std::nothrow) AvfsWavFile(avs,sampleType,sampleBlockSize,
      sampleCount,0,maxWaveFileSize,false/*forceWave64*/);
    if (file) {
      // Do not create the wave file if all data did not fit.
      if (file->sampleCount == sampleCount) {
        ssformat(fileName,maxFileNameChars,L"%s.wav",volume->GetMediaName());
        volume->CreateMediaFile(file,fileName,file->hdrSize+file->dataSize);
      }
      file->Release();
    }

    // Create single wave64 file containing all audio data.
    file = new(std::nothrow) AvfsWavFile(avs,sampleType,sampleBlockSize,
      sampleCount,0,UINT64_MAX,true/*forceWave64*/);
    if (file) {
      ssformat(fileName,maxFileNameChars,L"%s.w64",volume->GetMediaName());
      volume->CreateMediaFile(file,fileName,file->hdrSize+file->dataSize);
      file->Release();
    }

    // Create sequence of max compatible wave files containing all audio data.
    while (position < sampleCount) {
      file = new(std::nothrow) AvfsWavFile(avs,sampleType,sampleBlockSize,
        sampleCount,position,maxCompatWaveFileSize,false/*forceWave64*/);
      if(!file) {
        position = UINT64_MAX;
      }
      else {
        endPosition = position+file->sampleCount;
        ssformat(fileName,maxFileNameChars,L"%s.%02u.wav",
          volume->GetMediaName(),fileNumber);
        volume->CreateMediaFile(file,fileName,file->hdrSize+file->dataSize);
        file->Release();
        position = endPosition;
      }
      fileNumber ++;
    }
  }
}

void VsfsWavMediaInit(
    AvfsLog_* log,
    VapourSynther_* avs,
    AvfsVolume_* volume) {
    ASSERT(log && avs && volume);

    log->Printf(L"AvfsWavMediaInit: Clip has no audio.\r\n");
}
