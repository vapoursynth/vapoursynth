MergeFullDiff
=============

.. function::   MergeFullDiff(vnode clipa, vnode clipb)
   :module: std

   Merges back the difference in *clipb* to *clipa*. Note that the bitdepth of *clipb* has to be one higher than that of *clip*.
   This function is usually used together with *MakeFullDiff*, which is normally used to calculate the difference.

   Unsharp mask::

      blur_clip = core.std.Convolution(clip, matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
      diff_clip = core.std.MakeFullDiff(clip, blur_clip)
      sharpened_clip = core.std.MergeFullDiff(clip, diff_clip)
      
