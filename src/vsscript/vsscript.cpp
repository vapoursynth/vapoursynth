#include "VSScript.h"
#include "vapoursynth_api.h"

struct VSScript : public VPYScriptExport {
};

int initializationCount = 0;
int scriptId = 1000;
PyThreadState *ts = NULL;

VS_API(int) vseval_init(void) {
	if (initializationCount == 0) {
		Py_InitializeEx(0);
		int result = import_vapoursynth();
		if (result)
			return 0;
		ts = PyEval_SaveThread();
		vpy_initVSScript();
	}
	initializationCount++;
    return initializationCount;
}

VS_API(int) vseval_finalize(void) {
	initializationCount--;
    if (initializationCount)
        return initializationCount;
	PyEval_RestoreThread(ts);
    Py_Finalize();
    return 0;
}

VS_API(int) vseval_evaluateScript(VSScript **handle, const char *script, const char *errorFilename) {
    if (*handle == NULL) {
        *handle = new VSScript();
        (*handle)->pyenvdict = NULL;
        (*handle)->errstr = NULL;
		(*handle)->id = scriptId++;
    }
    return vpy_evaluateScript(*handle, script, errorFilename);
}

VS_API(void) vseval_freeScript(VSScript *handle) {
	if (handle) {
		vpy_freeScript(handle);
		delete handle;
	}
}

VS_API(const char *) vseval_getError(VSScript *handle) {
    return vpy_getError(handle);
}

VS_API(VSNodeRef *) vseval_getOutput(VSScript *handle, int index) {
	return vpy_getOutput(handle, index);
}

VS_API(void) vseval_clearOutput(VSScript *handle, int index) {
	vpy_clearOutput(handle, index);
}

VS_API(VSCore *) vseval_getCore(VSScript *handle) {
    return vpy_getCore(handle);
}

VS_API(const VSAPI *) vseval_getVSApi(void) {
    return vpy_getVSApi();
}

VS_API(int) vseval_getVariable(VSScript *handle, const char *name, VSMap *dst) {
	return vpy_getVariable(handle, name, dst);
}

VS_API(void) vseval_setVariable(VSScript *handle, const VSMap *vars) {
	vpy_setVariable(handle, (VSMap *)vars);
}

VS_API(int) vseval_clearVariable(VSScript *handle, const char *name) {
	return vpy_clearVariable(handle, name);
}

VS_API(void) vseval_clearEnvironment(VSScript *handle) {
    vpy_clearEnvironment(handle);
}
