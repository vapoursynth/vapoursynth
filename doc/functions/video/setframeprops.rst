SetFrameProps
=============

.. function:: SetFrameProps(vnode clip, ...)
   :module: std

   Adds the specified values as a frame property of every frame
   in *clip*. If a frame property with the same key already exists
   it will be replaced.

   For example, to set the field order to top field first::

      clip = c.std.SetFrameProps(clip, _FieldBased=2)
