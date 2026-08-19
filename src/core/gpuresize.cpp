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

/* GPU resize, factored the inverse of the scalar path: the kernel SHAPE is a compile-time
   constant of the shader (bicubic's polynomial with its parameters baked in, lanczos'
   windowed sinc), the GEOMETRY is push constants -- a scale, a shift and a tap count per
   resampled axis. No coefficient tables, so nothing to build variants of.

   Work is decided per frame, not per filter: arguments resolve once into a ConversionSpec,
   each frame's properties into a FrameState, and a pure function of the two yields the plan
   the recorder walks -- up to two passes per plane, each a geometry and an affine. Chroma
   siting, ranges and field parity are just numbers through that function, so a chromaloc-only
   change needs no special case; the identity-window test notices the moved grid itself.

   Weights are evaluated per output pixel, normalised by their own sum, out-of-bounds taps
   mirrored -- compute_filter's definition exactly, so this is the same resampling function
   rather than an approximation. Accuracy is measured against an independent float64
   reference (test/resize_reference.py), not against zimg.

   Recording is imperative against VSVULKANAPI rather than through gpufilter.h, whose static
   pass list cannot express a dispatch count that varies per frame.

   Output is deliberately not bit identical: resampling in float and rounding once beats the
   scalar integer path's quantised intermediate. Rounding is half up where an axis resampled
   and half to even where the plane only converted format, decided per plane. Anything
   unimplemented is a create error naming the reason, never a quiet download. Variable format
   and dimensions are excluded by construction -- kernels compile from the clip's one format,
   pipelines are built for its one pair of dimensions. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "VSVulkan4.h"
#include "resizeshared.h"
#include "zimg/blue.h"
#include "zimg/colorspace.h"
#include "zimg/colorspace_param.h"
#include "zimg/matrix3.h"

using namespace vsh;

namespace {

/* ------------------------------------------------------------------------------------------
   Kernels. The host side needs only the support and the parameters; the curve itself is
   emitted into the shader with its constants baked, so there is exactly one definition of
   each kernel and it lives in the GLSL below. Transcribed from zimg resize/filter.cpp,
   with the same NaN-is-unset parameter rule translate_resize_filter applies. */

struct KernelSpec {
    enum Type { Point, Bilinear, Bicubic, Spline16, Spline36, Spline64, Lanczos };
    Type type = Bicubic;
    double b = 0.0, c = 0.5;  /* bicubic */
    int taps = 3;             /* lanczos */
    double support = 2.0;

    bool operator==(const KernelSpec &o) const {
        return type == o.type && b == o.b && c == o.c && taps == o.taps;
    }
};

bool makeKernelSpec(const std::string &name, double a, double b, KernelSpec *out) {
    KernelSpec k;
    if (name == "point") {
        k.type = KernelSpec::Point;
        k.support = 0.0;
    } else if (name == "bilinear") {
        k.type = KernelSpec::Bilinear;
        k.support = 1.0;
    } else if (name == "bicubic") {
        k.type = KernelSpec::Bicubic;
        k.b = std::isnan(a) ? 0.0 : a;
        k.c = std::isnan(b) ? 0.5 : b;
        k.support = 2.0;
    } else if (name == "spline16") {
        k.type = KernelSpec::Spline16;
        k.support = 2.0;
    } else if (name == "spline36") {
        k.type = KernelSpec::Spline36;
        k.support = 3.0;
    } else if (name == "spline64") {
        k.type = KernelSpec::Spline64;
        k.support = 4.0;
    } else if (name == "lanczos") {
        k.taps = std::isnan(a) ? 3 : static_cast<int>(std::max(a, 1.0));
        /* zimg's MAX_TAPS is exclusive at 16. */
        if (k.taps >= 16)
            return false;
        k.type = KernelSpec::Lanczos;
        k.support = k.taps;
    } else {
        return false;
    }
    *out = k;
    return true;
}

/* A double as a GLSL float literal. %.9g round-trips float32, which is the precision the
   shader computes in anyway; the suffix check keeps "1" from becoming an int literal. */
std::string glslFloat(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.9g", v);
    std::string s = buf;
    if (s.find_first_of(".eE") == std::string::npos)
        s += ".0";
    return s;
}

/* The kernel curve as GLSL, constants baked so the compiler folds the polynomials. One
   body per kernel type; the same text serves both axes since only geometry differs. */
std::string kernelBody(const KernelSpec &k) {
    switch (k.type) {
    case KernelSpec::Point:
        /* One tap of weight one; the window placement does the half-up pixel pick. */
        return "float kernelWeight(float x) { return 1.0; }\n";
    case KernelSpec::Bilinear:
        return "float kernelWeight(float x) { return max(1.0 - abs(x), 0.0); }\n";
    case KernelSpec::Bicubic: {
        const double p0 = (6.0 - 2.0 * k.b) / 6.0;
        const double p2 = (-18.0 + 12.0 * k.b + 6.0 * k.c) / 6.0;
        const double p3 = (12.0 - 9.0 * k.b - 6.0 * k.c) / 6.0;
        const double q0 = (8.0 * k.b + 24.0 * k.c) / 6.0;
        const double q1 = (-12.0 * k.b - 48.0 * k.c) / 6.0;
        const double q2 = (6.0 * k.b + 30.0 * k.c) / 6.0;
        const double q3 = (-k.b - 6.0 * k.c) / 6.0;
        return "float kernelWeight(float x) {\n"
               "    x = abs(x);\n"
               "    if (x < 1.0) return " + glslFloat(p0) + " + x * x * (" + glslFloat(p2) +
               " + x * " + glslFloat(p3) + ");\n"
               "    if (x < 2.0) return " + glslFloat(q0) + " + x * (" + glslFloat(q1) +
               " + x * (" + glslFloat(q2) + " + x * " + glslFloat(q3) + "));\n"
               "    return 0.0;\n"
               "}\n";
    }
    case KernelSpec::Spline16:
        return "float kernelWeight(float x) {\n"
               "    x = abs(x);\n"
               "    if (x < 1.0) return 1.0 + x * (-0.2 + x * (-1.8 + x));\n"
               "    if (x < 2.0) { x -= 1.0; return x * (" + glslFloat(-7.0 / 15.0) +
               " + x * (0.8 + x * " + glslFloat(-1.0 / 3.0) + ")); }\n"
               "    return 0.0;\n"
               "}\n";
    case KernelSpec::Spline36:
        return "float kernelWeight(float x) {\n"
               "    x = abs(x);\n"
               "    if (x < 1.0) return 1.0 + x * (" + glslFloat(-3.0 / 209.0) + " + x * (" +
               glslFloat(-453.0 / 209.0) + " + x * " + glslFloat(13.0 / 11.0) + "));\n"
               "    if (x < 2.0) { x -= 1.0; return x * (" + glslFloat(-156.0 / 209.0) +
               " + x * (" + glslFloat(270.0 / 209.0) + " + x * " + glslFloat(-6.0 / 11.0) + ")); }\n"
               "    if (x < 3.0) { x -= 2.0; return x * (" + glslFloat(26.0 / 209.0) +
               " + x * (" + glslFloat(-45.0 / 209.0) + " + x * " + glslFloat(1.0 / 11.0) + ")); }\n"
               "    return 0.0;\n"
               "}\n";
    case KernelSpec::Spline64:
        return "float kernelWeight(float x) {\n"
               "    x = abs(x);\n"
               "    if (x < 1.0) return 1.0 + x * (" + glslFloat(-3.0 / 2911.0) + " + x * (" +
               glslFloat(-6387.0 / 2911.0) + " + x * " + glslFloat(49.0 / 41.0) + "));\n"
               "    if (x < 2.0) { x -= 1.0; return x * (" + glslFloat(-2328.0 / 2911.0) +
               " + x * (" + glslFloat(4032.0 / 2911.0) + " + x * " + glslFloat(-24.0 / 41.0) + ")); }\n"
               "    if (x < 3.0) { x -= 2.0; return x * (" + glslFloat(582.0 / 2911.0) +
               " + x * (" + glslFloat(-1008.0 / 2911.0) + " + x * " + glslFloat(6.0 / 41.0) + ")); }\n"
               "    if (x < 4.0) { x -= 3.0; return x * (" + glslFloat(-97.0 / 2911.0) +
               " + x * (" + glslFloat(168.0 / 2911.0) + " + x * " + glslFloat(-1.0 / 41.0) + ")); }\n"
               "    return 0.0;\n"
               "}\n";
    case KernelSpec::Lanczos:
        /* sinc is even, so no abs inside it; the device's sin is trusted to be good,
           which is a stated assumption of this design rather than an oversight. */
        return "const float PI_ = 3.14159265358979;\n"
               "float sinc_(float x) { return x == 0.0 ? 1.0 : sin(x * PI_) / (x * PI_); }\n"
               "float kernelWeight(float x) {\n"
               "    const float T = " + glslFloat(static_cast<double>(k.taps)) + ";\n"
               "    return abs(x) < T ? sinc_(x) * sinc_(x / T) : 0.0;\n"
               "}\n";
    }
    return {};
}

/* ------------------------------------------------------------------------------------------
   The gather pass. One shader for either axis, specialised by VERT so the addressing
   compiles to a constant pattern, and by sample types at each end so any pass of a plan
   can read frames or the float intermediate with the same text.

   The position arithmetic is compensated. In plain float32, pos = (o + 0.5) * invScale + shift
   carries an ulp of the POSITION's magnitude -- half a thousandth of a pixel at 4K, a uniform
   window shift costing ~25 dB against the reference. Splitting the product and the sum into
   value + residual keeps the in-window offset accurate to an ulp of ITSELF, and window
   placement consumes only the rounded value, where an ulp can at most flip a tie whose
   boundary tap has zero weight. The `precise` qualifiers are load bearing: without them the
   compiler contracts the very operations whose rounding the residuals recover. */
const char resizeMain[] =
    "float roundHalfup(float x) {\n"
    /* Positive ties nudge down one ulp so round(x - 1) == round(x) - 1 holds across
       zero, matching the host's grid rounding exactly. */
    "    return x < 0.0 ? floor(x + 0.5) : floor(x + 0.49999997);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    uint ox = gl_GlobalInvocationID.x;\n"
    "    uint oy = gl_GlobalInvocationID.y;\n"
    "    if (ox >= pc.outWidth || oy >= pc.outHeight) return;\n"
    "    uint o = VERT != 0 ? oy : ox;\n"
    "\n"
    "    float a = float(o) + 0.5;\n"
    "    precise float p = a * pc.invScale;\n"
    "    float pe = fma(a, pc.invScale, -p);\n"
    "    precise float s = p + pc.shift;\n"
    "    precise float bb = s - p;\n"
    "    precise float se = (p - (s - bb)) + (pc.shift - bb);\n"
    "\n"
    /* Not `half`, which GLSL reserves. */
    "    float halfTaps = float(pc.taps) * 0.5;\n"
    "    float begin = roundHalfup(s - halfTaps) + 0.5;\n"
    /* The low halves carry what narrowing the geometry to float32 lost -- without them a
       fractional ratio costs ~25 dB, since no amount of careful arithmetic can recover a
       constant that was rounded before the shader ever saw it. */
    "    precise float d = (s - begin) + ((pe + se) + fma(a, pc.invScaleLo, pc.shiftLo));\n"
    /* begin came from s alone, whose error grows with the position -- 2e-4 of a pixel at
       4K -- so a position near a window boundary can land on the wrong side of it. d is
       that same offset accurate to an ulp of ITSELF, so redo the decision with it: the
       window is canonical when d is in [half-1, half), and s is never a whole pixel out,
       so one step always reaches it.

       A position can sit exactly ON a boundary -- 224 to 96 chroma is shift -0.5 at a whole
       ratio, so output 13 is at source 31.000000 -- and then the sample is decided by the
       rule alone. Ties go up, which is what zimg does rather than what it says: its
       round_halfup nudges positive ties down by 2^-54, but past a position of 1 that deficit
       is below half an ulp of the addition, so floor() sees the whole number. Measured
       against the scalar path over 120k outputs of real chroma geometries, ties up disagree
       15 times and ties down 446. The rest is zimg's own division noise landing a few 1e-14
       on either side of a boundary, which is per output rather than per axis and so is not
       reachable from here: 3 outputs in 120k survive, all point. Only point notices any of
       this -- a wider kernel's two candidate windows differ by one tap at each end, and both
       sit at the support edge where the kernel is zero. */
    "    if (d >= halfTaps) { begin += 1.0; d -= 1.0; }\n"
    "    else if (d < halfTaps - 1.0) { begin -= 1.0; d += 1.0; }\n"
    "\n"
    "    float acc = 0.0;\n"
    "    float wsum = 0.0;\n"
    "    float lastSamp = 0.0;\n"
    "    for (uint k = 0u; k < pc.taps; k++) {\n"
    "        float w = kernelWeight((float(k) - d) * pc.kstep);\n"
    /* Out-of-bounds taps mirror back into the image, exactly as compute_filter builds
       its matrix; the trailing clamp is the same guard against a double bounce. */
    "        float xpos = begin + float(k);\n"
    "        float rp = xpos < 0.0 ? -xpos\n"
    "            : (xpos >= float(pc.srcDim) ? 2.0 * float(pc.srcDim) - xpos : xpos);\n"
    "        uint idx = uint(clamp(int(rp), 0, int(pc.srcDim) - 1));\n"
    /* The offsets are how a field of an interleaved frame reads as an image of its own:
       base plus doubled stride. Zero and the plane's stride everywhere else. */
    "        uint si = pc.srcOffset + (VERT != 0 ? idx * pc.srcStride + ox : oy * pc.srcStride + idx);\n"
    "        lastSamp = float(srcData[si]);\n"
    "        acc += w * lastSamp;\n"
    "        wsum += w;\n"
    "    }\n"
    /* Normalising by the tap total is zimg's own definition -- every matrix row it
       builds is divided by exactly this sum -- not an edge fixup of this path's. A
       single tap short-circuits to the sample itself: a kernel need not be 1 at zero
       (B-spline is 2/3), and w * s / w rounds twice where a copy must be exact. This is
       also what makes a depth-only pass an exact copy plus its affine. */
    "    acc = pc.taps == 1u ? lastSamp : acc / wsum;\n"
    "    acc = acc * pc.scale + pc.offset;\n"
    "#if ROUND_INT\n"
    /* Half up where an axis resampled (zimg's pack_pixel_u16), half to even for a plane
       that only converted format (lrint); which one is a per-frame, per-plane fact, so
       it rides the push constants rather than the pipeline. Saturation is not
       decoration: a windowed kernel really does overshoot. Dither, when this instance
       carries it at all, lands just before the round. */
    "#if DITHER_MODE > 0\n"
    "    if (pc.ditherOn != 0u) acc += ditherLookup(pc.ditherArg, ox, oy);\n"
    "#endif\n"
    "    acc = pc.roundEven != 0u ? clamp(roundEven(acc), 0.0, PEAK)\n"
    "                             : clamp(floor(acc + 0.5), 0.0, PEAK);\n"
    "#endif\n"
    "    dstData[pc.dstOffset + oy * pc.dstStride + ox] = DST_T(acc);\n"
    "}\n";

/* The dither indexers, shared by both kernels. Mode 1 is the 16x16 Bayer table with its
   four create-time variants selected per plane; mode 2 is the 64x64 blue noise with the
   per-plane row and column rotation. arg packs whatever the host resolved. */
const char ditherLookupGlsl[] =
    "#if DITHER_MODE == 1\n"
    "float ditherLookup(uint arg, uint x, uint y) {\n"
    "    return ditherTable[arg + (y & 15u) * 16u + (x & 15u)];\n"
    "}\n"
    "#elif DITHER_MODE == 2\n"
    "float ditherLookup(uint arg, uint x, uint y) {\n"
    "    return ditherTable[((y + (arg & 255u)) & 63u) * 64u + (((arg >> 8u) + x) & 63u)];\n"
    "}\n"
    "#endif\n";

struct ResizePush {
    uint32_t outWidth, outHeight;   /* extent of the surface this pass writes */
    uint32_t srcDim;                /* length of the resampled axis in this pass's input */
    uint32_t srcStride, dstStride;  /* in elements */
    uint32_t srcOffset, dstOffset;  /* in elements; a field of an interleaved frame */
    uint32_t taps;
    uint32_t roundEven;
    uint32_t ditherArg, ditherOn;
    float invScale, shift, kstep;
    /* The geometry to double precision: value + narrowing residual. */
    float invScaleLo, shiftLo;
    float scale, offset;            /* the store affine; identity mid-chain */
};

constexpr uint32_t resizeLocalSize = 16;

const char *sampleTypeName(const VSVideoFormat &f) {
    if (f.sampleType == stFloat)
        return f.bytesPerSample == 2 ? "float16_t" : "float";
    return f.bytesPerSample == 1 ? "uint8_t" : "uint16_t";
}

std::string resizeShaderSource(const KernelSpec &k, bool vertical, const char *srcType,
    const char *dstType, int intBits, int ditherMode) {
    std::string s = "#version 460\n";
    s += "#extension GL_EXT_shader_8bit_storage : require\n"
         "#extension GL_EXT_shader_16bit_storage : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n";
    if (!strcmp(srcType, "float16_t") || !strcmp(dstType, "float16_t"))
        s += "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";
    s += std::string("#define SRC_T ") + srcType + "\n";
    s += std::string("#define DST_T ") + dstType + "\n";
    s += std::string("#define VERT ") + (vertical ? "1" : "0") + "\n";
    /* Dither only matters where samples are stored, so kernels writing the float
       intermediates never carry the table. */
    s += "#define DITHER_MODE " + std::to_string(intBits > 0 ? ditherMode : 0) + "\n";
    if (intBits > 0) {
        s += "#define ROUND_INT 1\n";
        s += "#define PEAK " + std::to_string((1u << intBits) - 1u) + ".0\n";
    } else {
        s += "#define ROUND_INT 0\n";
    }
    s += "layout(local_size_x = " + std::to_string(resizeLocalSize) +
         ", local_size_y = " + std::to_string(resizeLocalSize) + ") in;\n";
    s += "layout(std430, set = 0, binding = 0) readonly buffer Src { SRC_T srcData[]; };\n"
         "layout(std430, set = 0, binding = 1) writeonly buffer Dst { DST_T dstData[]; };\n";
    if (intBits > 0 && ditherMode > 0)
        s += "layout(std430, set = 0, binding = 2) readonly buffer Dither { float ditherTable[]; };\n";
    s += "layout(push_constant, std430) uniform PC {\n"
         "    uint outWidth, outHeight, srcDim, srcStride, dstStride, srcOffset, dstOffset, taps, roundEven;\n"
         "    uint ditherArg, ditherOn;\n"
         "    float invScale, shift, kstep, invScaleLo, shiftLo, scale, offset;\n"
         "} pc;\n\n";
    s += kernelBody(k);
    s += ditherLookupGlsl;
    s += resizeMain;
    return s;
}

/* ------------------------------------------------------------------------------------------
   Geometry, transcribed from graphbuilder.cpp; validated line for line against
   test/resize_reference.py, which is the same math in float64. Offsets are in units of
   the plane's own pixels; subscale is the plane's size as a fraction of luma. */

enum { sitingCenter = 0, sitingLeft = 1 };
enum { sitingMiddle = 0, sitingTop = 1, sitingBottom = 2 };

/* How a frame relates to the field grid. Interlaced is one state, not two: whichever
   field is FIRST temporally, the top field spatially is always the even rows, and the
   resample runs once per field either way -- the same reading import_frame_props makes,
   which records only whether _FieldBased names a field order, not which. */
