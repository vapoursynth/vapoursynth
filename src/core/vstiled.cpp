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

/* Tiled filters (createTiledVideoFilter), frame maps (createFrameMapVideoFilter) and plane maps
   (createPlaneMapVideoFilter).

   A tiled node's getFrame is tiledGetFrame below. For every frame it first plans a tree: starting
   at the requested node it walks the inputs, and each input that is itself tiled and fusable (see
   VSNode::fusableInto and addInput) joins the tree instead of being requested. Everything else is
   an external input of the tree and is requested as usual. Once those frames arrive, every node's
   beginFrame runs, inputs first, so the frame properties flow up the tree the way they would
   through real frames. Then each plane is produced on its own: the tree is pulled from the top in
   bands of rows, every fused node writing into a small ring buffer that its consumer reads, so the
   frames between the nodes never exist.

   A node several consumers read joins too when every one of their edges into it is in the tree,
   at the same frame: it is then planned once and produced once, into one ring that holds what all
   its readers need (planTree, measureReaders and ringRows). So the tree is really a graph without
   cycles, and only where nothing outside it reads the node again: one also read by a consumer
   outside the tree, or at another frame, keeps its cache and is an external, which computes it
   once as well.

   A frame map's output frame is a frame of one of its inputs, so the walk goes through a fusable
   frame map as well: it asks the frame map which input and frame that is and goes on from there,
   and the frame map joins the tree only to have its beginFrame run. Each node in the tree makes a
   frame of its own number, which is the requested one until a frame map changes it, and an
   external input is a node and a frame number. A frame map outside a tree is a node like any
   other, whose getFrame (frameMapGetFrame) requests the one frame it maps to.

   A plane map is seen through the same way: it joins the tree with all its inputs, at its own
   frame, and each plane it gives is followed into the input and plane it comes from. A plane can
   therefore be produced as another plane of its producer: output plane 1 may be plane 2 of a node
   further down, which only works because a plane map's planes keep their size. An input the plane
   map only takes properties from joins the tree as well, so a tiled node there runs its beginFrame
   and never its process. Outside a tree, planeMapGetFrame makes the frame from the input frames'
   planes, sharing them.

   Plane maps can also make one plane of a node part of several planes of the root: a plane given
   twice, or given once and read again by a node that gives another plane. The planes of the root
   are produced one after the other, each with rings of its own, so that node would compute its
   plane once for each of them. planTree finds such a node and makes it an external instead, whose
   frame holds the plane for all of them (listRepeated).

   A node passing a plane through hands on one of its inputs' plane, so for that plane it drops out
   of the tree: its consumer reads whatever produces that input's plane directly, and a plane the
   root passes through all the way from an external frame is shared with that frame. A frame map
   passes every plane through. A node whose plane nobody in the tree reads is not run for it.

   An input a tiled node reads through its first plane alone (VSTiledFilterDesc::firstPlaneInputs),
   such as MaskedMerge's mask standing in for subsampled chroma, is an external input whenever its
   rows are read: every plane of the node is handed that frame's first plane, whole, which a plane
   of another size could not get from a ring. So nothing about it depends on the plane being
   produced, and the runs of the planes, which happen one after the other, all read the same frame.
   A node passing every plane through reads only its properties, and then it may join.

   A tree runs on the one thread that produces the root's frame, which is what keeps its rows in
   that core's cache, and also means the inputs of a node with several fused inputs are computed
   one after the other. So when the pool has threads with nothing to do, such inputs are requested
   as frames instead and the pool computes them at the same time, each as a tree of its own.

   With no fused input the tree is the one node and each plane is a single call over the whole
   plane, the same work a non-tiled filter does. */

#include "vstiled.h"
#include "vscore.h"
#include "VSHelper4.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* What a tiled node's description has beyond the common part. */
struct VSTiledFilter : VSFusableFilter {
    static constexpr Kind thisKind = Kind::Tiled;

    VSVideoInfo vi = {};
    std::vector<int> haloTop; /* clamped to the height, which already means the whole plane */
    std::vector<int> haloBottom;
    /* Per input, what its ring takes at the smallest band. 0 when the halo makes that ring the
       whole plane: running such an input together with this node would save no memory traffic
       and only keep its planes around for longer, so it is always requested as a frame. */
    std::vector<size_t> ringBytes;
    /* Per input, whether any of its rows are read: all inputs when a plane is processed, else
       only those planes are passed through from. */
    std::vector<char> readsInput;
    /* Per input, whether every plane reads its first plane, whole (firstPlaneInputs). Such an
       input is requested as a frame whenever its rows are read. */
    std::vector<char> firstPlaneInput;
    int passthroughPlanes = 0;
    int passthroughInput[3] = {};
    VSTiledFilterProcess process = nullptr;
    VSTiledFilterEndFrame endFrame = nullptr;

    VSTiledFilter() : VSFusableFilter(thisKind) {
    }
};

/* A frame map's. */
struct VSFrameMapFilter : VSFusableFilter {
    static constexpr Kind thisKind = Kind::FrameMap;

    VSFilterMapFrame mapFrame = nullptr;

    VSFrameMapFilter() : VSFusableFilter(thisKind) {
    }
};

/* A plane map's. */
struct VSPlaneMapFilter : VSFusableFilter {
    static constexpr Kind thisKind = Kind::PlaneMap;

    VSVideoInfo vi = {};
    std::vector<char> readsInput; /* per input, whether an output plane comes from it */
    int planeInput[3] = {};
    int planeSource[3] = {};

    VSPlaneMapFilter() : VSFusableFilter(thisKind) {
    }
};

namespace {

/* Bounds the recursion that plans and runs a tree, frame maps included. A longer chain becomes
   several trees, which costs one frame between each. Nodes planned only for their properties are
   bounded the same way on their own, so that a long chain of them never takes the room of the
   nodes whose rows are read. */
constexpr int maxTreeNodes = 64;

/* What one band of rows of every ring of a plane may take together, which sets the band height.
   The rings round up to a power of two rows above the band and the rows read around it, twice
   this for 3x3 kernels. A per core L2 is shared by two threads and the external inputs and the
   output stream through it as well; measured, larger budgets were no faster for 4K frames and
   slower for frames small enough to stay in L3. */
constexpr size_t ringBudget = 128 * 1024;
constexpr int minBandRows = 8;
constexpr int maxBandRows = 64;

/* What the rings of one tree may take at the smallest band. Filters reading many rows around
   each band need rings of that many rows, and a long chain of them would otherwise hold far more
   memory per frame than the frames it replaces. */
constexpr size_t maxTreeRingBytes = 16 * 1024 * 1024;

/* VS_TILED_FUSION=0 makes every tiled node produce whole frames, to compare the two. Read once:
   it is a debugging switch, not something to change while a process runs. */
bool fusionEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("VS_TILED_FUSION");
        return !(value && !std::strcmp(value, "0"));
    }();
    return enabled;
}

/* The rows of the ring behind a reader that works on a band at a time: with rows [a, b) in work
   its input is produced up to b + haloBottom and a - haloTop is still needed, and b - a is at
   most a band. The whole plane when a ring would be no smaller. */
