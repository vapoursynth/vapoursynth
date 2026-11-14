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

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <bit>
#include <memory>
#include <limits>
#include <string>
#include <algorithm>
#include <type_traits>
#include "gpufilter.h"
#include "internalfilters.h"
#include "VSVulkan4.h"
#include "VSHelper4.h"
#include "filtershared.h"
#include "cpufeatures.h"
#include "kernel/cpulevel.h"

using namespace vsh;

//////////////////////////////////////////
// Lut

#ifdef VS_TARGET_CPU_X86
void vs_lut1_b_b_avx512vbmi(const uint8_t *src, uint8_t *dst, int w, const uint8_t *lut);
#endif
#ifdef VS_TARGET_CPU_ARM64
void vs_lut1_b_b_neon(const uint8_t *src, uint8_t *dst, int w, const uint8_t *lut);
#endif

namespace {

typedef struct LutDataExtra {
    VSVideoInfo vi_out;
    const VSVideoInfo *vi;
    void *lut;
    bool process[3];
    bool use_vbmi;
    bool use_neon;
    ~LutDataExtra() { free(lut); };
} LutDataExtra;

typedef SingleNodeData<LutDataExtra> LutData;

} // namespace

template<typename T, typename U>
static void lutRow(const T * VS_RESTRICT srcp, U * VS_RESTRICT dstp, int w, const U * VS_RESTRICT lut, [[maybe_unused]] const LutData *d) {
#ifdef VS_TARGET_CPU_X86
    if constexpr (std::is_same_v<T, uint8_t> && std::is_same_v<U, uint8_t>) {
        if (d->use_vbmi) {
            vs_lut1_b_b_avx512vbmi(srcp, dstp, w, lut);
            return;
        }
    }
#endif
#ifdef VS_TARGET_CPU_ARM64
    if constexpr (std::is_same_v<T, uint8_t> && std::is_same_v<U, uint8_t>) {
        if (d->use_neon) {
            vs_lut1_b_b_neon(srcp, dstp, w, lut);
            return;
        }
    }
#endif
    if constexpr (std::is_same_v<U, uint8_t>) {
        constexpr bool le = std::endian::native == std::endian::little;
        for (int x = 0; x < w; x += 8) {
            uint64_t p = ((uint64_t)lut[srcp[x + 0]] << (le ?  0 : 56))
                | ((uint64_t)lut[srcp[x + 1]] << (le ?  8 : 48))
                | ((uint64_t)lut[srcp[x + 2]] << (le ? 16 : 40))
                | ((uint64_t)lut[srcp[x + 3]] << (le ? 24 : 32))
                | ((uint64_t)lut[srcp[x + 4]] << (le ? 32 : 24))
                | ((uint64_t)lut[srcp[x + 5]] << (le ? 40 : 16))
                | ((uint64_t)lut[srcp[x + 6]] << (le ? 48 :  8))
                | ((uint64_t)lut[srcp[x + 7]] << (le ? 56 :  0));
            std::memcpy(dstp + x, &p, sizeof(p));
        }
    } else if constexpr (std::is_same_v<U, uint16_t>) {
        constexpr bool le = std::endian::native == std::endian::little;
        for (int x = 0; x < w; x += 4) {
            uint64_t p = ((uint64_t)lut[srcp[x + 0]] << (le ?  0 : 48))
                | ((uint64_t)lut[srcp[x + 1]] << (le ? 16 : 32))
                | ((uint64_t)lut[srcp[x + 2]] << (le ? 32 : 16))
                | ((uint64_t)lut[srcp[x + 3]] << (le ? 48 :  0));
            std::memcpy(dstp + x, &p, sizeof(p));
        }
    } else {
        for (int x = 0; x < w; x++)
            dstp[x] = lut[srcp[x]];
    }
}

template<typename T, typename U>
static void VS_CC lutProcess(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, [[maybe_unused]] void *frameState, void *instanceData) {
    const LutData *d = static_cast<const LutData *>(instanceData);
    const U *lut = reinterpret_cast<const U *>(d->lut);
    for (int y = region->rowStart; y < region->rowEnd; y++)
        lutRow<T, U>(reinterpret_cast<const T *>(tileRow(&src[0], y)), reinterpret_cast<U *>(tileRow(dst, y)), region->width, lut, d);
}

