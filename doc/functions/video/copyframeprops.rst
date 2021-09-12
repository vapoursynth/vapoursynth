CopyFrameProps
==============

.. function:: CopyFrameProps(vnode clip, vnode prop_src)
   :module: std

   Returns *clip* but with all the frame properties replaced with the
   ones from the clip in *prop_src*. Note that if *clip* is longer
   than *prop_src* then the last existing frame's properties will be
   used instead.