int ringRows(int band, int haloTop, int haloBottom, int height) {
    int needed = band + haloTop + haloBottom;
    int rows = 1;
    while (rows < needed && rows < height)
        rows *= 2;
    return std::min(rows, height);
}

ptrdiff_t ringStride(int width, const VSVideoFormat &format) {
    const ptrdiff_t alignment = VSFrame::alignment;
    return (static_cast<ptrdiff_t>(width) * format.bytesPerSample + (alignment - 1)) & ~(alignment - 1);
}

/* desc as the kind of description T is, null when it is another kind or there is none. */
template<typename T>
T *as(VSFusableFilter *desc) {
    return desc && desc->kind == T::thisKind ? static_cast<T *>(desc) : nullptr;
}

/* Which input frame n of a frame map comes from, -1 when mapFrame names one that does not exist,
   and in frame the frame of it, clamped to that input. */
int mapFrame(const VSFrameMapFilter *map, int n, int &frame) {
    int input = 0;
    frame = map->mapFrame ? map->mapFrame(n, &input, map->instanceData) : n;
    if (input < 0 || static_cast<size_t>(input) >= map->inputs.size())
        return -1;
    frame = map->inputs[input]->clampFrameNumber(frame);
    return input;
}

struct PlanNode {
    VSNode *node = nullptr;
    VSFusableFilter *desc = nullptr; /* the node's description, of whichever kind */
    int frame = 0; /* the number of the frame the node makes */
    /* Per input: the plan index of a fused node, or -(external slot + 1). A frame map has one, the
       input its frame comes from. */
    std::vector<int> inputs;
    std::unique_ptr<VSMap> props; /* the output properties of a fused node with a beginFrame */
    const VSMap *result = nullptr; /* the output properties once begun: props, the root's frame's, or the input's */
    void *frameState = nullptr;
    int64_t time = 0; /* nanoseconds spent in its callbacks, only kept when filter timing is on */
    /* The consumer edges of the tree reaching the node, and for one several consumers read the
       number it has in all (VSNode::sharedConsumerEdges), else 0. A shared node only stays in the
       tree when the two agree; see planTree. */
    size_t edges = 0;
    size_t consumerEdges = 0;
    bool read = false; /* whether anything in the tree reads its rows, not only its properties */
    bool begun = false;
};

struct Plan {
    VSCore *core = nullptr;
    std::vector<PlanNode> nodes; /* inputs before their consumer, the root last */
    std::vector<std::pair<VSNode *, int>> externals; /* a node and the number of its frame */
    std::vector<const VSFrame *> frames; /* the externals' frames once they arrived */
    int planned = 0; /* nodes whose rows are read, see maxTreeNodes */
    int propsOnly = 0; /* nodes only planned for their properties, see maxTreeNodes */
    size_t ringBytes = 0; /* of the inputs fused so far, see maxTreeRingBytes */
    size_t spareThreads = 0; /* pool threads with nothing to do that this walk has not used */
    size_t spareThreadsSample = 0; /* how many there were, asked for when first needed */
    bool spareThreadsKnown = false;
    /* What earlier walks of planTree found: frames of nodes the tree cannot share, as a consumer
       outside it or at another frame reads them as well, frames of shared nodes that something
       reads although a walk first reached them for their properties alone, and frames of nodes a
       plane of which several planes of the root would be made from. */
    std::vector<std::pair<VSNode *, int>> unshared;
    std::vector<std::pair<VSNode *, int>> read;
    std::vector<std::pair<VSNode *, int>> repeated;
    bool walkAgain = false;

    ~Plan() {
        for (const VSFrame *frame : frames)
            if (frame)
                const_cast<VSFrame *>(frame)->release();
    }
};

int addExternal(Plan &plan, VSNode *node, int frame) {
    const std::pair<VSNode *, int> external(node, node->clampFrameNumber(frame));
    auto it = std::find(plan.externals.begin(), plan.externals.end(), external);
    if (it == plan.externals.end())
        it = plan.externals.insert(plan.externals.end(), external);
    return -static_cast<int>(it - plan.externals.begin()) - 1;
}

int addTiled(Plan &plan, VSNode *node, int n, bool read);
void addInputs(Plan &plan, PlanNode &planNode, const std::vector<char> &reads, const std::vector<size_t> &ringBytes, bool read, const std::vector<char> *requested = nullptr);

int findNode(const Plan &plan, const VSNode *node, int frame) {
    for (size_t i = 0; i < plan.nodes.size(); i++)
        if (plan.nodes[i].node == node && plan.nodes[i].frame == frame)
            return static_cast<int>(i);
    return -1;
}

bool listed(const std::vector<std::pair<VSNode *, int>> &list, VSNode *node, int frame) {
    return std::find(list.begin(), list.end(), std::make_pair(node, frame)) != list.end();
}

/* Plans frame `frame` of source, an input of consumer, which is in the tree: source joins the tree
   when it is fusable and the limits allow, a frame or plane map along with what it takes its
   frames from, and otherwise the frame is an external. A source several consumers read can join
   as well, once: a consumer reaching it again adds an edge to the node already there, which
   planTree checks against the edges it has in all. ringBytes is what the ring of a tiled source
   takes, as the tiled node reading the input works it out (maps keep the planes' sizes), 0 when no
   tiled source ever joins for that input. fuse false makes the frame an external anyway, as does a
   plane of it that several planes of the root would be made from (plan.repeated). read false says
   nothing reads the rows of the input, only its properties: it then joins whatever ringBytes says,
   since it never gets a ring, and counts against the bound of such nodes. Returns what goes in
   consumer's inputs. */
int addInput(Plan &plan, VSNode *consumer, VSNode *source, int frame, size_t ringBytes, bool fuse, bool read) {
    fuse = fuse && !listed(plan.repeated, source, frame);
    if (fuse) {
        const int existing = findNode(plan, source, frame);
        if (existing >= 0) {
            PlanNode &node = plan.nodes[existing];
            node.edges++;
            /* Planned for its properties alone, so nothing below it was planned to have rows. */
            if (read && !node.read && !listed(plan.read, source, frame)) {
                plan.read.emplace_back(source, frame);
                plan.walkAgain = true;
            }
            return existing;
        }
    }

    read = read || listed(plan.read, source, frame);
    int &planned = read ? plan.planned : plan.propsOnly;
    bool fusable = false;
    size_t consumerEdges = 0;
    if (fuse && planned < maxTreeNodes) {
        if (source->fusableInto(consumer)) {
            fusable = true;
        } else if (!listed(plan.unshared, source, frame)) {
            consumerEdges = source->sharedConsumerEdges();
            fusable = consumerEdges != 0;
        }
    }
    if (fusable) {
        int index = -1;
        VSFusableFilter *desc = source->getFusable();
        if (VSPlaneMapFilter *planes = as<VSPlaneMapFilter>(desc)) {
            planned++;
            PlanNode planNode;
            planNode.node = source;
            planNode.desc = planes;
            planNode.frame = frame;
            addInputs(plan, planNode, planes->readsInput, std::vector<size_t>(planes->inputs.size(), ringBytes), read);
            plan.nodes.push_back(std::move(planNode));
            index = static_cast<int>(plan.nodes.size()) - 1;
        } else if (VSFrameMapFilter *map = as<VSFrameMapFilter>(desc)) {
            /* A frame map naming an input that does not exist is left to its own getFrame, which
               fails the frame saying so. */
            int mapped;
            const int input = mapFrame(map, frame, mapped);
            if (input >= 0) {
                planned++;
                PlanNode planNode;
                planNode.node = source;
                planNode.desc = map;
                planNode.frame = frame;
                planNode.inputs.push_back(addInput(plan, source, map->inputs[input], mapped, ringBytes, true, read));
                plan.nodes.push_back(std::move(planNode));
                index = static_cast<int>(plan.nodes.size()) - 1;
            }
        } else if (!read) {
            index = addTiled(plan, source, frame, false);
        } else if (ringBytes && plan.ringBytes + ringBytes <= maxTreeRingBytes) {
            plan.ringBytes += ringBytes;
            index = addTiled(plan, source, frame, true);
        }
        if (index >= 0) {
            PlanNode &node = plan.nodes[index];
            node.edges = 1;
            node.consumerEdges = consumerEdges;
            node.read = read;
            return index;
        }
    }
    return addExternal(plan, source, frame);
}

