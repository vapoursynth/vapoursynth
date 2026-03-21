/*
* This file is an example on how to use the VSScript part of the VapourSynth API.
* It writes out all the frames of an input script to a file.
* This file may be freely modified/copied/distributed.
*
* For an example of how to use getFrameAsync() see src/vspipe/vspipe.cpp
* It's basically the same as this example but with a callback when the
* processing is done.
*/

#define VSSCRIPT_USE_LATEST_API

#include "VSScript4.h"
#include "VSHelper4.h"
#include <stdio.h>
#include <assert.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

/*
* This bit contains boilerplate code for how to locate and load the VSScript library and get the API pointer. Normally you
* should only load the library this way and not link with it directly since its location is very unlikely to be in the
* standard library search path.
*/

typedef VS_CC const VSSCRIPTAPI *(*getVSScriptAPIType)(int);
typedef VS_CC const char *(*getVSScriptAPILastErrorType)();

getVSScriptAPIType getVSScriptAPIFunc = NULL;
getVSScriptAPILastErrorType getVSScriptAPILastErrorFunc = NULL;

static int loadVSScriptLibrary() {
#ifdef _WIN32
    const wchar_t *vsscriptPath = _wgetenv(L"VSSCRIPT_PATH");
    HMODULE lib = LoadLibraryExW(vsscriptPath ? vsscriptPath : L"VSScript.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) {
        fprintf(stderr, "Failed to load VSScript library");
        return 1;
    }
    getVSScriptAPIFunc = (getVSScriptAPIType)GetProcAddress(lib, "getVSScriptAPI");
    getVSScriptAPILastErrorFunc = (getVSScriptAPILastErrorType)GetProcAddress(lib, "getVSScriptAPILastError");
    if (!getVSScriptAPIFunc || !getVSScriptAPILastErrorFunc) {
        fprintf(stderr, "Failed to locate entry points in VSScript library");
        return 1;
    }
    return 0;
#else
    const char *vsscriptPath = getenv(L"VSSCRIPT_PATH");
#ifdef __APPLE__
    const char *defaultLibName = "libvapoursynth-script.4.dylib";
#else
    const char *defaultLibName = "libvapoursynth-script.so.4";
#endif
    void *lib = dlopen(vsscriptPath ? vsscriptPath : defaultLibName, RTLD_LAZY);
    if (!lib) {
        fprintf(stderr, "Failed to load VSScript library: %s\n", dlerror());
        return 1;
    }
    getVSScriptAPIFunc = (getVSScriptAPIType)dlsym(lib, "getVSScriptAPI");
    getVSScriptAPILastErrorFunc = (getVSScriptAPILastErrorType)dlsym(lib, "getVSScriptAPILastError");
    if (!getVSScriptAPIFunc || !getVSScriptAPILastErrorFunc) {
        fprintf(stderr, "Failed to locate entry points in VSScript library: %s\n", dlerror());
        return 1;
    }
    return 0;

#endif
}

static const char *messageTypeToString(int msgType) {
    switch (msgType) {
        case mtDebug: return "Debug";
        case mtInformation: return "Information";
        case mtWarning: return "Warning";
        case mtCritical: return "Critical";
        case mtFatal: return "Fatal";
        default: return "";
    }
}

static void VS_CC logMessageHandler(int msgType, const char *msg, void *userData) {
    if (msgType >= mtInformation)
        fprintf(stderr, "%s: %s\n", messageTypeToString(msgType), msg);
}

int main(int argc, char **argv) {
    const VSAPI *vsapi = NULL;
    const VSSCRIPTAPI *vssapi = NULL;
    VSScript *se = NULL;
    VSCore *core = NULL;
    FILE *outFile = NULL;

    if (loadVSScriptLibrary())
        return 1;

    if (argc != 3) {
        fprintf(stderr, "Usage: vsscript_example <infile> <outfile>\n");
        return 1;
    }

    // Open the output file for writing
    outFile = fopen(argv[2], "wb");

    if (!outFile) {
        fprintf(stderr, "Failed to open output for writing\n");
        return 1;
    }

    // Initialize VSScript and get the api pointer
    vssapi = getVSScriptAPIFunc(VSSCRIPT_API_VERSION);
    if (!vssapi) {
        // VapourSynth probably isn't properly installed
        fprintf(stderr, "Failed to initialize VSScript library: %s\n", getVSScriptAPILastErrorFunc());
        return 1;
    }

    // Get a pointer to the normal api struct, exists so you don't have to link with the VapourSynth core library
    // Failure only happens on very rare API version mismatches and usually doesn't need to be checked
    vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
    assert(vsapi);

    // You need to manually create a core if you want to set construction options or attach a log handler
    // before script evaluation. This is useful to print indexing progress to users among other things.
    core = vsapi->createCore(0);
    vsapi->addLogHandler(logMessageHandler, NULL, NULL, core);

    // Keep track of how much processing time filters consume
    vsapi->setCoreNodeTiming(core, 1);

    // If you pass NULL as the core value it will create a core with the default options for you
    se = vssapi->createScript(core);

    // This line does the actual script evaluation
    if (vssapi->evaluateFile(se, argv[1])) {
        fprintf(stderr, "Script evaluation failed:\n%s", vssapi->getError(se));
        vssapi->freeScript(se);
        return 1;
    }

    // Get the clip set as output. It is valid until the out index is re-set/cleared/the script is freed
    VSNode *node = vssapi->getOutputNode(se, 0);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node\n");
       vssapi->freeScript(se);
       return 1;
    }

    // Reject hard to handle formats
    const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    if (!vsh_isConstantVideoFormat(vi)) {
        fprintf(stderr, "Cannot output clips with varying dimensions or format\n");
        vsapi->freeNode(node);
        vssapi->freeScript(se);
        return 1;
    }

    // Output all frames
    char errMsg[1024];
    int error = 0;
    for (int n = 0; n < vi->numFrames; n++) {
        const VSFrame *frame = vsapi->getFrame(n, node, errMsg, sizeof(errMsg));

        if (!frame) { // Check if an error happened when getting the frame
            error = 1;
            break;
        }

        // Loop over every row of every plane write to the file
        for (int p = 0; p < vi->format.numPlanes; p++) {
            ptrdiff_t stride = vsapi->getStride(frame, p);
            const uint8_t *readPtr = vsapi->getReadPtr(frame, p);
            int rowSize = vsapi->getFrameWidth(frame, p) * vi->format.bytesPerSample;
            int height = vsapi->getFrameHeight(frame, p);

            for (int y = 0; y < height; y++) {
                // You should probably handle any fwrite errors here as well
                fwrite(readPtr, rowSize, 1, outFile);
                readPtr += stride;
            }
        }

        vsapi->freeFrame(frame);
    }

    // Cleanup
    fclose(outFile);

    vsapi->freeNode(node);
    vssapi->freeScript(se);

    if (error) {
        fprintf(stderr, "%s", errMsg);
        return 1;
    }

    return 0;
}
