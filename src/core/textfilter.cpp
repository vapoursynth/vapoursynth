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
#include "VSVulkan4.h"
#include "gpufilter.h"

const int margin_h = 16;
const int margin_v = 16;

namespace {
using namespace std::string_literals;

typedef std::vector<std::string> stringlist;

/* GPU path, declared through the gpufilter.h driver: a copy pass lays the source plane
   down and one glyph pass overdraws every character of the frame in a single dispatch.
   The per frame glyph list -- the text varies per frame for FrameNum and friends --
   travels in the driver's frame data buffer as two words per glyph: character and luma x
   packed in the first, luma y in the second. The kernel shifts the luma coordinates by
   the plane's subsampling, which is exact because cell strides (character_width and
   character_height times scale) divide by every subsampling factor -- the same per glyph
   shift the CPU path applies. The glyph count rides in frameParams so the pass's
   reshape, which runs after prepareFrame fills them, sizes the grid to the text being
   drawn rather than to a worst case. */
struct TextGlyphPush {
    uint32_t dstStride, cellW, cellH, count;
    uint32_t scaleX, scaleY, subW, subH;
    uint32_t width, height; /* of the plane being written; bounds the store */
    float fg, bg;
};

struct TextCopyPush {
    uint32_t width, height, srcStride, dstStride;
};

} // namespace

