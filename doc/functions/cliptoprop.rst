ClipToProp
==========

.. function:: ClipToProp(clip clip, clip mclip[, string prop='_Alpha'])
   :module: std

   Stores each frame of *mclip* as a frame property named *prop* in *clip*. This
   is primarily intended to attach mask/alpha clips to another clip so that
   editing operations will apply to both. Unlike most other filters the output
   length is derived from the second argument named *mclip*.

   If the attached *mclip* does not represent the alpha channel, you should set
   *prop* to something else.

   It is the inverse of PropToClip().
