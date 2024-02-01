CopyFrameProps
==============

.. function:: CopyFrameProps(vnode clip, vnode prop_src[, string[] props])
   :module: std

   Returns *clip* but with all the frame properties replaced with the
   ones from the clip in *prop_src*. Note that if *clip* is longer
   than *prop_src* then the last source frame's properties will be
   used instead.
   
   If *props* is set only the specified properties will be copied. If
   the *prop_src* doesn't have the property it is deleted. In this mode
   all other properties in *clip* remain unchanged.
