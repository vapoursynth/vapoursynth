.. _rgvs:

RGVS
====


.. function:: RemoveGrain(clip clip, int[] mode)
   :module: rgvs

   RemoveGrain is a spatial denoising filter.

   Modes 0-4, 11, 12, 19, and 20 are implemented. Different modes can be
   specified for each plane. If there are fewer modes than planes, the last
   mode specified will be used for the remaining planes.

   **Mode 0**
      The input plane is simply passed through.

   **Mode 1**
      Every pixel is clamped to the lowest and highest values in the pixel's
      3x3 neighborhood, center pixel not included.

   **Mode 2**
      Same as mode 1, except the second-lowest and second-highest values are
      used.

   **Mode 3**
      Same as mode 1, except the third-lowest and third-highest values are
      used.

   **Mode 4**
      Same as mode 1, except the fourth-lowest and fourth-highest values are
      used.

   **Mode 11**
      Every pixel is replaced with a weighted arithmetic mean of its 3x3
      neighborhood.

      The center pixel has a weight of 4, the pixels above, below, to the
      left, and to the right of the center pixel each have a weight of 2,
      and the corner pixels each have a weight of 1.

   **Mode 12**
      In this implementation, mode 12 is identical to mode 11.

   **Mode 19**
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood,
      center pixel not included. In other words, the 8 neighbors are summed up
      and the sum is divided by 8.

   **Mode 20**
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood.
      In other words, all 9 pixels are summed up and the sum is divided by 9.

   The top and bottom rows and the leftmost and rightmost columns are not
   processed. They are simply copied from the source.


.. function:: Repair(clip clip, clip repairclip, int[] mode)
   :module: rgvs

   TODO


.. function:: Clense(clip clip, clip previous, clip next, int[] planes)
   :module: rgvs

   TODO


.. function:: ForwardClense(clip clip, int[] planes)
   :module: rgvs

   TODO


.. function:: BackwardClense(clip clip, int[] planes)
   :module: rgvs

   TODO
