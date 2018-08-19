MergeDiff
=========

.. function::   MergeDiff(clip clipa, clip clipb[, int[] planes])
   :module: std

   Merges back the difference in *clipb* to *clipa* and clamps the result.
   By default all *planes* are processed. This function is usually used together with *MakeDiff*, which is normally used to calculate the difference.

   Unsharp masking of luma::

      blur_clip = core.std.Convolution(clip, matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=[0])
      diff_clip = core.std.MakeDiff(clip, blur_clip, planes=[0])
      sharpened_clip = core.std.MergeDiff(clip, diff_clip, planes=[0])
      
