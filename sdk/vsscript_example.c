/*
* This file is an example on how to use the VSScript part of the VapourSynth API.
* It writes out all the frames of an input script to a file.
* This file may be freely modified/copied/distributed.
*
* For an example of how to use getFrameAsync() see src/vspipe/vspipe.cpp
* It's basically the same as this example but with a callback when the
* processing is done.
*/

#include "VSScript4.h"
#include "VSHelper4.h"
#include <stdio.h>
#include <assert.h>



int main(int argc, char **argv) {
    const VSAPI *vsapi = NULL;
    const VSSCRIPTAPI *vssapi = NULL;
    VSScript *se = NULL;
    FILE *outFile = NULL;

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
    vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    if (!vssapi) {
        // VapourSynth probably isn't properly installed at all
        fprintf(stderr, "Failed to initialize VSScript library\n");
        return 1;
    }

    // Get a pointer to the normal api struct, exists so you don't have to link with the VapourSynth core library
    // Failure only happens on very rare API version mismatches and usually doesn't need to be checked
    vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
    assert(vsapi);

    // This line does the actual script evaluation. If se = NULL it will create a new environment
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
