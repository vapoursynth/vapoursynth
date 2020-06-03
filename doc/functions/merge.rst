Merge
=====

.. function::   Merge(clip clipa, clip clipb[, float[] weight = 0.5])
   :module: std

   Merges *clipa* and *clipb* using the specified *weight* for each plane. The default
   is to use a 0.5 *weight* for all planes. A zero *weight* means that *clipa*
   is returned unchanged and 1 means that *clipb* is returned unchanged. If a
   single *weight* is specified, it will be used for all planes. If two weights
   are given then the second value will be used for the third plane as well.

   Values outside the 0-1 range are considered to be an error. Specifying more
   weights than planes in the clips is also an error. The clips must have the
   same dimensions and format.

   How to merge luma::

      Merge(clipa=A, clipb=B, weight=[0, 1])

   How to merge chroma::

      Merge(clipa=A, clipb=B, weight=[1, 0])

   The average of two clips::

      Merge(clipa=A, clipb=B)

   The frame properties are copied from *clipa*.
