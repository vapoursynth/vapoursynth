VapourSynth4.h
==============

Table of contents
#################

Introduction_


Macros_
   VS_CC_

   VS_EXTERNAL_API_

   VAPOURSYNTH_API_MAJOR_

   VAPOURSYNTH_API_MINOR_

   VAPOURSYNTH_API_VERSION_
   
   VS_AUDIO_FRAME_SAMPLES_
   
   VS_MAKE_VERSION_
   

Enums_
   VSColorFamily_

   VSSampleType_

   VSPresetVideoFormat_

   VSFilterMode_

   VSMediaType_
   
   VSAudioChannels_

   VSPropertyType_

   VSMapPropertyError_

   VSMapAppendMode_

   VSActivationReason_

   VSMessageType_

   VSCoreCreationFlags_
   
   VSPluginConfigFlags_
   
   VSDataTypeHint_
   
   VSRequestPattern_
   
   VSCacheMode_
   

Structs_
   VSFrame_

   VSNode_

   VSCore_

   VSPlugin_
   
   VSPluginFunction_

   VSFunction_

   VSMap_
   
   VSLogHandle_

   VSFrameContext_

   VSVideoFormat_

   VSVideoInfo_
   
   VSAudioFormat_
   
   VSAudioInfo_

   VSCoreInfo_
   
   VSFilterDependency_

   VSPLUGINAPI_

   VSAPI_

      * Functions that deal with the core:

          * createCore_

          * freeCore_

          * setMaxCacheSize_
          
          * setThreadCount_
          
          * getCoreInfo_
          
          * getAPIVersion_
          
      * Functions that deal with logging
          
          * addLogHandler_
          
          * removeLogHandler_

          * logMessage_

      * Functions that deal with frames:

          * newVideoFrame_

          * newVideoFrame2_
          
          * newAudioFrame_

          * newAudioFrame2_
          
          * freeFrame_
          
          * addFrameRef_
          
          * copyFrame_

          * getFramePropertiesRO_

          * getFramePropertiesRW_

          * getStride_

          * getReadPtr_

          * getWritePtr_

          * getVideoFrameFormat_
          
          * getAudioFrameFormat_
          
          * getFrameType_

          * getFrameWidth_

          * getFrameHeight_

          * getFrameLength_

      * Functions that deal with filters and nodes:
      
          * createVideoFilter_

          * createVideoFilter2_
          
          * createAudioFilter_

          * createAudioFilter2_
          
          * setLinearFilter_
          
          * setCacheMode_
          
          * setCacheOptions_

          * freeNode_
          
          * addNodeRef_
          
          * getNodeType_

          * getVideoInfo_

          * getAudioInfo_    

      * Functions that deal with formats:
      
          * getVideoFormatName_
          
          * getAudioFormatName_
          
          * queryVideoFormat_
          
          * queryAudioFormat_

          * queryVideoFormatID_

          * getVideoFormatByID_

      * Functions that deal with maps:

          * createMap_

          * freeMap_

          * clearMap_

          * mapGetError_
          
          * mapSetError_

          * mapNumKeys_

          * mapGetKey_

          * mapDeleteKey_

          * mapNumElements_
          
          * mapGetType_
          
          * mapSetEmpty_

          * mapGetInt_
          
          * mapGetIntSaturated_

          * mapGetIntArray_
          
          * mapSetInt_

          * mapSetIntArray_

          * mapGetFloat_
          
          * mapGetFloatSaturated_

          * mapGetFloatArray_
          
          * mapSetFloat_

          * mapSetFloatArray_

          * mapGetData_

          * mapGetDataSize_
          
          * mapGetDataTypeHint_

          * mapSetData_

          * mapGetNode_
          
          * mapSetNode_
          
          * mapConsumeNode_

          * mapGetFrame_
          
          * mapSetFrame_
          
          * mapConsumeFrame_

          * mapGetFunction_

          * mapSetFunction_
          
          * mapConsumeFunction_

      * Functions that deal with plugins and plugin functions:
      
          * registerFunction_

          * getPluginByID_

          * getPluginByNamespace_

          * getNextPlugin_
          
          * getPluginName_
          
          * getPluginID_
          
          * getPluginNamespace_
          
          * getNextPluginFunction_
          
          * getPluginFunctionByName_
          
          * getPluginFunctionName_
          
          * getPluginFunctionArguments_
          
          * getPluginFunctionReturnType_

          * getPluginPath_
          
          * getPluginVersion_
          
          * invoke_

      * Functions that deal with wrapped external functions:

          * createFunction_
          
          * freeFunction_

          * addFunctionRef_

          * callFunction_

      * Functions that are used to fetch frames and inside filters:

          * getFrame_

          * getFrameAsync_

          * getFrameFilter_

          * requestFrameFilter_        

          * releaseFrameEarly_
          
          * cacheFrame_
          
          * setFilterError_
          

Functions_
   getVapourSynthAPI_


`Writing plugins`_
   VSInitPlugin_

   VSFilterGetFrame_

   VSFilterFree_


Introduction
############

This is VapourSynth's main header file. Plugins and applications that use
the library must include it.

VapourSynth's public API is all C.


Macros
######

VapourSynth4.h defines some preprocessor macros that make the programmer's life
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

   VS_EXTERNAL_API(void) VapourSynthPluginInit2(...) { ... }


VAPOURSYNTH_API_MAJOR
---------------------

Major API version.


VAPOURSYNTH_API_MINOR
---------------------

Minor API version. It is bumped when new functions are added to VSAPI_ or core behavior is noticeably changed.


VAPOURSYNTH_API_VERSION
-----------------------

API version. The high 16 bits are VAPOURSYNTH_API_MAJOR_, the low 16
bits are VAPOURSYNTH_API_MINOR_.


VS_AUDIO_FRAME_SAMPLES
----------------------

The number of audio samples in an audio frame. It is a static number to make it possible to calculate which audio frames are needed to retrieve specific samples.


VS_MAKE_VERSION
---------------

Used to create version numbers. The first argument is the major version and second is the minor.


Enums
#####

.. _VSColorFamily:

enum VSColorFamily
------------------

   * cfUndefined

   * cfGray

   * cfRGB

   * cfYUV


.. _VSSampleType:

enum VSSampleType
-----------------

   * stInteger

   * stFloat


.. _VSPresetVideoFormat:

enum VSPresetVideoFormat
------------------------

   The presets suffixed with H and S have floating point sample type.
   The H and S suffixes stand for half precision and single precision,
   respectively. All formats are planar. See the header for all currently
   defined video format presets.
   
   * pf\*


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

   * fmFrameState

     For compatibility with other filtering architectures. DO NOT USE IN NEW FILTERS.
     The filter's "getframe" function only ever gets called from one thread at a
     time. Unlike fmUnordered, only one frame is processed at a time. 


.. _VSMediaType:

enum VSMediaType
----------------

   Used to indicate the type of a `VSFrame` or `VSNode` object.

   * mtVideo

   * mtAudio


.. _VSAudioChannels:

enum VSAudioChannels
--------------------

   Audio channel positions as an enum. Mirrors the FFmpeg audio channel constants in older api versions. See the header for all available values.
   
   * ac\*
   

.. _VSPropertyType:

