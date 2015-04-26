Loop
====

.. function::   Loop(clip clip[, int times=0])
   :module: std

   Returns a clip with the frames repeated over and over again. If *times* is
   less than 1 the clip will be repeated until the maximum clip length is
   reached, otherwise it will be repeated *times* times.

   In Python, std.Loop can also be invoked :ref:`using the multiplication operator <pythonreference>`.
