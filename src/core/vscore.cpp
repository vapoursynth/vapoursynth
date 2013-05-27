/*
* Copyright (c) 2012 Fredrik Mellbin
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
#include "x86utils.h"
#include "version.h"

// Filter headers
extern "C" {
#include "simplefilters.h"
#include "vsresize.h"
}
#include "cachefilter.h"
#include "exprfilter.h"

const VSAPI *VS_CC getVapourSynthAPI(int version);

static const QRegExp idRegExp("^[a-zA-Z][a-zA-Z0-9_]*$");
static const QRegExp sysPropRegExp("^_[a-zA-Z0-9_]*$");
static QMutex regExpLock;

static bool isValidIdentifier(const QByteArray &s) {
    QMutexLocker l(&regExpLock);
    return idRegExp.exactMatch(QString::fromUtf8(s.constData(), s.size()));
}

FrameContext::FrameContext(int n, int index, VSNode *clip, const PFrameContext &upstreamContext) : numFrameRequests(0), index(index), n(n), node(NULL), clip(clip), upstreamContext(upstreamContext), userData(NULL), frameContext(NULL), frameDone(NULL), error(false), lastCompletedN(-1), lastCompletedNode(NULL) {
}

FrameContext::FrameContext(int n, int index, VSNodeRef *node, VSFrameDoneCallback frameDone, void *userData) : numFrameRequests(0), index(index), n(n), node(node), clip(node->clip.data()), frameContext(NULL), userData(userData), frameDone(frameDone), error(false), lastCompletedN(-1), lastCompletedNode(NULL) {
}

void FrameContext::setError(const QByteArray &errorMsg) {
    error = true;
    errorMessage = errorMsg;
}

///////////////

VSFrameData::VSFrameData(quint32 size, MemoryUse *mem) : QSharedData(), mem(mem), size(size) {
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
    Q_CHECK_PTR(data);
    mem->add(size);
}

VSFrameData::VSFrameData(const VSFrameData &d) : QSharedData(d) {
    size = d.size;
    mem = d.mem;
    data = vs_aligned_malloc<uint8_t>(size, VSFrame::alignment);
    Q_CHECK_PTR(data);
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
        qFatal("Invalid new frame");

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
        qFatal("Invalid new frame");

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
                qFatal("Plane does no exist, error in frame creation");
            if (planeSrc[i]->getHeight(plane[i]) != getHeight(i) || planeSrc[i]->getWidth(plane[i]) != getWidth(i))
                qFatal("Copied plane dimensions do not match, error in frame creation");
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
    if (plane < 0 || plane >= format->numPlanes)
        qFatal("Invalid plane stride requested");
    return stride[plane];
}

const uint8_t *VSFrame::getReadPtr(int plane) const {
    if (plane < 0 || plane >= format->numPlanes)
        qFatal("Invalid plane requested");

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

uint8_t *VSFrame::getWritePtr(int plane) {
    if (plane < 0 || plane >= format->numPlanes)
        qFatal("Invalid plane requested");

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

VSFunction::VSFunction(const QByteArray &name, const QByteArray &argString, VSPublicFunction func, void *functionData)
    : name(name), argString(argString), functionData(functionData), func(func) {
    QString argQString = QString::fromUtf8(argString);
    QStringList argList = argQString.split(';', QString::SkipEmptyParts);
    foreach(QString arg, argList) {
        QStringList argParts = arg.split(':');

        if (argParts.count() < 2)
            qFatal("Invalid: %s", argString.constData());

        bool arr = false;
        enum FilterArgumentType type = faNone;
        QString typeName = argParts[1];

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
                qFatal("Invalid arg string: %s", typeName.toUtf8().constData());
        }

        bool link = false;
        bool opt = false;
        bool empty = false;

        for (int i = 2; i < argParts.count(); i++) {
            if (argParts[i] == "link")
                link = true;
            else if (argParts[i] == "opt")
                opt = true;
            else if (argParts[i] == "empty")
                empty = true;
        }

        if (!isValidIdentifier(argParts[0].toUtf8()))
            qFatal("Illegal argument identifier specified for function");

        if (empty && !arr)
            qFatal("Only array arguments can have the empty flag set");

        args.append(FilterArgument(argParts[0].toUtf8(), type, arr, empty, opt));
    }
}

VSNode::VSNode(const VSMap *in, VSMap *out, const QByteArray &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion, VSCore *core) :
    instanceData(instanceData), name(name), init(init), filterGetFrame(getFrame), free(free), filterMode(filterMode), apiVersion(apiVersion), core(core), flags(flags), inval(*in), hasVi(false), hasWarnedFPU(false) {
    QMutexLocker lock(&VSCore::filterLock);
    init(&inval, out, &this->instanceData, this, core, getVSAPIInternal(apiVersion));

    if (vsapi.getError(out))
        throw VSException(vsapi.getError(out));

    if (!hasVi)
        qFatal("Filter %s didn't set vi", name.constData());
}

VSNode::~VSNode() {
    QMutexLocker lock(&VSCore::filterLock);

    if (free)
        free(instanceData, core, &vsapi);
}

void VSNode::getFrame(const PFrameContext &ct) {
    core->threadPool->start(ct);
}

PVideoFrame VSNode::getFrameInternal(int n, int activationReason, const PFrameContext &frameCtx) {
    const VSFrameRef *r = filterGetFrame(n, activationReason, &instanceData, &frameCtx->frameContext, (VSFrameContext *)&frameCtx, core, &vsapi);
// This stuff really only works properly on windows, feel free to investigate what the linux ABI thinks about it
#ifdef _WIN32
    if (!vs_isMMXStateOk())
        qFatal("Bad MMX state detected after return from %s", name.constData());
    if (!hasWarnedFPU && !vs_isFPUStateOk()) {
        hasWarnedFPU = true;
        qWarning("Bad FPU state detected after return from %s", name.constData());
    }
    if (!vs_isSSEStateOk())
        qFatal("Bad SSE state detected after return from %s", name.constData());
#endif

    if (r) {
        PVideoFrame p(r->frame);
        delete r;
        const VSFormat *fi = p->getFormat();
        const VSVideoInfo &lvi = vi[frameCtx->index];

        if (!lvi.format && fi->colorFamily == cmCompat)
            qFatal("Illegal compat frame returned");
        else if (lvi.format && lvi.format != fi)
            qFatal("Frame returned not of the declared type");
        else if ((lvi.width || lvi.height) && (p->getWidth(0) != lvi.width || p->getHeight(0) != lvi.height))
            qFatal("Frame returned of not of the declared dimensions");

        return p;
    }

    PVideoFrame p;
    return p;
}


PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame *propSrc) {
    return PVideoFrame(new VSFrame(f, width, height, propSrc, this));
}

PVideoFrame VSCore::newVideoFrame(const VSFormat *f, int width, int height, const VSFrame * const *planeSrc, const int *planes, const VSFrame *propSrc) {
    return PVideoFrame(new VSFrame(f, width, height, planeSrc, planes, propSrc, this));
}

PVideoFrame VSCore::copyFrame(const PVideoFrame &srcf) {
    return PVideoFrame(new VSFrame(*srcf.data()));
}

void VSCore::copyFrameProps(const PVideoFrame &src, PVideoFrame &dst) {
    dst->setProperties(src->getProperties());
}

const VSFormat *VSCore::getFormatPreset(int id) {
    QMutexLocker lock(&formatLock);

    if (formats.contains(id))
        return formats[id];
    else
        return NULL;
}

const VSFormat *VSCore::registerFormat(VSColorFamily colorFamily, VSSampleType sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, const char *name, int id) {
    // this is to make exact format comparisons easy by simply allowing pointer comparison

    // block nonsense formats
    if (subSamplingH < 0 || subSamplingW < 0 || subSamplingH > 4 || subSamplingW > 4)
        qFatal("Nonsense format registration");

    if (sampleType < 0 || sampleType > 1)
        qFatal("Invalid sample type");

    if (colorFamily == cmRGB && (subSamplingH != 0 || subSamplingW != 0))
        qFatal("We do not like subsampled rgb around here");

    if (sampleType == stFloat && (bitsPerSample != 16 && bitsPerSample != 32))
        qFatal("Only floating point formats with 16 or 32 bit precision are allowed");

    if (bitsPerSample < 8 || bitsPerSample > 32)
        qFatal("Only formats with 8-32 bits per sample are allowed");

    if (colorFamily == cmCompat && !name)
        qFatal("No compatibility formats may be registered");

    QMutexLocker lock(&formatLock);

    for (QHash<int, VSFormat *>::const_iterator i = formats.constBegin(); i != formats.constEnd(); ++i) {
        const VSFormat *f = *i;

        if (f->colorFamily == colorFamily && f->sampleType == sampleType
                && f->subSamplingW == subSamplingW && f->subSamplingH == subSamplingH && f->bitsPerSample == bitsPerSample)
            return f;
    }

    VSFormat *f = new VSFormat();

    if (name) {
        strcpy(f->name, name);
    } else {
        strcpy(f->name, "runtime registered");
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

    formats.insertMulti(f->id, f);
    return f;
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
#ifdef _WIN32
extern "C" void VS_CC avsWrapperInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);
#endif

VSCore::VSCore(int threads) : memory(new MemoryUse()), formatIdOffset(1000) {
    threadPool = new VSThreadPool(this, threads);

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

    // The internal plugin units, the loading is a bit special so they can get special flags
    VSPlugin *p;

#ifdef _WIN32
    p = new VSPlugin(this);
    avsWrapperInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(p->identifier, p);
    p->enableCompat();
#endif

    p = new VSPlugin(this);
    configPlugin("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 0, p);
    loadPluginInitialize(::configPlugin, ::registerFunction, p);
    cacheInitialize(::configPlugin, ::registerFunction, p);
    stdlibInitialize(::configPlugin, ::registerFunction, p);
    exprInitialize(::configPlugin, ::registerFunction, p);
    p->enableCompat();
    p->lock();

    plugins.insert(p->identifier, p);
    p = new VSPlugin(this);
    resizeInitialize(::configPlugin, ::registerFunction, p);
    plugins.insert(p->identifier, p);
    p->enableCompat();
}

VSCore::~VSCore() {
    memory->signalFree();
    delete threadPool;
    foreach(VSPlugin * p, plugins)
        delete p;
}

QMutex VSCore::filterLock(QMutex::Recursive);

VSMap VSCore::getPlugins() {
    VSMap m;
    foreach(VSPlugin * p, plugins) {
        QByteArray b = p->fnamespace + ";" + p->identifier + ";" + p->fullname;
        vsapi.propSetData(&m, p->identifier.constData(), b.constData(), b.size(), 0);
    }
    return m;
}

VSPlugin *VSCore::getPluginId(const QByteArray &identifier) {
    return plugins.value(identifier);
}

VSPlugin *VSCore::getPluginNs(const QByteArray &ns) {
    foreach(VSPlugin * p, plugins)

    if (p->fnamespace == ns)
        return p;

    return NULL;
}

void VSCore::loadPlugin(const QByteArray &filename, const QByteArray &forcedNamespace)  {
    VSPlugin *p = new VSPlugin(filename, forcedNamespace, this);

    if (getPluginId(p->identifier)) {
        QByteArray error = "Plugin " + filename + " already loaded (" + p->identifier + ")";
        delete p;
        throw VSException(error);
    }

    if (getPluginNs(p->fnamespace)) {
        QByteArray error = "Plugin load failed, namespace " + p->fnamespace + " already populated (" + filename + ")";
        delete p;
        throw VSException(error);
    }

    plugins.insert(p->identifier, p);
}

void VSCore::createFilter(const VSMap *in, VSMap *out, const QByteArray &name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, VSFilterMode filterMode, int flags, void *instanceData, int apiVersion) {
    try {
        PVideoNode node(new VSNode(in, out, name, init, getFrame, free, filterMode, flags, instanceData, apiVersion, this));
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

VSPlugin::VSPlugin(const QByteArray &filename, const QByteArray &forcedNamespace, VSCore *core)
    : apiVersion(0), hasConfig(false), readOnly(false), compat(false), libHandle(0), filename(filename), core(core), fnamespace(forcedNamespace) {
#ifdef _WIN32
    QString uStr = QString::fromUtf8(filename.constData(), filename.size());
    QStdWString wStr(uStr.toStdWString());
    libHandle = LoadLibraryW(wStr.data());

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

    if (!libHandle)
        throw VSException("Failed to load " + filename);

    VSInitPlugin pluginInit = (VSInitPlugin)dlsym(libHandle, "VapourSynthPluginInit");

    if (!pluginInit) {
        dlclose(libHandle);
        throw VSException("No entry point found in " + filename);
    }
#endif
    pluginInit(&::configPlugin, &::registerFunction, this);

// This stuff really only works properly on windows, feel free to investigate what the linux ABI thinks about it
#ifdef _WIN32
    if (!vs_isMMXStateOk())
        qFatal("Bad MMX state detected after loading %s", fullname.constData());
    if (!vs_isFPUStateOk())
        qWarning("Bad FPU state detected after loading %s", fullname.constData());
    if (!vs_isSSEStateOk())
        qFatal("Bad SSE state detected after loading %s", fullname.constData());
#endif

    if (readOnlySet)
        readOnly = true;

    if (apiVersion != VAPOURSYNTH_API_VERSION && apiVersion != 2) {
#ifdef _WIN32
        FreeLibrary(libHandle);
#else
        dlclose(libHandle);
#endif
        throw VSException((QString("Core only supports API R") + QString::number(VAPOURSYNTH_API_VERSION) + QString(" and R2 but the loaded plugin uses API R") + QString::number(apiVersion)).toUtf8());
    }

    if (apiVersion == 2)
        qWarning("The plugin '%s' uses API R2 which is deprecated. Update to keep it working in future versions.", fullname.constData());
}

VSPlugin::~VSPlugin() {
#ifdef _WIN32
    if (libHandle)
        FreeLibrary(libHandle);
#else
    if (libHandle)
        dlclose(libHandle);
#endif
}

void VSPlugin::configPlugin(const QByteArray &identifier, const QByteArray &defaultNamespace, const QByteArray &fullname, int apiVersion, bool readOnly) {
    if (hasConfig)
        qFatal("Attempted to configure plugin %s twice", identifier.constData());

    this->identifier = identifier;

    if (!this->fnamespace.size())
        this->fnamespace = defaultNamespace;

    this->fullname = fullname;
    this->apiVersion = apiVersion;
    readOnlySet = readOnly;
    hasConfig = true;
}

void VSPlugin::registerFunction(const QByteArray &name, const QByteArray &args, VSPublicFunction argsFunc, void *functionData) {
    if (readOnly)
        qFatal("Tried to modify read only namespace");

    if (!isValidIdentifier(name))
        qFatal("Illegal identifier specified for function");

    foreach(const VSFunction & f, funcs)

    if (!qstricmp(name.constData(), f.name.constData()))
        qFatal("Duplicate function registered");

    funcs.append(VSFunction(name, args, argsFunc, functionData));
}

static bool hasCompatNodes(const VSMap &m) {
    foreach(const VSVariant & vsv, m) {
        if (vsv.vtype == VSVariant::vNode) {
            for (int i = 0; i < vsv.c.count(); i++) {
                for (int j = 0; j < vsv.c[i].clip->getNumOutputs(); j++) {
                    const VSVideoInfo &vi = vsv.c[i].clip->getVideoInfo(j);
                    if (vi.format && vi.format->colorFamily == cmCompat)
                        return true;
                }
            }
        }
    }
    return false;
}

VSMap VSPlugin::invoke(const QByteArray &funcName, const VSMap &args) {
    const char lookup[] = { 'i', 'f', 's', 'c', 'v', 'm' };
    VSMap v;

    try {
        foreach(const VSFunction & f, funcs)

        if (funcName == f.name) {
            if (!compat && hasCompatNodes(args))
                throw VSException(funcName + ": only special filters may accept compat input");

            VSMap argsCopy(args);

            for (int i = 0; i < f.args.count(); i++) {
                const FilterArgument &fa = f.args[i];
                char c = vsapi.propGetType(&args, fa.name);

                if (c != 'u') {
                    argsCopy.remove(fa.name);

                    if (lookup[(int)fa.type] != c)
                        throw VSException(funcName + ": argument " + fa.name + " is not of the correct type");

                    if (!fa.arr && args[fa.name].count() > 1)
                        throw VSException(funcName + ": argument " + fa.name + " is not of array type but more than one value was supplied");

                    if (!fa.empty && args[fa.name].count() < 1)
                        throw VSException(funcName + ": argument " + fa.name + " does not accept empty arrays");

                } else if (!fa.opt) {
                    throw VSException(funcName + ": argument " + fa.name + " is required");
                }
            }

            if (argsCopy.count()) {
                QList<QByteArray> bl = argsCopy.uniqueKeys();
                QStringList sl;

                for (int i = 0; i < bl.count(); i++)
                    sl.append(bl[i]);

                sl.sort();
                throw VSException(funcName + ": no argument named " + sl.join(", ").toUtf8());
            }

            f.func(&args, &v, f.functionData, core, getVSAPIInternal(apiVersion));

            if (!compat && hasCompatNodes(v))
                qFatal("%s: illegal filter node returning a compat format detected, DO NOT USE THE COMPAT FORMATS IN NEW FILTERS", funcName.constData());

            return v;
        }
    } catch (VSException &e) {
        vsapi.setError(&v, e.what());
        return v;
    }

    vsapi.setError(&v, "Function '" + funcName + "' not found in " + fullname);
    return v;
}

VSMap VSPlugin::getFunctions() {
    VSMap m;
    foreach(const VSFunction & f, funcs) {
        QByteArray b = f.name + ";" + f.argString;
        vsapi.propSetData(&m, f.name.constData(), b.constData(), b.size(), 0);
    }
    return m;
}

