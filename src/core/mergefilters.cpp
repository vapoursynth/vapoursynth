/*
* Copyright (c) 2012-2020 Fredrik Mellbin
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

#include <array>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <algorithm>
#include "cpufeatures.h"
#include "filtershared.h"
#include "gpufilter.h"
#include "internalfilters.h"
#include "kernel/cpulevel.h"
#include "kernel/merge.h"
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "VSVulkan4.h"

using namespace vsh;

namespace {

/* The same tail for the filters that keep their inputs in a vector rather than as a pair.
   Nulling what was handed over matters: the data object still owns the vector, and its
   destructor would free nodes the new filter now holds. */
template<typename T>
void createGPUFromVector(std::unique_ptr<T> &d, vsgpu::SimpleFilter &sf, int count, VSMap *out,
    VSCore *core, const VSAPI *vsapi) {
    std::string error;
    VSNode *node = vsgpu::createSimpleFilter(sf, d->nodes.data(), count, d->vi, core, vsapi, error);
    for (int i = 0; i < count; i++)
        d->nodes[i] = nullptr; /* consumed on success and failure alike */
    if (node)
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
    else
        vsapi->mapSetError(out, (std::string(sf.name) + ": " + error).c_str());
}

/* Shared tail for the two input GPU branches here: build the node, hand it back or report
   the error, and take both input nodes with it either way. */
template<typename T>
void createGPUFromDecl2(std::unique_ptr<T> &d, vsgpu::SimpleFilter &sf, VSMap *out, VSCore *core,
    const VSAPI *vsapi, const VSVideoInfo *outVi = nullptr) {
    VSNode *nodes[2] = { d->node1, d->node2 };
    std::string error;
    /* Most of these produce their input's format; the full diffs do not, so the output is
       nameable rather than assumed. */
    VSNode *node = vsgpu::createSimpleFilter(sf, nodes, 2, outVi ? outVi : d->vi, core, vsapi, error);
    d->node1 = nullptr; /* consumed on success and failure alike */
    d->node2 = nullptr;
    if (node)
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
    else
        vsapi->mapSetError(out, (std::string(sf.name) + ": " + error).c_str());
}

const char *premulPrelude =
    "int vsPremul(uint x, uint a, uint offset, uint maxval) {\n"
    "    int diff = int(x) - int(offset);\n"
    "    uint mag = uint(abs(diff));\n"
    "    mag = (mag * a + maxval / 2u) / maxval;\n"
    "    return diff < 0 ? -int(mag) : int(mag);\n"
    "}\n";

} // namespace

//////////////////////////////////////////
// Chroma-location-aware mask resampling helpers shared by MaskedMerge and PreMultiply.

static constexpr int numChromaLocations = 6; // VSC_CHROMA_LEFT (0) to VSC_CHROMA_BOTTOM (5)

// src_left/src_top (in source luma pixels) for downsampling a luma-resolution mask to
// chroma resolution while aligning output samples to chromaloc-implied luma
// position. The default centered downsample places output at luma offset (s-1)/2
// within a block of s luma pixels. Shift by the chromaloc offset minus that center.
static void getChromalocLumaShift(int chromaloc, int subSamplingW, int subSamplingH,
                                  double *src_left, double *src_top) {
    int s_w = 1 << subSamplingW;
    int s_h = 1 << subSamplingH;
    double center_w = (s_w - 1) / 2.0;
    double center_h = (s_h - 1) / 2.0;

    double h_offset = center_w;
    double v_offset = center_h;

    switch (chromaloc) {
        case VSC_CHROMA_LEFT:         h_offset = 0.0;       v_offset = center_h; break;
        case VSC_CHROMA_CENTER:       h_offset = center_w;  v_offset = center_h; break;
        case VSC_CHROMA_TOP_LEFT:     h_offset = 0.0;       v_offset = 0.0;      break;
        case VSC_CHROMA_TOP:          h_offset = center_w;  v_offset = 0.0;      break;
        case VSC_CHROMA_BOTTOM_LEFT:  h_offset = 0.0;       v_offset = s_h - 1;  break;
        case VSC_CHROMA_BOTTOM:       h_offset = center_w;  v_offset = s_h - 1;  break;
        default: break;
    }

    *src_left = h_offset - center_w;
    *src_top = v_offset - center_h;
}

static std::string createChromaResizeCandidates(VSNode *mask_node, int dst_width, int dst_height,
                                              int subSamplingW, int subSamplingH,
                                              VSNode *out[numChromaLocations],
                                              VSCore *core, const VSAPI *vsapi) {
    for (int i = 0; i < numChromaLocations; i++)
        out[i] = nullptr;

    VSPlugin *resize_plugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core);
    if (!resize_plugin)
        return "resize plugin not available";

    for (int loc = 0; loc < numChromaLocations; loc++) {
        double src_left, src_top;
        getChromalocLumaShift(loc, subSamplingW, subSamplingH, &src_left, &src_top);

        VSMap *args = vsapi->createMap();
        vsapi->mapSetNode(args, "clip", mask_node, maAppend);
        vsapi->mapSetInt(args, "width", dst_width, maAppend);
        vsapi->mapSetInt(args, "height", dst_height, maAppend);
        vsapi->mapSetFloat(args, "src_left", src_left, maAppend);
        vsapi->mapSetFloat(args, "src_top", src_top, maAppend);

        VSMap *result = vsapi->invoke(resize_plugin, "Bilinear", args);
        vsapi->freeMap(args);

        const char *invoke_err = vsapi->mapGetError(result);
        if (invoke_err) {
            std::string msg = std::string("resize.Bilinear failed: ") + invoke_err;
            vsapi->freeMap(result);
            for (int j = 0; j < loc; j++) {
                vsapi->freeNode(out[j]);
                out[j] = nullptr;
            }
            return msg;
        }

        out[loc] = vsapi->mapGetNode(result, "clip", 0, nullptr);
        vsapi->freeMap(result);
    }
    return {};
}

static int resolveChromaLocation(const VSFrame *frame, const VSAPI *vsapi) {
    int err;
    int64_t raw = vsapi->mapGetInt(vsapi->getFramePropertiesRO(frame), "_ChromaLocation", 0, &err);
    if (err || raw < VSC_CHROMA_LEFT || raw > VSC_CHROMA_BOTTOM)
        return VSC_CHROMA_LEFT;
    return static_cast<int>(raw);
}