/* Whether frame `frame` of source, an input of consumer, would put a tiled node in the tree, which
   then takes time to compute there: source itself, or what the frame and plane maps it goes
   through lead to. Follows at most budget nodes, the room the tree has left; leaves out the ring
   limit addInput also applies. */
bool reachesTiled(VSNode *consumer, VSNode *source, int frame, size_t ringBytes, int budget) {
    for (; budget > 0 && source->fusableInto(consumer); budget--) {
        if (const VSPlaneMapFilter *planes = as<VSPlaneMapFilter>(source->getFusable())) {
            for (size_t i = 0; i < planes->inputs.size(); i++)
                if (planes->readsInput[i] && reachesTiled(source, planes->inputs[i], frame, ringBytes, budget - 1))
                    return true;
            return false;
        }
        const VSFrameMapFilter *map = as<VSFrameMapFilter>(source->getFusable());
        if (!map)
            return ringBytes != 0;
        int mapped;
        const int input = mapFrame(map, frame, mapped);
        if (input < 0)
            return false;
        consumer = source;
        source = map->inputs[input];
        frame = mapped;
    }
    return false;
}

/* Plans the inputs of planNode, a tiled node or a plane map, at its frame: reads says which of
   them are read, ringBytes what the ring of a tiled node joining for each takes, read whether
   anything reads the rows of planNode itself, without which nothing below it is read either, and
   requested, when given, which are requested as frames whatever they are when their rows are
   read; one only giving properties stays free to join. */
void addInputs(Plan &plan, PlanNode &planNode, const std::vector<char> &reads, const std::vector<size_t> &ringBytes, bool read, const std::vector<char> *requested) {
    const std::vector<VSNode *> &inputs = planNode.desc->inputs;
    VSNode *node = planNode.node;
    const int n = planNode.frame;
    const size_t numInputs = inputs.size();

    /* Fused inputs are computed one after the other on the thread producing the root's frame. If
       the pool has threads with nothing to do, request as many of several such inputs as it can
       compute at the same time instead; this thread is free as well while it waits for them. An
       input nothing is read from stays fused, where it costs nothing: as a frame it would be
       computed in full. Below a node nothing reads, no input is read. One whose maps lead to an
       external frame stays fused too, as that takes no time to get. Only a node reading several
       inputs can have such inputs at all. */
    std::vector<char> fuse(numInputs, fusionEnabled());
    if (requested && read)
        for (size_t i = 0; i < numInputs; i++)
            fuse[i] = fuse[i] && !((*requested)[i] && reads[i]);
    if (read && fusionEnabled() && std::count(reads.begin(), reads.end(), 1) > 1) {
        const int budget = maxTreeNodes - plan.planned;
        std::vector<char> branch(numInputs, 0);
        size_t branches = 0;
        for (size_t i = 0; i < numInputs; i++) {
            if (fuse[i] && reads[i] && reachesTiled(node, inputs[i], n, ringBytes[i], budget)) {
                branch[i] = 1;
                branches++;
            }
        }
        if (branches > 1) {
            if (!plan.spareThreadsKnown) {
                plan.spareThreadsSample = plan.core->threadPool->spareThreads();
                plan.spareThreads = plan.spareThreadsSample;
                plan.spareThreadsKnown = true;
            }
            if (plan.spareThreads > 0) {
                size_t request = std::min(branches, plan.spareThreads + 1);
                plan.spareThreads -= request - 1;
                for (size_t i = numInputs; i-- > 0 && request > 0;) {
                    if (branch[i]) {
                        fuse[i] = 0;
                        request--;
                    }
                }
            }
        }
    }

    planNode.inputs.reserve(numInputs);
    for (size_t i = 0; i < numInputs; i++)
        planNode.inputs.push_back(addInput(plan, node, inputs[i], n, ringBytes[i], fuse[i] != 0, read && reads[i]));
}

/* Plans frame n of node, a tiled node, and what it reads; read false when nothing reads its rows.
   Returns its plan index. */
int addTiled(Plan &plan, VSNode *node, int n, bool read) {
    (read ? plan.planned : plan.propsOnly)++;
    const VSTiledFilter *filter = as<VSTiledFilter>(node->getFusable());
    PlanNode planNode;
    planNode.node = node;
    planNode.desc = node->getFusable();
    planNode.frame = n;
    addInputs(plan, planNode, filter->readsInput, filter->ringBytes, read, &filter->firstPlaneInput);
    plan.nodes.push_back(std::move(planNode));
    return static_cast<int>(plan.nodes.size()) - 1;
}

/* Whether node hands plane `plane` on from an input as it is: a frame map from its one input, a
   plane map from the input and plane it takes it from, a tiled node from the input it passes it
   through from. Then input is that input's entry in node.inputs and plane which plane of it. A
   tiled node processing the plane makes it itself. */
bool handsOn(const PlanNode &node, int &plane, int &input) {
    if (as<VSFrameMapFilter>(node.desc)) {
        input = node.inputs[0];
    } else if (const VSPlaneMapFilter *planes = as<VSPlaneMapFilter>(node.desc)) {
        input = node.inputs[planes->planeInput[plane]];
        plane = planes->planeSource[plane];
    } else {
        const VSTiledFilter *filter = as<VSTiledFilter>(node.desc);
        if (!(filter->passthroughPlanes & (1 << plane)))
            return false;
        input = node.inputs[filter->passthroughInput[plane]];
    }
    return true;
}

/* Per plan node and plane of it, the planes of the root it is made for, a bit each. */
typedef std::vector<std::array<uint8_t, 3>> ProducedFor;

/* Sets bit for every tiled node and plane of it that plane `plane` of plan node index is made
   from, the ones resolve() makes producers for. Goes no further down from a node in stop, when
   that is given. */
void markProduced(const Plan &plan, int index, int plane, uint8_t bit, ProducedFor &producedFor, const std::vector<char> *stop) {
    const PlanNode &node = plan.nodes[index];
    int input;
    if (handsOn(node, plane, input)) {
        if (input >= 0)
            markProduced(plan, input, plane, bit, producedFor, stop);
        return;
    }
    if (producedFor[index][plane] & bit)
        return;
    producedFor[index][plane] |= bit;
    if (stop && (*stop)[index])
        return;
    const VSTiledFilter *filter = as<VSTiledFilter>(node.desc);
    for (size_t i = 0; i < node.inputs.size(); i++)
        if (node.inputs[i] >= 0 && !filter->firstPlaneInput[i])
            markProduced(plan, node.inputs[i], plane, bit, producedFor, stop);
}

