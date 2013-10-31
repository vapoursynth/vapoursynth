Loop
====

.. function::   Loop(clip clip[, int times=0])
   :module: std

   Returns a clip with the frames repeated over and over again. If *times* is
   less than 1 the clip will be repeated an infinite number of times, otherwise
   it will be repeated *times* times.

   Infinite length clips obviously cannot be looped since they have no end and
   will therefore cause an error.
