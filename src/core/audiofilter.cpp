/*
* Copyright (c) 2012-2016 Fredrik Mellbin
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

#include "VSHelper.h"
#include "internalfilters.h"
#include "filtershared.h"
#include "time.h"

//////////////////////////////////////////
// Tone

#define PI 3.1415926535897932384626433832795
/**********************************************************
 *                         TONE                           *
 **********************************************************/
class SampleGenerator {
public:
    SampleGenerator() {}
    virtual float getValueAt(double where) { return 0.0f; }
};

class SineGenerator : public SampleGenerator {
public:
    SineGenerator() {}
    float getValueAt(double where) { return (float)sin(PI * where * 2.0); }
};


class NoiseGenerator : public SampleGenerator {
public:
    NoiseGenerator() {
        srand((unsigned)time(NULL));
    }

    float getValueAt(double where) { return (float)rand()*(2.0f / RAND_MAX) - 1.0f; }
};

class SquareGenerator : public SampleGenerator {
public:
    SquareGenerator() {}

    float getValueAt(double where) {
        if (where <= 0.5) {
            return 1.0f;
        }
        else {
            return -1.0f;
        }
    }
};

class TriangleGenerator : public SampleGenerator {
public:
    TriangleGenerator() {}

    float getValueAt(double where) {
        if (where <= 0.25) {
            return (float)(where*4.0);
        }
        else if (where <= 0.75) {
            return (float)((-4.0*(where - 0.50)));
        }
        else {
            return (float)((4.0*(where - 1.00)));
        }
    }
};

class SawtoothGenerator : public SampleGenerator {
public:
    SawtoothGenerator() {}

    float getValueAt(double where) {
        return (float)(2.0*(where - 0.5));
    }
};

__inline short Saturate_int16(float n) {
    if (n <= -32768.0f) return -32768;
    if (n >= 32767.0f) return  32767;
    return (short)(n + 0.5f);
}

void convertFromFloat(float* inbuf, void* outbuf, char sample_type, int count) {
    int i;

    signed short* samples = (signed short*)outbuf;
    for (i = 0; i < count; i++) {
        samples[i] = Saturate_int16(inbuf[i] * 32768.0f);
    }
}

class Tone {
    SampleGenerator *s;
    const float freq;            // Frequency in Hz
    const float samplerate;      // Samples per second
    const int ch;                 // Number of channels
    const double add_per_sample;  // How much should we add per sample in seconds
    const float level;

public:

    Tone(float _length, float _freq, int _samplerate, int _ch, float _level, const char *_type) :
        freq(_freq), samplerate(_samplerate), ch(_ch), add_per_sample(_freq / _samplerate), level(_level) {

        if (_type == NULL || !strcmp(_type, "Sine"))
            s = new SineGenerator();
        else if (!strcmp(_type, "Noise"))
            s = new NoiseGenerator();
        else if (!strcmp(_type, "Square"))
            s = new SquareGenerator();
        else if (!strcmp(_type, "Triangle"))
            s = new TriangleGenerator();
        else if (!strcmp(_type, "Sawtooth"))
            s = new SawtoothGenerator();
        else
            s = new SampleGenerator();
    }

    void __stdcall GetAudio(void* buf, __int64 start, __int64 count) {

        // Where in the cycle are we in?
        const double cycle = (freq * start) / samplerate;
        double period_place = cycle - floor(cycle);

        short* samples = (short* )buf;

        for (int i = 0; i < count; i++) {
            float v = s->getValueAt(period_place) * level;
            for (int o = 0; o < ch; o++) {
                samples[o + i * ch] = Saturate_int16(v * 32768.0f);
            }
            period_place += add_per_sample;
            if (period_place >= 1.0)
                period_place -= floor(period_place);
        }
    }
};

typedef struct AudioDubData {
    VSVideoInfo vi;
    VSNodeRef *clip1;
    VSNodeRef *clip2;
    VSVideoInfo clip1info;
    VSVideoInfo clip2info;
} AudioDubData;

typedef struct FadeInOutData {
    VSVideoInfo vi;
    VSNodeRef *clip1;
    float fade_duration;
};

typedef struct MixAudioData {
    VSVideoInfo vi;
    VSNodeRef *clip1;
    VSNodeRef *clip2;
    size_t tempbuffer_size;
    signed char *tempbuffer;
} MixAudioData;

typedef struct ToneData {
    VSVideoInfo vi;
    Tone *tone;
} ToneData;

static void VS_CC ToneGetAudio(VSCore *core, const VSAPI *vsapi, void *instanceData, void *lpBuffer, long lStart, long lSamples) {
    ToneData *d = (ToneData *)instanceData;

    d->tone->GetAudio(lpBuffer, lStart, lSamples);
}

static void VS_CC ToneInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    ToneData *d = (ToneData *)* instanceData;

    d->vi.hasAudio = true;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static void VS_CC ToneFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ToneData *d = (ToneData *)instanceData;

    delete d->tone;
}