namespace {
/* Subsampling is valid up to 4 per axis, a factor of sixteen, and the filter is twice the
   factor wide. Exotic, but constructible, and covering it costs only table space -- the total
   tap work per frame is actually constant, since the tap count grows as the square of the
   factor while the plane it is computed over shrinks by the same square. */

constexpr int maxChromaTaps = 32;

struct AxisFilter {
    int taps = 1;
    int origin = 0; /* first tap index, relative to subsampling factor * output index */
    float coeff[maxChromaTaps] = { 1.0f };
};

AxisFilter chromaAxisFilter(int subsampling, double shift) {
    const int s = 1 << subsampling;
    /* scale is 1/s and step is min(scale, 1); support is BilinearFilter's 1 over the step. */
    const double step = 1.0 / s;
    const double support = 1.0 / step;
    auto bilinear = [](double x) { return std::max(1.0 - std::abs(x), 0.0); };

    AxisFilter out;
    out.taps = std::max(static_cast<int>(std::ceil(support)) * 2, 1);
    /* Output sample zero is enough, by the argument above. Half UP, not half to even: zimg
       needs round(x - 1) == round(x) - 1 to hold on the pixel grid. */
    const double pos = 0.5 * s + shift;
    const double beginPos = std::floor(pos - out.taps / 2.0 + 0.5) + 0.5;
    out.origin = static_cast<int>(std::floor(beginPos));

    double total = 0.0;
    for (int j = 0; j < out.taps; j++)
        total += bilinear((beginPos + j - pos) * step);
    for (int j = 0; j < out.taps; j++)
        out.coeff[j] = static_cast<float>(bilinear((beginPos + j - pos) * step) / total);
    return out;
}

/* One pair per _ChromaLocation, since which one applies is a property of the frame and the
   compute path picks between them with a push constant rather than with six resize nodes. */
struct ChromaFilters {
    AxisFilter h[numChromaLocations];
    AxisFilter v[numChromaLocations];
};

ChromaFilters chromaFiltersFor(int subSamplingW, int subSamplingH) {
    ChromaFilters out;
    for (int loc = 0; loc < numChromaLocations; loc++) {
        double srcLeft, srcTop;
        getChromalocLumaShift(loc, subSamplingW, subSamplingH, &srcLeft, &srcTop);
        out.h[loc] = chromaAxisFilter(subSamplingW, srcLeft);
        out.v[loc] = chromaAxisFilter(subSamplingH, srcTop);
    }
    return out;
}

/* Every chroma location's coefficients, uploaded once, since which one applies is a property of
   the frame and the widest filter would not fit the push constant block anyway. Laid out
   [location][axis][tap]; the kernel is handed the base for the location as a push constant. */
std::vector<uint8_t> chromaCoefficientTable(const ChromaFilters &cf) {
    std::vector<float> table(static_cast<size_t>(numChromaLocations) * 2 * maxChromaTaps, 0.0f);
    for (int loc = 0; loc < numChromaLocations; loc++) {
        float *h = table.data() + (static_cast<size_t>(loc) * 2) * maxChromaTaps;
        float *v = h + maxChromaTaps;
        for (int j = 0; j < cf.h[loc].taps; j++)
            h[j] = cf.h[loc].coeff[j];
        for (int j = 0; j < cf.v[loc].taps; j++)
            v[j] = cf.v[loc].coeff[j];
    }
    std::vector<uint8_t> blob(table.size() * sizeof(float));
    std::memcpy(blob.data(), table.data(), blob.size());
    return blob;
}

/* How a kernel reaches the mask: a name for the plain read, plus the gather when the plane
   being written is smaller than the mask. Assembled per filter because which input carries the
   mask differs -- alpha is PreMultiply's second clip, mask is MaskedMerge's third. */
struct MaskSource {
    std::string prelude;
    std::string intExpr;   /* declares `uint m` */
    std::string floatExpr; /* declares `float mraw`, unclamped: the merge bodies clamp */
};

MaskSource maskSource(int idx, const VSVideoFormat &fmt, bool resamples, const ChromaFilters &cf) {
    const std::string n = std::to_string(idx);
    MaskSource out;
    out.prelude = "#define SRCMASK(xx, yy) SRC" + n + "(xx, yy)\n";
    if (!resamples) {
        out.intExpr = "    uint m = uint(SRCMASK(x, y));\n";
        out.floatExpr = "    float mraw = float(SRCMASK(x, y));\n";
        return out;
    }
    /* Tap counts follow the subsampling alone, so any chroma location's pair will do. */
    /* The mask's own dimensions rather than the plane being written, and derived rather than
       pushed: a subsampled format's dimensions must divide by the factor exactly -- odd sizes
       are rejected outright -- so shifting the chroma extent back up recovers the luma one. */
    out.prelude +=
        "float vsMaskAt(int xx, int yy) {\n"
        "    return float(s" + n + "[uint(vsMirror(yy, int(pc.height) * " + std::to_string(1 << fmt.subSamplingH) + ")) * pc.srcStride[" + n + "]\n"
        "        + uint(vsMirror(xx, int(pc.width) * " + std::to_string(1 << fmt.subSamplingW) + "))]);\n"
        "}\n"
        /* Separable and normalised on each axis, so the outer product is normalised too, and
           one fused pass replaces the scalar path's horizontal then vertical with a quantised
           intermediate between them. The mirroring matches compute_filter's, which reflects the
           tap position and lets coefficients landing on the same sample accumulate -- summing
           over mirrored indices does exactly that. */
        "float vsMaskResampled(int x, int y) {\n"
        "    int bx = " + std::to_string(1 << fmt.subSamplingW) + " * x + int(pc.u[4]);\n"
        "    int by = " + std::to_string(1 << fmt.subSamplingH) + " * y + int(pc.u[5]);\n"
        "    uint ch = pc.u[3];\n"
        "    uint cv = ch + " + std::to_string(maxChromaTaps) + "u;\n"
        "    float acc = 0.0;\n"
        "    for (int jy = 0; jy < " + std::to_string(cf.v[0].taps) + "; jy++) {\n"
        "        float row = 0.0;\n"
        "        for (int jx = 0; jx < " + std::to_string(cf.h[0].taps) + "; jx++)\n"
        "            row += lut0[ch + uint(jx)] * vsMaskAt(bx + jx, by + jy);\n"
        "        acc += lut0[cv + uint(jy)] * row;\n"
        "    }\n"
        "    return acc;\n"
        "}\n";
    /* pc.u[6] is set only on the planes that need it, so luma still reads the mask straight.
       The integer form rounds half up and clamps, which is what pack_pixel_u16 does at the end
       of zimg's own resample; the float form leaves the value alone for the merge to clamp. */
    out.intExpr =
        "    uint m = pc.u[6] != 0u\n"
        "        ? uint(clamp(floor(vsMaskResampled(x, y) + 0.5), 0.0, float(pc.u[0])))\n"
        "        : uint(SRCMASK(x, y));\n";
    out.floatExpr =
        "    float mraw = pc.u[6] != 0u ? vsMaskResampled(x, y) : float(SRCMASK(x, y));\n";
    return out;
}

/* Just the two tap origins per chroma location, which is all the gather needs from the host
   once the coefficients live in the constant buffer -- passing the whole ChromaFilters would
   copy a couple of kilobytes of tables into the callback for the sake of twelve ints. */
struct ChromaOrigins {
    int32_t h[numChromaLocations] = {};
    int32_t v[numChromaLocations] = {};
};

ChromaOrigins originsOf(const ChromaFilters &cf) {
    ChromaOrigins out;
    for (int loc = 0; loc < numChromaLocations; loc++) {
        out.h[loc] = cf.h[loc].origin;
        out.v[loc] = cf.v[loc].origin;
    }
    return out;
}

/* The half of the push constants the gather needs. u[0] to u[2] belong to the caller. */
void fillMaskParams(int plane, int loc, const ChromaOrigins &origins, bool resamples, uint32_t *u) {
    if (!resamples || plane == 0) {
        u[3] = 0;
        u[4] = 0;
        u[5] = 0;
        u[6] = 0;
        return;
    }
    if (loc < 0 || loc >= numChromaLocations)
        loc = VSC_CHROMA_LEFT;
    u[3] = static_cast<uint32_t>(loc) * 2u * maxChromaTaps;
    u[4] = static_cast<uint32_t>(origins.h[loc]);
    u[5] = static_cast<uint32_t>(origins.v[loc]);
    u[6] = 1;
}

} // namespace

