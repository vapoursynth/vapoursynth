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

There are several minor pitfalls related to the threading and design that have to be taken into consideration. Most of them usually aren't a problem but here's a small checklist of things you have to watch out for sometimes.

General API
-----------
You may not pass objects (clips, functions and so on) owned by one core as arguments to filters in another core. A manual full deep copy of the data you want to pass on is required. This is generally not a problem since you should never need more than one core per filter graph.

Plugins
-------
Plugin code may run more multithreaded than it initially appears. *VapourSynthPluginInit* is the only function always guaranteed to not run in parallel. This means that the contructor and destructor of a filter may be run in parallel for several instances. Use proper synchronization if you need to initialize shared data.

The *GetFrame* function is a bit more complicated so see the reference of the constants. Do however note that the parallelism is per instance. Even if a filter is *fmUnordered* or *fmSerial* other instances may enter *GetFrame* simultaneously.

There are two common misconseptions about which mode should be used. A simple rule is that *fmSerial* should never be used. And source filters (those returning a frame on *arInitial*) that need locking should use *fmUnordered*.

Reserved Frame Properties
#########################

All frames contain a map of key--value pairs. It is recommended that these
properties are named using only a-z, A-Z, 0-9 using CamelCase. There is a
special category of keys starting with _ which have strictly defined meanings
specified below. It is acceptable to not set any of these keys if they are
unknown. It is also a fatal error to set them to a value not specified below.

int _ChromaLocation

   Chroma sample position in YUV formats.
   
   0=left, 1=center, 2=topleft, 3=top, 4=bottomleft, 5=bottom.

int _ColorRange

   Full or limited range (PC/TV range). Primarily used with YUV formats.

   0=full range, 1=limited range.
 
int _Primaries

   Color primaries as specified in ITU-T H.265 Table E.3.
   
int _Matrix

   Matrix coefficients as specified in ITU-T H.265 Table E.5.
   
int _Transfer

   Transfer characteristics as specified in ITU-T H.265 Table E.4.
   
int _FieldBased

   If the frame is composed of two independent fields (interlaced).

   0=frame based (progressive), 1=bottom field first, 2=top field first.

float _AbsoluteTime

   The frame's absolute timestamp in seconds if reported by the source filter.
   Should only be set by the source filter and not be modified. Use durations
   for all operations that depend on frame length.

int _DurationNum, int _DurationDen

   The frame's duration in seconds as a rational number. Filters that
   modify the framerate should also change these values.

   This fraction should always be normalized.
   
bint _Combed

   Whether or not the frame needs postprocessing, usually hinted from field
   matching filters.

int _Field

   If the frame was produced by something like core.std.SeparateFields,
   this property signals which field was used to generate this frame.

   0=from bottom field, 1=from top field.

string _PictType

   A single character describing the frame type. It uses the common
   IPB letters but other letters may also be used for formats with
   additional frame types.

int _SARNum, int _SARDen

   Pixel (sample) aspect ratio as a rational number.

bint _SceneChangeNext

   If 1, this frame is the last frame of the current scene. The next frame starts a new scene.

bint _SceneChangePrev

   If 1, this frame starts a new scene.

frame _Alpha

   A clip's alpha channel can be attached to the clip one frame at a
   time using this property.

Deprecated Frame Properties
---------------------------

int _ColorSpace

   Superseded by _Matrix, _Transfer, and _Primaries.
