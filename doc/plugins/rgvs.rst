.. _rgvs:

RGVS
====


.. function:: RemoveGrain(clip clip, int[] mode)
   :module: rgvs

   RemoveGrain is a spatial denoising filter.

   Modes 0-24 are implemented. Different modes can be
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

   **Mode 5**
      TODO

   **Mode 6**
      TODO

   **Mode 7**
      TODO

   **Mode 8**
      TODO

   **Mode 9**
      TODO

   **Mode 10**
      TODO

   **Mode 11**
      Every pixel is replaced with a weighted arithmetic mean of its 3x3
      neighborhood.

      The center pixel has a weight of 4, the pixels above, below, to the
      left, and to the right of the center pixel each have a weight of 2,
      and the corner pixels each have a weight of 1.

   **Mode 12**
      In this implementation, mode 12 is identical to mode 11.

   **Mode 13**
      TODO

   **Mode 14**
      TODO

   **Mode 15**
      TODO

   **Mode 16**
      TODO

   **Mode 17**
      TODO

   **Mode 18**
      TODO

   **Mode 19**
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood,
      center pixel not included. In other words, the 8 neighbors are summed up
      and the sum is divided by 8.

   **Mode 20**
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood.
      In other words, all 9 pixels are summed up and the sum is divided by 9.

   **Mode 21**
      TODO

   **Mode 22**
      TODO

   **Mode 23**
      TODO

   **Mode 24**
      TODO

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


.. function:: VerticalCleaner(clip clip, int[] mode)
   :module: rgvs

   VerticalCleaner is a fast vertical median filter.

   Different modes can be specified for each plane. If there are fewer modes
   than planes, the last mode specified will be used for the remaining planes.

   **Mode 0**
      The input plane is simply passed through.

   **Mode 1**
      Vertical median.

   **Mode 2**
      Relaxed vertical median (preserves more detail).

   Let b1, b2, c, t1, t2 be a vertical sequence of pixels. The center pixel c is
   to be modified in terms of the 4 neighbours. For simplicity let us assume
   that b2 <= t1. Then in mode 1, c is clipped with respect to b2 and t1, i.e. c
   is replaced by max(b2, min(c, t1)). In mode 2 the clipping intervall is
   widened, i.e. mode 2 is more conservative than mode 1. If b2 > b1 and t1 > t2,
   then c is replaced by max(b2, min(c, max(t1,d1))), where d1 = min(b2 + (b2 -
   b1), t1 + (t1 - t2)). In other words, only if the gradient towards the center
   is positive on both clipping ends, then the upper clipping bound may be
   larger. If b2 < b1 and t1 < t2, then c is replaced by max(min(b2, d2), min(c,
   t1)), where d2 = max(b2 - (b1 - b2), t1 - (t2 - t1)). In other words, only if
   the gradient towards the center is negative on both clipping ends, then the
   lower clipping bound may be smaller.

   In mode 1 the top and the bottom line are always left unchanged. In mode 2
   the two first and the two last lines are always left unchanged.
