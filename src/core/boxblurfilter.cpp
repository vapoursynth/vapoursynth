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
#include <mutex>

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

constexpr int cmdSlots = 4;

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

struct GPUBoxBlurData {
    VSNode *node = nullptr;
    VSVideoInfo vi = {};
    bool process[3] = {};
    int hradius = 0, hpasses = 0, vradius = 0, vpasses = 0;

    VSCore *core = nullptr;
    const VSVULKANAPI *vkapi = nullptr;
    const VSVulkanFunctions *vk = nullptr; /* the core's dispatch table; everything goes through it */
    VSVulkanCoreHandles h = {};
    VkQueue computeQueue = VK_NULL_HANDLE; /* the handles carry family and index; the queue is fetched */

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd[cmdSlots] = {};
    uint64_t slotValue[cmdSlots] = {};
    int nextSlot = 0;

    /* This filter's own timeline: it signals rising values and publishes them as the
       producer pairs of the planes it writes, so consumers wait on the device instead of
       the host. It must outlive every consumer, so it lives and dies with the instance. */
    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t nextValue = 0;

    /* Source frames and scratch buffers whose lifetime has to reach past the call: the GPU
       is still reading them when getFrame returns. Released once the timeline says the
       submission that used them completed. */
    struct Retained {
        const VSFrame *frame;
        VSGPUBuffer *buffer;
        uint64_t value;
    };
    std::vector<Retained> retained;

    std::mutex lock; /* the instance runs fmParallel; this covers the rings and the values */
};

/* Frees everything the GPU has demonstrably finished with. Non blocking: the counter query
   is a plain read, and whatever is still pending simply stays for the next call. */
void sweepRetained(GPUBoxBlurData *d, const VSAPI *vsapi) {
    uint64_t completed = 0;
    if (d->vk->vkGetSemaphoreCounterValue(d->h.device, d->timeline, &completed) != VK_SUCCESS)
        return;
    size_t kept = 0;
    for (size_t i = 0; i < d->retained.size(); i++) {
        if (d->retained[i].value <= completed) {
            if (d->retained[i].frame)
                vsapi->freeFrame(d->retained[i].frame);
            if (d->retained[i].buffer)
                d->vkapi->destroyGPUBuffer(d->retained[i].buffer);
        } else {
            d->retained[kept++] = d->retained[i];
        }
    }
    d->retained.resize(kept);
}

void computeToComputeBarrier(const VSVulkanFunctions *vk, VkCommandBuffer cmd) {
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vk->vkCmdPipelineBarrier2(cmd, &dependency);
}

