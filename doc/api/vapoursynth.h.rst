VapourSynth.h
=============

Table of contents
#################

Introduction_


Macros_
   VS_CC_

   VS_EXTERNAL_API_

   VAPOURSYNTH_API_MAJOR_

   VAPOURSYNTH_API_MINOR_

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

      * Functions that deal with the core:

          * createCore_

          * freeCore_

          * getCoreInfo_
          
          * getCoreInfo2_

          * setMaxCacheSize_

          * setMessageHandler_
          
          * addMessageHandler_
          
          * removeMessageHandler_

          * logMessage_

          * setThreadCount_

      * Functions that deal with frames:

          * newVideoFrame_

          * newVideoFrame2_

          * copyFrame_

          * cloneFrameRef_

          * freeFrame_

          * getStride_

          * getReadPtr_

          * getWritePtr_

          * getFrameFormat_

          * getFrameWidth_

          * getFrameHeight_

          * copyFrameProps_

          * getFramePropsRO_

          * getFramePropsRW_

      * Functions that deal with nodes:

          * cloneNodeRef_

          * freeNode_

          * getFrame_

          * getFrameAsync_

          * getFrameFilter_

          * requestFrameFilter_

          * getVideoInfo_

          * setVideoInfo_

      * Functions that deal with formats:

          * getFormatPreset_

          * registerFormat_

      * Functions that deal with maps:

          * createMap_

          * freeMap_

          * clearMap_

          * setError_

          * getError_

          * propNumKeys_

          * propGetKey_

          * propDeleteKey_

          * propGetType_

          * propNumElements_

          * propGetInt_

          * propGetIntArray_

          * propGetFloat_

          * propGetFloatArray_

          * propGetData_

          * propGetDataSize_

          * propGetNode_

          * propGetFrame_

          * propGetFunc_

          * propSetInt_

          * propSetIntArray_

          * propSetFloat_

          * propSetFloatArray_

          * propSetData_

          * propSetNode_

          * propSetFrame_

          * propSetFunc_

      * Functions that deal with plugins:

          * getPluginById_

          * getPluginByNs_

          * getPlugins_

          * getFunctions_

          * getPluginPath_

      * Functions that deal with functions:

          * createFunc_

          * cloneFuncRef_

          * callFunc_

          * freeFunc_

      * Functions that are mostly used in plugins:

          * createFilter_

          * registerFunction_

      * Functions that resist classification:

          * invoke_

      * Functions that are useful only in a filter's getframe function,
        but otherwise still resist classification:

          * setFilterError_

          * getOutputIndex_

          * queryCompletedFrame_

          * releaseFrameEarly_


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


VAPOURSYNTH_API_MAJOR
---------------------

Major API version.

This macro was added in VapourSynth R26 (API R3).


VAPOURSYNTH_API_MINOR
---------------------

Minor API version. It is bumped when new functions are added to VSAPI_.

This macro was added in VapourSynth R26 (API R3).


VAPOURSYNTH_API_VERSION
-----------------------

API version. The high 16 bits are VAPOURSYNTH_API_MAJOR_, the low 16
bits are VAPOURSYNTH_API_MINOR_.

Prior to VapourSynth R26, the API version consisted of only the major
component.


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

   * pfYUV420P12

   * pfYUV422P12

   * pfYUV444P12

   * pfYUV420P14

   * pfYUV422P14

   * pfYUV444P14

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
     with activation reason arAllFramesReady at a time.

   * fmUnordered

     Only one thread can call the filter's "getframe" function at a time.
     Useful for filters that modify or examine their internal state to
     determine which frames to request.

     While the "getframe" function will only run in one thread at a
     time, the calls can happen in any order. For example, it can be
     called with reason arInitial for frame 0, then again with reason
     arInitial for frame 1, then with reason arAllFramesReady for
     frame 0.

   * fmSerial

     For compatibility with other filtering architectures.
     The filter's "getframe" function only ever gets called from one thread at a
     time. Unlike fmUnordered, only one frame is processed at a time.


.. _VSNodeFlags:

enum VSNodeFlags
----------------

   * nfNoCache

     This flag indicates that the frames returned by the filter should not
     be cached. "Fast" filters should set this to reduce cache bloat.

   * nfIsCache

     This flag must not be used in third-party filters. It is used to mark
     instances of the built-in Cache filter. Strange things may happen to
     your filter if you use this flag.

     This flag was introduced in VapourSynth R24 without bumping the
     API version (R3).

   * nfMakeLinear

     This flag should be used by filters which prefer linear access,
     like source filters, where seeking around can cause significant
     slowdowns. This flag only has any effect if the filter using it
     is immediately followed by an instance of the built-in Cache filter.

     This flag was introduced in API R3.3 (VapourSynth R30).


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

   When a propGet* function fails, it returns one of these in the *err*
   parameter.

   They are all non-zero.

   * peUnset

     The requested key was not found in the map.

   * peType

     The wrong function was used to retrieve the property. E.g.
     propGetInt_\ () was used on a property of type ptFloat.

   * peIndex

     The requested index was out of bounds.


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

