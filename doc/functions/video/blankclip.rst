BlankClip
=========

.. function:: BlankClip([vnode:all clip, int width=640, int height=480, int format=vs.RGB24, int length=(10*fpsnum)/fpsden, int fpsnum=24, int fpsden=1, float[] color=<black>, bint keep=0, bint varsize=0, bint varformat=0, bint gpu])
   :module: std

   Generates a new empty clip. This can be useful to have when editing video or
   for testing. The default is a 640x480 RGB24 24fps 10 second long black clip.
   Instead of specifying every property individually, BlankClip can also copy
   the properties from *clip*. If both an argument such as *width*, and *clip*
   are set, then *width* will take precedence.

   If *keep* is set, a reference to the same frame is returned on every request.
   Otherwise a new frame is generated every time. There should usually be no
   reason to change this setting.

   If *varsize* is set, a clip with variable size will be returned. The frames
   themselves will still have the size given by the width and height arguments.

   If *varformat* is set, a clip with variable format will be returned.
   The frames themselves will have the format given by the format argument.

   If *gpu* is set, the generated frames are GPU resident and filled on the
   device, so a blank clip can head a chain of GPU filters without a transfer.
   It defaults to the residency of *clip* when one is given and to CPU
   otherwise, which means a blank clip built from a GPU template is itself GPU
   resident unless *gpu=0* says otherwise. GPU frames need a constant format and
   constant dimensions, so combining *gpu* with *varsize* or *varformat* is an
   error. See :doc:`../../gpufilters`.

   It is never an error to use BlankClip, apart from the *gpu* combinations
   above.

