VapourSynth.h
=============

Table of contents
#################

Introduction_


Macros_
   VS_CC_

   VS_EXTERNAL_API_

   VAPOURSYNTH_API_VERSION_


Enums_
   VSColorFamily_

   VSSampleType_

   VSPresetFormat_

   VSFilterMode_

   VSNodeFlags_

   VSPropTypes_

   VSGetPropErrors_

   VSPropAppendMode_

   VSActivationReason_

   VSMessageType_


Structs_
   VSFrameRef_

   VSNodeRef_

   VSCore_

   VSPlugin_

   VSNode_

   VSFuncRef_

   VSMap_

   VSFrameContext_

   VSFormat_

   VSCoreInfo_

   VSVideoInfo_

   VSAPI_

   .. hlist::
      :columns: 4

      * createCore_

      * freeCore_

      * getCoreInfo_

      * cloneFrameRef_

      * cloneNodeRef_

      * cloneFuncRef_

      * freeFrame_

      * freeNode_

      * freeFunc_

      * newVideoFrame_

      * copyFrame_

      * copyFrameProps_

      * registerFunction_

      * getPluginById_

      * getPluginByNs_

      * getPlugins_

      * getFunctions_

      * createFilter_

      * setError_

      * getError_

      * setFilterError_

      * invoke_

      * getFormatPreset_

      * registerFormat_

      * getFrame_

      * getFrameAsync_

      * getFrameFilter_

      * requestFrameFilter_

      * queryCompletedFrame_

      * releaseFrameEarly_

      * getStride_

      * getReadPtr_

      * getWritePtr_

      * createFunc_

      * callFunc_

      * createMap_

      * freeMap_

      * clearMap_

      * getVideoInfo_

      * setVideoInfo_

      * getFrameFormat_

      * getFrameWidth_

      * getFrameHeight_

      * getFramePropsRO_

      * getFramePropsRW_

      * propNumKeys_

      * propGetKey_

      * propNumElements_

      * propGetType_

      * propGetInt_

      * propGetFloat_

      * propGetData_

      * propGetDataSize_

      * propGetNode_

      * propGetFrame_

      * propGetFunc_

      * propDeleteKey_

      * propSetInt_

      * propSetFloat_

      * propSetData_

      * propSetNode_

      * propSetFrame_

      * propSetFunc_

      * setMaxCacheSize_

      * getOutputIndex_

      * newVideoFrame2_

      * setMessageHandler_


Functions_
   getVapourSynthAPI_


`Writing plugins`_
   VSInitPlugin_

   VSFilterInit_

   VSFilterGetFrame_

   VSFilterFree_


Introduction
############

This is VapourSynth's main header file. Plugins and applications that use
the library must include it.

VapourSynth's public API is all C.


Macros
######

VapourSynth.h defines some preprocessor macros that make the programmer's life
easier. The relevant ones are described below.

VS_CC
-----

