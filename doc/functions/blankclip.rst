BlankClip
=========

.. function:: BlankClip([clip clip, int width=640, int height=480, int format=vs.RGB24, int length=(10*fpsnum)/fpsden, int fpsnum=24, int fpsden=1, float[] color=<black>, bint keep=0])
   :module: std

   Generates a new empty clip. This can be useful to have when editing video or
   for testing. The default is a 640x480 RGB24 24fps 10 second long black clip.
   Instead of specifying every property individually, BlankClip can also copy
   the properties from *clip*. If both an argument such as *width*, and *clip*
   are set, then *width* will take precedence.

   If *keep* is set, a reference to the same frame is returned on every request.
   Otherwise a new frame is generated every time. There should usually be no
   reason to change this setting.

   It is never an error to use BlankClip.

