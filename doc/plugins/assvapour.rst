.. _assvapour:

AssVapour
=========

AssVapour is a subtitle renderer that uses libass.

.. function::   AssRender(clip clip, string file[, string charset="UTF-8", float scale=1, int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, bint blend=True, int matrix, string matrix_s, int transfer, string transfer_s, int primaries, string primaries_s])
   :module: assvapour

   AssRender has two modes of operation. With blend=True (the default),
   it returns *clip* with the subtitles burned in. With blend=False, it
   returns a list of two clips. The first one is an RGB24 clip
   containing the rendered subtitles. The second one is a Gray8 clip
   containing a mask, to be used for blending the rendered subtitles
   into other clips.

   Parameters:
      clip
         Input clip.

      file
         ASS script to be rendered.

      charset
         Character set of the ASS script, in enca or iconv format.

      scale
         Font scale.

      debuglevel
         Debug level. Increase to make libass more chatty.
         See `ass_utils.h <https://github.com/libass/libass/blob/master/libass/ass_utils.h>`_
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

      blend
         If True, the subtitles will be blended into *clip*. Otherwise,
         the bitmaps will be returned untouched.

      matrix

      matrix_s

      transfer

      transfer_s

      primaries

      primaries_s
         If blend=True, these will be passed to resize.Bicubic when
         converting the RGB24 subtitles to YUV. The default matrix is
         "709".


.. function::   Subtitle(clip clip, string text[, string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1", int start=0, int end=clip.numFrames, int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, bint blend=True, int matrix, string matrix_s, int transfer, string transfer_s, int primaries, string primaries_s])
   :module: assvapour

   Instead of rendering an ASS script, Subtitle renders the string *text*.
   Otherwise it works the same as AssRender.

   Parameters:
      text
         String to be rendered. This can include ASS tags to enable rich text and animation.

      style
         Custom ASS style to be used.
      
      start, end
         Subtitle will be shown from *start* up until *end*. By default this will be for all frames in *clip*.

   The other parameters have the same meanings as with AssRender.


Example with manual blending::

   subs = core.assvapour.AssRender(clip=YUV420P10_video, file="asdf.ass", blend=False)

   gray10 = core.register_format(subs[1].format.color_family,
                                 YUV420P10_video.format.sample_type,
                                 YUV420P10_video.format.bits_per_sample,
                                 subs[1].format.subsampling_w,
                                 subs[1].format.subsampling_h)

   subs[0] = core.resize.Bicubic(clip=subs[0], format=YUV420P10_video.format.id, matrix_s="470bg")
   subs[1] = core.resize.Bicubic(clip=subs[1], format=gray10.id)

   hardsubbed_video = core.std.MaskedMerge(clipa=YUV420P10_video, clipb=subs[0], mask=subs[1])

Example with automatic blending (will use BT709 matrix)::

   hardsubbed_video = core.assvapour.AssRender(clip=YUV420P10_video, file="asdf.ass")