The ``VS_CC`` macro expands to the calling convention used by VapourSynth.
All functions meant to be called by VapourSynth must use this macro (a
filter's "init", "getframe", "free" functions, etc).

Example:

.. code-block:: c

   static void VS_CC fooInit(...) { ... }


VS_EXTERNAL_API
---------------

The ``VS_EXTERNAL_API`` macro expands to the platform-specific magic required
for functions exported by shared libraries. It also takes care of adding
``extern "C"`` when needed, and ``VS_CC``.

This macro must be used for a plugin's entry point, like so:

.. code-block:: c

   VS_EXTERNAL_API(void) VapourSynthPluginInit(...) { ... }


VAPOURSYNTH_API_VERSION
-----------------------

Self-explanatory. Expands to an integer.


Enums
#####

.. _VSColorFamily:

enum VSColorFamily
------------------

   * cmGray

   * cmRGB

   * cmYUV

   * cmYCoCg

   * cmCompat


.. _VSSampleType:

enum VSSampleType
-----------------

   * stInteger

   * stFloat


.. _VSPresetFormat:

enum VSPresetFormat
-------------------

   The presets suffixed with H and S have floating point sample type.
   The H and S suffixes stand for half precision and single precision,
   respectively.

   The compat formats are the only packed formats in VapourSynth. Everything
   else is planar. They exist for compatibility with Avisynth plugins.
   They are not to be implemented in native VapourSynth plugins.

   * pfNone

   * pfGray8

   * pfGray16

   * pfGrayH

   * pfGrayS

   * pfYUV420P8

   * pfYUV422P8

   * pfYUV444P8

   * pfYUV410P8

   * pfYUV411P8

   * pfYUV440P8

   * pfYUV420P9

   * pfYUV422P9

   * pfYUV444P9

   * pfYUV420P10

   * pfYUV422P10

   * pfYUV444P10

   * pfYUV420P16

   * pfYUV422P16

   * pfYUV444P16

   * pfYUV444PH

   * pfYUV444PS

   * pfRGB24

   * pfRGB27

   * pfRGB30

   * pfRGB48

   * pfRGBH

   * pfRGBS

   * pfCompatBGR32

   * pfCompatYUY2


.. _VSFilterMode:

enum VSFilterMode
-----------------

   Controls how a filter will be multithreaded, if at all.

   * fmParallel

     Completely parallel execution.
     Multiple threads will call a filter's "getframe" function, to fetch several
     frames in parallel.

   * fmParallelRequests

     For filters that are serial in nature but can request in advance one or
     more frames they need.
     A filter's "getframe" function will be called from multiple threads at a
     time with activation reason arInitial, but only one thread will call it
     with activation reason arAllFramesReady.

   * fmUnordered

     For filters that modify their internal state every request.

   * fmSerial

     For source filters and compatibility with other filtering architectures.
     The filter's "getframe" function only ever gets called from one thread at a
     time.


.. _VSNodeFlags:

enum VSNodeFlags
----------------

   * nfNoCache


.. _VSPropTypes:

enum VSPropTypes
----------------

   Types of properties that can be stored in a VSMap.

   * ptUnset

   * ptInt

   * ptFloat

   * ptData

   * ptNode

   * ptFrame

   * ptFunction


.. _VSGetPropErrors:

enum VSGetPropErrors
--------------------

   * peUnset

   * peType

   * peIndex


.. _VSPropAppendMode:

enum VSPropAppendMode
---------------------

   Controls the behaviour of propSetInt_\ () and friends.

   * paReplace

     All existing values associated with the key will be replaced with
     the new value.

   * paAppend

     The new value will be appended to the list of existing values
     associated with the key.

   * paTouch

     If the key exists in the map, nothing happens. Otherwise, the key
     is added to the map, with no values associated.


.. _VSActivationReason:

enum VSActivationReason
-----------------------

   See VSFilterGetFrame_.

   * arInitial

   * arFrameReady

   * arAllFramesReady

   * arError


.. _VSMessageType:

enum VSMessageType
------------------

   See setMessageHandler_\ ().

   * mtDebug

   * mtWarning

   * mtCritical

   * mtFatal


Structs
#######

Most of the structs are implemented in C++, therefore constructing instances
of them directly is not possible.


.. _VSFrameRef:

struct VSFrameRef
-----------------

   A frame.

   The contents of a frame (pixels) are guaranteed to have an alignment of 32
   bytes.

   Two frames with the same width are guaranteed to have the same stride.

   Any data can be attached to a frame, using a VSMap_.


.. _VSNodeRef:

struct VSNodeRef
----------------

   TODO


.. _VSCore:

struct VSCore
-------------

   TODO


.. _VSPlugin:

struct VSPlugin
---------------

   A VapourSynth plugin. There are a few of these built into the core,
   and therefore available at all times: the basic filters (identifier
   ``com.vapoursynth.std``, namespace ``std``), the resizers (identifier
   ``com.vapoursynth.resize``, namespace ``resize``), and the Avisynth
   compatibility module, if running in Windows (identifier
   ``com.vapoursynth.avisynth``, namespace ``avs``).

   The Function Reference describes how to load VapourSynth and Avisynth
   plugins.

   A VSPlugin instance is constructed by the core when loading a plugin
   (.so / .dylib / .dll), and the pointer is passed to the plugin's
   VapourSynthPluginInit() function.

   A VapourSynth plugin can export any number of filters.

   Plugins have a few attributes:

      - An identifier, which must be unique among all VapourSynth plugins in
        existence, because this is what the core uses to make sure a plugin
        only gets loaded once.

      - A namespace, also unique. The filters exported by a plugin end up in
        the plugin's namespace.

      - A full name, which is used by the core in a few error messages.

      - The VapourSynth API version the plugin requires.

   Things you can do with a VSPlugin:

      - Get a list of all the filters it exports, using getFunctions_\ ().

      - Invoke one of its filters, using invoke_\ ().

   A list of all loaded plugins (including built-in) can be obtained with
   getPlugins_\ ().

   Once loaded, a plugin only gets unloaded when the VapourSynth core is freed.


.. _VSNode:

struct VSNode
-------------

   TODO


.. _VSFuncRef:

struct VSFuncRef
----------------

   TODO


.. _VSMap:

struct VSMap
------------

   VSMap is a container that stores (key,value) pairs. The keys are strings
   and the values can be (arrays of) integers, floating point numbers,
   arrays of bytes, VSNodeRef_, VSFrameRef_, or VSFuncRef_.

   The pairs in a VSMap are sorted by key.

   In VapourSynth, VSMaps have several uses:
      - storing filters' arguments and return values

      - storing user-defined functions' arguments and return values

      - storing the properties attached to frames

   VSMap itself allows any non-zero bytes to be used in keys, but VapourSynth
   places certain restrictions on the characters allowed in filters' arguments.
   See registerFunc in VSInitPlugin_.

   Creating and destroying a map are done with createMap_\ () and freeMap_\ (),
   respectively.

   A map's contents can be retrieved and modified using a number of functions,
   all prefixed with "prop".

   A map's contents can be erased with clearMap_\ ().


.. _VSFrameContext:

struct VSFrameContext
---------------------

   TODO


.. _VSFormat:

struct VSFormat
---------------

   Describes the format of a clip.

   Don't create an instance of this struct manually (``struct VSFormat moo;``),
   but only through registerFormat_\ (). Registered VSFormat instances will be
   valid as long as the VapourSynth core object lives. They can be retrieved
   with getFormatPreset_\ ().

   .. c:member:: char name[32]

      A nice, printable name, like "YUV444P10", or "runtime registered",
      for custom formats.

   .. c:member:: int id

      A number that uniquely identifies the VSFormat instance. One of
      VSPresetFormat_, if it's a built-in format.

   .. c:member:: int colorFamily

      See VSColorFamily_.

   .. c:member:: int sampleType

      See VSSampleType_.

   .. c:member:: int bitsPerSample

      Number of significant bits.

   .. c:member:: int bytesPerSample

      Number of bytes needed for a sample. This is always a power of 2 and the
      smallest possible that can fit the number of bits used per sample.

   .. c:member:: int subSamplingW
   .. c:member:: int subSamplingH

      log2 subsampling factor, applied to second and third plane.
      Convenient numbers that can be used like so:

      .. code-block:: c

         uv_width = y_width >> subSamplingW;

   .. c:member:: int numPlanes

      Number of planes.


.. _VSCoreInfo:

struct VSCoreInfo
-----------------

   Contains information about a VSCore_ instance.

   .. c:member:: const char* versionString

      Printable string containing the name of the library, copyright notice,
      core and API versions.

   .. c:member:: int core

      Version of the core.

   .. c:member:: int api

      Version of the API.

   .. c:member:: int64_t maxFramebufferSize

      The framebuffer cache will be allowed to grow up to this size (bytes).

   .. c:member:: int64_t usedFramebufferSize

      Current size of the framebuffer cache, in bytes.


.. _VSVideoInfo:

struct VSVideoInfo
------------------

   Contains information about a clip.

   .. c:member:: const VSFormat* format

      Format of the clip. It will be NULL if the clip's format can vary.

   .. c:member:: int64_t fpsNum

      Numerator part of the clip's frame rate.

   .. c:member:: int64_t fpsDen

      Denominator part of the clip's frame rate.

   .. c:member:: int width

      Width of the clip. It will be 0 if the clip's dimensions can vary.

   .. c:member:: int height

      Height of the clip. It will be 0 if the clip's dimensions can vary.

   .. c:member:: int numFrames

      Length of the clip. It will be 0 if the clip's length is unknown.

   .. c:member:: int flags

      What is this?


.. _VSAPI:

struct VSAPI
------------

   This giant struct is the way to access VapourSynth's public API.

----------

   .. _createCore:

   .. c:member:: VSCreateCore createCore

      typedef VSCore_ \*(VS_CC \*VSCreateCore)(int threads)

      Creates the Vapoursynth processing core and returns a pointer to it. It is
      legal to create multiple cores.

      If plugin autoloading is enabled, plugins found in certain folders are
      automatically loaded.

      *threads*
         Number of desired worker threads. If 0, a suitable value is
         automatically chosen, based on the number of logical CPUs.

----------

   .. _freeCore:

   .. c:member:: VSFreeCore freeCore

      typedef void (VS_CC \*VSFreeCore)(VSCore_ \*core)

      Frees a core.

      ??? Conditions on the object state, threading ???

----------

   .. _getCoreInfo:

   .. c:member:: VSGetCoreInfo getCoreInfo

      typedef const VSCoreInfo_ \*(VS_CC \*VSGetCoreInfo)(VSCore_ \*core)

      Returns information about the VapourSynth core.

----------

   .. _cloneFrameRef:

   .. c:member:: VSCloneFrameRef cloneFrameRef

      typedef const VSFrameRef_ \*(VS_CC \*VSCloneFrameRef)(const VSFrameRef_ \*f)

      Duplicates a frame reference. This new reference has to be deleted with
      freeFrame_\ () when it is no longer needed.

----------

   .. _cloneNodeRef:

   .. c:member:: VSCloneNodeRef cloneNodeRef

      typedef VSNodeRef_ \*(VS_CC \*VSCloneNodeRef)(VSNodeRef_ \*node)

      Duplicates a node reference. This new reference has to be deleted with
      freeNode_\ () when it is no longer needed.

----------

   .. _cloneFuncRef:

   .. c:member:: VSCloneFuncRef cloneFuncRef

      typedef VSFuncRef_ \*(VS_CC \*VSCloneFuncRef)(VSFuncRef_ \*f)

      TODO

----------

   .. _freeFrame:

   .. c:member:: VSFreeFrame freeFrame

      typedef void (VS_CC \*VSFreeFrame)(const VSFrameRef_ \*f)

      Deletes a frame reference, releasing the caller's ownership of the frame.

      Don't try to use the frame once the reference has been deleted.

----------

   .. _freeNode:

   .. c:member:: VSFreeNode freeNode

      typedef void (VS_CC \*VSFreeNode)(VSNodeRef_ \*node)

      Deletes a node reference, releasing the caller's ownership of the node.

      Don't try to use the node once the reference has been deleted.

----------

   .. _freeFunc:

   .. c:member:: VSFreeFunc freeFunc

      typedef void (VS_CC \*VSFreeFunc)(VSFuncRef_ \*f)

      TODO

----------

   .. _newVideoFrame:

   .. c:member:: VSNewVideoFrame newVideoFrame

      typedef VSFrameRef_ \*(VS_CC \*VSNewVideoFrame)(const VSFormat_ \*format, int width, int height, const VSFrameRef_ \*propSrc, VSCore_ \*core)

      Creates a new frame, optionally copying the properties attached to another
      frame.

      The new frame contains uninitialised memory.

      *format*
         The desired colorspace format. Must not be NULL.

      *width*

      *height*
         The desired dimensions of the frame, in pixels. Must be greater than 0.

      *propSrc*
         A frame from which properties will be copied. Can be NULL.

      Returns a pointer to the created frame. Ownership of the new frame is
      transferred to the caller.

      See also newVideoFrame2_\ ().

----------

   .. _copyFrame:

   .. c:member:: VSCopyFrame copyFrame

      typedef VSFrameRef_ \*(VS_CC \*VSCopyFrame)(const VSFrameRef_ \*f, VSCore_ \*core)

      Duplicates the frame (not just the reference). As the frame buffer is
      shared in a copy-on-write fashion, the frame content is not really
      duplicated until a write operation occurs. This is transparent for the user.

      Returns a pointer to the new frame. Ownership is transferred to the caller.

----------

   .. _copyFrameProps:

   .. c:member:: VSCopyFrameProps copyFrameProps

      typedef void (VS_CC \*VSCopyFrameProps)(const VSFrameRef_ \*src, VSFrameRef_ \*dst, VSCore_ \*core)

      Copies the property map of a frame to another frame, owerwriting all
      existing properties.

----------

   .. _registerFunction:

   .. c:member:: VSRegisterFunction registerFunction

      typedef void (VS_CC \*VSRegisterFunction)(const char \*name, const char \*args, VSPublicFunction argsFunc, void \*functionData, VSPlugin_ \*plugin)

      See VSInitPlugin_.

----------

   .. _getPluginById:

   .. c:member:: VSGetPluginById getPluginById

      typedef VSPlugin_ \*(VS_CC \*VSGetPluginById)(const char \*identifier, VSCore_ \*core)

      Returns a pointer to the plugin with the given identifier, or NULL
      if not found.

      *identifier*
         Reverse URL that uniquely identifies the plugin.

----------

   .. _getPluginByNs:

   .. c:member:: VSGetPluginByNs getPluginByNs

      typedef VSPlugin_ \*(VS_CC \*VSGetPluginByNs)(const char \*ns, VSCore_ \*core)

      Returns a pointer to the plugin with the given namespace, or NULL
      if not found.

      *ns*
         Namespace.

----------

   .. _getPlugins:

   .. c:member:: VSGetPlugins getPlugins

      typedef VSMap_ \*(VS_CC \*VSGetPlugins)(VSCore_ \*core)

      Returns a map containing a list of all loaded plugins.

      Keys:
         The plugins' unique identifiers.

      Values:
         Namespace, identifier, and full name, separated by semicolons.

----------

   .. _getFunctions:

   .. c:member:: VSGetFunctions getFunctions

      typedef VSMap_ \*(VS_CC \*VSGetFunctions)(VSPlugin_ \*plugin)

      Returns a map containing a list of the filters exported by a plugin.

      Keys:
         The filter names.

      Values:
         The filter name followed by its argument string, separated by a semicolon.

----------

   .. _createFilter:

   .. c:member:: VSCreateFilter createFilter

      typedef void (VS_CC \*VSCreateFilter)(const VSMap_ \*in, VSMap_ \*out, const char \*name, VSFilterInit_ init, VSFilterGetFrame_ getFrame, VSFilterFree_ free, int filterMode, int flags, void \*instanceData, VSCore_ \*core)

      Creates a new filter node.

      *in*
         List of the filter's arguments.

      *out*
         List of the filter's return values (clip(s) or an error).

      *name*
         Instance name. Please make it the same as the filter's name.

      *init*
         The filter's "init" function. Must not be NULL.

      *getFrame*
         The filter's "getframe" function. Must not be NULL.

      *free*
         The filter's "free" function. Can be NULL.

      *filterMode*
         One of VSFilterMode_. Indicates the level of parallelism
         supported by the filter.

      *flags*
         Set to nfNoCache (VSNodeFlags_) if the frames generated by the filter
         should not be cached. It is useful for filters that only shuffle
         frames around without modifying them (e.g. std.Interleave). For most
         filters this should be 0.

      *instanceData*
         A pointer to the private filter data, usually allocated in the filter's
         argsFunc function.

      After this function returns, *out* will contain the new node(s) in the
      "clip" property, or an error, if something went wrong.

      .. warning::
         Never use inside a filter's "getframe" function.

----------

   .. _setError:

   .. c:member:: VSSetError setError

      typedef void (VS_CC \*VSSetError)(VSMap_ \*map, const char \*errorMessage)

      Adds an error message to a map. The map is cleared first.

      Never call from a filter's "getframe" function. See setFilterError_.

      *errorMessage*
         Pass NULL to get a useless default error message.

----------

   .. _getError:

   .. c:member:: VSGetError getError

      typedef const char \*(VS_CC \*VSGetError)(const VSMap_ \*map)

      Returns a pointer to the error message contained in the map,
      or NULL if there is no error message. The pointer is valid as long as
      the map lives.

----------

   .. _setFilterError:

   .. c:member:: VSSetFilterError setFilterError

      typedef void (VS_CC \*VSSetFilterError)(const char \*errorMessage, VSFrameContext_ \*frameCtx)

      Adds an error message to a frame context, replacing the existing message,
      if any.

      This is the way to report errors in a filter's "getframe" function.
      Such errors are not fatal, i.e. the caller can try to request the same
      frame again.

----------

   .. _invoke:

   .. c:member:: VSInvoke invoke

      typedef VSMap_ \*(VS_CC \*VSInvoke)(VSPlugin_ \*plugin, const char \*name, const VSMap_ \*args)

      Invokes a filter.

      invoke() makes sure the filter has no compat input nodes, checks that
      the *args* passed to the filter are consistent with the argument list
      registered by the plugin, calls the filter's "create" function, and
      checks that the filter doesn't return any compat nodes. If everything
      goes smoothly, the filter will be ready to generate frames after
      invoke() returns.

      ??? Concurrent call with other functions ???

      *plugin*
         A pointer to the plugin where the filter is located. Must not be NULL.

         See getPluginById_\ () and getPluginByNs_\ ().

      *name*
         Name of the filter to invoke.

      *args*
         Arguments for the filter.

      Returns a map containing the filter's return value(s). The caller gets
      ownership of the map. Use getError_\ () to check if the filter was invoked
      successfully.

      Most filters will either add an error to the map, or one or more clips
      with the key "clip". One exception is the special LoadPlugin "filter",
      which doesn't return any clips for obvious reasons.

      .. warning::
         Never use inside a filter's "getframe" function.

----------

   .. _getFormatPreset:

   .. c:member:: VSGetFormatPreset getFormatPreset

      typedef const VSFormat_ \*(VS_CC \*VSGetFormatPreset)(int id, VSCore_ \*core)

      Returns a VSFormat structure from a video format identifier.

      Concurrent access allowed with other video format functions.

      *id*
         The format identifier: one of VSPresetFormat_ or a custom registered
         format.

      Returns NULL if the identifier is not known.

----------

   .. _registerFormat:

   .. c:member:: VSRegisterFormat registerFormat

      typedef const VSFormat_ \*(VS_CC \*VSRegisterFormat)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore_ \*core)

      Registers a custom video format.

      Concurrent access allowed with other video format functions.

      *colorFamily*
         One of VSColorFamily_.

         .. note::
            Registering compat formats is not allowed.

      *sampleType*
         One of VSSampleType_.

      *bitsPerSample*
         Number of meaningful bits for a single component. The valid range is
         8-32.

         For floating point formats, only 16 or 32 bits are allowed.

      *subSamplingW*
         log2 of the horizontal chroma subsampling. 0 == no subsampling.

      *subSamplingH*
         log2 of the vertical chroma subsampling. The valid range is 0-4.

         .. note::
            RGB formats are not allowed to be subsampled in VapourSynth.

      Returns a pointer to the created VSFormat_ object. Its *id* member
      contains the attributed format identifier. The pointer is valid as long
      as the VSCore_ instance lives.

      If the parameters specify a format that is already registered (including
      preset formats), then no new format is created and the existing one is
      returned.

