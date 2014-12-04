AssumeFPS
=========

.. function:: AssumeFPS(clip clip[, clip src, int fpsnum, int fpsden=1])
   :module: std

   Returns a clip with the framerate changed. This does not in any way modify
   the frames, only their metadata.

   The framerate to assign can either be read from another clip, *src*, or given
   as a rational number with *fpsnum* and *fpsden*.

   It is an error to specify both *src* and *fpsnum*.

   AssumeFPS overwrites the frame properties ``_DurationNum`` and
   ``_DurationDen`` with the frame duration computed from the new frame rate.
