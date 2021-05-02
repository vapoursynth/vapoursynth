/*
* Copyright (c) 2013-2020 Fredrik Mellbin
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

#ifndef VSSCRIPT4_H
#define VSSCRIPT4_H

#include "VapourSynth4.h"

#define VSSCRIPT_API_MAJOR 4
#define VSSCRIPT_API_MINOR 0
#define VSSCRIPT_API_VERSION VS_MAKE_VERSION(VSSCRIPT_API_MAJOR, VSSCRIPT_API_MINOR)

typedef struct VSScript VSScript;
typedef struct VSSCRIPTAPI VSSCRIPTAPI;

typedef struct VSScriptOptions {
    /* Must be set to sizeof(VSScriptOptions) */
    int size; 

    /* Passed to createCore(), set to 0 to use the default options */
    int coreCreationFlags; 

    /* Passed to addLogHandler() right after a core is created if messageHandler is not NULL */
    VSLogHandler logHandler;
    VSLogHandlerFree logHandlerFree;
    void *logHandlerData;
} VSScriptOptions;

struct VSSCRIPTAPI {
    /* Returns the higest supported VSSCRIPT_API_VERSION */
    int (VS_CC *getAPIVersion)(void) VS_NOEXCEPT;

    /* Convenience function for retrieving a VSAPI pointer without having to use the VapourSynth library. Always pass VAPOURSYNTH_API_VERSION */
    const VSAPI *(VS_CC *getVSAPI)(int version) VS_NOEXCEPT;

    /*
    * Evaluates a script passed in the buffer argument. The scriptFilename is only used for display purposes. in Python
    * it means that the main module won't be unnamed in error messages.
    *
    * It is possible to set variables in the script context before evaluation by passing a VSMap. This is useful in order
    * to pass on command-line arguments to scripts or handle batch scripting. Only simple types like int, float and data are
    * allowed. Note that the datatype hint may affect how data is handled. Pass NULL to not set any variables.
    *
    * The coreCreationFlags are simply passed on to the createCore() call. This should in most cases be set to 0 to use the defaults.
    *
    * Returns a VSScript pointer both on success and error. Call getError() to see if the script evaluation succeeded.
    * Note that calling any function other than getError() and freeScript() on a VSScript object in the error state
    * will result in undefined behavior.
    */
    VSScript *(VS_CC *evaluateBuffer)(const char *buffer, const char *scriptFilename, const VSMap *vars, const VSScriptOptions *options) VS_NOEXCEPT;

    /* Convenience version of the above function that loads the script from scriptFilename and passes as the buffer to evaluateBuffer */
    VSScript *(VS_CC *evaluateFile)(const char *scriptFilename, const VSMap *vars, const VSScriptOptions *options) VS_NOEXCEPT;

    /* Returns NULL on success, otherwise an error message */
    const char *(VS_CC *getError)(VSScript *handle) VS_NOEXCEPT;

    /* Returns 0 on success, otherwise the exit code */
    int (VS_CC *getExitCode)(VSScript *handle) VS_NOEXCEPT;

    /*
    * The returned nodes must be freed using freeNode() before calling freeScript() since they may depend on data in the VSScript
    * environment. Returns NULL if no node was set as output in the script. Index 0 is used by default in scripts and other
    * values are rarely used.
    */
    VSNode *(VS_CC *getOutputNode)(VSScript *handle, int index) VS_NOEXCEPT;
    VSNode *(VS_CC *getOutputAlphaNode)(VSScript *handle, int index) VS_NOEXCEPT;

    /*
    * Fetches the options set in scripts. In Python this means the set_options() function. Only simple types like int, float
    * and data are allowed as output. Returns zero on success.
    */
    int (VS_CC *getOptions)(VSScript *handle, VSMap *dst);

    /* The core is valid as long as the environment exists, will trigger core creation if necessary and returns NULL on failures */
    VSCore *(VS_CC *getCore)(VSScript *handle) VS_NOEXCEPT;

    /* Unsets the logger specified in VSScriptOptions and does nothing if no logger was set, returns non-zero on success */
    int (VS_CC *clearLogHandler)(VSScript *handle) VS_NOEXCEPT;

    void (VS_CC *freeScript)(VSScript *handle) VS_NOEXCEPT;
};

VS_API(const VSSCRIPTAPI *) getVSScriptAPI(int version) VS_NOEXCEPT;

#endif /* VSSCRIPT4_H */
