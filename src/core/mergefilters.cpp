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
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <algorithm>
#include "cpufeatures.h"
#include "filtershared.h"
#include "float16_helper.h"
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

static int resolveChromaLocation(const VSMap *props, const VSAPI *vsapi) {
    int err;
    int64_t raw = vsapi->mapGetInt(props, "_ChromaLocation", 0, &err);
    if (err || raw < VSC_CHROMA_LEFT || raw > VSC_CHROMA_BOTTOM)
        return VSC_CHROMA_LEFT;
    return static_cast<int>(raw);
}

static int resolveChromaLocation(const VSFrame *frame, const VSAPI *vsapi) {
    return resolveChromaLocation(vsapi->getFramePropertiesRO(frame), vsapi);
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

/* One pair per _ChromaLocation, since which one applies is a property of the frame: the compute
   path picks between them with a push constant, the tiled path in beginFrame. */
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
        /* Separable and normalised on each axis, so the outer product is normalised too: one
           fused pass in float, rounded once, which MaskResampler matches on the CPU with exact
           integer sums. The mirroring matches compute_filter's, which reflects the tap position
           and lets coefficients landing on the same sample accumulate -- summing over mirrored
           indices does exactly that. */
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

/* The CPU's half of the same resample, for the tiled paths: a mask the size of the luma plane read
   for a subsampled chroma plane from the mask's whole first plane, which the core hands to every
   plane (VSTiledFilterDesc::firstPlaneInputs). Each axis is chromaAxisFilter's filter, the one the
   compute path uses, whose coefficients are exact binary fractions over a power of two: integer
   masks are summed exactly over both axes and rounded once, half up, which is what the compute
   path does in float, and float masks are summed in float. Rows and columns past an edge mirror the
   way zimg's compute_filter does. */
struct MaskAxis {
    int factor = 1; /* mask samples per output sample */
    int origin = 0; /* the first tap, relative to factor times the output index */
    int taps = 1;
    int shift = 0; /* the integer weights add up to 1 << shift */
    uint32_t weight[maxChromaTaps] = { 1 };
    float fweight[maxChromaTaps] = { 1.0f };
};

/* f, the filter for one axis subsampled by 2^subsampling, without its zero taps and with the
   integer weights its coefficients scale to. */
MaskAxis maskAxis(const AxisFilter &f, int subsampling) {
    MaskAxis out;
    out.factor = 1 << subsampling;
    int first = 0;
    int last = f.taps - 1;
    while (first < last && f.coeff[first] == 0.0f)
        first++;
    while (last > first && f.coeff[last] == 0.0f)
        last--;
    out.origin = f.origin + first;
    out.taps = last - first + 1;
    /* The smallest power of two that makes every weight an integer: the shifts are multiples of a
       quarter, which keeps it below 2^11 at the largest factor. */
    for (out.shift = 0; out.shift < 24; out.shift++) {
        bool integral = true;
        for (int j = first; j <= last; j++) {
            const float scaled = std::ldexp(f.coeff[j], out.shift);
            integral = integral && scaled == std::floor(scaled);
        }
        if (integral)
            break;
    }
    for (int j = 0; j < out.taps; j++) {
        out.weight[j] = static_cast<uint32_t>(std::ldexp(f.coeff[first + j], out.shift));
        out.fweight[j] = f.coeff[first + j];
    }
    return out;
}

/* How the rows of a mask frame are read, which its properties say the way resize reads them
   (import_frame_props in vsresize.cpp): _Field makes the frame one field of that parity, else
   _FieldBased 1 or 2 makes it two fields resampled apart, the top one being the even rows, and
   anything else one picture. The first three index MaskResampler::v, and mrFields takes the top or
   bottom field's from the row's parity. */
enum MaskRows { mrFrame, mrTopField, mrBottomField, mrFields };

struct MaskResampler {
    bool active = false; /* processed chroma planes are subsampled and read the first mask plane */
    bool wide = false; /* integer sums can pass 32 bits */
    MaskAxis h[numChromaLocations];
    /* Per chroma location: the frame, a top and a bottom field. A field is sited the way resize
       sites a field's chroma (chroma_offset_parity_adjustment in zimg's graph builder): half the
       frame's shift, moved by a quarter of a field line times one less than the factor, the top
       field up and the bottom one down. A mask that is one field (_Field) is sited like each field
       of an interlaced one. The frame path this replaced resized the mask itself and added the
       field's offset to the frame's whole shift rather than half of it, so with top or bottom
       siting it put a single field's chroma elsewhere. */
    MaskAxis v[numChromaLocations][3];
};

MaskResampler maskResampler(const VSVideoFormat &fmt, bool active) {
    MaskResampler out;
    out.active = active;
    if (!active)
        return out;
    /* The frame's filters are the compute path's; only the fields' are the CPU's own. */
    const ChromaFilters frame = chromaFiltersFor(fmt.subSamplingW, fmt.subSamplingH);
    const double field = 0.25 * (1 - (1 << fmt.subSamplingH));
    for (int loc = 0; loc < numChromaLocations; loc++) {
        double srcLeft, srcTop;
        getChromalocLumaShift(loc, fmt.subSamplingW, fmt.subSamplingH, &srcLeft, &srcTop);
        out.h[loc] = maskAxis(frame.h[loc], fmt.subSamplingW);
        out.v[loc][mrFrame] = maskAxis(frame.v[loc], fmt.subSamplingH);
        out.v[loc][mrTopField] = maskAxis(chromaAxisFilter(fmt.subSamplingH, srcTop / 2 + field), fmt.subSamplingH);
        out.v[loc][mrBottomField] = maskAxis(chromaAxisFilter(fmt.subSamplingH, srcTop / 2 - field), fmt.subSamplingH);
        for (const MaskAxis &v : out.v[loc])
            out.wide = out.wide || 8 * fmt.bytesPerSample + out.h[loc].shift + v.shift > 31;
    }
    return out;
}

/* Reads the mask's MaskRows from its frame properties; what = "mask" or "alpha" for the message.
   Without vertical subsampling a field's chroma rows are the frame's own, one tap each, so any
   arrangement of fields reads as a frame. With it, two fields need an even number of chroma rows,
   so that every field has its own whole rows. */
bool maskRowsOf(const VSMap *props, const VSVideoInfo *vi, const char *what, int &rows, char *errorMessage, int errorMessageSize, const VSAPI *vsapi) {
    rows = mrFrame;
    int err;
    if (vsapi->mapGetType(props, "_Field") == ptInt) {
        const int64_t field = vsapi->mapGetInt(props, "_Field", 0, &err);
        if (field != 0 && field != 1) {
            snprintf(errorMessage, errorMessageSize, "bad _Field value in the %s: %lld", what, static_cast<long long>(field));
            return false;
        }
        rows = field ? mrTopField : mrBottomField;
    } else if (vsapi->mapGetType(props, "_FieldBased") == ptInt) {
        const int64_t fieldBased = vsapi->mapGetInt(props, "_FieldBased", 0, &err);
        if (fieldBased != VSC_FIELD_PROGRESSIVE && fieldBased != VSC_FIELD_BOTTOM && fieldBased != VSC_FIELD_TOP) {
            snprintf(errorMessage, errorMessageSize, "bad _FieldBased value in the %s: %lld", what, static_cast<long long>(fieldBased));
            return false;
        }
        if (fieldBased != VSC_FIELD_PROGRESSIVE)
            rows = mrFields;
    }
    if (vi->format.subSamplingH == 0)
        rows = mrFrame;
    if (rows == mrFields && ((vi->height >> vi->format.subSamplingH) & 1)) {
        snprintf(errorMessage, errorMessageSize, "the %s is interlaced, which needs an even number of chroma rows", what);
        return false;
    }
    return true;
}

/* What beginFrame finds for a frame, in frameState: the range offset, the chroma location and the
   MaskRows. Float formats keep 0 for the offset, which their kernels never read. */
void *packMaskFrame(unsigned offset, int loc, int rows) {
    return reinterpret_cast<void *>((static_cast<uintptr_t>(offset) << 6) | (static_cast<uintptr_t>(rows) << 3) | static_cast<uintptr_t>(loc));
}

unsigned frameOffset(void *frameState) {
    return static_cast<unsigned>(reinterpret_cast<uintptr_t>(frameState) >> 6);
}

int frameLocation(void *frameState) {
    return static_cast<int>(reinterpret_cast<uintptr_t>(frameState) & 7);
}

int frameRows(void *frameState) {
    return static_cast<int>((reinterpret_cast<uintptr_t>(frameState) >> 3) & 7);
}

int mirrorIndex(int i, int n) {
    if (i < 0)
        i = -i - 1;
    else if (i >= n)
        i = 2 * n - 1 - i;
    return std::clamp(i, 0, n - 1);
}

/* The mask samples one chunk of output reads on a row, at most, which bounds the stack a chunk
   takes: the chunk is this over the horizontal factor. */
constexpr int maskChunkColumns = 2048;

enum class MaskSample { Byte, Word, Half, Float };

template<MaskSample K>
using MaskSampleType = std::conditional_t<K == MaskSample::Byte, uint8_t, std::conditional_t<K == MaskSample::Float, float, uint16_t>>;

/* Samples x0 to x0 + count - 1 of one row of a chroma plane's mask into out, from rows, the mask
   rows its vertical taps read in a plane width samples wide. Acc holds the sums: exact integers for
   integer samples, float otherwise. S is the horizontal factor when it is a constant, 0 to take it
   from h; a constant one lets the horizontal pass load its columns as vectors. */
template<MaskSample K, typename Acc, int S>
void resampleMaskChunk(const MaskAxis &v, const uint8_t *const *rows, const MaskAxis &h, int width, int x0, int count, unsigned maxval, void *dst) {
    using T = MaskSampleType<K>;
    constexpr bool integer = K == MaskSample::Byte || K == MaskSample::Word;
    auto load = [](T x) -> Acc {
        if constexpr (K == MaskSample::Half)
            return halfToFloat(x);
        else
            return static_cast<Acc>(x);
    };
    auto weight = [](const MaskAxis &axis, int j) -> Acc {
        if constexpr (integer)
            return axis.weight[j];
        else
            return axis.fweight[j];
    };
    auto vweight = [&](int j) { return weight(v, j); };

    Acc tmp[maskChunkColumns + maxChromaTaps];
    const int s = S ? S : h.factor;
    const int first = s * x0 + h.origin;
    const int columns = s * (count - 1) + h.taps;
    /* The columns that need no mirroring in one pass that vectorizes, the few at the edges apart. */
    const int lo = std::clamp(-first, 0, columns);
    const int hi = std::clamp(width - first, lo, columns);
    for (int k = lo; k < hi; k++)
        tmp[k] = 0;
    for (int j = 0; j < v.taps; j++) {
        const T *row = reinterpret_cast<const T *>(rows[j]);
        const Acc w = vweight(j);
        for (int k = lo; k < hi; k++)
            tmp[k] += w * load(row[first + k]);
    }
    for (int k = 0; k < columns; k++) {
        if (k == lo)
            k = hi;
        if (k >= columns)
            break;
        const int column = mirrorIndex(first + k, width);
        Acc sum = 0;
        for (int j = 0; j < v.taps; j++)
            sum += vweight(j) * load(reinterpret_cast<const T *>(rows[j])[column]);
        tmp[k] = sum;
    }

    /* The horizontal pass a tap at a time over the whole chunk, which is the loop that vectorizes.
       Each sample still adds its taps in order, so float sums are the same as one sample at a time. */
    int shift = 0;
    Acc init = 0;
    if constexpr (integer) {
        shift = v.shift + h.shift;
        init = shift ? Acc(1) << (shift - 1) : 0;
    }
    Acc sum[maskChunkColumns];
    for (int i = 0; i < count; i++)
        sum[i] = init;
    for (int j = 0; j < h.taps; j++) {
        const Acc w = weight(h, j);
        const Acc *column = tmp + j;
        for (int i = 0; i < count; i++)
            sum[i] += w * column[s * i];
    }

    T *out = static_cast<T *>(dst);
    for (int i = 0; i < count; i++) {
        if constexpr (integer)
            out[i] = static_cast<T>(std::min<Acc>(sum[i] >> shift, maxval));
        else if constexpr (K == MaskSample::Half)
            out[i] = floatToHalf(sum[i]);
        else
            out[i] = sum[i];
    }
}

/* resampleMaskChunk with the factor of 4:2:0 and 4:2:2 made a constant. */
template<MaskSample K, typename Acc>
void resampleMask(const MaskAxis &v, const uint8_t *const *rows, const MaskAxis &h, int width, int x0, int count, unsigned maxval, void *dst) {
    if (h.factor == 2)
        resampleMaskChunk<K, Acc, 2>(v, rows, h, width, x0, count, maxval, dst);
    else
        resampleMaskChunk<K, Acc, 0>(v, rows, h, width, x0, count, maxval, dst);
}

/* Calls fn(y, x0, count, maskChunk) for every chunk of the rows of region, a subsampled chroma
   plane, maskChunk holding the chunk's resampled mask in the format's samples. mask is the mask's
   whole first plane and width and height its size, the frame's. */
template<typename Fn>
void forEachResampledMaskChunk(const MaskResampler &r, const VSVideoFormat &fmt, const VSTileBuffer &mask, int width, int height, void *frameState, const VSTileRegion *region, Fn fn) {
    const int loc = frameLocation(frameState);
    const int maskRows = frameRows(frameState);
    const bool fields = maskRows == mrFields;
    const MaskAxis &h = r.h[loc];
    const int chunk = maskChunkColumns / h.factor;
    const unsigned maxval = fmt.sampleType == stInteger ? (1u << fmt.bitsPerSample) - 1 : 0;
    alignas(64) uint8_t maskChunk[maskChunkColumns * sizeof(float)];
    const uint8_t *rows[maxChromaTaps];

    for (int y = region->rowStart; y < region->rowEnd; y++) {
        /* An interlaced mask's field holds every other row of the frame, from the first row for
           the top field and the second for the bottom one, and each field mirrors at its own
           edges. */
        const int parity = fields ? (y & 1) : 0;
        const MaskAxis &v = r.v[loc][fields ? mrTopField + parity : maskRows];
        const int fieldHeight = fields ? height / 2 : height;
        const int base = v.factor * (fields ? y >> 1 : y) + v.origin;
        for (int j = 0; j < v.taps; j++) {
            const int row = mirrorIndex(base + j, fieldHeight);
            rows[j] = vsh::tileRow(&mask, fields ? 2 * row + parity : row);
        }
        for (int x0 = 0; x0 < region->width; x0 += chunk) {
            const int count = std::min(chunk, region->width - x0);
            if (fmt.sampleType == stFloat && fmt.bytesPerSample == 2)
                resampleMask<MaskSample::Half, float>(v, rows, h, width, x0, count, maxval, maskChunk);
            else if (fmt.sampleType == stFloat)
                resampleMask<MaskSample::Float, float>(v, rows, h, width, x0, count, maxval, maskChunk);
            else if (fmt.bytesPerSample == 1)
                resampleMask<MaskSample::Byte, uint32_t>(v, rows, h, width, x0, count, maxval, maskChunk);
            else if (!r.wide)
                resampleMask<MaskSample::Word, uint32_t>(v, rows, h, width, x0, count, maxval, maskChunk);
            else
                resampleMask<MaskSample::Word, uint64_t>(v, rows, h, width, x0, count, maxval, maskChunk);
            fn(y, x0, count, static_cast<const void *>(maskChunk));
        }
    }
}

} // namespace