ProducedFor producedFor(const Plan &plan, const std::vector<char> *stop) {
    const int root = static_cast<int>(plan.nodes.size()) - 1;
    ProducedFor result(plan.nodes.size(), std::array<uint8_t, 3>{});
    for (int p = 0; p < plan.nodes[root].node->getVideoInfo().format.numPlanes; p++)
        markProduced(plan, root, p, static_cast<uint8_t>(1 << p), result, stop);
    return result;
}

bool forSeveral(const std::array<uint8_t, 3> &planes) {
    for (uint8_t rootPlanes : planes)
        if (rootPlanes & (rootPlanes - 1))
            return true;
    return false;
}

/* Lists in plan.repeated the nodes with a plane that more than one plane of the root is made from,
   and says whether there are any. Only plane maps bring that about: without one, plane p of the
   root is made from plane p of everything. The planes of the root are produced one after the
   other, so such a node would compute that plane once for each of them, and write its rows as
   often. As an external it computes it once, into a frame. What is repeated below such a node
   leaves the tree with it, and may be repeated through it alone, so a node is only listed when the
   ways to it that pass no other repeated node already make it one; any left over show in the next
   walk. The repeated node nearest the root always qualifies, as nothing repeated reads it. */
bool listRepeated(Plan &plan) {
    bool planeMaps = false;
    for (const PlanNode &node : plan.nodes)
        planeMaps = planeMaps || node.desc->kind == VSFusableFilter::Kind::PlaneMap;
    if (!planeMaps)
        return false;
    const ProducedFor all = producedFor(plan, nullptr);
    std::vector<char> repeated(plan.nodes.size(), 0);
    bool any = false;
    for (size_t i = 0; i < plan.nodes.size(); i++)
        any = (repeated[i] = forSeveral(all[i])) || any;
    if (!any)
        return false;
    const ProducedFor direct = producedFor(plan, &repeated);
    for (size_t i = 0; i < plan.nodes.size(); i++)
        if (repeated[i] && forSeveral(direct[i]))
            plan.repeated.emplace_back(plan.nodes[i].node, plan.nodes[i].frame);
    return true;
}

/* Plans frame n of root, a tiled node. A node several consumers read joins the tree once, every
   consumer reaching it adding an edge (addInput), which is only right when the tree holds all of
   its edges at that frame: a consumer outside the tree, or one reading another frame of it, would
   compute those rows again, where its cache computes them once. So a walk ends with the edges of
   every shared node counted, and a node short of its edges is an external in a new walk, which can
   leave others short in turn. A shared node first reached for its properties alone and then by a
   reader takes a new walk as well, to be planned as read from the start. So does a node with a
   plane several planes of the root are made from, which is an external in the new walk
   (listRepeated); they are looked for once the walk is otherwise final. A new walk follows only from
   a node newly listed in plan.unshared, plan.read or plan.repeated, so there are at most as many
   as the tree has such nodes, and usually none; the spare threads are sampled once for all of
   them. */
void planTree(Plan &plan, VSNode *root, int n) {
    for (;;) {
        plan.nodes.clear();
        plan.externals.clear();
        plan.planned = 0;
        plan.propsOnly = 0;
        plan.ringBytes = 0;
        plan.spareThreads = plan.spareThreadsSample;
        plan.walkAgain = false;
        plan.nodes[addTiled(plan, root, n, true)].read = true;
        bool again = plan.walkAgain;
        for (const PlanNode &node : plan.nodes) {
            if (node.consumerEdges && node.edges != node.consumerEdges) {
                plan.unshared.emplace_back(node.node, node.frame);
                again = true;
            }
        }
        if (!again && !listRepeated(plan))
            return;
    }
}

/* Splits the time a frame takes between the nodes that ran for it, when filter timing is on:
   each lap goes to the node whose callback just returned. */
class Stopwatch {
    std::chrono::steady_clock::time_point last;
public:
    void start() {
        last = std::chrono::steady_clock::now();
    }

    int64_t lap() {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
        last = now;
        return elapsed;
    }
};

/* Where the rows of a plane come from: a producer, or -(external slot + 1) and which plane of that
   frame they are. */
struct Source {
    int index;
    int plane;
};

/* A node producing one plane, and where its rows go. */
struct Producer {
    int planIndex = 0;
    const VSTiledFilter *filter = nullptr; /* the plan node's */
    int plane = 0; /* of the node, which plane maps can make another than the output's */
    std::vector<Source> inputs;
    std::vector<VSTileBuffer> src;
    VSTileBuffer dst = {};
    size_t offset = 0; /* of its ring in the frame's ring memory */
    int ringRows = 0;
    /* Where its readers work, which sizes its ring (see ringRows): rows from the start of the band
       the root works on, lead how far past the end of that band they have it produced, back where
       the rows they still need begin. */
    int readers = 0;
    int64_t lead = 0;
    int64_t back = 0;
    int done = 0; /* rows written so far */
};

struct PlaneRun {
    Plan *plan = nullptr;
    Stopwatch *watch = nullptr; /* null unless filter timing is on */
    std::vector<Producer> producers;
    std::vector<std::array<int, 3>> producerOf; /* per plan node and plane of it, its producer or -1 */
    int plane = 0;
    int width = 0;
    int height = 0;
    int band = 0;
};

/* Where plane `plane` of plan node index comes from: its producer, made along with those of its
   inputs, or the external frame and plane it is passed through from. */
Source resolve(PlaneRun &run, int index, int plane) {
    const PlanNode &node = run.plan->nodes[index];
    int input;
    if (handsOn(node, plane, input))
        return input < 0 ? Source{ input, plane } : resolve(run, input, plane);

    /* A node several consumers in the tree read has one producer, which they all read. */
    if (run.producerOf.empty())
        run.producerOf.assign(run.plan->nodes.size(), { -1, -1, -1 });
    if (run.producerOf[index][plane] >= 0)
        return { run.producerOf[index][plane], plane };
    const VSTiledFilter *filter = as<VSTiledFilter>(node.desc);
    std::vector<Source> inputs;
    inputs.reserve(node.inputs.size());
    for (size_t i = 0; i < node.inputs.size(); i++) {
        const int nodeInput = node.inputs[i];
        /* An input read through its first plane is always an external when the node's rows are
           read, addInputs requesting it as a frame, and every plane gets that frame's first
           plane. */
        if (filter->firstPlaneInput[i]) {
            assert(nodeInput < 0);
            inputs.push_back(Source{ nodeInput, 0 });
        } else {
            inputs.push_back(nodeInput < 0 ? Source{ nodeInput, plane } : resolve(run, nodeInput, plane));
        }
    }
    Producer producer;
    producer.planIndex = index;
    producer.filter = filter;
    producer.plane = plane;
    producer.inputs = std::move(inputs);
    run.producers.push_back(std::move(producer));
    run.producerOf[index][plane] = static_cast<int>(run.producers.size()) - 1;
    return { run.producerOf[index][plane], plane };
}

