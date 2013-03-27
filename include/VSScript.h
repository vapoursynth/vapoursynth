#include "vapoursynth.h"

typedef struct VSScript VSScript;
typedef VSScript *VSScriptHandle;

// FIXME, missing variable injection to speed things up
// cannot take the output of one script and feed into the next one
// cannot specify which variables to read out


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

