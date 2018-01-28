BoxBlur
=======

.. function:: BoxBlur(clip clip[, int[] planes, int hradius = 1, int hpasses = 1, int vradius = 1, int vpasses = 1])
   :module: std

   Performs a box blur which is fast even for large radius values. Using multiple *passes* can be used to fairly cheaply
   approximate a gaussian blur. A *radius* of 0 means no processing is performed.
