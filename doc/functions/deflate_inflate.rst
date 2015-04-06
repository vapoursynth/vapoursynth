Deflate/Inflate
===============

.. function:: Deflate(clip clip[, int[] planes=[0, 1, 2], int threshold=65535])
   :module: std

   Replaces each pixel with the average of the eight pixels in its 3x3
   neighbourhood, but only if that average is less than the center pixel.

   *clip*
      Clip to process. It must have integer sample type, and bit depth
      between 8 and 16. If there are any frames with float samples or
      bit depth greater than 16, an error will be returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *threshold*
      Allows to limit how much pixels are changed. Output pixels will not
      become less than ``input - threshold``. With the default value of
      65535 there is no limit.


.. function:: Inflate(clip clip[, int[] planes=[0, 1, 2], int threshold=65535])
   :module: std

   Replaces each pixel with the average of the eight pixels in its 3x3
   neighbourhood, but only if that average is greater than the center
   pixel.

   *clip*
      Clip to process. It must have integer sample type, and bit depth
      between 8 and 16. If there are any frames with float samples or
      bit depth greater than 16, an error will be returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *threshold*
      Allows to limit how much pixels are changed. Output pixels will not
      become greater than ``input + threshold``. With the default value
      of 65535 there is no limit.
