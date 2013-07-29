#ifndef __PYX_HAVE_API__vapoursynth
#define __PYX_HAVE_API__vapoursynth
#include "Python.h"
#include "vapoursynth.h"

static int (*__pyx_f_11vapoursynth_vpy_evaluateScript)(struct VPYScriptExport *, char const *, char const *, int) = 0;
#define vpy_evaluateScript __pyx_f_11vapoursynth_vpy_evaluateScript
static int (*__pyx_f_11vapoursynth_vpy_evaluateFile)(struct VPYScriptExport *, char const *, int) = 0;
#define vpy_evaluateFile __pyx_f_11vapoursynth_vpy_evaluateFile
static void (*__pyx_f_11vapoursynth_vpy_freeScript)(struct VPYScriptExport *) = 0;
#define vpy_freeScript __pyx_f_11vapoursynth_vpy_freeScript
static char *(*__pyx_f_11vapoursynth_vpy_getError)(struct VPYScriptExport *) = 0;
#define vpy_getError __pyx_f_11vapoursynth_vpy_getError
static VSNodeRef *(*__pyx_f_11vapoursynth_vpy_getOutput)(struct VPYScriptExport *, int) = 0;
#define vpy_getOutput __pyx_f_11vapoursynth_vpy_getOutput
static void (*__pyx_f_11vapoursynth_vpy_clearOutput)(struct VPYScriptExport *, int) = 0;
#define vpy_clearOutput __pyx_f_11vapoursynth_vpy_clearOutput
static VSCore *(*__pyx_f_11vapoursynth_vpy_getCore)(struct VPYScriptExport *) = 0;
#define vpy_getCore __pyx_f_11vapoursynth_vpy_getCore
static VSAPI const *(*__pyx_f_11vapoursynth_vpy_getVSApi)(void) = 0;
#define vpy_getVSApi __pyx_f_11vapoursynth_vpy_getVSApi
static int (*__pyx_f_11vapoursynth_vpy_getVariable)(struct VPYScriptExport *, char const *, VSMap *) = 0;
#define vpy_getVariable __pyx_f_11vapoursynth_vpy_getVariable
static void (*__pyx_f_11vapoursynth_vpy_setVariable)(struct VPYScriptExport *, VSMap const *) = 0;
#define vpy_setVariable __pyx_f_11vapoursynth_vpy_setVariable
static int (*__pyx_f_11vapoursynth_vpy_clearVariable)(struct VPYScriptExport *, char const *) = 0;
#define vpy_clearVariable __pyx_f_11vapoursynth_vpy_clearVariable
static void (*__pyx_f_11vapoursynth_vpy_clearEnvironment)(struct VPYScriptExport *) = 0;
#define vpy_clearEnvironment __pyx_f_11vapoursynth_vpy_clearEnvironment
static void (*__pyx_f_11vapoursynth_vpy_initVSScript)(void) = 0;
#define vpy_initVSScript __pyx_f_11vapoursynth_vpy_initVSScript
#if !defined(__Pyx_PyIdentifier_FromString)
#if PY_MAJOR_VERSION < 3
  #define __Pyx_PyIdentifier_FromString(s) PyString_FromString(s)
#else
  #define __Pyx_PyIdentifier_FromString(s) PyUnicode_FromString(s)
#endif
#endif

#ifndef __PYX_HAVE_RT_ImportModule
#define __PYX_HAVE_RT_ImportModule
static PyObject *__Pyx_ImportModule(const char *name) {
    PyObject *py_name = 0;
    PyObject *py_module = 0;
    py_name = __Pyx_PyIdentifier_FromString(name);
    if (!py_name)
        goto bad;
    py_module = PyImport_Import(py_name);
    Py_DECREF(py_name);
    return py_module;
bad:
    Py_XDECREF(py_name);
    return 0;
}
#endif

