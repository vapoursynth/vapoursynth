#
# Copyright (c) 2021 Fredrik Mellbin
#
# This file is part of VapourSynth.
#
# VapourSynth is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# VapourSynth is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with VapourSynth; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

cdef extern from "include/VSConstants4.h" nogil:
    cpdef enum ColorRange "VSColorRange":
        RANGE_FULL "VSC_RANGE_FULL"
        RANGE_LIMITED "VSC_RANGE_LIMITED"
        
    cpdef enum ChromaLocation "VSChromaLocation":
        CHROMA_LEFT "VSC_CHROMA_LEFT"
        CHROMA_CENTER "VSC_CHROMA_CENTER"
        CHROMA_TOP_LEFT "VSC_CHROMA_TOP_LEFT"
        CHROMA_TOP "VSC_CHROMA_TOP"
        CHROMA_BOTTOM_LEFT "VSC_CHROMA_BOTTOM_LEFT"
        CHROMA_BOTTOM "VSC_CHROMA_BOTTOM"
        
    cpdef enum FieldBased "VSFieldBased":
        FIELD_PROGRESSIVE "VSC_FIELD_PROGRESSIVE"
        FIELD_TOP "VSC_FIELD_TOP"
        FIELD_BOTTOM "VSC_FIELD_BOTTOM"
        
    cpdef enum MatrixCoefficients "VSMatrixCoefficients":
        MATRIX_RGB "VSC_MATRIX_RGB"
        MATRIX_BT709 "VSC_MATRIX_BT709"
        MATRIX_UNSPECIFIED "VSC_MATRIX_UNSPECIFIED"
        MATRIX_FCC "VSC_MATRIX_FCC"
        MATRIX_BT470_BG "VSC_MATRIX_BT470_BG"
        MATRIX_ST170_M "VSC_MATRIX_ST170_M"
        MATRIX_ST240_M "VSC_MATRIX_ST240_M"
        MATRIX_YCGCO "VSC_MATRIX_YCGCO"
        MATRIX_BT2020_NCL "VSC_MATRIX_BT2020_NCL"
        MATRIX_BT2020_CL "VSC_MATRIX_BT2020_CL"
        MATRIX_CHROMATICITY_DERIVED_NCL "VSC_MATRIX_CHROMATICITY_DERIVED_NCL"
        MATRIX_CHROMATICITY_DERIVED_CL "VSC_MATRIX_CHROMATICITY_DERIVED_CL"
        MATRIX_ICTCP "VSC_MATRIX_ICTCP"
        
    cpdef enum TransferCharacteristics "VSTransferCharacteristics":
        TRANSFER_BT709 "VSC_TRANSFER_BT709"
        TRANSFER_UNSPECIFIED "VSC_TRANSFER_UNSPECIFIED"
        TRANSFER_BT470_M "VSC_TRANSFER_BT470_M"
        TRANSFER_BT470_BG "VSC_TRANSFER_BT470_BG"
        TRANSFER_BT601 "VSC_TRANSFER_BT601"
        TRANSFER_ST240_M "VSC_TRANSFER_ST240_M"
        TRANSFER_LINEAR "VSC_TRANSFER_LINEAR"
        TRANSFER_LOG_100 "VSC_TRANSFER_LOG_100"
        TRANSFER_LOG_316 "VSC_TRANSFER_LOG_316"
        TRANSFER_IEC_61966_2_4 "VSC_TRANSFER_IEC_61966_2_4"
        TRANSFER_IEC_61966_2_1 "VSC_TRANSFER_IEC_61966_2_1"
        TRANSFER_BT2020_10 "VSC_TRANSFER_BT2020_10"
        TRANSFER_BT2020_12 "VSC_TRANSFER_BT2020_12"
        TRANSFER_ST2084 "VSC_TRANSFER_ST2084"
        TRANSFER_ARIB_B67 "VSC_TRANSFER_ARIB_B67"
        
    cpdef enum ColorPrimaries "VSColorPrimaries":
        PRIMARIES_BT709 "VSC_PRIMARIES_BT709"
        PRIMARIES_UNSPECIFIED "VSC_PRIMARIES_UNSPECIFIED"
        PRIMARIES_BT470_M "VSC_PRIMARIES_BT470_M"
        PRIMARIES_BT470_BG "VSC_PRIMARIES_BT470_BG"
        PRIMARIES_ST170_M "VSC_PRIMARIES_ST170_M"
        PRIMARIES_ST240_M "VSC_PRIMARIES_ST240_M"
        PRIMARIES_FILM "VSC_PRIMARIES_FILM"
        PRIMARIES_BT2020 "VSC_PRIMARIES_BT2020"
        PRIMARIES_ST428 "VSC_PRIMARIES_ST428"
        PRIMARIES_ST431_2 "VSC_PRIMARIES_ST431_2"
        PRIMARIES_ST432_1 "VSC_PRIMARIES_ST432_1"
        PRIMARIES_EBU3213_E "VSC_PRIMARIES_EBU3213_E"
        