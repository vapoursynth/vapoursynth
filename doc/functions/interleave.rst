Interleave
==========

.. function::   Interleave(clip[] clips[, bint mismatch=0])
   :module: std
   
   Returns a clip with the frames from all *clips* interleaved. For example, Interleave(clips=[A, B]) will return A.Frame 0, B.Frame 0, A.Frame 1, B.Frame...
   
   Interleaving clips with different formats or dimensions, or mixing infinite length clips with normal ones is considered an error unless *mismatch* is true.