#ifndef __PYX_HAVE_RT_ImportFunction
#define __PYX_HAVE_RT_ImportFunction
static int __Pyx_ImportFunction(PyObject *module, const char *funcname, void (**f)(void), const char *sig) {
    PyObject *d = 0;
    PyObject *cobj = 0;
    union {
        void (*fp)(void);
        void *p;
    } tmp;
    d = PyObject_GetAttrString(module, (char *)"__pyx_capi__");
    if (!d)
        goto bad;
    cobj = PyDict_GetItemString(d, funcname);
    if (!cobj) {
        PyErr_Format(PyExc_ImportError,
            "%s does not export expected C function %s",
                PyModule_GetName(module), funcname);
        goto bad;
    }
#if PY_VERSION_HEX >= 0x02070000 && !(PY_MAJOR_VERSION==3 && PY_MINOR_VERSION==0)
    if (!PyCapsule_IsValid(cobj, sig)) {
        PyErr_Format(PyExc_TypeError,
            "C function %s.%s has wrong signature (expected %s, got %s)",
             PyModule_GetName(module), funcname, sig, PyCapsule_GetName(cobj));
        goto bad;
    }
    tmp.p = PyCapsule_GetPointer(cobj, sig);
#else
    {const char *desc, *s1, *s2;
    desc = (const char *)PyCObject_GetDesc(cobj);
    if (!desc)
        goto bad;
    s1 = desc; s2 = sig;
    while (*s1 != '\0' && *s1 == *s2) { s1++; s2++; }
    if (*s1 != *s2) {
        PyErr_Format(PyExc_TypeError,
            "C function %s.%s has wrong signature (expected %s, got %s)",
             PyModule_GetName(module), funcname, sig, desc);
        goto bad;
    }
    tmp.p = PyCObject_AsVoidPtr(cobj);}
#endif
    *f = tmp.fp;
    if (!(*f))
        goto bad;
    Py_DECREF(d);
    return 0;
bad:
    Py_XDECREF(d);
    return -1;
}
#endif


static int import_vapoursynth(void) {
  PyObject *module = 0;
  module = __Pyx_ImportModule("vapoursynth");
  if (!module) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_evaluateScript", (void (**)(void))&__pyx_f_11vapoursynth_vpy_evaluateScript, "int (struct VPYScriptExport *, char const *, char const *, int)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_evaluateFile", (void (**)(void))&__pyx_f_11vapoursynth_vpy_evaluateFile, "int (struct VPYScriptExport *, char const *, int)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_freeScript", (void (**)(void))&__pyx_f_11vapoursynth_vpy_freeScript, "void (struct VPYScriptExport *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_getError", (void (**)(void))&__pyx_f_11vapoursynth_vpy_getError, "char *(struct VPYScriptExport *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_getOutput", (void (**)(void))&__pyx_f_11vapoursynth_vpy_getOutput, "VSNodeRef *(struct VPYScriptExport *, int)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_clearOutput", (void (**)(void))&__pyx_f_11vapoursynth_vpy_clearOutput, "void (struct VPYScriptExport *, int)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_getCore", (void (**)(void))&__pyx_f_11vapoursynth_vpy_getCore, "VSCore *(struct VPYScriptExport *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_getVSApi", (void (**)(void))&__pyx_f_11vapoursynth_vpy_getVSApi, "VSAPI const *(void)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_getVariable", (void (**)(void))&__pyx_f_11vapoursynth_vpy_getVariable, "int (struct VPYScriptExport *, char const *, VSMap *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_setVariable", (void (**)(void))&__pyx_f_11vapoursynth_vpy_setVariable, "void (struct VPYScriptExport *, VSMap const *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_clearVariable", (void (**)(void))&__pyx_f_11vapoursynth_vpy_clearVariable, "int (struct VPYScriptExport *, char const *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_clearEnvironment", (void (**)(void))&__pyx_f_11vapoursynth_vpy_clearEnvironment, "void (struct VPYScriptExport *)") < 0) goto bad;
  if (__Pyx_ImportFunction(module, "vpy_initVSScript", (void (**)(void))&__pyx_f_11vapoursynth_vpy_initVSScript, "void (void)") < 0) goto bad;
  Py_DECREF(module); module = 0;
  return 0;
  bad:
  Py_XDECREF(module);
  return -1;
}

#endif /* !__PYX_HAVE_API__vapoursynth */
