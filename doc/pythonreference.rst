.. _pythonreference:

Python Reference
================

VapourSynth is separated into a core library and a Python module. This section
explains how the core library is exposed through Python and some of the
special things unique to Python scripting, such as slicing and output.

.. note::

   Any script executed through the vsscript api (that means vspipe, avfs, vsvfw or
   other API users) will have __name__ set to "__vapoursynth__" unlike normal Python
   scripts where it usually is "__main__".

VapourSynth Structure
#####################

Most operations in the VapourSynth library are performed through the singleton
core object. This core may load plugins, which all end up in their own unit,
or namespace, so to say, to avoid naming conflicts in the contained functions.
For this reason you call a plugin function with *core.unit.Function()*.

All arguments to functions have names that are lowercase and all function names
are CamelCase. Unit names are also lowercase and usually short. This is good to
remember as a general rule.

Grammar
#######

Slicing and Other Syntactic Sugar
*********************************

The VideoNode and AudioNode class (always called "clip" in practice) supports the full
range of indexing and slicing operations in Python. If you do perform a slicing
operation on a clip, you will get a new clip back with the desired frames.
Here are a few examples.

+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| Operation                       | Description                                                   | Equivalent                                             |
+=================================+===============================================================+========================================================+
| clip = clip[5]                  | Make a single frame clip containing frame number 5            |                                                        |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip[5:11]               | Make a clip containing frames 5 to 10 [#f1]_                  | clip = core.std.Trim(clip, first=5, last=10)           |
|                                 |                                                               |                                                        |
|                                 |                                                               | clip = core.std.AudioTrim(clip, first=5, last=10)      |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip[::2]                | Select even numbered frames                                   | clip = core.std.SelectEvery(clip, cycle=2, offsets=0)  |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip[1::2]               | Select odd numbered frames                                    | clip = core.std.SelectEvery(clip, cycle=2, offsets=1)  |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip[::-1]               | Reverses a clip                                               | clip = core.std.Reverse(clip)                          |
|                                 |                                                               |                                                        |
|                                 |                                                               | clip = core.std.AudioReverse(clip)                     |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip1 + clip2            | The addition operator can be used to splice clips together    | clip = core.std.Splice([clip1, clip2], mismatch=False) |
|                                 |                                                               |                                                        |
|                                 |                                                               | clip = core.std.AudioSplice([clip1, clip2])            |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+
| clip = clip * 10                | The multiplication operator can be used to loop a clip [#f2]_ | clip = core.std.Loop(clip, times=10)                   |
|                                 |                                                               |                                                        |
|                                 |                                                               | clip = core.std.AudioLoop(clip, times=10)              |
+---------------------------------+---------------------------------------------------------------+--------------------------------------------------------+

.. [#f1] Note that frame numbers, like python arrays, start counting at 0 and the end value of slicing is not inclusive

.. [#f2] Note that multiplication by 0 is a special case that will repeat the clip up to the maximum frame count


Filters can be chained with a dot::

   clip = clip.std.Trim(first=100, last=2000).std.FlipVertical()

Which is quivalent to::

   clip = core.std.FlipVertical(core.std.Trim(clip, first=100, last=2000))

Python Keywords as Filter Arguments
***********************************

If a filter's argument happens to be a Python keyword, you may append
an underscore to the argument's name when invoking the filter. The Python
module will strip one trailing underscore (if present) from all filter arguments before
passing them to the filters.

::

   clip = core.plugin.Filter(clip, lambda_=1)

Another way to deal with such arguments is to place them in a dictionary::

   kwargs = { "lambda": 1 }
   clip = core.plugin.Filter(clip, **kwargs)

VapourSynth will also support the PEP8 convention of using a single trailing
underscore to prevent collisions with python keywords.

Windows File Paths
******************

If you have a string containing backslashes, you must either prefix the
string with "r", or duplicate every single backslash. The reason is
that the backslash is an escape character in Python.

Use `os.path.normcase(path) <https://docs.python.org/3/library/os.path.html#os.path.normcase>`_
to fix Incorrect path string.

Correct example::

   "B:/VapourSynth/VapourSynth.dll"
   "B:\\VapourSynth\\VapourSynth.dll"
   r"B:\VapourSynth\VapourSynth.dll"

Output
******

The normal way of specifying the clip(s) to output is to call
*clip.set_output()*. All standard VapourSynth components only use output
index 0, except for vspipe where it's configurable but defaults to 0.
There are also other variables that can be set to control how a format is
output. For example, setting *alt_output=1* changes the packing of the
YUV422P10 format to one that is common in professional software (like Adobe
products). Note that currently *alt_output* modes only has an effect with
YUV420P8 (I420, IYUV), YUV422P8 (YUY2, UYVY) and YUV422P10 (v210).

An example on how to get v210 output::

   some_clip = core.resize.Bicubic(clip, format=vs.YUV422P10)
   some_clip.set_output(alt_output=1)

An example on how to get UYVY output::

   some_clip = core.resize.Bicubic(clip, format=vs.YUV422P8)
   some_clip.set_output(alt_output=2)

Raw Access to Frame Data
************************

The VideoFrame and AudioFrame classes contains one picture/audio chunk and all the metadata
associated with it. It is possible to access the raw data using either
*get_read_ptr(plane)* or *get_write_ptr(plane)* and *get_stride(plane)* with ctypes.

A more Python friendly wrapping is also available where each plane/channel can be accessed
as a Python array using *frame[plane/channel]*.

To get a frame simply call *get_frame(n)* on a clip. Should you desire to get
all frames in a clip, use this code::

   for frame in clip.frames():
       # Do stuff with your frame
       pass

Classes and Functions
#####################

.. py:attribute:: core

   Gets the singleton Core object. If it is the first time the function is called,
   the Core will be instantiated with the default options. This is the preferred
   way to reference the core.

.. py:function:: get_outputs()

   Return a read-only mapping of all outputs registered on the current node.

   The mapping will automatically update when a new output is registered.

.. py:function:: get_output([index = 0])

   Get a previously set output node. Throws an error if the index hasn't been
   set. Will return a VideoOutputTuple containing *alpha* and the *alt_output* setting for video output and an AudioNode for audio.

.. py:function:: clear_output([index = 0])

   Clears a clip previously set for output.

.. py:function:: clear_outputs()

   Clears all clips set for output in the current environment.

.. py:function:: construct_signature(signature[, injected=None])

   Creates a *inspect.Signature* object for the given registration signature.

   If *injected* is not None, the default of the first argument of the signature will be replaced with the value supplied with injected.


.. py:class:: Core

   The *Core* class uses a singleton pattern. Use the *core* attribute to obtain an
   instance. All loaded plugins are exposed as attributes of the core object.
   These attributes in turn hold the functions contained in the plugin.
   Use *plugins()* to obtain a full list of all currently loaded plugins
   you may call this way.

   .. py:attribute:: num_threads

      The number of concurrent threads used by the core. Can be set to change the number. Setting to a value less than one makes it default to the number of hardware threads.

   .. py:attribute:: max_cache_size

      Set the upper framebuffer cache size after which memory is aggressively
      freed. The value is in megabytes.

   .. py:method:: plugins()

      Containing all loaded plugins.

   .. py:method:: get_plugins()

      Deprecated, use *plugins()* instead.

   .. py:method:: list_functions()

      Deprecated, use *plugins()* instead.

   .. py:method:: get_video_format(id)

      Retrieve a Format object corresponding to the specified id. Returns None if the *id* is invalid.

   .. py:method:: get_format(id)

      Deprecated, use *get_video_format()* instead.

   .. py:method:: query_video_format(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h)

      Retrieve a Format object corresponding to the format information, Invalid formats throw an exception.

   .. py:method:: register_format(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h)

      Deprecated, use *query_video_format()* instead.

   .. py:method:: add_log_handler(handler_func)

      Installs a custom handler for the various error messages VapourSynth emits.
      The message handler is currently global, i.e. per process, not per VSCore instance.
      Returns a LogHandle object.
      *handler_func* is a callback function of the form *func(MessageType, message)*.

   .. py:method:: remove_log_handler(handle)

      Removes a custom handler.

   .. py:method:: log_message(message_type, message)

      Send a message through VapourSynth’s logging framework.

   .. py:method:: version()

      Returns version information as a string.

   .. py:method:: version_number()

      Returns the core version as a number.

   .. py:method:: rule6()

      Illegal behavior detection.

.. py:class:: VideoNode

   Represents a video clip. The class itself supports indexing and slicing to
   perform trim, reverse and selectevery operations. Several operators are also
   defined for the VideoNode class: addition appends clips and multiplication
   repeats them. Note that slicing and indexing always return a new VideoNode
   object and not a VideoFrame.

   .. py:attribute:: format

      A Format object describing the frame data. If the format can change
      between frames, this value is None.

   .. py:attribute:: width

      The width of the video. This value will be 0 if the width and height can
      change between frames.

   .. py:attribute:: height

      The height of the video. This value will be 0 if the width and height can
      change between frames.

   .. py:attribute:: num_frames

      The number of frames in the clip.

   .. py:attribute:: fps

      The framerate represented as a *Fraction*. It is 0/1 when the clip has a variable
      framerate.

      .. py:attribute:: numerator

         The numerator of the framerate. If the clip has variable framerate, the value will be 0.

      .. py:attribute:: denominator

         The denominator of the framerate. If the clip has variable framerate, the value will be 0.

   .. py:attribute:: fps_num

      Deprecated, use *fps.numerator* instead

   .. py:attribute:: fps_den

      Deprecated, use *fps.denominator* instead

   .. py:attribute:: flags

      Special flags set for this clip. This attribute should normally be
      ignored.

   .. py:method:: get_frame(n)

      Returns a VideoFrame from position *n*.

   .. py:method:: get_frame_async(n)

      Returns a concurrent.futures.Future-object which result will be a VideoFrame instance or sets the
      exception thrown when rendering the frame.

      *The future will always be in the running or completed state*

   .. py:method:: get_frame_async(n, cb: callable)
      :noindex:

      Renders a frame in another thread. When the frame is rendered, it will either call `cb(Frame, None)` on success
      or `cb(None, Exception)` if something fails.

      Added: R58

   .. py:method:: get_frame_async_raw(n, cb: callable)

      First form of this method. It will call the callback from another thread as soon as the frame is rendered.

      The `result`-value passed to the callback will either be a VideoFrame-instance on success or a Error-instance
      on failure.

      *This method is intended for glue code. For normal use, use get_frame_async instead.*

      Deprecated, use *get_frame_async(n, cb)* instead.

      :param n: The frame number
      :param cb: A callback in the form `cb(node, n, result)`

   .. py:method:: get_frame_async_raw(n, cb: Future[, wrapper: callable = None])
      :noindex:

      Second form of this method. It will take a Future-like object (including asyncio.Future or similar)
      and set its result or exception according to the result of the function.

      The optional `wrapper`-parameter is intended for calls like asyncio.EventLoop.call_soon_threadsafe in which
      all calls to its future-object must be wrapped.

      *This method is intended for glue code. For normal use, use get_frame_async instead.*

      Deprecated, use *get_frame_async(n, cb)* instead.

      :param n: The frame number
      :param cb: The future-object whose result will be set.
      :param wrapper: A wrapper-callback which is responsible for moving the result across thread boundaries. If not
                      given, the result of the future will be set in a random thread.

   .. py:method:: set_output(index = 0, alpha = None, alt_output = 0)

      Set the clip to be accessible for output. This is the standard way to
      specify which clip(s) to output. All VapourSynth tools (vsvfw, vsfs,
      vspipe) use the clip in *index* 0. It's possible to specify an additional
      containing the *alpha* to output at the same time. Currently only vspipe
      takes *alpha* into consideration when outputting.
      The *alt_output* argument is for optional alternate output modes. Currently
      it controls the FOURCCs used for VFW-style output with certain formats.

   .. py:method:: output(fileobj[, y4m = False, prefetch = 0, progress_update = None, backlog=-1])

      Write the whole clip to the specified file handle. It is possible to pipe to stdout by specifying *sys.stdout* as the file.
      YUV4MPEG2 headers will be added when *y4m* is true.
      The current progress can be reported by passing a callback function of the form *func(current_frame, total_frames)* to *progress_update*.
      The *prefetch* argument is only for debugging purposes and should never need to be changed.
      The *backlog* argument is only for debugging purposes and should never need to be changed.

   .. py:method:: frames([prefetch=None, backlog=None, close=False])

      Returns a generator iterator of all VideoFrames in the clip. It will render multiple frames concurrently.

      The *prefetch* argument defines how many frames are rendered concurrently. Is only there for debugging purposes and should never need to be changed.
      The *backlog* argument defines how many unconsumed frames (including those that did not finish rendering yet) vapoursynth buffers at most before it stops rendering additional frames. This argument is there to limit the memory this function uses storing frames.
      The *close* argument determines if the frame should be closed after each iteration step. It defaults to false to remain backward compatible.

   .. py:method:: is_inspectable(version=None)
   
      Returns a truthy value if you can use the node inspection API with a given version.
      The python inspection-api is versioned, as the underlying API is unstable at the time of writing.
      The version number will be incremented every time the python API changes.
      There will be no attempt to maintain backwards compatibility as long as the API is marked as unstable.

      This method may never return a truthy value.

      This is the only stable function in the current inspection api-implementation.

      .. note::

         Be aware that introspection features must be enabled manually by the backing environment. Standalone Python-Scripts,
         not running inside vspipe or other editors, have introspection enabled automatically.

      .. warning::

         The graph-inspection-api is unstable. Omitting the version-argument will therefore always return
         None.

      The current version of the unstable python graph-inspection API is 0.

      Added: R58

      :param version: If None, it will use the version number of the last stable API.

.. py:class:: VideoOutputTuple

      This class is returned by get_output if the output is video.

      .. py:attribute:: clip

         A VideoNode-instance containing the color planes.

      .. py:attribute:: alpha

         A VideoNode-instance containing the alpha planes.

      .. py:attribute:: alt_output

         An integer with the alternate output mode to be used. May be ignored if no meaningful mapping exists.

.. py:class:: VideoFrame

      This class represents a video frame and all metadata attached to it.

   .. py:attribute:: format

      A Format object describing the frame data.

   .. py:attribute:: width

      The width of the frame.

   .. py:attribute:: height

      The height of the frame.

   .. py:attribute:: readonly

      If *readonly* is True, the frame data and properties cannot be modified.

   .. py:attribute:: props

      This attribute holds all the frame's properties as a dict. They are also mapped as sub-attributes for
      compatibility with older scripts. For more information, see:
      `API Reference <apireference.html#reserved-frame-properties>`_
      Note: This includes the data for matrix, transfer and primaries. (_Matrix,
      _Transfer, _Primaries) See `Resize <functions/resize.html>`_ for more information.

   .. py:method:: copy()

      Returns a writable copy of the frame.

   .. py:method:: close()

      Forcefully releases the frame. Once freed, the you cannot call any function on the frame, nor use the associated
      FrameProps.

      To make sure you don't forget to close the frame, the frame is now a context-manager that automatically calls
      this method for you:

      .. code::

           with core.std.BlankClip().get_frame(0) as f:
               print(f.props)

   .. py:attribute:: closed

      Tells you if the frame has been closed. It will be False if the close()-method has not been called yet.

   .. py:method:: get_read_ptr(plane)

      Returns a pointer to the raw frame data. The data may not be modified.
      Note that this is a thin wrapper for the underlying
      C-api and as such calls to *get_write_ptr*, including the ones made internally by other functions in the Python bindings,
      may invalidate any pointers previously gotten to the frame with
      *get_read_ptr* when called.

   .. py:method:: get_write_ptr(plane)

      Returns a pointer to the raw frame data. It may be modified using ctypes
      or some other similar python package.  Note that this is a thin wrapper for the underlying
      C-api and as such calls to *get_write_ptr*, including the ones made internally by other functions in the Python bindings,
      may invalidate any pointers previously gotten to the frame with
      *get_read_ptr* when called.

   .. py:method:: get_stride(plane)

      Returns the stride between lines in a *plane*.

.. py:class:: VideoFormat

   This class represents all information needed to describe a frame format. It
   holds the general color type, subsampling, number of planes and so on.
   The names map directly to the C API so consult it for more detailed
   information.

   .. py:attribute:: id

      A unique *id* identifying the format.

   .. py:attribute:: name

      A human readable name of the format.

   .. py:attribute:: color_family

      Which group of colorspaces the format describes.

   .. py:attribute:: sample_type

      If the format is integer or floating point based.

   .. py:attribute:: bits_per_sample

      How many bits are used to store one sample in one plane.

   .. py:attribute:: bytes_per_sample

      The actual storage is padded up to 2^n bytes for efficiency.

   .. py:attribute:: subsampling_w

      The subsampling for the second and third plane in the horizontal
      direction.

   .. py:attribute:: subsampling_h

      The subsampling for the second and third plane in the vertical direction.

   .. py:attribute:: num_planes

      The number of planes the format has.

   .. py:method:: replace(core=None, **kwargs)

      Returns a new format with the given modifications.

      The only supported attributes that can be replaced are `color_family`,
      `sample_type`, `bits_per_sample`, `subsampling_w`, `subsampling_h`.

      The optional `core`-parameter defines on which core the new format
      should be registered. This is usually not needed and defaults
      to the core of the current environment.

.. py:class:: AudioNode

   Represents an audio clip. The class itself supports indexing and slicing to
   perform trim, reverse and selectevery operations. Several operators are also
   defined for the AudioNode class: addition appends clips and multiplication
   repeats them. Note that slicing and indexing always return a new AudioNode
   object and not a AudioFrame.

   .. py:attribute:: sample_type

      If the format is integer or floating point based.

   .. py:attribute:: bits_per_sample

      How many bits are used to store one sample in one plane.

   .. py:attribute:: bytes_per_sample

      The actual storage is padded up to 2^n bytes for efficiency.

   .. py:attribute:: channel_layout

      A mask of used channels.

   .. py:attribute:: num_channels

      The number of channels the format has.

   .. py:attribute:: sample_rate

      Playback sample rate.

   .. py:method:: get_frame(n)

      Returns an AudioFrame from position *n*.

   .. py:method:: get_frame_async(n)

      Returns a concurrent.futures.Future-object which result will be an AudioFrame instance or sets the
      exception thrown when rendering the frame.

      *The future will always be in the running or completed state*

   .. py:method:: get_frame_async_raw(n, cb: callable)

      First form of this method. It will call the callback from another thread as soon as the frame is rendered.

      The `result`-value passed to the callback will either be a AudioFrame-instance on success or a Error-instance
      on failure.

      *This method is intended for glue code. For normal use, use get_frame_async instead.*

      :param n: The frame number
      :param cb: A callback in the form `cb(node, n, result)`

   .. py:method:: get_frame_async_raw(n, cb: Future[, wrapper: callable = None])
      :noindex:

      Second form of this method. It will take a Future-like object (including asyncio.Future or similar)
      and set its result or exception according to the result of the function.

      The optional `wrapper`-parameter is intended for calls like asyncio.EventLoop.call_soon_threadsafe in which
      all calls to its future-object must be wrapped.

      *This method is intended for glue code. For normal use, use get_frame_async instead.*

      :param n: The frame number
      :param cb: The future-object whose result will be set.
      :param wrapper: A wrapper-callback which is responsible for moving the result across thread boundaries. If not
                      given, the result of the future will be set in a random thread.

   .. py:method:: set_output(index = 0)

      Set the clip to be accessible for output.

   .. py:method:: frames([prefetch=None, backlog=None])

      Returns a generator iterator of all AudioFrames in the clip. It will render multiple frames concurrently.

      The *prefetch* argument defines how many frames are rendered concurrently. Is only there for debugging purposes and should never need to be changed.
      The *backlog* argument defines how many unconsumed frames (including those that did not finish rendering yet) vapoursynth buffers at most before it stops rendering additional frames. This argument is there to limit the memory this function uses storing frames.

   .. py:method:: is_inspectable(version=None)
   
      Returns a truthy value if you can use the node inspection API with a given version.
      The python inspection-api is versioned, as the underlying API is unstable at the time of writing.
      The version number will be incremented every time the python API changes.
      There will be no attempt to maintain backwards compatibility as long as the API is marked as unstable.

      This method may never return a truthy value.

      This is the only stable function in the current inspection api-implementation.

      .. note::

         Be aware that introspection features must be enabled manually by the backing environment. Standalone Python-Scripts,
         not running inside vspipe or other editors, have introspection enabled automatically.

      .. warning::

         The graph-inspection-api is unstable. Omitting the version-argument will therefore always return
         None.

      The current version of the unstable python graph-inspection API is 0.

      Added: R58

      :param version: If None, it will use the version number of the last stable API.


.. py:class:: AudioFrame

      This class represents an audio frame and all metadata attached to it.

   .. py:attribute:: sample_type

      If the format is integer or floating point based.

   .. py:attribute:: bits_per_sample

      How many bits are used to store one sample in one plane.

   .. py:attribute:: bytes_per_sample

      The actual storage is padded up to 2^n bytes for efficiency.

   .. py:attribute:: channel_layout

      A mask of used channels.

   .. py:attribute:: num_channels

      The number of channels the format has.

   .. py:attribute:: readonly

      If *readonly* is True, the frame data and properties cannot be modified.

   .. py:attribute:: props

      This attribute holds all the frame's properties as a dict. Note that audio frame properties are fairly
      non-sensical as a concept for audio due to an arbitrary number of samples being lumped together and rarely used.

   .. py:method:: copy()

      Returns a writable copy of the frame.

   .. py:method:: get_read_ptr(plane)

      Returns a pointer to the raw frame data. The data may not be modified.

   .. py:method:: get_write_ptr(plane)

      Returns a pointer to the raw frame data. It may be modified using ctypes
      or some other similar python package.

   .. py:method:: get_stride(plane)

      Returns the stride between lines in a *plane*.

.. py:class:: Plugin

   Plugin is a class that represents a loaded plugin and its namespace.

   .. py:attribute:: namespace

      The namespace of the plugin.

   .. py:attribute:: name

      The name string of the plugin.

   .. py:attribute:: identifier

   .. py:method:: functions()

      Containing all the functions in the plugin, You can access it by calling *core.<namespace>.functions()*.

   .. py:method:: get_functions()

      Deprecated, use *functions()* instead.

   .. py:method:: list_functions()

      Deprecated, use *functions()* instead.

.. py:class:: Function

   Function is a simple wrapper class for a function provided by a VapourSynth plugin.
   Its main purpose is to be called and nothing else.

   .. py:attribute:: name

      The function name. Identical to the string used to register the function.

   .. py:attribute:: plugin

      The *Plugin* object the function belongs to.

   .. py:attribute:: signature

      Raw function signature string. Identical to the string used to register the function.

   .. py:attribute:: return_signature

      Raw function signature string. Identical to the return type string used register the function.

.. py:class:: Environment

   This class represents an environment.

   Some editors allow multiple vapoursynth-scripts to run in the same process, each of them comes with a different Core-instance and
   their own set of outputs. Each core-instance with their associated outputs represent their own environment.

   At any given time, only one environment can be active (in the same context). This class allows introspection about
   environments and allows to switch to them at will.

   .. code::

        env = get_current_environment()
        # sometime later
        with env.use():
          # Do stuff inside this env.

   .. warning::

      Environment-objects obtained using the :func:`vpy_current_environment` can directly be used as
      as a context manager. This can cause undefined behaviour when used in combination with generators and/or
      coroutines.

      This context-manager maintains a thread-local environment-stack that is used to restore the previous environment.
      This can cause issues if the frame is suspended inside the block.

      A similar problem also existed in previous VapourSynth versions!

      .. code::

         env = vpy_current_environment()
         with env:
              yield

   .. py:function:: is_single()

      Returns True if the script is _not_ running inside a vsscript-Environment.
      If it is running inside a vsscript-Environment, it returns False.

   .. py:attribute:: env_id

      Return -1 if the script is not running inside a vsscript-Environment.
      Otherwise, it will return the current environment-id.

   .. py:attribute:: single

      See is_single()

   .. py:attribute:: alive

      Has the environment been destroyed by the underlying application?

   .. py:method:: copy()

      Creates a copy of the environment-object.

      Added: R51

   .. py:method:: use()

      Returns a context-manager that enables the given environment in the block enclosed in the with-statement and restores the environment to the one
      defined before the with-block has been encountered.

      .. code::

         env = get_current_environment()
         with env.use():
             with env.use():
                 pass

      Added: R51

.. py:function:: vpy_current_environment()

   Deprecated. Use :func:`get_current_environment` instead.

   This function has been deprecated as this function has undefined behaviour when used together with generators or coroutines.

.. py:function:: get_current_environment()

   Returns an Environment-object representing the environment the script is currently running in. It will raise an error if we are currently not inside any
   script-environment while vsscript is being used.

   This function is intended for Python-based editors using vsscript.

   Added: R51

.. py:class:: EnvironmentPolicy

   This class is intended for subclassing by custom Script-Runners and Editors.
   Normal users don't need this class. Most methods implemented here have corresponding APIs in other parts of this module.

   An instance of this class controls which environment is activated in the current context.
   The exact meaning of "context" is defined by the concrete EnvironmentPolicy. A environment is represented by a :class:`EnvironmentData`-object.

   To use this class, first create a subclass and then use :func:`register_policy` to get VapourSynth to use your policy. This must happen before vapoursynth is first
   used. VapourSynth will automatically register an internal policy if it needs one. The subclass must be weak-referenciable!

   Once the method :meth:`on_policy_registered` has been called, the policy is responsible for creating and managing environments.

   Special considerations have been made to ensure the functions of class cannot be abused. You cannot retrieve the current running policy yourself.
   The additional API exposed by "on_policy_registered" is only valid if the policy has been registered.
   Once the policy is unregistered, all calls to the additional API will fail with a RuntimeError.

   Added: R51

   .. py:method:: on_policy_registered(special_api)

      This method is called when the policy has successfully been registered. It proivdes additional internal methods that are hidden as they are useless and or harmful
      unless you implement your own policy.

      :param special_api: This is a :class:`EnvironmentPolicyAPI`-object that exposes additional API

   .. py:method:: on_policy_cleared()

      This method is called once the python-process exits or when unregister_policy is called by the environment-policy. This allows the policy to free the resources
      used by the policy.

   .. py:method:: get_current_environment()

      This method is called by the module to detect which environment is currently running in the current context. If None is returned, it means that no environment is currently active.

      :returns: An :class:`EnvironmentData`-object representing the currently active environment in the current context.

   .. py:method:: set_environment(environment)

      This method is called by the module to change the currently active environment. If None is passed to this function the policy may switch to another environment of its choosing.

      Note: The function is responsible to check whether or not the environment is alive. If a dead environment is passed, it should act like None has been passed instead of the dead environment but must never error.

      :param environment: The :class:`EnvironmentData` to enable in the current context.
      :returns: The environment that was enabled previously.

   .. py:method:: is_alive(environment)

      Is the current environment still active and managed by the policy.

      The default implementation checks if `EnvironmentPolicyAPI.destroy_environment` has been called on the environment.


.. py:class:: EnvironmentPolicyAPI

   This class is intended to be used by custom Script-Runners and Editors. An instance of this class exposes an additional API.
   The methods are bound to a specific :class:`EnvironmentPolicy`-instance and will only work if the policy is currently registered.

   Added: R51

   .. py:method:: wrap_environment(environment)

      Creates a new :class:`Environment`-object bound to the passed environment-id.

      .. warning::

         This function does not check if the id corresponds to a live environment as the caller is expected to know which environments are active.

   .. py:method:: create_environment(flags = 0)

      Returns a :class:`Environment` that is used by the wrapper for context sensitive data used by VapourSynth.
      For example it holds the currently active core object as well as the currently registered outputs.

   .. py:method:: set_logger(environment, callback)

      This function sets the logger for the given environment.

      This logger is a callback function that accepts two parameters: Level, which is an instance of vs.MessageType and a string containing the log message.

   .. py:method:: destroy_environment(environment)

      Marks an environment as destroyed. Older environment-policy implementations that don't use this function still work.

      Either EnvironmentPolicy.is_alive must be overridden or this method be used to mark the environment as destroyed.

      Added: R52

   .. py:method:: unregister_policy()

      Unregisters the policy it is bound to and allows another policy to be registered.

.. py:function:: register_policy(policy)

   This function is intended for use by custom Script-Runners and Editors. It installs your custom :class:`EnvironmentPolicy`. This function only works if no other policy has been
   installed.

   If no policy is installed, the first environment-sensitive call will automatically register an internal policy.

   Added: R50

   .. note::

      This must be done before VapourSynth is used in any way. Here is a non-exhaustive list that automatically register a policy:

      * Using "vsscript_init" in "VSScript.h"
      * Using :func:`get_outputs`
      * Using :func:`get_output`
      * Using :func:`clear_output`
      * Using :func:`clear_outputs`
      * Using :func:`vpy_current_environment`
      * Using :func:`get_current_environment`
      * Accessing any attribute of :attr:`core`


.. py:function:: _try_enable_introspection(version=None)

   Tries to enable introspection. Returns true if it succeeds.

   :param version: If not passed it will use the newest stable introspection-api.

   Added: R58

.. py:function:: has_policy()

   This function is intended for subclassing by custom Script-Runners and Editors. This function checks if a :class:`EnvironmentPolicy` has been installed.

   Added: R50

.. py:class:: EnvironmentData

   Internal class that stores the context sensitive data that VapourSynth needs. It is an opaque object whose attributes you cannot access directly.

   A normal user has no way of getting an instance of this object. You can only encounter EnvironmentData-objects if you work with EnvironmentPolicies.

   This object is weak-referenciable meaning you can get a callback if the environment-data object is actually being freed (i.e. no other object holds an instance
   to the environment data.)

   Added: R50

.. py:class:: Func

   Func is a simple wrapper class for VapourSynth VSFunc objects.
   Its main purpose is to be called and manage reference counting.

.. py:exception:: Error

   The standard exception class. This exception is thrown on most errors
   encountered in VapourSynth.

Constants
#########

Video
*****

Color Family
------------

The color family constants describe groups of formats and the basic way their
color information is stored. You should be familiar with all of them apart from
maybe *YCOCG* and *COMPAT*. The latter is a special junk category for non-planar
formats. These are the declared constants in the module::

   UNDEFINED
   RGB
   YUV
   GRAY

Format
------

Format constants exactly describe a format. All common and even more uncommon
formats have handy constants predefined so in practice no one should really
need to register one of their own. These values are mostly used by the resizers
to specify which format to convert to. The naming system is quite simple. First
the color family, then the subsampling (only YUV has it) and after that how many
bits per sample in one plane. The exception to this rule is RGB, which has the
bits for all 3 planes added together. The long list of values::

   NONE
   GRAY8
   GRAY9
   GRAY10
   GRAY12
   GRAY14
   GRAY16
   GRAY32
   GRAYH
   GRAYS
   YUV420P8
   YUV422P8
   YUV444P8
   YUV410P8
   YUV411P8
   YUV440P8
   YUV420P9
   YUV422P9
   YUV444P9
   YUV420P10
   YUV422P10
   YUV444P10
   YUV420P12
   YUV422P12
   YUV444P12
   YUV420P14
   YUV422P14
   YUV444P14
   YUV420P16
   YUV422P16
   YUV444P16
   YUV444PH
   YUV444PS
   RGB24
   RGB27
   RGB30
   RGB36
   RGB42
   RGB48
   RGBH
   RGBS

Chroma Location
---------------

::

   CHROMA_LEFT
   CHROMA_CENTER
   CHROMA_TOP_LEFT
   CHROMA_TOP
   CHROMA_BOTTOM_LEFT
   CHROMA_BOTTOM

Field Based
-----------

::

   FIELD_PROGRESSIVE
   FIELD_TOP
   FIELD_BOTTOM

Color Range
-----------

::

   RANGE_FULL
   RANGE_LIMITED

Matrix Coefficients
-------------------

::

   MATRIX_RGB
   MATRIX_BT709
   MATRIX_UNSPECIFIED
   MATRIX_FCC
   MATRIX_BT470_BG
   MATRIX_ST170_M
   MATRIX_YCGCO
   MATRIX_BT2020_NCL
   MATRIX_BT2020_CL
   MATRIX_CHROMATICITY_DERIVED_NCL
   MATRIX_CHROMATICITY_DERIVED_CL
   MATRIX_ICTCP

TransferCharacteristics
-----------------------

::

   TRANSFER_BT709
   TRANSFER_UNSPECIFIED
   TRANSFER_BT470_M
   TRANSFER_BT470_BG
   TRANSFER_BT601
   TRANSFER_ST240_M
   TRANSFER_LINEAR
   TRANSFER_LOG_100
   TRANSFER_LOG_316
   TRANSFER_IEC_61966_2_4
   TRANSFER_IEC_61966_2_1
   TRANSFER_BT2020_10
   TRANSFER_BT2020_12
   TRANSFER_ST2084
   TRANSFER_ARIB_B67

Color Primaries
---------------

::

   PRIMARIES_BT709
   PRIMARIES_UNSPECIFIED
   PRIMARIES_BT470_M
   PRIMARIES_BT470_BG
   PRIMARIES_ST170_M
   PRIMARIES_ST240_M
   PRIMARIES_FILM
   PRIMARIES_BT2020
   PRIMARIES_ST428
   PRIMARIES_ST431_2
   PRIMARIES_ST432_1
   PRIMARIES_EBU3213_E

Audio
*****

Channels
--------

::

   FRONT_LEFT
   FRONT_RIGHT
   FRONT_CENTER
   LOW_FREQUENCY
   BACK_LEFT
   BACK_RIGHT
   FRONT_LEFT_OF_CENTER
   FRONT_RIGHT_OF_CENTER
   BACK_CENTER
   SIDE_LEFT
   SIDE_RIGHT
   TOP_CENTER
   TOP_FRONT_LEFT
   TOP_FRONT_CENTER
   TOP_FRONT_RIGHT
   TOP_BACK_LEFT
   TOP_BACK_CENTER
   TOP_BACK_RIGHT
   STEREO_LEFT
   STEREO_RIGHT
   WIDE_LEFT
   WIDE_RIGHT
   SURROUND_DIRECT_LEFT
   SURROUND_DIRECT_RIGHT
   LOW_FREQUENCY2

Sample Type
***********

::

   INTEGER
   FLOAT