template<typename T>
static bool funcToLut(int nin, int nout, void *vlut, VSFunction *func, const VSAPI *vsapi, std::string &errstr) {
    VSMap *in = vsapi->createMap();
    VSMap *out = vsapi->createMap();

    T *lut = reinterpret_cast<T *>(vlut);

    for (int i = 0; i < nin; i++) {
        vsapi->mapSetInt(in, "x", i, maReplace);
        vsapi->callFunction(func, in, out);

        const char *ret = vsapi->mapGetError(out);
        if (ret) {
            errstr = ret;
            break;
        }

        int err;
        if (std::numeric_limits<T>::is_integer) {
            int64_t v = vsapi->mapGetInt(out, "val", 0, &err);
            vsapi->clearMap(out);

            if (v < 0 || v >= nout || err) {
                errstr = "Lut: function(" + std::to_string(i) + ") returned invalid value: " + std::to_string(v);
                break;
            }

            lut[i] = static_cast<T>(v);
        } else {
            double v = vsapi->mapGetFloat(out, "val", 0, &err);
            vsapi->clearMap(out);

            if (err) {
                errstr = "Lut: function(" + std::to_string(i) + ") returned invalid value: " + std::to_string(v);
                break;
            }

            lut[i] = static_cast<T>(v);
        }
    }

    vsapi->freeMap(in);
    vsapi->freeMap(out);

    return errstr.empty();
}

template<typename T, typename U>
static void lutCreateHelper(const VSMap *in, VSMap *out, VSFunction *func, std::unique_ptr<LutData> &d, VSCore *core, const VSAPI *vsapi) {
    int inrange = 1 << d->vi->format.bitsPerSample;
    int maxval = (d->vi_out.format.sampleType == stInteger) ? (1 << d->vi_out.format.bitsPerSample) : 0;
    /* Allocate the LUT for the full input container (256 or 65536 entries) rather
       than just the valid range, so out-of-range samples index a padded entry
       instead of needing a per-pixel clamp in the hot loop. */
    size_t container = static_cast<size_t>(1) << (8 * sizeof(T));

    d->lut = malloc(container * sizeof(U));

    if (func) {
        std::string errstr;
        funcToLut<U>(inrange, maxval, d->lut, func, vsapi, errstr);
        vsapi->freeFunction(func);

        if (!errstr.empty())
            RETERROR(errstr.c_str());
    } else {

        U *lut = reinterpret_cast<U *>(d->lut);

        if (std::numeric_limits<U>::is_integer) {
            const int64_t *arr = vsapi->mapGetIntArray(in, "lut", nullptr);

            for (int i = 0; i < inrange; i++) {
                int64_t v = arr[i];
                if (v < 0 || v >= maxval)
                    RETERROR(("Lut: lut value " + std::to_string(v) + " out of valid range [0," + std::to_string(maxval) + "]").c_str());
                lut[i] = static_cast<U>(v);
            }
        } else {
            const double *arr = vsapi->mapGetFloatArray(in, "lutf", nullptr);

            for (int i = 0; i < inrange; i++) {
                double v = arr[i];
                lut[i] = static_cast<U>(v);
            }
        }
    }

    /* Pad [inrange, container) with lut[maxIndex] so any out-of-range (garbage
       high-bit) sample maps to the same value an explicit clamp would produce.
       No-op for full-width formats where inrange == container. */
    {
        U *lut = reinterpret_cast<U *>(d->lut);
        for (size_t i = inrange; i < container; i++)
            lut[i] = lut[inrange - 1];
    }

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        /* The table is already padded out to the container width above, so an out of range
           sample indexes a defined entry and the kernel needs no clamp of its own. */
        vsgpu::SimpleFilter sf;
        sf.name = "Lut";
        sf.srcFormat = &d->vi->format;
        const std::string body = "    STORE(lut0[uint(SRC0(x, y))]);";
        sf.bodyInt = body;
        sf.bodyFloat = body;
        for (int i = 0; i < 3; i++)
            sf.process[i] = d->process[i];
        const uint8_t *table = reinterpret_cast<const uint8_t *>(d->lut);
        sf.constants.emplace_back(table, table + container * sizeof(U));

        VSNode *node = d->node;
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, &node, 1, &d->vi_out, core, vsapi, error);
        d->node = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("Lut: " + error).c_str());
        return;
    }

    /* Lut only takes clips of constant format and size, so on the CPU it is always tiled. */
    createTiledFilter(out, "Lut", &d->vi_out, &d->node, 1, unprocessedPlanes(d->process, d->vi_out.format.numPlanes),
        lutProcess<T, U>, nullptr, filterFree<LutData>, d.get(), core, vsapi);
    d.release();
}

