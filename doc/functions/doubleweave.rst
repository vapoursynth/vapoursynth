DoubleWeave
===========

.. function:: DoubleWeave(clip clip, bint tff)
   :module: std

   Weaves the fields back together from a clip with interleaved fields.

   Since VapourSynth has no notion of field order internally, *tff* must be set.
   Setting *tff* to true means top fields first and false means bottom fields
   first.
