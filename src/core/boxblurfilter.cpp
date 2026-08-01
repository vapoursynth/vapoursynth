/*
* Copyright (c) 2017-2020 Fredrik Mellbin
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

#include <memory>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include "internalfilters.h"
#include "VSHelper4.h"
#include "VSVulkan4.h"
#include "filtershared.h"
#include "float16_helper.h"
#include "gpufilter.h"
#include <cstring>

using namespace std::string_literals;

//////////////////////////////////////////
// BoxBlur

struct BoxBlurData {
    VSNode *node;
    int radius, passes;
};

/*
 * Portable division-by-constant for the box-blur denominator (2*radius+1).
 * The divisor is fixed for the whole plane, so we precompute a 32-bit magic +
 * shift (libdivide/Hacker's-Delight style) once and replace the per-pixel
 * hardware divide with a multiply-high. Uses only a uint64_t product (32x32->64),
 * matching the magic-division already used in kernel/merge.c -- no __int128 /
 * __umulh, so it is portable to 32-bit and to MSVC/GCC/Clang alike. Exact for
 * every dividend in [0, 2^32); the box-blur accumulator stays below 2^32.
 */
struct FastDivU32 {
    uint32_t magic;
    uint32_t shift;
    uint32_t add;
};

static FastDivU32 makeFastDivU32(uint32_t d) {
    FastDivU32 fd;
    uint32_t l = 0;
    while ((d >> l) > 1)
        ++l; // l = floor(log2(d))

    if ((d & (d - 1)) == 0) { // power of two (2*radius+1 is odd, so unused, but keep correct)
        fd.magic = 0;
        fd.shift = l;
        fd.add = 0;
        return fd;
    }

    uint64_t two_l = static_cast<uint64_t>(1) << (32 + l);
    uint32_t m = static_cast<uint32_t>(two_l / d);
    uint32_t rem = static_cast<uint32_t>(two_l - static_cast<uint64_t>(m) * d);
    if (d - rem < (1u << l)) {
        fd.magic = m + 1;
        fd.shift = l;
        fd.add = 0;
    } else {
        uint32_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem)
            m += 1;
        fd.magic = m + 1;
        fd.shift = l;
        fd.add = 1;
    }
    return fd;
}

static inline uint32_t fastDivU32(uint32_t n, FastDivU32 fd) {
    if (fd.magic == 0)
        return n >> fd.shift;
    uint32_t q = static_cast<uint32_t>((static_cast<uint64_t>(fd.magic) * n) >> 32);
    if (fd.add) {
        uint32_t t = ((n - q) >> 1) + q;
        return t >> fd.shift;
    }
    return q >> fd.shift;
}

template<typename T>
static void blurH(const T * VS_RESTRICT src, T * VS_RESTRICT dst, const int width, const int radius, const FastDivU32 fd, const unsigned round) {
    unsigned acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
        acc -= src[std::max(x - radius, 0)];
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            acc += src[x + radius];
            dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
            acc -= src[x - radius];
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
            acc -= src[std::max(x - radius, 0)];
        }
    }
}

template<typename T>
static void blurH_inplace(const T *src, T *dst, const int width, const int radius, const FastDivU32 fd, const unsigned round, T *ring) {
    const int R = std::min(radius + 1, width);
    const unsigned first = src[0]; // the clamped left border always subtracts src[0]
    int wr = 0;

    unsigned acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        ring[wr] = src[x];
        if (++wr == R)
            wr = 0;
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
        acc -= first;
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += src[x + radius];
            dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
            acc -= ring[wr]; // ring[wr] is now the oldest entry == original src[x - radius]
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = static_cast<T>(fastDivU32(acc + round, fd));
            acc -= ring[wr];
        }
    }
}

template<typename T>
static void processPlane(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes, int radius, uint8_t *ring) {
    const unsigned div = radius * 2 + 1;
    const unsigned round = div - 1;
    const FastDivU32 fd = makeFastDivU32(div);
    for (int h = 0; h < height; h++) {
        blurH(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width, radius, fd, round);
        for (int p = 1; p < passes; p++)
            blurH_inplace(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width, radius, fd, (p & 1) ? 0 : round, reinterpret_cast<T *>(ring));
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHF(const T * VS_RESTRICT src, T * VS_RESTRICT dst, const int width, const int radius, const T div) {
    T acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = acc * div;
        acc -= src[std::max(x - radius, 0)];
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            acc += src[x + radius];
            dst[x] = acc * div;
            acc -= src[x - radius];
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = acc * div;
            acc -= src[std::max(x - radius, 0)];
        }
    }
}

