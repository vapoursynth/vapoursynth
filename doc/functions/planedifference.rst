PlaneDifference
===============

.. function:: PlaneDifference(clip[] clips, int plane[, string prop = 'PlaneDifference'])
   :module: std

   *PlaneDifference* calculates the absolute normalized difference between two
   *clips* for the given *plane* and stores the result in the frame property
   *prop* in the returned clip (which is a copy of the first clip).
   The normalization means that the result is always a float between 0 and 1,
   where 0 means no differences and 1 means the biggest possible difference for
   every pixel.
