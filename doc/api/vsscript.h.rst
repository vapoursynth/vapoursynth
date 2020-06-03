VSScript.h
==========

Table of contents
#################

Introduction_


Structs_
   VSScript_


Enums_
   VSEvalFlags_


Functions_
   vsscript_getApiVersion_
   
   vsscript_init_

   vsscript_finalize_

   vsscript_evaluateScript_

   vsscript_evaluateFile_

   vsscript_createScript_

   vsscript_freeScript_

   vsscript_getError_

   vsscript_getOutput_

   vsscript_clearOutput_

   vsscript_getCore_

   vsscript_getVSApi_
   
   vsscript_getVSApi2_

   vsscript_getVariable_

   vsscript_setVariable_

   vsscript_clearVariable_

   vsscript_clearEnvironment_


Introduction
############

VSScript provides a convenient wrapper for VapourSynth's scripting interface(s), allowing the evaluation of VapourSynth scripts and retrieval of output clips.

For reasons unknown, the VSScript library is called ``VSScript`` in Windows and ``vapoursynth-script`` everywhere else.

At this time, VapourSynth scripts can be written only in Python (version 3).

Here are a few users of the VSScript library:

   * `vspipe <https://github.com/vapoursynth/vapoursynth/blob/master/src/vspipe/vspipe.cpp>`_

   * `vsvfw <https://github.com/vapoursynth/vapoursynth/blob/master/src/vfw/vsvfw.cpp>`_

   * `an example program <https://github.com/vapoursynth/vapoursynth/blob/master/sdk/vsscript_example.c>`_

   * the video player `mpv <https://github.com/mpv-player/mpv/blob/master/video/filter/vf_vapoursynth.c>`_

.. note::
   If libvapoursynth-script is loaded with dlopen(), the RTLD_GLOBAL flag must be used. If not, Python won't be able to import binary modules. This is due to Python's design.


Structs
#######

VSScript
--------

A script environment. All communication with an evaluated script happens through a VSScript object.


Enums
#####

VSEvalFlags
-----------

   * efSetWorkingDir


Functions
#########

vsscript_getApiVersion
----------------------

.. c:function:: int vsscript_getApiVersion()

    Returns the api version provided by vsscript.

    
vsscript_init
-------------

.. c:function:: int vsscript_init()

    Initialises the available scripting interfaces.

    Returns the number of times vsscript_init_\ () has been called so far, including this call, or 0 in case of failure. This function will only fail if the VapourSynth installation is broken in some way.


vsscript_finalize
-----------------

.. c:function:: int vsscript_finalize()

    Frees all scripting interfaces.

    Returns the difference between the number of times vsscript_init_\ () was called and the number of times vsscript_finalize_\ () was called, including this call.


vsscript_evaluateScript
-----------------------

.. c:function:: int vsscript_evaluateScript(VSScript **handle, const char *script, const char *scriptFilename, int flags)

    Evaluates a script contained in a C string.

    *handle*
        Pointer to a script environment. If it is a pointer to NULL, a new script environment will be created and returned through this parameter. Passing NULL has the same effect as calling vsscript_createScript_\ () first and then passing the handle obtained from that function.

    *script*
        The entire script to evaluate, as a C string.

    *scriptFilename*
        A name for the script, which will be displayed in error messages. If this is NULL, the name "<string>" will be used in error messages.
        
        The special ``__file__`` variable will be set to *scriptFilename*'s absolute version if this is not NULL.

    *flags*
        0 or efSetWorkingDir (see VSEvalFlags_).

        If *scriptFilename* is not NULL and efSetWorkingDir is passed, the working directory will be changed to *scriptFilename*'s directory prior to evaluating the script.

        It is recommended to use efSetWorkingDir, so that relative paths in VapourSynth scripts work as expected.

    Restores the working directory before returning.

    Returns non-zero in case of errors. The error message can be retrieved with vsscript_getError_\ ().

    
vsscript_evaluateFile
---------------------

.. c:function:: int vsscript_evaluateFile(VSScript **handle, const char *scriptFilename, int flags)

    Evaluates a script contained in a file. This is a convenience function which reads the script from a file for you. It will only read the first 16 MiB (1024 * 1024 * 16), which should be enough for everyone.

    Behaves the same as vsscript_evaluateScript_\ ().


