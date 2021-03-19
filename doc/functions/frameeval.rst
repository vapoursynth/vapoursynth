FrameEval
=========

.. function:: FrameEval(clip clip, func eval[, clip[] prop_src])
   :module: std

   Allows an arbitrary function to be evaluated every frame. The function gets
   the frame number, *n*, as input and should return a clip the output frame can
   be requested from.

   The *clip* argument is only used to get the output format from since there is
   no reliable automatic way to deduce it.

   When using the argument *prop_src* the function will also have an argument,
   *f*, containing the current frames. This is mainly so frame properties can be
   accessed and used to make decisions. Note that *f* will only be a list if
   more than one *prop_src* clip is provided.

   This function can be used to accomplish the same things as Animate,
   ScriptClip and all the other conditional filters in Avisynth. Note that to
   modify per frame properties you should use *ModifyFrame*.

   How to animate a BlankClip to fade from white to black. This is the simplest
   use case without using the *prop_src* argument::

      import vapoursynth as vs
      import functools

      core = vs.core
      base_clip = core.std.BlankClip(format=vs.YUV420P8, length=1000, color=[255, 128, 128])

      def animator(n, clip):
         if n > 255:
            return clip
         else:
            return core.std.BlankClip(format=vs.YUV420P8, length=1000, color=[n, 128, 128])

      animated_clip = core.std.FrameEval(base_clip, functools.partial(animator, clip=base_clip))
      animated_clip.set_output()

   How to perform a simple per frame auto white balance. It shows how to access
   calculated frame properties and use them for conditional filtering::

      import vapoursynth as vs
      import functools
      import math

      def GrayWorld1Adjust(n, f, clip, core):
         small_number = 0.000000001
         red   = f[0].props['PlaneStatsAverage']
         green = f[1].props['PlaneStatsAverage']
         blue  = f[2].props['PlaneStatsAverage']
         max_rgb = max(red, green, blue)
         red_corr   = max_rgb/max(red, small_number)
         green_corr = max_rgb/max(green, small_number)
         blue_corr  = max_rgb/max(blue, small_number)
         norm = max(blue, math.sqrt(red_corr*red_corr + green_corr*green_corr + blue_corr*blue_corr) / math.sqrt(3), small_number)
         r_gain = red_corr/norm
         g_gain = green_corr/norm
         b_gain = blue_corr/norm
         return core.std.Expr(clip, expr=['x ' + repr(r_gain) + ' *', 'x ' + repr(g_gain) + ' *', 'x ' + repr(b_gain) + ' *'])

      def GrayWorld1(clip, matrix_s=None):
         core = vs.core
         rgb_clip = core.resize.Bilinear(clip, format=vs.RGB24)
         r_avg = core.std.PlaneStats(rgb_clip, plane=0)
         g_avg = core.std.PlaneStats(rgb_clip, plane=1)
         b_avg = core.std.PlaneStats(rgb_clip, plane=2)
         adjusted_clip = core.std.FrameEval(rgb_clip, functools.partial(GrayWorld1Adjust, clip=rgb_clip, core=core), prop_src=[r_avg, g_avg, b_avg])
         return core.resize.Bilinear(adjusted_clip, format=clip.format.id, matrix_s=matrix_s)

      core = vs.core
      core.std.LoadPlugin(path='ffms2.dll')
      main = core.ffms2.Source(source='...')
      main = GrayWorld1(main)
      main.set_output()
