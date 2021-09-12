BlankAudio
==========

.. function:: BlankAudio([anode clip, int channels=<stereo>, int bits=16, int sampletype=INTEGER, int samplerate=44100, int length=(10*samplerate), bint keep=0])
   :module: std

   Generates a new empty clip. This can be useful to have when editing audio
   or for testing. The default is a 10 second long 44.1kHz 16 bit stereo clip.
   Instead of specifying every property individually, BlankAudio can also copy
   the properties from *clip*. If both an argument such as *sampletype*, and *clip*
   are set, then *sampletype* will take precedence.
   
   The *channels* argument are a bitmask of shifted channel constants. For example
   the default stereo value is formed by doing (1 << acFrontLeft) | (1 << acFrontRight).
   
   The possible *sampletype* values are currently INTEGER (0) and FLOAT (1).

   If *keep* is set, a reference to the same frame is returned on every request.
   Otherwise a new frame is generated every time. There should usually be no
   reason to change this setting.