static void VS_CC lutCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LutData> d(new LutData(vsapi));

    try {

        int err;

        d->node = vsapi->mapGetNode(in, "clip", 0, 0);
        d->vi = vsapi->getVideoInfo(d->node);

        if (!isConstantVideoFormat(d->vi))
            RETERROR("Lut: only clips with constant format and dimensions supported");

        if (d->vi->format.sampleType != stInteger || d->vi->format.bitsPerSample > 16)
            RETERROR("Lut: only clips with integer samples and up to 16 bits per channel precision supported");

        bool floatout = !!vsapi->mapGetInt(in, "floatout", 0, &err);
        int bitsout = vsapi->mapGetIntSaturated(in, "bits", 0, &err);
        if (err)
            bitsout = (floatout ? sizeof(float) * 8 : d->vi->format.bitsPerSample);
        if ((floatout && bitsout != 32) || (!floatout && (bitsout < 8 || bitsout > 16)))
            RETERROR("Lut: only 8-16 bit integer and 32 bit float output supported");

        d->vi_out = *d->vi;
        vsapi->queryVideoFormat(&d->vi_out.format, d->vi->format.colorFamily, floatout ? stFloat : stInteger, bitsout, d->vi->format.subSamplingW, d->vi->format.subSamplingH, core);

        getPlanesArg(in, d->process, vsapi);

        if (d->vi_out.format.bytesPerSample != d->vi->format.bytesPerSample || d->vi_out.format.sampleType != d->vi->format.sampleType) {
            for (int plane = 0; plane < d->vi->format.numPlanes; plane++)
                if (!d->process[plane])
                    RETERROR("Lut: every plane must be processed when bits or floatout changes the sample storage, since an unprocessed plane cannot be passed through unchanged");
        }

        VSFunction *func = vsapi->mapGetFunction(in, "function", 0, &err);
        int lut_elem = vsapi->mapNumElements(in, "lut");
        int lutf_elem = vsapi->mapNumElements(in, "lutf");

        int num_set = (lut_elem >= 0) + (lutf_elem >= 0) + !!func;

        if (!num_set) {
            vsapi->freeFunction(func);
            RETERROR("Lut: none of lut, lutf and function are set");
        }

        if (num_set > 1) {
            vsapi->freeFunction(func);
            RETERROR("Lut: more than one of lut, lutf and function are set");
        }

        if (lut_elem >= 0 && floatout) {
            vsapi->freeFunction(func);
            RETERROR("Lut: lut set but float output specified");
        }

        if (lutf_elem >= 0 && !floatout) {
            vsapi->freeFunction(func);
            RETERROR("Lut: lutf set but float output not specified");
        }

        int n = (1 << d->vi->format.bitsPerSample);

        int lut_length = std::max(lut_elem, lutf_elem);

        if (lut_length >= 0 && lut_length != n) {
            vsapi->freeFunction(func);
            RETERROR(("Lut: bad lut length. Expected " + std::to_string(n) + " elements, got " + std::to_string(lut_length) + " instead").c_str());
        }

        vsapi->queryVideoFormat(&d->vi_out.format, d->vi->format.colorFamily, floatout ? stFloat : stInteger, bitsout, d->vi->format.subSamplingW, d->vi->format.subSamplingH, core);

        d->use_vbmi = false;
        d->use_neon = false;
#ifdef VS_TARGET_CPU_X86
        if (getCPUFeatures()->avx512_vbmi && vs_get_cpulevel(core) >= VS_CPU_LEVEL_AVX512)
            d->use_vbmi = true;
#elif defined(VS_TARGET_CPU_ARM64)
        if (vs_get_cpulevel(core) >= VS_CPU_LEVEL_NEON)
            d->use_neon = true;
#endif

        if (d->vi->format.bytesPerSample == 1 && bitsout == 8)
            lutCreateHelper<uint8_t, uint8_t>(in, out, func, d, core, vsapi);
        else if (d->vi->format.bytesPerSample == 1 && bitsout > 8 && bitsout <= 16)
            lutCreateHelper<uint8_t, uint16_t>(in, out, func, d, core, vsapi);
        else if (d->vi->format.bytesPerSample == 1 && floatout)
            lutCreateHelper<uint8_t, float>(in, out, func, d, core, vsapi);
        else if (d->vi->format.bytesPerSample == 2 && bitsout == 8)
            lutCreateHelper<uint16_t, uint8_t>(in, out, func, d, core, vsapi);
        else if (d->vi->format.bytesPerSample == 2 && bitsout > 8 && bitsout <= 16)
            lutCreateHelper<uint16_t, uint16_t>(in, out, func, d, core, vsapi);
        else if (d->vi->format.bytesPerSample == 2 && floatout)
            lutCreateHelper<uint16_t, float>(in, out, func, d, core, vsapi);

    } catch (std::runtime_error &e) {
        RETERROR(("Lut " + std::string(e.what())).c_str());
    }
}

