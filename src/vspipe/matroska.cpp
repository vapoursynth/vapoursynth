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

#include "matroska.h"
#include "../core/version.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {

/* Element ids, each carrying its own length marker as written in the specification. */
constexpr uint32_t idEBML = 0x1A45DFA3;
constexpr uint32_t idEBMLVersion = 0x4286;
constexpr uint32_t idEBMLReadVersion = 0x42F7;
constexpr uint32_t idEBMLMaxIDLength = 0x42F2;
constexpr uint32_t idEBMLMaxSizeLength = 0x42F3;
constexpr uint32_t idDocType = 0x4282;
constexpr uint32_t idDocTypeVersion = 0x4287;
constexpr uint32_t idDocTypeReadVersion = 0x4285;

constexpr uint32_t idSegment = 0x18538067;
constexpr uint32_t idInfo = 0x1549A966;
constexpr uint32_t idTimestampScale = 0x2AD7B1;
constexpr uint32_t idDuration = 0x4489;
constexpr uint32_t idMuxingApp = 0x4D80;
constexpr uint32_t idWritingApp = 0x5741;

constexpr uint32_t idTracks = 0x1654AE6B;
constexpr uint32_t idTrackEntry = 0xAE;
constexpr uint32_t idTrackNumber = 0xD7;
constexpr uint32_t idTrackUID = 0x73C5;
constexpr uint32_t idTrackType = 0x83;
constexpr uint32_t idFlagLacing = 0x9C;
constexpr uint32_t idDefaultDuration = 0x23E383;
constexpr uint32_t idCodecID = 0x86;

constexpr uint32_t idVideo = 0xE0;
constexpr uint32_t idPixelWidth = 0xB0;
constexpr uint32_t idPixelHeight = 0xBA;
constexpr uint32_t idColourSpace = 0x2EB524;

constexpr uint32_t idAudio = 0xE1;
constexpr uint32_t idSamplingFrequency = 0xB5;
constexpr uint32_t idChannels = 0x9F;
constexpr uint32_t idBitDepth = 0x6264;

constexpr uint32_t idCluster = 0x1F43B675;
constexpr uint32_t idClusterTimestamp = 0xE7;
constexpr uint32_t idSimpleBlock = 0xA3;

constexpr uint32_t idSeekHead = 0x114D9B74;
constexpr uint32_t idSeek = 0x4DBB;
constexpr uint32_t idSeekID = 0x53AB;
constexpr uint32_t idSeekPosition = 0x53AC;
constexpr uint32_t idVoid = 0xEC;

constexpr uint32_t idCues = 0x1C53BB6B;
constexpr uint32_t idCuePoint = 0xBB;
constexpr uint32_t idCueTime = 0xB3;
constexpr uint32_t idCueTrackPositions = 0xB7;
constexpr uint32_t idCueTrack = 0xF7;
constexpr uint32_t idCueClusterPosition = 0xF1;

/* Enough for a SeekHead holding one entry, reserved before the headers are written so the real
   one can be dropped in once the index position is known. Any slack is filled with Void. */
constexpr size_t seekHeadReservedBytes = 64;

constexpr uint64_t trackTypeVideo = 1;
constexpr uint64_t trackTypeAudio = 2;

/* One microsecond. Fine enough that variable frame durations survive the conversion to integer
   ticks essentially intact, while leaving a block able to sit 32 milliseconds away from its
   cluster, which is far more than the interval a new cluster gets opened at anyway. */
constexpr int64_t defaultTimestampScaleNs = 1000;

/* A block stores its timestamp as a signed 16 bit offset from the cluster it lives in, so a new
   cluster has to start before that range is used up. The margin keeps the check simple. */
constexpr int64_t maxClusterRelativeTicks = 30000;

/* Clusters are also bounded by size so that a damaged file stays mostly recoverable, and so that
   readers are not asked to treat an entire stream as one element. */
constexpr int64_t maxClusterPayloadBytes = 4 * 1024 * 1024;

/* Plain ftell and fseek take a long, which is 32 bits on Windows and would give up at two
   gigabytes. Uncompressed video passes that in seconds, so the wide forms are used throughout. */
int64_t filePosition(FILE *f) {
#ifdef VS_TARGET_OS_WINDOWS
    return _ftelli64(f);
#else
    return ftello(f);
#endif
}

