ShuffleChannels
===============

.. function::   ShuffleChannels(anode[] clips, int[] channels_in, int channels_out)
   :module: std

   ShuffleChannels can extract and combine channels from different clips in the most
   general way possible.

   Most of the returned clip's properties are implicitly determined from the
   first clip given to *clips*.

   The *clips* parameter takes one or more clips with the same format. If the clips
   are different lengths they'll be zero extended to that of the longest.

   The argument *channels_in* controls which of the input clips' channels to use and
   takes a channel constants as its argument. Specifying a non-existent channel
   is an error. If more *channels_in* than *clips* values are specified then the last
   clip in the *clips* list is reused as a source.

   Output order is implicitly determined from the *channels_out* bitmask and the mapping
   between input index and output channel happens on the order of lowest output channel
   identifier to the highest.
   
   

   Below are some examples of useful operations.

   Extract the left channel (assuming it exists)::

      ShuffleChannels(clips=clip, channels_in=vs.FrontLeft, channels_out=(1 << vs.FrontLeft))

   Swap left and right audio channels in a stereo clip::

      ShuffleChannels(clips=clip, channels_in=[vs.FrontRight, vs.FrontLeft], channels_out=(1 << vs.FrontLeft | 1 << vs.FrontRight))

   Merge 2 mono audio clips into a single stereo clip::

      ShuffleChannels(clips=[clipa, clipb], channels_in=[vs.FrontLeft, vs.FrontLeft], channels_out=(1 << vs.FrontLeft | 1 << vs.FrontRight))
