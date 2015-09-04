VapourSynth C API Reference
===========================

See the example filters in the sdk dir. Reading simplefilters.c, which contains
several built-in functions, can also be very helpful.

Public Headers
##############

.. toctree::
   :maxdepth: 1
   :glob:

   api/*
   
Common Pitfalls
###############

There are several minor pitfalls related to the threading and design that have to be taken into consideration. Most of them are usually aren't a problem but here's a small checklist of things you have to watch out for sometimes.

General API
-----------
You may not pass objects (clips, functions and so on) owned by one core as arguments to filters in another core. A manual full deep copy of the data you want to pass on is required. This is generally not a problem since you should never need more than one core per filter graph.

Plugins
-------
Plugin code may run more multithreaded than it initially appears. *VapourSynthPluginInit* is the only function always guaranteed to not run in parallel. This means that the contructor and destructor of a filter may be run in parallel for several instances. Use proper synchronization if you need to initialize shared data.

The *GetFrame* function is a bit more complicated so see the reference of the constants. Do however note that the parallelism is per instance. Even if a filter is *fmUnordered* or *fmSerial* other instances may enter *GetFrame* simultaneously.

There are two common misconseptions about which mode should be used. A simple rule is that *fmSerial* should never be used. And source filters (those returning a frame on *arInitial*) that need locking should use *fmUnordered*.

VSScript/Python
---------------
Python state is global, if an additional module has once been loaded it won't refresh automatically. Therefore never call vs.get_core() in the global scope of a module that can be imported.

   WRONG::
   
      user_function.py:
      import vapoursynth as vs
      core = vs.get_core()
      def foo():
         return core.std.BlankClip()

   RIGHT::
   
      user_function.py:
      import vapoursynth as vs
      def foo():
         core = vs.get_core()
         return core.std.BlankClip()

   MAIN SCRIPT::
   
      main.vpy:
      import vapoursynth as vs
      import user_function
      core = vs.get_core()
      user_function.foo().set_output()

In the wrong case the core variable in user_function.py will only be assigned the first time the script it run, meaning that it won't use the same core as the rest of the script after a reload.
In R26 and later this will return an error. Earlier versions may or may not crash with a fatal error.

Reserved Frame Properties
#########################

All frames contain a map of key--value pairs. It is recommended that these
properties are named using only a-z, A-Z, 0-9 using CamelCase. There is a
special category of keys starting with _ which have strictly defined meanings
specified below. It is acceptable to not set any of these keys if they are
unknown. It is also a fatal error to set them to a value not specified below.

::

   Chroma sample position in YUV formats.
   int _ChromaLocation (0=left, 1=center, 2=topleft, 3=top, 4=bottomleft, 5=bottom)
   
   Full or limited range (PC/TV range). Primarily used with YUV formats.
   int _ColorRange (0=full range, 1=limited range)
 
   As specified in ISO/IEC 14496-10, Colour primaries table
   int _Primaries
   
   As specified in ISO/IEC 14496-10, Matrix coefficients table
   int _Matrix
   
   As specified in ISO/IEC 14496-10, Transfer characteristics table
   int _Transfer
   
   If the frame is composed of two independent fields (interlaced).
   int _FieldBased (0=frame based, 1=BFF, 2=TFF)

   The frame's absolute timestamp in seconds if reported by the source filter.
   Should only be set by the source filter and not be modified. Use durations
   for all operations that depend on frame length.
   float _AbsoluteTime

   The frame's duration in seconds as a rational number. Filters that
   modify the framerate should also change this value.
   Should always be normalized.
   int _DurationNum
   int _DurationDen
   
   Whether or not the frame needs postprocessing, usually hinted from field
   matching filters.
   bint _Combed

   If the frame has been split into fields this says if the frame was
   derived from top or bottom fields.
   int _Field (0=from bottom field, 1=from top field)

   A single character describing the frame type. It uses the common
   IPB letters but other letters may also be used for formats with
   additional frame types.
   string _PictType

   Pixel aspect ratio as a rational number.
   int _SARNum
   int _SARDen

   Indicates a scenechange for the next/previous frame transition.
   bint _SceneChangeNext
   bint _SceneChangePrev

Deprecated frame properties::

   Encoded as in avcodec.h in libavcodec. It's so convoluted I'm not
   going to try to describe it here.
   int _ColorSpace
