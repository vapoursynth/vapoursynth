PlaneDifference (deprecated)
============================

.. function:: PlaneDifference(clip[] clips, int plane[, string prop = 'PlaneDifference'])
   :module: std
   
   Do not use this function in new scripts. Instead use *PlaneAverage*.
   PlaneDifference([a, b]) is equivalent to
   abs(PlaneAverage(a) - PlaneAverage(b)) 

   *PlaneDifference* calculates the absolute normalized difference between two
   *clips* for the given *plane* and stores the result in the frame property
   *prop* in the returned clip (which is a copy of the first clip).
   The normalization means that the result is always a float between 0 and 1,
   where 0 means no differences and 1 means the biggest possible difference for
   every pixel.

   The two clips don't need to have the same length, but if the second one is
   shorter, PlaneDifference will end up requesting frames past the second
   clip's end, receiving a copy of the clip's last frame every time.
