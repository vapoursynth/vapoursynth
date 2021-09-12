AudioMix
========

.. function::   AudioMix(anode[] clips, float[] matrix, int[] channels_out)
   :module: std

   AudioMix can mix and combine channels from different clips in the most
   general way possible.

   Most of the returned clip's properties are implicitly determined from the
   first clip given to *clips*.

   The *clips* parameter takes one or more clips with the same format. If the clips
   are different lengths they'll be zero extended to that of the longest.

   The argument *matrix* applies the coefficients to each channel of each input clip where
   the channels are in the numerical order of their channel constants. For example a stereo clip
   will have its channels presented in the order FRONT_LEFT and then FRONT_RIGHT.

   Output channels and order is determined by the *channels_out* array
   between input index and output channel happens on the order of lowest output channel
   identifier to the highest.
   
   

   Below are some examples of useful operations.

   Downmix stereo audio to mono::

      AudioMix(clips=clip, matrix=[0.5, 0.5], channels_out=[vs.FRONT_CENTER])

   Downmix 5.1 audio::

      AudioMix(clips=clip, matrix=[1, 0, 0.7071, 0, 0.7071, 0, 0, 1, 0.7071, 0, 0, 0.7071], channels_out=[vs.FRONT_LEFT, vs.FRONT_RIGHT])
      
   Copy stereo audio to 5.1 and zero the other channels::

      AudioMix(clips=c, matrix=[1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], channels_out=[vs.FRONT_LEFT, vs.FRONT_RIGHT, vs.FRONT_CENTER, vs.LOW_FREQUENCY, vs.BACK_LEFT, vs.BACK_RIGHT])
