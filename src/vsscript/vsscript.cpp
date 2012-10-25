#include "VSScript.h"
#include "vapoursynthpp_api.h"
#include <map>
#include <utility>

typedef std::pair<int, void *> SRec;
typedef std::map<ScriptExport *, SRec> SMap;

SMap openScripts;

VS_API(int) initVSScript(void) {
    Py_Initialize();
    return import_vapoursynth();
}

VS_API(int) freeVSScript(void) {
    if (!openScripts.empty())
        return -1;
    Py_Finalize();
    return 0;
}

VS_API(int) evaluateText(ScriptExport *se, const char *text, const char *filename, int scriptEngine) {
    if (scriptEngine == sePython) {
        VPYScriptExport *vpy = new VPYScriptExport();
        int res = vpy_evaluate_text((char *)text, (char *)filename, vpy);
        openScripts.insert(std::pair<ScriptExport *, SRec>(se, SRec(scriptEngine, (void *)vpy)));
        se->vsapi = vpy->vsapi;
        se->node = vpy->node;
        se->error = vpy->error;
        se->num_threads = vpy->num_threads;
        se->pad_scanlines = vpy->pad_scanlines;
        se->enable_v210 = vpy->enable_v210;
        return res;
    } else {
        return -1;
    }
}

VS_API(int) evaluateFile(ScriptExport *se, const char *filename, int scriptEngine) {
    if (scriptEngine == seDefault) {
        std::string fn = filename;
    }

    if (scriptEngine == sePython) {
        VPYScriptExport *vpy = new VPYScriptExport();
        int res = vpy_evaluate_file((char *)filename, vpy);
        openScripts.insert(std::pair<ScriptExport *, SRec>(se, SRec(scriptEngine, (void *)vpy)));
        se->vsapi = vpy->vsapi;
        se->node = vpy->node;
        se->error = vpy->error;
        se->num_threads = vpy->num_threads;
        se->pad_scanlines = vpy->pad_scanlines;
        se->enable_v210 = vpy->enable_v210;
        return res;
    } else {
        return -1;
    }
}

VS_API(int) freeScript(ScriptExport *se) {
    SMap::iterator i = openScripts.find(se);
    if (i == openScripts.end())
        return -1;
    openScripts.erase(openScripts.find(se));
    return 0;
}
