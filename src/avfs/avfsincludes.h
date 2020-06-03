#include "assertive.h"
#include <VSScript.h>
#include <VapourSynth.h>
#include <VSHelper.h>
#include <climits>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <wchar.h>
#include <cstdio>
#include <atomic>
#include <algorithm>
#include <new>
#include <chrono>
#include <thread>
#include <string>
#include <fstream>
#include <vector>
#include "ss.h"
namespace avs {
#include <avisynth.h>
}
#include "avfspfm.h"
#include "xxfs.h"
#include "avfs.h"
#include "vsfs.h"
#include "videoinfoadapter.h"
#include "../common/p2p_api.h"
#include "../common/fourcc.h"
#include "../common/vsutf16.h"
// Common vfw defines
#define WAVE_FORMAT_PCM               1
#define WAVE_FORMAT_IEEE_FLOAT   0x0003