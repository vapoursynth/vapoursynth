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