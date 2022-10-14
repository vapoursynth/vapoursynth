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
#include <memory>
#include <vector>
#include <algorithm>
#include "VSHelper4.h"
#include "filtershared.h"
#include "internalfilters.h"

using namespace vsh;

//////////////////////////////////////////
// Shared

struct MismatchInfo {
    bool match;
    bool differentDimensions;
    bool differentFormat;
    bool differentFrameRate;
    int clipnum;
};

static MismatchInfo findCommonVi(VSNode **nodes, int num, VSVideoInfo *outvi, const VSAPI *vsapi) {
    MismatchInfo result = {};
    const VSVideoInfo *vi;
    *outvi = *vsapi->getVideoInfo(nodes[0]);

    for (int i = 1; i < num; i++) {
        vi = vsapi->getVideoInfo(nodes[i]);

        if (outvi->width != vi->width || outvi->height != vi->height) {
            outvi->width = 0;
            outvi->height = 0;
            result.differentDimensions = true;
            if (!result.clipnum)
                result.clipnum = i;
        }

        if (!isSameVideoFormat(&outvi->format, &vi->format)) {
            outvi->format = {};
            result.differentFormat = true;
            if (!result.clipnum)
                result.clipnum = i;
        }

        if (outvi->fpsNum != vi->fpsNum || outvi->fpsDen != vi->fpsDen) {
            outvi->fpsDen = 0;
            outvi->fpsNum = 0;
            result.differentFrameRate = true;
            if (!result.clipnum)
                result.clipnum = i;
        }

        if (outvi->numFrames < vi->numFrames)
            outvi->numFrames = vi->numFrames;
    }

    result.match = !result.differentDimensions && !result.differentFormat && !result.differentFrameRate;

    return result;
}

static std::string mismatchToText(const MismatchInfo &info) {
    std::string s;
    if (info.differentFormat) {
        if (!s.empty())
            s += ", ";
        s += "format";
    }
    if (info.differentDimensions) {
        if (!s.empty())
            s += ", ";
        s += "dimensions";
    }
    if (info.differentFrameRate) {
        if (!s.empty())
            s += ", ";
        s += "framerate";
    }
    return s;
}

//////////////////////////////////////////
// Trim

typedef struct {
    int first;
} TrimDataExtra;

typedef SingleNodeData<TrimDataExtra> TrimData;

static const VSFrame *VS_CC trimGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
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

    d->first = vsapi->mapGetIntSaturated(in, "first", 0, &err);
    int firstset = !err;
    int last = vsapi->mapGetIntSaturated(in, "last", 0, &err);
    int lastset = !err;
    int length = vsapi->mapGetIntSaturated(in, "length", 0, &err);
    int lengthset = !err;

    if (lastset && lengthset)
        RETERROR("Trim: both last frame and length specified");

    if (lastset && last < d->first)
        RETERROR("Trim: invalid last frame specified (last is less than first)");

    if (lengthset && length < 1)
        RETERROR("Trim: invalid length specified (less than 1)");

    if (d->first < 0)
        RETERROR("Trim: invalid first frame specified (less than 0)");

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

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
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    vi.numFrames = trimlen;

    VSFilterDependency deps[] = {{d->node, rpNoFrameReuse}};
    vsapi->createVideoFilter(out, "Trim", &vi, trimGetframe, filterFree<TrimData>, fmParallel, deps, 1, d.release(), core);
}

//////////////////////////////////////////
// Interleave

typedef struct {
    VSVideoInfo vi;
    int numclips;
    int modifyDuration;
} InterleaveDataExtra;

typedef VariableNodeData<InterleaveDataExtra> InterleaveData;