enum VSPropertyType
-------------------

   Types of properties that can be stored in a VSMap.

   * ptUnset

   * ptInt

   * ptFloat

   * ptData
   
   * ptFunction

   * ptVideoNode
   
   * ptAudioNode

   * ptVideoFrame
   
   * ptAudioFrame


.. _VSMapPropertyError:

enum VSMapPropertyError
-----------------------

   When a mapGet* function fails, it returns one of these in the *err*
   parameter.

   All errors are non-zero.
   
   * peSuccess

   * peUnset

     The requested key was not found in the map.

   * peType

     The wrong function was used to retrieve the property. E.g.
     mapGetInt_\ () was used on a property of type ptFloat.

   * peIndex

     The requested index was out of bounds.
     
   * peError
   
     The map has the error state set.


.. _VSMapAppendMode:

enum VSMapAppendMode
---------------------

   Controls the behaviour of mapSetInt_\ () and friends.

   * maReplace

     All existing values associated with the key will be replaced with
     the new value.

   * maAppend

     The new value will be appended to the list of existing values
     associated with the key.


.. _VSActivationReason:

enum VSActivationReason
-----------------------

   See VSFilterGetFrame_.

   * arInitial

   * arAllFramesReady

   * arError


.. _VSMessageType:

enum VSMessageType
------------------

   See addLogHandler_\ ().

   * mtDebug
   
   * mtInformation

   * mtWarning

   * mtCritical

   * mtFatal


.. _VSCoreCreationFlags:

enum VSCoreCreationFlags
------------------------

   Options when creating a core.

   * ccfEnableGraphInspection
   
      Required to use the graph inspection api functions. Increases memory usage due to the extra information stored.
   
   * ccfDisableAutoLoading
   
      Don't autoload any user plugins. Core plugins are always loaded.
      
   * ccfDisableLibraryUnloading
   
      Don't unload plugin libraries when the core is destroyed. Due to a small amount of memory leaking every load
      and unload (windows feature, not my fault) of a library this may help in applications with extreme amount of script reloading.
   
   
.. _VSPluginConfigFlags:

enum VSPluginConfigFlags
------------------------

   Options when loading a plugin.

   * pcModifiable
   
      Allow functions to be added to the plugin object after the plugin loading phase. Mostly useful for
      Avisynth compatibility and other foreign plugin loaders.
   
   
.. _VSDataTypeHint:

enum VSDataTypeHint
-------------------

   Since the data type can contain both pure binary data and printable strings the type also contains a hint
   for whether or not it is human readable. Generally the unknown type should be very rare and is almost only
   created as an artifact of API3 compatibility.

   * dtUnknown
   
   * dtBinary
   
   * dtUtf8


.. _VSRequestPattern:

enum VSRequestPattern
---------------------

   Describes the upstream frame request pattern of a filter.

   * rpGeneral
   
      Anything goes. Note that filters that may be requesting beyond the end of a VSNode length in frames (repeating the last frame) should use *rpGeneral* and not any of the other modes.
   
   * rpNoFrameReuse
   
     Will only request an input frame at most once if all output frames are requested exactly one time. This includes filters such as Trim, Reverse, SelectEvery.
   
   * rpStrictSpatial
   
     Only requests frame N to output frame N. The main difference to *rpNoFrameReuse* is that the requested frame is always fixed and known ahead of time. Filter examples Lut, Expr (conditionally, see *rpGeneral* note) and similar.

   
.. _VSCacheMode:

enum VSCacheMode
----------------

   Describes how the output of a node is cached.

   * cmAuto
   
      Cache is enabled or disabled based on the reported request patterns and number of consumers.
   
   * cmForceDisable
   
      Never cache anything.
   
   * cmForceEnable

      * Always use the cache.


Structs
#######

Most structs are opaque and their contents can only be accessed using functions in the API.


.. _VSFrame:

struct VSFrame
-----------------

   A frame that can hold audio or video data.

   Each row of pixels in a frame is guaranteed to have an alignment of at least 32
   bytes. Two frames with the same width and bytes per sample are guaranteed to have the same stride.
   
   Audio data is also guaranteed to be at least 32 byte aligned.

   Any data can be attached to a frame, using a VSMap_.


.. _VSNode:

struct VSNode
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
   VapourSynthPluginInit2() function.

   A VapourSynth plugin can export any number of filters.

   Plugins have a few attributes:

      - An identifier, which must be unique among all VapourSynth plugins in
        existence, because this is what the core uses to make sure a plugin
        only gets loaded once.

      - A namespace, also unique. The filters exported by a plugin end up in
        the plugin's namespace.

      - A full name, which is used by the core in a few error messages.
      
      - The version of the plugin.

      - The VapourSynth API version the plugin requires.

      - A file name.

   Things you can do with a VSPlugin:

      - Enumerate all the filters it exports, using getNextPluginFunction_\ ().

      - Invoke one of its filters, using invoke_\ ().

      - Get its location in the file system, using getPluginPath_\ ().

   All loaded plugins (including built-in) can be enumerated with
   getNextPlugin_\ ().

   Once loaded, a plugin only gets unloaded when the VapourSynth core is freed.
   
   
.. _VSPluginFunction:

struct VSPluginFunction
-----------------------

   A function belonging to a Vapoursynth plugin. This object primarily exists so
   a plugin's name, argument list and return type can be queried by editors.
   
   One peculiarity is that plugin functions cannot be invoked using a `VSPluginFunction`
   pointer but is instead done using invoke_\ () which takes a `VSPlugin` and
   the function name as a string.


.. _VSFunction:

struct VSFunction
-----------------

   Holds a reference to a function that may be called. This type primarily exists
   so functions can be shared between the scripting layer and plugins in the core.


.. _VSMap:

struct VSMap
------------

   VSMap is a container that stores (key,value) pairs. The keys are strings
   and the values can be (arrays of) integers, floating point numbers,
   arrays of bytes, VSNode_, VSFrame_, or VSFunction_.

   The pairs in a VSMap are sorted by key.

   In VapourSynth, VSMaps have several uses:
      - storing filters' arguments and return values

      - storing user-defined functions' arguments and return values

      - storing the properties attached to frames

   Only alphanumeric characters and the underscore may be used in keys.

   Creating and destroying a map can be done with createMap_\ () and
   freeMap_\ (), respectively.

   A map's contents can be retrieved and modified using a number of functions,
   all prefixed with "map".

   A map's contents can be erased with clearMap_\ ().


.. _VSLogHandle:

struct VSLogHandle
------------------

   Opaque type representing a registered logger.


.. _VSFrameContext:

struct VSFrameContext
---------------------

   Opaque type representing the current frame request in a filter.


.. _VSVideoFormat:

struct VSVideoFormat
--------------------

   Describes the format of a clip. 
   
   Use queryVideoFormat_\ () to fill it in with proper error checking. Manually filling out the struct is allowed but discouraged
   since illegal combinations of values will cause undefined behavior.
   
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


.. _VSVideoInfo:

struct VSVideoInfo
------------------

   Contains information about a clip.

   .. c:member:: VSVideoFormat format

      Format of the clip. Will have *colorFamily* set to *cfUndefined* if the format can vary.

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
      
      
.. _VSAudioFormat:

