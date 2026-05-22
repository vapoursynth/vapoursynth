PropToClip
==========

.. function:: PropToClip(vnode clip[, string prop='_Alpha', int index = 0])
   :module: std

   Extracts a clip from the frames attached to the frame property *prop* at the specified *index* in
   *clip*.
   This function is mainly used to extract a mask/alpha clip that was stored in
   another one.

   It is the inverse of ClipToProp().
