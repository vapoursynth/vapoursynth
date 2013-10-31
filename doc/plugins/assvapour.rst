.. _assvapour:

AssVapour
=========

AssVapour is a subtitle renderer that uses libass.

.. function::   AssRender(clip clip, string file[, string charset="UTF-8", int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, float scale=1])
   :module: assvapour

   AssRender takes the ASS script *file* and returns a list of two clips. The
   first one is an RGB24 clip containing the rendered subtitles. The second one
   is a Y8 clip containing a mask, to be used for blending the rendered
   subtitles into other clips.

   Parameters:
      clip
         Clip whose dimensions will be used as reference for the output clips.
         It is not modified.

      file
         ASS script to be rendered.

      charset
         Character set of the ASS script, in enca or iconv format.

      debuglevel
         Debug level. Increase to make libass more chatty.
         See `ass_utils.h <https://code.google.com/p/libass/source/browse/libass/ass_utils.h>`_
         in libass for the list of meaningful values.

      fontdir
         Directory with additional fonts.

      linespacing
         Space between lines, in pixels.

      margins
         Additional margins, in pixels. Negative values are allowed. The order
         is top, bottom, left, right.

      sar
         Storage aspect ratio.

      scale
         Font scale.


.. function::   Subtitle(clip clip, string text[, int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1"])
   :module: assvapour

   Instead of rendering an ASS script, Subtitle renders the string *text*.
   It returns two clips, same as AssRender.

   Parameters:
      text
         String to be rendered.

      style
         Custom ASS style to be used.

   The other parameters have the same meanings as with AssRender.


Example::

   # assume c is the core and video is a YUV420P8 clip
   subs = c.assvapour.AssRender(video, "asdf.ass")
   subs[0] = c.resize.Bicubic(subs[0], format=vs.YUV420P8)
   hardsubbed_video = c.std.MaskedMerge([video, subs[0]], subs[1])

