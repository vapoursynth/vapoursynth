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


#include "VSScript.h"
#include "VSHelper.h"
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#ifdef VS_TARGET_OS_WINDOWS
#include <codecvt>
#include <io.h>
#include <fcntl.h>
#endif

#define __STDC_FORMAT_MACROS
#include <stdio.h>
#include <inttypes.h>


// Needed so windows doesn't drool on itself when ctrl-c is pressed
#ifdef VS_TARGET_OS_WINDOWS
#define NOMINMAX 
#include <Windows.h>
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        _exit(1);
    default:
        return FALSE;
    }
}
#endif

#ifdef VS_TARGET_OS_WINDOWS
typedef std::wstring nstring;
#define NSTRING(x) L##x
std::string nstringToUtf8(const nstring &s) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.to_bytes(s);
}
#else
typedef std::string nstring;
#define NSTRING(x) x
std::string nstringToUtf8(const nstring &s) {
    return s;
}
#endif

const VSAPI *vsapi = NULL;
VSScript *se = NULL;
VSNodeRef *node = NULL;
FILE *outFile = NULL;

int requests = 0;
int outputIndex = 0;
int outputFrames = 0;
int requestedFrames = 0;
int completedFrames = 0;
int totalFrames = 0;
int numPlanes = 0;
bool y4m = false;
bool outputError = false;
bool showInfo = false;
bool printFrameNumber = false;
double fps = 0;
bool hasMeaningfulFps = false;
std::map<int, const VSFrameRef *> reorderMap;

std::string errorMessage;
std::condition_variable condition;
std::mutex mutex;

std::chrono::time_point<std::chrono::high_resolution_clock> start;
std::chrono::time_point<std::chrono::high_resolution_clock> lastFpsReportTime;
int lastFpsReportFrame = 0;

void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
    completedFrames++;

    if (printFrameNumber) {
        std::chrono::time_point<std::chrono::high_resolution_clock> currentTime(std::chrono::high_resolution_clock::now());
        std::chrono::duration<double> elapsedSeconds = currentTime - lastFpsReportTime;
        if (elapsedSeconds.count() > 10) {
            hasMeaningfulFps = true;
            fps = (completedFrames - lastFpsReportFrame) / elapsedSeconds.count();
            lastFpsReportTime = currentTime;
            lastFpsReportFrame = completedFrames;
        }
    }

    if (f) {
        reorderMap.insert(std::make_pair(n, f));
        while (reorderMap.count(outputFrames)) {
            const VSFrameRef *frame = reorderMap[outputFrames];
            reorderMap.erase(outputFrames);
            if (!outputError) {
                if (y4m) {
                    if (fwrite("FRAME\n", 1, 6, outFile) != 6) {
                        if (errorMessage.empty())
                            errorMessage = "Error: fwrite() call failed when writing header, errno: " + std::to_string(errno);
                        totalFrames = requestedFrames;
                        outputError = true;
                    }
                }

                if (!outputError) {
                    const VSFormat *fi = vsapi->getFrameFormat(frame);
                    for (int p = 0; p < fi->numPlanes; p++) {
                        int stride = vsapi->getStride(frame, p);
                        const uint8_t *readPtr = vsapi->getReadPtr(frame, p);
                        int rowSize = vsapi->getFrameWidth(frame, p) * fi->bytesPerSample;
                        int height = vsapi->getFrameHeight(frame, p);
                        for (int y = 0; y < height; y++) {
                            if (fwrite(readPtr, 1, rowSize, outFile) != rowSize) {
                                if (errorMessage.empty())
                                    errorMessage = "Error: fwrite() call failed when writing frame: " + std::to_string(outputFrames) + ", plane: " + std::to_string(p) + 
                                        ", line: " + std::to_string(y) + ", errno: " + std::to_string(errno);
                                totalFrames = requestedFrames;
                                outputError = true;
                                p = 100; // break out of the outer loop
                                break;
                            }
                            readPtr += stride;
                        }
                    }
                }
            }
            vsapi->freeFrame(frame);
            outputFrames++;
        }
    } else {
        outputError = true;
        totalFrames = requestedFrames;
        if (errorMessage.empty()) {
            if (errorMsg)
                errorMessage = "Error: Failed to retrieve frame " + std::to_string(n) + " with error: " + errorMsg;
            else
                errorMessage = "Error: Failed to retrieve frame " + std::to_string(n);
        }
    }

    if (requestedFrames < totalFrames) {
        vsapi->getFrameAsync(requestedFrames, node, frameDoneCallback, NULL);
        requestedFrames++;
    }

    if (printFrameNumber && !outputError) {
        if (hasMeaningfulFps)
            fprintf(stderr, "Frame: %d/%d (%.2f fps)\r", completedFrames, totalFrames, fps);
        else
            fprintf(stderr, "Frame: %d/%d\r", completedFrames, totalFrames);
    }

    if (totalFrames == completedFrames) {
        std::lock_guard<std::mutex> lock(mutex);
        condition.notify_one();
    }
}

