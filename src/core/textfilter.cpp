/*
* Copyright (c) 2013-2014 John Smith
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

#include <cstdint>
#include <cstdlib>

#include <string>
#include <vector>

#include <VapourSynth.h>
#include <VSHelper.h>

#include "filtershared.h"
#include "ter-116n.h"
#include "internalfilters.h"

const int margin_h = 16;
const int margin_v = 16;

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }

typedef std::vector<std::string> stringlist;
} // namespace

static void scrawl_character_int(unsigned char c, uint8_t *image, int stride, int dest_x, int dest_y, int bitsPerSample, int scale) {
    int black = 16 << (bitsPerSample - 8);
    int white = 235 << (bitsPerSample - 8);
    int x, y;
    if (bitsPerSample == 8) {
        for (y = 0; y < character_height * scale; y++) {
            for (x = 0; x < character_width * scale; x++) {
                if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                    image[dest_y*stride + dest_x + x] = white;
                } else {
                    image[dest_y*stride + dest_x + x] = black;
                }
            }

            dest_y++;
        }
    } else {
        for (y = 0; y < character_height * scale; y++) {
            for (x = 0; x < character_width * scale; x++) {
                if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                    reinterpret_cast<uint16_t *>(image)[dest_y*stride/2 + dest_x + x] = white;
                } else {
                    reinterpret_cast<uint16_t *>(image)[dest_y*stride/2 + dest_x + x] = black;
                }
            }

            dest_y++;
        }
    }
}


static void scrawl_character_float(unsigned char c, uint8_t *image, int stride, int dest_x, int dest_y, int scale) {
    float white = 1.0f;
    float black = 0.0f;
    int x, y;

    for (y = 0; y < character_height * scale; y++) {
        for (x = 0; x < character_width * scale; x++) {
            if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                reinterpret_cast<float *>(image)[dest_y*stride/4 + dest_x + x] = white;
            } else {
                reinterpret_cast<float *>(image)[dest_y*stride/4 + dest_x + x] = black;
            }
        }

        dest_y++;
    }
}

static void sanitise_text(std::string& txt) {
    for (size_t i = 0; i < txt.length(); i++) {
        if (txt[i] == '\r') {
            if (txt[i+1] == '\n') {
                txt.erase(i, 1);
            } else {
                txt[i] = '\n';
            }
            continue;
        } else if (txt[i] == '\n') {
            continue;
        }

        // Must adjust the character code because of the five characters
        // missing from the font.
        unsigned char current_char = static_cast<unsigned char>(txt[i]);
        if (current_char < 32 ||
            current_char == 129 ||
            current_char == 141 ||
            current_char == 143 ||
            current_char == 144 ||
            current_char == 157) {
                txt[i] = '_';
                continue;
        }

        if (current_char > 157) {
            txt[i] -= 5;
        } else if (current_char > 144) {
            txt[i] -= 4;
        } else if (current_char > 141) {
            txt[i] -= 2;
        } else if (current_char > 129) {
            txt[i] -= 1;
        }
    }
}


static stringlist split_text(const std::string& txt, int width, int height, int scale) {
    stringlist lines;

    // First split by \n
    size_t prev_pos = -1;
    for (size_t i = 0; i < txt.length(); i++) {
        if (txt[i] == '\n') {
            //if (i > 0 && i - prev_pos > 1) { // No empty lines allowed
            lines.push_back(txt.substr(prev_pos + 1, i - prev_pos - 1));
            //}
            prev_pos = i;
        }
    }
    lines.push_back(txt.substr(prev_pos + 1));

    // Then split any lines that don't fit
    size_t horizontal_capacity = width / character_width / scale;
    for (stringlist::iterator iter = lines.begin(); iter != lines.end(); iter++) {
        if (iter->size() > horizontal_capacity) {
            iter = std::prev(lines.insert(std::next(iter), iter->substr(horizontal_capacity)));
            iter->erase(horizontal_capacity);
        }
    }

    // Also drop lines that would go over the frame's bottom edge
    size_t vertical_capacity = height / character_height / scale;
    if (lines.size() > vertical_capacity) {
        lines.resize(vertical_capacity);
    }

    return lines;
}


static void scrawl_text(std::string txt, int alignment, int scale, VSFrameRef *frame, const VSAPI *vsapi) {
    const VSFormat *frame_format = vsapi->getFrameFormat(frame);
    int width = vsapi->getFrameWidth(frame, 0);
    int height = vsapi->getFrameHeight(frame, 0);

    sanitise_text(txt);

    stringlist lines = split_text(txt, width - margin_h*2, height - margin_v*2, scale);

    int start_x = 0;
    int start_y = 0;

    switch (alignment) {
    case 7:
    case 8:
    case 9:
        start_y = margin_v;
        break;
    case 4:
    case 5:
    case 6:
        start_y = (height - static_cast<int>(lines.size())*character_height*scale) / 2;
        break;
    case 1:
    case 2:
    case 3:
        start_y = height - static_cast<int>(lines.size())*character_height*scale - margin_v;
        break;
    }

    for (const auto &iter : lines) {
        switch (alignment) {
        case 1:
        case 4:
        case 7:
            start_x = margin_h;
            break;
        case 2:
        case 5:
        case 8:
            start_x = (width - static_cast<int>(iter.size())*character_width*scale) / 2;
            break;
        case 3:
        case 6:
        case 9:
            start_x = width - static_cast<int>(iter.size())*character_width*scale - margin_h;
            break;
        }

        for (size_t i = 0; i < iter.size(); i++) {
            int dest_x = start_x + static_cast<int>(i)*character_width*scale;
            int dest_y = start_y;

            if (frame_format->colorFamily == cmRGB) {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    int stride = vsapi->getStride(frame, plane);

                    if (frame_format->sampleType == stInteger) {
                        scrawl_character_int(iter[i], image, stride, dest_x, dest_y, frame_format->bitsPerSample, scale);
                    } else {
                        scrawl_character_float(iter[i], image, stride, dest_x, dest_y, scale);
                    }
                }
            } else {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    int stride = vsapi->getStride(frame, plane);

                    if (plane == 0) {
                        if (frame_format->sampleType == stInteger) {
                            scrawl_character_int(iter[i], image, stride, dest_x, dest_y, frame_format->bitsPerSample, scale);
                        } else {
                            scrawl_character_float(iter[i], image, stride, dest_x, dest_y, scale);
                        }
                    } else {
                        int sub_w = scale * character_width  >> frame_format->subSamplingW;
                        int sub_h = scale * character_height >> frame_format->subSamplingH;
                        int sub_dest_x = dest_x >> frame_format->subSamplingW;
                        int sub_dest_y = dest_y >> frame_format->subSamplingH;
                        int y;

                        if (frame_format->bitsPerSample == 8) {
                            for (y = 0; y < sub_h; y++) {
                                memset(image + (y+sub_dest_y)*stride + sub_dest_x, 128, sub_w);
                            }
                        } else if (frame_format->bitsPerSample <= 16) {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset16(reinterpret_cast<uint16_t *>(image) + (y+sub_dest_y)*stride/2 + sub_dest_x, 128 << (frame_format->bitsPerSample - 8), sub_w);
                            }
                        } else {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset_float(reinterpret_cast<float *>(image) + (y+sub_dest_y)*stride/4 + sub_dest_x, 0.0f, sub_w);
                            }
                        }
                    } // if plane
                } // for plane in planes
            } // if colorFamily
        } // for i in line
        start_y += character_height * scale;
    } // for iter in lines
}


enum Filters {
    FILTER_TEXT,
    FILTER_CLIPINFO,
    FILTER_COREINFO,
    FILTER_FRAMENUM,
    FILTER_FRAMEPROPS
};


namespace {

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;

    std::string text;
    int alignment;
    int scale;
    intptr_t filter;
    stringlist props;
    std::string instanceName;
} TextData;

} // namespace


static void VS_CC textInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(*instanceData);
    vsapi->setVideoInfo(d->vi, 1, node);
}


static void append_prop(std::string &text, const std::string &key, const VSMap *map, const VSAPI *vsapi) {
    char type = vsapi->propGetType(map, key.c_str());
    int numElements = vsapi->propNumElements(map, key.c_str());
    int idx;
    // "<key>: <val0> <val1> <val2> ... <valn-1>"
    text += key + ":";
    if (type == ptInt) {
        const int64_t *intArr = vsapi->propGetIntArray(map, key.c_str(), nullptr);
        for (idx = 0; idx < numElements; idx++)
            text += " " + std::to_string(intArr[idx]);
    } else if (type == ptFloat) {
        const double *floatArr = vsapi->propGetFloatArray(map, key.c_str(), nullptr);
        for (idx = 0; idx < numElements; idx++)
            text += " " + std::to_string(floatArr[idx]);
    } else if (type == ptData) {
        for (idx = 0; idx < numElements; idx++) {
            const char *value = vsapi->propGetData(map, key.c_str(), idx, nullptr);
            int size = vsapi->propGetDataSize(map, key.c_str(), idx, nullptr);
            text += " ";
            if (size > 100) {
                text += "<property too long>";
            } else {
                text += value;
            }
        }
    } else if (type == ptFrame) {
        text += std::to_string(numElements) + " frame";
        if (numElements != 1)
            text += 's';
    } else if (type == ptNode) {
        text += std::to_string(numElements) + " node";
        if (numElements != 1)
            text += 's';
    } else if (type == ptFunction) {
        text += std::to_string(numElements) + " function";
        if (numElements != 1)
            text += 's';
    } else if (type == ptUnset) {
        text += " <no such property>";
    }

    text += "\n";
}

static std::string fieldBasedToString(int field) {
    std::string s = "Unknown";
    if (field == 0)
        s = "Frame based";
    else if (field == 1)
        s = "Bottom field first";
    else if (field == 2)
        s = "Top field first";
    return s;
}

static std::string colorFamilyToString(int cf) {
    std::string family = "Unknown";
    if (cf == cmGray)
        family = "Gray";
    else if (cf == cmRGB)
        family = "RGB";
    else if (cf == cmYUV)
        family = "YUV";
    else if (cf == cmYCoCg)
        family = "YCoCg";
    else if (cf == cmCompat)
        family = "Compat";
    return family;
}

static std::string chromaLocationToString(int location) {
    std::string s = "Unknown";
    if (location == 0)
        s = "Left";
    else if (location == 1)
        s = "Center";
    else if (location == 2)
        s = "Top left";
    else if (location == 3)
        s = "Top";
    else if (location == 4)
        s = "Bottom left";
    else if (location == 5)
        s = "Bottom";
    return s;
}

static std::string rangeToString(int range) {
    std::string s = "Unknown";
    if (range == 0)
        s = "Full range";
    else if (range == 1)
        s = "Limited range";
    return s;
}

static std::string matrixToString(int matrix) {
    std::string s = "Unknown";
    if (matrix == 0)
        s = "sRGB";
    else if (matrix == 1)
        s = "BT.709";
    else if (matrix == 4)
        s = "FCC";
    else if (matrix == 5 || matrix  == 6)
        s = "BT.601";
    else if (matrix == 7)
        s = "SMPTE 240M";
    else if (matrix == 8)
        s = "YCoCg";
    else if (matrix == 9)
        s = "BT.2020 NCL";
    else if (matrix == 10)
        s = "BT.2020 CL";
    else if (matrix == 11)
        s = "SMPTE 2085";
    else if (matrix == 12)
        s = "Cromaticity dervived cl";
    else if (matrix == 13)
        s = "Cromaticity dervived ncl";
    else if (matrix == 14)
        s = "ICtCp";
    return s;
}

static std::string primariesToString(int primaries) {
    std::string s = "Unknown";
    if (primaries == 1)
        s = "BT.709";
    else if (primaries == 4)
        s = "BT.470M";
    else if (primaries == 5)
        s = "BT.470BG";
    else if (primaries == 6)
        s = "SMPTE 170M";
    else if (primaries == 7)
        s = "SMPTE 240M";
    else if (primaries == 8)
        s = "FILM";
    else if (primaries == 9)
        s = "BT.2020";
    else if (primaries == 10)
        s = "SMPTE 428";
    else if (primaries == 11)
        s = "SMPTE 431";
    else if (primaries == 12)
        s = "SMPTE 432";
    else if (primaries == 22)
        s = "JEDEC P22";
    return s;
}

static std::string transferToString(int transfer) {
        std::string s = "Unknown";
        if (transfer == 1)
            s = "BT.709";
        else if (transfer == 4)
            s = "Gamma 2.2";
        else if (transfer == 5)
            s = "Gamma 2.8";
        else if (transfer == 6)
            s = "SMPTE 170M";
        else if (transfer == 7)
            s = "SMPTE 240M";
        else if (transfer == 8)
            s = "Linear";
        else if (transfer == 9)
            s = "Logaritmic (100:1 range)";
        else if (transfer == 10)
            s = "Logaritmic (100 * Sqrt(10) : 1 range)";
        else if (transfer == 11)
            s = "IEC 61966-2-4";
        else if (transfer == 12)
            s = "BT.1361 Extended Colour Gamut";
        else if (transfer == 13)
            s = "IEC 61966-2-1";
        else if (transfer == 14)
            s = "BT.2020 for 10 bit system";
        else if (transfer == 15)
            s = "BT.2020 for 12 bit system";
        else if (transfer == 16)
            s = "SMPTE 2084";
        else if (transfer == 17)
            s = "SMPTE 428";
        else if (transfer == 18)
            s = "ARIB STD-B67";
        return s;
}

static const VSFrameRef *VS_CC textGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSFormat *frame_format = vsapi->getFrameFormat(src);
        if ((frame_format->sampleType == stInteger && frame_format->bitsPerSample > 16) ||
            (frame_format->sampleType == stFloat && frame_format->bitsPerSample != 32)) {
                vsapi->freeFrame(src);
                vsapi->setFilterError((d->instanceName + ": Only 8..16 bit integer and 32 bit float formats supported").c_str(), frameCtx);
                return nullptr;
        }

        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);

        int minimum_width = 2 * margin_h + character_width * d->scale;
        int minimum_height = 2 * margin_v + character_height * d->scale;

        if (width < minimum_width || height < minimum_height) {
            vsapi->freeFrame(src);
            vsapi->setFilterError((d->instanceName + ": frame size must be at least " + std::to_string(minimum_width) + "x" + std::to_string(minimum_height) + " pixels.").c_str(), frameCtx);
            return nullptr;
        }

        VSFrameRef *dst = vsapi->copyFrame(src, core);

        if (d->filter == FILTER_FRAMENUM) {
            scrawl_text(std::to_string(n), d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_FRAMEPROPS) {
            const VSMap *props = vsapi->getFramePropsRO(dst);
            int numKeys = vsapi->propNumKeys(props);
            int i;
            std::string text = "Frame properties:\n";

            if (!d->props.empty()) {
                for (const auto &iter : d->props) {
                    append_prop(text, iter, props, vsapi);
                }
            } else {
                for (i = 0; i < numKeys; i++) {
                    const char *key = vsapi->propGetKey(props, i);
                    append_prop(text, key, props, vsapi);
                }
            }

            scrawl_text(text, d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_COREINFO) {
            VSCoreInfo ci;
            vsapi->getCoreInfo2(core, &ci);

            std::string text;
            text.append(ci.versionString).append("\n");
            text.append("Threads: ").append(std::to_string(ci.numThreads)).append("\n");
            text.append("Maximum framebuffer cache size: ").append(std::to_string(ci.maxFramebufferSize)).append(" bytes\n");
            text.append("Used framebuffer cache size: ").append(std::to_string(ci.usedFramebufferSize)).append(" bytes");

            scrawl_text(text, d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_CLIPINFO) {
            const VSMap *props = vsapi->getFramePropsRO(src);
            std::string text = "Clip info:\n";

            if (d->vi->width) {
                text += "Width: " + std::to_string(vsapi->getFrameWidth(dst, 0)) + " px\n";
                text += "Height: " + std::to_string(vsapi->getFrameHeight(dst, 0)) + " px\n";
            } else {
                text += "Width: " + std::to_string(vsapi->getFrameWidth(dst, 0)) + " px (may vary)\n";
                text += "Height: " + std::to_string(vsapi->getFrameHeight(dst, 0)) + " px (may vary)\n";
            }

            int snerr, sderr;
            int sn = int64ToIntS(vsapi->propGetInt(props, "_SARNum", 0, &snerr));
            int sd = int64ToIntS(vsapi->propGetInt(props, "_SARDen", 0, &sderr));
            if (snerr || sderr)
                text += "Aspect ratio: Unknown\n";
            else
                text += "Sample aspect ratio: " + std::to_string(sn) + ":" + std::to_string(sd) + "\n";

            text += "Length: " + std::to_string(d->vi->numFrames) + " frames\n";

            text += "Format name: "_s + frame_format->name + (d->vi->format ? "\n" : " (may vary)\n");

            text += "Color family: " + colorFamilyToString(frame_format->colorFamily) + "\n";
            text += "Sample type: "_s + (frame_format->sampleType == stInteger ? "Integer" : "Float") + "\n";
            text += "Bits per sample: " + std::to_string(frame_format->bitsPerSample) + "\n";
            text += "Subsampling Height/Width: " + std::to_string(1 << frame_format->subSamplingH) + "x/" + std::to_string(1 << frame_format->subSamplingW) + "x\n";

            int err;
            int matrix = int64ToIntS(vsapi->propGetInt(props, "_Matrix", 0, &err));
            if (err)
                matrix = -1;
            int primaries = int64ToIntS(vsapi->propGetInt(props, "_Primaries", 0, &err));
            if (err)
                primaries = -1;
            int transfer = int64ToIntS(vsapi->propGetInt(props, "_Transfer", 0, &err));
            if (err)
                transfer = -1;
            int range = int64ToIntS(vsapi->propGetInt(props, "_ColorRange", 0, &err));
            if (err)
                range = -1;
            int location = int64ToIntS(vsapi->propGetInt(props, "_ChromaLocation", 0, &err));
            if (err)
                location = -1;
            int field = int64ToIntS(vsapi->propGetInt(props, "_FieldBased", 0, &err));
            if (err)
                field = -1;

            const char *picttype = vsapi->propGetData(props, "_PictType", 0, &err);

            text += "Matrix: " + matrixToString(matrix) + "\n";
            text += "Primaries: " + primariesToString(primaries) + "\n";
            text += "Transfer: " + transferToString(transfer) + "\n";
            text += "Range: " + rangeToString(range) + "\n";
            text += "Chroma Location: " + chromaLocationToString(location) + "\n";
            text += "Field handling: " + fieldBasedToString(field) + "\n";
            text += "Picture type: "_s + (picttype ? picttype : "Unknown") + "\n";

            if (d->vi->fpsNum && d->vi->fpsDen) {
                text += "Fps: " + std::to_string(d->vi->fpsNum) + "/" + std::to_string(d->vi->fpsDen) + " (" + std::to_string(static_cast<double>(d->vi->fpsNum) / d->vi->fpsDen) + ")\n";
            } else {
                text += "Fps: Unknown\n";
            }

            int fnerr, fderr;
            int fn = int64ToIntS(vsapi->propGetInt(props, "_DurationNum", 0, &fnerr));
            int fd = int64ToIntS(vsapi->propGetInt(props, "_DurationDen", 0, &fderr));
            if (fnerr || fderr) {
                text += "Frame duration: Unknown\n";
            } else {
                text += "Frame duration: " + std::to_string(fn) + "/" + std::to_string(fd) + " (" + std::to_string(static_cast<double>(fn) / fd) + ")\n";
            }

            scrawl_text(text, d->alignment, d->scale, dst, vsapi);
        } else {
            scrawl_text(d->text, d->alignment, d->scale, dst, vsapi);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}


static void VS_CC textFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}


static void VS_CC textCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    TextData d;
    TextData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, &err);
    if (err) {
        // Can only happen for CoreInfo.
        VSMap *args = vsapi->createMap();
        VSPlugin *stdPlugin = vsapi->getPluginById("com.vapoursynth.std", core);
        VSMap *ret = vsapi->invoke(stdPlugin, "BlankClip", args);
        vsapi->freeMap(args);
        const char *error = vsapi->getError(ret);
        if (error) {
            std::string msg = "CoreInfo: No input clip was given and invoking BlankClip failed. The error message from BlankClip is:\n";
            msg += error;
            vsapi->setError(out, msg.c_str());
            vsapi->freeMap(ret);
            return;
        }
        d.node = vsapi->propGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(ret);
    }
    d.vi = vsapi->getVideoInfo(d.node);

    if (isCompatFormat(d.vi)) {
        vsapi->setError(out, "Text: Compat formats not supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi->format && ((d.vi->format->sampleType == stInteger && d.vi->format->bitsPerSample > 16) ||
        (d.vi->format->sampleType == stFloat && d.vi->format->bitsPerSample != 32))) {
            vsapi->setError(out, "Text: Only 8-16 bit integer and 32 bit float formats supported");
            vsapi->freeNode(d.node);
            return;
    }

    d.alignment = int64ToIntS(vsapi->propGetInt(in, "alignment", 0, &err));
    if (err) {
        d.alignment = 7; // top left
    }

    if (d.alignment < 1 || d.alignment > 9) {
        vsapi->setError(out, "Text: alignment must be between 1 and 9 (think numpad)");
        vsapi->freeNode(d.node);
        return;
    }

    d.scale = int64ToIntS(vsapi->propGetInt(in, "scale", 0, &err));
    if (err) {
        d.scale = 1;
    }

    d.filter = reinterpret_cast<intptr_t>(userData);

    switch (d.filter) {
    case FILTER_TEXT:
        d.text = vsapi->propGetData(in, "text", 0, nullptr);
        d.instanceName = "Text";
        break;
    case FILTER_CLIPINFO:
        d.instanceName = "ClipInfo";
        break;
    case FILTER_COREINFO:
        {
            d.instanceName = "CoreInfo";
            break;
        }
    case FILTER_FRAMENUM:
        d.instanceName = "FrameNum";
        break;
    case FILTER_FRAMEPROPS:
        int numProps = vsapi->propNumElements(in, "props");

        for (int i = 0; i < numProps; i++) {
            d.props.push_back(vsapi->propGetData(in, "props", i, nullptr));
        }

        d.instanceName = "FrameProps";
        break;
    }

    data = new TextData(d);

    vsapi->createFilter(in, out, d.instanceName.c_str(), textInit, textGetFrame, textFree, fmParallel, 0, data, core);
}


void VS_CC textInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.text", "text", "VapourSynth Text", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Text",
        "clip:clip;"
        "text:data;"
        "alignment:int:opt;"
        "scale:int:opt;",
        textCreate, reinterpret_cast<void *>(FILTER_TEXT), plugin);
    registerFunc("ClipInfo",
        "clip:clip;"
        "alignment:int:opt;"
        "scale:int:opt;",
        textCreate, reinterpret_cast<void *>(FILTER_CLIPINFO), plugin);
    registerFunc("CoreInfo",
        "clip:clip:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        textCreate, reinterpret_cast<void *>(FILTER_COREINFO), plugin);
    registerFunc("FrameNum",
        "clip:clip;"
        "alignment:int:opt;"
        "scale:int:opt;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMENUM), plugin);
    registerFunc("FrameProps",
        "clip:clip;"
        "props:data[]:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMEPROPS), plugin);
}
