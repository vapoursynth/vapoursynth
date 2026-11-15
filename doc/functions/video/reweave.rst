Reweave
=======

.. function:: Reweave(vnode:all clipa, vnode:all clipb[, bint tff])
   :module: std

   Weaves a field of each of two clips into one frame. Frame *n* of the
   output takes its first field from frame *n* of *clipa* and its second
   field from frame *n* of *clipb*. Setting *tff* to true means the first
   field is the top one, the even rows of every plane, and false means it is
   the bottom one, the odd rows.

   Both clips hold full frames, so nothing has to be separated first: what
   takes SeparateFields, SelectEvery and DoubleWeave as separate steps is one
   copy per output frame here.

   Unlike in SeparateFields and DoubleWeave, *tff* takes precedence over the
   frame properties, since weaving across frames often produces the field
   order opposite to the one the inputs are tagged with. Only when *tff* is
   not given does each frame's order come from the ``_FieldBased`` property
   of *clipa*'s frame, and a frame without an order is an error.

   The output has the length, frame rate and frame properties of *clipa*,
   with ``_Field`` deleted and ``_FieldBased`` set to the order used. If
   *clipb* is shorter its last frame is repeated. Both clips must have the
   same constant format and dimensions, and the height must be mod 2 in the
   smallest subsampled plane.

   Reinterlacing a double rate clip, such as the output of a bobber, takes
   the first fields from its even frames and the second ones from its odd
   frames::

      interlaced = core.std.Reweave(clip[::2], clip[1::2], tff=True)

   And this gives the same frames as SeparateFields followed by DoubleWeave,
   both with *tff* set to true, except the last one, where DoubleWeave weaves
   the last field with itself::

      doubled = core.std.Interleave([core.std.SetFieldBased(clip, 2), core.std.Reweave(clip, clip[1:], tff=False)])
