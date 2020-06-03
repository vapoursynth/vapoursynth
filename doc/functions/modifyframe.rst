ModifyFrame
===========

.. function:: ModifyFrame(clip clip, clip[] clips, func selector)
   :module: std

   The *selector* function is called for every single frame and can modify the
   properties of one of the frames gotten from *clips*. The additional *clips*'
   properties should only be read and not modified because only one modified
   frame can be returned.

   You must first copy the input frame to make it modifiable. Any frame may be
   returned as long as it has the same format as the *clip*.
   Failure to do so will produce an error. If for conditional reasons you do not
   need to modify the current frame's properties, you can simply pass it through.
   The selector function is passed *n*, the current frame number, and *f*, which
   is a frame or a list of frames if there is more than one clip specified.

   If you do not need to modify frame properties but only read them, you should
   probably be using *FrameEval* instead.

   How to set the property FrameNumber to the current frame number::

      def set_frame_number(n, f):
         fout = f.copy()
         fout.props['FrameNumber'] = n
         return fout
      ...
      ModifyFrame(clip=clip, clips=clip, selector=set_frame_number)

   How to remove a property::

      def remove_property(n, f):
         fout = f.copy()
         del fout.props['FrameNumber']
         return fout
      ...
      ModifyFrame(clip=clip, clips=clip, selector=remove_property)

   An example of how to copy certain properties from one clip to another
   (clip1 and clip2 have the same format)::

      def transfer_property(n, f):
         fout = f[1].copy()
         fout.props['FrameNumber'] = f[0].props['FrameNumber']
         fout.props['_Combed'] = f[0].props['_Combed']
         return fout
      ...
      ModifyFrame(clip=clip1, clips=[clip1, clip2], selector=transfer_property)
