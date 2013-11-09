/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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

#include "vscore.h"
#include "VSHelper.h"
#include "version.h"
#include <QtCore/QSettings>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <assert.h>
#include <regex>
#include <codecvt>

#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

#ifdef VS_TARGET_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <ShlObj.h>
#endif

// Internal filter headers
extern "C" {
#include "lutfilters.h"
#include "mergefilters.h"
#include "reorderfilters.h"
#include "simplefilters.h"
#include "vsresize.h"
}
#include "cachefilter.h"
#include "exprfilter.h"
#include "textfilter.h"

const VSAPI *VS_CC getVapourSynthAPI(int version);


static const std::regex idRegExp("^[a-zA-Z][a-zA-Z0-9_]*$");
static const std::regex sysPropRegExp("^_[a-zA-Z0-9_]*$");

static bool isValidIdentifier(const std::string &s) {
    return std::regex_match(s, idRegExp);
}

FrameContext::FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext) : numFrameRequests(0), index(index), n(n), node(NULL), clip(clip), upstreamContext(upstreamContext), userData(NULL), frameContext(NULL), frameDone(NULL), error(false), lastCompletedN(-1), lastCompletedNode(NULL) {
}

FrameContext::FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData) : numFrameRequests(0), index(index), n(n), node(node), clip(node->clip.get()), frameContext(NULL), userData(userData), frameDone(frameDone), error(false), lastCompletedN(-1), lastCompletedNode(NULL) {
}

void FrameContext::setError(const std::string &errorMsg) {
    error = true;
    errorMessage = errorMsg;
}

///////////////


VSVariant::VSVariant(VSVType vtype) : vtype(vtype), internalSize(0), storage(NULL) {
}

VSVariant::VSVariant(const VSVariant &v) : vtype(v.vtype), internalSize(v.internalSize), storage(NULL) {
    if (internalSize) {
        switch (vtype) {
        case VSVariant::vInt:
            storage = new IntList(*reinterpret_cast<IntList *>(v.storage)); break;
        case VSVariant::vFloat:
            storage = new FloatList(*reinterpret_cast<FloatList *>(v.storage)); break;
        case VSVariant::vData:
            storage = new DataList(*reinterpret_cast<DataList *>(v.storage)); break;
        case VSVariant::vNode:
            storage = new NodeList(*reinterpret_cast<NodeList *>(v.storage)); break;
        case VSVariant::vFrame:
            storage = new FrameList(*reinterpret_cast<FrameList *>(v.storage)); break;
        case VSVariant::vMethod:
            storage = new FuncList(*reinterpret_cast<FuncList *>(v.storage)); break;
        }
    }
}

VSVariant::~VSVariant() {
    if (storage) {
        switch (vtype) {
        case VSVariant::vInt:
            delete reinterpret_cast<IntList *>(storage); break;
        case VSVariant::vFloat:
            delete reinterpret_cast<FloatList *>(storage); break;
        case VSVariant::vData:
            delete reinterpret_cast<DataList *>(storage); break;
        case VSVariant::vNode:
            delete reinterpret_cast<NodeList *>(storage); break;
        case VSVariant::vFrame:
            delete reinterpret_cast<FrameList *>(storage); break;
        case VSVariant::vMethod:
            delete reinterpret_cast<FuncList *>(storage); break;
        }
    }
}

int VSVariant::size() const {
    return internalSize;
}

VSVariant::VSVType VSVariant::getType() const {
    return vtype;
}