// In-place-capable variant of blurHF; see blurH_inplace for how the ring works.
template<typename T>
static void blurHF_inplace(const T *src, T *dst, const int width, const int radius, const T div, T *ring) {
    const int R = std::min(radius + 1, width);
    const T first = src[0];
    int wr = 0;

    T acc = radius * src[0];
    for (int x = 0; x < radius; x++)
        acc += src[std::min(x, width - 1)];

    for (int x = 0; x < std::min(radius, width); x++) {
        ring[wr] = src[x];
        if (++wr == R)
            wr = 0;
        acc += src[std::min(x + radius, width - 1)];
        dst[x] = acc * div;
        acc -= first;
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += src[x + radius];
            dst[x] = acc * div;
            acc -= ring[wr];
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += src[std::min(x + radius, width - 1)];
            dst[x] = acc * div;
            acc -= ring[wr];
        }
    }
}

template<typename T>
static void processPlaneF(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes, int radius, uint8_t *ring) {
    const T div = static_cast<T>(1) / (radius * 2 + 1);
    for (int h = 0; h < height; h++) {
        blurHF(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width, radius, div);
        for (int p = 1; p < passes; p++)
            blurHF_inplace(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width, radius, div, reinterpret_cast<T *>(ring));
        src += stride;
        dst += stride;
    }
}

static void blurHF_half(const uint16_t * VS_RESTRICT src, uint16_t * VS_RESTRICT dst, const int width, const int radius, const float div) {
    float acc = radius * halfToFloat(src[0]);
    for (int x = 0; x < radius; x++)
        acc += halfToFloat(src[std::min(x, width - 1)]);

    for (int x = 0; x < std::min(radius, width); x++) {
        acc += halfToFloat(src[std::min(x + radius, width - 1)]);
        dst[x] = floatToHalf(acc * div);
        acc -= halfToFloat(src[std::max(x - radius, 0)]);
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            acc += halfToFloat(src[x + radius]);
            dst[x] = floatToHalf(acc * div);
            acc -= halfToFloat(src[x - radius]);
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            acc += halfToFloat(src[std::min(x + radius, width - 1)]);
            dst[x] = floatToHalf(acc * div);
            acc -= halfToFloat(src[std::max(x - radius, 0)]);
        }
    }
}

static void blurHF_inplace_half(const uint16_t *src, uint16_t *dst, const int width, const int radius, const float div, uint16_t *ring) {
    const int R = std::min(radius + 1, width);
    const float first = halfToFloat(src[0]);
    int wr = 0;

    float acc = radius * halfToFloat(src[0]);
    for (int x = 0; x < radius; x++)
        acc += halfToFloat(src[std::min(x, width - 1)]);

    for (int x = 0; x < std::min(radius, width); x++) {
        ring[wr] = src[x];
        if (++wr == R)
            wr = 0;
        acc += halfToFloat(src[std::min(x + radius, width - 1)]);
        dst[x] = floatToHalf(acc * div);
        acc -= first;
    }

    if (width > radius) {
        for (int x = radius; x < width - radius; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += halfToFloat(src[x + radius]);
            dst[x] = floatToHalf(acc * div);
            acc -= halfToFloat(ring[wr]);
        }

        for (int x = std::max(width - radius, radius); x < width; x++) {
            ring[wr] = src[x];
            if (++wr == R)
                wr = 0;
            acc += halfToFloat(src[std::min(x + radius, width - 1)]);
            dst[x] = floatToHalf(acc * div);
            acc -= halfToFloat(ring[wr]);
        }
    }
}