/* Where the readers of every producer work, as Producer::lead and back describe it. top, which
   writes the frame, works on the root's band itself. One pass from the top suffices, as a producer
   comes after everything it reads in run.producers: a reader's own lead is settled before it is
   used. */
void measureReaders(PlaneRun &run, int top) {
    for (Producer &producer : run.producers) {
        producer.readers = 0;
        producer.lead = 0;
        producer.back = 0;
    }
    for (size_t i = run.producers.size(); i-- > 0;) {
        const Producer &reader = run.producers[i];
        if (static_cast<int>(i) != top && !reader.readers)
            continue;
        const VSTiledFilter *filter = reader.filter;
        for (size_t j = 0; j < reader.inputs.size(); j++) {
            if (reader.inputs[j].index < 0)
                continue;
            Producer &input = run.producers[reader.inputs[j].index];
            const int64_t lead = reader.lead + filter->haloBottom[j];
            const int64_t back = reader.lead - filter->haloTop[j];
            input.lead = input.readers ? std::max(input.lead, lead) : lead;
            input.back = input.readers ? std::min(input.back, back) : back;
            input.readers++;
        }
    }
}

/* The rows of a producer's ring, the whole plane when a ring would be no smaller. A reader works
   on a band of rows [a, b) at a time, having its input produced up to b + haloBottom while it still
   needs a - haloTop, so one reader needs band + haloTop + haloBottom rows, lead - back. Several
   readers each have their own window, and once all work a band apart the earliest row any of them
   needs is lead - back rows behind the furthest one produced. Not in the first band, though: every
   reader starts at row 0 there, wherever it works later, while the one ahead has its input
   produced to band + lead, so rows from 0 are needed. Rows above the plane do not exist, and
   band + lead - min(back, 0) covers both. */
int ringRows(const Producer &producer, int band, int height) {
    const int64_t needed = band + producer.lead - (producer.readers > 1 ? std::min<int64_t>(producer.back, 0) : producer.back);
    int64_t rows = 1;
    while (rows < needed && rows < height)
        rows *= 2;
    return static_cast<int>(std::min<int64_t>(rows, height));
}

/* Picks the band height and lays out the rings of every producer but top, which writes the
   frame. Returns the bytes the rings take. */
size_t layoutPlane(PlaneRun &run, int top) {
    measureReaders(run, top);
    size_t rowBytes = 0;
    for (size_t i = 0; i < run.producers.size(); i++) {
        if (static_cast<int>(i) == top)
            continue;
        Producer &producer = run.producers[i];
        producer.dst.stride = ringStride(run.width, producer.filter->vi.format);
        rowBytes += static_cast<size_t>(producer.dst.stride);
    }

    if (!rowBytes) {
        run.band = run.height;
        return 0;
    }

    int band = minBandRows;
    while (band < maxBandRows && static_cast<size_t>(band) * 2 * rowBytes <= ringBudget)
        band *= 2;
    run.band = band;

    size_t total = 0;
    for (size_t i = 0; i < run.producers.size(); i++) {
        if (static_cast<int>(i) == top)
            continue;
        Producer &producer = run.producers[i];
        producer.ringRows = ringRows(producer, band, run.height);
        producer.dst.mask = producer.ringRows == run.height ? ~0u : static_cast<unsigned>(producer.ringRows - 1);
        producer.offset = total;
        total += (static_cast<size_t>(producer.ringRows) * static_cast<size_t>(producer.dst.stride) + 63) & ~static_cast<size_t>(63);
    }
    return total;
}

void placePlane(PlaneRun &run, int top, uint8_t *rings, VSFrame *dst, const std::vector<const VSFrame *> &frames) {
    for (size_t i = 0; i < run.producers.size(); i++) {
        Producer &producer = run.producers[i];
        if (static_cast<int>(i) == top)
            producer.dst = { dst->getWritePtr(run.plane), dst->getStride(run.plane), ~0u };
        else
            producer.dst.ptr = rings + producer.offset;
    }
    for (Producer &producer : run.producers) {
        producer.src.clear();
        for (const Source &input : producer.inputs) {
            if (input.index < 0) {
                const VSFrame *frame = frames[-input.index - 1];
                producer.src.push_back({ const_cast<uint8_t *>(frame->getReadPtr(input.plane)), frame->getStride(input.plane), ~0u });
            } else {
                producer.src.push_back(run.producers[input.index].dst);
            }
        }
    }
}

/* Has producer index write its rows up to rows, a band at a time, its inputs each just ahead of it. */
void produce(PlaneRun &run, int index, int rows) {
    Producer &producer = run.producers[index];
    PlanNode &node = run.plan->nodes[producer.planIndex];
    const VSTiledFilter *filter = producer.filter;
    while (producer.done < rows) {
        int end = std::min(rows, producer.done + run.band);
        for (size_t i = 0; i < producer.inputs.size(); i++)
            if (producer.inputs[i].index >= 0)
                produce(run, producer.inputs[i].index, std::min(run.height, end + filter->haloBottom[i]));
        VSTileRegion region = { producer.plane, run.width, run.height, producer.done, end };
        filter->process(&region, producer.src.data(), &producer.dst, node.frameState, filter->instanceData);
        if (run.watch)
            node.time += run.watch->lap();
        producer.done = end;
    }
}

/* Runs the beginFrame of desc, which has one, for frame n of node. False when it fails, with what
   it said, after the node's name, in error. */
bool beginFrame(const VSFusableFilter *desc, VSNode *node, int n, const VSMap *const *inputProps, VSMap *outputProps, void **frameState, std::string &error, VSCore *core, const VSAPI *vsapi) {
    char message[1024] = {};
    if (desc->beginFrame(n, inputProps, outputProps, frameState, desc->instanceData, message, sizeof(message), core, vsapi))
        return true;
    message[sizeof(message) - 1] = 0;
    error = node->getName() + ": " + (message[0] ? message : "beginFrame failed");
    return false;
}

/* A frame's rings, from the core's allocator so that they count against its memory limit like
   any frame, and given back as soon as the frame is done. */
class RingMemory {
    vs::MemoryUse &memory;
public:
    uint8_t *const data;

    RingMemory(vs::MemoryUse &memory, size_t bytes) : memory(memory), data(bytes ? memory.allocate(bytes) : nullptr) {
    }

    RingMemory(const RingMemory &) = delete;
    RingMemory &operator=(const RingMemory &) = delete;

    ~RingMemory() {
        if (data)
            memory.deallocate(data);
    }
};

