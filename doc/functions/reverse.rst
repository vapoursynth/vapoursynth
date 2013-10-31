Reverse
=======

.. function::   Reverse(clip clip)
   :module: std

   Returns a clip with the frame order reversed. For example a clip with 3
   frames would have the frame order 2, 1, 0.

   Infinite length clips obviously cannot be reversed since they have no end and
   will therefore cause an error.