/* The row kernels of one filter per instruction set, each indexed by sampleKind. A null entry
   means the instruction set has none for that sample type, and the next one down is used; a
   table without AVX-512 kernels leaves that row out. */
template<typename Kernel>
struct KernelTable {
    Kernel c[4];
#ifdef VS_TARGET_CPU_X86
    Kernel sse2[4];
    Kernel avx2[4];
    Kernel avx512[4];
#endif
};

/* 8 bit integer, 16 bit integer, single and half precision float: the formats
   is8to16orFloatFormat lets through. */
static int sampleKind(const VSVideoFormat &f) {
    if (f.sampleType == stInteger)
        return f.bytesPerSample == 1 ? 0 : 1;
    return f.bytesPerSample == 4 ? 2 : 3;
}

/* The fastest kernel in table for f that the CPU and cpulevel allow. */
template<typename Kernel>
static Kernel selectKernel(const KernelTable<Kernel> &table, const VSVideoFormat &f, [[maybe_unused]] int cpulevel) {
    const int kind = sampleKind(f);
#ifdef VS_TARGET_CPU_X86
    if (getCPUFeatures()->avx512 && cpulevel >= VS_CPU_LEVEL_AVX512 && table.avx512[kind])
        return table.avx512[kind];
    if (getCPUFeatures()->avx2 && cpulevel >= VS_CPU_LEVEL_AVX2 && table.avx2[kind])
        return table.avx2[kind];
    if (cpulevel >= VS_CPU_LEVEL_SSE2 && table.sse2[kind])
        return table.sse2[kind];
#endif
    return table.c[kind];
}

