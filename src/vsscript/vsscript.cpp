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
#include <cstring>
#include <cstdlib>

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
#define LOAD_LIBRARY(x) dlopen(x, RTLD_LAZY | RTLD_GLOBAL)
#define GET_FUNCTION_ADDRESS(x, y) dlsym(x, y)
#define FREE_LIBRARY(x) dlclose(x)
#endif

static std::filesystem::path getLibraryPath() {
#ifdef VS_TARGET_OS_WINDOWS
    HMODULE module;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&getLibraryPath), &module)) {
        std::vector<wchar_t> pathBuf(65536);
        GetModuleFileNameW(module, pathBuf.data(), (DWORD)pathBuf.size());
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, pathBuf.data(), -1, pathBuf.data(), static_cast<int>(pathBuf.size()), nullptr, nullptr, 0);
        return pathBuf.data();
    }
#else
    Dl_info info = {};
    if (dladdr(reinterpret_cast<void *>(&getLibraryPath), &info)) {
        std::string fname = info.dli_fname;
        auto pos = fname.find("/lib64/");
        if (pos != std::string::npos)
            fname.replace(pos, 7, "/lib/");
        return std::filesystem::path(fname).lexically_normal();
    }
#endif
    return {};
}

static std::string unescapeTOMLString(const std::string &input) {
    std::string output;
    output.reserve(input.size());
    bool escape = false;
    for (auto c : input) {
        escape = !escape && (c == '\\');
        if (!escape)
            output += c;
    }
    return output;
}

static std::pair<std::string, size_t> getTOMLString(const std::string &line, size_t offset = 0) {
    auto start = line.find('"', offset);
    if (start == std::string::npos)
        return {};
    auto end = line.find('"', start + 1);
    std::string s = line.substr(start + 1, end - start - 1);
    return std::make_pair(unescapeTOMLString(s), end + 1);
}

static std::pair<std::filesystem::path, std::filesystem::path> readEnvConfig(const std::filesystem::path &vsscriptPath) {
#ifdef VS_TARGET_OS_WINDOWS
    std::filesystem::path directPythonExePath = vsscriptPath.parent_path().parent_path().parent_path().parent_path();
    std::filesystem::path directPythonDLLPath = directPythonExePath / L"python3.dll";
    directPythonExePath /= L"python.exe";

    std::error_code ec;
    if (std::filesystem::exists(directPythonExePath, ec) && std::filesystem::exists(directPythonDLLPath, ec))
        return { directPythonExePath, directPythonDLLPath };

    std::filesystem::path configPath = _wgetenv(L"APPDATA");
#else
    std::filesystem::path configPath = std::getenv("HOME");
    configPath /= ".config";
#endif
    configPath /= "vapoursynth";
    configPath /= "vapoursynth.toml";

    std::ifstream configFile(configPath);
    if (!configFile.is_open())
        return {};
    std::string line;
    // Temp string here to ensure correct utf8 conversion
    std::string vsscriptStrPath = vsscriptPath.u8string();
    while (std::getline(configFile, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        
        auto s1 = getTOMLString(line);

        if (s1.first == vsscriptStrPath) {
            auto s2 = getTOMLString(line, s1.second);
            auto s3 = getTOMLString(line, s2.second);
            return { std::filesystem::u8path(s2.first), std::filesystem::u8path(s3.first) };
        }
    }
    return {};
}

static std::string extendedErrorMessage;

static void realInit() VS_NOEXCEPT {
    extendedErrorMessage.clear();

    MODULE_HANDLE_TYPE libraryHandle = nullptr;

    std::filesystem::path vsscriptPath = getLibraryPath();

    auto [pythonExePath, pythonSymbolPath] = readEnvConfig(vsscriptPath);

    if (pythonExePath.empty() || pythonSymbolPath.empty()) {
#ifdef VS_TARGET_OS_WINDOWS
        _wsystem(L"vapoursynth config >NUL 2>&1");
#else
        system("vapoursynth config >/dev/null 2>&1");
#endif
        std::tie(pythonExePath, pythonSymbolPath) = readEnvConfig(vsscriptPath);
    }

    if (pythonExePath.empty() || pythonSymbolPath.empty()) {
        extendedErrorMessage = "Python executable and library path couldn't be determined despite automatic configuration. Run `vapoursynth config` to set it for this Python installation and then try again.";
        return;
    }

    libraryHandle = LOAD_LIBRARY(pythonSymbolPath.c_str());
    if (!libraryHandle) {
        extendedErrorMessage = "Python library failed to load from " + pythonSymbolPath.u8string();
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
    p_Py_SetProgramName = reinterpret_cast<decltype(Py_SetProgramName) *>(GET_FUNCTION_ADDRESS(libraryHandle, "Py_SetProgramName"));

    if (!p_Py_DecRef || !p_PyObject_GetAttrString || !p_PyDict_GetItemString || !p_PyCapsule_IsValid || !p_PyCapsule_GetPointer || !p_PyImport_ImportModule
        || !p_Py_IsInitialized || !p_Py_InitializeEx || !p_PyGILState_Ensure || !p_PyEval_SaveThread || !p_Py_SetProgramName) {
        FREE_LIBRARY(libraryHandle);
        extendedErrorMessage = "Failed to load required Python API functions from " + pythonSymbolPath.u8string();
        return;
    }

    int preInitialized = p_Py_IsInitialized();
    if (!preInitialized) {
        p_Py_SetProgramName(pythonExePath.wstring().c_str());
        p_Py_InitializeEx(0);
    }
    s = p_PyGILState_Ensure();
    if (import_vapoursynth()) {
        FREE_LIBRARY(libraryHandle);
        extendedErrorMessage = "Failed to import the VapourSynth Python module.";
        return;
    }
    if (vpy4_initVSScript()) {
        FREE_LIBRARY(libraryHandle);
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

const VSSCRIPTAPI *VS_CC getVSScriptAPI(int version) VS_NOEXCEPT {
    int apiMajor = (version >> 16);
    int apiMinor = (version & 0xFFFF);

    if (apiMajor == VSSCRIPT_API_MAJOR && apiMinor <= VSSCRIPT_API_MINOR) {
        std::call_once(flag, realInit);
        if (initialized) {
            return &vsscript_api;
        }
    } 
    return nullptr;
}

const char *VS_CC getVSScriptAPILastError() VS_NOEXCEPT {
    return extendedErrorMessage.empty() ? nullptr : extendedErrorMessage.c_str();
}