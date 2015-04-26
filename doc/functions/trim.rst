Trim
====

.. function::   Trim(clip clip[, int first=0, int last, int length])
   :module: std

   Trim returns a clip with only the frames between the arguments *first* and
   *last*, or a clip of *length* frames, starting at *first*.
   Trim is inclusive so Trim(clip, first=3, last=3) will return one frame. If
   neither *last* nor *length* is specified, no frames are removed from the end
   of the clip.

   Specifying both *last* and *length* is considered to be an error.
   Likewise is calling Trim in a way that returns no frames, as 0 frame clips are
   not allowed in VapourSynth.

   In Python, std.Trim can also be invoked by :ref:`slicing a clip <pythonreference>`.
