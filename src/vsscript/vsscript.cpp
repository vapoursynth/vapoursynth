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

#ifdef VS_TARGET_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

static std::once_flag flag;

static std::mutex vsscriptlock;
static std::atomic<int> initializationCount(0);
static std::atomic<int> scriptID(1000);
static bool initialized = false;
static PyThreadState *ts = nullptr;
static PyGILState_STATE s;

static void real_init(void) VS_NOEXCEPT {
#ifdef VS_TARGET_OS_WINDOWS
#ifdef _WIN64
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth"
#else
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth-32"
#endif

#ifdef VSSCRIPT_PYTHON38
    const std::wstring pythonDllName = L"python38.dll";
#else
    const std::wstring pythonDllName = L"python3.dll";
#endif

    // portable
    HMODULE module;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&real_init, &module);
    std::vector<wchar_t> pathBuf(65536);
    GetModuleFileNameW(module, pathBuf.data(), (DWORD)pathBuf.size());
    std::filesystem::path dllPath = pathBuf.data();
    dllPath = dllPath.parent_path();
    bool isPortable = std::filesystem::exists(dllPath / L"portable.vs");

    HMODULE pythonDll = nullptr;

    if (isPortable) {
        pythonDll = LoadLibraryExW((dllPath / pythonDllName).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    } else {
        DWORD dwType = REG_SZ;
        HKEY hKey = 0;

        wchar_t value[1024];
        DWORD valueLength = 1000;
        if (RegOpenKeyW(HKEY_CURRENT_USER, VS_INSTALL_REGKEY, &hKey) != ERROR_SUCCESS) {
            if (RegOpenKeyW(HKEY_LOCAL_MACHINE, VS_INSTALL_REGKEY, &hKey) != ERROR_SUCCESS)
                return;
        }

        LSTATUS status = RegQueryValueExW(hKey, L"PythonPath", nullptr, &dwType, (LPBYTE)&value, &valueLength);
        RegCloseKey(hKey);
        if (status != ERROR_SUCCESS)
            return;

        std::filesystem::path pyPath = value;
        pyPath /= pythonDllName;

        pythonDll = LoadLibraryExW(pyPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    }
    if (!pythonDll)
        return;
#endif
    int preInitialized = Py_IsInitialized();
    if (!preInitialized)
        Py_InitializeEx(0);
    s = PyGILState_Ensure();
    if (import_vapoursynth())
        return;
    if (vpy4_initVSScript())
        return;
    ts = PyEval_SaveThread();
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
        std::call_once(flag, real_init);
        if (initialized)
            return &vsscript_api;
    }
    return nullptr;
}