enum class Parity { Progressive, TopField, BottomField, Interlaced };

/* A field's rows sit a quarter of a line off the progressive grid. */
double lumaParityOffset(Parity p) {
    if (p == Parity::TopField)
        return -0.25;
    if (p == Parity::BottomField)
        return 0.25;
    return 0.0;
}

/* Chroma inherits the field offset and halves its own siting offset on top. */
double chromaParityOffset(Parity p, double sitingOffset) {
    if (p == Parity::TopField)
        return -0.25 + sitingOffset / 2;
    if (p == Parity::BottomField)
        return 0.25 + sitingOffset / 2;
    return sitingOffset;
}

void splitChromaLocation(int64_t loc, int *w, int *h) {
    switch (loc) {
    case VSC_CHROMA_LEFT:          *w = sitingLeft;   *h = sitingMiddle; break;
    case VSC_CHROMA_CENTER:        *w = sitingCenter; *h = sitingMiddle; break;
    case VSC_CHROMA_TOP_LEFT:      *w = sitingLeft;   *h = sitingTop;    break;
    case VSC_CHROMA_TOP:           *w = sitingCenter; *h = sitingTop;    break;
    case VSC_CHROMA_BOTTOM_LEFT:   *w = sitingLeft;   *h = sitingBottom; break;
    default:                       *w = sitingCenter; *h = sitingBottom; break;
    }
}

/* Note that at subscale 1 every variant collapses to zero, which is what makes it safe
   to run every plane through the same expressions: luma and RGB's extra planes come out
   unshifted by construction rather than by special case. */
double chromaOffsetW(int siting, double subscale) {
    return siting == sitingLeft ? -0.5 + 0.5 * subscale : 0.0;
}

double chromaOffsetH(int siting, double subscale) {
    if (siting == sitingTop)
        return -0.5 + 0.5 * subscale;
    if (siting == sitingBottom)
        return 0.5 - 0.5 * subscale;
    return 0.0;
}

struct AxisWindow {
    double shift;
    double width;
};

/* Both ends may carry an active region: the working state of a conversion keeps the
   window of whichever side is smaller, so a chain's destination is not always the whole
   surface. The old behaviour is the special case dstActiveStart 0, dstActiveExtent the
   full luma dimension. */
AxisWindow axisWindow(double subIn, double subOut, double offIn, double offOut,
    double srcActiveStart, double srcActiveExtent, double dstActiveStart,
    double dstActiveExtent, uint32_t dstPlaneDim) {
    const double srcStart = srcActiveStart * subIn - offIn;
    const double srcExtent = srcActiveExtent * subIn;
    const double dstStart = dstActiveStart * subOut - offOut;
    const double dstExtent = dstActiveExtent * subOut;
    const double scale = dstExtent / srcExtent;
    AxisWindow w;
    w.shift = srcStart - dstStart / scale;
    w.width = srcExtent * (dstPlaneDim / dstExtent);
    return w;
}

/* zimg's sample numbering, from depth/quantize.h. Float is canonical: range 1, offset 0. */
double integerRange(const VSVideoFormat &fmt, bool chroma, bool fullrange, bool ycgco) {
    if (fmt.sampleType == stFloat)
        return 1.0;
    const unsigned d = static_cast<unsigned>(fmt.bitsPerSample);
    if (fullrange)
        return static_cast<double>((1u << d) - 1u);
    return static_cast<double>((chroma && !ycgco ? 224u : 219u) << (d - 8));
}

double integerOffset(const VSVideoFormat &fmt, bool chroma, bool fullrange) {
    if (fmt.sampleType == stFloat)
        return 0.0;
    const unsigned d = static_cast<unsigned>(fmt.bitsPerSample);
    if (chroma)
        return static_cast<double>(1u << (d - 1));
    return fullrange ? 0.0 : static_cast<double>(16u << (d - 8));
}

struct ScaleOffset { float scale, offset; };

ScaleOffset depthTransform(const VSVideoFormat &srcFmt, const VSVideoFormat &dstFmt,
    bool chroma, bool fullIn, bool fullOut, bool ycgco) {
    const double rangeIn = integerRange(srcFmt, chroma, fullIn, ycgco);
    const double rangeOut = integerRange(dstFmt, chroma, fullOut, ycgco);
    const double offsetIn = integerOffset(srcFmt, chroma, fullIn);
    const double offsetOut = integerOffset(dstFmt, chroma, fullOut);
    ScaleOffset r;
    r.scale = static_cast<float>(rangeOut / rangeIn);
    r.offset = static_cast<float>(-offsetIn * rangeOut / rangeIn + offsetOut);
    return r;
}

/* Unity float is the canonical space of the colour pass; these are the two halves of
   depthTransform split around it. */
ScaleOffset affineToUnity(const VSVideoFormat &fmt, bool chroma, bool full, bool ycgco) {
    const double range = integerRange(fmt, chroma, full, ycgco);
    const double offset = integerOffset(fmt, chroma, full);
    ScaleOffset r;
    r.scale = static_cast<float>(1.0 / range);
    r.offset = static_cast<float>(-offset / range);
    return r;
}

ScaleOffset affineFromUnity(const VSVideoFormat &fmt, bool chroma, bool full, bool ycgco) {
    ScaleOffset r;
    r.scale = static_cast<float>(integerRange(fmt, chroma, full, ycgco));
    r.offset = static_cast<float>(integerOffset(fmt, chroma, full));
    return r;
}

/* ------------------------------------------------------------------------------------------
   Colour. The whole pixel-local core of a conversion -- in-matrix, decode, gamut, encode,
   out-matrix or constant luminance, with the sample-numbering affines at both ends -- is ONE
   pointwise pass: load three samples, do the mathematics, store up to three. Splitting it into
   a pass per stage would cost a full read and write of three working planes each time, which
   on an integrated GPU dominates the conversion.

   The constants are a per-frame fact (the in-matrix follows _Matrix, the curves follow
   _Transfer), so they are built on the host in doubles each frame and ride to the device in a
   small vkCmdUpdateBuffer -- they do not fit push space, only 128 bytes of which is
   guaranteed. */

enum {
    curveLinear = 0,   /* identity, so a linear light intermediate costs nothing */
    curve1886 = 1,     /* BT.709, BT.601, BT.2020 10 and 12 bit, SMPTE 240M */
    curve470m = 2,
    curve470bg = 3,
    curveSrgb = 4,
    curveSt428 = 5,
    curveLog100 = 6,
    curveLog316 = 7,
    /* The two that carry a peak luminance, and so a scale on each side of them. */
    curveSt2084 = 8,
    curveAribB67 = 9,
    /* Extended-range 709: 1886 inside [0,1], the sign-extended OETF pair outside. */
    curveXvycc = 10,
};

/* Which curve a _Transfer value decodes with. select_transfer_function's scene_referred
   flag defaults to false and only constant luminance passes true, so this is the display
   referred column throughout: BT.709, BT.601, BT.2020 and SMPTE 240M all decode with the
   Rec.1886 EOTF, a plain 2.4 power law, not the camera OETF sharing their name. -1 is a
   transfer this path does not implement. */
int transferCurveOfValue(int64_t value) {
    switch (value) {
    case VSC_TRANSFER_LINEAR: return curveLinear;
    case VSC_TRANSFER_BT709:
    case VSC_TRANSFER_BT601:
    case VSC_TRANSFER_BT2020_10:
    case VSC_TRANSFER_BT2020_12:
    case VSC_TRANSFER_ST240_M: return curve1886;
    case VSC_TRANSFER_BT470_M: return curve470m;
    case VSC_TRANSFER_BT470_BG: return curve470bg;
    case VSC_TRANSFER_IEC_61966_2_1: return curveSrgb;
    case VSC_TRANSFER_ST428: return curveSt428;
    case VSC_TRANSFER_LOG_100: return curveLog100;
    case VSC_TRANSFER_LOG_316: return curveLog316;
    case VSC_TRANSFER_ST2084: return curveSt2084;
    case VSC_TRANSFER_ARIB_B67: return curveAribB67;
    case VSC_TRANSFER_IEC_61966_2_4: return curveXvycc;
    default: return -1;
    }
}

/* zimg's to_linear_scale and to_gamma_scale, 1.0 for every curve but the two carrying a
   peak luminance; applied as to_linear(x) * scale and to_gamma(x * scale). */
struct CurveScale { double toLinear, toGamma; };

CurveScale curveScaleOf(int curve, double peakLuminance) {
    constexpr double st2084Peak = 10000.0;  /* ST2084_PEAK_LUMINANCE, cd/m^2 */
    if (curve == curveSt2084)
        return { st2084Peak / peakLuminance, peakLuminance / st2084Peak };
    if (curve == curveAribB67)
        return { 1000.0 / peakLuminance, peakLuminance / 1000.0 };
    return { 1.0, 1.0 };
}

/* The non constant luminance matrices this path implements, by _Matrix value. The
   chromaticity derived and constant luminance families are handled separately since they
   are not one matrix but a family keyed on the primaries. */
bool matrixCoeffOfValue(int64_t value, zimg::colorspace::MatrixCoefficients *out) {
    using MC = zimg::colorspace::MatrixCoefficients;
    switch (value) {
    case VSC_MATRIX_BT709: *out = MC::REC_709; return true;
    case VSC_MATRIX_FCC: *out = MC::FCC; return true;
    case VSC_MATRIX_BT470_BG:
    case VSC_MATRIX_ST170_M: *out = MC::REC_601; return true;
    case VSC_MATRIX_ST240_M: *out = MC::SMPTE_240M; return true;
    case VSC_MATRIX_YCGCO: *out = MC::YCGCO; return true;
    case VSC_MATRIX_BT2020_NCL: *out = MC::REC_2020_NCL; return true;
    default: return false;
    }
}

bool primariesOfValue(int64_t value, zimg::colorspace::ColorPrimaries *out) {
    using CP = zimg::colorspace::ColorPrimaries;
    switch (value) {
    case VSC_PRIMARIES_BT709: *out = CP::REC_709; return true;
    case VSC_PRIMARIES_BT470_M: *out = CP::REC_470_M; return true;
    case VSC_PRIMARIES_BT470_BG: *out = CP::REC_470_BG; return true;
    /* 170m and 240m are one set of primaries under two tags, as in zimg's own table. */
    case VSC_PRIMARIES_ST170_M:
    case VSC_PRIMARIES_ST240_M: *out = CP::SMPTE_C; return true;
    case VSC_PRIMARIES_FILM: *out = CP::FILM; return true;
    case VSC_PRIMARIES_BT2020: *out = CP::REC_2020; return true;
    case VSC_PRIMARIES_ST428: *out = CP::XYZ; return true;
    case VSC_PRIMARIES_ST431_2: *out = CP::DCI_P3; return true;
    case VSC_PRIMARIES_ST432_1: *out = CP::DCI_P3_D65; return true;
    case VSC_PRIMARIES_EBU3213_E: *out = CP::EBU_3213_E; return true;
    default: return false;
    }
}

/* The constant block the colour kernel reads, one vkCmdUpdateBuffer per frame. Scalar
   floats and uints only, so the std430 layout is exactly the C++ layout. */
struct ColourConstants {
    float inMat[9];               /* yuv -> rgb or ICtCp -> LMS, when FLAG_IN_MAT */
    float gamut[9];               /* the mid 3x3 in linear light, when FLAG_GAMUT; the
                                     LMS<->RGB legs of ICtCp compose into it host side */
    float outMat[9];              /* rgb -> yuv or LMS -> ICtCp, when FLAG_OUT_MAT */
    float inScale[3], inOffset[3];    /* sample numbering -> unity float, per plane */
    float outScale[3], outOffset[3];  /* unity float -> sample numbering, per plane */
    float cl[7];                  /* kr, kg, kb, nb, pb, nr, pr for a CL input */
    float clOut[7];               /* the same for a CL output; both may exist at once */
    float ootfIn[3], ootfOut[3];  /* luma weights for the luminance-based B67 OOTF */
    float curveScaleIn, curveScaleOut;
    uint32_t curveIn, curveOut;   /* consulted when FLAG_CURVES */
    uint32_t flags;
    uint32_t roundEven[3];        /* per written plane, when the store rounds at all */
    uint32_t ditherArg[3];        /* per written plane; see the dither push field */
    uint32_t ditherOn[3];
};

constexpr uint32_t FLAG_IN_MAT = 1;
constexpr uint32_t FLAG_OUT_MAT = 2;
constexpr uint32_t FLAG_CURVES = 4;    /* decode -> (gamut) -> encode between matrices */
constexpr uint32_t FLAG_GAMUT = 8;
constexpr uint32_t FLAG_CL_IN = 16;    /* constant luminance replaces in-matrix + decode */
constexpr uint32_t FLAG_CL_OUT = 32;   /* ... or encode + out-matrix */
constexpr uint32_t FLAG_IN1_ZERO = 64; /* chroma that does not exist: grey sources */
constexpr uint32_t FLAG_IN2_ZERO = 128;
/* The Rec.2100 display-referred HLG OOTF applied through the LUMINANCE rather than per
   channel -- what zimg selects whenever the primaries are known and gamma is exact. */
constexpr uint32_t FLAG_B67L_IN = 256;
constexpr uint32_t FLAG_B67L_OUT = 512;

/* The curve implementations, shared prelude of the colour kernel. Transcribed from
   zimg/src/zimg/colorspace/gamma.cpp; guarding the negatives is not tidiness, pow is
   undefined below zero in GLSL and float YUV chroma really does arrive negative. */
const char colourCurvesGlsl[] =
    "const float SRGB_ALPHA = 1.055010718947587;\n"
    "const float SRGB_BETA = 0.003041282560128;\n"
    "const float ST2084_M1 = 0.1593017578125;\n"
    "const float ST2084_M2 = 78.84375;\n"
    "const float ST2084_C1 = 0.8359375;\n"
    "const float ST2084_C2 = 18.8515625;\n"
    "const float ST2084_C3 = 18.6875;\n"
    "const float B67_A = 0.17883277;\n"
    "const float B67_B = 0.28466892;\n"
    "const float B67_C = 0.55991073;\n"
    "\n"
    /* Hybrid Log Gamma, display referred: the OETF's inverse followed by a per channel
       1.2 power, per channel rather than by Rec.2100's iterative method, because
       matching zimg is the point. */
    "float b67InverseOetf(float x) {\n"
    "    x = max(x, 0.0);\n"
    "    return x <= 0.5 ? (x * x) * (1.0 / 3.0)\n"
    "        : (exp((x - B67_C) / B67_A) + B67_B) / 12.0;\n"
    "}\n"
    "float b67Oetf(float x) {\n"
    "    x = max(x, 0.0);\n"
    "    return x <= (1.0 / 12.0) ? sqrt(3.0 * x) : B67_A * log(12.0 * x - B67_B) + B67_C;\n"
    "}\n"
    "\n"
    "float toLinear(uint c, float x) {\n"
    "    if (c == 1u) return x < 0.0 ? 0.0 : pow(x, 2.4);\n"
    "    if (c == 2u) return x < 0.0 ? 0.0 : pow(x, 2.2);\n"
    "    if (c == 3u) return x < 0.0 ? 0.0 : pow(x, 2.8);\n"
    "    if (c == 4u) {\n"
    "        x = max(x, 0.0);\n"
    "        return x < 12.92 * SRGB_BETA ? x / 12.92\n"
    "            : pow((x + (SRGB_ALPHA - 1.0)) / SRGB_ALPHA, 2.4);\n"
    "    }\n"
    "    if (c == 5u) return x < 0.0 ? 0.0 : 52.37 / 48.0 * pow(x, 2.6);\n"
    "    if (c == 6u) return x <= 0.0 ? 0.01 : pow(10.0, 2.0 * (x - 1.0));\n"
    "    if (c == 7u) return x <= 0.0 ? 0.00316227766 : pow(10.0, 2.5 * (x - 1.0));\n"
    "    if (c == 8u) {\n"
    /* Negatives filtered rather than clamped, so the pow below never sees one. */
    "        if (!(x > 0.0)) return 0.0;\n"
    "        float xpow = pow(x, 1.0 / ST2084_M2);\n"
    "        float num = max(xpow - ST2084_C1, 0.0);\n"
    "        float den = max(ST2084_C2 - ST2084_C3 * xpow, 1.17549435e-38);\n"
    "        return pow(num / den, 1.0 / ST2084_M1);\n"
    "    }\n"
    "    if (c == 9u) {\n"
    "        float v = b67InverseOetf(x);\n"
    "        return v < 0.0 ? v : pow(v, 1.2);\n"
    "    }\n"
    "    if (c == 10u) {\n"
    /* xvYCC: Rec.1886 inside [0,1], the sign-extended 709 OETF pair outside. */
    "        if (x < 0.0 || x > 1.0) {\n"
    "            float a = abs(x);\n"
    "            float v = a < 4.5 * CL_B ? a / 4.5 : pow((a + (CL_A - 1.0)) / CL_A, 1.0 / 0.45);\n"
    "            return x < 0.0 ? -v : v;\n"
    "        }\n"
    "        return pow(x, 2.4);\n"
    "    }\n"
    "    return x;\n"
    "}\n"
    "\n"
    "float toGamma(uint c, float x) {\n"
    "    if (c == 1u) return x < 0.0 ? 0.0 : pow(x, 1.0 / 2.4);\n"
    "    if (c == 2u) return x < 0.0 ? 0.0 : pow(x, 1.0 / 2.2);\n"
    "    if (c == 3u) return x < 0.0 ? 0.0 : pow(x, 1.0 / 2.8);\n"
    "    if (c == 4u) {\n"
    "        x = max(x, 0.0);\n"
    "        return x < SRGB_BETA ? x * 12.92\n"
    "            : SRGB_ALPHA * pow(x, 1.0 / 2.4) - (SRGB_ALPHA - 1.0);\n"
    "    }\n"
    "    if (c == 5u) return x < 0.0 ? 0.0 : pow(x * (48.0 / 52.37), 1.0 / 2.6);\n"
    "    if (c == 6u) return x <= 0.01 ? 0.0 : 1.0 + log2(x) / (log2(10.0) * 2.0);\n"
    "    if (c == 7u) return x <= 0.00316227766 ? 0.0 : 1.0 + log2(x) / (log2(10.0) * 2.5);\n"
    "    if (c == 8u) {\n"
    "        if (!(x > 0.0)) return 0.0;\n"
    "        float xpow = pow(x, ST2084_M1);\n"
    /* zimg's rearrangement rather than the published form: it avoids a cancellation the
       straight ratio suffers near the bottom of the range. */
    "        float num = (ST2084_C1 - 1.0) + (ST2084_C2 - ST2084_C3) * xpow;\n"
    "        float den = 1.0 + ST2084_C3 * xpow;\n"
    "        return pow(1.0 + num / den, ST2084_M2);\n"
    "    }\n"
    "    if (c == 9u) return b67Oetf(x < 0.0 ? x : pow(x, 1.0 / 1.2));\n"
    "    if (c == 10u) {\n"
    "        if (x < 0.0 || x > 1.0) {\n"
    "            float a = abs(x);\n"
    "            float v = a < CL_B ? a * 4.5 : CL_A * pow(a, 0.45) - (CL_A - 1.0);\n"
    "            return x < 0.0 ? -v : v;\n"
    "        }\n"
    "        return pow(x, 1.0 / 2.4);\n"
    "    }\n"
    "    return x;\n"
    "}\n";

/* The 709 OETF constants serve three masters -- constant luminance, xvYCC's extension
   and nothing else -- so they sit ahead of both curve functions. */
