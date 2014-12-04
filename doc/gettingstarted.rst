Getting Started
===============

So you managed to install VapourSynth. Now what?

If you don't know the basics of Python, you may want to check out the
`tutorial <http://docs.python.org/py3k/tutorial/index.html>`_.

You can "play around" in the python interpreter if you want, but that's not how
most video scripts are created.

Example Script
##############

Here's a sample script to be inspired by::

   import vapoursynth as vs
   # get the core instance
   core = vs.get_core()
   # load a native vapoursynth plugin
   # you should use absolute paths as the working directory may not be what you think it is
   core.std.LoadPlugin(path=r'c:\plugins\ffms2.dll')
   # load an avisynth plugin
   # the loaded functions will always end up in the avs namespace
   core.avs.LoadPlugin(path=r'c:\avisynth\UnDot.dll')
   # open a video file; ret is now a clip object
   ret = core.ffms2.Source(source='Super Size Me.avi')
   # apply the undot filter to the video
   ret = core.avs.UnDot(ret)
   # set the clip to be output
   ret.set_output()

Remember that most VapourSynth objects have a quite nice string representation
in Python, so if you want to know more about an instance just call print().

Output with VSPipe
##################

VSPipe is very useful to pipe the output to various applications, for example x264 and FFmpeg for encoding.
Here are two examples of command lines that automatically pass on most video attributes.

For x264::

   vspipe --y4m script.vpy - | x264 --demuxer y4m - --output encoded.mkv

For FFmpeg::

   vspipe --y4m script.vpy - | ffmpeg -i pipe: encoded.mkv

