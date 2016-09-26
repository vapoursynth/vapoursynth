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
#include <codecvt>
#include <string>
#include <fstream>
#include <vector>
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include "ss.h"
#pragma warning(disable: 4244) // size conversion warning
#pragma warning(disable: 4245) // sign conversion warning
#include "avisynth.h"
#pragma warning(default: 4244)
#pragma warning(default: 4245)
#include "avfspfm.h"
#include "xxfs.h"
#include "avfs.h"
#include "vsfs.h"
#include "videoinfoadapter.h"
#include "../common/p2p_api.h"
#include "../common/fourcc.h"
// Common vfw defines
#define WAVE_FORMAT_PCM               1
#define WAVE_FORMAT_IEEE_FLOAT   0x0003