const char colourCurvePrelude[] =
    "const float CL_A = 1.09929682680944;\n"
    "const float CL_B = 0.018053968510807;\n"
    "float clOetf(float x) {\n"
    "    x = max(x, 0.0);\n"
    "    return x < CL_B ? x * 4.5 : CL_A * pow(x, 0.45) - (CL_A - 1.0);\n"
    "}\n"
    "float clInverseOetf(float x) {\n"
    "    x = max(x, 0.0);\n"
    "    return x < 4.5 * CL_B ? x / 4.5 : pow((x + (CL_A - 1.0)) / CL_A, 1.0 / 0.45);\n"
    "}\n";

struct ColourPush {
    uint32_t width, height;
    uint32_t inStride[3];
    uint32_t outStride[3];
};

const char colourMain[] =
    "void main() {\n"
    "    uint x = gl_GlobalInvocationID.x;\n"
    "    uint y = gl_GlobalInvocationID.y;\n"
    "    if (x >= pc.width || y >= pc.height) return;\n"
    "    float v0 = float(in0[y * pc.inStride[0] + x]);\n"
    "    float v1 = (cb.flags & 64u) != 0u ? 0.0 : float(in1[y * pc.inStride[1] + x]);\n"
    "    float v2 = (cb.flags & 128u) != 0u ? 0.0 : float(in2[y * pc.inStride[2] + x]);\n"
    "    v0 = v0 * cb.inScale[0] + cb.inOffset[0];\n"
    "    v1 = v1 * cb.inScale[1] + cb.inOffset[1];\n"
    "    v2 = v2 * cb.inScale[2] + cb.inOffset[2];\n"
    "\n"
    "    float r, g, b;\n"
    "    if ((cb.flags & 16u) != 0u) {\n"
    /* Constant luminance in: luma is the encoded luminance of the linear pixel and
       chroma are differences taken after encoding, so the curve lives inside the
       transform and what comes out is linear light. */
    "        float bmy = v1 < 0.0 ? v1 * 2.0 * cb.cl[3] : v1 * 2.0 * cb.cl[4];\n"
    "        float rmy = v2 < 0.0 ? v2 * 2.0 * cb.cl[5] : v2 * 2.0 * cb.cl[6];\n"
    "        b = clInverseOetf(bmy + v0);\n"
    "        r = clInverseOetf(rmy + v0);\n"
    "        float yl = clInverseOetf(v0);\n"
    "        g = (yl - cb.cl[0] * r - cb.cl[2] * b) / cb.cl[1];\n"
    "    } else {\n"
    "        if ((cb.flags & 1u) != 0u) {\n"
    "            r = cb.inMat[0] * v0 + cb.inMat[1] * v1 + cb.inMat[2] * v2;\n"
    "            g = cb.inMat[3] * v0 + cb.inMat[4] * v1 + cb.inMat[5] * v2;\n"
    "            b = cb.inMat[6] * v0 + cb.inMat[7] * v1 + cb.inMat[8] * v2;\n"
    "        } else {\n"
    "            r = v0; g = v1; b = v2;\n"
    "        }\n"
    "        if ((cb.flags & 256u) != 0u) {\n"
    /* Display-referred HLG through the luminance: per-channel inverse OETF, one OOTF
       power on the luma-weighted sum, the peak scale at the end -- transcribed from
       AribB67InverseOperationC. */
    "            r = b67InverseOetf(r);\n"
    "            g = b67InverseOetf(g);\n"
    "            b = b67InverseOetf(b);\n"
    "            float ys = pow(max(cb.ootfIn[0] * r + cb.ootfIn[1] * g + cb.ootfIn[2] * b, 1.17549435e-38), 0.2);\n"
    "            r = r * ys * cb.curveScaleIn;\n"
    "            g = g * ys * cb.curveScaleIn;\n"
    "            b = b * ys * cb.curveScaleIn;\n"
    "        } else if ((cb.flags & 4u) != 0u) {\n"
    "            r = toLinear(cb.curveIn, r) * cb.curveScaleIn;\n"
    "            g = toLinear(cb.curveIn, g) * cb.curveScaleIn;\n"
    "            b = toLinear(cb.curveIn, b) * cb.curveScaleIn;\n"
    "        }\n"
    "    }\n"
    "    if ((cb.flags & 8u) != 0u) {\n"
    "        float rr = cb.gamut[0] * r + cb.gamut[1] * g + cb.gamut[2] * b;\n"
    "        float gg = cb.gamut[3] * r + cb.gamut[4] * g + cb.gamut[5] * b;\n"
    "        float bb = cb.gamut[6] * r + cb.gamut[7] * g + cb.gamut[8] * b;\n"
    "        r = rr; g = gg; b = bb;\n"
    "    }\n"
    "    float w0, w1, w2;\n"
    "    if ((cb.flags & 32u) != 0u) {\n"
    /* Constant luminance out: encode luminance, then take the differences in encoded
       space, scaled by whichever of two constants the sign selects. Its own constant
       set, since a CL to CL conversion has a live one on each side. */
    "        float yy = clOetf(cb.clOut[0] * r + cb.clOut[1] * g + cb.clOut[2] * b);\n"
    "        float be = clOetf(b) - yy;\n"
    "        float re = clOetf(r) - yy;\n"
    "        w0 = yy;\n"
    "        w1 = be < 0.0 ? be / (2.0 * cb.clOut[3]) : be / (2.0 * cb.clOut[4]);\n"
    "        w2 = re < 0.0 ? re / (2.0 * cb.clOut[5]) : re / (2.0 * cb.clOut[6]);\n"
    "    } else {\n"
    "        if ((cb.flags & 512u) != 0u) {\n"
    /* The forward luminance OOTF: peak scale first, then divide the OETF inputs by
       Yd^(1/6) -- transcribed from AribB67OperationC. */
    "            r = r * cb.curveScaleOut;\n"
    "            g = g * cb.curveScaleOut;\n"
    "            b = b * cb.curveScaleOut;\n"
    "            float yd = max(cb.ootfOut[0] * r + cb.ootfOut[1] * g + cb.ootfOut[2] * b, 1.17549435e-38);\n"
    "            float ysInv = pow(yd, (1.0 - 1.2) / 1.2);\n"
    "            r = b67Oetf(r * ysInv);\n"
    "            g = b67Oetf(g * ysInv);\n"
    "            b = b67Oetf(b * ysInv);\n"
    "        } else if ((cb.flags & 4u) != 0u) {\n"
    "            r = toGamma(cb.curveOut, r * cb.curveScaleOut);\n"
    "            g = toGamma(cb.curveOut, g * cb.curveScaleOut);\n"
    "            b = toGamma(cb.curveOut, b * cb.curveScaleOut);\n"
    "        }\n"
    "        if ((cb.flags & 2u) != 0u) {\n"
    "            w0 = cb.outMat[0] * r + cb.outMat[1] * g + cb.outMat[2] * b;\n"
    "            w1 = cb.outMat[3] * r + cb.outMat[4] * g + cb.outMat[5] * b;\n"
    "            w2 = cb.outMat[6] * r + cb.outMat[7] * g + cb.outMat[8] * b;\n"
    "        } else {\n"
    "            w0 = r; w1 = g; w2 = b;\n"
    "        }\n"
    "    }\n"
    /* Rounding is per plane: a plane written to the frame directly stores samples, one
       written to the working scratch stays float and must be neither rounded nor
       clamped. Dither lands just before the round, exactly where zimg's tables do. */
    "    w0 = w0 * cb.outScale[0] + cb.outOffset[0];\n"
    "#if DST0_INT\n"
    "#if DITHER_MODE > 0\n"
    "    if (cb.ditherOn[0] != 0u) w0 += ditherLookup(cb.ditherArg[0], x, y);\n"
    "#endif\n"
    "    w0 = cb.roundEven[0] != 0u ? clamp(roundEven(w0), 0.0, PEAK) : clamp(floor(w0 + 0.5), 0.0, PEAK);\n"
    "#endif\n"
    "    out0[y * pc.outStride[0] + x] = DST0_T(w0);\n"
    "#if OUT_PLANES > 1\n"
    "    w1 = w1 * cb.outScale[1] + cb.outOffset[1];\n"
    "    w2 = w2 * cb.outScale[2] + cb.outOffset[2];\n"
    "#if DST1_INT\n"
    "#if DITHER_MODE > 0\n"
    "    if (cb.ditherOn[1] != 0u) w1 += ditherLookup(cb.ditherArg[1], x, y);\n"
    "#endif\n"
    "    w1 = cb.roundEven[1] != 0u ? clamp(roundEven(w1), 0.0, PEAK) : clamp(floor(w1 + 0.5), 0.0, PEAK);\n"
    "#endif\n"
    "#if DST2_INT\n"
    "#if DITHER_MODE > 0\n"
    "    if (cb.ditherOn[2] != 0u) w2 += ditherLookup(cb.ditherArg[2], x, y);\n"
    "#endif\n"
    "    w2 = cb.roundEven[2] != 0u ? clamp(roundEven(w2), 0.0, PEAK) : clamp(floor(w2 + 0.5), 0.0, PEAK);\n"
    "#endif\n"
    "    out1[y * pc.outStride[1] + x] = DST1_T(w1);\n"
    "    out2[y * pc.outStride[2] + x] = DST2_T(w2);\n"
    "#endif\n"
    "}\n";


std::string colourShaderSource(const char *srcT[3], const char *dstT[3], const bool dstInt[3],
    int outPlanes, int dstIntBits, int ditherMode) {
    std::string s = "#version 460\n";
    s += "#extension GL_EXT_shader_8bit_storage : require\n"
         "#extension GL_EXT_shader_16bit_storage : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
         "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n";
    bool anyHalf = false;
    for (int i = 0; i < 3; i++)
        anyHalf = anyHalf || !strcmp(srcT[i], "float16_t") || (i < outPlanes && !strcmp(dstT[i], "float16_t"));
    if (anyHalf)
        s += "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";
    for (int i = 0; i < 3; i++)
        s += "#define SRC" + std::to_string(i) + "_T " + srcT[i] + "\n";
    for (int i = 0; i < outPlanes; i++) {
        s += "#define DST" + std::to_string(i) + "_T " + dstT[i] + "\n";
        s += "#define DST" + std::to_string(i) + "_INT " + (dstInt[i] ? "1" : "0") + "\n";
    }
    s += "#define OUT_PLANES " + std::to_string(outPlanes) + "\n";
    s += "#define DITHER_MODE " + std::to_string(ditherMode) + "\n";
    if (dstIntBits > 0)
        s += "#define PEAK " + std::to_string((1u << dstIntBits) - 1u) + ".0\n";
    s += "layout(local_size_x = " + std::to_string(resizeLocalSize) +
         ", local_size_y = " + std::to_string(resizeLocalSize) + ") in;\n";
    for (int i = 0; i < 3; i++)
        s += "layout(std430, set = 0, binding = " + std::to_string(i) + ") readonly buffer In" +
             std::to_string(i) + " { SRC" + std::to_string(i) + "_T in" + std::to_string(i) + "[]; };\n";
    for (int i = 0; i < outPlanes; i++)
        s += "layout(std430, set = 0, binding = " + std::to_string(3 + i) + ") writeonly buffer Out" +
             std::to_string(i) + " { DST" + std::to_string(i) + "_T out" + std::to_string(i) + "[]; };\n";
    s += "layout(std430, set = 0, binding = " + std::to_string(3 + outPlanes) + ") readonly buffer CB {\n"
         "    float inMat[9]; float gamut[9]; float outMat[9];\n"
         "    float inScale[3]; float inOffset[3]; float outScale[3]; float outOffset[3];\n"
         "    float cl[7]; float clOut[7];\n"
         "    float ootfIn[3]; float ootfOut[3];\n"
         "    float curveScaleIn; float curveScaleOut;\n"
         "    uint curveIn; uint curveOut; uint flags; uint roundEven[3];\n"
         "    uint ditherArg[3]; uint ditherOn[3];\n"
         "} cb;\n";
    if (ditherMode > 0)
        s += "layout(std430, set = 0, binding = " + std::to_string(3 + outPlanes + 1) +
             ") readonly buffer Dither { float ditherTable[]; };\n";
    s += "layout(push_constant, std430) uniform PC {\n"
         "    uint width, height;\n"
         "    uint inStride[3];\n"
         "    uint outStride[3];\n"
         "} pc;\n\n";
    s += colourCurvePrelude;
    s += colourCurvesGlsl;
    s += ditherLookupGlsl;
    s += colourMain;
    return s;
}

/* ------------------------------------------------------------------------------------------
   The spec: everything the arguments settle, resolved once at create. */

/* What a colour conversion adds: a fused pointwise pass at the working size, between the
   per-plane resample stages. The input side is a per-frame fact (the frame names its
   matrix and transfer); the output side is settled by arguments -- or, for a conversion
   that keeps the YUV encoding, by the same per-frame answer as the input. */
struct ColourConfig {
    bool active = false;
    uint32_t workW = 0, workH = 0;
    /* The working state carries the active region of whichever side is SMALLER per axis,
       exactly as graphbuilder builds its float 4:4:4 state -- that is what lets a window
       ride an upscaling conversion and still be consumed by exactly one resample per
       plane: the stage that crosses from the windowed state to the unwindowed one. */
    double workActiveLeft = 0, workActiveTop = 0, workActiveWidth = 0, workActiveHeight = 0;
    /* The working families: 4:4:4 float32, gray widened to YUV so the matrices have
       three planes to read and write. */
    int workInFamily = cfYUV, workOutFamily = cfYUV;
    bool inMatActive = false, outMatActive = false;

    /* MatFrame resolves the frame's own _Matrix, whatever kind of matrix that names --
       ordinary, constant luminance, chromaticity derived or ICtCp; the argument-named
       kinds are settled at create. MatFrameSame is an output that re-encodes with the
       input's per-frame answer, which is what a transfer or gamut change on YUV means. */
    enum MatKind { MatFrame, MatFrameSame, MatFixed, MatNclPrim, MatCL2020, MatCLPrim, MatICtCp };
    MatKind inKind = MatFrame;    /* meaningful when inMatActive */
    MatKind outKind = MatFixed;   /* meaningful when outMatActive */
    int64_t inMatrixFallback = VSC_MATRIX_UNSPECIFIED;   /* matrix_in */
    int64_t outMatrixValue = VSC_MATRIX_RGB;             /* stated; also the tag */

    bool doTransfer = false;
    int outCurve = -1;                 /* stated transfer's curve */
    int64_t outTransferValue = -1;     /* stated transfer's tag */
    int64_t transferInFallback = -1;   /* transfer_in, for a frame naming none */

    bool doGamut = false;
    int64_t outPrimValue = -1;         /* stated primaries; also the tag */
    int64_t primInFallback = -1;       /* primaries_in */

    double peakLuminance = 100.0;

    /* Which planes need a resample stage on each side of the colour pass; the others are
       read from or written to the frames directly, with the numbering affine folded into
       the colour constants. All create-time facts: sizes and the window decide them,
       siting only moves shifts. */
    bool needIn[3] = {}, needOut[3] = {};
    bool inZero[3] = {};               /* chroma a grey source does not have */
};

enum class DitherMode { None, Ordered, Random };

/* zimg's Bayer table from depth/dither.cpp, with its four variants -- original,
   horizontal flip, vertical flip, transpose -- selected per plane; values become
   (v + 1) / 257 - 0.5. The blue noise table for random dither is vendored whole. */
constexpr uint8_t bayerTable[16][16] = {
    {   0, 192,  48, 240,  12, 204,  60, 252,   3, 195,  51, 243,  15, 207,  63, 255, },
    { 128,  64, 176, 112, 140,  76, 188, 124, 131,  67, 179, 115, 143,  79, 191, 127, },
    {  32, 224,  16, 208,  44, 236,  28, 220,  35, 227,  19, 211,  47, 239,  31, 223, },
    { 160,  96, 144,  80, 172, 108, 156,  92, 163,  99, 147,  83, 175, 111, 159,  95, },
    {   8, 200,  56, 248,   4, 196,  52, 244,  11, 203,  59, 251,   7, 199,  55, 247, },
    { 136,  72, 184, 120, 132,  68, 180, 116, 139,  75, 187, 123, 135,  71, 183, 119, },
    {  40, 232,  24, 216,  36, 228,  20, 212,  43, 235,  27, 219,  39, 231,  23, 215, },
    { 168, 104, 152,  88, 164, 100, 148,  84, 171, 107, 155,  91, 167, 103, 151,  87, },
    {   2, 194,  50, 242,  14, 206,  62, 254,   1, 193,  49, 241,  13, 205,  61, 253, },
    { 130,  66, 178, 114, 142,  78, 190, 126, 129,  65, 177, 113, 141,  77, 189, 125, },
    {  34, 226,  18, 210,  46, 238,  30, 222,  33, 225,  17, 209,  45, 237,  29, 221, },
    { 162,  98, 146,  82, 174, 110, 158,  94, 161,  97, 145,  81, 173, 109, 157,  93, },
    {  10, 202,  58, 250,   6, 198,  54, 246,   9, 201,  57, 249,   5, 197,  53, 245, },
    { 138,  74, 186, 122, 134,  70, 182, 118, 137,  73, 185, 121, 133,  69, 181, 117, },
    {  42, 234,  26, 218,  38, 230,  22, 214,  41, 233,  25, 217,  37, 229,  21, 213, },
    { 170, 106, 154,  90, 166, 102, 150,  86, 169, 105, 153,  89, 165, 101, 149,  85, },
};

std::vector<float> buildDitherTable(DitherMode mode) {
    std::vector<float> table;
    if (mode == DitherMode::Ordered) {
        table.resize(16 * 16 * 4);
        auto value = [](int r, int c) { return (bayerTable[r][c] + 1) / 257.0f - 0.5f; };
        for (int r = 0; r < 16; r++) {
            for (int c = 0; c < 16; c++) {
                table[0 * 256 + r * 16 + c] = value(r, c);
                table[1 * 256 + r * 16 + c] = value(r, 15 - c);   /* horizontal flip */
                table[2 * 256 + r * 16 + c] = value(15 - r, c);   /* vertical flip */
                table[3 * 256 + r * 16 + c] = value(c, r);        /* transposed */
            }
        }
    } else if (mode == DitherMode::Random) {
        table.resize(64 * 64);
        for (int i = 0; i < 64 * 64; i++)
            table[i] = (zimg::depth::blue_noise_table[i / 64][i % 64] + 1) / 257.0f - 0.5f;
    }
    return table;
}

/* What the store's dither argument packs per plane: the variant base for Bayer, the row
   and column rotation for blue noise -- zimg's own per-plane sequence tables. */
uint32_t ditherArgFor(DitherMode mode, int plane) {
    if (mode == DitherMode::Ordered)
        return static_cast<uint32_t>((plane % 4) * 256);
    static constexpr uint32_t offsets[4] = { (0u << 8) | 0u, (32u << 8) | 12u, (16u << 8) | 55u, (48u << 8) | 26u };
    return offsets[plane % 4];
}

/* zimg only dithers a conversion that loses information: an identical format is a no-op
   before dither is even considered, anything to float is exact, and an integer widening
   between limited-range formats is a left shift. */
bool ditherApplies(DitherMode mode, const VSVideoFormat &srcFmt, const VSVideoFormat &dstFmt,
    bool fullIn, bool fullOut) {
    if (mode == DitherMode::None || dstFmt.sampleType != stInteger)
        return false;
    if (srcFmt.sampleType == dstFmt.sampleType && srcFmt.bitsPerSample == dstFmt.bitsPerSample &&
        fullIn == fullOut)
        return false;
    const bool lossless = srcFmt.sampleType == stInteger && !fullIn && !fullOut &&
        dstFmt.bitsPerSample >= srcFmt.bitsPerSample;
    return !lossless;
}

