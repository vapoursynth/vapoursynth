PreMultiply
===========

.. function::   PreMultiply(clip clip, clip alpha)
   :module: std

   PreMultiply simply multiplies *clip* and *alpha* in order to make it more suitable for
   later operations. This will yield much better results when resizing and a clip with an
   alpha channel and :doc:`MaskedMerge <maskedmerge>` can use it as input. The *alpha* clip
   must be the grayscale format equivalent of *clip*.
   
   Note that limited range pre-multiplied contents excludes the offset. For example with
   8 bit input 60 luma and 128 alpha would be calculated as ((60 - 16) * 128)/255 + 16
   and not (60 * 128)/255.