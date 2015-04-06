Binarize
========

.. function:: Binarize(clip clip[, int threshold, int v0=0, int v1, int[] planes=[0, 1, 2]])
   :module: std

   Turns every pixel in the image into either *v0*, if it's below
   *threshold*, or *v1*, otherwise.

   *clip*
      Clip to process. It must have constant format, integer sample type,
      and bit depth between 8 and 16.

   *threshold*
      Defaults to the maximum value allowed by the format, divided by 2,
      and rounded up.

   *v0*
      Value given to pixels that are below *threshold*.

   *v1*
      Value given to pixels that are greater than or equal to *threshold*.
      Defaults to the maximum value allowed by the format.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
