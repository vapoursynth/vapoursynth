#include "shared.h"

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.removegrainvs", "rgvs", "RemoveGrain VapourSynth Port", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("RemoveGrain", "clip:clip;mode:int[];", removeGrainCreate, 0, plugin);
    registerFunc("Repair", "clip:clip;repairclip:clip;mode:int[];", repairCreate, 0, plugin);
}
