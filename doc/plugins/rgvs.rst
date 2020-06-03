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
      Deprecated. Use the function Median instead.
   
      Same as mode 1, except the fourth-lowest and fourth-highest values are
      used.

   **Mode 5**
      Line-sensitive clipping giving the minimal change.
      
      Specifically, it clips the center pixel with four pairs 
      of opposing pixels respectively, and the pair that results 
      in the smallest change to the center pixel is used.

   **Mode 6**
      Line-sensitive clipping, intermediate.
      
      It considers the range of the clipping operation
      (the difference between the two opposing pixels)
      as well as the change applied to the center pixel.
      
      The change applied to the center pixel is prioritized 
      (ratio 2:1) in this mode.

   **Mode 7**
      Same as mode 6, except the ratio is 1:1 in this mode.

   **Mode 8**
      Same as mode 6, except the difference between the two opposing
      pixels is prioritized in this mode, again with a 2:1 ratio.
      
   **Mode 9**
      Line-sensitive clipping on a line where the neighbours pixels are the closest.
      
      Only the difference between the two opposing pixels is considered in this mode,
      and the pair with the smallest difference is used for cliping the center pixel.
      
      This can be useful to fix interrupted lines, as long as the length of the gap never exceeds one pixel.

   **Mode 10**
      Replaces the center pixel with the closest neighbour. "Very poor denoise sharpener"

   **Mode 11**
      Deprecated. Use Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1]) instead.
   
      Every pixel is replaced with a weighted arithmetic mean of its 3x3
      neighborhood.

      The center pixel has a weight of 4, the pixels above, below, to the
      left, and to the right of the center pixel each have a weight of 2,
      and the corner pixels each have a weight of 1.

   **Mode 12**
      Deprecated. Use Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1]) instead.
   
      In this implementation, mode 12 is identical to mode 11.

   **Mode 13**
      Bob mode, interpolates top field from the line where the neighbours pixels are the closest.

   **Mode 14**
      Bob mode, interpolates bottom field from the line where the neighbours pixels are the closest.

   **Mode 15**
      Bob mode, interpolates top field. Same as mode 13 but with a more complicated interpolation formula.

   **Mode 16**
      Bob mode, interpolates bottom field. Same as mode 14 but with a more complicated interpolation formula.

   **Mode 17**
      Clips the pixel with the minimum and maximum of respectively the maximum and minimum of each pair of opposite neighbour pixels.

   **Mode 18**
      Line-sensitive clipping using opposite neighbours whose greatest distance from the current pixel is minimal.

   **Mode 19**
      Deprecated. Use Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1]) instead.
   
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood,
      center pixel not included. In other words, the 8 neighbors are summed up
      and the sum is divided by 8.

   **Mode 20**
      Deprecated. Use Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1]) instead.
      
      Every pixel is replaced with the arithmetic mean of its 3x3 neighborhood.
      In other words, all 9 pixels are summed up and the sum is divided by 9.

   **Mode 21**
      The center pixel is clipped to the smallest and the biggest average of the four surrounding pairs. 

   **Mode 22**
      Same as mode 21 but simpler and faster. (rounding handled differently)

   **Mode 23**
      Small edge and halo removal, but reputed useless.

   **Mode 24**
      Same as mode 23 but considerably more conservative and slightly slower. Preferred.

   The top and bottom rows and the leftmost and rightmost columns are not
   processed. They are simply copied from the source.


.. function:: Repair(clip clip, clip repairclip, int[] mode)
   :module: rgvs

   Modes 0-24 are implemented. Different modes can be
   specified for each plane. If there are fewer modes than planes, the last
   mode specified will be used for the remaining planes.

   **Mode 0**
      The input plane is simply passed through.

   **Mode 1-4**
      Clips the source pixel with the Nth minimum and maximum found on the 3×3-pixel square from the reference clip.

   **Mode 5**
      Line-sensitive clipping giving the minimal change.

   **Mode 6-8**
      Line-sensitive clipping, intermediate.

   **Mode 9**
      Line-sensitive clipping on a line where the neighbor pixels are the closest.

   **Mode 10**
      Replaces the target pixel with the closest pixel from the 3×3-pixel reference square. 

   **Mode 11-14**
      Same as modes 1–4 but uses min(Nth_min, c) and max(Nth_max, c) for the clipping, 
      where c is the value of the center pixel of the reference clip.

   **Mode 15-16**
      Clips the source pixels using a clipping pair from the RemoveGrain modes 5 and 6.

   **Mode 17-18**
      Clips the source pixels using a clipping pair from the RemoveGrain modes 17 and 18.


.. function:: Clense(clip clip, clip previous, clip next, int[] planes)
   :module: rgvs

   Clense is a Temporal median of three frames. (previous, current and next)


.. function:: ForwardClense(clip clip, int[] planes)
   :module: rgvs

   Modified version of Clense that works on current and next frames. 


.. function:: BackwardClense(clip clip, int[] planes)
   :module: rgvs

   Modified version of Clense that works on current and previous frames.


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