//////////////////////////////////////////
// PreMultiply


typedef struct {
    const VSVideoInfo *vi;
    bool chroma_dispatch;  // If true, nodes 2-7 are 6 per-chromaloc resizes
    int cpulevel;
} PreMultiplyDataExtra;

typedef VariableNodeData<PreMultiplyDataExtra> PreMultiplyData;

/* Takes the format rather than the VideoInfo so a compute path can capture it by value; the
   VideoInfo overload is what the scalar paths already had. */
static unsigned getLimitedRangeOffset(const VSFrame *f, const VSVideoFormat &fmt, const VSAPI *vsapi) {
    int err;
    bool limited = (vsapi->mapGetInt(vsapi->getFramePropertiesRO(f), "_Range", 0, &err) == VSC_RANGE_LIMITED);
    if (err)
        limited = (fmt.colorFamily == cfGray || fmt.colorFamily == cfYUV);
    return (limited ? (16 << (fmt.bitsPerSample - 8)) : 0);
}

static unsigned getLimitedRangeOffset(const VSFrame *f, const VSVideoInfo *vi, const VSAPI *vsapi) {
    return getLimitedRangeOffset(f, vi->format, vsapi);
}

static const VSFrame *VS_CC preMultiplyGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    PreMultiplyData *d = reinterpret_cast<PreMultiplyData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        if (d->vi->format.numPlanes > 1 && !d->chroma_dispatch) {
            // No subsampling: reuse alpha for chroma planes (nodes[2] is an alpha alias).
            vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
        }
    } else if (activationReason == arAllFramesReady && d->chroma_dispatch && !frameData[0]) {
        // Defer chroma-resize candidate selection until we've read source frame's _ChromaLocation.
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        int loc = resolveChromaLocation(src1, vsapi);
        vsapi->freeFrame(src1);

        VSNode *chroma_candidate = d->nodes[2 + loc];
        frameData[0] = chroma_candidate;
        vsapi->requestFrameFilter(n, chroma_candidate, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *src2_23 = nullptr;
        if (d->vi->format.numPlanes > 1) {
            VSNode *chroma_node = d->chroma_dispatch
                ? reinterpret_cast<VSNode *>(frameData[0])
                : d->nodes[2];
            src2_23 = vsapi->getFrameFilter(n, chroma_node, frameCtx);
        }
        VSFrame *dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src1, plane);
            ptrdiff_t stride = vsapi->getStride(src1, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(plane > 0 ? src2_23 : src2, 0);
            uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);
            bool yuvhandling = (plane > 0) && (d->vi->format.colorFamily == cfYUV);
            unsigned offset = getLimitedRangeOffset(src1, d->vi, vsapi);
            unsigned depth = d->vi->format.bitsPerSample;

            void (*func)(const void *, const void *, void *, unsigned, unsigned, unsigned) = nullptr;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_half_avx2;
            }
#endif

            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                    func = vs_premultiply_byte_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_word_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                    func = vs_premultiply_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                    func = vs_premultiply_half_c;
            }

            if (!func)
                continue;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, yuvhandling ? (1 << (depth - 1)) : offset, w);

                srcp1 += stride;
                srcp2 += stride;
                dstp += stride;
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        vsapi->freeFrame(src2_23);
        return dst;
    }

    return nullptr;
}

