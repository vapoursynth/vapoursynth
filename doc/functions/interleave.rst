Interleave
==========

.. function::   Interleave(clip[] clips[, bint extend=0, bint mismatch=0])
   :module: std

   Returns a clip with the frames from all *clips* interleaved. For example,
   Interleave(clips=[A, B]) will return A.Frame 0, B.Frame 0, A.Frame 1,
   B.Frame...
   The *extend* argument controls whether or not all input clips will be treated
   as if they have the same length as the longest clip.

   Interleaving clips with different formats or dimensions, or mixing infinite
   length clips with normal ones is considered an error unless *mismatch* is true.