struct VSAudioFormat
--------------------

   Describes the format of a clip. 
   
   Use queryAudioFormat_\ () to fill it in with proper error checking. Manually filling out the struct is allowed but discouraged
   since illegal combinations of values will cause undefined behavior.

   .. c:member:: int sampleType

      See VSSampleType_.

   .. c:member:: int bitsPerSample

      Number of significant bits.

   .. c:member:: int bytesPerSample

      Number of bytes needed for a sample. This is always a power of 2 and the
      smallest possible that can fit the number of bits used per sample.

   .. c:member:: int numChannels

      Number of audio channels.

   .. c:member:: uint64_t channelLayout

      A bitmask representing the channels present using the constants in 1 left shifted by the constants in VSAudioChannels_. 


.. _VSAudioInfo:

struct VSAudioInfo
------------------

   Contains information about a clip.

   .. c:member:: VSAudioFormat format

      Format of the clip. Unlike video the audio format can never change.

   .. c:member:: int sampleRate

      Sample rate.   
      
   .. c:member:: int64_t numSamples

      Length of the clip in audio samples.   

   .. c:member:: int numFrames

      Length of the clip in audio frames.   
      

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


.. _VSFilterDependency:

struct VSFilterDependency
-------------------------

   Contains information about a VSCore_ instance.

   .. c:member:: VSNode *source

      The node frames are requested from.

   .. c:member:: int requestPattern

      A value from VSRequestPattern_.
      

.. _VSPLUGINAPI:

struct VSPLUGINAPI
------------------
  
   This struct is used to access VapourSynth's API when a plugin is initially loaded.

----------

   int getAPIVersion()
   
      See getAPIVersion_\ () in the struct VSAPI_.

----------

   .. _configPlugin:

   int configPlugin(const char \*identifier, const char \*pluginNamespace, const char \*name, int pluginVersion, int apiVersion, int flags, VSPlugin \*plugin)
   
      Used to provide information about a plugin when loaded. Must be called exactly once from the *VapourSynthPluginInit2* entry point.
      It is recommended to use the VS_MAKE_VERSION_ macro when providing the *pluginVersion*. If you don't know the specific *apiVersion* you actually require simply
      pass VAPOURSYNTH_API_VERSION_ to match the header version you're compiling against. The *flags* consist of values from VSPluginConfigFlags_ ORed together
      but should for most plugins typically be 0.
      
      Returns non-zero on success.

----------

   int registerFunction(const char \*name, const char \*args, const char \*returnType, VSPublicFunction argsFunc, void \*functionData, VSPlugin \*plugin)
   
      See registerFunction_\ () in the struct VSAPI_.


.. _VSAPI:

struct VSAPI
------------

   This giant struct is the way to access VapourSynth's public API.

----------

   .. _createCore:

   VSCore_ \*createCore(int flags)

      Creates the VapourSynth processing core and returns a pointer to it. It is
      possible to create multiple cores but in most cases it shouldn't be needed.

      *flags*
         `VSCoreCreationFlags` ORed together if desired. Pass 0 for sane defaults
         that should suit most uses.

----------

   .. _freeCore:

   void freeCore(VSCore_ \*core)

      Frees a core. Should only be done after all frame requests have completed
      and all objects belonging to the core have been released.

----------

   .. _setMaxCacheSize:

   int64_t setMaxCacheSize(int64_t bytes, VSCore_ \*core)

      Sets the maximum size of the framebuffer cache. Returns the new maximum
      size.

----------

   .. _setThreadCount:

   int setThreadCount(int threads, VSCore_ \*core)

      Sets the number of threads used for processing. Pass 0 to automatically detect.
      Returns the number of threads that will be used for processing.

----------

   .. _getCoreInfo:

   void getCoreInfo(VSCore_ \*core, VSCoreInfo_ \*info)

      Returns information about the VapourSynth core.

----------

   .. _getAPIVersion:

   int getAPIVersion()

      Returns the highest VAPOURSYNTH_API_VERSION_ the library support.

----------

   .. _logMessage:

   void logMessage(int msgType, const char \*msg, VSCore \*core)

      Send a message through VapourSynth's logging framework. See
      addLogHandler_.

      *msgType*
         The type of message. One of VSMessageType_.

         If *msgType* is mtFatal, VapourSynth will call abort() after
         delivering the message.

      *msg*
         The message.
      
----------

   .. _addLogHandler:

   VSLogHandle \*addLogHandler(VSLogHandler handler, VSLogHandlerFree free, void \*userData, VSCore_ \*core)

      Installs a custom handler for the various error messages VapourSynth
      emits. The message handler is per VSCore_ instance. Returns a unique handle.

      If no log handler is installed up to a few hundred messages are cached and
      will be delivered as soon as a log handler is attached. This behavior exists
      mostly so that warnings when auto-loading plugins (default behavior) won't disappear-

      *handler*
         typedef void (VS_CC \*VSLogHandler)(int msgType, const char \*msg, void \*userdata)

         Custom message handler. If this is NULL, the default message
         handler will be restored.

         *msgType*
            The type of message. One of VSMessageType_.

            If *msgType* is mtFatal, VapourSynth will call abort() after the
            message handler returns.

         *msg*
            The message.
            
      *free*
         typedef void (VS_CC \*VSLogHandlerFree)(void \*userData)
         
         Called when a handler is removed.

      *userData*
         Pointer that gets passed to the message handler.
                  
----------

   .. _removeLogHandler:

   int removeLogHandler(VSLogHandle \*handle, VSCore \*core)

      Removes a custom handler. Return non-zero on success and zero if
      the handle is invalid.

      *handle*
         Handle obtained from addLogHandler_\ ().

----------

   .. _newVideoFrame:

   VSFrame_ \*newVideoFrame(const VSVideoFormat_ \*format, int width, int height, const VSFrame_ \*propSrc, VSCore_ \*core)

      Creates a new video frame, optionally copying the properties attached to another
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

   VSFrame_ \*newVideoFrame2(const VSVideoFormat_ \*format, int width, int height, const VSFrame_ \**planeSrc, const int \*planes, const VSFrame_ \*propSrc, VSCore_ \*core)

      Creates a new video frame from the planes of existing frames, optionally copying
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

         const VSFrame * frames[3] = { frameA, frameB, frameC };
         const int planes[3] = { 1, 0, 2 };
         VSFrame * newFrame = vsapi->newVideoFrame2(f, w, h, frames, planes, frameB, core);
         
      The newFrame's first plane is now a copy of *frameA*'s second plane,
      the second plane is a copy of *frameB*'s first plane,
      the third plane is a copy of *frameC*'s third plane
      and the properties have been copied from *frameB*.

----------

   .. _newAudioFrame:

   VSFrame_ \*newAudioFrame(const VSAudioFormat \*format, int numSamples, const VSFrame \*propSrc, VSCore \*core)

      Creates a new audio frame, optionally copying the properties attached to another
      frame. It is a fatal error to pass invalid arguments to this function.

      The new frame contains uninitialised memory.

      *format*
         The desired audio format. Must not be NULL.

      *numSamples*
         The number of samples in the frame. All audio frames apart from the last one returned by a filter must have VS_AUDIO_FRAME_SAMPLES_.

      *propSrc*
         A frame from which properties will be copied. Can be NULL.

      Returns a pointer to the created frame. Ownership of the new frame is
      transferred to the caller.

      See also newAudioFrame2_\ ().
      