struct ConversionSpec {
    VSVideoFormat srcFmt = {}, dstFmt = {};
    uint32_t srcW = 0, srcH = 0, dstW = 0, dstH = 0;
    double activeLeft = 0, activeTop = 0, activeWidth = 0, activeHeight = 0;
    bool windowed = false;
    KernelSpec kernelY, kernelUV;
    bool uvDiffers = false;
    int rangeOut = -1, rangeIn = -1;              /* VSC values; -1 unset */
    int64_t chromaLocOut = -1, chromaLocIn = -1;  /* VSC values; -1 unset */
    bool dropsChroma = false, fillsChroma = false;
    bool sameFamily = true, bothSubsampledYUV = false;
    bool hFirst = true;  /* pass order; the window is create-time so this is too */
    /* Bob: the input is one field, stored whole, and the output is a progressive frame
       of twice the height -- the one resize whose two ends do not share a parity. */
    bool deinterlace = false;
    DitherMode dither = DitherMode::None;
    /* matrix_in, transfer_in and primaries_in when nothing is converted: they say what the
       source is, so with no output side asked for they are simply what an untagged frame
       carries out. VSC values; -1 unset. Only the colour-inactive path uses these -- a
       conversion decides its own tags. */
    int64_t tagMatrixIn = -1, tagTransferIn = -1, tagPrimariesIn = -1;
    ColourConfig colour;
};

/* Everything only the frame knows, resolved once per frame. */
struct FrameState {
    int sitingW = sitingLeft, sitingH = sitingMiddle;        /* input chroma */
    int dstSitingW = sitingLeft, dstSitingH = sitingMiddle;  /* output chroma */
    int64_t locOut = VSC_CHROMA_LEFT;  /* what _ChromaLocation the output carries */
    bool fullIn = false, fullOut = false;
    bool ycgco = false;      /* the resolved input matrix is YCgCo */
    bool ycgcoOut = false;   /* ... and the resolved output matrix, colour path only */
    Parity parity = Parity::Progressive;
    Parity dstParity = Parity::Progressive;  /* differs from parity exactly for bob */
    /* The colour pass's constants and the tags it decides, built per frame since the
       input side follows the frame's own properties. */
    ColourConstants consts = {};
    int64_t colourMatrixTag = -1, colourTransferTag = -1, colourPrimariesTag = -1;
};

float rec709OetfHost(double x) {
    constexpr double a = 1.09929682680944, b = 0.018053968510807;
    x = std::max(x, 0.0);
    return static_cast<float>(x < b ? x * 4.5 : a * std::pow(x, 0.45) - (a - 1.0));
}

/* The zimg transfer enums the ICtCp matrices are keyed on. */
bool zimgTransferOfValue(int64_t value, zimg::colorspace::TransferCharacteristics *out) {
    using TC = zimg::colorspace::TransferCharacteristics;
    if (value == VSC_TRANSFER_ST2084) {
        *out = TC::ST_2084;
        return true;
    }
    if (value == VSC_TRANSFER_ARIB_B67) {
        *out = TC::ARIB_B67;
        return true;
    }
    return false;
}

/* Builds the colour constants for one frame: which matrices, which curves, which affines.
   All the host-side mathematics of a conversion lives here, in doubles, leaving the
   kernel one fixed pipeline with identity defaults. The matrix KIND itself is per frame:
   a clip tagged _Matrix=2020cl converts through constant luminance without any argument
   saying so, exactly as the scalar path reads the tag. zimg's builders throw for
   underivable combinations (XYZ primaries have no luma coefficients), which surfaces as
   a frame error rather than a wrong picture. */
bool resolveColourFrame(const ConversionSpec &spec, const VSMap *props, const VSAPI *vsapi,
    FrameState *st, std::string &error) {
    using namespace zimg::colorspace;
    const ColourConfig &cc = spec.colour;
    ColourConstants &cb = st->consts;
    cb = {};
    int err;

    auto readProp = [&](const char *key, int64_t unspec, int64_t fallback) {
        int64_t v = vsapi->mapGetInt(props, key, 0, &err);
        if (err || v == unspec)
            v = fallback;
        return v;
    };
    auto copyMat = [](float *out, const Matrix3x3 &m) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                out[r * 3 + c] = static_cast<float>(m[r][c]);
    };

    int64_t inPrimValue = -1;
    /* The soft probe leaves inPrimValue unset without erroring: the b67 OOTF selection
       asks whether the primaries are KNOWN, which is a different question from needing
       them. */
    auto probeInPrimaries = [&](ColorPrimaries *out) -> bool {
        const int64_t v = readProp("_Primaries", VSC_PRIMARIES_UNSPECIFIED,
            cc.primInFallback >= 0 ? cc.primInFallback : VSC_PRIMARIES_UNSPECIFIED);
        if (v == VSC_PRIMARIES_UNSPECIFIED || !primariesOfValue(v, out))
            return false;
        inPrimValue = v;
        return true;
    };
    auto resolveInPrimaries = [&](ColorPrimaries *out) -> bool {
        if (probeInPrimaries(out))
            return true;
        error = "Resize: this conversion needs primaries; pass primaries_in or set "
                "_Primaries on the frame";
        return false;
    };
    /* Where an output-side stage keyed on primaries takes them from: a gamut change has
       already retagged them by then, so it reads the stated output primaries; otherwise
       the frame's own, exactly as the scalar path reads the rewritten tag. */
    auto resolveOutSidePrimaries = [&](ColorPrimaries *out) -> bool {
        if (cc.doGamut)
            return primariesOfValue(cc.outPrimValue, out);
        return resolveInPrimaries(out);
    };
    auto probeOutSidePrimaries = [&](ColorPrimaries *out) -> bool {
        if (cc.doGamut)
            return primariesOfValue(cc.outPrimValue, out);
        return probeInPrimaries(out);
    };
    auto fillCL = [&](float *target, bool derived, bool inputSide) -> bool {
        Matrix3x3 m;
        if (derived) {
            ColorPrimaries p;
            if (inputSide ? !resolveInPrimaries(&p) : !resolveOutSidePrimaries(&p)) {
                if (error.empty())
                    error = "Resize: the constant luminance coefficients could not be derived";
                return false;
            }
            m = ncl_rgb_to_yuv_matrix_from_primaries(p);
        } else {
            m = ncl_rgb_to_yuv_matrix(MatrixCoefficients::REC_2020_CL);
        }
        const double kr = m[0][0], kb = m[0][2];
        target[0] = static_cast<float>(kr);
        target[1] = static_cast<float>(1.0 - kr - kb);
        target[2] = static_cast<float>(kb);
        target[3] = rec709OetfHost(1.0 - kb);
        target[4] = 1.0f - rec709OetfHost(kb);
        target[5] = rec709OetfHost(1.0 - kr);
        target[6] = 1.0f - rec709OetfHost(kr);
        return true;
    };

    /* What kind of matrix a per-frame value names. */
    auto kindOfValue = [](int64_t v, ColourConfig::MatKind fallback) {
        if (v == VSC_MATRIX_BT2020_CL)
            return ColourConfig::MatCL2020;
        if (v == VSC_MATRIX_CHROMATICITY_DERIVED_CL)
            return ColourConfig::MatCLPrim;
        if (v == VSC_MATRIX_CHROMATICITY_DERIVED_NCL)
            return ColourConfig::MatNclPrim;
        if (v == VSC_MATRIX_ICTCP)
            return ColourConfig::MatICtCp;
        return fallback;
    };

    bool ycgcoIn = false;
    bool clIn = false, clOut = false, ictcpIn = false, ictcpOut = false;
    int64_t inMatValue = -1, outMatValue = -1;
    int64_t inTransferValue = -1;
    Matrix3x3 postInLms = {}, preOutLms = {};
    bool postInActive = false, preOutActive = false;

    /* Both ICtCp legs need the transfer settled before the matrices, since the matrices
       are keyed on it; read it once up front and let the curve step reuse the answer. */
    auto resolveInTransfer = [&]() -> bool {
        if (inTransferValue >= 0)
            return true;
        inTransferValue = readProp("_Transfer", VSC_TRANSFER_UNSPECIFIED,
            cc.transferInFallback >= 0 ? cc.transferInFallback : VSC_TRANSFER_UNSPECIFIED);
        if (inTransferValue == VSC_TRANSFER_UNSPECIFIED) {
            inTransferValue = -1;
            error = "Resize: converting the transfer needs to know what the source "
                    "is; pass transfer_in or set _Transfer on the frame";
            return false;
        }
        if (transferCurveOfValue(inTransferValue) < 0) {
            error = "Resize: transfer " + std::to_string(inTransferValue) +
                    " is not implemented on the compute path";
            return false;
        }
        return true;
    };

    try {
        /* --- the input matrix, out of YUV or ICtCp */
        if (cc.inMatActive) {
            ColourConfig::MatKind kind = cc.inKind;
            int64_t v;
            switch (kind) {
            case ColourConfig::MatCL2020: v = VSC_MATRIX_BT2020_CL; break;
            case ColourConfig::MatCLPrim: v = VSC_MATRIX_CHROMATICITY_DERIVED_CL; break;
            case ColourConfig::MatNclPrim: v = VSC_MATRIX_CHROMATICITY_DERIVED_NCL; break;
            case ColourConfig::MatICtCp: v = VSC_MATRIX_ICTCP; break;
            default:
                v = readProp("_Matrix", VSC_MATRIX_UNSPECIFIED, cc.inMatrixFallback);
                if (v == VSC_MATRIX_UNSPECIFIED) {
                    error = "Resize: this conversion needs a matrix; pass matrix_in or "
                            "set _Matrix on the frame";
                    return false;
                }
                kind = kindOfValue(v, ColourConfig::MatFrame);
                break;
            }
            inMatValue = v;

            switch (kind) {
            case ColourConfig::MatNclPrim: {
                ColorPrimaries p;
                if (!resolveInPrimaries(&p))
                    return false;
                copyMat(cb.inMat, ncl_yuv_to_rgb_matrix_from_primaries(p));
                cb.flags |= FLAG_IN_MAT;
                break;
            }
            case ColourConfig::MatCL2020:
            case ColourConfig::MatCLPrim:
                if (!fillCL(cb.cl, kind == ColourConfig::MatCLPrim, true))
                    return false;
                cb.flags |= FLAG_CL_IN;
                clIn = true;
                break;
            case ColourConfig::MatICtCp: {
                /* Rec.2100 reaches ICtCp through LMS: the near matrix takes the samples
                   into gamma-encoded LMS, the curve step runs THERE, and the LMS to RGB
                   leg rides the mid matrix. Only legal over BT.2020 primaries with the
                   PQ or HLG curve -- is_valid_ictcp, enforced with real errors since the
                   scalar path refuses it too. */
                if (!resolveInTransfer())
                    return false;
                TransferCharacteristics tc;
                if (!zimgTransferOfValue(inTransferValue, &tc)) {
                    error = "Resize: ICtCp needs an ST 2084 or ARIB B67 transfer";
                    return false;
                }
                ColorPrimaries p;
                if (!resolveInPrimaries(&p))
                    return false;
                if (p != ColorPrimaries::REC_2020) {
                    error = "Resize: ICtCp needs BT.2020 primaries";
                    return false;
                }
                copyMat(cb.inMat, ictcp_to_lms_matrix(tc));
                cb.flags |= FLAG_IN_MAT;
                postInLms = ncl_yuv_to_rgb_matrix(MatrixCoefficients::REC_2100_LMS);
                postInActive = true;
                ictcpIn = true;
                break;
            }
            default: {
                MatrixCoefficients mc;
                if (!matrixCoeffOfValue(v, &mc)) {
                    error = "Resize: matrix " + std::to_string(v) +
                            " is not implemented on the compute path";
                    return false;
                }
                copyMat(cb.inMat, ncl_yuv_to_rgb_matrix(mc));
                cb.flags |= FLAG_IN_MAT;
                ycgcoIn = v == VSC_MATRIX_YCGCO;
                break;
            }
            }
        }

        /* --- the output matrix kind, resolved before the curves because an ICtCp
           output moves the encode into LMS. */
        ColourConfig::MatKind outKind = cc.outKind;
        if (cc.outMatActive) {
            int64_t v;
            if (outKind == ColourConfig::MatFrameSame) {
                /* A transfer or gamut change that keeps the YUV encoding: back in
                   through whatever the input resolved to. */
                v = inMatValue;
                outKind = kindOfValue(v, ColourConfig::MatFixed);
            } else {
                v = cc.outMatrixValue;
                outKind = kindOfValue(v, outKind);
            }
            outMatValue = v;
            clOut = outKind == ColourConfig::MatCL2020 || outKind == ColourConfig::MatCLPrim;
            ictcpOut = outKind == ColourConfig::MatICtCp;
            st->ycgcoOut = v == VSC_MATRIX_YCGCO;
        }

        /* --- the curves. A stage that would decode and re-encode the same curve with
           nothing between is skipped rather than trusted to be near enough identity. */
        const bool linearPassage = cc.doTransfer || cc.doGamut || clIn || clOut ||
            ictcpIn || ictcpOut;
        int curveIn = curveLinear, curveOut = curveLinear;
        int64_t outTransferValue = cc.doTransfer ? cc.outTransferValue : -1;
        if (linearPassage) {
            const bool decodeNeeded = !clIn;
            const bool encodeNeeded = !clOut;
            const bool needFrameCurve = decodeNeeded || (encodeNeeded && !cc.doTransfer);
            if (needFrameCurve) {
                if (!resolveInTransfer())
                    return false;
                const int c = transferCurveOfValue(inTransferValue);
                curveIn = decodeNeeded ? c : curveLinear;
                curveOut = cc.doTransfer ? cc.outCurve : c;
                if (outTransferValue < 0 && encodeNeeded)
                    outTransferValue = inTransferValue;
            } else {
                curveIn = curveLinear;
                curveOut = cc.doTransfer ? cc.outCurve : curveLinear;
            }
            if (clOut)
                curveOut = curveLinear;
            /* An ICtCp output must encode with PQ or HLG whatever the transfer argument
               said; the create checks let only those through, and zimg refuses the rest
               the same way. */
            if (ictcpOut) {
                TransferCharacteristics tc;
                if (!zimgTransferOfValue(outTransferValue, &tc)) {
                    error = "Resize: ICtCp needs an ST 2084 or ARIB B67 transfer";
                    return false;
                }
            }
            const bool identity = curveIn == curveOut && !cc.doGamut && !clIn && !clOut &&
                !ictcpIn && !ictcpOut;
            if (!identity) {
                cb.flags |= FLAG_CURVES;
                cb.curveIn = static_cast<uint32_t>(curveIn);
                cb.curveOut = static_cast<uint32_t>(curveOut);
                cb.curveScaleIn = static_cast<float>(curveScaleOf(curveIn, cc.peakLuminance).toLinear);
                cb.curveScaleOut = static_cast<float>(curveScaleOf(curveOut, cc.peakLuminance).toGamma);

                /* HLG decodes and encodes through the luminance-weighted OOTF whenever
                   the side's primaries are known -- zimg's use_display_referred_b67 with
                   exact gamma -- and per channel otherwise. ICtCp's LMS legs force
                   BT.2020, so they always take the luminance form, weights and all. */
                if (curveIn == curveAribB67) {
                    ColorPrimaries p;
                    if (ictcpIn ? (p = ColorPrimaries::REC_2020, true) : probeInPrimaries(&p)) {
                        const Matrix3x3 m = ncl_rgb_to_yuv_matrix_from_primaries(p);
                        cb.ootfIn[0] = static_cast<float>(m[0][0]);
                        cb.ootfIn[1] = static_cast<float>(m[0][1]);
                        cb.ootfIn[2] = static_cast<float>(m[0][2]);
                        cb.flags |= FLAG_B67L_IN;
                    }
                }
                if (curveOut == curveAribB67) {
                    ColorPrimaries p;
                    if (ictcpOut ? (p = ColorPrimaries::REC_2020, true) : probeOutSidePrimaries(&p)) {
                        const Matrix3x3 m = ncl_rgb_to_yuv_matrix_from_primaries(p);
                        cb.ootfOut[0] = static_cast<float>(m[0][0]);
                        cb.ootfOut[1] = static_cast<float>(m[0][1]);
                        cb.ootfOut[2] = static_cast<float>(m[0][2]);
                        cb.flags |= FLAG_B67L_OUT;
                    }
                }
            }
        }

        /* --- the mid matrix in linear light: any gamut change, with the LMS legs of
           ICtCp composed in on their sides. Composed WITHOUT the white point adaptation,
           because that is what the scalar path does: zimg reads chromatic_adaptation
           only from API 2.5 up and this build declares 2.4, so the field vsresize fills
           is never looked at. Measured rather than inferred, and a shared colour shift
           beats a visible one between the two paths. */
        Matrix3x3 mid = {};
        bool midActive = false;
        if (postInActive) {
            mid = postInLms;
            midActive = true;
        }
        if (cc.doGamut) {
            ColorPrimaries inP, outP;
            if (!resolveInPrimaries(&inP))
                return false;
            if (!primariesOfValue(cc.outPrimValue, &outP)) {
                error = "Resize: primaries " + std::to_string(cc.outPrimValue) +
                        " is not implemented on the compute path";
                return false;
            }
            const Matrix3x3 g = gamut_xyz_to_rgb_matrix(outP) * gamut_rgb_to_xyz_matrix(inP);
            mid = midActive ? g * mid : g;
            midActive = true;
        }

        /* --- the output matrix, into YUV or ICtCp */
        if (cc.outMatActive) {
            switch (outKind) {
            case ColourConfig::MatNclPrim: {
                ColorPrimaries p;
                if (!resolveOutSidePrimaries(&p)) {
                    if (error.empty())
                        error = "Resize: a chromaticity derived matrix needs primaries; "
                                "pass primaries_in or set _Primaries on the frame";
                    return false;
                }
                copyMat(cb.outMat, ncl_rgb_to_yuv_matrix_from_primaries(p));
                cb.flags |= FLAG_OUT_MAT;
                break;
            }
            case ColourConfig::MatCL2020:
            case ColourConfig::MatCLPrim:
                if (!fillCL(cb.clOut, outKind == ColourConfig::MatCLPrim, false))
                    return false;
                cb.flags |= FLAG_CL_OUT;
                break;
            case ColourConfig::MatICtCp: {
                TransferCharacteristics tc;
                if (!zimgTransferOfValue(outTransferValue, &tc)) {
                    error = "Resize: ICtCp needs an ST 2084 or ARIB B67 transfer";
                    return false;
                }
                ColorPrimaries p;
                if (!resolveOutSidePrimaries(&p)) {
                    if (error.empty())
                        error = "Resize: ICtCp needs BT.2020 primaries";
                    return false;
                }
                if (p != ColorPrimaries::REC_2020) {
                    error = "Resize: ICtCp needs BT.2020 primaries";
                    return false;
                }
                copyMat(cb.outMat, lms_to_ictcp_matrix(tc));
                cb.flags |= FLAG_OUT_MAT;
                preOutLms = ncl_rgb_to_yuv_matrix(MatrixCoefficients::REC_2100_LMS);
                preOutActive = true;
                break;
            }
            default: {
                MatrixCoefficients mc;
                if (!matrixCoeffOfValue(outMatValue, &mc)) {
                    error = "Resize: matrix " + std::to_string(outMatValue) +
                            " is not implemented on the compute path";
                    return false;
                }
                copyMat(cb.outMat, ncl_rgb_to_yuv_matrix(mc));
                cb.flags |= FLAG_OUT_MAT;
                break;
            }
            }
        }
        if (preOutActive) {
            mid = midActive ? preOutLms * mid : preOutLms;
            midActive = true;
        }
        if (midActive) {
            copyMat(cb.gamut, mid);
            cb.flags |= FLAG_GAMUT;
        }
    } catch (const std::exception &e) {
        error = std::string("Resize: the colour constants could not be built: ") + e.what();
        return false;
    }

    /* The numbering affines for planes the colour pass touches directly; staged planes
       arrive in and leave in unity float already. */
    for (int p = 0; p < 3; p++) {
        cb.inScale[p] = 1.0f;
        cb.outScale[p] = 1.0f;
        cb.inOffset[p] = 0.0f;
        cb.outOffset[p] = 0.0f;
        if (!cc.needIn[p] && !cc.inZero[p] && p < spec.srcFmt.numPlanes) {
            const bool chroma = spec.srcFmt.colorFamily == cfYUV && p != 0;
            const ScaleOffset so = affineToUnity(spec.srcFmt, chroma, st->fullIn, ycgcoIn);
            cb.inScale[p] = so.scale;
            cb.inOffset[p] = so.offset;
        }
        if (!cc.needOut[p] && p < spec.dstFmt.numPlanes) {
            const bool chroma = spec.dstFmt.colorFamily == cfYUV && p != 0;
            const ScaleOffset so = affineFromUnity(spec.dstFmt, chroma, st->fullOut, st->ycgcoOut);
            cb.outScale[p] = so.scale;
            cb.outOffset[p] = so.offset;
        }
        cb.roundEven[p] = (!cc.needIn[p] && !cc.needOut[p] && !cc.inZero[p]) ? 1 : 0;
        /* The colour pipeline's own output is float mathematics, so any direct integer
           store is a lossy conversion by definition; staged planes dither in their out
           chain instead. */
        if (!cc.needOut[p] && spec.dither != DitherMode::None &&
            spec.dstFmt.sampleType == stInteger && p < spec.dstFmt.numPlanes) {
            cb.ditherOn[p] = 1;
            cb.ditherArg[p] = ditherArgFor(spec.dither, p);
            cb.roundEven[p] = 1;
        }
    }
    if (cc.inZero[1])
        cb.flags |= FLAG_IN1_ZERO;
    if (cc.inZero[2])
        cb.flags |= FLAG_IN2_ZERO;
    /* The input chains' affines read this too, so it has to reflect the RESOLVED matrix
       rather than only what the frame said. */
    st->ycgco = ycgcoIn;

    /* The tags the conversion decides rather than carries. A curve or primaries the
       fallback arguments resolved is stated too: the scalar path propagates the resolved
       value, not the raw property. */
    st->colourMatrixTag = spec.dstFmt.colorFamily == cfRGB ? VSC_MATRIX_RGB
        : cc.outMatActive ? outMatValue : -1;
    st->colourTransferTag = cc.doTransfer ? cc.outTransferValue
        : inTransferValue >= 0 ? inTransferValue : -1;
    st->colourPrimariesTag = cc.doGamut ? cc.outPrimValue
        : inPrimValue >= 0 ? inPrimValue : -1;
    return true;
}

