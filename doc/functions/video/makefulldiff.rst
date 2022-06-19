MakeFullDiff
============

.. function::   MakeFullDiff(vnode clipa, vnode clipb)
   :module: std

   Calculates the difference between *clipa* and *clipb* and outputs a clip with a one higher bitdepth to avoid the clamping or wraparound issues
   that would otherwise happen with filters like *MakeDiff* when forming a difference.
   This function is usually used together with *MergeFullDiff*, which can be used to add back the difference.

   Unsharp mask::

      blur_clip = core.std.Convolution(clip, matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
      diff_clip = core.std.MakeFullDiff(clip, blur_clip)
      sharpened_clip = core.std.MergeFullDiff(clip, diff_clip)
      
