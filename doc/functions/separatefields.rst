SeparateFields
==============

.. function:: SeparateFields(clip clip, bint tff)
   :module: std

   Returns a clip with the fields separated and interleaved.

   The *tff* argument only has an effect when the field order isn't set for a frame.
   Setting *tff* to true means top field first and false means bottom field
   first.

   The output clip's frame rate is double the input clip's frame rate.
   The frame durations are halved.

   The ``_FieldBased`` frame property is deleted. The ``_Field`` frame
   property is added.