Most structs are opaque and their contents can only be accessed using functions in the API.


.. _VSFrameRef:

struct VSFrameRef
-----------------

   A frame.

   Each row of pixels in a frame is guaranteed to have an alignment of 32
   bytes.

   Two frames with the same width are guaranteed to have the same stride.

   Any data can be attached to a frame, using a VSMap_.


.. _VSNodeRef:

struct VSNodeRef
----------------

   A reference to a node in the constructed filter graph. Its primary use
   is as an argument to other filter or to request frames from.


.. _VSCore:

struct VSCore
-------------

   The core represents one instance of VapourSynth. Every core individually
   loads plugins and keeps track of memory.


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

      - A file name.

   Things you can do with a VSPlugin:

      - Get a list of all the filters it exports, using getFunctions_\ ().

      - Invoke one of its filters, using invoke_\ ().

      - Get its location in the file system, using getPluginPath_\ ().

   A list of all loaded plugins (including built-in) can be obtained with
   getPlugins_\ ().

   Once loaded, a plugin only gets unloaded when the VapourSynth core is freed.


.. _VSNode:

struct VSNode
-------------

   Not really interesting.


.. _VSFuncRef:

struct VSFuncRef
----------------

   Holds a reference to a function that may be called. This type primarily exists
   so functions can be shared between the scripting layer and plugins in the core. 


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

   Only alphanumeric characters and the underscore may be used in keys.

   Creating and destroying a map can be done with createMap_\ () and
   freeMap_\ (), respectively.

   A map's contents can be retrieved and modified using a number of functions,
   all prefixed with "prop".

   A map's contents can be erased with clearMap_\ ().


.. _VSFrameContext:

struct VSFrameContext
---------------------

   Not really interesting.


.. _VSFormat:

struct VSFormat
---------------

   Describes the format of a clip.

   Don't create an instance of this struct manually (``struct VSFormat moo;``),
   but only through registerFormat_\ (). Registered VSFormat instances will be
   valid as long as the VapourSynth core object lives. They can be retrieved
   with getFormatPreset_\ () or registerFormat_\ ().

   .. c:member:: char name[32]

      A nice, printable name, like "YUV444P10".

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

   .. c:member:: int numThreads

      Number of worker threads.

   .. c:member:: int64_t maxFramebufferSize

      The framebuffer cache will be allowed to grow up to this size (bytes) before memory is aggressively reclaimed.

   .. c:member:: int64_t usedFramebufferSize

      Current size of the framebuffer cache, in bytes.


.. _VSVideoInfo:

struct VSVideoInfo
------------------

   Contains information about a clip.

   .. c:member:: const VSFormat* format

      Format of the clip. It will be NULL if the clip's format can vary.

   .. c:member:: int64_t fpsNum

      Numerator part of the clip's frame rate. It will be 0 if the frame
      rate can vary. Should always be a reduced fraction.

   .. c:member:: int64_t fpsDen

      Denominator part of the clip's frame rate. It will be 0 if the frame
      rate can vary. Should always be a reduced fraction.

   .. c:member:: int width

      Width of the clip. Both width and height will be 0 if the clip's dimensions can vary.

   .. c:member:: int height

      Height of the clip. Both width and height will be 0 if the clip's dimensions can vary.

   .. c:member:: int numFrames

      Length of the clip.

      Since API R3.2 (VapourSynth R27) this is no longer allowed to be 0,
      i.e. clips with unknown length are not supported.

   .. c:member:: int flags

      The flags passed to createFilter_ (either 0, or one or more of
      VSNodeFlags_).


.. _VSAPI:

struct VSAPI
------------

   This giant struct is the way to access VapourSynth's public API.

----------

   .. _createCore:

   VSCore_ \*createCore(int threads)

      Creates the VapourSynth processing core and returns a pointer to it. It is
      legal to create multiple cores but in most cases it shouldn't be needed.

      *threads*
         Number of desired worker threads. If 0 or lower, a suitable value is
         automatically chosen, based on the number of logical CPUs.

