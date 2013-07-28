/*
* This file is an example on how to use the VSScript part of the VapourSynth API.
* It writes out all the frames of an input script to a file.
* This file may be freely modified/copied/distributed.
*/

#include <QtCore/QString>
#include <QtCore/QMap>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QWaitCondition>
#include "VSScript.h"
#include "VSHelper.h"



const VSAPI *vsapi = NULL;
VSScript *se = NULL;
FILE *outFile = NULL;
QWaitCondition condition;
QMutex mutex;

bool error = false;
int outputFrames = 0;
int requestedFrames = 0;
int completedFrames = 0;
int totalFrames = 0;
QMap<int, const VSFrameRef *> reorderMap;
QString errorMessage;

// This function gets called every time a frame is successfully produced or an error happens during processing
// f == NULL on errors, note that errorMsg is usually but not always set
// VapourSynth has internal locking so this function will only be called from one thread at a time to make
// things easier
// If multiple frames are requested they can arrive in any order so you will have to reorder them

void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *node, const char *errorMsg) {
    completedFrames++;

    if (f) {
        // Use a map to reorder frames in a lazy way
        reorderMap.insert(n, f);
        // Write the next frame(s) to file
        while (reorderMap.contains(outputFrames)) {
            const VSFrameRef *frame = reorderMap.take(outputFrames);
            const VSFormat *fi = vsapi->getFrameFormat(frame);
            for (int p = 0; p < fi->numPlanes; p++) {
                int stride = vsapi->getStride(frame, p);
                const uint8_t *readPtr = vsapi->getReadPtr(frame, p);
                int rowSize = vsapi->getFrameWidth(frame, p) * fi->bytesPerSample;
                int height = vsapi->getFrameHeight(frame, p);
                for (int y = 0; y < height; y++) {
                    // You should probably handle any fwrite errors here as well
                    fwrite(readPtr, rowSize, 1, outFile);
                    readPtr += stride;
                }
            }

            vsapi->freeFrame(frame);
            outputFrames++;
        }
    } else {
        // Abort on error, do it by setting the total number of frames to the number already requested so there are
        // no outstanding requests when main() continues
        error = true;
        totalFrames = requestedFrames;
        if (errorMsg)
            errorMessage = QString("Error: Failed to retrieve frame ") + n + QString(" with error: ") + QString::fromUtf8(errorMsg);
        else
            errorMessage = QString("Error: Failed to retrieve frame ") + n;
    }

    // Request another frame
    if (requestedFrames < totalFrames) {
        vsapi->getFrameAsync(requestedFrames, node, frameDoneCallback, NULL);
        requestedFrames++;
    }

    // Let main() continue when all frames are done
    if (totalFrames == completedFrames) {
        QMutexLocker lock(&mutex);
        condition.wakeOne();
    }
}

int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stderr, "Usage: vsscript_example <infile> <outfile>\n");
        return 1;
    }

    // Open the output file for writing
    QString outputFilename = argv[2];
    outFile = fopen(outputFilename.toLocal8Bit(), "wb");

    if (!outFile) {
        fprintf(stderr, "Failed to open output for writing\n");
        return 1;
    }

    // Initialize VSScript, vsscript_finalize() needs to be called the same number of times as vsscript_init()
    if (!vsscript_init()) {
        // VapourSynth probably isn't properly installed at all
        fprintf(stderr, "Failed to initialize VapourSynth environment\n");
        return 1;
    }

    // Get a pointer to the normal api struct, exists so you don't have to link with the VapourSynth core library
    // Failure only happens on very rare API version mismatches
    vsapi = vsscript_getVSApi();
    Q_ASSERT(vsapi);

    // This line does the actual script evaluation. If se = NULL it will create a new environment
    if (vsscript_evaluateFile(&se, QString(argv[1]).toUtf8())) {
        fprintf(stderr, "Script evaluation failed:\n%s", vsscript_getError(se));
        vsscript_freeScript(se);
        vsscript_finalize();
        return 1;
    }

    // Get the clip set as output. It is valid until the out index is re-set/cleared/the script is freed
    VSNodeRef *node = vsscript_getOutput(se, 0);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node\n");
       vsscript_freeScript(se);
       vsscript_finalize();
       return 1;
    }

    // Put an annoying restriction in the code
	const VSVideoInfo *vi = vsapi->getVideoInfo(node);

    if (!isConstantFormat(vi) || vi->numFrames == 0) {
        fprintf(stderr, "Cannot output clips with varying dimensions or unknown length\n");
        vsapi->freeNode(node);
        vsscript_freeScript(se);
        vsscript_finalize();
        return 1;
    }

    // This part handles the frame request loop
    // You can also just use vsapi->getFrame() to have a simple synchronous way to get frames
    // but with possibly worse performance
    const VSCoreInfo *info = vsapi->getCoreInfo(vsscript_getCore(se));
    totalFrames = vi->numFrames;

    // Since it's a callback we need a mutex and condition to wait until all the requested
    // frames have been returned
    QMutexLocker lock(&mutex);

    // Start by requesting as many frames as the instantiated core has worker threads
    // This is the upper limit of how many frames it ever makes sense to request in parallel
    int intitalRequestSize = std::min(info->numThreads, totalFrames);
    // Has to be assigned BEFORE requesting any frames or there will be a race
    requestedFrames = intitalRequestSize;

    // Do the initial request, note that frameDoneCallback() may be called already after the
    // first loop iteration
    for (int n = 0; n < intitalRequestSize; n++)
        vsapi->getFrameAsync(n, node, frameDoneCallback, NULL);

    // Wait for all frames to be written to disk
    condition.wait(&mutex);

    fclose(outFile);

    vsapi->freeNode(node);
    vsscript_freeScript(se);
    vsscript_finalize();

    if (error) {
        fprintf(stderr, "%s", errorMessage.toUtf8().constData());
        return 1;
    }

    return 0;
}
