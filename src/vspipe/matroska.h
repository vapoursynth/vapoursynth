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

#ifndef VSPIPE_MATROSKA_H
#define VSPIPE_MATROSKA_H

#include "VapourSynth4.h"
#include "ebml.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct MatroskaTrackInfo {
    bool isVideo = true;

    /* Video */
    int width = 0;
    int height = 0;
    uint32_t fourCC = 0;
    int64_t frameDurationNum = 0;   // seconds per frame, zero when the rate is unknown
    int64_t frameDurationDen = 1;

    /* Audio */
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    bool isFloat = false;

    /* Used only to write a Duration into the header, which is optional but lets players show a
       length without scanning the whole file. Zero when unknown. */
    int64_t durationNs = 0;
};

class MatroskaWriter {
public:
    /* Writes the EBML header through to the start of the first Cluster. The Segment is opened with
       an unknown size so nothing here has to be revisited later. */
    bool initialize(FILE *outFile, const std::vector<MatroskaTrackInfo> &tracks, std::string &errorMessage);

    /* Frames are written in two steps because the callers hold video as separate planes and would
       otherwise have to assemble a contiguous copy purely to hand it over. The total size has to be
       known up front regardless, since a SimpleBlock is length prefixed. */
    bool writeFrameHeader(int trackIndex, int64_t timestampNs, size_t frameSize, bool keyFrame, std::string &errorMessage);
    void notePayloadWritten(size_t bytes);

    bool finalize(std::string &errorMessage);

    /* Maps a VapourSynth format onto a fourcc describing how its planes are laid out. Every format
       that maps at all maps to a planar layout, so frames are always written through untouched.
       Returns false for formats with no fourcc a reader would recognise. */
    static bool getVideoFourCC(const VSVideoFormat &format, uint32_t &fourCC);
    static bool isAudioFormatSupported(const VSAudioFormat &format);

private:
    bool writeBuffer(const EbmlBuffer &buffer, const char *context, std::string &errorMessage);
    bool startCluster(int64_t timestampNs, std::string &errorMessage);

    /* One entry per cluster, which is the usual granularity: every frame here is a keyframe, so
       cueing them all would produce an index rivalling the headers in size for no added
       precision a seek could use. */
    struct CuePoint {
        int64_t timestamp = 0;
        int trackIndex = 0;
        int64_t clusterPosition = 0;
    };

    FILE *outFile = nullptr;
    int64_t timestampScaleNs = 0;
    int64_t clusterTimestamp = -1;
    int64_t clusterPayloadBytes = 0;
    size_t trackCount = 0;
    bool clusterOpen = false;

    /* An index can only be written when the destination can be seeked back into to record where
       it ended up, so piped output simply goes without one. */
    bool seekable = false;
    int64_t bytesWritten = 0;
    int64_t segmentDataStart = 0;
    int64_t seekHeadPosition = -1;
    int64_t pendingClusterPosition = 0;
    std::vector<CuePoint> cuePoints;
};

#endif
