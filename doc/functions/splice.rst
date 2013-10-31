Splice
======

.. function::   Splice(clip[] clips[, bint mismatch=0])
   :module: std

   Returns a clip with all *clips* appended in the given order.

   Splicing clips with different formats or dimensions, or infinite length with
   normal ones, is considered an error unless *mismatch* is true. Also, an
   infinite length clip can only appear as the last one to append.