//////////////////////////////////////////
// Lut2

struct Lut2DataExtra {
    VSVideoInfo vi_out;
    const VSVideoInfo *vi[2];
    void *lut;
    bool process[3];
    int cpulevel;
    bool use_gather;
    ~Lut2DataExtra() { free(lut); };
};

typedef DualNodeData<Lut2DataExtra> Lut2Data;

#ifdef VS_TARGET_CPU_X86
void vs_lut2_gather_ww_w_avx512(const uint16_t *, const uint16_t *, uint16_t *, int, const uint16_t *, int, unsigned, unsigned);
void vs_lut2_gather_ww_b_avx512(const uint16_t *, const uint16_t *, uint8_t *, int, const uint8_t *, int, unsigned, unsigned);
void vs_lut2_gather_wb_w_avx512(const uint16_t *, const uint8_t *, uint16_t *, int, const uint16_t *, int, unsigned, unsigned);
void vs_lut2_gather_wb_b_avx512(const uint16_t *, const uint8_t *, uint8_t *, int, const uint8_t *, int, unsigned, unsigned);
void vs_lut2_gather_bw_w_avx512(const uint8_t *, const uint16_t *, uint16_t *, int, const uint16_t *, int, unsigned, unsigned);
void vs_lut2_gather_bw_b_avx512(const uint8_t *, const uint16_t *, uint8_t *, int, const uint8_t *, int, unsigned, unsigned);
#endif

/* Dispatch one row to the gather kernel for the current type combo.
   Returns false (compile-time) for combos without a kernel so the caller falls
   back to the scalar path; only ever called when d->use_gather is set. */
template<typename T, typename U, typename V>
static inline bool lut2GatherRow([[maybe_unused]] const T *sx, [[maybe_unused]] const U *sy, [[maybe_unused]] V *d, [[maybe_unused]] int w,
    [[maybe_unused]] const V *lut, [[maybe_unused]] int bitsx, [[maybe_unused]] unsigned mx, [[maybe_unused]] unsigned my) {
#ifdef VS_TARGET_CPU_X86
    if constexpr (std::is_integral_v<V>) {
        if constexpr (std::is_same_v<T, uint16_t> && std::is_same_v<U, uint16_t>) {
            if constexpr (sizeof(V) == 2) vs_lut2_gather_ww_w_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            else vs_lut2_gather_ww_b_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            return true;
        } else if constexpr (std::is_same_v<T, uint16_t> && std::is_same_v<U, uint8_t>) {
            if constexpr (sizeof(V) == 2) vs_lut2_gather_wb_w_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            else vs_lut2_gather_wb_b_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            return true;
        } else if constexpr (std::is_same_v<T, uint8_t> && std::is_same_v<U, uint16_t>) {
            if constexpr (sizeof(V) == 2) vs_lut2_gather_bw_w_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            else vs_lut2_gather_bw_b_avx512(sx, sy, d, w, lut, bitsx, mx, my);
            return true;
        }
    }
#endif
    return false;
}