bool resolveFrameState(const ConversionSpec &spec, const VSMap *props, const VSAPI *vsapi,
    FrameState *st, std::string &error) {
    int err;

    /* _Field names the frame as a single field, stored whole; _FieldBased says it holds
       two interleaved ones. The scalar path reads them in this order and never both. */
    if (vsapi->mapNumElements(props, "_Field") > 0) {
        const int64_t f = vsapi->mapGetInt(props, "_Field", 0, nullptr);
        if (f != 0 && f != 1) {
            error = "Resize: bad _Field value: " + std::to_string(f);
            return false;
        }
        st->parity = f == 1 ? Parity::TopField : Parity::BottomField;
    } else {
        const int64_t fb = vsapi->mapGetInt(props, "_FieldBased", 0, &err);
        if (!err && fb != VSC_FIELD_PROGRESSIVE) {
            if (fb != VSC_FIELD_BOTTOM && fb != VSC_FIELD_TOP) {
                error = "Resize: bad _FieldBased value: " + std::to_string(fb);
                return false;
            }
            /* Two fields, and then the chroma subsampling on top, both have to come out
               whole -- on each side with its own subsampling, since a conversion changes
               it. */
            if (spec.srcH % (2u << spec.srcFmt.subSamplingH) ||
                spec.dstH % (2u << spec.dstFmt.subSamplingH)) {
                error = "Resize: a field based frame needs a height that divides evenly "
                        "into two fields on every plane";
                return false;
            }
            st->parity = Parity::Interlaced;
        }
    }
    if (spec.deinterlace) {
        /* Bobbing needs a frame that IS one field. The scalar path phrases this as a
           fatal log, which aborts the process; unreachable through resize.Bob, since the
           SeparateFields it builds on always states _Field. */
        if (st->parity != Parity::TopField && st->parity != Parity::BottomField) {
            error = "Resize: deinterlacing needs a frame that is a single field; set "
                    "_Field, or use SeparateFields first";
            return false;
        }
        st->dstParity = Parity::Progressive;
    } else {
        st->dstParity = st->parity;
    }

    /* chromaloc_in stands in for a frame that states none, and left sited is what a
       subsampled format means when neither does. The frame outranks the argument, as
       with every other tag. */
    int64_t locIn = vsapi->mapGetInt(props, "_ChromaLocation", 0, &err);
    if (err || locIn < 0)
        locIn = spec.chromaLocIn >= 0 ? spec.chromaLocIn : VSC_CHROMA_LEFT;
    if (locIn > VSC_CHROMA_BOTTOM) {
        error = "Resize: bad _ChromaLocation value: " + std::to_string(locIn);
        return false;
    }
    splitChromaLocation(locIn, &st->sitingW, &st->sitingH);

    /* RGB is full range and everything else limited unless the frame says otherwise. */
    bool fullIn = spec.rangeIn >= 0 ? spec.rangeIn == VSC_RANGE_FULL
        : spec.srcFmt.colorFamily == cfRGB;
    if (vsapi->mapNumElements(props, "_Range") > 0) {
        const int64_t r = vsapi->mapGetInt(props, "_Range", 0, nullptr);
        if (r != VSC_RANGE_FULL && r != VSC_RANGE_LIMITED) {
            error = "Resize: bad _Range value: " + std::to_string(r);
            return false;
        }
        fullIn = r == VSC_RANGE_FULL;
    }
    st->fullIn = fullIn;
    /* Unstated, the output keeps the source's numbering while the colour family does;
       across a family change the scalar path takes the destination family's own default
       instead of carrying. */
    st->fullOut = spec.rangeOut >= 0 ? spec.rangeOut == VSC_RANGE_FULL
        : spec.sameFamily ? fullIn : spec.dstFmt.colorFamily == cfRGB;

    /* YCgCo puts chroma over the same span as luma, the one matrix that changes what a
       sample value means without a conversion. */
    const int64_t m = vsapi->mapGetInt(props, "_Matrix", 0, &err);
    st->ycgco = !err && m == VSC_MATRIX_YCGCO;

    /* Where the output's chroma sits: stated, carried between two subsampled YUV
       formats, or the destination format's own default. */
    st->locOut = spec.chromaLocOut >= 0 ? spec.chromaLocOut
        : spec.bothSubsampledYUV ? locIn : VSC_CHROMA_LEFT;
    splitChromaLocation(st->locOut, &st->dstSitingW, &st->dstSitingH);

    if (spec.colour.active && !resolveColourFrame(spec, props, vsapi, st, error))
        return false;
    return true;
}

/* ------------------------------------------------------------------------------------------
   The plan: a pure function of (spec, state). Up to two passes per plane, each one
   dispatch; a plane whose windows are all identities and whose affine is one shares its
   source plane instead of running anything. */

struct AxisPass {
    bool run = false;
    uint32_t srcDim = 0, dstDim = 0, taps = 0;
    float invScale = 1.0f, shift = 0.0f, kstep = 1.0f;
    float invScaleLo = 0.0f, shiftLo = 0.0f;
};

bool isIdentityWindow(uint32_t srcDim, uint32_t dstDim, double shift, double width) {
    return srcDim == dstDim && shift == 0.0 && width == static_cast<double>(srcDim);
}

AxisPass planAxis(const KernelSpec &k, uint32_t srcDim, uint32_t dstDim, double shift,
    double width) {
    AxisPass ax;
    ax.srcDim = srcDim;
    ax.dstDim = dstDim;
    if (isIdentityWindow(srcDim, dstDim, shift, width))
        return ax;
    const double scale = dstDim / width;
    const double step = std::min(scale, 1.0);
    ax.taps = std::max(static_cast<uint32_t>(std::ceil(k.support / step)) * 2u, 1u);
    const double invScale = width / dstDim;
    ax.invScale = static_cast<float>(invScale);
    ax.invScaleLo = static_cast<float>(invScale - ax.invScale);
    ax.shift = static_cast<float>(shift);
    ax.shiftLo = static_cast<float>(shift - ax.shift);
    ax.kstep = static_cast<float>(step);
    ax.run = true;
    return ax;
}

/* An identity gather: one tap, which the shader short-circuits to an exact copy. What a
   depth-only plane runs through, and what a chroma fill scales away. */
AxisPass identityAxis(uint32_t dim) {
    AxisPass ax;
    ax.srcDim = dim;
    ax.dstDim = dim;
    ax.taps = 1;
    ax.run = true;
    return ax;
}

/* Where a chain's ends live. WorkIn and WorkOut are the 4:4:4 float working planes either
   side of the colour pass; Mid is the shared intermediate inside one chain. */
struct Surface {
    enum Kind { SrcPlane, DstPlane, WorkIn, WorkOut };
    Kind kind = SrcPlane;
    int plane = 0;
};

struct PassPlan {
    bool vertical = false;
    AxisPass ax;
    uint32_t outW = 0, outH = 0;     /* the surface this pass writes */
    bool readsFrame = true;          /* the chain's source end; else the mid scratch */
    bool writesFrame = true;         /* the chain's destination end; else the mid scratch */
    bool fill = false;               /* reads luma and scales it away to neutral */
    float scale = 1.0f, offset = 0.0f;
    uint32_t roundEven = 0;
    uint32_t ditherArg = 0, ditherOn = 0;
    /* A field of an interleaved frame: base row plus every second row, on whichever ends
       are frames or field-interleaved scratch. */
    uint32_t srcRowOffset = 0, srcStrideMul = 1;
    uint32_t dstRowOffset = 0, dstStrideMul = 1;
};

struct PlanePlan {
    /* Up to three: an interleaved frame's vertical resample is one pass per field. */
    int count = 0;
    PassPlan pass[3];
    bool share = false;
    Surface from, to;   /* what readsFrame and writesFrame refer to */
};

struct FramePlan {
    /* The colour path's input chains, source planes to WorkIn; empty otherwise. */
    PlanePlan inChain[3];
    bool colourActive = false;
    Surface colourIn[3], colourOut[3];
    int colourOutPlanes = 3;
    /* The main chains: source to destination on the plain path, WorkOut to destination
       planes on the colour path. */
    PlanePlan plane[3];
    bool anyWork = false, anyShare = false;
    VkDeviceSize scratchBytes = 0;   /* the shared mid intermediate */
    bool workInUsed[3] = {}, workOutUsed[3] = {};
    uint32_t workW = 0, workH = 0;
    ColourConstants consts = {};
};

/* Builds the pass list for one plane's separable resample; returns the pass count, zero
   when both axes are identities. The affine and store rounding land on whatever writes
   the chain's destination. */
int buildChainPasses(PassPlan pass[3], const AxisPass &axH, const AxisPass &axVTop,
    const AxisPass &axVBottom, uint32_t srcPW, uint32_t srcPH, uint32_t dstPW, uint32_t dstPH,
    bool interlaced, bool hFirstPref, float scale, float offset, VkDeviceSize *midBytes) {
    if (!axH.run && !axVTop.run)
        return 0;

    auto emitVertical = [&](PassPlan &ps, const AxisPass &ax, int phase,
        bool readsFrame, bool writesFrame, uint32_t outW) {
        ps.vertical = true;
        ps.ax = ax;
        ps.outW = outW;
        ps.outH = interlaced ? dstPH / 2 : dstPH;
        ps.readsFrame = readsFrame;
        ps.writesFrame = writesFrame;
        if (interlaced) {
            ps.srcRowOffset = static_cast<uint32_t>(phase);
            ps.srcStrideMul = 2;
            ps.dstRowOffset = static_cast<uint32_t>(phase);
            ps.dstStrideMul = 2;
        }
    };

    const bool hFirst = !axVTop.run || (axH.run && hFirstPref);
    int n = 0;
    if (hFirst && axH.run) {
        PassPlan &ps = pass[n++];
        ps.ax = axH;
        ps.outW = dstPW;
        ps.outH = srcPH;
        ps.writesFrame = !axVTop.run;
    }
    if (axVTop.run) {
        /* The width the vertical work runs at is whatever the horizontal state is by
           then: destination width after an H pass, source width before. */
        const uint32_t vOutW = (hFirst && axH.run) ? dstPW : srcPW;
        emitVertical(pass[n], axVTop, 0, !(hFirst && axH.run), !(!hFirst && axH.run), vOutW);
        n++;
        if (interlaced) {
            emitVertical(pass[n], axVBottom, 1, !(hFirst && axH.run), !(!hFirst && axH.run), vOutW);
            n++;
        }
    }
    if (!hFirst && axH.run) {
        PassPlan &ps = pass[n++];
        ps.ax = axH;
        ps.outW = dstPW;
        ps.outH = dstPH;
        ps.readsFrame = false;
    }

    for (int i = 0; i < n; i++) {
        if (pass[i].writesFrame) {
            pass[i].scale = scale;
            pass[i].offset = offset;
        }
    }

    /* The intermediate is the full mid surface whichever stage writes it: an H pass
       covers every row of both fields at once, and per-field V passes interleave into it
       at twice the stride. */
    bool usesScratch = false;
    for (int i = 0; i < n; i++)
        usesScratch = usesScratch || !pass[i].writesFrame;
    if (usesScratch) {
        const uint32_t midW = pass[0].outW;
        const uint32_t midH = pass[0].vertical && interlaced ? pass[0].outH * 2 : pass[0].outH;
        *midBytes = std::max(*midBytes, static_cast<VkDeviceSize>(midW) * midH * sizeof(float));
    }
    return n;
}

/* The geometry of one plane's resample between two format sides, shared by the plain
   path and both phases of a conversion. Everything is doubles until it lands in push
   constants. */
struct ChainGeom {
    AxisPass axH, axVTop, axVBottom;
};

/* The active regions of one chain's two ends, in each side's own luma units. */
struct ChainActive {
    double srcL = 0, srcT = 0, srcW = 0, srcH = 0;
    double dstL = 0, dstT = 0, dstW = 0, dstH = 0;
};

ChainGeom chainGeometry(const KernelSpec &k, int cls, bool interlaced, Parity parIn, Parity parOut,
    uint32_t srcPW, uint32_t srcPH, uint32_t dstPW, uint32_t dstPH,
    int subInWBits, int subOutWBits, int subInHBits, int subOutHBits,
    int sitingInW, int sitingOutW, int sitingInH, int sitingOutH,
    const ChainActive &act) {
    /* At subscale 1 the siting offsets vanish, so luma and RGB's extra planes flow
       through the same expressions unshifted. */
    const double subInW = cls == 0 ? 1.0 : 1.0 / (1 << subInWBits);
    const double subOutW = cls == 0 ? 1.0 : 1.0 / (1 << subOutWBits);
    const double subInH = cls == 0 ? 1.0 : 1.0 / (1 << subInHBits);
    const double subOutH = cls == 0 ? 1.0 : 1.0 / (1 << subOutHBits);
    const double offInW = cls == 0 ? 0.0 : chromaOffsetW(sitingInW, subInW);
    const double offOutW = cls == 0 ? 0.0 : chromaOffsetW(sitingOutW, subOutW);

    ChainGeom g;
    const AxisWindow ww = axisWindow(subInW, subOutW, offInW, offOutW,
        act.srcL, act.srcW, act.dstL, act.dstW, dstPW);
    g.axH = planAxis(k, srcPW, dstPW, ww.shift, ww.width);

    /* The vertical axis is where parity lives. A whole frame that IS one field keeps its
       size and only moves a quarter line; an interleaved frame measures every vertical
       term within one field, halving the windows and both dimensions, and resamples each
       field with its own offsets. Both fields share their identity verdict, since only
       the sign of the offset differs and it appears on both ends. */
    auto verticalWindow = [&](Parity pi, Parity po) {
        const double offInV = cls == 0 ? lumaParityOffset(pi)
            : chromaParityOffset(pi, chromaOffsetH(sitingInH, subInH));
        const double offOutV = cls == 0 ? lumaParityOffset(po)
            : chromaParityOffset(po, chromaOffsetH(sitingOutH, subOutH));
        const double half = interlaced ? 0.5 : 1.0;
        const uint32_t srcDim = interlaced ? srcPH / 2 : srcPH;
        const uint32_t dstDim = interlaced ? dstPH / 2 : dstPH;
        const AxisWindow w = axisWindow(subInH, subOutH, offInV, offOutV,
            act.srcT * half, act.srcH * half, act.dstT * half, act.dstH * half, dstDim);
        return planAxis(k, srcDim, dstDim, w.shift, w.width);
    };
    g.axVTop = verticalWindow(interlaced ? Parity::TopField : parIn,
        interlaced ? Parity::TopField : parOut);
    g.axVBottom = interlaced ? verticalWindow(Parity::BottomField, Parity::BottomField)
        : g.axVTop;
    return g;
}

