Lut2
=======

.. function:: Lut2(clip[] clips, int[] lut, int[] planes)
   :module: std
   
   Applies a lut that takes the pixel values of two clips into account. The lut needs to contain 2^(clip1.bits_per_sample + clip2.bits_per_sample) entries and will be applied to the planes listed in *planes*. The other planes will simply be passed through unchanged.
   
   How to average 2 clips::
   
      lut = []
      for y in range(2**clipx.format.bits_per_sample)
		for x in range(2**clipy.format.bits_per_sample)
         luty.append((x + y)/2)
      Lut(clip=[clipx, clipy], lut=lut, planes=[0, 1, 2])