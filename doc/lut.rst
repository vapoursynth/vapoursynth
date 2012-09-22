Lut
=======

.. function:: Lut(clip clip, int[] lut, int[] planes)
   :module: std
   
   Applies a lut to the given clip. The lut needs to contain 2^bits_per_sample entries and will be applied to the planes listed in *planes*. The other planes will simply be passed through unchanged.
   
   How to limit YUV range::
   
      luty = []
      for x in range(2**clip.format.bits_per_sample)
         luty.append(max(min(x, 235), 16))
      lutuv = []
      for x in range(2**clip.format.bits_per_sample)
         lutuv.append(max(min(x, 240), 16))
      ret = Lut(clip=clip, lut=luty, planes=0)
      limited_clip = Lut(clip=ret, lut=lutuv, planes=[1, 2])