bool outputNode() {
    if (requests < 1) {
        const VSCoreInfo *info = vsapi->getCoreInfo(vsscript_getCore(se));
        requests = info->numThreads;
    }

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    totalFrames = vi->numFrames;

    if (y4m && (vi->format->colorFamily != cmGray && vi->format->colorFamily != cmYUV)) {
        errorMessage = "Error: Can only apply y4m headers to YUV and Gray format clips";
        fprintf(stderr, "%s\n", errorMessage.c_str());
        return true;
    }

    std::string y4mFormat;
    std::string numBits;

    if (y4m) {
        if (vi->format->colorFamily == cmGray) {
            y4mFormat = "mono";
            if (vi->format->bitsPerSample > 8)
                y4mFormat = y4mFormat + std::to_string(vi->format->bitsPerSample);
        } else if (vi->format->colorFamily == cmYUV) {
            if (vi->format->subSamplingW == 1 && vi->format->subSamplingH == 1)
                y4mFormat = "420";
            else if (vi->format->subSamplingW == 1 && vi->format->subSamplingH == 0)
                y4mFormat = "422";
            else if (vi->format->subSamplingW == 0 && vi->format->subSamplingH == 0)
                y4mFormat = "444";
            else if (vi->format->subSamplingW == 2 && vi->format->subSamplingH == 2)
                y4mFormat = "410";
            else if (vi->format->subSamplingW == 2 && vi->format->subSamplingH == 0)
                y4mFormat = "411";
            else if (vi->format->subSamplingW == 0 && vi->format->subSamplingH == 1)
                y4mFormat = "440";
            else {
                fprintf(stderr, "No y4m identifier exists for current format\n");
                return true;
            }

            if (vi->format->bitsPerSample > 8)
                y4mFormat = y4mFormat + "p" + std::to_string(vi->format->bitsPerSample);
        } else {
            fprintf(stderr, "No y4m identifier exists for current format\n");
            return true;
        }
    }
    if (!y4mFormat.empty())
        y4mFormat = "C" + y4mFormat + " ";

    std::string header = "YUV4MPEG2 " + y4mFormat + "W" + std::to_string(vi->width) + " H" + std::to_string(vi->height) + " F" + std::to_string(vi->fpsNum) + ":" + std::to_string(vi->fpsDen) + " Ip A0:0\n";

    if (y4m) {
        if (fwrite(header.c_str(), 1, header.size(), outFile) != header.size()) {
            errorMessage = "Error: fwrite() call failed when writing initial header, errno: " + std::to_string(errno);
            outputError = true;
            return outputError;
        }
    }

    std::unique_lock<std::mutex> lock(mutex);

    int intitalRequestSize = std::min(requests, totalFrames);
    requestedFrames = intitalRequestSize;
    for (int n = 0; n < intitalRequestSize; n++)
        vsapi->getFrameAsync(n, node, frameDoneCallback, NULL);

    condition.wait(lock);

    if (outputError) {
        fprintf(stderr, "%s\n", errorMessage.c_str());
    }

    return outputError;
}

const char *colorFamilyToString(int colorFamily) {
    switch (colorFamily) {
    case cmGray: return "Gray";
    case cmRGB: return "RGB";
    case cmYUV: return "YUV";
    case cmYCoCg: return "YCoCg";
    case cmCompat: return "Compat";
    }
    return "";
}

bool nstringToInt(const nstring &ns, int &result) {
    size_t pos = 0;
    std::string s = nstringToUtf8(ns);
    try {
        result = std::stoi(s, &pos);
    } catch (std::invalid_argument &) {
        return false;
    } catch (std::out_of_range &) {
        return false;
    }
    return pos == s.length();
}

