AudioResample
=============

.. function::   AudioResample(anode clip[, int samplerate, int sampletype, int bits, int[] channels, string dither_type="triangular", bint normalize, bint overflow_error=False])
   :module: std

   Converts audio between sample rates, channel layouts and sample formats.
   Every argument defaults to the corresponding property of the input clip, so
   only the parts that are explicitly specified are changed.

   Integer samples are stored right aligned in the range
   -2\ :sup:`bits-1`\  to 2\ :sup:`bits-1`\ -1 and float samples use the nominal
   -1.0 to 1.0 range, so 16 bit integer -32768 corresponds to float -1.0.
   Converting to a wider integer format or to float is exact and reversible.

   Note that only pure format conversions are exact to the last bit. Sample rate
   conversion and channel mixing work on 32 bit floating point values
   internally, so anything that changes the rate or the layout carries about 24
   significant bits through the conversion. That resolves more than any real
   signal chain but means 32 and 24 bit integer input doesn't survive those
   operations bit exactly even when the output format could represent it.

   *clip*:

      Input clip.

   *samplerate*:

      Output sample rate.

      The conversion uses a Kaiser windowed sinc filter with the passband
      reaching 91% of the lower of the two Nyquist frequencies, which for
      44.1 kHz output means everything up to about 20 kHz is preserved flat.
      Measured stopband attenuation is better than 110 dB and total harmonic
      distortion plus noise is below -115 dB.

      The output length is the input length scaled by the rate ratio and rounded
      up. Samples before the start and after the end of the clip are treated as
      silence, so the very first and last output samples fade in and out over the
      length of the filter rather than wrapping around.

      The ratio between the two rates is reduced to an exact fraction and one
      filter phase is generated per step of that fraction, so all the common
      rates convert cheaply. Rates whose ratio doesn't reduce, such as 48000 to
      96001, need an impractically large filter and are rejected with an error.

   *sampletype*:

      Output sample type, 0 for integer and 1 for float. When set to float and
      *bits* isn't specified the output is 32 bit float.

   *bits*:

      Output bits per sample. Integer output accepts 16 to 32 bits and float
      output only accepts 32 bits.

   *channels*:

      Output channel layout, specified as a list of channel constants the same
      way as in BlankAudio. The order the channels are listed in doesn't matter,
      a layout is a set.

      The mix matrix is derived from the two layouts rather than looked up per
      layout pair, so any combination is handled. Channels that exist in both
      layouts pass straight through untouched, and each input channel that has
      nowhere to go is folded into the channels that do exist:

      * A front centre with no destination is folded into front left and right
        at -3 dB each, and a front left/right pair with no destination is folded
        onto the centre the same way, which is how stereo collapses to mono.
      * Surrounds are folded into the fronts at -3 dB. Side and back channels
        name the same position when a layout carries only one of the two, so a
        layout using side surrounds converts to one using back surrounds without
        any loss.
      * A back centre folds into the back pair if there is one, the side pair
        otherwise, and the fronts as a last resort.
      * Elevated and auxiliary positions collapse onto the channel below or
        beside them and then follow that channel's own rule.
      * The LFE is discarded unless the output layout has an LFE of its own.

      The -3 dB fold levels are the ones ITU-R BS.775 specifies and that ATSC
      A/52 carries by default, so the standard 5.1 to stereo downmix comes out as
      ``L' = L + 0.707*C + 0.707*Ls``.

      Output channels that no input channel feeds contain digital silence and
      are never dithered. Upmixing spreads what is there but doesn't invent
      surround content, so converting stereo to 5.1 gives you the original two
      channels and four silent ones.

      Matrix encoded output such as Dolby Surround Lt/Rt isn't supported, since
      folding the surrounds in with a phase shift needs a filter rather than a
      matrix.

   *dither_type*:

      Dither applied when the destination has less precision than the source,
      which means when converting from float to integer or to a narrower integer
      format. Widening a format or converting to float is exact and never
      dithered regardless of this setting. Sample rate conversion and channel
      mixing always work in floating point internally, so any integer output is
      dithered when either of them is active. The following methods are
      available:

      *none*
         Plain rounding. Produces quantization distortion that is correlated with
         the signal.

      *rectangular*
         Uniformly distributed noise spanning one quantization step. Decorrelates
         the error from the signal but not the error power.

      *triangular*
         Triangularly distributed noise spanning two quantization steps, the
         default. Slightly noisier than rectangular but decorrelates both the
         error and its power from the signal, which is normally what you want.

      The dither noise is derived from the position of each sample rather than
      from a running random number generator, and the resampler likewise computes
      the filter phase for each output sample directly from its position instead
      of carrying state between samples. The output of a given frame is therefore
      always identical no matter in which order frames are requested, which keeps
      output reproducible when seeking. Note that dithering a signal that is
      already at full scale can push samples past the last representable value
      and cause clipping.

   *normalize*:

      Whether to scale the mix matrix down so that no output channel can clip.
      The worst case gain of an output channel is the sum of the weights feeding
      it, which for the standard 5.1 to stereo downmix is about 2.41, so
      normalizing costs roughly 7.6 dB of level in exchange for never clipping.
      The whole matrix is scaled by the same factor rather than each row on its
      own, which keeps the balance between the output channels intact.

      Defaults to on for integer output, where clipping is unrecoverable, and off
      for float output, which has the headroom to carry the plain BS.775
      coefficients. Only has an effect when the channel layout changes.

   *overflow_error*:

      Will stop processing with an error if clipping is detected if
      *overflow_error* is set. If it's false a warning will be printed for the
      first audio block with clipping.

   To convert to 44.1 kHz 16 bit integer, the usual cd delivery format::

      AudioResample(clip=clip, samplerate=44100, sampletype=0, bits=16)

   To downmix 5.1 to stereo::

      AudioResample(clip=clip, channels=[vs.FRONT_LEFT, vs.FRONT_RIGHT])

   To convert a float clip to 16 bit integer with the default dither::

      AudioResample(clip=clip, sampletype=0, bits=16)

   To convert to 32 bit float::

      AudioResample(clip=clip, sampletype=1)

   To reduce an integer clip to 16 bits without adding any dither noise::

      AudioResample(clip=clip, bits=16, dither_type="none")
