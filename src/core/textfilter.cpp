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
#include <memory>
#include "VapourSynth4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"
#include "version.h"
#include "filtershared.h"
#include "ter-116n.h"
#include "internalfilters.h"

const int margin_h = 16;
const int margin_v = 16;

namespace {
std::string operator""_s(const char *str, size_t len) { return{ str, len }; }

typedef std::vector<std::string> stringlist;
} // namespace

static void scrawl_character_int(unsigned char c, uint8_t *image, ptrdiff_t stride, int dest_x, int dest_y, int bitsPerSample, int scale) {
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


static void scrawl_character_float(unsigned char c, uint8_t *image, ptrdiff_t stride, int dest_x, int dest_y, int scale) {
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


static void scrawl_text(std::string txt, int alignment, int scale, VSFrame *frame, const VSAPI *vsapi) {
    const VSVideoFormat *frame_format = vsapi->getVideoFrameFormat(frame);
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

            if (frame_format->colorFamily == cfRGB) {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    ptrdiff_t stride = vsapi->getStride(frame, plane);

                    if (frame_format->sampleType == stInteger) {
                        scrawl_character_int(iter[i], image, stride, dest_x, dest_y, frame_format->bitsPerSample, scale);
                    } else {
                        scrawl_character_float(iter[i], image, stride, dest_x, dest_y, scale);
                    }
                }
            } else {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    ptrdiff_t stride = vsapi->getStride(frame, plane);

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
                                vs_memset<uint8_t>(image + (y+sub_dest_y)*stride + sub_dest_x, 128, sub_w);
                            }
                        } else if (frame_format->bitsPerSample <= 16) {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset<uint16_t>(reinterpret_cast<uint16_t *>(image) + (y+sub_dest_y)*stride/2 + sub_dest_x, 128 << (frame_format->bitsPerSample - 8), sub_w);
                            }
                        } else {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset<float>(reinterpret_cast<float *>(image) + (y+sub_dest_y)*stride/4 + sub_dest_x, 0.0f, sub_w);
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
    VSNode *node;
    const VSVideoInfo *vi;

    std::string text;
    int alignment;
    int scale;
    intptr_t filter;
    stringlist props;
    std::string instanceName;
} TextData;

} // namespace