FramePlan planFrame(const ConversionSpec &spec, const FrameState &st) {
    FramePlan fp;
    const bool interlaced = st.parity == Parity::Interlaced;
    const ColourConfig &cc = spec.colour;

    if (cc.active) {
        fp.colourActive = true;
        fp.workW = cc.workW;
        fp.workH = cc.workH;
        fp.consts = st.consts;
        const uint32_t workW = cc.workW, workH = cc.workH;

        /* Input chains: source planes to WorkIn, in unity float. The destination end
           carries the working state's active region, which is what lets a window ride
           through and be consumed by exactly one resample per plane. */
        ChainActive inAct;
        inAct.srcL = spec.activeLeft;
        inAct.srcT = spec.activeTop;
        inAct.srcW = spec.activeWidth;
        inAct.srcH = spec.activeHeight;
        inAct.dstL = cc.workActiveLeft;
        inAct.dstT = cc.workActiveTop;
        inAct.dstW = cc.workActiveWidth;
        inAct.dstH = cc.workActiveHeight;
        const int inPlanes = spec.srcFmt.colorFamily == cfGray ? 1
            : spec.srcFmt.numPlanes;
        for (int p = 0; p < inPlanes; p++) {
            if (!cc.needIn[p])
                continue;
            PlanePlan &pp = fp.inChain[p];
            pp.from = { Surface::SrcPlane, p };
            pp.to = { Surface::WorkIn, p };
            const int cls = p == 0 ? 0 : 1;
            const KernelSpec &k = cls == 0 ? spec.kernelY : spec.kernelUV;
            const uint32_t srcPW = p == 0 ? spec.srcW : spec.srcW >> spec.srcFmt.subSamplingW;
            const uint32_t srcPH = p == 0 ? spec.srcH : spec.srcH >> spec.srcFmt.subSamplingH;
            const ChainGeom g = chainGeometry(k, cls, interlaced,
                st.parity, st.parity, srcPW, srcPH, workW, workH,
                spec.srcFmt.subSamplingW, 0, spec.srcFmt.subSamplingH, 0,
                st.sitingW, sitingCenter, st.sitingH, sitingMiddle, inAct);
            const bool chroma = spec.srcFmt.colorFamily == cfYUV && p != 0;
            const ScaleOffset so = affineToUnity(spec.srcFmt, chroma, st.fullIn, st.ycgco);
            pp.count = buildChainPasses(pp.pass, g.axH, g.axVTop, g.axVBottom,
                srcPW, srcPH, workW, workH, interlaced, spec.hFirst, so.scale, so.offset,
                &fp.scratchBytes);
            if (pp.count == 0) {
                /* The plane still has to reach WorkIn in unity float: one exact copy
                   carrying the affine. Only a windowless same-size integer plane lands
                   here, and only when the sibling planes made needIn true. */
                PassPlan &ps = pp.pass[0];
                ps.ax = identityAxis(srcPW);
                ps.outW = workW;
                ps.outH = workH;
                ps.scale = so.scale;
                ps.offset = so.offset;
                pp.count = 1;
            }
            fp.workInUsed[p] = true;
            fp.anyWork = true;
        }

        /* The colour pass's plumbing. */
        for (int p = 0; p < 3; p++) {
            if (cc.inZero[p])
                fp.colourIn[p] = { Surface::SrcPlane, 0 };  /* a valid dummy; flag skips it */
            else if (cc.needIn[p])
                fp.colourIn[p] = { Surface::WorkIn, p };
            else
                fp.colourIn[p] = { Surface::SrcPlane, p };
        }
        fp.colourOutPlanes = spec.dstFmt.colorFamily == cfGray ? 1 : 3;
        for (int p = 0; p < fp.colourOutPlanes; p++) {
            if (cc.needOut[p]) {
                fp.colourOut[p] = { Surface::WorkOut, p };
                fp.workOutUsed[p] = true;
            } else {
                fp.colourOut[p] = { Surface::DstPlane, p };
            }
        }
        fp.anyWork = true;

        /* Output chains: WorkOut to destination planes, out of unity float, with the
           destination's siting and -- for bob -- the progressive grid. The source end
           carries the working state's window on an upscale. */
        ChainActive outAct;
        outAct.srcL = cc.workActiveLeft;
        outAct.srcT = cc.workActiveTop;
        outAct.srcW = cc.workActiveWidth;
        outAct.srcH = cc.workActiveHeight;
        outAct.dstW = spec.dstW;
        outAct.dstH = spec.dstH;
        for (int p = 0; p < spec.dstFmt.numPlanes; p++) {
            if (!cc.needOut[p])
                continue;
            PlanePlan &pp = fp.plane[p];
            pp.from = { Surface::WorkOut, p };
            pp.to = { Surface::DstPlane, p };
            const int cls = p == 0 ? 0 : 1;
            const KernelSpec &k = cls == 0 ? spec.kernelY : spec.kernelUV;
            const uint32_t dstPW = p == 0 ? spec.dstW : spec.dstW >> spec.dstFmt.subSamplingW;
            const uint32_t dstPH = p == 0 ? spec.dstH : spec.dstH >> spec.dstFmt.subSamplingH;
            const ChainGeom g = chainGeometry(k, cls, interlaced,
                st.parity, st.dstParity, workW, workH, dstPW, dstPH,
                0, spec.dstFmt.subSamplingW, 0, spec.dstFmt.subSamplingH,
                sitingCenter, st.dstSitingW, sitingMiddle, st.dstSitingH, outAct);
            const bool chroma = spec.dstFmt.colorFamily == cfYUV && p != 0;
            const ScaleOffset so = affineFromUnity(spec.dstFmt, chroma, st.fullOut, st.ycgcoOut);
            pp.count = buildChainPasses(pp.pass, g.axH, g.axVTop, g.axVBottom,
                workW, workH, dstPW, dstPH, interlaced, spec.hFirst, so.scale, so.offset,
                &fp.scratchBytes);
            if (pp.count == 0) {
                PassPlan &ps = pp.pass[0];
                ps.ax = identityAxis(workW);
                ps.outW = dstPW;
                ps.outH = dstPH;
                ps.scale = so.scale;
                ps.offset = so.offset;
                ps.roundEven = 1;
                pp.count = 1;
            }
            /* Dither at the store: the colour pipeline's output is float, so any
               integer destination is a lossy conversion by definition. */
            if (spec.dither != DitherMode::None && spec.dstFmt.sampleType == stInteger) {
                for (int i = 0; i < pp.count; i++) {
                    if (pp.pass[i].writesFrame) {
                        pp.pass[i].ditherOn = 1;
                        pp.pass[i].ditherArg = ditherArgFor(spec.dither, p);
                        pp.pass[i].roundEven = 1;
                    }
                }
            }
        }
        return fp;
    }

    /* The plain path: source planes straight to destination planes. */
    const int numPlanes = spec.dstFmt.numPlanes;
    for (int p = 0; p < numPlanes; p++) {
        PlanePlan &pp = fp.plane[p];
        pp.from = { Surface::SrcPlane, p };
        pp.to = { Surface::DstPlane, p };
        const int cls = p == 0 ? 0 : 1;
        const KernelSpec &k = cls == 0 ? spec.kernelY : spec.kernelUV;
        const bool chroma = spec.dstFmt.colorFamily == cfYUV && p != 0;
        const uint32_t dstPW = p == 0 ? spec.dstW : spec.dstW >> spec.dstFmt.subSamplingW;
        const uint32_t dstPH = p == 0 ? spec.dstH : spec.dstH >> spec.dstFmt.subSamplingH;

        if (spec.fillsChroma && p != 0) {
            /* Chroma that does not exist yet: neutral at the destination's numbering,
               made by reading luma and multiplying it away. No pass order to speak of. */
            PassPlan &ps = pp.pass[0];
            ps.fill = true;
            ps.ax = identityAxis(spec.srcW);
            ps.outW = dstPW;
            ps.outH = dstPH;
            /* The dispatch covers the destination chroma plane while the read lands in the
               source luma one, and only the resampled axis is bounded by srcDim -- so on any
               vertical upscale the row index runs past the luma buffer. A zero row stride
               pins every output row to source row 0, which is in range whatever the two
               heights are and costs nothing, the sample being multiplied away below. */
            ps.srcStrideMul = 0;
            ps.scale = 0.0f;
            ps.offset = static_cast<float>(integerOffset(spec.dstFmt, true, st.fullOut));
            ps.roundEven = 1;
            pp.count = 1;
            fp.anyWork = true;
            continue;
        }

        const uint32_t srcPW = p == 0 ? spec.srcW : spec.srcW >> spec.srcFmt.subSamplingW;
        const uint32_t srcPH = p == 0 ? spec.srcH : spec.srcH >> spec.srcFmt.subSamplingH;
        ChainActive act;
        act.srcL = spec.activeLeft;
        act.srcT = spec.activeTop;
        act.srcW = spec.activeWidth;
        act.srcH = spec.activeHeight;
        act.dstW = spec.dstW;
        act.dstH = spec.dstH;
        const ChainGeom g = chainGeometry(k, cls, interlaced,
            st.parity, st.dstParity, srcPW, srcPH, dstPW, dstPH,
            spec.srcFmt.subSamplingW, spec.dstFmt.subSamplingW,
            spec.srcFmt.subSamplingH, spec.dstFmt.subSamplingH,
            st.sitingW, st.dstSitingW, st.sitingH, st.dstSitingH, act);

        const ScaleOffset so = depthTransform(spec.srcFmt, spec.dstFmt, chroma,
            st.fullIn, st.fullOut, st.ycgco);
        const bool needAffine = so.scale != 1.0f || so.offset != 0.0f;
        const bool dithered = ditherApplies(spec.dither, spec.srcFmt, spec.dstFmt,
            st.fullIn, st.fullOut);

        pp.count = buildChainPasses(pp.pass, g.axH, g.axVTop, g.axVBottom,
            srcPW, srcPH, dstPW, dstPH, interlaced, spec.hFirst, so.scale, so.offset,
            &fp.scratchBytes);
        if (pp.count == 0) {
            if (!needAffine && !dithered) {
                pp.share = true;
                fp.anyShare = true;
                continue;
            }
            /* A pure format conversion: one identity gather carrying the affine. */
            PassPlan &ps = pp.pass[0];
            ps.ax = identityAxis(srcPW);
            ps.outW = dstPW;
            ps.outH = dstPH;
            ps.scale = so.scale;
            ps.offset = so.offset;
            ps.roundEven = 1;
            pp.count = 1;
        }
        if (dithered) {
            /* Dither rides the store, rounding half to even the way zimg's dither
               kernels lrint. */
            for (int i = 0; i < pp.count; i++) {
                if (pp.pass[i].writesFrame) {
                    pp.pass[i].ditherOn = 1;
                    pp.pass[i].ditherArg = ditherArgFor(spec.dither, p);
                    pp.pass[i].roundEven = 1;
                }
            }
        }
        fp.anyWork = true;
    }
    return fp;
}

/* ------------------------------------------------------------------------------------------
   Frame properties, the host-side half. Mirrors export_frame_props and propagate_sar for
   the scope this path accepts; the values written are the same ones the plan computed
   with, which is what keeps the tags and the numbers agreeing. */

void finishResizeProps(const ConversionSpec &spec, const FrameState &st, VSFrame *dst,
    const VSFrame *src, const VSAPI *vsapi) {
    const VSMap *srcProps = vsapi->getFramePropertiesRO(src);
    VSMap *props = vsapi->getFramePropertiesRW(dst);
    int err;

    /* The tag only means something on subsampled YUV, and the scalar side drops it
       otherwise rather than carrying a value that describes nothing. */
    if (spec.dstFmt.colorFamily == cfYUV &&
        (spec.dstFmt.subSamplingW || spec.dstFmt.subSamplingH))
        vsapi->mapSetInt(props, "_ChromaLocation", st.locOut, maReplace);
    else
        vsapi->mapDeleteKey(props, "_ChromaLocation");

    vsapi->mapSetInt(props, "_Range", st.fullOut ? VSC_RANGE_FULL : VSC_RANGE_LIMITED, maReplace);

    /* UNSPECIFIED in the source means "not stated", so the format's own default stands --
       which for matrix is RGB on an RGB clip and unspecified everywhere else. A tag a
       conversion decided is stated outright instead of carried. */
    auto carry = [&](const char *key, int64_t unspecified, int64_t fallback) {
        int64_t v = vsapi->mapGetInt(srcProps, key, 0, &err);
        if (err || v == unspecified)
            v = fallback;
        vsapi->mapSetInt(props, key, v, maReplace);
    };
    if (st.colourMatrixTag >= 0)
        vsapi->mapSetInt(props, "_Matrix", st.colourMatrixTag, maReplace);
    else
        carry("_Matrix", VSC_MATRIX_UNSPECIFIED,
            spec.tagMatrixIn >= 0 ? spec.tagMatrixIn
            : (spec.dstFmt.colorFamily == cfRGB ? VSC_MATRIX_RGB : VSC_MATRIX_UNSPECIFIED));
    if (st.colourTransferTag >= 0)
        vsapi->mapSetInt(props, "_Transfer", st.colourTransferTag, maReplace);
    else
        carry("_Transfer", VSC_TRANSFER_UNSPECIFIED,
            spec.tagTransferIn >= 0 ? spec.tagTransferIn : VSC_TRANSFER_UNSPECIFIED);
    if (st.colourPrimariesTag >= 0)
        vsapi->mapSetInt(props, "_Primaries", st.colourPrimariesTag, maReplace);
    else
        carry("_Primaries", VSC_PRIMARIES_UNSPECIFIED,
            spec.tagPrimariesIn >= 0 ? spec.tagPrimariesIn : VSC_PRIMARIES_UNSPECIFIED);

    int64_t sarNum = vsapi->mapGetInt(srcProps, "_SARNum", 0, &err);
    if (err)
        sarNum = 0;
    int64_t sarDen = vsapi->mapGetInt(srcProps, "_SARDen", 0, &err);
    if (err)
        sarDen = 0;
    if (sarNum <= 0 || sarDen <= 0) {
        vsapi->mapDeleteKey(props, "_SARNum");
        vsapi->mapDeleteKey(props, "_SARDen");
    } else {
        /* A windowed source resamples a fraction of the frame into the whole output, so
           the pixel aspect follows the active region; the sixteenths keep a quarter
           pixel window rational, as the scalar path does. */
        if (spec.activeWidth != spec.srcW)
            muldivRational(&sarNum, &sarDen, std::llround(spec.activeWidth * 16), static_cast<int64_t>(spec.dstW) * 16);
        else
            muldivRational(&sarNum, &sarDen, spec.srcW, spec.dstW);
        if (spec.activeHeight != spec.srcH)
            muldivRational(&sarNum, &sarDen, static_cast<int64_t>(spec.dstH) * 16, std::llround(spec.activeHeight * 16));
        else
            muldivRational(&sarNum, &sarDen, spec.dstH, spec.srcH);
        vsapi->mapSetInt(props, "_SARNum", sarNum, maReplace);
        vsapi->mapSetInt(props, "_SARDen", sarDen, maReplace);
    }
}

/* ------------------------------------------------------------------------------------------
   The instance: pipelines for every pass shape the planner can emit, the exec pool, and
   the spec. Pipelines are enumerated up front -- direction x which end is samples, per
   kernel class -- because which of them a frame needs is a per-frame fact but the closed
   set of possibilities is not. Twelve at the very most, six in the common case, and the
   core caches compiles by source text. */

struct ResizePipeline {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

/* Everything the instance's one (source format, size) pairing needs: the resolved spec
   and its pipelines. There is exactly one, since a resident clip must have a constant
   format and constant dimensions to reach this path at all. */
struct PipeSet {
    ConversionSpec spec;
    /* [class][vertical][chain source is a frame plane][chain destination is one]; the
       float-to-float combinations exist too, since a conversion's chains run between
       the frames and the float working planes. */
    ResizePipeline pipes[2][2][2][2];
    ResizePipeline colourPipe;
    int numClasses = 1;
    /* Built once, shipped per frame; empty when not dithering. */
    std::vector<float> ditherTable;
};

struct GPUResizeData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    const VSVULKANAPI *vkapi = nullptr;
    const VSVulkanFunctions *vk = nullptr;
    VSVulkanCoreHandles handles = {};
    VSGPUExecPool *pool = nullptr;

    PipeSet pipes;

    void destroyPipeSet(PipeSet &ps) {
        auto destroy = [&](ResizePipeline &p) {
            if (p.pipeline)
                vk->vkDestroyPipeline(handles.device, p.pipeline, nullptr);
            if (p.pipeLayout)
                vk->vkDestroyPipelineLayout(handles.device, p.pipeLayout, nullptr);
            if (p.setLayout)
                vk->vkDestroyDescriptorSetLayout(handles.device, p.setLayout, nullptr);
        };
        for (auto &c : ps.pipes) for (auto &v : c) for (auto &r : v) for (ResizePipeline &p : r)
            destroy(p);
        destroy(ps.colourPipe);
    }

    ~GPUResizeData() {
        if (vk) {
            /* The pool drains the device before returning, so pipelines a submission
               was still using are safe to destroy only after this point. */
            if (pool)
                vkapi->freeGPUExecPool(pool);
            destroyPipeSet(pipes);
        }
    }
};

void computeBarrier(const VSVulkanFunctions *vk, VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb = {};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vk->vkCmdPipelineBarrier2(cmd, &dep);
}