//////////////////////////////////////////
// PreMultiply


typedef void (*PreMultiplyKernel)(const void *, const void *, void *, unsigned, unsigned, unsigned);

typedef struct {
    const VSVideoInfo *vi;
    PreMultiplyKernel kernel;
    MaskResampler resampler; /* the alpha for subsampled chroma */
} PreMultiplyDataExtra;

typedef VariableNodeData<PreMultiplyDataExtra> PreMultiplyData;

static const KernelTable<PreMultiplyKernel> preMultiplyKernels = {
    { vs_premultiply_byte_c, vs_premultiply_word_c, vs_premultiply_float_c, vs_premultiply_half_c },
#ifdef VS_TARGET_CPU_X86
    { nullptr, nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, vs_premultiply_half_avx2 },
#endif
};

/* Takes the format rather than the VideoInfo so a compute path can capture it by value. */
static unsigned getLimitedRangeOffset(const VSMap *props, const VSVideoFormat &fmt, const VSAPI *vsapi) {
    int err;
    bool limited = (vsapi->mapGetInt(props, "_Range", 0, &err) == VSC_RANGE_LIMITED);
    if (err)
        limited = (fmt.colorFamily == cfGray || fmt.colorFamily == cfYUV);
    return (limited ? (16 << (fmt.bitsPerSample - 8)) : 0);
}

