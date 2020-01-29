Getting Started
===============

So you managed to install VapourSynth. Now what?

If you don't know the basics of Python, you may want to check out a
`tutorial <https://learnxinyminutes.com/docs/python3/>`_.

You can "play around" in the python interpreter if you want, but that's not how
most video scripts are created.

Example Script
##############

Here's a sample script to be inspired by, it assumes that ffms2 is installed and :doc:`auto-loaded <plugins>`::

   from vapoursynth import core
   video = core.ffms2.Source(source='Rule6.mkv')
   video = core.std.Transpose(video)
   video.set_output()
   
What it does is to get an instance of the core and load a video file using FFMS2. The video is then transposed
(think matrix transpose, or if you don't know that, a 90 degree rotation plus horizontal flip).

Remember that most VapourSynth objects have a quite nice string representation
in Python, so if you want to know more about an instance just call print().

It it also possible to directly open the script in VapourSynth Editor or VirtualDub FilterMod for previewing.

Output with VSPipe
##################

VSPipe is very useful to pipe the output to various applications, for example x264 and FFmpeg for encoding.
Here are two examples of command lines that automatically pass on most video attributes.

For x264::

   vspipe --y4m script.vpy - | x264 --demuxer y4m - --output encoded.mkv

For FFmpeg::

   vspipe --y4m script.vpy - | ffmpeg -i pipe: encoded.mkv
