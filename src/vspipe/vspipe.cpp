/*
* Copyright (c) 2013-2021 Fredrik Mellbin
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
extern "C" {
#include "md5.h"
}
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

// fixme, add a more verbose graph mode with filter times included
// fixme, using a "." for no output is weird

// Needed so windows doesn't drool on itself when ctrl-c is pressed
#ifdef VS_TARGET_OS_WINDOWS
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

enum class VSPipeMode {
    Output,
    PrintVersion,
    PrintHelp,
    PrintInfo,
    PrintSimpleGraph,
    PrintFullGraph
};

enum class VSPipeHeaders {
    None,
    Y4M,
    WAVE,
    WAVE64
};

// Struct used to return the parsed command line options
struct VSPipeOptions {
    VSPipeMode mode = VSPipeMode::Output;
    VSPipeHeaders outputHeaders = VSPipeHeaders::None;
    int64_t startPos = 0;
    int64_t endPos = -1;
    int outputIndex = 0;
    int requests = 0;
    bool printProgress = false;
    bool printFilterTime = false;
    bool calculateMD5 = false;
    nstring scriptFilename;
    nstring outputFilename;
    nstring timecodesFilename;
    std::map<std::string, std::string> scriptArgs;
};

// All state used for outputting frames

struct VSPipeOutputData {
    /* Core fields */
    const VSAPI *vsapi = nullptr;
    VSPipeHeaders outputHeaders = VSPipeHeaders::None;
    FILE *outFile = nullptr;
    VSNode *node = nullptr;
    VSNode *alphaNode = nullptr;

    /* Total number of frames and samples */
    int totalFrames = -1;
    int64_t totalSamples = -1;

    /* Fields used for keeping track of how many frames have been requested and completed and how to reorder them */
    int outputFrames = 0;
    int requestedFrames = 0;
    int completedFrames = 0;
    int completedAlphaFrames = 0;
    std::map<int, std::pair<const VSFrame *, const VSFrame *>> reorderMap;

    /* Error reporting */
    bool outputError = false;
    std::string errorMessage;

    /* Only used to keep the main thread waiting during processing */
    std::condition_variable condition;
    std::mutex mutex;

    /* Buffer used to interleave audio or to pack together video where the rowsize isn't the same as pitch due to multiple calls to stdout being very slow */
    std::vector<uint8_t> buffer;

    /* Statistics */
    bool calculateMD5 = false;
    MD5_CTX md5Ctx = {};
    bool printProgress = false;
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    std::chrono::time_point<std::chrono::steady_clock> lastFPSReportTime;

    /* Timecode output */
    FILE *timecodesFile = nullptr;
    int64_t currentTimecodeNum = 0;
    int64_t currentTimecodeDen = 1;
};

/////////////////////////////////////////////

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

static const char *messageTypeToString(int msgType) {
    switch (msgType) {
        case mtDebug: return "Debug";
        case mtInformation: return "Information";
        case mtWarning: return "Warning";
        case mtCritical: return "Critical";
        case mtFatal: return "Fatal";
        default: return "";
    }
}

static void VS_CC logMessageHandler(int msgType, const char *msg, void *userData) {
#ifdef NDEBUG
    if (msgType >= mtInformation)
#else
    if (msgType >= mtDebug)
#endif
        fprintf(stderr, "%s: %s\n", messageTypeToString(msgType), msg);
}

static bool isCompletedFrame(const std::pair<const VSFrame *, const VSFrame *> &f, bool hasAlpha) {
    return (f.first && (!hasAlpha || f.second));
}

