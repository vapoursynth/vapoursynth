.. _subtext:

Subtext
=======

Subtext is a subtitle renderer that uses libass and ffmpeg.

.. function::   TextFile(clip clip, string file[, string charset="UTF-8", float scale=1, int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, string style="", bint blend=True, int matrix, string matrix_s, int transfer, string transfer_s, int primaries, string primaries_s])
   :module: sub

   TextFile renders text subtitles. Supported formats include ASS,
   JACOsub, MicroDVD, SAMI, SRT, WebVTT, and some other obscure ones.

   TextFile has two modes of operation. With blend=True (the default),
   it returns *clip* with the subtitles burned in. With blend=False, it
   returns a list of two clips. The first one is an RGB24 clip
   containing the rendered subtitles. The second one is a Gray8 clip
   containing a mask, to be used for blending the rendered subtitles
   into other clips.

   Parameters:
      clip
         Input clip.

      file
         Subtitle file to be rendered.

      charset
         Character set of the subtitle, in iconv format.

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
         Additional margins, in pixels. Negative values are not
         allowed. The order is top, bottom, left, right.

      sar
         Storage aspect ratio.

      style
         Custom ASS style for subtitle formats other than ASS. If empty
         (the default), libavcodec's default style is used. This
         parameter has no effect on ASS subtitles.

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


.. function::   Subtitle(clip clip, string text[, int start=0, int end=clip.numFrames, int debuglevel=0, string fontdir="", float linespacing=0, int[] margins=[0, 0, 0, 0], float sar=0, string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1", bint blend=True, int matrix, string matrix_s, int transfer, string transfer_s, int primaries, string primaries_s])
   :module: sub

   Instead of rendering a subtitle file, Subtitle renders the string *text*.
   Otherwise it works the same as TextFile.

   Parameters:
      text
         String to be rendered. This can include ASS tags to enable rich text and animation.

      style
         Custom ASS style to be used.
      
      start, end
         Subtitle will be shown from *start* up until *end*. By default this will be for all frames in *clip*.

   The other parameters have the same meanings as with TextFile.


.. function::   ImageFile(clip clip, string file[, int id=-1, int[] palette, bint gray=False, bint info=False, bint flatten=False, bint blend=True, int matrix, string matrix_s, int transfer, string transfer_s, int primaries, string primaries_s])
   :module: sub

   ImageFile renders image-based subtitles such as VOBSUB and PGS.

   ImageFile has two modes of operation. With blend=True (the default),
   it returns *clip* with the subtitles burned in. With blend=False, it
   returns an RGB24 clip containing the rendered subtitles, with a Gray8
   frame attached to each frame in the ``_Alpha`` frame property. These
   Gray8 frames can be extracted using std.PropToClip.

   Parameters:
      *clip*
         If *blend* is True, the subtitles will be burned into this
         clip, Otherwise, only the frame rate and number of frames
         will be obtained from this clip.

      *file*
         Name of the subtitle file. For VOBSUB, it must the name of the
         idx file. The corresponding sub file must be in the same
         folder, and it must have the same name.

      *id*
         Id of the subtitle track to render. There may be several
         subtitle tracks in the same file. If this is -1, the first
         supported subtitle track will be rendered. Use info=True to
         see a list of all subtitle tracks, including their ids.

         Default: -1.

      *palette*
         Custom palette. This is an array of at most 256 integers. Each
         element's least significant four bytes must contain the values
         for alpha, red, green, and blue, in that order, from most
         significant to least.

         Additionally, the special value 2**42 means that the
         corresponding element of the original palette is used. This
         way it is possible to override only the third element, without
         overriding the first and second ones, for example.

         An alpha value of 255 means the colour will be completely
         opaque, and a value of 0 means the colour will be completely
         transparent.

      *gray*
         If True, the subtitles will be turned gray.

         Default: False.

      *info*
         If this is True, a list of all supported subtitle tracks found
         in the file will be printed on each frame of the output. The
         information printed about each track includes the id, the
         language (if known), the resolution, and the format.

         Default: False.

      *flatten*
         If this is True, ImageFile will output a clip with exactly as
         many frames as there are pictures in the subtitle file.

         If this is True, *blend* has no effect (no automatic blending).

         Default: False.

   The other parameters have the same meanings as with TextFile.


Example with manual blending::

   subs = core.sub.TextFile(clip=YUV420P10_video, file="asdf.ass", blend=False)

   gray10 = core.register_format(subs[1].format.color_family,
                                 YUV420P10_video.format.sample_type,
                                 YUV420P10_video.format.bits_per_sample,
                                 subs[1].format.subsampling_w,
                                 subs[1].format.subsampling_h)

   subs[0] = core.resize.Bicubic(clip=subs[0], format=YUV420P10_video.format.id, matrix_s="470bg")
   subs[1] = core.resize.Bicubic(clip=subs[1], format=gray10.id)

   hardsubbed_video = core.std.MaskedMerge(clipa=YUV420P10_video, clipb=subs[0], mask=subs[1])

Example with automatic blending (will use BT709 matrix)::

   hardsubbed_video = core.sub.TextFile(clip=YUV420P10_video, file="asdf.ass")

Example with a custom palette and automatic blending::

   def rgba(r, g, b, a=255):
       if r < 0 or r > 255 or g < 0 or g > 255 or b < 0 or b > 255 or a < 0 or a > 255:
           raise vs.Error("Colours must be in the range [0, 255].")

       return (a << 24) + (r << 16) + (g << 8) + b
   
   unused = 1 << 42

   src = core.ffms2.Source("video.mp4")

   # Override only the third element of the palette. Set it to some kind of green.
   ret = core.sub.ImageFile(src, "subtitles.sup", palette=[unused, unused, rgba(0, 192, 128)])
