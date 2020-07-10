/*
* Copyright (c) 2012-2016 Fredrik Mellbin
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

#include "internalfilters.h"
#include "VSHelper4.h"
#include "filtershared.h"
#include "filtersharedcpp.h"
#include <cstdlib>
#include <memory>
#include <vector>
#include <algorithm>

//////////////////////////////////////////
// Shared

enum MismatchCauses {
    DifferentDimensions = 1,
    DifferentFormats,
    DifferentFrameRates,
    DifferentLengths
};

static int findCommonVi(VSNodeRef **nodes, int num, VSVideoInfo *outvi, int ignorelength, const VSAPI *vsapi) {
    int mismatch = 0;
    const VSVideoInfo *vi;
    *outvi = *vsapi->getVideoInfo(nodes[0]);

    for (int i = 1; i < num; i++) {
        vi = vsapi->getVideoInfo(nodes[i]);

        if (outvi->width != vi->width || outvi->height != vi->height) {
            outvi->width = 0;
            outvi->height = 0;
            mismatch = DifferentDimensions;
        }

        if (!isSameVideoFormat(&outvi->format, &vi->format)) {
            memset(&outvi->format, 0, sizeof(outvi->format));
            mismatch = DifferentFormats;
        }

        if (outvi->fpsNum != vi->fpsNum || outvi->fpsDen != vi->fpsDen) {
            outvi->fpsDen = 0;
            outvi->fpsNum = 0;
            mismatch = DifferentFrameRates;
        }

        if (outvi->numFrames < vi->numFrames) {
            outvi->numFrames = vi->numFrames;

            if (!ignorelength)
                mismatch = DifferentLengths;
        }
    }

    return mismatch;
}

//////////////////////////////////////////
// Trim

typedef struct {
    int first;
} TrimDataExtra;

typedef SingleNodeData<TrimDataExtra> TrimData;

static const VSFrameRef *VS_CC trimGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TrimData *d = reinterpret_cast<TrimData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n + d->first, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n + d->first, d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC trimCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TrimData> d(new TrimData(vsapi));
    int err;
    int trimlen;

    d->first = int64ToIntS(vsapi->propGetInt(in, "first", 0, &err));
    int firstset = !err;
    int last = int64ToIntS(vsapi->propGetInt(in, "last", 0, &err));
    int lastset = !err;
    int length = int64ToIntS(vsapi->propGetInt(in, "length", 0, &err));
    int lengthset = !err;

    if (lastset && lengthset)
        RETERROR("Trim: both last frame and length specified");

    if (lastset && last < d->first)
        RETERROR("Trim: invalid last frame specified (last is less than first)");

    if (lengthset && length < 1)
        RETERROR("Trim: invalid length specified (less than 1)");

    if (d->first < 0)
        RETERROR("Trim: invalid first frame specified (less than 0)");

    d->node = vsapi->propGetNode(in, "clip", 0, 0);

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    if ((lastset && last >= vi.numFrames) || (lengthset && (d->first + length) > vi.numFrames) || (vi.numFrames <= d->first))
        RETERROR("Trim: last frame beyond clip end");

    if (lastset) {
        trimlen = last - d->first + 1;
    } else if (lengthset) {
        trimlen = length;
    } else {
        trimlen = vi.numFrames - d->first;
    }

    // obvious nop() so just pass through the input clip
    if ((!firstset && !lastset && !lengthset) || (trimlen && trimlen == vi.numFrames)) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        return;
    }

    vi.numFrames = trimlen;

    vsapi->createVideoFilter(out, "Trim", &vi, 1, trimGetframe, filterFree<TrimData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// Interleave

typedef struct {
    VSVideoInfo vi;
    int numclips;
    int modifyDuration;
} InterleaveDataExtra;

typedef VariableNodeData<InterleaveDataExtra> InterleaveData;


static const VSFrameRef *VS_CC interleaveGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = reinterpret_cast<InterleaveData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / d->numclips, d->node[n % d->numclips], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n / d->numclips, d->node[n % d->numclips], frameCtx);
        if (d->modifyDuration) {
            VSFrameRef *dst = vsapi->copyFrame(src, core);
            vsapi->freeFrame(src);

            VSMap *dst_props = vsapi->getFramePropsRW(dst);
            int errNum, errDen;
            int64_t durationNum = vsapi->propGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->propGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                vs_muldivRational(&durationNum, &durationDen, 1, d->numclips);
                vsapi->propSetInt(dst_props, "_DurationNum", durationNum, paReplace);
                vsapi->propSetInt(dst_props, "_DurationDen", durationDen, paReplace);
            }
            return dst;
        } else {
            return src;
        }
    }

    return nullptr;
}

static void VS_CC interleaveCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<InterleaveData> d(new InterleaveData(vsapi));
    int err;

    bool mismatch = !!vsapi->propGetInt(in, "mismatch", 0, &err);
    bool extend = !!vsapi->propGetInt(in, "extend", 0, &err);
    d->modifyDuration = !!vsapi->propGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = 1;
    d->numclips = vsapi->propNumElements(in, "clips");

    if (d->numclips == 1) { // passthrough for the special case with only one clip
        VSNodeRef *cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, paReplace);
        vsapi->freeNode(cref);
    } else {
        d->node.resize(d->numclips);
        bool compat = false;

        for (int i = 0; i < d->numclips; i++) {
            d->node[i] = vsapi->propGetNode(in, "clips", i, 0);

            if (isCompatFormat(&vsapi->getVideoInfo(d->node[i])->format))
                compat = true;
        }

        int mismatchCause = findCommonVi(d->node.data(), d->numclips, &d->vi, 1, vsapi);
        if (mismatchCause && (!mismatch || compat)) {
            if (mismatchCause == DifferentDimensions)
                RETERROR("Interleave: the clips' dimensions don't match");
            else if (mismatchCause == DifferentFormats)
                RETERROR("Interleave: the clips' formats don't match");
            else if (mismatchCause == DifferentFrameRates)
                RETERROR("Interleave: the clips' frame rates don't match");
            else if (mismatchCause == DifferentLengths)
                RETERROR("Interleave: the clips' lengths don't match");
        }

        bool overflow = false;

        if (extend) {
            if (d->vi.numFrames > INT_MAX / d->numclips)
                overflow = true;
            d->vi.numFrames *= d->numclips;
        } else if (d->vi.numFrames) {
            // this is exactly how avisynth does it
            d->vi.numFrames = (vsapi->getVideoInfo(d->node[0])->numFrames - 1) * d->numclips + 1;
            for (int i = 0; i < d->numclips; i++) {
                if (vsapi->getVideoInfo(d->node[i])->numFrames > ((INT_MAX - i - 1) / d->numclips + 1))
                    overflow = true;
                d->vi.numFrames = VSMAX(d->vi.numFrames, (vsapi->getVideoInfo(d->node[i])->numFrames - 1) * d->numclips + i + 1);
            }
        }

        if (overflow)
            RETERROR("Interleave: resulting clip is too long");

        if (d->modifyDuration)
            vs_muldivRational(&d->vi.fpsNum, &d->vi.fpsDen, d->numclips, 1);

        vsapi->createVideoFilter(out, "Interleave", &d->vi, 1, interleaveGetframe, filterFree<InterleaveData>, fmParallel, nfNoCache, d.get(), core);
        d.release();
    }
}

//////////////////////////////////////////
// Reverse

typedef SingleNodeData<VIPointerData> ReverseData;

static const VSFrameRef *VS_CC reverseGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ReverseData *d = reinterpret_cast<ReverseData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(std::max(d->vi->numFrames - n - 1, 0), d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(std::max(d->vi->numFrames - n - 1, 0), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC reverseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ReverseData> d(new ReverseData(vsapi));

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    vsapi->createVideoFilter(out, "Reverse", d->vi, 1, reverseGetframe, filterFree<ReverseData>, fmParallel, nfNoCache, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Loop

typedef SingleNodeData<VIPointerData> LoopData;

static const VSFrameRef *VS_CC loopGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LoopData *d = reinterpret_cast<LoopData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n % d->vi->numFrames, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(n % d->vi->numFrames, d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC loopCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<LoopData> d(new LoopData(vsapi));
    int err;
    int times = int64ToIntS(vsapi->propGetInt(in, "times", 0, &err));
    if (times < 0)
        RETERROR("Loop: cannot repeat clip a negative number of times");

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);
    VSVideoInfo vi = *d->vi;

    // early termination for the trivial case
    if (times == 1) {
        vsapi->propSetNode(out, "clip", d->node, paReplace);
        return;
    }

    if (times > 0) {
        if (vi.numFrames > INT_MAX / times)
            RETERROR("Loop: resulting clip is too long");

        vi.numFrames *= times;
    } else { // loop for maximum duration
        vi.numFrames = INT_MAX;
    }

    vsapi->createVideoFilter(out, "Loop", &vi, 1, loopGetframe, filterFree<LoopData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// SelectEvery

typedef struct {
    std::vector<int> offsets;
    int cycle;
    int num;
    bool modifyDuration;
} SelectEveryDataExtra;

typedef SingleNodeData<SelectEveryDataExtra> SelectEveryData;

static const VSFrameRef *VS_CC selectEveryGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = reinterpret_cast<SelectEveryData *>(instanceData);

    if (activationReason == arInitial) {
        n = (n / d->num) * d->cycle + d->offsets[n % d->num];
        frameData[0] = reinterpret_cast<void *>(n);
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(reinterpret_cast<int>(frameData[0]), d->node, frameCtx);
        if (d->modifyDuration) {
            VSFrameRef *dst = vsapi->copyFrame(src, core);
            VSMap *dst_props = vsapi->getFramePropsRW(dst);
            int errNum, errDen;
            int64_t durationNum = vsapi->propGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->propGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                vs_muldivRational(&durationNum, &durationDen, d->cycle, d->num);
                vsapi->propSetInt(dst_props, "_DurationNum", durationNum, paReplace);
                vsapi->propSetInt(dst_props, "_DurationDen", durationDen, paReplace);
            }
            vsapi->freeFrame(src);
            return dst;
        } else {
            return src;
        }
    }

    return nullptr;
}

static void VS_CC selectEveryCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SelectEveryData> d(new SelectEveryData(vsapi));
    int err;

    d->cycle = int64ToIntS(vsapi->propGetInt(in, "cycle", 0, 0));

    if (d->cycle <= 1)
        RETERROR("SelectEvery: invalid cycle size (must be greater than 1)");

    d->num = vsapi->propNumElements(in, "offsets");
    d->modifyDuration = !!vsapi->propGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = true;

    d->offsets.resize(d->num);

    for (int i = 0; i < d->num; i++) {
        d->offsets[i] = int64ToIntS(vsapi->propGetInt(in, "offsets", i, 0));

        if (d->offsets[i] < 0 || d->offsets[i] >= d->cycle)
            RETERROR("SelectEvery: invalid offset specified");
    }

    d->node = vsapi->propGetNode(in, "clip", 0, 0);

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);
    int inputnframes = vi.numFrames;
    if (inputnframes) {
        vi.numFrames = (inputnframes / d->cycle) * d->num;
        for (int i = 0; i < d->num; i++)
            if (d->offsets[i] < inputnframes % d->cycle)
                vi.numFrames++;
    }

    if (vi.numFrames == 0)
        RETERROR("SelectEvery: no frames to output, all offsets outside available frames");

    if (d->modifyDuration)
        vs_muldivRational(&vi.fpsNum, &vi.fpsDen, d->num, d->cycle);

    vsapi->createVideoFilter(out, "SelectEvery", &vi, 1, selectEveryGetframe, filterFree<SelectEveryData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// Splice

typedef struct {
    std::vector<int> numframes;
    int numclips;
} SpliceDataExtra;

typedef VariableNodeData<SpliceDataExtra> SpliceData;

static const VSFrameRef *VS_CC spliceGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SpliceData *d = reinterpret_cast<SpliceData *>(instanceData);

    if (activationReason == arInitial) {
        int frame = 0;
        int idx = 0;
        int cumframe = 0;

        for (int i = 0; i < d->numclips; i++) {
            if ((n >= cumframe && n < cumframe + d->numframes[i]) || i == d->numclips - 1) {
                idx = i;
                frame = n - cumframe;
                break;
            }

            cumframe += d->numframes[i];
        }

        frameData[0] = d->node[idx];
        frameData[1] = reinterpret_cast<void *>(frame);
        vsapi->requestFrameFilter(frame, d->node[idx], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *f = vsapi->getFrameFilter(reinterpret_cast<int>(frameData[1]), reinterpret_cast<VSNodeRef *>(frameData[0]), frameCtx);
        return f;
    }

    return nullptr;
}

static void VS_CC spliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SpliceData> d(new SpliceData(vsapi));
    int err;
    VSVideoInfo vi;

    d->numclips = vsapi->propNumElements(in, "clips");
    bool mismatch = !!vsapi->propGetInt(in, "mismatch", 0, &err);

    if (d->numclips == 1) { // passthrough for the special case with only one clip
        VSNodeRef *cref = vsapi->propGetNode(in, "clips", 0, 0);
        vsapi->propSetNode(out, "clip", cref, paReplace);
        vsapi->freeNode(cref);
    } else {
        bool compat = false;
        d->node.resize(d->numclips);

        for (int i = 0; i < d->numclips; i++) {
            d->node[i] = vsapi->propGetNode(in, "clips", i, 0);

            if (isCompatFormat(&vsapi->getVideoInfo(d->node[i])->format))
                compat = 1;
        }

        // fixme, put mismatch cause text and check in a helper function
        int mismatchCause = findCommonVi(d->node.data(), d->numclips, &vi, 1, vsapi);
        if (mismatchCause && (!mismatch || compat) && !isSameVideoInfo(&vi, vsapi->getVideoInfo(d->node[0]))) {
            if (mismatchCause == DifferentDimensions)
                RETERROR("Splice: the clips' dimensions don't match");
            else if (mismatchCause == DifferentFormats)
                RETERROR("Splice: the clips' formats don't match");
            else if (mismatchCause == DifferentFrameRates)
                RETERROR("Splice: the clips' frame rates don't match");
            else if (mismatchCause == DifferentLengths)
                RETERROR("Splice: the clips' lengths don't match");
        }

        d->numframes.resize(d->numclips);
        vi.numFrames = 0;

        for (int i = 0; i < d->numclips; i++) {
            d->numframes[i] = (vsapi->getVideoInfo(d->node[i]))->numFrames;
            vi.numFrames += d->numframes[i];

            // did it overflow?
            if (vi.numFrames < d->numframes[i])
                RETERROR("Splice: the resulting clip is too long");
        }

        vsapi->createVideoFilter(out, "Splice", &vi, 1, spliceGetframe, filterFree<SpliceData>, fmParallel, nfNoCache, d.release(), core);
    }
}

//////////////////////////////////////////
// DuplicateFrames

typedef struct {
    std::vector<int> dups;
    int num_dups;
} DuplicateFramesDataExtra;

typedef SingleNodeData<DuplicateFramesDataExtra> DuplicateFramesData;

static const VSFrameRef *VS_CC duplicateFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DuplicateFramesData *d = reinterpret_cast<DuplicateFramesData *>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_dups; i++)
            if (n > d->dups[i])
                n--;
            else
                break;

        frameData[0] = reinterpret_cast<void *>(n);

        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(reinterpret_cast<int>(frameData[0]), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC duplicateFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DuplicateFramesData> d(new DuplicateFramesData(vsapi));

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    d->num_dups = vsapi->propNumElements(in, "frames");

    d->dups.resize(d->num_dups);

    for (int i = 0; i < d->num_dups; i++) {
        d->dups[i] = int64ToIntS(vsapi->propGetInt(in, "frames", i, 0));

        if (d->dups[i] < 0 || (vi.numFrames && d->dups[i] > vi.numFrames - 1))
            RETERROR("DuplicateFrames: out of bounds frame number");
    }

    std::sort(d->dups.begin(), d->dups.end());

    if (vi.numFrames + d->num_dups < vi.numFrames)
        RETERROR("DuplicateFrames: resulting clip is too long");

    vi.numFrames += d->num_dups;

    vsapi->createVideoFilter(out, "DuplicateFrames", &vi, 1, duplicateFramesGetFrame, filterFree<DuplicateFramesData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// DeleteFrames

typedef struct {
    std::vector<int> del;
    int num_delete;
} DeleteFramesDataExtra;

typedef SingleNodeData<DeleteFramesDataExtra> DeleteFramesData;

static const VSFrameRef *VS_CC deleteFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DeleteFramesData *d = reinterpret_cast<DeleteFramesData *>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_delete; i++)
            if (n >= d->del[i])
                n++;
            else
                break;
        frameData[0] = reinterpret_cast<void *>(n);
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(reinterpret_cast<int>(frameData[0]), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC deleteFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DeleteFramesData> d(new DeleteFramesData(vsapi));

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    d->num_delete = vsapi->propNumElements(in, "frames");

    d->del.resize(d->num_delete);

    for (int i = 0; i < d->num_delete; i++) {
        d->del[i] = int64ToIntS(vsapi->propGetInt(in, "frames", i, 0));

        if (d->del[i] < 0 || (vi.numFrames && d->del[i] >= vi.numFrames))
            RETERROR("DeleteFrames: out of bounds frame number");
    }

    std::sort(d->del.begin(), d->del.end());

    for (int i = 0; i < d->num_delete - 1; i++) {
        if (d->del[i] == d->del[i + 1])
            RETERROR("DeleteFrames: can't delete a frame more than once");
    }

    if (vi.numFrames) {
        vi.numFrames -= d->num_delete;
        if (vi.numFrames <= 0)
            RETERROR("DeleteFrames: can't delete all frames");
    }

    vsapi->createVideoFilter(out, "DeleteFrames", &vi, 1, deleteFramesGetFrame, filterFree<DeleteFramesData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// FreezeFrames

struct Freeze {
    int first;
    int last;
    int replacement;
    bool operator<(const Freeze &other) const {
        return first < other.first;
    }
};

typedef struct {
    std::vector<Freeze> freeze;
} FreezeFramesDataExtra;

typedef SingleNodeData<FreezeFramesDataExtra> FreezeFramesData;

static const VSFrameRef *VS_CC freezeFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FreezeFramesData *d = reinterpret_cast<FreezeFramesData *>(instanceData);

    if (activationReason == arInitial) {
        if (n >= d->freeze.front().first && n <= d->freeze.back().last)
            for (auto &iter : d->freeze)
                if (n >= iter.first && n <= iter.last) {
                    n = iter.replacement;
                    break;
                }

        frameData[0] = reinterpret_cast<void *>(n);

        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(reinterpret_cast<int>(frameData[0]), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC freezeFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FreezeFramesData> d(new FreezeFramesData(vsapi));

    int num_freeze = vsapi->propNumElements(in, "first");
    if (num_freeze != vsapi->propNumElements(in, "last") || num_freeze != vsapi->propNumElements(in, "replacement"))
        RETERROR("FreezeFrames: 'first', 'last', and 'replacement' must have the same length.");

    d->node = vsapi->propGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    d->freeze.resize(num_freeze);

    for (int i = 0; i < num_freeze; i++) {
        d->freeze[i].first = int64ToIntS(vsapi->propGetInt(in, "first", i, 0));
        d->freeze[i].last = int64ToIntS(vsapi->propGetInt(in, "last", i, 0));
        d->freeze[i].replacement = int64ToIntS(vsapi->propGetInt(in, "replacement", i, 0));

        if (d->freeze[i].first > d->freeze[i].last) {
            int tmp = d->freeze[i].first;
            d->freeze[i].first = d->freeze[i].last;
            d->freeze[i].last = tmp;
        }

        if (d->freeze[i].first < 0 || (vi->numFrames && d->freeze[i].last >= vi->numFrames) ||
            d->freeze[i].replacement < 0 || (vi->numFrames && d->freeze[i].replacement >= vi->numFrames))
            RETERROR("FreezeFrames: out of bounds frame number(s)");
    }

    std::sort(d->freeze.begin(), d->freeze.end());

    for (int i = 0; i < num_freeze - 1; i++)
        if (d->freeze[i].last >= d->freeze[i + 1].first)
            RETERROR("FreezeFrames: the frame ranges must not overlap");

    vsapi->createVideoFilter(out, "FreezeFrames", vi, 1, freezeFramesGetFrame, filterFree<FreezeFramesData>, fmParallel, nfNoCache, d.release(), core);
}

//////////////////////////////////////////
// Init

void VS_CC reorderInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Trim", "clip:vnode;first:int:opt;last:int:opt;length:int:opt;", "clip:vnode;", trimCreate, 0, plugin);
    vspapi->registerFunction("Reverse", "clip:vnode;", "clip:vnode;", reverseCreate, 0, plugin);
    vspapi->registerFunction("Loop", "clip:vnode;times:int:opt;", "clip:vnode;", loopCreate, 0, plugin);
    vspapi->registerFunction("Interleave", "clips:vnode[];extend:int:opt;mismatch:int:opt;modify_duration:int:opt;", "clip:vnode;", interleaveCreate, 0, plugin);
    vspapi->registerFunction("SelectEvery", "clip:vnode;cycle:int;offsets:int[];modify_duration:int:opt;", "clip:vnode;", selectEveryCreate, 0, plugin);
    vspapi->registerFunction("Splice", "clips:vnode[];mismatch:int:opt;", "clip:vnode;", spliceCreate, 0, plugin);
    vspapi->registerFunction("DuplicateFrames", "clip:vnode;frames:int[];", "clip:vnode;", duplicateFramesCreate, 0, plugin);
    vspapi->registerFunction("DeleteFrames", "clip:vnode;frames:int[];", "clip:vnode;", deleteFramesCreate, 0, plugin);
    vspapi->registerFunction("FreezeFrames", "clip:vnode;first:int[];last:int[];replacement:int[];", "clip:vnode;", freezeFramesCreate, 0, plugin);
}
