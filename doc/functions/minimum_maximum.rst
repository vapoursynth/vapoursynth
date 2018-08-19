Minimum/Maximum
===============

.. function:: Minimum(clip clip[, int[] planes=[0, 1, 2], float threshold, bint[] coordinates=[1, 1, 1, 1, 1, 1, 1, 1]])
   :module: std

   Replaces each pixel with the smallest value in its 3x3 neighbourhood.
   This operation is also known as erosion.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *threshold*
      Allows to limit how much pixels are changed. Output pixels will not
      become less than ``input - threshold``. The default is no limit.

   *coordinates*
      Specifies which pixels from the 3x3 neighbourhood are considered.
      If an element of this array is 0, the corresponding pixel is not
      considered when finding the minimum value. This must contain exactly
      8 numbers.

      Here is how each number corresponds to a pixel in the 3x3
      neighbourhood::

         1 2 3
         4   5
         6 7 8


.. function:: Maximum(clip clip[, int[] planes=[0, 1, 2], float threshold, bint[] coordinates=[1, 1, 1, 1, 1, 1, 1, 1]])
   :module: std

   Replaces each pixel with the largest value in its 3x3 neighbourhood.
   This operation is also known as dilation.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *threshold*
      Allows to limit how much pixels are changed. Output pixels will not
      become less than ``input - threshold``. The default is no limit.

   *coordinates*
      Specifies which pixels from the 3x3 neighbourhood are considered.
      If an element of this array is 0, the corresponding pixel is not
      considered when finding the maximum value. This must contain exactly
      8 numbers.

      Here is how each number corresponds to a pixel in the 3x3
      neighbourhood::

         1 2 3
         4   5
         6 7 8