static void processPlaneF_half(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes, int radius, uint8_t *ring) {
    const float div = 1.0f / (radius * 2 + 1);
    for (int h = 0; h < height; h++) {
        blurHF_half(reinterpret_cast<const uint16_t *>(src), reinterpret_cast<uint16_t *>(dst), width, radius, div);
        for (int p = 1; p < passes; p++)
            blurHF_inplace_half(reinterpret_cast<const uint16_t *>(dst), reinterpret_cast<uint16_t *>(dst), width, radius, div, reinterpret_cast<uint16_t *>(ring));
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHR1(const T *src, T *dst, int width, const unsigned round) {
    unsigned tmp[2] = { src[0], src[1] };
    unsigned acc = tmp[0] * 2 + tmp[1];
    dst[0] = (acc + round) / 3;
    acc -= tmp[0];

    unsigned v = src[2];
    acc += v;
    dst[1] = (acc + round) / 3;
    acc -= tmp[0];
    tmp[0] = v;

    for (int x = 2; x < width - 2; x += 2) {
        v = src[x + 1];
        acc += v;
        dst[x] = (acc + round) / 3;
        acc -= tmp[1];
        tmp[1] = v;

        v = src[x + 2];
        acc += v;
        dst[x + 1] = (acc + round) / 3;
        acc -= tmp[0];
        tmp[0] = v;
    }

    if (width & 1) {
        acc += tmp[0];
        dst[width - 1] = (acc + round) / 3;
    } else {
        v = src[width - 1];
        acc += v;
        dst[width - 2] = (acc + round) / 3;
        acc -= tmp[1];

        acc += v;
        dst[width - 1] = (acc + round) / 3;
    }
}

template<typename T>
static void processPlaneR1(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width, 2);
        for (int p = 1; p < passes; p++)
            blurHR1(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width, (p & 1) ? 0 : 2);
        src += stride;
        dst += stride;
    }
}

template<typename T>
static void blurHR1F(const T *src, T *dst, int width) {
    T tmp[2] = { src[0], src[1] };
    T acc = tmp[0] * 2 + tmp[1];
    const T div = static_cast<T>(1) / 3;
    dst[0] = acc * div;
    acc -= tmp[0];

    T v = src[2];
    acc += v;
    dst[1] = acc * div;
    acc -= tmp[0];
    tmp[0] = v;

    for (int x = 2; x < width - 2; x += 2) {
        v = src[x + 1];
        acc += v;
        dst[x] = acc * div;
        acc -= tmp[1];
        tmp[1] = v;

        v = src[x + 2];
        acc += v;
        dst[x + 1] = acc * div;
        acc -= tmp[0];
        tmp[0] = v;
    }

    if (width & 1) {
        acc += tmp[0];
        dst[width - 1] = acc * div;
    }
    else {
        v = src[width - 1];
        acc += v;
        dst[width - 2] = acc * div;
        acc -= tmp[1];

        acc += v;
        dst[width - 1] = acc * div;
    }
}

template<typename T>
static void processPlaneR1F(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1F(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width);
        for (int p = 1; p < passes; p++)
            blurHR1F(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width);
        src += stride;
        dst += stride;
    }
}

// float16 radius-1 fast path: same read-ahead structure as blurHR1F (so it stays
// in-place safe), accumulating in float32 and widening/narrowing at the edges.
static void blurHR1F_half(const uint16_t *src, uint16_t *dst, int width) {
    float tmp[2] = { halfToFloat(src[0]), halfToFloat(src[1]) };
    float acc = tmp[0] * 2 + tmp[1];
    const float div = 1.0f / 3;
    dst[0] = floatToHalf(acc * div);
    acc -= tmp[0];

    float v = halfToFloat(src[2]);
    acc += v;
    dst[1] = floatToHalf(acc * div);
    acc -= tmp[0];
    tmp[0] = v;

    for (int x = 2; x < width - 2; x += 2) {
        v = halfToFloat(src[x + 1]);
        acc += v;
        dst[x] = floatToHalf(acc * div);
        acc -= tmp[1];
        tmp[1] = v;

        v = halfToFloat(src[x + 2]);
        acc += v;
        dst[x + 1] = floatToHalf(acc * div);
        acc -= tmp[0];
        tmp[0] = v;
    }

    if (width & 1) {
        acc += tmp[0];
        dst[width - 1] = floatToHalf(acc * div);
    }
    else {
        v = halfToFloat(src[width - 1]);
        acc += v;
        dst[width - 2] = floatToHalf(acc * div);
        acc -= tmp[1];

        acc += v;
        dst[width - 1] = floatToHalf(acc * div);
    }
}