bool fileSeek(FILE *f, int64_t position) {
#ifdef VS_TARGET_OS_WINDOWS
    return _fseeki64(f, position, SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(position), SEEK_SET) == 0;
#endif
}

constexpr uint32_t makeFourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
        (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

} // namespace

bool MatroskaWriter::getVideoFourCC(const VSVideoFormat &format, bool hasAlpha, uint32_t &fourCC) {
    int depthCode = 0;
    if (format.sampleType == stFloat)
        depthCode = format.bitsPerSample == 16 ? 17 : 33;
    else
        depthCode = format.bitsPerSample;

    /* An alpha clip is Gray at the same dimensions and depth as the video it belongs to, which is
       exactly a fourth plane of the same geometry, so it is written as one. The fourccs for that
       are the three plane ones with the count raised, and unlike those they do cover eight bits.
       There is no such spelling for Gray video or for the rarer subsamplings. */
    if (hasAlpha) {
        if (format.colorFamily == cfRGB && !format.subSamplingW && !format.subSamplingH) {
            fourCC = makeFourCC('G', '4', 0, static_cast<char>(depthCode));
            return true;
        }

        if (format.colorFamily == cfYUV && format.sampleType == stInteger) {
            bool supportedSubSampling =
                (format.subSamplingW == 1 && format.subSamplingH == 1) ||
                (format.subSamplingW == 1 && format.subSamplingH == 0) ||
                (format.subSamplingW == 0 && format.subSamplingH == 0);
            if (supportedSubSampling) {
                int subSamplingCode = format.subSamplingW * 10 + format.subSamplingH;
                fourCC = makeFourCC('Y', '4', static_cast<char>(subSamplingCode), static_cast<char>(depthCode));
                return true;
            }
        }

        return false;
    }

    /* Layouts VapourSynth already holds exactly the way the fourcc describes are taken first,
       since those frames can go out plane by plane with no conversion at all. */
    if (format.sampleType == stInteger && format.bitsPerSample == 8) {

        if (format.colorFamily == cfGray && !format.subSamplingW && !format.subSamplingH) {
            fourCC = makeFourCC('Y', '8', '0', '0');
            return true;
        }

        if (format.colorFamily == cfYUV) {
            /* VapourSynth orders planes Y, U, V, which is what each of these describes. The V
               first spellings of the same layouts deliberately have no entry. */
            if (format.subSamplingW == 1 && format.subSamplingH == 1) {
                fourCC = makeFourCC('I', '4', '2', '0');
                return true;
            }
            if (format.subSamplingW == 1 && format.subSamplingH == 0) {
                fourCC = makeFourCC('Y', '4', '2', 'B');
                return true;
            }
            if (format.subSamplingW == 2 && format.subSamplingH == 0) {
                fourCC = makeFourCC('Y', '4', '1', 'B');
                return true;
            }
            if (format.subSamplingW == 2 && format.subSamplingH == 2) {
                fourCC = makeFourCC('Y', 'U', 'V', '9');
                return true;
            }
            if (format.subSamplingW == 0 && format.subSamplingH == 0) {
                fourCC = makeFourCC('I', '4', '4', '4');
                return true;
            }
            if (format.subSamplingW == 0 && format.subSamplingH == 1) {
                fourCC = makeFourCC('I', '4', '4', '0');
                return true;
            }
        }
    }

    /* Beyond eight bits there is a general scheme for describing planar layouts, where the last
       byte carries the depth and, for YUV, the third carries the subsampling. It covers every
       depth VapourSynth has, float included, so these all stay planar rather than being packed.
       Depths are the declared ones; the samples themselves sit in the usual containers. */
    if (format.colorFamily == cfRGB && !format.subSamplingW && !format.subSamplingH) {
        /* Written G, B, R rather than VapourSynth's R, G, B, which is a reordering of the plane
           writes and costs nothing. */
        fourCC = makeFourCC('G', '3', 0, static_cast<char>(depthCode));
        return true;
    }

    if (format.sampleType == stInteger && format.bitsPerSample > 8) {
        if (format.colorFamily == cfGray && !format.subSamplingW && !format.subSamplingH) {
            fourCC = makeFourCC('Y', '1', 0, static_cast<char>(depthCode));
            return true;
        }

        if (format.colorFamily == cfYUV) {
            int subSamplingCode = format.subSamplingW * 10 + format.subSamplingH;
            fourCC = makeFourCC('Y', '3', static_cast<char>(subSamplingCode), static_cast<char>(depthCode));
            return true;
        }
    }

    /* Anything left has no raw layout a reader could identify. Interleaving it into one of the
       packed layouts would not help, since the fourccs describing those, P010 and r210 among them,
       are not recognised as raw video either, so such a file would mux cleanly and then fail to
       open. Refusing outright is the more useful answer. */
    return false;
}

