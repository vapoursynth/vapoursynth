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
#    define NOMINMAX
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include "vsvulkan.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {

std::string versionToString(uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
        std::to_string(VK_API_VERSION_MINOR(version)) + "." +
        std::to_string(VK_API_VERSION_PATCH(version));
}

/* The queried and the enabled features travel through the same chain layout; only which side
   fills in the booleans differs. */
struct VSVulkanFeatureChain {
    VkPhysicalDeviceFeatures2 f2 = {};
    VkPhysicalDeviceVulkan12Features f12 = {};
    VkPhysicalDeviceVulkan13Features f13 = {};
    VkPhysicalDeviceVulkan14Features f14 = {};

    VSVulkanFeatureChain() {
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        f14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        f2.pNext = &f12;
        f12.pNext = &f13;
        f13.pNext = &f14;
    }
};

/* The version and feature gate shared by device selection, adoption and enumeration. The reason
   is phrased to continue a sentence beginning with the device name. */
bool deviceSuitable(const VSVulkanFunctions &vkf, VkPhysicalDevice dev, uint32_t &apiVersion, std::string &reason) {
    VkPhysicalDeviceProperties2 props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkf.vkGetPhysicalDeviceProperties2(dev, &props);
    apiVersion = props.properties.apiVersion;

    if (apiVersion < VS_VULKAN_API_VERSION) {
        reason = "reports Vulkan " + versionToString(apiVersion) + " but 1.4 is required";
        return false;
    }

    VSVulkanFeatureChain chain;
    vkf.vkGetPhysicalDeviceFeatures2(dev, &chain.f2);
#define VS_VK_CHECK_FEATURE(ver, member, req) \
    if (req == VS_VK_REQUIRED && !chain.f##ver.member) { \
        reason = "does not support the " #member " feature"; \
        return false; \
    }
    VS_VK_FEATURE_LIST(VS_VK_CHECK_FEATURE)
#undef VS_VK_CHECK_FEATURE

    return true;
}