static const VSFrame *VS_CC interleaveGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InterleaveData *d = reinterpret_cast<InterleaveData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / d->numclips, d->nodes[n % d->numclips], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n / d->numclips, d->nodes[n % d->numclips], frameCtx);
        if (d->modifyDuration) {
            VSFrame *dst = vsapi->copyFrame(src, core);
            vsapi->freeFrame(src);

            VSMap *dst_props = vsapi->getFramePropertiesRW(dst);
            int errNum, errDen;
            int64_t durationNum = vsapi->mapGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->mapGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                muldivRational(&durationNum, &durationDen, 1, d->numclips);
                vsapi->mapSetInt(dst_props, "_DurationNum", durationNum, maReplace);
                vsapi->mapSetInt(dst_props, "_DurationDen", durationDen, maReplace);
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

    bool mismatch = !!vsapi->mapGetInt(in, "mismatch", 0, &err);
    bool extend = !!vsapi->mapGetInt(in, "extend", 0, &err);
    d->modifyDuration = !!vsapi->mapGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = 1;
    d->numclips = vsapi->mapNumElements(in, "clips");

    if (d->numclips == 1) { // passthrough for the special case with only one clip
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clips", 0, 0), maReplace);
    } else {
        d->nodes.resize(d->numclips);

        for (int i = 0; i < d->numclips; i++)
            d->nodes[i] = vsapi->mapGetNode(in, "clips", i, 0);

        MismatchInfo mminfo = findCommonVi(d->nodes.data(), d->numclips, &d->vi, vsapi);
        if (!mminfo.match && !mismatch)
            RETERROR(("Interleave: clips are mismatched in " + mismatchToText(mminfo) + " starting at clip #" + std::to_string(mminfo.clipnum) + ", passed " + videoInfoToString(&d->vi, vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->nodes[mminfo.clipnum]), vsapi)).c_str());

        bool overflow = false;
        int maxNumFrames = d->numclips;

        if (extend) {
            if (d->vi.numFrames > INT_MAX / d->numclips)
                overflow = true;
            d->vi.numFrames *= d->numclips;
        } else {
            // this is exactly how avisynth does it
            d->vi.numFrames = (vsapi->getVideoInfo(d->nodes[0])->numFrames - 1) * d->numclips + 1;
            for (int i = 0; i < d->numclips; i++) {
                if (vsapi->getVideoInfo(d->nodes[i])->numFrames > ((INT_MAX - i - 1) / d->numclips + 1))
                    overflow = true;
                d->vi.numFrames = std::max(d->vi.numFrames, (vsapi->getVideoInfo(d->nodes[i])->numFrames - 1) * d->numclips + i + 1);
            }
        }

        if (overflow)
            RETERROR("Interleave: resulting clip is too long");

        if (d->modifyDuration)
            muldivRational(&d->vi.fpsNum, &d->vi.fpsDen, d->numclips, 1);

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < d->numclips; i++)
            deps.push_back({d->nodes[i], (maxNumFrames <= vsapi->getVideoInfo(d->nodes[i])->numFrames) ? rpStrictSpatial : rpGeneral});
        vsapi->createVideoFilter(out, "Interleave", &d->vi, interleaveGetframe, filterFree<InterleaveData>, fmParallel, deps.data(), d->numclips, d.get(), core);
        d.release();
    }
}

//////////////////////////////////////////
// Reverse

typedef SingleNodeData<VIPointerData> ReverseData;

static const VSFrame *VS_CC reverseGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
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

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    VSFilterDependency deps[] = {{ d->node, rpNoFrameReuse }};
    vsapi->createVideoFilter(out, "Reverse", d->vi, reverseGetframe, filterFree<ReverseData>, fmParallel, deps, 1, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Loop

typedef SingleNodeData<VIPointerData> LoopData;

static const VSFrame *VS_CC loopGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
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
    int times = vsapi->mapGetIntSaturated(in, "times", 0, &err);
    if (times < 0)
        RETERROR("Loop: cannot repeat clip a negative number of times");

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);
    VSVideoInfo vi = *d->vi;

    // early termination for the trivial case
    if (times == 1) {
        vsapi->mapSetNode(out, "clip", d->node, maReplace);
        return;
    }

    if (times > 0) {
        if (vi.numFrames > INT_MAX / times)
            RETERROR("Loop: resulting clip is too long");

        vi.numFrames *= times;
    } else { // loop for maximum duration
        vi.numFrames = INT_MAX;
    }

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "Loop", &vi, loopGetframe, filterFree<LoopData>, fmParallel, deps, 1, d.release(), core);
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

