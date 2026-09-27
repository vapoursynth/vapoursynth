Splice
======

.. function::   Splice(vnode[]:all clips[, bint mismatch=0])
   :module: std

   Returns a clip with all *clips* appended in the given order.

   Splicing clips with different formats or dimensions is
   considered an error unless *mismatch* is true, or the first clip
   already varies in that property. Clips with different frame rates
   can always be spliced, the result then has a variable frame rate.

   In Python, std.Splice can also be invoked :ref:`using the addition operator <pythonreference>`.
