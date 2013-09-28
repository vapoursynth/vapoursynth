FrameEval
=========

.. function:: FrameEval(clip clip, func eval)
   :module: std
   
   Allows an arbitrary function to be evaluated every frame. The the function gets the frame number, *n*, as input and should return a clip the output frame can be requested from.
   
   The *clip* argument is only used to get the output format from.
   
   This function can be used to accomplish the same things as Animate, ScriptClip and all the other conditional filters in Avisynth. Note that for certain frame based conditions it is more efficient to use *SelectClip*.
   
   How to animate a BlankClip to fade from white to black::
   
      import vapoursynth as vs
	  import functools
	  
      core = vs.get_core()
      base_clip = core.std.BlankClip(format=vs.YUV420P8, length=1000, color=[255, 128, 128])

      def animator(n, clip):
	     if n > 255:
            return clip
	     else:
            return core.std.BlankClip(format=vs.YUV420P8, length=1000, color=[n, 128, 128])

      animated_clip = core.std.FrameEval(base_clip, functools.partial(animator, clip=base_clip))
      animated_clip.set_output()
	  
   Note that global variables are accessible from the evaluated function as well but to avoid confusion and accidentally creating a circular frame request using functools.partial() is encouraged.
   