static const VSFrame *VS_CC selectEveryGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = reinterpret_cast<SelectEveryData *>(instanceData);

    if (activationReason == arInitial) {
        n = (n / d->num) * d->cycle + d->offsets[n % d->num];
        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
        if (d->modifyDuration) {
            VSFrame *dst = vsapi->copyFrame(src, core);
            VSMap *dst_props = vsapi->getFramePropertiesRW(dst);
            int errNum, errDen;
            int64_t durationNum = vsapi->mapGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->mapGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                muldivRational(&durationNum, &durationDen, d->cycle, d->num);
                vsapi->mapSetInt(dst_props, "_DurationNum", durationNum, maReplace);
                vsapi->mapSetInt(dst_props, "_DurationDen", durationDen, maReplace);
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

    d->cycle = vsapi->mapGetIntSaturated(in, "cycle", 0, 0);

    if (d->cycle <= 1)
        RETERROR("SelectEvery: invalid cycle size (must be greater than 1)");

    d->num = vsapi->mapNumElements(in, "offsets");
    d->modifyDuration = !!vsapi->mapGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = true;

    d->offsets.resize(d->num);

    for (int i = 0; i < d->num; i++) {
        d->offsets[i] = vsapi->mapGetIntSaturated(in, "offsets", i, 0);

        if (d->offsets[i] < 0 || d->offsets[i] >= d->cycle)
            RETERROR("SelectEvery: invalid offset specified");
    }

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

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
        muldivRational(&vi.fpsNum, &vi.fpsDen, d->num, d->cycle);

    VSFilterDependency deps[] = {{d->node, rpNoFrameReuse}};
    vsapi->createVideoFilter(out, "SelectEvery", &vi, selectEveryGetframe, filterFree<SelectEveryData>, fmParallel, deps, 1, d.release(), core);
}

//////////////////////////////////////////
// Splice

typedef struct {
    std::vector<int> numframes;
    int numclips;
} SpliceDataExtra;

typedef VariableNodeData<SpliceDataExtra> SpliceData;

static const VSFrame *VS_CC spliceGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
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

        frameData[0] = d->nodes[idx];
        frameData[1] = reinterpret_cast<void *>(static_cast<intptr_t>(frame));
        vsapi->requestFrameFilter(frame, d->nodes[idx], frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *f = vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[1])), reinterpret_cast<VSNode *>(frameData[0]), frameCtx);
        return f;
    }

    return nullptr;
}

static void VS_CC spliceCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SpliceData> d(new SpliceData(vsapi));
    int err;
    VSVideoInfo vi;

    d->numclips = vsapi->mapNumElements(in, "clips");
    bool mismatch = !!vsapi->mapGetInt(in, "mismatch", 0, &err);

    if (d->numclips == 1) { // passthrough for the special case with only one clip
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clips", 0, 0), maReplace);
    } else {
        d->nodes.resize(d->numclips);

        for (int i = 0; i < d->numclips; i++)
            d->nodes[i] = vsapi->mapGetNode(in, "clips", i, 0);

        MismatchInfo mminfo = findCommonVi(d->nodes.data(), d->numclips, &vi, vsapi);
        if (!mminfo.match && !mismatch && !isSameVideoInfo(&vi, vsapi->getVideoInfo(d->nodes[0])))
            RETERROR(("Splice: clips are mismatched in " + mismatchToText(mminfo) + " starting at clip #" + std::to_string(mminfo.clipnum) + ", passed " + videoInfoToString(vsapi->getVideoInfo(d->nodes[mminfo.clipnum - 1]), vsapi) + " and " + videoInfoToString(vsapi->getVideoInfo(d->nodes[mminfo.clipnum]), vsapi)).c_str());

        d->numframes.resize(d->numclips);
        vi.numFrames = 0;

        for (int i = 0; i < d->numclips; i++) {
            d->numframes[i] = (vsapi->getVideoInfo(d->nodes[i]))->numFrames;
            vi.numFrames += d->numframes[i];

            // did it overflow?
            if (vi.numFrames < d->numframes[i])
                RETERROR("Splice: the resulting clip is too long");
        }

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < d->numclips; i++)
            deps.push_back({ d->nodes[i], rpNoFrameReuse });
        vsapi->createVideoFilter(out, "Splice", &vi, spliceGetframe, filterFree<SpliceData>, fmParallel, deps.data(), d->numclips, d.get(), core);
        d.release();
    }
}

//////////////////////////////////////////
// DuplicateFrames

typedef struct {
    std::vector<int> dups;
    int num_dups;
} DuplicateFramesDataExtra;

typedef SingleNodeData<DuplicateFramesDataExtra> DuplicateFramesData;

static const VSFrame *VS_CC duplicateFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DuplicateFramesData *d = reinterpret_cast<DuplicateFramesData *>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_dups; i++)
            if (n > d->dups[i])
                n--;
            else
                break;

        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));

        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC duplicateFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DuplicateFramesData> d(new DuplicateFramesData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    d->num_dups = vsapi->mapNumElements(in, "frames");

    d->dups.resize(d->num_dups);

    for (int i = 0; i < d->num_dups; i++) {
        d->dups[i] = vsapi->mapGetIntSaturated(in, "frames", i, 0);

        if (d->dups[i] < 0 || (vi.numFrames && d->dups[i] > vi.numFrames - 1))
            RETERROR("DuplicateFrames: out of bounds frame number");
    }

    std::sort(d->dups.begin(), d->dups.end());

    if (vi.numFrames + d->num_dups < vi.numFrames)
        RETERROR("DuplicateFrames: resulting clip is too long");

    vi.numFrames += d->num_dups;

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "DuplicateFrames", &vi, duplicateFramesGetFrame, filterFree<DuplicateFramesData>, fmParallel, deps, 1, d.release(), core);
}

