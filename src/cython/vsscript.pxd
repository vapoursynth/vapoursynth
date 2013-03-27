#
# Copyright (c) 2013 Fredrik Mellbin
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

from vapoursynth cimport VSAPI, VSCore, VSMap, VSNodeRef

cdef extern from "include/VSScript.h" nogil:
    ctypedef struct VSScript:
        pass
    ctypedef VSScript *VSScriptHandle
 
    int vseval_init() nogil
    int vseval_finalize() nogil

    int vseval_evaluatePythonScript(VSScriptHandle *handle, char *script, char *errorFilename, VSCore *core) nogil
    void vseval_freeScript(VSScriptHandle handle) nogil
    char *vseval_getError(VSScriptHandle handle) nogil
    VSNodeRef *vseval_getOutput(VSScriptHandle handle) nogil
    void vseval_clearOutput(VSScriptHandle handle) nogil
    VSCore *vseval_getCore(VSScriptHandle handle) nogil
    VSAPI *vseval_getVSApi(VSScriptHandle handle) nogil

    int vseval_getVariable(VSScriptHandle handle, char *name, VSMap *dst) nogil
    void vseval_setVariables(VSScriptHandle handle, VSMap *vars) nogil