template<typename T, typename U, typename V>
static void lut2Row(const T * VS_RESTRICT srcpx, const U * VS_RESTRICT srcpy, V * VS_RESTRICT dstp, int w, const V * VS_RESTRICT lut, int shift, T maxvalx, U maxvaly, bool use_gather) {
    if (!(use_gather && lut2GatherRow<T, U, V>(srcpx, srcpy, dstp, w, lut, shift, maxvalx, maxvaly))) {
        for (int x = 0; x < w; x++)
            dstp[x] =  lut[(std::min(srcpy[x], maxvaly) << shift) + std::min(srcpx[x], maxvalx)];
    }
}

template<typename T, typename U, typename V>
static void VS_CC lut2Process(const VSTileRegion *region, const VSTileBuffer *src, const VSTileBuffer *dst, [[maybe_unused]] void *frameState, void *instanceData) {
    const Lut2Data *d = static_cast<const Lut2Data *>(instanceData);
    const int shift = d->vi[0]->format.bitsPerSample;
    const T maxvalx = static_cast<T>((static_cast<int64_t>(1) << shift) - 1);
    const U maxvaly = static_cast<U>((static_cast<int64_t>(1) << d->vi[1]->format.bitsPerSample) - 1);
    const V *lut = reinterpret_cast<const V *>(d->lut);
    for (int y = region->rowStart; y < region->rowEnd; y++)
        lut2Row<T, U, V>(reinterpret_cast<const T *>(tileRow(&src[0], y)), reinterpret_cast<const U *>(tileRow(&src[1], y)), reinterpret_cast<V *>(tileRow(dst, y)), region->width, lut, shift, maxvalx, maxvaly, d->use_gather);
}

template<typename T>
static bool funcToLut2(int nxin, int nyin, int nout, void *vlut, VSFunction *func, const VSAPI *vsapi, std::string &errstr) {
    VSMap *in = vsapi->createMap();
    VSMap *out = vsapi->createMap();

    T *lut = reinterpret_cast<T *>(vlut);

    for (int i = 0; i < nyin; i++) {
        vsapi->mapSetInt(in, "y", i, maReplace);
        for (int j = 0; j < nxin; j++) {
            vsapi->mapSetInt(in, "x", j, maReplace);
            vsapi->callFunction(func, in, out);

            const char *ret = vsapi->mapGetError(out);
            if (ret) {
                errstr = "Lut2: function(" + std::to_string(j) + ", " + std::to_string(i) + ") returned an error: ";
                errstr += ret;
                break;
            }

            int err;
            if (std::numeric_limits<T>::is_integer) {
                int64_t v = vsapi->mapGetInt(out, "val", 0, &err);
                vsapi->clearMap(out);

                if (v < 0 || v >= nout || err) {
                    if (err)
                        errstr = "Lut2: function(" + std::to_string(j) + ", " + std::to_string(i) + ") didn't return an integer value";
                    else
                        errstr = "Lut2: function(" + std::to_string(j) + ", " + std::to_string(i) + ") returned invalid value: " + std::to_string(v) + ", max allowed: " + std::to_string(nout);

                    break;
                }

                lut[j + i * nxin] = static_cast<T>(v);
            } else {
                double v = vsapi->mapGetFloat(out, "val", 0, &err);
                vsapi->clearMap(out);

                if (err) {
                    errstr = "Lut2: function(" + std::to_string(j) + ", " + std::to_string(i) + ") didn't return a float value";
                    break;
                }

                lut[j + i * nxin] = static_cast<T>(v);
            }
        }
        if (!errstr.empty())
            break;
    }

    vsapi->freeMap(in);
    vsapi->freeMap(out);
    return errstr.empty();
}