bool MatroskaWriter::isAudioFormatSupported(const VSAudioFormat &format) {
    /* A_PCM/INT/LIT covers the integer depths and A_PCM/FLOAT/IEEE the float one, which between
       them is every audio format VapourSynth can produce. */
    if (format.sampleType == stFloat)
        return format.bitsPerSample == 32;
    return format.bitsPerSample == 16 || format.bitsPerSample == 24 || format.bitsPerSample == 32;
}

bool MatroskaWriter::writeBuffer(const EbmlBuffer &buffer, const char *context, std::string &errorMessage) {
    if (!outFile || buffer.empty())
        return true;

    if (fwrite(buffer.data(), 1, buffer.size(), outFile) != buffer.size()) {
        errorMessage = std::string("Error: fwrite() call failed when writing ") + context + ", errno: " + std::to_string(errno);
        return false;
    }

    bytesWritten += static_cast<int64_t>(buffer.size());
    return true;
}

bool MatroskaWriter::initialize(FILE *file, const std::vector<MatroskaTrackInfo> &tracks, std::string &errorMessage) {
    if (tracks.empty()) {
        errorMessage = "Error: Matroska track list is empty";
        return false;
    }

    outFile = file;
    timestampScaleNs = defaultTimestampScaleNs;
    trackCount = tracks.size();
    clusterOpen = false;
    clusterTimestamp = -1;
    clusterPayloadBytes = 0;

    EbmlBuffer header;

    EbmlBuffer ebmlHead;
    ebmlUInt(ebmlHead, idEBMLVersion, 1);
    ebmlUInt(ebmlHead, idEBMLReadVersion, 1);
    ebmlUInt(ebmlHead, idEBMLMaxIDLength, 4);
    ebmlUInt(ebmlHead, idEBMLMaxSizeLength, 8);
    ebmlString(ebmlHead, idDocType, "matroska");
    ebmlUInt(ebmlHead, idDocTypeVersion, 2);
    ebmlUInt(ebmlHead, idDocTypeReadVersion, 2);
    ebmlMaster(header, idEBML, ebmlHead);

    /* Everything past this point lives inside the Segment, which is left open for the rest of the
       file so that no length ever has to be patched in afterwards. */
    ebmlMasterUnknownSize(header, idSegment);

    /* Positions inside a Segment are counted from the first byte after its header. */
    if (!writeBuffer(header, "Matroska header", errorMessage))
        return false;
    segmentDataStart = bytesWritten;
    header.clear();

    /* A file that can be seeked in gets an index, which needs somewhere near the front pointing at
       it. The space is claimed now and filled in once the index has been written and its position
       is known; until then it reads as padding. */
    seekable = outFile && filePosition(outFile) >= 0;
    if (seekable) {
        seekHeadPosition = bytesWritten;
        EbmlBuffer placeholder;
        ebmlPutId(placeholder, idVoid);
        ebmlPutSize(placeholder, seekHeadReservedBytes - 2);
        placeholder.resize(seekHeadReservedBytes, 0);
        if (!writeBuffer(placeholder, "Matroska seek head placeholder", errorMessage))
            return false;
    }

    EbmlBuffer info;
    ebmlUInt(info, idTimestampScale, static_cast<uint64_t>(timestampScaleNs));
    ebmlString(info, idMuxingApp, "VapourSynth R" XSTR(VAPOURSYNTH_CORE_VERSION));
    ebmlString(info, idWritingApp, "vspipe R" XSTR(VAPOURSYNTH_CORE_VERSION));

    int64_t longestNs = 0;
    for (const auto &track : tracks)
        longestNs = std::max(longestNs, track.durationNs);
    if (longestNs > 0)
        ebmlFloat(info, idDuration, static_cast<double>(longestNs) / static_cast<double>(timestampScaleNs));
    ebmlMaster(header, idInfo, info);

    EbmlBuffer trackList;
    for (size_t i = 0; i < tracks.size(); i++) {
        const MatroskaTrackInfo &track = tracks[i];
        EbmlBuffer entry;

        /* Track numbers are one based and are what a block refers to. */
        ebmlUInt(entry, idTrackNumber, i + 1);
        ebmlUInt(entry, idTrackUID, i + 1);
        ebmlUInt(entry, idTrackType, track.isVideo ? trackTypeVideo : trackTypeAudio);
        /* Lacing packs several small frames into one block. Uncompressed frames are far too large
           for that to ever apply, and saying so up front spares readers the check. */
        ebmlUInt(entry, idFlagLacing, 0);

        if (track.isVideo) {
            ebmlString(entry, idCodecID, "V_UNCOMPRESSED");
            if (track.frameDurationNum > 0 && track.frameDurationDen > 0)
                ebmlUInt(entry, idDefaultDuration, static_cast<uint64_t>(1000000000LL * track.frameDurationNum / track.frameDurationDen));

            EbmlBuffer video;
            ebmlUInt(video, idPixelWidth, static_cast<uint64_t>(track.width));
            ebmlUInt(video, idPixelHeight, static_cast<uint64_t>(track.height));
            /* The layout of the frames is carried entirely by this fourcc, stored little endian
               the same way the equivalent AVI field is. */
            uint8_t colourSpace[4] = {
                static_cast<uint8_t>(track.fourCC),
                static_cast<uint8_t>(track.fourCC >> 8),
                static_cast<uint8_t>(track.fourCC >> 16),
                static_cast<uint8_t>(track.fourCC >> 24)
            };
            ebmlBinary(video, idColourSpace, colourSpace, sizeof(colourSpace));
            ebmlMaster(entry, idVideo, video);
        } else {
            ebmlString(entry, idCodecID, track.isFloat ? "A_PCM/FLOAT/IEEE" : "A_PCM/INT/LIT");

            EbmlBuffer audio;
            ebmlFloat(audio, idSamplingFrequency, static_cast<double>(track.sampleRate));
            ebmlUInt(audio, idChannels, static_cast<uint64_t>(track.channels));
            ebmlUInt(audio, idBitDepth, static_cast<uint64_t>(track.bitsPerSample));
            ebmlMaster(entry, idAudio, audio);
        }

        ebmlMaster(trackList, idTrackEntry, entry);
    }
    ebmlMaster(header, idTracks, trackList);

    return writeBuffer(header, "Matroska header", errorMessage);
}

