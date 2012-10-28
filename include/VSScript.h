#include "vapoursynth.h"

typedef struct {
    int size;
    VSAPI *vsapi;
    VSNodeRef *node;
    char *error;
    int num_threads;
    int pad_scanlines;
    int enable_v210;
} ScriptExport;

typedef enum {
    seDefault = -1,
    sePython = 0
} ScriptEngine;

// FIXME, missing variable injection to speed things up
// cannot take the output of one script and feed into the next one
// cannot specify which variables to read out


// Initialize the available scripting runtimes, returns non-zero on failure
VS_API(int) initVSScript(void);

// Free all scripting runtimes, returns non-zero on failure (scripts still open)
VS_API(int) freeVSScript(void);

// Evaluate a given piece of text as a script, the filename is just symbolic and will be used to make
// the errors reported not be in a "mystery file". The script engine to use must also be specified.
// Returns 0 on success, below 0 if the script engine is invalid and above 0 if there is an error
// evaluating the script.
VS_API(int) evaluateText(ScriptExport *se, const char *text, const char *filename, int scriptEngine);

// Evaluate a file with
// Returns 0 on success, below 0 if the script engine is invalid and above 0 if there is an error
// evaluating the script.
VS_API(int) evaluateFile(ScriptExport *se, const char *filename, int scriptEngine);
VS_API(int) freeScript(ScriptExport *se);
