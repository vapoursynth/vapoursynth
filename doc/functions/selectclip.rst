SelectClip
==========

.. function:: SelectClip(clip[] clips, func selector[, clip[] src])
   :module: std
   
   Selects which of the *clips* to return a frame from based on the *selector* function. The *selector* function has access to the frame number and the frames in the *src* clips.
   The selector function is passed a dict containing the frame number ('N') and the frames retrieved from *src* ('F0', 'F1'...).
   
   How to return alternating frames from the clips (A.Frame0, B.Frame1, A.Frame2, B.Frame3...)::
   
      def interleave2(props):
         # get the frame number
         n = props['N']
         n = n % 2
         return {val:n}
      ...
      SelectClip(clips=[A, B], selector=interleave2)

   How to select based on a frame property::
   
      def special_processing(props):
         f = props['F0']
         # get the frame's property dict
         fprop = f.get_props()
         if fprop['IsCombed']:
            return {val:1}
         else:
            return {val:0}
      ...
      SelectClip(clips=[A, B], src=A, selector=special_processing)