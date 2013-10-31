CropAbs/CropRel
===============

.. function::   CropAbs(clip clip, int width, int height[, int x=0, int y=0])
                CropRel(clip clip[, int left=0, int right=0, int top=0, int bottom=0])
   :module: std

   Crops the frames in a clip.

   CropRel is the simplest to use of the two. The arguments specify how many
   pixels to crop from each side.

   CropAbs, on the other hand, is special because it can accept variable frame
   size clips and crop out a fixed size area, thus making it a fixed size clip.
   The arguments *x* and *y* can be linked to frame properties to extract a
   moving window from a clip.

   Both functions return an error if the whole picture is cropped away or if the
   subsampling restrictions aren't met.
