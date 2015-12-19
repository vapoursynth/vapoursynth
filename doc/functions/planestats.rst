PlaneStats
==========

.. function:: PlaneAverage(clip clip, int plane[, string prop = 'PlaneStats'])
   :module: std

   This function calculates the min, max and average normalized value of all
   the pixels in the specified *plane* and stores the values in the frame properties
   named *prop*MinMax and *prop*Average.
   
   The normalization means that the output of the average will always be a float
   between 0 and 1, no matter what the input format is.
