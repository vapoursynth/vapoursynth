Merge
=====

.. function::   Merge(clip[] clips[, float[] weight])
   :module: std
   
   Merges two *clips* using the specified *weight* for each plane, the default is to use a 0.5 *weight* for all planes. A zero *weight* means that the first clip is returned and 1 means the second clip is returned unchanged. If a single *weight* is specified it will be used for all planes, if two weights are given then the second value will be used for the third plane as well.
   
   Values outside the 0-1 range are considered to be an error. Specifying more weights than planes in the clips is also an error. The clips must have the same dimensions and format.
   
   How to merge luma::
   
      Merge(clips=[A, B], weight=[0, 1])
   
   How to merge chroma::
   
      Merge(clips=[A, B], weight=[1, 0])
      
   The average of two clips::
   
      Merge(clips=[A, B])