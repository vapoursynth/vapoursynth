ShuffleChannels
===============

.. function::   ShuffleChannels(anode[] clips, int[] channels_in, int[] channels_out)
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
   clip in the *clips* list is reused as a source. In addition to the channel constant
   it's also possible to specify the nth channel by using negative numbers.

   The output channel mapping is determined by *channels_out* and corresponds to the
   input channel order. The number of *channels_out* entries must be the same as the
   number of *channels_in* entries. Specifying the same output channel twice is an error.
   
   

   Below are some examples of useful operations.

   Extract the left channel (assuming it exists)::

      ShuffleChannels(clips=clip, channels_in=vs.FRONT_LEFT, channels_out=vs.FRONT_LEFT)

   Swap left and right audio channels in a stereo clip::

      ShuffleChannels(clips=clip, channels_in=[vs.FRONT_RIGHT, vs.FRONT_LEFT], channels_out=[vs.FRONT_LEFT, vs.FRONT_RIGHT])
      
   Swap left and right audio channels in a stereo clip (alternate ordering of arguments)::

      ShuffleChannels(clips=clip, channels_in=[vs.FRONT_LEFT, vs.FRONT_RIGHT], channels_out=[vs.FRONT_RIGHT, vs.FRONT_LEFT])
      
   Swap left and right audio channels in a stereo clip (alternate indexing)::

      ShuffleChannels(clips=clip, channels_in=[-2, -1], channels_out=[vs.FRONT_LEFT, vs.FRONT_RIGHT])

   Merge 2 mono audio clips into a single stereo clip::

      ShuffleChannels(clips=[clipa, clipb], channels_in=[vs.FRONT_LEFT, vs.FRONT_LEFT], channels_out=[vs.FRONT_LEFT, vs.FRONT_RIGHT])
