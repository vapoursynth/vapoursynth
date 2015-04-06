Levels
======

.. function:: Levels(clip clip[, int min_in=0, int max_in, float gamma=1.0, int min_out=0, int max_out, int[] planes=[0, 1, 2]])
   :module: std

   Adjusts brightness, contrast, and gamma.

   The range [*min_in*, *max_in*] is remapped into [*min_out*, *max_out*].

   For example, to convert from limited range YUV to full range (8 bit)::

      clip = std.Levels(clip, min_in=16, max_in=235, min_out=0, max_out=255, planes=0)
      clip = std.Levels(clip, min_in=16, max_in=240, min_out=0, max_out=255, planes=[1,2])

   The default value of *max_in* and *max_out* is the format's maximum
   allowed value.

   *clip*
      Clip to process. It must have constant format, integer sample type,
      and bit depth between 8 and 16.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
