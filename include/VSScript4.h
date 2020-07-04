/*
* Copyright (c) 2013-2018 Fredrik Mellbin
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
#define VSSCRIPT_API_VERSION ((VSSCRIPT_API_MAJOR << 16) | (VSSCRIPT_API_MINOR))

typedef struct VSScript VSScript;
typedef struct VSSCRIPTAPI VSSCRIPTAPI;

typedef enum VSEvalFlags {
    efSetWorkingDir = 1,
} VSEvalFlags;

struct VSSCRIPTAPI {
    int (VS_CC *getApiVersion)(void) VS_NOEXCEPT;

    /* Convenience function for retrieving a VSAPI pointer */
    const VSAPI *(VS_CC *getVSApi)(int version) VS_NOEXCEPT;

    /* Initialize the available scripting runtimes, returns zero on failure */
    int (VS_CC *init)(void) VS_NOEXCEPT;

    /* Free all scripting runtimes */
    int (VS_CC *finalize)(void) VS_NOEXCEPT;

    /*
    * Pass a pointer to a null handle to create a new one
    * The values returned by the query functions are only valid during the lifetime of the VSScript
    * scriptFilename is if the error message should reference a certain file, NULL allowed in vsscript_evaluateScript()
    * core is to pass in an already created instance so that mixed environments can be used,
    * NULL creates a new core that can be fetched with vsscript_getCore() later OR implicitly uses the one associated with an already existing handle when passed
    * If efSetWorkingDir is passed to flags the current working directory will be changed to the path of the script
    * note that if scriptFilename is NULL in vsscript_evaluateScript() then __file__ won't be set and the working directory won't be changed
    * Set efSetWorkingDir to get the default and recommended behavior
    */
    int (VS_CC *evaluateScript)(VSScript **handle, const char *script, const char *scriptFilename, int flags) VS_NOEXCEPT;

    /* Convenience version of the above function that loads the script from a file */
    int (VS_CC *evaluateFile)(VSScript **handle, const char *scriptFilename, int flags) VS_NOEXCEPT;

    /* Create an empty environment for use in later invocations, mostly useful to set script variables before execution */
    int (VS_CC *createScript)(VSScript **handle) VS_NOEXCEPT;

    void (VS_CC *freeScript)(VSScript *handle) VS_NOEXCEPT;

    const char *(VS_CC *getError)(VSScript *handle) VS_NOEXCEPT;

    /*
    * Both nodes returned must be freed using freeNode() before calling freeScript()
    * The alpha node pointer will only be set if an alpha clip has been set in the script.
    * Pass NULL to the alpha argument if you're not interested in it.
    */
    VSNodeRef *(VS_CC *getOutput)(VSScript *handle, int index, VSNodeRef **alpha) VS_NOEXCEPT;

    /* Unset an output index */
    int (VS_CC *clearOutput)(VSScript *handle, int index) VS_NOEXCEPT;

    /* The core is valid as long as the environment exists */
    VSCore *(VS_CC *getCore)(VSScript *handle) VS_NOEXCEPT;

    /* Variables names that are not set or not of a convertible type will return an error */
    int (VS_CC *getVariable)(VSScript *handle, const char *name, VSMap *dst) VS_NOEXCEPT;
    int (VS_CC *setVariable)(VSScript *handle, const VSMap *vars) VS_NOEXCEPT;
    int (VS_CC *clearVariable)(VSScript *handle, const char *name) VS_NOEXCEPT;

    /* Tries to clear everything set in an environment, normally it is better to simply free an environment completely and create a new one */
    void (VS_CC *clearEnvironment)(VSScript *handle) VS_NOEXCEPT;
};

VS_API(const VSSCRIPTAPI *) getVSScriptAPI(int version) VS_NOEXCEPT;

#endif /* VSSCRIPT4_H */