static void VS_CC preMultiplyCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<PreMultiplyData> d(new PreMultiplyData(vsapi));

    d->nodes.resize(2);
    d->nodes[0] = vsapi->mapGetNode(in, "clip", 0, 0);
    d->nodes[1] = vsapi->mapGetNode(in, "alpha", 0, 0);

    d->vi = vsapi->getVideoInfo(d->nodes[0]);

    const VSVideoInfo *alphavi = vsapi->getVideoInfo(d->nodes[1]);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "PreMultiply").c_str());

    if (alphavi->format.colorFamily != cfGray || alphavi->format.sampleType != d->vi->format.sampleType || alphavi->format.bitsPerSample != d->vi->format.bitsPerSample)
        RETERROR("PreMultiply: alpha clip must be grayscale and same sample format and bitdepth as main clip");

    if (!isConstantVideoFormat(d->vi) || !isConstantVideoFormat(alphavi) || d->vi->width != alphavi->width || d->vi->height != alphavi->height)
        RETERROR("PreMultiply: both clips must have the same constant format and dimensions");

    d->cpulevel = vs_get_cpulevel(core);

    bool subsampled = (d->vi->format.numPlanes > 1) && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0);
    d->chroma_dispatch = subsampled;

    const ClipResidencyResult residency = residencyOfClips(d->nodes.data(), 2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("PreMultiply", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        const VSVideoFormat &fmt = d->vi->format;
        const ChromaFilters cf = chromaFiltersFor(fmt.subSamplingW, fmt.subSamplingH);
        const unsigned depth = fmt.bitsPerSample;
        const uint32_t maxval = static_cast<uint32_t>((1ull << depth) - 1u);
        const bool isYUV = fmt.colorFamily == cfYUV;
        const bool isInt = fmt.sampleType == stInteger;
        const MaskSource ms = maskSource(1, fmt, subsampled, cf);

        vsgpu::SimpleFilter sf;
        sf.name = "PreMultiply";
        sf.numInputs = 2;
        sf.srcPlane[1] = 0;
        sf.prelude = std::string(premulPrelude) + ms.prelude;
        sf.bodyInt = ms.intExpr +
            "    STORE(uint(vsPremul(uint(SRC0(x, y)), m, pc.u[1], pc.u[0]) + int(pc.u[1])));";
        /* The scalar float kernel ignores the offset outright: a premultiplied float chroma
           sample is already centred on zero. */
        sf.bodyFloat = ms.floatExpr +
            "    STORE(float(SRC0(x, y)) * mraw);";
        if (subsampled) {
            sf.constants.push_back(chromaCoefficientTable(cf));
            sf.constantType = "float";
        }

        /* The offset on integer, and the chroma location when the alpha is resampled; both are
           properties of the frame rather than of the clip. A float clip with nothing subsampled
           wants neither, and then reading frame properties every frame would buy nothing. */
        const ChromaOrigins origins = originsOf(cf);
        if (isInt || subsampled) {
            const VSVideoFormat capturedFmt = fmt;
            sf.frameParamCount = 2;
            sf.prepareFrame = [capturedFmt, isInt, subsampled](int, const VSFrame *const *sources,
                    int numSources, const VSAPI *api, uint32_t *params, std::string &) {
                params[0] = (isInt && numSources > 0) ? getLimitedRangeOffset(sources[0], capturedFmt, api) : 0;
                params[1] = (subsampled && numSources > 0)
                    ? static_cast<uint32_t>(resolveChromaLocation(sources[0], api)) : 0;
                return true;
            };
            sf.fillParams = [isYUV, depth, maxval, origins, subsampled](
                    int plane, const uint32_t *params, float *, uint32_t *u) {
                u[0] = maxval;
                u[1] = (plane > 0 && isYUV) ? (1u << (depth - 1)) : params[0];
                fillMaskParams(plane, static_cast<int>(params[1]), origins, subsampled, u);
            };
        } else {
            sf.fillParams = [maxval](int, const uint32_t *, float *, uint32_t *u) { u[0] = maxval; };
        }

        createGPUFromVector(d, sf, 2, out, core, vsapi);
        return;
    }

    if (subsampled) {
        VSNode *candidates[numChromaLocations] = {};
        std::string err_msg = createChromaResizeCandidates(d->nodes[1],
            d->vi->width >> d->vi->format.subSamplingW,
            d->vi->height >> d->vi->format.subSamplingH,
            d->vi->format.subSamplingW, d->vi->format.subSamplingH,
            candidates, core, vsapi);
        if (!err_msg.empty())
            RETERROR(("PreMultiply: " + err_msg).c_str());
        d->nodes.resize(2 + numChromaLocations);
        for (int i = 0; i < numChromaLocations; i++)
            d->nodes[2 + i] = candidates[i];
    } else if (d->vi->format.numPlanes > 1) {
        d->nodes.resize(3);
        d->nodes[2] = vsapi->addNodeRef(d->nodes[1]);
    }

    std::vector<VSFilterDependency> deps;
    deps.push_back({ d->nodes[0], rpStrictSpatial });
    deps.push_back({ d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    if (subsampled) {
        for (int i = 0; i < numChromaLocations; i++)
            deps.push_back({ d->nodes[2 + i], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2 + i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    } else if (d->vi->format.numPlanes > 1) {
        deps.push_back({ d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    }

    vsapi->createVideoFilter(out, "PreMultiply", d->vi, preMultiplyGetFrame, filterFree<PreMultiplyData>, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Merge

typedef struct {
    const VSVideoInfo *vi;
    unsigned weight[3];
    float fweight[3];
    int process[3];
    int cpulevel;
} MergeDataExtra;

typedef DualNodeData<MergeDataExtra> MergeData;

const unsigned MergeShift = 15;

static const VSFrame *VS_CC mergeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeData *d = reinterpret_cast<MergeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = {0, 1, 2};
        const VSFrame *fs[] = { 0, src1, src2 };
        const VSFrame *fr[] = {fs[d->process[0]], fs[d->process[1]], fs[d->process[2]]};
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane] == 0) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, union vs_merge_weight, unsigned) = 0;
                union vs_merge_weight weight;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_half_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_merge_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_merge_float_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_merge_half_c;
                }

                if (!func)
                    continue;

                if (d->vi->format.sampleType == stInteger)
                    weight.u = d->weight[plane];
                else
                    weight.f = d->fweight[plane];

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, weight, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC mergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeData> d(new MergeData(vsapi));

    int nweight = vsapi->mapNumElements(in, "weight");
    for (int i = 0; i < 3; i++)
        d->fweight[i] = 0.5f;
    for (int i = 0; i < nweight; i++)
        d->fweight[i] = (float)vsapi->mapGetFloat(in, "weight", i, 0);

    if (nweight == 2) {
        d->fweight[2] = d->fweight[1];
    } else if (nweight == 1) {
        d->fweight[1] = d->fweight[0];
        d->fweight[2] = d->fweight[0];
    }

    for (int i = 0; i < 3; i++) {
        if (!(d->fweight[i] >= 0 && d->fweight[i] <= 1))
            RETERROR("Merge: weights must be between 0 and 1");
        d->weight[i] = std::min<unsigned>((d->fweight[i] * (1 << MergeShift) + 0.5f), (1U << MergeShift) - 1);
    }

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    for (int i = 0; i < 3; i++) {
        d->process[i] = 0;
        if (d->vi->format.sampleType == stInteger) {
            if (d->weight[i] == 0)
                d->process[i] = 1;
            else if (d->fweight[i] == 1.0f)
                d->process[i] = 2;
        } else if (d->vi->format.sampleType == stFloat) {
            if (d->fweight[i] == 0.0f)
                d->process[i] = 1;
            else if (d->fweight[i] == 1.0f)
                d->process[i] = 2;
        }
    }

    d->cpulevel = vs_get_cpulevel(core);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "Merge").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("Merge: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (nweight > d->vi->format.numPlanes)
        RETERROR("Merge: more weights given than the number of planes to merge");

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("Merge", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "Merge";
        sf.numInputs = 2;
        /* The integer form is deliberately modular: the scalar kernel subtracts in
           unsigned, so v2 < v1 wraps and the shift brings it back. uint does the same. */
        sf.bodyInt =
            "    uint v1 = uint(SRC0(x, y));\n"
            "    uint v2 = uint(SRC1(x, y));\n"
            "    STORE(v1 + (((v2 - v1) * pc.u[0] + " + std::to_string(1u << (MergeShift - 1)) +
            "u) >> " + std::to_string(MergeShift) + "));";
        sf.bodyFloat =
            "    float v1 = float(SRC0(x, y));\n"
            "    float v2 = float(SRC1(x, y));\n"
            "    STORE(v1 + (v2 - v1) * pc.f[0]);";
        std::array<unsigned, 3> w;
        std::array<float, 3> wf;
        for (int p = 0; p < 3; p++) {
            w[p] = d->weight[p];
            wf[p] = d->fweight[p];
        }
        for (int p = 0; p < 3; p++) {
            sf.process[p] = d->process[p] == 0;
            sf.shareClip[p] = d->process[p] == 2 ? 1 : 0;
        }
        sf.fillParams = [w, wf](int plane, const uint32_t *, float *f, uint32_t *u) {
            u[0] = w[plane];
            f[0] = wf[plane];
        };
        createGPUFromDecl2(d, sf, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "Merge", d->vi, mergeGetFrame, filterFree<MergeData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MaskedMerge

typedef struct {
    const VSVideoInfo *vi;
    bool premultiplied;
    bool first_plane;
    bool process[3];
    int cpulevel;
    bool chroma_dispatch;   // If true, nodes 3-8 are 6 per-chromaloc resizes of mask
} MaskedMergeDataExtra;

typedef VariableNodeData<MaskedMergeDataExtra> MaskedMergeData;

static const VSFrame *VS_CC maskedMergeGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MaskedMergeData *d = reinterpret_cast<MaskedMergeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->nodes[0], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[1], frameCtx);
        vsapi->requestFrameFilter(n, d->nodes[2], frameCtx);
    } else if (activationReason == arAllFramesReady && d->chroma_dispatch && !frameData[0]) {
        // Defer chroma-resize selection until we've read source frame's _ChromaLocation.
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        int loc = resolveChromaLocation(src1, vsapi);
        vsapi->freeFrame(src1);

        VSNode *chroma_candidate = d->nodes[3 + loc];
        frameData[0] = chroma_candidate;
        vsapi->requestFrameFilter(n, chroma_candidate, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->nodes[0], frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->nodes[1], frameCtx);
        const VSFrame *mask = vsapi->getFrameFilter(n, d->nodes[2], frameCtx);
        const VSFrame *mask23 = nullptr;

        // With subsampled chroma, clipa and clipb chroma must sample the same
        // locations to be meaningfully merged, only relevant when chroma is processed.
        if ((d->process[1] || d->process[2]) && (d->vi->format.subSamplingW > 0 || d->vi->format.subSamplingH > 0)) {
            int loc1 = resolveChromaLocation(src1, vsapi);
            int loc2 = resolveChromaLocation(src2, vsapi);
            if (loc1 != loc2) {
                vsapi->freeFrame(src1);
                vsapi->freeFrame(src2);
                vsapi->freeFrame(mask);
                vsapi->setFilterError(("MaskedMerge: clipa and clipb have different chroma locations (_ChromaLocation "
                    + std::to_string(loc1) + " vs " + std::to_string(loc2) + ")").c_str(), frameCtx);
                return nullptr;
            }
        }

        unsigned offset1 = getLimitedRangeOffset(src1, d->vi, vsapi);
        unsigned offset2 = getLimitedRangeOffset(src2, d->vi, vsapi);

        const int pl[] = {0, 1, 2};
        const VSFrame *fr[] = {d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1};
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        if (d->chroma_dispatch)
            mask23 = vsapi->getFrameFilter(n, reinterpret_cast<VSNode *>(frameData[0]), frameCtx);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                const uint8_t *maskp = vsapi->getReadPtr((plane && mask23) ? mask23 : mask, d->first_plane ? 0 : plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, const void *, void *, unsigned, unsigned, unsigned) = 0;
                int yuvhandling = (plane > 0) && (d->vi->format.colorFamily == cfYUV);

                if (d->premultiplied && d->vi->format.sampleType == stInteger && offset1 != offset2) {
                    vsapi->freeFrame(src1);
                    vsapi->freeFrame(src2);
                    vsapi->freeFrame(mask);
                    vsapi->freeFrame(mask23);
                    vsapi->freeFrame(dst);
                    vsapi->setFilterError("MaskedMerge: Input frames must have the same range", frameCtx);
                    return nullptr;
                }

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx512 && d->cpulevel >= VS_CPU_LEVEL_AVX512) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_avx512 : vs_mask_merge_byte_avx512;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_avx512 : vs_mask_merge_word_avx512;
                }
                if (!func && getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_avx2 : vs_mask_merge_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_avx2 : vs_mask_merge_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_avx2 : vs_mask_merge_float_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_half_avx2 : vs_mask_merge_half_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_sse2 : vs_mask_merge_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_sse2 : vs_mask_merge_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_sse2 : vs_mask_merge_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = d->premultiplied ? vs_mask_merge_premul_byte_c : vs_mask_merge_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_word_c : vs_mask_merge_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = d->premultiplied ? vs_mask_merge_premul_float_c : vs_mask_merge_float_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = d->premultiplied ? vs_mask_merge_premul_half_c : vs_mask_merge_half_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; y++) {
                    func(srcp1, srcp2, maskp, dstp, depth, yuvhandling ? (1 << (depth - 1)) : offset1, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    maskp += stride;
                    dstp += stride;
                }
            }
        }
        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        vsapi->freeFrame(mask);
        vsapi->freeFrame(mask23);
        return dst;
    }

    return nullptr;
}

