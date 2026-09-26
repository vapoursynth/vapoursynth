/* L9: what happens when a producer pair is published on a value nothing will ever signal.

   VSVulkan4.h states the rule on setGPUPlaneProducer -- "Never a value you still have to signal
   yourself from the host" -- and explains why: freeing a frame waits out its producer, cache
   eviction frees frames, so a signal needing a host thread can be waiting on the very thread it
   blocks. I24 has ~VSPlaneData wait unconditionally, and gpuTimelineWaitValue says a value an
   exec pool has not submitted is fatal "while a timeline you signal yourself has no bound the
   core could check".

   So a plugin that gets this wrong has an unbounded wait inside frame destruction, by design and
   documented. This probe measures what that actually looks like: which call hangs, for how long,
   and whether anything reports it -- the difference between a documented hazard and a recorded
   measurement.

   Two cases, control first so a hang is attributable:
     control  publish value 0, which a fresh timeline has already reached -> must not hang
     hazard   publish value 1, which nothing ever signals              -> expected to hang

   A watchdog prints the step it died on, so the last line names the hang site. */
#include "VapourSynth4.h"
#include "VSVulkan4.h"

#include <stdio.h>
#include <string.h>
#include "probe_compat.h"

static const VSAPI *vsapi;
static const VSVULKANAPI *vkapi;

static volatile LONG watchdogStep = 0;
static char stepName[128] = "startup";
static int hazardStarted = 0;

static void step(const char *what) {
    strncpy(stepName, what, sizeof(stepName) - 1);
    stepName[sizeof(stepName) - 1] = 0;
    InterlockedIncrement(&watchdogStep);
    printf("  -> %s\n", what);
}

static DWORD WINAPI watchdog(LPVOID p) {
    LONG last = -1;
    int idle = 0;
    (void)p;
    for (;;) {
        Sleep(1000);
        if (InterlockedGet(&watchdogStep) == last) {
            if (++idle >= 30) {
                printf("\n  stuck for 30 s in: %s\n", stepName);
                if (hazardStarted) {
                    printf("\nRESULT: the unsignalled producer is an UNBOUNDED wait in frame\n"
                           "        destruction. Nothing reported it and nothing timed out; the\n"
                           "        thread freeing the frame is parked forever. A plugin that\n"
                           "        publishes a value it forgets to signal hangs the core here,\n"
                           "        and if it is a cache eviction doing the freeing it takes the\n"
                           "        evicting thread with it.\n");
                    printf("MEASURED: hang confirmed\n");
                } else {
                    printf("\nFAIL: the CONTROL case hung, which it must not -- value 0 is\n"
                           "      already reached on a fresh timeline.\n");
                    printf("FAILED\n");
                }
                fflush(stdout);
                TerminateProcess(GetCurrentProcess(), hazardStarted ? 0 : 1);
            }
        } else {
            idle = 0;
        }
        last = InterlockedGet(&watchdogStep);
    }
}

static int runCase(const char *label, uint64_t value) {
    char err[512] = { 0 };
    VSCore *core;
    VSGPUTimeline *timeline;
    VSFrame *frame;
    VSVideoFormat fmt;
    unsigned long long t0, t1;

    printf("\n%s: publish producer value %llu\n", label, (unsigned long long)value);
    core = vsapi->createCore(0);
    if (!core) { printf("  createCore failed\n"); return 1; }

    step("createGPUTimeline");
    timeline = vkapi->createGPUTimeline(core, err, sizeof(err));
    if (!timeline) { printf("  createGPUTimeline failed: %s\n", err); vsapi->freeCore(core); return 1; }

    step("newGPUVideoFrame");
    vsapi->queryVideoFormat(&fmt, cfGray, stInteger, 8, 0, 0, core);
    frame = vkapi->newGPUVideoFrame(&fmt, 256, 256, NULL, core);
    if (!frame) { printf("  newGPUVideoFrame failed (no GPU frame support?)\n"); 
                  vkapi->freeGPUTimeline(timeline); vsapi->freeCore(core); return 1; }

    step("setGPUPlaneProducer");
    vkapi->setGPUPlaneProducer(frame, 0, timeline, value);

    step("freeFrame -- this is where ~VSPlaneData waits out the producer");
    t0 = probe_millis();
    vsapi->freeFrame(frame);
    t1 = probe_millis();
    printf("     returned after %llu ms\n", (unsigned long long)(t1 - t0));

    step("freeGPUTimeline");
    vkapi->freeGPUTimeline(timeline);
    step("freeCore");
    vsapi->freeCore(core);
    printf("  %s completed\n", label);
    return 0;
}

int main(void) {
    char err[512] = { 0 };
    VSVulkanCoreHandles h;
    VSCore *probe;

    setvbuf(stdout, NULL, _IONBF, 0);
    vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { printf("no api\n"); return 3; }
    vkapi = vsapi->getVulkanAPI();
    probe = vsapi->createCore(0);
    if (vkapi->getVulkanHandles(probe, &h, err, sizeof(err))) { printf("no vulkan: %s\n", err); return 3; }
    vsapi->freeCore(probe);

    printf("L9: an unsignalled producer pair, and what frame destruction does about it\n");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    if (runCase("control (value 0, already reached)", 0)) {
        printf("FAILED\n");
        return 1;
    }

    hazardStarted = 1;
    if (runCase("hazard (value 1, never signalled)", 1)) {
        printf("FAILED\n");
        return 1;
    }

    printf("\nRESULT: the unsignalled producer did NOT hang -- frame destruction returned.\n"
           "        Something bounds it after all; record what and where.\n");
    printf("MEASURED: no hang\n");
    return 0;
}
