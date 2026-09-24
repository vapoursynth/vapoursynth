RemoveFrameProps
================

.. function:: RemoveFrameProps(vnode:all clip[, string[] props])
   :module: std

   Returns *clip* but with all the frame properties named in
   *props* removed. If *props* is unset then all frame properties
   are removed.

   Note that *props* accepts wildcards (* and ?) which can be very
   useful if you for example only want to clear properties set by
   a single filter, since they're usually prefixed, such as VFM\*.
   The pattern _\* is a shorthand to conveniently clear only
   the internally reserved properties (colorimetry, time,
   field structure).