template<typename T, typename U, typename V>
static void lut2CreateHelper(const VSMap *in, VSMap *out, VSFunction *func, std::unique_ptr<Lut2Data> &d, VSCore *core, const VSAPI *vsapi) {
    int inrange = (1 << d->vi[0]->format.bitsPerSample) * (1 << d->vi[1]->format.bitsPerSample);
    int maxval = (d->vi_out.format.sampleType == stInteger) ? (1 << d->vi_out.format.bitsPerSample) : 0;

    /* +64 bytes so the AVX-512 gather's dword-granularity overread past the last
       entry stays in bounds (see lut2GatherRow). */
    d->lut = malloc((size_t)inrange * sizeof(V) + 64);

    if (func) {
        std::string errstr;
        funcToLut2<V>(1 << d->vi[0]->format.bitsPerSample, 1 << d->vi[1]->format.bitsPerSample, maxval, d->lut, func, vsapi, errstr);
        vsapi->freeFunction(func);

        if (!errstr.empty())
            RETERROR(errstr.c_str());
    } else {

        V *lut = reinterpret_cast<V *>(d->lut);

        if (std::numeric_limits<V>::is_integer) {
            const int64_t *arr = vsapi->mapGetIntArray(in, "lut", nullptr);

            for (int i = 0; i < inrange; i++) {
                int64_t v = arr[i];
                if (v < 0 || v >= maxval)
                    RETERROR(("Lut2: lut value " + std::to_string(v) + " out of valid range [0," + std::to_string(maxval) + "]").c_str());
                lut[i] = static_cast<V>(v);
            }
        } else {
            const double *arr = vsapi->mapGetFloatArray(in, "lutf", nullptr);

            for (int i = 0; i < inrange; i++) {
                double v = arr[i];
                lut[i] = static_cast<V>(v);
            }
        }
    }

    /* Use the AVX-512 gather only where it wins: integer output and a LUT large
       enough to spill L2 (>= ~512 KB), on CPUs with AVX-512 (a safe fast-gather
       gate). Smaller tables and float output stay on the scalar path. */
    d->use_gather = false;
#ifdef VS_TARGET_CPU_X86
    if constexpr (std::is_integral_v<V>) {
        if (getCPUFeatures()->avx512 && d->cpulevel >= VS_CPU_LEVEL_AVX512 && (size_t)inrange * sizeof(V) >= (512u << 10))
            d->use_gather = true;
    }
#endif

    const ClipResidencyResult residency = residencyOfClips(d->node1, d->node2, vsapi);
    if (residency.kind == ClipResidency::Mixed)
        RETERROR(residencyMismatchError("Lut2", residency.mixedAt).c_str());

    if (residency.kind == ClipResidency::AllGPU) {
        /* Indexed by both samples at once, y in the high bits, exactly how the table was
           laid out on the host. */
        vsgpu::SimpleFilter sf;
        sf.name = "Lut2";
        sf.numInputs = 2;
        /* clipa and clipb may differ in depth and container width, so each input is
           declared with its own sample type rather than sharing clipa's. */
        sf.srcFormats[0] = &d->vi[0]->format;
        sf.srcFormats[1] = &d->vi[1]->format;
        /* The table is only inrange entries, with no padding out to the container widths, so
           a sample carrying more than its declared depth has to be clamped here exactly as
           the scalar path clamps it -- otherwise the index runs off the end of the buffer. */
        const std::string body =
            "    uint idx = (min(uint(SRC1(x, y)), pc.u[2]) << pc.u[0]) | min(uint(SRC0(x, y)), pc.u[1]);\n"
            "    STORE(lut0[idx]);";
        sf.bodyInt = body;
        sf.bodyFloat = body;
        for (int i = 0; i < 3; i++)
            sf.process[i] = d->process[i];
        const uint32_t shiftBits = d->vi[0]->format.bitsPerSample;
        const uint32_t maxvalx = (1u << d->vi[0]->format.bitsPerSample) - 1u;
        const uint32_t maxvaly = (1u << d->vi[1]->format.bitsPerSample) - 1u;
        sf.fillParams = [shiftBits, maxvalx, maxvaly](int, const uint32_t *, float *, uint32_t *u) {
            u[0] = shiftBits;
            u[1] = maxvalx;
            u[2] = maxvaly;
        };
        const uint8_t *table = reinterpret_cast<const uint8_t *>(d->lut);
        sf.constants.emplace_back(table, table + static_cast<size_t>(inrange) * sizeof(V));

        VSNode *nodes[2] = { d->node1, d->node2 };
        std::string error;
        VSNode *result = vsgpu::createSimpleFilter(sf, nodes, 2, &d->vi_out, core, vsapi, error);
        d->node1 = d->node2 = nullptr;
        if (result)
            vsapi->mapConsumeNode(out, "clip", result, maAppend);
        else
            vsapi->mapSetError(out, ("Lut2: " + error).c_str());
        return;
    }

    /* Lut2 only takes clips of constant format and size, and clipb has every plane that is
       processed, so on the CPU it is always tiled. */
    VSNode *inputs[] = { d->node1, d->node2 };
    createTiledFilter(out, "Lut2", &d->vi_out, inputs, 2, unprocessedPlanes(d->process, d->vi_out.format.numPlanes),
        lut2Process<T, U, V>, nullptr, filterFree<Lut2Data>, d.get(), core, vsapi);
    d.release();
}

