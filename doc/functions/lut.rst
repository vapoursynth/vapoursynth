Lut
===

.. function:: Lut(clip clip[, int[] planes, int[] lut, float[] lutf, func function, int bits, bint floatout])
   :module: std

   Applies a look-up table to the given clip. The lut can be specified as either an array
   of 2^bits_per_sample values or given as a *function* having an argument named
   *x* to be evaluated. Either *lut*, *lutf* or *function* must be used. The lut will be
   applied to the planes listed in *planes* and the other planes will simply be
   passed through unchanged. By default all *planes* are processed.
   
   If *floatout* is set then the output will be floating point instead, and either
   *lutf* needs to be set or *function* always needs to return floating point
   values.

   How to limit YUV range (by passing an array):

   .. code-block:: python

      luty = []
      for x in range(2**clip.format.bits_per_sample):
         luty.append(max(min(x, 235), 16))
      lutuv = []
      for x in range(2**clip.format.bits_per_sample):
         lutuv.append(max(min(x, 240), 16))
      ret = Lut(clip=clip, planes=0, lut=luty)
      limited_clip = Lut(clip=ret, planes=[1, 2], lut=lutuv)

   How to limit YUV range (using a function):

   .. code-block:: python

      def limity(x):
         return max(min(x, 235), 16)
      def limituv(x):
         return max(min(x, 240), 16)
      ret = Lut(clip=clip, planes=0, function=limity)
      limited_clip = Lut(clip=ret, planes=[1, 2], function=limituv)
