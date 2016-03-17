DoubleWeave
===========

.. function:: DoubleWeave(clip clip[, bint tff])
   :module: std

   Weaves the fields back together from a clip with interleaved fields.

   Since VapourSynth only has a weak notion of field order internally, *tff*
   may have to be set. Setting *tff* to true means top fields first and false
   means bottom fields first. Note that the ``_Field`` frame property, if present
   and in a valid combination, takes precedence over *tff*.

   DoubleWeave's output has the same number of frames as the input. One must
   use DoubleWeave together with SelectEvery to undo the effect of
   SeparateFields::

      sep = core.std.SeparateFields(source)
      ...
      woven = core.std.DoubleWeave(sep)
      woven = core.std.SelectEvery(woven, 2, 0)

   The ``_Field`` frame property is deleted and ``_FieldBased`` is set accordingly.
