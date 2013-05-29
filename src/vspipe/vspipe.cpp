/*
* Copyright (c) 2013 Fredrik Mellbin
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

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QWaitCondition>
#include <QtCore/QFile>
#include <QtCore/QMap>
#include "VSScript.h"
#include "VSHelper.h"




int numThreads = 1;
const VSAPI *vsapi = NULL;
VSScript *se = NULL;
VSNodeRef *node = NULL;
FILE *outfile = NULL;

int prefetch = 1;
int outputFrames = 0;
int requestedFrames = 0;
int completedFrames = 0;
int totalFrames = 0;
int numPlanes = 0;
bool y4m = false;
bool outputError = false;
QMap<int, const VSFrameRef *> reorderMap;

QString errorMessage;
QWaitCondition condition;
QMutex mutex;

void VS_CC frameDoneCallback(void *userData, const VSFrameRef *f, int n, VSNodeRef *, const char *errorMsg) {
    completedFrames++;

    if (f) {
        reorderMap.insert(n, f);
        while (reorderMap.contains(outputFrames)) {
            const VSFrameRef *frame = reorderMap.take(outputFrames);
            if (outputError)
                goto fwriteError;
            if (y4m) {
                if (!fwrite("FRAME\n", 6, 1, outfile)) {
                    errorMessage = "Error: fwrite() call failed";
                    totalFrames = requestedFrames;
                    outputError = true;
                    goto fwriteError;
                }
            }

            const VSFormat *fi = vsapi->getFrameFormat(frame);
            for (int p = 0; p < fi->numPlanes; p++) {
                int stride = vsapi->getStride(frame, p);
                const uint8_t *readPtr = vsapi->getReadPtr(frame, p);
                int rowSize = vsapi->getFrameWidth(frame, p) * fi->bytesPerSample;
                int height = vsapi->getFrameHeight(frame, p);
                for (int y = 0; y < height; y++) {
                    if (!fwrite(readPtr, rowSize, 1, outfile)) {
                        errorMessage = "Error: fwrite() call failed";
                        totalFrames = requestedFrames;
                        outputError = true;
                        goto fwriteError;
                    }
                    readPtr += stride;
                }
            }
            fwriteError:
            vsapi->freeFrame(frame);
            outputFrames++;
        }
    } else {
        outputError = true;
        totalFrames = requestedFrames;
        if (errorMsg)
            errorMessage = QString("Error: Failed to retrieve frame ") + n + QString(" with error: ") + QString::fromUtf8(errorMsg);
        else
            errorMessage = QString("Error: Failed to retrieve frame ") + n;
    }

    if (requestedFrames < totalFrames) {
        vsapi->getFrameAsync(requestedFrames, node, frameDoneCallback, NULL);
        requestedFrames++;
    }

    if (totalFrames == completedFrames) {
        QMutexLocker lock(&mutex);
        condition.wakeOne();
    }
}

bool outputNode() {
    if (prefetch < 1)
        prefetch = numThreads;

	const VSVideoInfo *vi = vsapi->getVideoInfo(node);
	if (y4m && (vi->format->colorFamily != cmGray && vi->format->colorFamily != cmYUV)) {
		errorMessage = "Error: Can only apply y4m headers to YUV and Gray format clips";
		fprintf(stderr, "%s", errorMessage.toUtf8().constData());
        outputError = true;
		return outputError;
	}

    QString y4mFormat;
    QString numBits;

    if (y4m) {
        if (vi->format->colorFamily == cmGray) {
            y4mFormat = "mono";
            if (vi->format->bitsPerSample > 8)
				y4mFormat = y4mFormat + QString::number(vi->format->bitsPerSample);
		} else if (vi->format->colorFamily == cmYUV) {
			if (vi->format->subSamplingW == 1 && vi->format->subSamplingH == 1)
                y4mFormat = "420";
			else if (vi->format->subSamplingW == 1 && vi->format->subSamplingH == 0)
                y4mFormat = "422";
			else if (vi->format->subSamplingW == 0 && vi->format->subSamplingH == 0)
                y4mFormat = "444";
			else if (vi->format->subSamplingW == 2 && vi->format->subSamplingH == 2)
                y4mFormat = "410";
			else if (vi->format->subSamplingW == 2 && vi->format->subSamplingH == 0)
                y4mFormat = "411";
			else if (vi->format->subSamplingW == 0 && vi->format->subSamplingH == 1)
                y4mFormat = "440";
            if (vi->format->bitsPerSample > 8)
                y4mFormat = y4mFormat + "p" + QString::number(vi->format->bitsPerSample);
		}
    }
	if (!y4mFormat.isEmpty())
        y4mFormat = "C" + y4mFormat + " ";

	QString header = "YUV4MPEG2 " + y4mFormat + "W" + QString::number(vi->width) + " H" + QString::number(vi->height) + " F" + QString::number(vi->fpsNum)  + ":" + QString::number(vi->fpsDen)  + " Ip A0:0\n";
    QByteArray rawHeader = header.toUtf8();

    if (y4m) {
        if (!fwrite(rawHeader.constData(), rawHeader.size(), 1, outfile)) {
            errorMessage = "Error: fwrite() call failed";
			fprintf(stderr, "%s", errorMessage.toUtf8().constData());
			outputError = true;
			return outputError;
        }
    }

	QMutexLocker lock(&mutex);

	int intitalRequestSize = std::min(prefetch, totalFrames);
	requestedFrames = intitalRequestSize;
	for (int n = 0; n < intitalRequestSize; n++)
		vsapi->getFrameAsync(n, node, frameDoneCallback, NULL);

	condition.wait(&mutex);

    if (outputError) {
        fprintf(stderr, "%s", errorMessage.toUtf8().constData());
    }

    return outputError;
}

// script output y4m index prefetch
// fixme, handle unicode on windows
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif

    if (argc < 3) {
        fprintf(stderr, "VSPipe\n");
        fprintf(stderr, "Write to stdout: vspipe script.vpy -\n");
        fprintf(stderr, "Write to file: vspipe script.vpy <outfile>\n");
        fprintf(stderr, "Output with y4m headers: vspipe script.vpy <outfile> y4m\n");
        return 1;
    }

    if (!vseval_init()) {
        fprintf(stderr, "Failed to initialize VapourSynth environment\n");
        return 1;
    }

    vsapi = vseval_getVSApi();
    if (vsapi) {
        fprintf(stderr, "Failed to get VapourSynth API\n");
        vseval_finalize();
        return 1;
    }

	QFile scriptFile(QString::fromWCharArray(argv[1]));
    QByteArray scriptData = scriptFile.readAll();
    if (scriptData.isEmpty()) {
        fprintf(stderr, "Failed to read script file or file is empty\n");
        vseval_finalize();
        return 1;
    }

	if (vseval_evaluateScript(&se, scriptData.constData(), QString::fromWCharArray(argv[1]).toUtf8())) {
        fprintf(stderr, "Script evaluation failed:\n%s", vseval_getError(se));
        vseval_freeScript(se);
        vseval_finalize();
        return 1;
    }

    node = vseval_getOutput(se, 0);
    if (!node) {
       fprintf(stderr, "Failed to retrieve output node\n");
       vseval_freeScript(se);
       vseval_finalize();
       return 1;
    }

    const VSCoreInfo *info = vsapi->getCoreInfo(vseval_getCore());
    numThreads = info->numThreads;

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (!isConstantFormat(vi) || vi->numFrames == 0) {
        fprintf(stderr, "Cannot output clips with varying dimensions or unknown length\n");
        vseval_freeScript(se);
        vseval_finalize();
        return 1;
    }

    outfile = stdout;




    vseval_freeScript(se);
    vseval_finalize();
}

/*
VapourSynthFile::~VapourSynthFile() {
    if (vi) {
        vi = NULL;
		while (pending_requests > 0) {};
		vseval_freeScript(se);
    }
}
*/