static void VS_CC ToneCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ToneData *data = (ToneData*)malloc(sizeof(ToneData));
    int err;
    float length = 10.0;
    float frequency = 440;
    int samplerate = 48000L;
    int64_t channels = 2;
    const char *type = 0;
    float level = 1.0;

    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, &err);
    if (!err) {
        data->vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
    }

    length = vsapi->propGetFloat(in, "length", 0, &err);
    if (err) {
        length = 10.0;
    }

    samplerate = vsapi->propGetInt(in, "samplerate", 0, &err);
    if (err) {
        samplerate = 48000;
    }

    frequency = vsapi->propGetFloat(in, "frequency", 0, &err);
    if (err) {
        frequency = 440;
    }

    channels = vsapi->propGetInt(in, "channels", 0, &err);
    if (err) {
        channels = 2;
    }

    type = vsapi->propGetData(in, "type", 0, &err);
    if (err) {
        type = NULL;
    }

    level = vsapi->propGetFloat(in, "level", 0, &err);
    if (err) {
        level = 1.0;
    }

    data->vi.format = vsapi->getFormatPreset(pfAudioOnly, core);
    data->vi.numFrames = 0;

    data->vi.channels = channels;
    data->vi.audio_samplerate = samplerate;
    data->vi.numAudioSample = length * samplerate;

    Tone *tone = new Tone(length, frequency, samplerate, channels, level, type);
    data->tone = tone;

    vsapi->createFilter(in, out, "Tone", ToneInit, NULL, ToneFree, ToneGetAudio, fmParallel, nfNoCache, data, core);
}

long clamp(long n, long min, long max)
{
    n = n > max ? max : n;
    return n < min ? min : n;
}

static __int64 signed_saturated_add64(__int64 x, __int64 y) {
    // determine the lower or upper bound of the result
    __int64 ret = (x < 0) ? INT64_MIN : INT64_MAX;
    // this is always well defined:
    // if x < 0 this adds a positive value to INT64_MIN
    // if x > 0 this subtracts a positive value from INT64_MAX
    __int64 comp = ret - x;
    // the codition is equivalent to
    // ((x < 0) && (y > comp)) || ((x >=0) && (y <= comp))
    if ((x < 0) == (y > comp)) ret = x + y;
    return ret;
}

static void VS_CC MixAudioGetAudio(VSCore *core, const VSAPI *vsapi, void *instanceData, void *lpBuffer, long lStart, long lSamples) {
    MixAudioData *d = (MixAudioData *)instanceData;

    if (d->tempbuffer_size < lSamples)
    {
        if (d->tempbuffer_size)
            delete[] d->tempbuffer;

        d->tempbuffer = new signed char[(size_t)(lSamples * 4)];
        d->tempbuffer_size = (int)lSamples;
    }

    vsapi->getAudio(d->clip1, lpBuffer, lStart, lSamples);
    vsapi->getAudio(d->clip2, (void*)d->tempbuffer, lStart, lSamples);

    short* samples = (short*)lpBuffer;
    short* clip_samples = (short*)d->tempbuffer;
    for (unsigned i = 0; i < unsigned(lSamples) * 2; ++i) {
        samples[i] = (samples[i] / 2) + (clip_samples[i] / 2);
    }
}

static void VS_CC MixAudioInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    MixAudioData *d = (MixAudioData *)* instanceData;

    d->vi.hasAudio = true;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static void VS_CC MixAudioFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
}

static void VS_CC MixAudioCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    MixAudioData *data = (MixAudioData*)malloc(sizeof(MixAudioData));
    int err;

    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, &err);

    if (!err) {
        data->vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
    }

    VSNodeRef *clip1 = vsapi->propGetNode(in, "clip1", 0, &err);
    VSNodeRef *clip2 = vsapi->propGetNode(in, "clip2", 0, &err);
    VSVideoInfo clip1info = *vsapi->getVideoInfo(clip1);

    data->clip1 = clip1;
    data->clip2 = clip2;
    data->tempbuffer = NULL;
    data->tempbuffer_size = 0;

    data->vi.numFrames = 0;
    data->vi.format = vsapi->getFormatPreset(pfAudioOnly, core);
    data->vi.numAudioSample = clip1info.audio_samplerate;

    data->vi.audio_samplerate = clip1info.audio_samplerate;
    data->vi.channels = clip1info.channels;

    vsapi->createFilter(in, out, "MixAudio", MixAudioInit, NULL, MixAudioFree, MixAudioGetAudio, fmParallel, nfNoCache, data, core);
}

static void VS_CC FadeInOutInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    FadeInOutData *d = (FadeInOutData *)* instanceData;

    d->vi.hasAudio = true;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static void VS_CC FadeInOutFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
}

