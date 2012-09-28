ModifyProps
===========

.. function:: ModifyProps(clips clip[], func selector)
   :module: std
   
   The *selector* function is called for every single frame and can modify the properties of the first given clip. The additional clips' properties
   may only be read and not modified. The function is called with a dict similar to the one in SelectClip and the clip's properties will be set to the ones in the returned dict.
   The selector function is passed a dict containing the frame number ('N') and the frames retrieved are in ('F'), which will be a list if there is more than one clip given.
   
   How to remove a property::
   
      def clear_one_property(props):
         frame_props = props['F'].get_props()
         del frame_props['IsCombed']
         return frame_props
      ...
      ModifyProps(clips=clip, selector=clear_one_property)
   
   How to remove all properties (usually a veeeeeery bad idea)::
   
      def remove_all_props(props):
         return {}
      ...
      ModifyProps(clips=clip, selector=remove_all_props)