static void scrawl_character_int(unsigned char c, uint8_t *image, ptrdiff_t stride, int dest_x, int dest_y, int bitsPerSample, int scale) {
    int black = 16 << (bitsPerSample - 8);
    int white = 235 << (bitsPerSample - 8);
    if (bitsPerSample == 8) {
        for (int y = 0; y < character_height * scale; y++) {
            for (int x = 0; x < character_width * scale; x++) {
                if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                    image[dest_y*stride + dest_x + x] = white;
                } else {
                    image[dest_y*stride + dest_x + x] = black;
                }
            }

            dest_y++;
        }
    } else {
        for (int y = 0; y < character_height * scale; y++) {
            for (int x = 0; x < character_width * scale; x++) {
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

    for (int y = 0; y < character_height * scale; y++) {
        for (int x = 0; x < character_width * scale; x++) {
            if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                reinterpret_cast<float *>(image)[dest_y*stride/4 + dest_x + x] = white;
            } else {
                reinterpret_cast<float *>(image)[dest_y*stride/4 + dest_x + x] = black;
            }
        }

        dest_y++;
    }
}

static void scrawl_character_half(unsigned char c, uint8_t *image, ptrdiff_t stride, int dest_x, int dest_y, int scale) {
    const uint16_t white = 0x3C00; // 1.0
    const uint16_t black = 0x0000; // 0.0

    for (int y = 0; y < character_height * scale; y++) {
        for (int x = 0; x < character_width * scale; x++) {
            if (__font_bitmap__[c * character_height + y/scale] & (1 << (7 - x/scale))) {
                reinterpret_cast<uint16_t *>(image)[dest_y*stride/2 + dest_x + x] = white;
            } else {
                reinterpret_cast<uint16_t *>(image)[dest_y*stride/2 + dest_x + x] = black;
            }
        }

        dest_y++;
    }
}

static std::string textSamplePreamble(const VSVideoFormat &fmt) {
    std::string s = "#version 460\n" + vsgpu::glslTypePreamble(vsgpu::glslUsesFloat16(fmt));
    s += std::string("#define SAMPLE_T ") + vsgpu::glslElementType(fmt) + "\n";
    return s;
}

/* The font is a static 4 KB table, so it goes into the source as literals: no buffer, no
   upload, and the core caches shaders by source text, so every Text instance in a script
   shares the one compile. Chroma passes scaleX of zero, which is the flag for "flat
   rectangle" -- the character cell is still drawn, just filled with the neutral value the
   luma glyph background uses. */
static std::string textKernelSource(const VSVideoFormat &fmt) {
    std::string s = textSamplePreamble(fmt);
    s += "\nlayout(local_size_x = 8, local_size_y = 8) in;\n"
         "layout(std430, set = 0, binding = 0) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
         "layout(std430, set = 0, binding = 1) readonly buffer Glyphs { uint fd[]; };\n"
         "layout(push_constant) uniform PC {\n"
         "    uint dstStride, cellW, cellH, count;\n"
         "    uint scaleX, scaleY, subW, subH;\n"
         "    uint width, height;\n"
         "    float fg, bg;\n"
         "} pc;\n\n";

    /* 256 glyphs of 16 rows, one byte per row, packed four rows to a word. */
    s += "const uint font[" + std::to_string(sizeof(__font_bitmap__) / 4) + "] = uint[](";
    for (size_t i = 0; i + 3 < sizeof(__font_bitmap__); i += 4) {
        const uint32_t w = static_cast<uint32_t>(__font_bitmap__[i]) |
                           (static_cast<uint32_t>(__font_bitmap__[i + 1]) << 8) |
                           (static_cast<uint32_t>(__font_bitmap__[i + 2]) << 16) |
                           (static_cast<uint32_t>(__font_bitmap__[i + 3]) << 24);
        if (i)
            s += ',';
        s += std::to_string(w) + 'u';
    }
    s += ");\n\n";

    s += "void main() {\n"
         "    uint gx = gl_GlobalInvocationID.x;\n"
         "    uint cy = gl_GlobalInvocationID.y;\n"
         "    uint glyph = gx / pc.cellW;\n"
         "    if (glyph >= pc.count || cy >= pc.cellH) return;\n"
         "    uint cx = gx - glyph * pc.cellW;\n"
         "    uint w0 = fd[glyph * 2u];\n"
         "    bool on = false;\n"
         "    if (pc.scaleX != 0u) {\n"
         "        uint ch = w0 >> 24u;\n"
         "        uint byteIndex = ch * 16u + cy / pc.scaleY;\n"
         "        uint row = (font[byteIndex >> 2] >> ((byteIndex & 3u) * 8u)) & 0xFFu;\n"
         "        on = (row & (1u << (7u - cx / pc.scaleX))) != 0u;\n"
         "    }\n"
         "    uint px = ((w0 & 0xFFFFFFu) >> pc.subW) + cx;\n"
         "    uint py = (fd[glyph * 2u + 1u] >> pc.subH) + cy;\n"
         /* Never fires for a layout built to fit, which is the only kind that reaches here;
            it is what makes one that is not cost a dropped glyph instead of a write past the
            plane, the pipelines carrying no robustness for that. */
         "    if (px >= pc.width || py >= pc.height) return;\n"
         "    dstData[py * pc.dstStride + px] = SAMPLE_T(on ? pc.fg : pc.bg);\n"
         "}\n";
    return s;
}

static std::string textCopySource(const VSVideoFormat &fmt) {
    std::string s = textSamplePreamble(fmt);
    s += "\nlayout(local_size_x = 16, local_size_y = 16) in;\n"
         "layout(std430, set = 0, binding = 0) readonly buffer Src { SAMPLE_T srcData[]; };\n"
         "layout(std430, set = 0, binding = 1) writeonly buffer Dst { SAMPLE_T dstData[]; };\n"
         "layout(push_constant) uniform PC { uint width, height, srcStride, dstStride; } pc;\n"
         "void main() {\n"
         "    uint x = gl_GlobalInvocationID.x, y = gl_GlobalInvocationID.y;\n"
         "    if (x >= pc.width || y >= pc.height) return;\n"
         "    dstData[y * pc.dstStride + x] = srcData[y * pc.srcStride + x];\n"
         "}\n";
    return s;
}

static void map_text_to_font_indices(std::string& txt) {
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

        /* Substitute what has no glyph, then subtract the number of absent codes below this
           one. The thresholds are the absent codes themselves, and since each has just been
           excluded above, every range below is half open: > 141 can only be 142, because 143
           and 144 are handled by the branch above it. */
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

    map_text_to_font_indices(txt);

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
                    } else if (frame_format->bitsPerSample == 16) {
                        scrawl_character_half(iter[i], image, stride, dest_x, dest_y, scale);
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
                        } else if (frame_format->bitsPerSample == 16) {
                            scrawl_character_half(iter[i], image, stride, dest_x, dest_y, scale);
                        } else {
                            scrawl_character_float(iter[i], image, stride, dest_x, dest_y, scale);
                        }
                    } else {
                        int sub_w = scale * character_width  >> frame_format->subSamplingW;
                        int sub_h = scale * character_height >> frame_format->subSamplingH;
                        int sub_dest_x = dest_x >> frame_format->subSamplingW;
                        int sub_dest_y = dest_y >> frame_format->subSamplingH;
                        int y;

                        if (frame_format->sampleType == stFloat && frame_format->bitsPerSample == 16) {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset<uint16_t>(reinterpret_cast<uint16_t *>(image) + (y+sub_dest_y)*stride/2 + sub_dest_x, 0x0000, sub_w); // half 0.0
                            }
                        } else if (frame_format->bitsPerSample == 8) {
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

/* Everything either path needs to assemble and place the text, copyable so the GPU
   declaration's callbacks can hold it by shared_ptr. vi is a copy: ClipInfo prints from
   it, and the callbacks outlive any pointer into the node. */
struct TextState {
    std::string text;
    int alignment = 7;
    int scale = 1;
    intptr_t filter = 0;
    stringlist props;
    std::string instanceName;
    VSVideoInfo vi = {};
};

typedef struct {
    VSNode *node;
    const VSVideoInfo *vi;
    TextState st;
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
            int hint = vsapi->mapGetDataTypeHint(map, key.c_str(), idx, nullptr);
            text += " ";
            if (hint == dtBinary) {
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
        s = "Chromaticity derived NCL";
    else if (matrix == VSC_MATRIX_CHROMATICITY_DERIVED_CL)
        s = "Chromaticity derived CL";
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
        s = "SMPTE ST 170";
    else if (primaries == VSC_PRIMARIES_ST240_M)
        s = "SMPTE ST 240";
    else if (primaries == VSC_PRIMARIES_FILM)
        s = "FILM";
    else if (primaries == VSC_PRIMARIES_BT2020)
        s = "BT.2020";
    else if (primaries == VSC_PRIMARIES_ST428)
        s = "SMPTE ST 428-1";
    else if (primaries == VSC_PRIMARIES_ST431_2)
        s = "SMPTE RP 431-2";
    else if (primaries == VSC_PRIMARIES_ST432_1)
        s = "SMPTE EG 432-1";
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
            s = "SMPTE ST 170";
        else if (transfer == VSC_TRANSFER_ST240_M)
            s = "SMPTE ST 240";
        else if (transfer == VSC_TRANSFER_LINEAR)
            s = "Linear";
        else if (transfer == VSC_TRANSFER_LOG_100)
            s = "Logarithmic (100:1 range)";
        else if (transfer == VSC_TRANSFER_LOG_316)
            s = "Logarithmic (100 * Sqrt(10) : 1 range)";
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
            s = "SMPTE ST 2084";
        else if (transfer == VSC_TRANSFER_ST428)
            s = "SMPTE ST 428-1";
        else if (transfer == VSC_TRANSFER_ARIB_B67)
            s = "ARIB STD-B67";
        return s;
}

static std::string buildText(const TextState &st, int n, const VSFrame *src, VSCore *core, const VSAPI *vsapi) {
    std::string drawText;

    if (st.filter == FILTER_FRAMENUM) {
        drawText = std::to_string(n);
    } else if (st.filter == FILTER_FRAMEPROPS) {
        const VSMap *props = vsapi->getFramePropertiesRO(src);
        int numKeys = vsapi->mapNumKeys(props);
        int i;
        std::string text = "Frame properties:\n";

        if (!st.props.empty()) {
            for (const auto &iter : st.props) {
                append_prop(text, iter, props, vsapi);
            }
        } else {
            for (i = 0; i < numKeys; i++) {
                const char *key = vsapi->mapGetKey(props, i);
                append_prop(text, key, props, vsapi);
            }
        }

        drawText = text;
    } else if (st.filter == FILTER_COREINFO) {
        VSCoreInfo ci;
        vsapi->getCoreInfo(core, &ci);

        std::string text;
        text.append(ci.versionString).append("\n");
        text.append("Threads: ").append(std::to_string(ci.numThreads)).append("\n");
        text.append("Maximum framebuffer cache size: ").append(std::to_string(ci.maxFramebufferSize)).append(" bytes\n");
        text.append("Used framebuffer cache size: ").append(std::to_string(ci.usedFramebufferSize)).append(" bytes");

        drawText = text;
    } else if (st.filter == FILTER_CLIPINFO) {
        const VSMap *props = vsapi->getFramePropertiesRO(src);
        const VSVideoFormat *frame_format = vsapi->getVideoFrameFormat(src);
        std::string text = "Clip info:\n";

        if (st.vi.width) {
            text += "Width: " + std::to_string(vsapi->getFrameWidth(src, 0)) + " px\n";
            text += "Height: " + std::to_string(vsapi->getFrameHeight(src, 0)) + " px\n";
        } else {
            text += "Width: " + std::to_string(vsapi->getFrameWidth(src, 0)) + " px (may vary)\n";
            text += "Height: " + std::to_string(vsapi->getFrameHeight(src, 0)) + " px (may vary)\n";
        }

        int snerr, sderr;
        int64_t sn = vsapi->mapGetInt(props, "_SARNum", 0, &snerr);
        int64_t sd = vsapi->mapGetInt(props, "_SARDen", 0, &sderr);
        if (snerr || sderr)
            text += "Sample aspect ratio: Unknown\n";
        else
            text += "Sample aspect ratio: " + std::to_string(sn) + ":" + std::to_string(sd) + "\n";

        text += "Length: " + std::to_string(st.vi.numFrames) + " frames\n";

        char nameBuffer[32];
        vsapi->getVideoFormatName(&st.vi.format, nameBuffer);

        text += "Format name: "s + std::string(nameBuffer) + (st.vi.format.colorFamily == cfUndefined ? " (may vary)\n" : "\n");

        text += "Color family: " + colorFamilyToString(frame_format->colorFamily) + "\n";
        text += "Sample type: "s + (frame_format->sampleType == stInteger ? "Integer" : "Float") + "\n";
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
        int range = vsapi->mapGetIntSaturated(props, "_Range", 0, &err);
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
        text += "Picture type: "s + (picttype ? picttype : "Unknown") + "\n";

        if (st.vi.fpsNum && st.vi.fpsDen) {
            text += "Fps: " + std::to_string(st.vi.fpsNum) + "/" + std::to_string(st.vi.fpsDen) + " (" + std::to_string(static_cast<double>(st.vi.fpsNum) / st.vi.fpsDen) + ")\n";
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

        drawText = text;
    } else {
        drawText = st.text;
    }

    return drawText;
}

/* The glyph layout, shared by both paths in shape: the same map/split/alignment walk
   scrawl_text performs, emitted as records instead of drawn. Two words per glyph:
   font index and luma x in the first, luma y in the second. */
static std::vector<uint32_t> buildGlyphRecords(std::string txt, int width, int height, int alignment, int scale) {
    map_text_to_font_indices(txt);
    const stringlist lines = split_text(txt, width - margin_h * 2, height - margin_v * 2, scale);

    int start_y = 0;
    switch (alignment) {
    case 7: case 8: case 9: start_y = margin_v; break;
    case 4: case 5: case 6: start_y = (height - static_cast<int>(lines.size()) * character_height * scale) / 2; break;
    case 1: case 2: case 3: start_y = height - static_cast<int>(lines.size()) * character_height * scale - margin_v; break;
    }

    std::vector<uint32_t> records;
    for (const auto &line : lines) {
        int start_x = 0;
        switch (alignment) {
        case 1: case 4: case 7: start_x = margin_h; break;
        case 2: case 5: case 8: start_x = (width - static_cast<int>(line.size()) * character_width * scale) / 2; break;
        case 3: case 6: case 9: start_x = width - static_cast<int>(line.size()) * character_width * scale - margin_h; break;
        }
        for (size_t i = 0; i < line.size(); i++) {
            const uint32_t x = static_cast<uint32_t>(start_x + static_cast<int>(i) * character_width * scale);
            records.push_back((static_cast<uint32_t>(static_cast<unsigned char>(line[i])) << 24) | (x & 0xFFFFFFu));
            records.push_back(static_cast<uint32_t>(start_y));
        }
        start_y += character_height * scale;
    }
    return records;
}

static const VSFrame *VS_CC textGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSVideoFormat *frame_format = vsapi->getVideoFrameFormat(src);
        if (!is8to16orFloatFormat(*frame_format)) {
            vsapi->freeFrame(src);
            vsapi->setFilterError(invalidVideoFormatMessage(*frame_format, vsapi, d->st.instanceName.c_str()).c_str(), frameCtx);
            return nullptr;
        }

        int width = vsapi->getFrameWidth(src, 0);
        int height = vsapi->getFrameHeight(src, 0);

        int minimum_width = 2 * margin_h + character_width * d->st.scale;
        int minimum_height = 2 * margin_v + character_height * d->st.scale;

        if (width < minimum_width || height < minimum_height) {
            vsapi->freeFrame(src);
            vsapi->setFilterError((d->st.instanceName + ": frame size must be at least " + std::to_string(minimum_width) + "x" + std::to_string(minimum_height) + " pixels.").c_str(), frameCtx);
            return nullptr;
        }

        std::string drawText = buildText(d->st, n, src, core, vsapi);

        VSFrame *dst = vsapi->copyFrame(src, core);
        scrawl_text(drawText, d->st.alignment, d->st.scale, dst, vsapi);

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

/* The glyph list of the frame being prepared, handed from prepareFrame -- which publishes its
   length as the dispatch count -- to prepareFrameData, which stages it. The driver calls the
   pair back to back on the one thread producing that frame and enters no other filter in
   between, so a thread local is enough to keep the two halves of one frame together. */
static thread_local std::vector<uint32_t> gpuGlyphRecords;

static void createGPUText(std::unique_ptr<TextData> &d, VSMap *out, VSCore *core, const VSAPI *vsapi) {
    const std::string name = d->st.instanceName;
    auto fail = [&](const std::string &msg) {
        vsapi->mapSetError(out, (name + ": " + msg).c_str());
        if (d->node)
            vsapi->freeNode(d->node);
        d->node = nullptr;
    };

    if (!vsh::isConstantVideoFormat(d->vi)) {
        fail("the GPU path needs a clip with constant format and dimensions; "
            "insert GPUDownload to draw text on the CPU");
        return;
    }

    const VSVideoFormat fmt = d->vi->format;
    const int width = d->vi->width, height = d->vi->height;
    const int scale = d->st.scale;
    const bool rgb = fmt.colorFamily == cfRGB;
    const bool isInt = fmt.sampleType == stInteger;
    const float shift = isInt ? static_cast<float>(1 << (fmt.bitsPerSample - 8)) : 0.0f;
    const uint32_t ssW = fmt.subSamplingW, ssH = fmt.subSamplingH;

    /* The same capacity split_text clamps to, so the frame data buffer bounds every
       layout; a frame smaller than one glyph plus margins keeps failing per frame below,
       like the CPU path does. */
    const int hcap = std::max(0, (width - 2 * margin_h) / character_width / scale);
    const int vcap = std::max(0, (height - 2 * margin_v) / character_height / scale);
    const uint32_t maxGlyphs = static_cast<uint32_t>(std::max(1, hcap * vcap));

    auto st = std::make_shared<TextState>(d->st);
    const int minW = 2 * margin_h + character_width * scale;
    const int minH = 2 * margin_v + character_height * scale;

    vsgpu::FilterDesc desc;
    desc.vi = *d->vi;
    desc.nodes.push_back(d->node);

    desc.frameParamCount = 1;
    desc.prepareFrame = [st, core, minW, minH, maxGlyphs](int n, const VSFrame *const *sources, int,
        const VSAPI *vsapi, uint32_t *params, std::string &error) {
        const int w = vsapi->getFrameWidth(sources[0], 0), h = vsapi->getFrameHeight(sources[0], 0);
        if (w < minW || h < minH) {
            error = st->instanceName + ": frame size must be at least " + std::to_string(minW) +
                "x" + std::to_string(minH) + " pixels.";
            return false;
        }
        /* Built once and kept for prepareFrameData rather than built again there: the text is
           not necessarily the same twice -- CoreInfo prints the live framebuffer usage, and the
           driver allocates this frame's buffers between the two calls, which moves it -- and a
           count that disagrees with the records leaves the kernel dispatching over entries
           nobody wrote. Capped at the buffer's capacity for the same reason. */
        gpuGlyphRecords = buildGlyphRecords(buildText(*st, n, sources[0], core, vsapi), w, h,
            st->alignment, st->scale);
        if (gpuGlyphRecords.size() > static_cast<size_t>(maxGlyphs) * 2)
            gpuGlyphRecords.resize(static_cast<size_t>(maxGlyphs) * 2);
        params[0] = static_cast<uint32_t>(gpuGlyphRecords.size() / 2);
        return true;
    };
    desc.frameDataBytes = maxGlyphs * 2 * sizeof(uint32_t);
    desc.prepareFrameData = [](int, const VSFrame *const *, int,
        const VSAPI *, void *data, std::string &) {
        if (!gpuGlyphRecords.empty())
            std::memcpy(data, gpuGlyphRecords.data(), gpuGlyphRecords.size() * sizeof(uint32_t));
        gpuGlyphRecords.clear();
        return true;
    };

    vsgpu::Program copyProg;
    copyProg.glsl = textCopySource(fmt);
    copyProg.storageBufferCount = 2;
    copyProg.pushConstantBytes = sizeof(TextCopyPush);
    desc.programs.push_back(std::move(copyProg));

    vsgpu::Program glyphProg;
    glyphProg.glsl = textKernelSource(fmt);
    glyphProg.storageBufferCount = 2;
    glyphProg.pushConstantBytes = sizeof(TextGlyphPush);
    glyphProg.localSizeX = 8;
    glyphProg.localSizeY = 8;
    desc.programs.push_back(std::move(glyphProg));

    {
        vsgpu::Pass pass;
        pass.bindings = { vsgpu::Operand::source(), vsgpu::Operand::output() };
        desc.passes.push_back(std::move(pass));
    }
    {
        vsgpu::Pass pass;
        pass.program = 1;
        pass.bindings = { vsgpu::Operand::output(), vsgpu::Operand::frameData() };
        pass.reshape = [scale, rgb, ssW, ssH](vsgpu::PassInfo &info) {
            const uint32_t subW = (!rgb && info.plane > 0) ? ssW : 0;
            const uint32_t subH = (!rgb && info.plane > 0) ? ssH : 0;
            info.width = info.frameParams[0] * ((character_width * scale) >> subW);
            info.height = (character_height * scale) >> subH;
        };
        desc.passes.push_back(std::move(pass));
    }

    desc.fillPush = [scale, rgb, isInt, shift, ssW, ssH](const vsgpu::PassInfo &info, void *push) {
        if (info.pass == 0) {
            TextCopyPush pc = { info.width, info.height, info.strideElements[0], info.strideElements[1] };
            std::memcpy(push, &pc, sizeof(pc));
            return;
        }
        const bool chroma = !rgb && info.plane > 0;
        const uint32_t subW = chroma ? ssW : 0, subH = chroma ? ssH : 0;
        TextGlyphPush pc = {};
        pc.dstStride = info.strideElements[0];
        pc.cellW = (character_width * scale) >> subW;
        pc.cellH = (character_height * scale) >> subH;
        pc.count = info.frameParams[0];
        /* Zero scaleX is the flat rectangle case; chroma never carries a glyph. */
        pc.scaleX = chroma ? 0u : static_cast<uint32_t>(scale);
        pc.scaleY = static_cast<uint32_t>(scale);
        pc.subW = subW;
        pc.subH = subH;
        /* srcWidth/srcHeight are this plane's dimensions -- reshape moved width/height to the
           glyph grid -- and Text does not change geometry, so they bound the store. */
        pc.width = info.srcWidth;
        pc.height = info.srcHeight;
        if (chroma) {
            pc.fg = pc.bg = isInt ? 128.0f * shift : 0.0f;
        } else {
            pc.fg = isInt ? 235.0f * shift : 1.0f;
            pc.bg = isInt ? 16.0f * shift : 0.0f;
        }
        std::memcpy(push, &pc, sizeof(pc));
    };

    VSFilterDependency deps[] = {{ d->node, rpStrictSpatial }};
    std::string error;
    VSNode *node = vsgpu::createFilter(name.c_str(), desc, deps, 1, core, vsapi, error);
    d->node = nullptr; /* consumed on success and failure alike */
    if (node)
        vsapi->mapConsumeNode(out, "clip", node, maAppend);
    else
        vsapi->mapSetError(out, (name + ": " + error).c_str());
}


static void VS_CC textFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TextData *d = static_cast<TextData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}


static void VS_CC textCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<TextData> d(new TextData{});
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

    if (!is8to16orFloatFormat(d->vi->format, true)) {
        vsapi->mapSetError(out, invalidVideoFormatMessage(d->vi->format, vsapi, "Text", true).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    d->st.alignment = vsapi->mapGetIntSaturated(in, "alignment", 0, &err);
    if (err) {
        d->st.alignment = 7; // top left
    }

    if (d->st.alignment < 1 || d->st.alignment > 9) {
        vsapi->mapSetError(out, "Text: alignment must be between 1 and 9 (think numpad)");
        vsapi->freeNode(d->node);
        return;
    }

    d->st.scale = vsh::int64ToIntS(vsapi->mapGetInt(in, "scale", 0, &err));
    if (err) {
        d->st.scale = 1;
    }

    if (d->st.scale < 1 || d->st.scale > (1 << 16)) {
        vsapi->mapSetError(out, "Text: scale must be a positive integer no larger than 65536");
        vsapi->freeNode(d->node);
        return;
    }

    d->st.filter = reinterpret_cast<intptr_t>(userData);
    d->st.vi = *d->vi;

    switch (d->st.filter) {
    case FILTER_TEXT:
        d->st.text = vsapi->mapGetData(in, "text", 0, nullptr);
        d->st.instanceName = "Text";
        break;
    case FILTER_CLIPINFO:
        d->st.instanceName = "ClipInfo";
        break;
    case FILTER_COREINFO:
        d->st.instanceName = "CoreInfo";
        break;
    case FILTER_FRAMENUM:
        d->st.instanceName = "FrameNum";
        break;
    case FILTER_FRAMEPROPS:
        int numProps = vsapi->mapNumElements(in, "props");

        for (int i = 0; i < numProps; i++) {
            d->st.props.push_back(vsapi->mapGetData(in, "props", i, nullptr));
        }

        d->st.instanceName = "FrameProps";
        break;
    }

    if (vsapi->getNodeResidency(d->node) == nrGPU) {
        createGPUText(d, out, core, vsapi);
        return;
    }

    VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
    vsapi->createVideoFilterEx(out, d->st.instanceName.c_str(), d->vi, textGetFrame, textFree,
        fmParallel, 0, deps, 1, d.get(), core);
    d.release();
}


void textInitialize(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin(VSH_TEXT_PLUGIN_ID, "text", "VapourSynth Text", VAPOURSYNTH_INTERNAL_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Text",
        "clip:vnode:all;"
        "text:data;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode:all;",
        textCreate, reinterpret_cast<void *>(FILTER_TEXT), plugin);
    vspapi->registerFunction("ClipInfo",
        "clip:vnode:all;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode:all;",
        textCreate, reinterpret_cast<void *>(FILTER_CLIPINFO), plugin);
    vspapi->registerFunction("CoreInfo",
        "clip:vnode:all:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode:all;",
        textCreate, reinterpret_cast<void *>(FILTER_COREINFO), plugin);
    vspapi->registerFunction("FrameNum",
        "clip:vnode:all;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode:all;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMENUM), plugin);
    vspapi->registerFunction("FrameProps",
        "clip:vnode:all;"
        "props:data[]:opt;"
        "alignment:int:opt;"
        "scale:int:opt;",
        "clip:vnode:all;",
        textCreate, reinterpret_cast<void *>(FILTER_FRAMEPROPS), plugin);
}
