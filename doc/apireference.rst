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
   int _ColorPrimaries
   
   As specified in ISO/IEC 14496-10, Matrix coefficients table
   int _ColorMatrix
   
   As specified in ISO/IEC 14496-10, Transfer characteristics table
   int _TransferCharacteristics
   
   If the frame is composed of two independent fields (interlaced).
   int _FieldBased (0=frame based, 1=BFF, 2=TFF)

   The frame's absolute timestamp in seconds if reported by the source filter.
   Should only be set by the source filter and not be modified. Use durations
   for all operations that depend on frame length.
   float _AbsoluteTime

   The frame's duration in seconds as a rational number.
   Filters that modify the framerate should also change this value.
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
