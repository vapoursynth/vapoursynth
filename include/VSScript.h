/*
* Copyright (c) 2013 Fredrik Mellbin
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

#include "VapourSynth.h"

typedef struct VSScript VSScript;

// Initialize the available scripting runtimes, returns zero on failure
VS_API(int) vseval_init(void);

// Free all scripting runtimes
VS_API(int) vseval_finalize(void);

// Pass a pointer to a null handle to create a new one
// The values returned by the query functions are only valid during the lifetime of the VSScript
// ErrorFilename is if the error message should reference a certain file
// core is to pass in an already created instance so that mixed environments can be used,
// NULL creates a new core that can be fetched with vseval_getCore() later OR implicitly uses the one associated with an already existing handle when passed
VS_API(int) vseval_evaluateScript(VSScript **handle, const char *script, const char *errorFilename);
VS_API(void) vseval_freeScript(VSScript *handle);
VS_API(const char *) vseval_getError(VSScript *handle);
// The node returned must be freed using freeNode() before calling vseval_freeScript()
VS_API(VSNodeRef *) vseval_getOutput(VSScript *handle, int index);
VS_API(void) vseval_clearOutput(VSScript *handle, int index);
// The core is valid as long as the environment exists
VS_API(VSCore *) vseval_getCore(VSScript *handle);
VS_API(const VSAPI *) vseval_getVSApi(void);

// Variables names that are not set or not of a convertible type will return an error
VS_API(int) vseval_getVariable(VSScript *handle, const char *name, VSMap *dst);
VS_API(void) vseval_setVariable(VSScript *handle, const VSMap *vars);
VS_API(int) vseval_clearVariable(VSScript *handle, const char *name);
// Tries to clear everything set in an environment, normally it is better to simply free an environment completely and create a new one
VS_API(void) vseval_clearEnvironment(VSScript *handle);