bool instanceLayerAvailable(const VSVulkanFunctions &vkf, const char *layerName) {
    uint32_t count = 0;
    if (vkf.vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkf.vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;
    for (const auto &layer : layers) {
        if (!strcmp(layer.layerName, layerName))
            return true;
    }
    return false;
}

bool instanceExtensionAvailable(const VSVulkanFunctions &vkf, const char *layerName, const char *extensionName) {
    uint32_t count = 0;
    if (vkf.vkEnumerateInstanceExtensionProperties(layerName, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkf.vkEnumerateInstanceExtensionProperties(layerName, &count, extensions.data()) != VK_SUCCESS)
        return false;
    for (const auto &ext : extensions) {
        if (!strcmp(ext.extensionName, extensionName))
            return true;
    }
    return false;
}

VkResult createBareInstance(const VSVulkanFunctions &vkf, uint32_t layerCount, const char *const *layers,
    uint32_t extensionCount, const char *const *extensions, VkInstance &instance) {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "VapourSynth";
    app.apiVersion = VS_VULKAN_API_VERSION;

    VkInstanceCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;
    create.enabledLayerCount = layerCount;
    create.ppEnabledLayerNames = layers;
    create.enabledExtensionCount = extensionCount;
    create.ppEnabledExtensionNames = extensions;

    return vkf.vkCreateInstance(&create, nullptr, &instance);
}

} // namespace

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

VSVulkanDevice::~VSVulkanDevice() {
    if (owned)
        teardown();
}

void VSVulkanDevice::teardown() {
    /* Null checks on the entry points as well as the handles: a failure partway through loading
       leaves the table filled only up to the missing function. */
    if (deviceHandle && vk.vkDeviceWaitIdle && vk.vkDestroyDevice) {
        vk.vkDeviceWaitIdle(deviceHandle);
        vk.vkDestroyDevice(deviceHandle, nullptr);
    }
    if (messenger && vk.vkDestroyDebugUtilsMessengerEXT)
        vk.vkDestroyDebugUtilsMessengerEXT(instanceHandle, messenger, nullptr);
    if (instanceHandle && vk.vkDestroyInstance)
        vk.vkDestroyInstance(instanceHandle, nullptr);
    deviceHandle = VK_NULL_HANDLE;
    messenger = VK_NULL_HANDLE;
    instanceHandle = VK_NULL_HANDLE;
    physicalDeviceHandle = VK_NULL_HANDLE;
}

void VSVulkanDevice::emitLog(int severity, const std::string &message) const {
    if (logFn)
        logFn(severity, message.c_str(), logUserData);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VSVulkanDevice::debugMessengerTrampoline(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData) {
    const VSVulkanDevice *self = static_cast<const VSVulkanDevice *>(userData);
    int mapped = VS_VK_LOG_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        mapped = VS_VK_LOG_ERROR;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        mapped = VS_VK_LOG_WARNING;
    self->emitLog(mapped, (callbackData && callbackData->pMessage) ? callbackData->pMessage : "");
    /* Never abort the offending call; that choice belongs to the validation layer settings. */
    return VK_FALSE;
}

bool VSVulkanDevice::create(int physicalDeviceIndex, bool enableValidation, std::string &errorMessage) {
    if (state != State::Unused) {
        errorMessage = "VSVulkanDevice cannot be reused after a previous create or adopt";
        return false;
    }
    /* Flipped to Ready only at the very end so every failure return leaves the object dead. */
    state = State::Failed;

    if (!loader.initialize(errorMessage))
        return false;

    uint32_t loaderVersion = 0;
    if (!loader.checkInstanceVersion(VS_VULKAN_API_VERSION, loaderVersion, errorMessage))
        return false;

    /* Tooling is best effort. The validation layer is a development install, so asking for it
       when it is missing warns rather than fails; debug utils goes in whenever present because
       labels are free and make captures readable. */
    const char *layers[1] = {};
    uint32_t layerCount = 0;
    if (enableValidation) {
        if (instanceLayerAvailable(vk, "VK_LAYER_KHRONOS_validation")) {
            layers[layerCount++] = "VK_LAYER_KHRONOS_validation";
        } else {
            emitLog(VS_VK_LOG_WARNING, "Vulkan validation requested but VK_LAYER_KHRONOS_validation is not installed");
        }
    }

    const char *extensions[1] = {};
    uint32_t extensionCount = 0;
    bool wantMessenger = false;
    if (instanceExtensionAvailable(vk, nullptr, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) ||
        (layerCount && instanceExtensionAvailable(vk, layers[0], VK_EXT_DEBUG_UTILS_EXTENSION_NAME))) {
        extensions[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        wantMessenger = enableValidation;
    }

    VkResult res = createBareInstance(vk, layerCount, layers, extensionCount, extensions, instanceHandle);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateInstance failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    if (!loader.loadInstance(instanceHandle, errorMessage)) {
        teardown();
        return false;
    }

    if (wantMessenger && vk.vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT mci = {};
        mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = &debugMessengerTrampoline;
        mci.pUserData = this;
        if (vk.vkCreateDebugUtilsMessengerEXT(instanceHandle, &mci, nullptr, &messenger) != VK_SUCCESS) {
            messenger = VK_NULL_HANDLE;
            emitLog(VS_VK_LOG_WARNING, "Failed to create the Vulkan debug messenger, validation output will be lost");
        }
    }

    uint32_t deviceCount = 0;
    res = vk.vkEnumeratePhysicalDevices(instanceHandle, &deviceCount, nullptr);
    if (res != VK_SUCCESS || deviceCount == 0) {
        errorMessage = "No Vulkan devices found";
        teardown();
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    res = vk.vkEnumeratePhysicalDevices(instanceHandle, &deviceCount, devices.data());
    if (res != VK_SUCCESS) {
        errorMessage = "vkEnumeratePhysicalDevices failed (VkResult " + std::to_string(res) + ")";
        teardown();
        return false;
    }

    if (physicalDeviceIndex >= 0) {
        if (static_cast<uint32_t>(physicalDeviceIndex) >= deviceCount) {
            errorMessage = "Vulkan device " + std::to_string(physicalDeviceIndex) + " requested but only " +
                std::to_string(deviceCount) + " present";
            teardown();
            return false;
        }
        uint32_t apiVersion = 0;
        std::string reason;
        VkPhysicalDeviceProperties2 p2 = {};
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vk.vkGetPhysicalDeviceProperties2(devices[physicalDeviceIndex], &p2);
        if (!deviceSuitable(vk, devices[physicalDeviceIndex], apiVersion, reason)) {
            errorMessage = "Vulkan device " + std::to_string(physicalDeviceIndex) + " (" +
                p2.properties.deviceName + ") " + reason;
            teardown();
            return false;
        }
        physicalDeviceHandle = devices[physicalDeviceIndex];
    } else {
        /* First suitable discrete GPU, then anything suitable; the reasons of every rejected
           device end up in the error so a refusal is diagnosable from the message alone. */
        int pick = -1;
        int fallback = -1;
        std::string reasons;
        for (uint32_t i = 0; i < deviceCount; i++) {
            VkPhysicalDeviceProperties2 p2 = {};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            vk.vkGetPhysicalDeviceProperties2(devices[i], &p2);
            uint32_t apiVersion = 0;
            std::string reason;
            if (!deviceSuitable(vk, devices[i], apiVersion, reason)) {
                reasons += std::string(reasons.empty() ? "" : "; ") + p2.properties.deviceName + " " + reason;
                continue;
            }
            if (p2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                pick = static_cast<int>(i);
                break;
            } else if (fallback < 0) {
                fallback = static_cast<int>(i);
            }
        }
        if (pick < 0)
            pick = fallback;
        if (pick < 0) {
            errorMessage = "No usable Vulkan device found" + (reasons.empty() ? std::string() : " (" + reasons + ")");
            teardown();
            return false;
        }
        physicalDeviceHandle = devices[pick];
    }

    /* A compute family without graphics maps to the async compute engines on both major vendors
       and keeps filter work out of the way of whatever rendering the process may also be doing;
       a family that is neither compute nor graphics but can transfer is the DMA engine, which
       copies without occupying the compute units at all. */
    uint32_t familyCount = 0;
    vk.vkGetPhysicalDeviceQueueFamilyProperties2(physicalDeviceHandle, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> families(familyCount);
    for (auto &f : families) {
        f = {};
        f.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vk.vkGetPhysicalDeviceQueueFamilyProperties2(physicalDeviceHandle, &familyCount, families.data());

    uint32_t computeFamily = UINT32_MAX;
    uint32_t transferFamily = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; i++) {
        VkQueueFlags flags = families[i].queueFamilyProperties.queueFlags;
        if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT)) {
            computeFamily = i;
            break;
        }
    }
    for (uint32_t i = 0; i < familyCount && computeFamily == UINT32_MAX; i++) {
        if (families[i].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT)
            computeFamily = i;
    }
    if (computeFamily == UINT32_MAX) {
        errorMessage = "The selected Vulkan device has no compute capable queue family";
        teardown();
        return false;
    }
    for (uint32_t i = 0; i < familyCount; i++) {
        VkQueueFlags flags = families[i].queueFamilyProperties.queueFlags;
        if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT))) {
            transferFamily = i;
            break;
        }
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreate[2] = {};
    uint32_t queueCreateCount = 0;
    queueCreate[queueCreateCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreate[queueCreateCount].queueFamilyIndex = computeFamily;
    queueCreate[queueCreateCount].queueCount = 1;
    queueCreate[queueCreateCount].pQueuePriorities = &priority;
    queueCreateCount++;
    if (transferFamily != UINT32_MAX) {
        queueCreate[queueCreateCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreate[queueCreateCount].queueFamilyIndex = transferFamily;
        queueCreate[queueCreateCount].queueCount = 1;
        queueCreate[queueCreateCount].pQueuePriorities = &priority;
        queueCreateCount++;
    }

    /* Only what VS_VK_FEATURE_LIST names is enabled rather than echoing back everything the
       device offers; drivers specialize on disabled features and there is no reason to pay for
       paths nothing uses. Optional features are enabled when the device has them, which is what
       the earlier suitability check deliberately did not insist on. Note that no device
       extensions are enabled at all. */
    VSVulkanFeatureChain queried;
    vk.vkGetPhysicalDeviceFeatures2(physicalDeviceHandle, &queried.f2);
    VSVulkanFeatureChain enabled;
#define VS_VK_ENABLE_FEATURE(ver, member, req) \
    enabled.f##ver.member = (req == VS_VK_REQUIRED) ? VK_TRUE : queried.f##ver.member;
    VS_VK_FEATURE_LIST(VS_VK_ENABLE_FEATURE)
#undef VS_VK_ENABLE_FEATURE
    hostImageCopyFlag = queried.f14.hostImageCopy != 0;

    VkDeviceCreateInfo deviceCreate = {};
    deviceCreate.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreate.pNext = &enabled.f2;
    deviceCreate.queueCreateInfoCount = queueCreateCount;
    deviceCreate.pQueueCreateInfos = queueCreate;

    res = vk.vkCreateDevice(physicalDeviceHandle, &deviceCreate, nullptr, &deviceHandle);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateDevice failed (VkResult " + std::to_string(res) + ")";
        deviceHandle = VK_NULL_HANDLE;
        teardown();
        return false;
    }

    if (!loader.loadDevice(deviceHandle, errorMessage)) {
        teardown();
        return false;
    }

    VkDeviceQueueInfo2 queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queueInfo.queueFamilyIndex = computeFamily;
    queueInfo.queueIndex = 0;
    vk.vkGetDeviceQueue2(deviceHandle, &queueInfo, &computeQ.queue);
    computeQ.family = computeFamily;
    computeQ.index = 0;
    if (transferFamily != UINT32_MAX) {
        queueInfo.queueFamilyIndex = transferFamily;
        vk.vkGetDeviceQueue2(deviceHandle, &queueInfo, &transferQ.queue);
        transferQ.family = transferFamily;
        transferQ.index = 0;
        transferPtr = &transferQ;
    }

    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vk.vkGetPhysicalDeviceProperties2(physicalDeviceHandle, &props2);
    props = props2.properties;
    VkPhysicalDeviceMemoryProperties2 memProps2 = {};
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vk.vkGetPhysicalDeviceMemoryProperties2(physicalDeviceHandle, &memProps2);
    memProps = memProps2.memoryProperties;

    owned = true;
    state = State::Ready;
    return true;
}

bool VSVulkanDevice::adopt(const VSVulkanDeviceImport &import, std::string &errorMessage) {
    if (state != State::Unused) {
        errorMessage = "VSVulkanDevice cannot be reused after a previous create or adopt";
        return false;
    }

    /* Argument validation happens before the loader is touched, so a rejection here leaves the
       object still usable with corrected input. */
    if (!import.getInstanceProcAddr || !import.instance || !import.physicalDevice || !import.device) {
        errorMessage = "Adopting a device requires getInstanceProcAddr, instance, physicalDevice and device";
        return false;
    }

    state = State::Failed;

    if (!loader.initialize(import.getInstanceProcAddr, errorMessage))
        return false;
    if (!loader.loadInstance(import.instance, errorMessage))
        return false;
    if (!loader.loadDevice(import.device, errorMessage))
        return false;

    instanceHandle = import.instance;
    physicalDeviceHandle = import.physicalDevice;
    deviceHandle = import.device;

    uint32_t apiVersion = 0;
    std::string reason;
    if (!deviceSuitable(vk, physicalDeviceHandle, apiVersion, reason)) {
        errorMessage = "The adopted Vulkan device " + reason;
        return false;
    }

    /* Availability stands in for enablement here since the latter cannot be queried. */
    VSVulkanFeatureChain available;
    vk.vkGetPhysicalDeviceFeatures2(physicalDeviceHandle, &available.f2);
    hostImageCopyFlag = available.f14.hostImageCopy != 0;

    VkDeviceQueueInfo2 queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queueInfo.queueFamilyIndex = import.computeQueueFamily;
    queueInfo.queueIndex = import.computeQueueIndex;
    vk.vkGetDeviceQueue2(deviceHandle, &queueInfo, &computeQ.queue);
    computeQ.family = import.computeQueueFamily;
    computeQ.index = import.computeQueueIndex;
    computeQ.lockFn = import.lockQueue;
    computeQ.unlockFn = import.unlockQueue;
    computeQ.lockContext = import.queueLockContext;
    if (import.transferQueueFamily != UINT32_MAX) {
        queueInfo.queueFamilyIndex = import.transferQueueFamily;
        queueInfo.queueIndex = import.transferQueueIndex;
        vk.vkGetDeviceQueue2(deviceHandle, &queueInfo, &transferQ.queue);
        transferQ.family = import.transferQueueFamily;
        transferQ.index = import.transferQueueIndex;
        transferQ.lockFn = import.lockQueue;
        transferQ.unlockFn = import.unlockQueue;
        transferQ.lockContext = import.queueLockContext;
        transferPtr = &transferQ;
    }

    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vk.vkGetPhysicalDeviceProperties2(physicalDeviceHandle, &props2);
    props = props2.properties;
    VkPhysicalDeviceMemoryProperties2 memProps2 = {};
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vk.vkGetPhysicalDeviceMemoryProperties2(physicalDeviceHandle, &memProps2);
    memProps = memProps2.memoryProperties;

    owned = false;
    state = State::Ready;
    return true;
}

uint32_t VSVulkanDevice::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const {
    VkMemoryPropertyFlags both = required | preferred;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & both) == both)
            return i;
    }
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return UINT32_MAX;
}

