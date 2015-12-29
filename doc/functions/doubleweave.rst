DoubleWeave
===========

.. function:: DoubleWeave(clip clip, bint tff)
   :module: std

   Weaves the fields back together from a clip with interleaved fields.

   Since VapourSynth has no notion of field order internally, *tff* must be set.
   Setting *tff* to true means top fields first and false means bottom fields
   first.

   DoubleWeave's output has the same number of frames as the input. One must
   use DoubleWeave together with SelectEvery to undo the effect of
   SeparateFields::

      sep = core.std.SeparateFields(source, tff=True)
      ...
      woven = core.std.DoubleWeave(sep, tff=True)
      woven = core.std.SelectEvery(woven, 2, 0)

   The ``_Field`` frame property is deleted.