----------

   .. _freeCore:

   void freeCore(VSCore_ \*core)

      Frees a core. Should only be done after all frame requests have completed
      and all objects belonging to the core have been released.

----------

   .. _getCoreInfo:

   const VSCoreInfo_ \*getCoreInfo(VSCore_ \*core)
   
      Deprecated as of API 3.6 (VapourSynth R47)

      Returns information about the VapourSynth core.
      
      VapourSynth retains ownership of the returned pointer.
      
----------

   .. _getCoreInfo2:

   void getCoreInfo2(VSCore_ \*core, VSCoreInfo_ \*info)

      Returns information about the VapourSynth core.
      
      This function is thread-safe.

----------

   .. _setMaxCacheSize:

   int64_t setMaxCacheSize(int64_t bytes, VSCore_ \*core)

      Sets the maximum size of the framebuffer cache. Returns the new maximum
      size.

----------

   .. _setMessageHandler:

   void setMessageHandler(VSMessageHandler handler, void \*userData)
   
      Deprecated as of API 3.6 (VapourSynth R47)

      Installs a custom handler for the various error messages VapourSynth
      emits. The message handler is currently global, i.e. per process, not
      per VSCore_ instance.

      The default message handler simply sends the messages to the
      standard error stream.

      This function is thread-safe.

      *handler*
         typedef void (VS_CC \*VSMessageHandler)(int msgType, const char \*msg, void \*userdata)

         Custom message handler. If this is NULL, the default message
         handler will be restored.

         *msgType*
            The type of message. One of VSMessageType_.

            If *msgType* is mtFatal, VapourSynth will call abort() after the
            message handler returns.

         *msg*
            The message.

      *userData*
         Pointer that gets passed to the message handler.
         
----------

   .. _addMessageHandler:

   int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void \*userData)

      Installs a custom handler for the various error messages VapourSynth
      emits. The message handler is currently global, i.e. per process, not
      per VSCore_ instance. Returns a unique id for the handler.

      If no error handler is installed the messages are sent to the
      standard error stream.

      This function is thread-safe.

      *handler*
         typedef void (VS_CC \*VSMessageHandler)(int msgType, const char \*msg, void \*userdata)

         Custom message handler. If this is NULL, the default message
         handler will be restored.

         *msgType*
            The type of message. One of VSMessageType_.

            If *msgType* is mtFatal, VapourSynth will call abort() after the
            message handler returns.

         *msg*
            The message.
            
      *free*
         typedef void (VS_CC \*VSMessageHandlerFree)(void \*userData)
         
         Called when a handler is removed.

      *userData*
         Pointer that gets passed to the message handler.
         
      This function was introduced in API R3.6 (VapourSynth R47).
         
----------

   .. _removeMessageHandler:

   int removeMessageHandler(int id)

      Removes a custom handler. Return non-zero on success and zero if
      the handler id is invalid.

      This function is thread-safe.

      *id*
         Message handler id obtained from addMessageHandler_\ ().
         
      This function was introduced in API R3.6 (VapourSynth R47).

----------

   .. _logMessage:

   void logMessage(int msgType, const char \*msg)

      Send a message through VapourSynth's logging framework. See
      setMessageHandler_.

      This function is thread-safe.

      *msgType*
         The type of message. One of VSMessageType_.

         If *msgType* is mtFatal, VapourSynth will call abort() after
         delivering the message.

      *msg*
         The message.

      This function was introduced in API R3.4 (VapourSynth R30).

----------

   .. _setThreadCount:

   int setThreadCount(int threads, VSCore_ \*core)

      Sets the number of worker threads for the given core. If the requested
      number of threads is zero or lower, the number of hardware threads will
      be detected and used.

      Returns the new thread count.

      This function was introduced in VapourSynth R24 without bumping
      the API version (R3).

----------

   .. _newVideoFrame:

   VSFrameRef_ \*newVideoFrame(const VSFormat_ \*format, int width, int height, const VSFrameRef_ \*propSrc, VSCore_ \*core)

      Creates a new frame, optionally copying the properties attached to another
      frame. It is a fatal error to pass invalid arguments to this function.

      The new frame contains uninitialised memory.

      *format*
         The desired colorspace format. Must not be NULL.

      *width*

      *height*
         The desired dimensions of the frame, in pixels. Must be greater than 0 and have a suitable multiple for the subsampling in format.

      *propSrc*
         A frame from which properties will be copied. Can be NULL.

      Returns a pointer to the created frame. Ownership of the new frame is
      transferred to the caller.

      See also newVideoFrame2_\ ().