bool MatroskaWriter::startCluster(int64_t timestampNs, std::string &errorMessage) {
    /* Recorded before the cluster is written, since a cue has to point at the element itself. */
    pendingClusterPosition = bytesWritten - segmentDataStart;

    EbmlBuffer cluster;
    /* Left unsized like the Segment. A reader ends it when it meets the next cluster or the end of
       the file, which is what makes writing to a pipe possible. */
    ebmlMasterUnknownSize(cluster, idCluster);
    ebmlUInt(cluster, idClusterTimestamp, static_cast<uint64_t>(timestampNs / timestampScaleNs));

    if (!writeBuffer(cluster, "Matroska cluster header", errorMessage))
        return false;

    clusterTimestamp = timestampNs / timestampScaleNs;
    clusterPayloadBytes = 0;
    clusterOpen = true;
    return true;
}

bool MatroskaWriter::writeFrameHeader(int trackIndex, int64_t timestampNs, size_t frameSize, bool keyFrame, std::string &errorMessage) {
    if (trackIndex < 0 || static_cast<size_t>(trackIndex) >= trackCount) {
        errorMessage = "Error: invalid Matroska track index";
        return false;
    }
    if (timestampNs < 0) {
        errorMessage = "Error: negative Matroska timestamp is not supported";
        return false;
    }

    const int64_t ticks = timestampNs / timestampScaleNs;

    /* A new cluster is needed once the block can no longer reach back to the current one, or once
       the current one has grown past the size a reader should have to buffer. */
    if (!clusterOpen || ticks - clusterTimestamp > maxClusterRelativeTicks || clusterPayloadBytes >= maxClusterPayloadBytes) {
        if (!startCluster(timestampNs, errorMessage))
            return false;
        /* The frame that opened the cluster is the one a seek to this point would land on. */
        if (seekable)
            cuePoints.push_back({ ticks, trackIndex, pendingClusterPosition });
    }

    const int64_t relative = ticks - clusterTimestamp;
    if (relative < -32768 || relative > 32767) {
        errorMessage = "Error: Matroska block timestamp is out of range for its cluster";
        return false;
    }

    EbmlBuffer block;
    EbmlBuffer payloadHeader;
    /* Track number, then the offset from the cluster, then the flags, and then the frame. */
    ebmlPutSize(payloadHeader, static_cast<uint64_t>(trackIndex + 1));
    payloadHeader.push_back(static_cast<uint8_t>(static_cast<uint16_t>(relative) >> 8));
    payloadHeader.push_back(static_cast<uint8_t>(static_cast<uint16_t>(relative)));
    payloadHeader.push_back(static_cast<uint8_t>(keyFrame ? 0x80 : 0x00));

    ebmlPutId(block, idSimpleBlock);
    ebmlPutSize(block, payloadHeader.size() + frameSize);
    block.insert(block.end(), payloadHeader.begin(), payloadHeader.end());

    if (!writeBuffer(block, "Matroska block header", errorMessage))
        return false;

    clusterPayloadBytes += static_cast<int64_t>(block.size());
    return true;
}

