Convolution
===========

.. function:: Convolution(clip clip, float[] matrix[, float bias=0.0, float divisor=0.0, int[] planes=[0, 1, 2], bint saturate=True, string mode="s"])
   :module: std

   Performs a spatial convolution.

   Here is how a 3x3 convolution is done. Each pixel in the 3x3
   neighbourhood is multiplied by the corresponding coefficient in
   *matrix*. The results of the nine multiplications are added together,
   then this sum is divided by *divisor*. Next, *bias* is added, and the
   result is rounded to the nearest larger integer. If this integer
   result is negative and the *saturate* parameter is False, it is
   multiplied by -1. Finally, the result is clamped to the format's range
   of valid values.

   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.

   *matrix*
      Coefficients for the convolution.
      
      When *mode* is "s", this must be an array of 9 or 25 numbers, for
      a 3x3 or 5x5 convolution, respectively.

      When *mode* is "h" or "v", this must be an array of 3 to 25 numbers,
      with an odd number of elements.

      The values of the coefficients must be between -1023 and 1023
      (inclusive). The coefficients are rounded to integers when
      the input is an integer format.

      This is how the elements of *matrix* correspond to the pixels in
      a 3x3 neighbourhood::

         1 2 3
         4 5 6
         7 8 9

      It's the same principle for the other types of convolutions. The
      middle element of *matrix* always corresponds to the center pixel.

   *bias*
      Value to add to the final result of the convolution (before clamping
      the result to the format's range of valid values).

   *divisor*
      Divide the output of the convolution by this value (before adding
      *bias*).

      If this parameter is 0.0 (the default), the output of the convolution
      will be divided by the sum of the elements of *matrix*, or by 1.0,
      if the sum is 0.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.

   *saturate*
      The final result is clamped to the format's range of valid values
      (0 .. (2**bitdepth)-1). Therefore, if this parameter is True,
      negative values become 0. If this parameter is False, it's instead
      the absolute value that is clamped and returned.

   *mode*
      Selects the type of convolution. Possible values are "s", for square,
      "h" for horizontal, and "v" for vertical.

   How to apply a simple blur equivalent to Avisynth's Blur(1):
   
   .. code-block:: python
   
      Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
   
   How to apply a stronger blur equivalent to Avisynth's Blur(1.58):

   .. code-block:: python
      
      Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])