const VSFrame *VS_CC gpuBoxBlurGetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        return nullptr;
    } else if (activationReason != arAllFramesReady) {
        return nullptr;
    }

    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
    int w = vsapi->getFrameWidth(src, 0);
    int h = vsapi->getFrameHeight(src, 0);

    /* Unprocessed planes are shared straight from the source, which keeps them on the
       device with their own producer pairs intact; when every plane is processed there is
       nothing to share and a plain GPU frame is what is wanted. */
    bool shareAny = false;
    for (int p = 0; p < fmt->numPlanes; p++)
        shareAny = shareAny || !d->process[p];

    VSFrame *dst;
    if (shareAny) {
        const VSFrame *planeSrc[3] = {};
        int planes[3] = { 0, 1, 2 };
        for (int p = 0; p < fmt->numPlanes; p++)
            planeSrc[p] = d->process[p] ? nullptr : src;
        dst = vsapi->newVideoFrame2(fmt, w, h, planeSrc, planes, src, core);
    } else {
        dst = d->vkapi->newGPUVideoFrame(fmt, w, h, src, core);
    }
    if (!dst) {
        vsapi->setFilterError("GPUBoxBlur: failed to allocate the output frame", frameCtx);
        vsapi->freeFrame(src);
        return nullptr;
    }

    char err[512] = { 0 };
    std::lock_guard<std::mutex> instanceLock(d->lock);
    sweepRetained(d, vsapi);

    /* The ping pong scratch buffer, sized for the largest processed plane, needed as soon
       as any plane runs more than one pass. */
    int totalPasses = (d->hradius > 0 ? d->hpasses : 0) + (d->vradius > 0 ? d->vpasses : 0);
    VSGPUBuffer *tmp = nullptr;
    VkBuffer tmpBuffer = VK_NULL_HANDLE;
    if (totalPasses > 1) {
        VkDeviceSize maxBytes = 0;
        for (int p = 0; p < fmt->numPlanes; p++) {
            if (!d->process[p])
                continue;
            VSVulkanPlaneInfo info;
            if (d->vkapi->getGPUPlane(src, p, &info)) {
                vsapi->setFilterError("GPUBoxBlur: source frame is not GPU resident", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                return nullptr;
            }
            maxBytes = std::max(maxBytes, info.bufferSize);
        }
        VSVulkanBufferInfo tmpInfo = {};
        tmp = d->vkapi->createGPUBuffer(core, maxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &tmpInfo, err, sizeof(err));
        if (!tmp) {
            vsapi->setFilterError((std::string("GPUBoxBlur: ") + err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
        tmpBuffer = tmpInfo.buffer;
    }

    /* Claim a command buffer slot and wait out whatever the GPU still owes it. The wait is
       instant except when this instance is already cmdSlots frames deep, which is the
       intended backpressure. */
    int slot = d->nextSlot;
    d->nextSlot = (d->nextSlot + 1) % cmdSlots;
    if (d->slotValue[slot]) {
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &d->timeline;
        waitInfo.pValues = &d->slotValue[slot];
        if (d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX) != VK_SUCCESS) {
            vsapi->setFilterError("GPUBoxBlur: waiting for a command buffer slot failed", frameCtx);
            if (tmp)
                d->vkapi->destroyGPUBuffer(tmp);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
    }

    VkCommandBuffer cmd = d->cmd[slot];
    d->vk->vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d->vk->vkBeginCommandBuffer(cmd, &begin);
    d->vk->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);

    /* Every plane this submission reads contributes its producer pair as a device side
       wait, deduplicated to the highest value per timeline the way the contract asks. */
    VkSemaphoreSubmitInfo waits[3] = {};
    uint32_t waitCount = 0;
    auto addWait = [&](VkSemaphore semaphore, uint64_t value) {
        if (!semaphore)
            return;
        for (uint32_t i = 0; i < waitCount; i++) {
            if (waits[i].semaphore == semaphore) {
                waits[i].value = std::max(waits[i].value, value);
                return;
            }
        }
        waits[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waits[waitCount].semaphore = semaphore;
        waits[waitCount].value = value;
        waits[waitCount].stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        waitCount++;
    };

    bool firstDispatch = true;
    for (int p = 0; p < fmt->numPlanes; p++) {
        if (!d->process[p])
            continue;

        VSVulkanPlaneInfo srcPlane, dstPlane;
        if (d->vkapi->getGPUPlane(src, p, &srcPlane) || d->vkapi->getGPUPlane(dst, p, &dstPlane)) {
            vsapi->setFilterError("GPUBoxBlur: frames are not GPU resident", frameCtx);
            d->vk->vkEndCommandBuffer(cmd);
            if (tmp)
                d->vkapi->destroyGPUBuffer(tmp);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }
        addWait(srcPlane.readySemaphore, srcPlane.readyValue);

        uint32_t planeWidth = static_cast<uint32_t>(vsapi->getFrameWidth(src, p));
        uint32_t planeHeight = static_cast<uint32_t>(vsapi->getFrameHeight(src, p));
        uint32_t strideElems = static_cast<uint32_t>(vsapi->getStride(src, p) / fmt->bytesPerSample);

        /* The pass schedule replicates the CPU filter exactly: horizontal passes first with
           rounding div-1, 0, div-1, ... then vertical ones restarting the pattern with their
           own divisor. Float formats have no rounding term, just the reciprocal divisor. */
        struct Pass { uint32_t radius, rounding, vertical; float invDiv; };
        std::vector<Pass> schedule;
        if (d->hradius > 0) {
            for (int i = 0; i < d->hpasses; i++)
                schedule.push_back({ static_cast<uint32_t>(d->hradius), !(i & 1) ? 2u * d->hradius : 0u, 0,
                    1.0f / (2 * d->hradius + 1) });
        }
        if (d->vradius > 0) {
            for (int i = 0; i < d->vpasses; i++)
                schedule.push_back({ static_cast<uint32_t>(d->vradius), !(i & 1) ? 2u * d->vradius : 0u, 1,
                    1.0f / (2 * d->vradius + 1) });
        }

        VkBuffer srcBuf = srcPlane.buffer;
        const int passes = static_cast<int>(schedule.size());
        for (int i = 0; i < passes; i++) {
            /* Alternate so the final pass always lands in the destination plane. */
            VkBuffer target = ((passes - 1 - i) % 2 == 0) ? dstPlane.buffer : tmpBuffer;
            if (!firstDispatch)
                computeToComputeBarrier(d->vk, cmd);
            firstDispatch = false;

            VkDescriptorBufferInfo bufferInfo[2] = {};
            VkWriteDescriptorSet writes[2] = {};
            bufferInfo[0].buffer = srcBuf;
            bufferInfo[0].range = VK_WHOLE_SIZE;
            bufferInfo[1].buffer = target;
            bufferInfo[1].range = VK_WHOLE_SIZE;
            for (int b = 0; b < 2; b++) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstBinding = static_cast<uint32_t>(b);
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].pBufferInfo = &bufferInfo[b];
            }
            d->vk->vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeLayout, 0, 2, writes);

            BlurPush push = {};
            push.width = planeWidth;
            push.height = planeHeight;
            push.srcStride = strideElems;
            push.dstStride = strideElems;
            push.radius = schedule[i].radius;
            push.rounding = schedule[i].rounding;
            push.vertical = schedule[i].vertical;
            push.invDiv = schedule[i].invDiv;
            VkPushConstantsInfo pushInfo = {};
            pushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
            pushInfo.layout = d->pipeLayout;
            pushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushInfo.size = sizeof(push);
            pushInfo.pValues = &push;
            d->vk->vkCmdPushConstants2(cmd, &pushInfo);

            d->vk->vkCmdDispatch(cmd, (planeWidth + 15) / 16, (planeHeight + 15) / 16, 1);
            srcBuf = target;
        }
    }

    d->vk->vkEndCommandBuffer(cmd);

    /* Value allocation and submission stay together under the queue lock, since timeline
       signal values must reach the queue in increasing order. */
    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo signal = {};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = d->timeline;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = waitCount;
    submit.pWaitSemaphoreInfos = waitCount ? waits : nullptr;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;

    uint64_t value;
    VkResult res;
    d->vkapi->lockVulkanQueue(core, vqCompute);
    value = d->nextValue + 1;
    signal.value = value;
    res = d->vk->vkQueueSubmit2(d->computeQueue, 1, &submit, VK_NULL_HANDLE);
    if (res == VK_SUCCESS)
        d->nextValue = value;
    d->vkapi->unlockVulkanQueue(core, vqCompute);

    if (res != VK_SUCCESS) {
        vsapi->setFilterError("GPUBoxBlur: vkQueueSubmit2 failed", frameCtx);
        if (tmp)
            d->vkapi->destroyGPUBuffer(tmp);
        vsapi->freeFrame(src);
        vsapi->freeFrame(dst);
        return nullptr;
    }

    d->slotValue[slot] = value;
    /* The source frame and the scratch buffer must outlive the submission reading them. */
    d->retained.push_back({ src, nullptr, value });
    if (tmp)
        d->retained.push_back({ nullptr, tmp, value });

    /* Only the planes this submission wrote get the new producer pair; shared planes keep
       the one they arrived with, which consumers wait on independently. */
    for (int p = 0; p < fmt->numPlanes; p++) {
        if (d->process[p])
            d->vkapi->setGPUPlaneProducer(dst, p, d->timeline, value);
    }

    return dst;
}

