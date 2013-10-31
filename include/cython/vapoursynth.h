#ifndef __PYX_HAVE__vapoursynth
#define __PYX_HAVE__vapoursynth

struct VPYScriptExport;

/* "vapoursynth.pyx":1015
 *
 * # for whole script evaluation and export
 * cdef public struct VPYScriptExport:             # <<<<<<<<<<<<<<
 *     void *pyenvdict
 *     void *errstr
 */
struct VPYScriptExport {
  void *pyenvdict;
  void *errstr;
  int id;
};

#ifndef __PYX_HAVE_API__vapoursynth

#ifndef __PYX_EXTERN_C
  #ifdef __cplusplus
    #define __PYX_EXTERN_C extern "C"
  #else
    #define __PYX_EXTERN_C extern
  #endif
#endif

__PYX_EXTERN_C DL_IMPORT(int) vpy_evaluateScript(struct VPYScriptExport *, char const *, char const *, int);
__PYX_EXTERN_C DL_IMPORT(int) vpy_evaluateFile(struct VPYScriptExport *, char const *, int);
__PYX_EXTERN_C DL_IMPORT(void) vpy_freeScript(struct VPYScriptExport *);
__PYX_EXTERN_C DL_IMPORT(char) *vpy_getError(struct VPYScriptExport *);
__PYX_EXTERN_C DL_IMPORT(VSNodeRef) *vpy_getOutput(struct VPYScriptExport *, int);
__PYX_EXTERN_C DL_IMPORT(void) vpy_clearOutput(struct VPYScriptExport *, int);
__PYX_EXTERN_C DL_IMPORT(VSCore) *vpy_getCore(struct VPYScriptExport *);
__PYX_EXTERN_C DL_IMPORT(VSAPI) const *vpy_getVSApi(void);
__PYX_EXTERN_C DL_IMPORT(int) vpy_getVariable(struct VPYScriptExport *, char const *, VSMap *);
__PYX_EXTERN_C DL_IMPORT(void) vpy_setVariable(struct VPYScriptExport *, VSMap const *);
__PYX_EXTERN_C DL_IMPORT(int) vpy_clearVariable(struct VPYScriptExport *, char const *);
__PYX_EXTERN_C DL_IMPORT(void) vpy_clearEnvironment(struct VPYScriptExport *);
__PYX_EXTERN_C DL_IMPORT(void) vpy_initVSScript(void);

#endif /* !__PYX_HAVE_API__vapoursynth */

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initvapoursynth(void);
#else
PyMODINIT_FUNC PyInit_vapoursynth(void);
#endif

#endif /* !__PYX_HAVE__vapoursynth */