vsscript_createScript
---------------------

.. c:function:: int vsscript_createScript(VSScript **handle)

    Creates an empty script environment. This function can be useful when it is necessary to set some variable in the script environment before evaluating any scripts. Like in mpv's vf_vapoursynth filter, which passes the video to VapourSynth scripts in a variable called "video_in".

    If *handle* points to an existing script environment, you must call vsscript_freeScript_\ () first to avoid leaking memory.

    Returns non-zero in case of errors. The error message can be retrieved with vsscript_getError_\ ().


vsscript_freeScript
-------------------

.. c:function:: void vsscript_freeScript(VSScript *handle)

    Frees a script environment. *handle* is no longer usable.

    * Cancels any clips set for output in the script environment.

    * Clears any variables set in the script environment.

    * Clears the error message from the script environment, if there is one.

    * Frees the VapourSynth core used in the script environment, if there is one.

    Since this function frees the VapourSynth core, it must be called only after all frame requests are finished and all objects obtained from the script have been freed (frames, nodes, etc).

    It is safe to pass NULL.


vsscript_getError
-----------------

.. c:function:: const char * vsscript_getError(VSScript *handle)

    Returns the error message from a script environment, or NULL, if there is no error message.

    It is okay to pass NULL.
    
    VSScript retains ownership of the pointer.


vsscript_getOutput
------------------

.. c:function:: VSNodeRef * vsscript_getOutput(VSScript *handle, int index)

    Retrieves a node from the script environment. A node in the script must have been marked for output with the requested *index*.

    Ownership of the node is transferred to the caller.

    Returns NULL if there is no node at the requested index.


vsscript_clearOutput
--------------------

.. c:function:: int vsscript_clearOutput(VSScript *handle, int index)

    Cancels a node set for output. The node will no longer be available to vsscript_getOutput_\ ().

    Returns non-zero if there is no node at the requested index.


vsscript_getCore
----------------

.. c:function:: VSCore * vsscript_getCore(VSScript *handle)

    Retrieves the VapourSynth core that was created in the script environment. If a VapourSynth core has not been created yet, it will be created now, with the default options (see the :doc:`../pythonreference`).
    
    VSScript retains ownership of the pointer.

    Returns NULL on error.


vsscript_getVSApi
-----------------

.. c:function:: const VSAPI * vsscript_getVSApi()

    Deprecated in favor of vsscript_getVSApi2_\ (). Retrieves the VSAPI struct.

    This could return NULL if the scripting interface library (the Python module) expects an API version that the core VapourSynth library doesn't provide (for example, if either library was replaced with an older/newer copy).


vsscript_getVSApi2
------------------

.. c:function:: const VSAPI * vsscript_getVSApi2(int version)

    Retrieves the VSAPI struct.

    This could return NULL if the VapourSynth library doesn't provide the requested version.


vsscript_getVariable
--------------------

.. c:function:: int vsscript_getVariable(VSScript *handle, const char *name, VSMap *dst)

    Retrieves a variable from the script environment.

    If a VapourSynth core has not been created yet in the script environment, one will be created now, with the default options (see the :doc:`../pythonreference`).

    *name*
        Name of the variable to retrieve.

    *dst*
        Map where the variable's value will be placed, with the key *name*.

    Returns non-zero on error.


vsscript_setVariable
--------------------

.. c:function:: int vsscript_setVariable(VSScript *handle, const VSMap *vars)

    Sets variables in the script environment.

    The variables are now available to the script.

    If a VapourSynth core has not been created yet in the script environment, one will be created now, with the default options (see the :doc:`../pythonreference`).

    *vars*
        Map containing the variables to set.

    Returns non-zero on error.


vsscript_clearVariable
----------------------

.. c:function:: int vsscript_clearVariable(VSScript *handle, const char *name)

    Deletes a variable from the script environment.

    Returns non-zero on error.


vsscript_clearEnvironment
-------------------------

.. c:function:: void vsscript_clearEnvironment(VSScript *handle)

    Clears the script environment.

    * Cancels any clips set for output in the script environment.

    * Clears any variables set in the script environment.