static unsigned getLimitedRangeOffset(const VSFrame *f, const VSVideoFormat &fmt, const VSAPI *vsapi) {
    return getLimitedRangeOffset(vsapi->getFramePropertiesRO(f), fmt, vsapi);
}

/* The offset, and for subsampled chroma the chroma location and how resize reads the alpha's rows,
   which the clip's and the alpha's properties give. */
static int VS_CC preMultiplyBeginFrame([[maybe_unused]] int n, const VSMap *const *inputProps, [[maybe_unused]] VSMap *outputProps, void **frameState, void *instanceData, char *errorMessage, int errorMessageSize, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    const PreMultiplyData *d = static_cast<const PreMultiplyData *>(instanceData);
    const VSVideoFormat &fmt = d->vi->format;
    const unsigned offset = fmt.sampleType == stInteger ? getLimitedRangeOffset(inputProps[0], fmt, vsapi) : 0;
    int loc = 0;
    int rows = mrFrame;
    if (d->resampler.active) {
        loc = resolveChromaLocation(inputProps[0], vsapi);
        if (!maskRowsOf(inputProps[1], d->vi, "alpha", rows, errorMessage, errorMessageSize, vsapi))
            return 0;
    }
    *frameState = packMaskFrame(offset, loc, rows);
    return 1;
}

