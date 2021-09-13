AudioLoop
=========

.. function::   AudioLoop(anode clip[, int times=0])
   :module: std

   Returns a clip with the frames or samples repeated over and over again. If *times* is
   less than 1 the clip will be repeated until the maximum clip length is
   reached, otherwise it will be repeated *times* times.

   In Python, std.AudioLoop can also be invoked :ref:`using the multiplication operator <pythonreference>`.
