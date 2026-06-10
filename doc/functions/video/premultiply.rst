PreMultiply
===========

.. function::   PreMultiply(vnode clip, vnode alpha)
   :module: std

   PreMultiply simply multiplies *clip* and *alpha* in order to make it more suitable for
   later operations. This will yield much better results when resizing and a clip with an
   alpha channel and :doc:`MaskedMerge <maskedmerge>` can use it as input. The *alpha* clip
   must be the grayscale format equivalent of *clip*.
   
   Note that limited range pre-multiplied contents excludes the offset. For example with
   8 bit input 60 luma and 128 alpha would be calculated as ((60 - 16) * 128)/255 + 16
   and not (60 * 128)/255.

   When *clip* is a YUV clip with subsampled chroma, *alpha* is resized
   down to the chroma resolution with bilinear resampling before being
   multiplied into the chroma planes. This resampling is chroma-location
   aware: each frame's ``_ChromaLocation`` property (guessing 0, *left*, when
   absent) determines the sub-pixel shift needed to keep *alpha* aligned with
   the luma plane.
