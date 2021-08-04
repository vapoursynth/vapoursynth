#include "shared.h"

struct VerticalCleanerData {
    VSNode * node;
    const VSVideoInfo * vi;
    int mode[3];
};

template<typename T>
static void verticalMedian(const T * VS_RESTRICT srcp, T * VS_RESTRICT dstp, const int width, const int height, const ptrdiff_t stride) {
    memcpy(dstp, srcp, stride * sizeof(T));

    srcp += stride;
    dstp += stride;

    for (int y = 1; y < height - 1; y++) {
        for (int x = 0; x < width; x++) {
            const T up = srcp[x - stride];
            const T center = srcp[x];
            const T down = srcp[x + stride];
            dstp[x] = std::min(std::max(std::min(up, down), center), std::max(up, down));
        }

        srcp += stride;
        dstp += stride;
    }

    memcpy(dstp, srcp, stride * sizeof(T));
}

template<typename T>
static void relaxedVerticalMedian(const T * VS_RESTRICT srcp, T * VS_RESTRICT dstp, const int width, const int height, const ptrdiff_t stride, const int bitsPerSample) {
    const int peak = (1 << bitsPerSample) - 1;

    memcpy(dstp, srcp, stride * sizeof(T) * 2);

    srcp += stride * 2;
    dstp += stride * 2;

    for (int y = 2; y < height - 2; y++) {
        for (int x = 0; x < width; x++) {
            const T p2 = srcp[x - stride * 2];
            const T p1 = srcp[x - stride];
            const T c = srcp[x];
            const T n1 = srcp[x + stride];
            const T n2 = srcp[x + stride * 2];

            const T upper = std::max<int>(std::max<int>(std::min<int>(limit(limit(p1 - p2, 0, peak) + p1, 0, peak), limit(limit(n1 - n2, 0, peak) + n1, 0, peak)), p1), n1);
            const T lower = std::min<int>(std::min<int>(p1, n1), std::max<int>(limit(p1 - limit(p2 - p1, 0, peak), 0, peak), limit(n1 - limit(n2 - n1, 0, peak), 0, peak)));

            dstp[x] = limit(c, lower, upper);
        }

        srcp += stride;
        dstp += stride;
    }

    memcpy(dstp, srcp, stride * sizeof(T) * 2);
}

static const VSFrame *VS_CC verticalCleanerGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const VerticalCleanerData * d = static_cast<const VerticalCleanerData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame * fr[] = { d->mode[0] ? nullptr : src, d->mode[1] ? nullptr : src, d->mode[2] ? nullptr : src };
        const int pl[] = { 0, 1, 2 };
        VSFrame * dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);

        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const ptrdiff_t stride = vsapi->getStride(src, plane);
            const uint8_t * srcp = vsapi->getReadPtr(src, plane);
            uint8_t * dstp = vsapi->getWritePtr(dst, plane);

            if (d->mode[plane] == 1) {
                if (d->vi->format.bytesPerSample == 1)
                    verticalMedian<uint8_t>(srcp, dstp, width, height, stride);
                else
                    verticalMedian<uint16_t>(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, height, stride / 2);
            } else if (d->mode[plane] == 2) {
                if (d->vi->format.bytesPerSample == 1)
                    relaxedVerticalMedian<uint8_t>(srcp, dstp, width, height, stride, d->vi->format.bitsPerSample);
                else
                    relaxedVerticalMedian<uint16_t>(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), width, height, stride / 2, d->vi->format.bitsPerSample);
            }
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC verticalCleanerFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    VerticalCleanerData * d = static_cast<VerticalCleanerData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC verticalCleanerCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VerticalCleanerData d;

    d.node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantVideoFormat(d.vi) || d.vi->format.sampleType != stInteger || d.vi->format.bytesPerSample > 2) {
        vsapi->mapSetError(out, "VerticalCleaner: only constant format 8-16 bits integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    const int m = vsapi->mapNumElements(in, "mode");

    if (m > d.vi->format.numPlanes) {
        vsapi->mapSetError(out, "VerticalCleaner: number of modes specified must be equal to or fewer than the number of input planes");
        vsapi->freeNode(d.node);
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (i < m) {
            d.mode[i] = vsapi->mapGetIntSaturated(in, "mode", i, nullptr);
            if (d.mode[i] < 0 || d.mode[i] > 2) {
                vsapi->mapSetError(out, "VerticalCleaner: invalid mode specified, only modes 0-2 supported");
                vsapi->freeNode(d.node);
                return;
            }
        } else {
            d.mode[i] = d.mode[i - 1];
        }

        const int height = d.vi->height >> (i ? d.vi->format.subSamplingH : 0);
        if (d.mode[i] == 1 && height < 3) {
            vsapi->mapSetError(out, "VerticalCleaner: corresponding plane's height must be greater than or equal to 3 for mode 1");
            vsapi->freeNode(d.node);
            return;
        } else if (d.mode[i] == 2 && height < 5) {
            vsapi->mapSetError(out, "VerticalCleaner: corresponding plane's height must be greater than or equal to 5 for mode 2");
            vsapi->freeNode(d.node);
            return;
        }
    }

    VerticalCleanerData * data = new VerticalCleanerData(d);

    VSFilterDependency deps[] = {{d.node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "VerticalCleaner", data->vi, verticalCleanerGetFrame, verticalCleanerFree, fmParallel, deps, 1, data, core);
}
