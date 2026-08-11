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

#ifndef RESIZESHARED_H
#define RESIZESHARED_H

/* Everything the scalar zimg wrapper and the GPU compute path share: the argument
   vocabulary below, and the one entry point vsresize.cpp calls to try the GPU first.

   The string spellings the resize entry points accept are shared so the two cannot drift
   apart -- a name added to one and not the other would make the same script mean different
   things depending on where the clip lives.

   Values are the H.273 codes the frame properties use, which are numerically identical
   to zimg's own enums for every table here, range included: VSRange under the current
   API numbers full 1 and limited 0 exactly as zimg does. vsresize.cpp asserts that
   identity entry by entry at compile time before casting across the two domains.

   Deliberately absent: the cpu, dither and resample filter tables, which are statements
   about the scalar implementation rather than shared vocabulary. */

#include <string>
#include <string_view>
#include "VSConstants4.h"
#include "VapourSynth4.h"

/* Builds a GPU resize node into out and returns true, or returns false with the decline
   reason after touching nothing, in which case the caller runs the scalar graph. */
bool createGPUResize(const VSMap *in, VSMap *out, const char *kernelName, bool deinterlace,
    VSCore *core, const VSAPI *vsapi, std::string &decline);

struct ResizeEnumEntry {
    std::string_view name;
    int value;
};

template<size_t N>
const int *findResizeEnum(const ResizeEnumEntry (&table)[N], std::string_view name) {
    for (const ResizeEnumEntry &entry : table) {
        if (entry.name == name)
            return &entry.value;
    }
    return nullptr;
}

constexpr ResizeEnumEntry resizeRangeTable[] = {
    { "limited", VSC_RANGE_LIMITED },
    { "full",    VSC_RANGE_FULL },
};

constexpr ResizeEnumEntry resizeChromaLocTable[] = {
    { "left",        VSC_CHROMA_LEFT },
    { "center",      VSC_CHROMA_CENTER },
    { "top_left",    VSC_CHROMA_TOP_LEFT },
    { "top",         VSC_CHROMA_TOP },
    { "bottom_left", VSC_CHROMA_BOTTOM_LEFT },
    { "bottom",      VSC_CHROMA_BOTTOM },
};

constexpr ResizeEnumEntry resizeMatrixTable[] = {
    { "rgb",       VSC_MATRIX_RGB },
    { "709",       VSC_MATRIX_BT709 },
    { "unspec",    VSC_MATRIX_UNSPECIFIED },
    { "170m",      VSC_MATRIX_ST170_M },
    { "240m",      VSC_MATRIX_ST240_M },
    { "470bg",     VSC_MATRIX_BT470_BG },
    { "fcc",       VSC_MATRIX_FCC },
    { "ycgco",     VSC_MATRIX_YCGCO },
    { "2020ncl",   VSC_MATRIX_BT2020_NCL },
    { "2020cl",    VSC_MATRIX_BT2020_CL },
    { "chromacl",  VSC_MATRIX_CHROMATICITY_DERIVED_CL },
    { "chromancl", VSC_MATRIX_CHROMATICITY_DERIVED_NCL },
    { "ictcp",     VSC_MATRIX_ICTCP },
};

constexpr ResizeEnumEntry resizeTransferTable[] = {
    { "709",     VSC_TRANSFER_BT709 },
    { "unspec",  VSC_TRANSFER_UNSPECIFIED },
    { "601",     VSC_TRANSFER_BT601 },
    { "linear",  VSC_TRANSFER_LINEAR },
    { "2020_10", VSC_TRANSFER_BT2020_10 },
    { "2020_12", VSC_TRANSFER_BT2020_12 },
    { "240m",    VSC_TRANSFER_ST240_M },
    { "470m",    VSC_TRANSFER_BT470_M },
    { "470bg",   VSC_TRANSFER_BT470_BG },
    { "log100",  VSC_TRANSFER_LOG_100 },
    { "log316",  VSC_TRANSFER_LOG_316 },
    { "st2084",  VSC_TRANSFER_ST2084 },
    { "std-b67", VSC_TRANSFER_ARIB_B67 },
    { "st428",   VSC_TRANSFER_ST428 },
    { "srgb",    VSC_TRANSFER_IEC_61966_2_1 },
    { "xvycc",   VSC_TRANSFER_IEC_61966_2_4 },
};

constexpr ResizeEnumEntry resizePrimariesTable[] = {
    { "709",       VSC_PRIMARIES_BT709 },
    { "unspec",    VSC_PRIMARIES_UNSPECIFIED },
    { "170m",      VSC_PRIMARIES_ST170_M },
    { "240m",      VSC_PRIMARIES_ST240_M },
    { "470m",      VSC_PRIMARIES_BT470_M },
    { "470bg",     VSC_PRIMARIES_BT470_BG },
    { "film",      VSC_PRIMARIES_FILM },
    { "2020",      VSC_PRIMARIES_BT2020 },
    { "st428",     VSC_PRIMARIES_ST428 },
    /* zimg's own table spells the XYZ primaries both ways. */
    { "xyz",       VSC_PRIMARIES_ST428 },
    { "st431-2",   VSC_PRIMARIES_ST431_2 },
    { "st432-1",   VSC_PRIMARIES_ST432_1 },
    { "ebu3213-e", VSC_PRIMARIES_EBU3213_E },
};

#endif // RESIZESHARED_H
