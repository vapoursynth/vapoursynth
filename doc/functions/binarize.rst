Binarize/BinarizeMask
=====================

.. function:: Binarize(clip clip[, float[] threshold, float[] v0, float[] v1, int[] planes=[0, 1, 2]])
.. function:: BinarizeMask(clip clip[, float[] threshold, float[] v0, float[] v1, int[] planes=[0, 1, 2]])
   :module: std

   Turns every pixel in the image into either *v0*, if it's below
   *threshold*, or *v1*, otherwise. The *BinarizeMask* version is intended
   for use on mask clips where all planes have the same value range and
   only differs in the default values of *v0* and *v1*.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *threshold*
      Defaults to the middle point of range allowed by the format.
      Can be specified for each plane individually.

   *v0*
      Value given to pixels that are below *threshold*. Can be specified
      for each plane individually. Defaults to the lower bound of the format.

   *v1*
      Value given to pixels that are greater than or equal to *threshold*.
      Defaults to the maximum value allowed by the format. Can be specified
      for each plane individually. Defaults to the upper bound of the format.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