const VSFrame *VS_CC tiledGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    VSNode *rootNode = frameCtx->key.first;
    assert(rootNode->getFusable() == static_cast<VSFusableFilter *>(instanceData));
    const VSTiledFilter *root = as<VSTiledFilter>(static_cast<VSFusableFilter *>(instanceData));

    if (activationReason == arInitial) {
        std::unique_ptr<Plan> plan(new Plan);
        plan->core = core;
        planTree(*plan, rootNode, n);
        for (const auto &external : plan->externals)
            vsapi->requestFrameFilter(external.second, external.first, frameCtx);
        *frameData = plan.release();
        return nullptr;
    }

    std::unique_ptr<Plan> plan(static_cast<Plan *>(*frameData));
    *frameData = nullptr;
    if (activationReason != arAllFramesReady)
        return nullptr;

    plan->frames.reserve(plan->externals.size());
    for (const auto &external : plan->externals)
        plan->frames.push_back(vsapi->getFrameFilter(external.second, external.first, frameCtx));

    const VSVideoFormat &format = root->vi.format;
    const int numPlanes = format.numPlanes;
    const int rootIndex = static_cast<int>(plan->nodes.size()) - 1;

    /* With filter timing on, the time spent in each fused node's callbacks is credited to that
       node and left out of the root's, whose own time is whatever remains of this call. */
    const bool timing = core->getNodeTiming();
    Stopwatch watch;

    /* Every plane is resolved before the frame exists, so that one coming unchanged from an
       external frame can be shared with it. */
    PlaneRun runs[3];
    int sources[3] = {};
    const VSFrame *planeSrc[3] = {};
    int planeIndex[3] = { 0, 1, 2 };
    for (int p = 0; p < numPlanes; p++) {
        PlaneRun &run = runs[p];
        run.plan = plan.get();
        run.watch = timing ? &watch : nullptr;
        run.plane = p;
        run.width = root->vi.width >> (p ? format.subSamplingW : 0);
        run.height = root->vi.height >> (p ? format.subSamplingH : 0);
        const Source source = resolve(run, rootIndex, p);
        sources[p] = source.index;
        if (source.index < 0) {
            planeSrc[p] = plan->frames[-source.index - 1];
            planeIndex[p] = source.plane;
        }
    }

    VSFrame *dst = new VSFrame(format, root->vi.width, root->vi.height, planeSrc, planeIndex, nullptr, core);

    std::string error;
    std::vector<const VSMap *> inputProps;
    if (timing)
        watch.start();
    for (int i = 0; i <= rootIndex; i++) {
        PlanNode &node = plan->nodes[i];
        const bool begins = node.desc->beginFrame != nullptr;
        inputProps.clear();
        for (int input : node.inputs)
            inputProps.push_back(input < 0 ? &plan->frames[-input - 1]->getConstProperties() : plan->nodes[input].result);
        VSMap *props = nullptr;
        if (i == rootIndex) {
            dst->getProperties() = *inputProps[0];
            props = &dst->getProperties();
        } else if (begins) {
            node.props.reset(new VSMap(inputProps[0], true));
            props = node.props.get();
        }
        node.result = props ? props : inputProps[0];
        if (begins) {
            /* A map has no per frame state, see VSFilterBeginFrame. */
            void **frameState = node.desc->kind == VSFusableFilter::Kind::Tiled ? &node.frameState : nullptr;
            if (!beginFrame(node.desc, node.node, node.frame, inputProps.data(), props, frameState, error, core, vsapi))
                break;
            if (timing)
                node.time += watch.lap();
        }
        node.begun = true;
    }

    if (error.empty()) {
        size_t ringBytes = 0;
        for (int p = 0; p < numPlanes; p++)
            if (sources[p] >= 0)
                ringBytes = std::max(ringBytes, layoutPlane(runs[p], sources[p]));

        RingMemory rings(*core->memory, ringBytes);
        if (ringBytes && !rings.data) {
            error = rootNode->getName() + ": could not allocate " + std::to_string(ringBytes) + " bytes of row buffers";
        } else {
            for (int p = 0; p < numPlanes; p++) {
                if (sources[p] < 0)
                    continue;
                placePlane(runs[p], sources[p], rings.data, dst, plan->frames);
                if (timing)
                    watch.lap(); /* the setup is the root's */
                produce(runs[p], sources[p], runs[p].height);
            }
        }
    }

    for (PlanNode &node : plan->nodes) {
        const VSTiledFilter *filter = as<VSTiledFilter>(node.desc);
        if (node.begun && filter && filter->endFrame)
            filter->endFrame(node.frameState, filter->instanceData);
    }

    if (timing) {
        for (int i = 0; i < rootIndex; i++) {
            plan->nodes[i].node->addProcessingTime(plan->nodes[i].time);
            frameCtx->fusedTime += plan->nodes[i].time;
        }
    }

    if (!error.empty()) {
        dst->release();
        vsapi->setFilterError(error.c_str(), frameCtx);
        return nullptr;
    }

    return dst;
}

/* The free function of every node made here: the filter's own, then the description. */
void VS_CC fusableFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    VSFusableFilter *desc = static_cast<VSFusableFilter *>(instanceData);
    if (desc->freeFunc)
        desc->freeFunc(desc->instanceData, core, vsapi);
    delete desc;
}

/* A frame map's frame when no tiled node takes it into its tree: the input frame it maps to, or a
   copy of it with the properties beginFrame sets. */
const VSFrame *VS_CC frameMapGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const VSFrameMapFilter *map = as<VSFrameMapFilter>(static_cast<VSFusableFilter *>(instanceData));

    if (activationReason == arInitial) {
        int frame;
        const int input = mapFrame(map, n, frame);
        if (input < 0) {
            vsapi->setFilterError((frameCtx->key.first->getName() + ": mapFrame named an input that does not exist for frame " + std::to_string(n)).c_str(), frameCtx);
            return nullptr;
        }
        frameData[0] = map->inputs[input];
        frameData[1] = reinterpret_cast<void *>(static_cast<intptr_t>(frame));
        vsapi->requestFrameFilter(frame, map->inputs[input], frameCtx);
        return nullptr;
    }

    if (activationReason != arAllFramesReady)
        return nullptr;

    const VSFrame *src = vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[1])), static_cast<VSNode *>(frameData[0]), frameCtx);
    if (!map->beginFrame)
        return src;

    VSFrame *dst = new VSFrame(*src);
    const VSMap *inputProps[] = { &src->getConstProperties() };
    std::string error;
    const bool ok = beginFrame(map, frameCtx->key.first, n, inputProps, &dst->getProperties(), nullptr, error, core, vsapi);
    const_cast<VSFrame *>(src)->release();
    if (!ok) {
        dst->release();
        vsapi->setFilterError(error.c_str(), frameCtx);
        return nullptr;
    }
    return dst;
}

/* A plane map's frame when no tiled node takes it into its tree: the input frames' planes, shared,
   with input 0's properties or what beginFrame makes of them. */
const VSFrame *VS_CC planeMapGetFrame(int n, int activationReason, void *instanceData, [[maybe_unused]] void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const VSPlaneMapFilter *planes = as<VSPlaneMapFilter>(static_cast<VSFusableFilter *>(instanceData));

    if (activationReason == arInitial) {
        for (VSNode *input : planes->inputs)
            vsapi->requestFrameFilter(n, input, frameCtx);
        return nullptr;
    }

    if (activationReason != arAllFramesReady)
        return nullptr;

    std::vector<const VSFrame *> frames;
    std::vector<const VSMap *> inputProps;
    frames.reserve(planes->inputs.size());
    inputProps.reserve(planes->inputs.size());
    for (VSNode *input : planes->inputs) {
        frames.push_back(vsapi->getFrameFilter(n, input, frameCtx));
        inputProps.push_back(&frames.back()->getConstProperties());
    }

    const VSFrame *planeSrc[3] = {};
    for (int p = 0; p < planes->vi.format.numPlanes; p++)
        planeSrc[p] = frames[planes->planeInput[p]];
    VSFrame *dst = new VSFrame(planes->vi.format, planes->vi.width, planes->vi.height, planeSrc, planes->planeSource, frames[0], core);

    std::string error;
    const bool ok = !planes->beginFrame || beginFrame(planes, frameCtx->key.first, n, inputProps.data(), &dst->getProperties(), nullptr, error, core, vsapi);
    for (const VSFrame *frame : frames)
        const_cast<VSFrame *>(frame)->release();
    if (!ok) {
        dst->release();
        vsapi->setFilterError(error.c_str(), frameCtx);
        return nullptr;
    }
    return dst;
}

