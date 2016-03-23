SetFieldBased (Experimental)
============================

.. function:: SetFieldBased(clip clip, int value)
   :module: std
   
   This function is experimental and may be removed or greatly changed
   in the next version.


   This is a convenience function. See *SetFrameProps* if you want to
   set other properties.
   
   SetFieldBased sets ``_FieldBased`` to *value* and deletes
   the ``_Field`` frame property. The possible values are:
   
      0 = Frame Based
      
      1 = Bottom Field First
      
      2 = Top Field First
   
   For example, if you have source material that's progressive but has
   been encoded as interlaced you can set it to be treated as frame based
   (not interlaced) to improve resizing quality::

      clip = core.ffms2.Source("rule6.mkv")
      clip = core.std.SetFieldBased(clip, 0)
      clip = clip.resize.Bilinear(clip, width=320, height=240)
