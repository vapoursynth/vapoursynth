/*
* Copyright (c) 2013 Fredrik Mellbin
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

#include <string>
#include <algorithm>
#include <fstream>
#include <string>
#include <cerrno>

#include "VSScript.h"
#include "VSHelper.h"

int num_threads = 1;
const VSAPI *vsapi = NULL;
VSScript *se = NULL;
VSNodeRef *node = NULL;

std::string get_file_contents(const char *filename)
{
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (in)
  {
    std::string contents;
    in.seekg(0, std::ios::end);
    contents.resize(in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    return(contents);
  }
  return "";
}

//#ifdef _WIN32
//int wmain(int argc, wchar_t **argv) {
//#else
int main(int argc, char **argv) {
//#endif

    if (argc != 3) {
        fprintf(stderr, "VSPipe\n");
        fprintf(stderr, "Write to stdout: vspipe script.vpy -\n");
        fprintf(stderr, "Write to file: vspipe script.vpy <outfile>\n");
        fprintf(stderr, "Output with y4m headers: vspipe script.vpy <outfile> y4m\n");
        return 1;
    }

    if (!vseval_init()) {
        fprintf(stderr, "Failed to initialize VapourSynth environment\n");
        return 1;
    }

    vsapi = vseval_getVSApi();
    if (vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API\n");
        vseval_finalize();
        return 1;
    }

    std::string script = get_file_contents(argv[1]);
    if (script.empty()) {
        fprintf(stderr, "Failed to read script file or file is empty\n");
        vseval_finalize();
        return 1;
    }

    if (vseval_evaluateScript(&se, script.c_str(), argv[1])) {
        fprintf(stderr, "Script evaluation failed:\n%s", vseval_getError(se));
        vseval_freeScript(se);
        vseval_finalize();
        return 1;
    }

    node = vseval_getOutput(se, 0);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node\n");
       vseval_freeScript(se);
       vseval_finalize();
       return 1;
    }

    vi = vsapi->getVideoInfo(node);
    if (!isConstantFormat(vi) || vi->numFrames == 0) {
        fprintf(stderr, "Cannot output clips with varying dimensions or unknown length\n");
        vseval_freeScript(se);
        vseval_finalize();
        return 1;
    }



    vseval_freeScript(se);
    vseval_finalize();
}


VapourSynthFile::~VapourSynthFile() {
    if (vi) {
        vi = NULL;
		while (pending_requests > 0) {};
		vseval_freeScript(se);
    }
}



bool VapourSynthFile::DelayInit2() {
    if (!szScriptName.empty() && !vi) {

		std::string script = get_file_contents(szScriptName.c_str());
		if (script.empty())
			goto vpyerror;

		if (!vseval_evaluateScript(&se, script.c_str(), szScriptName.c_str())) {
			
			node = vseval_getOutput(se, 0);
			if (!node)
				goto vpyerror;
			vi = vsapi->getVideoInfo(node);
            error_msg.clear();

            if (vi->width == 0 || vi->height == 0 || vi->format == NULL || vi->numFrames == 0) {
                error_msg = "Cannot open clips with varying dimensions or format in vfw";
                goto vpyerror;
            }

            int id = vi->format->id;
            if (id != pfCompatBGR32
                && id != pfCompatYUY2
                && id != pfYUV420P8
                && id != pfGray8
                && id != pfYUV444P8
                && id != pfYUV422P8
                && id != pfYUV411P8
                && id != pfYUV410P8
                && id != pfYUV420P10
                && id != pfYUV420P16
                && id != pfYUV422P10
                && id != pfYUV422P16) {
                error_msg = "VFW module doesn't support ";
                error_msg += vi->format->name;
                error_msg += " output";
                goto vpyerror;
            }

			// set the special options hidden in global variables
			int error;
			int64_t val;
			VSMap *options = vsapi->createMap();
			vseval_getVariable(se, "enable_v210", options);
			val = vsapi->propGetInt(options, "enable_v210", 0, &error);
			if (!error)
				enable_v210 = !!val;
			vseval_getVariable(se, "pad_scanlines", options);
			val = vsapi->propGetInt(options, "pad_scanlines", 0, &error);
			if (!error)
				pad_scanlines = !!val;
			vsapi->freeMap(options);

			const VSCoreInfo *info = vsapi->getCoreInfo(vseval_getCore());
			num_threads = info->numThreads;

            return true;
        } else {
			error_msg = vseval_getError(se);
            vpyerror:
            vi = NULL;
			vseval_freeScript(se);
			se = NULL;
            int res = vseval_evaluateScript(&se, ErrorScript, "vfw_error.bleh");
			const char *et = vseval_getError(se);
			node = vseval_getOutput(se, 0);
            vi = vsapi->getVideoInfo(node);
            return true;
        }
    } else {
        return !!vi;
    }
}

void VapourSynthFile::Lock() {
    EnterCriticalSection(&cs_filter_graph);
}

void VapourSynthFile::Unlock() {
    LeaveCriticalSection(&cs_filter_graph);
}



STDMETHODIMP VapourSynthFile::Info(AVIFILEINFOW *pfi, LONG lSize) {
    if (!pfi)
        return E_POINTER;

    if (!DelayInit())
        return E_FAIL;

    AVIFILEINFOW afi;
    memset(&afi, 0, sizeof(afi));

    afi.dwMaxBytesPerSec	= 0;
    afi.dwFlags				= AVIFILEINFO_HASINDEX | AVIFILEINFO_ISINTERLEAVED;
    afi.dwCaps				= AVIFILECAPS_CANREAD | AVIFILECAPS_ALLKEYFRAMES | AVIFILECAPS_NOCOMPRESSION;

    afi.dwStreams				= 1;
    afi.dwSuggestedBufferSize	= 0;
    afi.dwWidth					= vi->width;
    afi.dwHeight				= vi->height;
    afi.dwEditCount				= 0;

    afi.dwRate					= int64ToIntS(vi->fpsNum ? vi->fpsNum : 1);
    afi.dwScale					= int64ToIntS(vi->fpsDen ? vi->fpsDen : 30);
    afi.dwLength				= vi->numFrames;

    wcscpy(afi.szFileType, L"VapourSynth");

    // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(afi)
    memset(pfi, 0, lSize);
    memcpy(pfi, &afi, min(size_t(lSize), sizeof(afi)));
    return S_OK;
}

////////////////////////////////////////////////////////////////////////
//////////// local

void VS_CC VapourSynthFile::frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
    VapourSynthFile *vsfile = (VapourSynthFile *)userData;
    vsfile->vsapi->freeFrame(f);
    InterlockedDecrement(&vsfile->pending_requests);
}

bool VapourSynthStream::ReadFrame(void* lpBuffer, int n) {
    const VSAPI *vsapi = parent->vsapi;
    const VSFrameRef *f = vsapi->getFrame(n, parent->node, 0, 0);
    if (!f)
        return false;

    const VSFormat *fi = vsapi->getFrameFormat(f);
    const int pitch    = vsapi->getStride(f, 0);
    const int row_size = vsapi->getFrameWidth(f, 0) * fi->bytesPerSample;
    const int height   = vsapi->getFrameHeight(f, 0);

    int out_pitch;
    int out_pitchUV;

    bool semi_packed_p10 = (fi->id == pfYUV420P10) || (fi->id == pfYUV422P10);
    bool semi_packed_p16 = (fi->id == pfYUV420P16) || (fi->id == pfYUV422P16);

    // BMP scanlines are dword-aligned
    if (fi->numPlanes == 1) {
        out_pitch = (row_size+3) & ~3;
        out_pitchUV = (vsapi->getFrameWidth(f, 1) * fi->bytesPerSample+3) & ~3;
    }
    // Planar scanlines are packed
    else {
        out_pitch = row_size;
        out_pitchUV = vsapi->getFrameWidth(f, 1) * fi->bytesPerSample;
    }

    {
        BitBlt((BYTE*)lpBuffer, out_pitch, vsapi->getReadPtr(f, 0), pitch, row_size, height);
    }

    if (fi->numPlanes == 3) {
        BitBlt((BYTE*)lpBuffer + (out_pitch*height),
            out_pitchUV,               vsapi->getReadPtr(f, 2),
            vsapi->getStride(f, 2), vsapi->getFrameWidth(f, 2),
            vsapi->getFrameHeight(f, 2) );

        BitBlt((BYTE*)lpBuffer + (out_pitch*height + vsapi->getFrameHeight(f, 1)*out_pitchUV),
            out_pitchUV,               vsapi->getReadPtr(f, 1),
            vsapi->getStride(f, 1), vsapi->getFrameWidth(f, 1),
            vsapi->getFrameHeight(f, 1) );
    }

    vsapi->freeFrame(f);

    for (int i = n + 1; i < std::min<int>(n + parent->num_threads, parent->vi->numFrames); i++) {
        InterlockedIncrement(&parent->pending_requests);
        vsapi->getFrameAsync(i, parent->node, VapourSynthFile::frameDoneCallback, (void *)parent);
    }

    return true;
}


