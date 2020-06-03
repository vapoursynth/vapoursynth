Prewitt/Sobel
===================

.. function:: Prewitt(clip clip[, int[] planes=[0, 1, 2], float scale=1])
   :module: std

   Creates an edge mask using the Prewitt operator.

.. function:: Sobel(clip clip[, int[] planes=[0, 1, 2], float scale=1])
   :module: std

   Creates an edge mask using the Sobel operator.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *scale*
      Multiply all pixels by scale before outputting. This can be used to
      increase or decrease the intensity of edges in the output.
