/*
VapourSynth adaption by Fredrik Mellbin

Copyright(c) 2013 Victor Efimov

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files(the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions :

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#include <limits>
#include "shared.h"

#define CLENSE_RETERROR(x) do { vsapi->mapSetError(out, (x)); vsapi->freeNode(d.cnode); vsapi->freeNode(d.pnode); vsapi->freeNode(d.nnode); return; } while (0)
#define CLAMP(value, lower, upper) do { if (value < lower) value = lower; else if (value > upper) value = upper; } while(0)

typedef struct {
    VSNode *cnode;
    VSNode *pnode;
    VSNode *nnode;
    const VSVideoInfo *vi;
    int mode;
    int process[3];
} ClenseData;

struct PlaneProc {
    template<typename T>
    static void clenseProcessPlane(T* VS_RESTRICT pDst, const T* VS_RESTRICT pSrc, const T* VS_RESTRICT pRef1, const T* VS_RESTRICT pRef2, ptrdiff_t stride, int width, int height) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x)
                pDst[x] = std::min(std::max(pSrc[x], std::min(pRef1[x], pRef2[x])), std::max(pRef1[x], pRef2[x]));
            pDst += stride;
            pSrc += stride;
            pRef1 += stride;
            pRef2 += stride;
        }
    }
};

struct PlaneProcFB {
    template<typename T>
    static void clenseProcessPlane(T* VS_RESTRICT pDst, const T* VS_RESTRICT pSrc, const T* VS_RESTRICT pRef1, const T* VS_RESTRICT pRef2, ptrdiff_t stride, int width, int height) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                T minref = std::min(pRef1[x], pRef2[x]);
                T maxref = std::max(pRef1[x], pRef2[x]);
                int lowref = minref * 2 - pRef2[x];
                int upref = maxref * 2 - pRef2[x];
                T src = pSrc[x];
                CLAMP(src, std::max<int>(lowref, std::numeric_limits<T>::min()), std::min<int>(upref, std::numeric_limits<T>::max()));
                pDst[x] = src;
            }

            pDst += stride;
            pSrc += stride;
            pRef1 += stride;
            pRef2 += stride;
        }
    }
};

template<typename T, typename Processor>
static const VSFrame *VS_CC clenseGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClenseData *d = static_cast<ClenseData *>(instanceData);

    if (activationReason == arInitial) {
        if (d->mode == cmNormal) {
            if (n >= 1 && (!d->vi->numFrames || n <= d->vi->numFrames - 2)) {
                *frameData = reinterpret_cast<void *>(1);
                vsapi->requestFrameFilter(n - 1, d->pnode, frameCtx);
                vsapi->requestFrameFilter(n, d->cnode, frameCtx);
                vsapi->requestFrameFilter(n + 1, d->nnode, frameCtx);
            } else {
                vsapi->requestFrameFilter(n, d->cnode, frameCtx);
            }
        } else if (d->mode == cmForward) {
            vsapi->requestFrameFilter(n, d->cnode, frameCtx);
            if (!d->vi->numFrames || n <= d->vi->numFrames - 3) {
                *frameData = reinterpret_cast<void *>(1);
                vsapi->requestFrameFilter(n + 1, d->cnode, frameCtx);
                vsapi->requestFrameFilter(n + 2, d->cnode, frameCtx);
            }
        } else if (d->mode == cmBackward) {
            if (n >= 2) {
                *frameData = reinterpret_cast<void *>(1);
                vsapi->requestFrameFilter(n - 2, d->cnode, frameCtx);
                vsapi->requestFrameFilter(n - 1, d->cnode, frameCtx);
            }
            vsapi->requestFrameFilter(n, d->cnode, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = nullptr, *frame1 = nullptr, *frame2 = nullptr;

        if (!*frameData) // skip processing on first/last frames
            return vsapi->getFrameFilter(n, d->cnode, frameCtx);

        if (d->mode == cmNormal) {
            frame1 = vsapi->getFrameFilter(n - 1, d->pnode, frameCtx);
            src = vsapi->getFrameFilter(n, d->cnode, frameCtx);
            frame2 = vsapi->getFrameFilter(n + 1, d->nnode, frameCtx);
        } else if (d->mode == cmForward) {
            src = vsapi->getFrameFilter(n, d->cnode, frameCtx);
            frame1 = vsapi->getFrameFilter(n + 1, d->cnode, frameCtx);
            frame2 = vsapi->getFrameFilter(n + 2, d->cnode, frameCtx);
        } else if (d->mode == cmBackward) {
            frame2 = vsapi->getFrameFilter(n - 2, d->cnode, frameCtx);
            frame1 = vsapi->getFrameFilter(n - 1, d->cnode, frameCtx);
            src = vsapi->getFrameFilter(n, d->cnode, frameCtx);
        }

        const int pl[] = { 0, 1, 2 };
        const VSFrame *fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };

        VSFrame *dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);

        int numPlanes = d->vi->format.numPlanes;
        for (int i = 0; i < numPlanes; i++) {
            if (d->process[i]) {
                Processor::template clenseProcessPlane<T>(
                    reinterpret_cast<T *>(vsapi->getWritePtr(dst, i)),
                    reinterpret_cast<const T *>(vsapi->getReadPtr(src, i)),
                    reinterpret_cast<const T *>(vsapi->getReadPtr(frame1, i)),
                    reinterpret_cast<const T *>(vsapi->getReadPtr(frame2, i)),
                    vsapi->getStride(dst, i)/sizeof(T),
                    vsapi->getFrameWidth(dst, i),
                    vsapi->getFrameHeight(dst, i));
            }
        }

        vsapi->freeFrame(src);
        vsapi->freeFrame(frame1);
        vsapi->freeFrame(frame2);

        return dst;
    }

    return nullptr;
}

static void VS_CC clenseFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ClenseData *d = static_cast<ClenseData *>(instanceData);
    vsapi->freeNode(d->cnode);
    vsapi->freeNode(d->pnode);
    vsapi->freeNode(d->nnode);
    delete d;
}

void VS_CC clenseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ClenseData d = {};
    ClenseData *data;
    int err;
    int n, m, o;
    int i;

    d.mode = static_cast<int>(reinterpret_cast<intptr_t>(userData));
    d.cnode = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.cnode);
    if (!isConstantVideoFormat(d.vi))
        CLENSE_RETERROR("Clense: only constant format input supported");

    if (d.mode == cmNormal) {
        d.pnode = vsapi->mapGetNode(in, "previous", 0, &err);
        if (err)
            d.pnode = vsapi->addNodeRef(d.cnode);
        d.nnode = vsapi->mapGetNode(in, "next", 0, &err);
        if (err)
            d.nnode = vsapi->addNodeRef(d.cnode);
    }

    if (d.pnode && !isSameVideoInfo(d.vi, vsapi->getVideoInfo(d.pnode)))
        CLENSE_RETERROR("Clense: previous clip doesn't have the same format as the main clip");

    if (d.nnode && !isSameVideoInfo(d.vi, vsapi->getVideoInfo(d.nnode)))
        CLENSE_RETERROR("Clense: previous clip doesn't have the same format as the main clip");

    n = d.vi->format.numPlanes;
    m = vsapi->mapNumElements(in, "planes");

    for (i = 0; i < 3; i++)
        d.process[i] = m <= 0;

    for (i = 0; i < m; i++) {
        o = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);

        if (o < 0 || o >= n)
            CLENSE_RETERROR("Clense: plane index out of range");

        if (d.process[o])
            CLENSE_RETERROR("Clense: plane specified twice");

        d.process[o] = 1;
    }

    VSFilterGetFrame getFrameFunc = nullptr;
    if (d.vi->format.sampleType == stInteger) {
        if (d.mode == cmNormal) {
            switch (d.vi->format.bitsPerSample) {
            case 8: getFrameFunc = clenseGetFrame<uint8_t, PlaneProc>; break;
            case 16: getFrameFunc = clenseGetFrame<uint16_t, PlaneProc>; break;
            }
        } else {
            switch (d.vi->format.bitsPerSample) {
            case 8: getFrameFunc = clenseGetFrame<uint8_t, PlaneProcFB>; break;
            case 16: getFrameFunc = clenseGetFrame<uint16_t, PlaneProcFB>; break;
            }
        }
    }

    if (!getFrameFunc)
        CLENSE_RETERROR("Clense: only 8 and 16 bit integer input supported");

    data = new ClenseData(d);

    VSFilterDependency deps3[] = {{d.cnode, rpStrictSpatial}, {d.nnode, rpNoFrameReuse}, {d.pnode, rpNoFrameReuse}};
    VSFilterDependency deps1[] = {{d.cnode, rpGeneral}};
    vsapi->createVideoFilter(out, "Clense", data->vi, getFrameFunc, clenseFree, fmParallel, (d.mode == cmNormal) ? deps3 : deps1, (d.mode == cmNormal) ? 3 : 1, data, core);
}
