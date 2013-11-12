#include "VapourSynth.h"
#include "VSScript.h"
#include "cython/vapoursynth_api.h"
#include <mutex>
#include <atomic>

std::once_flag flag;

struct VSScript : public VPYScriptExport {
};

std::atomic<int> initializationCount(0);
std::atomic<int> scriptId(1000);
bool initialized = false;
PyThreadState *ts = NULL;
PyGILState_STATE s;

static void real_init(void) {
    int preInitialized = Py_IsInitialized();
    if (!preInitialized)
        Py_InitializeEx(0);
    PyGILState_STATE s = PyGILState_Ensure();
    int result = import_vapoursynth();
    if (result)
        return;
    vpy_initVSScript();
    ts = PyEval_SaveThread();
    initialized = true;
}

VS_API(int) vsscript_init() {
    std::call_once(flag, real_init);
    if (initialized)
        return ++initializationCount;
    else
        return initializationCount;
}

VS_API(int) vsscript_finalize(void) {
    return --initializationCount;
}

VS_API(int) vsscript_evaluateScript(VSScript **handle, const char *script, const char *scriptFilename, int flags) {
    if (*handle == NULL) {
        *handle = new(std::nothrow)VSScript();
        (*handle)->pyenvdict = NULL;
        (*handle)->errstr = NULL;
        (*handle)->id = ++scriptId;
    }
    return vpy_evaluateScript(*handle, script, scriptFilename ? scriptFilename : "<string>", flags);
}

VS_API(int) vsscript_evaluateFile(VSScript **handle, const char *scriptFilename, int flags) {
    if (*handle == NULL) {
        *handle = new(std::nothrow)VSScript();
        (*handle)->pyenvdict = NULL;
        (*handle)->errstr = NULL;
        (*handle)->id = ++scriptId;
    }
    return vpy_evaluateFile(*handle, scriptFilename, flags);
}

VS_API(void) vsscript_freeScript(VSScript *handle) {
    if (handle) {
        vpy_freeScript(handle);
        delete handle;
    }
}

VS_API(const char *) vsscript_getError(VSScript *handle) {
    return vpy_getError(handle);
}

VS_API(VSNodeRef *) vsscript_getOutput(VSScript *handle, int index) {
    return vpy_getOutput(handle, index);
}

VS_API(void) vsscript_clearOutput(VSScript *handle, int index) {
    vpy_clearOutput(handle, index);
}

VS_API(VSCore *) vsscript_getCore(VSScript *handle) {
    return vpy_getCore(handle);
}

VS_API(const VSAPI *) vsscript_getVSApi(void) {
    return vpy_getVSApi();
}

VS_API(int) vsscript_getVariable(VSScript *handle, const char *name, VSMap *dst) {
    return vpy_getVariable(handle, name, dst);
}

VS_API(void) vsscript_setVariable(VSScript *handle, const VSMap *vars) {
    vpy_setVariable(handle, (VSMap *)vars);
}

VS_API(int) vsscript_clearVariable(VSScript *handle, const char *name) {
    return vpy_clearVariable(handle, name);
}

VS_API(void) vsscript_clearEnvironment(VSScript *handle) {
    vpy_clearEnvironment(handle);
}
