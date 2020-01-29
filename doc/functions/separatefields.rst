SeparateFields
==============

.. function:: SeparateFields(clip clip[, bint tff, bint modify_duration=True])
   :module: std

   Returns a clip with the fields separated and interleaved.

   The *tff* argument only has an effect when the field order isn't set for a frame.
   Setting *tff* to true means top field first and false means bottom field
   first.

   If *modify_duration* is set then the output clip's frame rate is double that of the input clip.
   The frame durations will also be halved.

   The ``_FieldBased`` frame property is deleted. The ``_Field`` frame
   property is added.
   
   If no field order is specified in ``_FieldBased`` or *tff* an error
   will be returned.