static void VS_CC maskedMergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MaskedMergeData> d(new MaskedMergeData(vsapi));

    d->nodes.resize(3);

    int err;
    d->nodes[0] = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->nodes[1] = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->nodes[2] = vsapi->mapGetNode(in, "mask", 0, 0);
    d->vi = vsapi->getVideoInfo(d->nodes[0]);
    const VSVideoInfo *maskvi = vsapi->getVideoInfo(d->nodes[2]);
    d->first_plane = !!vsapi->mapGetInt(in, "first_plane", 0, &err);
    d->premultiplied = !!vsapi->mapGetInt(in, "premultiplied", 0, &err);
    // always use the first mask plane for all planes when it is the only one
    if (maskvi->format.numPlanes == 1)
        d->first_plane = 1;

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MaskedMerge").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->nodes[1])))
        RETERROR(("MaskedMerge: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->nodes[1]), vsapi)).c_str());

    if (maskvi->width != d->vi->width || maskvi->height != d->vi->height || maskvi->format.bitsPerSample != d->vi->format.bitsPerSample
        || (!isSameVideoFormat(&maskvi->format, &d->vi->format) && maskvi->format.colorFamily != cfGray && !d->first_plane))
        RETERROR(("MaskedMerge: mask clip must have same dimensions as main clip and be the same format or grayscale, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(maskvi, vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MaskedMerge", d->process, vsapi))
        return;

    // Do we need to resample the first mask plane and use it for the chroma planes?
    bool need_chroma_resize = (d->first_plane && d->vi->format.numPlanes > 1)
                              && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0)
                              && (d->process[1] || d->process[2]);
    d->chroma_dispatch = need_chroma_resize;

    const ClipResidencyResult maskedResidency = residencyOfClips(d->nodes.data(), 3, vsapi);
    if (maskedResidency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MaskedMerge", maskedResidency.mixedAt).c_str());

    if (maskedResidency.kind == ClipResidency::AllGPU) {
        const VSVideoFormat &fmt = d->vi->format;
        const ChromaFilters cf = chromaFiltersFor(fmt.subSamplingW, fmt.subSamplingH);
        const unsigned depth = fmt.bitsPerSample;
        const uint32_t maxval = static_cast<uint32_t>((1ull << depth) - 1u);
        const bool isYUV = fmt.colorFamily == cfYUV;
        const bool isInt = fmt.sampleType == stInteger;
        const bool premul = d->premultiplied;

        vsgpu::SimpleFilter sf;
        sf.name = "MaskedMerge";
        sf.numInputs = 3;
        /* The mask is read as the OUTPUT's sample type, not its own -- which is what the
           scalar kernels do, casting the mask pointer to the same type as the sources. */
        sf.srcPlane[2] = d->first_plane ? 0 : -1;
        const MaskSource ms = maskSource(2, fmt, need_chroma_resize, cf);
        /* vsPremul is only reached from the premultiplied bodies. */
        sf.prelude = (premul ? std::string(premulPrelude) : std::string()) + ms.prelude;
        for (int p = 0; p < 3; p++)
            sf.process[p] = d->process[p];
        if (need_chroma_resize) {
            sf.constants.push_back(chromaCoefficientTable(cf));
            sf.constantType = "float";
        }

        /* u[2] is the storage width, not maxval: the scalar kernels hold invmask in the
           sample type, so a mask sample above maxval -- which nothing forbids at a depth
           the type has room above -- wraps there and has to wrap identically here. A
           resampled mask is already clamped, so this only bites on the direct read. */
        if (premul) {
            /* Premultiplied clamps where the plain form cannot go out of range at all:
               clipb is already scaled, so the sum can leave the storage range. */
            sf.bodyInt = ms.intExpr +
                "    uint inv = (pc.u[0] - m) & pc.u[2];\n"
                "    int v = vsPremul(uint(SRC0(x, y)), inv, pc.u[1], pc.u[0]) + int(uint(SRC1(x, y)));\n"
                "    STORE(uint(clamp(v, 0, int(pc.u[0]))));";
            sf.bodyFloat = ms.floatExpr +
                "    float m = clamp(mraw, 0.0, 1.0);\n"
                "    STORE((1.0 - m) * float(SRC0(x, y)) + float(SRC1(x, y)));";
        } else {
            sf.bodyInt = ms.intExpr +
                "    uint inv = (pc.u[0] - m) & pc.u[2];\n"
                "    STORE((inv * uint(SRC0(x, y)) + m * uint(SRC1(x, y)) + pc.u[0] / 2u) / pc.u[0]);";
            sf.bodyFloat = ms.floatExpr +
                "    float m = clamp(mraw, 0.0, 1.0);\n"
                "    float v1 = float(SRC0(x, y));\n"
                "    STORE(v1 + (float(SRC1(x, y)) - v1) * m);";
        }
        const uint32_t storageMask = fmt.bytesPerSample == 1 ? 0xFFu : 0xFFFFu;

        /* Two per frame checks the scalar path makes and the compute path must keep: the
           two inputs have to agree on chroma siting before their chroma can be merged at
           all, and premultiplied integer needs them to agree on black level too, since
           one offset is applied to both. */
        const bool checkLoc = (d->process[1] || d->process[2]) &&
            (fmt.subSamplingW > 0 || fmt.subSamplingH > 0);
        /* The scalar path makes the range check inside its per plane loop, so a call that
           processes nothing never reaches it; gated on the same condition here because
           prepareFrame runs ahead of the driver's no-work shortcut and would otherwise
           fail a frame the CPU path hands back untouched. Bounded by numPlanes for the
           same reason that loop is: the planes argument fills all three flags. */
        bool anyProcessed = false;
        for (int p = 0; p < fmt.numPlanes; p++)
            anyProcessed = anyProcessed || d->process[p];
        const bool checkRange = premul && isInt && anyProcessed;

        /* Resampling always implies checkLoc -- both want subsampling and chroma being
           processed -- so the chroma location the gather needs is already being read and
           already known to agree between the two inputs. */
        const ChromaOrigins origins = originsOf(cf);
        if (checkLoc || checkRange) {
            const VSVideoFormat capturedFmt = fmt;
            sf.frameParamCount = 2;
            sf.prepareFrame = [capturedFmt, checkLoc, checkRange](int, const VSFrame *const *sources,
                    int numSources, const VSAPI *api, uint32_t *params, std::string &error) {
                if (numSources < 2) {
                    error = "MaskedMerge: missing source frames";
                    return false;
                }
                params[1] = 0;
                if (checkLoc) {
                    int loc1 = resolveChromaLocation(sources[0], api);
                    int loc2 = resolveChromaLocation(sources[1], api);
                    if (loc1 != loc2) {
                        error = "MaskedMerge: clipa and clipb have different chroma locations (_ChromaLocation "
                            + std::to_string(loc1) + " vs " + std::to_string(loc2) + ")";
                        return false;
                    }
                    params[1] = static_cast<uint32_t>(loc1);
                }
                unsigned offset1 = getLimitedRangeOffset(sources[0], capturedFmt, api);
                if (checkRange && offset1 != getLimitedRangeOffset(sources[1], capturedFmt, api)) {
                    error = "MaskedMerge: Input frames must have the same range";
                    return false;
                }
                params[0] = offset1;
                return true;
            };
            sf.fillParams = [isYUV, depth, maxval, storageMask, origins, need_chroma_resize](
                    int plane, const uint32_t *params, float *, uint32_t *u) {
                u[0] = maxval;
                u[1] = (plane > 0 && isYUV) ? (1u << (depth - 1)) : params[0];
                u[2] = storageMask;
                fillMaskParams(plane, static_cast<int>(params[1]), origins, need_chroma_resize, u);
            };
        } else {
            /* Never resamples: reaching here needs no chroma processed or no subsampling. */
            sf.fillParams = [maxval, storageMask](int, const uint32_t *, float *, uint32_t *u) {
                u[0] = maxval;
                u[2] = storageMask;
            };
        }

        createGPUFromVector(d, sf, 3, out, core, vsapi);
        return;
    }

    if (need_chroma_resize) {
        // If the mask has more than 1 plane, extract plane 0 first so the
        // resize candidates only resample the plane we'll read.
        VSNode *single_plane_mask = nullptr;
        if (maskvi->format.numPlanes > 1) {
            VSMap *min = vsapi->createMap();
            vsapi->mapSetNode(min, "clips", d->nodes[2], maAppend);
            vsapi->mapSetInt(min, "planes", 0, maAppend);
            vsapi->mapSetInt(min, "colorfamily", cfGray, maAppend);
            VSMap *mout = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShufflePlanes", min);
            vsapi->freeMap(min);
            const char *shuffle_err = vsapi->mapGetError(mout);
            if (shuffle_err) {
                std::string msg = std::string("MaskedMerge: ShufflePlanes failed: ") + shuffle_err;
                vsapi->freeMap(mout);
                RETERROR(msg.c_str());
            }
            single_plane_mask = vsapi->mapGetNode(mout, "clip", 0, 0);
            vsapi->freeMap(mout);
        } else {
            single_plane_mask = vsapi->addNodeRef(d->nodes[2]);
        }

        VSNode *candidates[numChromaLocations] = {};
        std::string err_msg = createChromaResizeCandidates(single_plane_mask,
            d->vi->width >> d->vi->format.subSamplingW,
            d->vi->height >> d->vi->format.subSamplingH,
            d->vi->format.subSamplingW, d->vi->format.subSamplingH,
            candidates, core, vsapi);
        vsapi->freeNode(single_plane_mask);
        if (!err_msg.empty())
            RETERROR(("MaskedMerge: " + err_msg).c_str());

        d->nodes.resize(3 + numChromaLocations);
        for (int i = 0; i < numChromaLocations; i++)
            d->nodes[3 + i] = candidates[i];
    }

    d->cpulevel = vs_get_cpulevel(core);

    std::vector<VSFilterDependency> deps;
    deps.push_back({ d->nodes[0], rpStrictSpatial });
    deps.push_back({ d->nodes[1], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[1])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    deps.push_back({ d->nodes[2], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[2])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    if (need_chroma_resize) {
        for (int i = 0; i < numChromaLocations; i++)
            deps.push_back({ d->nodes[3 + i], (d->vi->numFrames <= vsapi->getVideoInfo(d->nodes[3 + i])->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly });
    }

    vsapi->createVideoFilter(out, "MaskedMerge", d->vi, maskedMergeGetFrame, filterFree<MaskedMergeData>, fmParallel, deps.data(), static_cast<int>(deps.size()), d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MakeDiff

typedef struct {
    const VSVideoInfo *vi;
    bool process[3];
    int cpulevel;
} MakeDiffDataExtra;

typedef DualNodeData<MakeDiffDataExtra> MakeDiffData;

static const VSFrame *VS_CC makeDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MakeDiffData *d = reinterpret_cast<MakeDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1 };
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src2, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_half_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_makediff_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_makediff_float_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_makediff_half_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, depth, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC makeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MakeDiffData> d(new MakeDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MakeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MakeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MakeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MakeDiff", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "MakeDiff";
        sf.numInputs = 2;
        sf.bodyInt =
            "    int tmp = int(SRC0(x, y)) - int(SRC1(x, y)) + int(pc.u[1]);\n"
            "    STORE(uint(clamp(tmp, 0, int(pc.u[0]))));";
        sf.bodyFloat = "    STORE(float(SRC0(x, y)) - float(SRC1(x, y)));";
        const uint32_t maxval = static_cast<uint32_t>((1ull << d->vi->format.bitsPerSample) - 1);
        const uint32_t half = 1u << (d->vi->format.bitsPerSample - 1);
        for (int p = 0; p < 3; p++)
            sf.process[p] = d->process[p];
        sf.fillParams = [maxval, half](int, const uint32_t *, float *, uint32_t *u) { u[0] = maxval; u[1] = half; };
        createGPUFromDecl2(d, sf, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MakeDiff", d->vi, makeDiffGetFrame, filterFree<MakeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MakeFullDiff

typedef struct {
    const VSVideoInfo *vi;
    VSVideoInfo outvi;
    int cpulevel;
} MakeFullDiffDataExtra;

typedef DualNodeData<MakeFullDiffDataExtra> MakeFullDiffData;

static const VSFrame *VS_CC makeFullDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MakeFullDiffData *d = reinterpret_cast<MakeFullDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->outvi.format, d->outvi.width, d->outvi.height, src1, core);
        for (int plane = 0; plane < d->outvi.format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src2, plane);
            ptrdiff_t srcstride = vsapi->getStride(src1, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
            uint8_t *VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

            void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_avx2;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_makediff_half_avx2;
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_sse2;
            }
#endif

            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 8)
                    func = vs_makefulldiff_byte_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample < 16)
                    func = vs_makefulldiff_word_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 16)
                    func = vs_makefulldiff_word_dword_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_makediff_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_makediff_half_c;
            }

            if (!func)
                continue;

            int depth = d->vi->format.bitsPerSample;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, w);
                srcp1 += srcstride;
                srcp2 += srcstride;
                dstp += dststride;
            }

        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC makeFullDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MakeFullDiffData> d(new MakeFullDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MakeFullDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MakeFullDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    d->outvi = *d->vi;
    if (d->outvi.format.sampleType == stInteger) {
        d->outvi.format.bitsPerSample++;
        d->outvi.format.bytesPerSample = (d->outvi.format.bitsPerSample > 16) ? 4 : 2;
    }

    d->cpulevel = vs_get_cpulevel(core);

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MakeFullDiff", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "MakeFullDiff";
        sf.numInputs = 2;
        /* Both inputs are the narrow format; the output is one bit wider, so the source and
           destination types differ and each is declared separately. */
        sf.srcFormat = &d->vi->format;
        sf.bodyInt = "    STORE(uint(int(SRC0(x, y)) - int(SRC1(x, y)) + int(pc.u[0])));";
        /* Float neither widens nor biases, so the body is MakeDiff's float body; the scalar
           path routes float through vs_makediff_float/half for the same reason. */
        sf.bodyFloat = "    STORE(float(SRC0(x, y)) - float(SRC1(x, y)));";
        /* Shifted in 64 bits because a 32 bit float clip reaches here too and would otherwise
           shift a uint32 by its own width; the value is unused on the float path. */
        const uint32_t half = static_cast<uint32_t>(1ull << d->vi->format.bitsPerSample);
        sf.fillParams = [half](int, const uint32_t *, float *, uint32_t *u) { u[0] = half; };
        createGPUFromDecl2(d, sf, out, core, vsapi, &d->outvi);
        return;
    }

    VSFilterDependency deps[] = { {d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MakeFullDiff", &d->outvi, makeFullDiffGetFrame, filterFree<MakeFullDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MergeDiff

struct MergeDiffDataExtra {
    const VSVideoInfo *vi;
    bool process[3];
    int cpulevel;
};

typedef DualNodeData<MergeDiffDataExtra> MergeDiffData;

static const VSFrame *VS_CC mergeDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeDiffData *d = reinterpret_cast<MergeDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? 0 : src1, d->process[1] ? 0 : src1, d->process[2] ? 0 : src1 };
        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (d->process[plane]) {
                int h = vsapi->getFrameHeight(src1, plane);
                int w = vsapi->getFrameWidth(src1, plane);
                ptrdiff_t stride = vsapi->getStride(src1, plane);
                const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
                const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
                uint8_t * VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

                void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
                if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_avx2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_avx2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_half_avx2;
                }
                if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_sse2;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_sse2;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_sse2;
                }
