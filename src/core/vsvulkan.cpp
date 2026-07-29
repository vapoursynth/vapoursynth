/*
* Copyright (c) 2026 Fredrik Mellbin
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

#ifdef VS_TARGET_OS_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include "vsvulkan.h"

#include <cstddef>
#include <cstring>

namespace {

/* One descriptor per entry point. The name is not stored here: the names live end to end in a
   single string so that the table needs no relocation per entry, which is the same reason ffmpeg
   does it, and the offsets into that string are walked in step with this array. */
struct VSVulkanLoadInfo {
    VSVulkanLevel level;
    VSVulkanRequirement requirement;
    uint16_t structOffset;
};

const VSVulkanLoadInfo vsVulkanLoadInfo[] = {
#define VS_VK_LOAD_INFO(level, req, name) \
    { level, req, static_cast<uint16_t>(offsetof(VSVulkanFunctions, vk##name)) },
    VS_VK_FUNCTION_LIST(VS_VK_LOAD_INFO)
#undef VS_VK_LOAD_INFO
};

/* "vkCreateInstance\0vkDestroyInstance\0..." walked in lockstep with the table above. */
const char vsVulkanNames[] =
#define VS_VK_NAME(level, req, name) "vk" #name "\0"
    VS_VK_FUNCTION_LIST(VS_VK_NAME)
#undef VS_VK_NAME
    ;

constexpr size_t vsVulkanFunctionCount = sizeof(vsVulkanLoadInfo) / sizeof(vsVulkanLoadInfo[0]);

PFN_vkVoidFunction *functionSlot(VSVulkanFunctions &vk, uint16_t structOffset) {
    return reinterpret_cast<PFN_vkVoidFunction *>(reinterpret_cast<char *>(&vk) + structOffset);
}

/* Resolves every entry point belonging to one level. Instance and device handles are both passed
   because a device level lookup needs the instance level vkGetDeviceProcAddr to have been found
   first, and that in turn came from the instance. */
bool loadLevel(VSVulkanFunctions &vk, VSVulkanLevel level, PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    VkInstance instance, VkDevice device, std::string &errorMessage) {
    const char *name = vsVulkanNames;

    for (size_t i = 0; i < vsVulkanFunctionCount; name += strlen(name) + 1, i++) {
        const VSVulkanLoadInfo &info = vsVulkanLoadInfo[i];
        if (info.level != level)
            continue;

        PFN_vkVoidFunction fn = nullptr;
        if (level == VS_VK_DEVICE)
            fn = vk.vkGetDeviceProcAddr(device, name);
        else
            fn = getInstanceProcAddr(level == VS_VK_GLOBAL ? VK_NULL_HANDLE : instance, name);

        if (!fn && info.requirement == VS_VK_REQUIRED) {
            if (level == VS_VK_DEVICE)
                errorMessage = std::string("Vulkan entry point ") + name + " is missing, which should "
                    "not be possible on a device reporting 1.4 support";
            else
                errorMessage = std::string("Vulkan entry point ") + name + " is missing from the " +
                    (level == VS_VK_GLOBAL ? "loader" : "instance") + ", which likely predates 1.4";
            return false;
        }

        *functionSlot(vk, info.structOffset) = fn;
    }

    return true;
}

} // namespace

VSVulkanLoader::~VSVulkanLoader() {
    closeLibrary();
}

void VSVulkanLoader::closeLibrary() {
    if (library) {
#ifdef VS_TARGET_OS_WINDOWS
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
        library = nullptr;
    }
}

bool VSVulkanLoader::initialize(std::string &errorMessage) {
    if (getInstanceProcAddrFn || library) {
        errorMessage = "Vulkan loader is already initialized";
        return false;
    }

    /* Loaded by name rather than linked, so that a build with Vulkan compiled in still starts on a
       machine without a driver and can report that cleanly instead of failing to load at all. */
#ifdef VS_TARGET_OS_WINDOWS
    const char *libraryName = "vulkan-1.dll";
    library = LoadLibraryA(libraryName);
#elif defined(__APPLE__)
    const char *libraryName = "libvulkan.1.dylib";
    library = dlopen(libraryName, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        libraryName = "libMoltenVK.dylib";
        library = dlopen(libraryName, RTLD_NOW | RTLD_LOCAL);
    }
#else
    const char *libraryName = "libvulkan.so.1";
    library = dlopen(libraryName, RTLD_NOW | RTLD_LOCAL);
#endif

    if (!library) {
        errorMessage = std::string("Failed to load the Vulkan loader (") + libraryName + ")";
        return false;
    }

#ifdef VS_TARGET_OS_WINDOWS
    PFN_vkGetInstanceProcAddr entry = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr")));
#else
    PFN_vkGetInstanceProcAddr entry = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(library, "vkGetInstanceProcAddr"));
#endif

    if (!entry) {
        closeLibrary();
        errorMessage = "The Vulkan loader does not export vkGetInstanceProcAddr";
        return false;
    }

    getInstanceProcAddrFn = entry;
    if (!loadLevel(vk, VS_VK_GLOBAL, getInstanceProcAddrFn, VK_NULL_HANDLE, VK_NULL_HANDLE, errorMessage)) {
        /* Everything resolved so far points into the library about to be unloaded, so a failure
           has to put the whole object back to its unused state or a later call would sail past
           the initialization guards and jump through freed memory. */
        vk = {};
        getInstanceProcAddrFn = nullptr;
        closeLibrary();
        return false;
    }

    return true;
}

bool VSVulkanLoader::initialize(PFN_vkGetInstanceProcAddr getInstanceProcAddr, std::string &errorMessage) {
    if (getInstanceProcAddrFn || library) {
        errorMessage = "Vulkan loader is already initialized";
        return false;
    }

    if (!getInstanceProcAddr) {
        errorMessage = "No vkGetInstanceProcAddr supplied";
        return false;
    }

    /* Nothing to close later: the caller owns whatever this came from and will outlive us. */
    getInstanceProcAddrFn = getInstanceProcAddr;
    if (!loadLevel(vk, VS_VK_GLOBAL, getInstanceProcAddrFn, VK_NULL_HANDLE, VK_NULL_HANDLE, errorMessage)) {
        vk = {};
        getInstanceProcAddrFn = nullptr;
        return false;
    }

    return true;
}

bool VSVulkanLoader::loadInstance(VkInstance instance, std::string &errorMessage) {
    if (!getInstanceProcAddrFn) {
        errorMessage = "Vulkan loader used before it was initialized";
        return false;
    }

    return loadLevel(vk, VS_VK_INSTANCE, getInstanceProcAddrFn, instance, VK_NULL_HANDLE, errorMessage);
}

bool VSVulkanLoader::loadDevice(VkDevice device, std::string &errorMessage) {
    if (!vk.vkGetDeviceProcAddr) {
        errorMessage = "Vulkan device entry points requested before the instance ones were loaded";
        return false;
    }

    return loadLevel(vk, VS_VK_DEVICE, getInstanceProcAddrFn, VK_NULL_HANDLE, device, errorMessage);
}

bool VSVulkanLoader::checkInstanceVersion(uint32_t required, uint32_t &found, std::string &errorMessage) const {
    if (!vk.vkEnumerateInstanceVersion) {
        errorMessage = "Vulkan loader used before it was initialized";
        return false;
    }

    found = 0;
    VkResult res = vk.vkEnumerateInstanceVersion(&found);
    if (res != VK_SUCCESS) {
        errorMessage = "Failed to query the Vulkan instance version";
        return false;
    }

    if (found < required) {
        errorMessage = "The Vulkan loader supports " + std::to_string(VK_API_VERSION_MAJOR(found)) + "." +
            std::to_string(VK_API_VERSION_MINOR(found)) + " but " +
            std::to_string(VK_API_VERSION_MAJOR(required)) + "." +
            std::to_string(VK_API_VERSION_MINOR(required)) + " is required";
        return false;
    }

    return true;
}
