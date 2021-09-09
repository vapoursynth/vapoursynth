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
#include <cinttypes>
#include <memory>
#include <limits>
#include <string>
#include <algorithm>
#include "internalfilters.h"
#include "VSHelper4.h"
#include "filtershared.h"

using namespace vsh;

//////////////////////////////////////////
// Lut

namespace {

typedef struct LutDataExtra {
    VSVideoInfo vi_out;
    const VSVideoInfo *vi;
    void *lut;
    bool process[3];
    ~LutDataExtra() { free(lut); };
} LutDataExtra;

typedef SingleNodeData<LutDataExtra> LutData;

} // namespace

template<typename T, typename U>
static const VSFrame *VS_CC lutGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LutData *d = reinterpret_cast<LutData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat &fi = d->vi_out.format;
        const int pl[] = {0, 1, 2};
        const VSFrame *fr[] = {d->process[0] ? 0 : src, d->process[1] ? 0 : src, d->process[2] ? 0 : src};
        VSFrame *dst = vsapi->newVideoFrame2(&fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);

        T maxval = static_cast<T>((static_cast<int64_t>(1) << fi.bitsPerSample) - 1);

        for (int plane = 0; plane < fi.numPlanes; plane++) {

            if (d->process[plane]) {
                const T * VS_RESTRICT srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                U * VS_RESTRICT dstp = reinterpret_cast<U *>(vsapi->getWritePtr(dst, plane));
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(src, plane);
                int w = vsapi->getFrameWidth(src, plane);

                const U * VS_RESTRICT lut = reinterpret_cast<const U *>(d->lut);

                for (int hl = 0; hl < h; hl++) {
                    for (int x = 0; x < w; x++)
                        dstp[x] =  lut[std::min(srcp[x], maxval)];

                    dstp += dst_stride / sizeof(U);
                    srcp += src_stride / sizeof(T);
                }
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
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
    int maxval = 1 << d->vi_out.format.bitsPerSample;

    d->lut = malloc(inrange * sizeof(U));

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

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "Lut", &d->vi_out, lutGetframe<T, U>, filterFree<LutData>, fmParallel, deps, 1, d.get(), core);
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
    ~Lut2DataExtra() { free(lut); };
};

typedef DualNodeData<Lut2DataExtra> Lut2Data;

template<typename T, typename U, typename V>
static const VSFrame *VS_CC lut2Getframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    Lut2Data *d = reinterpret_cast<Lut2Data *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *srcx = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame *srcy = vsapi->getFrameFilter(n, d->node2, frameCtx);
        const VSVideoFormat &fi = d->vi_out.format;
        const int pl[] = {0, 1, 2};
        const VSFrame *fr[] = {d->process[0] ? 0 : srcx, d->process[1] ? 0 : srcx, d->process[2] ? 0 : srcx};
        VSFrame *dst = vsapi->newVideoFrame2(&fi, vsapi->getFrameWidth(srcx, 0), vsapi->getFrameHeight(srcx, 0), fr, pl, srcx, core);

        int shift = vsapi->getVideoFrameFormat(srcx)->bitsPerSample;
        T maxvalx = static_cast<T>((static_cast<int64_t>(1) << shift) - 1);
        U maxvaly = static_cast<U>((static_cast<int64_t>(1) << vsapi->getVideoFrameFormat(srcy)->bitsPerSample) - 1);

        for (int plane = 0; plane < fi.numPlanes; plane++) {

            if (d->process[plane]) {
                const T * VS_RESTRICT srcpx = reinterpret_cast<const T *>(vsapi->getReadPtr(srcx, plane));
                const U * VS_RESTRICT srcpy = reinterpret_cast<const U *>(vsapi->getReadPtr(srcy, plane));
                ptrdiff_t srcx_stride = vsapi->getStride(srcx, plane);
                ptrdiff_t srcy_stride = vsapi->getStride(srcy, plane);
                V * VS_RESTRICT dstp = reinterpret_cast<V *>(vsapi->getWritePtr(dst, plane));
                const V * VS_RESTRICT lut = reinterpret_cast<const V *>(d->lut);
                ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(srcx, plane);
                int w = vsapi->getFrameWidth(srcx, plane);

                for (int hl = 0; hl < h; hl++) {
                    for (int x = 0; x < w; x++)
                        dstp[x] =  lut[(std::min(srcpy[x], maxvaly) << shift) + std::min(srcpx[x], maxvalx)];
                    srcpx += srcx_stride / sizeof(T);
                    srcpy += srcy_stride / sizeof(U);
                    dstp += dst_stride / sizeof(V);
                }
            }
        }

        vsapi->freeFrame(srcx);
        vsapi->freeFrame(srcy);
        return dst;
    }

    return nullptr;
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
    }

    vsapi->freeMap(in);
    vsapi->freeMap(out);
    return errstr.empty();
}

template<typename T, typename U, typename V>
static void lut2CreateHelper(const VSMap *in, VSMap *out, VSFunction *func, std::unique_ptr<Lut2Data> &d, VSCore *core, const VSAPI *vsapi) {
    int inrange = (1 << d->vi[0]->format.bitsPerSample) * (1 << d->vi[1]->format.bitsPerSample);
    int maxval = 1 << d->vi_out.format.bitsPerSample;

    d->lut = malloc(inrange * sizeof(V));

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

    VSFilterDependency deps[] = {{ d->node1, rpStrictSpatial }, { d->node2, (d->vi[0]->numFrames <= d->vi[1]->numFrames) ? rpStrictSpatial : rpGeneral }};
    vsapi->createVideoFilter(out, "Lut2", &d->vi_out, lut2Getframe<T, U, V>, filterFree<Lut2Data>, fmParallel, deps, 2, d.get(), core);
    d.release();
}

static void VS_CC lut2Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<Lut2Data> d(new Lut2Data(vsapi));

    try {
        d->node1 = vsapi->mapGetNode(in, "clipa", 0, 0);
        d->node2 = vsapi->mapGetNode(in, "clipb", 0, 0);
        d->vi[0] = vsapi->getVideoInfo(d->node1);
        d->vi[1] = vsapi->getVideoInfo(d->node2);

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
    vspapi->registerFunction("Lut", "clip:vnode;planes:int[]:opt;lut:int[]:opt;lutf:float[]:opt;function:func:opt;bits:int:opt;floatout:int:opt;", "clip:vnode;", lutCreate, 0, plugin);
    vspapi->registerFunction("Lut2", "clipa:vnode;clipb:vnode;planes:int[]:opt;lut:int[]:opt;lutf:float[]:opt;function:func:opt;bits:int:opt;floatout:int:opt;", "clip:vnode;", lut2Create, 0, plugin);
}
