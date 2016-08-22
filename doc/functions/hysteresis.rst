Hysteresis
======

.. function:: Hysteresis(clip clipa, clip clipb[, int[] planes=[0, 1, 2]])
   :module: std

   Grows *clipa* into *clipb* by connecting components. That allows to build
   more robust edge masks.

   *clipa*, *clipb*
      Clips to process. The clips must have the same dimensions and format.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