----------

   .. _newVideoFrame2:

   VSFrameRef_ \*newVideoFrame2(const VSFormat_ \*format, int width, int height, const VSFrameRef_ \**planeSrc, const int \*planes, const VSFrameRef_ \*propSrc, VSCore_ \*core)

      Creates a new frame from the planes of existing frames, optionally copying
      the properties attached to another frame. It is a fatal error to pass invalid arguments to this function.

      *format*
         The desired colorspace format. Must not be NULL.

      *width*

      *height*
         The desired dimensions of the frame, in pixels. Must be greater than 0 and have a suitable multiple for the subsampling in format.

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

      Example (assume *frameA*, *frameB*, *frameC* are existing frames):
      
      .. code-block:: c

         const VSFrameRef * frames[3] = { frameA, frameB, frameC };
         const int planes[3] = { 1, 0, 2 };
         VSFrameRef * newFrame = vsapi->newVideoFrame2(f, w, h, frames, planes, frameB, core);
         
      The newFrame's first plane is now a copy of *frameA*'s second plane,
      the second plane is a copy of *frameB*'s first plane,
      the third plane is a copy of *frameC*'s third plane
      and the properties have been copied from *frameB*.

----------

   .. _copyFrame:

   VSFrameRef_ \*copyFrame(const VSFrameRef_ \*f, VSCore_ \*core)

      Duplicates the frame (not just the reference). As the frame buffer is
      shared in a copy-on-write fashion, the frame content is not really
      duplicated until a write operation occurs. This is transparent for the user.

      Returns a pointer to the new frame. Ownership is transferred to the caller.

----------

   .. _cloneFrameRef:

   const VSFrameRef_ \*cloneFrameRef(const VSFrameRef_ \*f)

      Duplicates a frame reference. This new reference has to be deleted with
      freeFrame_\ () when it is no longer needed.

----------

   .. _freeFrame:

   void freeFrame(const VSFrameRef_ \*f)

      Deletes a frame reference, releasing the caller's ownership of the frame.

      It is safe to pass NULL.

      Don't try to use the frame once the reference has been deleted.

----------

   .. _getStride:

   int getStride(const VSFrameRef_ \*f, int plane)

      Returns the distance in bytes between two consecutive lines of a plane of
      a frame. The stride is always positive.

      Passing an invalid plane number will cause a fatal error.

