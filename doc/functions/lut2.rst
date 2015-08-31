Lut2
====

.. function:: Lut2(clip clipa, clip clipb[, int[] planes, int[] lut, float[] lutf, func function, int bits, bint floatout])
   :module: std

   Applies a look-up table that takes into account the pixel values of two clips. The
   *lut* needs to contain 2^(clip1.bits_per_sample + clip2.bits_per_sample)
   entries and will be applied to the planes listed in *planes*. Alternatively
   a *function* taking *x* and *y* as arguments can be used to make the lut.
   The other planes will be passed through unchanged. By default all *planes*
   are processed.

   Lut2 also takes an optional bit depth parameter, *bits*, which defaults to
   the bit depth of the first input clip, and specifies the bit depth of the
   output clip. The user is responsible for understanding the effects of bit
   depth conversion, specifically from higher bit depths to lower bit depths,
   as no scaling or clamping is applied.
   
   If *floatout* is set then the output will be floating point instead, and either
   *lutf* needs to be set or *function* always needs to return floating point
   values.

   How to average 2 clips:

   .. code-block:: python

      lut = []
      for y in range(2 ** clipy.format.bits_per_sample):
         for x in range(2 ** clipx.format.bits_per_sample):
            lut.append((x + y)//2)
      Lut2(clipa=clipa, clipb=clipb, lut=lut)

   How to average 2 clips with a 10-bit output:

   .. code-block:: python

      def f(x, y):
         return (x*4 + y)//2
      Lut2(clipa=clipa8bit, clipb=clipb10bit, function=f, bits=10)