static void processPlaneR1F_half(const uint8_t *src, uint8_t *dst, ptrdiff_t stride, int width, int height, int passes) {
    for (int h = 0; h < height; h++) {
        blurHR1F_half(reinterpret_cast<const uint16_t *>(src), reinterpret_cast<uint16_t *>(dst), width);
        for (int p = 1; p < passes; p++)
            blurHR1F_half(reinterpret_cast<const uint16_t *>(dst), reinterpret_cast<uint16_t *>(dst), width);
        src += stride;
        dst += stride;
    }
}

static const VSFrame *VS_CC boxBlurGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        VSFrame *dst = vsapi->newVideoFrame(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);
        int bytesPerSample = fi->bytesPerSample;
        int radius = d->radius;

        const uint8_t *srcp = vsapi->getReadPtr(src, 0);
        ptrdiff_t stride = vsapi->getStride(src, 0);
        uint8_t *dstp = vsapi->getWritePtr(dst, 0);
        int h = vsapi->getFrameHeight(src, 0);
        int w = vsapi->getFrameWidth(src, 0);

        // the radius 1 fast path reads three pixels unconditionally so narrower planes are
        // routed through the general clamped path instead
        bool useR1 = radius == 1 && w >= 3;
        uint8_t *ring = (!useR1 && d->passes > 1) ? new uint8_t[bytesPerSample * std::min(radius + 1, w)] : nullptr;

        if (useR1) {
            if (bytesPerSample == 1)
                processPlaneR1<uint8_t>(srcp, dstp, stride, w, h, d->passes);
            else if (fi->sampleType == stInteger && bytesPerSample == 2)
                processPlaneR1<uint16_t>(srcp, dstp, stride, w, h, d->passes);
            else if (bytesPerSample == 2)
                processPlaneR1F_half(srcp, dstp, stride, w, h, d->passes);
            else
                processPlaneR1F<float>(srcp, dstp, stride, w, h, d->passes);
        } else {
            if (bytesPerSample == 1)
                processPlane<uint8_t>(srcp, dstp, stride, w, h, d->passes, radius, ring);
            else if (fi->sampleType == stInteger && bytesPerSample == 2)
                processPlane<uint16_t>(srcp, dstp, stride, w, h, d->passes, radius, ring);
            else if (bytesPerSample == 2)
                processPlaneF_half(srcp, dstp, stride, w, h, d->passes, radius, ring);
            else
                processPlaneF<float>(srcp, dstp, stride, w, h, d->passes, radius, ring);
        }

        delete[] ring;

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC boxBlurFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

//////////////////////////////////////////
// The GPU implementation of the same filter. std.BoxBlur is residency polymorphic:
// declared vnode:all, it builds this single node when handed a GPU clip and the CPU
// decomposition below otherwise, so a script only ever names one filter.