----------

   .. _getFrame:

   .. c:member:: VSGetFrame getFrame

      typedef const VSFrameRef_ \*(VS_CC \*VSGetFrame)(int n, VSNodeRef_ \*node, char \*errorMsg, int bufSize)

      Generates a frame directly. The frame is available when the function
      returns.

      This function is meant for external applications using the core as a
      library, or if frame requests are necessary during a filter's
      initialization.

      *n*
         The frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *bufSize*
         Maximum length for the error message, in bytes (including the
         trailing '\0'). Can be 0 if no error message is wanted.

      *errorMsg*
         Pointer to a buffer of *bufSize* bytes to store a possible error
         message. Can be NULL if no error message is wanted.

      Returns a reference to the generated frame, or NULL in case of failure.
      The ownership of the frame is transferred to the caller.

      .. warning::
         Never use inside a filter's "getframe" function.

----------

   .. _getFrameAsync:

   .. c:member:: VSGetFrameAsync getFrameAsync

      typedef void (VS_CC \*VSGetFrameAsync)(int n, VSNodeRef_ \*node, VSFrameDoneCallback callback, void \*userData)

      Requests the generation of a frame. When the frame is ready,
      a user-provided function is called.

      This function is meant for applications using VapourSynth as a library.

      ??? Could be called concurrently ???

      *n*
         Frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *callback*
         typedef void (VS_CC \*VSFrameDoneCallback)(void \*userData, const VSFrameRef_ \*f, int n, VSNodeRef_ \*node, const char \*errorMsg)

         Function of the client application called by the core when a requested
         frame is ready, after a call to getFrameAsync().

         If multiple frames were requested, they can be returned in any order.
         Client applications must take care of reordering them.

         This function is only ever called from one thread at a time.

         getFrameAsync() may be called from this function to request more
         frames.

         *userData*
            Pointer to private data from the client application, as passed
            previously to getFrameAsync().

         *f*
            The finished frame.

            The ownership of the frame is kept by the core, hence a new
            reference must be created if the frame has to be stored for later
            use (after the function returns).

            It will be NULL in case of error.

         *n*
            The frame number.

         *node*
            Node the frame belongs to.

         *errorMsg*
            String that usually contains an error message if the frame
            generation failed. NULL if there is no error.

      *userData*
         Pointer passed to the callback.

      .. warning::
         Never use inside a filter's "getframe" function.

