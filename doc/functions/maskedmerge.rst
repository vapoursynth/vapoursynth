MaskedMerge
===========

.. function::   MaskedMerge(clip clipa, clip clipb, clip mask[, int[] planes, bint first_plane=0, bint premultiplied=0])
   :module: std

   MaskedMerge merges *clipa* with *clipb* using the per pixel weights in the *mask*,
   where 0 means that *clipa* is returned unchanged.
   The *mask* clip is assumed to  be full range for all planes and in the 
   0-1 interval for float formats regardless of the colorspace.
   If *mask* is a grayscale clip or if *first_plane* is true, the mask's first
   plane will be used as the mask for merging all planes. The mask will be
   bilinearly resized if necessary.
   
   If *premultiplied* is set the blending is performed as if *clipb* has been pre-multiplied
   with alpha. In pre-multiplied mode it is an error to try to merge two frames with
   mismatched full and limited range since it will most likely cause horrible unintended
   color shifts. In the other mode it's just a very, very bad idea.

   By default all planes will be
   processed, but it is also possible to specify a list of the *planes* to merge
   in the output. The unprocessed planes will be copied from the first clip.

   *clipa* and *clipb* must have the same dimensions and format, and the *mask* must be the
   same format as the clips or the grayscale equivalent.

   How to apply a mask to the first plane::

      MaskedMerge(clipa=A, clipb=B, mask=Mask, planes=0)

   How to apply the first plane of a mask to the second and third plane::

      MaskedMerge(clipa=A, clipb=B, mask=Mask, planes=[1, 2], first_plane=True)

   The frame properties are copied from *clipa*.