bool buildPipeline(GPUResizeData *d, const std::string &glsl, int bindingCount,
    uint32_t pushBytes, ResizePipeline *out, VSCore *core, std::string &error) {
    char err[512] = { 0 };
    VSGPUShader *shader = d->vkapi->compileGPUShader(core, slGLSL, glsl.c_str(), err, sizeof(err));
    if (!shader) {
        error = std::string("kernel failed to compile: ") + err;
        return false;
    }
    size_t spirvBytes = 0;
    const uint32_t *spirv = d->vkapi->getGPUShaderCode(shader, &spirvBytes);

    VkDescriptorSetLayoutBinding bindings[8] = {};
    for (int b = 0; b < bindingCount; b++) {
        bindings[b].binding = static_cast<uint32_t>(b);
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    setInfo.bindingCount = static_cast<uint32_t>(bindingCount);
    setInfo.pBindings = bindings;

    bool ok = spirv && spirvBytes &&
        d->vk->vkCreateDescriptorSetLayout(d->handles.device, &setInfo, nullptr, &out->setLayout) == VK_SUCCESS;

    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = pushBytes;
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &out->setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    ok = ok && d->vk->vkCreatePipelineLayout(d->handles.device, &layoutInfo, nullptr, &out->pipeLayout) == VK_SUCCESS;

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirvBytes;
    moduleInfo.pCode = spirv;
    VkComputePipelineCreateInfo pipeInfo = {};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.pNext = &moduleInfo;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = out->pipeLayout;
    ok = ok && d->vk->vkCreateComputePipelines(d->handles.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &out->pipeline) == VK_SUCCESS;

    d->vkapi->freeGPUShader(shader);
    if (!ok && error.empty())
        error = "pipeline creation failed";
    return ok;
}

/* Compiles every pipeline one PipeSet's spec can dispatch. Which of them a frame needs
   is per frame, but the closed set of possibilities is not; the core caches compiles by
   source text, so repeat combinations parse once. */
bool buildPipeSet(GPUResizeData *d, PipeSet &ps, VSCore *core, std::string &error) {
    const ConversionSpec &spec = ps.spec;
    ps.numClasses = spec.uvDiffers ? 2 : 1;
    ps.ditherTable = buildDitherTable(spec.dither);
    const int ditherMode = spec.dither == DitherMode::Ordered ? 1
        : spec.dither == DitherMode::Random ? 2 : 0;
    for (int cls = 0; cls < ps.numClasses; cls++) {
        const KernelSpec &k = cls == 0 ? spec.kernelY : spec.kernelUV;
        for (int vert = 0; vert < 2; vert++) {
            /* Both ends frame samples, either end the float working space, or neither --
               a conversion's chains run between the frames and the working planes. */
            for (int srcSample = 0; srcSample < 2; srcSample++) {
                for (int dstSample = 0; dstSample < 2; dstSample++) {
                    const bool intOut = dstSample && spec.dstFmt.sampleType == stInteger;
                    const std::string glsl = resizeShaderSource(k, vert != 0,
                        srcSample ? sampleTypeName(spec.srcFmt) : "float",
                        dstSample ? sampleTypeName(spec.dstFmt) : "float",
                        intOut ? spec.dstFmt.bitsPerSample : 0, ditherMode);
                    if (!buildPipeline(d, glsl, intOut && ditherMode > 0 ? 3 : 2,
                            sizeof(ResizePush),
                            &ps.pipes[cls][vert][srcSample][dstSample], core, error))
                        return false;
                }
            }
        }
    }
    if (spec.colour.active) {
        const char *srcT[3], *dstT[3] = { "float", "float", "float" };
        bool dstInt[3] = {};
        for (int i = 0; i < 3; i++) {
            const bool direct = spec.colour.inZero[i] ||
                (!spec.colour.needIn[i] && i < spec.srcFmt.numPlanes);
            srcT[i] = direct ? sampleTypeName(spec.srcFmt) : "float";
        }
        const int outPlanes = spec.dstFmt.colorFamily == cfGray ? 1 : 3;
        for (int i = 0; i < outPlanes; i++) {
            const bool direct = !spec.colour.needOut[i];
            dstT[i] = direct ? sampleTypeName(spec.dstFmt) : "float";
            dstInt[i] = direct && spec.dstFmt.sampleType == stInteger;
        }
        const std::string glsl = colourShaderSource(srcT, dstT, dstInt, outPlanes,
            spec.dstFmt.sampleType == stInteger ? spec.dstFmt.bitsPerSample : 0, ditherMode);
        if (!buildPipeline(d, glsl, 3 + outPlanes + 1 + (ditherMode > 0 ? 1 : 0),
                sizeof(ColourPush), &ps.colourPipe, core, error))
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------------------------------
   The frame: resolve state, plan, record. */

bool resolveSpec(const VSMap *in, const char *kernelName, bool deinterlace,
    const VSVideoInfo *vi, VSCore *core, const VSAPI *vsapi, ConversionSpec *out,
    std::string &decline);

const VSFrame *VS_CC gpuResizeGetFrame(int n, int activationReason, void *instanceData, void **,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUResizeData *d = static_cast<GPUResizeData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    }
    if (activationReason != arAllFramesReady)
        return nullptr;

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    char err[512] = { 0 };

    auto fail = [&](const std::string &message) -> const VSFrame * {
        vsapi->setFilterError(message.c_str(), frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    };

    /* One spec and one set of pipelines for the instance: the clip's format and dimensions
       are constant, which createGPUResize required before building any of it. */
    const PipeSet &pset = d->pipes;
    const ConversionSpec &spec = pset.spec;

    FrameState st;
    std::string stateError;
    if (!resolveFrameState(spec, vsapi->getFramePropertiesRO(src), vsapi, &st, stateError))
        return fail(stateError);

    const FramePlan fp = planFrame(spec, st);
    const int numPlanes = spec.dstFmt.numPlanes;

    VSFrame *dst;
    if (fp.anyShare) {
        /* Unprocessed planes ride along from the source, which keeps them on the device
           with their own producer pairs; everything else is written by this submission. */
        const VSFrame *planeSrc[3] = {};
        int planeIdx[3] = { 0, 1, 2 };
        for (int p = 0; p < numPlanes; p++)
            planeSrc[p] = fp.plane[p].share ? src : nullptr;
        dst = vsapi->newVideoFrame2(&spec.dstFmt, static_cast<int>(spec.dstW),
            static_cast<int>(spec.dstH), planeSrc, planeIdx, src, core);
    } else {
        dst = d->vkapi->newGPUVideoFrame(&spec.dstFmt, static_cast<int>(spec.dstW),
            static_cast<int>(spec.dstH), src, core);
    }
    if (!dst)
        return fail("Resize: failed to allocate the output frame");

    /* Nothing to run: every plane shares, the frame is complete, and submitting an empty
       command buffer would only cost a round trip. Properties still change. */
    if (!fp.anyWork) {
        finishResizeProps(spec, st, dst, src, vsapi);
        vsapi->freeFrame(src);
        return dst;
    }

    VSGPUExecContext *ctx = d->vkapi->gpuExecAcquire(d->pool, err, sizeof(err));
    if (!ctx) {
        vsapi->freeFrame(dst);
        return fail(std::string("Resize: ") + err);
    }
    d->vkapi->gpuExecReadsFrame(ctx, src);

    auto abandon = [&](const std::string &message) -> const VSFrame * {
        d->vkapi->gpuExecAbandon(ctx);
        vsapi->freeFrame(dst);
        return fail(message);
    };

    /* The chain intermediate, packed tight and reused by every chain in turn -- the
       barrier between dispatches covers the reuse -- plus the working planes and the
       colour constants when a conversion runs. */
    VSVulkanBufferInfo scratch = {};
    VSVulkanBufferInfo workIn[3] = {}, workOut[3] = {}, constsBuf = {};
    auto allocBuffer = [&](VkDeviceSize bytes, VkBufferUsageFlags usage, VSVulkanBufferInfo *info) {
        VSGPUBuffer *buffer = d->vkapi->createGPUBuffer(core, bytes, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, info, err, sizeof(err));
        if (buffer)
            d->vkapi->gpuExecUsesBuffer(ctx, buffer);
        return buffer != nullptr;
    };
    VSVulkanBufferInfo ditherBuf = {};
    if (fp.scratchBytes && !allocBuffer(fp.scratchBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &scratch))
        return abandon(std::string("Resize: ") + err);
    if (fp.colourActive) {
        const VkDeviceSize wbytes = static_cast<VkDeviceSize>(fp.workW) * fp.workH * sizeof(float);
        for (int p = 0; p < 3; p++) {
            if (fp.workInUsed[p] && !allocBuffer(wbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &workIn[p]))
                return abandon(std::string("Resize: ") + err);
            if (fp.workOutUsed[p] && !allocBuffer(wbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &workOut[p]))
                return abandon(std::string("Resize: ") + err);
        }
        if (!allocBuffer(sizeof(ColourConstants),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &constsBuf))
            return abandon(std::string("Resize: ") + err);
    }
    if (!pset.ditherTable.empty() && !allocBuffer(pset.ditherTable.size() * sizeof(float),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &ditherBuf))
        return abandon(std::string("Resize: ") + err);

    VkCommandBuffer cmd = d->vkapi->gpuExecCommandBuffer(ctx);
    bool firstDispatch = true;

    if (fp.colourActive || ditherBuf.buffer) {
        /* Constants and the dither table ride inline in the command buffer; small
           enough by far, and it keeps them device local without a staging round trip. */
        if (fp.colourActive)
            d->vk->vkCmdUpdateBuffer(cmd, constsBuf.buffer, 0, sizeof(ColourConstants), &fp.consts);
        if (ditherBuf.buffer)
            d->vk->vkCmdUpdateBuffer(cmd, ditherBuf.buffer, 0,
                pset.ditherTable.size() * sizeof(float), pset.ditherTable.data());
        VkMemoryBarrier2 mb = {};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        d->vk->vkCmdPipelineBarrier2(cmd, &dep);
    }

    bool declaredWrite[3] = {};
    auto declareWrite = [&](int p) {
        if (!declaredWrite[p]) {
            d->vkapi->gpuExecWritesPlane(ctx, dst, p);
            declaredWrite[p] = true;
        }
    };
    bool bindFailed = false;
    auto surfaceBuffer = [&](const Surface &s, uint32_t *strideBase) -> VkBuffer {
        VSVulkanPlaneInfo info;
        switch (s.kind) {
        case Surface::SrcPlane:
            if (d->vkapi->getGPUPlane(src, s.plane, &info)) {
                bindFailed = true;
                return VK_NULL_HANDLE;
            }
            *strideBase = static_cast<uint32_t>(
                vsapi->getStride(src, s.plane) / spec.srcFmt.bytesPerSample);
            return info.buffer;
        case Surface::DstPlane:
            if (d->vkapi->getGPUPlane(dst, s.plane, &info)) {
                bindFailed = true;
                return VK_NULL_HANDLE;
            }
            *strideBase = static_cast<uint32_t>(
                vsapi->getStride(dst, s.plane) / spec.dstFmt.bytesPerSample);
            return info.buffer;
        case Surface::WorkIn:
            *strideBase = fp.workW;
            return workIn[s.plane].buffer;
        default:
            *strideBase = fp.workW;
            return workOut[s.plane].buffer;
        }
    };

    auto dispatchOne = [&](const ResizePipeline &pipe, const VkBuffer *buffers, int count,
        const void *push, uint32_t pushBytes, uint32_t w, uint32_t h) {
        VkDescriptorBufferInfo bufferInfo[8] = {};
        VkWriteDescriptorSet writes[8] = {};
        for (int b = 0; b < count; b++) {
            bufferInfo[b].buffer = buffers[b];
            bufferInfo[b].range = VK_WHOLE_SIZE;
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstBinding = static_cast<uint32_t>(b);
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &bufferInfo[b];
        }
        if (!firstDispatch)
            computeBarrier(d->vk, cmd);
        firstDispatch = false;
        d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline);
        d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeLayout,
            0, static_cast<uint32_t>(count), writes);
        VkPushConstantsInfo pushInfo = {};
        pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
        pushInfo.layout = pipe.pipeLayout;
        pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushInfo.size = pushBytes;
        pushInfo.pValues = push;
        d->vk->vkCmdPushConstants2(cmd, &pushInfo);
        d->vk->vkCmdDispatch(cmd, (w + resizeLocalSize - 1) / resizeLocalSize,
            (h + resizeLocalSize - 1) / resizeLocalSize, 1);
    };

    auto recordChain = [&](const PlanePlan &pp) {
        if (pp.share || pp.count == 0)
            return;
        if (pp.to.kind == Surface::DstPlane)
            declareWrite(pp.to.plane);
        /* The chain's source is resolved lazily: a fill chain's nominal source plane is
           chroma the frame does not have, and only its luma read may touch the frame. */
        uint32_t fromStride = 0, toStride = 0;
        VkBuffer fromBuf = VK_NULL_HANDLE;
        bool fromResolved = false;
        const VkBuffer toBuf = surfaceBuffer(pp.to, &toStride);
        if (bindFailed)
            return;
        const int p = pp.to.plane;
        const int cls = (p == 0 || pset.numClasses == 1) ? 0 : 1;

        for (int i = 0; i < pp.count; i++) {
            const PassPlan &ps = pp.pass[i];
            VkBuffer srcBuf;
            uint32_t srcStrideBase;
            if (ps.readsFrame) {
                if (ps.fill) {
                    Surface luma = { Surface::SrcPlane, 0 };
                    srcBuf = surfaceBuffer(luma, &srcStrideBase);
                    if (bindFailed)
                        return;
                } else {
                    if (!fromResolved) {
                        fromBuf = surfaceBuffer(pp.from, &fromStride);
                        fromResolved = true;
                        if (bindFailed)
                            return;
                    }
                    srcBuf = fromBuf;
                    srcStrideBase = fromStride;
                }
            } else {
                srcBuf = scratch.buffer;
                srcStrideBase = pp.pass[0].outW;
            }
            const VkBuffer dstBuf = ps.writesFrame ? toBuf : scratch.buffer;
            const uint32_t dstStrideBase = ps.writesFrame ? toStride : ps.outW;

            ResizePush push = {};
            push.outWidth = ps.outW;
            push.outHeight = ps.outH;
            push.srcDim = ps.ax.srcDim;
            /* The field addressing: base row plus doubled stride, in whichever surface
               each end happens to be. */
            push.srcStride = srcStrideBase * ps.srcStrideMul;
            push.srcOffset = ps.srcRowOffset * srcStrideBase;
            push.dstStride = dstStrideBase * ps.dstStrideMul;
            push.dstOffset = ps.dstRowOffset * dstStrideBase;
            push.taps = ps.ax.taps;
            push.roundEven = ps.roundEven;
            push.ditherArg = ps.ditherArg;
            push.ditherOn = ps.ditherOn;
            push.invScale = ps.ax.invScale;
            push.shift = ps.ax.shift;
            push.kstep = ps.ax.kstep;
            push.invScaleLo = ps.ax.invScaleLo;
            push.shiftLo = ps.ax.shiftLo;
            push.scale = ps.scale;
            push.offset = ps.offset;

            const bool srcSample = ps.readsFrame && pp.from.kind == Surface::SrcPlane;
            const bool dstSample = ps.writesFrame && pp.to.kind == Surface::DstPlane;
            const ResizePipeline &pipe =
                pset.pipes[cls][ps.vertical ? 1 : 0][srcSample ? 1 : 0][dstSample ? 1 : 0];
            /* Sample-writing pipelines carry the dither table binding when the instance
               dithers at all; the count has to match the layout exactly. */
            const bool withDither = dstSample && ditherBuf.buffer &&
                spec.dstFmt.sampleType == stInteger;
            const VkBuffer bufs[3] = { srcBuf, dstBuf, ditherBuf.buffer };
            dispatchOne(pipe, bufs, withDither ? 3 : 2, &push, sizeof(push), ps.outW, ps.outH);
        }
    };

    for (int p = 0; p < 3; p++)
        recordChain(fp.inChain[p]);
    if (bindFailed)
        return abandon("Resize: a frame plane is not GPU resident");

    if (fp.colourActive) {
        VkBuffer bufs[8];
        ColourPush cpush = {};
        cpush.width = fp.workW;
        cpush.height = fp.workH;
        for (int i = 0; i < 3; i++)
            bufs[i] = surfaceBuffer(fp.colourIn[i], &cpush.inStride[i]);
        for (int i = 0; i < fp.colourOutPlanes; i++) {
            if (fp.colourOut[i].kind == Surface::DstPlane)
                declareWrite(fp.colourOut[i].plane);
            bufs[3 + i] = surfaceBuffer(fp.colourOut[i], &cpush.outStride[i]);
        }
        if (bindFailed)
            return abandon("Resize: a frame plane is not GPU resident");
        int count = 3 + fp.colourOutPlanes;
        bufs[count++] = constsBuf.buffer;
        if (ditherBuf.buffer)
            bufs[count++] = ditherBuf.buffer;
        dispatchOne(pset.colourPipe, bufs, count, &cpush, sizeof(cpush), fp.workW, fp.workH);
    }

    for (int p = 0; p < numPlanes; p++)
        recordChain(fp.plane[p]);
    if (bindFailed)
        return abandon("Resize: a frame plane is not GPU resident");

    if (d->vkapi->gpuExecSubmit(ctx, nullptr, err, sizeof(err))) {
        vsapi->freeFrame(dst);
        return fail(std::string("Resize: ") + err);
    }

    finishResizeProps(spec, st, dst, src, vsapi);
    vsapi->freeFrame(src);
    return dst;
}

void VS_CC gpuResizeFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    GPUResizeData *d = static_cast<GPUResizeData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

/* ------------------------------------------------------------------------------------------
   Argument resolution into the spec. */

double optFloat(const VSMap *in, const char *key, double def, const VSAPI *vsapi) {
    int err;
    const double v = vsapi->mapGetFloat(in, key, 0, &err);
    return err ? def : v;
}

bool present(const VSMap *in, const char *key, const VSAPI *vsapi) {
    return vsapi->mapNumElements(in, key) > 0;
}

std::string lowercased(const char *s) {
    std::string r = s ? s : "";
    for (char &c : r)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return r;
}

/* An enum argument in both spellings, resolved the way lookup_enum in vsresize.cpp
   resolves it: the integer form wins and the string is only consulted when absent. */
enum class ArgLookup { Absent, Resolved, Unsupported };

template<size_t N>
ArgLookup lookupSharedEnum(const VSMap *in, const char *key,
    const ResizeEnumEntry (&table)[N], const VSAPI *vsapi, int64_t *value) {
    if (present(in, key, vsapi)) {
        *value = vsapi->mapGetInt(in, key, 0, nullptr);
        for (const ResizeEnumEntry &e : table)
            if (e.value == *value)
                return ArgLookup::Resolved;
        return ArgLookup::Unsupported;
    }
    const std::string strKey = std::string(key) + "_s";
    if (!present(in, strKey.c_str(), vsapi))
        return ArgLookup::Absent;
    const std::string name = lowercased(vsapi->mapGetData(in, strKey.c_str(), 0, nullptr));
    if (const int *v = findResizeEnum(table, name)) {
        *value = *v;
        return ArgLookup::Resolved;
    }
    return ArgLookup::Unsupported;
}

/* Any argument that means colour work: matrix, transfer and primaries are not
   implemented, so their mere presence is a decline. Both spellings. */
bool anyColourArg(const VSMap *in, const VSAPI *vsapi) {
    static const char *const keys[] = { "matrix", "transfer", "primaries",
        "matrix_in", "transfer_in", "primaries_in" };
    for (const char *k : keys) {
        if (present(in, k, vsapi) || present(in, (std::string(k) + "_s").c_str(), vsapi))
            return true;
    }
    return false;
}

/* Builds the spec from the argument map, or declines with a reason. Touches no Vulkan
   state and owns nothing. */
bool resolveSpec(const VSMap *in, const char *kernelName, bool deinterlace,
    const VSVideoInfo *vi, VSCore *core, const VSAPI *vsapi, ConversionSpec *out,
    std::string &decline) {
    auto give_up = [&](const std::string &reason) {
        decline = reason;
        return false;
    };

    if (!isConstantVideoFormat(vi))
        return give_up("variable format or dimensions");

    ConversionSpec spec;
    spec.srcFmt = vi->format;
    spec.dstFmt = vi->format;
    if (present(in, "format", vsapi)) {
        const int id = vsapi->mapGetIntSaturated(in, "format", 0, nullptr);
        if (!vsapi->getVideoFormatByID(&spec.dstFmt, static_cast<uint32_t>(id), core) ||
            spec.dstFmt.colorFamily == cfUndefined)
            return give_up("an unknown output format");
    }

    for (const VSVideoFormat *f : { &spec.srcFmt, &spec.dstFmt }) {
        if (f->colorFamily != cfGray && f->colorFamily != cfYUV && f->colorFamily != cfRGB)
            return give_up("an unsupported colour family");
        /* Nothing stores video as wider integers; they only exist as intermediates
           inside filters that widen, and PEAK would shift by the type's width. */
        if (f->sampleType == stInteger && f->bytesPerSample > 2)
            return give_up("a wider than 16 bit integer sample");
    }

    /* Dropping chroma is not a conversion -- the output simply has nowhere to put it --
       and growing it out of grey fills in neutral; both stay inside the no-matrix world.
       Everything else between families is a colour conversion. */
    spec.dropsChroma = spec.dstFmt.colorFamily == cfGray && spec.srcFmt.colorFamily == cfYUV;
    spec.fillsChroma = spec.srcFmt.colorFamily == cfGray && spec.dstFmt.colorFamily == cfYUV;
    spec.sameFamily = spec.srcFmt.colorFamily == spec.dstFmt.colorFamily;

    int64_t v;
    switch (lookupSharedEnum(in, "range", resizeRangeTable, vsapi, &v)) {
    case ArgLookup::Resolved: spec.rangeOut = static_cast<int>(v); break;
    case ArgLookup::Unsupported: return give_up("a range this path does not recognise");
    case ArgLookup::Absent: break;
    }
    switch (lookupSharedEnum(in, "range_in", resizeRangeTable, vsapi, &v)) {
    case ArgLookup::Resolved: spec.rangeIn = static_cast<int>(v); break;
    case ArgLookup::Unsupported: return give_up("a range_in this path does not recognise");
    case ArgLookup::Absent: break;
    }
    switch (lookupSharedEnum(in, "chromaloc", resizeChromaLocTable, vsapi, &v)) {
    case ArgLookup::Resolved: spec.chromaLocOut = v; break;
    case ArgLookup::Unsupported: return give_up("a chroma location this path does not recognise");
    case ArgLookup::Absent: break;
    }
    switch (lookupSharedEnum(in, "chromaloc_in", resizeChromaLocTable, vsapi, &v)) {
    case ArgLookup::Resolved: spec.chromaLocIn = v; break;
    case ArgLookup::Unsupported: return give_up("a chromaloc_in this path does not recognise");
    case ArgLookup::Absent: break;
    }

    int ditherErr;
    const char *ditherType = vsapi->mapGetData(in, "dither_type", 0, &ditherErr);
    if (!ditherErr) {
        const std::string d = lowercased(ditherType);
        if (d == "none")
            spec.dither = DitherMode::None;
        else if (d == "ordered")
            spec.dither = DitherMode::Ordered;
        else if (d == "random")
            spec.dither = DitherMode::Random;
        else
            /* Error diffusion is a serial recurrence over the whole image; the scalar
               implementation is the right one for it. */
            return give_up("error diffusion dithering");
    }
    /* Asking for a specific CPU is a statement about where the work should run. */
    if (present(in, "cpu_type", vsapi))
        return give_up("cpu_type was given");

    if (!makeKernelSpec(lowercased(kernelName),
            optFloat(in, "filter_param_a", NAN, vsapi),
            optFloat(in, "filter_param_b", NAN, vsapi), &spec.kernelY))
        return give_up("the kernel or its parameters are not implemented");
    /* zimg's own quirk, kept deliberately: the uv kernel is chosen by plane index, so on
       RGB it applies to green and blue. Matching resize matters more than the name. */
    int uvErr;
    const char *uvName = vsapi->mapGetData(in, "resample_filter_uv", 0, &uvErr);
    if (uvErr) {
        spec.kernelUV = spec.kernelY;
    } else if (!makeKernelSpec(lowercased(uvName),
            optFloat(in, "filter_param_a_uv", NAN, vsapi),
            optFloat(in, "filter_param_b_uv", NAN, vsapi), &spec.kernelUV)) {
        return give_up("the chroma kernel or its parameters are not implemented");
    }
    spec.uvDiffers = !(spec.kernelUV == spec.kernelY);

    const int dstWArg = present(in, "width", vsapi) ? vsapi->mapGetIntSaturated(in, "width", 0, nullptr) : 0;
    const int dstHArg = present(in, "height", vsapi) ? vsapi->mapGetIntSaturated(in, "height", 0, nullptr) : 0;
    if (dstWArg < 0 || dstHArg < 0)
        return give_up("bad output dimensions");
    spec.srcW = static_cast<uint32_t>(vi->width);
    spec.srcH = static_cast<uint32_t>(vi->height);
    spec.dstW = dstWArg > 0 ? static_cast<uint32_t>(dstWArg) : spec.srcW;
    /* Bob's output height is twice its input's whatever height says, which is what the
       scalar path does -- it reads the argument and then overwrites it. */
    spec.deinterlace = deinterlace;
    spec.dstH = deinterlace ? spec.srcH * 2
        : dstHArg > 0 ? static_cast<uint32_t>(dstHArg) : spec.srcH;
    /* An output the chroma planes cannot be cut from is a script error, and phrasing it
       is the scalar path's job rather than something to reword here. */
    if ((spec.dstW & ((1u << spec.dstFmt.subSamplingW) - 1)) ||
        (spec.dstH & ((1u << spec.dstFmt.subSamplingH) - 1)))
        return give_up("the output size does not divide by the subsampling");

    spec.activeLeft = optFloat(in, "src_left", 0.0, vsapi);
    spec.activeTop = optFloat(in, "src_top", 0.0, vsapi);
    spec.activeWidth = optFloat(in, "src_width", vi->width, vsapi);
    spec.activeHeight = optFloat(in, "src_height", vi->height, vsapi);
    if (!(spec.activeWidth > 0) || !(spec.activeHeight > 0))
        return give_up("bad source window");
    spec.windowed = spec.activeLeft != 0 || spec.activeTop != 0 ||
        spec.activeWidth != vi->width || spec.activeHeight != vi->height;

    spec.bothSubsampledYUV = spec.dstFmt.colorFamily == cfYUV &&
        (spec.dstFmt.subSamplingW || spec.dstFmt.subSamplingH) &&
        spec.srcFmt.colorFamily == cfYUV &&
        (spec.srcFmt.subSamplingW || spec.srcFmt.subSamplingH);

    /* Same cost model the scalar path uses to order its passes: whichever order makes
       the intermediate smaller wins, which for a downscale means shrinking the larger
       axis first. Measured on luma, the plane the sizes are quoted in. */
    const double xscale = spec.dstW / spec.activeWidth, yscale = spec.dstH / spec.activeHeight;
    const double hFirstCost = std::max(xscale, 1.0) * 2.0 + xscale * std::max(yscale, 1.0);
    const double vFirstCost = std::max(yscale, 1.0) + yscale * std::max(xscale, 1.0) * 2.0;
    spec.hFirst = hFirstCost < vFirstCost;

    /* ---- colour. Which conversion, if any, these arguments ask for; everything the
       fused pass cannot mean yet declines here so the scalar graph answers instead. */
    auto lookupColour = [&](const char *key, const auto &table, int64_t unspec,
        int64_t *value, ArgLookup *res, const char *what) -> bool {
        *res = lookupSharedEnum(in, key, table, vsapi, value);
        if (*res == ArgLookup::Unsupported) {
            decline = std::string("a ") + what + " this path does not recognise";
            return false;
        }
        /* Naming "unspec" outright means the same as saying nothing. */
        if (*res == ArgLookup::Resolved && *value == unspec)
            *res = ArgLookup::Absent;
        return true;
    };
    int64_t matrixVal = -1, matrixInVal = -1, transferVal = -1, transferInVal = -1,
        primariesVal = -1, primariesInVal = -1;
    ArgLookup matrixArg, matrixInArg, transferArg, transferInArg, primariesArg, primariesInArg;
    if (!lookupColour("matrix", resizeMatrixTable, VSC_MATRIX_UNSPECIFIED, &matrixVal, &matrixArg, "matrix") ||
        !lookupColour("matrix_in", resizeMatrixTable, VSC_MATRIX_UNSPECIFIED, &matrixInVal, &matrixInArg, "matrix_in") ||
        !lookupColour("transfer", resizeTransferTable, VSC_TRANSFER_UNSPECIFIED, &transferVal, &transferArg, "transfer") ||
        !lookupColour("transfer_in", resizeTransferTable, VSC_TRANSFER_UNSPECIFIED, &transferInVal, &transferInArg, "transfer_in") ||
        !lookupColour("primaries", resizePrimariesTable, VSC_PRIMARIES_UNSPECIFIED, &primariesVal, &primariesArg, "primaries") ||
        !lookupColour("primaries_in", resizePrimariesTable, VSC_PRIMARIES_UNSPECIFIED, &primariesInVal, &primariesInArg, "primaries_in"))
        return false;

    const bool srcYuvish = spec.srcFmt.colorFamily == cfYUV || spec.srcFmt.colorFamily == cfGray;
    const bool dstYuvish = spec.dstFmt.colorFamily == cfYUV || spec.dstFmt.colorFamily == cfGray;
    const bool toRGB = srcYuvish && spec.dstFmt.colorFamily == cfRGB;
    const bool toYUV = spec.srcFmt.colorFamily == cfRGB && dstYuvish;
    const bool bothRGB = spec.srcFmt.colorFamily == cfRGB && spec.dstFmt.colorFamily == cfRGB;
    const bool bothYUV = spec.srcFmt.colorFamily == cfYUV && spec.dstFmt.colorFamily == cfYUV;
    const bool needsTransfer = transferArg == ArgLookup::Resolved;
    const bool needsGamut = primariesArg == ArgLookup::Resolved;
    /* One YUV matrix to another is out through RGB and back in. The matrix argument
       makes it a conversion outright; a transfer or gamut change without one still
       passes through RGB, re-encoding with the same per-frame matrix it decoded with. */
    const bool yuvToYuvMatrix = bothYUV && matrixArg == ArgLookup::Resolved;
    const bool yuvToYuvSame = bothYUV && !yuvToYuvMatrix && (needsTransfer || needsGamut);
    const bool yuvToYuv = yuvToYuvMatrix || yuvToYuvSame;

    /* The _in arguments state what the source is; they ask for nothing on their own. So
       an output tag that no conversion decided falls back to them rather than to
       unspecified, which is the rule the scalar path follows for a frame carrying no tag
       of its own. True whether or not anything else here converts. */
    if (matrixInArg == ArgLookup::Resolved)
        spec.tagMatrixIn = matrixInVal;
    if (transferInArg == ArgLookup::Resolved)
        spec.tagTransferIn = transferInVal;
    if (primariesInArg == ArgLookup::Resolved)
        spec.tagPrimariesIn = primariesInVal;

    if (!(toRGB || toYUV || yuvToYuv || needsTransfer || needsGamut)) {
        if (!spec.sameFamily && !spec.dropsChroma && !spec.fillsChroma)
            return give_up("a colour family conversion");
        /* Nothing is converted and the only arguments that can still be here are the _in
           ones, so the output is what it would be with no argument at all, retagged.
           Declining these turned `matrix_in_s=...` on an ordinary 4:2:0 to 4:4:4 resize
           into a hard error. The one combination left to refuse is the one the scalar
           path refuses too, with "RGB color family cannot have YUV matrix coefficients". */
        if (matrixInArg == ArgLookup::Resolved && spec.srcFmt.colorFamily == cfRGB &&
                matrixInVal != VSC_MATRIX_RGB)
            return give_up("matrix_in names something other than RGB on an RGB source");
    } else {
        if ((needsTransfer || needsGamut) && !(toRGB || toYUV || bothRGB || yuvToYuv))
            return give_up("a transfer or primaries conversion that does not pass through RGB");

        ColourConfig cc;
        cc.active = true;
        cc.workInFamily = spec.srcFmt.colorFamily == cfRGB ? cfRGB : cfYUV;
        cc.workOutFamily = spec.dstFmt.colorFamily == cfRGB ? cfRGB : cfYUV;
        cc.inMatActive = toRGB || yuvToYuv;
        cc.outMatActive = toYUV || yuvToYuv;

        /* Which kind of matrix an argument value names; ordinary values additionally
           have to be implemented. The per-frame path makes the same decision from
           _Matrix in resolveColourFrame. */
        auto argKind = [](int64_t v, ColourConfig::MatKind *kind) {
            if (v == VSC_MATRIX_BT2020_CL)
                *kind = ColourConfig::MatCL2020;
            else if (v == VSC_MATRIX_CHROMATICITY_DERIVED_CL)
                *kind = ColourConfig::MatCLPrim;
            else if (v == VSC_MATRIX_CHROMATICITY_DERIVED_NCL)
                *kind = ColourConfig::MatNclPrim;
            else if (v == VSC_MATRIX_ICTCP)
                *kind = ColourConfig::MatICtCp;
            else {
                zimg::colorspace::MatrixCoefficients mc;
                if (!matrixCoeffOfValue(v, &mc))
                    return false;
                *kind = ColourConfig::MatFixed;
            }
            return true;
        };

        if (cc.inMatActive) {
            cc.inKind = ColourConfig::MatFrame;
            if (matrixInArg == ArgLookup::Resolved) {
                ColourConfig::MatKind k;
                if (!argKind(matrixInVal, &k))
                    return give_up("a matrix_in this path does not implement");
                if (k == ColourConfig::MatFixed)
                    cc.inMatrixFallback = matrixInVal;   /* the frame still outranks it */
                else
                    cc.inKind = k;                       /* specials are settled by the argument */
            }
        } else if (matrixInArg == ArgLookup::Resolved && matrixInVal != VSC_MATRIX_RGB) {
            return give_up("matrix_in names something other than RGB on an RGB source");
        }

        if (cc.outMatActive) {
            if (yuvToYuvSame) {
                cc.outKind = ColourConfig::MatFrameSame;
            } else {
                if (toYUV && matrixArg != ArgLookup::Resolved)
                    return give_up("no matrix for a conversion to YUV");
                ColourConfig::MatKind k;
                if (!argKind(matrixVal, &k))
                    return give_up("a matrix this path does not implement");
                cc.outKind = k;
                cc.outMatrixValue = matrixVal;
            }
        } else if (matrixArg == ArgLookup::Resolved && matrixVal != VSC_MATRIX_RGB) {
            return give_up("a matrix named on a call that stays in RGB");
        }

        /* A special matrix -- constant luminance, chromaticity derived or ICtCp -- makes
           the _in fallback arguments meaningful on their own, since those stages read
           the transfer and primaries as inputs. */
        auto isSpecialValue = [](int64_t v) {
            return v == VSC_MATRIX_BT2020_CL || v == VSC_MATRIX_CHROMATICITY_DERIVED_CL ||
                v == VSC_MATRIX_CHROMATICITY_DERIVED_NCL || v == VSC_MATRIX_ICTCP;
        };
        const bool clSpecialNamed =
            (matrixInArg == ArgLookup::Resolved && isSpecialValue(matrixInVal)) ||
            (matrixArg == ArgLookup::Resolved && isSpecialValue(matrixVal));

        if (needsTransfer) {
            const int c = transferCurveOfValue(transferVal);
            if (c < 0)
                return give_up("a transfer this path does not implement");
            cc.doTransfer = true;
            cc.outCurve = c;
            cc.outTransferValue = transferVal;
        }
        if (transferInArg == ArgLookup::Resolved) {
            if (transferCurveOfValue(transferInVal) < 0)
                return give_up("a transfer_in this path does not implement");
            cc.transferInFallback = transferInVal;
            /* Naming only the input curve with nothing that decodes is a tag change with
               no pixels behind it; alongside a gamut, constant luminance or ICtCp stage
               it says what to linearise from. */
            if (!needsTransfer && !needsGamut && !clSpecialNamed)
                return give_up("transfer_in without transfer");
        }

        if (needsGamut) {
            zimg::colorspace::ColorPrimaries p;
            if (!primariesOfValue(primariesVal, &p))
                return give_up("primaries this path does not implement");
            cc.doGamut = true;
            cc.outPrimValue = primariesVal;
            /* chromatic_adaptation is accepted and ignored: zimg reads it only from API
               2.5 up, so the scalar path quietly does the same thing. The white patch
               test pins both paths to the no-adaptation answer. */
        }
        /* primaries_in without primaries used to be declined here. It converts no gamut
           -- that needs the output side named -- so all it does is say what the source
           is, which settles the input tag that then carries out, and feeds a
           chromaticity-derived matrix if one is in play. The block below already does
           both; declining first only stopped it. */
        if (primariesInArg == ArgLookup::Resolved) {
            zimg::colorspace::ColorPrimaries p;
            if (!primariesOfValue(primariesInVal, &p))
                return give_up("primaries_in this path does not implement");
            cc.primInFallback = primariesInVal;
        }
        cc.peakLuminance = optFloat(in, "nominal_luminance", 100.0, vsapi);

        /* The colour pass runs at the smaller of the two sizes on each axis, which is
           what leaves every axis resampled exactly once: a downscale does all of it
           before, an upscale all of it after. The working state CARRIES the smaller
           side's active region, exactly as graphbuilder builds its float 4:4:4 state --
           which is what lets a window ride an upscaling conversion and still be consumed
           by exactly one resample per plane. */
        const bool srcSmallerW = spec.srcW < spec.dstW;
        const bool srcSmallerH = spec.srcH < spec.dstH;
        cc.workW = srcSmallerW ? spec.srcW : spec.dstW;
        cc.workH = srcSmallerH ? spec.srcH : spec.dstH;
        cc.workActiveLeft = srcSmallerW ? spec.activeLeft : 0.0;
        cc.workActiveWidth = srcSmallerW ? spec.activeWidth : spec.dstW;
        cc.workActiveTop = srcSmallerH ? spec.activeTop : 0.0;
        cc.workActiveHeight = srcSmallerH ? spec.activeHeight : spec.dstH;

        /* A plane needs a stage on a side exactly when its state differs from the
           working state: dimensions, or the active region -- a windowed luma plane at
           the working size IS the working state on an upscale, and passes through. */
        const bool srcActiveDiffers = spec.activeLeft != cc.workActiveLeft ||
            spec.activeTop != cc.workActiveTop || spec.activeWidth != cc.workActiveWidth ||
            spec.activeHeight != cc.workActiveHeight;
        const bool dstActiveDiffers = cc.workActiveLeft != 0.0 || cc.workActiveTop != 0.0 ||
            cc.workActiveWidth != spec.dstW || cc.workActiveHeight != spec.dstH;
        for (int p = 0; p < 3; p++) {
            if (spec.srcFmt.colorFamily == cfGray && p != 0) {
                cc.inZero[p] = true;
                continue;
            }
            if (p >= spec.srcFmt.numPlanes)
                continue;
            const uint32_t pw = p == 0 ? spec.srcW : spec.srcW >> spec.srcFmt.subSamplingW;
            const uint32_t ph = p == 0 ? spec.srcH : spec.srcH >> spec.srcFmt.subSamplingH;
            cc.needIn[p] = pw != cc.workW || ph != cc.workH || srcActiveDiffers;
        }
        for (int p = 0; p < spec.dstFmt.numPlanes; p++) {
            const uint32_t pw = p == 0 ? spec.dstW : spec.dstW >> spec.dstFmt.subSamplingW;
            const uint32_t ph = p == 0 ? spec.dstH : spec.dstH >> spec.dstFmt.subSamplingH;
            cc.needOut[p] = pw != cc.workW || ph != cc.workH || dstActiveDiffers;
        }
        spec.colour = cc;
    }

    *out = spec;
    return true;
}

} // namespace

