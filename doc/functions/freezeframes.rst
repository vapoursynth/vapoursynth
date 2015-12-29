FreezeFrames
============

.. function:: FreezeFrames(clip clip, int[] first, int[] last, int[] replacement)
   :module: std

   FreezeFrames replaces all the frames in the [*first*,\ *last*] range
   (inclusive) with *replacement*.

   A single call to FreezeFrames can freeze any number of ranges::

      core.std.FreezeFrames(input, first=[0, 100, 231], last=[15, 112, 300], replacement=[8, 50, 2])

   This replaces [0,15] with 8, [100,112] with 50, and [231,300] with 2 (the
   original frame number 2, not frame number 2 after it was replaced with
   number 8 by the first range).

   The frame ranges must not overlap.
