/* L25: the Vulkan loader's ICD load/unload race, isolated from VapourSynth.

   L24 found the loader reaching _dl_close_worker without its own lock: one thread frees a driver
   library while another reads it, and once that landed as a SEGV at a program counter inside a
   library the loader had just unmapped. This probe touches nothing but the loader, so it says
   whether a mitigation works without anything of ours in the picture -- and it is what an upstream
   report can be built from.

     loader_icd_race [seconds] [threads] [keepalive]     defaults 10, 8, 0

   Every thread loops over the calls that make the loader load and unload drivers: the global
   extension query (which preloads the ICDs), instance creation (which drops them again),
   physical device enumeration, and instance destruction. With keepalive set, the main thread
   holds one instance for the whole run, so the ICD reference count never reaches zero.

   Run it under ThreadSanitizer with NO suppression file and count the reports:

     LD_PRELOAD=$(gcc -print-file-name=libtsan.so) TSAN_OPTIONS=handle_segv=0 ./loader_icd_race

   It exits 0 whatever it finds -- ThreadSanitizer's own exit code is the verdict, and a crash is
   the other one. */
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static PFN_vkGetInstanceProcAddr getProc;
static PFN_vkCreateInstance createInstance;
static PFN_vkDestroyInstance destroyInstance;
static PFN_vkEnumerateInstanceExtensionProperties enumExtensions;
static PFN_vkEnumerateInstanceVersion enumVersion;
static PFN_vkEnumeratePhysicalDevices enumPhysical;
static volatile int stop;
static long iterations[64];

static unsigned long long millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static VkInstance makeInstance(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo info;
    VkInstance instance = VK_NULL_HANDLE;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "loader_icd_race";
    app.apiVersion = VK_API_VERSION_1_0;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    if (createInstance(&info, NULL, &instance) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return instance;
}

static void *worker(void *arg) {
    long idx = (long)(size_t)arg;
    while (!stop) {
        uint32_t count = 0;
        VkInstance instance;
        /* Global level: the loader scans the ICD manifests and loads what they name. */
        enumExtensions(NULL, &count, NULL);
        if (enumVersion) {
            uint32_t version = 0;
            enumVersion(&version);
        }
        /* Instance level: creation releases the preloaded drivers, destruction unloads them
           when the last instance goes. */
        instance = makeInstance();
        if (instance) {
            uint32_t devices = 0;
            enumPhysical(instance, &devices, NULL);
            destroyInstance(instance, NULL);
        }
        iterations[idx]++;
    }
    return NULL;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    int threads = argc > 2 ? atoi(argv[2]) : 8;
    int keepalive = argc > 3 ? atoi(argv[3]) : 0;
    VkInstance held = VK_NULL_HANDLE;
    pthread_t t[64];
    void *library;
    long total = 0;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;

    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) { printf("no loader: %s\n", dlerror()); return 3; }
    getProc = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!getProc) { printf("no vkGetInstanceProcAddr\n"); return 3; }
    createInstance = (PFN_vkCreateInstance)getProc(NULL, "vkCreateInstance");
    enumExtensions = (PFN_vkEnumerateInstanceExtensionProperties)getProc(NULL, "vkEnumerateInstanceExtensionProperties");
    enumVersion = (PFN_vkEnumerateInstanceVersion)getProc(NULL, "vkEnumerateInstanceVersion");
    if (!createInstance || !enumExtensions) { printf("loader is missing entry points\n"); return 3; }

    printf("L25: %d threads, %d s, keepalive instance: %s\n", threads, seconds, keepalive ? "yes" : "no");
    {
        VkInstance probe = makeInstance();
        if (!probe) { printf("vkCreateInstance failed; nothing to measure\n"); return 3; }
        destroyInstance = (PFN_vkDestroyInstance)getProc(probe, "vkDestroyInstance");
        enumPhysical = (PFN_vkEnumeratePhysicalDevices)getProc(probe, "vkEnumeratePhysicalDevices");
        if (!destroyInstance || !enumPhysical) { printf("loader is missing instance entry points\n"); return 3; }
        destroyInstance(probe, NULL);
    }
    if (keepalive) {
        held = makeInstance();
        printf("     holding one instance for the whole run: %s\n", held ? "ok" : "FAILED");
    }

    for (i = 0; i < threads; i++) pthread_create(&t[i], NULL, worker, (void *)(size_t)i);
    {
        unsigned long long until = millis() + (unsigned long long)seconds * 1000ull;
        while (millis() < until) {
            struct timespec nap = { 0, 50 * 1000 * 1000 };
            nanosleep(&nap, NULL);
        }
    }
    stop = 1;
    for (i = 0; i < threads; i++) pthread_join(t[i], NULL);
    for (i = 0; i < threads; i++) total += iterations[i];
    if (held) destroyInstance(held, NULL);
    printf("     %ld instance create/destroy cycles\n", total);
    printf("done\n");
    return 0;
}