/* What a creation function fails with: the reason, after the filter's name, in errorMessage. */
class Failure {
    const char *name;
    std::string &errorMessage;
public:
    Failure(const char *name, std::string &errorMessage) : name(name), errorMessage(errorMessage) {
    }

    VSNode *operator()(const std::string &reason) const {
        errorMessage = std::string(name) + ": " + reason;
        return nullptr;
    }
};

/* The dependencies of a node whose frame n is made from frame n of every input. An input shorter
   than the output repeats its last frame, which is no longer strictly spatial; such an input is
   simply never fused. */
std::vector<VSFilterDependency> sameFrameDependencies(VSNode *const *inputs, int numInputs, const VSVideoInfo *vi) {
    std::vector<VSFilterDependency> deps(numInputs);
    for (int i = 0; i < numInputs; i++)
        deps[i] = { inputs[i], inputs[i]->getVideoInfo().numFrames >= vi->numFrames ? rpStrictSpatial : rpFrameReuseLastOnly };
    return deps;
}

/* Makes the node of a description that passed its checks; the node owns it from then on. */
VSNode *makeNode(const char *name, const VSVideoInfo *vi, VSFilterGetFrame getFrame, const VSFilterDependency *deps, int numDeps, bool gpu, std::unique_ptr<VSFusableFilter> desc,
    VSCore *core, std::string &errorMessage) {
    try {
        VSNode *node = new VSNode(name, vi, getFrame, fusableFree, fmParallel, deps, numDeps, desc.get(), gpu, desc.get(), core);
        desc.release();
        return node;
    } catch (VSException &e) {
        errorMessage = e.what();
        return nullptr;
    }
}

} // namespace

VSNode *createFusableFilterNode(const char *name, const VSTiledFilterDesc *desc, VSCore *core, std::string &errorMessage) {
    const Failure fail(name, errorMessage);

    const VSVideoInfo *vi = desc->vi;
    if (!vi || !VSCore::isValidVideoInfo(*vi) || vi->format.colorFamily == cfUndefined || !vi->width || !vi->height)
        return fail("a tiled filter's output must have a constant format and size");

    const int numPlanes = vi->format.numPlanes;
    const int allPlanes = (1 << numPlanes) - 1;
    if (desc->passthroughPlanes & ~allPlanes)
        return fail("passthroughPlanes names a plane the output format does not have");
    if (!desc->process && desc->passthroughPlanes != allPlanes)
        return fail("process can only be NULL when every plane is passed through");
    if (desc->numInputs < 1 || !desc->inputs)
        return fail("a tiled filter needs at least one input");
    for (int p = 0; p < numPlanes; p++)
        if ((desc->passthroughPlanes & (1 << p)) && (desc->passthroughInput[p] < 0 || desc->passthroughInput[p] >= desc->numInputs))
            return fail("passthroughInput names an input that does not exist for plane " + std::to_string(p));
    if (desc->firstPlaneInputs < 0 || (desc->numInputs < 31 && (desc->firstPlaneInputs >> desc->numInputs)))
        return fail("firstPlaneInputs names an input that does not exist");

    for (int i = 0; i < desc->numInputs; i++) {
        VSNode *input = desc->inputs[i];
        const std::string which = "input " + std::to_string(i);
        if (!input || input->getNodeType() != mtVideo)
            return fail(which + " is not a video node");
        if (input->isGPUOutput())
            return fail(which + " is GPU resident, tiled filters run on the CPU");
        const VSVideoInfo &ivi = input->getVideoInfo();
        if (ivi.format.colorFamily == cfUndefined || ivi.width != vi->width || ivi.height != vi->height)
            return fail(which + " must have a constant format and the output's size");
        /* Read only through its first plane, which has the size of the output's first plane, as
           the frame sizes match; it gives no plane to pass through. */
        if (i < 31 && (desc->firstPlaneInputs & (1 << i))) {
            for (int p = 0; p < numPlanes; p++)
                if ((desc->passthroughPlanes & (1 << p)) && desc->passthroughInput[p] == i)
                    return fail("plane " + std::to_string(p) + " can't be passed through from " + which + ", which is read through its first plane");
            continue;
        }
        /* The planes read from this input are the processed ones and those passed through from it. */
        for (int p = 0; p < numPlanes; p++) {
            const bool passed = (desc->passthroughPlanes & (1 << p)) != 0;
            if (passed && desc->passthroughInput[p] != i)
                continue;
            if (p >= ivi.format.numPlanes || (p > 0 && (ivi.format.subSamplingW != vi->format.subSamplingW || ivi.format.subSamplingH != vi->format.subSamplingH)))
                return fail(which + " has no plane " + std::to_string(p) + " of the output plane's size, which the filter reads from it");
            /* The same rule newVideoFrame2 applies to sharing a plane. */
            if (passed && (ivi.format.sampleType != vi->format.sampleType || ivi.format.bytesPerSample != vi->format.bytesPerSample))
                return fail("plane " + std::to_string(p) + " can only be passed through from " + which + " when it stores its samples like the output");
        }
        if ((desc->haloTop && desc->haloTop[i] < 0) || (desc->haloBottom && desc->haloBottom[i] < 0))
            return fail("the halo of " + which + " is negative");
    }

    std::unique_ptr<VSTiledFilter> filter(new VSTiledFilter);
    filter->vi = *vi;
    filter->inputs.assign(desc->inputs, desc->inputs + desc->numInputs);
    filter->haloTop.resize(desc->numInputs);
    filter->haloBottom.resize(desc->numInputs);
    filter->ringBytes.resize(desc->numInputs);
    filter->firstPlaneInput.resize(desc->numInputs);
    for (int i = 0; i < desc->numInputs; i++) {
        /* Always a whole frame, so neither a halo nor a ring applies. */
        if (i < 31 && (desc->firstPlaneInputs & (1 << i))) {
            filter->firstPlaneInput[i] = 1;
            continue;
        }
        /* A halo reaching past the plane reads the whole plane, so clamping it to the height
           changes nothing but keeps the band and ring arithmetic far from overflowing. */
        filter->haloTop[i] = desc->haloTop ? std::min(desc->haloTop[i], vi->height) : 0;
        filter->haloBottom[i] = desc->haloBottom ? std::min(desc->haloBottom[i], vi->height) : 0;
        const int rows = ringRows(minBandRows, filter->haloTop[i], filter->haloBottom[i], vi->height);
        if (rows < vi->height)
            filter->ringBytes[i] = static_cast<size_t>(rows) * static_cast<size_t>(ringStride(vi->width, desc->inputs[i]->getVideoInfo().format));
    }
    filter->passthroughPlanes = desc->passthroughPlanes;
    filter->readsInput.assign(desc->numInputs, desc->passthroughPlanes != allPlanes);
    for (int p = 0; p < numPlanes; p++) {
        if (desc->passthroughPlanes & (1 << p)) {
            filter->passthroughInput[p] = desc->passthroughInput[p];
            filter->readsInput[desc->passthroughInput[p]] = 1;
        }
    }
    filter->beginFrame = desc->beginFrame;
    filter->process = desc->process;
    filter->endFrame = desc->endFrame;
    filter->freeFunc = desc->free;
    filter->instanceData = desc->instanceData;

    const std::vector<VSFilterDependency> deps = sameFrameDependencies(desc->inputs, desc->numInputs, vi);
    return makeNode(name, vi, tiledGetFrame, deps.data(), desc->numInputs, false, std::move(filter), core, errorMessage);
}