namespace {

struct BlurPush {
    uint32_t width;
    uint32_t height;
    uint32_t srcStride; /* in elements */
    uint32_t dstStride;
    uint32_t radius;
    uint32_t rounding; /* integer variants only */
    uint32_t vertical;
    float invDiv;      /* float variants only */
};

/* One box blur pass over one plane, horizontal or vertical by push constant. Integer
   variants match the CPU filter's math exactly: clamped edges and (sum + rounding)/(2r+1),
   with the caller alternating the rounding term between passes the same way the CPU code
   does. Float variants accumulate in float and multiply by 1/(2r+1); they cannot be bit
   exact against the CPU's running sum, which rounds differently, so they are verified with
   a tolerance instead.

   Compiled at filter creation through compileGPUShader, specialized by a preamble carrying
   SAMPLE_T in {uint8_t, uint16_t} and, with FLOAT_SAMPLES, {float, float16_t}. The preamble
   also supplies #version, which the language demands be the very first token, so the body
   here starts at the extension list. The core caches by source text, so the four variants
   parse once each however many instances a script builds. */
const char boxBlurGlsl[] =
    "#extension GL_EXT_shader_8bit_storage : require\n"
    "#extension GL_EXT_shader_16bit_storage : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require\n"
    "#ifdef FLOAT_SAMPLES\n"
    "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n"
    "#endif\n"
    "\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "\n"
    "layout(std430, set = 0, binding = 0) readonly buffer Src { SAMPLE_T srcData[]; };\n"
    "layout(std430, set = 0, binding = 1) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
    "\n"
    "layout(push_constant, std430) uniform Push {\n"
    "    uint width;\n"
    "    uint height;\n"
    "    uint srcStride; /* in elements */\n"
    "    uint dstStride;\n"
    "    uint radius;\n"
    "    uint rounding;  /* integer variants only */\n"
    "    uint vertical;\n"
    "    float invDiv;   /* float variants only */\n"
    "} pc;\n"
    "\n"
    "void main() {\n"
    "    uint x = gl_GlobalInvocationID.x;\n"
    "    uint y = gl_GlobalInvocationID.y;\n"
    "    if (x >= pc.width || y >= pc.height)\n"
    "        return;\n"
    "\n"
    "    int len = int(pc.vertical != 0u ? pc.height : pc.width);\n"
    "    int pos = int(pc.vertical != 0u ? y : x);\n"
    "    int stepSize = int(pc.vertical != 0u ? pc.srcStride : 1u);\n"
    "    int base = int(y * pc.srcStride + x);\n"
    "    int r = int(pc.radius);\n"
    "\n"
    "#ifdef FLOAT_SAMPLES\n"
    "    float acc = 0.0;\n"
    "    for (int k = -r; k <= r; k++) {\n"
    "        int p = clamp(pos + k, 0, len - 1);\n"
    "        acc += float(srcData[uint(base + (p - pos) * stepSize)]);\n"
    "    }\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T(acc * pc.invDiv);\n"
    "#else\n"
    "    uint acc = 0u;\n"
    "    for (int k = -r; k <= r; k++) {\n"
    "        int p = clamp(pos + k, 0, len - 1);\n"
    "        acc += uint(srcData[uint(base + (p - pos) * stepSize)]);\n"
    "    }\n"
    "    dstData[y * pc.dstStride + x] = SAMPLE_T((acc + pc.rounding) / (2u * pc.radius + 1u));\n"
    "#endif\n"
    "}\n";

/* The pass schedule replicates the CPU filter exactly: horizontal passes first with
   rounding div-1, 0, div-1, ... then vertical ones restarting the pattern with their own
   divisor. Float formats have no rounding term, just the reciprocal divisor. */
struct BlurPass {
    uint32_t radius, rounding, vertical;
    float invDiv;
};

std::vector<BlurPass> buildSchedule(int hradius, int hpasses, int vradius, int vpasses) {
    std::vector<BlurPass> schedule;
    if (hradius > 0)
        for (int i = 0; i < hpasses; i++)
            schedule.push_back({ static_cast<uint32_t>(hradius), !(i & 1) ? 2u * hradius : 0u, 0,
                1.0f / (2 * hradius + 1) });
    if (vradius > 0)
        for (int i = 0; i < vpasses; i++)
            schedule.push_back({ static_cast<uint32_t>(vradius), !(i & 1) ? 2u * vradius : 0u, 1,
                1.0f / (2 * vradius + 1) });
    return schedule;
}

/* Builds the GPU node by describing it rather than recording it: the driver in gpufilter.h
   owns the frame loop, so all that is left here is choosing the kernel variant, laying out
   the ping pong so the last pass lands in the output plane, and filling push constants.
   Consumes the node reference on success and leaves it to the caller on failure. */
VSNode *createGPUBoxBlur(VSNode *node, const bool process[3], int hradius, int hpasses, int vradius, int vpasses,
    VSCore *core, const VSAPI *vsapi) {
    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    std::string preamble = "#version 460\n";
    if (vi->format.sampleType == stInteger) {
        preamble += vi->format.bytesPerSample == 1 ? "#define SAMPLE_T uint8_t\n" : "#define SAMPLE_T uint16_t\n";
    } else if (vi->format.bytesPerSample == 4) {
        preamble += "#define SAMPLE_T float\n#define FLOAT_SAMPLES\n";
    } else {
        preamble += "#define SAMPLE_T float16_t\n#define FLOAT_SAMPLES\n";
    }

    vsgpu::FilterDesc desc;
    desc.vi = *vi;
    desc.nodes.push_back(node);
    for (int p = 0; p < 3; p++)
        desc.process[p] = process[p];

    vsgpu::Program program;
    program.glsl = preamble + boxBlurGlsl;
    program.storageBufferCount = 2;
    program.pushConstantBytes = sizeof(BlurPush);
    desc.programs.push_back(program);

    const std::vector<BlurPass> schedule = buildSchedule(hradius, hpasses, vradius, vpasses);
    const int passes = static_cast<int>(schedule.size());
    desc.scratchCount = passes > 1 ? 1 : 0;

    /* Alternate so the final pass always lands in the destination plane. */
    for (int i = 0; i < passes; i++) {
        vsgpu::Pass pass;
        pass.bindings.push_back(i == 0 ? vsgpu::Operand::source()
                                       : (((passes - i) % 2 == 0) ? vsgpu::Operand::output() : vsgpu::Operand::scratch(0)));
        pass.bindings.push_back(((passes - 1 - i) % 2 == 0) ? vsgpu::Operand::output() : vsgpu::Operand::scratch(0));
        desc.passes.push_back(std::move(pass));
    }

    desc.fillPush = [schedule](const vsgpu::PassInfo &info, void *pushData) {
        const BlurPass &p = schedule[info.pass];
        BlurPush push = {};
        push.width = info.width;
        push.height = info.height;
        push.srcStride = info.srcStrideElements();
        push.dstStride = info.dstStrideElements();
        push.radius = p.radius;
        push.rounding = p.rounding;
        push.vertical = p.vertical;
        push.invDiv = p.invDiv;
        std::memcpy(pushData, &push, sizeof(push));
    };

    VSFilterDependency deps[] = {{ node, rpStrictSpatial }};
    std::string createError;
    VSNode *result = vsgpu::createFilter("BoxBlur", desc, deps, 1, core, vsapi, createError);
    if (!result) {
        /* A half precision kernel is the one variant a conformant device may refuse. */
        if (vi->format.sampleType == stFloat && vi->format.bytesPerSample == 2)
            createError += " (half precision formats need the shaderFloat16 feature, which this device may lack)";
        throw std::runtime_error(createError);
    }
    return result;
}

} // namespace

