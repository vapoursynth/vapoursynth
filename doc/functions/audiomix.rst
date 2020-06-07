AudioMix
========

.. function::   AudioMix(anode[] clips, float[] matrix, int channels_out)
   :module: std

   AudioMix can mix and combine channels from different clips in the most
   general way possible.

   Most of the returned clip's properties are implicitly determined from the
   first clip given to *clips*.

   The *clips* parameter takes one or more clips with the same format. If the clips
   are different lengths they'll be zero extended to that of the longest.

   The argument *matrix* applies the coefficients to each channel blah blah blah.

   Output order is implicitly determined from the *channels_out* bitmask and the mapping
   between input index and output channel happens on the order of lowest output channel
   identifier to the highest.
   
   

   Below are some examples of useful operations.

   Downmix stereo audio to mono::

      AudioMix(clips=clip, matrix=[0.5, 0.5], channels_out=(1 << vs.FrontLeft))

   Downmix 5.1 audio::

      AudioMix(clips=clip, matrix=[1, 0, 0.7071, 0, 0.7071, 0, 0, 1, 0.7071, 0, 0, 0.7071], channels_out=(1 << vs.FrontLeft | 1 << vs.FrontRight))
      
   Copy stereo audio to 5.1 and zero the other channels::

      AudioMix(clips=c, matrix=[1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], channels_out=(1 << vs.FrontLeft) | (1 << vs.FrontRight) | (1 << vs.FrontCenter) | (1 << vs.LowFrequency) | (1 << vs.BackLeft) | (1 << vs.BackRight))