#endif
                if (!func) {
                    if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1)
                        func = vs_mergediff_byte_c;
                    else if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_word_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4)
                        func = vs_mergediff_float_c;
                    else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 2)
                        func = vs_mergediff_half_c;
                }

                if (!func)
                    continue;

                int depth = d->vi->format.bitsPerSample;

                for (int y = 0; y < h; ++y) {
                    func(srcp1, srcp2, dstp, depth, w);
                    srcp1 += stride;
                    srcp2 += stride;
                    dstp += stride;
                }
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static void VS_CC mergeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeDiffData> d(new MergeDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MergeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MergeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MergeDiff", d->process, vsapi))
        return;

    d->cpulevel = vs_get_cpulevel(core);

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MergeDiff", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "MergeDiff";
        sf.numInputs = 2;
        sf.bodyInt =
            "    int tmp = int(SRC0(x, y)) + int(SRC1(x, y)) - int(pc.u[1]);\n"
            "    STORE(uint(clamp(tmp, 0, int(pc.u[0]))));";
        sf.bodyFloat = "    STORE(float(SRC0(x, y)) + float(SRC1(x, y)));";
        const uint32_t maxval = static_cast<uint32_t>((1ull << d->vi->format.bitsPerSample) - 1);
        const uint32_t half = 1u << (d->vi->format.bitsPerSample - 1);
        for (int p = 0; p < 3; p++)
            sf.process[p] = d->process[p];
        sf.fillParams = [maxval, half](int, const uint32_t *, float *, uint32_t *u) { u[0] = maxval; u[1] = half; };
        createGPUFromDecl2(d, sf, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = {{d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly}};
    vsapi->createVideoFilter(out, "MergeDiff", d->vi, mergeDiffGetFrame, filterFree<MergeDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// MergeFullDiff

struct MergeFullDiffDataExtra {
    const VSVideoInfo *vi;
    int cpulevel;
};

typedef DualNodeData<MergeFullDiffDataExtra> MergeFullDiffData;

static const VSFrame *VS_CC mergeFullDiffGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    MergeFullDiffData *d = reinterpret_cast<MergeFullDiffData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
        VSFrame *dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src1, core);
        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            int h = vsapi->getFrameHeight(src1, plane);
            int w = vsapi->getFrameWidth(src1, plane);
            ptrdiff_t src1stride = vsapi->getStride(src1, plane);
            ptrdiff_t src2stride = vsapi->getStride(src2, plane);
            ptrdiff_t dststride = vsapi->getStride(dst, plane);
            const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
            const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
            uint8_t *VS_RESTRICT dstp = vsapi->getWritePtr(dst, plane);

            void (*func)(const void *, const void *, void *, unsigned, unsigned) = 0;

#ifdef VS_TARGET_CPU_X86
            if (getCPUFeatures()->avx2 && d->cpulevel >= VS_CPU_LEVEL_AVX2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_avx2;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_mergediff_half_avx2;
            }
            if (!func && d->cpulevel >= VS_CPU_LEVEL_SSE2) {
                if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_sse2;
            }
#endif
            if (!func) {
                if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 8)
                    func = vs_mergefulldiff_word_byte_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample < 16)
                    func = vs_mergefulldiff_word_word_c;
                else if (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample == 16)
                    func = vs_mergefulldiff_dword_word_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 32)
                    func = vs_mergediff_float_c;
                else if (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample == 16)
                    func = vs_mergediff_half_c;
            }

            if (!func)
                continue;

            int depth = d->vi->format.bitsPerSample;

            for (int y = 0; y < h; ++y) {
                func(srcp1, srcp2, dstp, depth, w);
                srcp1 += src1stride;
                srcp2 += src2stride;
                dstp += dststride;
            }
        }

        vsapi->freeFrame(src1);
        vsapi->freeFrame(src2);
        return dst;
    }

    return nullptr;
}

