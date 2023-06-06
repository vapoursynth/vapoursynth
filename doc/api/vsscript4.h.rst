VSScript4.h
===========

Table of contents
#################

Introduction_


Structs_
   VSScript_
   
   VSScriptAPI_
   
   
Functions_
   getVSScriptAPI_

   getApiVersion_
      
   getVSAPI_
      
   createScript_
      
   getCore_
      
   evaluateBuffer_
      
   evaluateFile_
      
   getError_
      
   getExitCode_
   
   getVariable_
     
   setVariables_
      
   getOutputNode_
      
   getOutputAlphaNode_
      
   getAltOutputMode_
      
   freeScript_
      
   evalSetWorkingDir_


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

A script environment. All evaluation and communication with evaluated scripts happens through a VSScript object.


VSScriptAPI
-----------

This struct is the way to access VSScript's public API.


Functions
#########

getVSScriptAPI
--------------

.. c:function:: const VSSCRIPTAPI *getVSScriptAPI(int version)

    Returns a struct containing function pointer for the api. Will return NULL is the specified *version* isn't supported.
    
    It is recommended to always pass *VSSCRIPT_API_VERSION*.
    

getApiVersion
-------------

.. c:function:: int getApiVersion()

    Returns the api version provided by vsscript.

    
getVSAPI
--------

.. c:function:: const VSAPI *getVSAPI(int version)

    Retrieves the VSAPI struct. Exists mostly as a convenience so the vapoursynth module doesn't have to be explicitly loaded.

    This could return NULL if the VapourSynth library doesn't provide the requested version.


createScript
------------

.. c:function:: VSScript *createScript(VSCore *core)

    Creates an empty script environment that can be used to evaluate scripts. Passing a pre-created *core* can be usful to have custom core creation flags, log callbacks or plugins pre-loaded. Passing NULL will automatically create a new core with default settings.
    
    Takes over ownership of the *core* regardless of success or failure. Returns NULL on error.


getCore
-------

.. c:function:: VSCore *getCore(VSScript *handle)

    Retrieves the VapourSynth core that was created in the script environment. If a VapourSynth core has not been created yet, it will be created now, with the default options (see the :doc:`../pythonreference`).
    
    VSScript retains ownership of the returned core object.

    Returns NULL on error.




evaluateBuffer
--------------

.. c:function:: int evaluateBuffer(VSScript *handle, const char *buffer, const char *scriptFilename)

    Evaluates a script contained in a C string. Can be called multiple times on the same script environment to successively add more processing.

    *handle*
        Pointer to a script environment.

    *buffer*
        The entire script to evaluate, as a C string.

    *scriptFilename*
        A name for the script, which will be displayed in error messages. If this is NULL, the name "<string>" will be used.
        
        The special ``__file__`` variable will be set to *scriptFilename*'s absolute path if this is not NULL.

    Returns non-zero in case of errors. The error message can be retrieved with getError_\ (). If the script calls *sys.exit(code)* the exit code can be retrieved with getExitCode_\ (). The working directory behavior can be changed by calling evalSetWorkingDir_\ () before this function.
    
    
evaluateFile
------------

.. c:function:: int evaluateFile(VSScript **handle, const char *scriptFilename)

    Evaluates a script contained in a file. This is a convenience function which reads the script from a file for you. It will only read the first 16 MiB which should be enough for everyone.

    Behaves the same as evaluateBuffer\ ().


getError
--------

.. c:function:: const char *getError(VSScript *handle)

    Returns the error message from a script environment, or NULL, if there is no error.

    It is okay to pass NULL.
    
    VSScript retains ownership of the pointer and it is only guaranteed to be valid until the next vsscript operation on the *handle*.


getExitCode
-----------

.. c:function:: int getExitCode(VSScript *handle)

    Returns the exit code if the script calls *sys.exit(code)*, or 0, if the script fails for other reasons or calls *sys.exit(0)*.

    It is okay to pass NULL.


getVariable
-----------

.. c:function:: int getVariable(VSScript *handle, const char *name, VSMap *dst)

    Retrieves a variable from the script environment.

    If a VapourSynth core has not been created yet in the script environment, one will be created now, with the default options (see the :doc:`../pythonreference`).

    *name*
        Name of the variable to retrieve.

    *dst*
        Map where the variable's value will be placed, with the key *name*.

    Returns non-zero on error.


setVariables
------------

.. c:function:: int vsscript_setVariable(VSScript *handle, const VSMap *vars)

    Sets variables in the script environment.

    The variables are now available to the script.

    If a VapourSynth core has not been created yet in the script environment, one will be created now, with the default options (see the :doc:`../pythonreference`).

    *vars*
        Map containing the variables to set.

    Returns non-zero on error.


getOutputNode
-------------

.. c:function:: VSNode *getOutputNode(VSScript *handle, int index)

    Retrieves a node from the script environment. A node in the script must have been marked for output with the requested *index*.

    The returned node has its reference count incremented by one.

    Returns NULL if there is no node at the requested index.

    
getOutputAlphaNode
------------------

.. c:function:: VSNode *getOutputAlphaNode(VSScript *handle, int index)

    Retrieves an alpha node from the script environment. A node with associated alpha in the script must have been marked for output with the requested *index*.

    The returned node has its reference count incremented by one.

    Returns NULL if there is no alpha node at the requested index.


getAltOutputMode
----------------

.. c:function:: int getAltOutputMode(VSScript *handle, int index)

    Retrieves the alternative output mode settings from the script. This value has no fixed meaning but in vspipe and vsvfw it
    indicates that alternate output formats should be used when multiple ones are available. It is up to the client application to define the exact meaning or simply disregard it completely.

    Returns 0 if there is no alt output mode set.


freeScript
----------

.. c:function:: void freeScript(VSScript *handle)

    Frees a script environment. *handle* is no longer usable.

    * Cancels any clips set for output in the script environment.

    * Clears any variables set in the script environment.

    * Clears the error message from the script environment, if there is one.

    * Frees the VapourSynth core used in the script environment, if there is one.

    Since this function frees the VapourSynth core, it must be called only after all frame requests are finished and all objects obtained from the script have been freed (frames, nodes, etc).

    It is safe to pass NULL.


evalSetWorkingDir
-----------------

.. c:function:: void evalSetWorkingDir(VSScript *handle, int setCWD)

    Set whether or not the working directory is temporarily changed to the same
    location as the script file when evaluateFile is called. Off by default.
