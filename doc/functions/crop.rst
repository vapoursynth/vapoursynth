Crop/CropAbs
===============

.. function::   Crop(clip clip[, int left=0, int right=0, int top=0, int bottom=0])  
				CropAbs(clip clip, int width, int height[, int left=0, int top=0])
   :module: std

   Crops the frames in a clip.

   Crop is the simplest to use of the two. The arguments specify how many
   pixels to crop from each side. This function used to be called CropRel
   which is still an alias for it.

   CropAbs, on the other hand, is special, because it can accept clips with
   variable frame sizes and crop out a fixed size area, thus making it a fixed
   size clip.

   Both functions return an error if the whole picture is cropped away, if the
   cropped area extends beyond the input or if the subsampling restrictions
   aren't met.
