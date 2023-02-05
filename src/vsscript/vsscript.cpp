/*
* Copyright (c) 2013-2020 Fredrik Mellbin
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
#include <vector>

#ifdef VS_TARGET_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <algorithm>
#endif

static std::once_flag flag;

static std::mutex vsscriptlock;
static std::atomic<int> initializationCount(0);
static std::atomic<int> scriptID(1000);
static bool initialized = false;
static PyThreadState *ts = nullptr;
static PyGILState_STATE s;


static PyGILState_STATE initialize_gil() VS_NOEXCEPT {
    int preInitialized = Py_IsInitialized();
    if (!preInitialized)
        Py_InitializeEx(0);
    return PyGILState_Ensure();
}

static bool check_vapoursynth() {
    PyObject *module = 0;
    module = PyImport_ImportModule("vapoursynth");

    if (module) {
        Py_DECREF(module);
        module = 0;
        return true;
    }

    Py_XDECREF(module);

    return false;
}

#ifdef VS_TARGET_OS_WINDOWS

#ifdef _WIN64
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth"
#else
    #define VS_INSTALL_REGKEY L"Software\\VapourSynth-32"
#endif

#define PYTHON_CORE_REGKEY L"Software\\Python\\PythonCore\\"

const std::vector<std::wstring> pythonVersions {
    L"3.12", L"3.11", L"3.10", L"3.9", L"3.8",
};

static bool checkPythonInstall(const std::wstring pyPath, const std::wstring pythonVer) {
    HMODULE pythonDll = nullptr;

    std::wcerr << L"vsscript: " << pyPath << L"." << std::endl;

    std::wstring dllName = pythonVer;
    dllName.erase(std::remove(dllName.begin(), dllName.end(), L'.'), dllName.end());

    std::wstring path = pyPath + L"\\python" + dllName + L".dll";
    pythonDll = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (pythonDll) {
        initialize_gil();

        if (check_vapoursynth()) {
            std::wcerr << L"vsscript: found " << path << L"." << std::endl;
            return true;
        }

        FreeLibrary(pythonDll);
    }

    std::wcerr << L"vsscript: not found " << path << L"." << std::endl;

    return !!pythonDll;
}

static bool checkPythonInstallAllVersions(const std::wstring pyPath) {
    for (const auto &pyVer: pythonVersions) {
        if (checkPythonInstall(pyPath, pyVer)) {
            return true;
        }
    }

    return false;
}

static bool findPythonInstall(void) VS_NOEXCEPT {
    HMODULE module;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&findPythonInstall, &module);

    std::vector<wchar_t> pathBuf(65536);
    GetModuleFileNameW(module, pathBuf.data(), (DWORD)pathBuf.size());

    std::wstring dllPath = pathBuf.data();
    dllPath.resize(dllPath.find_last_of('\\') + 1);

    if (checkPythonInstallAllVersions(dllPath)) {
        return true;
    }

    HMODULE pythonDll = nullptr;
    DWORD dwType = REG_SZ;
    HKEY hKey = 0;
    wchar_t value[1024];
    DWORD valueLength = 1000;

    HKEY installType = 0;

    if (RegOpenKeyW(HKEY_CURRENT_USER, VS_INSTALL_REGKEY, &hKey) == ERROR_SUCCESS) {
        installType = HKEY_CURRENT_USER;
    } else if (RegOpenKeyW(HKEY_LOCAL_MACHINE, VS_INSTALL_REGKEY, &hKey) == ERROR_SUCCESS) {
        installType = HKEY_LOCAL_MACHINE;
    }

    if (!installType) {
        return false;
    }

    LSTATUS status;

    status = RegQueryValueExW(hKey, L"PythonPath", nullptr, &dwType, (LPBYTE)&value, &valueLength);

    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && checkPythonInstallAllVersions(value)) {
        return true;
    }

    hKey = 0;
    for (const auto &pyVer: pythonVersions) {
        std::wstring keyName = PYTHON_CORE_REGKEY + pyVer + L"\\InstallPath";

        std::wcerr << L"vsscript: " << keyName << "." << std::endl;

        if (RegOpenKeyW(installType, keyName.c_str(), &hKey) == ERROR_SUCCESS) {
            status = RegQueryValueExW(hKey, NULL, nullptr, &dwType, (LPBYTE)&value, &valueLength);
            RegCloseKey(hKey);

            if (status == ERROR_SUCCESS && checkPythonInstall(value, pyVer)) {
                return true;
            }
        }
    }

    return false;
}

#endif

static void real_init(void) VS_NOEXCEPT {
#ifdef VS_TARGET_OS_WINDOWS
    if (!findPythonInstall()) {
        std::wcerr << L"vsscript: unable to find a suitable python install." << std::endl;
        abort();
    }
#endif
    s = initialize_gil();
    if (import_vapoursynth())
        return;
    if (vpy4_initVSScript())
        return;
    ts = PyEval_SaveThread();
    initialized = true;
}

// V3 API compatibility
VS_API(int) vsscript_getApiVersion(void) VS_NOEXCEPT {
    return VS_MAKE_VERSION(3, 2);
}

static int VS_CC getAPIVersion(void) VS_NOEXCEPT {
    return VSSCRIPT_API_VERSION;
}

// V3 API compatibility
VS_API(int) vsscript_init() VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    std::call_once(flag, real_init);
    if (initialized)
        return ++initializationCount;
    else
        return initializationCount;
}

// V3 API compatibility
VS_API(int) vsscript_finalize(void) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    int count = --initializationCount;
    assert(count >= 0);
    return count;
}

// V3 API compatibility
static int createScriptInternal(VSScript **handle) VS_NOEXCEPT {
    *handle = new VSScript();
    (*handle)->id = ++scriptID;
    return vpy4_createScript(*handle);
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

// V3 API compatibility
VS_API(int) vsscript_createScript(VSScript **handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return createScriptInternal(handle);
}

// V3 API compatibility
VS_API(int) vsscript_evaluateScript(VSScript **handle, const char *script, const char *scriptFilename, int flags) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (*handle == nullptr) {
        if (createScriptInternal(handle)) return 1;
    }
    return vpy_evaluateScript(*handle, script, scriptFilename ? scriptFilename : "<undefined>", flags);
}

// V3 API compatibility
VS_API(int) vsscript_evaluateFile(VSScript **handle, const char *scriptFilename, int flags) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (*handle == nullptr) {
        if (createScriptInternal(handle)) return 1;
    }
    return vpy_evaluateFile(*handle, scriptFilename, flags);
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

VS_API(void) vsscript_freeScript(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle) {
        vpy4_freeScript(handle);
        delete handle;
    }
}

VS_API(const char *) vsscript_getError(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle)
        return vpy4_getError(handle);
    else
        return "Invalid handle (NULL)";
}

VS_API(int) vsscript_getExitCode(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    if (handle)
        return handle->exitCode;
    else
        return 0;
}

VS_API(const VSAPI *) vsscript_getVSApi2(int version) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getVSAPI(version);
}

// V3 API compatibility
VS_API(VSNode *) vsscript_getOutput2(VSScript *handle, int index, VSNode **alpha) VS_NOEXCEPT {
    if (alpha)
        *alpha = nullptr;
    std::lock_guard<std::mutex> lock(vsscriptlock);
    VSNode *node = vpy4_getOutput(handle, index);
    const VSAPI *vsapi = vpy4_getVSAPI(VAPOURSYNTH_API_VERSION);
    if (node && vsapi->getNodeType(node) == mtAudio) {
        vsapi->freeNode(node);
        return nullptr;
    } else if (node && alpha) {
        *alpha = vpy4_getAlphaOutput(handle, index);
    }
    return node;
}

// V3 API compatibility
VS_API(VSNode *) vsscript_getOutput(VSScript *handle, int index) VS_NOEXCEPT {
    return vsscript_getOutput2(handle, index, nullptr);
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

// V3 API compatibility
VS_API(int) vsscript_clearOutput(VSScript *handle, int index) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy_clearOutput(handle, index);
}

VS_API(VSCore *) vsscript_getCore(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getCore(handle);
}

// V3 API compatibility
VS_API(const VSAPI *) vsscript_getVSApi(void) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getVSAPI(3 << 16);
}

// V3 API compatibility
VS_API(int) vsscript_getVariable(VSScript *handle, const char *name, VSMap *dst) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    int result = vpy4_getVariable(handle, name, dst);
    const VSAPI *vsapi = vpy4_getVSAPI(VAPOURSYNTH_API_VERSION);
    int numKeys = vsapi->mapNumKeys(dst);
    for (int i = 0; i < numKeys; i++) {
        int keyType = vsapi->mapGetType(dst, vsapi->mapGetKey(dst, i));
        if (keyType == ptAudioNode || keyType == ptAudioFrame) {
            vsapi->clearMap(dst);
            return 1;
        }
    }
    return result;
}

static int VS_CC getVariable(VSScript *handle, const char *name, VSMap *dst) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_getVariable(handle, name, dst);
}

VS_API(int) vsscript_setVariable(VSScript *handle, const VSMap *vars) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy4_setVariables(handle, vars);
}

static void VS_CC evalSetWorkingDir(VSScript *handle, int setCWD) VS_NOEXCEPT {
    handle->setCWD = setCWD;
}

// V3 API compatibility
VS_API(int) vsscript_clearVariable(VSScript *handle, const char *name) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    return vpy_clearVariable(handle, name);
}

// V3 API compatibility
VS_API(void) vsscript_clearEnvironment(VSScript *handle) VS_NOEXCEPT {
    std::lock_guard<std::mutex> lock(vsscriptlock);
    vpy_clearEnvironment(handle);
}

static VSSCRIPTAPI vsscript_api = {
    &getAPIVersion,
    &vsscript_getVSApi2,
    &createScript,
    &vsscript_getCore,
    &evaluateBuffer,
    &evaluateFile,
    &vsscript_getError,
    &vsscript_getExitCode,
    &getVariable,
    &vsscript_setVariable,
    &getOutputNode,
    &getOutputAlphaNode,
    &getAltOutputMode,
    &vsscript_freeScript,
    &evalSetWorkingDir
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