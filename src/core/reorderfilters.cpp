/*
* Copyright (c) 2012-2025 Fredrik Mellbin
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

/* videoInfoToString leaves out the frame rate, so a mismatch in it would otherwise name two
   clips that print the same. */
static std::string mismatchedClipToString(const VSVideoInfo *vi, const MismatchInfo &info, const VSAPI *vsapi) {
    std::string s = videoInfoToString(vi, vsapi);
    if (info.differentFrameRate)
        s += (vi->fpsNum && vi->fpsDen) ? " at " + std::to_string(vi->fpsNum) + "/" + std::to_string(vi->fpsDen) + " fps" : " at a variable frame rate";
    return s;
}

/* Every filter here is a frame map: it outputs its input frames as they are, and at most changes
   their properties. This scales a frame's duration by mul / div, the change of frame rate the
   filters keeping the clip's length in time make. */
static void scaleDuration(VSMap *props, int64_t mul, int64_t div, const VSAPI *vsapi) {
    int errNum, errDen;
    int64_t durationNum = vsapi->mapGetInt(props, "_DurationNum", 0, &errNum);
    int64_t durationDen = vsapi->mapGetInt(props, "_DurationDen", 0, &errDen);
    if (!errNum && !errDen) {
        muldivRational(&durationNum, &durationDen, mul, div);
        vsapi->mapSetInt(props, "_DurationNum", durationNum, maReplace);
        vsapi->mapSetInt(props, "_DurationDen", durationDen, maReplace);
    }
}

//////////////////////////////////////////
// Trim

typedef struct {
    int first;
} TrimDataExtra;

typedef SingleNodeData<TrimDataExtra> TrimData;

static int VS_CC trimMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    return n + reinterpret_cast<TrimData *>(instanceData)->first;
}