// fixme, only allow info without output
#ifdef VS_TARGET_OS_WINDOWS
int wmain(int argc, wchar_t **argv) {
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        fprintf(stderr, "Failed to set stdout to binary mode\n");
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
int main(int argc, char **argv) {
#endif

    if (argc == 2) {
        if (nstring(argv[1]) == NSTRING("-version")) {
            if (!vsscript_init()) {
                fprintf(stderr, "Failed to initialize VapourSynth environment\n");
                return 1;
            }

            vsapi = vsscript_getVSApi();
            if (!vsapi) {
                fprintf(stderr, "Failed to get VapourSynth API pointer\n");
                vsscript_finalize();
                return 1;
            }

            VSCore *core = vsapi->createCore(0);
            if (!core) {
                fprintf(stderr, "Failed to create core\n");
                vsscript_finalize();
                return 1;
            }

            const VSCoreInfo *info = vsapi->getCoreInfo(core);
            printf("%s", info->versionString);
            vsapi->freeCore(core);
            return 0;
        }
    }

    if (argc < 3) {
        fprintf(stderr, "VSPipe usage:\n");
        fprintf(stderr, "Show version info: vspipe -version\n");
        fprintf(stderr, "Show script info: vspipe script.vpy - -info\n");
        fprintf(stderr, "Write to stdout: vspipe script.vpy - [options]\n");
        fprintf(stderr, "Write to file: vspipe script.vpy <outFile> [options]\n");
        fprintf(stderr, "Available options:\n");
        fprintf(stderr, "Select output index: -index N\n");
        fprintf(stderr, "Set number of concurrent frame requests: -requests N\n");
        fprintf(stderr, "Add YUV4MPEG headers: -y4m\n");
        fprintf(stderr, "Print progress to stderr: -progress\n");
        fprintf(stderr, "Show video info: -info (overrides other options)\n");
        return 1;
    }

    nstring outputFilename = argv[2];
    if (outputFilename == NSTRING("-")) {
        outFile = stdout;
    } else {
#ifdef VS_TARGET_OS_WINDOWS
        outFile = _wfopen(outputFilename.c_str(), L"wb");
#else
        outFile = fopen(outputFilename.c_str(), "wb");
#endif
        if (!outFile) {
            fprintf(stderr, "Failed to open output for writing\n");
            return 1;
        }
    }

    for (int arg = 3; arg < argc; arg++) {
        nstring argString = argv[arg];
        if (argString == NSTRING("-y4m")) {
            y4m = true;
        } else if (argString == NSTRING("-info")) {
            showInfo = true;
        } else if (argString == NSTRING("-index")) {
            bool ok = false;
            if (argc <= arg + 1) {
                fprintf(stderr, "No index number specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], outputIndex)) {
                fprintf(stderr, "Couldn't convert %s to an integer\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }
            arg++;
        } else if (argString == NSTRING("-requests")) {
            bool ok = false;
            if (argc <= arg + 1) {
                fprintf(stderr, "No request number specified\n");
                return 1;
            }
            if (!nstringToInt(argv[arg + 1], requests)) {
                fprintf(stderr, "Couldn't convert %s to an integer\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }
            arg++;
        } else if (argString == NSTRING("-progress")) {
            printFrameNumber = true;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", nstringToUtf8(argString).c_str());
            return 1;
        }
    }

    if (!vsscript_init()) {
        fprintf(stderr, "Failed to initialize VapourSynth environment\n");
        return 1;
    }

    vsapi = vsscript_getVSApi();
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        vsscript_finalize();
        return 1;
    }

    start = std::chrono::high_resolution_clock::now();
    if (vsscript_evaluateFile(&se,  nstringToUtf8(argv[1]).c_str(), efSetWorkingDir)) {
        fprintf(stderr, "Script evaluation failed:\n%s\n", vsscript_getError(se));
        vsscript_freeScript(se);
        vsscript_finalize();
        return 1;
    }

    node = vsscript_getOutput(se, outputIndex);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node. Invalid index specified?\n");
       vsscript_freeScript(se);
       vsscript_finalize();
       return 1;
    }

    bool error = false;
    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    if (showInfo) {
        fprintf(outFile, "Width: %d\n", vi->width);
        fprintf(outFile, "Height: %d\n", vi->height);
        fprintf(outFile, "Frames: %d\n", vi->numFrames);
        fprintf(outFile, "FPS: %" PRId64 "/%" PRId64 "\n", vi->fpsNum, vi->fpsDen);

        if (vi->format) {
            fprintf(outFile, "Format Name: %s\n", vi->format->name);
            fprintf(outFile, "Color Family: %s\n", colorFamilyToString(vi->format->colorFamily));
            fprintf(outFile, "Bits: %d\n", vi->format->bitsPerSample);
            fprintf(outFile, "SubSampling W: %d\n", vi->format->subSamplingW);
            fprintf(outFile, "SubSampling H: %d\n", vi->format->subSamplingH);
        } else {
            fprintf(outFile, "Format Name: Variable\n");
        }
    } else {
        if (!isConstantFormat(vi) || vi->numFrames == 0) {
            fprintf(stderr, "Cannot output clips with varying dimensions or unknown length\n");
            vsapi->freeNode(node);
            vsscript_freeScript(se);
            vsscript_finalize();
            return 1;
        }

        lastFpsReportTime = std::chrono::high_resolution_clock::now();;
        error = outputNode();
    }

    fflush(outFile);
    std::chrono::duration<double> elapsedSeconds = std::chrono::high_resolution_clock::now() - start;
    fprintf(stderr, "Output %d frames in %.2f seconds (%.2f fps)\n", outputFrames, elapsedSeconds.count(), outputFrames / elapsedSeconds.count());
    vsapi->freeNode(node);
    vsscript_freeScript(se);
    vsscript_finalize();

    return error;
}