----------

   .. _newAudioFrame2:

   VSFrame_ \*newAudioFrame2(const VSAudioFormat_ \*format, int numSamples, const VSFrame_ \*\*channelSrc, const int \*channels, const VSFrame_ \*propSrc, VSCore \*core)

      Creates a new audio frame, optionally copying the properties attached to another
      frame. It is a fatal error to pass invalid arguments to this function.

      The new frame contains uninitialised memory.

      *format*
         The desired audio format. Must not be NULL.

      *numSamples*
         The number of samples in the frame. All audio frames apart from the last one returned by a filter must have VS_AUDIO_FRAME_SAMPLES_.

      *channelSrc*
         Array of frames from which channels will be copied. If any elements of
         the array are NULL, the corresponding planes in the new frame will
         contain uninitialised memory.

      *channels*
         Array of channel numbers indicating which channel to copy from the
         corresponding source frame. Note that the number refers to the nth channel
         and not a channel name constant.

      *propSrc*
         A frame from which properties will be copied. Can be NULL.

      Returns a pointer to the created frame. Ownership of the new frame is
      transferred to the caller.

      See also newVideoFrame2_\ ().

----------

   .. _freeFrame:

   void freeFrame(const VSFrame_ \*f)

      Decrements the reference count of a frame and deletes it when it reaches 0.

      It is safe to pass NULL.
      
----------

   .. _addFrameRef:

   const VSFrame_ \*addFrameRef(const VSFrame_ \*f)

      Increments the reference count of a frame. Returns *f* as a convenience.

----------

   .. _copyFrame:

   VSFrame_ \*copyFrame(const VSFrame_ \*f, VSCore_ \*core)

      Duplicates the frame (not just the reference). As the frame buffer is
      shared in a copy-on-write fashion, the frame content is not really
      duplicated until a write operation occurs. This is transparent for the user.

      Returns a pointer to the new frame. Ownership is transferred to the caller.

----------

   .. _getFramePropertiesRO:

   const VSMap_ \*getFramePropertiesRO(const VSFrame_ \*f)

      Returns a read-only pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _getFramePropertiesRW:

   VSMap_ \*getFramePropertiesRW(VSFrame_ \*f)

      Returns a read/write pointer to a frame's properties. The pointer is valid
      as long as the frame lives.

----------

   .. _getStride:

   ptrdiff_t getStride(const VSFrame_ \*f, int plane)

      Returns the distance in bytes between two consecutive lines of a plane of
      a video frame. The stride is always positive. Returns 0 if the requested
      *plane* doesn't exist or if it isn't a video frame.

