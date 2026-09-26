/* Isolates what freeCore's "still allocated in GPU framebuffers" warning is counting.
   PROBE_MODE=download  : BlankClip -> GPUUpload -> GPUDownload, fetch, free everything.
   PROBE_MODE=upload    : BlankClip -> GPUUpload, fetch the GPU frame, free everything.
   PROBE_CLEAR=1        : call clearCoreCaches (which sweeps the exec pools) before freeCore. */
#include "VapourSynth4.h"

#include "probe_compat.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::vector<std::string> warnings;

static void VS_CC logHandler(int msgType, const char *msg, void *) {
    if (msgType >= mtWarning)
        warnings.push_back(msg);
}

static VSNode *call(const VSAPI *vsapi, VSPlugin *plugin, const char *name, VSMap *args) {
    VSMap *ret = vsapi->invoke(plugin, name, args);
    VSNode *node = vsapi->mapGetNode(ret, "clip", 0, nullptr);
    if (!node)
        std::printf("%s failed: %s\n", name, vsapi->mapGetError(ret));
    vsapi->freeMap(ret);
    vsapi->clearMap(args);
    return node;
}

int main() {
    probe_setenv("VS_VULKAN_FORCE_STAGING", "1");
    const char *mode = getenv("PROBE_MODE");
    const bool wantDownload = !mode || std::string(mode) == "download";
    const VSAPI *vsapi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) { std::printf("no api\n"); return 3; }
    VSCore *core = vsapi->createCore(0);
    vsapi->addLogHandler(logHandler, nullptr, nullptr, core);
    VSPlugin *stdPlugin = vsapi->getPluginByID("com.vapoursynth.std", core);
    if (!stdPlugin) { std::printf("no std plugin\n"); return 3; }

    VSMap *args = vsapi->createMap();
    vsapi->mapSetInt(args, "width", 1920, maReplace);
    vsapi->mapSetInt(args, "height", 1080, maReplace);
    vsapi->mapSetInt(args, "format", pfGray8, maReplace);
    vsapi->mapSetInt(args, "length", 1, maReplace);
    VSNode *blank = call(vsapi, stdPlugin, "BlankClip", args);
    if (!blank) return 3;
    vsapi->mapConsumeNode(args, "clip", blank, maReplace);
    VSNode *last = call(vsapi, stdPlugin, "GPUUpload", args);
    if (!last) return 3;
    if (wantDownload) {
        vsapi->mapConsumeNode(args, "clip", last, maReplace);
        last = call(vsapi, stdPlugin, "GPUDownload", args);
        if (!last) return 3;
    }
    vsapi->freeMap(args);

    char err[256] = {};
    const VSFrame *frame = vsapi->getFrame(0, last, err, sizeof(err));
    if (!frame) { std::printf("getFrame failed: %s\n", err); return 3; }
    vsapi->freeFrame(frame);
    vsapi->freeNode(last); /* the whole chain and its caches go with it */

    if (getenv("PROBE_CLEAR")) {
        vsapi->clearCoreCaches(core);
        std::printf("clearCoreCaches called\n");
    }

    std::printf("mode: %s\n", wantDownload ? "download" : "upload");
    vsapi->freeCore(core);
    std::printf("warnings logged by freeCore: %zu\n", warnings.size());
    for (const std::string &w : warnings) {
        if (w.find("API 3") != std::string::npos)
            continue;
        std::printf("  %s\n", w.c_str());
    }
    return 0;
}
