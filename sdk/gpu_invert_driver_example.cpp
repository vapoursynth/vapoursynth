/*
* GPU filter example: the same invert as gpu_invert_example.c, declared through the DRIVER in
* gpufilter.h instead of recorded by hand. This is the third and shortest of the three shapes:
*
*   gpu_invert_raw_example.c    every obligation spelled out -- own timeline, queue locking,
*                               producer waits, a retained ring keeping sources alive
*   gpu_invert_example.c        the execution pool discharges those, leaving pipeline creation
*                               and per frame recording
*   this file                   the driver discharges recording too; what is left is the
*                               per pixel expression and the parameters it reads
*
* The driver owns the frame loop: output allocation with plane sharing, the exec context,
* producer waits, retention, dispatch geometry, barriers, producer publication and submission.
* It also owns the pipeline -- descriptor layout, push constants, format specialization and
* shader compilation all come from the declaration. A filter supplies one GLSL statement per
* sample type and a callback filling the parameter block.
*
* WHAT THIS COSTS: gpufilter.h models one shape -- for each processed plane, a fixed list of
* compute passes over frame planes, scratch and constant buffers. A filter needing something
* outside it (indirect dispatch, its own descriptor layout, a dispatch count that varies per
* frame) drops to VSVulkan4.h and looks like one of the other two examples. The two compose:
* such a filter can still take its exec pool from the same API.
*
* gpufilter.h is INTERNAL to the core, not part of the installed API, and deliberately so: it
* is inline code over the public VSVULKANAPI with no ABI commitment, so it may change shape
* between releases. Copy it next to your source and build against your copy -- the way
* VSHelper4.h is used -- rather than including it from a VapourSynth checkout, so a core update
* cannot silently change what your plugin compiles. It needs C++20 and nothing else.
*
*   clang-cl /LD /MD /O2 /EHsc /std:c++20 gpu_invert_driver_example.cpp ^
*       /I<your copy of gpufilter.h> /I<vapoursynth include> /I<vulkan sdk include>
*
* Unlike the other two this handles float formats as well as 8-16 bit integer, which is the
* two extra lines of bodyFloat below. Doing that by hand means a second kernel, a second
* pipeline and a format switch at every store.
*/

#define VS_USE_API_43
#include "VapourSynth4.h"
#include "VSVulkan4.h"

/* Your copy of the core's src/core/gpufilter.h. */
#include "gpufilter.h"

#include <memory>
#include <string>

namespace {

struct InvertDriverData {
    VSNode *node = nullptr;
    const VSVideoInfo *vi = nullptr;
};

static void VS_CC invertDriverCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<InvertDriverData> d(new InvertDriverData());
    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    /* The driver refuses a variable clip itself, with a message naming the remedy, so there is
       nothing to check here that it does not already check. A filter with its own format
       restrictions still states them: this one accepts anything the driver compiles for. */

    vsgpu::SimpleFilter sf;
    sf.name = "InvertDriverGPU";
    sf.inputs = 1;

    /* One statement per sample type, with int x, int y in scope. SRC0(x, y) reads the source
       plane clamped to its edges and STORE() writes the output at (x, y); the driver supplies
       both macros already specialized to the clip's format, so the same text serves 8, 10, 12
       and 16 bit integer. pc.u[] and pc.f[] are the parameter block fill() writes below. */
    sf.bodyInt = "    STORE(pc.u[0] - min(uint(SRC0(x, y)), pc.u[0]));";

    /* Float inverts around 1.0 for luma and by negation for chroma, matching what std.Invert
       does on the CPU -- on half that sign flip is the same bit pattern as an xor with 0x8000,
       zeros and NaNs included. */
    sf.bodyFloat = "    float s = float(SRC0(x, y));\n"
                   "    STORE(pc.u[1] != 0u ? -s : 1.0 - s);";

    /* Called once per plane per frame to fill the parameter block the bodies read. Capture by
       value: it outlives this function and runs on worker threads. */
    const VSVideoFormat fmt = d->vi->format;
    sf.fill = [fmt](int plane, float *, uint32_t *u) {
        u[0] = (1u << fmt.bitsPerSample) - 1;                       /* peak value */
        u[1] = (fmt.colorFamily == cfYUV && plane > 0) ? 1u : 0u;   /* chroma? */
    };

    /* Consumes the node references on success AND failure, so there is nothing to free here in
       either branch -- releasing d->node afterwards would be a double free. */
    std::string error;
    VSNode *node = vsgpu::createSimpleFilter(sf, &d->node, 1, d->vi, core, vsapi, error);
    d->node = nullptr;
    if (node)
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
    else
        vsapi->mapSetError(out, (std::string(sf.name) + ": " + error).c_str());
}

} // namespace

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.example.gpuinvertdriver", "vkdriverexample",
        "Out of tree GPU filter example built on the declaration driver",
        VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("InvertDriverGPU", "clip:vnode:gpu;", "clip:vnode:gpu;",
        invertDriverCreate, nullptr, plugin);
}
