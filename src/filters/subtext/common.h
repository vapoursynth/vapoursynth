#ifndef SUBTEXT_COMMON_H
#define SUBTEXT_COMMON_H

#include <stddef.h>

#include "VapourSynth4.h"

#ifdef __cplusplus
extern "C" {
#endif

void blendSubtitles(VSNode *clip, VSNode *subs, const VSMap *in, VSMap *out, const char *filter_name, char *error, size_t error_size, VSCore *core, const VSAPI *vsapi);

#ifdef __cplusplus
}
#endif

#endif // SUBTEXT_COMMON_H
