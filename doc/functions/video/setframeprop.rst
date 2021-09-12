SetFrameProp
============

.. function:: SetFrameProp(vnode clip, string prop[, int[] intval, float[] floatval, string[] data])
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