static VSNode *applyBoxBlurPlaneFiltering(VSPlugin *stdplugin, VSNode *node, int hradius, int hpasses, int vradius, int vpasses, VSCore *core, const VSAPI *vsapi) {
    bool hblur = (hradius > 0) && (hpasses > 0);
    bool vblur = (vradius > 0) && (vpasses > 0);

    if (hblur) {
        VSFilterDependency deps[] = {{node, rpStrictSpatial}};
        node = vsapi->createVideoFilter2("BoxBlur", vsapi->getVideoInfo(node), boxBlurGetframe, boxBlurFree, fmParallel, deps, 1, new BoxBlurData{node, hradius, hpasses}, core);
    }

    if (vblur) {
        VSMap *vtmp1 = vsapi->createMap();
        vsapi->mapConsumeNode(vtmp1, "clip", node, maAppend);
        VSMap *vtmp2 = vsapi->invoke(stdplugin, "Transpose", vtmp1);
        vsapi->clearMap(vtmp1);
        node = vsapi->mapGetNode(vtmp2, "clip", 0, nullptr);
        vsapi->clearMap(vtmp2);
        VSFilterDependency deps[] = {{node, rpStrictSpatial}};
        vsapi->createVideoFilter(vtmp2, "BoxBlur", vsapi->getVideoInfo(node), boxBlurGetframe, boxBlurFree, fmParallel, deps, 1, new BoxBlurData{ node, vradius, vpasses }, core);
        vsapi->freeMap(vtmp1);
        vtmp1 = vsapi->invoke(stdplugin, "Transpose", vtmp2);
        vsapi->freeMap(vtmp2);
        node = vsapi->mapGetNode(vtmp1, "clip", 0, nullptr);
        vsapi->freeMap(vtmp1);
    }

    return node;
}

