AudioGain
=========

.. function::   AudioGain(anode clip, float[] gain, bint overflow_error = False)
   :module: std

   AudioGain can either change the volume of individual channels
   if a separate *gain* for each channel is given or if only a single
   *gain* value is supplied it's applied to all channels.
   
   Negative *gain* values are allowed. Applying a too large gain will
   lead to clipping in integer formats.
   
   Will stop processing with an error if clipping is detected if *overflow_error*
   is set. If it's false a warning will be printed for the first audio block with clipping.