static void VS_CC lut2Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<Lut2Data> d(new Lut2Data(vsapi));

    try {
        d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
        d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
        d->vi[0] = vsapi->getVideoInfo(d->node1);
        d->vi[1] = vsapi->getVideoInfo(d->node2);
        d->cpulevel = vs_get_cpulevel(core);

        if (!isConstantVideoFormat(d->vi[0]) || !isConstantVideoFormat(d->vi[1]))
            RETERROR("Lut2: only clips with constant format and dimensions supported");

        if (d->vi[0]->format.sampleType != stInteger || d->vi[1]->format.sampleType != stInteger
            || (d->vi[0]->format.bitsPerSample + d->vi[1]->format.bitsPerSample) > 20
            || d->vi[0]->format.subSamplingH != d->vi[1]->format.subSamplingH
            || d->vi[0]->format.subSamplingW != d->vi[1]->format.subSamplingW
            || d->vi[0]->width != d->vi[1]->width || d->vi[0]->height != d->vi[1]->height)
            RETERROR("Lut2: only clips with integer samples, same dimensions, same subsampling and up to a total of 20 indexing bits supported");

        int err;
        bool floatout = !!vsapi->mapGetInt(in, "floatout", 0, &err);
        int bitsout = vsapi->mapGetIntSaturated(in, "bits", 0, &err);
        if (err)
            bitsout = (floatout ? sizeof(float) * 8 : d->vi[0]->format.bitsPerSample);
        if ((floatout && bitsout != 32) || (!floatout && (bitsout < 8 || bitsout > 16)))
            RETERROR("Lut2: only 8-16 bit integer and 32 bit float output supported");

        d->vi_out = *d->vi[0];
        vsapi->queryVideoFormat(&d->vi_out.format, d->vi[0]->format.colorFamily, floatout ? stFloat : stInteger, bitsout, d->vi[0]->format.subSamplingW, d->vi[0]->format.subSamplingH, core);

        getPlanesArg(in, d->process, vsapi);

        for (int plane = 0; plane < d->vi[0]->format.numPlanes; plane++) {
            if (d->process[plane] && plane >= d->vi[1]->format.numPlanes)
                RETERROR("Lut2: clipb has fewer planes than clipa, only planes present in both clips can be processed");
        }

        /* As in Lut: a plane the caller excluded is passed through by sharing clipa's
           allocation, which only describes the same bytes while the storage is unchanged. */
        if (d->vi_out.format.bytesPerSample != d->vi[0]->format.bytesPerSample || d->vi_out.format.sampleType != d->vi[0]->format.sampleType) {
            for (int plane = 0; plane < d->vi[0]->format.numPlanes; plane++)
                if (!d->process[plane])
                    RETERROR("Lut2: every plane must be processed when bits or floatout changes the sample storage, since an unprocessed plane cannot be passed through unchanged");
        }

        VSFunction *func = vsapi->mapGetFunction(in, "function", 0, &err);
        int lut_elem = vsapi->mapNumElements(in, "lut");
        int lutf_elem = vsapi->mapNumElements(in, "lutf");

        int num_set = (lut_elem >= 0) + (lutf_elem >= 0) + !!func;

        if (!num_set) {
            vsapi->freeFunction(func);
            RETERROR("Lut2: none of lut, lutf and function are set");
        }

        if (num_set > 1) {
            vsapi->freeFunction(func);
            RETERROR("Lut2: more than one of lut, lutf and function are set");
        }

        if (lut_elem >= 0 && floatout) {
            vsapi->freeFunction(func);
            RETERROR("Lut2: lut set but float output specified");
        }

        if (lutf_elem >= 0 && !floatout) {
            vsapi->freeFunction(func);
            RETERROR("Lut2: lutf set but float output not specified");
        }

        int n = 1 << (d->vi[0]->format.bitsPerSample + d->vi[1]->format.bitsPerSample);

        int lut_length = std::max(lut_elem, lutf_elem);

        if (lut_length >= 0 && lut_length != n) {
            vsapi->freeFunction(func);
            RETERROR(("Lut2: bad lut length. Expected " + std::to_string(n) + " elements, got " + std::to_string(lut_length) + " instead").c_str());
        }

        if (d->vi[0]->format.bytesPerSample == 1) {
            if (d->vi[1]->format.bytesPerSample == 1) {
                if (d->vi_out.format.bytesPerSample == 1 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint8_t, uint8_t, uint8_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bytesPerSample == 2 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint8_t, uint8_t, uint16_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bitsPerSample == 32 && d->vi_out.format.sampleType == stFloat)
                    lut2CreateHelper<uint8_t, uint8_t, float>(in, out, func, d, core, vsapi);
            } else if (d->vi[1]->format.bytesPerSample == 2) {
                if (d->vi_out.format.bytesPerSample == 1 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint8_t, uint16_t, uint8_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bytesPerSample == 2 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint8_t, uint16_t, uint16_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bitsPerSample == 32 && d->vi_out.format.sampleType == stFloat)
                    lut2CreateHelper<uint8_t, uint16_t, float>(in, out, func, d, core, vsapi);
            }
        } else if (d->vi[0]->format.bytesPerSample == 2) {
            if (d->vi[1]->format.bytesPerSample == 1) {
                if (d->vi_out.format.bytesPerSample == 1 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint16_t, uint8_t, uint8_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bytesPerSample == 2 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint16_t, uint8_t, uint16_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bitsPerSample == 32 && d->vi_out.format.sampleType == stFloat)
                    lut2CreateHelper<uint16_t, uint8_t, float>(in, out, func, d, core, vsapi);
            } else if (d->vi[1]->format.bytesPerSample == 2) {
                if (d->vi_out.format.bytesPerSample == 1 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint16_t, uint16_t, uint8_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bytesPerSample == 2 && d->vi_out.format.sampleType == stInteger)
                    lut2CreateHelper<uint16_t, uint16_t, uint16_t>(in, out, func, d, core, vsapi);
                else if (d->vi_out.format.bitsPerSample == 32 && d->vi_out.format.sampleType == stFloat)
                    lut2CreateHelper<uint16_t, uint16_t, float>(in, out, func, d, core, vsapi);
            }
        }

    } catch (std::runtime_error &e) {
        RETERROR(("Lut2 " + std::string(e.what())).c_str());
    }
}

//////////////////////////////////////////
// Init

void lutInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Lut", "clip:vnode:all;planes:int[]:opt;lut:int[]:opt;lutf:float[]:opt;function:func:opt;bits:int:opt;floatout:int:opt;", "clip:vnode:all;", lutCreate, 0, plugin);
    vspapi->registerFunction("Lut2", "clipa:vnode:all;clipb:vnode:all;planes:int[]:opt;lut:int[]:opt;lutf:float[]:opt;function:func:opt;bits:int:opt;floatout:int:opt;", "clip:vnode:all;", lut2Create, 0, plugin);
}
