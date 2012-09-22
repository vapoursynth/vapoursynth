Splice
=======

.. function::   Splice(clip[] clips[, bint mismatch=0])
   :module: std
   
   Returns a clip with all *clips* appended in the given order.
   
   Splicing clips with different formats, dimensions or inifite length with normal ones is considered an error unless *mismatch* is true. An infinite length clip can also only appear as the last one to append.