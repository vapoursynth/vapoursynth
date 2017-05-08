.. _misc:

Miscellaneous Filters
=====================

Miscellaneous Filters is a random collection of filters that mostly are useful for Avisynth compatibility.

.. function:: AverageFrames(clip[] clips, float[] weights[, float scale, bint scenechange, int[] planes])
   :module: misc
   
   AverageFrames has two main modes depending on whether one or multiple *clips* are supplied.
   The filter is named AverageFrames since using ones for weights is an easy way to average
   many frames together but it can also be seen as a temporal or multiple frame convolution.
   
   If multiple *clips* are supplied then the frames from each of the *clips* are multiplied by
   the respective *weights*, summed together and divided by *scale* before being output. Note
   that only integer *weights* and *scale* are allowed for integer input formats.
   
   If a single *clip* is supplied then an odd number of *weights* are needed and they will instead
   be temporally centered on the current frame of the *clip*. The rest works as multiple *clip* mode
   with the only difference being that *scenechange* can be set to avoid averaging frames over scene
   changes. If this happens then all the weights beyond a scene change are instead applied to the frame
   right before it.
   
   At most 31 *weights* can be supplied.
    
.. function:: Hysteresis(clip clipa, clip clipb[, int[] planes])
   :module: misc
   
   Grows the mask in *clipa* into the mask in *clipb*. This is an equivalent of the Avisynth function *mt_hysteresis*.
    
.. function:: SCDetect(clip clip[, float threshold=0.1])
   :module: misc
   
   A simple filter to mark scene changes. It works by calculating the absolute difference between the next and previous
   frames and scaling it to a 0-1 range and then comparing it to *threshold*. It's basically just a wrapper for
   *PlaneStats*.
