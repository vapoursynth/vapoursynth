VapourSynth C API Reference
===========================

See the example filters in the sdk dir. Reading simplefilters.c, which contains
almost all built-in functions, can also be very helpful.

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

   The frame's absolute timestamp in seconds if reported by the source filter.
   Should only be set by the source filter and not be modified.
   _AbsoluteTime float

   Chroma sample position in YUV formats.
   _ChromaLocation int (0=left, 1=center, 2=topleft, 3=top, 4=bottomleft, 5=bottom)

   Full or limited range (PC/TV range). Primarily used with YUV formats.
   _ColorRange int (0 = full range, 1 = limited range)

   Encoded as in avcodec.h in libavcodec. It's so convoluted I'm not
   going to try to describe it here.
   _ColorSpace int

   Whether or not the frame needs postprocessing, usually hinted from field
   matching filters.
   _Combed bint

   The frame's duration in seconds as a rational number.
   Filters that modify the framerate should also change this value.
   _DurationNum int
   _DurationDen int

   If the frame has been split into fields this says if the frame was
   derived from top or bottom fields.
   _Field int (0 = from bottom field, 1 = from top field)

   If the frame is composed of two independent fields (interlaced).
   _FieldBased int (0 = frame based, 1 = field based)

   A single character describing the frame type. It uses the common
   IPB letters but other letters may also be used for formats with
   additional frame types.
   _PictType data

   Display aspect ratio as a rational number.
   _SARNum int
   _SARDen int

   Indicates a scenechange for the next/previous frame transition.
   _SceneChangeNext bint
   _SceneChangePrev bint