static bool mergeFullDiffIsCompatibleVideoInfo(const VSVideoInfo *v1, const VSVideoInfo *v2) {
    return v1->height == v2->height && v1->width == v2->width && v1->format.colorFamily == v2->format.colorFamily && v1->format.sampleType == v2->format.sampleType && v1->format.bitsPerSample == v2->format.bitsPerSample - ((v1->format.sampleType == stInteger) ? 1 : 0) && v1->format.subSamplingW == v2->format.subSamplingW && v1->format.subSamplingH == v2->format.subSamplingH;
}

static void VS_CC mergeFullDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeFullDiffData> d(new MergeFullDiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MergeFullDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !mergeFullDiffIsCompatibleVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MergeFullDiff: both clips must have the same (bitdepth+1 for second clip) constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    d->cpulevel = vs_get_cpulevel(core);

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MergeFullDiff", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        vsgpu::SimpleFilter sf;
        sf.name = "MergeFullDiff";
        sf.numInputs = 2;
        /* clipa is the narrow original and clipb the wider diff, so the two inputs carry
           different sample types -- the case srcFormats exists for. */
        sf.srcFormats[0] = &d->vi->format;
        sf.srcFormats[1] = &vsapi->getVideoInfo(d->node2)->format;
        sf.bodyInt = "    int tmp = int(SRC0(x, y)) + int(SRC1(x, y)) - int(pc.u[0]);\n"
                     "    STORE(uint(clamp(tmp, 0, int(pc.u[1]))));";
        /* Float neither widens, biases nor clamps, so the body is MergeDiff's float body. */
        sf.bodyFloat = "    STORE(float(SRC0(x, y)) + float(SRC1(x, y)));";
        /* 64 bit shift for the same reason as MakeFullDiff; both are unused on the float path. */
        const uint32_t half = static_cast<uint32_t>(1ull << d->vi->format.bitsPerSample);
        const uint32_t maxval = static_cast<uint32_t>((1ull << d->vi->format.bitsPerSample) - 1);
        sf.fillParams = [half, maxval](int, const uint32_t *, float *, uint32_t *u) { u[0] = half; u[1] = maxval; };
        createGPUFromDecl2(d, sf, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = { {d->node1, rpStrictSpatial}, {d->node2, (d->vi->numFrames <= vsapi->getVideoInfo(d->node2)->numFrames) ? rpStrictSpatial : rpFrameReuseLastOnly} };
    vsapi->createVideoFilter(out, "MergeFullDiff", d->vi, mergeFullDiffGetFrame, filterFree<MergeFullDiffData>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

void mergeInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("PreMultiply", "clip:vnode:all;alpha:vnode:all;", "clip:vnode:all;", preMultiplyCreate, 0, plugin);
    vspapi->registerFunction("Merge", "clipa:vnode:all;clipb:vnode:all;weight:float[]:opt;", "clip:vnode:all;", mergeCreate, 0, plugin);
    vspapi->registerFunction("MaskedMerge", "clipa:vnode:all;clipb:vnode:all;mask:vnode:all;planes:int[]:opt;first_plane:int:opt;premultiplied:int:opt;", "clip:vnode:all;", maskedMergeCreate, 0, plugin);
    vspapi->registerFunction("MakeDiff", "clipa:vnode:all;clipb:vnode:all;planes:int[]:opt;", "clip:vnode:all;", makeDiffCreate, 0, plugin);
    vspapi->registerFunction("MakeFullDiff", "clipa:vnode:all;clipb:vnode:all;", "clip:vnode:all;", makeFullDiffCreate, 0, plugin);
    vspapi->registerFunction("MergeDiff", "clipa:vnode:all;clipb:vnode:all;planes:int[]:opt;", "clip:vnode:all;", mergeDiffCreate, 0, plugin);
    vspapi->registerFunction("MergeFullDiff", "clipa:vnode:all;clipb:vnode:all;", "clip:vnode:all;", mergeFullDiffCreate, 0, plugin);
}