/* The alpha's first plane serves every plane; subsampled chroma reads it resampled. */
static void VS_CC preMultiplyProcess(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, void *frameState, void *instanceData) {
    const PreMultiplyData *d = static_cast<const PreMultiplyData *>(instanceData);
    const VSVideoFormat &f = d->vi->format;
    const unsigned depth = static_cast<unsigned>(f.bitsPerSample);
    const unsigned offset = (region->plane > 0 && f.colorFamily == cfYUV) ? (1u << (depth - 1)) : frameOffset(frameState);
    if (region->plane > 0 && d->resampler.active) {
        forEachResampledMaskChunk(d->resampler, f, src[1], d->vi->width, d->vi->height, frameState, region, [&](int y, int x0, int count, const void *alpha) {
            const size_t at = static_cast<size_t>(x0) * f.bytesPerSample;
            d->kernel(vsh::tileRow(&src[0], y) + at, alpha, vsh::tileRow(dst, y) + at, depth, offset, static_cast<unsigned>(count));
        });
        return;
    }
    for (int y = region->rowStart; y < region->rowEnd; y++)
        d->kernel(vsh::tileRow(&src[0], y), vsh::tileRow(&src[1], y), vsh::tileRow(dst, y), depth, offset, static_cast<unsigned>(region->width));
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

    bool subsampled = (d->vi->format.numPlanes > 1) && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0);

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

    const VSVideoFormat &fmt = d->vi->format;
    d->kernel = selectKernel(preMultiplyKernels, fmt, vs_get_cpulevel(core));

    /* The alpha's first plane serves every plane, resampled for subsampled chroma. */
    d->resampler = maskResampler(fmt, subsampled);
    VSNode *inputs[] = { d->nodes[0], d->nodes[1] };
    createTiledFilter(out, "PreMultiply", d->vi, inputs, 2, 0, preMultiplyProcess, preMultiplyBeginFrame, filterFree<PreMultiplyData>, d.get(), core, vsapi,
        0, nullptr, nullptr, fmt.numPlanes > 1 ? 1 << 1 : 0);
    d.release();
}

//////////////////////////////////////////
// Merge

typedef void (*MergeKernel)(const void *, const void *, void *, union vs_merge_weight, unsigned);

typedef struct {
    const VSVideoInfo *vi;
    unsigned weight[3];
    float fweight[3];
    int process[3];
    MergeKernel kernel;
} MergeDataExtra;

typedef DualNodeData<MergeDataExtra> MergeData;

const unsigned MergeShift = 15;

static const KernelTable<MergeKernel> mergeKernels = {
    { vs_merge_byte_c, vs_merge_word_c, vs_merge_float_c, vs_merge_half_c },
#ifdef VS_TARGET_CPU_X86
    { vs_merge_byte_sse2, vs_merge_word_sse2, vs_merge_float_sse2, nullptr },
    { vs_merge_byte_avx2, vs_merge_word_avx2, vs_merge_float_avx2, vs_merge_half_avx2 },
#endif
};

static union vs_merge_weight mergeWeight(const MergeData *d, int plane) {
    union vs_merge_weight weight;
    if (d->vi->format.sampleType == stInteger)
        weight.u = d->weight[plane];
    else
        weight.f = d->fweight[plane];
    return weight;
}