//////////////////////////////////////////
// DeleteFrames

typedef struct {
    std::vector<int> del;
    int num_delete;
} DeleteFramesDataExtra;

typedef SingleNodeData<DeleteFramesDataExtra> DeleteFramesData;

static const VSFrame *VS_CC deleteFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DeleteFramesData *d = reinterpret_cast<DeleteFramesData *>(instanceData);

    if (activationReason == arInitial) {
        for (int i = 0; i < d->num_delete; i++)
            if (n >= d->del[i])
                n++;
            else
                break;
        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC deleteFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DeleteFramesData> d(new DeleteFramesData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    d->num_delete = vsapi->mapNumElements(in, "frames");

    d->del.resize(d->num_delete);

    for (int i = 0; i < d->num_delete; i++) {
        d->del[i] = vsapi->mapGetIntSaturated(in, "frames", i, 0);

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

    VSFilterDependency deps[] = {{d->node, rpNoFrameReuse}};
    vsapi->createVideoFilter(out, "DeleteFrames", &vi, deleteFramesGetFrame, filterFree<DeleteFramesData>, fmParallel, deps, 1, d.release(), core);
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

static const VSFrame *VS_CC freezeFramesGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    FreezeFramesData *d = reinterpret_cast<FreezeFramesData *>(instanceData);

    if (activationReason == arInitial) {
        if (n >= d->freeze.front().first && n <= d->freeze.back().last)
            for (auto &iter : d->freeze)
                if (n >= iter.first && n <= iter.last) {
                    n = iter.replacement;
                    break;
                }

        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));

        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        return vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
    }

    return nullptr;
}

static void VS_CC freezeFramesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int num_freeze = vsapi->mapNumElements(in, "first");
    if (num_freeze != vsapi->mapNumElements(in, "last") || num_freeze != vsapi->mapNumElements(in, "replacement"))
        RETERROR("FreezeFrames: 'first', 'last', and 'replacement' must have the same length.");

    if (num_freeze == 0) {
        vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(in, "clip", 0, 0), maAppend);
        return;
    }

    std::unique_ptr<FreezeFramesData> d(new FreezeFramesData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    d->freeze.resize(num_freeze);

    for (int i = 0; i < num_freeze; i++) {
        d->freeze[i].first = vsapi->mapGetIntSaturated(in, "first", i, 0);
        d->freeze[i].last = vsapi->mapGetIntSaturated(in, "last", i, 0);
        d->freeze[i].replacement = vsapi->mapGetIntSaturated(in, "replacement", i, 0);

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

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "FreezeFrames", vi, freezeFramesGetFrame, filterFree<FreezeFramesData>, fmParallel, deps, 1, d.release(), core);
}

//////////////////////////////////////////
// Init

void reorderInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Trim", "clip:vnode;first:int:opt;last:int:opt;length:int:opt;", "clip:vnode;", trimCreate, 0, plugin);
    vspapi->registerFunction("Reverse", "clip:vnode;", "clip:vnode;", reverseCreate, 0, plugin);
    vspapi->registerFunction("Loop", "clip:vnode;times:int:opt;", "clip:vnode;", loopCreate, 0, plugin);
    vspapi->registerFunction("Interleave", "clips:vnode[];extend:int:opt;mismatch:int:opt;modify_duration:int:opt;", "clip:vnode;", interleaveCreate, 0, plugin);
    vspapi->registerFunction("SelectEvery", "clip:vnode;cycle:int;offsets:int[];modify_duration:int:opt;", "clip:vnode;", selectEveryCreate, 0, plugin);
    vspapi->registerFunction("Splice", "clips:vnode[];mismatch:int:opt;", "clip:vnode;", spliceCreate, 0, plugin);
    vspapi->registerFunction("DuplicateFrames", "clip:vnode;frames:int[];", "clip:vnode;", duplicateFramesCreate, 0, plugin);
    vspapi->registerFunction("DeleteFrames", "clip:vnode;frames:int[];", "clip:vnode;", deleteFramesCreate, 0, plugin);
    vspapi->registerFunction("FreezeFrames", "clip:vnode;first:int[]:empty;last:int[]:empty;replacement:int[]:empty;", "clip:vnode;", freezeFramesCreate, 0, plugin);
}