static void VS_CC boxBlurCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNode *node = vsapi->mapGetNode(in, "clip", 0, 0);

    try {
        int err;
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);

        if (!is8to16orFloatFormat(vi->format))
            throw std::runtime_error(invalidVideoFormatMessage(vi->format, vsapi, nullptr));

        bool process[3];
        getPlanesArg(in, process, vsapi);

        int hradius = vsapi->mapGetIntSaturated(in, "hradius", 0, &err);
        if (err)
            hradius = 1;
        int hpasses = vsapi->mapGetIntSaturated(in, "hpasses", 0, &err);
        if (err)
            hpasses = 1;
        bool hblur = (hradius > 0) && (hpasses > 0);

        int vradius = vsapi->mapGetIntSaturated(in, "vradius", 0, &err);
        if (err)
            vradius = 1;
        int vpasses = vsapi->mapGetIntSaturated(in, "vpasses", 0, &err);
        if (err)
            vpasses = 1;
        bool vblur = (vradius > 0) && (vpasses > 0);

        if (hpasses < 0 || vpasses < 0)
            throw std::runtime_error("number of passes can't be negative");

        if (hradius < 0 || vradius < 0)
            throw std::runtime_error("radius can't be negative");

        if (hradius > 30000 || vradius > 30000)
            throw std::runtime_error("radius must be less than 30000");

        if (!hblur && !vblur)
            throw std::runtime_error("nothing to be performed");

        if (vi->width < 4 || vi->height < 4)
            throw std::runtime_error("dimensions must be at least 4x4");

        /* Residency polymorphic: a GPU clip gets the GPU node, which does every plane and
           both directions in one submission, and a CPU clip gets the decomposition below.
           The arguments, their validation and the results are the same either way. */
        if (vsapi->getNodeResidency(node) == nrGPU) {
            VSNode *gpuNode = createGPUBoxBlur(node, process, hradius, hpasses, vradius, vpasses, core, vsapi);
            node = nullptr; /* consumed on success */
            vsapi->mapConsumeNode(out, "clip", gpuNode, maAppend);
            return;
        }

        VSPlugin *stdplugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);

        if (vi->format.numPlanes == 1) {
            VSNode *tmpnode = applyBoxBlurPlaneFiltering(stdplugin, node, hradius, hpasses, vradius, vpasses, core, vsapi);
            node = nullptr;
            vsapi->mapSetNode(out, "clip", tmpnode, maAppend);
            vsapi->freeNode(tmpnode);
        } else {
            VSMap *mergeargs = vsapi->createMap();
            int64_t psrc[3] = { 0, process[1] ? 0 : 1, process[2] ? 0 : 2 };
            vsapi->mapSetIntArray(mergeargs, "planes", psrc, 3);
            vsapi->mapSetInt(mergeargs, "colorfamily", vi->format.colorFamily, maAppend);

            for (int plane = 0; plane < vi->format.numPlanes; plane++) {
                if (process[plane]) {
                    VSMap *vtmp1 = vsapi->createMap();
                    vsapi->mapSetNode(vtmp1, "clips", node, maAppend);
                    vsapi->mapSetInt(vtmp1, "planes", plane, maAppend);
                    vsapi->mapSetInt(vtmp1, "colorfamily", cfGray, maAppend);
                    VSMap *vtmp2 = vsapi->invoke(stdplugin, "ShufflePlanes", vtmp1);
                    vsapi->freeMap(vtmp1);
                    VSNode *tmpnode = vsapi->mapGetNode(vtmp2, "clip", 0, nullptr);
                    vsapi->freeMap(vtmp2);
                    tmpnode = applyBoxBlurPlaneFiltering(stdplugin, tmpnode, hradius, hpasses, vradius, vpasses, core, vsapi);
                    vsapi->mapConsumeNode(mergeargs, "clips", tmpnode, maAppend);
                } else {
                    vsapi->mapSetNode(mergeargs, "clips", node, maAppend);
                }
            }

            vsapi->freeNode(node);
            node = nullptr;

            VSMap *retmap = vsapi->invoke(stdplugin, "ShufflePlanes", mergeargs);
            vsapi->freeMap(mergeargs);
            vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(retmap, "clip", 0, nullptr), maAppend);
            vsapi->freeMap(retmap);
        }

    } catch (const std::exception &e) {
        vsapi->freeNode(node);
        RETERROR(("BoxBlur: "s + e.what()).c_str());
    }
}

//////////////////////////////////////////
// Init

void boxBlurInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("BoxBlur", "clip:vnode:all;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;", "clip:vnode:all;", boxBlurCreate, 0, plugin);
}
