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

struct AvfsWavFile final:
   AvfsMediaFile_
{
  int references = 1;
  Synther_* avs = nullptr;
  uint64_t startSample;
  uint64_t sampleCount;
  union {
    WaveHeader wave;
    Wave64Header wave64;
  } hdr;
  uint16_t sampleBlockSize;
  size_t hdrSize;
  uint64_t dataSize;

  AvfsWavFile(
    Synther_* avs,
    bool isFloat,
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
  Synther_ *inAvs,
  bool isFloat,
  uint16_t inSampleBlockSize,
  uint64_t totalSampleCount,
  uint64_t inStartSample,
  uint64_t maxFileSize,
  bool forceWave64)
{
  avs = inAvs;
  avs->AddRef();
  startSample = inStartSample;
  sampleCount = totalSampleCount - inStartSample;
  sampleBlockSize = inSampleBlockSize;

  const VideoInfoAdapter &vi = avs->GetVideoInfo();

  uint64_t maxSampleCount = (maxFileSize-sizeof(hdr.wave)) / sampleBlockSize;
  bool wave64 = forceWave64;
  if (maxFileSize > maxWaveFileSize || wave64) {
     maxSampleCount = (maxFileSize - sizeof(hdr.wave64)) / sampleBlockSize;
  }
  if (sampleCount > maxSampleCount) {
     sampleCount = maxSampleCount;
  }
  dataSize = sampleCount * sampleBlockSize;
  if (sizeof(hdr.wave) + dataSize > maxWaveFileSize)
    wave64 = true;

  // Initialize file header.
  if (wave64) {
    hdr.wave64 = CreateWave64Header(isFloat, vi.BitsPerChannelSample(), vi.SamplesPerSecond(), vi.AudioChannels(), sampleCount);
    hdrSize = sizeof(hdr.wave64);
  } else {
    // Use normal wave header for files less than 2GB.
    hdr.wave = CreateWaveHeader(isFloat, vi.BitsPerChannelSample(), vi.SamplesPerSecond(), vi.AudioChannels(), sampleCount);
    hdrSize = sizeof(hdr.wave);
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
  Synther_* avs,
  AvfsVolume_* volume)
{
  ASSERT(log && avs && volume);
  AvfsWavFile* file;
  const VideoInfoAdapter &vi = avs->GetVideoInfo();
  size_t bytesPerOutputSample = (vi.BitsPerChannelSample() + 7) / 8;
  uint16_t sampleBlockSize = uint16_t(bytesPerOutputSample * vi.AudioChannels());
  uint64_t sampleCount = static_cast<uint64_t>(vi.num_audio_samples);
  uint64_t position = 0;
  uint64_t endPosition;
  unsigned fileNumber = 0;
  static const size_t maxFileNameChars = 300;
  wchar_t fileName[maxFileNameChars];

  if (!vi.HasAudio()) {
    log->Printf(L"AvfsWavMediaInit: Clip has no audio.\r\n");
  } else if (sampleBlockSize > waveMaxSampleBlockSize) {
    log->Printf(
      L"AvfsWavMediaInit: Unsupported BitsPerChannelSample(%i)"
      L"or SampleType(%i).\n",sampleBlockSize, vi.BitsPerChannelSample());
  } else {
    // Create single wave file containing up to 4GB of audio data.
    file = new(std::nothrow) AvfsWavFile(avs, vi.AudioIsFloat(),sampleBlockSize,
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
    file = new(std::nothrow) AvfsWavFile(avs, vi.AudioIsFloat(),sampleBlockSize,
      sampleCount,0,UINT64_MAX,true/*forceWave64*/);
    if (file) {
      ssformat(fileName,maxFileNameChars,L"%s.w64",volume->GetMediaName());
      volume->CreateMediaFile(file,fileName,file->hdrSize+file->dataSize);
      file->Release();
    }

    // Create sequence of max compatible wave files containing all audio data.
    while (position < sampleCount) {
      file = new(std::nothrow) AvfsWavFile(avs, vi.AudioIsFloat(),sampleBlockSize,
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