void VSVariant::append(int64_t val) {
    initStorage(vInt);
    reinterpret_cast<IntList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::append(double val) {
    initStorage(vFloat);
    reinterpret_cast<FloatList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::append(const std::string &val) {
    initStorage(vData);
    reinterpret_cast<DataList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::append(const VSNodeRef &val) {
    initStorage(vNode);
    reinterpret_cast<NodeList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::append(const PVideoFrame &val) {
    initStorage(vFrame);
    reinterpret_cast<FrameList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::append(const PExtFunction &val) {
    initStorage(vMethod);
    reinterpret_cast<FuncList *>(storage)->append(val);
    internalSize++;
}

void VSVariant::initStorage(VSVType t) {
    assert(vtype == vUnset || vtype == t);
    vtype = t;
    if (!storage) {
        switch (t) {
        case VSVariant::vInt:
            storage = new IntList(); break;
        case VSVariant::vFloat:
            storage = new FloatList(); break;
        case VSVariant::vData:
            storage = new DataList(); break;
        case VSVariant::vNode:
            storage = new NodeList(); break;
        case VSVariant::vFrame:
            storage = new FrameList(); break;
        case VSVariant::vMethod:
            storage = new FuncList(); break;
        }
    }
}

///////////////

VSFrameData::VSFrameData(quint32 size, MemoryUse *mem) : QSharedData(), mem(mem), size(size) {
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for planes. Out of memory.");
    mem->add(size);
}

VSFrameData::VSFrameData(const VSFrameData &d) : QSharedData(d) {
    size = d.size;
    mem = d.mem;
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
    assert(data);
    if (!data)
        vsFatal("Failed to allocate memory for plane in copy constructor. Out of memory.");
    mem->add(size);
    memcpy(data, d.data, size);
}

VSFrameData::~VSFrameData() {
    vs_aligned_free(data);
    mem->subtract(size);
}

///////////////

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc, VSCore *core) : format(f), width(width), height(height), frameLocation(flLocal) {
    if (!f || width <= 0 || height <= 0)
        vsFatal("Invalid new frame");

    if (propSrc)
        properties = propSrc->properties;

    stride[0] = (width * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (f->numPlanes == 3) {
        int plane23 = ((width >> f->subSamplingW) * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    data[0] = new VSFrameData(stride[0] * height, core->memory);
    if (f->numPlanes == 3) {
        int size23 = stride[1] * (height >> f->subSamplingH);
        data[1] = new VSFrameData(size23, core->memory);
        data[2] = new VSFrameData(size23, core->memory);
    }
}

VSFrame::VSFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *plane, const VSFrame *propSrc, VSCore *core) : format(f), width(width), height(height), frameLocation(flLocal) {
    if (!f || width <= 0 || height <= 0)
        vsFatal("Invalid new frame");

    if (propSrc)
        properties = propSrc->properties;

    stride[0] = (width * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);

    if (f->numPlanes == 3) {
        int plane23 = ((width >> f->subSamplingW) * (f->bytesPerSample) + (alignment - 1)) & ~(alignment - 1);
        stride[1] = plane23;
        stride[2] = plane23;
    } else {
        stride[1] = 0;
        stride[2] = 0;
    }

    for (int i = 0; i < format->numPlanes; i++) {
        if (planeSrc[i]) {
            if (plane[i] < 0 || plane[i] >= planeSrc[i]->format->numPlanes)
                vsFatal("Plane does no exist, error in frame creation");
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                vsFatal("Copied plane dimensions do not match, error in frame creation");
            data[i] = planeSrc[i]->data[plane[i]];
        } else {
            if (i == 0) {
                data[i] = new VSFrameData(stride[0] * height, core->memory);
            } else {
                data[i] = new VSFrameData(stride[i] * (height >> f->subSamplingH), core->memory);
            }
        }
    }
}

VSFrame::VSFrame(const VSFrame &f) {
    data[0] = f.data[0];
    data[1] = f.data[1];
    data[2] = f.data[2];
    format = f.format;
    width = f.width;
    height = f.height;
    stride[0] = f.stride[0];
    stride[1] = f.stride[1];
    stride[2] = f.stride[2];
    frameLocation = f.frameLocation;
    properties = f.properties;
}

int VSFrame::getStride(int plane) const {
    assert(plane >= 0 && plane < 3);
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane stride requested");
    return stride[plane];
}

const uint8_t * VS_RESTRICT VSFrame::getReadPtr(int plane) const {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane requested");

    switch (plane) {
    case 0:
        return data[0].constData()->data;
    case 1:
        return data[1].constData()->data;
    case 2:
        return data[2].constData()->data;
    default:
        return NULL;
    }
}

uint8_t * VS_RESTRICT VSFrame::getWritePtr(int plane) {
    if (plane < 0 || plane >= format->numPlanes)
        vsFatal("Invalid plane requested");

    switch (plane) {
    case 0:
        return data[0].data()->data;
    case 1:
        return data[1].data()->data;
    case 2:
        return data[2].data()->data;
    default:
        return NULL;
    }
}

struct split1 {
    enum empties_t { empties_ok, no_empties };
};

template <typename Container>
Container& split(
    Container& result,
    const typename Container::value_type& s,
    const typename Container::value_type& delimiters,
    split1::empties_t empties = split1::empties_ok) {
    result.clear();
    size_t current;
    size_t next = -1;
    do {
        if (empties == split1::no_empties) {
            next = s.find_first_not_of(delimiters, next + 1);
            if (next == Container::value_type::npos) break;
            next -= 1;
        }
        current = next + 1;
        next = s.find_first_of(delimiters, current);
        result.push_back(s.substr(current, next - current));
    } while (next != Container::value_type::npos);
    return result;
}

VSFunction::VSFunction(const std::string &argString, VSPublicFunction func, void *functionData)
    : argString(argString), functionData(functionData), func(func) {
    std::list<std::string> argList;
    split(argList, argString, std::string(";"), split1::no_empties);
    for(const std::string &arg : argList) {
        std::vector<std::string> argParts;
        split(argParts, arg, std::string(":"), split1::no_empties);

        if (argParts.size() < 2)
            vsFatal("Invalid argument specifier: %s", arg.c_str());

        bool arr = false;
        enum FilterArgumentType type = faNone;
        const std::string &argName = argParts[0];
        const std::string &typeName = argParts[1];

        if (typeName == "int")
            type = faInt;
        else if (typeName == "float")
            type = faFloat;
        else if (typeName == "data")
            type = faData;
        else if (typeName == "clip")
            type = faClip;
        else if (typeName == "frame")
            type = faFrame;
        else if (typeName == "func")
            type = faFunc;
        else {
            arr = true;

            if (typeName == "int[]")
                type = faInt;
            else if (typeName == "float[]")
                type = faFloat;
            else if (typeName == "data[]")
                type = faData;
            else if (typeName == "clip[]")
                type = faClip;
            else if (typeName == "frame[]")
                type = faFrame;
            else if (typeName == "func[]")
                type = faFunc;
            else
                vsFatal("Invalid arg string: %s", typeName.c_str());
        }

        bool opt = false;
        bool empty = false;

        for (size_t i = 2; i < argParts.size(); i++) {
            if (argParts[i] == "opt") {
                if (opt)
                    vsFatal("Duplicate argument specifier: %s", argParts[i]);
                opt = true;
            } else if (argParts[i] == "empty") {
                if (empty)
                    vsFatal("Duplicate argument specifier: %s", argParts[i]);
                empty = true;
            }  else {
                vsFatal("Unknown argument modifier: %s", argParts[i]);
            }
        }

        if (!isValidIdentifier(argName))
            vsFatal("Illegal argument identifier specified for function");

        if (empty && !arr)
            vsFatal("Only array arguments can have the empty flag set");

        args.push_back(FilterArgument(argName, type, arr, empty, opt));
    }
}

VSNode::VSNode(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion, VSCore *core) :
    instanceData(instanceData), name(name), init(init), filterGetFrame(getFrame), free(free), filterMode(filterMode), apiVersion(apiVersion), core(core), flags(flags), hasVi(false), serialFrame(-1) {
    VSMap inval(*in);
    init(&inval, out, &this->instanceData, this, core, getVSAPIInternal(apiVersion));

    if (vsapi.getError(out))
        throw VSException(vsapi.getError(out));

    if (!hasVi)
        vsFatal("Filter %s didn't set vi", name.c_str());
}

VSNode::~VSNode() {
    if (free)
        free(instanceData, core, &vsapi);
}

void VSNode::getFrame(const PFrameContext &ct) {
    core->threadPool->start(ct);
}

const VSVideoInfo &VSNode::getVideoInfo(int index) {
    if (index < 0 || index >= (int)vi.size())
        vsFatal("Out of bounds videoinfo index");
    return vi[index];
}
void VSNode::setVideoInfo(const VSVideoInfo *vi, int numOutputs) {
    if (numOutputs < 1)
        vsFatal("Video filter needs to have at least one output");
    for (int i = 0; i < numOutputs; i++) {
        if ((!!vi[i].height) ^ (!!vi[i].width))
            vsFatal("Variable dimension clips must have both width and height set to 0");
        if (vi[i].format && !core->isValidFormatPointer(vi[i].format))
            vsFatal("The VSFormat pointer passed to setVideoInfo() was not gotten from registerFormat() or getFormatPreset()");
        this->vi.push_back(vi[i]);
        this->vi[i].flags = flags;
    }
    hasVi = true;
}

PVideoFrame VSNode::getFrameInternal(int n, int activationReason, VSFrameContext &frameCtx) {
    const VSFrameRef *r = filterGetFrame(n, activationReason, &instanceData, &frameCtx.ctx->frameContext, &frameCtx, core, &vsapi);
// This stuff really only works properly on windows, feel free to investigate what the linux ABI thinks about it
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected after return from %s", name.c_str());
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after return from %s", name.c_str());
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after return from %s", name.c_str());
#endif

    if (r) {
        PVideoFrame p(r->frame);
        delete r;
        const VSFormat *fi = p->getFormat();
        const VSVideoInfo &lvi = vi[frameCtx.ctx->index];

        if (!lvi.format && fi->colorFamily == cmCompat)
            vsFatal("Illegal compat frame returned");
        else if (lvi.format && lvi.format != fi)
            vsFatal("Frame returned not of the declared type");
        else if ((lvi.width || lvi.height) && (p->getWidth(0) != lvi.width || p->getHeight(0) != lvi.height))
            vsFatal("Frame returned of not of the declared dimensions");

        return p;
    }

    PVideoFrame p;
    return p;
}

void VSNode::reserveThread() {
    core->threadPool->reserveThread();
}

void VSNode::releaseThread() {
    core->threadPool->releaseThread();
}

bool VSNode::isWorkerThread() {
    return core->threadPool->isWorkerThread();
}

void VSNode::notifyCache(bool needMemory) {
    std::lock_guard<std::mutex> lock(serialMutex);
    CacheInstance *cache = (CacheInstance *)instanceData;
    cache->cache.adjustSize(needMemory);
}

PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc) {
    return std::make_shared<VSFrame>(f, width, height, propSrc, this);
}

PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *planes, const VSFrame *propSrc) {
    return std::make_shared<VSFrame>(f, width, height, planeSrc, planes, propSrc, this);
}

PVideoFrame VSCore::copyFrame(const PVideoFrame &srcf) {
    return std::make_shared<VSFrame>(*srcf.get());
}

void VSCore::copyFrameProps(const PVideoFrame &src, PVideoFrame &dst) {
    dst->setProperties(src->getProperties());
}

const VSFormat *VSCore::getFormatPreset(int id) {
    std::lock_guard<std::mutex> lock(formatLock);

    try {
        return formats.at(id);
    } catch (std::out_of_range &) {
    }

    return NULL;
}

const VSFormat *VSCore::registerFormat(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name, int id) {
    // this is to make exact format comparisons easy by simply allowing pointer comparison

    // block nonsense formats
    if (subSamplingH < 0 || subSamplingW < 0 || subSamplingH > 4 || subSamplingW > 4)
        vsFatal("Nonsense format registration");

    if (sampleType < 0 || sampleType > 1)
        vsFatal("Invalid sample type");

    if (colorFamily == cmRGB && (subSamplingH != 0 || subSamplingW != 0))
        vsFatal("We do not like subsampled rgb around here");

    if (sampleType == stFloat && (bitsPerSample != 16 && bitsPerSample != 32))
        vsFatal("Only floating point formats with 16 or 32 bit precision are allowed");

    if (bitsPerSample < 8 || bitsPerSample > 32)
        vsFatal("Only formats with 8-32 bits per sample are allowed");

    if (colorFamily == cmCompat && !name)
        vsFatal("No compatibility formats may be registered");

    std::lock_guard<std::mutex> lock(formatLock);

    for (const auto &iter : formats) {
        const VSFormat *f = iter.second;

        if (f->colorFamily == colorFamily && f->sampleType == sampleType
                && f->subSamplingW == subSamplingW && f->subSamplingH == subSamplingH && f->bitsPerSample == bitsPerSample)
            return f;
    }

    VSFormat *f = new VSFormat();

    if (name) {
        strcpy(f->name, name);
    } else {
        strcpy(f->name, "Runtime Registered");
    }

    if (id != pfNone)
        f->id = id;
    else
        f->id = colorFamily + formatIdOffset++;

    f->colorFamily = colorFamily;
    f->sampleType = sampleType;
    f->bitsPerSample = bitsPerSample;
    f->bytesPerSample = 1;

    while (f->bytesPerSample * 8 < bitsPerSample)
        f->bytesPerSample *= 2;

    f->subSamplingW = subSamplingW;
    f->subSamplingH = subSamplingH;
    f->numPlanes = (colorFamily == cmGray || colorFamily == cmCompat) ? 1 : 3;

    formats.insert(std::pair<int, VSFormat *>(f->id, f));
    return f;
}

bool VSCore::isValidFormatPointer(const VSFormat *f) {
    std::lock_guard<std::mutex> lock(formatLock);

    for (const auto &iter : formats) {
        if (iter.second == f)
            return true;
    }
    return false;
}

const VSCoreInfo &VSCore::getCoreInfo() {
    coreInfo.versionString = VAPOURSYNTH_VERSION_STRING;
    coreInfo.core = VAPOURSYNTH_CORE_VERSION;
    coreInfo.api = VAPOURSYNTH_API_VERSION;
    coreInfo.numThreads = threadPool->threadCount();
    coreInfo.maxFramebufferSize = memory->getLimit();
    coreInfo.usedFramebufferSize = memory->memoryUse();
    return coreInfo;
}

void VS_CC configPlugin(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readOnly, VSPlugin *plugin);
void VS_CC registerFunction(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);

static void VS_CC loadPlugin(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    try {
        int err;
        core->loadPlugin(vsapi->propGetData(in, "path", 0, 0), vsapi->propGetData(in, "forcens", 0, &err));
    } catch (VSException &e) {
        vsapi->setError(out, e.what());
    }
}

void VS_CC loadPluginInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    registerFunc("LoadPlugin", "path:data;forcens:data:opt;", &loadPlugin, NULL, plugin);
}

// fixme, not the most elegant way but avoids the mess that would happen if avscompat.h was included
#if defined(VS_TARGET_OS_WINDOWS) && defined(VS_FEATURE_AVISYNTH)
extern "C" void VS_CC avsWrapperInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
#endif

void VSCore::registerFormats() {
    // Register known formats with informational names
    registerFormat(cmGray, stInteger,  8, 0, 0, "Gray8", pfGray8);
    registerFormat(cmGray, stInteger, 16, 0, 0, "Gray16", pfGray16);

    registerFormat(cmGray, stFloat,   16, 0, 0, "GrayH", pfGrayH);
    registerFormat(cmGray, stFloat,   32, 0, 0, "GrayS", pfGrayS);

    registerFormat(cmYUV,  stInteger, 8, 1, 1, "YUV420P8", pfYUV420P8);
    registerFormat(cmYUV,  stInteger, 8, 1, 0, "YUV422P8", pfYUV422P8);
    registerFormat(cmYUV,  stInteger, 8, 0, 0, "YUV444P8", pfYUV444P8);
    registerFormat(cmYUV,  stInteger, 8, 2, 2, "YUV410P8", pfYUV410P8);
    registerFormat(cmYUV,  stInteger, 8, 2, 0, "YUV411P8", pfYUV411P8);
    registerFormat(cmYUV,  stInteger, 8, 0, 1, "YUV440P8", pfYUV440P8);

    registerFormat(cmYUV,  stInteger, 9, 1, 1, "YUV420P9", pfYUV420P9);
    registerFormat(cmYUV,  stInteger, 9, 1, 0, "YUV422P9", pfYUV422P9);
    registerFormat(cmYUV,  stInteger, 9, 0, 0, "YUV444P9", pfYUV444P9);

    registerFormat(cmYUV,  stInteger, 10, 1, 1, "YUV420P10", pfYUV420P10);
    registerFormat(cmYUV,  stInteger, 10, 1, 0, "YUV422P10", pfYUV422P10);
    registerFormat(cmYUV,  stInteger, 10, 0, 0, "YUV444P10", pfYUV444P10);

    registerFormat(cmYUV,  stInteger, 16, 1, 1, "YUV420P16", pfYUV420P16);
    registerFormat(cmYUV,  stInteger, 16, 1, 0, "YUV422P16", pfYUV422P16);
    registerFormat(cmYUV,  stInteger, 16, 0, 0, "YUV444P16", pfYUV444P16);

    registerFormat(cmYUV,  stFloat,   16, 0, 0, "YUV444PH", pfYUV444PH);
    registerFormat(cmYUV,  stFloat,   32, 0, 0, "YUV444PS", pfYUV444PS);

    registerFormat(cmRGB,  stInteger, 8, 0, 0, "RGB24", pfRGB24);
    registerFormat(cmRGB,  stInteger, 9, 0, 0, "RGB27", pfRGB27);
    registerFormat(cmRGB,  stInteger, 10, 0, 0, "RGB30", pfRGB30);
    registerFormat(cmRGB,  stInteger, 16, 0, 0, "RGB48", pfRGB48);

    registerFormat(cmRGB,  stFloat,   16, 0, 0, "RGBH", pfRGBH);
    registerFormat(cmRGB,  stFloat,   32, 0, 0, "RGBS", pfRGBS);

    registerFormat(cmCompat,  stInteger, 32, 0, 0, "CompatBGR32", pfCompatBGR32);
    registerFormat(cmCompat,  stInteger, 16, 1, 0, "CompatYUY2", pfCompatYUY2);
}

bool VSCore::loadAllPluginsInPath(const QString &path, const QString &filter) {
    if (path.isEmpty())
        return false;

    QDir dir(path, filter, QDir::Name | QDir::IgnoreCase, QDir::Files | QDir::NoDotDot);
    if (!dir.exists())
        return false;

    QDirIterator dirIterator(dir);
    while (dirIterator.hasNext()) {
        try {
            loadPlugin(dirIterator.next().toUtf8().constData());
        } catch (VSException &) {
            // Ignore any errors
        }
    }

    return true;
}

VSCore::VSCore(int threads) : memory(new MemoryUse()), formatIdOffset(1000) {

#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected creating new core");
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after creating new core. Any other FPU state warnings after this one should be ignored.");
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after creating new core");
#endif

    threadPool = new VSThreadPool(this, threads);

    registerFormats();

    // The internal plugin units, the loading is a bit special so they can get special flags
    VSPlugin *p;

    // Initialize internal plugins
#if defined(VS_TARGET_OS_WINDOWS) && defined(VS_FEATURE_AVISYNTH)
    p = new VSPlugin(this);
    avsWrapperInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
    p->enableCompat();
#endif

    p = new VSPlugin(this);
    configPlugin("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 0, p);
    loadPluginInitialize(::configPlugin, ::registerFunction, p);
    cacheInitialize(::configPlugin, ::registerFunction, p);
    exprInitialize(::configPlugin, ::registerFunction, p);
    lutInitialize(::configPlugin, ::registerFunction, p);
    mergeInitialize(::configPlugin, ::registerFunction, p);
    reorderInitialize(::configPlugin, ::registerFunction, p);
    stdlibInitialize(::configPlugin, ::registerFunction, p);
    p->enableCompat();
    p->lock();

    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
    p = new VSPlugin(this);
    resizeInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
    p->enableCompat();

    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
    p = new VSPlugin(this);
    textInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
    p->enableCompat();

#ifdef VS_TARGET_OS_WINDOWS
    QString filter = "*.dll";
    // Autoload user specific plugins first so a user can always override
    std::vector<wchar_t> appDataBuffer(MAX_PATH + 1);
    SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, &appDataBuffer[0]);

    QString appDataPath = QString::fromUtf16(&appDataBuffer[0]) + "\\VapourSynth\\plugins";
    if (!loadAllPluginsInPath(appDataPath, filter))
        vsWarning("User specific plugin autoloading failed. Directory '%s' doesn't exist?", appDataPath.toUtf8().constData());

    // Autoload bundled plugins
    QSettings settings("HKEY_LOCAL_MACHINE\\Software\\VapourSynth", QSettings::NativeFormat);
    QString corePluginPath = settings.value("CorePlugins").toString();
    if (!loadAllPluginsInPath(corePluginPath, filter))
        vsWarning("Core plugin autoloading failed. Directory '%s' doesn't exist?", corePluginPath.toUtf8().constData());

    // Autoload global plugins last, this is so the bundled plugins cannot be overridden easily
    // and accidentally block updated bundled versions
    QString globalPluginPath = settings.value("Plugins").toString();
    if (!loadAllPluginsInPath(globalPluginPath, filter))
        vsWarning("Global plugin autoloading failed. Directory '%s' doesn't exist?", globalPluginPath.toUtf8().constData());
#elif defined(VS_TARGET_OS_LINUX) || defined(VS_TARGET_OS_DARWIN)

#ifdef VS_TARGET_OS_DARWIN
    QString filter = "*.dylib";
#else
    QString filter = "*.so";
#endif

    // Will read "~/.config/vapoursynth/vapoursynth.conf"
    // or "/etc/xdg/vapoursynth/vapoursynth.conf".
    QSettings settings("vapoursynth", "vapoursynth");

    bool autoloadUserPluginDir = settings.value("AutoloadUserPluginDir", true).toBool();
    QString userPluginDir = settings.value("UserPluginDir").toString();
    if (autoloadUserPluginDir && !userPluginDir.isEmpty()) {
        if (!loadAllPluginsInPath(userPluginDir, filter)) {
            vsWarning("Autoloading the user plugin dir '%s' failed. Directory doesn't exist?", userPluginDir.toUtf8().constData());
        }
    }

    bool autoloadSystemPluginDir = settings.value("AutoloadSystemPluginDir", true).toBool();
    QString systemPluginDir = settings.value("SystemPluginDir", QString(VS_PATH_PLUGINDIR)).toString();
    if (autoloadSystemPluginDir) {
        if (!loadAllPluginsInPath(systemPluginDir, filter)) {
            vsWarning("Autoloading the system plugin dir '%s' failed. Directory doesn't exist?", systemPluginDir.toUtf8().constData());
        }
    }
#endif
}

VSCore::~VSCore() {
    memory->signalFree();
    delete threadPool;
    for(const auto &iter : plugins)
        delete iter.second;
    plugins.clear();
    for (const auto &iter : formats)
        delete iter.second;
    formats.clear();
}

VSMap VSCore::getPlugins() {
    VSMap m;
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    for (const auto &iter : plugins) {
        std::string b = iter.second->fnamespace + ";" + iter.second->identifier + ";" + iter.second->fullname;
        vsapi.propSetData(&m, iter.second->identifier.c_str(), b.c_str(), b.size(), 0);
    }
    return m;
}

VSPlugin *VSCore::getPluginById(const std::string &identifier) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    try {
        return plugins.at(identifier);
    } catch (std::out_of_range &) {
    }
    return NULL;
}

VSPlugin *VSCore::getPluginByNs(const std::string &ns) {
    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    for (const auto &iter : plugins) {
        if (iter.second->fnamespace == ns)
            return iter.second;
    }
    return NULL;
}

void VSCore::loadPlugin(const std::string &filename, const std::string &forcedNamespace) {
    VSPlugin *p = new VSPlugin(filename, forcedNamespace, this);

    std::lock_guard<std::recursive_mutex> lock(pluginLock);
    if (getPluginById(p->identifier)) {
        std::string error = "Plugin " + filename + " already loaded (" + p->identifier + ")";
        delete p;
        throw VSException(error);
    }

    if (getPluginByNs(p->fnamespace)) {
        std::string error = "Plugin load failed, namespace " + p->fnamespace + " already populated (" + filename + ")";
        delete p;
        throw VSException(error);
    }

    plugins.insert(std::pair<std::string, VSPlugin *>(p->identifier, p));
}

void VSCore::createFilter(const VSMap *in, VSMap *out, const std::string &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion) {
    try {
        PVideoNode node(std::make_shared<VSNode>(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiVersion, this));
        for (int i = 0; i < node->getNumOutputs(); i++) {
            // fixme, not that elegant but saves more variant poking code
            VSNodeRef *ref = new VSNodeRef(node, i);
            vsapi.propSetNode(out, "clip", ref, paAppend);
            delete ref;
        }
    } catch (VSException &e) {
        vsapi.setError(out, e.what());
    }
}

int64_t VSCore::setMaxCacheSize(int64_t bytes) {
    return memory->setMaxMemoryUse(bytes);
}

VSPlugin::VSPlugin(VSCore *core)
    : apiVersion(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), core(core) {
}

VSPlugin::VSPlugin(const std::string &filename, const std::string &forcedNamespace, VSCore *core)
    : apiVersion(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), filename(filename), core(core), fnamespace(forcedNamespace) {
#ifdef VS_TARGET_OS_WINDOWS
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conversion;
    std::wstring  wPath = conversion.from_bytes(filename);
    
    libHandle = LoadLibrary(wPath.c_str());

    if (!libHandle)
        throw VSException("Failed to load " + filename);

    VSInitPlugin pluginInit = (VSInitPlugin)GetProcAddress(libHandle, "VapourSynthPluginInit");

    if (!pluginInit)
        pluginInit = (VSInitPlugin)GetProcAddress(libHandle, "_VapourSynthPluginInit@12");

    if (!pluginInit) {
        FreeLibrary(libHandle);
        throw VSException("No entry point found in " + filename);
    }
#else
    libHandle = dlopen(filename.constData(), RTLD_LAZY);

    if (!libHandle) {
        const char *dlError = dlerror();
        if (dlError)
            throw VSException("Failed to load " + filename + ". Error given: " + std::string(dlError));
        else
            throw VSException("Failed to load " + filename);
    }

    VSInitPlugin pluginInit = (VSInitPlugin)dlsym(libHandle, "VapourSynthPluginInit");

    if (!pluginInit) {
        dlclose(libHandle);
        throw VSException("No entry point found in " + filename);
    }
#endif
    pluginInit(&::configPlugin, &::registerFunction, this);

// This stuff really only works properly on windows, feel free to investigate what the linux ABI thinks about it
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isMMXStateOk())
        vsFatal("Bad MMX state detected after loading %s", fullname.c_str());
    if (!vs_isFPUStateOk())
        vsWarning("Bad FPU state detected after loading %s", fullname.c_str());
    if (!vs_isSSEStateOk())
        vsFatal("Bad SSE state detected after loading %s", fullname.c_str());
#endif

    if (readOnlySet)
        readOnly = true;

    if (apiVersion != VAPOURSYNTH_API_VERSION) {
#ifdef VS_TARGET_OS_WINDOWS
        FreeLibrary(libHandle);
#else
        dlclose(libHandle);
#endif
        throw VSException((QString("Core only supports API R") + QString::number(VAPOURSYNTH_API_VERSION) + QString(" but the loaded plugin uses API R") + QString::number(apiVersion)).toUtf8());
    }
}

VSPlugin::~VSPlugin() {
#ifdef VS_TARGET_OS_WINDOWS
    if (libHandle)
        FreeLibrary(libHandle);
#else
    if (libHandle)
        dlclose(libHandle);
#endif
}

void VSPlugin::configPlugin(const std::string &identifier, const std::string &defaultNamespace, const std::string &fullname, int apiVersion, bool readOnly) {
    if (hasConfig)
        vsFatal("Attempted to configure plugin %s twice", identifier.c_str());

    this->identifier = identifier;

    if (!this->fnamespace.size())
        this->fnamespace = defaultNamespace;

    this->fullname = fullname;
    this->apiVersion = apiVersion;
    readOnlySet = readOnly;
    hasConfig = true;
}

void VSPlugin::registerFunction(const std::string &name, const std::string &args, VSPublicFunction argsFunc, void *functionData) {
    if (readOnly)
        vsFatal("Tried to modify read only namespace");

    if (!isValidIdentifier(name))
        vsFatal("Illegal identifier specified for function");


    if (funcs.count(name))
        vsFatal("Duplicate function registered");        

    funcs.insert(std::pair<std::string, VSFunction>(name, VSFunction(args, argsFunc, functionData)));
}

static bool hasCompatNodes(const VSMap &m) {
    foreach(const VSVariant & vsv, m) {
        if (vsv.getType() == VSVariant::vNode) {
            for (int i = 0; i < vsv.size(); i++) {
                for (int j = 0; j < vsv.getValue<VSNodeRef>(i).clip->getNumOutputs(); j++) {
                    const VSVideoInfo &vi = vsv.getValue<VSNodeRef>(i).clip->getVideoInfo(j);
                    if (vi.format && vi.format->colorFamily == cmCompat)
                        return true;
                }
            }
        }
    }
    return false;
}

VSMap VSPlugin::invoke(const std::string &funcName, const VSMap &args) {
    const char lookup[] = { 'i', 'f', 's', 'c', 'v', 'm' };
    VSMap v;

    try {
        if (funcs.count(funcName)) {
            const VSFunction &f = funcs[funcName];
            if (!compat && hasCompatNodes(args))
                throw VSException(funcName + ": only special filters may accept compat input");

            std::set<std::string> remainingArgs;
            QList<QByteArray> keys = args.keys();
            for (const auto &key : keys)
                remainingArgs.insert(key.constData());

            for (const FilterArgument &fa : f.args) {
                char c = vsapi.propGetType(&args, fa.name.c_str());

                if (c != 'u') {
                    remainingArgs.erase(fa.name);

                    if (lookup[(int)fa.type] != c)
                        throw VSException(funcName + ": argument " + fa.name + " is not of the correct type");

                    if (!fa.arr && args[fa.name.c_str()].size() > 1)
                        throw VSException(funcName + ": argument " + fa.name + " is not of array type but more than one value was supplied");

                    if (!fa.empty && args[fa.name.c_str()].size() < 1)
                        throw VSException(funcName + ": argument " + fa.name + " does not accept empty arrays");

                } else if (!fa.opt) {
                    throw VSException(funcName + ": argument " + fa.name + " is required");
                }
            }

            if (!remainingArgs.empty()) {
                auto iter = remainingArgs.cbegin();
                std::string s = *iter;
                ++iter;
                for (; iter != remainingArgs.cend(); ++iter)
                    s += ", " + *iter;
                throw VSException(funcName + ": no argument(s) named " + s);
            }

            f.func(&args, &v, f.functionData, core, getVSAPIInternal(apiVersion));

            if (!compat && hasCompatNodes(v))
                vsFatal("%s: illegal filter node returning a compat format detected, DO NOT USE THE COMPAT FORMATS IN NEW FILTERS", funcName.c_str());

            return v;
        }
    } catch (VSException &e) {
        vsapi.setError(&v, e.what());
        return v;
    }

    vsapi.setError(&v, ("Function '" + funcName + "' not found in " + fullname).c_str());
    return v;
}

VSMap VSPlugin::getFunctions() {
    VSMap m;
    for (const auto & f : funcs) {
        std::string b = f.first + ";" + f.second.argString;
        vsapi.propSetData(&m, f.first.c_str(), b.c_str(), b.size(), 0);
    }
    return m;
}