/* Builds a GPU resize node into out and returns true, or returns false, in which case
   either decline names an argument the compute path does not model and out is untouched,
   or the path failed at something it does implement and the final message is already in
   out. */
bool createGPUResize(const VSMap *in, VSMap *out, const char *kernelName, bool deinterlace,
    VSCore *core, const VSAPI *vsapi, std::string &decline) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    auto give_up = [&](const std::string &reason) {
        decline = reason;
        vsapi->freeNode(node);
        return false;
    };

    /* Distinct from give_up: a decline says the arguments are outside what this path
       models, which the caller phrases as such and points at GPUDownload. These are the
       path failing at something it does implement -- a driver refusing a pipeline, VRAM
       exhausted, a device feature a kernel needs -- and calling that unimplemented sends
       the reader after the wrong thing, so the message is final and set here. */
    auto hard_error = [&](const std::string &message) {
        vsapi->mapSetError(out, ("Resize: " + message).c_str());
        vsapi->freeNode(node);
        return false;
    };

    auto d = std::make_unique<GPUResizeData>();
    /* resolveSpec refuses a variable clip too, but saying it here keeps the reason the
       caller is told the specific one rather than whatever the spec resolution happens to
       trip over first. */
    if (!isConstantVideoFormat(vi))
        return give_up("a variable format or variable dimension clip");
    if (!resolveSpec(in, kernelName, deinterlace, vi, core, vsapi, &d->pipes.spec, decline)) {
        vsapi->freeNode(node);
        return false;
    }
    d->vi = *vi;
    d->vi.width = static_cast<int>(d->pipes.spec.dstW);
    d->vi.height = static_cast<int>(d->pipes.spec.dstH);
    d->vi.format = d->pipes.spec.dstFmt;

    char err[512] = { 0 };
    d->vkapi = vsapi->getVulkanAPI();
    if (d->vkapi->getVulkanHandles(core, &d->handles, err, sizeof(err)))
        return hard_error(err);
    d->vk = d->vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!d->vk)
        return hard_error(err);

    /* Every pass shape the planner can emit: direction x which end is samples, per
       kernel class. Built up front because which of them a frame needs is per frame but
       the set of possibilities is not; the core caches compiles by source text, so the
       repeat combinations parse once. */
    std::string error;
    if (!buildPipeSet(d.get(), d->pipes, core, error))
        return hard_error(error);

    d->pool = d->vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!d->pool)
        return hard_error(err);

    VSFilterDependency deps[] = {{ node, rpStrictSpatial }};
    d->node = node;
    VSNode *built = vsapi->createVideoFilterEx2(deinterlace ? "Bob" : kernelName, &d->vi,
        gpuResizeGetFrame, gpuResizeFree, fmParallel, ffGPUOutput, deps, 1, d.get(), core);
    if (!built) {
        d->node = nullptr;
        return hard_error("filter creation failed");
    }
    d.release();
    vsapi->mapConsumeNode(out, "clip", built, maReplace);
    return true;
}
