PlaneAverage
============

.. function:: PlaneAverage(clip clip, int plane[, string prop = 'PlaneAverage'])
   :module: std

   This function calculates the average normalized value of all the pixels in
   the specified *plane* and stores the value in the frame property named
   *prop*.
   The normalization means that the output will always be a float between 0 and
   1 no matter what the input format is.
