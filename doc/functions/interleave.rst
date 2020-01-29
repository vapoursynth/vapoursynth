Interleave
==========

.. function::   Interleave(clip[] clips[, bint extend=0, bint mismatch=0, bint modify_duration=True])
   :module: std

   Returns a clip with the frames from all *clips* interleaved. For example,
   Interleave(clips=[A, B]) will return A.Frame 0, B.Frame 0, A.Frame 1,
   B.Frame...

   The *extend* argument controls whether or not all input clips will be treated
   as if they have the same length as the longest clip.

   Interleaving clips with different formats or dimensions is considered an
   error unless *mismatch* is true.

   If *modify_duration* is set then the output clip's frame rate is the first
   input clip's frame rate multiplied by the number of input clips. The frame durations are divided
   by the number of input clips. Otherwise the first input clip's frame rate is used.
