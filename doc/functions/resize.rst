Resize
======

.. function::   Bilinear(clip clip[, int width, int height, int format, ...])
                Bicubic(clip clip[, int width, int height, int format, ...])
                Point(clip clip[, int width, int height, int format, ...])
                Lanczos(clip clip[, int width, int height, int format, ...])
                Spline16(clip clip[, int width, int height, int format, ...])
                Spline36(clip clip[, int width, int height, int format, ...])
   :module: resize
   
   Note that the resize functions are a simple wrapper for zimg. See the zimg
   documentation for the full list of arguments. The differently named
   functions simply set the *resample_filter* argument to the appropriate
   value and passes all the other arguments through.

   In VapourSynth the resizers have several functions. In addition to scaling,
   they also do colorspace conversions and conversions to and from the compat
   formats.

   All the optional arguments default to the input *clip*'s properties. The
   resize filters can handle varying size and format input clips and turn them
   into constant format clips.

   If you do not know which resizer to choose, then try Bicubic. It usually
   makes a good neutral default.

   The function will return an error if the subsampling restrictions aren't
   followed.

   To convert to YV12::

      Bicubic(clip=clip, format=vs.YUV420P8)

   To resize and convert to planar RGB::

      Bicubic(clip=clip, width=1920, height=1080, format=vs.RGB24)
