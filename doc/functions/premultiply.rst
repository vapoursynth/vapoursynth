PreMultiply
===========

.. function::   PreMultiply(clip clip, clip alpha)
   :module: std

   PreMultiply simply multiplies *clip* and *alpha* in order to make it more suitable for
   later operations. This will yield much better results when resizing and a clip with an
   alpha channel and :doc:`MaskedMerge <maskedmerge>` can use it as input. The *alpha* clip
   must be the same format as *clip* except grayscale only.