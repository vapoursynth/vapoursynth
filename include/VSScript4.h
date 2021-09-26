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
#define VSSCRIPT_API_MINOR 1
#define VSSCRIPT_API_VERSION VS_MAKE_VERSION(VSSCRIPT_API_MAJOR, VSSCRIPT_API_MINOR)

typedef struct VSScript VSScript;
typedef struct VSSCRIPTAPI VSSCRIPTAPI;

struct VSSCRIPTAPI {
    /* Returns the highest supported VSSCRIPT_API_VERSION */
    int (VS_CC *getAPIVersion)(void) VS_NOEXCEPT;

    /* Convenience function for retrieving a VSAPI pointer without having to use the VapourSynth library. Always pass VAPOURSYNTH_API_VERSION */
    const VSAPI *(VS_CC *getVSAPI)(int version) VS_NOEXCEPT;

    /* 
    * Providing a pre-created core is useful for setting core creation flags, log callbacks, preload specific plugins and many other things.
    * You must create a VSScript object before evaluating a script. Always takes ownership of the core even on failure. Returns NULL on failure.
    * Pass NULL to have a core automatically created with the default options.
    */
    VSScript *(VS_CC *createScript)(VSCore *core) VS_NOEXCEPT;

    /* The core is valid as long as the environment exists, return NULL on error */
    VSCore *(VS_CC *getCore)(VSScript *handle) VS_NOEXCEPT;

    /*
    * Evaluates a script passed in the buffer argument. The scriptFilename is only used for display purposes. in Python
    * it means that the main module won't be unnamed in error messages.
    * 
    * Returns 0 on success.
    * 
    * Note that calling any function other than getError() and freeScript() on a VSScript object in the error state
    * will result in undefined behavior.
    */
    int (VS_CC *evaluateBuffer)(VSScript *handle, const char *buffer, const char *scriptFilename) VS_NOEXCEPT;

    /* Convenience version of the above function that loads the script from scriptFilename and passes as the buffer to evaluateBuffer */
    int (VS_CC *evaluateFile)(VSScript *handle, const char *scriptFilename) VS_NOEXCEPT;

    /* Returns NULL on success, otherwise an error message */
    const char *(VS_CC *getError)(VSScript *handle) VS_NOEXCEPT;

    /* Returns the script's reported exit code */
    int (VS_CC *getExitCode)(VSScript *handle) VS_NOEXCEPT;

    /* Fetches a variable of any VSMap storable type set in a script. It is stored in the key with the same name in dst. Returns 0 on success. */
    int (VS_CC *getVariable)(VSScript *handle, const char *name, VSMap *dst) VS_NOEXCEPT;

    /* Sets all keys in the provided VSMap as variables in the script. Returns 0 on success. */
    int (VS_CC *setVariables)(VSScript *handle, const VSMap *vars) VS_NOEXCEPT;

    /*
    * The returned nodes must be freed using freeNode() before calling freeScript() since they may depend on data in the VSScript
    * environment. Returns NULL if no node was set as output in the script. Index 0 is used by default in scripts and other
    * values are rarely used.
    */
    VSNode *(VS_CC *getOutputNode)(VSScript *handle, int index) VS_NOEXCEPT;
    VSNode *(VS_CC *getOutputAlphaNode)(VSScript *handle, int index) VS_NOEXCEPT;
    int (VS_CC *getAltOutputMode)(VSScript *handle, int index) VS_NOEXCEPT;

    void (VS_CC *freeScript)(VSScript *handle) VS_NOEXCEPT;

    /*
    * Set whether or not the working directory is temporarily changed to the same
    * location as the script file when evaluateFile is called. Off by default.
    */
    void (VS_CC *evalSetWorkingDir)(VSScript *handle, int setCWD) VS_NOEXCEPT;

};

VS_API(const VSSCRIPTAPI *) getVSScriptAPI(int version) VS_NOEXCEPT;

#endif /* VSSCRIPT4_H */