static void VS_CC mergeProcess(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, [[maybe_unused]] void *frameState, void *instanceData) {
    const MergeData *d = static_cast<const MergeData *>(instanceData);
    union vs_merge_weight weight = mergeWeight(d, region->plane);
    for (int y = region->rowStart; y < region->rowEnd; y++)
        d->kernel(tileRow(&src[0], y), tileRow(&src[1], y), tileRow(dst, y), weight, region->width);
}

static void VS_CC mergeCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<MergeData> d(new MergeData(vsapi));

    int nweight = vsapi->mapNumElements(in, "weight");
    if (nweight > 3)
        RETERROR("Merge: more weights given than the number of planes to merge");
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

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "Merge").c_str());

    d->kernel = selectKernel(mergeKernels, d->vi->format, vs_get_cpulevel(core));

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

    /* A plane with weight 0 or 1 is one clip's plane unchanged: passed through, not merged. */
    int passthrough = 0;
    int passthroughInput[3] = {};
    for (int p = 0; p < d->vi->format.numPlanes; p++) {
        if (d->process[p] != 0) {
            passthrough |= 1 << p;
            passthroughInput[p] = d->process[p] - 1;
        }
    }
    VSNode *inputs[] = { d->node1, d->node2 };
    createTiledFilter(out, "Merge", d->vi, inputs, 2, passthrough, mergeProcess, nullptr, filterFree<MergeData>, d.get(), core, vsapi, 0, passthroughInput);
    d.release();
}

//////////////////////////////////////////
// MaskedMerge

typedef void (*MaskMergeKernel)(const void *, const void *, const void *, void *, unsigned, unsigned, unsigned);

typedef struct {
    const VSVideoInfo *vi;
    bool premultiplied;
    bool first_plane;
    bool process[3];
    bool checkLoc;          // chroma is merged, so clipa and clipb must agree on chroma location
    bool checkRange;        // premultiplied integer, so clipa and clipb must agree on range
    MaskMergeKernel kernel;
    MaskResampler resampler; /* the mask's first plane for subsampled chroma */
} MaskedMergeDataExtra;

typedef VariableNodeData<MaskedMergeDataExtra> MaskedMergeData;

static const KernelTable<MaskMergeKernel> maskMergeKernels = {
    { vs_mask_merge_byte_c, vs_mask_merge_word_c, vs_mask_merge_float_c, vs_mask_merge_half_c },
#ifdef VS_TARGET_CPU_X86
    { vs_mask_merge_byte_sse2, vs_mask_merge_word_sse2, vs_mask_merge_float_sse2, nullptr },
    { vs_mask_merge_byte_avx2, vs_mask_merge_word_avx2, vs_mask_merge_float_avx2, vs_mask_merge_half_avx2 },
    { vs_mask_merge_byte_avx512, vs_mask_merge_word_avx512, nullptr, nullptr },
#endif
};

static const KernelTable<MaskMergeKernel> maskMergePremulKernels = {
    { vs_mask_merge_premul_byte_c, vs_mask_merge_premul_word_c, vs_mask_merge_premul_float_c, vs_mask_merge_premul_half_c },
#ifdef VS_TARGET_CPU_X86
    { vs_mask_merge_premul_byte_sse2, vs_mask_merge_premul_word_sse2, vs_mask_merge_premul_float_sse2, nullptr },
    { vs_mask_merge_premul_byte_avx2, vs_mask_merge_premul_word_avx2, vs_mask_merge_premul_float_avx2, vs_mask_merge_premul_half_avx2 },
    { vs_mask_merge_premul_byte_avx512, vs_mask_merge_premul_word_avx512, nullptr, nullptr },
#endif
};

/* The per frame checks the frame path used to make, made on the properties: clipa and clipb have to
   agree on chroma siting before their chroma can be merged, and on range for premultiplied integer.
   Leaves clipa's range offset in *frameState for maskedMergeProcess, and when the mask's first
   plane is resampled for subsampled chroma, clipa's chroma location and how resize reads the mask's
   rows. */
