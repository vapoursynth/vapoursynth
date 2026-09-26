/* L12: the fatal guards on the plugin-facing exec API, none of which had any coverage.

   Each is enforced with vulkanFatal, so a refusal ENDS THE PROCESS and each case needs its own
   run: pass the case index as argv[1]. A case passes when its marker prints and the process dies
   before printing NOT REFUSED.

     0  I27  setGPUPlaneProducer on a plane shared by copyFrame
     1  I27  gpuExecWritesPlane on a plane shared by copyFrame
     2  I22  gpuExecRetain on a handle whose recording already ended (submitted)
     3  I22  gpuExecSubmit on a handle whose recording already ended (abandoned)
     4  I16  a second gpuExecAcquire on the same pool from one thread
     5  I18  gpuExecPoolWaitIdle while this thread holds a context of the pool
     6  I23  setGPUPlaneProducer on the pool's timeline with a value it has not submitted
     7  ceiling  gpuExecWaitValue on a value past everything the pool submitted
     8  ceiling  gpuTimelineWaitValue on a pool timeline past what it submitted
     9  I25  copyMap of a map holding a node into a frame's property map

   6-8 are the guards that close the circular-wait shapes: a producer pair or a host wait on a
   value that only a LATER submission would signal. Case 7 also documents that gpuExecWaitValue
   does not object to the caller holding a context -- the ceiling is what refuses it, not I18.

   I27's own header names the reachable shape: "copying a frame shares its planes, and a GPU plane
   has no copy on write, so a write declared on a shared plane would land in the other frame as
   well". That is two ordinary calls away, which is why it is worth a regression test. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"
#include "VSHelper4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;
static VSCore *core;
static VSGPUExecPool *pool;
static int tag;

static const char *caseName(int i) {
    switch (i) {
    case 0: return "setGPUPlaneProducer on a shared plane (I27)";
    case 1: return "gpuExecWritesPlane on a shared plane (I27)";
    case 2: return "gpuExecRetain after submit (I22)";
    case 3: return "gpuExecSubmit after abandon (I22)";
    case 4: return "second gpuExecAcquire on one pool (I16)";
    case 5: return "gpuExecPoolWaitIdle holding a context (I18)";
    case 6: return "setGPUPlaneProducer with an unsubmitted pool value (I23)";
    case 7: return "gpuExecWaitValue past the pool's ceiling";
    case 8: return "gpuTimelineWaitValue past the pool timeline's ceiling";
    case 9: return "copyMap smuggling a node into frame properties (I25)";
    default: return "?";
    }
}
#define NCASES 10

int main(int argc, char **argv) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSGPUExecContext *ctx;
    VSVideoFormat fmt;
    VSFrame *frame;
    VSFrame *shared;
    VSGPUTimeline *timeline;
    uint64_t submitted = 0;
    int theCase;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        int i;
        printf("usage: %s <case>   where case is one of:\n", argv[0]);
        for (i = 0; i < NCASES; i++)
            printf("  %d  %s\n", i, caseName(i));
        printf("\nEach case ends the process by design, so run them one at a time.\n");
        return 2;
    }
    theCase = atoi(argv[1]);
    if (theCase < 0 || theCase >= NCASES) { printf("no such case\n"); return 2; }

    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    core = vsapi->createCore(0);
    vkapi = vsapi->getVulkanAPI();
    if (vkapi->getVulkanHandles(core, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }

    printf("L12 case %d: %s\n", theCase, caseName(theCase));
    pool = vkapi->createGPUExecPool(core, vqCompute, err, sizeof(err));
    if (!pool) { printf("createGPUExecPool failed: %s\n", err); return 3; }

    vsapi->queryVideoFormat(&fmt, cfGray, stInteger, 8, 0, 0, core);
    frame = vkapi->newGPUVideoFrame(&fmt, 256, 256, NULL, core);
    if (!frame) { printf("newGPUVideoFrame failed\n"); return 3; }

    ctx = vkapi->gpuExecAcquire(pool, err, sizeof(err));
    if (!ctx) { printf("gpuExecAcquire failed: %s\n", err); return 3; }

    switch (theCase) {
    case 0:
    case 1:
        /* copyFrame shares the planes, so neither frame owns plane 0 outright any more. */
        shared = vsapi->copyFrame(frame, core);
        printf("  MARKER: plane shared via copyFrame, calling the guarded entry point\n");
        fflush(stdout);
        if (theCase == 0) {
            timeline = vkapi->createGPUTimeline(core, err, sizeof(err));
            if (!timeline) { printf("createGPUTimeline failed: %s\n", err); return 3; }
            vkapi->setGPUPlaneProducer(shared, 0, timeline, 0);
        } else {
            vkapi->gpuExecWritesPlane(ctx, shared, 0);
        }
        break;
    case 2:
        if (vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err))) {
            printf("gpuExecSubmit failed: %s\n", err); return 3;
        }
        printf("  MARKER: recording ended by submit, calling gpuExecRetain on the stale handle\n");
        fflush(stdout);
        vkapi->gpuExecRetain(ctx, NULL, &tag, 0);
        break;
    case 3:
        vkapi->gpuExecAbandon(ctx);
        printf("  MARKER: recording ended by abandon, calling gpuExecSubmit on the stale handle\n");
        fflush(stdout);
        vkapi->gpuExecSubmit(ctx, &submitted, err, sizeof(err));
        break;
    case 4:
        printf("  MARKER: already holding a context, acquiring a second on the same pool\n");
        fflush(stdout);
        vkapi->gpuExecAcquire(pool, err, sizeof(err));
        break;
    case 5:
        printf("  MARKER: already holding a context, calling gpuExecPoolWaitIdle\n");
        fflush(stdout);
        vkapi->gpuExecPoolWaitIdle(pool, err, sizeof(err));
        break;
    case 6:
        /* Nothing submitted yet, so any nonzero value is one the pool has not produced. */
        printf("  MARKER: publishing value 1000 on the pool's timeline before any submit\n");
        fflush(stdout);
        vkapi->setGPUPlaneProducer(frame, 0, vkapi->gpuExecPoolTimeline(pool), 1000);
        break;
    case 7:
        printf("  MARKER: waiting on value 1000, past the pool's ceiling (and holding a context)\n");
        fflush(stdout);
        vkapi->gpuExecWaitValue(pool, 1000, err, sizeof(err));
        break;
    case 8:
        printf("  MARKER: timeline wait on value 1000, past what the pool submitted\n");
        fflush(stdout);
        vkapi->gpuTimelineWaitValue(vkapi->gpuExecPoolTimeline(pool), 1000, err, sizeof(err));
        break;
    case 9: {
        /* I25: a frame's map never holds a node, so freeing a frame -- under cacheLock, inside
           release callbacks -- never destroys a node or runs plugin code. The setters refuse
           with an error; copyMap of a map that already holds one is the fatal. */
        VSPlugin *stdp = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);
        VSMap *args = vsapi->createMap(), *out, *carrier = vsapi->createMap();
        VSNode *node; int e = 0;
        vsapi->mapSetInt(args, "width", 64, maReplace); vsapi->mapSetInt(args, "height", 64, maReplace);
        out = vsapi->invoke(stdp, "BlankClip", args); vsapi->freeMap(args);
        node = vsapi->mapGetNode(out, "clip", 0, &e); vsapi->freeMap(out);
        vsapi->mapSetNode(carrier, "n", node, maReplace); /* fine on an ordinary map */
        printf("  MARKER: setter refused directly? %s; now copyMap into the frame's map\n",
            vsapi->mapSetNode(vsapi->getFramePropertiesRW(frame), "n", node, maReplace) ? "yes" : "NO");
        fflush(stdout);
        vsapi->copyMap(carrier, vsapi->getFramePropertiesRW(frame));
        break;
    }
    default: break;
    }

    printf("\nRESULT: NOT REFUSED -- %s was accepted\n", caseName(theCase));
    printf("FAILED\n");
    return 1;
}