void MatroskaWriter::notePayloadWritten(size_t bytes) {
    clusterPayloadBytes += static_cast<int64_t>(bytes);
}

bool MatroskaWriter::finalize(std::string &errorMessage) {
    /* Both the Segment and the last Cluster were opened with an unknown size, so reaching the end
       of the file closes them. Only the index remains, and only when there is somewhere to put a
       pointer to it. */
    if (!seekable || !outFile || cuePoints.empty())
        return true;

    const int64_t cuesPosition = bytesWritten - segmentDataStart;

    EbmlBuffer entries;
    for (const auto &cue : cuePoints) {
        EbmlBuffer positions;
        ebmlUInt(positions, idCueTrack, static_cast<uint64_t>(cue.trackIndex + 1));
        ebmlUInt(positions, idCueClusterPosition, static_cast<uint64_t>(cue.clusterPosition));

        EbmlBuffer point;
        ebmlUInt(point, idCueTime, static_cast<uint64_t>(cue.timestamp));
        ebmlMaster(point, idCueTrackPositions, positions);
        ebmlMaster(entries, idCuePoint, point);
    }

    EbmlBuffer cues;
    ebmlMaster(cues, idCues, entries);
    if (!writeBuffer(cues, "Matroska cues", errorMessage))
        return false;

    /* Now go back and turn the reserved padding into a real pointer at the index. */
    EbmlBuffer seekEntry;
    uint8_t cuesId[4] = { 0x1C, 0x53, 0xBB, 0x6B };
    ebmlBinary(seekEntry, idSeekID, cuesId, sizeof(cuesId));
    ebmlUInt(seekEntry, idSeekPosition, static_cast<uint64_t>(cuesPosition));

    EbmlBuffer seekEntries;
    ebmlMaster(seekEntries, idSeek, seekEntry);

    EbmlBuffer seekHead;
    ebmlMaster(seekHead, idSeekHead, seekEntries);

    /* Whatever is left of the reservation becomes a Void, which readers skip. Two bytes is the
       smallest a Void can be, so the reservation is sized to always leave at least that. */
    if (seekHead.size() + 2 > seekHeadReservedBytes) {
        errorMessage = "Error: Matroska seek head does not fit its reservation";
        return false;
    }
    /* Measured before the Void header goes on, since appending it would otherwise be counted
       against the payload it is about to describe. */
    const size_t voidPayload = seekHeadReservedBytes - seekHead.size() - 2;
    ebmlPutId(seekHead, idVoid);
    ebmlPutSize(seekHead, voidPayload);
    seekHead.resize(seekHeadReservedBytes, 0);

    const int64_t endPosition = filePosition(outFile);
    if (endPosition < 0 || !fileSeek(outFile, seekHeadPosition)) {
        errorMessage = "Error: failed to seek back to write the Matroska index pointer";
        return false;
    }
    if (fwrite(seekHead.data(), 1, seekHead.size(), outFile) != seekHead.size()) {
        errorMessage = "Error: fwrite() call failed when writing the Matroska index pointer, errno: " + std::to_string(errno);
        return false;
    }
    if (!fileSeek(outFile, endPosition)) {
        errorMessage = "Error: failed to seek back to the end of the Matroska file";
        return false;
    }

    return true;
}
