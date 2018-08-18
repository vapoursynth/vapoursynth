Levels
======

.. function:: Levels(clip clip[, float min_in, float max_in, float gamma=1.0, float min_out, float max_out, int[] planes=[0, 1, 2]])
   :module: std

   Adjusts brightness, contrast, and gamma.

   The range [*min_in*, *max_in*] is remapped into [*min_out*, *max_out*]. Note that the
   range behavior is unintuitive for YUV float formats since the assumed range will be
   0-1 even for the UV-planes.

   For example, to convert from limited range YUV to full range (8 bit)::

      clip = std.Levels(clip, min_in=16, max_in=235, min_out=0, max_out=255, planes=0)
      clip = std.Levels(clip, min_in=16, max_in=240, min_out=0, max_out=255, planes=[1,2])

   The default value of *max_in* and *max_out* is the format's minimum and maximum
   allowed values repsectively. Note that all input is clamped to the input range
   to prevent out of range output.
   
   .. warning::
      The default ranges are 0-1 for floating point formats. This may have an undesired
	  effect on YUV formats.
	  
   *clip*
      Clip to process. It must have integer sample type and bit depth
      between 8 and 16, or float sample type and bit depth of 32. If
      there are any frames with other formats, an error will be
      returned.
      
   *gamma*
      Controls the degree of non-linearity of the conversion. Values
      greater than 1.0 brighten the output, while values less than 1.0
      darken it.

   *planes*
      Specifies which planes will be processed. Any unprocessed planes
      will be simply copied.
