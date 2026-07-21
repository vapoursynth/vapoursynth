BlankAudio
==========

.. function:: BlankAudio([anode clip, int[] channels=[FRONT_LEFT, FRONT_RIGHT], int bits=16, int sampletype=INTEGER, int samplerate=44100, int length=(10*samplerate), bint keep=0, string waveform="none", float amplitude=1.0, float frequency=440.0])
   :module: std

   Generates a new empty clip. This can be useful to have when editing audio
   or for testing. The default is a 10 second long 44.1kHz 16 bit stereo clip.
   Instead of specifying every property individually, BlankAudio can also copy
   the properties from *clip*. If both an argument such as *sampletype*, and *clip*
   are set, then *sampletype* will take precedence.

   The *channels* argument is a list of channel constants. Specifying the same channel twice
   is not allowed.

   The possible *sampletype* values are currently INTEGER (0) and FLOAT (1).

   If *keep* is set, a reference to the same frame is returned on every request.
   Otherwise a new frame is generated every time. There should usually be no
   reason to change this setting. It has no effect when a *waveform* is
   generated since every frame is then different.

   *waveform*:

      The signal to generate. Defaults to *none*, which fills the clip with
      silence the way it always has. The other options all produce a periodic
      signal at *frequency* with a peak level of *amplitude*, and the same
      signal is written to every channel:

      *none*
         Silence.

      *slope*
         A sawtooth ramping from -1 up to +1 across each period and jumping back.

      *sine*
         A sine wave starting at zero and rising.

      *square*
         A square wave, at its positive level for the first half of each period.

      *triangle*
         A triangle starting at zero, peaking at a quarter of the way through
         each period and bottoming out at three quarters.

      The phase of a sample depends only on its position in the clip, so a frame
      is identical no matter in which order frames are requested and the phase
      does not drift over long clips.

   *amplitude*:

      Peak level of the generated waveform, where 1.0 is full scale. Defaults
      to 1.0. Negative values invert the waveform.

      Since integer formats reach one step further down than up, a waveform that
      sits exactly at full scale for a while, such as a square wave at the
      default amplitude, is clamped to the last representable positive value.
      Use a slightly lower amplitude to avoid that.

      No dither is applied when generating an integer format. Generate a float
      clip and convert it with AudioResample if you want dithered output.

   *frequency*:

      Frequency of the generated waveform in Hz. Defaults to 440. A frequency of
      0 holds the waveform at its value for phase zero, and frequencies at or
      above the sample rate alias the way an actual sampled signal would.

   To generate a 1 kHz alignment tone at -6 dB::

      BlankAudio(waveform="sine", frequency=1000, amplitude=0.5)
