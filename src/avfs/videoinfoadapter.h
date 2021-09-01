// videoinfoadapter.h : Avisynth Virtual File System
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
#ifndef VIDEOINFOADAPTER_H
#define VIDEOINFOADAPTER_H


struct VideoInfoAdapter {
private:
    const VSVideoInfo *vsvi = nullptr;
    const VSAudioInfo *vsai = nullptr;
    const avs::VideoInfo *avsvi = nullptr;
public:
    VSVideoFormat vf = {};
    int num_frames;
    uint32_t fps_numerator;
    uint32_t fps_denominator;
    int width;
    int height;
    int64_t num_audio_samples;
    int alt_output;


    Avisynther_ *avssynther = nullptr;
    VapourSynther_ *vssynther = nullptr;

    VideoInfoAdapter(const VSVideoInfo *vi, const VSAudioInfo *ai, VapourSynther_ *vssynther, int alt_output) : vsvi(vi), vsai(ai), alt_output(alt_output), vssynther(vssynther) {
        num_frames = vi ? vi->numFrames : 0;
        fps_numerator = static_cast<uint32_t>(vi ? vi->fpsNum : 0);
        fps_denominator = static_cast<uint32_t>(vi ? vi->fpsDen : 0);
        width = vi->width;
        height = vi->height;
        num_audio_samples = ai ? ai->numSamples : 0;
        vf = vi->format;
    };

    VideoInfoAdapter(const avs::VideoInfo *vi, Avisynther_ *avssynther, int alt_output) : avsvi(vi), alt_output(alt_output), avssynther(avssynther) {
        num_frames = vi->num_frames;
        fps_numerator = vi->fps_numerator;
        fps_denominator = vi->fps_denominator;
        width = vi->width;
        height = vi->height;
        num_audio_samples = vi->num_audio_samples;

        // Minimum supported version is AVS 2.6. Only required interleaved format is BGR32.
        if (!vi->IsPlanar() && !vi->IsRGB32()) {
            vf.colorFamily = ::cfUndefined;
            return;
        }

        int subsampling_w = 0;
        int subsampling_h = 0;

        if (vi->IsYUV() && vi->IsPlanar()) {
            subsampling_w = vi->GetPlaneWidthSubsampling(avs::PLANAR_U);
            subsampling_h = vi->GetPlaneHeightSubsampling(avs::PLANAR_U);
        }

        bool hasSubSampling = vi->IsYUV();
        vf.colorFamily = vi->IsYUV() ? cfYUV : (vi->IsRGB() ? cfRGB : (vi->IsY() ? cfGray : cfUndefined));
        if (vf.colorFamily != cfUndefined) {
            vf.sampleType = vi->BitsPerComponent() == 32 ? stFloat : stInteger;
            vf.bitsPerSample = vi->BitsPerComponent();
            if (vf.bitsPerSample <= 8)
                vf.bytesPerSample = 1;
            else if (vf.bitsPerSample <= 16)
                vf.bytesPerSample = 2;
            else
                vf.bytesPerSample = 4;
            vf.subSamplingW = hasSubSampling ? vi->GetPlaneWidthSubsampling(avs::PLANAR_U) : 0;
            vf.subSamplingH = hasSubSampling ? vi->GetPlaneHeightSubsampling(avs::PLANAR_U) : 0;
            vf.numPlanes = (vi->IsPlanar() && (vi->IsYUV() || vi->IsRGB())) ? 3 : 1;
        }
    };

    bool HasAudio() const {
        return vsai || (avsvi && avsvi->HasAudio());
    }

    bool HasVideo() const {
        return vsvi || (avsvi && avsvi->HasVideo());
    }

    int AudioChannels() const {
        if (vsai) {
            return vsai->format.numChannels;
        } else {
            return (avsvi && avsvi->AudioChannels() <= 8) ? avsvi->AudioChannels() : 0;
        }
    }

    uint64_t ChannelLayout() const {
        if (vsai) {
            return vsai->format.channelLayout;
        } else {
            if (avsvi && avsvi->AudioChannels() <= 8) {
                const uint64_t guessedLayout[9] = { 0x0000, 0x0004, 0x0003, 0x0007, 0x0033, 0x0037, 0x003F, 0x013F, 0x063F };
                return guessedLayout[avsvi->AudioChannels()];
            }
            return 0;
        }
    }

    int SamplesPerSecond() const {
        if (vsai)
            return vsai->sampleRate;
        else
            return avsvi ? avsvi->SamplesPerSecond() : 0;
    }

    int BytesPerChannelSample() const {
        if (vsai)
            return vsai->format.bytesPerSample;
        else
            return avsvi ? avsvi->BytesPerChannelSample() : 0;
    }

    int64_t AudioSamplesFromFrames(int frames) const {
        if (vsvi && vsai && vsvi->fpsNum > 0)
            return static_cast<int64_t>(frames) * vsai->sampleRate * vsvi->fpsDen / vsvi->fpsNum;
        else if (avsvi)
            return avsvi->AudioSamplesFromFrames(frames);
        else
            return 0;
    }

    int FramesFromAudioSamples(int64_t samples) const {
        if (vsvi && vsai && vsvi->fpsDen > 0)
            return static_cast<int>((samples * vsvi->fpsNum) / (static_cast<int64_t>(fps_denominator) * vsai->sampleRate));
        else if (avsvi)
            return avsvi->FramesFromAudioSamples(samples);
        else
            return 0;
    }

    int BytesPerAudioSample() const {
        if (vsai)
            return vsai->format.bytesPerSample * vsai->format.numChannels;
        else
            return avsvi ? avsvi->BytesPerAudioSample() : 0;
    }

    int BitsPerChannelSample() const {
        if (vsai)
            return vsai->format.bitsPerSample;
        else
            return avsvi ? (avsvi->BytesPerChannelSample() * 8) : 0;
    }

    int BMPSize() const {
        return vsvi ? vssynther->BMPSize() : avssynther->BMPSize();
    }

    int BitsPerPixel() const {
        return vsvi ? vssynther->BitsPerPixel() : avssynther->BitsPerPixel();
    }

    bool AudioIsFloat() const {
        if (avsvi && avsvi->sample_type == avs::SAMPLE_FLOAT)
            return true;
        if (vsai && vsai->format.sampleType == stFloat)
            return true;
        return false;
    }
};

#endif
