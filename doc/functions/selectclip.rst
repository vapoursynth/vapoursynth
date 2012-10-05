SelectClip
==========

.. function:: SelectClip(clip[] clips, clip[] src, func selector)
   :module: std
   
   Selects which of the *clips* to return a frame from based on the *selector* function. The *selector* function has access to the frame number and the frames in the *src* clips.
   The selector function is passed a dict containing the frame number ('N') and the frames retrieved from *src* are in ('F'), which will be a list if there is more than one source clip.
   
   How to return alternating frames from the clips (A.Frame0, B.Frame1, A.Frame2, B.Frame3...)::
   
      def interleave2(n, f):
         return n % 2
      ...
      # src is only given here because it is not optional
      SelectClip(clips=[A, B], src=A, selector=interleave2)

   How to select based on a frame property::
   
      def special_processing(n, f):
         if f.props.IsCombed:
            return 1
         else:
            return 0
      ...
      SelectClip(clips=[A, B], src=A, selector=special_processing)