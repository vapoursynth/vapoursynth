SelectEvery
===========

.. function:: SelectEvery(clip clip, int cycle, int[] offsets[, bint modify_duration=True])
   :module: std

   Returns a clip with only some of the frames in every *cycle* selected. The
   *offsets* given must be between 0 and *cycle* - 1.

   Below are some examples of useful operations.

   Return even numbered frames, starting with 0::

      SelectEvery(clip=clip, cycle=2, offsets=0)

   Return odd numbered frames, starting with 1::

      SelectEvery(clip=clip, cycle=2, offsets=1)

   Fixed pattern 1 in 5 decimation, first frame in every cycle removed::

      SelectEvery(clip=clip, cycle=5, offsets=[1, 2, 3, 4])

   Duplicate every fourth frame::

      SelectEvery(clip=clip, cycle=4, offsets=[0, 1, 2, 3, 3])

   In Python, std.SelectEvery can also be invoked by :ref:`slicing a clip <pythonreference>`.

   If *modify_duration* is set the clip's frame rate is multiplied by the number
   of offsets and divided by *cycle*. The frame durations are adjusted in the same manner.
