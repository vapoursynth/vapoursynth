RemoveFrameProps
================

.. function:: RemoveFrameProps(vnode clip[, string props[]])
   :module: std

   Returns *clip* but with all the frame properties named in
   *props* removed. If *props* is unset them all frame properties
   are removed.

   Note that *props* accepts wildcards (* and ?) which can be very
   useful if you for example only want to clear properties set by 
   a single filter since they're usually prefixed such as VFM\* or
   _\* can be used as a shorthand to conveniently clear only
   the internally reserved properties (colorimetry, time,
   field structure).
   