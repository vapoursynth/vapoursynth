#include "VSScript.h"
#include "vapoursynthpp_api.h"
#include <map>
#include <utility>

struct VSScript : public VPYScriptExport {
};

int initializationCount = 0;
PyThreadState *ts = NULL;

VS_API(int) vseval_init(void) {
	if (initializationCount == 0)
	{
		Py_Initialize();
		int result = import_vapoursynth();
		if (result)
			return 0;
		ts = PyEval_SaveThread();
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
    if (*handle == NULL)
    {
        *handle = new VSScript();
        (*handle)->pyenvdict = NULL;
        (*handle)->errstr = NULL;
    }
    return vpy_evaluateScript(*handle, script, errorFilename);
}

VS_API(void) vseval_freeScript(VSScript *handle) {
    vpy_freeScript(handle);
    delete handle;
}

VS_API(const char *) vseval_getError(VSScript *handle) {
    return vpy_getError(handle);
}

VS_API(VSNodeRef *) vseval_getOutput(VSScript *handle, int index) {
	return vpy_getOutput(handle, index);
}

VS_API(void) vseval_clearOutput(VSScript *handle, int index) {

}

VS_API(VSCore *) vseval_getCore(void) {
    return vpy_getCore();
}

VS_API(const VSAPI *) vseval_getVSApi(void) {
    return vpy_getVSApi();
}

VS_API(int) vseval_getVariable(VSScript *handle, const char *name, VSMap *dst) {
	return 0;
}

VS_API(void) vseval_setVariable(VSScript *handle, const VSMap *vars) {

}

VS_API(int) vseval_clearVariable(VSScript *handle, const char *name) {
	return 0;
}

VS_API(void) vseval_clearEnvironment(VSScript *handle) {
    vpy_clearEnvironment(handle);
}
