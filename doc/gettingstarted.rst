Getting Started
===============

So you managed to install VapourSynth. Now what?

If you don't know the basics of Python, you may want to check out a
`tutorial <https://learnxinyminutes.com/docs/python3/>`_.

You can "play around" in the python interpreter if you want, but that's not how
most scripts are created.

Example Script
##############

It all starts with a *.vpy* script.
Here's a sample script to be inspired by, it assumes that `FFMS2 <https://github.com/FFMS/ffms2>`_
is installed and :doc:`auto-loaded <installation>`.

.. code-block:: python

   from vapoursynth import core                     # Get an instance of the core
   clip = core.ffms2.Source(source='filename.mkv')  # Load a video track in mkv file
   clip = core.std.FlipHorizontal(clip)             # Flip the video clip in the horizontal direction
   clip.set_output()                                # Set the video clip to be accessible for output

Audio is also supported, use `BestAudioSource <https://github.com/vapoursynth/bestaudiosource>`_ to load your audio file.

.. code-block:: python

   from vapoursynth import core                     # Get an instance of the core
   clip = core.bas.Source(source='filename.mkv')    # Load an audio track in mkv file
   clip = core.std.AudioGain(clip,gain=2.0)         # Gain all channels 2x
   clip.set_output()                                # Set the audio clip to be accessible for output

You can combine 2 operations in one script.

.. code-block:: python

   from vapoursynth import core
   video = core.ffms2.Source(source='filename.mkv')
   audio = core.bas.Source(source='filename.mkv')
   video = core.std.FlipHorizontal(video)
   audio = core.std.AudioGain(audio,gain=2.0)
   video.set_output(index=0)
   audio.set_output(index=1)

Remember that most VapourSynth objects have a quite nice string representation
in Python, so if you want to know more about an instance just call ``print()``.

Preview
#######

It's possible to directly open the script in `VapourSynth Editor <https://github.com/YomikoR/VapourSynth-Editor>`_
or `VirtualDub FilterMod <https://sourceforge.net/projects/vdfiltermod/>`_ for previewing.

Output with VSPipe
##################

VSPipe is very useful to pipe the output to various applications, for example x264 and flac for encoding.

Here are some examples of command lines that automatically pass on most video and audio attributes.

For x264::

   vspipe -c y4m script.vpy - | x264 --demuxer y4m - --output encoded.264

For flac::

   vspipe -c wav script.vpy - | flac - -o encoded.flac

For FFmpeg::

   vspipe -c y4m script.vpy - | ffmpeg -i - encoded.mkv

For mpv::

   vspipe -c y4m script.vpy - | mpv -
   vspipe -c wav script.vpy - | mpv -
