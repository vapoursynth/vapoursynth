DeleteFrames
============

.. function:: DeleteFrames(clip clip, int[] frames)
   :module: std

   Deletes the specified frames.

   All frame numbers apply to the input clip.
   
   Returns an error if the same frame is deleted twice or if all frames in a clip are deleted.
