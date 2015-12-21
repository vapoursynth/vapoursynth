PlaneAverage (deprecated)
=========================

.. function:: PlaneAverage(clip clip, int plane[, string prop = 'PlaneAverage'])
   :module: std

   This function is deprecated and will be removed in a future version. Use
   *PlaneStats* instead, which does the same thing (and more).
   
   This function calculates the average normalized value of all the pixels in
   the specified *plane* and stores the value in the frame property named
   *prop*.
   The normalization means that the output will always be a float between 0 and
   1, no matter what the input format is.
