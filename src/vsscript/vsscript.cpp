/*
* Copyright (c) 2013-2015 Fredrik Mellbin
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

#include "VapourSynth.h"
#include "VSScript.h"
#include "cython/vapoursynth_api.h"
#include <mutex>
#include <atomic>

#ifdef VS_TARGET_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

std::once_flag flag;

struct VSScript : public VPYScriptExport {
};

std::atomic<int> initializationCount(0);
std::atomic<int> scriptId(1000);
bool initialized = false;
PyThreadState *ts = nullptr;
PyGILState_STATE s;

static void real_init(void) {
#ifdef VS_TARGET_OS_WINDOWS

#if defined(_WIN32) && defined(_WIN64)
    const wchar_t *keyPython = L"SOFTWARE\\Python\\PythonCore\\3.5\\InstallPath";
#elif defined(_WIN32) && !defined(_WIN64)
    const wchar_t *keyPython = L"SOFTWARE\\Python\\PythonCore\\3.5-32\\InstallPath";
#else
#error "Cannot determine Compiler Bitdepth - is this really Windows?"
#endif

    DWORD dwType = REG_SZ;
    HKEY hKey = 0;

    wchar_t value[1024];
    DWORD valueLength = 1000;
    if (RegOpenKeyW(HKEY_CURRENT_USER, keyPython, &hKey) != ERROR_SUCCESS
        && RegOpenKeyW(HKEY_LOCAL_MACHINE, keyPython, &hKey) != ERROR_SUCCESS)
        return;
    LSTATUS status = RegQueryValueExW(hKey, L"", nullptr, &dwType, (LPBYTE)&value, &valueLength);
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS)
        return;

    std::wstring pyPath = value;
    pyPath += L"python35.dll";

    HMODULE pythonDll = LoadLibraryW(pyPath.c_str());
    if (!pythonDll)
        return;
#endif
    int preInitialized = Py_IsInitialized();
    if (!preInitialized)
        Py_InitializeEx(0);
    s = PyGILState_Ensure();
    if (import_vapoursynth())
        return;
    if (vpy_initVSScript())
        return;
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
    int count = --initializationCount;
    assert(count >= 0);
    return count;
}

VS_API(int) vsscript_createScript(VSScript **handle) {
    *handle = new(std::nothrow)VSScript();
    if (*handle) {
        (*handle)->pyenvdict = nullptr;
        (*handle)->errstr = nullptr;
        (*handle)->id = ++scriptId;
        return vpy_createScript(*handle);
    } else {
        return 1;
    }
}

VS_API(int) vsscript_evaluateScript(VSScript **handle, const char *script, const char *scriptFilename, int flags) {
    if (*handle == nullptr) {
        *handle = new(std::nothrow)VSScript();
        if (*handle) {
            (*handle)->pyenvdict = nullptr;
            (*handle)->errstr = nullptr;
            (*handle)->id = ++scriptId;
        } else {
            return 1;
        }
    }
    return vpy_evaluateScript(*handle, script, scriptFilename ? scriptFilename : "<string>", flags);
}

VS_API(int) vsscript_evaluateFile(VSScript **handle, const char *scriptFilename, int flags) {
    if (*handle == nullptr) {
        *handle = new(std::nothrow)VSScript();
        if (*handle) {
            (*handle)->pyenvdict = nullptr;
            (*handle)->errstr = nullptr;
            (*handle)->id = ++scriptId;
        } else {
            return 1;
        }
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
    if (handle)
        return vpy_getError(handle);
    else
        return "Invalid handle (NULL)";
}

VS_API(VSNodeRef *) vsscript_getOutput(VSScript *handle, int index) {
    return vpy_getOutput(handle, index);
}

VS_API(int) vsscript_clearOutput(VSScript *handle, int index) {
    return vpy_clearOutput(handle, index);
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

VS_API(int) vsscript_setVariable(VSScript *handle, const VSMap *vars) {
    return vpy_setVariable(handle, vars);
}

VS_API(int) vsscript_clearVariable(VSScript *handle, const char *name) {
    return vpy_clearVariable(handle, name);
}

VS_API(void) vsscript_clearEnvironment(VSScript *handle) {
    vpy_clearEnvironment(handle);
}
