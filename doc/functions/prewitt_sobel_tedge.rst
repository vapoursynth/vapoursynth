Prewitt/Sobel/TEdge
===================

.. function:: Prewitt(clip clip[, int min=0, int max=65535, int[] planes=[0, 1, 2], int rshift=0])
   :module: std

   Creates an edge mask using the Prewitt operator.

.. function:: Sobel(clip clip[, int min=0, int max=65535, int[] planes=[0, 1, 2], int rshift=0])
   :module: std

   Creates an edge mask using the Sobel operator.

.. function:: TEdge(clip clip[, int min=0, int max=65535, int[] planes=[0, 1, 2], int rshift=0])
   :module: std

   Creates an edge mask using approximately the same algorithm as the
   Avisynth plugin TEdgeMask with mode=2.

   *clip*
      Clip to process. It must have integer sample type, and bit depth
      between 8 and 16. If there are any frames with float samples or
      bit depth greater than 16, an error will be returned.

   *min*
      If an output pixel has a value less than or equal to this, it will
      be set to 0.

   *max*
      If an output pixel has a value greater than or equal to this, it
      will be set to the maximum value allowed by the frame's bit depth.

      Output pixels are compared with *max* first, then with *min*.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *rshift*
      Before comparing with *min* and *max*, output pixels can be shifted
      to the right by *rshift* bits. In other words, they can be divided
      by ``2 ** rshift``.
