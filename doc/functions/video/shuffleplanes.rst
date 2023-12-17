ShufflePlanes
=============

.. function::   ShufflePlanes(vnode[] clips, int[] planes, int colorfamily[, vnode[] prop_src=clips[0]])
   :module: std

   ShufflePlanes can extract and combine planes from different clips in the most
   general way possible.
   This is both good and bad, as there are almost no error checks.

   Most of the returned clip's properties are implicitly determined from the
   first clip given to *clips*.

   The *clips* parameter takes between one and three clips for color families
   with three planes. In this case clips=[A] is equivalent to clips=[A, A, A]
   and clips=[A, B] is equivalent to clips=[A, B, B]. For the GRAY color
   family, which has one plane, it takes exactly one clip.

   The argument *planes* controls which of the input clips' planes to use.
   Zero indexed. The first number refers to the first input clip, the second
   number to the second clip, the third number to the third clip.

   The only thing that needs to be specified is *colorfamily*, which controls which
   color family (YUV, RGB, GRAY) the output clip will be.
   Properties such as subsampling are determined from the relative size of the
   given planes to combine.

   The argument *prop_src* is used to specify the output props of the clip.
   By default it will grab the first node of *clips*. You can pass an empty array
   to remove the props entirely from the output clip.

   ShufflePlanes accepts clips with variable format and dimensions only when
   extracting a single plane.

   Below are some examples of useful operations.

   Extract plane with index X. X=0 will mean luma in a YUV clip and R in an RGB
   clip. Likewise 1 will return the U and G channels, respectively::

      ShufflePlanes(clips=clip, planes=X, colorfamily=vs.GRAY)

   Swap U and V in a YUV clip::

      ShufflePlanes(clips=clip, planes=[0, 2, 1], colorfamily=vs.YUV)

   Merge 3 grayscale clips into a YUV clip::

      ShufflePlanes(clips=[Yclip, Uclip, Vclip], planes=[0, 0, 0], colorfamily=vs.YUV)

   Cast a YUV clip to RGB::

      ShufflePlanes(clips=[YUVclip], planes=[0, 1, 2], colorfamily=vs.RGB)

   Note that in any example that mixes color families, if the clip has correct colorspace
   information specified in its frame props, when you try to pass it to functions that
   access and check frame props, it will error;
   For example any function in the *resize* plugin will throw an error saying that an RGB clip
   can't have GRAY/YUV colorspace information and vice-versa.

   To avoid this you can set prop_src to an empty array to remove said props::

      ShufflePlanes(clips=[YUVclip], planes=[0, 1, 2], colorfamily=vs.RGB, prop_src=[])