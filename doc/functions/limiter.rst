Limiter
=======

.. function:: Limiter(clip clip[, int min=0, int max=65535, int[] planes=[0, 1, 2]])
   :module: std

   Limits the pixel values to the range [*min*, *max*].

   *clip*
      Clip to process. It must have integer sample type, and bit depth
      between 8 and 16. If there are any frames with float samples or
      bit depth greater than 16, an error will be returned.

   *min*
      Lower bound.

   *max*
      Upper bound.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