----------

   .. _getFrameFilter:

   .. c:member:: VSGetFrameFilter getFrameFilter

      typedef const VSFrameRef_ \*(VS_CC \*VSGetFrameFilter)(int n, VSNodeRef_ \*node, VSFrameContext_ \*frameCtx)

      Retrieves a frame that was previously requested with
      requestFrameFilter_\ ().

      Only use inside a filter's "getframe" function.

      A filter usually calls this function when its activation reason is
      arAllFramesReady or arFrameReady.

      *n*
         The frame number.

      *node*
         The node from which the frame is retrieved.

      *frameCtx*
         The context passed to the filter's "getframe" function.

      Returns a pointer to the requested frame, or NULL if the requested frame
      is not available for any reason. The ownership of the frame is
      transferred to the caller.

----------

   .. _requestFrameFilter:

   .. c:member:: VSRequestFrameFilter requestFrameFilter

      typedef void (VS_CC \*VSRequestFrameFilter)(int n, VSNodeRef_ \*node, VSFrameContext_ \*frameCtx)

      Requests a frame from a node and returns immediately.

      Only use inside a filter's "getframe" function.

      A filter usually calls this function when its activation reason is
      arInitial. The requested frame can then be retrieved using
      getFrameFilter_\ (), when the filter's activation reason is
      arAllFramesReady or arFrameReady.

      *n*
         The frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *frameCtx*
         The context passed to the filter's "getframe" function.

