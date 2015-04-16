Splice
======

.. function::   Splice(clip[] clips[, bint mismatch=0])
   :module: std

   Returns a clip with all *clips* appended in the given order.

   Splicing clips with different formats or dimensions is
   considered an error unless *mismatch* is true.
