Prewitt/Sobel
===================

.. function:: Prewitt(clip clip[, float min=0.0, float max, int[] planes=[0, 1, 2], float scale=1])
   :module: std

   Creates an edge mask using the Prewitt operator.

.. function:: Sobel(clip clip[, float min=0.0, float max, int[] planes=[0, 1, 2], int rshift=0])
   :module: std

   Creates an edge mask using the Sobel operator.

   *clip*
      Clip to process. It must have integer sample type, and bit depth
      between 8 and 16. If there are any frames with float samples or
      bit depth greater than 16, an error will be returned.

   *min*
      If an output pixel has a value less than or equal to this, it will
      be set to 0.

   *max*
      If an output pixel has a value greater than or equal to this, it
      will be set to the maximum value allowed by the frame's bit depth
      or 1 if it's float.
      
      By default max will be 1 for float and the maximum value for integer.
      
      Output pixels are compared with *max* first, then with *min*.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *scale*
      Multiply all pixels by scale before outputting. This can be used to
      increase or decrease the intensity of edges in the output. The test
      against *min* and *max* is applied after this operation.
