AudioGain
=========

.. function::   AudioGain(anode clip, float[] gain)
   :module: std

   AudioGain can either change the volume of individual channels
   if a separate *gain* for each channel is given or if only a single
   *gain* value is supplied it's applied to all channels.
   
   Negative *gain* values are allowed. Applying a too large gain will
   lead to clipping in integer formats.