static int VS_CC maskedMergeBeginFrame([[maybe_unused]] int n, const VSMap *const *inputProps, [[maybe_unused]] VSMap *outputProps, void **frameState, void *instanceData, char *errorMessage, int errorMessageSize, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    const MaskedMergeData *d = static_cast<const MaskedMergeData *>(instanceData);
    const VSVideoFormat &fmt = d->vi->format;
    int loc = 0;
    if (d->checkLoc) {
        int loc1 = resolveChromaLocation(inputProps[0], vsapi);
        int loc2 = resolveChromaLocation(inputProps[1], vsapi);
        if (loc1 != loc2) {
            snprintf(errorMessage, errorMessageSize, "clipa and clipb have different chroma locations (_ChromaLocation %d vs %d)", loc1, loc2);
            return 0;
        }
        loc = loc1;
    }
    unsigned offset1 = fmt.sampleType == stInteger ? getLimitedRangeOffset(inputProps[0], fmt, vsapi) : 0;
    if (d->checkRange && offset1 != getLimitedRangeOffset(inputProps[1], fmt, vsapi)) {
        snprintf(errorMessage, errorMessageSize, "Input frames must have the same range");
        return 0;
    }
    int rows = mrFrame;
    if (d->resampler.active && !maskRowsOf(inputProps[2], d->vi, "mask", rows, errorMessage, errorMessageSize, vsapi))
        return 0;
    *frameState = packMaskFrame(offset1, loc, rows);
    return 1;
}

/* The mask is plane p of the mask clip, or its first plane for every plane (first_plane), which
   subsampled chroma reads resampled. */
