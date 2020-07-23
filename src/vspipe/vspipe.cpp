/*
* Copyright (c) 2013-2020 Fredrik Mellbin
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

#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSScript4.h"
#include "../core/version.h"
#include "printgraph.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <locale>
#include <sstream>
#include "../common/wave.h"
#ifdef VS_TARGET_OS_WINDOWS
#include <io.h>
#include <fcntl.h>
#include "../common/vsutf16.h"
#endif

#define __STDC_FORMAT_MACROS
#include <cstdio>
#include <cinttypes>

#ifdef VS_USE_MIMALLOC
#   include <mimalloc-new-delete.h>
#endif

// fixme, don't allow info/graph and output options to be combined
// fixme, install a message handler (needs vsscript fix)
// fixme, refactor to not use global variables everywhere so it's a better code sample
// fixme, add a second less verbose graph mode only showing top level invoke calls

// Needed so windows doesn't drool on itself when ctrl-c is pressed
#ifdef VS_TARGET_OS_WINDOWS
#define NOMINMAX
#include <windows.h>
static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
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
    return utf16_to_utf8(s);
}
#else
typedef std::string nstring;
#define NSTRING(x) x
std::string nstringToUtf8(const nstring &s) {
    return s;
}
#endif

using namespace vsh;

/////////////////////////////////////////////

static const VSAPI *vsapi = nullptr;
static const VSSCRIPTAPI *vssapi = nullptr;
static VSScript *se = nullptr;
static VSNodeRef *node = nullptr;
static VSNodeRef *alphaNode = nullptr;
static FILE *outFile = nullptr;
static FILE *timecodesFile = nullptr;

static int requests = 0;
static int outputIndex = 0;
static int outputFrames = 0;
static int requestedFrames = 0;
static int completedFrames = 0;
static int completedAlphaFrames = 0;
static int totalFrames = -1;
static int64_t totalSamples = -1;
static int64_t startPos = 0;
static int64_t endPos = -1;
static bool y4m = false;
static bool w64 = false;
static bool wav = false;
static int64_t currentTimecodeNum = 0;
static int64_t currentTimecodeDen = 1;
static bool outputError = false;
static bool showInfo = false;
static bool showGraph = false;
static bool preserveCwd = false;
static bool showVersion = false;
static bool printFrameNumber = false;
static double fps = 0;
static bool hasMeaningfulFPS = false;
static std::map<int, std::pair<const VSFrameRef *, const VSFrameRef *>> reorderMap;

static std::string errorMessage;
static std::condition_variable condition;
static std::mutex mutex;
static std::vector<uint8_t> buffer;

static std::chrono::time_point<std::chrono::steady_clock> startTime;
static std::chrono::time_point<std::chrono::steady_clock> lastFPSReportTime;

static std::string channelMaskToName(uint64_t v) {
    std::string s;
    auto checkConstant = [&s, v](uint64_t c, const char *name) {
        if ((static_cast<uint64_t>(1) << c) & v) {
            if (!s.empty())
                s += ", ";
            s += name;
        }
    };

    checkConstant(acFrontLeft, "Front Left");
    checkConstant(acFrontRight, "Front Right");
    checkConstant(acFrontCenter, "Center");
    checkConstant(acLowFrequency, "LFE");
    checkConstant(acBackLeft, "Back Left");
    checkConstant(acBackRight, "Back Right");
    checkConstant(acFrontLeftOFCenter, "Front Left of Center");
    checkConstant(acFrontRightOFCenter, "Front Right of Center");
    checkConstant(acBackCenter, "Back Center");
    checkConstant(acSideLeft, "Side Left");
    checkConstant(acSideRight, "Side Right");
    checkConstant(acTopCenter, "Top Center");
    checkConstant(acTopFrontLeft, "Top Front Left");
    checkConstant(acTopFrontCenter, "Top Front Center");
    checkConstant(acTopFrontRight, "Top Front Right");
    checkConstant(acTopBackLeft, "Top Back Left");
    checkConstant(acTopBackCenter, "Top Back Center");
    checkConstant(acTopBackRight, "Top Back Right");
    checkConstant(acStereoLeft, "Stereo Left");
    checkConstant(acStereoRight, "Stereo Right");
    checkConstant(acWideLeft, "Wide Left");
    checkConstant(acWideRight, "Wide Right");
    checkConstant(acSurroundDirectLeft, "Surround Direct Left");
    checkConstant(acSurroundDirectRight, "Surround Direct Right");
    checkConstant(acLowFrequency2, "LFE2");

    return s;
}

static bool isCompletedFrame(const std::pair<const VSFrameRef *, const VSFrameRef *> &f) {
    return (f.first && (!alphaNode || f.second));
}

template<typename T>
static size_t interleaveSamples(const VSFrameRef *frame, uint8_t *dstBuf) {
    const VSAudioFormat *fi = vsapi->getAudioFrameFormat(frame);
    T *dstBuffer = reinterpret_cast<T *>(dstBuf);

    size_t numChannels = fi->numChannels;
    size_t numSamples = vsapi->getFrameLength(frame);

    std::vector<const T *> srcPtrs;
    srcPtrs.reserve(numChannels);
    for (int channel = 0; channel < numChannels; channel++)
        srcPtrs.push_back(reinterpret_cast<const T *>(vsapi->getReadPtr(frame, channel)));
    const T **srcPtrsBuffer = srcPtrs.data();

    for (int sample = 0; sample < numSamples; sample++) {
        for (int channel = 0; channel < numChannels; channel++) {
            *dstBuffer = *srcPtrs[channel];
            ++srcPtrs[channel];
            ++dstBuffer;
        }
    }

    return numSamples * numChannels * sizeof(T);
}

static void outputFrame(const VSFrameRef *frame) {
    if (!outputError && outFile) {
        if (vsapi->getFrameType(frame) == mtVideo) {
            const VSVideoFormat *fi = vsapi->getVideoFrameFormat(frame);
            const int rgbRemap[] = { 1, 2, 0 };
            for (int rp = 0; rp < fi->numPlanes; rp++) {
                int p = (fi->colorFamily == cfRGB) ? rgbRemap[rp] : rp;
                ptrdiff_t stride = vsapi->getStride(frame, p);
                const uint8_t *readPtr = vsapi->getReadPtr(frame, p);
                int rowSize = vsapi->getFrameWidth(frame, p) * fi->bytesPerSample;
                int height = vsapi->getFrameHeight(frame, p);

                if (rowSize != stride) {
                    bitblt(buffer.data(), rowSize, readPtr, stride, rowSize, height);
                    readPtr = buffer.data();
                }

                if (fwrite(readPtr, 1, rowSize * height, outFile) != static_cast<size_t>(rowSize * height)) {
                    if (errorMessage.empty())
                        errorMessage = "Error: fwrite() call failed when writing frame: " + std::to_string(outputFrames) + ", plane: " + std::to_string(p) +
                        ", errno: " + std::to_string(errno);
                    totalFrames = requestedFrames;
                    outputError = true;
                    break;
                }
            }
        } else if (vsapi->getFrameType(frame) == mtAudio) {
            const VSAudioFormat *fi = vsapi->getAudioFrameFormat(frame);

            size_t numChannels = fi->numChannels;
            size_t numSamples = vsapi->getFrameLength(frame);
            size_t bytesPerOutputSample = (fi->bitsPerSample + 7) / 8;
            size_t toOutput = bytesPerOutputSample * numSamples * numChannels;

            std::vector<const uint8_t *> srcPtrs;
            srcPtrs.reserve(numChannels);
            for (int channel = 0; channel < numChannels; channel++)
                srcPtrs.push_back(vsapi->getReadPtr(frame, channel));
            
            if (bytesPerOutputSample == 2)
                PackChannels16to16le(srcPtrs.data(), buffer.data(), numSamples, numChannels);
            else if (bytesPerOutputSample == 3)
                PackChannels32to24le(srcPtrs.data(), buffer.data(), numSamples, numChannels);
            else if (bytesPerOutputSample == 4)
                PackChannels32to32le(srcPtrs.data(), buffer.data(), numSamples, numChannels);

            if (fwrite(buffer.data(), 1, toOutput, outFile) != toOutput) {
                if (errorMessage.empty())
                    errorMessage = "Error: fwrite() call failed when writing frame: " + std::to_string(outputFrames) + ", errno: " + std::to_string(errno);
                totalFrames = requestedFrames;
                outputError = true;
            }
        }
    }
}

static void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *rnode, const char *errorMsg) {
    bool printToConsole = false;
    if (printFrameNumber) {
        printToConsole = (n == 0);

        std::chrono::time_point<std::chrono::steady_clock> currentTime(std::chrono::steady_clock::now());
        std::chrono::duration<double> elapsedSeconds = currentTime - lastFPSReportTime;
        std::chrono::duration<double> elapsedSecondsFromStart = currentTime - startTime;

        if (elapsedSeconds.count() > .5) {
            printToConsole = true;
            lastFPSReportTime = currentTime;
        }

        if (elapsedSecondsFromStart.count() > 8) {
            hasMeaningfulFPS = true;
            fps = completedFrames / elapsedSeconds.count();
        }
    }

    // completed frames simply correspond to how many times the completion callback is called
    if (rnode == node) {
        completedFrames++;
        if (!alphaNode)
            completedAlphaFrames++;
    } else {
        completedAlphaFrames++;
    }

    if (f) {
        if (rnode == node)
            reorderMap[n].first = f;
        else
            reorderMap[n].second = f;

        bool completed = isCompletedFrame(reorderMap[n]);

        if (completed && requestedFrames < totalFrames) {
            vsapi->getFrameAsync(requestedFrames, node, frameDoneCallback, nullptr);
            if (alphaNode)
                vsapi->getFrameAsync(requestedFrames, alphaNode, frameDoneCallback, nullptr);
            requestedFrames++;
        }

        while (reorderMap.count(outputFrames) && isCompletedFrame(reorderMap[outputFrames])) {
            const VSFrameRef *frame = reorderMap[outputFrames].first;
            const VSFrameRef *alphaFrame = reorderMap[outputFrames].second;
            reorderMap.erase(outputFrames);
            if (!outputError) {
                if (y4m && outFile) {
                    if (fwrite("FRAME\n", 1, 6, outFile) != 6) {
                        if (errorMessage.empty())
                            errorMessage = "Error: fwrite() call failed when writing header, errno: " + std::to_string(errno);
                        totalFrames = requestedFrames;
                        outputError = true;
                    }
                }

                outputFrame(frame);
                if (alphaFrame)
                    outputFrame(alphaFrame);

                if (timecodesFile && !outputError) {
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
            vsapi->freeFrame(alphaFrame);
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

    if (printToConsole && !outputError) {
        if (vsapi->getNodeType(rnode) == mtVideo) {
            if (hasMeaningfulFPS)
                fprintf(stderr, "Frame: %d/%d (%.2f fps)\r", completedFrames, totalFrames, fps);
            else
                fprintf(stderr, "Frame: %d/%d\r", completedFrames, totalFrames);
        } else {
            if (hasMeaningfulFPS)
                fprintf(stderr, "Sample: %" PRId64 "/%" PRId64 " (%.2f sps)\r", static_cast<int64_t>(completedFrames * VS_AUDIO_FRAME_SAMPLES), static_cast<int64_t>(totalFrames * VS_AUDIO_FRAME_SAMPLES), fps);
            else
                fprintf(stderr, "Sample: %" PRId64 "/%" PRId64 "\r", static_cast<int64_t>(completedFrames * VS_AUDIO_FRAME_SAMPLES), static_cast<int64_t>(totalFrames * VS_AUDIO_FRAME_SAMPLES));
        }
    }

    if (totalFrames == completedFrames && totalFrames == completedAlphaFrames) {
        std::lock_guard<std::mutex> lock(mutex);
        condition.notify_one();
    }
}

static std::string floatBitsToLetter(int bits) {
    switch (bits) {
    case 16:
        return "h";
    case 32:
        return "s";
    case 64:
        return "d";
    default:
        assert(false);
        return "u";
    }
}

static bool initializeVideoOutput() {
    if (wav || w64) {
        fprintf(stderr, "Error: can't apply wave headers to video\n");
        return false;
    }

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    if (y4m && ((vi->format.colorFamily != cfGray && vi->format.colorFamily != cfYUV) || alphaNode)) {
        fprintf(stderr, "Error: can only apply y4m headers to YUV and Gray format clips without alpha\n");
        return false;
    }

    std::string y4mFormat;

    if (y4m) {
        if (vi->format.colorFamily == cfGray) {
            y4mFormat = "mono";
            if (vi->format.bitsPerSample > 8)
                y4mFormat = y4mFormat + std::to_string(vi->format.bitsPerSample);
        } else if (vi->format.colorFamily == cfYUV) {
            if (vi->format.subSamplingW == 1 && vi->format.subSamplingH == 1)
                y4mFormat = "420";
            else if (vi->format.subSamplingW == 1 && vi->format.subSamplingH == 0)
                y4mFormat = "422";
            else if (vi->format.subSamplingW == 0 && vi->format.subSamplingH == 0)
                y4mFormat = "444";
            else if (vi->format.subSamplingW == 2 && vi->format.subSamplingH == 2)
                y4mFormat = "410";
            else if (vi->format.subSamplingW == 2 && vi->format.subSamplingH == 0)
                y4mFormat = "411";
            else if (vi->format.subSamplingW == 0 && vi->format.subSamplingH == 1)
                y4mFormat = "440";
            else {
                fprintf(stderr, "Error: no y4m identifier exists for current format\n");
                return false;
            }

            if (vi->format.bitsPerSample > 8 && vi->format.sampleType == stInteger)
                y4mFormat += "p" + std::to_string(vi->format.bitsPerSample);
            else if (vi->format.sampleType == stFloat)
                y4mFormat += "p" + floatBitsToLetter(vi->format.bitsPerSample);
        } else {
            fprintf(stderr, "Error: no y4m identifier exists for current format\n");
            return false;
        }

        if (!y4mFormat.empty())
            y4mFormat = " C" + y4mFormat;

        std::string header = "YUV4MPEG2" + y4mFormat
            + " W" + std::to_string(vi->width)
            + " H" + std::to_string(vi->height)
            + " F" + std::to_string(vi->fpsNum) + ":" + std::to_string(vi->fpsDen)
            + " Ip A0:0"
            + " XLENGTH=" + std::to_string(vi->numFrames) + "\n";

        if (outFile) {
            if (fwrite(header.c_str(), 1, header.size(), outFile) != header.size()) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    }

    if (timecodesFile && !outputError) {
        if (fprintf(timecodesFile, "# timecode format v2\n") < 0) {
            fprintf(stderr, "Error: failed to write timecodes file header, errno: %d\n", errno);
            return false;
        }
    }

    buffer.resize(vi->width * vi->height * vi->format.bytesPerSample);
    return true;
}

static bool initializeAudioOutput() {
    if (y4m) {
        fprintf(stderr, "Error: can't apply y4m headers to audio\n");
        return false;
    }

    const VSAudioInfo *ai = vsapi->getAudioInfo(node);

    if (w64) {
        Wave64Header header;
        if (!CreateWave64Header(header, ai->format.sampleType == stFloat, ai->format.bitsPerSample, ai->sampleRate, ai->format.channelLayout, ai->numSamples)) {
            fprintf(stderr, "Error: cannot create valid w64 header\n");
            return false;
        }
        if (outFile) {
            if (fwrite(&header, 1, sizeof(header), outFile) != sizeof(header)) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    } else if (wav) {
        WaveHeader header;
        if (!CreateWaveHeader(header, ai->format.sampleType == stFloat, ai->format.bitsPerSample, ai->sampleRate, ai->format.channelLayout, ai->numSamples)) {
            fprintf(stderr, "Error: cannot create valid wav header\n");
            return false;
        }

        if (outFile) {
            if (fwrite(&header, 1, sizeof(header), outFile) != sizeof(header)) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    }

    buffer.resize(ai->format.numChannels * VS_AUDIO_FRAME_SAMPLES * ai->format.bytesPerSample);
    return true;
}

static bool outputNode() {
    if (requests < 1) {
        VSCoreInfo info;
        vsapi->getCoreInfo(vssapi->getCore(se), &info);
        requests = info.numThreads;
    }

    std::unique_lock<std::mutex> lock(mutex);

    int intitalRequestSize = std::min(requests, totalFrames);
    requestedFrames = intitalRequestSize;
    for (int n = 0; n < intitalRequestSize; n++) {
        vsapi->getFrameAsync(n, node, frameDoneCallback, nullptr);
        if (alphaNode)
            vsapi->getFrameAsync(n, alphaNode, frameDoneCallback, nullptr);
    }

    condition.wait(lock);

    if (outputError) {
        for (auto &iter : reorderMap) {
            vsapi->freeFrame(iter.second.first);
            vsapi->freeFrame(iter.second.second);
        }
        fprintf(stderr, "%s\n", errorMessage.c_str());
    }

    return outputError;
}

static const char *colorFamilyToString(int colorFamily) {
    switch (colorFamily) {
    case cfGray: return "Gray";
    case cfRGB: return "RGB";
    case cfYUV: return "YUV";
    case cfCompatBGR32: return "CompatBGR32";
    case cfCompatYUY2: return "CompatYUY2";
    }
    return "";
}

static bool nstringToInt64(const nstring &ns, int64_t &result) {
    size_t pos = 0;
    std::string s = nstringToUtf8(ns);
    try {
        result = std::stoll(s, &pos);
    } catch (std::invalid_argument &) {
        return false;
    } catch (std::out_of_range &) {
        return false;
    }
    return pos == s.length();
}

static bool nstringToInt(const nstring &ns, int &result) {
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

static bool printVersion() {
    vsapi = vssapi->getVSApi(VAPOURSYNTH_API_VERSION);
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        return false;
    }

    VSCore *core = vsapi->createCore(0);
    if (!core) {
        fprintf(stderr, "Failed to create core\n");
        return false;
    }

    VSCoreInfo info;
    vsapi->getCoreInfo(core, &info);
    printf("%s", info.versionString);
    vsapi->freeCore(core);
    return true;
}

static void printHelp() {
    fprintf(stderr,
        "VSPipe R" XSTR(VAPOURSYNTH_CORE_VERSION) " usage:\n"
        "  vspipe [options] <script> <outfile>\n"
        "\n"
        "Available options:\n"
        "  -a, --arg key=value   Argument to pass to the script environment\n"
        "  -s, --start N         Set output frame/sample range start\n"
        "  -e, --end N           Set output frame/sample range end (inclusive)\n"
        "  -o, --outputindex N   Select output index\n"
        "  -r, --requests N      Set number of concurrent frame requests\n"
        "  -y, --y4m             Add YUV4MPEG headers to video output\n"
        "  -w, --w64             Add WAVE64 headers to audio output\n"
        "      --wav             Add WAVE headers to audio output\n"
        "  -t, --timecodes FILE  Write timecodes v2 file\n"
        "  -c  --preserve-cwd    Don't temporarily change the working directory the script path\n"
        "  -p, --progress        Print progress to stderr\n"
        "  -i, --info            Show output node info and exit\n"
        "  -g, --graph           Print output node filter graph in dot format and exit\n"
        "  -v, --version         Show version info and exit\n"
        "\n"
        "Examples:\n"
        "  Show script info:\n"
        "    vspipe --info script.vpy -\n"
        "  Write to stdout:\n"
        "    vspipe [options] script.vpy -\n"
        "  Request all frames but don't output them:\n"
        "    vspipe [options] script.vpy .\n"
        "  Write frames 5-100 to file:\n"
        "    vspipe --start 5 --end 100 script.vpy output.raw\n"
        "  Pass values to a script:\n"
        "    vspipe --arg deinterlace=yes --arg \"message=fluffy kittens\" script.vpy output.raw\n"
        "  Pipe to x264 and write timecodes file:\n"
        "    vspipe script.vpy - --y4m --timecodes timecodes.txt | x264 --demuxer y4m -o script.mkv -\n"
        );
}

#ifdef VS_TARGET_OS_WINDOWS
int wmain(int argc, wchar_t **argv) {
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        fprintf(stderr, "Failed to set stdout to binary mode\n");
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
int main(int argc, char **argv) {
#endif
    vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    if (!vssapi) {
        fprintf(stderr, "Failed to initialize VSScript\n");
        return 1;
    }

    nstring outputFilename, scriptFilename, timecodesFilename;
    bool showHelp = false;
    std::map<std::string, std::string> scriptArgs;

    for (int arg = 1; arg < argc; arg++) {
        nstring argString = argv[arg];
        if (argString == NSTRING("-v") || argString == NSTRING("--version")) {
            showVersion = true;
        } else if (argString == NSTRING("-y") || argString == NSTRING("--y4m")) {
            y4m = true;
        } else if (argString == NSTRING("-w") || argString == NSTRING("--w64")) {
            w64 = true;
        } else if (argString == NSTRING("--wav")) {
            wav = true;
        } else if (argString == NSTRING("-p") || argString == NSTRING("--progress")) {
            printFrameNumber = true;
        } else if (argString == NSTRING("-i") || argString == NSTRING("--info")) {
            showInfo = true;
        } else if (argString == NSTRING("-g") || argString == NSTRING("--graph")) {
            showGraph = true;
        } else if (argString == NSTRING("-h") || argString == NSTRING("--help")) {
            showHelp = true;
        } else if (argString == NSTRING("-c") || argString == NSTRING("--preserve-cwd")) {
            preserveCwd = true;
        } else if (argString == NSTRING("-s") || argString == NSTRING("--start")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No start frame specified\n");
                return 1;
            }

            if (!nstringToInt64(argv[arg + 1], startPos)) {
                fprintf(stderr, "Couldn't convert %s to an integer (start)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (startPos < 0) {
                fprintf(stderr, "Negative start position specified\n");
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-e") || argString == NSTRING("--end")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No end frame specified\n");
                return 1;
            }

            if (!nstringToInt64(argv[arg + 1], endPos)) {
                fprintf(stderr, "Couldn't convert %s to an integer (end)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (endPos < 0) {
                fprintf(stderr, "Negative end frame specified\n");
                return 1;
            }

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

    if (wav && w64) {
        fprintf(stderr, "Cannot combine wave64 and wave headers\n");
        return 1;
    } else if (showInfo && showGraph) {
        fprintf(stderr, "Cannot combine info and graph options\n");
        return 1;
    } else if (showVersion && argc > 2) {
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
    } else if (outputFilename == NSTRING(".")) {
        // do nothing
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

    if (!timecodesFilename.empty()) {
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

    vsapi = vssapi->getVSApi(VAPOURSYNTH_API_VERSION);
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        return 1;
    }

    std::chrono::time_point<std::chrono::steady_clock> scriptEvaluationStart = std::chrono::steady_clock::now();
    
    // Should always succeed
    if (vssapi->createScript(&se)) {
        fprintf(stderr, "Script environment initialization failed:\n%s\n", vssapi->getError(se));
        vssapi->freeScript(se);
        return 1;
    }

    {
        VSMap *foldedArgs = vsapi->createMap();
        for (const auto &iter : scriptArgs)
            vsapi->propSetData(foldedArgs, iter.first.c_str(), iter.second.c_str(), static_cast<int>(iter.second.size()), dtUtf8, paAppend);
        vssapi->setVariable(se, foldedArgs);
        vsapi->freeMap(foldedArgs);
    }

    if (vssapi->evaluateFile(&se, nstringToUtf8(scriptFilename).c_str(), preserveCwd ? 0 : efSetWorkingDir)) {
        fprintf(stderr, "Script evaluation failed:\n%s\n", vssapi->getError(se));
        vssapi->freeScript(se);
        return 1;
    }

    node = vssapi->getOutput(se, outputIndex, &alphaNode);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node. Invalid index specified?\n");
       vssapi->freeScript(se);
       return 1;
    }

    std::chrono::duration<double> scriptEvaluationTime = std::chrono::steady_clock::now() - scriptEvaluationStart;
    if (printFrameNumber)
        fprintf(stderr, "Script evaluation done in %.2f seconds\n", scriptEvaluationTime.count());

    startTime = std::chrono::steady_clock::now();
    bool success = true;

    if (showGraph) {
        fprintf(outFile, "%s\n", printNodeGraph(node, vsapi).c_str());
    } else {

        int nodeType = vsapi->getNodeType(node);

        if (startPos != 0 || endPos != -1) {
            VSMap *args = vsapi->createMap();
            vsapi->propSetNode(args, "clip", node, paAppend);
            if (startPos != 0)
                vsapi->propSetInt(args, "first", startPos, paAppend);
            if (endPos > -1)
                vsapi->propSetInt(args, "last", endPos, paAppend);
            VSMap *result = vsapi->invoke(vsapi->getPluginByID("com.vapoursynth.std", vssapi->getCore(se)), (nodeType == mtVideo) ? "Trim" : "AudioTrim", args);
            vsapi->freeMap(args);
            if (vsapi->getError(result)) {
                fprintf(stderr, "%s\n", vsapi->getError(result));
                vsapi->freeMap(result);
                vsapi->freeNode(node);
                vsapi->freeNode(alphaNode);
                vssapi->freeScript(se);
                return 1;
            } else {
                vsapi->freeNode(node);
                node = vsapi->propGetNode(result, "clip", 0, nullptr);
                vsapi->freeMap(result);
            }
        }

        if (nodeType == mtVideo) {

            const VSVideoInfo *vi = vsapi->getVideoInfo(node);

            if (showInfo) {
                if (outFile) {
                    if (vi->width && vi->height) {
                        fprintf(outFile, "Width: %d\n", vi->width);
                        fprintf(outFile, "Height: %d\n", vi->height);
                    } else {
                        fprintf(outFile, "Width: Variable\n");
                        fprintf(outFile, "Height: Variable\n");
                    }
                    fprintf(outFile, "Frames: %d\n", vi->numFrames);
                    if (vi->fpsNum && vi->fpsDen)
                        fprintf(outFile, "FPS: %" PRId64 "/%" PRId64 " (%.3f fps)\n", vi->fpsNum, vi->fpsDen, vi->fpsNum / static_cast<double>(vi->fpsDen));
                    else
                        fprintf(outFile, "FPS: Variable\n");

                    if (vi->format.colorFamily != cfUndefined) {
                        char nameBuffer[32];
                        vsapi->getVideoFormatName(&vi->format, nameBuffer);
                        fprintf(outFile, "Format Name: %s\n", nameBuffer);
                        fprintf(outFile, "Color Family: %s\n", colorFamilyToString(vi->format.colorFamily));
                        fprintf(outFile, "Alpha: %s\n", alphaNode ? "Yes" : "No");
                        fprintf(outFile, "Sample Type: %s\n", (vi->format.sampleType == stInteger) ? "Integer" : "Float");
                        fprintf(outFile, "Bits: %d\n", vi->format.bitsPerSample);
                        fprintf(outFile, "SubSampling W: %d\n", vi->format.subSamplingW);
                        fprintf(outFile, "SubSampling H: %d\n", vi->format.subSamplingH);
                    } else {
                        fprintf(outFile, "Format Name: Variable\n");
                    }
                }
            } else {
                if (!isConstantVideoFormat(vi)) {
                    fprintf(stderr, "Cannot output clips with varying dimensions\n");
                    vsapi->freeNode(node);
                    vsapi->freeNode(alphaNode);
                    vssapi->freeScript(se);
                    return 1;
                }

                totalFrames = vi->numFrames;

                success = initializeVideoOutput();
                if (success) {
                    lastFPSReportTime = std::chrono::steady_clock::now();
                    success = !outputNode();
                }
            }
        } else if (nodeType == mtAudio) {

            const VSAudioInfo *ai = vsapi->getAudioInfo(node);

            if (showInfo) {
                if (outFile) {
                    char nameBuffer[32];
                    vsapi->getAudioFormatName(&ai->format, nameBuffer);
                    fprintf(outFile, "Samples: %" PRId64 "\n", ai->numSamples);
                    fprintf(outFile, "Sample Rate: %d\n", ai->sampleRate);
                    fprintf(outFile, "Format Name: %s\n", nameBuffer);
                    fprintf(outFile, "Sample Type: %s\n", (ai->format.sampleType == stInteger) ? "Integer" : "Float");
                    fprintf(outFile, "Bits: %d\n", ai->format.bitsPerSample);
                    fprintf(outFile, "Channels: %d\n", ai->format.numChannels);
                    fprintf(outFile, "Layout: %s\n", channelMaskToName(ai->format.channelLayout).c_str());
                }
            } else {
                totalFrames = ai->numFrames;
                totalSamples = ai->numSamples;

                success = initializeAudioOutput();
                if (success) {
                    lastFPSReportTime = std::chrono::steady_clock::now();
                    success = !outputNode();
                }
            }
        }
    }

    if (outFile)
        fflush(outFile);
    if (timecodesFile)
        fclose(timecodesFile);

    if (!showInfo && !showGraph) {
        std::chrono::duration<double> elapsedSeconds = std::chrono::steady_clock::now() - startTime;
        if (vsapi->getNodeType(node) == mtVideo)
            fprintf(stderr, "Output %d frames in %.2f seconds (%.2f fps)\n", totalFrames, elapsedSeconds.count(), totalFrames / elapsedSeconds.count());
        else
            fprintf(stderr, "Output %" PRId64 " samples in %.2f seconds (%.2f sps)\n", totalSamples, elapsedSeconds.count(), (totalFrames / elapsedSeconds.count()) * VS_AUDIO_FRAME_SAMPLES);
    }
    vsapi->freeNode(node);
    vsapi->freeNode(alphaNode);
    vssapi->freeScript(se);

    return success ? 0 : 1;
}
