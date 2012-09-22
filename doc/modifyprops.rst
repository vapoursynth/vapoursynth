ModifyProps
===========

.. function:: ModifyProps(clip clip, func selector)
   :module: std
   
   The *selector* function is called for ever single frame and can modify their properties in any thinkable way.
   The function is called with the current frame's properties as the only argument and the return value are the new properties that will be set.
   
   How to select based on a frame property::
   
      def clear_one_property(props):
         del props['IsCombed']
         return props
      ...
      ModifyProps(clip=clip, selector=special_processing)
   
   How to remove all properties (usualyl a bad idea)::
   
      def remove_all_props(props):
         return {}
      ...
      ModifyProps(clip=clip, selector=remove_all_props)

