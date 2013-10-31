PEMVerifier
===========

.. function:: PEMVerifier(clip clip[, int[] upper, int[] lower])
   :module: std

   The *PEMVerifier* is used to check for out of bounds values during filter
   development. It is a public function so badly coded filters won't go
   unnoticed.
   If no values are set then *upper* defaults to the max value allowed in the
   format and *lower* defaults to 0 for all planes. If an out of bounds value is
   encountered a frame error is set and the coordinates of the first bad pixel
   are included in the error message.
