PropToClip
==========

.. function:: PropToClip(clip clip[, string prop='_Alpha'])
   :module: std

   Extracts a clip from the frames attached to the frame property *prop* in
   *clip*.
   This function is mainly used to extract a mask/alpha clip that was stored in
   another one.

   It is the inverse of ClipToProp().
