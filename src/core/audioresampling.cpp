/*
* Copyright (c) 2026 Fredrik Mellbin
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "internalfilters.h"
#include "filtershared.h"
#include "VSHelper4.h"

//////////////////////////////////////////
// AudioResample

enum class DitherType {
    None,
    Rectangular,
    Triangular
};

// Dither has to be a pure function of the sample position. A running prng would make the output
// of a frame depend on which frames happened to be produced before it, so seeking or recomputing
// an evicted frame would return different samples than a linear pass. Hashing the position keeps
// every frame independent and bit exact regardless of request order.
static const uint64_t ditherChannelStride = 0x9E3779B97F4A7C15ull;

static inline uint64_t ditherMix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

// The returned offset is in destination lsb units, rpdf covers +-0.5 and tpdf +-1.
template<DitherType DT>
static inline double ditherValue(int64_t sampleIndex, int channel) {
    if constexpr (DT == DitherType::None) {
        return 0.0;
    } else {
        uint64_t h = ditherMix(static_cast<uint64_t>(sampleIndex) + (static_cast<uint64_t>(channel) + 1) * ditherChannelStride);
        const double scale = 1.0 / 4294967296.0;
        double u1 = static_cast<double>(static_cast<uint32_t>(h)) * scale;
        if constexpr (DT == DitherType::Rectangular)
            return u1 - 0.5;
        else
            return u1 - static_cast<double>(static_cast<uint32_t>(h >> 32)) * scale;
    }
}

// Everything the conversion of one span of samples needs to know. A load into the float working
// format is a spec with only the scale filled in, a float destination neither dithers nor clamps
// so the remaining fields are never read.
struct ConvertSpec {
    double mul = 1.0;
    double minV = 0.0;
    double maxV = 0.0;
    double outputShift = 1.0;   // left aligns the quantized value in its container, see below
    int64_t positionBase = 0;   // absolute position of the first sample, seeds the dither hash
    int channel = 0;            // decorrelates the dither between channels
};

typedef bool (*ConvertFunc)(const uint8_t *srcp, uint8_t *dstp, int length, const ConvertSpec &spec);

// Returns true when at least one sample had to be clamped to fit the destination range.
template<typename S, typename D, DitherType DT>
static bool convertPlane(const uint8_t *srcp, uint8_t *dstp, int length, const ConvertSpec &spec) {
    const S * VS_RESTRICT src = reinterpret_cast<const S *>(srcp);
    D * VS_RESTRICT dst = reinterpret_cast<D *>(dstp);
    double mul = spec.mul;

    if constexpr (std::is_integral_v<D>) {
        double minV = spec.minV;
        double maxV = spec.maxV;
        double outputShift = spec.outputShift;
        bool clipped = false;
        for (int i = 0; i < length; i++) {
            // rounding has to happen before clamping, the dither offset can push a sample that is
            // still in range past the last representable value
            double rounded = std::round(static_cast<double>(src[i]) * mul + ditherValue<DT>(spec.positionBase + i, spec.channel));
            double limited = std::clamp(rounded, minV, maxV);
            clipped |= (limited != rounded);
            // quantization happens on the significant bit grid, the shift then moves the result up
            // into the container so the padding ends up in the unused low bits
            dst[i] = static_cast<D>(limited * outputShift);
        }
        return clipped;
    } else {
        for (int i = 0; i < length; i++)
            dst[i] = static_cast<D>(static_cast<double>(src[i]) * mul);
        return false;
    }
}

template<typename S, typename D>
static ConvertFunc selectDitherFunc(DitherType dither) {
    if constexpr (std::is_integral_v<D>) {
        switch (dither) {
            case DitherType::Rectangular:
                return convertPlane<S, D, DitherType::Rectangular>;
            case DitherType::Triangular:
                return convertPlane<S, D, DitherType::Triangular>;
            default:
                return convertPlane<S, D, DitherType::None>;
        }
    } else {
        return convertPlane<S, D, DitherType::None>;
    }
}

template<typename S>
static ConvertFunc selectDstFunc(const VSAudioFormat &dst, DitherType dither) {
    if (dst.sampleType == stFloat)
        return selectDitherFunc<S, float>(dither);
    else if (dst.bytesPerSample == 2)
        return selectDitherFunc<S, int16_t>(dither);
    else
        return selectDitherFunc<S, int32_t>(dither);
}

static ConvertFunc selectConvertFunc(const VSAudioFormat &src, const VSAudioFormat &dst, DitherType dither) {
    if (src.sampleType == stFloat)
        return selectDstFunc<float>(dst, dither);
    else if (src.bytesPerSample == 2)
        return selectDstFunc<int16_t>(dst, dither);
    else
        return selectDstFunc<int32_t>(dst, dither);
}

// Integer samples fill the whole width of their container with the significant bits left aligned,
// so 24 bit samples sit in the top 24 bits of a 32 bit container and the unused low bits are
// padding. That is the layout the wave writers and every source filter use.
static int containerBits(const VSAudioFormat &f) {
    return f.bytesPerSample * 8;
}

// Float samples use the nominal [-1, 1) range, so reading an integer sample normalizes against the
// range of the container it fills rather than against its significant bits.
static double formatToNorm(const VSAudioFormat &f) {
    return (f.sampleType == stFloat) ? 1.0 : std::ldexp(1.0, -(containerBits(f) - 1));
}

// Writing goes the other way but stops at the significant bits, since quantizing has to land on the
// grid the format actually resolves. ConvertSpec::outputShift moves the result into the container.
static double normToFormat(const VSAudioFormat &f) {
    return (f.sampleType == stFloat) ? 1.0 : std::ldexp(1.0, f.bitsPerSample - 1);
}

static double formatOutputShift(const VSAudioFormat &f) {
    return (f.sampleType == stFloat) ? 1.0 : std::ldexp(1.0, containerBits(f) - f.bitsPerSample);
}

// Only quantization to a coarser representation benefits from dither, widening an integer format
// is exact and a float destination isn't quantized at all.
static bool needsDither(const VSAudioFormat &src, const VSAudioFormat &dst) {
    if (dst.sampleType != stInteger)
        return false;
    if (src.sampleType == stFloat)
        return true;
    return src.bitsPerSample > dst.bitsPerSample;
}

// The whole dither decision in one place. The working format is whatever the destination is
// quantized from, the source format for direct conversion and float for the mixing and
// resampling pipeline, which is why integer output of the pipeline always dithers.
static DitherType resolveDither(DitherType requested, const VSAudioFormat &working, const VSAudioFormat &dst) {
    return needsDither(working, dst) ? requested : DitherType::None;
}

// The intermediate format mixing and resampling operate on, normalized 32 bit float.
static VSAudioFormat floatWorkingFormat() {
    VSAudioFormat f = {};
    f.sampleType = stFloat;
    f.bitsPerSample = 32;
    f.bytesPerSample = 4;
    return f;
}

//////////////////////////////////////////
// Polyphase resampling
//
// The output rate over the input rate reduces to the exact fraction p/q, which makes output sample
// m land on input position m*q/p. Both the whole part and the fractional phase of that position are
// computed with integer arithmetic straight from m, so no state carries between samples and a frame
// produced after a seek is identical to the same frame produced by a linear pass. The kernel is a
// Kaiser windowed sinc evaluated once per phase at construction time.

static const int resampleTapsPerSide = 64;      // 128 taps at a 1:1 ratio
static const double resampleCutoff = 0.91;      // passband as a fraction of the lower nyquist
static const double resampleKaiserBeta = 9.0;   // roughly 90 db of stopband attenuation
static const int resampleMaxTaps = 8192;        // caps the per sample cost for extreme decimation
static const uint64_t resampleMaxBankBytes = 32u << 20;

struct ResampleBank {
    int64_t p = 1;              // output rate divided by the gcd, also the number of phases
    int64_t q = 1;              // input rate divided by the gcd
    int64_t baseStep = 0;       // whole input samples advanced per output sample
    int64_t phaseStep = 0;      // phase advanced per output sample
    int halfTaps = 0;           // kernel half width in input samples
    int tapCount = 0;
    std::vector<float> coeffs;  // p rows of tapCount coefficients
};

static int64_t gcd64(int64_t a, int64_t b) {
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    double quarterSq = x * x / 4.0;
    for (int k = 1; k < 128; k++) {
        term *= quarterSq / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term <= sum * 1e-18)
            break;
    }
    return sum;
}

static double normalizedSinc(double x) {
    if (x == 0.0)
        return 1.0;
    double pix = 3.14159265358979323846 * x;
    return std::sin(pix) / pix;
}

static std::string buildResampleBank(ResampleBank &bank, int inRate, int outRate) {
    int64_t g = gcd64(inRate, outRate);
    bank.p = outRate / g;
    bank.q = inRate / g;
    bank.baseStep = bank.q / bank.p;
    bank.phaseStep = bank.q % bank.p;

    // The kernel has to stop the lower of the two nyquist frequencies. Decimation lowers the
    // cutoff, which stretches the kernel over proportionally more input samples.
    double ratio = std::min(1.0, static_cast<double>(bank.p) / static_cast<double>(bank.q));
    bank.halfTaps = static_cast<int>(std::ceil(resampleTapsPerSide / ratio));
    bank.tapCount = 2 * bank.halfTaps;

    if (bank.tapCount > resampleMaxTaps)
        return "the sample rate ratio requires an impractically long filter, convert in several steps instead";
    if (static_cast<uint64_t>(bank.p) * static_cast<uint64_t>(bank.tapCount) * sizeof(float) > resampleMaxBankBytes)
        return "the sample rate ratio reduces to a fraction with too many phases to build a filter for";

    double fc = 0.5 * ratio * resampleCutoff;   // cutoff in cycles per input sample
    double invI0Beta = 1.0 / besselI0(resampleKaiserBeta);

    bank.coeffs.resize(static_cast<size_t>(bank.p) * static_cast<size_t>(bank.tapCount));
    std::vector<double> row(bank.tapCount);

    for (int64_t phase = 0; phase < bank.p; phase++) {
        double frac = static_cast<double>(phase) / static_cast<double>(bank.p);
        double sum = 0.0;

        for (int j = 0; j < bank.tapCount; j++) {
            // distance in input samples from the output position to the contributing input sample
            double t = frac + static_cast<double>(bank.halfTaps - 1 - j);
            double w = t / static_cast<double>(bank.halfTaps);
            double value = 0.0;
            if (w > -1.0 && w < 1.0)
                value = 2.0 * fc * normalizedSinc(2.0 * fc * t) * besselI0(resampleKaiserBeta * std::sqrt(1.0 - w * w)) * invI0Beta;
            row[j] = value;
            sum += value;
        }

        // Every phase is normalized to unity dc gain on its own. Without this the small differences
        // between the truncated phases turn into a gain ripple at the phase rate.
        double norm = (sum != 0.0) ? (1.0 / sum) : 1.0;
        float *dstRow = bank.coeffs.data() + static_cast<size_t>(phase) * static_cast<size_t>(bank.tapCount);
        for (int j = 0; j < bank.tapCount; j++)
            dstRow[j] = static_cast<float>(row[j] * norm);
    }

    return {};
}

// floor division, b must be positive
static int64_t floorDiv(int64_t a, int64_t b) {
    int64_t quot = a / b;
    if ((a % b != 0) && (a < 0))
        quot--;
    return quot;
}

//////////////////////////////////////////
// Channel mixing
//
// The matrix is derived from the two layouts rather than looked up per layout pair, which would
// need a table with a hole in it for every combination nobody thought of. Channels that exist on
// both sides pass straight through and every input channel that has no home in the output is
// folded into the channels that do exist, following one rule per channel position.

// ITU-R BS.775 folds the centre and the surrounds in at -3 dB, which is also the default that
// ATSC A/52 carries in its clev and slev fields.
static const double mixLevelCenter = 0.70710678118654752440;
static const double mixLevelSurround = 0.70710678118654752440;

static std::vector<int> layoutChannels(uint64_t layout) {
    std::vector<int> channels;
    for (int i = 0; i < 64; i++) {
        if ((layout >> i) & 1)
            channels.push_back(i);
    }
    return channels;
}

// Elevated and auxiliary positions carry no fold rule of their own, they collapse onto the channel
// below or beside them and then follow whatever that channel does.
static int groundEquivalent(int channel) {
    switch (channel) {
        case acTopFrontLeft: case acWideLeft: case acStereoLeft:
            return acFrontLeft;
        case acTopFrontRight: case acWideRight: case acStereoRight:
            return acFrontRight;
        case acTopFrontCenter: case acTopCenter:
            return acFrontCenter;
        case acTopBackLeft: case acSurroundDirectLeft:
            return acBackLeft;
        case acTopBackRight: case acSurroundDirectRight:
            return acBackRight;
        case acTopBackCenter:
            return acBackCenter;
        default:
            return channel;
    }
}

struct MixMatrix {
    std::vector<float> weights;      // numOut rows of numIn coefficients, empty when no mixing is needed
    std::vector<uint8_t> silentRows; // set for rows without any contributing input channel
};

static MixMatrix buildMixMatrix(uint64_t inLayout, uint64_t outLayout, bool normalize) {
    std::vector<int> inChannels = layoutChannels(inLayout);
    std::vector<int> outChannels = layoutChannels(outLayout);
    int numIn = static_cast<int>(inChannels.size());
    int numOut = static_cast<int>(outChannels.size());

    int outIndex[64];
    for (int i = 0; i < 64; i++)
        outIndex[i] = -1;
    for (int i = 0; i < numOut; i++)
        outIndex[outChannels[i]] = i;

    std::vector<double> m(static_cast<size_t>(numOut) * numIn, 0.0);

    auto has = [&](int channel) { return channel >= 0 && channel < 64 && outIndex[channel] >= 0; };
    auto add = [&](int channel, int inIdx, double weight) { m[static_cast<size_t>(outIndex[channel]) * numIn + inIdx] += weight; };

    // one rule for both surround sides so the mirrored cases can't drift apart, alt is the same
    // position under its other name since layouts disagree about whether the surrounds are side
    // or back channels
    auto foldSurround = [&](int inIdx, int alt, int front) {
        if (has(alt))
            add(alt, inIdx, 1.0);
        else if (has(front))
            add(front, inIdx, mixLevelSurround);
        else if (has(acFrontCenter))
            add(acFrontCenter, inIdx, mixLevelSurround * mixLevelCenter);
    };

    for (int inIdx = 0; inIdx < numIn; inIdx++) {
        int channel = inChannels[inIdx];
        if (has(channel)) {
            add(channel, inIdx, 1.0);
            continue;
        }

        channel = groundEquivalent(channel);
        if (has(channel)) {
            add(channel, inIdx, 1.0);
            continue;
        }

        switch (channel) {
            case acFrontCenter:
                if (has(acFrontLeft) && has(acFrontRight)) {
                    add(acFrontLeft, inIdx, mixLevelCenter);
                    add(acFrontRight, inIdx, mixLevelCenter);
                } else if (has(acFrontLeft)) {
                    add(acFrontLeft, inIdx, mixLevelCenter);
                } else if (has(acFrontRight)) {
                    add(acFrontRight, inIdx, mixLevelCenter);
                }
                break;

            case acFrontLeft:
            case acFrontRight: {
                // a front pair with nowhere to go collapses onto the centre, which is how stereo
                // folds down to mono
                int other = (channel == acFrontLeft) ? acFrontRight : acFrontLeft;
                if (has(acFrontCenter))
                    add(acFrontCenter, inIdx, mixLevelCenter);
                else if (has(other))
                    add(other, inIdx, mixLevelCenter);
                break;
            }

            case acBackLeft:
            case acSideLeft:
                foldSurround(inIdx, (channel == acBackLeft) ? acSideLeft : acBackLeft, acFrontLeft);
                break;

            case acBackRight:
            case acSideRight:
                foldSurround(inIdx, (channel == acBackRight) ? acSideRight : acBackRight, acFrontRight);
                break;

            case acBackCenter:
                if (has(acBackLeft) && has(acBackRight)) {
                    add(acBackLeft, inIdx, mixLevelCenter);
                    add(acBackRight, inIdx, mixLevelCenter);
                } else if (has(acSideLeft) && has(acSideRight)) {
                    add(acSideLeft, inIdx, mixLevelCenter);
                    add(acSideRight, inIdx, mixLevelCenter);
                } else if (has(acFrontLeft) && has(acFrontRight)) {
                    add(acFrontLeft, inIdx, mixLevelSurround * mixLevelCenter);
                    add(acFrontRight, inIdx, mixLevelSurround * mixLevelCenter);
                } else if (has(acFrontCenter)) {
                    add(acFrontCenter, inIdx, mixLevelSurround);
                }
                break;

            case acFrontLeftOFCenter:
            case acFrontRightOFCenter: {
                // sits between a front channel and the centre so it splits evenly between them
                int side = (channel == acFrontLeftOFCenter) ? acFrontLeft : acFrontRight;
                if (has(side) && has(acFrontCenter)) {
                    add(side, inIdx, mixLevelCenter);
                    add(acFrontCenter, inIdx, mixLevelCenter);
                } else if (has(side)) {
                    add(side, inIdx, 1.0);
                } else if (has(acFrontCenter)) {
                    add(acFrontCenter, inIdx, 1.0);
                }
                break;
            }

            case acLowFrequency:
            case acLowFrequency2: {
                // the lfe is discarded when the output has no lfe of its own, folding boosted
                // bass into the mains is a surprise nobody wants by default
                int alt = (channel == acLowFrequency) ? acLowFrequency2 : acLowFrequency;
                if (has(alt))
                    add(alt, inIdx, 1.0);
                break;
            }

            default:
                // an input channel with no sensible destination is dropped rather than smeared
                // into an unrelated position
                break;
        }
    }

    // The worst case gain of an output channel is the sum of the absolute weights feeding it.
    // Scaling the whole matrix instead of the individual rows is what keeps the balance between
    // the output channels intact.
    if (normalize) {
        double peak = 0.0;
        for (int o = 0; o < numOut; o++) {
            double sum = 0.0;
            for (int i = 0; i < numIn; i++)
                sum += std::abs(m[static_cast<size_t>(o) * numIn + i]);
            peak = std::max(peak, sum);
        }
        if (peak > 1.0) {
            for (double &v : m)
                v /= peak;
        }
    }

    MixMatrix result;
    result.weights.resize(m.size());
    for (size_t i = 0; i < m.size(); i++)
        result.weights[i] = static_cast<float>(m[i]);

    result.silentRows.resize(numOut);
    for (int o = 0; o < numOut; o++) {
        bool silent = true;
        for (int i = 0; i < numIn; i++)
            silent = silent && (m[static_cast<size_t>(o) * numIn + i] == 0.0);
        result.silentRows[o] = silent;
    }
    return result;
}

//////////////////////////////////////////
// Filter implementation

enum class OutputStage : uint8_t {
    Convert,    // run the format conversion on the assembled samples
    Silence     // nothing feeds the channel so it stays digital silence
};

struct AudioResampleDataExtra {
    VSAudioInfo ai = {};
    ResampleBank bank;
    MixMatrix mix;
    std::vector<OutputStage> outputStage;   // what the terminal stage does for each output channel
    ConvertFunc convert = nullptr;
    ConvertFunc gather = nullptr;           // conversion to the normalized float working format
    double mul = 1.0;
    double srcScale = 1.0;
    double minV = 0.0;
    double maxV = 0.0;
    double outputShift = 1.0;
    int srcNumFrames = 0;
    int srcBytesPerSample = 0;
    int numInChannels = 0;
    int numOutChannels = 0;
    bool doResample = false;
    bool overflowError = false;
    std::atomic<bool> overflowWarned = false;
};

typedef SingleNodeData<AudioResampleDataExtra> AudioResampleData;

// Settles the entire output side in one place: which conversion runs, its scale, whether it
// dithers, the clamping range and what the terminal stage does for every channel.
static void planOutputStage(AudioResampleData *d, const VSAudioFormat &working, DitherType requested) {
    const VSAudioFormat &dst = d->ai.format;
    d->convert = selectConvertFunc(working, dst, resolveDither(requested, working, dst));
    d->mul = formatToNorm(working) * normToFormat(dst);
    d->outputShift = formatOutputShift(dst);

    if (dst.sampleType == stInteger) {
        d->maxV = static_cast<double>((static_cast<int64_t>(1) << (dst.bitsPerSample - 1)) - 1);
        d->minV = -static_cast<double>(static_cast<int64_t>(1) << (dst.bitsPerSample - 1));
    }

    // a channel nothing feeds bypasses conversion so it stays digital silence, quantization
    // would stamp dither noise onto it
    d->outputStage.assign(d->numOutChannels, OutputStage::Convert);
    for (size_t oc = 0; oc < d->mix.silentRows.size(); oc++) {
        if (d->mix.silentRows[oc])
            d->outputStage[oc] = OutputStage::Silence;
    }
}

// Returns true when the frame has been failed and freed instead of merely warned about.
static bool reportClipping(AudioResampleData *d, int64_t startSample, int length, VSFrame *dst, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    std::string msg = "AudioResample: clipping detected in the sample interval " + std::to_string(startSample) + " to " + std::to_string(startSample + length - 1);
    if (d->overflowError) {
        vsapi->setFilterError(msg.c_str(), frameCtx);
        vsapi->freeFrame(dst);
        return true;
    }
    if (!d->overflowWarned.exchange(true))
        vsapi->logMessage(mtWarning, (msg + ", only the first encountered clipped segment has a warning printed").c_str(), core);
    return false;
}

// Sample format conversion only, output frame n maps directly onto input frame n.
static const VSFrame *VS_CC audioResampleGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioResampleData *d = reinterpret_cast<AudioResampleData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int length = vsapi->getFrameLength(src);
        VSFrame *dst = vsapi->newAudioFrame(&d->ai.format, length, src, core);
        // derived from the frame number and never from a running counter so the dither sequence
        // depends on position alone
        int64_t startSample = static_cast<int64_t>(n) * VS_AUDIO_FRAME_SAMPLES;
        bool clipped = false;

        for (int p = 0; p < d->numOutChannels; p++)
            clipped |= d->convert(vsapi->getReadPtr(src, p), vsapi->getWritePtr(dst, p), length, {.mul = d->mul, .minV = d->minV, .maxV = d->maxV, .outputShift = d->outputShift, .positionBase =startSample, .channel = p});

        vsapi->freeFrame(src);

        if (clipped && reportClipping(d, startSample, length, dst, frameCtx, core, vsapi))
            return nullptr;

        return dst;
    }

    return nullptr;
}

// Channel mixing and sample rate conversion, both working on normalized float. Mixing runs first
// because a downmix then leaves fewer channels to filter, and since both operations are linear the
// order only affects the amount of work. The input range for output frame n is recomputed from n
// alone every time so the result never depends on what was produced before it.
static const VSFrame *VS_CC audioResampleGetFrameFloat(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    AudioResampleData *d = reinterpret_cast<AudioResampleData *>(instanceData);
    const ResampleBank &bank = d->bank;

    int64_t firstOutSample = static_cast<int64_t>(n) * VS_AUDIO_FRAME_SAMPLES;
    int outCount = static_cast<int>(std::min<int64_t>(VS_AUDIO_FRAME_SAMPLES, d->ai.numSamples - firstOutSample));

    int64_t firstBase = 0;
    int64_t firstPhase = 0;
    int64_t firstInSample;
    int64_t lastInSample;

    if (d->doResample) {
        // input position of the first and last output sample of this frame, exact in integers
        int64_t firstPos = firstOutSample * bank.q;
        firstBase = firstPos / bank.p;
        firstPhase = firstPos % bank.p;
        int64_t lastBase = ((firstOutSample + outCount - 1) * bank.q) / bank.p;
        firstInSample = firstBase - bank.halfTaps + 1;
        lastInSample = lastBase + bank.halfTaps;
    } else {
        firstInSample = firstOutSample;
        lastInSample = firstOutSample + outCount - 1;
    }

    int64_t firstInFrame = std::max<int64_t>(floorDiv(firstInSample, VS_AUDIO_FRAME_SAMPLES), 0);
    int64_t lastInFrame = std::min<int64_t>(floorDiv(lastInSample, VS_AUDIO_FRAME_SAMPLES), d->srcNumFrames - 1);

    if (activationReason == arInitial) {
        for (int64_t f = firstInFrame; f <= lastInFrame; f++)
            vsapi->requestFrameFilter(static_cast<int>(f), d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        std::vector<const VSFrame *> srcFrames;
        srcFrames.reserve(static_cast<size_t>(std::max<int64_t>(lastInFrame - firstInFrame + 1, 0)));
        for (int64_t f = firstInFrame; f <= lastInFrame; f++)
            srcFrames.push_back(vsapi->getFrameFilter(static_cast<int>(f), d->node, frameCtx));

        VSFrame *dst = vsapi->newAudioFrame(&d->ai.format, outCount, srcFrames.empty() ? nullptr : srcFrames[0], core);

        int inCount = static_cast<int>(lastInSample - firstInSample + 1);
        int numIn = d->numInChannels;
        int numOut = d->numOutChannels;
        bool mixing = !d->mix.weights.empty();

        // one row per output channel, holding the input window after mixing
        std::vector<float> rows(static_cast<size_t>(numOut) * inCount, 0.0f);
        std::vector<float> scratch(mixing ? inCount : 0);
        std::vector<float> outBuf(d->doResample ? outCount : 0);
        bool clipped = false;

        // samples before the start and after the end of the clip read as silence, which is what
        // makes the first and last output samples fade rather than wrap
        auto gatherChannel = [&](int channel, float *dstRow) {
            std::fill(dstRow, dstRow + inCount, 0.0f);
            for (size_t k = 0; k < srcFrames.size(); k++) {
                int64_t frameStart = (firstInFrame + static_cast<int64_t>(k)) * VS_AUDIO_FRAME_SAMPLES;
                int64_t lo = std::max(frameStart, firstInSample);
                int64_t hi = std::min(frameStart + vsapi->getFrameLength(srcFrames[k]), firstInSample + inCount);
                if (lo < hi)
                    d->gather(vsapi->getReadPtr(srcFrames[k], channel) + (lo - frameStart) * d->srcBytesPerSample, reinterpret_cast<uint8_t *>(dstRow + (lo - firstInSample)), static_cast<int>(hi - lo), {.mul = d->srcScale});
            }
        };

        if (mixing) {
            // accumulate each input channel into every output channel it feeds, which avoids
            // holding the whole input window for all channels at once
            for (int ic = 0; ic < numIn; ic++) {
                gatherChannel(ic, scratch.data());
                for (int oc = 0; oc < numOut; oc++) {
                    float weight = d->mix.weights[static_cast<size_t>(oc) * numIn + ic];
                    if (weight == 0.0f)
                        continue;
                    float * VS_RESTRICT row = rows.data() + static_cast<size_t>(oc) * inCount;
                    for (int i = 0; i < inCount; i++)
                        row[i] += weight * scratch[i];
                }
            }
        } else {
            for (int oc = 0; oc < numOut; oc++)
                gatherChannel(oc, rows.data() + static_cast<size_t>(oc) * inCount);
        }

        for (int oc = 0; oc < numOut; oc++) {
            if (d->outputStage[oc] == OutputStage::Silence) {
                memset(vsapi->getWritePtr(dst, oc), 0, static_cast<size_t>(outCount) * d->ai.format.bytesPerSample);
                continue;
            }

            const float *mixed = rows.data() + static_cast<size_t>(oc) * inCount;
            const float *quantSrc = mixed;

            if (d->doResample) {
                int64_t base = firstBase;
                int64_t phase = firstPhase;
                for (int m = 0; m < outCount; m++) {
                    const float * VS_RESTRICT coeff = bank.coeffs.data() + static_cast<size_t>(phase) * static_cast<size_t>(bank.tapCount);
                    const float * VS_RESTRICT window = mixed + (base - bank.halfTaps + 1 - firstInSample);
                    double acc = 0.0;
                    for (int j = 0; j < bank.tapCount; j++)
                        acc += static_cast<double>(window[j]) * static_cast<double>(coeff[j]);
                    outBuf[m] = static_cast<float>(acc);

                    base += bank.baseStep;
                    phase += bank.phaseStep;
                    if (phase >= bank.p) {
                        phase -= bank.p;
                        base++;
                    }
                }
                quantSrc = outBuf.data();
            }

            clipped |= d->convert(reinterpret_cast<const uint8_t *>(quantSrc), vsapi->getWritePtr(dst, oc), outCount, {.mul = d->mul, .minV = d->minV, .maxV = d->maxV, .outputShift = d->outputShift, .positionBase =firstOutSample, .channel = oc});
        }

        for (const VSFrame *f : srcFrames)
            vsapi->freeFrame(f);

        if (clipped && reportClipping(d, firstOutSample, outCount, dst, frameCtx, core, vsapi))
            return nullptr;

        return dst;
    }

    return nullptr;
}

// Everything the arguments describe, parsed and validated up front before any decisions are
// made so that a bad argument is reported even when the requested conversion is a no-op.
struct AudioResampleArgs {
    int sampleRate = 0;
    uint64_t channelLayout = 0;
    int sampleType = 0;
    int bits = 0;
    DitherType dither = DitherType::Triangular;
    bool normalize = false;
    bool normalizeSet = false;  // the normalize default depends on the output sample type
    bool overflowError = false;
};

// Returns an error description or an empty string.
static std::string parseAudioResampleArgs(AudioResampleArgs &a, const VSMap *in, const VSAudioInfo &srcAi, const VSAPI *vsapi) {
    int err;

    a.sampleRate = vsapi->mapGetIntSaturated(in, "samplerate", 0, &err);
    if (err)
        a.sampleRate = srcAi.sampleRate;
    if (a.sampleRate < 1)
        return "samplerate must be positive";

    a.channelLayout = srcAi.format.channelLayout;
    int numChannelElems = vsapi->mapNumElements(in, "channels");
    if (numChannelElems > 0) {
        a.channelLayout = 0;
        for (int i = 0; i < numChannelElems; i++) {
            int64_t channel = vsapi->mapGetInt(in, "channels", i, nullptr);
            if (channel < 0 || channel >= 64)
                return "channel number out of range (0-63)";
            uint64_t ctemp = static_cast<uint64_t>(1) << channel;
            if (a.channelLayout & ctemp)
                return "channel specified twice";
            a.channelLayout |= ctemp;
        }
    }

    a.sampleType = srcAi.format.sampleType;
    int64_t sampleTypeArg = vsapi->mapGetInt(in, "sampletype", 0, &err);
    if (!err)
        a.sampleType = sampleTypeArg ? stFloat : stInteger;

    a.bits = vsapi->mapGetIntSaturated(in, "bits", 0, &err);
    if (err)
        a.bits = (a.sampleType == stFloat) ? 32 : srcAi.format.bitsPerSample;

    const char *ditherName = vsapi->mapGetData(in, "dither_type", 0, &err);
    if (!err) {
        std::string ditherStr(ditherName, vsapi->mapGetDataSize(in, "dither_type", 0, nullptr));
        if (ditherStr == "none")
            a.dither = DitherType::None;
        else if (ditherStr == "rectangular")
            a.dither = DitherType::Rectangular;
        else if (ditherStr == "triangular")
            a.dither = DitherType::Triangular;
        else
            return "invalid dither_type, must be none, rectangular or triangular";
    }

    a.normalize = !!vsapi->mapGetInt(in, "normalize", 0, &err);
    a.normalizeSet = !err;
    a.overflowError = !!vsapi->mapGetInt(in, "overflow_error", 0, &err);

    return {};
}

static void VS_CC audioResampleCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<AudioResampleData> d(new AudioResampleData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSAudioInfo srcAi = *vsapi->getAudioInfo(d->node);
    d->ai = srcAi;
    d->srcNumFrames = srcAi.numFrames;
    d->numInChannels = srcAi.format.numChannels;

    AudioResampleArgs args;
    std::string argError = parseAudioResampleArgs(args, in, srcAi, vsapi);
    if (!argError.empty())
        RETERROR(("AudioResample: " + argError).c_str());

    if (!vsapi->queryAudioFormat(&d->ai.format, args.sampleType, args.bits, args.channelLayout, core))
        RETERROR("AudioResample: invalid output format specified");
    d->ai.sampleRate = args.sampleRate;
    d->numOutChannels = d->ai.format.numChannels;
    d->overflowError = args.overflowError;

    d->doResample = (args.sampleRate != srcAi.sampleRate);
    bool mixing = (args.channelLayout != srcAi.format.channelLayout);
    bool formatChanged = (d->ai.format.sampleType != srcAi.format.sampleType || d->ai.format.bitsPerSample != srcAi.format.bitsPerSample);

    if (!d->doResample && !mixing && !formatChanged) {
        vsapi->mapConsumeNode(out, "clip", d->node, maAppend);
        d->node = nullptr;
        return;
    }

    if (d->doResample) {
        std::string bankError = buildResampleBank(d->bank, srcAi.sampleRate, args.sampleRate);
        if (!bankError.empty())
            RETERROR(("AudioResample: " + bankError).c_str());

        // output sample m is valid while its input position m*q/p is still inside the clip, so the
        // length rounds up
        if (srcAi.numSamples > (std::numeric_limits<int64_t>::max() - d->bank.q + 1) / d->bank.p)
            RETERROR("AudioResample: clip is too long to resample by this ratio");
        int64_t numOut = (srcAi.numSamples * d->bank.p + d->bank.q - 1) / d->bank.q;
        if (numOut < 1 || numOut > static_cast<int64_t>(std::numeric_limits<int>::max()) * VS_AUDIO_FRAME_SAMPLES)
            RETERROR("AudioResample: the resampled clip length is out of range");
        d->ai.numSamples = numOut;
    }

    if (mixing) {
        // Guarding against the worst case sum costs level on every downmix, so it is only the
        // default for integer output where clipping can't be undone. Float output keeps the plain
        // BS.775 coefficients and is free to exceed unity.
        bool normalize = args.normalizeSet ? args.normalize : (d->ai.format.sampleType == stInteger);
        d->mix = buildMixMatrix(srcAi.format.channelLayout, args.channelLayout, normalize);
    }

    if (d->doResample || mixing) {
        d->srcScale = formatToNorm(srcAi.format);
        d->srcBytesPerSample = srcAi.format.bytesPerSample;
        // gathering is an ordinary format conversion into the working format, so it comes from
        // the same dispatch as the output quantizer
        d->gather = selectConvertFunc(srcAi.format, floatWorkingFormat(), DitherType::None);
        planOutputStage(d.get(), floatWorkingFormat(), args.dither);

        // without a rate change output frame n still only reads input frame n
        VSFilterDependency deps[] = {{d->node, d->doResample ? rpGeneral : rpStrictSpatial}};
        vsapi->createAudioFilter(out, "AudioResample", &d->ai, audioResampleGetFrameFloat, filterFree<AudioResampleData>, fmParallel, deps, 1, d.get(), core);
    } else {
        planOutputStage(d.get(), srcAi.format, args.dither);

        VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
        vsapi->createAudioFilter(out, "AudioResample", &d->ai, audioResampleGetFrame, filterFree<AudioResampleData>, fmParallel, deps, 1, d.get(), core);
    }

    d.release();
}

//////////////////////////////////////////
// Init

void audioResamplingInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("AudioResample", "clip:anode;samplerate:int:opt;sampletype:int:opt;bits:int:opt;channels:int[]:opt;dither_type:data:opt;normalize:int:opt;overflow_error:int:opt;", "clip:anode;", audioResampleCreate, nullptr, plugin);
}
