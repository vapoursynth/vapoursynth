MaskedMerge
===========

.. function::   MaskedMerge(clip[] clips, clip mask[, int[] planes, bint first_plane=0])
   :module: std
   
   MaskedMerge merges two *clips* using the per pixel weights in the *mask*, where 0 means that the original clip is returned unchanged.
   If *mask* is a grayscale clip or if *first_plane* is true the mask's first plane will be used as the mask for merging all planes, the mask will be bilinearly resized if necessary.
   
   By default all planes will be
   processed but it is also possible to specify a list of the *planes* to merge in the output.
   
   The clips must have the same dimensions and format and the *mask* must be the same format as the *clips* or the grayscale equivalent.
   
   How to apply a mask to the first plane::
   
      MaskedMerge(clips=[A, B], mask=Mask, plane=0)
   
   How to apply the first plane of a mask to the second and third plane::
   
      MaskedMerge(clips=[A, B], mask=Mask, plane=[1, 2], first_plane=1)
      