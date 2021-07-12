Invert/InvertMask
=================

.. function:: Invert(clip clip[, int[] planes=[0, 1, 2]])
.. function:: InvertMask(clip clip[, int[] planes=[0, 1, 2]])
   :module: std

   Inverts the pixel values. Specifically, it subtracts the value of the
   input pixel from the format's maximum allowed value. The *InvertMask*
   version is intended for use on mask clips where all planes have the
   same maximum value regardless of the colorspace.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
