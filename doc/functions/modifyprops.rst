ModifyProps
===========

.. function:: ModifyProps(clips clip[], func selector)
   :module: std
   
   The *selector* function is called for every single frame and can modify the properties of the first given clip. The additional clips' properties
   may only be read and not modified. You must first copy the input frame to make it modifiable. Returning any other frame is likely to return in a fatal error or
   unpredictable behavior. If for conditional reasons you do not need to modify the current frame's properties you can simply pass it through.
   The selector function is passed *n*, the current frame number, and *f*, which will be a list if there is more than one clip specified.
   
   How to set the property FrameNumber to the current frame number::
   
      def set_frame_number(n, f):
         fout = f.copy()
         fout.props.FrameNumber = n
         return fout
      ...
      ModifyProps(clips=clip, selector=set_frame_number)
   
   How to remove a property::
   
      def remove_property(n, f):
         fout = f.copy()
         del fout.props.FrameNumber
         return fout
      ...
      ModifyProps(clips=clip, selector=remove_property)