----------

   .. _getReadPtr:

   const uint8_t \*getReadPtr(const VSFrameRef_ \*f, int plane)

      Returns a read-only pointer to a plane of a frame.

      Passing an invalid plane number will cause a fatal error.

      .. note::
         Don't assume all three planes of a frame are allocated in one
         contiguous chunk (they're not).

----------

   .. _getWritePtr:

   uint8_t \*getWritePtr(VSFrameRef_ \*f, int plane)

      Returns a read/write pointer to a plane of a frame.

      Passing an invalid plane number will cause a fatal error.

      .. note::
         Don't assume all three planes of a frame are allocated in one
         contiguous chunk (they're not).

----------

   .. _getFrameFormat:

   const VSFormat_ \*getFrameFormat(const VSFrameRef_ \*f)

      Retrieves the format of a frame.

----------

   .. _getFrameWidth:

   int getFrameWidth(const VSFrameRef_ \*f, int plane)

      Returns the width of a plane of a given frame, in pixels. The width
      depends on the plane number because of the possible chroma subsampling.

----------

   .. _getFrameHeight:

   int getFrameHeight(const VSFrameRef_ \*f, int plane)

      Returns the height of a plane of a given frame, in pixels. The height
      depends on the plane number because of the possible chroma subsampling.

----------

   .. _copyFrameProps:

   void copyFrameProps(const VSFrameRef_ \*src, VSFrameRef_ \*dst, VSCore_ \*core)

      Copies the property map of a frame to another frame, overwriting all
      existing properties.

----------

   .. _getFramePropsRO:

   const VSMap_ \*getFramePropsRO(const VSFrameRef_ \*f)

      Returns a read-only pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _getFramePropsRW:

   VSMap_ \*getFramePropsRW(VSFrameRef_ \*f)

      Returns a read/write pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _cloneNodeRef:

   VSNodeRef_ \*cloneNodeRef(VSNodeRef_ \*node)

      Duplicates a node reference. This new reference has to be deleted with
      freeNode_\ () when it is no longer needed.

----------

   .. _freeNode:

   void freeNode(VSNodeRef_ \*node)

      Deletes a node reference, releasing the caller's ownership of the node.

      It is safe to pass NULL.

      Don't try to use the node once the reference has been deleted.

----------

   .. _getFrame:

   const VSFrameRef_ \*getFrame(int n, VSNodeRef_ \*node, char \*errorMsg, int bufSize)

      Generates a frame directly. The frame is available when the function
      returns.

      This function is meant for external applications using the core as a
      library, or if frame requests are necessary during a filter's
      initialization.
      
      Thread-safe.

      *n*
         The frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *errorMsg*
         Pointer to a buffer of *bufSize* bytes to store a possible error
         message. Can be NULL if no error message is wanted.
         
      *bufSize*
         Maximum length for the error message, in bytes (including the
         trailing '\0'). Can be 0 if no error message is wanted.

      Returns a reference to the generated frame, or NULL in case of failure.
      The ownership of the frame is transferred to the caller.

      .. warning::
         Never use inside a filter's "getframe" function.

----------

   .. _getFrameAsync:

   void getFrameAsync(int n, VSNodeRef_ \*node, VSFrameDoneCallback callback, void \*userData)

      Requests the generation of a frame. When the frame is ready,
      a user-provided function is called.
      
      This function is meant for applications using VapourSynth as a library.
      
      Thread-safe.

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
            Contains a reference to the generated frame, or NULL in case of failure.
            The ownership of the frame is transferred to the caller.

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

   const VSFrameRef_ \*getFrameFilter(int n, VSNodeRef_ \*node, VSFrameContext_ \*frameCtx)

      Retrieves a frame that was previously requested with
      requestFrameFilter_\ ().

      Only use inside a filter's "getframe" function.

      A filter usually calls this function when its activation reason is
      arAllFramesReady or arFrameReady. See VSActivationReason_.

      It is safe to retrieve a frame more than once, but each reference
      needs to be freed.

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

   void requestFrameFilter(int n, VSNodeRef_ \*node, VSFrameContext_ \*frameCtx)

      Requests a frame from a node and returns immediately.

      Only use inside a filter's "getframe" function.

      A filter usually calls this function when its activation reason is
      arInitial. The requested frame can then be retrieved using
      getFrameFilter_\ (), when the filter's activation reason is
      arAllFramesReady or arFrameReady. See VSActivationReason_.

      It is safe to request a frame more than once. An unimportant consequence
      of requesting a frame more than once is that the getframe function may
      be called more than once for the same frame with reason arFrameReady.

      It is best to request frames in ascending order, i.e. n, n+1, n+2, etc.

      *n*
         The frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *frameCtx*
         The context passed to the filter's "getframe" function.

----------

   .. _getVideoInfo:

   const VSVideoInfo_ \*getVideoInfo(VSNodeRef_ \*node)

      Returns a pointer to the video info associated with a node. The pointer is
      valid as long as the node lives.

----------

   .. _setVideoInfo:

   void setVideoInfo(const VSVideoInfo_ \*vi, int numOutputs, VSNode_ \*node)

      Sets the node's video info.

      *vi*
         Pointer to *numOutputs* VSVideoInfo_ instances. The structures are
         copied by the core. The flags are however ignored and replaced by the
         flags passed to _createFilter.

      *numOutputs*
         Number of clips the filter wants to return. Must be greater than 0.

      *node*
         Pointer to the node whose video info is to be set.

----------

   .. _getFormatPreset:

   const VSFormat_ \*getFormatPreset(int id, VSCore_ \*core)

      Returns a VSFormat structure from a video format identifier.

      Thread-safe.

      *id*
         The format identifier: one of VSPresetFormat_ or a custom registered
         format.

      Returns NULL if the identifier is not known.

----------

   .. _registerFormat:

   const VSFormat_ \*registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore_ \*core)

      Registers a custom video format.

      Thread-safe.

      *colorFamily*
         One of VSColorFamily_.

         .. note::
            Registering compat formats is not allowed. Only certain privileged
            built-in filters are allowed to handle compat formats.

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
      as the VSCore_ instance lives. Returns NULL in case an invalid format
      is described.

      If the parameters specify a format that is already registered (including
      preset formats), then no new format is created and the existing one is
      returned.

----------

   .. _createMap:

   VSMap_ \*createMap(void)

      Creates a new property map. It must be deallocated later with
      freeMap_\ ().

----------

   .. _freeMap:

   void freeMap(VSMap_ \*map)

      Frees a map and all the objects it contains.

----------

   .. _clearMap:

   void clearMap(VSMap_ \*map)

      Deletes all the keys and their associated values from the map, leaving it
      empty.

----------

   .. _setError:

   void setError(VSMap_ \*map, const char \*errorMessage)

      Adds an error message to a map. The map is cleared first. The error
      message is copied. In this state the map may only be freed, cleared
      or queried for the error message.

      For errors encountered in a filter's "getframe" function, use
      setFilterError_.

----------

   .. _getError:

   const char \*getError(const VSMap_ \*map)

      Returns a pointer to the error message contained in the map,
      or NULL if there is no error message. The pointer is valid as long as
      the map lives.

----------

   .. _propNumKeys:

   int propNumKeys(const VSMap_ \*map)

      Returns the number of keys contained in a property map.

----------

   .. _propGetKey:

   const char \*propGetKey(const VSMap_ \*map, int index)

      Returns a key from a property map.

      Passing an invalid *index* will cause a fatal error.

      The pointer is valid as long as the key exists in the map.

----------

   .. _propDeleteKey:

   int propDeleteKey(VSMap_ \*map, const char \*key)

      Removes the property with the given key. All values associated with the
      key are lost.

      Returns 0 if the key isn't in the map. Otherwise it returns 1.

----------

   .. _propGetType:

   char propGetType(const VSMap_ \*map, const char \*key)

      Returns the type of the elements associated with the given key in a
      property map.

      The returned value is one of VSPropTypes_. If there is no such key in the
      map, the returned value is ptUnset.

----------

   .. _propNumElements:

   int propNumElements(const VSMap_ \*map, const char \*key)

      Returns the number of elements associated with a key in a property map.
      Returns -1 if there is no such key in the map.

----------

   .. _propGetInt:

   int64_t propGetInt(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves an integer from a map.

      Returns the number on success, or 0 in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetIntArray:

   const int64_t \*propGetIntArray(const VSMap_ \*map, const char \*key, int \*error)

      Retrieves an array of integers from a map. Use this function if there
      are a lot of numbers associated with a key, because it is faster than
      calling propGetInt_\ () in a loop.

      Returns a pointer to the first element of the array on success, or NULL
      in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      Use propNumElements_\ () to know the total number of elements associated
      with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

      This function was introduced in API R3.1 (VapourSynth R26).

----------

   .. _propGetFloat:

   double propGetFloat(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a floating point number from a map.

      Returns the number on success, or 0 in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFloatArray:

   const double \*propGetFloatArray(const VSMap_ \*map, const char \*key, int \*error)

      Retrieves an array of floating point numbers from a map. Use this
      function if there are a lot of numbers associated with a key, because
      it is faster than calling propGetFloat_\ () in a loop.

      Returns a pointer to the first element of the array on success, or NULL
      in case of error.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      Use propNumElements_\ () to know the total number of elements associated
      with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

      This function was introduced in API R3.1 (VapourSynth R26).

----------

   .. _propGetData:

   const char \*propGetData(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves arbitrary binary data from a map.

      Returns a pointer to the data on success, or NULL in case of error.

      The array returned is guaranteed to be NULL-terminated. The NULL
      byte is not considered to be part of the array (propGetDataSize_
      doesn't count it).

      The pointer is valid until the map is destroyed, or until the
      corresponding key is removed from the map or altered.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetDataSize:

   int propGetDataSize(const VSMap_ \*map, const char \*key, int index, int \*error)

      Returns the size in bytes of a property of type ptData (see
      VSPropTypes_), or 0 in case of error. The terminating NULL byte
      added by propSetData_\ () is not counted.



----------

   .. _propGetNode:

   VSNodeRef_ \*propGetNode(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a node from a map.

      Returns a pointer to the node on success, or NULL in case of error.

      This function increases the node's reference count, so freeNode_\ () must
      be used when the node is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFrame:

   const VSFrameRef_ \*propGetFrame(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a frame from a map.

      Returns a pointer to the frame on success, or NULL in case of error.

      This function increases the frame's reference count, so freeFrame_\ () must
      be used when the frame is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propGetFunc:

   VSFuncRef_ \*propGetFunc(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a function from a map.

      Returns a pointer to the function on success, or NULL in case of error.

      This function increases the function's reference count, so freeFunc_\ () must
      be used when the function is no longer needed.

      If the map has an error set (i.e. if getError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.

      *index*
         Zero-based index of the element.

         Use propNumElements_\ () to know the total number of elements associated
         with a key.

      *error*
         One of VSGetPropErrors_, or 0 on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _propSetInt:

   int propSetInt(VSMap_ \*map, const char \*key, int64_t i, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *i*
         Value to store.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetIntArray:

   int propSetIntArray(VSMap_ \*map, const char \*key, const int64_t \*i, int size)

      Adds an array of integers to a map. Use this function if there are a
      lot of numbers to add, because it is faster than calling propSetInt_\ ()
      in a loop.

      If *map* already contains a property with this *key*, that property will
      be overwritten and all old values will be lost.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *i*
         Pointer to the first element of the array to store.

      *size*
         Number of integers to read from the array. It can be 0, in which case
         no integers are read from the array, and the property will be created
         empty.

      Returns 0 on success, or 1 if *size* is negative.

      This function was introduced in API R3.1 (VapourSynth R26).

----------

   .. _propSetFloat:

   int propSetFloat(VSMap_ \*map, const char \*key, double d, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *d*
         Value to store.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetFloatArray:

   int propSetFloatArray(VSMap_ \*map, const char \*key, const double \*d, int size)

      Adds an array of floating point numbers to a map. Use this function if
      there are a lot of numbers to add, because it is faster than calling
      propSetFloat_\ () in a loop.

      If *map* already contains a property with this *key*, that property will
      be overwritten and all old values will be lost.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *d*
         Pointer to the first element of the array to store.

      *size*
         Number of floating point numbers to read from the array. It can be 0,
         in which case no numbers are read from the array, and the property
         will be created empty.

      Returns 0 on success, or 1 if *size* is negative.

      This function was introduced in API R3.1 (VapourSynth R26).

----------

   .. _propSetData:

   int propSetData(VSMap_ \*map, const char \*key, const char \*data, int size, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *data*
         Value to store.

         This function copies the data, so the pointer should be freed when
         no longer needed.

      *size*
         The number of bytes to copy. If this is negative, everything up to
         the first NULL byte will be copied.

         This function will always add a NULL byte at the end of the data.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _propSetNode:

   int propSetNode(VSMap_ \*map, const char \*key, VSNodeRef_ \*node, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

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

   int propSetFrame(VSMap_ \*map, const char \*key, const VSFrameRef_ \*f, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

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

   int propSetFunc(VSMap_ \*map, const char \*key, VSFuncRef_ \*func, int append)

      Adds a property to a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *func*
         Value to store.

         This function will increase the function's reference count, so the
         pointer should be freed when no longer needed.

      *append*
         One of VSPropAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _getPluginById:

   VSPlugin_ \*getPluginById(const char \*identifier, VSCore_ \*core)

      Returns a pointer to the plugin with the given identifier, or NULL
      if not found.

      *identifier*
         Reverse URL that uniquely identifies the plugin.

----------

   .. _getPluginByNs:

   VSPlugin_ \*getPluginByNs(const char \*ns, VSCore_ \*core)

      Returns a pointer to the plugin with the given namespace, or NULL
      if not found.

      getPluginById_ should be used instead.

      *ns*
         Namespace.

----------

   .. _getPlugins:

   VSMap_ \*getPlugins(VSCore_ \*core)

      Returns a map containing a list of all loaded plugins. The map
      must be freed when no longer needed.

      Keys:
         Meaningless unique strings.

      Values:
         Namespace, identifier, and full name, separated by semicolons.

----------

   .. _getFunctions:

   VSMap_ \*getFunctions(VSPlugin_ \*plugin)

      Returns a map containing a list of the filters exported by a plugin.
      The map must be freed when no longer needed.

      Keys:
         The filter names.

      Values:
         The filter name followed by its argument string, separated by a semicolon.

----------

   .. _getPluginPath:

   const char \*getPluginPath(const VSPlugin_ \*plugin)

      Returns the absolute path to the plugin, including the plugin's file
      name. This is the real location of the plugin, i.e. there are no
      symbolic links in the path.

      Path elements are always delimited with forward slashes.

      VapourSynth retains ownership of the returned pointer.

      This function was introduced in VapourSynth R25 without bumping
      the API version (R3).

----------

   .. _createFunc:

   VSFuncRef_ \*createFunc(VSPublicFunction func, void \*userData, VSFreeFuncData free, VSCore \*core, const VSAPI \*vsapi)

      *func*
         typedef void (VS_CC \*VSPublicFunction)(const VSMap_ \*in, VSMap_ \*out, void \*userData, VSCore_ \*core, const VSAPI_ \*vsapi)

         User-defined function that may be called in any context.

      *userData*
         Pointer passed to *func*.

      *free*
         typedef void (VS_CC \*VSFreeFuncData)(void \*userData)

         Callback tasked with freeing *userData*.

----------

   .. _cloneFuncRef:

   VSFuncRef_ \*cloneFuncRef(VSFuncRef_ \*f)

      Duplicates a func reference. This new reference has to be deleted with
      freeFunc_\ () when it is no longer needed.

----------

   .. _callFunc:

   void callFunc(VSFuncRef_ \*func, const VSMap_ \*in, VSMap_ \*out, VSCore_ \*core, const VSAPI_ \*vsapi)

      Calls a function. If the call fails *out* will have an error set.
      
      *func*
         Function to be called.

      *in*
         Arguments passed to *func*.
         
      *out*
         Returned values from *func*.

      *core*
         Must be NULL.
         
      *vsapi*
         Must be NULL.

----------

   .. _freeFunc:

   void freeFunc(VSFuncRef_ \*f)

      Deletes a function reference, releasing the caller's ownership of the function.

      It is safe to pass NULL.

      Don't try to use the function once the reference has been deleted.

----------

   .. _createFilter:

   void createFilter(const VSMap_ \*in, VSMap_ \*out, const char \*name, VSFilterInit_ init, VSFilterGetFrame_ getFrame, VSFilterFree_ free, int filterMode, int flags, void \*instanceData, VSCore_ \*core)

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
         A pointer to the private filter data. This pointer will be passed to
         the *init*, *getFrame*, and *free* functions. It should be freed by
         the *free* function.

      After this function returns, *out* will contain the new node(s) in the
      "clip" property, or an error, if something went wrong.

----------

   .. _registerFunction:

   void registerFunction(const char \*name, const char \*args, VSPublicFunction argsFunc, void \*functionData, VSPlugin_ \*plugin)

      See VSInitPlugin_.

----------

   .. _invoke:

   VSMap_ \*invoke(VSPlugin_ \*plugin, const char \*name, const VSMap_ \*args)

      Invokes a filter.

      invoke() makes sure the filter has no compat input nodes, checks that
      the *args* passed to the filter are consistent with the argument list
      registered by the plugin that contains the filter, calls the filter's
      "create" function, and checks that the filter doesn't return any compat
      nodes. If everything goes smoothly, the filter will be ready to generate
      frames after invoke() returns.

      Thread-safe.

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
      with the key "clip". The exception to this are functions, for example
      LoadPlugin, which doesn't return any clips for obvious reasons.

----------

   .. _setFilterError:

   void setFilterError(const char \*errorMessage, VSFrameContext_ \*frameCtx)

      Adds an error message to a frame context, replacing the existing message,
      if any.

      This is the way to report errors in a filter's "getframe" function.
      Such errors are not necessarily fatal, i.e. the caller can try to
      request the same frame again.

----------

   .. _getOutputIndex:

   int getOutputIndex(VSFrameContext_ \*frameCtx)

      Returns the index of the node from which the frame is being requested.

      Only needed in the "getframe" function of filters that return more than
      one clip.

----------

   .. _queryCompletedFrame:

   void queryCompletedFrame(VSNodeRef_ \**node, int \*n, VSFrameContext_ \*frameCtx)

      .. warning::
         This function has several issues and may or may not return the
         actual node or frame number.
         
      Finds out which requested frame is ready. To be used in a filter's
      "getframe" function, when it is called with *activationReason*
      arFrameReady.

----------

   .. _releaseFrameEarly:

   void releaseFrameEarly(VSNodeRef_ \*node, int n, VSFrameContext_ \*frameCtx)

      Normally a reference is kept to all requested frames until the current frame is complete.
      If a filter scans a large number of frames this can consume all memory, instead the filter
      should release the internal frame references as well immediately by calling this function.

      Only use inside a filter's "getframe" function.


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
         shouldn't be too long. Additionally, words that are special to
         Python, e.g. "del", should be avoided.

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
         initialised, and createFilter_\ () should be called. This is where
         the filter should perform any other initialisation it requires.

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

   This is the only place where setVideoInfo_\ () can be called. There is no
   reason to do anything else here.

   If an error occurs in this function:
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

   It is never called concurrently for the same frame number.

   *n*
      Requested frame number.

   *activationReason*
      One of VSActivationReason_.

      This function is first called with *activationReason* arInitial. At this
      point the function should request the input frames it needs and return
      NULL. When one or all of the requested frames are ready, this function
      is called again with *activationReason* arFrameReady or arAllFramesReady.
      The function should only return a frame when called with
      *activationReason* arAllFramesReady.

      In the case of arFrameReady, use queryCompletedFrame_\ () to find out
      which of the requested frames is ready.

      Most filters will only need to handle arInitial and arAllFramesReady.

   *instanceData*
      The filter's private instance data.

   *frameData*
      Optional private data associated with output frame number *n*.
      It must be deallocated before the last call for the given frame
      (arAllFramesReady or error).

      By default, *frameData* is a pointer to NULL.

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