static void outputFrame(const VSFrame *frame, VSPipeOutputData *data) {
    if (!data->outputError && data->outFile) {
        if (data->vsapi->getFrameType(frame) == mtVideo) {
            const VSVideoFormat *fi = data->vsapi->getVideoFrameFormat(frame);
            const int rgbRemap[] = { 1, 2, 0 };
            for (int rp = 0; rp < fi->numPlanes; rp++) {
                int p = (fi->colorFamily == cfRGB) ? rgbRemap[rp] : rp;
                ptrdiff_t stride = data->vsapi->getStride(frame, p);
                const uint8_t *readPtr = data->vsapi->getReadPtr(frame, p);
                int rowSize = data->vsapi->getFrameWidth(frame, p) * fi->bytesPerSample;
                int height = data->vsapi->getFrameHeight(frame, p);

                if (rowSize != stride) {
                    bitblt(data->buffer.data(), rowSize, readPtr, stride, rowSize, height);
                    readPtr = data->buffer.data();
                }

                if (data->calculateMD5)
                    MD5_Update(&data->md5Ctx, readPtr, rowSize * height);

                if (fwrite(readPtr, 1, rowSize * height, data->outFile) != static_cast<size_t>(rowSize * height)) {
                    if (data->errorMessage.empty())
                        data->errorMessage = "Error: fwrite() call failed when writing frame: " + std::to_string(data->outputFrames) + ", plane: " + std::to_string(p) +
                        ", errno: " + std::to_string(errno);
                    data->totalFrames = data->requestedFrames;
                    data->outputError = true;
                    break;
                }
            }
        } else if (data->vsapi->getFrameType(frame) == mtAudio) {
            const VSAudioFormat *fi = data->vsapi->getAudioFrameFormat(frame);

            int numChannels = fi->numChannels;
            int numSamples = data->vsapi->getFrameLength(frame);
            size_t bytesPerOutputSample = (fi->bitsPerSample + 7) / 8;
            size_t toOutput = bytesPerOutputSample * numSamples * numChannels;

            std::vector<const uint8_t *> srcPtrs;
            srcPtrs.reserve(numChannels);
            for (int channel = 0; channel < numChannels; channel++)
                srcPtrs.push_back(data->vsapi->getReadPtr(frame, channel));
            
            if (bytesPerOutputSample == 2)
                PackChannels16to16le(srcPtrs.data(), data->buffer.data(), numSamples, numChannels);
            else if (bytesPerOutputSample == 3)
                PackChannels32to24le(srcPtrs.data(), data->buffer.data(), numSamples, numChannels);
            else if (bytesPerOutputSample == 4)
                PackChannels32to32le(srcPtrs.data(), data->buffer.data(), numSamples, numChannels);

            if (data->calculateMD5)
                MD5_Update(&data->md5Ctx, data->buffer.data(), static_cast<unsigned long>(toOutput));

            if (fwrite(data->buffer.data(), 1, toOutput, data->outFile) != toOutput) {
                if (data->errorMessage.empty())
                    data->errorMessage = "Error: fwrite() call failed when writing frame: " + std::to_string(data->outputFrames) + ", errno: " + std::to_string(errno);
                data->totalFrames = data->requestedFrames;
                data->outputError = true;
            }
        }
    }
}

