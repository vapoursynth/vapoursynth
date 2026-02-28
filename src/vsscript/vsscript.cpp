/*
* Copyright (c) 2013-2025 Fredrik Mellbin
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
#include "VSScript4.h"
#include "vsscript_internal.h"
#include "cython/vapoursynth_api.h"
#include <mutex>
#include <atomic>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string.h>

#ifdef VS_TARGET_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

static std::once_flag flag;

static std::mutex vsscriptlock;
static std::atomic<int> scriptID(1000);
static bool initialized = false;
static PyThreadState *ts = nullptr;
static PyGILState_STATE s;

#ifdef VS_TARGET_OS_WINDOWS
#define MODULE_HANDLE_TYPE HMODULE
#define LOAD_LIBRARY(x) LoadLibraryExW(x,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH)
#define GET_FUNCTION_ADDRESS(x,y) GetProcAddress(x,y)
#define FREE_LIBRARY(x) FreeLibrary(x)
#else
#define MODULE_HANDLE_TYPE void*
#define LOAD_LIBRARY(x) dlopen(x, RTLD_LAZY)
#define GET_FUNCTION_ADDRESS(x, y) dlsym(x, y)
#define FREE_LIBRARY(x) dlclose(x)
#endif

static std::filesystem::path getLibraryPath() {
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE module;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&getLibraryPath), &module);
    std::vector<wchar_t> pathBuf(65536);
    GetModuleFileNameW(module, pathBuf.data(), (DWORD)pathBuf.size());
    return pathBuf.data();
#else
    Dl_info info = {};
    if (dladdr(&vs_internal_vsapi, &info))
        return info.dli_fname;
#endif
    return {};
}

static std::filesystem::path readEnvConfig(const std::filesystem::path &path) {
    std::ifstream configFile(path);
    if (!configFile.is_open())
        return {};
    std::string line;
    while (std::getline(configFile, line)) {
        if (line.substr(0, 10) == "executable") {
            auto pos = line.find(" = ");
            if (pos != std::string::npos)
                return std::filesystem::u8path(line.substr(pos + 3));
        }
    }
    return {};
}

static std::string extendedErrorMessage;

static void real_init(void) VS_NOEXCEPT {

    extendedErrorMessage.clear();

    MODULE_HANDLE_TYPE libraryHandle = nullptr;

    const char *venvRoot = getenv("VIRTUAL_ENV");
    std::filesystem::path pythonPath;

    if (venvRoot) {
        std::filesystem::path configPath = std::filesystem::u8path(venvRoot);
        configPath /= "pyvenv.cfg";
        pythonPath = readEnvConfig(configPath);
    } else {
        std::filesystem::path configPath = getLibraryPath();
        configPath.replace_filename("pyenv.cfg");
        pythonPath = readEnvConfig(configPath);
    }

    if (!pythonPath.empty()) {
#ifdef VS_TARGET_OS_WINDOWS
        pythonPath.replace_filename("python3.dll");
#endif
        libraryHandle = LOAD_LIBRARY(pythonPath.c_str());
    } else {
        if (venvRoot)
            extendedErrorMessage = "Python executable path couldn't be determined from pyvenv.cfg";
        else
            extendedErrorMessage = "Python executable path couldn't be determined from the global config file. Run `python -m vapoursynth vsscript-config` to set it for this Python installation and then try again.";
        return;
    }
    if (!libraryHandle) {
        extendedErrorMessage = "Python library failed to load from " + pythonPath.u8string();
        return;
    }

    p_Py_DecRef = reinterpret_cast<decltype(_Py_DecRef) *>(GET_FUNCTION_ADDRESS(libraryHandle, "_Py_DecRef"));
    p_PyObject_GetAttrString = reinterpret_cast<decltype(PyObject_GetAttrString) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyObject_GetAttrString"));
    p_PyDict_GetItemString = reinterpret_cast<decltype(PyDict_GetItemString) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyDict_GetItemString"));
    p_PyCapsule_IsValid = reinterpret_cast<decltype(PyCapsule_IsValid) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyCapsule_IsValid"));
    p_PyCapsule_GetPointer = reinterpret_cast<decltype(PyCapsule_GetPointer) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyCapsule_GetPointer"));
    p_PyImport_ImportModule = reinterpret_cast<decltype(PyImport_ImportModule) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyImport_ImportModule"));
    p_Py_IsInitialized = reinterpret_cast<decltype(Py_IsInitialized) *>(GET_FUNCTION_ADDRESS(libraryHandle, "Py_IsInitialized"));
    p_Py_InitializeEx = reinterpret_cast<decltype(Py_InitializeEx) *>(GET_FUNCTION_ADDRESS(libraryHandle, "Py_InitializeEx"));
    p_PyGILState_Ensure = reinterpret_cast<decltype(PyGILState_Ensure) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyGILState_Ensure"));
    p_PyEval_SaveThread = reinterpret_cast<decltype(PyEval_SaveThread) *>(GET_FUNCTION_ADDRESS(libraryHandle, "PyEval_SaveThread"));

    if (!p_Py_DecRef || !p_PyObject_GetAttrString || !p_PyDict_GetItemString || !p_PyCapsule_IsValid || !p_PyCapsule_GetPointer || !p_PyImport_ImportModule || !p_Py_IsInitialized || !p_Py_InitializeEx || !p_PyGILState_Ensure || !p_PyEval_SaveThread) {
        FREE_LIBRARY(libraryHandle);
        extendedErrorMessage = "Failed to load required Python API functions from the library.";
        return;
    }

    // FIXME, unload library here as well?
    int preInitialized = p_Py_IsInitialized();
    if (!preInitialized)
        p_Py_InitializeEx(0);
    s = p_PyGILState_Ensure();
    if (import_vapoursynth()) {
        extendedErrorMessage = "Failed to import the VapourSynth Python module.";
        return;
    }
    if (vpy4_initVSScript()) {
        extendedErrorMessage = "Failed to initialize the VapourSynth Python module for VSScript use.";
        return;
    }
    ts = p_PyEval_SaveThread();
    initialized = true;
}

static int VS_CC getAPIVersion(void) VS_NOEXCEPT {
    return VSSCRIPT_API_VERSION;
}

static VSScript *VS_CC createScript(VSCore *core) VS_NOEXCEPT {
    VSScript *handle = new VSScript();
    handle->core = core;
    handle->id = ++scriptID;
    if (vpy4_createScript(handle)) {
        const VSAPI *vsapi = vpy4_getVSAPI(VAPOURSYNTH_API_VERSION);
        vsapi->freeCore(core);
        delete handle;
        return nullptr;
    } else {
        return handle;
    }
}

static int VS_CC evaluateBuffer(VSScript *handle, const char *buffer, const char *scriptFilename) VS_NOEXCEPT {
    assert(handle);
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_evaluateBuffer(handle, buffer, scriptFilename);
}

static int VS_CC evaluateFile(VSScript *handle, const char *scriptFilename) VS_NOEXCEPT {
    assert(handle);
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_evaluateFile(handle, scriptFilename);
}

static void VS_CC freeScript(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle) {
        vpy4_freeScript(handle);
        delete handle;
    }
}

static const char *VS_CC getError(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle)
        return vpy4_getError(handle);
    else
        return "Invalid handle (NULL)";
}

static int VS_CC getExitCode(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle)
        return handle->exitCode;
    else
        return 0;
}

static const VSAPI *VS_CC getVSApi(int version) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getVSAPI(version);
}

static VSNode *VS_CC getOutputNode(VSScript *handle, int index) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getOutput(handle, index);
}

static VSNode *VS_CC getOutputAlphaNode(VSScript *handle, int index) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getAlphaOutput(handle, index);
}

static int VS_CC getAltOutputMode(VSScript *handle, int index) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getAltOutputMode(handle, index);
}

static VSCore *VS_CC getCore(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getCore(handle);
}

static int VS_CC getVariable(VSScript *handle, const char *name, VSMap *dst) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getVariable(handle, name, dst);
}

static int VS_CC setVariable(VSScript *handle, const VSMap *vars) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_setVariables(handle, vars);
}

static void VS_CC evalSetWorkingDir(VSScript *handle, int setCWD) VS_NOEXCEPT {
    handle->setCWD = setCWD;
}

static int VS_CC getAvailableOutputNodes(VSScript *handle, int size, int *dst) VS_NOEXCEPT {
    assert(size <= 0 || dst);
    std::lock_guard<std::mutex> lock(vsscriptlock);
    int count = vpy4_getAvailableOutputNodes(handle, size, dst);
    std::sort(dst, dst + std::min(size, count));
    return count;
}

static VSSCRIPTAPI vsscript_api = {
    &getAPIVersion,
    &getVSApi,
    &createScript,
    &getCore,
    &evaluateBuffer,
    &evaluateFile,
    &getError,
    &getExitCode,
    &getVariable,
    &setVariable,
    &getOutputNode,
    &getOutputAlphaNode,
    &getAltOutputMode,
    &freeScript,
    &evalSetWorkingDir,
    &getAvailableOutputNodes
};

const VSSCRIPTAPI *VS_CC getVSScriptAPI2(int version, char *errMsg, int errSize) VS_NOEXCEPT {
    if (errSize < 0 || (!errMsg && errSize > 0))
        return nullptr;

    memset(errMsg, 0, errSize);
    int apiMajor = (version >> 16);
    int apiMinor = (version & 0xFFFF);

    if (apiMajor == VSSCRIPT_API_MAJOR && apiMinor <= VSSCRIPT_API_MINOR) {
        std::call_once(flag, real_init);
        if (initialized) {
            return &vsscript_api;
        } else {
            if (errMsg) {
                strncpy(errMsg, extendedErrorMessage.c_str(), errSize);
                errMsg[errSize - 1] = 0;
            }
        }
    } else {
        if (errMsg) {
            strncpy(errMsg, "Unsupported API version requested", errSize);
            errMsg[errSize - 1] = 0;
        }
    }
    return nullptr;
}

const VSSCRIPTAPI *VS_CC getVSScriptAPI(int version) VS_NOEXCEPT {
    return getVSScriptAPI2(version, nullptr, 0);
}