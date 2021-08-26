/*
* Copyright (c) 2021 Fredrik Mellbin
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

#ifndef VSCONSTANTS4_H
#define VSCONSTANTS4_H

typedef enum VSColorRange {
	VSC_RANGE_FULL = 0,
	VSC_RANGE_LIMITED = 1
} VSColorRange;

typedef enum VSChromaLocation {
	VSC_CHROMA_LEFT = 0,
	VSC_CHROMA_CENTER = 1,
	VSC_CHROMA_TOP_LEFT = 2,
	VSC_CHROMA_TOP = 3,
	VSC_CHROMA_BOTTOM_LEFT = 4,
	VSC_CHROMA_BOTTOM = 5
} VSChromaLocation;

typedef enum VSFieldBased {
	VSC_FIELD_PROGRESSIVE = 0,
	VSC_FIELD_BOTTOM = 1,
	VSC_FIELD_TOP = 2
} VSFieldBased;

typedef enum VSMatrixCoefficients {
	VSC_MATRIX_RGB = 0,
	VSC_MATRIX_BT709 = 1,
	VSC_MATRIX_UNSPECIFIED = 2,
	VSC_MATRIX_FCC = 4,
	VSC_MATRIX_BT470_BG = 5,
	VSC_MATRIX_ST170_M = 6,  /* Equivalent to 5. */
	VSC_MATRIX_ST240_M = 7,
	VSC_MATRIX_YCGCO = 8,
	VSC_MATRIX_BT2020_NCL = 9,
	VSC_MATRIX_BT2020_CL = 10,
	VSC_MATRIX_CHROMATICITY_DERIVED_NCL = 12,
	VSC_MATRIX_CHROMATICITY_DERIVED_CL = 13,
	VSC_MATRIX_ICTCP = 14
} VSMatrixCoefficients;

typedef enum VSTransferCharacteristics {
	VSC_TRANSFER_BT709 = 1,
	VSC_TRANSFER_UNSPECIFIED = 2,
	VSC_TRANSFER_BT470_M = 4,
	VSC_TRANSFER_BT470_BG = 5,
	VSC_TRANSFER_BT601 = 6,  /* Equivalent to 1. */
	VSC_TRANSFER_ST240_M = 7,
	VSC_TRANSFER_LINEAR = 8,
	VSC_TRANSFER_LOG_100 = 9,
	VSC_TRANSFER_LOG_316 = 10,
	VSC_TRANSFER_IEC_61966_2_4 = 11,
	VSC_TRANSFER_IEC_61966_2_1 = 13,
	VSC_TRANSFER_BT2020_10 = 14, /* Equivalent to 1. */
	VSC_TRANSFER_BT2020_12 = 15, /* Equivalent to 1. */
	VSC_TRANSFER_ST2084 = 16,
	VSC_TRANSFER_ARIB_B67 = 18
} VSTransferCharacteristics;

typedef enum VSColorPrimaries {
	VSC_PRIMARIES_BT709 = 1,
	VSC_PRIMARIES_UNSPECIFIED = 2,
	VSC_PRIMARIES_BT470_M = 4,
	VSC_PRIMARIES_BT470_BG = 5,
	VSC_PRIMARIES_ST170_M = 6,
	VSC_PRIMARIES_ST240_M = 7,  /* Equivalent to 6. */
	VSC_PRIMARIES_FILM = 8,
	VSC_PRIMARIES_BT2020 = 9,
	VSC_PRIMARIES_ST428 = 10,
	VSC_PRIMARIES_ST431_2 = 11,
	VSC_PRIMARIES_ST432_1 = 12,
	VSC_PRIMARIES_EBU3213_E = 22
} VSColorPrimaries;

#endif /* VSCONSTANTS4_H */
