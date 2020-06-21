Reverse/AudioReverse
====================

.. function::   Reverse(vnode clip)
                AudioReverse(anode clip)
   :module: std

   Returns a clip with the frame or sample order reversed. For example, a clip with 3
   frames would have the frame order 2, 1, 0.

   In Python, std.Reverse and std.AudioReverse can also be invoked by :ref:`slicing a clip <pythonreference>`.