static void VS_CC trimCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TrimData> d(new TrimData(vsapi));
    int err;
    int trimlen;

    d->first = vsapi->mapGetIntSaturated(in, "first", 0, &err);
    bool firstset = !err;
    int last = vsapi->mapGetIntSaturated(in, "last", 0, &err);
    bool lastset = !err;
    int length = vsapi->mapGetIntSaturated(in, "length", 0, &err);
    bool lengthset = !err;

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);

    if (d->first < 0)
        RETERROR("Trim: invalid first frame specified (less than 0)");

    if (d->first >= vi.numFrames)
        RETERROR("Trim: invalid first frame specified (beyond clip end)");

    if (lastset && lengthset)
        RETERROR("Trim: both last frame and length specified");

    if (lastset && last < d->first)
        RETERROR("Trim: invalid last frame specified (last is less than first)");

    if (lengthset && length < 1)
        RETERROR("Trim: invalid length specified (less than 1)");

    if ((lastset && last >= vi.numFrames) || (lengthset && length > vi.numFrames - d->first))
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

    VSFilterDependency deps[] = {{d->node, (d->first == 0) ? rpStrictSpatial : rpNoFrameReuse}};
    createFrameMap(out, "Trim", &vi, deps, 1, trimMapFrame, nullptr, filterFree<TrimData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// Interleave

typedef struct {
    VSVideoInfo vi;
    int numclips;
    int modifyDuration;
} InterleaveDataExtra;

typedef VariableNodeData<InterleaveDataExtra> InterleaveData;


static int VS_CC interleaveMapFrame(int n, int *input, void *instanceData) {
    const InterleaveData *d = reinterpret_cast<const InterleaveData *>(instanceData);
    *input = n % d->numclips;
    return n / d->numclips;
}

static int VS_CC interleaveBeginFrame([[maybe_unused]] int n, [[maybe_unused]] const VSMap *const *inputProps, VSMap *outputProps, [[maybe_unused]] void **frameState, void *instanceData, [[maybe_unused]] char *errorMessage, [[maybe_unused]] int errorMessageSize, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    scaleDuration(outputProps, 1, reinterpret_cast<const InterleaveData *>(instanceData)->numclips, vsapi);
    return 1;
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

        const ClipResidencyResult residency = residencyOfClips(d->nodes.data(), d->numclips, vsapi);
        if (residency.kind == ClipResidency::Mixed)
            RETERROR(residencyMismatchError("Interleave", residency.mixedAt).c_str());

        MismatchInfo mminfo = findCommonVi(d->nodes.data(), d->numclips, &d->vi, vsapi);
        if (!mminfo.match && !mismatch)
            RETERROR(("Interleave: clips are mismatched in " + mismatchToText(mminfo) + " starting at clip #" + std::to_string(mminfo.clipnum) + ", passed " + mismatchedClipToString(vsapi->getVideoInfo(d->nodes[mminfo.clipnum - 1]), mminfo, vsapi) + " and " + mismatchedClipToString(vsapi->getVideoInfo(d->nodes[mminfo.clipnum]), mminfo, vsapi)).c_str());

        bool overflow = false;

        if (extend) {
            if (d->vi.numFrames > INT_MAX / d->numclips)
                overflow = true;
            d->vi.numFrames *= d->numclips;
        } else {
            // this is exactly how avisynth does it
            d->vi.numFrames = 0;
            for (int i = 0; i < d->numclips; i++) {
                int clipFrames = vsapi->getVideoInfo(d->nodes[i])->numFrames;
                if (clipFrames > ((INT_MAX - i - 1) / d->numclips + 1))
                    overflow = true;
                else
                    d->vi.numFrames = std::max(d->vi.numFrames, (clipFrames - 1) * d->numclips + i + 1);
            }
        }

        if (overflow)
            RETERROR("Interleave: resulting clip is too long");

        if (d->modifyDuration)
            muldivRational(&d->vi.fpsNum, &d->vi.fpsDen, d->numclips, 1);

        // frames requested per input clip, only clips at least this long never get a repeated clamped request
        int maxNumFrames = (d->vi.numFrames + d->numclips - 1) / d->numclips;

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < d->numclips; i++)
            deps.push_back({d->nodes[i], (maxNumFrames <= vsapi->getVideoInfo(d->nodes[i])->numFrames) ? rpNoFrameReuse : rpFrameReuseLastOnly});
        createFrameMap(out, "Interleave", &d->vi, deps.data(), d->numclips, interleaveMapFrame, d->modifyDuration ? interleaveBeginFrame : nullptr, filterFree<InterleaveData>, d.get(), core, vsapi);
        d.release();
    }
}

//////////////////////////////////////////
// Reverse

typedef SingleNodeData<VIPointerData> ReverseData;

static int VS_CC reverseMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    return std::max(reinterpret_cast<ReverseData *>(instanceData)->vi->numFrames - n - 1, 0);
}