static void VS_CC FadeInOutGetAudio(VSCore *core, const VSAPI *vsapi, void *instanceData, void *lpBuffer, long lStart, long lSamples) {
    FadeInOutData *d = (FadeInOutData *)instanceData;
    VSVideoInfo clip1info = *vsapi->getVideoInfo(d->clip1);

    vsapi->getAudio(d->clip1, lpBuffer, lStart, lSamples);

    int totalNumberOfSamples = clip1info.numAudioSample;
    int samplesToFadeOut = d->fade_duration;
    int notCovered = totalNumberOfSamples - (lStart + lSamples);

    unsigned int j = 0;
    short* samples = (short*)lpBuffer;

    if (notCovered < samplesToFadeOut) { // Needs of fade out
        int covered = totalNumberOfSamples - (samplesToFadeOut - notCovered);
        for (unsigned int i = covered - lStart; i < lSamples; i++) {
            for (int o = 0; o < clip1info.channels; o++) {
                samples[o + i * clip1info.channels] = samples[o + i * clip1info.channels] - ((samples[o + i * clip1info.channels] / samplesToFadeOut) * (j + 1));
            }
            j++;
        }
    }
    j = 0;

    if (lStart < samplesToFadeOut) {
        int covered = samplesToFadeOut - lStart;
        if (covered > lSamples) {
            covered = lSamples;
        }
        for (unsigned int i = 0; i < covered; i++) {
            for (int o = 0; o < clip1info.channels; o++) {
                samples[o + i * clip1info.channels] = samples[o + i * clip1info.channels] - ((samples[o + i * clip1info.channels] / samplesToFadeOut) * (samplesToFadeOut - j));
            }
            j++;
        }
    }
}

static void VS_CC FadeInOutCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    FadeInOutData *data = (FadeInOutData*)malloc(sizeof(FadeInOutData));
    int err;

    VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, &err);
    data = (FadeInOutData*)malloc(sizeof(FadeInOutData));

    if (!err) {
        data->vi = *vsapi->getVideoInfo(node);
        vsapi->freeNode(node);
    }

    VSNodeRef *clip1 = vsapi->propGetNode(in, "clip1", 0, &err);
    float num_frames = vsapi->propGetFloat(in, "num_frames", 0, &err);
    VSVideoInfo clip1info = *vsapi->getVideoInfo(clip1);

    data->clip1 = clip1;
    data->fade_duration = num_frames;

    data->vi.format = clip1info.format;
    data->vi.numFrames = clip1info.numFrames;
    data->vi.audio_samplerate = clip1info.audio_samplerate;
    data->vi.channels = clip1info.channels;
    data->vi.numAudioSample = clip1info.numAudioSample;

    vsapi->createFilter(in, out, "FadeOut", FadeInOutInit, NULL, FadeInOutFree, FadeInOutGetAudio, fmParallel, nfNoCache, data, core);
}

static void VS_CC AudioDubInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    AudioDubData *d = (AudioDubData *)* instanceData;

    d->clip1info.hasAudio = true;
    d->clip1info.audio_samplerate = d->clip2info.audio_samplerate;
    d->clip1info.channels = d->clip2info.channels;

    vsapi->setVideoInfo(&d->clip1info, 1, node);
}

static void VS_CC AudioDubFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
}

static void VS_CC AudioDubGetAudio(VSCore *core, const VSAPI *vsapi, void *instanceData, void *lpBuffer, long lStart, long lSamples) {
    AudioDubData *d = (AudioDubData *)instanceData;

    vsapi->getAudio(d->clip2, lpBuffer, lStart, lSamples);
}

static const VSFrameRef *VS_CC AudioDubGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioDubData *d = (AudioDubData *)* instanceData;

    return vsapi->getFrame(n, d->clip1, NULL, 0);
}

static void VS_CC AudioDubCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    AudioDubData *data = (AudioDubData*)malloc(sizeof(AudioDubData));
    int err;

    VSNodeRef *clip1 = vsapi->propGetNode(in, "clip1", 0, &err);
    VSNodeRef *clip2 = vsapi->propGetNode(in, "clip2", 0, &err);

    data->vi = *vsapi->getVideoInfo(clip1);

    VSVideoInfo clip1info = *vsapi->getVideoInfo(clip1);
    VSVideoInfo clip2info = *vsapi->getVideoInfo(clip2);

    data->clip1 = clip1;
    data->clip1info = clip1info;
    data->clip2 = clip2;
    data->clip2info = clip2info;

    data->vi.numAudioSample = clip2info.numAudioSample;

    vsapi->createFilter(in, out, "AudioDub", AudioDubInit, AudioDubGetFrame, MixAudioFree, AudioDubGetAudio, fmParallel, nfNoCache, data, core);
}

void VS_CC audioInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("Tone", "length:float:opt;frequency:float:opt;samplerate:int:opt;channels:int:opt;type:data:opt;level:float:opt", ToneCreate, 0, plugin);
    registerFunc("MixAudio", "clip1:clip;clip2:clip;clip1_factor:float:opt;clip2_factor:float:opt", MixAudioCreate, 0, plugin);
    registerFunc("FadeInOut", "clip1:clip;num_frames:float", FadeInOutCreate, 0, plugin);
    registerFunc("AudioDub", "clip1:clip;clip2:clip", AudioDubCreate, 0, plugin);
}