static void VS_CC maskedMergeProcess(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, void *frameState, void *instanceData) {
    const MaskedMergeData *d = static_cast<const MaskedMergeData *>(instanceData);
    const VSVideoFormat &f = d->vi->format;
    const unsigned depth = static_cast<unsigned>(f.bitsPerSample);
    const unsigned offset = (region->plane > 0 && f.colorFamily == cfYUV) ? (1u << (depth - 1)) : frameOffset(frameState);
    if (region->plane > 0 && d->resampler.active) {
        forEachResampledMaskChunk(d->resampler, f, src[2], d->vi->width, d->vi->height, frameState, region, [&](int y, int x0, int count, const void *mask) {
            const size_t at = static_cast<size_t>(x0) * f.bytesPerSample;
            d->kernel(vsh::tileRow(&src[0], y) + at, vsh::tileRow(&src[1], y) + at, mask, vsh::tileRow(dst, y) + at, depth, offset, static_cast<unsigned>(count));
        });
        return;
    }
    for (int y = region->rowStart; y < region->rowEnd; y++)
        d->kernel(vsh::tileRow(&src[0], y), vsh::tileRow(&src[1], y), vsh::tileRow(&src[2], y), vsh::tileRow(dst, y), depth, offset, static_cast<unsigned>(region->width));
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

    if (maskvi->width != d->vi->width || maskvi->height != d->vi->height || maskvi->format.sampleType != d->vi->format.sampleType || maskvi->format.bitsPerSample != d->vi->format.bitsPerSample
        || (!isSameVideoFormat(&maskvi->format, &d->vi->format) && maskvi->format.colorFamily != cfGray && !d->first_plane))
        RETERROR(("MaskedMerge: mask clip must have same dimensions, sample type and bitdepth as main clip and be the same format or grayscale, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(maskvi, vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MaskedMerge", d->process, vsapi))
        return;

    /* Two per frame checks every path makes: the two inputs have to agree on chroma siting before
       their chroma can be merged at all, and premultiplied integer needs them to agree on black
       level too, since one offset is applied to both. The range check only when a plane is
       processed, as a call processing nothing applies no offset, and looking at the format's planes
       alone, since the planes argument fills all three flags. */
    bool anyProcessed = false;
    for (int p = 0; p < d->vi->format.numPlanes; p++)
        anyProcessed = anyProcessed || d->process[p];
    d->checkLoc = (d->process[1] || d->process[2]) && (d->vi->format.subSamplingW > 0 || d->vi->format.subSamplingH > 0);
    d->checkRange = d->premultiplied && d->vi->format.sampleType == stInteger && anyProcessed;

    // Do we need to resample the first mask plane and use it for the chroma planes?
    bool need_chroma_resize = (d->first_plane && d->vi->format.numPlanes > 1)
                              && (d->vi->format.subSamplingH > 0 || d->vi->format.subSamplingW > 0)
                              && (d->process[1] || d->process[2]);

    const ClipResidencyResult maskedResidency = residencyOfClips(d->nodes.data(), 3, vsapi);
    if (maskedResidency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("MaskedMerge", maskedResidency.mixedAt).c_str());

    if (maskedResidency.kind == ClipResidency::AllGPU) {
        const VSVideoFormat &fmt = d->vi->format;
        const ChromaFilters cf = chromaFiltersFor(fmt.subSamplingW, fmt.subSamplingH);
        const unsigned depth = fmt.bitsPerSample;
        const uint32_t maxval = static_cast<uint32_t>((1ull << depth) - 1u);
        const bool isYUV = fmt.colorFamily == cfYUV;
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

        /* The per frame checks every path makes, worked out above. The range check being only
           made when a plane is processed matters here too: prepareFrame runs ahead of the
           driver's no-work shortcut and would otherwise fail a frame the CPU path hands back
           untouched. */
        const bool checkLoc = d->checkLoc;
        const bool checkRange = d->checkRange;

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

    d->kernel = selectKernel(d->premultiplied ? maskMergePremulKernels : maskMergeKernels, d->vi->format, vs_get_cpulevel(core));

    /* The mask's first plane standing in for processed planes past the first: every plane reads it,
       whole, and subsampled chroma resamples it. */
    const bool firstPlaneForAll = d->first_plane && d->vi->format.numPlanes > 1 && (d->process[1] || d->process[2]);
    d->resampler = maskResampler(d->vi->format, need_chroma_resize);
    VSNode *inputs[] = { d->nodes[0], d->nodes[1], d->nodes[2] };
    createTiledFilter(out, "MaskedMerge", d->vi, inputs, 3, unprocessedPlanes(d->process, d->vi->format.numPlanes), maskedMergeProcess,
        maskedMergeBeginFrame, filterFree<MaskedMergeData>, d.get(), core, vsapi, 0, nullptr, nullptr, firstPlaneForAll ? 1 << 2 : 0);
    d.release();
}

//////////////////////////////////////////
// MakeDiff and MergeDiff

typedef void (*DiffKernel)(const void *, const void *, void *, unsigned, unsigned);

typedef struct {
    const VSVideoInfo *vi;
    bool process[3];
    DiffKernel kernel;
} DiffDataExtra;

typedef DualNodeData<DiffDataExtra> DiffData;

static const KernelTable<DiffKernel> makeDiffKernels = {
    { vs_makediff_byte_c, vs_makediff_word_c, vs_makediff_float_c, vs_makediff_half_c },
#ifdef VS_TARGET_CPU_X86
    { vs_makediff_byte_sse2, vs_makediff_word_sse2, vs_makediff_float_sse2, nullptr },
    { vs_makediff_byte_avx2, vs_makediff_word_avx2, vs_makediff_float_avx2, vs_makediff_half_avx2 },
#endif
};

static const KernelTable<DiffKernel> mergeDiffKernels = {
    { vs_mergediff_byte_c, vs_mergediff_word_c, vs_mergediff_float_c, vs_mergediff_half_c },
#ifdef VS_TARGET_CPU_X86
    { vs_mergediff_byte_sse2, vs_mergediff_word_sse2, vs_mergediff_float_sse2, nullptr },
    { vs_mergediff_byte_avx2, vs_mergediff_word_avx2, vs_mergediff_float_avx2, vs_mergediff_half_avx2 },
#endif
};

static void VS_CC diffProcess(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, [[maybe_unused]] void *frameState, void *instanceData) {
    const DiffData *d = static_cast<const DiffData *>(instanceData);
    unsigned depth = static_cast<unsigned>(d->vi->format.bitsPerSample);
    for (int y = region->rowStart; y < region->rowEnd; y++)
        d->kernel(tileRow(&src[0], y), tileRow(&src[1], y), tileRow(dst, y), depth, region->width);
}

static void VS_CC makeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DiffData> d(new DiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MakeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MakeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MakeDiff", d->process, vsapi))
        return;

    d->kernel = selectKernel(makeDiffKernels, d->vi->format, vs_get_cpulevel(core));

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

    VSNode *inputs[] = { d->node1, d->node2 };
    createTiledFilter(out, "MakeDiff", d->vi, inputs, 2, unprocessedPlanes(d->process, d->vi->format.numPlanes), diffProcess, nullptr, filterFree<DiffData>, d.get(), core, vsapi);
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

static void VS_CC mergeDiffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DiffData> d(new DiffData(vsapi));

    d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
    d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node1);

    if (!is8to16orFloatFormat(d->vi->format))
        RETERROR(invalidVideoFormatMessage(d->vi->format, vsapi, "MergeDiff").c_str());

    if (!isConstantVideoFormat(d->vi) || !isSameVideoInfo(d->vi, vsapi->getVideoInfo(d->node2)))
        RETERROR(("MergeDiff: both clips must have the same constant format and dimensions, passed " + videoInfoToString(d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->node2), vsapi)).c_str());

    if (!getProcessPlanesArg(in, out, "MergeDiff", d->process, vsapi))
        return;

    d->kernel = selectKernel(mergeDiffKernels, d->vi->format, vs_get_cpulevel(core));

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

    VSNode *inputs[] = { d->node1, d->node2 };
    createTiledFilter(out, "MergeDiff", d->vi, inputs, 2, unprocessedPlanes(d->process, d->vi->format.numPlanes), diffProcess, nullptr, filterFree<DiffData>, d.get(), core, vsapi);
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
