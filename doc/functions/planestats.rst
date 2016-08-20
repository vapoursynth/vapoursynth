PlaneStats
==========

.. function:: PlaneStats(clip clipa[, clip clipb, int plane=0, string prop='PlaneStats'])
   :module: std

   This function calculates the min, max and average normalized value of all
   the pixels in the specified *plane* and stores the values in the frame properties
   named *prop*\ Min, *prop*\ Max and *prop*\ Average.
   
   If *clipb* is supplied, the absolute normalized difference between the two clips
   will be stored in *prop*\ Diff as well.
   
   The normalization means that the average and the diff will always be floats
   between 0 and 1, no matter what the input format is.
