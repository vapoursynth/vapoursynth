Splice/AudioSplice
==================

.. function::   Splice(vnode[] clips[, bint mismatch=0])
                AudioSplice(anode[] clips)
   :module: std

   Returns a clip with all *clips* appended in the given order.

   Splicing clips with different formats or dimensions is
   considered an error unless *mismatch* is true.

   In Python, std.Splice and std.AudioSplice can also be invoked :ref:`using the addition operator <pythonreference>`.
