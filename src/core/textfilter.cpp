/*
* Copyright (c) 2013 John Smith
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

#include <stdint.h>
#include <stdlib.h>

#include <list>
#include <string>

#include <VapourSynth.h>
#include <VSHelper.h>

#include "filtershared.h"
#include "ter-116n.h"
#include "textfilter.h"

typedef std::list<std::string> stringlist;

void scrawl_character_int(unsigned char c, uint8_t *image, int stride, int dest_x, int dest_y, int bitsPerSample) {
    int black = 16 << (bitsPerSample - 8);
    int white = 235 << (bitsPerSample - 8);
    int x, y;
    if (bitsPerSample == 8) {
        for (y = 0; y < character_height; y++) {
            for (x = 0; x < character_width; x++) {
                if (__font_bitmap__[c * character_height + y] & (1 << (7 - x))) {
                    image[dest_y*stride + dest_x + x] = white;
                } else {
                    image[dest_y*stride + dest_x + x] = black;
                }
            }

            dest_y++;
        }
    } else {
        for (y = 0; y < character_height; y++) {
            for (x = 0; x < character_width; x++) {
                if (__font_bitmap__[c * character_height + y] & (1 << (7 - x))) {
                    ((uint16_t*)image)[dest_y*stride/2 + dest_x + x] = white;
                } else {
                    ((uint16_t*)image)[dest_y*stride/2 + dest_x + x] = black;
                }
            }

            dest_y++;
        }
    }
}


void scrawl_character_float(unsigned char c, uint8_t *image, int stride, int dest_x, int dest_y) {
    float white = 1.0f;
    float black = 0.0f;
    int x, y;

    for (y = 0; y < character_height; y++) {
        for (x = 0; x < character_width; x++) {
            if (__font_bitmap__[c * character_height + y] & (1 << (7 - x))) {
                ((float*)image)[dest_y*stride/4 + dest_x + x] = white;
            } else {
                ((float*)image)[dest_y*stride/4 + dest_x + x] = black;
            }
        }

        dest_y++;
    }
}

void sanitise_text(std::string& txt) {
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
        unsigned char current_char = (unsigned char)txt[i];
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


stringlist split_text(const std::string& txt, int width, int height) {
    stringlist lines;

    // First split by \n
    int prev_pos = -1;
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
    size_t horizontal_capacity = width / character_width;
    stringlist::iterator iter;
    for (iter = lines.begin(); iter != lines.end(); iter++) {
        if (iter->size() > horizontal_capacity) {
            lines.insert(std::next(iter), iter->substr(horizontal_capacity));
            iter->erase(horizontal_capacity);
        }
    }

    // Also drop lines that would go over the frame's bottom edge
    size_t vertical_capacity = height / character_height;
    if (lines.size() > vertical_capacity) {
        lines.resize(vertical_capacity);
    }

    return lines;
}


void scrawl_text(std::string txt, int alignment, VSFrameRef *frame, const VSAPI *vsapi) {
    const VSFormat *frame_format = vsapi->getFrameFormat(frame);
    int width = vsapi->getFrameWidth(frame, 0);
    int height = vsapi->getFrameHeight(frame, 0);

    const int margin_h = 16;
    const int margin_v = 16;

    sanitise_text(txt);

    stringlist lines = split_text(txt, width - margin_h*2, height - margin_v*2);

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
        start_y = (height - lines.size()*character_height) / 2;
        break;
    case 1:
    case 2:
    case 3:
        start_y = height - lines.size()*character_height - margin_v;
        break;
    }

    for (stringlist::const_iterator iter = lines.begin(); iter != lines.end(); iter++) {
        switch (alignment) {
        case 1:
        case 4:
        case 7:
            start_x = margin_h;
            break;
        case 2:
        case 5:
        case 8:
            start_x = (width - iter->size()*character_width) / 2;
            break;
        case 3:
        case 6:
        case 9:
            start_x = width - iter->size()*character_width - margin_h;
            break;
        }

        for (size_t i = 0; i < iter->size(); i++) {
            int dest_x = start_x + i*character_width;
            int dest_y = start_y;

            if (frame_format->colorFamily == cmRGB) {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    int stride = vsapi->getStride(frame, plane);

                    if (frame_format->sampleType == stInteger) {
                        scrawl_character_int((*iter)[i], image, stride, dest_x, dest_y, frame_format->bitsPerSample);
                    } else {
                        scrawl_character_float((*iter)[i], image, stride, dest_x, dest_y);
                    }
                }
            } else {
                for (int plane = 0; plane < frame_format->numPlanes; plane++) {
                    uint8_t *image = vsapi->getWritePtr(frame, plane);
                    int stride = vsapi->getStride(frame, plane);

                    if (plane == 0) {
                        if (frame_format->sampleType == stInteger) {
                            scrawl_character_int((*iter)[i], image, stride, dest_x, dest_y, frame_format->bitsPerSample);
                        } else {
                            scrawl_character_float((*iter)[i], image, stride, dest_x, dest_y);
                        }
                    } else {
                        int sub_w = character_width  >> frame_format->subSamplingW;
                        int sub_h = character_height >> frame_format->subSamplingH;
                        int sub_dest_x = dest_x >> frame_format->subSamplingW;
                        int sub_dest_y = dest_y >> frame_format->subSamplingH;
                        int y;

                        if (frame_format->bitsPerSample == 8) {
                            for (y = 0; y < sub_h; y++) {
                                memset(image + (y+sub_dest_y)*stride + sub_dest_x, 128, sub_w);
                            }
                        } else if (frame_format->bitsPerSample <= 16) {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset16((uint16_t*)image + (y+sub_dest_y)*stride/2 + sub_dest_x, 128 << (frame_format->bitsPerSample - 8), sub_w);
                            }
                        } else {
                            for (y = 0; y < sub_h; y++) {
                                vs_memset_float((float*)image + (y+sub_dest_y)*stride/4 + sub_dest_x, 0.0f, sub_w);
                            }
                        }
                    } // if plane
                } // for plane in planes
            } // if colorFamily
        } // for i in line
        start_y += character_height;
    } // for iter in lines
}


enum Filters {
    FILTER_TEXT,
    FILTER_CLIPINFO,
    FILTER_COREINFO,
    FILTER_FRAMENUM,
    FILTER_FRAMEPROPS
};


typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi;

    std::string text;
    int alignment;
    intptr_t filter;
    stringlist props;
    std::string instanceName;
} TextData;


static void VS_CC textInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TextData *d = (TextData *) * instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}


static void append_prop(std::string &text, const std::string &key, const VSMap *map, const VSAPI *vsapi) {
    char type = vsapi->propGetType(map, key.c_str());
    int numElements = vsapi->propNumElements(map, key.c_str());
    int idx;
    // "<key>: <val0> <val1> <val2> ... <valn-1>"
    text.append(key).append(": ");
    if (type == ptInt) {
        for (idx = 0; idx < numElements; idx++) {
            int64_t value = vsapi->propGetInt(map, key.c_str(), idx, NULL);
            text.append(std::to_string(value));
            if (idx < numElements-1) {
                text.append(" ");
            }
        }
    } else if (type == ptFloat) {
        for (idx = 0; idx < numElements; idx++) {
            double value = vsapi->propGetFloat(map, key.c_str(), idx, NULL);
            text.append(std::to_string(value));
            if (idx < numElements-1) {
                text.append(" ");
            }
        }
    } else if (type == ptData) {
        for (idx = 0; idx < numElements; idx++) {
            const char *value = vsapi->propGetData(map, key.c_str(), idx, NULL);
            int size = vsapi->propGetDataSize(map, key.c_str(), idx, NULL);
            if (size > 100) {
                text.append("<property too long>");
            } else {
                text.append(value);
            }
            if (idx < numElements-1) {
                text.append(" ");
            }
        }
    } else if (type == ptUnset) {
        text.append("<no such property>");
    }

    text.append("\n");
}


static const VSFrameRef *VS_CC textGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    TextData *d = (TextData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef *dst = vsapi->copyFrame(src, core);
        vsapi->freeFrame(src);

        const VSFormat *frame_format = vsapi->getFrameFormat(dst);
        if ((frame_format->sampleType == stInteger && frame_format->bitsPerSample > 16) ||
            (frame_format->sampleType == stFloat && frame_format->bitsPerSample != 32)) {
                vsapi->freeFrame(dst);
                vsapi->setFilterError((d->instanceName + ": Only 8..16 bit integer and 32 bit float formats supported").c_str(), frameCtx);
                return NULL;
        }

        if (d->filter == FILTER_FRAMENUM) {
            scrawl_text(std::to_string(n), d->alignment, dst, vsapi);
        } else if (d->filter == FILTER_FRAMEPROPS) {
            const VSMap *props = vsapi->getFramePropsRO(dst);
            int numKeys = vsapi->propNumKeys(props);
            int i;
            std::string text = "Frame properties:\n";

            if (!d->props.empty()) {
                for (stringlist::const_iterator iter = d->props.begin(); iter != d->props.end(); ++iter) {
                    append_prop(text, *iter, props, vsapi);
                }
            } else {
                for (i = 0; i < numKeys; i++) {
                    const char *key = vsapi->propGetKey(props, i);
                    append_prop(text, key, props, vsapi);
                }
            }

            scrawl_text(text, d->alignment, dst, vsapi);
        } else if (d->filter == FILTER_COREINFO) {
            const VSCoreInfo *ci = vsapi->getCoreInfo(core);

            std::string text;
            text.append(ci->versionString).append("\n");
            text.append("Threads: ").append(std::to_string(ci->numThreads)).append("\n");
            text.append("Maximum framebuffer cache size: ").append(std::to_string(ci->maxFramebufferSize)).append(" bytes\n");
            text.append("Used framebuffer cache size: ").append(std::to_string(ci->usedFramebufferSize)).append(" bytes");

            scrawl_text(text, d->alignment, dst, vsapi);
        } else {
            scrawl_text(d->text, d->alignment, dst, vsapi);
        }

        return dst;
    }

    return 0;
}


static void VS_CC textFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TextData *d = (TextData *)instanceData;
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
            msg.append(error);
            vsapi->setError(out, msg.c_str());
            vsapi->freeMap(ret);
            return;
        }
        d.node = vsapi->propGetNode(ret, "clip", 0, 0);
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

    d.filter = (intptr_t)userData;

    switch (d.filter) {
    case FILTER_TEXT:
        d.text = vsapi->propGetData(in, "text", 0, 0);

        d.instanceName = "Text";
        break;
    case FILTER_CLIPINFO:
        d.text.append("Clip info:\n");

        if (d.vi->width) {
            d.text.append("Width: ").append(std::to_string(d.vi->width)).append(" px\n");
            d.text.append("Height: ").append(std::to_string(d.vi->height)).append(" px\n");
        } else {
            d.text.append("Width: may vary\n");
            d.text.append("Height: may vary\n");
        }

        if (d.vi->numFrames) {
            d.text.append("Length: ").append(std::to_string(d.vi->numFrames)).append(" frames\n");
        } else {
            d.text.append("Length: unknown\n");
        }

        if (d.vi->format) {
            const VSFormat *fi = d.vi->format;
            const char *family;
            switch (fi->colorFamily) {
            case cmGray:
                family = "Gray";
                break;
            case cmRGB:
                family = "RGB";
                break;
            case cmYUV:
                family = "YUV";
                break;
            case cmYCoCg:
                family = "YCoCg";
                break;
            case cmCompat:
                family = "Compat";
                break;
            default:
                family = "impossible";
                break;
            }

            const char *type;
            switch (fi->sampleType) {
            case stInteger:
                type = "integer";
                break;
            case stFloat:
                type = "float";
                break;
            default:
                type = "impossible";
                break;
            }

            d.text.append("Format name: ").append(fi->name).append("\n");
            d.text.append("Format id: ").append(std::to_string(fi->id)).append("\n");
            d.text.append("Color family: ").append(family).append("\n");
            d.text.append("Sample type: ").append(type).append("\n");
            d.text.append("Bits per sample: ").append(std::to_string(fi->bitsPerSample)).append("\n");
            d.text.append("Bytes per sample: ").append(std::to_string(fi->bytesPerSample)).append("\n");
            d.text.append("Horizontal subsampling: ").append(std::to_string(fi->subSamplingW)).append("\n");
            d.text.append("Vertical subsampling: ").append(std::to_string(fi->subSamplingH)).append("\n");
            d.text.append("Number of planes: ").append(std::to_string(fi->numPlanes)).append("\n");
        } else {
            d.text.append("Format: may vary").append("\n");
        }

        d.text.append("FpsNum: ").append(std::to_string(d.vi->fpsNum)).append("\n");
        d.text.append("FpsDen: ").append(std::to_string(d.vi->fpsDen));

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
            d.props.push_back(vsapi->propGetData(in, "props", i, 0));
        }

        d.instanceName = "FrameProps";
        break;
    }

    data = new TextData();
    *data = d;

    vsapi->createFilter(in, out, d.instanceName.c_str(), textInit, textGetFrame, textFree, fmParallel, 0, data, core);
}


void VS_CC textInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.text", "text", "VapourSynth Text", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Text",
        "clip:clip;"
        "text:data;"
        "alignment:int:opt;",
        textCreate, (void *)FILTER_TEXT, plugin);
    registerFunc("ClipInfo",
        "clip:clip;"
        "alignment:int:opt;",
        textCreate, (void *)FILTER_CLIPINFO, plugin);
    registerFunc("CoreInfo",
        "clip:clip:opt;"
        "alignment:int:opt;",
        textCreate, (void *)FILTER_COREINFO, plugin);
    registerFunc("FrameNum",
        "clip:clip;"
        "alignment:int:opt;",
        textCreate, (void *)FILTER_FRAMENUM, plugin);
    registerFunc("FrameProps",
        "clip:clip;"
        "props:data[]:opt;"
        "alignment:int:opt;",
        textCreate, (void *)FILTER_FRAMEPROPS, plugin);
}