void VS_CC gpuBoxBlurFree(void *instanceData, VSCore *, const VSAPI *vsapi) {
    GPUBoxBlurData *d = static_cast<GPUBoxBlurData *>(instanceData);

    /* Everything below is destroyed while submissions may still reference it, so drain
       first; the timeline reaching the last value issued means the GPU is done. */
    if (d->timeline && d->nextValue) {
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &d->timeline;
        waitInfo.pValues = &d->nextValue;
        d->vk->vkWaitSemaphores(d->h.device, &waitInfo, UINT64_MAX);
    }
    for (const auto &r : d->retained) {
        if (r.frame)
            vsapi->freeFrame(r.frame);
        if (r.buffer)
            d->vkapi->destroyGPUBuffer(r.buffer);
    }

    if (d->cmdPool)
        d->vk->vkDestroyCommandPool(d->h.device, d->cmdPool, nullptr);
    if (d->pipeline)
        d->vk->vkDestroyPipeline(d->h.device, d->pipeline, nullptr);
    if (d->pipeLayout)
        d->vk->vkDestroyPipelineLayout(d->h.device, d->pipeLayout, nullptr);
    if (d->setLayout)
        d->vk->vkDestroyDescriptorSetLayout(d->h.device, d->setLayout, nullptr);
    if (d->timeline)
        d->vk->vkDestroySemaphore(d->h.device, d->timeline, nullptr);

    if (d->node)
        vsapi->freeNode(d->node);
    delete d;
}

