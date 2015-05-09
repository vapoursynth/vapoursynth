/*
* Copyright (c) 2013-2015 Fredrik Mellbin
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

#include "VapourSynth.h"
#include "VSHelper.h"
#include "VSScript.h"
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <locale>
#include <sstream>
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
#include <windows.h>
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

const VSAPI *vsapi = nullptr;
VSScript *se = nullptr;
VSNodeRef *node = nullptr;
FILE *outFile = nullptr;
FILE *timecodesFile = nullptr;

int requests = 0;
int outputIndex = 0;
int outputFrames = 0;
int requestedFrames = 0;
int completedFrames = 0;
int totalFrames = -1;
int numPlanes = 0;
bool y4m = false;
bool timecodes = false;
int64_t currentTimecodeNum = 0;
int64_t currentTimecodeDen = 1;
bool outputError = false;
bool showInfo = false;
bool showVersion = false;
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

static inline void addRational(int64_t *num, int64_t *den, int64_t addnum, int64_t addden) {
    if (*den == addden) {
        *num += addnum;
    } else {
        int64_t temp = addden;
        addnum *= *den;
        addden *= *den;
        *num *= temp;
        *den *= temp;

        *num += addnum;

        // Simplify
        muldivRational(num, den, 1, 1);
    }
}

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
                            if (fwrite(readPtr, 1, rowSize, outFile) != static_cast<size_t>(rowSize)) {
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

                if (timecodes && !outputError) {
                    std::ostringstream stream;
                    stream.imbue(std::locale("C"));
                    stream.setf(std::ios::fixed, std::ios::floatfield);
                    stream << (currentTimecodeNum * 1000 / static_cast<double>(currentTimecodeDen));
                    if (fprintf(timecodesFile, "%s\n", stream.str().c_str()) < 0) {
                        if (errorMessage.empty())
                            errorMessage = "Error: failed to write timecode for frame " + std::to_string(outputFrames) + ". errno: " + std::to_string(errno);
                        totalFrames = requestedFrames;
                        outputError = true;
                    } else {
                        const VSMap *props = vsapi->getFramePropsRO(frame);
                        int err_num, err_den;
                        int64_t duration_num = vsapi->propGetInt(props, "_DurationNum", 0, &err_num);
                        int64_t duration_den = vsapi->propGetInt(props, "_DurationDen", 0, &err_den);

                        if (err_num || err_den || !duration_den) {
                            if (errorMessage.empty()) {
                                if (err_num || err_den)
                                    errorMessage = "Error: missing duration at frame ";
                                else if (!duration_den)
                                    errorMessage = "Error: duration denominator is zero at frame ";
                                errorMessage += std::to_string(outputFrames);
                            }

                            totalFrames = requestedFrames;
                            outputError = true;
                        } else {
                            addRational(&currentTimecodeNum, &currentTimecodeDen, duration_num, duration_den);
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
        vsapi->getFrameAsync(requestedFrames, node, frameDoneCallback, nullptr);
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

    if (y4m && (vi->format->colorFamily != cmGray && vi->format->colorFamily != cmYUV)) {
        errorMessage = "Error: Can only apply y4m headers to YUV and Gray format clips";
        fprintf(stderr, "%s\n", errorMessage.c_str());
        return true;
    }

    std::string y4mFormat;

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

    if (timecodes && !outputError) {
        if (fprintf(timecodesFile, "# timecode format v2\n") < 0) {
            errorMessage = "Error: failed to write timecodes file header, errno: " + std::to_string(errno);
            outputError = true;
            return outputError;
        }
    }

    std::unique_lock<std::mutex> lock(mutex);

    int requestStart = completedFrames;
    int intitalRequestSize = std::min(requests, totalFrames - requestStart);
    requestedFrames = requestStart + intitalRequestSize;
    for (int n = requestStart; n < requestStart + intitalRequestSize; n++)
        vsapi->getFrameAsync(n, node, frameDoneCallback, nullptr);

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

bool printVersion() {
    if (!vsscript_init()) {
        fprintf(stderr, "Failed to initialize VapourSynth environment\n");
        return false;
    }

    vsapi = vsscript_getVSApi();
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        vsscript_finalize();
        return false;
    }

    VSCore *core = vsapi->createCore(1);
    if (!core) {
        fprintf(stderr, "Failed to create core\n");
        vsscript_finalize();
        return false;
    }

    const VSCoreInfo *info = vsapi->getCoreInfo(core);
    printf("%s", info->versionString);
    vsapi->freeCore(core);
    vsscript_finalize();
    return true;
}

void printHelp() {
    fprintf(stderr,
        "VSPipe usage:\n"
        "  vspipe [options] <script> <outfile>\n"
        "\n"
        "Available options:\n"
        "  -a, --arg key=value   Argument to pass to the script environment\n"
        "  -s, --start N         Set output frame range (first frame)\n"
        "  -e, --end N           Set output frame range (last frame)\n"
        "  -o, --outputindex N   Select output index\n"
        "  -r, --requests N      Set number of concurrent frame requests\n"
        "  -y, --y4m             Add YUV4MPEG headers to output\n"
        "  -t, --timecodes FILE  Write timecodes v2 file\n"
        "  -p, --progress        Print progress to stderr\n"
        "  -i, --info            Show video info and exit\n"
        "  -v, --version         Show version info and exit\n"
        "\n"
        "Examples:\n"
        "  Show script info:\n"
        "    vspipe --info script.vpy -\n"
        "  Write to stdout:\n"
        "    vspipe [options] script.vpy -\n"
        "  Write frames 5-100 to file:\n"
        "    vspipe --start 5 --end 100 script.vpy output.raw\n"
        "  Pass values to a script:\n"
        "    vspipe --arg deinterlace=yes --arg \"message=fluffy kittens\" script.vpy output.raw\n"
        "  Pipe to x264 and write timecodes file:\n"
        "    vspipe script.vpy - --y4m --timecodes timecodes.txt | x264 --demuxer y4m -o script.mkv -\n"
        );
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
    nstring outputFilename, scriptFilename, timecodesFilename;
    bool showHelp = false;
    std::map<std::string, std::string> scriptArgs;
    int startFrame = 0;

    for (int arg = 1; arg < argc; arg++) {
        nstring argString = argv[arg];
        if (argString == NSTRING("-v") || argString == NSTRING("--version")) {
            showVersion = true;
        } else if (argString == NSTRING("-y") || argString == NSTRING("--y4m")) {
            y4m = true;
        } else if (argString == NSTRING("-p") || argString == NSTRING("--progress")) {
            printFrameNumber = true;
        } else if (argString == NSTRING("-i") || argString == NSTRING("--info")) {
            showInfo = true;
        } else if (argString == NSTRING("-h") || argString == NSTRING("--help")) {
            showHelp = true;
        } else if (argString == NSTRING("-s") || argString == NSTRING("--start")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No start frame specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], startFrame)) {
                fprintf(stderr, "Couldn't convert %s to an integer (start)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (startFrame < 0) {
                fprintf(stderr, "Negative start frame specified\n");
                return 1;
            }

            completedFrames = startFrame;
            outputFrames = startFrame;
            requestedFrames = startFrame;
            lastFpsReportFrame = startFrame;

            arg++;
        } else if (argString == NSTRING("-e") || argString == NSTRING("--end")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No end frame specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], totalFrames)) {
                fprintf(stderr, "Couldn't convert %s to an integer (end)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (totalFrames < 0) {
                fprintf(stderr, "Negative end frame specified\n");
                return 1;
            }

            totalFrames++;
            arg++;
        } else if (argString == NSTRING("-o") || argString == NSTRING("--outputindex")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No output index specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], outputIndex)) {
                fprintf(stderr, "Couldn't convert %s to an integer (index)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }
            arg++;
        } else if (argString == NSTRING("-r") || argString == NSTRING("--requests")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "Number of requests not specified\n");
                return 1;
            }
            if (!nstringToInt(argv[arg + 1], requests)) {
                fprintf(stderr, "Couldn't convert %s to an integer (requests)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }
            arg++;
        } else if (argString == NSTRING("-a") || argString == NSTRING("--arg")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No argument specified\n");
                return 1;
            }

            std::string aLine = nstringToUtf8(argv[arg + 1]).c_str();
            size_t equalsPos = aLine.find("=");
            if (equalsPos == std::string::npos) {
                fprintf(stderr, "No value specified for argument: %s\n", aLine.c_str());
                return 1;
            }

            scriptArgs[aLine.substr(0, equalsPos)] = aLine.substr(equalsPos + 1);

            arg++;
        } else if (argString == NSTRING("-t") || argString == NSTRING("--timecodes")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No timecodes file specified\n");
                return 1;
            }

            timecodes = true;
            timecodesFilename = argv[arg + 1];

            arg++;
        } else if (scriptFilename.empty() && !argString.empty() && argString.substr(0, 1) != NSTRING("-")) {
            scriptFilename = argString;
        } else if (outputFilename.empty() && !argString.empty() && (argString == NSTRING("-") || (argString.substr(0, 1) != NSTRING("-")))) {
            outputFilename = argString;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", nstringToUtf8(argString).c_str());
            return 1;
        }
    }

    if (showVersion && argc > 2) {
        fprintf(stderr, "Cannot combine version information with other options\n");
        return 1;
    } else if (showVersion) {
        return printVersion() ? 0 : 1;
    } else if (showHelp || argc <= 1) {
        printHelp();
        return 1;
    } else if (scriptFilename.empty()) {
        fprintf(stderr, "No script file specified\n");
        return 1;
    } else if (outputFilename.empty()) {
        fprintf(stderr, "No output file specified\n");
        return 1;
    }

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

    if (timecodes) {
#ifdef VS_TARGET_OS_WINDOWS
        timecodesFile = _wfopen(timecodesFilename.c_str(), L"wb");
#else
        timecodesFile = fopen(timecodesFilename.c_str(), "wb");
#endif
        if (!timecodesFile) {
            fprintf(stderr, "Failed to open timecodes file for writing\n");
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
    
    // Should always succeed
    if (vsscript_createScript(&se)) {
        fprintf(stderr, "Script environment initialization failed:\n%s\n", vsscript_getError(se));
        vsscript_freeScript(se);
        vsscript_finalize();
        return 1;
    }

    {
        VSMap *foldedArgs = vsapi->createMap();
        for (const auto &iter : scriptArgs)
            vsapi->propSetData(foldedArgs, iter.first.c_str(), iter.second.c_str(), static_cast<int>(iter.second.size()), paAppend);
        vsscript_setVariable(se, foldedArgs);
        vsapi->freeMap(foldedArgs);
    }

    start = std::chrono::high_resolution_clock::now();
    if (vsscript_evaluateFile(&se, nstringToUtf8(scriptFilename).c_str(), efSetWorkingDir)) {
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
        if (vi->width && vi->height) {
            fprintf(outFile, "Width: %d\n", vi->width);
            fprintf(outFile, "Height: %d\n", vi->height);
        } else {
            fprintf(outFile, "Width: Variable\n");
            fprintf(outFile, "Height: Variable\n");
        }
        fprintf(outFile, "Frames: %d\n", vi->numFrames);
        if (vi->fpsNum && vi->fpsDen)
            fprintf(outFile, "FPS: %" PRId64 "/%" PRId64 " (%.3f fps)\n", vi->fpsNum, vi->fpsDen, vi->fpsNum/static_cast<double>(vi->fpsDen));
        else
            fprintf(outFile, "FPS: Variable\n");

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
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);
        if (totalFrames == -1)
            totalFrames = vi->numFrames;
        if ((vi->numFrames && vi->numFrames < totalFrames) || completedFrames >= totalFrames) {
            fprintf(stderr, "Invalid range of frames to output specified:\nfirst: %d\nlast: %d\nclip length: %d\nframes to output: %d\n", completedFrames, totalFrames, vi->numFrames, totalFrames - completedFrames);
            vsapi->freeNode(node);
            vsscript_freeScript(se);
            vsscript_finalize();
            return 1;
        }

        if (!isConstantFormat(vi)) {
            fprintf(stderr, "Cannot output clips with varying dimensions\n");
            vsapi->freeNode(node);
            vsscript_freeScript(se);
            vsscript_finalize();
            return 1;
        }

        lastFpsReportTime = std::chrono::high_resolution_clock::now();;
        error = outputNode();
    }

    fflush(outFile);
    if (timecodesFile)
        fclose(timecodesFile);

    if (!showInfo) {
        int totalFrames = outputFrames - startFrame;
        std::chrono::duration<double> elapsedSeconds = std::chrono::high_resolution_clock::now() - start;
        fprintf(stderr, "Output %d frames in %.2f seconds (%.2f fps)\n", totalFrames, elapsedSeconds.count(), totalFrames / elapsedSeconds.count());
    }
    vsapi->freeNode(node);
    vsscript_freeScript(se);
    vsscript_finalize();

    return error ? 1 : 0;
}