static void VS_CC frameDoneCallback(void *userData, const VSFrame *f, int n, VSNode *rnode, const char *errorMsg) {
    VSPipeOutputData *data = reinterpret_cast<VSPipeOutputData *>(userData);

    bool printToConsole = false;
    bool hasMeaningfulFPS = false;
    double fps = 0;

    if (data->printProgress) {
        printToConsole = (n == 0);

        std::chrono::time_point<std::chrono::steady_clock> currentTime(std::chrono::steady_clock::now());
        std::chrono::duration<double> elapsedSeconds = currentTime - data->lastFPSReportTime;
        std::chrono::duration<double> elapsedSecondsFromStart = currentTime - data->startTime;

        if (elapsedSeconds.count() > .5) {
            printToConsole = true;
            data->lastFPSReportTime = currentTime;
        }

        if (elapsedSecondsFromStart.count() > 8) {
            hasMeaningfulFPS = true;
            fps = data->completedFrames / elapsedSecondsFromStart.count();
        }
    }

    // completed frames simply correspond to how many times the completion callback is called
    if (rnode == data->node) {
        data->completedFrames++;
        if (!data->alphaNode)
            data->completedAlphaFrames++;
    } else {
        data->completedAlphaFrames++;
    }

    if (f) {
        if (rnode == data->node)
            data->reorderMap[n].first = f;
        else
            data->reorderMap[n].second = f;

        bool completed = isCompletedFrame(data->reorderMap[n], !!data->alphaNode);

        if (completed && data->requestedFrames < data->totalFrames) {
            data->vsapi->getFrameAsync(data->requestedFrames, data->node, frameDoneCallback, userData);
            if (data->alphaNode)
                data->vsapi->getFrameAsync(data->requestedFrames, data->alphaNode, frameDoneCallback, userData);
            data->requestedFrames++;
        }

        while (data->reorderMap.count(data->outputFrames) && isCompletedFrame(data->reorderMap[data->outputFrames], !!data->alphaNode)) {
            const VSFrame *frame = data->reorderMap[data->outputFrames].first;
            const VSFrame *alphaFrame = data->reorderMap[data->outputFrames].second;
            data->reorderMap.erase(data->outputFrames);
            if (!data->outputError) {
                if (data->outputHeaders == VSPipeHeaders::Y4M && data->outFile) {
                    if (fwrite("FRAME\n", 1, 6, data->outFile) != 6) {
                        if (data->errorMessage.empty())
                            data->errorMessage = "Error: fwrite() call failed when writing header, errno: " + std::to_string(errno);
                        data->totalFrames = data->requestedFrames;
                        data->outputError = true;
                    }
                }

                outputFrame(frame, data);
                if (alphaFrame)
                    outputFrame(alphaFrame, data);

                if (data->timecodesFile && !data->outputError) {
                    std::ostringstream stream;
                    stream.imbue(std::locale("C"));
                    stream.setf(std::ios::fixed, std::ios::floatfield);
                    stream << (data->currentTimecodeNum * 1000 / static_cast<double>(data->currentTimecodeDen));
                    if (fprintf(data->timecodesFile, "%s\n", stream.str().c_str()) < 0) {
                        if (data->errorMessage.empty())
                            data->errorMessage = "Error: failed to write timecode for frame " + std::to_string(data->outputFrames) + ". errno: " + std::to_string(errno);
                        data->totalFrames = data->requestedFrames;
                        data->outputError = true;
                    } else {
                        const VSMap *props = data->vsapi->getFramePropertiesRO(frame);
                        int err_num, err_den;
                        int64_t duration_num = data->vsapi->mapGetInt(props, "_DurationNum", 0, &err_num);
                        int64_t duration_den = data->vsapi->mapGetInt(props, "_DurationDen", 0, &err_den);

                        if (err_num || err_den || !duration_den) {
                            if (data->errorMessage.empty()) {
                                if (err_num || err_den)
                                    data->errorMessage = "Error: missing duration at frame ";
                                else if (!duration_den)
                                    data->errorMessage = "Error: duration denominator is zero at frame ";
                                data->errorMessage += std::to_string(data->outputFrames);
                            }

                            data->totalFrames = data->requestedFrames;
                            data->outputError = true;
                        } else {
                            addRational(&data->currentTimecodeNum, &data->currentTimecodeDen, duration_num, duration_den);
                        }
                    }
                }
            }
            data->vsapi->freeFrame(frame);
            data->vsapi->freeFrame(alphaFrame);
            data->outputFrames++;
        }
    } else {
        data->outputError = true;
        data->totalFrames = data->requestedFrames;
        if (data->errorMessage.empty()) {
            if (errorMsg)
                data->errorMessage = "Error: Failed to retrieve frame " + std::to_string(n) + " with error: " + errorMsg;
            else
                data->errorMessage = "Error: Failed to retrieve frame " + std::to_string(n);
        }
    }

    if (printToConsole && !data->outputError) {
        if (data->vsapi->getNodeType(rnode) == mtVideo) {
            if (hasMeaningfulFPS)
                fprintf(stderr, "Frame: %d/%d (%.2f fps)\r", data->completedFrames, data->totalFrames, fps);
            else
                fprintf(stderr, "Frame: %d/%d\r", data->completedFrames, data->totalFrames);
        } else {
            if (hasMeaningfulFPS)
                fprintf(stderr, "Sample: %" PRId64 "/%" PRId64 " (%.2f sps)\r", static_cast<int64_t>(data->completedFrames * VS_AUDIO_FRAME_SAMPLES), static_cast<int64_t>(data->totalFrames * VS_AUDIO_FRAME_SAMPLES), fps);
            else
                fprintf(stderr, "Sample: %" PRId64 "/%" PRId64 "\r", static_cast<int64_t>(data->completedFrames * VS_AUDIO_FRAME_SAMPLES), static_cast<int64_t>(data->totalFrames * VS_AUDIO_FRAME_SAMPLES));
        }
    }

    if (data->totalFrames == data->completedFrames && data->totalFrames == data->completedAlphaFrames) {
        std::lock_guard<std::mutex> lock(data->mutex);
        data->condition.notify_one();
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

static bool initializeVideoOutput(VSPipeOutputData *data) {
    if (data->outputHeaders != VSPipeHeaders::None && data->outputHeaders != VSPipeHeaders::Y4M) {
        fprintf(stderr, "Error: can't apply selected header type to video\n");
        return false;
    }

    const VSVideoInfo *vi = data->vsapi->getVideoInfo(data->node);

    if (data->outputHeaders == VSPipeHeaders::Y4M && ((vi->format.colorFamily != cfGray && vi->format.colorFamily != cfYUV) || data->alphaNode)) {
        fprintf(stderr, "Error: can only apply y4m headers to YUV and Gray format clips without alpha\n");
        return false;
    }

    std::string y4mFormat;

    if (data->outputHeaders == VSPipeHeaders::Y4M) {
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

        if (data->outFile) {
            if (fwrite(header.c_str(), 1, header.size(), data->outFile) != header.size()) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    }

    if (data->timecodesFile && !data->outputError) {
        if (fprintf(data->timecodesFile, "# timecode format v2\n") < 0) {
            fprintf(stderr, "Error: failed to write timecodes file header, errno: %d\n", errno);
            return false;
        }
    }

    data->buffer.resize(vi->width * vi->height * vi->format.bytesPerSample);
    return true;
}

static bool initializeAudioOutput(VSPipeOutputData *data) {
    if (data->outputHeaders != VSPipeHeaders::None && data->outputHeaders != VSPipeHeaders::WAVE && data->outputHeaders != VSPipeHeaders::WAVE64) {
        fprintf(stderr, "Error: can't apply apply selected header type to audio\n");
        return false;
    }

    const VSAudioInfo *ai = data->vsapi->getAudioInfo(data->node);

    if (data->outputHeaders == VSPipeHeaders::WAVE64) {
        Wave64Header header;
        if (!CreateWave64Header(header, ai->format.sampleType == stFloat, ai->format.bitsPerSample, ai->sampleRate, ai->format.channelLayout, ai->numSamples)) {
            fprintf(stderr, "Error: cannot create valid w64 header\n");
            return false;
        }
        if (data->outFile) {
            if (fwrite(&header, 1, sizeof(header), data->outFile) != sizeof(header)) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    } else if (data->outputHeaders == VSPipeHeaders::WAVE) {
        WaveHeader header;
        if (!CreateWaveHeader(header, ai->format.sampleType == stFloat, ai->format.bitsPerSample, ai->sampleRate, ai->format.channelLayout, ai->numSamples)) {
            fprintf(stderr, "Error: cannot create valid wav header\n");
            return false;
        }

        if (data->outFile) {
            if (fwrite(&header, 1, sizeof(header), data->outFile) != sizeof(header)) {
                fprintf(stderr, "Error: fwrite() call failed when writing initial header, errno: %d\n", errno);
                return false;
            }
        }
    }

    data->buffer.resize(ai->format.numChannels * VS_AUDIO_FRAME_SAMPLES * ai->format.bytesPerSample);
    return true;
}

static bool outputNode(const VSPipeOptions &opts, VSPipeOutputData *data, VSCore *core) {
    int requests = opts.requests;
    if (requests < 1) {
        VSCoreInfo info;
        data->vsapi->getCoreInfo(core, &info);
        requests = info.numThreads;
    }

    data->startTime = std::chrono::steady_clock::now();
    data->lastFPSReportTime = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(data->mutex);

    int intitalRequestSize = std::min(requests, data->totalFrames);
    data->requestedFrames = intitalRequestSize;
    for (int n = 0; n < intitalRequestSize; n++) {
        data->vsapi->getFrameAsync(n, data->node, frameDoneCallback, data);
        if (data->alphaNode)
            data->vsapi->getFrameAsync(n, data->alphaNode, frameDoneCallback, data);
    }

    data->condition.wait(lock, [&]() { return data->totalFrames == data->completedFrames && data->totalFrames == data->completedAlphaFrames; });

    if (data->outputError) {
        for (auto &iter : data->reorderMap) {
            data->vsapi->freeFrame(iter.second.first);
            data->vsapi->freeFrame(iter.second.second);
        }
        fprintf(stderr, "%s\n", data->errorMessage.c_str());
    }

    return data->outputError;
}

static const char *colorFamilyToString(int colorFamily) {
    switch (colorFamily) {
    case cfGray: return "Gray";
    case cfRGB: return "RGB";
    case cfYUV: return "YUV";
    }
    return "Error";
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

static bool printVersion(const VSAPI *vsapi) {
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
        "  -a, --arg key=value              Argument to pass to the script environment\n"
        "  -s, --start N                    Set output frame/sample range start\n"
        "  -e, --end N                      Set output frame/sample range end (inclusive)\n"
        "  -o, --outputindex N              Select output index\n"
        "  -r, --requests N                 Set number of concurrent frame requests\n"
        "  -c, --container <y4m/wav/w64>    Add headers for the specified format to the output\n"
        "  -t, --timecodes FILE             Write timecodes v2 file\n"
        "  -p, --progress                   Print progress to stderr\n"
        "      --filter-time                Prints time spent in individual filters after processing\n"
        "  -i, --info                       Show output node info and exit\n"
        "  -g  --graph <simple/full>        Print output node filter graph in dot format and exit\n"
        "  -v, --version                    Show version info and exit\n"
        "\n"
        "Examples:\n"
        "  Show script info:\n"
        "    vspipe --info script.vpy\n"
        "  Write to stdout:\n"
        "    vspipe [options] script.vpy -\n"
        "  Request all frames but don't output them:\n"
        "    vspipe [options] script.vpy .\n"
        "  Write frames 5-100 to file:\n"
        "    vspipe --start 5 --end 100 script.vpy output.raw\n"
        "  Pass values to a script:\n"
        "    vspipe --arg deinterlace=yes --arg \"message=fluffy kittens\" script.vpy output.raw\n"
        "  Pipe to x264 and write timecodes file:\n"
        "    vspipe script.vpy - -c y4m --timecodes timecodes.txt | x264 --demuxer y4m -o script.mkv -\n"
        );
}

template<typename T>
static int parseOptions(VSPipeOptions &opts, int argc, T **argv) {
    for (int arg = 1; arg < argc; arg++) {
        nstring argString = argv[arg];
        if (argString == NSTRING("-v") || argString == NSTRING("--version")) {
            if (argc > 2) {
                fprintf(stderr, "Cannot combine version information with other options\n");
                return 1;
            }

            opts.mode = VSPipeMode::PrintVersion;
        } else if (argString == NSTRING("-c") || argString == NSTRING("--container")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No container type specified\n");
                return 1;
            }

            if (nstringToUtf8(argv[arg + 1]) == "y4m") {
                opts.outputHeaders = VSPipeHeaders::Y4M;
            } else if (nstringToUtf8(argv[arg + 1]) == "wav") {
                opts.outputHeaders = VSPipeHeaders::WAVE;
            } else if (nstringToUtf8(argv[arg + 1]) == "w64") {
                opts.outputHeaders = VSPipeHeaders::WAVE64;
            } else {
                fprintf(stderr, "Unknown container type specified: %s\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-y") || argString == NSTRING("--y4m")) { // secret option for comaptibility with V3
            fprintf(stderr, "Deprecated option --y4m specified, use -c y4m instead\n");
            opts.outputHeaders = VSPipeHeaders::Y4M;
        } else if (argString == NSTRING("-p") || argString == NSTRING("--progress")) {
            opts.printProgress = true;
        } else if (argString == NSTRING("--md5")) {
            opts.calculateMD5 = true;
        } else if (argString == NSTRING("--filter-time")) {
            opts.printFilterTime = true;
        } else if (argString == NSTRING("-i") || argString == NSTRING("--info")) {
            if (opts.mode == VSPipeMode::PrintSimpleGraph || opts.mode == VSPipeMode::PrintFullGraph) {
                fprintf(stderr, "Cannot combine graph and info arguments\n");
                return 1;
            }

            opts.mode = VSPipeMode::PrintInfo;
        } else if (argString == NSTRING("-g") || argString == NSTRING("--graph")) {
            if (opts.mode == VSPipeMode::PrintInfo) {
                fprintf(stderr, "Cannot combine graph and info arguments\n");
                return 1;
            }

            if (argc <= arg + 1) {
                fprintf(stderr, "No graph type specified\n");
                return 1;
            }

            if (nstringToUtf8(argv[arg + 1]) == "simple") {
                opts.mode = VSPipeMode::PrintSimpleGraph;
            } else if (nstringToUtf8(argv[arg + 1]) == "full") {
                opts.mode = VSPipeMode::PrintFullGraph;
            } else {
                fprintf(stderr, "Unknown graph type specified: %s\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-h") || argString == NSTRING("--help")) {
            if (argc > 2) {
                fprintf(stderr, "Cannot combine help with other options\n");
                return 1;
            }

            opts.mode = VSPipeMode::PrintHelp;
        } else if (argString == NSTRING("-s") || argString == NSTRING("--start")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No start frame specified\n");
                return 1;
            }

            if (!nstringToInt64(argv[arg + 1], opts.startPos)) {
                fprintf(stderr, "Couldn't convert %s to an integer (start)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (opts.startPos < 0) {
                fprintf(stderr, "Negative start position specified\n");
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-e") || argString == NSTRING("--end")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No end frame specified\n");
                return 1;
            }

            if (!nstringToInt64(argv[arg + 1], opts.endPos)) {
                fprintf(stderr, "Couldn't convert %s to an integer (end)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            if (opts.endPos < 0) {
                fprintf(stderr, "Negative end frame specified\n");
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-o") || argString == NSTRING("--outputindex")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No output index specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], opts.outputIndex)) {
                fprintf(stderr, "Couldn't convert %s to an integer (index)\n", nstringToUtf8(argv[arg + 1]).c_str());
                return 1;
            }

            arg++;
        } else if (argString == NSTRING("-r") || argString == NSTRING("--requests")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "Number of requests not specified\n");
                return 1;
            }

            if (!nstringToInt(argv[arg + 1], opts.requests)) {
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

            opts.scriptArgs[aLine.substr(0, equalsPos)] = aLine.substr(equalsPos + 1);

            arg++;
        } else if (argString == NSTRING("-t") || argString == NSTRING("--timecodes")) {
            if (argc <= arg + 1) {
                fprintf(stderr, "No timecodes file specified\n");
                return 1;
            }

            opts.timecodesFilename = argv[arg + 1];

            arg++;
        } else if (opts.scriptFilename.empty() && !argString.empty() && argString.substr(0, 1) != NSTRING("-")) {
            opts.scriptFilename = argString;
        } else if (opts.outputFilename.empty() && !argString.empty() && (argString == NSTRING("-") || (argString.substr(0, 1) != NSTRING("-")))) {
            opts.outputFilename = argString;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", nstringToUtf8(argString).c_str());
            return 1;
        }
    }

    // Print help if no options provided
    if (argc <= 1)
        opts.mode = VSPipeMode::PrintHelp;

    if ((opts.mode == VSPipeMode::Output || opts.mode == VSPipeMode::PrintInfo || opts.mode == VSPipeMode::PrintSimpleGraph || opts.mode == VSPipeMode::PrintFullGraph) && opts.scriptFilename.empty()) {
        fprintf(stderr, "No script file specified\n");
        return 1;
    } else if (opts.mode == VSPipeMode::Output && opts.outputFilename.empty()) {
        fprintf(stderr, "No output file specified\n");
        return 1;
    }

    return 0;
}

#ifdef VS_TARGET_OS_WINDOWS
int wmain(int argc, wchar_t **argv) {
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        fprintf(stderr, "Failed to set stdout to binary mode\n");
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
int main(int argc, char **argv) {
#endif
    const VSSCRIPTAPI *vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    if (!vssapi) {
        fprintf(stderr, "Failed to initialize VSScript\n");
        return 1;
    }

    const VSAPI *vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        return 1;
    }

    VSPipeOptions opts{};
    int parseResult = parseOptions(opts, argc, argv);
    if (parseResult)
        return parseResult;

    if (opts.mode == VSPipeMode::PrintVersion) {
        return printVersion(vsapi) ? 0 : 1;
    } else if (opts.mode == VSPipeMode::PrintHelp) {
        printHelp();
        return 0;
    }

    FILE *outFile = nullptr;
    bool closeOutFile = false;

    if (opts.outputFilename.empty() || opts.outputFilename == NSTRING("-")) {
        outFile = stdout;
    } else if (opts.outputFilename == NSTRING(".")) {
        // do nothing
    } else {
#ifdef VS_TARGET_OS_WINDOWS
        outFile = _wfopen(opts.outputFilename.c_str(), L"wb");
#else
        outFile = fopen(opts.outputFilename.c_str(), "wb");
#endif
        if (!outFile) {
            fprintf(stderr, "Failed to open output for writing\n");
            return 1;
        }
        closeOutFile = true;
    }

    FILE *timecodesFile = nullptr;
    if (opts.mode == VSPipeMode::Output && !opts.timecodesFilename.empty()) {
#ifdef VS_TARGET_OS_WINDOWS
        timecodesFile = _wfopen(opts.timecodesFilename.c_str(), L"wb");
#else
        timecodesFile = fopen(opts.timecodesFilename.c_str(), "wb");
#endif
        if (!timecodesFile) {
            fprintf(stderr, "Failed to open timecodes file for writing\n");
            return 1;
        }
    }

    vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
    if (!vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API pointer\n");
        return 1;
    }

    std::chrono::time_point<std::chrono::steady_clock> scriptEvaluationStart = std::chrono::steady_clock::now();
    

    VSCore *core = vsapi->createCore((opts.mode == VSPipeMode::PrintSimpleGraph || opts.mode == VSPipeMode::PrintFullGraph || opts.printFilterTime) ? ccfEnableGraphInspection : 0);
    vsapi->addLogHandler(logMessageHandler, nullptr, nullptr, core);
    VSScript *se = vssapi->createScript(core);
    vssapi->evalSetWorkingDir(se, 1);
    if (!opts.scriptArgs.empty()) {
        VSMap *foldedArgs = vsapi->createMap();
        for (const auto &iter : opts.scriptArgs)
            vsapi->mapSetData(foldedArgs, iter.first.c_str(), iter.second.c_str(), static_cast<int>(iter.second.size()), dtUtf8, maAppend);
        vssapi->setVariables(se, foldedArgs);
        vsapi->freeMap(foldedArgs);
    }
    vssapi->evaluateFile(se, nstringToUtf8(opts.scriptFilename).c_str());

    if (vssapi->getError(se)) {
        int code = vssapi->getExitCode(se);
        if (code == 0) code = 1;
        fprintf(stderr, "Script evaluation failed:\n%s\n", vssapi->getError(se));
        vssapi->freeScript(se);
        return code;
    }

    VSNode *node = vssapi->getOutputNode(se, opts.outputIndex);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node. Invalid index specified?\n");
       vssapi->freeScript(se);
       return 1;
    }

    VSNode *alphaNode = vssapi->getOutputAlphaNode(se, opts.outputIndex);

    // disable cache since no frame is ever requested twice
    vsapi->setCacheMode(node, cmForceDisable);
    if (alphaNode)
        vsapi->setCacheMode(alphaNode, cmForceDisable);

    std::chrono::duration<double> scriptEvaluationTime = std::chrono::steady_clock::now() - scriptEvaluationStart;
    if (opts.printProgress)
        fprintf(stderr, "Script evaluation done in %.2f seconds\n", scriptEvaluationTime.count());

    bool success = true;

    if (opts.mode == VSPipeMode::PrintSimpleGraph) {
        std::string graph = printNodeGraph(true, node, vsapi);
        if (outFile)
            fprintf(outFile, "%s\n", graph.c_str());
    } else if (opts.mode == VSPipeMode::PrintFullGraph) {
        std::string graph = printNodeGraph(false, node, vsapi);
        if (outFile)
            fprintf(outFile, "%s\n", graph.c_str());
    } else {
        int nodeType = vsapi->getNodeType(node);

        if (opts.startPos != 0 || opts.endPos != -1) {
            VSMap *args = vsapi->createMap();
            vsapi->mapSetNode(args, "clip", node, maAppend);
            if (opts.startPos != 0)
                vsapi->mapSetInt(args, "first", opts.startPos, maAppend);
            if (opts.endPos > -1)
                vsapi->mapSetInt(args, "last", opts.endPos, maAppend);

            VSPlugin *stdPlugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, vssapi->getCore(se));
            VSMap *result = vsapi->invoke(stdPlugin, (nodeType == mtVideo) ? "Trim" : "AudioTrim", args);

            VSMap *alphaResult;
            if (alphaNode) {
                vsapi->mapSetNode(args, "clip", alphaNode, maReplace);
                alphaResult = vsapi->invoke(stdPlugin, "Trim", args);
            }

            vsapi->freeMap(args);

            VSMap *mapError;

            if (vsapi->mapGetError(result)) {
                mapError = result;
            } else if (vsapi->mapGetError(alphaResult)) {
                mapError = alphaResult;
            }

            if (mapError) {
                fprintf(stderr, "%s\n", vsapi->mapGetError(mapError));
                vsapi->freeMap(mapError);
                vsapi->freeMap(result);
                vsapi->freeMap(alphaResult);
                vsapi->freeNode(node);
                vsapi->freeNode(alphaNode);
                vssapi->freeScript(se);
                return 1;
            } else {
                vsapi->freeNode(node);
                node = vsapi->mapGetNode(result, "clip", 0, nullptr);
                if (alphaResult)
                    alphaNode = vsapi->mapGetNode(alphaResult, "clip", 0, nullptr);
                vsapi->freeMap(result);
                vsapi->freeMap(alphaResult);
            }
        }

        std::unique_ptr<VSPipeOutputData> data(new VSPipeOutputData());

        data->vsapi = vsapi;
        data->outputHeaders = opts.outputHeaders;
        data->calculateMD5 = opts.calculateMD5;
        MD5_Init(&data->md5Ctx);
        data->printProgress = opts.printProgress;
        data->node = node;
        data->alphaNode = alphaNode;
        data->outFile = outFile;
        data->timecodesFile = timecodesFile;
        
        if (nodeType == mtVideo) {

            const VSVideoInfo *vi = vsapi->getVideoInfo(node);

            if (opts.mode == VSPipeMode::PrintInfo) {
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

                data->totalFrames = vi->numFrames;

                success = initializeVideoOutput(data.get());
                if (success) {
                    data->lastFPSReportTime = std::chrono::steady_clock::now();
                    success = !outputNode(opts, data.get(), vssapi->getCore(se));
                }
            }
        } else if (nodeType == mtAudio) {

            const VSAudioInfo *ai = vsapi->getAudioInfo(node);

            if (opts.mode == VSPipeMode::PrintInfo) {
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
                data->totalFrames = ai->numFrames;
                data->totalSamples = ai->numSamples;

                success = initializeAudioOutput(data.get());
                if (success) {
                    
                    success = !outputNode(opts, data.get(), vssapi->getCore(se));
                }
            }
        }

        unsigned char md5[16];
        MD5_Final(md5, &data->md5Ctx);

        if (outFile)
            fflush(outFile);
        if (timecodesFile)
            fflush(timecodesFile);

        std::chrono::duration<double> elapsedSeconds = std::chrono::steady_clock::now() - data->startTime;
        if (opts.mode == VSPipeMode::Output) {
            if (vsapi->getNodeType(node) == mtVideo)
                fprintf(stderr, "Output %d frames in %.2f seconds (%.2f fps)\n", data->totalFrames, elapsedSeconds.count(), data->totalFrames / elapsedSeconds.count());
            else
                fprintf(stderr, "Output %" PRId64 " samples in %.2f seconds (%.2f sps)\n", data->totalSamples, elapsedSeconds.count(), (data->totalFrames / elapsedSeconds.count()) * VS_AUDIO_FRAME_SAMPLES);
        }

        if (opts.calculateMD5 && outFile) {
            fprintf(stderr, "MD5: ");
            for (int i = 0; i < 16; i++)
                fprintf(stderr, "%02x", (int)md5[i]);
            fprintf(stderr, "\n");
        } else if (opts.calculateMD5) {
            fprintf(stderr, "MD5: OUTPUT REQUIRED");
        }

        if (opts.printFilterTime)
            fprintf(stderr, "%s", printNodeTimes(node, elapsedSeconds.count(), vsapi).c_str());
    }

    if (outFile && closeOutFile)
        fclose(outFile);
    if (timecodesFile)
        fclose(timecodesFile);


    vsapi->freeNode(node);
    vsapi->freeNode(alphaNode);
    vssapi->freeScript(se);

    return success ? 0 : 1;
}
