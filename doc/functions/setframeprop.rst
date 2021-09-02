SetFrameProp
============

.. function:: SetFrameProp(vnode clip, string prop[, bint delete=False, int[] intval, float[] floatval, string[] data])
   :module: std

   Adds a frame property to every frame in *clip*.

   If there is already a property with the name *prop* in the frames,
   it will be overwritten.

   The type of the property added depends on which of the *intval*,
   *floatval*, or *data* parameters is used.

   The *data* parameter can only be used to add NULL-terminated strings,
   not arbitrary binary data.

   For example, to set the field order to top field first::

      clip = c.std.SetFrameProp(clip, prop="_FieldBased", intval=2)

   For backward compatibility reasons, it is possible to delete the given property
   by setting *delete=True*, however a new API is provided as *RemoveFrameProps*
   for this. For users not requiring compatibility with R54 or older VapourSynth
   releases, it is also recommended to use the more intuitive *SetFrameProps*
   function to set frame properties.
