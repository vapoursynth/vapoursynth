Median
======

.. function:: Median(clip clip[, int[] planes=[0, 1, 2]])
   :module: std

   Replaces each pixel with the median of the nine pixels in its 3x3
   neighbourhood. In other words, the nine pixels are sorted from lowest
   to highest, and the middle value is picked.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