static void append_prop(std::string &text, const std::string &key, const VSMap *map, const VSAPI *vsapi) {
    char type = vsapi->mapGetType(map, key.c_str());
    int numElements = vsapi->mapNumElements(map, key.c_str());
    int idx;
    // "<key>: <val0> <val1> <val2> ... <valn-1>"
    text += key + ":";
    if (type == ptInt) {
        const int64_t *intArr = vsapi->mapGetIntArray(map, key.c_str(), nullptr);
        for (idx = 0; idx < numElements; idx++)
            text += " " + std::to_string(intArr[idx]);
    } else if (type == ptFloat) {
        const double *floatArr = vsapi->mapGetFloatArray(map, key.c_str(), nullptr);
        for (idx = 0; idx < numElements; idx++)
            text += " " + std::to_string(floatArr[idx]);
    } else if (type == ptData) {
        for (idx = 0; idx < numElements; idx++) {
            const char *value = vsapi->mapGetData(map, key.c_str(), idx, nullptr);
            int size = vsapi->mapGetDataSize(map, key.c_str(), idx, nullptr);
            int type = vsapi->mapGetDataTypeHint(map, key.c_str(), idx, nullptr);
            text += " ";
            if (type == dtBinary) {
                text += "<binary data (" + std::to_string(size) + " bytes)>";
            } else if (size > 100) {
                text += "<property too long (" + std::to_string(size) + " bytes)>";
            } else {
                text += value;
            }
        }
    } else if (type == ptVideoFrame) {
        text += std::to_string(numElements) + " video frame";
        if (numElements != 1)
            text += 's';
    } else if (type == ptAudioFrame) {
        text += std::to_string(numElements) + " audio frame";
        if (numElements != 1)
            text += 's';
    } else if (type == ptVideoNode) {
        text += std::to_string(numElements) + " video node";
        if (numElements != 1)
            text += 's';
    } else if (type == ptAudioNode) {
        text += std::to_string(numElements) + " audio node";
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
    if (field == VSC_FIELD_PROGRESSIVE)
        s = "Frame based";
    else if (field == VSC_FIELD_BOTTOM)
        s = "Bottom field first";
    else if (field == VSC_FIELD_TOP)
        s = "Top field first";
    return s;
}

static std::string colorFamilyToString(int cf) {
    std::string family = "Unknown";
    if (cf == cfGray)
        family = "Gray";
    else if (cf == cfRGB)
        family = "RGB";
    else if (cf == cfYUV)
        family = "YUV";
    return family;
}

static std::string chromaLocationToString(int location) {
    std::string s = "Unknown";
    if (location == VSC_CHROMA_LEFT)
        s = "Left";
    else if (location == VSC_CHROMA_CENTER)
        s = "Center";
    else if (location == VSC_CHROMA_TOP_LEFT)
        s = "Top left";
    else if (location == VSC_CHROMA_TOP)
        s = "Top";
    else if (location == VSC_CHROMA_BOTTOM_LEFT)
        s = "Bottom left";
    else if (location == VSC_CHROMA_BOTTOM)
        s = "Bottom";
    return s;
}

static std::string rangeToString(int range) {
    std::string s = "Unknown";
    if (range == VSC_RANGE_FULL)
        s = "Full range";
    else if (range == VSC_RANGE_LIMITED)
        s = "Limited range";
    return s;
}

static std::string matrixToString(int matrix) {
    std::string s = "Unknown";
    if (matrix == VSC_MATRIX_RGB)
        s = "sRGB";
    else if (matrix == VSC_MATRIX_BT709)
        s = "BT.709";
    else if (matrix == VSC_MATRIX_FCC)
        s = "FCC";
    else if (matrix == VSC_MATRIX_BT470_BG || matrix  == VSC_MATRIX_ST170_M)
        s = "BT.601";
    else if (matrix == VSC_MATRIX_ST240_M)
        s = "SMPTE 240M";
    else if (matrix == VSC_MATRIX_YCGCO)
        s = "YCoCg";
    else if (matrix == VSC_MATRIX_BT2020_NCL)
        s = "BT.2020 NCL";
    else if (matrix == VSC_MATRIX_BT2020_CL)
        s = "BT.2020 CL";
    else if (matrix == 11)
        s = "SMPTE 2085";
    else if (matrix == VSC_MATRIX_CHROMATICITY_DERIVED_NCL)
        s = "Cromaticity derived cl";
    else if (matrix == VSC_MATRIX_CHROMATICITY_DERIVED_CL)
        s = "Cromaticity derived ncl";
    else if (matrix == VSC_MATRIX_ICTCP)
        s = "ICtCp";
    return s;
}

static std::string primariesToString(int primaries) {
    std::string s = "Unknown";
    if (primaries == VSC_PRIMARIES_BT709)
        s = "BT.709";
    else if (primaries == VSC_PRIMARIES_BT470_M)
        s = "BT.470M";
    else if (primaries == VSC_PRIMARIES_BT470_BG)
        s = "BT.470BG";
    else if (primaries == VSC_PRIMARIES_ST170_M)
        s = "SMPTE 170M";
    else if (primaries == VSC_PRIMARIES_ST240_M)
        s = "SMPTE 240M";
    else if (primaries == VSC_PRIMARIES_FILM)
        s = "FILM";
    else if (primaries == VSC_PRIMARIES_BT2020)
        s = "BT.2020";
    else if (primaries == VSC_PRIMARIES_ST428)
        s = "SMPTE 428";
    else if (primaries == VSC_PRIMARIES_ST431_2)
        s = "SMPTE 431";
    else if (primaries == VSC_PRIMARIES_ST432_1)
        s = "SMPTE 432";
    else if (primaries == VSC_PRIMARIES_EBU3213_E)
        s = "JEDEC P22";
    return s;
}

static std::string transferToString(int transfer) {
        std::string s = "Unknown";
        if (transfer == VSC_TRANSFER_BT709)
            s = "BT.709";
        else if (transfer == VSC_TRANSFER_BT470_M)
            s = "Gamma 2.2";
        else if (transfer == VSC_TRANSFER_BT470_BG)
            s = "Gamma 2.8";
        else if (transfer == VSC_TRANSFER_BT601)
            s = "SMPTE 170M";
        else if (transfer == VSC_TRANSFER_ST240_M)
            s = "SMPTE 240M";
        else if (transfer == VSC_TRANSFER_LINEAR)
            s = "Linear";
        else if (transfer == VSC_TRANSFER_LOG_100)
            s = "Logaritmic (100:1 range)";
        else if (transfer == VSC_TRANSFER_LOG_316)
            s = "Logaritmic (100 * Sqrt(10) : 1 range)";
        else if (transfer == VSC_TRANSFER_IEC_61966_2_4)
            s = "IEC 61966-2-4";
        else if (transfer == 12)
            s = "BT.1361 Extended Colour Gamut";
        else if (transfer == VSC_TRANSFER_IEC_61966_2_1)
            s = "IEC 61966-2-1";
        else if (transfer == VSC_TRANSFER_BT2020_10)
            s = "BT.2020 for 10 bit system";
        else if (transfer == VSC_TRANSFER_BT2020_12)
            s = "BT.2020 for 12 bit system";
        else if (transfer == VSC_TRANSFER_ST2084)
            s = "SMPTE 2084";
        else if (transfer == 17)
            s = "SMPTE 428";
        else if (transfer == VSC_TRANSFER_ARIB_B67)
            s = "ARIB STD-B67";
        return s;
}

static const VSFrame *VS_CC textGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSVideoFormat *frame_format = vsapi->getVideoFrameFormat(src);
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

        VSFrame *dst = vsapi->copyFrame(src, core);

        if (d->filter == FILTER_FRAMENUM) {
            scrawl_text(std::to_string(n), d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_FRAMEPROPS) {
            const VSMap *props = vsapi->getFramePropertiesRO(dst);
            int numKeys = vsapi->mapNumKeys(props);
            int i;
            std::string text = "Frame properties:\n";

            if (!d->props.empty()) {
                for (const auto &iter : d->props) {
                    append_prop(text, iter, props, vsapi);
                }
            } else {
                for (i = 0; i < numKeys; i++) {
                    const char *key = vsapi->mapGetKey(props, i);
                    append_prop(text, key, props, vsapi);
                }
            }

            scrawl_text(text, d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_COREINFO) {
            VSCoreInfo ci;
            vsapi->getCoreInfo(core, &ci);

            std::string text;
            text.append(ci.versionString).append("\n");
            text.append("Threads: ").append(std::to_string(ci.numThreads)).append("\n");
            text.append("Maximum framebuffer cache size: ").append(std::to_string(ci.maxFramebufferSize)).append(" bytes\n");
            text.append("Used framebuffer cache size: ").append(std::to_string(ci.usedFramebufferSize)).append(" bytes");

            scrawl_text(text, d->alignment, d->scale, dst, vsapi);
        } else if (d->filter == FILTER_CLIPINFO) {
            const VSMap *props = vsapi->getFramePropertiesRO(src);
            std::string text = "Clip info:\n";

            if (d->vi->width) {
                text += "Width: " + std::to_string(vsapi->getFrameWidth(dst, 0)) + " px\n";
                text += "Height: " + std::to_string(vsapi->getFrameHeight(dst, 0)) + " px\n";
            } else {
                text += "Width: " + std::to_string(vsapi->getFrameWidth(dst, 0)) + " px (may vary)\n";
                text += "Height: " + std::to_string(vsapi->getFrameHeight(dst, 0)) + " px (may vary)\n";
            }

            int snerr, sderr;
            int64_t sn = vsapi->mapGetInt(props, "_SARNum", 0, &snerr);
            int64_t sd = vsapi->mapGetInt(props, "_SARDen", 0, &sderr);
            if (snerr || sderr)
                text += "Aspect ratio: Unknown\n";
            else
                text += "Sample aspect ratio: " + std::to_string(sn) + ":" + std::to_string(sd) + "\n";

            text += "Length: " + std::to_string(d->vi->numFrames) + " frames\n";

            char nameBuffer[32];
            vsapi->getVideoFormatName(&d->vi->format, nameBuffer);

            text += "Format name: "_s + std::string(nameBuffer) + (d->vi->format.colorFamily == cfUndefined ? "\n" : " (may vary)\n");

            text += "Color family: " + colorFamilyToString(frame_format->colorFamily) + "\n";
            text += "Sample type: "_s + (frame_format->sampleType == stInteger ? "Integer" : "Float") + "\n";
            text += "Bits per sample: " + std::to_string(frame_format->bitsPerSample) + "\n";
            text += "Subsampling Height/Width: " + std::to_string(1 << frame_format->subSamplingH) + "x/" + std::to_string(1 << frame_format->subSamplingW) + "x\n";

            int err;
            int matrix = vsapi->mapGetIntSaturated(props, "_Matrix", 0, &err);
            if (err)
                matrix = -1;
            int primaries = vsapi->mapGetIntSaturated(props, "_Primaries", 0, &err);
            if (err)
                primaries = -1;
            int transfer = vsapi->mapGetIntSaturated(props, "_Transfer", 0, &err);
            if (err)
                transfer = -1;
            int range = vsapi->mapGetIntSaturated(props, "_ColorRange", 0, &err);
            if (err)
                range = -1;
            int location = vsapi->mapGetIntSaturated(props, "_ChromaLocation", 0, &err);
            if (err)
                location = -1;
            int field = vsapi->mapGetIntSaturated(props, "_FieldBased", 0, &err);
            if (err)
                field = -1;

            const char *picttype = vsapi->mapGetData(props, "_PictType", 0, &err);

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
            int64_t fn = vsapi->mapGetInt(props, "_DurationNum", 0, &fnerr);
            int64_t fd = vsapi->mapGetInt(props, "_DurationDen", 0, &fderr);
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
    std::unique_ptr<TextData> d(new TextData);
    int err;

    d->node = vsapi->mapGetNode(in, "clip", 0, &err);
    if (err) {
        // Can only happen for CoreInfo.
        VSMap *args = vsapi->createMap();
        VSPlugin *stdPlugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);
        VSMap *ret = vsapi->invoke(stdPlugin, "BlankClip", args);
        vsapi->freeMap(args);
        const char *error = vsapi->mapGetError(ret);
        if (error) {
            std::string msg = "CoreInfo: No input clip was given and invoking BlankClip failed. The error message from BlankClip is:\n";
            msg += error;
            vsapi->mapSetError(out, msg.c_str());
            vsapi->freeMap(ret);
            return;
        }
        d->node = vsapi->mapGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(ret);
    }
    d->vi = vsapi->getVideoInfo(d->node);

    if (d->vi->format.colorFamily != cfUndefined && ((d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample > 16) ||
        (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 32))) {
            vsapi->mapSetError(out, "Text: Only 8-16 bit integer and 32 bit float formats supported");
            vsapi->freeNode(d->node);
            return;
    }

    d->alignment = vsapi->mapGetIntSaturated(in, "alignment", 0, &err);
    if (err) {
        d->alignment = 7; // top left
    }

    if (d->alignment < 1 || d->alignment > 9) {
        vsapi->mapSetError(out, "Text: alignment must be between 1 and 9 (think numpad)");
        vsapi->freeNode(d->node);
        return;
    }

    d->scale = vsh::int64ToIntS(vsapi->mapGetInt(in, "scale", 0, &err));
    if (err) {
        d->scale = 1;
    }

    d->filter = reinterpret_cast<intptr_t>(userData);

    switch (d->filter) {
    case FILTER_TEXT:
        d->text = vsapi->mapGetData(in, "text", 0, nullptr);
        d->instanceName = "Text";
        break;
    case FILTER_CLIPINFO:
        d->instanceName = "ClipInfo";
        break;
    case FILTER_COREINFO:
        {
            d->instanceName = "CoreInfo";
            break;
        }
    case FILTER_FRAMENUM:
        d->instanceName = "FrameNum";
        break;
    case FILTER_FRAMEPROPS:
        int numProps = vsapi->mapNumElements(in, "props");

        for (int i = 0; i < numProps; i++) {
            d->props.push_back(vsapi->mapGetData(in, "props", i, nullptr));
        }

        d->instanceName = "FrameProps";
        break;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, d->instanceName.c_str(), d->vi, textGetFrame, textFree, fmParallel, deps, 1, d.get(), core);
    d.release();
}


void textInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(VSH_TEXT_PLUGIN_ID, "text", "VapourSynth Text", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Text",
        "clip:vnode;"
        "text:data;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode;",
        textCreate, reinterpret_cast<void *>(FILTER_TEXT), plugin);
    vspapi->registerFunction("ClipInfo",
        "clip:vnode;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode;",
        textCreate, reinterpret_cast<void *>(FILTER_CLIPINFO), plugin);
    vspapi->registerFunction("CoreInfo",
        "clip:vnode:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode;",
        textCreate, reinterpret_cast<void *>(FILTER_COREINFO), plugin);
    vspapi->registerFunction("FrameNum",
        "clip:vnode;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMENUM), plugin);
    vspapi->registerFunction("FrameProps",
        "clip:vnode;"
        "props:data[]:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMEPROPS), plugin);
}