VSNode *createFusableFilterNode(const char *name, const VSFrameMapFilterDesc *desc, VSCore *core, std::string &errorMessage) {
    const Failure fail(name, errorMessage);

    const VSVideoInfo *vi = desc->vi;
    if (!vi || !VSCore::isValidVideoInfo(*vi))
        return fail("the output's VSVideoInfo is invalid");
    if (desc->numInputs < 1 || !desc->inputs)
        return fail("a frame map needs at least one input");

    /* The output frames are the input frames, so the node is resident where they are, and a
       constant format or size it declares must be theirs. */
    bool gpu = false;
    for (int i = 0; i < desc->numInputs; i++) {
        VSNode *input = desc->inputs[i].source;
        const std::string which = "input " + std::to_string(i);
        if (!input || input->getNodeType() != mtVideo)
            return fail(which + " is not a video node");
        if (i == 0)
            gpu = input->isGPUOutput();
        else if (input->isGPUOutput() != gpu)
            return fail(which + " is not resident where input 0 is, all inputs must be on the CPU or all on the GPU");
        const VSVideoInfo &ivi = input->getVideoInfo();
        if (vi->format.colorFamily != cfUndefined && !vsh::isSameVideoFormat(&vi->format, &ivi.format))
            return fail(which + " does not have the output's format");
        if (vi->width && (ivi.width != vi->width || ivi.height != vi->height))
            return fail(which + " does not have the output's size");
    }

    std::unique_ptr<VSFrameMapFilter> map(new VSFrameMapFilter);
    map->inputs.reserve(desc->numInputs);
    for (int i = 0; i < desc->numInputs; i++)
        map->inputs.push_back(desc->inputs[i].source);
    map->mapFrame = desc->mapFrame;
    map->beginFrame = desc->beginFrame;
    map->freeFunc = desc->free;
    map->instanceData = desc->instanceData;

    return makeNode(name, vi, frameMapGetFrame, desc->inputs, desc->numInputs, gpu, std::move(map), core, errorMessage);
}

VSNode *createFusableFilterNode(const char *name, const VSPlaneMapFilterDesc *desc, VSCore *core, std::string &errorMessage) {
    const Failure fail(name, errorMessage);

    const VSVideoInfo *vi = desc->vi;
    if (!vi || !VSCore::isValidVideoInfo(*vi) || vi->format.colorFamily == cfUndefined || !vi->width || !vi->height)
        return fail("a plane map's output must have a constant format and size");
    if (desc->numInputs < 1 || !desc->inputs)
        return fail("a plane map needs at least one input");

    const int numPlanes = vi->format.numPlanes;
    std::vector<char> givesPlanes(desc->numInputs, 0);
    for (int p = 0; p < numPlanes; p++) {
        if (desc->planeInput[p] < 0 || desc->planeInput[p] >= desc->numInputs)
            return fail("planeInput names an input that does not exist for plane " + std::to_string(p));
        givesPlanes[desc->planeInput[p]] = 1;
    }

    for (int i = 0; i < desc->numInputs; i++) {
        VSNode *input = desc->inputs[i];
        const std::string which = "input " + std::to_string(i);
        if (!input || input->getNodeType() != mtVideo)
            return fail(which + " is not a video node");
        for (int j = 0; j < i; j++)
            if (desc->inputs[j] == input)
                return fail(which + " is input " + std::to_string(j) + " again, each node must be given once");
        if (givesPlanes[i] && !vsh::isConstantVideoFormat(&input->getVideoInfo()))
            return fail(which + " gives planes, so it must have a constant format and size");
    }

    /* The output frames are made of the planes of the inputs that give planes, so the node is
       resident where they are; an input only giving properties can be anywhere. */
    int residencyInput = -1;
    for (int i = 0; i < desc->numInputs; i++) {
        if (!givesPlanes[i])
            continue;
        if (residencyInput < 0)
            residencyInput = i;
        else if (desc->inputs[i]->isGPUOutput() != desc->inputs[residencyInput]->isGPUOutput())
            return fail("input " + std::to_string(i) + " is not resident where input " + std::to_string(residencyInput) + " is, all inputs giving planes must be on the CPU or all on the GPU");
    }

    for (int p = 0; p < numPlanes; p++) {
        const VSVideoInfo &ivi = desc->inputs[desc->planeInput[p]]->getVideoInfo();
        const int q = desc->planeSource[p];
        const std::string which = "plane " + std::to_string(p) + " (plane " + std::to_string(q) + " of input " + std::to_string(desc->planeInput[p]) + ")";
        if (q < 0 || q >= ivi.format.numPlanes)
            return fail(which + " does not exist");
        if ((ivi.width >> (q ? ivi.format.subSamplingW : 0)) != (vi->width >> (p ? vi->format.subSamplingW : 0)) ||
            (ivi.height >> (q ? ivi.format.subSamplingH : 0)) != (vi->height >> (p ? vi->format.subSamplingH : 0)))
            return fail(which + " does not have the output plane's size");
        /* The same rule newVideoFrame2 applies to sharing a plane. */
        if (ivi.format.sampleType != vi->format.sampleType || ivi.format.bytesPerSample != vi->format.bytesPerSample)
            return fail(which + " does not store its samples like the output");
    }

    std::unique_ptr<VSPlaneMapFilter> planes(new VSPlaneMapFilter);
    planes->vi = *vi;
    planes->inputs.assign(desc->inputs, desc->inputs + desc->numInputs);
    planes->readsInput = givesPlanes;
    for (int p = 0; p < 3; p++) {
        planes->planeInput[p] = p < numPlanes ? desc->planeInput[p] : 0;
        planes->planeSource[p] = p < numPlanes ? desc->planeSource[p] : 0;
    }
    planes->beginFrame = desc->beginFrame;
    planes->freeFunc = desc->free;
    planes->instanceData = desc->instanceData;

    const std::vector<VSFilterDependency> deps = sameFrameDependencies(desc->inputs, desc->numInputs, vi);
    const bool gpu = residencyInput >= 0 && desc->inputs[residencyInput]->isGPUOutput();
    return makeNode(name, vi, planeMapGetFrame, deps.data(), desc->numInputs, gpu, std::move(planes), core, errorMessage);
}