----------

   .. _queryCompletedFrame:

   .. c:member:: VSQueryCompletedFrame queryCompletedFrame

      typedef void (VS_CC \*VSQueryCompletedFrame)(VSNodeRef_ \**node, int \*n, VSFrameContext_ \*frameCtx)

      Finds out which requested frame is ready. To be used in a filter's
      "getframe" function, when it is called with *activationReason*
      arFrameReady.

      The node and the frame number will be available in *node* and *n*.

----------

   .. _releaseFrameEarly:

   .. c:member:: VSReleaseFrameEarly releaseFrameEarly

      typedef void (VS_CC \*VSReleaseFrameEarly)(VSNodeRef_ \*node, int n, VSFrameContext_ \*frameCtx)

      TODO

      Only use inside a filter's "getframe" function.

----------

   .. _getStride:

   .. c:member:: VSGetStride getStride

      typedef int (VS_CC \*VSGetStride)(const VSFrameRef_ \*f, int plane)

      Returns the distance in bytes between two consecutive lines of a plane of
      a frame.

      Passing an invalid plane number will cause a fatal error.

----------

   .. _getReadPtr:

   .. c:member:: VSGetReadPtr getReadPtr

      typedef const uint8_t \*(VS_CC \*VSGetReadPtr)(const VSFrameRef_ \*f, int plane)

      Returns a read-only pointer to a plane of a frame.

      Passing an invalid plane number will cause a fatal error.

      .. note::
         Don't assume all three planes of a frame are allocated in one
         contiguous chunk (they're not).

----------

   .. _getWritePtr:

   .. c:member:: VSGetWritePtr getWritePtr

      typedef uint8_t \*(VS_CC \*VSGetWritePtr)(VSFrameRef_ \*f, int plane)

      Returns a read/write pointer to a plane of a frame.

      Passing an invalid plane number will cause a fatal error.

----------

   .. _createFunc:

   .. c:member:: VSCreateFunc createFunc

      typedef VSFuncRef_ \*(VS_CC \*VSCreateFunc)(VSPublicFunction func, void \*userData, VSFreeFuncData free)

      *func*
         typedef void (VS_CC \*VSPublicFunction)(const VSMap_ \*in, VSMap_ \*out, void \*userData, VSCore_ \*core, const VSAPI_ \*vsapi)

         User-defined function that does stuff. ???

      *userData*
         Pointer passed to *func*


      *free*
         typedef void (VS_CC \*VSFreeFuncData)(void \*userData)

         Callback tasked with freeing *userData*.

----------

   .. _callFunc:

   .. c:member:: VSCallFunc callFunc

      typedef void (VS_CC \*VSCallFunc)(VSFuncRef_ \*func, const VSMap_ \*in, VSMap_ \*out, VSCore_ \*core, const VSAPI_ \*vsapi)

      TODO

----------

   .. _createMap:

   .. c:member:: VSCreateMap createMap

      typedef VSMap_ \*(VS_CC \*VSCreateMap)(void)

      Creates a new property map. It must be deallocated later with
      freeMap_\ ().

----------

   .. _freeMap:

   .. c:member:: VSFreeMap freeMap

      typedef void (VS_CC \*VSFreeMap)(VSMap_ \*map)

      Frees a map and all the objects it contains.

----------

   .. _clearMap:

   .. c:member:: VSClearMap clearMap

      typedef void (VS_CC \*VSClearMap)(VSMap_ \*map)

      Deletes all the keys and their associated values from the map, leaving it
      empty.

----------

   .. _getVideoInfo:

   .. c:member:: VSGetVideoInfo getVideoInfo

      typedef const VSVideoInfo_ \*(VS_CC \*VSGetVideoInfo)(VSNodeRef_ \*node)

      Returns a pointer to the video info associated with a node. The pointer is
      valid as long as the node lives.

----------

   .. _setVideoInfo:

   .. c:member:: VSSetVideoInfo setVideoInfo

      typedef void (VS_CC \*VSSetVideoInfo)(const VSVideoInfo_ \*vi, int numOutputs, VSNode_ \*node)

      Sets the node's video info.

      *vi*
         Pointer to *numOutputs* VSVideoInfo_ instances. The structures are
         copied by the core.

      *numOutputs*
         Number of clips the filter wants to return. Must be greater than 0.

      *node*
         Pointer to the node whose video info is to be set.

----------

   .. _getFrameFormat:

   .. c:member:: VSGetFrameFormat getFrameFormat

      typedef const VSFormat_ \*(VS_CC \*VSGetFrameFormat)(const VSFrameRef_ \*f)

      Retrieves the format of a frame.

----------

   .. _getFrameWidth:

   .. c:member:: VSGetFrameWidth getFrameWidth

      typedef int (VS_CC \*VSGetFrameWidth)(const VSFrameRef_ \*f, int plane)

      Returns the width of a plane of a given frame, in pixels. The width
      depends on the plane number because of the possible chroma subsampling.

----------

   .. _getFrameHeight:

   .. c:member:: VSGetFrameHeight getFrameHeight

      typedef int (VS_CC \*VSGetFrameHeight)(const VSFrameRef_ \*f, int plane)

      Returns the height of a plane of a given frame, in pixels. The height
      depends on the plane number because of the possible chroma subsampling.

----------

   .. _getFramePropsRO:

   .. c:member:: VSGetFramePropsRO getFramePropsRO

      typedef const VSMap_ \*(VS_CC \*VSGetFramePropsRO)(const VSFrameRef_ \*f)

      Returns a read-only pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _getFramePropsRW:

   .. c:member:: VSGetFramePropsRW getFramePropsRW

      typedef VSMap_ \*(VS_CC \*VSGetFramePropsRW)(VSFrameRef_ \*f)

      Returns a read/write pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _propNumKeys:

   .. c:member:: VSPropNumKeys propNumKeys

      typedef int (VS_CC \*VSPropNumKeys)(const VSMap_ \*map)

      Returns the number of keys contained in a property map.

----------

   .. _propGetKey:

   .. c:member:: VSPropGetKey propGetKey

      typedef const char \*(VS_CC \*VSPropGetKey)(const VSMap_ \*map, int index)

      Returns a key from a property map.

      Passing an invalid *index* will cause a fatal error.

      The pointer is valid as long as the key exists in the map.

----------

   .. _propNumElements:

   .. c:member:: VSPropNumElements propNumElements

      typedef int (VS_CC \*VSPropNumElements)(const VSMap_ \*map, const char \*key)

      Returns the number of elements associated with a key in a property map.
      Returns -1 if there is no such key in the map.

----------

   .. _propGetType:

   .. c:member:: VSPropGetType propGetType

      typedef char (VS_CC \*VSPropGetType)(const VSMap_ \*map, const char \*key)

      Returns the type of the elements associated with the given key in a
      property map.

      The returned value is one of VSPropTypes_. If there is no such key in the
      map, the returned value is ptUnset.

----------

   .. _propGetInt:

   .. c:member:: VSPropGetInt propGetInt

      typedef int64_t (VS_CC \*VSPropGetInt)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves an integer from a map.

      Returns the number on success, or 0 in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFloat:

   .. c:member:: VSPropGetFloat propGetFloat

      typedef double (VS_CC \*VSPropGetFloat)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a floating point number from a map.

      Returns the number on success, or 0 in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetData:

   .. c:member:: VSPropGetData propGetData

      typedef const char \*(VS_CC \*VSPropGetData)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves arbitrary binary data from a map.

      Returns a pointer to the data on success, or NULL in case of error.

      The pointer is valid until the map is destroyed, or until the
      corresponding key is removed from the map or altered.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetDataSize:

   .. c:member:: VSPropGetDataSize propGetDataSize

      typedef int (VS_CC \*VSPropGetDataSize)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Returns the size in bytes of a property of type ptData. See VSPropTypes_.

----------

   .. _propGetNode:

   .. c:member:: VSPropGetNode propGetNode

      typedef VSNodeRef_ \*(VS_CC \*VSPropGetNode)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a node from a map.

      Returns a pointer to the node on success, or NULL in case of error.

      This function increases the node's reference count, so freeNode_\ () must
      be used when the node is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFrame:

   .. c:member:: VSPropGetFrame propGetFrame

      typedef const VSFrameRef_ \*(VS_CC \*VSPropGetFrame)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a frame from a map.

      Returns a pointer to the frame on success, or NULL in case of error.

      This function increases the frame's reference count, so freeFrame_\ () must
      be used when the frame is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFunc:

   .. c:member:: VSPropGetFunc propGetFunc

      typedef VSFuncRef_ \*(VS_CC \*VSPropGetFunc)(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a function from a map.

      Returns a pointer to the function on success, or NULL in case of error.

      This function increases the function's reference count, so freeFunc_\ () must
      be used when the function is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL)
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements associated
         with a key.

      *error*
         A bitwise OR of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propDeleteKey:

   .. c:member:: VSPropDeleteKey propDeleteKey

      typedef int (VS_CC \*VSPropDeleteKey)(VSMap_ \*map, const char \*key)

      Removes the property with the given key. All values associated with the
      key are lost.

      Returns 0 if the key isn't in the map. Otherwise it returns 1.

----------

   .. _propSetInt:

   .. c:member:: VSPropSetInt propSetInt

      typedef int (VS_CC \*VSPropSetInt)(VSMap_ \*map, const char \*key, int64_t i, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *i*
         Value to store.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetFloat:

   .. c:member:: VSPropSetFloat propSetFloat

      typedef int (VS_CC \*VSPropSetFloat)(VSMap_ \*map, const char \*key, double d, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *d*
         Value to store.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetData:

   .. c:member:: VSPropSetData propSetData

      typedef int (VS_CC \*VSPropSetData)(VSMap_ \*map, const char \*key, const char \*data, int size, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *data*
         Value to store.

         This function copies the data, so the pointer should be freed when
         no longer needed.

      *size*
         The number of bytes to copy.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetNode:

   .. c:member:: VSPropSetNode propSetNode

      typedef int (VS_CC \*VSPropSetNode)(VSMap_ \*map, const char \*key, VSNodeRef_ \*node, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *node*
         Value to store.

         This function will increase the node's reference count, so the
         pointer should be freed when no longer needed.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetFrame:

   .. c:member:: VSPropSetFrame propSetFrame

      typedef int (VS_CC \*VSPropSetFrame)(VSMap_ \*map, const char \*key, const VSFrameRef_ \*f, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *f*
         Value to store.

         This function will increase the frame's reference count, so the
         pointer should be freed when no longer needed.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetFunc:

   .. c:member:: VSPropSetFunc propSetFunc

      typedef int (VS_CC \*VSPropSetFunc)(VSMap_ \*map, const char \*key, VSFuncRef_ \*func, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Any characters may be used.

      *func*
         Value to store.

         This function will increase the function's reference count, so the
         pointer should be freed when no longer needed.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _setMaxCacheSize:

   .. c:member:: VSSetMaxCacheSize setMaxCacheSize

      typedef int64_t (VS_CC \*VSSetMaxCacheSize)(int64_t bytes, VSCore_ \*core)

      Sets the maximum size of the framebuffer cache. Returns the new maximum
      size.

----------

   .. _getOutputIndex:

   .. c:member:: VSGetOutputIndex getOutputIndex

      typedef int (VS_CC \*VSGetOutputIndex)(VSFrameContext_ \*frameCtx)

      Returns the index of the node from which the frame is being requested.

      Only needed in the "getframe" function of filters that return more than
      one clip.

----------

   .. _newVideoFrame2:

   .. c:member:: VSNewVideoFrame2 newVideoFrame2

      typedef VSFrameRef_ \*(VS_CC \*VSNewVideoFrame2)(const VSFormat_ \*format, int width, int height, const VSFrameRef_ \**planeSrc, const int \*planes, const VSFrameRef_ \*propSrc, VSCore_ \*core)

      Creates a new frame from the planes of existing frames, optionally copying
      the properties attached to another frame.

      *format*
         The desired colorspace format. Must not be NULL.

      *width*

      *height*
         The desired dimensions of the frame, in pixels. Must be greater than 0.

      *planeSrc*
         Array of frames from which planes will be copied. If any elements of
         the array are NULL, the corresponding planes in the new frame will
         contain uninitialised memory.

      *planes*
         Array of plane numbers indicating which plane to copy from the
         corresponding source frame.

      *propSrc*
         A frame from which properties will be copied. Can be NULL.

      Returns a pointer to the created frame. Ownership of the new frame is
      transferred to the caller.

      Example:

      .. code-block:: c

         // Assume frameA, frameB, frameC are existing frames.
         const VSFrameRef * frames[3] = { frameA, frameB, frameC };
         const int planes[3] = { 1, 0, 2 };

         VSFrameRef * newFrame = vsapi->newVideoFrame2(f, w, h, frames, planes, NULL, core);
         // newFrame's first plane is now a copy of frameA's second plane,
         // the second plane is a copy of frameB's first plane,
         // the third plane is a copy of frameC's third plane.

----------

   .. _setMessageHandler:

   .. c:member:: VSSetMessageHandler setMessageHandler

      typedef void (VS_CC \*VSSetMessageHandler)(VSMessageHandler handler, void \*userData)

      Installs a custom handler for the various error messages VapourSynth
      emits. The message handler is currently global, i.e. per process, not
      per VSCore_ instance.

      This function wraps `qInstallMsgHandler <http://qt-project.org/doc/qt-4.8/qtglobal.html#qInstallMsgHandler>`_.

      *handler*
         typedef void (VS_CC \*VSMessageHandler)(int msgType, const char \*msg, void \*userdata)

         Custom message handler.

         *msgType*
            The type of message. One of VSMessageType_.

            If *msgType* is mtFatal, VapourSynth will call abort() after the
            message handler returns.

         *msg*
            The message.

      *userData*
         Pointer that gets passed to the message handler.


Functions
#########

.. _getVapourSynthAPI:

const VSAPI_\* getVapourSynthAPI(int version)

   Returns a pointer to the global VSAPI instance.

   Returns NULL if the requested API version is not supported or if the system
   does not meet the minimum requirements to run VapourSynth.


Writing plugins
###############


A simple VapourSynth plugin which exports one filter will contain five
functions: an entry point (called ``VapourSynthPluginInit``), a function tasked
with creating a filter instance (often called ``fooCreate``), an "init" function
(often called ``fooInit``), a "getframe" function (often called ``fooGetframe``),
and a "free" function (often called ``fooFree``). These functions are described
below.

Another thing a filter requires is an object for storing a filter instance's
private data. This object will usually contain the filter's input nodes (if it
has any) and a VSVideoInfo_ struct describing the video the filter wants to
return.

The `sdk <https://github.com/vapoursynth/vapoursynth/tree/master/sdk>`_ folder
in the VapourSynth source contains some examples.

----------

.. _VSInitPlugin:

typedef void (VS_CC \*VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin_ \*plugin)

   A plugin's entry point. It must be called ``VapourSynthPluginInit``.
   This function is called after the core loads the shared library. Its purpose
   is to configure the plugin and to register the filters the plugin wants to
   export.

   *configFunc*
      typedef void (VS_CC \*VSConfigPlugin)(const char \*identifier, const char \*defaultNamespace, const char \*name, int apiVersion, int readonly, VSPlugin_ \*plugin)

      Configures the plugin. Call **once**, before calling *registerFunc*.

      *identifier*
         Reverse URL that must uniquely identify the plugin.

         If you don't own a domain then make one up that's related to the
         plugin name.

         Example: "com.vapoursynth.std"

      *defaultNamespace*
         Namespace where the plugin's filters will go. This, too, must be
         unique.

         Only lowercase letters and the underscore should be used, and it
         shouldn't be too long.

         Example: "resize"

      *name*
         Plugin name in readable form.

      *apiVersion*
         The VapourSynth API version the plugin uses.

         Use the VAPOURSYNTH_API_VERSION_ macro.

      *readonly*
         If set to 0, the plugin can export new filters after initialisation.
         The built-in Avisynth compat plugin uses this feature to add filters
         at runtime, as they are loaded. Most plugins should set this to 1.

      *plugin*
         Pointer to the plugin object in the core, as passed to
         VapourSynthPluginInit().

   *registerFunc*
      typedef void (VS_CC \*VSRegisterFunction)(const char \*name, const char \*args, VSPublicFunction argsFunc, void \*functionData, VSPlugin_ \*plugin)

      Function that registers a filter exported by the plugin. A plugin can
      export any number of filters.

      *name*
         Filter name. The characters allowed are letters, numbers, and the
         underscore. The first character must be a letter. In other words:
         ``^[a-zA-Z][a-zA-Z0-9_]*$``

         Filter names *should be* PascalCase.

      *args*
         String containing the filter's list of arguments.

         Arguments are separated by a semicolon. Each argument is made of
         several fields separated by a colon. Don't insert additional
         whitespace characters, or VapourSynth will die.

         Fields:
            The argument name.
               The same characters are allowed as for the filter's name.
               Argument names *should be* all lowercase and use only letters
               and the underscore.

            The type.
               "int": int64_t

               "float": double

               "data": const char*

               "clip": const VSNodeRef_\ *

               "frame": const VSFrameRef_\ *

               "func": const VSFuncRef_\ *

               It is possible to declare an array by appending "[]" to the type.

            "opt"
               If the parameter is optional.

            "empty"
               For arrays that are allowed to be empty.

         The following example declares the arguments "blah", "moo", and "asdf"::

            blah:clip;moo:int[]:opt;asdf:float:opt;

      *argsFunc*
         typedef void (VS_CC \*VSPublicFunction)(const VSMap_ \*in, VSMap_ \*out, void \*userData, VSCore_ \*core, const VSAPI_ \*vsapi)

         User-defined function called by the core to create an instance of the
         filter. This function is often named ``fooCreate``.

         In this function, the filter's input parameters should be retrieved
         and validated, the filter's private instance data should be
         initialised, and createFilter_\ () should be called.

         If for some reason you cannot create the filter, you have to free any
         created node references using freeNode_\ (), call setError_\ () on
         *out*, and return.

         *in*
            Input parameter list.

            Use propGetInt_\ () and friends to retrieve a parameter value.

            The map is guaranteed to exist only until the filter's "init"
            function returns. In other words, pointers returned by
            propGetData_\ () will not be usable in the filter's "getframe" and
            "free" functions.

         *out*
            Output parameter list. createFilter_\ () will add the output
            node(s) with the key named "clip", or an error, if something went
            wrong.

         *userData*
            Pointer that was passed to registerFunction_\ ().

      *functionData*
         Pointer to user data that gets passed to *argsFunc* when creating a
         filter. Useful to register multiple filters using a single *argsFunc*
         function.

      *plugin*
         Pointer to the plugin object in the core, as passed to
         VapourSynthPluginInit().

   *plugin*
      The plugin object in the core. Pass to *configFunc* and *registerFunc*.

----------

.. _VSFilterInit:

typedef void (VS_CC \*VSFilterInit)(VSMap_ \*in, VSMap_ \*out, void \**instanceData, VSNode_ \*node, VSCore_ \*core, const VSAPI_ \*vsapi)

   A filter's "init" function.

   This function is called by createFilter_\ () (indirectly).

   This is where the filter should perform whatever initialisation it requires.
   This is the only place where the video properties may be set (see
   setVideoInfo_\ ()).

   If an error occurs during initialisation:
      - free the input nodes, if any

      - free the instance data

      - free whatever else got allocated so far (obviously)

      - call setError_\ () on the *out* map

      - return

   *instanceData*
      Pointer to a pointer to the filter's private instance data.

----------

.. _VSFilterGetFrame:

typedef const VSFrameRef_ \*(VS_CC \*VSFilterGetFrame)(int n, int activationReason, void \**instanceData, void \**frameData, VSFrameContext_ \*frameCtx, VSCore_ \*core, const VSAPI_ \*vsapi)

   A filter's "getframe" function. It is called by the core when it needs
   the filter to generate a frame.

   It is possible to allocate local data, persistent during the multiple
   calls requesting the output frame.

   In case of error, call setFilterError_\ (), free \*frameData if required,
   and return NULL.

   Depending on the VSFilterMode_ set for the filter, multiple output frames
   could be requested concurrently.

   ??? Could there be concurrent calls for the same output frame with
   arFrameReady and arAllFramesReady ???

   *n*
      Requested frame number.

   *activationReason*
      One of VSActivationReason_.

      This function is first called with *activationReason* arInitial. At this
      point the function should request the input frames and return. When one or
      all of the requested frames are ready, this function is called again with
      *activationReason* arFrameReady or arAllFramesReady. The function should
      only return a frame when called with *activationReason* arAllFramesReady.

      In the case of arFrameReady, use queryCompletedFrame_\ () to find out
      which of the requested frames is ready.

      Most filters will only need to handle arInitial and arAllFramesReady.

   *instanceData*
      The filter's private instance data.

   *frameData*
      Optional private data associated with output frame number *n*.
      It must be deallocated before the last call for the given frame
      (arAllFramesReady or error).

   Return a reference to the output frame number *n* when it is ready, or NULL.
   The ownership of the frame is transferred to the caller.

----------

.. _VSFilterFree:

typedef void (VS_CC \*VSFilterFree)(void \*instanceData, VSCore_ \*core, const VSAPI_ \*vsapi)

   A filter's "free" function.

   This is where the filter should free everything it allocated,
   including its instance data.

   *instanceData*
      The filter's private instance data.