bool VSVulkanDevice::createBuffer(VSVulkanBuffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferredFlags, std::string &errorMessage) {
    buffer = {};

    uint32_t families[2] = { computeQ.family, transferQ.family };
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    if (hasDedicatedTransferQueue()) {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = 2;
        bufferInfo.pQueueFamilyIndices = families;
    } else {
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult res = vk.vkCreateBuffer(deviceHandle, &bufferInfo, nullptr, &buffer.buffer);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateBuffer failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    VkBufferMemoryRequirementsInfo2 reqInfo = {};
    reqInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    reqInfo.buffer = buffer.buffer;
    VkMemoryRequirements2 req = {};
    req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    vk.vkGetBufferMemoryRequirements2(deviceHandle, &reqInfo, &req);

    uint32_t typeIndex = findMemoryType(req.memoryRequirements.memoryTypeBits, requiredFlags, preferredFlags);
    if (typeIndex == UINT32_MAX) {
        errorMessage = "No memory type provides the requested properties for this buffer";
        destroyBuffer(buffer);
        return false;
    }

    /* Device addresses need opting in on the allocation as well as the buffer. */
    VkMemoryAllocateFlagsInfo allocFlags = {};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        allocInfo.pNext = &allocFlags;
    allocInfo.allocationSize = req.memoryRequirements.size;
    allocInfo.memoryTypeIndex = typeIndex;

    res = vk.vkAllocateMemory(deviceHandle, &allocInfo, nullptr, &buffer.memory);
    if (res != VK_SUCCESS) {
        errorMessage = "vkAllocateMemory failed for a buffer of " + std::to_string(size) + " bytes (VkResult " +
            std::to_string(res) + ")";
        destroyBuffer(buffer);
        return false;
    }

    VkBindBufferMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    bindInfo.buffer = buffer.buffer;
    bindInfo.memory = buffer.memory;
    res = vk.vkBindBufferMemory2(deviceHandle, 1, &bindInfo);
    if (res != VK_SUCCESS) {
        errorMessage = "vkBindBufferMemory2 failed (VkResult " + std::to_string(res) + ")";
        destroyBuffer(buffer);
        return false;
    }

    buffer.size = size;
    buffer.memoryFlags = memProps.memoryTypes[typeIndex].propertyFlags;

    if (buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VkMemoryMapInfo mapInfo = {};
        mapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
        mapInfo.memory = buffer.memory;
        mapInfo.size = VK_WHOLE_SIZE;
        res = vk.vkMapMemory2(deviceHandle, &mapInfo, &buffer.mapped);
        if (res != VK_SUCCESS) {
            errorMessage = "vkMapMemory2 failed (VkResult " + std::to_string(res) + ")";
            destroyBuffer(buffer);
            return false;
        }
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.buffer;
        buffer.address = vk.vkGetBufferDeviceAddress(deviceHandle, &addressInfo);
    }

    return true;
}

void VSVulkanDevice::destroyBuffer(VSVulkanBuffer &buffer) {
    if (buffer.mapped) {
        VkMemoryUnmapInfo unmapInfo = {};
        unmapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO;
        unmapInfo.memory = buffer.memory;
        vk.vkUnmapMemory2(deviceHandle, &unmapInfo);
    }
    if (buffer.buffer)
        vk.vkDestroyBuffer(deviceHandle, buffer.buffer, nullptr);
    if (buffer.memory)
        vk.vkFreeMemory(deviceHandle, buffer.memory, nullptr);
    buffer = {};
}

bool VSVulkanDevice::createImage2D(VSVulkanImage &image, VkFormat format, uint32_t width, uint32_t height,
    VkImageUsageFlags usage, std::string &errorMessage) {
    image = {};

    uint32_t families[2] = { computeQ.family, transferQ.family };
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (hasDedicatedTransferQueue()) {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = 2;
        imageInfo.pQueueFamilyIndices = families;
    } else {
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult res = vk.vkCreateImage(deviceHandle, &imageInfo, nullptr, &image.image);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateImage failed (VkResult " + std::to_string(res) + ")";
        return false;
    }

    VkImageMemoryRequirementsInfo2 reqInfo = {};
    reqInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    reqInfo.image = image.image;
    VkMemoryRequirements2 req = {};
    req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    vk.vkGetImageMemoryRequirements2(deviceHandle, &reqInfo, &req);

    uint32_t typeIndex = findMemoryType(req.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (typeIndex == UINT32_MAX) {
        errorMessage = "No device local memory type accepts this image";
        destroyImage(image);
        return false;
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.memoryRequirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    res = vk.vkAllocateMemory(deviceHandle, &allocInfo, nullptr, &image.memory);
    if (res != VK_SUCCESS) {
        errorMessage = "vkAllocateMemory failed for an image (VkResult " + std::to_string(res) + ")";
        destroyImage(image);
        return false;
    }

    VkBindImageMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bindInfo.image = image.image;
    bindInfo.memory = image.memory;
    res = vk.vkBindImageMemory2(deviceHandle, 1, &bindInfo);
    if (res != VK_SUCCESS) {
        errorMessage = "vkBindImageMemory2 failed (VkResult " + std::to_string(res) + ")";
        destroyImage(image);
        return false;
    }

    image.format = format;
    image.width = width;
    image.height = height;
    return true;
}

void VSVulkanDevice::destroyImage(VSVulkanImage &image) {
    if (image.image)
        vk.vkDestroyImage(deviceHandle, image.image, nullptr);
    if (image.memory)
        vk.vkFreeMemory(deviceHandle, image.memory, nullptr);
    image = {};
}

bool VSVulkanDevice::enumerateDevices(std::vector<VSVulkanDeviceInfo> &devices, std::string &errorMessage) {
    devices.clear();

    VSVulkanLoader loader;
    if (!loader.initialize(errorMessage))
        return false;
    uint32_t loaderVersion = 0;
    if (!loader.checkInstanceVersion(VS_VULKAN_API_VERSION, loaderVersion, errorMessage))
        return false;
    const VSVulkanFunctions &vkf = loader.functions();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = createBareInstance(vkf, 0, nullptr, 0, nullptr, instance);
    if (res != VK_SUCCESS) {
        errorMessage = "vkCreateInstance failed (VkResult " + std::to_string(res) + ")";
        return false;
    }
    if (!loader.loadInstance(instance, errorMessage)) {
        if (vkf.vkDestroyInstance)
            vkf.vkDestroyInstance(instance, nullptr);
        return false;
    }

    uint32_t deviceCount = 0;
    vkf.vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> handles(deviceCount);
    if (deviceCount)
        vkf.vkEnumeratePhysicalDevices(instance, &deviceCount, handles.data());

    for (uint32_t i = 0; i < deviceCount; i++) {
        VSVulkanDeviceInfo info;

        VkPhysicalDeviceProperties2 p2 = {};
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkf.vkGetPhysicalDeviceProperties2(handles[i], &p2);
        info.name = p2.properties.deviceName;
        info.type = p2.properties.deviceType;

        VkPhysicalDeviceMemoryProperties2 m2 = {};
        m2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        vkf.vkGetPhysicalDeviceMemoryProperties2(handles[i], &m2);
        for (uint32_t h = 0; h < m2.memoryProperties.memoryHeapCount; h++) {
            if (m2.memoryProperties.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                info.deviceLocalMemory = std::max(info.deviceLocalMemory, m2.memoryProperties.memoryHeaps[h].size);
        }

        info.usable = deviceSuitable(vkf, handles[i], info.apiVersion, info.reason);
        devices.push_back(std::move(info));
    }

    vkf.vkDestroyInstance(instance, nullptr);
    return true;
}
