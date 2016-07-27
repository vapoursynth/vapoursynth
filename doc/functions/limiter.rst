Limiter
=======

.. function:: Limiter(clip clip[, float[] min, float[] max, int[] planes=[0, 1, 2]])
   :module: std

   Limits the pixel values to the range [*min*, *max*].

   *clip*
      Clip to process.

   *min*
      Lower bound. Defaults to the lowest allowed value for the input. Can be specified for each plane individually.

   *max*
      Upper bound. Defaults to the highest allowed value for the input. Can be specified for each plane individually.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
