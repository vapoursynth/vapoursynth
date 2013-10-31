SeparateFields
==============

.. function:: SeparateFields(clip clip, bint tff)
   :module: std

   Returns a clip with the fields separated and interleaved.

   Since VapourSynth has no notion of field order internally, *tff* must be set.
   Setting *tff* to true means top field first and false means bottom field
   first.
