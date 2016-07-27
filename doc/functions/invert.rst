Invert
======

.. function:: Invert(clip clip[, int[] planes=[0, 1, 2]])
   :module: std

   Inverts the pixel values. Specifically, it subtracts the value of the
   input pixel from the format's maximum allowed value.

   *clip*
      Clip to process.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
