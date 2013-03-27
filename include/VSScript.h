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

#include "vapoursynth.h"

typedef struct VSScript VSScript;
typedef VSScript *VSScriptHandle;

// Initialize the available scripting runtimes, returns non-zero on failure
VS_API(int) vseval_init(void);

// Free all scripting runtimes, returns non-zero on failure (such as scripts still open and everything will now crash)
VS_API(int) vseval_finalize(void);

// Pass a pointer to a null handle to create a new one
// The values returned by the query functions are only valid during the lifetime of the VSScriptHandle
// ErrorFilename is if the error message should reference a certain file
// core is to pass in an already created instance so that mixed environments can be used,
// NULL creates a new core that can be fetched with vseval_getCore() later OR implicitly uses the one associated with an already existing handle when passed
VS_API(int) vseval_evaluatePythonScript(VSScriptHandle *handle, const char *script, const char *errorFilename, VSCore *core);
VS_API(void) vseval_freeScript(VSScriptHandle handle);
VS_API(const char *) vseval_getError(VSScriptHandle handle);
VS_API(VSNodeRef *) vseval_getOutput(VSScriptHandle handle);
VS_API(void) vseval_clearOutput(VSScriptHandle handle);
VS_API(VSCore *) vseval_getCore(VSScriptHandle handle);
VS_API(const VSAPI *) vseval_getVSApi(VSScriptHandle handle);

// Variables names that are not set or not of a convertible type
VS_API(int) vseval_getVariable(VSScriptHandle handle, const char *name, VSMap *dst);
VS_API(void) vseval_setVariables(VSScriptHandle handle, const VSMap *vars);