/* Builds the single GPU node. Arguments arrive already parsed and validated by the shared
   create below, so this only does the Vulkan half. Consumes the node reference on success
   and leaves it to the caller on failure, which is why the failure path clears d->node
   before the free callback runs: the Vulkan objects built so far must go, the caller's
   reference must not. */
VSNode *createGPUBoxBlur(VSNode *node, const bool process[3], int hradius, int hpasses, int vradius, int vpasses,
    VSCore *core, const VSAPI *vsapi) {
    auto d = std::make_unique<GPUBoxBlurData>();
    char err[512] = { 0 };

    try {
    d->node = node;
    d->vi = *vsapi->getVideoInfo(node);
    d->core = core;
    for (int p = 0; p < 3; p++)
        d->process[p] = process[p];
    d->hradius = hradius;
    d->hpasses = hpasses;
    d->vradius = vradius;
    d->vpasses = vpasses;
    if (!((d->hradius > 0) && (d->hpasses > 0)))
        d->hradius = 0;
    if (!((d->vradius > 0) && (d->vpasses > 0)))
        d->vradius = 0;

    d->vkapi = vsapi->getVulkanAPI(VSVULKAN_API_VERSION);
    if (!d->vkapi)
        throw std::runtime_error("the GPU API is not available");
    if (d->vkapi->getVulkanHandles(core, &d->h, err, sizeof(err)))
        throw std::runtime_error(err);
    d->vk = d->vkapi->getVulkanFunctions(core, err, sizeof(err));
    if (!d->vk)
        throw std::runtime_error(err);

    VkDeviceQueueInfo2 queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queueInfo.queueFamilyIndex = d->h.computeQueueFamily;
    queueInfo.queueIndex = d->h.computeQueueIndex;
    d->vk->vkGetDeviceQueue2(d->h.device, &queueInfo, &d->computeQueue);

    VSVulkanCoreInfo coreInfo = {};
    if (d->vkapi->getVulkanCoreInfo(core, &coreInfo, err, sizeof(err)))
        throw std::runtime_error(err);

    std::string preamble = "#version 460\n";
    if (d->vi.format.sampleType == stInteger) {
        preamble += d->vi.format.bytesPerSample == 1 ? "#define SAMPLE_T uint8_t\n" : "#define SAMPLE_T uint16_t\n";
    } else if (d->vi.format.bytesPerSample == 4) {
        preamble += "#define SAMPLE_T float\n#define FLOAT_SAMPLES\n";
    } else {
        preamble += "#define SAMPLE_T float16_t\n#define FLOAT_SAMPLES\n";
    }

    VSGPUShader *shader = d->vkapi->compileGPUShader(core, slGLSL, (preamble + boxBlurGlsl).c_str(), err, sizeof(err));
    if (!shader) {
        /* A half precision kernel is the one variant a conformant device may refuse. */
        if (d->vi.format.sampleType == stFloat && d->vi.format.bytesPerSample == 2)
            throw std::runtime_error("half precision formats need the shaderFloat16 feature, which this device lacks");
        throw std::runtime_error(std::string("kernel failed to compile: ") + err);
    }
    size_t shaderBytes = 0;
    const uint32_t *shaderCode = d->vkapi->getGPUShaderCode(shader, &shaderBytes);

    VkDescriptorSetLayoutBinding bindings[2] = {};
    for (int b = 0; b < 2; b++) {
        bindings[b].binding = static_cast<uint32_t>(b);
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    setInfo.bindingCount = 2;
    setInfo.pBindings = bindings;
    if (d->vk->vkCreateDescriptorSetLayout(d->h.device, &setInfo, nullptr, &d->setLayout) != VK_SUCCESS) {
        d->vkapi->freeGPUShader(shader);
        throw std::runtime_error("descriptor set layout creation failed");
    }

    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = sizeof(BlurPush);
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &d->setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (d->vk->vkCreatePipelineLayout(d->h.device, &layoutInfo, nullptr, &d->pipeLayout) != VK_SUCCESS) {
        d->vkapi->freeGPUShader(shader);
        throw std::runtime_error("pipeline layout creation failed");
    }

    /* maintenance5 is in the required feature set, so the SPIR-V rides along in the stage's
       pNext and no shader module object is needed. */
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = shaderBytes;
    moduleInfo.pCode = shaderCode;
    VkComputePipelineCreateInfo pipeInfo = {};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.pNext = &moduleInfo;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = d->pipeLayout;
    VkResult pipeRes = d->vk->vkCreateComputePipelines(d->h.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &d->pipeline);
    d->vkapi->freeGPUShader(shader); /* the pipeline owns the code now */
    if (pipeRes != VK_SUCCESS)
        throw std::runtime_error("compute pipeline creation failed");

    /* Created exportable when the device can, so foreign APIs consuming this filter's frames
       can import the producer pair instead of host waiting. */
    VkExportSemaphoreCreateInfo semExport = {};
    semExport.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    semExport.handleTypes = static_cast<VkExternalSemaphoreHandleTypeFlags>(coreInfo.semaphoreExportHandleType);
    VkSemaphoreTypeCreateInfo semType = {};
    semType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semType.pNext = coreInfo.semaphoreExportHandleType ? &semExport : nullptr;
    semType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &semType;
    if (d->vk->vkCreateSemaphore(d->h.device, &semInfo, nullptr, &d->timeline) != VK_SUCCESS)
        throw std::runtime_error("timeline semaphore creation failed");

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = d->h.computeQueueFamily;
    if (d->vk->vkCreateCommandPool(d->h.device, &poolInfo, nullptr, &d->cmdPool) != VK_SUCCESS)
        throw std::runtime_error("command pool creation failed");
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = d->cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = cmdSlots;
    if (d->vk->vkAllocateCommandBuffers(d->h.device, &allocInfo, d->cmd) != VK_SUCCESS)
        throw std::runtime_error("command buffer allocation failed");

    VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
    VSNode *result = vsapi->createVideoFilterEx2("BoxBlur", &d->vi, gpuBoxBlurGetFrame, gpuBoxBlurFree,
        fmParallel, ffGPUOutput, deps, 1, d.get(), core);
    if (!result)
        throw std::runtime_error("filter creation failed");
    d.release();
    return result;
    } catch (...) {
        d->node = nullptr; /* still the caller's; everything else here is ours to unwind */
        gpuBoxBlurFree(d.release(), core, vsapi);
        throw;
    }
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