----------

   .. _getReadPtr:

   const uint8_t \*getReadPtr(const VSFrame_ \*f, int plane)

      Returns a read-only pointer to a *plane* or channel of a frame.
      Returns NULL if an invalid *plane* or channel number is passed.

      .. note::
         Don't assume all three planes of a frame are allocated in one
         contiguous chunk (they're not).

----------

   .. _getWritePtr:

   uint8_t \*getWritePtr(VSFrame_ \*f, int plane)

      Returns a read-write pointer to a *plane* or channel of a frame.
      Returns NULL if an invalid *plane* or channel number is passed.

      .. note::
         Don't assume all three planes of a frame are allocated in one
         contiguous chunk (they're not).

----------

   .. _getVideoFrameFormat:

   const VSVideoFormat_ \*getVideoFrameFormat(const VSFrame_ \*f)

      Retrieves the format of a video frame.
      
----------

   .. _getAudioFrameFormat:

   const VSAudioFormat_ \*getAudioFrameFormat(const VSFrame_ \*f)

      Retrieves the format of an audio frame.
      
----------

   .. _getFrameType:

   int getFrameType(const VSFrame_ \*f)

      Returns a value from VSMediaType_ to distinguish audio and video frames.

----------

   .. _getFrameWidth:

   int getFrameWidth(const VSFrame_ \*f, int plane)

      Returns the width of a *plane* of a given video frame, in pixels. The width
      depends on the plane number because of the possible chroma subsampling. Returns 0
      for audio frames.

----------

   .. _getFrameHeight:

   int getFrameHeight(const VSFrame_ \*f, int plane)

      Returns the height of a *plane* of a given video frame, in pixels. The height
      depends on the plane number because of the possible chroma subsampling. Returns 0
      for audio frames.
      
----------

   .. _getFrameLength:

   int getFrameLength(const VSFrame_ \*f)

      Returns the number of audio samples in a frame. Always returns 1 for video frames.

----------

   .. _createVideoFilter:

   void createVideoFilter(VSMap_ \*out, const char \*name, const VSVideoInfo_ \*vi, VSFilterGetFrame_ getFrame, VSFilterFree_ free, int filterMode, const VSFilterDependency_ \*dependencies, int numDeps, void \*instanceData, VSCore_ \*core)

      Creates a new video filter node.

      *out*
         Output map for the filter node.

      *name*
         Instance name. Please make it the same as the filter's name for easy identification.

      *vi*
         The output format of the filter.

      *getFrame*
         The filter's "getframe" function. Must not be NULL.

      *free*
         The filter's "free" function. Can be NULL.

      *filterMode*
         One of VSFilterMode_. Indicates the level of parallelism
         supported by the filter.

      *dependencies*
         An array of nodes the filter requests frames from and the access pattern.
         Used to more efficiently configure caches.
         
      *numDeps*
         Length of the *dependencies* array.

      *instanceData*
         A pointer to the private filter data. This pointer will be passed to
         the *getFrame* and *free* functions. It should be freed by
         the *free* function.

      After this function returns, *out* will contain the new node appended to the
      "clip" property, or an error, if something went wrong.

----------

   .. _createVideoFilter2:

   VSNode_ \*createVideoFilter2(const char \*name, const VSVideoInfo_ \*vi, VSFilterGetFrame_ getFrame, VSFilterFree_ free, int filterMode, const VSFilterDependency_ \*dependencies, int numDeps, void \*instanceData, VSCore_ \*core)

      Identical to createVideoFilter_ except that the new node is returned
      instead of appended to the *out* map. Returns NULL on error.

----------

   .. _createAudioFilter:

   void createAudioFilter(VSMap \*out, const char \*name, const VSAudioInfo \*ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency_ \*dependencies, int numDeps, void \*instanceData, VSCore \*core)

      Creates a new video filter node.

      *out*
         Output map for the filter node.

      *name*
         Instance name. Please make it the same as the filter's name for easy identification.

      *ai*
         The output format of the filter.

      *getFrame*
         The filter's "getframe" function. Must not be NULL.

      *free*
         The filter's "free" function. Can be NULL.

      *filterMode*
         One of VSFilterMode_. Indicates the level of parallelism
         supported by the filter.

      *dependencies*
         An array of nodes the filter requests frames from and the access pattern.
         Used to more efficiently configure caches.
         
      *numDeps*
         Length of the *dependencies* array.

      *instanceData*
         A pointer to the private filter data. This pointer will be passed to
         the *getFrame* and *free* functions. It should be freed by
         the *free* function.

      After this function returns, *out* will contain the new node appended to the
      "clip" property, or an error, if something went wrong.

----------

   .. _createAudioFilter2:

   VSNode \*createAudioFilter2(const char \*name, const VSAudioInfo \*ai, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, const VSFilterDependency_ \*dependencies, int numDeps, void \*instanceData, VSCore \*core)

      Identical to createAudioFilter_ except that the new node is returned
      instead of appended to the *out* map. Returns NULL on error.

----------

   .. _setLinearFilter:

   int setLinearFilter(VSNode_ \*node)

      Must be called immediately after audio or video filter creation.
      Returns the upper bound of how many additional frames it is
      reasonable to pass to cacheFrame_ when trying to make a request
      more linear.
      
----------

   .. _setCacheMode:

   void setCacheMode(VSNode_ \*node, int mode)

      Determines the strategy for frame caching. Pass a VSCacheMode_ constant.
      Mostly useful for cache debugging since the auto mode should work well
      in just about all cases. Calls to this function may also be silently ignored.
      
      Resets the cache to default options when called, discarding setCacheOptions_
      changes.
      
----------

   .. _setCacheOptions:

   void setCacheOptions(VSNode_ \*node, int fixedSize, int maxSize, int maxHistorySize)

      Call after setCacheMode_ or the changes will be discarded. Sets internal
      details of a node's associated cache. Calls to this function may also
      be silently ignored.
      
      *fixedSize*
         Set to non-zero to make the cache always hold *maxSize* frames.
         
      *maxSize*
         The maximum number of frames to cache. Note that this value is automatically
         adjusted using an internal algorithm unless *fixedSize* is set.
         
      *maxHistorySize*
         How many frames that have been recently evicted from the cache to keep track off.
         Used to determine if growing or shrinking the cache is beneficial. Has no effect
         when *fixedSize* is set.
      
----------

   .. _freeNode:

   void freeNode(VSNode_ \*node)

      Decreases the reference count of a node and destroys it once it reaches 0.

      It is safe to pass NULL.
      
----------

   .. _addNodeRef:

   VSNode_ \*addNodeRef(VSNode_ \*node)

      Increment the reference count of a node. Returns the same *node* for convenience.

----------

   .. _getNodeType:

   int getNodeType(VSNode_ \*node)

      Returns VSMediaType_. Used to determine if a node is of audio or video type.

----------

   .. _getVideoInfo:

   const VSVideoInfo_ \*getVideoInfo(VSNode_ \*node)

      Returns a pointer to the video info associated with a node. The pointer is
      valid as long as the node lives. It is undefined behavior to pass a non-video
      node.
      
----------

   .. _getAudioInfo:

   const VSAudioInfo_ \*getAudioInfo(VSNode_ \*node)

      Returns a pointer to the audio info associated with a node. The pointer is
      valid as long as the node lives. It is undefined behavior to pass a non-audio
      node.

----------

   .. _getVideoFormatName:

   int getVideoFormatName(const VSVideoFormat \*format, char \*buffer)

      Tries to output a fairly human-readable name of a video format.
      
      *format*
         The input video format.
      
      *buffer*
         Destination buffer. At most 32 bytes including terminating NULL
         will be written.
      
      Returns non-zero on success.
      
----------

   .. _getAudioFormatName:

   int getAudioFormatName(const VSAudioFormat \*format, char \*buffer)

      Tries to output a fairly human-readable name of an audio format.
      
      *format*
         The input audio format.
      
      *buffer*
         Destination buffer. At most 32 bytes including terminating NULL
         will be written.
      
      Returns non-zero on success.

----------

   .. _queryVideoFormat:

   int queryVideoFormat(VSVideoFormat_ \*format, int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore_ \*core)

      Fills out a VSVideoInfo_ struct based on the provided arguments. Validates the arguments before filling out *format*.

      *format*
         The struct to fill out.

      *colorFamily*
         One of VSColorFamily_.

      *sampleType*
         One of VSSampleType_.

      *bitsPerSample*
         Number of meaningful bits for a single component. The valid range is
         8-32.

         For floating point formats only 16 or 32 bits are allowed.

      *subSamplingW*
         log2 of the horizontal chroma subsampling. 0 == no subsampling. The valid range is 0-4.

      *subSamplingH*
         log2 of the vertical chroma subsampling. 0 == no subsampling. The valid range is 0-4.

         .. note::
            RGB formats are not allowed to be subsampled in VapourSynth.

      Returns non-zero on success.

----------

   .. _queryAudioFormat:

   int queryAudioFormat(VSAudioFormat_ \*format, int sampleType, int bitsPerSample, uint64_t channelLayout, VSCore_ \*core)

      Fills out a VSAudioFormat_ struct based on the provided arguments. Validates the arguments before filling out *format*.

      *format*
         The struct to fill out.

      *sampleType*
         One of VSSampleType_.

      *bitsPerSample*
         Number of meaningful bits for a single component. The valid range is
         8-32.

         For floating point formats only 32 bits are allowed.

      *channelLayout*
         A bitmask constructed from bitshifted constants in VSAudioChannels_. For example stereo is expressed as (1 << acFrontLeft) | (1 << acFrontRight).

      Returns non-zero on success.
          
----------

   .. _queryVideoFormatID:

   uint32_t queryVideoFormatID(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore_ \*core)

      Get the id associated with a video format. Similar to queryVideoFormat_\ () except that it returns a format id instead
      of filling out a VSVideoInfo_ struct.

      *colorFamily*
         One of VSColorFamily_.

      *sampleType*
         One of VSSampleType_.

      *bitsPerSample*
         Number of meaningful bits for a single component. The valid range is
         8-32.

         For floating point formats, only 16 or 32 bits are allowed.

      *subSamplingW*
         log2 of the horizontal chroma subsampling. 0 == no subsampling. The valid range is 0-4.

      *subSamplingH*
         log2 of the vertical chroma subsampling. 0 == no subsampling. The valid range is 0-4.

         .. note::
            RGB formats are not allowed to be subsampled in VapourSynth.

      Returns a valid format id if the provided arguments are valid, on error
      0 is returned.
      
----------
      
   .. _getVideoFormatByID:

   int getVideoFormatByID(VSVideoFormat_ \*format, uint32_t id, VSCore_ \*core)

      Fills out the VSVideoFormat_ struct passed to *format* based
      
      *format*
         The struct to fill out.

      *id*
         The format identifier: one of VSPresetVideoFormat_ or a value gotten from queryVideoFormatID_.

      Returns 0 on failure and non-zero on success.

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

   .. _mapGetError:

   const char \*mapGetError(const VSMap_ \*map)

      Returns a pointer to the error message contained in the map,
      or NULL if there is no error set. The pointer is valid until
      the next modifying operation on the map.

----------

   .. _mapSetError:

   void mapSetError(VSMap_ \*map, const char \*errorMessage)

      Adds an error message to a map. The map is cleared first. The error
      message is copied. In this state the map may only be freed, cleared
      or queried for the error message.

      For errors encountered in a filter's "getframe" function, use
      setFilterError_.

----------

   .. _mapNumKeys:

   int mapNumKeys(const VSMap_ \*map)

      Returns the number of keys contained in a property map.

----------

   .. _mapGetKey:

   const char \*mapGetKey(const VSMap_ \*map, int index)

      Returns the nth key from a property map.

      Passing an invalid *index* will cause a fatal error.

      The pointer is valid as long as the key exists in the map.

----------

   .. _mapDeleteKey:

   int mapDeleteKey(VSMap_ \*map, const char \*key)

      Removes the property with the given key. All values associated with the
      key are lost.

      Returns 0 if the key isn't in the map. Otherwise it returns 1.

----------

   .. _mapNumElements:

   int mapNumElements(const VSMap_ \*map, const char \*key)

      Returns the number of elements associated with a key in a property map.
      Returns -1 if there is no such key in the map.

----------

   .. _mapGetType:

   int mapGetType(const VSMap_ \*map, const char \*key)

      Returns a value from VSPropertyType_ representing type
      of elements in the given key. If there is no such key in the
      map, the returned value is ptUnset. Note that also empty
      arrays created with mapSetEmpty_ are typed.
      
----------

   .. _mapSetEmpty:

   int mapSetEmpty(const VSMap_ \*map, const char \*key, int type)

      Creates an empty array of *type* in *key*. Returns non-zero
      value on failure due to *key* already existing or having an
      invalid name.

----------

   .. _mapGetInt:

   int64_t mapGetInt(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves an integer from a specified *key* in a *map*.

      Returns the number on success, or 0 in case of error.

      If the map has an error set (i.e. if mapGetError_\ () returns non-NULL),
      VapourSynth will die with a fatal error.
      
      *index*
         Zero-based index of the element.

         Use mapNumElements_\ () to know the total number of elements
         associated with a key.

      *error*
         One of VSMapPropertyError_, peSuccess on success.

         You may pass NULL here, but then any problems encountered while
         retrieving the property will cause VapourSynth to die with a fatal
         error.

----------

   .. _mapGetIntSaturated:

   int mapGetIntSaturated(const VSMap_ \*map, const char \*key, int index, int \*error)

      Works just like mapGetInt_\ () except that the value returned is also
      converted to an integer using saturation.

----------

   .. _mapGetIntArray:

   const int64_t \*mapGetIntArray(const VSMap_ \*map, const char \*key, int \*error)

      Retrieves an array of integers from a map. Use this function if there
      are a lot of numbers associated with a key, because it is faster than
      calling mapGetInt_\ () in a loop.

      Returns a pointer to the first element of the array on success, or NULL
      in case of error. Use mapNumElements_\ () to know the total number of
      elements associated with a key.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapSetInt:

   int mapSetInt(VSMap_ \*map, const char \*key, int64_t i, int append)

      Sets an integer to the specified key in a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and underscore
         may be used.

      *i*
         Value to store.

      *append*
         One of VSMapAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type to an existing key.

----------

   .. _mapSetIntArray:

   int mapSetIntArray(VSMap_ \*map, const char \*key, const int64_t \*i, int size)

      Adds an array of integers to a map. Use this function if there are a
      lot of numbers to add, because it is faster than calling mapSetInt_\ ()
      in a loop.

      If *map* already contains a property with this *key*, that property will
      be overwritten and all old values will be lost.

      *key*
         Name of the property. Alphanumeric characters and underscore
         may be used.

      *i*
         Pointer to the first element of the array to store.

      *size*
         Number of integers to read from the array. It can be 0, in which case
         no integers are read from the array, and the property will be created
         empty.

      Returns 0 on success, or 1 if *size* is negative.

----------

   .. _mapGetFloat:

   double mapGetFloat(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a floating point number from a map.

      Returns the number on success, or 0 in case of error.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapGetFloatSaturated:

   float mapGetFloatSaturated(const VSMap_ \*map, const char \*key, int index, int \*error)

      Works just like mapGetFloat_\ () except that the value returned is also
      converted to a float.

----------

   .. _mapGetFloatArray:

   const double \*mapGetFloatArray(const VSMap_ \*map, const char \*key, int \*error)
   
      Retrieves an array of floating point numbers from a map. Use this function if there
      are a lot of numbers associated with a key, because it is faster than
      calling mapGetFloat_\ () in a loop.

      Returns a pointer to the first element of the array on success, or NULL
      in case of error. Use mapNumElements_\ () to know the total number of
      elements associated with a key.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapSetFloat:

   int mapSetFloat(VSMap_ \*map, const char \*key, double d, int append)
   
      Sets a float to the specified key in a map.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapSetFloatArray:

   int mapSetFloatArray(VSMap_ \*map, const char \*key, const double \*d, int size)

      Adds an array of floating point numbers to a map. Use this function if
      there are a lot of numbers to add, because it is faster than calling
      mapSetFloat_\ () in a loop.

      If *map* already contains a property with this *key*, that property will
      be overwritten and all old values will be lost.

      *key*
         Name of the property. Alphanumeric characters and underscore
         may be used.

      *d*
         Pointer to the first element of the array to store.

      *size*
         Number of floating point numbers to read from the array. It can be 0,
         in which case no numbers are read from the array, and the property
         will be created empty.

      Returns 0 on success, or 1 if *size* is negative.

----------

   .. _mapGetData:

   const char \*mapGetData(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves arbitrary binary data from a map. Checking mapGetDataTypeHint_\ ()
      may provide a hint about whether or not the data is human readable.

      Returns a pointer to the data on success, or NULL in case of error.

      The array returned is guaranteed to be NULL-terminated. The NULL
      byte is not considered to be part of the array (mapGetDataSize_
      doesn't count it).

      The pointer is valid until the map is destroyed, or until the
      corresponding key is removed from the map or altered.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapGetDataSize:

   int mapGetDataSize(const VSMap_ \*map, const char \*key, int index, int \*error)

      Returns the size in bytes of a property of type ptData (see
      VSPropertyType_), or 0 in case of error. The terminating NULL byte
      added by mapSetData_\ () is not counted.
      
      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapGetDataTypeHint:

   int mapGetDataTypeHint(const VSMap_ \*map, const char \*key, int index, int \*error)

      Returns the size in bytes of a property of type ptData (see
      VSPropertyType_), or 0 in case of error. The terminating NULL byte
      added by mapSetData_\ () is not counted.
      
      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapSetData:

   int mapSetData(VSMap \*map, const char \*key, const char \*data, int size, int type, int append)

      Sets binary data to the specified key in a map.

      Multiple values can be associated with one key, but they must all be the
      same type.

      *key*
         Name of the property. Alphanumeric characters and the underscore
         may be used.

      *data*
         Value to store.

         This function copies the data, so the pointer should be freed when
         no longer needed. A terminating NULL is always added to the copied data
         but not included in the total size to make string handling easier.

      *size*
         The number of bytes to copy. If this is negative, everything up to
         the first NULL byte will be copied.
         
      *type*
         One of VSDataTypeHint_ to hint whether or not it is human readable data.

      *append*
         One of VSMapAppendMode_.

      Returns 0 on success, or 1 if trying to append to a property with the
      wrong type.

----------

   .. _mapGetNode:

   VSNode_ \*mapGetNode(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a node from a map.

      Returns a pointer to the node on success, or NULL in case of error.

      This function increases the node's reference count, so freeNode_\ () must
      be used when the node is no longer needed.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapSetNode:

   int mapSetNode(VSMap_ \*map, const char \*key, VSNode_ \*node, int append)

      Sets a node to the specified key in a map.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapConsumeNode:

   int mapConsumeNode(VSMap_ \*map, const char \*key, VSNode_ \*node, int append)

      Sets a node to the specified key in a map and decreases the reference count.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapGetFrame:

   const VSFrame_ \*propGetFrame(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a frame from a map.

      Returns a pointer to the frame on success, or NULL in case of error.

      This function increases the frame's reference count, so freeFrame_\ () must
      be used when the frame is no longer needed.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.
      
----------

   .. _mapSetFrame:

   int mapSetFrame(VSMap_ \*map, const char \*key, const VSFrame_ \*f, int append)

      Sets a frame to the specified key in a map.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapConsumeFrame:

   int mapConsumeFrame(VSMap_ \*map, const char \*key, const VSFrame_ \*f, int append)

      Sets a frame to the specified key in a map and decreases the reference count.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapGetFunction:

   VSFunctionRef \*mapGetFunc(const VSMap_ \*map, const char \*key, int index, int \*error)

      Retrieves a function from a map.

      Returns a pointer to the function on success, or NULL in case of error.

      This function increases the function's reference count, so freeFunction_\ () must
      be used when the function is no longer needed.

      See mapGetInt_\ () for a complete description of the arguments and general behavior.
      
----------

   .. _mapSetFunction:

   int mapSetFunction(VSMap_ \*map, const char \*key, VSFunction \*func, int append)

      Sets a function object to the specified key in a map.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _mapConsumeFunction:

   int mapConsumeFunction(VSMap_ \*map, const char \*key, VSFunction \*func, int append)

      Sets a function object to the specified key in a map and decreases the reference count.

      See mapSetInt_\ () for a complete description of the arguments and general behavior.

----------

   .. _getPluginByID:

   VSPlugin_ \*getPluginByID(const char \*identifier, VSCore_ \*core)

      Returns a pointer to the plugin with the given identifier, or NULL
      if not found.

      *identifier*
         Reverse URL that uniquely identifies the plugin.

----------

   .. _getPluginByNamespace:

   VSPlugin_ \*getPluginByNamespace(const char \*ns, VSCore_ \*core)

      Returns a pointer to the plugin with the given namespace, or NULL
      if not found.

      getPluginByID_ is generally a better option.

      *ns*
         Namespace.

----------

   .. _getNextPlugin:

   VSPlugin_ \*getNextPlugin(VSPlugin_ \*plugin, VSCore_ \*core)

      Used to enumerate over all currently loaded plugins. The order
      is fixed but provides no other guarantees.

      *plugin*
         Current plugin. Pass NULL to get the first plugin.

      Returns a pointer to the next plugin in order or NULL if the final
      plugin has been reached.
      
----------

   .. _getPluginName:

   const char \*getPluginName(VSPlugin_ \*plugin)

      Returns the name of the plugin that was passed to configPlugin_.

----------

   .. _getPluginID:

   const char \*getPluginID(VSPlugin_ \*plugin)

      Returns the identifier of the plugin that was passed to configPlugin_.

----------

   .. _getPluginNamespace:

   const char \*getPluginNamespace(VSPlugin_ \*plugin)

      Returns the namespace the plugin currently is loaded in.

----------

   .. _getNextPluginFunction:

   VSPluginFunction_ \*getNextPluginFunction(VSPluginFunction_ \*func, VSPlugin \*plugin)

      Used to enumerate over all functions in a plugin. The order
      is fixed but provides no other guarantees.

      *func*
         Current function. Pass NULL to get the first function.

      *plugin*
         The plugin to enumerate functions in.

      Returns a pointer to the next function in order or NULL if the final
      function has been reached.

----------

   .. _getPluginFunctionByName:

   VSPluginFunction_ \*getPluginFunctionByName(const char \*name, VSPlugin_ \*plugin)

      Get a function belonging to a plugin by its name.

----------

   .. _getPluginFunctionName:

   const char \*getPluginFunctionName(VSPluginFunction_ \*func)

      Returns the name of the function that was passed to registerFunction_.

----------

   .. _getPluginFunctionArguments:

   const char \*getPluginFunctionArguments(VSPluginFunction_ \*func)

      Returns the argument string of the function that was passed to registerFunction_.       

----------

   .. _getPluginFunctionReturnType:

   const char \*getPluginFunctionReturnType(VSPluginFunction_ \*func)

      Returns the return type string of the function that was passed to registerFunction_.       

----------

   .. _getPluginPath:

   const char \*getPluginPath(const VSPlugin_ \*plugin)

      Returns the absolute path to the plugin, including the plugin's file
      name. This is the real location of the plugin, i.e. there are no
      symbolic links in the path.

      Path elements are always delimited with forward slashes.

      VapourSynth retains ownership of the returned pointer.

----------

   .. _getPluginVersion:

   int getPluginVersion(const VSPlugin_ \*plugin)

      Returns the version of the plugin. This is the same as the version number passed to configPlugin_.

----------

   .. _invoke:

   VSMap_ \*invoke(VSPlugin_ \*plugin, const char \*name, const VSMap_ \*args)

      Invokes a filter.

      invoke() checks that
      the *args* passed to the filter are consistent with the argument list
      registered by the plugin that contains the filter, calls the filter's
      "create" function, and checks that the filter returns the declared types.
      If everything goes smoothly, the filter will be ready to generate
      frames after invoke() returns.

      *plugin*
         A pointer to the plugin where the filter is located. Must not be NULL.

         See getPluginByID_\ ().

      *name*
         Name of the filter to invoke.

      *args*
         Arguments for the filter.

      Returns a map containing the filter's return value(s). The caller takes
      ownership of the map. Use mapGetError_\ () to check if the filter was invoked
      successfully.

      Most filters will either set an error, or one or more clips
      with the key "clip". The exception to this are functions, for example
      LoadPlugin, which doesn't return any clips for obvious reasons.

----------

   .. _createFunction:

   VSFunction_ \*createFunction(VSPublicFunction func, void \*userData, VSFreeFunctionData free, VSCore_ \*core)

      *func*
         typedef void (VS_CC \*VSPublicFunction)(const VSMap_ \*in, VSMap_ \*out, void \*userData, VSCore_ \*core, const VSAPI_ \*vsapi)

         User-defined function that may be called in any context.

      *userData*
         Pointer passed to *func*.

      *free*
         typedef void (VS_CC \*VSFreeFunctionData)(void \*userData)

         Callback tasked with freeing *userData*. Can be NULL.

----------

   .. _freeFunction:

   void freeFunction(VSFunction_ \*f)

      Decrements the reference count of a function and deletes it when it reaches 0.

      It is safe to pass NULL.
      
----------

   .. _addFunctionRef:

   VSFunction_ \*addFunctionRef(VSFunction_ \*f)

      Increments the reference count of a function. Returns *f* as a convenience.

----------

   .. _callFunction:

   void callFunction(VSFunction_ \*func, const VSMap_ \*in, VSMap_ \*out)

      Calls a function. If the call fails *out* will have an error set.
      
      *func*
         Function to be called.

      *in*
         Arguments passed to *func*.
         
      *out*
         Returned values from *func*.

----------

   .. _getFrame:

   const VSFrame_ \*getFrame(int n, VSNode_ \*node, char \*errorMsg, int bufSize)

      Fetches a frame synchronously. The frame is available when the function
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

   void getFrameAsync(int n, VSNode_ \*node, VSFrameDoneCallback callback, void \*userData)

      Requests the generation of a frame. When the frame is ready,
      a user-provided function is called. Note that the completion
      *callback* will only be called from a single thread at a time.
      
      This function is meant for applications using VapourSynth as a library.
      
      Thread-safe.

      *n*
         Frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *callback*
         typedef void (VS_CC \*VSFrameDoneCallback)(void \*userData, const VSFrame_ \*f, int n, VSNode_ \*node, const char \*errorMsg)

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

   const VSFrame_ \*getFrameFilter(int n, VSNode_ \*node, VSFrameContext_ \*frameCtx)

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

   void requestFrameFilter(int n, VSNode_ \*node, VSFrameContext_ \*frameCtx)

      Requests a frame from a node and returns immediately.

      Only use inside a filter's "getframe" function.

      A filter usually calls this function when its activation reason is
      arInitial. The requested frame can then be retrieved using
      getFrameFilter_\ (), when the filter's activation reason is
      arAllFramesReady. See VSActivationReason_.

      It is best to request frames in ascending order, i.e. n, n+1, n+2, etc.

      *n*
         The frame number. Negative values will cause an error.

      *node*
         The node from which the frame is requested.

      *frameCtx*
         The context passed to the filter's "getframe" function.
         
----------

   .. _releaseFrameEarly:

   void releaseFrameEarly(VSNode_ \*node, int n, VSFrameContext_ \*frameCtx)

      By default all requested frames are referenced until a filter's frame
      request is done. In extreme cases where a filter needs to reduce 20+
      frames into a single output frame it may be beneficial to request
      these in batches and incrementally process the data instead.
      
      Should rarely be needed.

      Only use inside a filter's "getframe" function.
      
      *node*
         The node from which the frame was requested.
         
      *n*
         The frame number. Invalid frame numbers (not cached or negative) will simply be ignored.

      *frameCtx*
         The context passed to the filter's "getframe" function.

----------

   .. _registerFunction:

   int registerFunction(const char \*name, const char \*args, const char \*returnType, VSPublicFunction argsFunc, void \*functionData, VSPlugin_ \*plugin)

      Function that registers a filter exported by the plugin. A plugin can
      export any number of filters. This function may only be called during the plugin
      loading phase unless the pcModifiable flag was set by configPlugin_.

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

               "anode": const VSNode_\ * (audio type)

               "vnode": const VSNode_\ * (video type)

               "aframe": const VSFrame_\ * (audio type)
               
               "vframe": const VSFrame_\ * (video type)

               "func": const VSFunctionRef\ *

               It is possible to declare an array by appending "[]" to the type.

            "opt"
               If the parameter is optional.

            "empty"
               For arrays that are allowed to be empty.
               
            "any"
               Can only be placed last without a semicolon after. Indicates that all remaining arguments that don't match
               should also be passed through.

         The following example declares the arguments "blah", "moo", and "asdf"::

            blah:vnode;moo:int[]:opt;asdf:float:opt;
            
         The following example declares the arguments "blah" and accepts all other arguments no matter the type::

            blah:vnode;any

      *returnType*
         Specifies works similarly to *args* but instead specifies which keys and what type will be returned. Typically this will be::
         
            clip:vnode; 
            
         for video filters. It is important to not simply specify "any" for all filters since this information is used for better
         auto-completion in many editors.

      *argsFunc*
         typedef void (VS_CC \*VSPublicFunction)(const VSMap_ \*in, VSMap_ \*out, void \*userData, VSCore_ \*core, const VSAPI_ \*vsapi)

         User-defined function called by the core to create an instance of the
         filter. This function is often named ``fooCreate``.

         In this function, the filter's input parameters should be retrieved
         and validated, the filter's private instance data should be
         initialised, and createAudioFilter_\ () or createVideoFilter_\ () should be called. This is where
         the filter should perform any other initialisation it requires.

         If for some reason you cannot create the filter, you have to free any
         created node references using freeNode_\ (), call mapSetError_\ () on
         *out*, and return.

         *in*
            Input parameter list.

            Use mapGetInt_\ () and friends to retrieve a parameter value.

            The map is guaranteed to exist only until the filter's "init"
            function returns. In other words, pointers returned by
            mapGetData_\ () will not be usable in the filter's "getframe" and
            "free" functions.

         *out*
            Output parameter list. createAudioFilter_\ () or createVideoFilter_\ () will add the output
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
         VapourSynthPluginInit2().

----------

   .. _cacheFrame:

   void cacheFrame(const VSFrame_ \*frame, int n, VSFrameContext_ \*frameCtx)

      Pushes a not requested frame into the cache. This is useful for (source) filters that greatly
      benefit from completely linear access and producing all output in linear order.
 
      This function may only be used in filters that were created with setLinearFilter_.

      Only use inside a filter's "getframe" function.
      
----------

   .. _setFilterError:

   void setFilterError(const char \*errorMessage, VSFrameContext_ \*frameCtx)

      Adds an error message to a frame context, replacing the existing message,
      if any.

      This is the way to report errors in a filter's "getframe" function.
      Such errors are not necessarily fatal, i.e. the caller can try to
      request the same frame again.

Functions
#########

.. _getVapourSynthAPI:

const VSAPI_\* getVapourSynthAPI(int version)

   Returns a pointer to the global VSAPI instance.

   Returns NULL if the requested API version is not supported or if the system
   does not meet the minimum requirements to run VapourSynth. It is recommended
   to pass VAPOURSYNTH_API_VERSION_.
   

Writing plugins
###############


A simple VapourSynth plugin which exports one filter will contain five
functions: an entry point (called ``VapourSynthPluginInit2``), a function tasked
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

typedef void (VS_CC \*VSInitPlugin)(VSPlugin_ \*plugin, const VSPLUGINAPI_ \*vspapi)

   A plugin's entry point. It must be called ``VapourSynthPluginInit2``.
   This function is called after the core loads the shared library. Its purpose
   is to configure the plugin and to register the filters the plugin wants to
   export.

   *plugin*
      A pointer to the plugin object to be initialized.

   *vspapi*
      A pointer to a VSPLUGINAPI_ struct with a subset of the VapourSynth API used for initializing plugins.
      The proper way to do things is to call configPlugin_ and then registerFunction_ for each function to export.

----------

.. _VSFilterGetFrame:

typedef const VSFrame_ \*(VS_CC \*VSFilterGetFrame)(int n, int activationReason, void \*instanceData, void \**frameData, VSFrameContext_ \*frameCtx, VSCore_ \*core, const VSAPI_ \*vsapi)

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
      is called again with arAllFramesReady.
      The function should only return a frame when called with
      *activationReason* arAllFramesReady.

      If a the function is called with arError all processing has to be aborted
      and any.

   *instanceData*
      The filter's private instance data.

   *frameData*
      Optional private data associated with output frame number *n*.
      It must be deallocated before the last call for the given frame
      (arAllFramesReady or error).

      It points to a void \*[4] array of memory that may be used freely.
      See filters like Splice and Trim for examples.

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