static void VS_CC reverseCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<ReverseData> d(new ReverseData(vsapi));

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    d->vi = vsapi->getVideoInfo(d->node);

    VSFilterDependency deps[] = {{ d->node, rpNoFrameReuse }};
    createFrameMap(out, "Reverse", d->vi, deps, 1, reverseMapFrame, nullptr, filterFree<ReverseData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// Loop

typedef SingleNodeData<VIPointerData> LoopData;

static int VS_CC loopMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    return n % reinterpret_cast<LoopData *>(instanceData)->vi->numFrames;
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
    createFrameMap(out, "Loop", &vi, deps, 1, loopMapFrame, nullptr, filterFree<LoopData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// SelectEvery

typedef struct {
    std::vector<int> offsets;
    std::vector<int> tailOffsets; // offsets surviving in the final partial cycle, in output order
    int cycle;
    int num;
    int fullCycles;
    bool modifyDuration;
} SelectEveryDataExtra;

typedef SingleNodeData<SelectEveryDataExtra> SelectEveryData;

static int VS_CC selectEveryMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    const SelectEveryData *d = reinterpret_cast<const SelectEveryData *>(instanceData);
    int cyc = n / d->num;
    if (cyc == d->fullCycles)
        return cyc * d->cycle + d->tailOffsets[n % d->num];
    return cyc * d->cycle + d->offsets[n % d->num];
}

static int VS_CC selectEveryBeginFrame([[maybe_unused]] int n, [[maybe_unused]] const VSMap *const *inputProps, VSMap *outputProps, [[maybe_unused]] void **frameState, void *instanceData, [[maybe_unused]] char *errorMessage, [[maybe_unused]] int errorMessageSize, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    const SelectEveryData *d = reinterpret_cast<const SelectEveryData *>(instanceData);
    scaleDuration(outputProps, d->cycle, d->num, vsapi);
    return 1;
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

    bool duplicateOffset = false;
    for (int i = 0; i < d->num; i++) {
        for (int j = i + 1; j < d->num; j++) {
            if (d->offsets[i] == d->offsets[j]) {
                duplicateOffset = true;
                break;
            }
        }
    }

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);
    int inputnframes = vi.numFrames;
    d->fullCycles = inputnframes / d->cycle;
    if (inputnframes) {
        int64_t numFrames = static_cast<int64_t>(d->fullCycles) * d->num;
        for (int i = 0; i < d->num; i++)
            if (d->offsets[i] < inputnframes % d->cycle) {
                numFrames++;
                d->tailOffsets.push_back(d->offsets[i]);
            }
        if (numFrames > INT_MAX)
            RETERROR("SelectEvery: resulting clip is too long");
        vi.numFrames = static_cast<int>(numFrames);
    }

    if (vi.numFrames == 0)
        RETERROR("SelectEvery: no frames to output, all offsets outside available frames");

    if (d->modifyDuration)
        muldivRational(&vi.fpsNum, &vi.fpsDen, d->num, d->cycle);

    VSFilterDependency deps[] = {{d->node, duplicateOffset ? rpGeneral : rpNoFrameReuse}};
    createFrameMap(out, "SelectEvery", &vi, deps, 1, selectEveryMapFrame, d->modifyDuration ? selectEveryBeginFrame : nullptr, filterFree<SelectEveryData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// Splice

typedef struct {
    std::vector<int> numframes;
    int numclips;
} SpliceDataExtra;

typedef VariableNodeData<SpliceDataExtra> SpliceData;

/* The last clip takes every frame past the end of the others. */
static int VS_CC spliceMapFrame(int n, int *input, void *instanceData) {
    const SpliceData *d = reinterpret_cast<const SpliceData *>(instanceData);
    int i = 0;
    for (; i < d->numclips - 1 && n >= d->numframes[i]; i++)
        n -= d->numframes[i];
    *input = i;
    return n;
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

        const ClipResidencyResult residency = residencyOfClips(d->nodes.data(), d->numclips, vsapi);
        if (residency.kind == ClipResidency::Mixed)
            RETERROR(residencyMismatchError("Splice", residency.mixedAt).c_str());

        findCommonVi(d->nodes.data(), d->numclips, &vi, vsapi);
        if (!mismatch) {
            const VSVideoInfo *first = vsapi->getVideoInfo(d->nodes[0]);
            for (int i = 1; i < d->numclips; i++) {
                const VSVideoInfo *cur = vsapi->getVideoInfo(d->nodes[i]);
                MismatchInfo bad = {};
                bad.differentFormat = first->format.colorFamily != cfUndefined && !isSameVideoFormat(&first->format, &cur->format);
                bad.differentDimensions = first->width && (first->width != cur->width || first->height != cur->height);
                if (bad.differentFormat || bad.differentDimensions)
                    RETERROR(("Splice: clips are mismatched in " + mismatchToText(bad) + " starting at clip #" + std::to_string(i) + ", passed " + videoInfoToString(vsapi->getVideoInfo(d->nodes[i - 1]), vsapi) + " and " + videoInfoToString(cur, vsapi)).c_str());
            }
        }

        d->numframes.resize(d->numclips);
        vi.numFrames = 0;

        for (int i = 0; i < d->numclips; i++) {
            d->numframes[i] = (vsapi->getVideoInfo(d->nodes[i]))->numFrames;

            if (d->numframes[i] > INT_MAX - vi.numFrames)
                RETERROR("Splice: the resulting clip is too long");
            vi.numFrames += d->numframes[i];
        }

        std::vector<VSFilterDependency> deps;
        for (int i = 0; i < d->numclips; i++)
            deps.push_back({ d->nodes[i], rpNoFrameReuse });
        createFrameMap(out, "Splice", &vi, deps.data(), d->numclips, spliceMapFrame, nullptr, filterFree<SpliceData>, d.get(), core, vsapi);
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

static int VS_CC duplicateFramesMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    const DuplicateFramesData *d = reinterpret_cast<const DuplicateFramesData *>(instanceData);
    for (int i = 0; i < d->num_dups; i++)
        if (n > d->dups[i])
            n--;
        else
            break;
    return n;
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

    if (d->num_dups > INT_MAX - vi.numFrames)
        RETERROR("DuplicateFrames: resulting clip is too long");

    vi.numFrames += d->num_dups;

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    createFrameMap(out, "DuplicateFrames", &vi, deps, 1, duplicateFramesMapFrame, nullptr, filterFree<DuplicateFramesData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// DeleteFrames

typedef struct {
    std::vector<int> del;
    int num_delete;
} DeleteFramesDataExtra;

typedef SingleNodeData<DeleteFramesDataExtra> DeleteFramesData;

static int VS_CC deleteFramesMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    const DeleteFramesData *d = reinterpret_cast<const DeleteFramesData *>(instanceData);
    for (int i = 0; i < d->num_delete; i++)
        if (n >= d->del[i])
            n++;
        else
            break;
    return n;
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
    createFrameMap(out, "DeleteFrames", &vi, deps, 1, deleteFramesMapFrame, nullptr, filterFree<DeleteFramesData>, d.get(), core, vsapi);
    d.release();
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

static int VS_CC freezeFramesMapFrame(int n, [[maybe_unused]] int *input, void *instanceData) {
    const FreezeFramesData *d = reinterpret_cast<const FreezeFramesData *>(instanceData);
    if (n >= d->freeze.front().first && n <= d->freeze.back().last)
        for (const auto &iter : d->freeze)
            if (n >= iter.first && n <= iter.last)
                return iter.replacement;
    return n;
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
    createFrameMap(out, "FreezeFrames", vi, deps, 1, freezeFramesMapFrame, nullptr, filterFree<FreezeFramesData>, d.get(), core, vsapi);
    d.release();
}

//////////////////////////////////////////
// Init

void reorderInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->registerFunction("Trim", "clip:vnode:all;first:int:opt;last:int:opt;length:int:opt;", "clip:vnode:all;", trimCreate, 0, plugin);
    vspapi->registerFunction("Reverse", "clip:vnode:all;", "clip:vnode:all;", reverseCreate, 0, plugin);
    vspapi->registerFunction("Loop", "clip:vnode:all;times:int:opt;", "clip:vnode:all;", loopCreate, 0, plugin);
    vspapi->registerFunction("Interleave", "clips:vnode[]:all;extend:int:opt;mismatch:int:opt;modify_duration:int:opt;", "clip:vnode:all;", interleaveCreate, 0, plugin);
    vspapi->registerFunction("SelectEvery", "clip:vnode:all;cycle:int;offsets:int[];modify_duration:int:opt;", "clip:vnode:all;", selectEveryCreate, 0, plugin);
    vspapi->registerFunction("Splice", "clips:vnode[]:all;mismatch:int:opt;", "clip:vnode:all;", spliceCreate, 0, plugin);
    vspapi->registerFunction("DuplicateFrames", "clip:vnode:all;frames:int[];", "clip:vnode:all;", duplicateFramesCreate, 0, plugin);
    vspapi->registerFunction("DeleteFrames", "clip:vnode:all;frames:int[];", "clip:vnode:all;", deleteFramesCreate, 0, plugin);
    vspapi->registerFunction("FreezeFrames", "clip:vnode:all;first:int[]:empty;last:int[]:empty;replacement:int[]:empty;", "clip:vnode:all;", freezeFramesCreate, 0, plugin);
}
